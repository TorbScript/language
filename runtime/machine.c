/*
 * machine.c - the kernel the bytecode VM runs on (docs/design/VM.md section 6).
 *
 * The VM is TorbScript compiled into `torb`. Its registers are one `ArrayList<Int64>` of 64-bit words, and a value is
 * the runtime's own bytes spread over as many words as it needs - a `torb_text` is two words, a list two, a pointer
 * one. What TorbScript cannot do on such words - read a `torb_text` out of two of them, call a function of the runtime
 * with them, allocate a counted block - is what this file does, behind six natives of `std/machine`:
 *
 *     torb_machine_operate      one operation of the kernel's table, or one function of the runtime through its thunk
 *     torb_machine_load         one word at an address - a register, a field of a block, a word of the code
 *     torb_machine_store        one word to an address (both `static inline` in torb.h: the loop's every register)
 *     torb_machine_place_text   an immortal copy of a text into two words: how the constant pool gets its strings
 *     torb_machine_place_float  the bits of a float into one word
 *     torb_machine_install      the closure of the interpreter the kernel calls back into (`torb_machine_call_back`)
 *
 * `operate` reads its operands out of a list of words (`code`), beginning at `at`: the operation's number, then what
 * the operation takes. A register operand is an offset from `base`, the frame's first word. The numbers of the
 * operations are the positions of `KernelOperation` in compiler/src/backend/bytecode/format.trb; a number from
 * `TORB_MACHINE_NATIVE_BASE` on is a function of the runtime, in the order of the manifest, through the table of
 * thunks that `torb natives --header` writes into machine_natives.c.
 *
 * **A reference word** is where a `var` parameter's storage is: an odd word is a register (`index * 2 + 1`, an
 * absolute index into the words), an even word the address of storage inside a counted block. `torb_machine_address`
 * turns one into a pointer, which is valid until the words grow - which no operation here does.
 *
 * Nothing here decides a question of the language. Every panic is the runtime's own panic function with the location
 * the IR gave, every count is `torb_retain`/`torb_release`, and every allocation is `torb_allocate` - so a program run by
 * the VM prints, panics, counts and leaks exactly as its native binary does.
 *
 * **While a sandbox is open** (sandbox.c, docs/design/SCRIPTS.md) every operation runs under a recovery point of its
 * own, so a panic of a receiver script - or a stop of the sandbox, which is a panic with a kind - ends the operation
 * with `TORB_MACHINE_STOPPED` instead of the process, and the interpreter unwinds the script from there.
 */

#include "torb.h"
#include "torb_natives.h"
#include "torb_machine.h"
#include "torb_pool.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------ registers and words --- */

int64_t *torb_machine_words(torb_list *words) {
  torb_list_make_unique(words);
  return (int64_t *)torb_list_storage_data(words->storage) + words->offset;
}

void *torb_machine_address(int64_t *words, int64_t reference) {
  if ((reference & 1) != 0) {
    return (void *)(words + (reference >> 1));
  }
  return (void *)(intptr_t)reference;
}

double torb_machine_double_of(int64_t word) {
  double value;
  memcpy(&value, &word, sizeof value);
  return value;
}

int64_t torb_machine_word_of_double(double value) {
  int64_t word;
  memcpy(&word, &value, sizeof word);
  return word;
}

void torb_machine_place_text(torb_list *words, int64_t at, torb_text text) {
  int64_t *registers = torb_machine_words(words);
  torb_text copy = torb_text_empty();
  if (text.length > 0) {
    uint8_t *data = NULL;
    copy = torb_text_allocate(text.length, &data);
    memcpy(data, text.storage->data + text.offset, text.length);
    torb_make_immortal(copy.storage);
  }
  memcpy(registers + at, &copy, sizeof copy);
}

void torb_machine_place_float(torb_list *words, int64_t at, double value) {
  int64_t *registers = torb_machine_words(words);
  registers[at] = torb_machine_word_of_double(value);
}

/* ------------------------------------------------------------------------------------------- the call back --- */

/*
 * The interpreters, as the closures `Machine.install` handed over: what the kernel calls where the runtime has to run
 * code of the program - a test body behind its recovery point, an `equals` a map asks for, a task the scheduler resumes.
 * One per thread that may run code of the program (docs/design/VM.md section 4): slot 0 is the thread that runs the
 * program, a worker of the pool's ring has the slot of its index, and thread `k` of the blocking pool the slot
 * `TORB_MACHINE_RING_SLOTS + k`. Each closure works on registers of its own, so two threads never share a word of the
 * interpreter. A thread without one falls back to slot 0's, which a program whose tasks never move only reaches while
 * the thread that runs the program waits. The kernel owns one count of every environment, and gives it back when
 * another closure replaces it.
 */
#define TORB_MACHINE_RING_SLOTS 1024u
#define TORB_MACHINE_BLOCKING_SLOTS 64u
#define TORB_MACHINE_SLOTS (TORB_MACHINE_RING_SLOTS + TORB_MACHINE_BLOCKING_SLOTS)

static torb_closure torb_machine_interpreters[TORB_MACHINE_SLOTS];
/* Where the next `Machine.install` goes (`InstallFor`); back to slot 0 after it. */
static size_t torb_machine_install_slot = 0u;
/* Whether the tasks of the program may move to another thread: every thread of the pool has an interpreter. */
static bool torb_machine_portable = false;

/* The slot of the calling thread; `TORB_MACHINE_SLOTS` for a thread that has none (a thread of the IO core). */
static size_t torb_machine_slot(void) {
  uint32_t index = torb_worker_current()->index;
  if (index < TORB_MACHINE_RING_SLOTS) {
    return (size_t)index;
  }
  if (index > TORB_BLOCKING_INBOX && index - TORB_BLOCKING_INBOX - 1u < TORB_MACHINE_BLOCKING_SLOTS) {
    return (size_t)TORB_MACHINE_RING_SLOTS + (size_t)(index - TORB_BLOCKING_INBOX - 1u);
  }
  return TORB_MACHINE_SLOTS;
}

/* How many operations of the loop the calling thread is inside (see `torb_machine_queue_closer`). */
static uint32_t *torb_machine_operating_here(void);

void torb_machine_install(torb_closure interpreter) {
  size_t slot = torb_machine_install_slot;
  torb_closure previous = torb_machine_interpreters[slot];
  torb_machine_interpreters[slot] = interpreter;
  torb_machine_install_slot = 0u;
  torb_environment_release(previous.environment);
}

/* Every interpreter but slot 0's given back, once no thread of the pool runs code of the program any more. */
static void torb_machine_uninstall_workers(void) {
  /* The environments are the interpreter's, and no block of the program */
  unsigned inside = torb_count_in_machine(0u);
  for (size_t slot = 1u; slot < TORB_MACHINE_SLOTS; slot++) {
    torb_closure previous = torb_machine_interpreters[slot];
    torb_machine_interpreters[slot].code = NULL;
    torb_machine_interpreters[slot].environment = NULL;
    torb_environment_release(previous.environment);
  }
  (void)torb_count_in_machine(inside);
}

/*
 * A sandboxed script that stopped inside a call back (an `equals` a map asked for, a task the scheduler resumed) stops
 * the operation that made the call as well: the interpreter says so (`StopInCallBack`) before it answers, and the call
 * back panics again with the stop the inner operation recorded, which the recovery point of the outer operation takes
 * as it is (`torb_machine_restopping`) - so the loop that made the outer call unwinds the script as for any stop.
 */
static bool torb_machine_stopped_in_call_back = false;
static bool torb_machine_restopping = false;
static char torb_machine_stop_message[1024];
static torb_location torb_machine_stop_at = { NULL, 0, 0 };

/*
 * A script's tasks belong to it (docs/design/VM.md section 10). Every opening of a sandbox is a generation of its own,
 * and a task remembers the one it was started in (0 outside every sandbox): it runs only while that sandbox is open,
 * so a script's code never runs outside its grant and budget - after the script ended, or in another script's test.
 * Where a script stops inside a task the scheduler resumed, the task stops there and the stop waits (`pending`) for the
 * operation that ran the scheduler to return, which then stops the script: nothing jumps through the scheduler, which
 * stays whole, and no other task of the script runs meanwhile.
 */
static int64_t torb_machine_sandbox_generations = 0;
static int64_t torb_machine_sandbox_open = 0;
static bool torb_machine_stop_pending = false;

/* The interpreter of the calling thread, run on a request; a stop inside it stays recorded for the caller. */
static int64_t torb_machine_call_interpreter(int64_t request) {
  unsigned inside;
  int64_t answer;
  size_t slot = torb_machine_slot();
  torb_closure interpreter = torb_machine_interpreters[0];
  if (slot < TORB_MACHINE_SLOTS && torb_machine_interpreters[slot].code != NULL) {
    interpreter = torb_machine_interpreters[slot];
  }
  if (interpreter.code == NULL) {
    torb_panic_text("internal error: the kernel has no interpreter to call back", torb_location_unknown);
  }
  /* What the interpreter allocates itself is `torb`'s, and what its calls of the kernel allocate the program's again */
  inside = torb_count_in_machine(0u);
  answer = ((int64_t (*)(torb_environment *, int64_t))interpreter.code)(interpreter.environment, request);
  (void)torb_count_in_machine(inside);
  return answer;
}

int64_t torb_machine_call_back(int64_t request) {
  int64_t answer = torb_machine_call_interpreter(request);
  if (torb_machine_stopped_in_call_back) {
    torb_machine_stopped_in_call_back = false;
    torb_machine_restopping = true;
    torb_panic_text(torb_machine_stop_message, torb_machine_stop_at);
  }
  return answer;
}

/*
 * What the kernel asks the interpreter for. The interpreter reads the fields with `Machine.load`, so their order is the
 * contract: `runRequest` and the `request` offsets of compiler/src/vm/interpret.trb.
 */
typedef struct torb_machine_request {
  /* TORB_REQUEST_* */
  int64_t kind;
  /* The chunk to run: a closure's function word (its index plus one), or the chunk of an `equals`/`hash`. */
  int64_t chunk;
  /* A closure's environment; the address of the first operand of an `equals`, of the operand of a `hash`. */
  int64_t first;
  /* The address of the second operand of an `equals`. */
  int64_t second;
  /* How many words an operand of an `equals` or a `hash` has. */
  int64_t words;
} torb_machine_request;

enum {
  /* Run a closure of the program without arguments, and answer nothing: a test body, a group body. */
  TORB_REQUEST_RUN = 0,
  /* Call the program's `equals` on the elements at two addresses, and answer whether they are equal. */
  TORB_REQUEST_EQUALS = 1,
  /* Call the program's `hash` on the element at an address, and answer the hash. */
  TORB_REQUEST_HASH = 2,
  /* Resume the body of a task at the state it recorded, and answer its `torb_poll`. */
  TORB_REQUEST_RESUME = 3,
  /* Run a chunk without parameters and answer nothing: the release of the entry cells at a `Process.exit`. */
  TORB_REQUEST_CALL = 4,
  /* Run the destructors this thread queued, now: a release outside every operation of the loop (below). */
  TORB_REQUEST_CLOSE = 5
};

/* The chunk that releases the entry cells, which `torb_process_exit` runs through the interpreter; -1 for none. */
static int64_t torb_machine_exit_chunk = -1;

static void torb_machine_release_on_exit(void) {
  torb_machine_request request = { TORB_REQUEST_CALL, torb_machine_exit_chunk, 0, 0, 0 };
  unsigned inside = torb_count_in_machine(1u);
  (void)torb_machine_call_back((int64_t)(intptr_t)&request);
  (void)torb_count_in_machine(inside);
}

/*
 * The one resume function of every task of the VM. A task block's frame is the chunk of its body in its first word, the
 * generation of the sandbox it was started in in its second, then the words of the body's frame as its last suspension
 * left them - frames are data - and the interpreter goes on at the state the body recorded, with the task in the body's
 * task register.
 */
static torb_poll torb_machine_resume(torb_task *task) {
  int64_t *frame = (int64_t *)torb_task_frame(task);
  torb_machine_request request;
  int64_t answer;
  /*
   * A script's task outside its sandbox, or after its script stopped, runs nothing more: it stops where it is, and what
   * its frame holds is not given back, as nothing is when a script stops.
   */
  if (frame[1] != 0 && (frame[1] != torb_machine_sandbox_open || torb_machine_stop_pending)) {
    return TORB_POLL_STOPPED;
  }
  /*
   * A thread of the pool that runs a task of the program is the program's from here on: what its scheduler frees after
   * the resume - the task block, a result nobody waits for - is the program's, as it is on the thread that runs the
   * program, whose scheduler runs inside a call of the kernel. The call back counts the interpreter's own apart.
   */
  (void)torb_count_in_machine(1u);
  request.kind = TORB_REQUEST_RESUME;
  request.chunk = frame[0];
  request.first = (int64_t)(intptr_t)task;
  request.second = (int64_t)(intptr_t)(frame + 2);
  request.words = (int64_t)task->state;
  if (frame[1] == 0 && torb_machine_sandbox_open != 0) {
    /*
     * A task of the program that the scheduler of a script resumes - the script's test waits for its own tasks and runs
     * whatever is queued meanwhile - runs under the program's rules and not the script's grant: the sandbox is set
     * aside for it, what it starts is the program's, and a panic of it is the program's and no stop of the script.
     */
    int active = torb_sandbox_active;
    int64_t open = torb_machine_sandbox_open;
    torb_recovery *point = torb_begin_recovery(NULL);
    torb_sandbox_active = 0;
    torb_machine_sandbox_open = 0;
    answer = torb_machine_call_interpreter((int64_t)(intptr_t)&request);
    torb_machine_sandbox_open = open;
    torb_sandbox_active = active;
    torb_end_recovery(point);
    return (torb_poll)answer;
  }
  answer = torb_machine_call_interpreter((int64_t)(intptr_t)&request);
  if (torb_machine_stopped_in_call_back) {
    /* The script stopped inside the task: the task stops, and the operation that ran the scheduler passes it on */
    torb_machine_stopped_in_call_back = false;
    torb_machine_stop_pending = true;
    return TORB_POLL_STOPPED;
  }
  return (torb_poll)answer;
}

/*
 * A task over the body `chunk`, with room for the chunk's word, the sandbox's generation and `words` words of its
 * frame, not started yet.
 */
static torb_task *torb_machine_task_new(int64_t chunk, int64_t words, const torb_element *result) {
  torb_task *task = torb_task_new(torb_machine_resume, 8u * (size_t)(2 + words), result);
  ((int64_t *)torb_task_frame(task))[0] = chunk;
  ((int64_t *)torb_task_frame(task))[1] = torb_machine_sandbox_open;
  return task;
}

/*
 * The C closure `runtime/test.c` calls: its environment is the request, which lives on the stack of the substitute. A
 * script that stops inside the body stops as it does inside a task: the body returns, and the operation that ran the
 * test passes the stop on - it is the script's end, and no failure of the test behind the test's recovery point.
 */
static void torb_machine_run_request(torb_environment *environment) {
  (void)torb_machine_call_interpreter((int64_t)(intptr_t)(void *)environment);
  if (torb_machine_stopped_in_call_back) {
    torb_machine_stopped_in_call_back = false;
    torb_machine_stop_pending = true;
  }
}

/*
 * `test` and `group` inside the VM: the program's closure is two words of the registers - its function and its
 * environment - and no C code, so the runtime gets a C closure that hands a request for it back to the interpreter. The
 * recovery point of a test is `torb_test_case`'s own, which is what makes a failing body in the VM report and go on
 * exactly as it does in a native binary.
 */
void torb_machine_test_case(torb_text name, const int64_t *body) {
  torb_machine_request request = { TORB_REQUEST_RUN, body[0], body[1], 0, 0 };
  torb_closure closure = { (void (*)(void))torb_machine_run_request, (torb_environment *)(void *)&request };
  /* A failing body jumps back here past the call back, which then never puts the counting back itself */
  unsigned inside = torb_count_in_machine(1u);
  /* ... nor the operations it was inside; and the wait for the test's tasks runs the scheduler, outside every one */
  uint32_t operating = *torb_machine_operating_here();
  *torb_machine_operating_here() = 0u;
  torb_test_case(name, closure);
  *torb_machine_operating_here() = operating;
  (void)torb_count_in_machine(inside);
}

void torb_machine_test_group(torb_text name, const int64_t *body) {
  torb_machine_request request = { TORB_REQUEST_RUN, body[0], body[1], 0, 0 };
  torb_closure closure = { (void (*)(void))torb_machine_run_request, (torb_environment *)(void *)&request };
  uint32_t operating = *torb_machine_operating_here();
  *torb_machine_operating_here() = 0u;
  torb_test_group(name, closure);
  *torb_machine_operating_here() = operating;
}

/* ------------------------------------------------------------------------------------------------- locations --- */

static torb_location *torb_machine_location_table = NULL;
static size_t torb_machine_location_count = 0;

torb_location torb_machine_location(int64_t index) {
  if (index < 0 || (size_t)index >= torb_machine_location_count) {
    return torb_location_unknown;
  }
  return torb_machine_location_table[index];
}

/*
 * The table outlives every program the VM runs, like the static data of a native binary, so it is allocated with
 * `malloc` and never counted: the leak counter is about the program's blocks.
 */
static void torb_machine_define_location(int64_t index, torb_text path, int64_t line, int64_t column) {
  char *copy;
  if (index < 0) {
    return;
  }
  if ((size_t)index >= torb_machine_location_count) {
    size_t count = (size_t)index + 1;
    torb_location *grown = (torb_location *)realloc(torb_machine_location_table, count * sizeof(torb_location));
    if (grown == NULL) {
      torb_panic_out_of_memory(count * sizeof(torb_location));
    }
    for (size_t position = torb_machine_location_count; position < count; position++) {
      grown[position] = torb_location_unknown;
    }
    torb_machine_location_table = grown;
    torb_machine_location_count = count;
  }
  copy = (char *)malloc((size_t)path.length + 1);
  if (copy == NULL) {
    torb_panic_out_of_memory((size_t)path.length + 1);
  }
  if (path.length > 0) {
    memcpy(copy, path.storage->data + path.offset, path.length);
  }
  copy[path.length] = '\0';
  torb_machine_location_table[index] = TORB_LOCATION(copy, line, column);
}

/* ---------------------------------------------------------------------------------------- element descriptors --- */

static const torb_element **torb_machine_element_table = NULL;
static size_t torb_machine_element_count = 0;

const torb_element *torb_machine_element(int64_t index) {
  if (index < 0 || (size_t)index >= torb_machine_element_count || torb_machine_element_table[index] == NULL) {
    torb_panic_text("internal error: the VM named an element descriptor it never defined", torb_location_unknown);
  }
  return torb_machine_element_table[index];
}

static const torb_element *torb_machine_counted_element(int64_t words, int64_t shape, int64_t equals, int64_t hash);
static const torb_element *torb_machine_narrow_element(int64_t narrow);

/*
 * A descriptor the VM makes: a trivial element of one word is the runtime's own `Int64` descriptor (a `Bool`, a
 * `Char` and every integer are one widened word in the VM), a `String` is the runtime's text descriptor, a trivial
 * element of several words gets a descriptor of its own with no callbacks, and a counted element of the program's own
 * types (kind 2) gets callbacks that walk its shape. `equals` and `hash` are the chunks of the program's own, -1 where
 * the words decide: an element that has them gets a slot of the pool whatever else it is, because a float or a record
 * that holds a `String` is equal to another one its words differ from.
 */
static void torb_machine_define_element(int64_t index, int64_t words, int64_t kind, int64_t shape, int64_t equals,
                                        int64_t hash) {
  const torb_element *element;
  if (index < 0) {
    return;
  }
  if ((size_t)index >= torb_machine_element_count) {
    size_t count = (size_t)index + 1;
    const torb_element **grown =
      (const torb_element **)realloc((void *)torb_machine_element_table, count * sizeof(torb_element *));
    if (grown == NULL) {
      torb_panic_out_of_memory(count * sizeof(torb_element *));
    }
    for (size_t position = torb_machine_element_count; position < count; position++) {
      grown[position] = NULL;
    }
    torb_machine_element_table = grown;
    torb_machine_element_count = count;
  }
  if (kind == 1) {
    element = &torb_element_text;
  } else if (kind == 3) {
    /* `shape` is the narrow code of an element stored in its own width */
    element = torb_machine_narrow_element(shape);
  } else if (kind == 2) {
    element = torb_machine_counted_element(words, shape, equals, hash);
  } else if (words == 1 && equals < 0) {
    element = &torb_element_int64;
  } else {
    element = torb_machine_counted_element(words, -1, equals, hash);
  }
  torb_machine_element_table[index] = element;
}

/* ---------------------------------------------------------------------------------------------- arithmetic --- */

/* The kind codes of `numericKindCode` in compiler/src/backend/bytecode/emit.trb. */
enum {
  TORB_KIND_I8 = 1,
  TORB_KIND_I16 = 2,
  TORB_KIND_I32 = 3,
  TORB_KIND_I64 = 4,
  TORB_KIND_U8 = 5,
  TORB_KIND_U16 = 6,
  TORB_KIND_U32 = 7,
  TORB_KIND_U64 = 8,
  TORB_KIND_F32 = 9,
  TORB_KIND_F64 = 10,
  TORB_KIND_TEXT = 11
};

/* The operations of `arithmeticCode`; 256 is added where the operation is checked. */
enum {
  TORB_ARITHMETIC_ADD = 0,
  TORB_ARITHMETIC_SUBTRACT = 1,
  TORB_ARITHMETIC_MULTIPLY = 2,
  TORB_ARITHMETIC_DIVIDE = 3,
  TORB_ARITHMETIC_REMAINDER = 4,
  TORB_ARITHMETIC_NEGATE = 5,
  TORB_ARITHMETIC_AND = 6,
  TORB_ARITHMETIC_OR = 7,
  TORB_ARITHMETIC_EXCLUSIVE_OR = 8,
  TORB_ARITHMETIC_NOT = 9,
  TORB_ARITHMETIC_SHIFTED_LEFT = 10,
  TORB_ARITHMETIC_SHIFTED_RIGHT = 11
};

#define TORB_MACHINE_INTEGER_CASES(suffix, type)                                                                    \
  switch (operation) {                                                                                              \
    case TORB_ARITHMETIC_ADD:                                                                                       \
      return checked ? (int64_t)torb_add_##suffix((type)first, (type)second, at)                                     \
                     : (int64_t)(type)((uint64_t)(type)first + (uint64_t)(type)second);                             \
    case TORB_ARITHMETIC_SUBTRACT:                                                                                  \
      return checked ? (int64_t)torb_subtract_##suffix((type)first, (type)second, at)                                \
                     : (int64_t)(type)((uint64_t)(type)first - (uint64_t)(type)second);                             \
    case TORB_ARITHMETIC_MULTIPLY:                                                                                  \
      return checked ? (int64_t)torb_multiply_##suffix((type)first, (type)second, at)                                \
                     : (int64_t)(type)((uint64_t)(type)first * (uint64_t)(type)second);                             \
    case TORB_ARITHMETIC_DIVIDE:                                                                                    \
      return (int64_t)torb_divide_##suffix((type)first, (type)second, at);                                           \
    case TORB_ARITHMETIC_REMAINDER:                                                                                 \
      return (int64_t)torb_remainder_##suffix((type)first, (type)second, at);                                        \
    case TORB_ARITHMETIC_AND:                                                                                       \
      return (int64_t)torb_bitwise_and_##suffix((type)first, (type)second);                                         \
    case TORB_ARITHMETIC_OR:                                                                                        \
      return (int64_t)torb_bitwise_or_##suffix((type)first, (type)second);                                          \
    case TORB_ARITHMETIC_EXCLUSIVE_OR:                                                                              \
      return (int64_t)torb_bitwise_exclusive_or_##suffix((type)first, (type)second);                                \
    case TORB_ARITHMETIC_NOT:                                                                                       \
      return (int64_t)torb_bitwise_not_##suffix((type)first);                                                       \
    case TORB_ARITHMETIC_SHIFTED_LEFT:                                                                              \
      return (int64_t)torb_shifted_left_##suffix((type)first, second, at);                                          \
    case TORB_ARITHMETIC_SHIFTED_RIGHT:                                                                             \
      return (int64_t)torb_shifted_right_##suffix((type)first, second, at);                                         \
    default:                                                                                                        \
      break;                                                                                                        \
  }

#define TORB_MACHINE_SIGNED_NEGATE(suffix, type)                                                                    \
  if (operation == TORB_ARITHMETIC_NEGATE) {                                                                        \
    return checked ? (int64_t)torb_negate_##suffix((type)first, at) : (int64_t)(type)(0 - (uint64_t)(type)first);   \
  }

static int64_t torb_machine_integer(int64_t kind, int64_t operation, bool checked, int64_t first, int64_t second,
                                    torb_location at) {
  switch (kind) {
    case TORB_KIND_I8: {
      TORB_MACHINE_SIGNED_NEGATE(i8, int8_t)
      TORB_MACHINE_INTEGER_CASES(i8, int8_t)
      break;
    }
    case TORB_KIND_I16: {
      TORB_MACHINE_SIGNED_NEGATE(i16, int16_t)
      TORB_MACHINE_INTEGER_CASES(i16, int16_t)
      break;
    }
    case TORB_KIND_I32: {
      TORB_MACHINE_SIGNED_NEGATE(i32, int32_t)
      TORB_MACHINE_INTEGER_CASES(i32, int32_t)
      break;
    }
    case TORB_KIND_I64: {
      TORB_MACHINE_SIGNED_NEGATE(i64, int64_t)
      TORB_MACHINE_INTEGER_CASES(i64, int64_t)
      break;
    }
    case TORB_KIND_U8: {
      TORB_MACHINE_INTEGER_CASES(u8, uint8_t)
      break;
    }
    case TORB_KIND_U16: {
      TORB_MACHINE_INTEGER_CASES(u16, uint16_t)
      break;
    }
    case TORB_KIND_U32: {
      TORB_MACHINE_INTEGER_CASES(u32, uint32_t)
      break;
    }
    case TORB_KIND_U64: {
      TORB_MACHINE_INTEGER_CASES(u64, uint64_t)
      break;
    }
    default:
      break;
  }
  torb_panic_text("internal error: the VM asked for an integer operation the kernel does not have", at);
}

/* A float operation is the C operator, as the C back end writes it; a `Float32` rounds to `float` after each one. */
static int64_t torb_machine_floating(int64_t kind, int64_t operation, int64_t first, int64_t second,
                                     torb_location at) {
  double a = torb_machine_double_of(first);
  double b = torb_machine_double_of(second);
  double result;
  if (kind == TORB_KIND_F32) {
    float x = (float)a;
    float y = (float)b;
    float found;
    switch (operation) {
      case TORB_ARITHMETIC_ADD: found = x + y; break;
      case TORB_ARITHMETIC_SUBTRACT: found = x - y; break;
      case TORB_ARITHMETIC_MULTIPLY: found = x * y; break;
      case TORB_ARITHMETIC_DIVIDE: found = x / y; break;
      case TORB_ARITHMETIC_REMAINDER: found = torb_remainder_f32(x, y); break;
      case TORB_ARITHMETIC_NEGATE: found = -x; break;
      default: torb_panic_text("internal error: a bit operation on a float", at);
    }
    return torb_machine_word_of_double((double)found);
  }
  switch (operation) {
    case TORB_ARITHMETIC_ADD: result = a + b; break;
    case TORB_ARITHMETIC_SUBTRACT: result = a - b; break;
    case TORB_ARITHMETIC_MULTIPLY: result = a * b; break;
    case TORB_ARITHMETIC_DIVIDE: result = a / b; break;
    case TORB_ARITHMETIC_REMAINDER: result = torb_remainder_f64(a, b); break;
    case TORB_ARITHMETIC_NEGATE: result = -a; break;
    default: torb_panic_text("internal error: a bit operation on a float", at);
  }
  return torb_machine_word_of_double(result);
}

/* The comparisons of `comparisonCode`. */
enum {
  TORB_COMPARE_EQUAL = 0,
  TORB_COMPARE_NOT_EQUAL = 1,
  TORB_COMPARE_LESS = 2,
  TORB_COMPARE_LESS_OR_EQUAL = 3,
  TORB_COMPARE_GREATER = 4,
  TORB_COMPARE_GREATER_OR_EQUAL = 5
};

#define TORB_MACHINE_COMPARE(first, second)                                                                         \
  switch (operation) {                                                                                              \
    case TORB_COMPARE_EQUAL: return (first) == (second);                                                            \
    case TORB_COMPARE_NOT_EQUAL: return (first) != (second);                                                        \
    case TORB_COMPARE_LESS: return (first) < (second);                                                              \
    case TORB_COMPARE_LESS_OR_EQUAL: return (first) <= (second);                                                    \
    case TORB_COMPARE_GREATER: return (first) > (second);                                                           \
    default: return (first) >= (second);                                                                            \
  }

static int64_t torb_machine_compare(int64_t *words, int64_t base, int64_t first, int64_t second, int64_t operation,
                                    int64_t kind) {
  if (kind == TORB_KIND_TEXT) {
    torb_text a;
    torb_text b;
    memcpy(&a, words + base + first, sizeof a);
    memcpy(&b, words + base + second, sizeof b);
    if (operation == TORB_COMPARE_EQUAL) {
      return torb_text_equal(a, b);
    }
    if (operation == TORB_COMPARE_NOT_EQUAL) {
      return !torb_text_equal(a, b);
    }
    {
      int32_t sign = torb_text_compare(a, b);
      TORB_MACHINE_COMPARE(sign, 0)
    }
  }
  if (kind == TORB_KIND_F64 || kind == TORB_KIND_F32) {
    double a = torb_machine_double_of(words[base + first]);
    double b = torb_machine_double_of(words[base + second]);
    if (kind == TORB_KIND_F32) {
      float x = (float)a;
      float y = (float)b;
      TORB_MACHINE_COMPARE(x, y)
    }
    TORB_MACHINE_COMPARE(a, b)
  }
  if (kind == TORB_KIND_U64) {
    uint64_t a = (uint64_t)words[base + first];
    uint64_t b = (uint64_t)words[base + second];
    TORB_MACHINE_COMPARE(a, b)
  }
  {
    int64_t a = words[base + first];
    int64_t b = words[base + second];
    TORB_MACHINE_COMPARE(a, b)
  }
}

/* `Convert` is the C cast of the C back end, from the kind the value has to the kind it becomes. */
static int64_t torb_machine_convert(int64_t word, int64_t from, int64_t to) {
  double floating = 0.0;
  int64_t integer = 0;
  uint64_t natural = 0;
  bool isFloating = from == TORB_KIND_F32 || from == TORB_KIND_F64;
  bool isUnsigned = from >= TORB_KIND_U8 && from <= TORB_KIND_U64;
  if (isFloating) {
    floating = torb_machine_double_of(word);
  } else if (isUnsigned) {
    natural = (uint64_t)word;
  } else {
    integer = word;
  }
#define TORB_MACHINE_CAST(type) \
  (isFloating ? (type)floating : isUnsigned ? (type)natural : (type)integer)
  switch (to) {
    case TORB_KIND_I8: return (int64_t)TORB_MACHINE_CAST(int8_t);
    case TORB_KIND_I16: return (int64_t)TORB_MACHINE_CAST(int16_t);
    case TORB_KIND_I32: return (int64_t)TORB_MACHINE_CAST(int32_t);
    case TORB_KIND_I64: return (int64_t)TORB_MACHINE_CAST(int64_t);
    case TORB_KIND_U8: return (int64_t)TORB_MACHINE_CAST(uint8_t);
    case TORB_KIND_U16: return (int64_t)TORB_MACHINE_CAST(uint16_t);
    case TORB_KIND_U32: return (int64_t)TORB_MACHINE_CAST(uint32_t);
    case TORB_KIND_U64: return (int64_t)TORB_MACHINE_CAST(uint64_t);
    case TORB_KIND_F32: return torb_machine_word_of_double((double)TORB_MACHINE_CAST(float));
    default: return torb_machine_word_of_double(TORB_MACHINE_CAST(double));
  }
#undef TORB_MACHINE_CAST
}

/* ----------------------------------------------------------------------------------------------------- text --- */

/* The part kinds of `textPartCode`, which are the `torb_text_part_kind` values in the same order. */
static torb_text torb_machine_concat(int64_t *words, int64_t base, const int64_t *operands) {
  int64_t count = operands[1];
  torb_text_part fixed[16];
  torb_text_part *parts = fixed;
  memset(fixed, 0, sizeof fixed);
  torb_text result;
  if (count > 16) {
    parts = (torb_text_part *)malloc((size_t)count * sizeof(torb_text_part));
    if (parts == NULL) {
      torb_panic_out_of_memory((size_t)count * sizeof(torb_text_part));
    }
  }
  for (int64_t index = 0; index < count; index++) {
    int64_t *value = words + base + operands[2 + 2 * index];
    torb_text_part part;
    memset(&part, 0, sizeof part);
    part.kind = (int32_t)operands[3 + 2 * index];
    switch (part.kind) {
      case TORB_PART_TEXT: memcpy(&part.text, value, sizeof part.text); break;
      case TORB_PART_SIGNED: part.signed_value = value[0]; break;
      case TORB_PART_UNSIGNED: part.unsigned_value = (uint64_t)value[0]; break;
      case TORB_PART_FLOATING: part.floating = torb_machine_double_of(value[0]); break;
      case TORB_PART_BOOLEAN: part.signed_value = value[0] != 0; break;
      case TORB_PART_CHARACTER: part.unsigned_value = (uint64_t)value[0]; break;
      default: break;
    }
    parts[index] = part;
  }
  result = torb_text_concat_parts(parts, (size_t)count);
  if (parts != fixed) {
    free(parts);
  }
  return result;
}

static void torb_machine_print(int64_t *words, int64_t base, const int64_t *operands, bool isError) {
  int64_t count = operands[0];
  torb_text fixed[16];
  torb_text *parts = fixed;
  memset(fixed, 0, sizeof fixed);
  if (count > 16) {
    parts = (torb_text *)malloc((size_t)count * sizeof(torb_text));
    if (parts == NULL) {
      torb_panic_out_of_memory((size_t)count * sizeof(torb_text));
    }
  }
  for (int64_t index = 0; index < count; index++) {
    memcpy(&parts[index], words + base + operands[1 + index], sizeof(torb_text));
  }
  if (isError) {
    torb_print_error_parts(count == 0 ? NULL : parts, (size_t)count);
  } else {
    torb_print_parts(count == 0 ? NULL : parts, (size_t)count);
  }
  if (parts != fixed) {
    free(parts);
  }
}

/* ---------------------------------------------------------------------------------------------- the shapes --- */

/*
 * The shapes of the program the VM runs (`Shape` of the bytecode format), defined once when it is loaded: which words
 * of a value are counted and how, per variant for an inline variant, and the destructor of a block. Retaining and
 * releasing a value walks its shape here, in C, in the order the C back end's helpers visit the fields - so the counts
 * change in the same order and a `close()` runs where the C back end runs it.
 *
 * Like the location table they outlive every program and are never counted.
 */

/* The counted kinds of `CountedKind`, in its order. */
enum {
  TORB_COUNTED_TEXT = 0,
  TORB_COUNTED_LIST = 1,
  TORB_COUNTED_MAP = 2,
  TORB_COUNTED_SET = 3,
  TORB_COUNTED_TASK = 4,
  TORB_COUNTED_CHANNEL = 5,
  TORB_COUNTED_FILE = 6,
  TORB_COUNTED_BLOCK = 7,
  TORB_COUNTED_ENVIRONMENT = 8,
  TORB_COUNTED_PAYLOAD = 9,
  TORB_COUNTED_NESTED = 10
};

typedef struct torb_machine_word {
  int64_t offset;
  int64_t kind;
  int64_t shape;
} torb_machine_word;

typedef struct torb_machine_group {
  size_t count;
  torb_machine_word *words;
} torb_machine_group;

typedef struct torb_machine_shape {
  int64_t width;
  int64_t tag;
  int64_t closer;
  bool reversed;
  torb_machine_group common;
  size_t group_count;
  torb_machine_group *groups;
} torb_machine_shape;

static torb_machine_shape *torb_machine_shape_table = NULL;
static size_t torb_machine_shape_count = 0;

static void *torb_machine_allocate_table(size_t count, size_t size) {
  void *made = calloc(count == 0 ? 1 : count, size);
  if (made == NULL) {
    torb_panic_out_of_memory(count * size);
  }
  return made;
}

static const torb_machine_shape *torb_machine_shape_at(int64_t index) {
  if (index < 0 || (size_t)index >= torb_machine_shape_count) {
    torb_panic_text("internal error: the VM named a shape it never defined", torb_location_unknown);
  }
  return &torb_machine_shape_table[index];
}

/* `count`, then `(offset, kind, shape)` per word. Answers how many operands it read. */
static int64_t torb_machine_read_group(const int64_t *operands, torb_machine_group *group) {
  size_t count = (size_t)operands[0];
  group->count = count;
  group->words = (torb_machine_word *)torb_machine_allocate_table(count, sizeof(torb_machine_word));
  for (size_t index = 0; index < count; index++) {
    group->words[index].offset = operands[1 + 3 * index];
    group->words[index].kind = operands[2 + 3 * index];
    group->words[index].shape = operands[3 + 3 * index];
  }
  return 1 + 3 * (int64_t)count;
}

/* `index, width, tag, closer, reversed`, the common group, the number of variants, then one group per variant. */
static void torb_machine_define_shape(const int64_t *operands) {
  int64_t index = operands[0];
  torb_machine_shape *shape;
  int64_t cursor;
  if (index < 0) {
    return;
  }
  if ((size_t)index >= torb_machine_shape_count) {
    size_t count = (size_t)index + 1;
    torb_machine_shape *grown =
      (torb_machine_shape *)realloc(torb_machine_shape_table, count * sizeof(torb_machine_shape));
    if (grown == NULL) {
      torb_panic_out_of_memory(count * sizeof(torb_machine_shape));
    }
    memset(grown + torb_machine_shape_count, 0, (count - torb_machine_shape_count) * sizeof(torb_machine_shape));
    torb_machine_shape_table = grown;
    torb_machine_shape_count = count;
  }
  shape = &torb_machine_shape_table[index];
  shape->width = operands[1];
  shape->tag = operands[2];
  shape->closer = operands[3];
  shape->reversed = operands[4] != 0;
  cursor = 5 + torb_machine_read_group(operands + 5, &shape->common);
  shape->group_count = (size_t)operands[cursor];
  cursor += 1;
  shape->groups = (torb_machine_group *)torb_machine_allocate_table(shape->group_count, sizeof(torb_machine_group));
  for (size_t group = 0; group < shape->group_count; group++) {
    cursor += torb_machine_read_group(operands + cursor, &shape->groups[group]);
  }
}

/* The words of a counted block of the VM: after the header, the shape word, then the contents. */
static int64_t *torb_machine_contents(void *block) {
  return (int64_t *)((uint8_t *)block + sizeof(torb_header));
}

/* ------------------------------------------------------------------------------------------------ retaining --- */

static void torb_machine_retain_value(int64_t *value, const torb_machine_shape *shape);

static void torb_machine_retain_group(int64_t *value, const torb_machine_group *group) {
  for (size_t index = 0; index < group->count; index++) {
    const torb_machine_word *word = &group->words[index];
    if (word->kind == TORB_COUNTED_NESTED) {
      torb_machine_retain_value(value + word->offset, torb_machine_shape_at(word->shape));
      continue;
    }
    /* Every other kind has its counted block in its first word, and a null block is nothing to retain */
    torb_retain((void *)(intptr_t)value[word->offset]);
  }
}

static void torb_machine_retain_value(int64_t *value, const torb_machine_shape *shape) {
  torb_machine_retain_group(value, &shape->common);
  if (shape->tag >= 0) {
    int64_t variant = value[shape->tag];
    if (variant >= 0 && (size_t)variant < shape->group_count) {
      torb_machine_retain_group(value, &shape->groups[variant]);
    }
  }
}

/* ------------------------------------------------------------------------------------------------ releasing --- */

/*
 * The blocks whose destructor has to run, in the order their last count went: the VM takes them out one by one
 * (`TakeCloser`), runs each `close()`, and hands the block back (`FinishRelease`), which releases its fields. The runtime
 * cannot call into the interpreter, and nothing but a destructor can tell that it runs a moment later, after the
 * kernel call that dropped the count returned (docs/design/VM.md section 7).
 */
/* One queue per thread: the destructors run on the thread whose release queued them, by that thread's interpreter. */
typedef struct torb_machine_closers {
  void **blocks;
  size_t count;
  size_t capacity;
  size_t first;
} torb_machine_closers;

static torb_machine_closers torb_machine_closer_queues[TORB_MACHINE_SLOTS + 1u];

static torb_machine_closers *torb_machine_closers_here(void) {
  return &torb_machine_closer_queues[torb_machine_slot()];
}

/*
 * How many operations of the loop the thread is inside, by slot. A destructor queued inside one runs when the loop
 * looks at the queue after it, before its next instruction. One queued outside every one - by the scheduler of a worker
 * that releases the value of a task nobody waits for, or while an operation runs the scheduler, which counts as outside
 * - runs at once, through a call back, as the C back end's drop function runs inside the release that dropped the last
 * count (docs/design/VM.md section 7).
 */
static uint32_t torb_machine_operating[TORB_MACHINE_SLOTS + 1u];

/* The count of the calling thread's slot. */
static uint32_t *torb_machine_operating_here(void) {
  return &torb_machine_operating[torb_machine_slot()];
}

static void torb_machine_close_now(void) {
  size_t slot = torb_machine_slot();
  torb_machine_request request = { TORB_REQUEST_CLOSE, 0, 0, 0, 0 };
  /* Only on a thread with an interpreter of its own: another one's registers are not this thread's to use */
  if (slot >= TORB_MACHINE_SLOTS || torb_machine_interpreters[slot].code == NULL) {
    return;
  }
  torb_machine_operating[slot] += 1u;
  (void)torb_machine_call_back((int64_t)(intptr_t)&request);
  torb_machine_operating[slot] -= 1u;
}

static void torb_machine_queue_closer(void *block) {
  torb_machine_closers *queue = torb_machine_closers_here();
  if (queue->count == queue->capacity) {
    size_t capacity = queue->capacity == 0 ? 8 : queue->capacity * 2;
    void **grown = (void **)realloc((void *)queue->blocks, capacity * sizeof(void *));
    if (grown == NULL) {
      torb_panic_out_of_memory(capacity * sizeof(void *));
    }
    queue->blocks = grown;
    queue->capacity = capacity;
  }
  queue->blocks[queue->count] = block;
  queue->count += 1;
  if (*torb_machine_operating_here() == 0u) {
    torb_machine_close_now();
  }
}

static void torb_machine_release_value(int64_t *value, const torb_machine_shape *shape);

/*
 * One count less of a block of the VM. The last one releases its fields and frees it - or, where its shape has a
 * destructor, queues it for the VM with that last count still in place.
 */
static void torb_machine_release_block(void *block) {
  torb_header *header = (torb_header *)block;
  const torb_machine_shape *shape;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  if ((header->count & TORB_SHARED_COUNT) != 0u) {
    /* A shared environment, which several workers may hold: whoever takes the last count frees it */
    if (!torb_count_down(block)) {
      return;
    }
    shape = torb_machine_shape_at(torb_machine_contents(block)[0]);
    torb_machine_release_value(torb_machine_contents(block) + 1, shape);
    torb_free_counted(block, NULL);
    return;
  }
  if (header->count == 0) {
    torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
  }
  if (header->count > 1) {
    header->count -= 1;
    return;
  }
  shape = torb_machine_shape_at(torb_machine_contents(block)[0]);
  if (shape->closer >= 0) {
    torb_machine_queue_closer(block);
    return;
  }
  torb_machine_release_value(torb_machine_contents(block) + 1, shape);
  torb_release(block, NULL);
}

static void torb_machine_release_word(int64_t *value, const torb_machine_word *word) {
  int64_t *place = value + word->offset;
  switch (word->kind) {
    case TORB_COUNTED_TEXT: {
      torb_text text;
      memcpy(&text, place, sizeof text);
      torb_text_release(text);
      return;
    }
    case TORB_COUNTED_LIST: {
      torb_list list;
      memcpy(&list, place, sizeof list);
      if (list.storage != NULL) {
        torb_list_release(list);
      }
      return;
    }
    case TORB_COUNTED_MAP: {
      torb_map map;
      memcpy(&map, place, sizeof map);
      if (map.storage != NULL) {
        torb_map_release(map);
      }
      return;
    }
    case TORB_COUNTED_SET: {
      torb_set set;
      memcpy(&set, place, sizeof set);
      if (set.storage != NULL) {
        torb_set_release(set);
      }
      return;
    }
    case TORB_COUNTED_TASK:
      if (place[0] != 0) {
        torb_task_release((torb_task *)(intptr_t)place[0]);
      }
      return;
    case TORB_COUNTED_CHANNEL:
      if (place[0] != 0) {
        torb_channel_release((torb_channel *)(intptr_t)place[0]);
      }
      return;
    case TORB_COUNTED_FILE:
      torb_release((void *)(intptr_t)place[0], torb_file_drop);
      return;
    case TORB_COUNTED_BLOCK:
    case TORB_COUNTED_ENVIRONMENT:
    case TORB_COUNTED_PAYLOAD:
      torb_machine_release_block((void *)(intptr_t)place[0]);
      return;
    case TORB_COUNTED_NESTED:
      torb_machine_release_value(place, torb_machine_shape_at(word->shape));
      return;
    default:
      torb_panic_text("internal error: a counted word of a kind the kernel does not release", torb_location_unknown);
  }
}

static void torb_machine_release_group(int64_t *value, const torb_machine_group *group) {
  for (size_t index = 0; index < group->count; index++) {
    torb_machine_release_word(value, &group->words[index]);
  }
}

/*
 * The fields of a value in the order the C back end's drop function releases them: the common fields, then the group
 * of the variant the tag names - or, reversed, the group first and the common fields after it, each group already in
 * reverse declaration order in the shape.
 */
static void torb_machine_release_value(int64_t *value, const torb_machine_shape *shape) {
  const torb_machine_group *group = NULL;
  if (shape->tag >= 0) {
    int64_t variant = value[shape->tag];
    if (variant >= 0 && (size_t)variant < shape->group_count) {
      group = &shape->groups[variant];
    }
  }
  if (shape->reversed) {
    if (group != NULL) {
      torb_machine_release_group(value, group);
    }
    torb_machine_release_group(value, &shape->common);
    return;
  }
  torb_machine_release_group(value, &shape->common);
  if (group != NULL) {
    torb_machine_release_group(value, group);
  }
}

/* The destructors queued since the VM last asked, which it takes out in order. */
static int64_t torb_machine_queued(void) {
  const torb_machine_closers *queue = torb_machine_closers_here();
  return (int64_t)(queue->count - queue->first);
}

/*
 * The first queued block into a register, answering the chunk of its `close()`; -1 when the queue is empty. The queue
 * is emptied as it is read, so the blocks a `FinishRelease` queues afterwards are the next ones the VM takes.
 */
static int64_t torb_machine_take_closer(int64_t *target) {
  torb_machine_closers *queue = torb_machine_closers_here();
  void *block;
  if (queue->first >= queue->count) {
    queue->first = 0;
    queue->count = 0;
    return -1;
  }
  block = queue->blocks[queue->first];
  queue->first += 1;
  if (queue->first == queue->count) {
    queue->first = 0;
    queue->count = 0;
  }
  target[0] = (int64_t)(intptr_t)block;
  return torb_machine_shape_at(torb_machine_contents(block)[0])->closer;
}

/* After its `close()` ran: the fields of a queued block, and the block itself. */
static void torb_machine_finish_release(void *block) {
  torb_machine_release_value(torb_machine_contents(block) + 1, torb_machine_shape_at(torb_machine_contents(block)[0]));
  torb_release(block, NULL);
}

/*
 * A block the VM writes through must be its own: where somebody else holds it too, it is copied into a new block of the
 * same kind, the counted words of the copy are retained, and the copy replaces the old block at the reference - which
 * keeps its other holders' counts. An immortal block always copies.
 */
static void torb_machine_block_unique(int64_t *words, int64_t reference) {
  void **place = (void **)torb_machine_address(words, reference);
  torb_header *header = (torb_header *)*place;
  const torb_machine_shape *shape;
  torb_header *copy;
  size_t size;
  if (header == NULL || header->count == 1) {
    return;
  }
  shape = torb_machine_shape_at(torb_machine_contents(header)[0]);
  size = sizeof(torb_header) + 8u + 8u * (size_t)shape->width;
  copy = (torb_header *)torb_allocate(size, (torb_block_kind)header->kind);
  memcpy(torb_machine_contents(copy), torb_machine_contents(header), size - sizeof(torb_header));
  torb_machine_retain_value(torb_machine_contents(copy) + 1, shape);
  if (header->count != TORB_IMMORTAL_COUNT) {
    header->count -= 1;
  }
  *place = copy;
}

/* -------------------------------------------------------------------------------------- counted elements --- */

/*
 * A counted element of a container is released and retained by the runtime, through the callbacks of its descriptor -
 * which receive the element and nothing else. So every counted element type of the program gets a slot of this pool:
 * the slot's two callbacks walk the shape the slot was given. A program with more counted element types than slots is
 * refused with an internal error rather than miscounted.
 */
#define TORB_MACHINE_ELEMENT_SLOTS 256

static int64_t torb_machine_element_shapes[TORB_MACHINE_ELEMENT_SLOTS];
static size_t torb_machine_element_slots = 0;

static int64_t torb_machine_element_words[TORB_MACHINE_ELEMENT_SLOTS];

static bool torb_machine_equal_words(const void *first, const void *second, int64_t words) {
  return memcmp(first, second, 8u * (size_t)words) == 0;
}

/* FNV-1a over the words: the hash of a key is the table's business, and never observable. */
static uint64_t torb_machine_hash_words(const void *value, int64_t words) {
  const uint8_t *bytes = (const uint8_t *)value;
  uint64_t hash = 14695981039346656037u;
  for (size_t index = 0; index < 8u * (size_t)words; index++) {
    hash = (hash ^ bytes[index]) * 1099511628211u;
  }
  return hash;
}

/*
 * The `equals` and the `hash` of the program's own a slot's elements are compared and hashed by, as chunks of the
 * bytecode; -1 where the words are the answer (`comparesByWords` of the bytecode: integers, `Bool`s and `Char`s).
 */
static int64_t torb_machine_element_equals[TORB_MACHINE_ELEMENT_SLOTS];
static int64_t torb_machine_element_hashes[TORB_MACHINE_ELEMENT_SLOTS];

/* Two elements of a slot: by their words, or by the program's `equals`, which the interpreter runs. */
static bool torb_machine_equal_slot(size_t slot, const void *first, const void *second) {
  torb_machine_request request;
  if (torb_machine_element_equals[slot] < 0) {
    return torb_machine_equal_words(first, second, torb_machine_element_words[slot]);
  }
  request.kind = TORB_REQUEST_EQUALS;
  request.chunk = torb_machine_element_equals[slot];
  request.first = (int64_t)(intptr_t)first;
  request.second = (int64_t)(intptr_t)second;
  request.words = torb_machine_element_words[slot];
  return torb_machine_call_back((int64_t)(intptr_t)&request) != 0;
}

/* The hash of an element of a slot: of its words, or the program's `hash`, which the interpreter runs. */
static uint64_t torb_machine_hash_slot(size_t slot, const void *value) {
  torb_machine_request request;
  if (torb_machine_element_hashes[slot] < 0) {
    return torb_machine_hash_words(value, torb_machine_element_words[slot]);
  }
  request.kind = TORB_REQUEST_HASH;
  request.chunk = torb_machine_element_hashes[slot];
  request.first = (int64_t)(intptr_t)value;
  request.second = 0;
  request.words = torb_machine_element_words[slot];
  return (uint64_t)torb_machine_call_back((int64_t)(intptr_t)&request);
}

/* Four callbacks per slot; the slot is the index of the shape they walk and of the words they compare. */
static void torb_machine_retain_element_0(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[0]));
}
static void torb_machine_release_element_0(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[0]));
}
static bool torb_machine_equal_element_0(const void *first, const void *second) {
  return torb_machine_equal_slot(0, first, second);
}
static uint64_t torb_machine_hash_element_0(const void *value) {
  return torb_machine_hash_slot(0, value);
}
static void torb_machine_retain_element_1(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[1]));
}
static void torb_machine_release_element_1(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[1]));
}
static bool torb_machine_equal_element_1(const void *first, const void *second) {
  return torb_machine_equal_slot(1, first, second);
}
static uint64_t torb_machine_hash_element_1(const void *value) {
  return torb_machine_hash_slot(1, value);
}
static void torb_machine_retain_element_2(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[2]));
}
static void torb_machine_release_element_2(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[2]));
}
static bool torb_machine_equal_element_2(const void *first, const void *second) {
  return torb_machine_equal_slot(2, first, second);
}
static uint64_t torb_machine_hash_element_2(const void *value) {
  return torb_machine_hash_slot(2, value);
}
static void torb_machine_retain_element_3(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[3]));
}
static void torb_machine_release_element_3(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[3]));
}
static bool torb_machine_equal_element_3(const void *first, const void *second) {
  return torb_machine_equal_slot(3, first, second);
}
static uint64_t torb_machine_hash_element_3(const void *value) {
  return torb_machine_hash_slot(3, value);
}
static void torb_machine_retain_element_4(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[4]));
}
static void torb_machine_release_element_4(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[4]));
}
static bool torb_machine_equal_element_4(const void *first, const void *second) {
  return torb_machine_equal_slot(4, first, second);
}
static uint64_t torb_machine_hash_element_4(const void *value) {
  return torb_machine_hash_slot(4, value);
}
static void torb_machine_retain_element_5(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[5]));
}
static void torb_machine_release_element_5(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[5]));
}
static bool torb_machine_equal_element_5(const void *first, const void *second) {
  return torb_machine_equal_slot(5, first, second);
}
static uint64_t torb_machine_hash_element_5(const void *value) {
  return torb_machine_hash_slot(5, value);
}
static void torb_machine_retain_element_6(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[6]));
}
static void torb_machine_release_element_6(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[6]));
}
static bool torb_machine_equal_element_6(const void *first, const void *second) {
  return torb_machine_equal_slot(6, first, second);
}
static uint64_t torb_machine_hash_element_6(const void *value) {
  return torb_machine_hash_slot(6, value);
}
static void torb_machine_retain_element_7(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[7]));
}
static void torb_machine_release_element_7(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[7]));
}
static bool torb_machine_equal_element_7(const void *first, const void *second) {
  return torb_machine_equal_slot(7, first, second);
}
static uint64_t torb_machine_hash_element_7(const void *value) {
  return torb_machine_hash_slot(7, value);
}
static void torb_machine_retain_element_8(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[8]));
}
static void torb_machine_release_element_8(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[8]));
}
static bool torb_machine_equal_element_8(const void *first, const void *second) {
  return torb_machine_equal_slot(8, first, second);
}
static uint64_t torb_machine_hash_element_8(const void *value) {
  return torb_machine_hash_slot(8, value);
}
static void torb_machine_retain_element_9(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[9]));
}
static void torb_machine_release_element_9(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[9]));
}
static bool torb_machine_equal_element_9(const void *first, const void *second) {
  return torb_machine_equal_slot(9, first, second);
}
static uint64_t torb_machine_hash_element_9(const void *value) {
  return torb_machine_hash_slot(9, value);
}
static void torb_machine_retain_element_10(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[10]));
}
static void torb_machine_release_element_10(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[10]));
}
static bool torb_machine_equal_element_10(const void *first, const void *second) {
  return torb_machine_equal_slot(10, first, second);
}
static uint64_t torb_machine_hash_element_10(const void *value) {
  return torb_machine_hash_slot(10, value);
}
static void torb_machine_retain_element_11(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[11]));
}
static void torb_machine_release_element_11(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[11]));
}
static bool torb_machine_equal_element_11(const void *first, const void *second) {
  return torb_machine_equal_slot(11, first, second);
}
static uint64_t torb_machine_hash_element_11(const void *value) {
  return torb_machine_hash_slot(11, value);
}
static void torb_machine_retain_element_12(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[12]));
}
static void torb_machine_release_element_12(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[12]));
}
static bool torb_machine_equal_element_12(const void *first, const void *second) {
  return torb_machine_equal_slot(12, first, second);
}
static uint64_t torb_machine_hash_element_12(const void *value) {
  return torb_machine_hash_slot(12, value);
}
static void torb_machine_retain_element_13(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[13]));
}
static void torb_machine_release_element_13(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[13]));
}
static bool torb_machine_equal_element_13(const void *first, const void *second) {
  return torb_machine_equal_slot(13, first, second);
}
static uint64_t torb_machine_hash_element_13(const void *value) {
  return torb_machine_hash_slot(13, value);
}
static void torb_machine_retain_element_14(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[14]));
}
static void torb_machine_release_element_14(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[14]));
}
static bool torb_machine_equal_element_14(const void *first, const void *second) {
  return torb_machine_equal_slot(14, first, second);
}
static uint64_t torb_machine_hash_element_14(const void *value) {
  return torb_machine_hash_slot(14, value);
}
static void torb_machine_retain_element_15(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[15]));
}
static void torb_machine_release_element_15(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[15]));
}
static bool torb_machine_equal_element_15(const void *first, const void *second) {
  return torb_machine_equal_slot(15, first, second);
}
static uint64_t torb_machine_hash_element_15(const void *value) {
  return torb_machine_hash_slot(15, value);
}
static void torb_machine_retain_element_16(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[16]));
}
static void torb_machine_release_element_16(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[16]));
}
static bool torb_machine_equal_element_16(const void *first, const void *second) {
  return torb_machine_equal_slot(16, first, second);
}
static uint64_t torb_machine_hash_element_16(const void *value) {
  return torb_machine_hash_slot(16, value);
}
static void torb_machine_retain_element_17(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[17]));
}
static void torb_machine_release_element_17(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[17]));
}
static bool torb_machine_equal_element_17(const void *first, const void *second) {
  return torb_machine_equal_slot(17, first, second);
}
static uint64_t torb_machine_hash_element_17(const void *value) {
  return torb_machine_hash_slot(17, value);
}
static void torb_machine_retain_element_18(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[18]));
}
static void torb_machine_release_element_18(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[18]));
}
static bool torb_machine_equal_element_18(const void *first, const void *second) {
  return torb_machine_equal_slot(18, first, second);
}
static uint64_t torb_machine_hash_element_18(const void *value) {
  return torb_machine_hash_slot(18, value);
}
static void torb_machine_retain_element_19(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[19]));
}
static void torb_machine_release_element_19(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[19]));
}
static bool torb_machine_equal_element_19(const void *first, const void *second) {
  return torb_machine_equal_slot(19, first, second);
}
static uint64_t torb_machine_hash_element_19(const void *value) {
  return torb_machine_hash_slot(19, value);
}
static void torb_machine_retain_element_20(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[20]));
}
static void torb_machine_release_element_20(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[20]));
}
static bool torb_machine_equal_element_20(const void *first, const void *second) {
  return torb_machine_equal_slot(20, first, second);
}
static uint64_t torb_machine_hash_element_20(const void *value) {
  return torb_machine_hash_slot(20, value);
}
static void torb_machine_retain_element_21(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[21]));
}
static void torb_machine_release_element_21(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[21]));
}
static bool torb_machine_equal_element_21(const void *first, const void *second) {
  return torb_machine_equal_slot(21, first, second);
}
static uint64_t torb_machine_hash_element_21(const void *value) {
  return torb_machine_hash_slot(21, value);
}
static void torb_machine_retain_element_22(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[22]));
}
static void torb_machine_release_element_22(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[22]));
}
static bool torb_machine_equal_element_22(const void *first, const void *second) {
  return torb_machine_equal_slot(22, first, second);
}
static uint64_t torb_machine_hash_element_22(const void *value) {
  return torb_machine_hash_slot(22, value);
}
static void torb_machine_retain_element_23(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[23]));
}
static void torb_machine_release_element_23(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[23]));
}
static bool torb_machine_equal_element_23(const void *first, const void *second) {
  return torb_machine_equal_slot(23, first, second);
}
static uint64_t torb_machine_hash_element_23(const void *value) {
  return torb_machine_hash_slot(23, value);
}
static void torb_machine_retain_element_24(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[24]));
}
static void torb_machine_release_element_24(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[24]));
}
static bool torb_machine_equal_element_24(const void *first, const void *second) {
  return torb_machine_equal_slot(24, first, second);
}
static uint64_t torb_machine_hash_element_24(const void *value) {
  return torb_machine_hash_slot(24, value);
}
static void torb_machine_retain_element_25(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[25]));
}
static void torb_machine_release_element_25(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[25]));
}
static bool torb_machine_equal_element_25(const void *first, const void *second) {
  return torb_machine_equal_slot(25, first, second);
}
static uint64_t torb_machine_hash_element_25(const void *value) {
  return torb_machine_hash_slot(25, value);
}
static void torb_machine_retain_element_26(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[26]));
}
static void torb_machine_release_element_26(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[26]));
}
static bool torb_machine_equal_element_26(const void *first, const void *second) {
  return torb_machine_equal_slot(26, first, second);
}
static uint64_t torb_machine_hash_element_26(const void *value) {
  return torb_machine_hash_slot(26, value);
}
static void torb_machine_retain_element_27(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[27]));
}
static void torb_machine_release_element_27(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[27]));
}
static bool torb_machine_equal_element_27(const void *first, const void *second) {
  return torb_machine_equal_slot(27, first, second);
}
static uint64_t torb_machine_hash_element_27(const void *value) {
  return torb_machine_hash_slot(27, value);
}
static void torb_machine_retain_element_28(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[28]));
}
static void torb_machine_release_element_28(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[28]));
}
static bool torb_machine_equal_element_28(const void *first, const void *second) {
  return torb_machine_equal_slot(28, first, second);
}
static uint64_t torb_machine_hash_element_28(const void *value) {
  return torb_machine_hash_slot(28, value);
}
static void torb_machine_retain_element_29(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[29]));
}
static void torb_machine_release_element_29(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[29]));
}
static bool torb_machine_equal_element_29(const void *first, const void *second) {
  return torb_machine_equal_slot(29, first, second);
}
static uint64_t torb_machine_hash_element_29(const void *value) {
  return torb_machine_hash_slot(29, value);
}
static void torb_machine_retain_element_30(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[30]));
}
static void torb_machine_release_element_30(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[30]));
}
static bool torb_machine_equal_element_30(const void *first, const void *second) {
  return torb_machine_equal_slot(30, first, second);
}
static uint64_t torb_machine_hash_element_30(const void *value) {
  return torb_machine_hash_slot(30, value);
}
static void torb_machine_retain_element_31(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[31]));
}
static void torb_machine_release_element_31(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[31]));
}
static bool torb_machine_equal_element_31(const void *first, const void *second) {
  return torb_machine_equal_slot(31, first, second);
}
static uint64_t torb_machine_hash_element_31(const void *value) {
  return torb_machine_hash_slot(31, value);
}
static void torb_machine_retain_element_32(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[32]));
}
static void torb_machine_release_element_32(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[32]));
}
static bool torb_machine_equal_element_32(const void *first, const void *second) {
  return torb_machine_equal_slot(32, first, second);
}
static uint64_t torb_machine_hash_element_32(const void *value) {
  return torb_machine_hash_slot(32, value);
}
static void torb_machine_retain_element_33(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[33]));
}
static void torb_machine_release_element_33(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[33]));
}
static bool torb_machine_equal_element_33(const void *first, const void *second) {
  return torb_machine_equal_slot(33, first, second);
}
static uint64_t torb_machine_hash_element_33(const void *value) {
  return torb_machine_hash_slot(33, value);
}
static void torb_machine_retain_element_34(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[34]));
}
static void torb_machine_release_element_34(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[34]));
}
static bool torb_machine_equal_element_34(const void *first, const void *second) {
  return torb_machine_equal_slot(34, first, second);
}
static uint64_t torb_machine_hash_element_34(const void *value) {
  return torb_machine_hash_slot(34, value);
}
static void torb_machine_retain_element_35(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[35]));
}
static void torb_machine_release_element_35(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[35]));
}
static bool torb_machine_equal_element_35(const void *first, const void *second) {
  return torb_machine_equal_slot(35, first, second);
}
static uint64_t torb_machine_hash_element_35(const void *value) {
  return torb_machine_hash_slot(35, value);
}
static void torb_machine_retain_element_36(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[36]));
}
static void torb_machine_release_element_36(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[36]));
}
static bool torb_machine_equal_element_36(const void *first, const void *second) {
  return torb_machine_equal_slot(36, first, second);
}
static uint64_t torb_machine_hash_element_36(const void *value) {
  return torb_machine_hash_slot(36, value);
}
static void torb_machine_retain_element_37(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[37]));
}
static void torb_machine_release_element_37(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[37]));
}
static bool torb_machine_equal_element_37(const void *first, const void *second) {
  return torb_machine_equal_slot(37, first, second);
}
static uint64_t torb_machine_hash_element_37(const void *value) {
  return torb_machine_hash_slot(37, value);
}
static void torb_machine_retain_element_38(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[38]));
}
static void torb_machine_release_element_38(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[38]));
}
static bool torb_machine_equal_element_38(const void *first, const void *second) {
  return torb_machine_equal_slot(38, first, second);
}
static uint64_t torb_machine_hash_element_38(const void *value) {
  return torb_machine_hash_slot(38, value);
}
static void torb_machine_retain_element_39(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[39]));
}
static void torb_machine_release_element_39(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[39]));
}
static bool torb_machine_equal_element_39(const void *first, const void *second) {
  return torb_machine_equal_slot(39, first, second);
}
static uint64_t torb_machine_hash_element_39(const void *value) {
  return torb_machine_hash_slot(39, value);
}
static void torb_machine_retain_element_40(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[40]));
}
static void torb_machine_release_element_40(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[40]));
}
static bool torb_machine_equal_element_40(const void *first, const void *second) {
  return torb_machine_equal_slot(40, first, second);
}
static uint64_t torb_machine_hash_element_40(const void *value) {
  return torb_machine_hash_slot(40, value);
}
static void torb_machine_retain_element_41(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[41]));
}
static void torb_machine_release_element_41(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[41]));
}
static bool torb_machine_equal_element_41(const void *first, const void *second) {
  return torb_machine_equal_slot(41, first, second);
}
static uint64_t torb_machine_hash_element_41(const void *value) {
  return torb_machine_hash_slot(41, value);
}
static void torb_machine_retain_element_42(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[42]));
}
static void torb_machine_release_element_42(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[42]));
}
static bool torb_machine_equal_element_42(const void *first, const void *second) {
  return torb_machine_equal_slot(42, first, second);
}
static uint64_t torb_machine_hash_element_42(const void *value) {
  return torb_machine_hash_slot(42, value);
}
static void torb_machine_retain_element_43(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[43]));
}
static void torb_machine_release_element_43(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[43]));
}
static bool torb_machine_equal_element_43(const void *first, const void *second) {
  return torb_machine_equal_slot(43, first, second);
}
static uint64_t torb_machine_hash_element_43(const void *value) {
  return torb_machine_hash_slot(43, value);
}
static void torb_machine_retain_element_44(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[44]));
}
static void torb_machine_release_element_44(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[44]));
}
static bool torb_machine_equal_element_44(const void *first, const void *second) {
  return torb_machine_equal_slot(44, first, second);
}
static uint64_t torb_machine_hash_element_44(const void *value) {
  return torb_machine_hash_slot(44, value);
}
static void torb_machine_retain_element_45(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[45]));
}
static void torb_machine_release_element_45(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[45]));
}
static bool torb_machine_equal_element_45(const void *first, const void *second) {
  return torb_machine_equal_slot(45, first, second);
}
static uint64_t torb_machine_hash_element_45(const void *value) {
  return torb_machine_hash_slot(45, value);
}
static void torb_machine_retain_element_46(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[46]));
}
static void torb_machine_release_element_46(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[46]));
}
static bool torb_machine_equal_element_46(const void *first, const void *second) {
  return torb_machine_equal_slot(46, first, second);
}
static uint64_t torb_machine_hash_element_46(const void *value) {
  return torb_machine_hash_slot(46, value);
}
static void torb_machine_retain_element_47(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[47]));
}
static void torb_machine_release_element_47(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[47]));
}
static bool torb_machine_equal_element_47(const void *first, const void *second) {
  return torb_machine_equal_slot(47, first, second);
}
static uint64_t torb_machine_hash_element_47(const void *value) {
  return torb_machine_hash_slot(47, value);
}
static void torb_machine_retain_element_48(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[48]));
}
static void torb_machine_release_element_48(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[48]));
}
static bool torb_machine_equal_element_48(const void *first, const void *second) {
  return torb_machine_equal_slot(48, first, second);
}
static uint64_t torb_machine_hash_element_48(const void *value) {
  return torb_machine_hash_slot(48, value);
}
static void torb_machine_retain_element_49(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[49]));
}
static void torb_machine_release_element_49(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[49]));
}
static bool torb_machine_equal_element_49(const void *first, const void *second) {
  return torb_machine_equal_slot(49, first, second);
}
static uint64_t torb_machine_hash_element_49(const void *value) {
  return torb_machine_hash_slot(49, value);
}
static void torb_machine_retain_element_50(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[50]));
}
static void torb_machine_release_element_50(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[50]));
}
static bool torb_machine_equal_element_50(const void *first, const void *second) {
  return torb_machine_equal_slot(50, first, second);
}
static uint64_t torb_machine_hash_element_50(const void *value) {
  return torb_machine_hash_slot(50, value);
}
static void torb_machine_retain_element_51(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[51]));
}
static void torb_machine_release_element_51(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[51]));
}
static bool torb_machine_equal_element_51(const void *first, const void *second) {
  return torb_machine_equal_slot(51, first, second);
}
static uint64_t torb_machine_hash_element_51(const void *value) {
  return torb_machine_hash_slot(51, value);
}
static void torb_machine_retain_element_52(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[52]));
}
static void torb_machine_release_element_52(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[52]));
}
static bool torb_machine_equal_element_52(const void *first, const void *second) {
  return torb_machine_equal_slot(52, first, second);
}
static uint64_t torb_machine_hash_element_52(const void *value) {
  return torb_machine_hash_slot(52, value);
}
static void torb_machine_retain_element_53(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[53]));
}
static void torb_machine_release_element_53(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[53]));
}
static bool torb_machine_equal_element_53(const void *first, const void *second) {
  return torb_machine_equal_slot(53, first, second);
}
static uint64_t torb_machine_hash_element_53(const void *value) {
  return torb_machine_hash_slot(53, value);
}
static void torb_machine_retain_element_54(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[54]));
}
static void torb_machine_release_element_54(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[54]));
}
static bool torb_machine_equal_element_54(const void *first, const void *second) {
  return torb_machine_equal_slot(54, first, second);
}
static uint64_t torb_machine_hash_element_54(const void *value) {
  return torb_machine_hash_slot(54, value);
}
static void torb_machine_retain_element_55(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[55]));
}
static void torb_machine_release_element_55(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[55]));
}
static bool torb_machine_equal_element_55(const void *first, const void *second) {
  return torb_machine_equal_slot(55, first, second);
}
static uint64_t torb_machine_hash_element_55(const void *value) {
  return torb_machine_hash_slot(55, value);
}
static void torb_machine_retain_element_56(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[56]));
}
static void torb_machine_release_element_56(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[56]));
}
static bool torb_machine_equal_element_56(const void *first, const void *second) {
  return torb_machine_equal_slot(56, first, second);
}
static uint64_t torb_machine_hash_element_56(const void *value) {
  return torb_machine_hash_slot(56, value);
}
static void torb_machine_retain_element_57(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[57]));
}
static void torb_machine_release_element_57(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[57]));
}
static bool torb_machine_equal_element_57(const void *first, const void *second) {
  return torb_machine_equal_slot(57, first, second);
}
static uint64_t torb_machine_hash_element_57(const void *value) {
  return torb_machine_hash_slot(57, value);
}
static void torb_machine_retain_element_58(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[58]));
}
static void torb_machine_release_element_58(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[58]));
}
static bool torb_machine_equal_element_58(const void *first, const void *second) {
  return torb_machine_equal_slot(58, first, second);
}
static uint64_t torb_machine_hash_element_58(const void *value) {
  return torb_machine_hash_slot(58, value);
}
static void torb_machine_retain_element_59(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[59]));
}
static void torb_machine_release_element_59(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[59]));
}
static bool torb_machine_equal_element_59(const void *first, const void *second) {
  return torb_machine_equal_slot(59, first, second);
}
static uint64_t torb_machine_hash_element_59(const void *value) {
  return torb_machine_hash_slot(59, value);
}
static void torb_machine_retain_element_60(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[60]));
}
static void torb_machine_release_element_60(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[60]));
}
static bool torb_machine_equal_element_60(const void *first, const void *second) {
  return torb_machine_equal_slot(60, first, second);
}
static uint64_t torb_machine_hash_element_60(const void *value) {
  return torb_machine_hash_slot(60, value);
}
static void torb_machine_retain_element_61(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[61]));
}
static void torb_machine_release_element_61(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[61]));
}
static bool torb_machine_equal_element_61(const void *first, const void *second) {
  return torb_machine_equal_slot(61, first, second);
}
static uint64_t torb_machine_hash_element_61(const void *value) {
  return torb_machine_hash_slot(61, value);
}
static void torb_machine_retain_element_62(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[62]));
}
static void torb_machine_release_element_62(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[62]));
}
static bool torb_machine_equal_element_62(const void *first, const void *second) {
  return torb_machine_equal_slot(62, first, second);
}
static uint64_t torb_machine_hash_element_62(const void *value) {
  return torb_machine_hash_slot(62, value);
}
static void torb_machine_retain_element_63(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[63]));
}
static void torb_machine_release_element_63(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[63]));
}
static bool torb_machine_equal_element_63(const void *first, const void *second) {
  return torb_machine_equal_slot(63, first, second);
}
static uint64_t torb_machine_hash_element_63(const void *value) {
  return torb_machine_hash_slot(63, value);
}
static void torb_machine_retain_element_64(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[64]));
}
static void torb_machine_release_element_64(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[64]));
}
static bool torb_machine_equal_element_64(const void *first, const void *second) {
  return torb_machine_equal_slot(64, first, second);
}
static uint64_t torb_machine_hash_element_64(const void *value) {
  return torb_machine_hash_slot(64, value);
}
static void torb_machine_retain_element_65(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[65]));
}
static void torb_machine_release_element_65(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[65]));
}
static bool torb_machine_equal_element_65(const void *first, const void *second) {
  return torb_machine_equal_slot(65, first, second);
}
static uint64_t torb_machine_hash_element_65(const void *value) {
  return torb_machine_hash_slot(65, value);
}
static void torb_machine_retain_element_66(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[66]));
}
static void torb_machine_release_element_66(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[66]));
}
static bool torb_machine_equal_element_66(const void *first, const void *second) {
  return torb_machine_equal_slot(66, first, second);
}
static uint64_t torb_machine_hash_element_66(const void *value) {
  return torb_machine_hash_slot(66, value);
}
static void torb_machine_retain_element_67(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[67]));
}
static void torb_machine_release_element_67(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[67]));
}
static bool torb_machine_equal_element_67(const void *first, const void *second) {
  return torb_machine_equal_slot(67, first, second);
}
static uint64_t torb_machine_hash_element_67(const void *value) {
  return torb_machine_hash_slot(67, value);
}
static void torb_machine_retain_element_68(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[68]));
}
static void torb_machine_release_element_68(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[68]));
}
static bool torb_machine_equal_element_68(const void *first, const void *second) {
  return torb_machine_equal_slot(68, first, second);
}
static uint64_t torb_machine_hash_element_68(const void *value) {
  return torb_machine_hash_slot(68, value);
}
static void torb_machine_retain_element_69(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[69]));
}
static void torb_machine_release_element_69(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[69]));
}
static bool torb_machine_equal_element_69(const void *first, const void *second) {
  return torb_machine_equal_slot(69, first, second);
}
static uint64_t torb_machine_hash_element_69(const void *value) {
  return torb_machine_hash_slot(69, value);
}
static void torb_machine_retain_element_70(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[70]));
}
static void torb_machine_release_element_70(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[70]));
}
static bool torb_machine_equal_element_70(const void *first, const void *second) {
  return torb_machine_equal_slot(70, first, second);
}
static uint64_t torb_machine_hash_element_70(const void *value) {
  return torb_machine_hash_slot(70, value);
}
static void torb_machine_retain_element_71(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[71]));
}
static void torb_machine_release_element_71(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[71]));
}
static bool torb_machine_equal_element_71(const void *first, const void *second) {
  return torb_machine_equal_slot(71, first, second);
}
static uint64_t torb_machine_hash_element_71(const void *value) {
  return torb_machine_hash_slot(71, value);
}
static void torb_machine_retain_element_72(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[72]));
}
static void torb_machine_release_element_72(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[72]));
}
static bool torb_machine_equal_element_72(const void *first, const void *second) {
  return torb_machine_equal_slot(72, first, second);
}
static uint64_t torb_machine_hash_element_72(const void *value) {
  return torb_machine_hash_slot(72, value);
}
static void torb_machine_retain_element_73(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[73]));
}
static void torb_machine_release_element_73(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[73]));
}
static bool torb_machine_equal_element_73(const void *first, const void *second) {
  return torb_machine_equal_slot(73, first, second);
}
static uint64_t torb_machine_hash_element_73(const void *value) {
  return torb_machine_hash_slot(73, value);
}
static void torb_machine_retain_element_74(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[74]));
}
static void torb_machine_release_element_74(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[74]));
}
static bool torb_machine_equal_element_74(const void *first, const void *second) {
  return torb_machine_equal_slot(74, first, second);
}
static uint64_t torb_machine_hash_element_74(const void *value) {
  return torb_machine_hash_slot(74, value);
}
static void torb_machine_retain_element_75(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[75]));
}
static void torb_machine_release_element_75(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[75]));
}
static bool torb_machine_equal_element_75(const void *first, const void *second) {
  return torb_machine_equal_slot(75, first, second);
}
static uint64_t torb_machine_hash_element_75(const void *value) {
  return torb_machine_hash_slot(75, value);
}
static void torb_machine_retain_element_76(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[76]));
}
static void torb_machine_release_element_76(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[76]));
}
static bool torb_machine_equal_element_76(const void *first, const void *second) {
  return torb_machine_equal_slot(76, first, second);
}
static uint64_t torb_machine_hash_element_76(const void *value) {
  return torb_machine_hash_slot(76, value);
}
static void torb_machine_retain_element_77(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[77]));
}
static void torb_machine_release_element_77(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[77]));
}
static bool torb_machine_equal_element_77(const void *first, const void *second) {
  return torb_machine_equal_slot(77, first, second);
}
static uint64_t torb_machine_hash_element_77(const void *value) {
  return torb_machine_hash_slot(77, value);
}
static void torb_machine_retain_element_78(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[78]));
}
static void torb_machine_release_element_78(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[78]));
}
static bool torb_machine_equal_element_78(const void *first, const void *second) {
  return torb_machine_equal_slot(78, first, second);
}
static uint64_t torb_machine_hash_element_78(const void *value) {
  return torb_machine_hash_slot(78, value);
}
static void torb_machine_retain_element_79(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[79]));
}
static void torb_machine_release_element_79(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[79]));
}
static bool torb_machine_equal_element_79(const void *first, const void *second) {
  return torb_machine_equal_slot(79, first, second);
}
static uint64_t torb_machine_hash_element_79(const void *value) {
  return torb_machine_hash_slot(79, value);
}
static void torb_machine_retain_element_80(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[80]));
}
static void torb_machine_release_element_80(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[80]));
}
static bool torb_machine_equal_element_80(const void *first, const void *second) {
  return torb_machine_equal_slot(80, first, second);
}
static uint64_t torb_machine_hash_element_80(const void *value) {
  return torb_machine_hash_slot(80, value);
}
static void torb_machine_retain_element_81(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[81]));
}
static void torb_machine_release_element_81(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[81]));
}
static bool torb_machine_equal_element_81(const void *first, const void *second) {
  return torb_machine_equal_slot(81, first, second);
}
static uint64_t torb_machine_hash_element_81(const void *value) {
  return torb_machine_hash_slot(81, value);
}
static void torb_machine_retain_element_82(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[82]));
}
static void torb_machine_release_element_82(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[82]));
}
static bool torb_machine_equal_element_82(const void *first, const void *second) {
  return torb_machine_equal_slot(82, first, second);
}
static uint64_t torb_machine_hash_element_82(const void *value) {
  return torb_machine_hash_slot(82, value);
}
static void torb_machine_retain_element_83(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[83]));
}
static void torb_machine_release_element_83(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[83]));
}
static bool torb_machine_equal_element_83(const void *first, const void *second) {
  return torb_machine_equal_slot(83, first, second);
}
static uint64_t torb_machine_hash_element_83(const void *value) {
  return torb_machine_hash_slot(83, value);
}
static void torb_machine_retain_element_84(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[84]));
}
static void torb_machine_release_element_84(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[84]));
}
static bool torb_machine_equal_element_84(const void *first, const void *second) {
  return torb_machine_equal_slot(84, first, second);
}
static uint64_t torb_machine_hash_element_84(const void *value) {
  return torb_machine_hash_slot(84, value);
}
static void torb_machine_retain_element_85(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[85]));
}
static void torb_machine_release_element_85(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[85]));
}
static bool torb_machine_equal_element_85(const void *first, const void *second) {
  return torb_machine_equal_slot(85, first, second);
}
static uint64_t torb_machine_hash_element_85(const void *value) {
  return torb_machine_hash_slot(85, value);
}
static void torb_machine_retain_element_86(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[86]));
}
static void torb_machine_release_element_86(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[86]));
}
static bool torb_machine_equal_element_86(const void *first, const void *second) {
  return torb_machine_equal_slot(86, first, second);
}
static uint64_t torb_machine_hash_element_86(const void *value) {
  return torb_machine_hash_slot(86, value);
}
static void torb_machine_retain_element_87(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[87]));
}
static void torb_machine_release_element_87(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[87]));
}
static bool torb_machine_equal_element_87(const void *first, const void *second) {
  return torb_machine_equal_slot(87, first, second);
}
static uint64_t torb_machine_hash_element_87(const void *value) {
  return torb_machine_hash_slot(87, value);
}
static void torb_machine_retain_element_88(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[88]));
}
static void torb_machine_release_element_88(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[88]));
}
static bool torb_machine_equal_element_88(const void *first, const void *second) {
  return torb_machine_equal_slot(88, first, second);
}
static uint64_t torb_machine_hash_element_88(const void *value) {
  return torb_machine_hash_slot(88, value);
}
static void torb_machine_retain_element_89(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[89]));
}
static void torb_machine_release_element_89(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[89]));
}
static bool torb_machine_equal_element_89(const void *first, const void *second) {
  return torb_machine_equal_slot(89, first, second);
}
static uint64_t torb_machine_hash_element_89(const void *value) {
  return torb_machine_hash_slot(89, value);
}
static void torb_machine_retain_element_90(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[90]));
}
static void torb_machine_release_element_90(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[90]));
}
static bool torb_machine_equal_element_90(const void *first, const void *second) {
  return torb_machine_equal_slot(90, first, second);
}
static uint64_t torb_machine_hash_element_90(const void *value) {
  return torb_machine_hash_slot(90, value);
}
static void torb_machine_retain_element_91(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[91]));
}
static void torb_machine_release_element_91(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[91]));
}
static bool torb_machine_equal_element_91(const void *first, const void *second) {
  return torb_machine_equal_slot(91, first, second);
}
static uint64_t torb_machine_hash_element_91(const void *value) {
  return torb_machine_hash_slot(91, value);
}
static void torb_machine_retain_element_92(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[92]));
}
static void torb_machine_release_element_92(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[92]));
}
static bool torb_machine_equal_element_92(const void *first, const void *second) {
  return torb_machine_equal_slot(92, first, second);
}
static uint64_t torb_machine_hash_element_92(const void *value) {
  return torb_machine_hash_slot(92, value);
}
static void torb_machine_retain_element_93(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[93]));
}
static void torb_machine_release_element_93(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[93]));
}
static bool torb_machine_equal_element_93(const void *first, const void *second) {
  return torb_machine_equal_slot(93, first, second);
}
static uint64_t torb_machine_hash_element_93(const void *value) {
  return torb_machine_hash_slot(93, value);
}
static void torb_machine_retain_element_94(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[94]));
}
static void torb_machine_release_element_94(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[94]));
}
static bool torb_machine_equal_element_94(const void *first, const void *second) {
  return torb_machine_equal_slot(94, first, second);
}
static uint64_t torb_machine_hash_element_94(const void *value) {
  return torb_machine_hash_slot(94, value);
}
static void torb_machine_retain_element_95(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[95]));
}
static void torb_machine_release_element_95(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[95]));
}
static bool torb_machine_equal_element_95(const void *first, const void *second) {
  return torb_machine_equal_slot(95, first, second);
}
static uint64_t torb_machine_hash_element_95(const void *value) {
  return torb_machine_hash_slot(95, value);
}
static void torb_machine_retain_element_96(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[96]));
}
static void torb_machine_release_element_96(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[96]));
}
static bool torb_machine_equal_element_96(const void *first, const void *second) {
  return torb_machine_equal_slot(96, first, second);
}
static uint64_t torb_machine_hash_element_96(const void *value) {
  return torb_machine_hash_slot(96, value);
}
static void torb_machine_retain_element_97(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[97]));
}
static void torb_machine_release_element_97(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[97]));
}
static bool torb_machine_equal_element_97(const void *first, const void *second) {
  return torb_machine_equal_slot(97, first, second);
}
static uint64_t torb_machine_hash_element_97(const void *value) {
  return torb_machine_hash_slot(97, value);
}
static void torb_machine_retain_element_98(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[98]));
}
static void torb_machine_release_element_98(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[98]));
}
static bool torb_machine_equal_element_98(const void *first, const void *second) {
  return torb_machine_equal_slot(98, first, second);
}
static uint64_t torb_machine_hash_element_98(const void *value) {
  return torb_machine_hash_slot(98, value);
}
static void torb_machine_retain_element_99(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[99]));
}
static void torb_machine_release_element_99(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[99]));
}
static bool torb_machine_equal_element_99(const void *first, const void *second) {
  return torb_machine_equal_slot(99, first, second);
}
static uint64_t torb_machine_hash_element_99(const void *value) {
  return torb_machine_hash_slot(99, value);
}
static void torb_machine_retain_element_100(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[100]));
}
static void torb_machine_release_element_100(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[100]));
}
static bool torb_machine_equal_element_100(const void *first, const void *second) {
  return torb_machine_equal_slot(100, first, second);
}
static uint64_t torb_machine_hash_element_100(const void *value) {
  return torb_machine_hash_slot(100, value);
}
static void torb_machine_retain_element_101(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[101]));
}
static void torb_machine_release_element_101(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[101]));
}
static bool torb_machine_equal_element_101(const void *first, const void *second) {
  return torb_machine_equal_slot(101, first, second);
}
static uint64_t torb_machine_hash_element_101(const void *value) {
  return torb_machine_hash_slot(101, value);
}
static void torb_machine_retain_element_102(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[102]));
}
static void torb_machine_release_element_102(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[102]));
}
static bool torb_machine_equal_element_102(const void *first, const void *second) {
  return torb_machine_equal_slot(102, first, second);
}
static uint64_t torb_machine_hash_element_102(const void *value) {
  return torb_machine_hash_slot(102, value);
}
static void torb_machine_retain_element_103(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[103]));
}
static void torb_machine_release_element_103(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[103]));
}
static bool torb_machine_equal_element_103(const void *first, const void *second) {
  return torb_machine_equal_slot(103, first, second);
}
static uint64_t torb_machine_hash_element_103(const void *value) {
  return torb_machine_hash_slot(103, value);
}
static void torb_machine_retain_element_104(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[104]));
}
static void torb_machine_release_element_104(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[104]));
}
static bool torb_machine_equal_element_104(const void *first, const void *second) {
  return torb_machine_equal_slot(104, first, second);
}
static uint64_t torb_machine_hash_element_104(const void *value) {
  return torb_machine_hash_slot(104, value);
}
static void torb_machine_retain_element_105(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[105]));
}
static void torb_machine_release_element_105(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[105]));
}
static bool torb_machine_equal_element_105(const void *first, const void *second) {
  return torb_machine_equal_slot(105, first, second);
}
static uint64_t torb_machine_hash_element_105(const void *value) {
  return torb_machine_hash_slot(105, value);
}
static void torb_machine_retain_element_106(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[106]));
}
static void torb_machine_release_element_106(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[106]));
}
static bool torb_machine_equal_element_106(const void *first, const void *second) {
  return torb_machine_equal_slot(106, first, second);
}
static uint64_t torb_machine_hash_element_106(const void *value) {
  return torb_machine_hash_slot(106, value);
}
static void torb_machine_retain_element_107(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[107]));
}
static void torb_machine_release_element_107(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[107]));
}
static bool torb_machine_equal_element_107(const void *first, const void *second) {
  return torb_machine_equal_slot(107, first, second);
}
static uint64_t torb_machine_hash_element_107(const void *value) {
  return torb_machine_hash_slot(107, value);
}
static void torb_machine_retain_element_108(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[108]));
}
static void torb_machine_release_element_108(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[108]));
}
static bool torb_machine_equal_element_108(const void *first, const void *second) {
  return torb_machine_equal_slot(108, first, second);
}
static uint64_t torb_machine_hash_element_108(const void *value) {
  return torb_machine_hash_slot(108, value);
}
static void torb_machine_retain_element_109(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[109]));
}
static void torb_machine_release_element_109(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[109]));
}
static bool torb_machine_equal_element_109(const void *first, const void *second) {
  return torb_machine_equal_slot(109, first, second);
}
static uint64_t torb_machine_hash_element_109(const void *value) {
  return torb_machine_hash_slot(109, value);
}
static void torb_machine_retain_element_110(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[110]));
}
static void torb_machine_release_element_110(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[110]));
}
static bool torb_machine_equal_element_110(const void *first, const void *second) {
  return torb_machine_equal_slot(110, first, second);
}
static uint64_t torb_machine_hash_element_110(const void *value) {
  return torb_machine_hash_slot(110, value);
}
static void torb_machine_retain_element_111(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[111]));
}
static void torb_machine_release_element_111(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[111]));
}
static bool torb_machine_equal_element_111(const void *first, const void *second) {
  return torb_machine_equal_slot(111, first, second);
}
static uint64_t torb_machine_hash_element_111(const void *value) {
  return torb_machine_hash_slot(111, value);
}
static void torb_machine_retain_element_112(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[112]));
}
static void torb_machine_release_element_112(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[112]));
}
static bool torb_machine_equal_element_112(const void *first, const void *second) {
  return torb_machine_equal_slot(112, first, second);
}
static uint64_t torb_machine_hash_element_112(const void *value) {
  return torb_machine_hash_slot(112, value);
}
static void torb_machine_retain_element_113(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[113]));
}
static void torb_machine_release_element_113(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[113]));
}
static bool torb_machine_equal_element_113(const void *first, const void *second) {
  return torb_machine_equal_slot(113, first, second);
}
static uint64_t torb_machine_hash_element_113(const void *value) {
  return torb_machine_hash_slot(113, value);
}
static void torb_machine_retain_element_114(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[114]));
}
static void torb_machine_release_element_114(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[114]));
}
static bool torb_machine_equal_element_114(const void *first, const void *second) {
  return torb_machine_equal_slot(114, first, second);
}
static uint64_t torb_machine_hash_element_114(const void *value) {
  return torb_machine_hash_slot(114, value);
}
static void torb_machine_retain_element_115(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[115]));
}
static void torb_machine_release_element_115(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[115]));
}
static bool torb_machine_equal_element_115(const void *first, const void *second) {
  return torb_machine_equal_slot(115, first, second);
}
static uint64_t torb_machine_hash_element_115(const void *value) {
  return torb_machine_hash_slot(115, value);
}
static void torb_machine_retain_element_116(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[116]));
}
static void torb_machine_release_element_116(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[116]));
}
static bool torb_machine_equal_element_116(const void *first, const void *second) {
  return torb_machine_equal_slot(116, first, second);
}
static uint64_t torb_machine_hash_element_116(const void *value) {
  return torb_machine_hash_slot(116, value);
}
static void torb_machine_retain_element_117(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[117]));
}
static void torb_machine_release_element_117(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[117]));
}
static bool torb_machine_equal_element_117(const void *first, const void *second) {
  return torb_machine_equal_slot(117, first, second);
}
static uint64_t torb_machine_hash_element_117(const void *value) {
  return torb_machine_hash_slot(117, value);
}
static void torb_machine_retain_element_118(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[118]));
}
static void torb_machine_release_element_118(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[118]));
}
static bool torb_machine_equal_element_118(const void *first, const void *second) {
  return torb_machine_equal_slot(118, first, second);
}
static uint64_t torb_machine_hash_element_118(const void *value) {
  return torb_machine_hash_slot(118, value);
}
static void torb_machine_retain_element_119(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[119]));
}
static void torb_machine_release_element_119(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[119]));
}
static bool torb_machine_equal_element_119(const void *first, const void *second) {
  return torb_machine_equal_slot(119, first, second);
}
static uint64_t torb_machine_hash_element_119(const void *value) {
  return torb_machine_hash_slot(119, value);
}
static void torb_machine_retain_element_120(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[120]));
}
static void torb_machine_release_element_120(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[120]));
}
static bool torb_machine_equal_element_120(const void *first, const void *second) {
  return torb_machine_equal_slot(120, first, second);
}
static uint64_t torb_machine_hash_element_120(const void *value) {
  return torb_machine_hash_slot(120, value);
}
static void torb_machine_retain_element_121(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[121]));
}
static void torb_machine_release_element_121(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[121]));
}
static bool torb_machine_equal_element_121(const void *first, const void *second) {
  return torb_machine_equal_slot(121, first, second);
}
static uint64_t torb_machine_hash_element_121(const void *value) {
  return torb_machine_hash_slot(121, value);
}
static void torb_machine_retain_element_122(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[122]));
}
static void torb_machine_release_element_122(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[122]));
}
static bool torb_machine_equal_element_122(const void *first, const void *second) {
  return torb_machine_equal_slot(122, first, second);
}
static uint64_t torb_machine_hash_element_122(const void *value) {
  return torb_machine_hash_slot(122, value);
}
static void torb_machine_retain_element_123(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[123]));
}
static void torb_machine_release_element_123(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[123]));
}
static bool torb_machine_equal_element_123(const void *first, const void *second) {
  return torb_machine_equal_slot(123, first, second);
}
static uint64_t torb_machine_hash_element_123(const void *value) {
  return torb_machine_hash_slot(123, value);
}
static void torb_machine_retain_element_124(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[124]));
}
static void torb_machine_release_element_124(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[124]));
}
static bool torb_machine_equal_element_124(const void *first, const void *second) {
  return torb_machine_equal_slot(124, first, second);
}
static uint64_t torb_machine_hash_element_124(const void *value) {
  return torb_machine_hash_slot(124, value);
}
static void torb_machine_retain_element_125(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[125]));
}
static void torb_machine_release_element_125(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[125]));
}
static bool torb_machine_equal_element_125(const void *first, const void *second) {
  return torb_machine_equal_slot(125, first, second);
}
static uint64_t torb_machine_hash_element_125(const void *value) {
  return torb_machine_hash_slot(125, value);
}
static void torb_machine_retain_element_126(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[126]));
}
static void torb_machine_release_element_126(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[126]));
}
static bool torb_machine_equal_element_126(const void *first, const void *second) {
  return torb_machine_equal_slot(126, first, second);
}
static uint64_t torb_machine_hash_element_126(const void *value) {
  return torb_machine_hash_slot(126, value);
}
static void torb_machine_retain_element_127(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[127]));
}
static void torb_machine_release_element_127(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[127]));
}
static bool torb_machine_equal_element_127(const void *first, const void *second) {
  return torb_machine_equal_slot(127, first, second);
}
static uint64_t torb_machine_hash_element_127(const void *value) {
  return torb_machine_hash_slot(127, value);
}
static void torb_machine_retain_element_128(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[128]));
}
static void torb_machine_release_element_128(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[128]));
}
static bool torb_machine_equal_element_128(const void *first, const void *second) {
  return torb_machine_equal_slot(128, first, second);
}
static uint64_t torb_machine_hash_element_128(const void *value) {
  return torb_machine_hash_slot(128, value);
}
static void torb_machine_retain_element_129(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[129]));
}
static void torb_machine_release_element_129(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[129]));
}
static bool torb_machine_equal_element_129(const void *first, const void *second) {
  return torb_machine_equal_slot(129, first, second);
}
static uint64_t torb_machine_hash_element_129(const void *value) {
  return torb_machine_hash_slot(129, value);
}
static void torb_machine_retain_element_130(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[130]));
}
static void torb_machine_release_element_130(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[130]));
}
static bool torb_machine_equal_element_130(const void *first, const void *second) {
  return torb_machine_equal_slot(130, first, second);
}
static uint64_t torb_machine_hash_element_130(const void *value) {
  return torb_machine_hash_slot(130, value);
}
static void torb_machine_retain_element_131(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[131]));
}
static void torb_machine_release_element_131(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[131]));
}
static bool torb_machine_equal_element_131(const void *first, const void *second) {
  return torb_machine_equal_slot(131, first, second);
}
static uint64_t torb_machine_hash_element_131(const void *value) {
  return torb_machine_hash_slot(131, value);
}
static void torb_machine_retain_element_132(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[132]));
}
static void torb_machine_release_element_132(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[132]));
}
static bool torb_machine_equal_element_132(const void *first, const void *second) {
  return torb_machine_equal_slot(132, first, second);
}
static uint64_t torb_machine_hash_element_132(const void *value) {
  return torb_machine_hash_slot(132, value);
}
static void torb_machine_retain_element_133(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[133]));
}
static void torb_machine_release_element_133(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[133]));
}
static bool torb_machine_equal_element_133(const void *first, const void *second) {
  return torb_machine_equal_slot(133, first, second);
}
static uint64_t torb_machine_hash_element_133(const void *value) {
  return torb_machine_hash_slot(133, value);
}
static void torb_machine_retain_element_134(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[134]));
}
static void torb_machine_release_element_134(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[134]));
}
static bool torb_machine_equal_element_134(const void *first, const void *second) {
  return torb_machine_equal_slot(134, first, second);
}
static uint64_t torb_machine_hash_element_134(const void *value) {
  return torb_machine_hash_slot(134, value);
}
static void torb_machine_retain_element_135(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[135]));
}
static void torb_machine_release_element_135(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[135]));
}
static bool torb_machine_equal_element_135(const void *first, const void *second) {
  return torb_machine_equal_slot(135, first, second);
}
static uint64_t torb_machine_hash_element_135(const void *value) {
  return torb_machine_hash_slot(135, value);
}
static void torb_machine_retain_element_136(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[136]));
}
static void torb_machine_release_element_136(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[136]));
}
static bool torb_machine_equal_element_136(const void *first, const void *second) {
  return torb_machine_equal_slot(136, first, second);
}
static uint64_t torb_machine_hash_element_136(const void *value) {
  return torb_machine_hash_slot(136, value);
}
static void torb_machine_retain_element_137(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[137]));
}
static void torb_machine_release_element_137(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[137]));
}
static bool torb_machine_equal_element_137(const void *first, const void *second) {
  return torb_machine_equal_slot(137, first, second);
}
static uint64_t torb_machine_hash_element_137(const void *value) {
  return torb_machine_hash_slot(137, value);
}
static void torb_machine_retain_element_138(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[138]));
}
static void torb_machine_release_element_138(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[138]));
}
static bool torb_machine_equal_element_138(const void *first, const void *second) {
  return torb_machine_equal_slot(138, first, second);
}
static uint64_t torb_machine_hash_element_138(const void *value) {
  return torb_machine_hash_slot(138, value);
}
static void torb_machine_retain_element_139(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[139]));
}
static void torb_machine_release_element_139(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[139]));
}
static bool torb_machine_equal_element_139(const void *first, const void *second) {
  return torb_machine_equal_slot(139, first, second);
}
static uint64_t torb_machine_hash_element_139(const void *value) {
  return torb_machine_hash_slot(139, value);
}
static void torb_machine_retain_element_140(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[140]));
}
static void torb_machine_release_element_140(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[140]));
}
static bool torb_machine_equal_element_140(const void *first, const void *second) {
  return torb_machine_equal_slot(140, first, second);
}
static uint64_t torb_machine_hash_element_140(const void *value) {
  return torb_machine_hash_slot(140, value);
}
static void torb_machine_retain_element_141(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[141]));
}
static void torb_machine_release_element_141(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[141]));
}
static bool torb_machine_equal_element_141(const void *first, const void *second) {
  return torb_machine_equal_slot(141, first, second);
}
static uint64_t torb_machine_hash_element_141(const void *value) {
  return torb_machine_hash_slot(141, value);
}
static void torb_machine_retain_element_142(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[142]));
}
static void torb_machine_release_element_142(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[142]));
}
static bool torb_machine_equal_element_142(const void *first, const void *second) {
  return torb_machine_equal_slot(142, first, second);
}
static uint64_t torb_machine_hash_element_142(const void *value) {
  return torb_machine_hash_slot(142, value);
}
static void torb_machine_retain_element_143(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[143]));
}
static void torb_machine_release_element_143(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[143]));
}
static bool torb_machine_equal_element_143(const void *first, const void *second) {
  return torb_machine_equal_slot(143, first, second);
}
static uint64_t torb_machine_hash_element_143(const void *value) {
  return torb_machine_hash_slot(143, value);
}
static void torb_machine_retain_element_144(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[144]));
}
static void torb_machine_release_element_144(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[144]));
}
static bool torb_machine_equal_element_144(const void *first, const void *second) {
  return torb_machine_equal_slot(144, first, second);
}
static uint64_t torb_machine_hash_element_144(const void *value) {
  return torb_machine_hash_slot(144, value);
}
static void torb_machine_retain_element_145(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[145]));
}
static void torb_machine_release_element_145(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[145]));
}
static bool torb_machine_equal_element_145(const void *first, const void *second) {
  return torb_machine_equal_slot(145, first, second);
}
static uint64_t torb_machine_hash_element_145(const void *value) {
  return torb_machine_hash_slot(145, value);
}
static void torb_machine_retain_element_146(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[146]));
}
static void torb_machine_release_element_146(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[146]));
}
static bool torb_machine_equal_element_146(const void *first, const void *second) {
  return torb_machine_equal_slot(146, first, second);
}
static uint64_t torb_machine_hash_element_146(const void *value) {
  return torb_machine_hash_slot(146, value);
}
static void torb_machine_retain_element_147(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[147]));
}
static void torb_machine_release_element_147(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[147]));
}
static bool torb_machine_equal_element_147(const void *first, const void *second) {
  return torb_machine_equal_slot(147, first, second);
}
static uint64_t torb_machine_hash_element_147(const void *value) {
  return torb_machine_hash_slot(147, value);
}
static void torb_machine_retain_element_148(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[148]));
}
static void torb_machine_release_element_148(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[148]));
}
static bool torb_machine_equal_element_148(const void *first, const void *second) {
  return torb_machine_equal_slot(148, first, second);
}
static uint64_t torb_machine_hash_element_148(const void *value) {
  return torb_machine_hash_slot(148, value);
}
static void torb_machine_retain_element_149(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[149]));
}
static void torb_machine_release_element_149(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[149]));
}
static bool torb_machine_equal_element_149(const void *first, const void *second) {
  return torb_machine_equal_slot(149, first, second);
}
static uint64_t torb_machine_hash_element_149(const void *value) {
  return torb_machine_hash_slot(149, value);
}
static void torb_machine_retain_element_150(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[150]));
}
static void torb_machine_release_element_150(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[150]));
}
static bool torb_machine_equal_element_150(const void *first, const void *second) {
  return torb_machine_equal_slot(150, first, second);
}
static uint64_t torb_machine_hash_element_150(const void *value) {
  return torb_machine_hash_slot(150, value);
}
static void torb_machine_retain_element_151(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[151]));
}
static void torb_machine_release_element_151(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[151]));
}
static bool torb_machine_equal_element_151(const void *first, const void *second) {
  return torb_machine_equal_slot(151, first, second);
}
static uint64_t torb_machine_hash_element_151(const void *value) {
  return torb_machine_hash_slot(151, value);
}
static void torb_machine_retain_element_152(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[152]));
}
static void torb_machine_release_element_152(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[152]));
}
static bool torb_machine_equal_element_152(const void *first, const void *second) {
  return torb_machine_equal_slot(152, first, second);
}
static uint64_t torb_machine_hash_element_152(const void *value) {
  return torb_machine_hash_slot(152, value);
}
static void torb_machine_retain_element_153(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[153]));
}
static void torb_machine_release_element_153(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[153]));
}
static bool torb_machine_equal_element_153(const void *first, const void *second) {
  return torb_machine_equal_slot(153, first, second);
}
static uint64_t torb_machine_hash_element_153(const void *value) {
  return torb_machine_hash_slot(153, value);
}
static void torb_machine_retain_element_154(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[154]));
}
static void torb_machine_release_element_154(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[154]));
}
static bool torb_machine_equal_element_154(const void *first, const void *second) {
  return torb_machine_equal_slot(154, first, second);
}
static uint64_t torb_machine_hash_element_154(const void *value) {
  return torb_machine_hash_slot(154, value);
}
static void torb_machine_retain_element_155(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[155]));
}
static void torb_machine_release_element_155(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[155]));
}
static bool torb_machine_equal_element_155(const void *first, const void *second) {
  return torb_machine_equal_slot(155, first, second);
}
static uint64_t torb_machine_hash_element_155(const void *value) {
  return torb_machine_hash_slot(155, value);
}
static void torb_machine_retain_element_156(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[156]));
}
static void torb_machine_release_element_156(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[156]));
}
static bool torb_machine_equal_element_156(const void *first, const void *second) {
  return torb_machine_equal_slot(156, first, second);
}
static uint64_t torb_machine_hash_element_156(const void *value) {
  return torb_machine_hash_slot(156, value);
}
static void torb_machine_retain_element_157(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[157]));
}
static void torb_machine_release_element_157(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[157]));
}
static bool torb_machine_equal_element_157(const void *first, const void *second) {
  return torb_machine_equal_slot(157, first, second);
}
static uint64_t torb_machine_hash_element_157(const void *value) {
  return torb_machine_hash_slot(157, value);
}
static void torb_machine_retain_element_158(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[158]));
}
static void torb_machine_release_element_158(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[158]));
}
static bool torb_machine_equal_element_158(const void *first, const void *second) {
  return torb_machine_equal_slot(158, first, second);
}
static uint64_t torb_machine_hash_element_158(const void *value) {
  return torb_machine_hash_slot(158, value);
}
static void torb_machine_retain_element_159(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[159]));
}
static void torb_machine_release_element_159(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[159]));
}
static bool torb_machine_equal_element_159(const void *first, const void *second) {
  return torb_machine_equal_slot(159, first, second);
}
static uint64_t torb_machine_hash_element_159(const void *value) {
  return torb_machine_hash_slot(159, value);
}
static void torb_machine_retain_element_160(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[160]));
}
static void torb_machine_release_element_160(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[160]));
}
static bool torb_machine_equal_element_160(const void *first, const void *second) {
  return torb_machine_equal_slot(160, first, second);
}
static uint64_t torb_machine_hash_element_160(const void *value) {
  return torb_machine_hash_slot(160, value);
}
static void torb_machine_retain_element_161(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[161]));
}
static void torb_machine_release_element_161(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[161]));
}
static bool torb_machine_equal_element_161(const void *first, const void *second) {
  return torb_machine_equal_slot(161, first, second);
}
static uint64_t torb_machine_hash_element_161(const void *value) {
  return torb_machine_hash_slot(161, value);
}
static void torb_machine_retain_element_162(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[162]));
}
static void torb_machine_release_element_162(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[162]));
}
static bool torb_machine_equal_element_162(const void *first, const void *second) {
  return torb_machine_equal_slot(162, first, second);
}
static uint64_t torb_machine_hash_element_162(const void *value) {
  return torb_machine_hash_slot(162, value);
}
static void torb_machine_retain_element_163(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[163]));
}
static void torb_machine_release_element_163(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[163]));
}
static bool torb_machine_equal_element_163(const void *first, const void *second) {
  return torb_machine_equal_slot(163, first, second);
}
static uint64_t torb_machine_hash_element_163(const void *value) {
  return torb_machine_hash_slot(163, value);
}
static void torb_machine_retain_element_164(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[164]));
}
static void torb_machine_release_element_164(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[164]));
}
static bool torb_machine_equal_element_164(const void *first, const void *second) {
  return torb_machine_equal_slot(164, first, second);
}
static uint64_t torb_machine_hash_element_164(const void *value) {
  return torb_machine_hash_slot(164, value);
}
static void torb_machine_retain_element_165(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[165]));
}
static void torb_machine_release_element_165(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[165]));
}
static bool torb_machine_equal_element_165(const void *first, const void *second) {
  return torb_machine_equal_slot(165, first, second);
}
static uint64_t torb_machine_hash_element_165(const void *value) {
  return torb_machine_hash_slot(165, value);
}
static void torb_machine_retain_element_166(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[166]));
}
static void torb_machine_release_element_166(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[166]));
}
static bool torb_machine_equal_element_166(const void *first, const void *second) {
  return torb_machine_equal_slot(166, first, second);
}
static uint64_t torb_machine_hash_element_166(const void *value) {
  return torb_machine_hash_slot(166, value);
}
static void torb_machine_retain_element_167(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[167]));
}
static void torb_machine_release_element_167(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[167]));
}
static bool torb_machine_equal_element_167(const void *first, const void *second) {
  return torb_machine_equal_slot(167, first, second);
}
static uint64_t torb_machine_hash_element_167(const void *value) {
  return torb_machine_hash_slot(167, value);
}
static void torb_machine_retain_element_168(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[168]));
}
static void torb_machine_release_element_168(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[168]));
}
static bool torb_machine_equal_element_168(const void *first, const void *second) {
  return torb_machine_equal_slot(168, first, second);
}
static uint64_t torb_machine_hash_element_168(const void *value) {
  return torb_machine_hash_slot(168, value);
}
static void torb_machine_retain_element_169(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[169]));
}
static void torb_machine_release_element_169(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[169]));
}
static bool torb_machine_equal_element_169(const void *first, const void *second) {
  return torb_machine_equal_slot(169, first, second);
}
static uint64_t torb_machine_hash_element_169(const void *value) {
  return torb_machine_hash_slot(169, value);
}
static void torb_machine_retain_element_170(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[170]));
}
static void torb_machine_release_element_170(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[170]));
}
static bool torb_machine_equal_element_170(const void *first, const void *second) {
  return torb_machine_equal_slot(170, first, second);
}
static uint64_t torb_machine_hash_element_170(const void *value) {
  return torb_machine_hash_slot(170, value);
}
static void torb_machine_retain_element_171(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[171]));
}
static void torb_machine_release_element_171(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[171]));
}
static bool torb_machine_equal_element_171(const void *first, const void *second) {
  return torb_machine_equal_slot(171, first, second);
}
static uint64_t torb_machine_hash_element_171(const void *value) {
  return torb_machine_hash_slot(171, value);
}
static void torb_machine_retain_element_172(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[172]));
}
static void torb_machine_release_element_172(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[172]));
}
static bool torb_machine_equal_element_172(const void *first, const void *second) {
  return torb_machine_equal_slot(172, first, second);
}
static uint64_t torb_machine_hash_element_172(const void *value) {
  return torb_machine_hash_slot(172, value);
}
static void torb_machine_retain_element_173(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[173]));
}
static void torb_machine_release_element_173(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[173]));
}
static bool torb_machine_equal_element_173(const void *first, const void *second) {
  return torb_machine_equal_slot(173, first, second);
}
static uint64_t torb_machine_hash_element_173(const void *value) {
  return torb_machine_hash_slot(173, value);
}
static void torb_machine_retain_element_174(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[174]));
}
static void torb_machine_release_element_174(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[174]));
}
static bool torb_machine_equal_element_174(const void *first, const void *second) {
  return torb_machine_equal_slot(174, first, second);
}
static uint64_t torb_machine_hash_element_174(const void *value) {
  return torb_machine_hash_slot(174, value);
}
static void torb_machine_retain_element_175(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[175]));
}
static void torb_machine_release_element_175(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[175]));
}
static bool torb_machine_equal_element_175(const void *first, const void *second) {
  return torb_machine_equal_slot(175, first, second);
}
static uint64_t torb_machine_hash_element_175(const void *value) {
  return torb_machine_hash_slot(175, value);
}
static void torb_machine_retain_element_176(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[176]));
}
static void torb_machine_release_element_176(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[176]));
}
static bool torb_machine_equal_element_176(const void *first, const void *second) {
  return torb_machine_equal_slot(176, first, second);
}
static uint64_t torb_machine_hash_element_176(const void *value) {
  return torb_machine_hash_slot(176, value);
}
static void torb_machine_retain_element_177(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[177]));
}
static void torb_machine_release_element_177(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[177]));
}
static bool torb_machine_equal_element_177(const void *first, const void *second) {
  return torb_machine_equal_slot(177, first, second);
}
static uint64_t torb_machine_hash_element_177(const void *value) {
  return torb_machine_hash_slot(177, value);
}
static void torb_machine_retain_element_178(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[178]));
}
static void torb_machine_release_element_178(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[178]));
}
static bool torb_machine_equal_element_178(const void *first, const void *second) {
  return torb_machine_equal_slot(178, first, second);
}
static uint64_t torb_machine_hash_element_178(const void *value) {
  return torb_machine_hash_slot(178, value);
}
static void torb_machine_retain_element_179(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[179]));
}
static void torb_machine_release_element_179(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[179]));
}
static bool torb_machine_equal_element_179(const void *first, const void *second) {
  return torb_machine_equal_slot(179, first, second);
}
static uint64_t torb_machine_hash_element_179(const void *value) {
  return torb_machine_hash_slot(179, value);
}
static void torb_machine_retain_element_180(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[180]));
}
static void torb_machine_release_element_180(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[180]));
}
static bool torb_machine_equal_element_180(const void *first, const void *second) {
  return torb_machine_equal_slot(180, first, second);
}
static uint64_t torb_machine_hash_element_180(const void *value) {
  return torb_machine_hash_slot(180, value);
}
static void torb_machine_retain_element_181(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[181]));
}
static void torb_machine_release_element_181(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[181]));
}
static bool torb_machine_equal_element_181(const void *first, const void *second) {
  return torb_machine_equal_slot(181, first, second);
}
static uint64_t torb_machine_hash_element_181(const void *value) {
  return torb_machine_hash_slot(181, value);
}
static void torb_machine_retain_element_182(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[182]));
}
static void torb_machine_release_element_182(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[182]));
}
static bool torb_machine_equal_element_182(const void *first, const void *second) {
  return torb_machine_equal_slot(182, first, second);
}
static uint64_t torb_machine_hash_element_182(const void *value) {
  return torb_machine_hash_slot(182, value);
}
static void torb_machine_retain_element_183(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[183]));
}
static void torb_machine_release_element_183(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[183]));
}
static bool torb_machine_equal_element_183(const void *first, const void *second) {
  return torb_machine_equal_slot(183, first, second);
}
static uint64_t torb_machine_hash_element_183(const void *value) {
  return torb_machine_hash_slot(183, value);
}
static void torb_machine_retain_element_184(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[184]));
}
static void torb_machine_release_element_184(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[184]));
}
static bool torb_machine_equal_element_184(const void *first, const void *second) {
  return torb_machine_equal_slot(184, first, second);
}
static uint64_t torb_machine_hash_element_184(const void *value) {
  return torb_machine_hash_slot(184, value);
}
static void torb_machine_retain_element_185(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[185]));
}
static void torb_machine_release_element_185(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[185]));
}
static bool torb_machine_equal_element_185(const void *first, const void *second) {
  return torb_machine_equal_slot(185, first, second);
}
static uint64_t torb_machine_hash_element_185(const void *value) {
  return torb_machine_hash_slot(185, value);
}
static void torb_machine_retain_element_186(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[186]));
}
static void torb_machine_release_element_186(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[186]));
}
static bool torb_machine_equal_element_186(const void *first, const void *second) {
  return torb_machine_equal_slot(186, first, second);
}
static uint64_t torb_machine_hash_element_186(const void *value) {
  return torb_machine_hash_slot(186, value);
}
static void torb_machine_retain_element_187(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[187]));
}
static void torb_machine_release_element_187(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[187]));
}
static bool torb_machine_equal_element_187(const void *first, const void *second) {
  return torb_machine_equal_slot(187, first, second);
}
static uint64_t torb_machine_hash_element_187(const void *value) {
  return torb_machine_hash_slot(187, value);
}
static void torb_machine_retain_element_188(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[188]));
}
static void torb_machine_release_element_188(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[188]));
}
static bool torb_machine_equal_element_188(const void *first, const void *second) {
  return torb_machine_equal_slot(188, first, second);
}
static uint64_t torb_machine_hash_element_188(const void *value) {
  return torb_machine_hash_slot(188, value);
}
static void torb_machine_retain_element_189(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[189]));
}
static void torb_machine_release_element_189(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[189]));
}
static bool torb_machine_equal_element_189(const void *first, const void *second) {
  return torb_machine_equal_slot(189, first, second);
}
static uint64_t torb_machine_hash_element_189(const void *value) {
  return torb_machine_hash_slot(189, value);
}
static void torb_machine_retain_element_190(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[190]));
}
static void torb_machine_release_element_190(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[190]));
}
static bool torb_machine_equal_element_190(const void *first, const void *second) {
  return torb_machine_equal_slot(190, first, second);
}
static uint64_t torb_machine_hash_element_190(const void *value) {
  return torb_machine_hash_slot(190, value);
}
static void torb_machine_retain_element_191(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[191]));
}
static void torb_machine_release_element_191(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[191]));
}
static bool torb_machine_equal_element_191(const void *first, const void *second) {
  return torb_machine_equal_slot(191, first, second);
}
static uint64_t torb_machine_hash_element_191(const void *value) {
  return torb_machine_hash_slot(191, value);
}
static void torb_machine_retain_element_192(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[192]));
}
static void torb_machine_release_element_192(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[192]));
}
static bool torb_machine_equal_element_192(const void *first, const void *second) {
  return torb_machine_equal_slot(192, first, second);
}
static uint64_t torb_machine_hash_element_192(const void *value) {
  return torb_machine_hash_slot(192, value);
}
static void torb_machine_retain_element_193(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[193]));
}
static void torb_machine_release_element_193(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[193]));
}
static bool torb_machine_equal_element_193(const void *first, const void *second) {
  return torb_machine_equal_slot(193, first, second);
}
static uint64_t torb_machine_hash_element_193(const void *value) {
  return torb_machine_hash_slot(193, value);
}
static void torb_machine_retain_element_194(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[194]));
}
static void torb_machine_release_element_194(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[194]));
}
static bool torb_machine_equal_element_194(const void *first, const void *second) {
  return torb_machine_equal_slot(194, first, second);
}
static uint64_t torb_machine_hash_element_194(const void *value) {
  return torb_machine_hash_slot(194, value);
}
static void torb_machine_retain_element_195(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[195]));
}
static void torb_machine_release_element_195(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[195]));
}
static bool torb_machine_equal_element_195(const void *first, const void *second) {
  return torb_machine_equal_slot(195, first, second);
}
static uint64_t torb_machine_hash_element_195(const void *value) {
  return torb_machine_hash_slot(195, value);
}
static void torb_machine_retain_element_196(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[196]));
}
static void torb_machine_release_element_196(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[196]));
}
static bool torb_machine_equal_element_196(const void *first, const void *second) {
  return torb_machine_equal_slot(196, first, second);
}
static uint64_t torb_machine_hash_element_196(const void *value) {
  return torb_machine_hash_slot(196, value);
}
static void torb_machine_retain_element_197(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[197]));
}
static void torb_machine_release_element_197(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[197]));
}
static bool torb_machine_equal_element_197(const void *first, const void *second) {
  return torb_machine_equal_slot(197, first, second);
}
static uint64_t torb_machine_hash_element_197(const void *value) {
  return torb_machine_hash_slot(197, value);
}
static void torb_machine_retain_element_198(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[198]));
}
static void torb_machine_release_element_198(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[198]));
}
static bool torb_machine_equal_element_198(const void *first, const void *second) {
  return torb_machine_equal_slot(198, first, second);
}
static uint64_t torb_machine_hash_element_198(const void *value) {
  return torb_machine_hash_slot(198, value);
}
static void torb_machine_retain_element_199(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[199]));
}
static void torb_machine_release_element_199(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[199]));
}
static bool torb_machine_equal_element_199(const void *first, const void *second) {
  return torb_machine_equal_slot(199, first, second);
}
static uint64_t torb_machine_hash_element_199(const void *value) {
  return torb_machine_hash_slot(199, value);
}
static void torb_machine_retain_element_200(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[200]));
}
static void torb_machine_release_element_200(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[200]));
}
static bool torb_machine_equal_element_200(const void *first, const void *second) {
  return torb_machine_equal_slot(200, first, second);
}
static uint64_t torb_machine_hash_element_200(const void *value) {
  return torb_machine_hash_slot(200, value);
}
static void torb_machine_retain_element_201(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[201]));
}
static void torb_machine_release_element_201(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[201]));
}
static bool torb_machine_equal_element_201(const void *first, const void *second) {
  return torb_machine_equal_slot(201, first, second);
}
static uint64_t torb_machine_hash_element_201(const void *value) {
  return torb_machine_hash_slot(201, value);
}
static void torb_machine_retain_element_202(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[202]));
}
static void torb_machine_release_element_202(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[202]));
}
static bool torb_machine_equal_element_202(const void *first, const void *second) {
  return torb_machine_equal_slot(202, first, second);
}
static uint64_t torb_machine_hash_element_202(const void *value) {
  return torb_machine_hash_slot(202, value);
}
static void torb_machine_retain_element_203(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[203]));
}
static void torb_machine_release_element_203(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[203]));
}
static bool torb_machine_equal_element_203(const void *first, const void *second) {
  return torb_machine_equal_slot(203, first, second);
}
static uint64_t torb_machine_hash_element_203(const void *value) {
  return torb_machine_hash_slot(203, value);
}
static void torb_machine_retain_element_204(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[204]));
}
static void torb_machine_release_element_204(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[204]));
}
static bool torb_machine_equal_element_204(const void *first, const void *second) {
  return torb_machine_equal_slot(204, first, second);
}
static uint64_t torb_machine_hash_element_204(const void *value) {
  return torb_machine_hash_slot(204, value);
}
static void torb_machine_retain_element_205(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[205]));
}
static void torb_machine_release_element_205(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[205]));
}
static bool torb_machine_equal_element_205(const void *first, const void *second) {
  return torb_machine_equal_slot(205, first, second);
}
static uint64_t torb_machine_hash_element_205(const void *value) {
  return torb_machine_hash_slot(205, value);
}
static void torb_machine_retain_element_206(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[206]));
}
static void torb_machine_release_element_206(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[206]));
}
static bool torb_machine_equal_element_206(const void *first, const void *second) {
  return torb_machine_equal_slot(206, first, second);
}
static uint64_t torb_machine_hash_element_206(const void *value) {
  return torb_machine_hash_slot(206, value);
}
static void torb_machine_retain_element_207(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[207]));
}
static void torb_machine_release_element_207(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[207]));
}
static bool torb_machine_equal_element_207(const void *first, const void *second) {
  return torb_machine_equal_slot(207, first, second);
}
static uint64_t torb_machine_hash_element_207(const void *value) {
  return torb_machine_hash_slot(207, value);
}
static void torb_machine_retain_element_208(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[208]));
}
static void torb_machine_release_element_208(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[208]));
}
static bool torb_machine_equal_element_208(const void *first, const void *second) {
  return torb_machine_equal_slot(208, first, second);
}
static uint64_t torb_machine_hash_element_208(const void *value) {
  return torb_machine_hash_slot(208, value);
}
static void torb_machine_retain_element_209(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[209]));
}
static void torb_machine_release_element_209(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[209]));
}
static bool torb_machine_equal_element_209(const void *first, const void *second) {
  return torb_machine_equal_slot(209, first, second);
}
static uint64_t torb_machine_hash_element_209(const void *value) {
  return torb_machine_hash_slot(209, value);
}
static void torb_machine_retain_element_210(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[210]));
}
static void torb_machine_release_element_210(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[210]));
}
static bool torb_machine_equal_element_210(const void *first, const void *second) {
  return torb_machine_equal_slot(210, first, second);
}
static uint64_t torb_machine_hash_element_210(const void *value) {
  return torb_machine_hash_slot(210, value);
}
static void torb_machine_retain_element_211(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[211]));
}
static void torb_machine_release_element_211(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[211]));
}
static bool torb_machine_equal_element_211(const void *first, const void *second) {
  return torb_machine_equal_slot(211, first, second);
}
static uint64_t torb_machine_hash_element_211(const void *value) {
  return torb_machine_hash_slot(211, value);
}
static void torb_machine_retain_element_212(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[212]));
}
static void torb_machine_release_element_212(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[212]));
}
static bool torb_machine_equal_element_212(const void *first, const void *second) {
  return torb_machine_equal_slot(212, first, second);
}
static uint64_t torb_machine_hash_element_212(const void *value) {
  return torb_machine_hash_slot(212, value);
}
static void torb_machine_retain_element_213(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[213]));
}
static void torb_machine_release_element_213(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[213]));
}
static bool torb_machine_equal_element_213(const void *first, const void *second) {
  return torb_machine_equal_slot(213, first, second);
}
static uint64_t torb_machine_hash_element_213(const void *value) {
  return torb_machine_hash_slot(213, value);
}
static void torb_machine_retain_element_214(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[214]));
}
static void torb_machine_release_element_214(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[214]));
}
static bool torb_machine_equal_element_214(const void *first, const void *second) {
  return torb_machine_equal_slot(214, first, second);
}
static uint64_t torb_machine_hash_element_214(const void *value) {
  return torb_machine_hash_slot(214, value);
}
static void torb_machine_retain_element_215(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[215]));
}
static void torb_machine_release_element_215(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[215]));
}
static bool torb_machine_equal_element_215(const void *first, const void *second) {
  return torb_machine_equal_slot(215, first, second);
}
static uint64_t torb_machine_hash_element_215(const void *value) {
  return torb_machine_hash_slot(215, value);
}
static void torb_machine_retain_element_216(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[216]));
}
static void torb_machine_release_element_216(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[216]));
}
static bool torb_machine_equal_element_216(const void *first, const void *second) {
  return torb_machine_equal_slot(216, first, second);
}
static uint64_t torb_machine_hash_element_216(const void *value) {
  return torb_machine_hash_slot(216, value);
}
static void torb_machine_retain_element_217(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[217]));
}
static void torb_machine_release_element_217(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[217]));
}
static bool torb_machine_equal_element_217(const void *first, const void *second) {
  return torb_machine_equal_slot(217, first, second);
}
static uint64_t torb_machine_hash_element_217(const void *value) {
  return torb_machine_hash_slot(217, value);
}
static void torb_machine_retain_element_218(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[218]));
}
static void torb_machine_release_element_218(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[218]));
}
static bool torb_machine_equal_element_218(const void *first, const void *second) {
  return torb_machine_equal_slot(218, first, second);
}
static uint64_t torb_machine_hash_element_218(const void *value) {
  return torb_machine_hash_slot(218, value);
}
static void torb_machine_retain_element_219(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[219]));
}
static void torb_machine_release_element_219(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[219]));
}
static bool torb_machine_equal_element_219(const void *first, const void *second) {
  return torb_machine_equal_slot(219, first, second);
}
static uint64_t torb_machine_hash_element_219(const void *value) {
  return torb_machine_hash_slot(219, value);
}
static void torb_machine_retain_element_220(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[220]));
}
static void torb_machine_release_element_220(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[220]));
}
static bool torb_machine_equal_element_220(const void *first, const void *second) {
  return torb_machine_equal_slot(220, first, second);
}
static uint64_t torb_machine_hash_element_220(const void *value) {
  return torb_machine_hash_slot(220, value);
}
static void torb_machine_retain_element_221(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[221]));
}
static void torb_machine_release_element_221(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[221]));
}
static bool torb_machine_equal_element_221(const void *first, const void *second) {
  return torb_machine_equal_slot(221, first, second);
}
static uint64_t torb_machine_hash_element_221(const void *value) {
  return torb_machine_hash_slot(221, value);
}
static void torb_machine_retain_element_222(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[222]));
}
static void torb_machine_release_element_222(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[222]));
}
static bool torb_machine_equal_element_222(const void *first, const void *second) {
  return torb_machine_equal_slot(222, first, second);
}
static uint64_t torb_machine_hash_element_222(const void *value) {
  return torb_machine_hash_slot(222, value);
}
static void torb_machine_retain_element_223(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[223]));
}
static void torb_machine_release_element_223(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[223]));
}
static bool torb_machine_equal_element_223(const void *first, const void *second) {
  return torb_machine_equal_slot(223, first, second);
}
static uint64_t torb_machine_hash_element_223(const void *value) {
  return torb_machine_hash_slot(223, value);
}
static void torb_machine_retain_element_224(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[224]));
}
static void torb_machine_release_element_224(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[224]));
}
static bool torb_machine_equal_element_224(const void *first, const void *second) {
  return torb_machine_equal_slot(224, first, second);
}
static uint64_t torb_machine_hash_element_224(const void *value) {
  return torb_machine_hash_slot(224, value);
}
static void torb_machine_retain_element_225(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[225]));
}
static void torb_machine_release_element_225(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[225]));
}
static bool torb_machine_equal_element_225(const void *first, const void *second) {
  return torb_machine_equal_slot(225, first, second);
}
static uint64_t torb_machine_hash_element_225(const void *value) {
  return torb_machine_hash_slot(225, value);
}
static void torb_machine_retain_element_226(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[226]));
}
static void torb_machine_release_element_226(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[226]));
}
static bool torb_machine_equal_element_226(const void *first, const void *second) {
  return torb_machine_equal_slot(226, first, second);
}
static uint64_t torb_machine_hash_element_226(const void *value) {
  return torb_machine_hash_slot(226, value);
}
static void torb_machine_retain_element_227(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[227]));
}
static void torb_machine_release_element_227(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[227]));
}
static bool torb_machine_equal_element_227(const void *first, const void *second) {
  return torb_machine_equal_slot(227, first, second);
}
static uint64_t torb_machine_hash_element_227(const void *value) {
  return torb_machine_hash_slot(227, value);
}
static void torb_machine_retain_element_228(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[228]));
}
static void torb_machine_release_element_228(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[228]));
}
static bool torb_machine_equal_element_228(const void *first, const void *second) {
  return torb_machine_equal_slot(228, first, second);
}
static uint64_t torb_machine_hash_element_228(const void *value) {
  return torb_machine_hash_slot(228, value);
}
static void torb_machine_retain_element_229(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[229]));
}
static void torb_machine_release_element_229(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[229]));
}
static bool torb_machine_equal_element_229(const void *first, const void *second) {
  return torb_machine_equal_slot(229, first, second);
}
static uint64_t torb_machine_hash_element_229(const void *value) {
  return torb_machine_hash_slot(229, value);
}
static void torb_machine_retain_element_230(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[230]));
}
static void torb_machine_release_element_230(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[230]));
}
static bool torb_machine_equal_element_230(const void *first, const void *second) {
  return torb_machine_equal_slot(230, first, second);
}
static uint64_t torb_machine_hash_element_230(const void *value) {
  return torb_machine_hash_slot(230, value);
}
static void torb_machine_retain_element_231(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[231]));
}
static void torb_machine_release_element_231(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[231]));
}
static bool torb_machine_equal_element_231(const void *first, const void *second) {
  return torb_machine_equal_slot(231, first, second);
}
static uint64_t torb_machine_hash_element_231(const void *value) {
  return torb_machine_hash_slot(231, value);
}
static void torb_machine_retain_element_232(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[232]));
}
static void torb_machine_release_element_232(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[232]));
}
static bool torb_machine_equal_element_232(const void *first, const void *second) {
  return torb_machine_equal_slot(232, first, second);
}
static uint64_t torb_machine_hash_element_232(const void *value) {
  return torb_machine_hash_slot(232, value);
}
static void torb_machine_retain_element_233(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[233]));
}
static void torb_machine_release_element_233(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[233]));
}
static bool torb_machine_equal_element_233(const void *first, const void *second) {
  return torb_machine_equal_slot(233, first, second);
}
static uint64_t torb_machine_hash_element_233(const void *value) {
  return torb_machine_hash_slot(233, value);
}
static void torb_machine_retain_element_234(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[234]));
}
static void torb_machine_release_element_234(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[234]));
}
static bool torb_machine_equal_element_234(const void *first, const void *second) {
  return torb_machine_equal_slot(234, first, second);
}
static uint64_t torb_machine_hash_element_234(const void *value) {
  return torb_machine_hash_slot(234, value);
}
static void torb_machine_retain_element_235(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[235]));
}
static void torb_machine_release_element_235(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[235]));
}
static bool torb_machine_equal_element_235(const void *first, const void *second) {
  return torb_machine_equal_slot(235, first, second);
}
static uint64_t torb_machine_hash_element_235(const void *value) {
  return torb_machine_hash_slot(235, value);
}
static void torb_machine_retain_element_236(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[236]));
}
static void torb_machine_release_element_236(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[236]));
}
static bool torb_machine_equal_element_236(const void *first, const void *second) {
  return torb_machine_equal_slot(236, first, second);
}
static uint64_t torb_machine_hash_element_236(const void *value) {
  return torb_machine_hash_slot(236, value);
}
static void torb_machine_retain_element_237(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[237]));
}
static void torb_machine_release_element_237(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[237]));
}
static bool torb_machine_equal_element_237(const void *first, const void *second) {
  return torb_machine_equal_slot(237, first, second);
}
static uint64_t torb_machine_hash_element_237(const void *value) {
  return torb_machine_hash_slot(237, value);
}
static void torb_machine_retain_element_238(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[238]));
}
static void torb_machine_release_element_238(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[238]));
}
static bool torb_machine_equal_element_238(const void *first, const void *second) {
  return torb_machine_equal_slot(238, first, second);
}
static uint64_t torb_machine_hash_element_238(const void *value) {
  return torb_machine_hash_slot(238, value);
}
static void torb_machine_retain_element_239(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[239]));
}
static void torb_machine_release_element_239(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[239]));
}
static bool torb_machine_equal_element_239(const void *first, const void *second) {
  return torb_machine_equal_slot(239, first, second);
}
static uint64_t torb_machine_hash_element_239(const void *value) {
  return torb_machine_hash_slot(239, value);
}
static void torb_machine_retain_element_240(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[240]));
}
static void torb_machine_release_element_240(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[240]));
}
static bool torb_machine_equal_element_240(const void *first, const void *second) {
  return torb_machine_equal_slot(240, first, second);
}
static uint64_t torb_machine_hash_element_240(const void *value) {
  return torb_machine_hash_slot(240, value);
}
static void torb_machine_retain_element_241(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[241]));
}
static void torb_machine_release_element_241(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[241]));
}
static bool torb_machine_equal_element_241(const void *first, const void *second) {
  return torb_machine_equal_slot(241, first, second);
}
static uint64_t torb_machine_hash_element_241(const void *value) {
  return torb_machine_hash_slot(241, value);
}
static void torb_machine_retain_element_242(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[242]));
}
static void torb_machine_release_element_242(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[242]));
}
static bool torb_machine_equal_element_242(const void *first, const void *second) {
  return torb_machine_equal_slot(242, first, second);
}
static uint64_t torb_machine_hash_element_242(const void *value) {
  return torb_machine_hash_slot(242, value);
}
static void torb_machine_retain_element_243(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[243]));
}
static void torb_machine_release_element_243(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[243]));
}
static bool torb_machine_equal_element_243(const void *first, const void *second) {
  return torb_machine_equal_slot(243, first, second);
}
static uint64_t torb_machine_hash_element_243(const void *value) {
  return torb_machine_hash_slot(243, value);
}
static void torb_machine_retain_element_244(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[244]));
}
static void torb_machine_release_element_244(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[244]));
}
static bool torb_machine_equal_element_244(const void *first, const void *second) {
  return torb_machine_equal_slot(244, first, second);
}
static uint64_t torb_machine_hash_element_244(const void *value) {
  return torb_machine_hash_slot(244, value);
}
static void torb_machine_retain_element_245(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[245]));
}
static void torb_machine_release_element_245(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[245]));
}
static bool torb_machine_equal_element_245(const void *first, const void *second) {
  return torb_machine_equal_slot(245, first, second);
}
static uint64_t torb_machine_hash_element_245(const void *value) {
  return torb_machine_hash_slot(245, value);
}
static void torb_machine_retain_element_246(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[246]));
}
static void torb_machine_release_element_246(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[246]));
}
static bool torb_machine_equal_element_246(const void *first, const void *second) {
  return torb_machine_equal_slot(246, first, second);
}
static uint64_t torb_machine_hash_element_246(const void *value) {
  return torb_machine_hash_slot(246, value);
}
static void torb_machine_retain_element_247(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[247]));
}
static void torb_machine_release_element_247(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[247]));
}
static bool torb_machine_equal_element_247(const void *first, const void *second) {
  return torb_machine_equal_slot(247, first, second);
}
static uint64_t torb_machine_hash_element_247(const void *value) {
  return torb_machine_hash_slot(247, value);
}
static void torb_machine_retain_element_248(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[248]));
}
static void torb_machine_release_element_248(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[248]));
}
static bool torb_machine_equal_element_248(const void *first, const void *second) {
  return torb_machine_equal_slot(248, first, second);
}
static uint64_t torb_machine_hash_element_248(const void *value) {
  return torb_machine_hash_slot(248, value);
}
static void torb_machine_retain_element_249(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[249]));
}
static void torb_machine_release_element_249(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[249]));
}
static bool torb_machine_equal_element_249(const void *first, const void *second) {
  return torb_machine_equal_slot(249, first, second);
}
static uint64_t torb_machine_hash_element_249(const void *value) {
  return torb_machine_hash_slot(249, value);
}
static void torb_machine_retain_element_250(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[250]));
}
static void torb_machine_release_element_250(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[250]));
}
static bool torb_machine_equal_element_250(const void *first, const void *second) {
  return torb_machine_equal_slot(250, first, second);
}
static uint64_t torb_machine_hash_element_250(const void *value) {
  return torb_machine_hash_slot(250, value);
}
static void torb_machine_retain_element_251(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[251]));
}
static void torb_machine_release_element_251(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[251]));
}
static bool torb_machine_equal_element_251(const void *first, const void *second) {
  return torb_machine_equal_slot(251, first, second);
}
static uint64_t torb_machine_hash_element_251(const void *value) {
  return torb_machine_hash_slot(251, value);
}
static void torb_machine_retain_element_252(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[252]));
}
static void torb_machine_release_element_252(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[252]));
}
static bool torb_machine_equal_element_252(const void *first, const void *second) {
  return torb_machine_equal_slot(252, first, second);
}
static uint64_t torb_machine_hash_element_252(const void *value) {
  return torb_machine_hash_slot(252, value);
}
static void torb_machine_retain_element_253(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[253]));
}
static void torb_machine_release_element_253(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[253]));
}
static bool torb_machine_equal_element_253(const void *first, const void *second) {
  return torb_machine_equal_slot(253, first, second);
}
static uint64_t torb_machine_hash_element_253(const void *value) {
  return torb_machine_hash_slot(253, value);
}
static void torb_machine_retain_element_254(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[254]));
}
static void torb_machine_release_element_254(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[254]));
}
static bool torb_machine_equal_element_254(const void *first, const void *second) {
  return torb_machine_equal_slot(254, first, second);
}
static uint64_t torb_machine_hash_element_254(const void *value) {
  return torb_machine_hash_slot(254, value);
}
static void torb_machine_retain_element_255(void *element) {
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[255]));
}
static void torb_machine_release_element_255(void *element) {
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[255]));
}
static bool torb_machine_equal_element_255(const void *first, const void *second) {
  return torb_machine_equal_slot(255, first, second);
}
static uint64_t torb_machine_hash_element_255(const void *value) {
  return torb_machine_hash_slot(255, value);
}

typedef struct torb_machine_element_callbacks {
  void (*retain)(void *);
  void (*release)(void *);
  bool (*equals)(const void *, const void *);
  uint64_t (*hash)(const void *);
} torb_machine_element_callbacks;

static const torb_machine_element_callbacks torb_machine_element_pool[TORB_MACHINE_ELEMENT_SLOTS] = {
  { torb_machine_retain_element_0, torb_machine_release_element_0, torb_machine_equal_element_0,
    torb_machine_hash_element_0 },
  { torb_machine_retain_element_1, torb_machine_release_element_1, torb_machine_equal_element_1,
    torb_machine_hash_element_1 },
  { torb_machine_retain_element_2, torb_machine_release_element_2, torb_machine_equal_element_2,
    torb_machine_hash_element_2 },
  { torb_machine_retain_element_3, torb_machine_release_element_3, torb_machine_equal_element_3,
    torb_machine_hash_element_3 },
  { torb_machine_retain_element_4, torb_machine_release_element_4, torb_machine_equal_element_4,
    torb_machine_hash_element_4 },
  { torb_machine_retain_element_5, torb_machine_release_element_5, torb_machine_equal_element_5,
    torb_machine_hash_element_5 },
  { torb_machine_retain_element_6, torb_machine_release_element_6, torb_machine_equal_element_6,
    torb_machine_hash_element_6 },
  { torb_machine_retain_element_7, torb_machine_release_element_7, torb_machine_equal_element_7,
    torb_machine_hash_element_7 },
  { torb_machine_retain_element_8, torb_machine_release_element_8, torb_machine_equal_element_8,
    torb_machine_hash_element_8 },
  { torb_machine_retain_element_9, torb_machine_release_element_9, torb_machine_equal_element_9,
    torb_machine_hash_element_9 },
  { torb_machine_retain_element_10, torb_machine_release_element_10, torb_machine_equal_element_10,
    torb_machine_hash_element_10 },
  { torb_machine_retain_element_11, torb_machine_release_element_11, torb_machine_equal_element_11,
    torb_machine_hash_element_11 },
  { torb_machine_retain_element_12, torb_machine_release_element_12, torb_machine_equal_element_12,
    torb_machine_hash_element_12 },
  { torb_machine_retain_element_13, torb_machine_release_element_13, torb_machine_equal_element_13,
    torb_machine_hash_element_13 },
  { torb_machine_retain_element_14, torb_machine_release_element_14, torb_machine_equal_element_14,
    torb_machine_hash_element_14 },
  { torb_machine_retain_element_15, torb_machine_release_element_15, torb_machine_equal_element_15,
    torb_machine_hash_element_15 },
  { torb_machine_retain_element_16, torb_machine_release_element_16, torb_machine_equal_element_16,
    torb_machine_hash_element_16 },
  { torb_machine_retain_element_17, torb_machine_release_element_17, torb_machine_equal_element_17,
    torb_machine_hash_element_17 },
  { torb_machine_retain_element_18, torb_machine_release_element_18, torb_machine_equal_element_18,
    torb_machine_hash_element_18 },
  { torb_machine_retain_element_19, torb_machine_release_element_19, torb_machine_equal_element_19,
    torb_machine_hash_element_19 },
  { torb_machine_retain_element_20, torb_machine_release_element_20, torb_machine_equal_element_20,
    torb_machine_hash_element_20 },
  { torb_machine_retain_element_21, torb_machine_release_element_21, torb_machine_equal_element_21,
    torb_machine_hash_element_21 },
  { torb_machine_retain_element_22, torb_machine_release_element_22, torb_machine_equal_element_22,
    torb_machine_hash_element_22 },
  { torb_machine_retain_element_23, torb_machine_release_element_23, torb_machine_equal_element_23,
    torb_machine_hash_element_23 },
  { torb_machine_retain_element_24, torb_machine_release_element_24, torb_machine_equal_element_24,
    torb_machine_hash_element_24 },
  { torb_machine_retain_element_25, torb_machine_release_element_25, torb_machine_equal_element_25,
    torb_machine_hash_element_25 },
  { torb_machine_retain_element_26, torb_machine_release_element_26, torb_machine_equal_element_26,
    torb_machine_hash_element_26 },
  { torb_machine_retain_element_27, torb_machine_release_element_27, torb_machine_equal_element_27,
    torb_machine_hash_element_27 },
  { torb_machine_retain_element_28, torb_machine_release_element_28, torb_machine_equal_element_28,
    torb_machine_hash_element_28 },
  { torb_machine_retain_element_29, torb_machine_release_element_29, torb_machine_equal_element_29,
    torb_machine_hash_element_29 },
  { torb_machine_retain_element_30, torb_machine_release_element_30, torb_machine_equal_element_30,
    torb_machine_hash_element_30 },
  { torb_machine_retain_element_31, torb_machine_release_element_31, torb_machine_equal_element_31,
    torb_machine_hash_element_31 },
  { torb_machine_retain_element_32, torb_machine_release_element_32, torb_machine_equal_element_32,
    torb_machine_hash_element_32 },
  { torb_machine_retain_element_33, torb_machine_release_element_33, torb_machine_equal_element_33,
    torb_machine_hash_element_33 },
  { torb_machine_retain_element_34, torb_machine_release_element_34, torb_machine_equal_element_34,
    torb_machine_hash_element_34 },
  { torb_machine_retain_element_35, torb_machine_release_element_35, torb_machine_equal_element_35,
    torb_machine_hash_element_35 },
  { torb_machine_retain_element_36, torb_machine_release_element_36, torb_machine_equal_element_36,
    torb_machine_hash_element_36 },
  { torb_machine_retain_element_37, torb_machine_release_element_37, torb_machine_equal_element_37,
    torb_machine_hash_element_37 },
  { torb_machine_retain_element_38, torb_machine_release_element_38, torb_machine_equal_element_38,
    torb_machine_hash_element_38 },
  { torb_machine_retain_element_39, torb_machine_release_element_39, torb_machine_equal_element_39,
    torb_machine_hash_element_39 },
  { torb_machine_retain_element_40, torb_machine_release_element_40, torb_machine_equal_element_40,
    torb_machine_hash_element_40 },
  { torb_machine_retain_element_41, torb_machine_release_element_41, torb_machine_equal_element_41,
    torb_machine_hash_element_41 },
  { torb_machine_retain_element_42, torb_machine_release_element_42, torb_machine_equal_element_42,
    torb_machine_hash_element_42 },
  { torb_machine_retain_element_43, torb_machine_release_element_43, torb_machine_equal_element_43,
    torb_machine_hash_element_43 },
  { torb_machine_retain_element_44, torb_machine_release_element_44, torb_machine_equal_element_44,
    torb_machine_hash_element_44 },
  { torb_machine_retain_element_45, torb_machine_release_element_45, torb_machine_equal_element_45,
    torb_machine_hash_element_45 },
  { torb_machine_retain_element_46, torb_machine_release_element_46, torb_machine_equal_element_46,
    torb_machine_hash_element_46 },
  { torb_machine_retain_element_47, torb_machine_release_element_47, torb_machine_equal_element_47,
    torb_machine_hash_element_47 },
  { torb_machine_retain_element_48, torb_machine_release_element_48, torb_machine_equal_element_48,
    torb_machine_hash_element_48 },
  { torb_machine_retain_element_49, torb_machine_release_element_49, torb_machine_equal_element_49,
    torb_machine_hash_element_49 },
  { torb_machine_retain_element_50, torb_machine_release_element_50, torb_machine_equal_element_50,
    torb_machine_hash_element_50 },
  { torb_machine_retain_element_51, torb_machine_release_element_51, torb_machine_equal_element_51,
    torb_machine_hash_element_51 },
  { torb_machine_retain_element_52, torb_machine_release_element_52, torb_machine_equal_element_52,
    torb_machine_hash_element_52 },
  { torb_machine_retain_element_53, torb_machine_release_element_53, torb_machine_equal_element_53,
    torb_machine_hash_element_53 },
  { torb_machine_retain_element_54, torb_machine_release_element_54, torb_machine_equal_element_54,
    torb_machine_hash_element_54 },
  { torb_machine_retain_element_55, torb_machine_release_element_55, torb_machine_equal_element_55,
    torb_machine_hash_element_55 },
  { torb_machine_retain_element_56, torb_machine_release_element_56, torb_machine_equal_element_56,
    torb_machine_hash_element_56 },
  { torb_machine_retain_element_57, torb_machine_release_element_57, torb_machine_equal_element_57,
    torb_machine_hash_element_57 },
  { torb_machine_retain_element_58, torb_machine_release_element_58, torb_machine_equal_element_58,
    torb_machine_hash_element_58 },
  { torb_machine_retain_element_59, torb_machine_release_element_59, torb_machine_equal_element_59,
    torb_machine_hash_element_59 },
  { torb_machine_retain_element_60, torb_machine_release_element_60, torb_machine_equal_element_60,
    torb_machine_hash_element_60 },
  { torb_machine_retain_element_61, torb_machine_release_element_61, torb_machine_equal_element_61,
    torb_machine_hash_element_61 },
  { torb_machine_retain_element_62, torb_machine_release_element_62, torb_machine_equal_element_62,
    torb_machine_hash_element_62 },
  { torb_machine_retain_element_63, torb_machine_release_element_63, torb_machine_equal_element_63,
    torb_machine_hash_element_63 },
  { torb_machine_retain_element_64, torb_machine_release_element_64, torb_machine_equal_element_64,
    torb_machine_hash_element_64 },
  { torb_machine_retain_element_65, torb_machine_release_element_65, torb_machine_equal_element_65,
    torb_machine_hash_element_65 },
  { torb_machine_retain_element_66, torb_machine_release_element_66, torb_machine_equal_element_66,
    torb_machine_hash_element_66 },
  { torb_machine_retain_element_67, torb_machine_release_element_67, torb_machine_equal_element_67,
    torb_machine_hash_element_67 },
  { torb_machine_retain_element_68, torb_machine_release_element_68, torb_machine_equal_element_68,
    torb_machine_hash_element_68 },
  { torb_machine_retain_element_69, torb_machine_release_element_69, torb_machine_equal_element_69,
    torb_machine_hash_element_69 },
  { torb_machine_retain_element_70, torb_machine_release_element_70, torb_machine_equal_element_70,
    torb_machine_hash_element_70 },
  { torb_machine_retain_element_71, torb_machine_release_element_71, torb_machine_equal_element_71,
    torb_machine_hash_element_71 },
  { torb_machine_retain_element_72, torb_machine_release_element_72, torb_machine_equal_element_72,
    torb_machine_hash_element_72 },
  { torb_machine_retain_element_73, torb_machine_release_element_73, torb_machine_equal_element_73,
    torb_machine_hash_element_73 },
  { torb_machine_retain_element_74, torb_machine_release_element_74, torb_machine_equal_element_74,
    torb_machine_hash_element_74 },
  { torb_machine_retain_element_75, torb_machine_release_element_75, torb_machine_equal_element_75,
    torb_machine_hash_element_75 },
  { torb_machine_retain_element_76, torb_machine_release_element_76, torb_machine_equal_element_76,
    torb_machine_hash_element_76 },
  { torb_machine_retain_element_77, torb_machine_release_element_77, torb_machine_equal_element_77,
    torb_machine_hash_element_77 },
  { torb_machine_retain_element_78, torb_machine_release_element_78, torb_machine_equal_element_78,
    torb_machine_hash_element_78 },
  { torb_machine_retain_element_79, torb_machine_release_element_79, torb_machine_equal_element_79,
    torb_machine_hash_element_79 },
  { torb_machine_retain_element_80, torb_machine_release_element_80, torb_machine_equal_element_80,
    torb_machine_hash_element_80 },
  { torb_machine_retain_element_81, torb_machine_release_element_81, torb_machine_equal_element_81,
    torb_machine_hash_element_81 },
  { torb_machine_retain_element_82, torb_machine_release_element_82, torb_machine_equal_element_82,
    torb_machine_hash_element_82 },
  { torb_machine_retain_element_83, torb_machine_release_element_83, torb_machine_equal_element_83,
    torb_machine_hash_element_83 },
  { torb_machine_retain_element_84, torb_machine_release_element_84, torb_machine_equal_element_84,
    torb_machine_hash_element_84 },
  { torb_machine_retain_element_85, torb_machine_release_element_85, torb_machine_equal_element_85,
    torb_machine_hash_element_85 },
  { torb_machine_retain_element_86, torb_machine_release_element_86, torb_machine_equal_element_86,
    torb_machine_hash_element_86 },
  { torb_machine_retain_element_87, torb_machine_release_element_87, torb_machine_equal_element_87,
    torb_machine_hash_element_87 },
  { torb_machine_retain_element_88, torb_machine_release_element_88, torb_machine_equal_element_88,
    torb_machine_hash_element_88 },
  { torb_machine_retain_element_89, torb_machine_release_element_89, torb_machine_equal_element_89,
    torb_machine_hash_element_89 },
  { torb_machine_retain_element_90, torb_machine_release_element_90, torb_machine_equal_element_90,
    torb_machine_hash_element_90 },
  { torb_machine_retain_element_91, torb_machine_release_element_91, torb_machine_equal_element_91,
    torb_machine_hash_element_91 },
  { torb_machine_retain_element_92, torb_machine_release_element_92, torb_machine_equal_element_92,
    torb_machine_hash_element_92 },
  { torb_machine_retain_element_93, torb_machine_release_element_93, torb_machine_equal_element_93,
    torb_machine_hash_element_93 },
  { torb_machine_retain_element_94, torb_machine_release_element_94, torb_machine_equal_element_94,
    torb_machine_hash_element_94 },
  { torb_machine_retain_element_95, torb_machine_release_element_95, torb_machine_equal_element_95,
    torb_machine_hash_element_95 },
  { torb_machine_retain_element_96, torb_machine_release_element_96, torb_machine_equal_element_96,
    torb_machine_hash_element_96 },
  { torb_machine_retain_element_97, torb_machine_release_element_97, torb_machine_equal_element_97,
    torb_machine_hash_element_97 },
  { torb_machine_retain_element_98, torb_machine_release_element_98, torb_machine_equal_element_98,
    torb_machine_hash_element_98 },
  { torb_machine_retain_element_99, torb_machine_release_element_99, torb_machine_equal_element_99,
    torb_machine_hash_element_99 },
  { torb_machine_retain_element_100, torb_machine_release_element_100, torb_machine_equal_element_100,
    torb_machine_hash_element_100 },
  { torb_machine_retain_element_101, torb_machine_release_element_101, torb_machine_equal_element_101,
    torb_machine_hash_element_101 },
  { torb_machine_retain_element_102, torb_machine_release_element_102, torb_machine_equal_element_102,
    torb_machine_hash_element_102 },
  { torb_machine_retain_element_103, torb_machine_release_element_103, torb_machine_equal_element_103,
    torb_machine_hash_element_103 },
  { torb_machine_retain_element_104, torb_machine_release_element_104, torb_machine_equal_element_104,
    torb_machine_hash_element_104 },
  { torb_machine_retain_element_105, torb_machine_release_element_105, torb_machine_equal_element_105,
    torb_machine_hash_element_105 },
  { torb_machine_retain_element_106, torb_machine_release_element_106, torb_machine_equal_element_106,
    torb_machine_hash_element_106 },
  { torb_machine_retain_element_107, torb_machine_release_element_107, torb_machine_equal_element_107,
    torb_machine_hash_element_107 },
  { torb_machine_retain_element_108, torb_machine_release_element_108, torb_machine_equal_element_108,
    torb_machine_hash_element_108 },
  { torb_machine_retain_element_109, torb_machine_release_element_109, torb_machine_equal_element_109,
    torb_machine_hash_element_109 },
  { torb_machine_retain_element_110, torb_machine_release_element_110, torb_machine_equal_element_110,
    torb_machine_hash_element_110 },
  { torb_machine_retain_element_111, torb_machine_release_element_111, torb_machine_equal_element_111,
    torb_machine_hash_element_111 },
  { torb_machine_retain_element_112, torb_machine_release_element_112, torb_machine_equal_element_112,
    torb_machine_hash_element_112 },
  { torb_machine_retain_element_113, torb_machine_release_element_113, torb_machine_equal_element_113,
    torb_machine_hash_element_113 },
  { torb_machine_retain_element_114, torb_machine_release_element_114, torb_machine_equal_element_114,
    torb_machine_hash_element_114 },
  { torb_machine_retain_element_115, torb_machine_release_element_115, torb_machine_equal_element_115,
    torb_machine_hash_element_115 },
  { torb_machine_retain_element_116, torb_machine_release_element_116, torb_machine_equal_element_116,
    torb_machine_hash_element_116 },
  { torb_machine_retain_element_117, torb_machine_release_element_117, torb_machine_equal_element_117,
    torb_machine_hash_element_117 },
  { torb_machine_retain_element_118, torb_machine_release_element_118, torb_machine_equal_element_118,
    torb_machine_hash_element_118 },
  { torb_machine_retain_element_119, torb_machine_release_element_119, torb_machine_equal_element_119,
    torb_machine_hash_element_119 },
  { torb_machine_retain_element_120, torb_machine_release_element_120, torb_machine_equal_element_120,
    torb_machine_hash_element_120 },
  { torb_machine_retain_element_121, torb_machine_release_element_121, torb_machine_equal_element_121,
    torb_machine_hash_element_121 },
  { torb_machine_retain_element_122, torb_machine_release_element_122, torb_machine_equal_element_122,
    torb_machine_hash_element_122 },
  { torb_machine_retain_element_123, torb_machine_release_element_123, torb_machine_equal_element_123,
    torb_machine_hash_element_123 },
  { torb_machine_retain_element_124, torb_machine_release_element_124, torb_machine_equal_element_124,
    torb_machine_hash_element_124 },
  { torb_machine_retain_element_125, torb_machine_release_element_125, torb_machine_equal_element_125,
    torb_machine_hash_element_125 },
  { torb_machine_retain_element_126, torb_machine_release_element_126, torb_machine_equal_element_126,
    torb_machine_hash_element_126 },
  { torb_machine_retain_element_127, torb_machine_release_element_127, torb_machine_equal_element_127,
    torb_machine_hash_element_127 },
  { torb_machine_retain_element_128, torb_machine_release_element_128, torb_machine_equal_element_128,
    torb_machine_hash_element_128 },
  { torb_machine_retain_element_129, torb_machine_release_element_129, torb_machine_equal_element_129,
    torb_machine_hash_element_129 },
  { torb_machine_retain_element_130, torb_machine_release_element_130, torb_machine_equal_element_130,
    torb_machine_hash_element_130 },
  { torb_machine_retain_element_131, torb_machine_release_element_131, torb_machine_equal_element_131,
    torb_machine_hash_element_131 },
  { torb_machine_retain_element_132, torb_machine_release_element_132, torb_machine_equal_element_132,
    torb_machine_hash_element_132 },
  { torb_machine_retain_element_133, torb_machine_release_element_133, torb_machine_equal_element_133,
    torb_machine_hash_element_133 },
  { torb_machine_retain_element_134, torb_machine_release_element_134, torb_machine_equal_element_134,
    torb_machine_hash_element_134 },
  { torb_machine_retain_element_135, torb_machine_release_element_135, torb_machine_equal_element_135,
    torb_machine_hash_element_135 },
  { torb_machine_retain_element_136, torb_machine_release_element_136, torb_machine_equal_element_136,
    torb_machine_hash_element_136 },
  { torb_machine_retain_element_137, torb_machine_release_element_137, torb_machine_equal_element_137,
    torb_machine_hash_element_137 },
  { torb_machine_retain_element_138, torb_machine_release_element_138, torb_machine_equal_element_138,
    torb_machine_hash_element_138 },
  { torb_machine_retain_element_139, torb_machine_release_element_139, torb_machine_equal_element_139,
    torb_machine_hash_element_139 },
  { torb_machine_retain_element_140, torb_machine_release_element_140, torb_machine_equal_element_140,
    torb_machine_hash_element_140 },
  { torb_machine_retain_element_141, torb_machine_release_element_141, torb_machine_equal_element_141,
    torb_machine_hash_element_141 },
  { torb_machine_retain_element_142, torb_machine_release_element_142, torb_machine_equal_element_142,
    torb_machine_hash_element_142 },
  { torb_machine_retain_element_143, torb_machine_release_element_143, torb_machine_equal_element_143,
    torb_machine_hash_element_143 },
  { torb_machine_retain_element_144, torb_machine_release_element_144, torb_machine_equal_element_144,
    torb_machine_hash_element_144 },
  { torb_machine_retain_element_145, torb_machine_release_element_145, torb_machine_equal_element_145,
    torb_machine_hash_element_145 },
  { torb_machine_retain_element_146, torb_machine_release_element_146, torb_machine_equal_element_146,
    torb_machine_hash_element_146 },
  { torb_machine_retain_element_147, torb_machine_release_element_147, torb_machine_equal_element_147,
    torb_machine_hash_element_147 },
  { torb_machine_retain_element_148, torb_machine_release_element_148, torb_machine_equal_element_148,
    torb_machine_hash_element_148 },
  { torb_machine_retain_element_149, torb_machine_release_element_149, torb_machine_equal_element_149,
    torb_machine_hash_element_149 },
  { torb_machine_retain_element_150, torb_machine_release_element_150, torb_machine_equal_element_150,
    torb_machine_hash_element_150 },
  { torb_machine_retain_element_151, torb_machine_release_element_151, torb_machine_equal_element_151,
    torb_machine_hash_element_151 },
  { torb_machine_retain_element_152, torb_machine_release_element_152, torb_machine_equal_element_152,
    torb_machine_hash_element_152 },
  { torb_machine_retain_element_153, torb_machine_release_element_153, torb_machine_equal_element_153,
    torb_machine_hash_element_153 },
  { torb_machine_retain_element_154, torb_machine_release_element_154, torb_machine_equal_element_154,
    torb_machine_hash_element_154 },
  { torb_machine_retain_element_155, torb_machine_release_element_155, torb_machine_equal_element_155,
    torb_machine_hash_element_155 },
  { torb_machine_retain_element_156, torb_machine_release_element_156, torb_machine_equal_element_156,
    torb_machine_hash_element_156 },
  { torb_machine_retain_element_157, torb_machine_release_element_157, torb_machine_equal_element_157,
    torb_machine_hash_element_157 },
  { torb_machine_retain_element_158, torb_machine_release_element_158, torb_machine_equal_element_158,
    torb_machine_hash_element_158 },
  { torb_machine_retain_element_159, torb_machine_release_element_159, torb_machine_equal_element_159,
    torb_machine_hash_element_159 },
  { torb_machine_retain_element_160, torb_machine_release_element_160, torb_machine_equal_element_160,
    torb_machine_hash_element_160 },
  { torb_machine_retain_element_161, torb_machine_release_element_161, torb_machine_equal_element_161,
    torb_machine_hash_element_161 },
  { torb_machine_retain_element_162, torb_machine_release_element_162, torb_machine_equal_element_162,
    torb_machine_hash_element_162 },
  { torb_machine_retain_element_163, torb_machine_release_element_163, torb_machine_equal_element_163,
    torb_machine_hash_element_163 },
  { torb_machine_retain_element_164, torb_machine_release_element_164, torb_machine_equal_element_164,
    torb_machine_hash_element_164 },
  { torb_machine_retain_element_165, torb_machine_release_element_165, torb_machine_equal_element_165,
    torb_machine_hash_element_165 },
  { torb_machine_retain_element_166, torb_machine_release_element_166, torb_machine_equal_element_166,
    torb_machine_hash_element_166 },
  { torb_machine_retain_element_167, torb_machine_release_element_167, torb_machine_equal_element_167,
    torb_machine_hash_element_167 },
  { torb_machine_retain_element_168, torb_machine_release_element_168, torb_machine_equal_element_168,
    torb_machine_hash_element_168 },
  { torb_machine_retain_element_169, torb_machine_release_element_169, torb_machine_equal_element_169,
    torb_machine_hash_element_169 },
  { torb_machine_retain_element_170, torb_machine_release_element_170, torb_machine_equal_element_170,
    torb_machine_hash_element_170 },
  { torb_machine_retain_element_171, torb_machine_release_element_171, torb_machine_equal_element_171,
    torb_machine_hash_element_171 },
  { torb_machine_retain_element_172, torb_machine_release_element_172, torb_machine_equal_element_172,
    torb_machine_hash_element_172 },
  { torb_machine_retain_element_173, torb_machine_release_element_173, torb_machine_equal_element_173,
    torb_machine_hash_element_173 },
  { torb_machine_retain_element_174, torb_machine_release_element_174, torb_machine_equal_element_174,
    torb_machine_hash_element_174 },
  { torb_machine_retain_element_175, torb_machine_release_element_175, torb_machine_equal_element_175,
    torb_machine_hash_element_175 },
  { torb_machine_retain_element_176, torb_machine_release_element_176, torb_machine_equal_element_176,
    torb_machine_hash_element_176 },
  { torb_machine_retain_element_177, torb_machine_release_element_177, torb_machine_equal_element_177,
    torb_machine_hash_element_177 },
  { torb_machine_retain_element_178, torb_machine_release_element_178, torb_machine_equal_element_178,
    torb_machine_hash_element_178 },
  { torb_machine_retain_element_179, torb_machine_release_element_179, torb_machine_equal_element_179,
    torb_machine_hash_element_179 },
  { torb_machine_retain_element_180, torb_machine_release_element_180, torb_machine_equal_element_180,
    torb_machine_hash_element_180 },
  { torb_machine_retain_element_181, torb_machine_release_element_181, torb_machine_equal_element_181,
    torb_machine_hash_element_181 },
  { torb_machine_retain_element_182, torb_machine_release_element_182, torb_machine_equal_element_182,
    torb_machine_hash_element_182 },
  { torb_machine_retain_element_183, torb_machine_release_element_183, torb_machine_equal_element_183,
    torb_machine_hash_element_183 },
  { torb_machine_retain_element_184, torb_machine_release_element_184, torb_machine_equal_element_184,
    torb_machine_hash_element_184 },
  { torb_machine_retain_element_185, torb_machine_release_element_185, torb_machine_equal_element_185,
    torb_machine_hash_element_185 },
  { torb_machine_retain_element_186, torb_machine_release_element_186, torb_machine_equal_element_186,
    torb_machine_hash_element_186 },
  { torb_machine_retain_element_187, torb_machine_release_element_187, torb_machine_equal_element_187,
    torb_machine_hash_element_187 },
  { torb_machine_retain_element_188, torb_machine_release_element_188, torb_machine_equal_element_188,
    torb_machine_hash_element_188 },
  { torb_machine_retain_element_189, torb_machine_release_element_189, torb_machine_equal_element_189,
    torb_machine_hash_element_189 },
  { torb_machine_retain_element_190, torb_machine_release_element_190, torb_machine_equal_element_190,
    torb_machine_hash_element_190 },
  { torb_machine_retain_element_191, torb_machine_release_element_191, torb_machine_equal_element_191,
    torb_machine_hash_element_191 },
  { torb_machine_retain_element_192, torb_machine_release_element_192, torb_machine_equal_element_192,
    torb_machine_hash_element_192 },
  { torb_machine_retain_element_193, torb_machine_release_element_193, torb_machine_equal_element_193,
    torb_machine_hash_element_193 },
  { torb_machine_retain_element_194, torb_machine_release_element_194, torb_machine_equal_element_194,
    torb_machine_hash_element_194 },
  { torb_machine_retain_element_195, torb_machine_release_element_195, torb_machine_equal_element_195,
    torb_machine_hash_element_195 },
  { torb_machine_retain_element_196, torb_machine_release_element_196, torb_machine_equal_element_196,
    torb_machine_hash_element_196 },
  { torb_machine_retain_element_197, torb_machine_release_element_197, torb_machine_equal_element_197,
    torb_machine_hash_element_197 },
  { torb_machine_retain_element_198, torb_machine_release_element_198, torb_machine_equal_element_198,
    torb_machine_hash_element_198 },
  { torb_machine_retain_element_199, torb_machine_release_element_199, torb_machine_equal_element_199,
    torb_machine_hash_element_199 },
  { torb_machine_retain_element_200, torb_machine_release_element_200, torb_machine_equal_element_200,
    torb_machine_hash_element_200 },
  { torb_machine_retain_element_201, torb_machine_release_element_201, torb_machine_equal_element_201,
    torb_machine_hash_element_201 },
  { torb_machine_retain_element_202, torb_machine_release_element_202, torb_machine_equal_element_202,
    torb_machine_hash_element_202 },
  { torb_machine_retain_element_203, torb_machine_release_element_203, torb_machine_equal_element_203,
    torb_machine_hash_element_203 },
  { torb_machine_retain_element_204, torb_machine_release_element_204, torb_machine_equal_element_204,
    torb_machine_hash_element_204 },
  { torb_machine_retain_element_205, torb_machine_release_element_205, torb_machine_equal_element_205,
    torb_machine_hash_element_205 },
  { torb_machine_retain_element_206, torb_machine_release_element_206, torb_machine_equal_element_206,
    torb_machine_hash_element_206 },
  { torb_machine_retain_element_207, torb_machine_release_element_207, torb_machine_equal_element_207,
    torb_machine_hash_element_207 },
  { torb_machine_retain_element_208, torb_machine_release_element_208, torb_machine_equal_element_208,
    torb_machine_hash_element_208 },
  { torb_machine_retain_element_209, torb_machine_release_element_209, torb_machine_equal_element_209,
    torb_machine_hash_element_209 },
  { torb_machine_retain_element_210, torb_machine_release_element_210, torb_machine_equal_element_210,
    torb_machine_hash_element_210 },
  { torb_machine_retain_element_211, torb_machine_release_element_211, torb_machine_equal_element_211,
    torb_machine_hash_element_211 },
  { torb_machine_retain_element_212, torb_machine_release_element_212, torb_machine_equal_element_212,
    torb_machine_hash_element_212 },
  { torb_machine_retain_element_213, torb_machine_release_element_213, torb_machine_equal_element_213,
    torb_machine_hash_element_213 },
  { torb_machine_retain_element_214, torb_machine_release_element_214, torb_machine_equal_element_214,
    torb_machine_hash_element_214 },
  { torb_machine_retain_element_215, torb_machine_release_element_215, torb_machine_equal_element_215,
    torb_machine_hash_element_215 },
  { torb_machine_retain_element_216, torb_machine_release_element_216, torb_machine_equal_element_216,
    torb_machine_hash_element_216 },
  { torb_machine_retain_element_217, torb_machine_release_element_217, torb_machine_equal_element_217,
    torb_machine_hash_element_217 },
  { torb_machine_retain_element_218, torb_machine_release_element_218, torb_machine_equal_element_218,
    torb_machine_hash_element_218 },
  { torb_machine_retain_element_219, torb_machine_release_element_219, torb_machine_equal_element_219,
    torb_machine_hash_element_219 },
  { torb_machine_retain_element_220, torb_machine_release_element_220, torb_machine_equal_element_220,
    torb_machine_hash_element_220 },
  { torb_machine_retain_element_221, torb_machine_release_element_221, torb_machine_equal_element_221,
    torb_machine_hash_element_221 },
  { torb_machine_retain_element_222, torb_machine_release_element_222, torb_machine_equal_element_222,
    torb_machine_hash_element_222 },
  { torb_machine_retain_element_223, torb_machine_release_element_223, torb_machine_equal_element_223,
    torb_machine_hash_element_223 },
  { torb_machine_retain_element_224, torb_machine_release_element_224, torb_machine_equal_element_224,
    torb_machine_hash_element_224 },
  { torb_machine_retain_element_225, torb_machine_release_element_225, torb_machine_equal_element_225,
    torb_machine_hash_element_225 },
  { torb_machine_retain_element_226, torb_machine_release_element_226, torb_machine_equal_element_226,
    torb_machine_hash_element_226 },
  { torb_machine_retain_element_227, torb_machine_release_element_227, torb_machine_equal_element_227,
    torb_machine_hash_element_227 },
  { torb_machine_retain_element_228, torb_machine_release_element_228, torb_machine_equal_element_228,
    torb_machine_hash_element_228 },
  { torb_machine_retain_element_229, torb_machine_release_element_229, torb_machine_equal_element_229,
    torb_machine_hash_element_229 },
  { torb_machine_retain_element_230, torb_machine_release_element_230, torb_machine_equal_element_230,
    torb_machine_hash_element_230 },
  { torb_machine_retain_element_231, torb_machine_release_element_231, torb_machine_equal_element_231,
    torb_machine_hash_element_231 },
  { torb_machine_retain_element_232, torb_machine_release_element_232, torb_machine_equal_element_232,
    torb_machine_hash_element_232 },
  { torb_machine_retain_element_233, torb_machine_release_element_233, torb_machine_equal_element_233,
    torb_machine_hash_element_233 },
  { torb_machine_retain_element_234, torb_machine_release_element_234, torb_machine_equal_element_234,
    torb_machine_hash_element_234 },
  { torb_machine_retain_element_235, torb_machine_release_element_235, torb_machine_equal_element_235,
    torb_machine_hash_element_235 },
  { torb_machine_retain_element_236, torb_machine_release_element_236, torb_machine_equal_element_236,
    torb_machine_hash_element_236 },
  { torb_machine_retain_element_237, torb_machine_release_element_237, torb_machine_equal_element_237,
    torb_machine_hash_element_237 },
  { torb_machine_retain_element_238, torb_machine_release_element_238, torb_machine_equal_element_238,
    torb_machine_hash_element_238 },
  { torb_machine_retain_element_239, torb_machine_release_element_239, torb_machine_equal_element_239,
    torb_machine_hash_element_239 },
  { torb_machine_retain_element_240, torb_machine_release_element_240, torb_machine_equal_element_240,
    torb_machine_hash_element_240 },
  { torb_machine_retain_element_241, torb_machine_release_element_241, torb_machine_equal_element_241,
    torb_machine_hash_element_241 },
  { torb_machine_retain_element_242, torb_machine_release_element_242, torb_machine_equal_element_242,
    torb_machine_hash_element_242 },
  { torb_machine_retain_element_243, torb_machine_release_element_243, torb_machine_equal_element_243,
    torb_machine_hash_element_243 },
  { torb_machine_retain_element_244, torb_machine_release_element_244, torb_machine_equal_element_244,
    torb_machine_hash_element_244 },
  { torb_machine_retain_element_245, torb_machine_release_element_245, torb_machine_equal_element_245,
    torb_machine_hash_element_245 },
  { torb_machine_retain_element_246, torb_machine_release_element_246, torb_machine_equal_element_246,
    torb_machine_hash_element_246 },
  { torb_machine_retain_element_247, torb_machine_release_element_247, torb_machine_equal_element_247,
    torb_machine_hash_element_247 },
  { torb_machine_retain_element_248, torb_machine_release_element_248, torb_machine_equal_element_248,
    torb_machine_hash_element_248 },
  { torb_machine_retain_element_249, torb_machine_release_element_249, torb_machine_equal_element_249,
    torb_machine_hash_element_249 },
  { torb_machine_retain_element_250, torb_machine_release_element_250, torb_machine_equal_element_250,
    torb_machine_hash_element_250 },
  { torb_machine_retain_element_251, torb_machine_release_element_251, torb_machine_equal_element_251,
    torb_machine_hash_element_251 },
  { torb_machine_retain_element_252, torb_machine_release_element_252, torb_machine_equal_element_252,
    torb_machine_hash_element_252 },
  { torb_machine_retain_element_253, torb_machine_release_element_253, torb_machine_equal_element_253,
    torb_machine_hash_element_253 },
  { torb_machine_retain_element_254, torb_machine_release_element_254, torb_machine_equal_element_254,
    torb_machine_hash_element_254 },
  { torb_machine_retain_element_255, torb_machine_release_element_255, torb_machine_equal_element_255,
    torb_machine_hash_element_255 },
};

/*
 * A descriptor of one slot of the pool: counted by `shape` (-1 for a trivial element), and compared and hashed by the
 * program's own `equals` and `hash` where it has them, word by word where it does not - which is exact for integers,
 * `Bool`s and `Char`s, the one kind of element the bytecode hands over without them.
 *
 * A slot is shared by every element type of the same words, shape, `equals` and `hash`: the descriptors would be the
 * same, and a session of `torb repl` loads one continuation of the program per entry, each with its own element table
 * (the interpreter names the first of equal shapes, so a `List<Point>` of every entry lands in one slot).
 */
static const torb_element *torb_machine_element_made[TORB_MACHINE_ELEMENT_SLOTS];

static const torb_element *torb_machine_counted_element(int64_t words, int64_t shape, int64_t equals, int64_t hash) {
  torb_element *made;
  size_t slot = torb_machine_element_slots;
  for (size_t known = 0; known < torb_machine_element_slots; known++) {
    if (torb_machine_element_shapes[known] == shape && torb_machine_element_words[known] == words &&
        torb_machine_element_equals[known] == equals && torb_machine_element_hashes[known] == hash) {
      return torb_machine_element_made[known];
    }
  }
  if (slot >= TORB_MACHINE_ELEMENT_SLOTS) {
    torb_panic_text("internal error: the VM ran out of slots for the counted element types of the program",
                    torb_location_unknown);
  }
  torb_machine_element_slots += 1;
  torb_machine_element_shapes[slot] = shape;
  torb_machine_element_words[slot] = words;
  torb_machine_element_equals[slot] = equals;
  torb_machine_element_hashes[slot] = hash;
  made = (torb_element *)calloc(1, sizeof(torb_element));
  if (made == NULL) {
    torb_panic_out_of_memory(sizeof(torb_element));
  }
  made->size = (uint32_t)(words * 8);
  made->align = 8u;
  if (shape >= 0) {
    made->retain = torb_machine_element_pool[slot].retain;
    made->release = torb_machine_element_pool[slot].release;
  }
  made->equals = torb_machine_element_pool[slot].equals;
  made->hash = torb_machine_element_pool[slot].hash;
  torb_machine_element_made[slot] = made;
  return made;
}

/* -------------------------------------------------------------------------------------------- narrow storage --- */

/*
 * An integer narrower than a word, a `Bool` and a `Char` are stored in a container in their own width, as the C back
 * end stores them (docs/design/VM.md section 2), so a function of the runtime that reads a `List<UInt8>` reads bytes.
 * A register holds the value widened to a word. A narrow code is the element's bytes times four plus its kind: 1
 * signed, 2 unsigned (`narrowCode` of compiler/src/backend/bytecode/format.trb).
 */
static int64_t torb_machine_widen(const void *from, int64_t narrow) {
  bool isSigned = (narrow & 3) == 1;
  switch (narrow >> 2) {
    case 1:
      return isSigned ? (int64_t)*(const int8_t *)from : (int64_t)*(const uint8_t *)from;
    case 2:
      return isSigned ? (int64_t)*(const int16_t *)from : (int64_t)*(const uint16_t *)from;
    case 4:
      return isSigned ? (int64_t)*(const int32_t *)from : (int64_t)*(const uint32_t *)from;
    default:
      return *(const int64_t *)from;
  }
}

static void torb_machine_narrow(void *to, int64_t value, int64_t narrow) {
  switch (narrow >> 2) {
    case 1:
      *(uint8_t *)to = (uint8_t)value;
      return;
    case 2:
      *(uint16_t *)to = (uint16_t)value;
      return;
    case 4:
      *(uint32_t *)to = (uint32_t)value;
      return;
    default:
      *(int64_t *)to = value;
      return;
  }
}

/*
 * The descriptors of narrow elements: equal by their bytes, and hashed as the word a register holds of them, which is
 * what a word-sized element of the same values hashed to before.
 */
#define TORB_MACHINE_NARROW_ELEMENT(NAME, TYPE)                                                                        \
  static bool torb_machine_equal_##NAME(const void *first, const void *second) {                                       \
    return *(const TYPE *)first == *(const TYPE *)second;                                                              \
  }                                                                                                                    \
  static uint64_t torb_machine_hash_##NAME(const void *value) {                                                        \
    int64_t word = (int64_t)*(const TYPE *)value;                                                                      \
    return torb_element_int64.hash(&word);                                                                             \
  }                                                                                                                    \
  static const torb_element torb_machine_element_##NAME = { (uint32_t)sizeof(TYPE), (uint32_t)TORB_ALIGN_OF(TYPE),     \
                                                            NULL, NULL, torb_machine_equal_##NAME,                     \
                                                            torb_machine_hash_##NAME };

TORB_MACHINE_NARROW_ELEMENT(i8, int8_t)
TORB_MACHINE_NARROW_ELEMENT(u8, uint8_t)
TORB_MACHINE_NARROW_ELEMENT(i16, int16_t)
TORB_MACHINE_NARROW_ELEMENT(u16, uint16_t)
TORB_MACHINE_NARROW_ELEMENT(i32, int32_t)
TORB_MACHINE_NARROW_ELEMENT(u32, uint32_t)

static const torb_element *torb_machine_narrow_element(int64_t narrow) {
  bool isSigned = (narrow & 3) == 1;
  switch (narrow >> 2) {
    case 1:
      return isSigned ? &torb_machine_element_i8 : &torb_machine_element_u8;
    case 2:
      return isSigned ? &torb_machine_element_i16 : &torb_machine_element_u16;
    case 4:
      return isSigned ? &torb_machine_element_i32 : &torb_machine_element_u32;
    default:
      return &torb_element_int64;
  }
}

/* ---------------------------------------------------------------------------------------- the copy at a crossing --- */

/*
 * The frame of a task made private before an idle worker may take it, while the pool has more than one worker
 * (docs/design/CONCURRENCY.md section 16, "The copy at the crossing"): every counted value of the frame replaced in
 * place by an equal one that shares no counted block with anything else, by the shape of its words. The emitter only
 * asks for it where the C back end's `privateOf` answers that every value may be copied soundly, so what is walked
 * here is texts, containers, and what crosses as it is: a task or a channel of plain values, a shared environment.
 */
static bool torb_machine_privatize_value(int64_t *value, const torb_machine_shape *shape);

static bool torb_machine_privatize_element_of(size_t slot, void *element) {
  return torb_machine_privatize_value((int64_t *)element, torb_machine_shape_at(torb_machine_element_shapes[slot]));
}

#define TORB_MACHINE_PRIVATIZER(N)                                                                                     \
  static bool torb_machine_privatize_element_##N(void *element) {                                                      \
    return torb_machine_privatize_element_of(N, element);                                                              \
  }

TORB_MACHINE_PRIVATIZER(0)
TORB_MACHINE_PRIVATIZER(1)
TORB_MACHINE_PRIVATIZER(2)
TORB_MACHINE_PRIVATIZER(3)
TORB_MACHINE_PRIVATIZER(4)
TORB_MACHINE_PRIVATIZER(5)
TORB_MACHINE_PRIVATIZER(6)
TORB_MACHINE_PRIVATIZER(7)
TORB_MACHINE_PRIVATIZER(8)
TORB_MACHINE_PRIVATIZER(9)
TORB_MACHINE_PRIVATIZER(10)
TORB_MACHINE_PRIVATIZER(11)
TORB_MACHINE_PRIVATIZER(12)
TORB_MACHINE_PRIVATIZER(13)
TORB_MACHINE_PRIVATIZER(14)
TORB_MACHINE_PRIVATIZER(15)
TORB_MACHINE_PRIVATIZER(16)
TORB_MACHINE_PRIVATIZER(17)
TORB_MACHINE_PRIVATIZER(18)
TORB_MACHINE_PRIVATIZER(19)
TORB_MACHINE_PRIVATIZER(20)
TORB_MACHINE_PRIVATIZER(21)
TORB_MACHINE_PRIVATIZER(22)
TORB_MACHINE_PRIVATIZER(23)
TORB_MACHINE_PRIVATIZER(24)
TORB_MACHINE_PRIVATIZER(25)
TORB_MACHINE_PRIVATIZER(26)
TORB_MACHINE_PRIVATIZER(27)
TORB_MACHINE_PRIVATIZER(28)
TORB_MACHINE_PRIVATIZER(29)
TORB_MACHINE_PRIVATIZER(30)
TORB_MACHINE_PRIVATIZER(31)
TORB_MACHINE_PRIVATIZER(32)
TORB_MACHINE_PRIVATIZER(33)
TORB_MACHINE_PRIVATIZER(34)
TORB_MACHINE_PRIVATIZER(35)
TORB_MACHINE_PRIVATIZER(36)
TORB_MACHINE_PRIVATIZER(37)
TORB_MACHINE_PRIVATIZER(38)
TORB_MACHINE_PRIVATIZER(39)
TORB_MACHINE_PRIVATIZER(40)
TORB_MACHINE_PRIVATIZER(41)
TORB_MACHINE_PRIVATIZER(42)
TORB_MACHINE_PRIVATIZER(43)
TORB_MACHINE_PRIVATIZER(44)
TORB_MACHINE_PRIVATIZER(45)
TORB_MACHINE_PRIVATIZER(46)
TORB_MACHINE_PRIVATIZER(47)
TORB_MACHINE_PRIVATIZER(48)
TORB_MACHINE_PRIVATIZER(49)
TORB_MACHINE_PRIVATIZER(50)
TORB_MACHINE_PRIVATIZER(51)
TORB_MACHINE_PRIVATIZER(52)
TORB_MACHINE_PRIVATIZER(53)
TORB_MACHINE_PRIVATIZER(54)
TORB_MACHINE_PRIVATIZER(55)
TORB_MACHINE_PRIVATIZER(56)
TORB_MACHINE_PRIVATIZER(57)
TORB_MACHINE_PRIVATIZER(58)
TORB_MACHINE_PRIVATIZER(59)
TORB_MACHINE_PRIVATIZER(60)
TORB_MACHINE_PRIVATIZER(61)
TORB_MACHINE_PRIVATIZER(62)
TORB_MACHINE_PRIVATIZER(63)
TORB_MACHINE_PRIVATIZER(64)
TORB_MACHINE_PRIVATIZER(65)
TORB_MACHINE_PRIVATIZER(66)
TORB_MACHINE_PRIVATIZER(67)
TORB_MACHINE_PRIVATIZER(68)
TORB_MACHINE_PRIVATIZER(69)
TORB_MACHINE_PRIVATIZER(70)
TORB_MACHINE_PRIVATIZER(71)
TORB_MACHINE_PRIVATIZER(72)
TORB_MACHINE_PRIVATIZER(73)
TORB_MACHINE_PRIVATIZER(74)
TORB_MACHINE_PRIVATIZER(75)
TORB_MACHINE_PRIVATIZER(76)
TORB_MACHINE_PRIVATIZER(77)
TORB_MACHINE_PRIVATIZER(78)
TORB_MACHINE_PRIVATIZER(79)
TORB_MACHINE_PRIVATIZER(80)
TORB_MACHINE_PRIVATIZER(81)
TORB_MACHINE_PRIVATIZER(82)
TORB_MACHINE_PRIVATIZER(83)
TORB_MACHINE_PRIVATIZER(84)
TORB_MACHINE_PRIVATIZER(85)
TORB_MACHINE_PRIVATIZER(86)
TORB_MACHINE_PRIVATIZER(87)
TORB_MACHINE_PRIVATIZER(88)
TORB_MACHINE_PRIVATIZER(89)
TORB_MACHINE_PRIVATIZER(90)
TORB_MACHINE_PRIVATIZER(91)
TORB_MACHINE_PRIVATIZER(92)
TORB_MACHINE_PRIVATIZER(93)
TORB_MACHINE_PRIVATIZER(94)
TORB_MACHINE_PRIVATIZER(95)
TORB_MACHINE_PRIVATIZER(96)
TORB_MACHINE_PRIVATIZER(97)
TORB_MACHINE_PRIVATIZER(98)
TORB_MACHINE_PRIVATIZER(99)
TORB_MACHINE_PRIVATIZER(100)
TORB_MACHINE_PRIVATIZER(101)
TORB_MACHINE_PRIVATIZER(102)
TORB_MACHINE_PRIVATIZER(103)
TORB_MACHINE_PRIVATIZER(104)
TORB_MACHINE_PRIVATIZER(105)
TORB_MACHINE_PRIVATIZER(106)
TORB_MACHINE_PRIVATIZER(107)
TORB_MACHINE_PRIVATIZER(108)
TORB_MACHINE_PRIVATIZER(109)
TORB_MACHINE_PRIVATIZER(110)
TORB_MACHINE_PRIVATIZER(111)
TORB_MACHINE_PRIVATIZER(112)
TORB_MACHINE_PRIVATIZER(113)
TORB_MACHINE_PRIVATIZER(114)
TORB_MACHINE_PRIVATIZER(115)
TORB_MACHINE_PRIVATIZER(116)
TORB_MACHINE_PRIVATIZER(117)
TORB_MACHINE_PRIVATIZER(118)
TORB_MACHINE_PRIVATIZER(119)
TORB_MACHINE_PRIVATIZER(120)
TORB_MACHINE_PRIVATIZER(121)
TORB_MACHINE_PRIVATIZER(122)
TORB_MACHINE_PRIVATIZER(123)
TORB_MACHINE_PRIVATIZER(124)
TORB_MACHINE_PRIVATIZER(125)
TORB_MACHINE_PRIVATIZER(126)
TORB_MACHINE_PRIVATIZER(127)
TORB_MACHINE_PRIVATIZER(128)
TORB_MACHINE_PRIVATIZER(129)
TORB_MACHINE_PRIVATIZER(130)
TORB_MACHINE_PRIVATIZER(131)
TORB_MACHINE_PRIVATIZER(132)
TORB_MACHINE_PRIVATIZER(133)
TORB_MACHINE_PRIVATIZER(134)
TORB_MACHINE_PRIVATIZER(135)
TORB_MACHINE_PRIVATIZER(136)
TORB_MACHINE_PRIVATIZER(137)
TORB_MACHINE_PRIVATIZER(138)
TORB_MACHINE_PRIVATIZER(139)
TORB_MACHINE_PRIVATIZER(140)
TORB_MACHINE_PRIVATIZER(141)
TORB_MACHINE_PRIVATIZER(142)
TORB_MACHINE_PRIVATIZER(143)
TORB_MACHINE_PRIVATIZER(144)
TORB_MACHINE_PRIVATIZER(145)
TORB_MACHINE_PRIVATIZER(146)
TORB_MACHINE_PRIVATIZER(147)
TORB_MACHINE_PRIVATIZER(148)
TORB_MACHINE_PRIVATIZER(149)
TORB_MACHINE_PRIVATIZER(150)
TORB_MACHINE_PRIVATIZER(151)
TORB_MACHINE_PRIVATIZER(152)
TORB_MACHINE_PRIVATIZER(153)
TORB_MACHINE_PRIVATIZER(154)
TORB_MACHINE_PRIVATIZER(155)
TORB_MACHINE_PRIVATIZER(156)
TORB_MACHINE_PRIVATIZER(157)
TORB_MACHINE_PRIVATIZER(158)
TORB_MACHINE_PRIVATIZER(159)
TORB_MACHINE_PRIVATIZER(160)
TORB_MACHINE_PRIVATIZER(161)
TORB_MACHINE_PRIVATIZER(162)
TORB_MACHINE_PRIVATIZER(163)
TORB_MACHINE_PRIVATIZER(164)
TORB_MACHINE_PRIVATIZER(165)
TORB_MACHINE_PRIVATIZER(166)
TORB_MACHINE_PRIVATIZER(167)
TORB_MACHINE_PRIVATIZER(168)
TORB_MACHINE_PRIVATIZER(169)
TORB_MACHINE_PRIVATIZER(170)
TORB_MACHINE_PRIVATIZER(171)
TORB_MACHINE_PRIVATIZER(172)
TORB_MACHINE_PRIVATIZER(173)
TORB_MACHINE_PRIVATIZER(174)
TORB_MACHINE_PRIVATIZER(175)
TORB_MACHINE_PRIVATIZER(176)
TORB_MACHINE_PRIVATIZER(177)
TORB_MACHINE_PRIVATIZER(178)
TORB_MACHINE_PRIVATIZER(179)
TORB_MACHINE_PRIVATIZER(180)
TORB_MACHINE_PRIVATIZER(181)
TORB_MACHINE_PRIVATIZER(182)
TORB_MACHINE_PRIVATIZER(183)
TORB_MACHINE_PRIVATIZER(184)
TORB_MACHINE_PRIVATIZER(185)
TORB_MACHINE_PRIVATIZER(186)
TORB_MACHINE_PRIVATIZER(187)
TORB_MACHINE_PRIVATIZER(188)
TORB_MACHINE_PRIVATIZER(189)
TORB_MACHINE_PRIVATIZER(190)
TORB_MACHINE_PRIVATIZER(191)
TORB_MACHINE_PRIVATIZER(192)
TORB_MACHINE_PRIVATIZER(193)
TORB_MACHINE_PRIVATIZER(194)
TORB_MACHINE_PRIVATIZER(195)
TORB_MACHINE_PRIVATIZER(196)
TORB_MACHINE_PRIVATIZER(197)
TORB_MACHINE_PRIVATIZER(198)
TORB_MACHINE_PRIVATIZER(199)
TORB_MACHINE_PRIVATIZER(200)
TORB_MACHINE_PRIVATIZER(201)
TORB_MACHINE_PRIVATIZER(202)
TORB_MACHINE_PRIVATIZER(203)
TORB_MACHINE_PRIVATIZER(204)
TORB_MACHINE_PRIVATIZER(205)
TORB_MACHINE_PRIVATIZER(206)
TORB_MACHINE_PRIVATIZER(207)
TORB_MACHINE_PRIVATIZER(208)
TORB_MACHINE_PRIVATIZER(209)
TORB_MACHINE_PRIVATIZER(210)
TORB_MACHINE_PRIVATIZER(211)
TORB_MACHINE_PRIVATIZER(212)
TORB_MACHINE_PRIVATIZER(213)
TORB_MACHINE_PRIVATIZER(214)
TORB_MACHINE_PRIVATIZER(215)
TORB_MACHINE_PRIVATIZER(216)
TORB_MACHINE_PRIVATIZER(217)
TORB_MACHINE_PRIVATIZER(218)
TORB_MACHINE_PRIVATIZER(219)
TORB_MACHINE_PRIVATIZER(220)
TORB_MACHINE_PRIVATIZER(221)
TORB_MACHINE_PRIVATIZER(222)
TORB_MACHINE_PRIVATIZER(223)
TORB_MACHINE_PRIVATIZER(224)
TORB_MACHINE_PRIVATIZER(225)
TORB_MACHINE_PRIVATIZER(226)
TORB_MACHINE_PRIVATIZER(227)
TORB_MACHINE_PRIVATIZER(228)
TORB_MACHINE_PRIVATIZER(229)
TORB_MACHINE_PRIVATIZER(230)
TORB_MACHINE_PRIVATIZER(231)
TORB_MACHINE_PRIVATIZER(232)
TORB_MACHINE_PRIVATIZER(233)
TORB_MACHINE_PRIVATIZER(234)
TORB_MACHINE_PRIVATIZER(235)
TORB_MACHINE_PRIVATIZER(236)
TORB_MACHINE_PRIVATIZER(237)
TORB_MACHINE_PRIVATIZER(238)
TORB_MACHINE_PRIVATIZER(239)
TORB_MACHINE_PRIVATIZER(240)
TORB_MACHINE_PRIVATIZER(241)
TORB_MACHINE_PRIVATIZER(242)
TORB_MACHINE_PRIVATIZER(243)
TORB_MACHINE_PRIVATIZER(244)
TORB_MACHINE_PRIVATIZER(245)
TORB_MACHINE_PRIVATIZER(246)
TORB_MACHINE_PRIVATIZER(247)
TORB_MACHINE_PRIVATIZER(248)
TORB_MACHINE_PRIVATIZER(249)
TORB_MACHINE_PRIVATIZER(250)
TORB_MACHINE_PRIVATIZER(251)
TORB_MACHINE_PRIVATIZER(252)
TORB_MACHINE_PRIVATIZER(253)
TORB_MACHINE_PRIVATIZER(254)
TORB_MACHINE_PRIVATIZER(255)

static const torb_privatize_function torb_machine_privatizers[TORB_MACHINE_ELEMENT_SLOTS] = {
  torb_machine_privatize_element_0, torb_machine_privatize_element_1, torb_machine_privatize_element_2, torb_machine_privatize_element_3,
  torb_machine_privatize_element_4, torb_machine_privatize_element_5, torb_machine_privatize_element_6, torb_machine_privatize_element_7,
  torb_machine_privatize_element_8, torb_machine_privatize_element_9, torb_machine_privatize_element_10, torb_machine_privatize_element_11,
  torb_machine_privatize_element_12, torb_machine_privatize_element_13, torb_machine_privatize_element_14, torb_machine_privatize_element_15,
  torb_machine_privatize_element_16, torb_machine_privatize_element_17, torb_machine_privatize_element_18, torb_machine_privatize_element_19,
  torb_machine_privatize_element_20, torb_machine_privatize_element_21, torb_machine_privatize_element_22, torb_machine_privatize_element_23,
  torb_machine_privatize_element_24, torb_machine_privatize_element_25, torb_machine_privatize_element_26, torb_machine_privatize_element_27,
  torb_machine_privatize_element_28, torb_machine_privatize_element_29, torb_machine_privatize_element_30, torb_machine_privatize_element_31,
  torb_machine_privatize_element_32, torb_machine_privatize_element_33, torb_machine_privatize_element_34, torb_machine_privatize_element_35,
  torb_machine_privatize_element_36, torb_machine_privatize_element_37, torb_machine_privatize_element_38, torb_machine_privatize_element_39,
  torb_machine_privatize_element_40, torb_machine_privatize_element_41, torb_machine_privatize_element_42, torb_machine_privatize_element_43,
  torb_machine_privatize_element_44, torb_machine_privatize_element_45, torb_machine_privatize_element_46, torb_machine_privatize_element_47,
  torb_machine_privatize_element_48, torb_machine_privatize_element_49, torb_machine_privatize_element_50, torb_machine_privatize_element_51,
  torb_machine_privatize_element_52, torb_machine_privatize_element_53, torb_machine_privatize_element_54, torb_machine_privatize_element_55,
  torb_machine_privatize_element_56, torb_machine_privatize_element_57, torb_machine_privatize_element_58, torb_machine_privatize_element_59,
  torb_machine_privatize_element_60, torb_machine_privatize_element_61, torb_machine_privatize_element_62, torb_machine_privatize_element_63,
  torb_machine_privatize_element_64, torb_machine_privatize_element_65, torb_machine_privatize_element_66, torb_machine_privatize_element_67,
  torb_machine_privatize_element_68, torb_machine_privatize_element_69, torb_machine_privatize_element_70, torb_machine_privatize_element_71,
  torb_machine_privatize_element_72, torb_machine_privatize_element_73, torb_machine_privatize_element_74, torb_machine_privatize_element_75,
  torb_machine_privatize_element_76, torb_machine_privatize_element_77, torb_machine_privatize_element_78, torb_machine_privatize_element_79,
  torb_machine_privatize_element_80, torb_machine_privatize_element_81, torb_machine_privatize_element_82, torb_machine_privatize_element_83,
  torb_machine_privatize_element_84, torb_machine_privatize_element_85, torb_machine_privatize_element_86, torb_machine_privatize_element_87,
  torb_machine_privatize_element_88, torb_machine_privatize_element_89, torb_machine_privatize_element_90, torb_machine_privatize_element_91,
  torb_machine_privatize_element_92, torb_machine_privatize_element_93, torb_machine_privatize_element_94, torb_machine_privatize_element_95,
  torb_machine_privatize_element_96, torb_machine_privatize_element_97, torb_machine_privatize_element_98, torb_machine_privatize_element_99,
  torb_machine_privatize_element_100, torb_machine_privatize_element_101, torb_machine_privatize_element_102, torb_machine_privatize_element_103,
  torb_machine_privatize_element_104, torb_machine_privatize_element_105, torb_machine_privatize_element_106, torb_machine_privatize_element_107,
  torb_machine_privatize_element_108, torb_machine_privatize_element_109, torb_machine_privatize_element_110, torb_machine_privatize_element_111,
  torb_machine_privatize_element_112, torb_machine_privatize_element_113, torb_machine_privatize_element_114, torb_machine_privatize_element_115,
  torb_machine_privatize_element_116, torb_machine_privatize_element_117, torb_machine_privatize_element_118, torb_machine_privatize_element_119,
  torb_machine_privatize_element_120, torb_machine_privatize_element_121, torb_machine_privatize_element_122, torb_machine_privatize_element_123,
  torb_machine_privatize_element_124, torb_machine_privatize_element_125, torb_machine_privatize_element_126, torb_machine_privatize_element_127,
  torb_machine_privatize_element_128, torb_machine_privatize_element_129, torb_machine_privatize_element_130, torb_machine_privatize_element_131,
  torb_machine_privatize_element_132, torb_machine_privatize_element_133, torb_machine_privatize_element_134, torb_machine_privatize_element_135,
  torb_machine_privatize_element_136, torb_machine_privatize_element_137, torb_machine_privatize_element_138, torb_machine_privatize_element_139,
  torb_machine_privatize_element_140, torb_machine_privatize_element_141, torb_machine_privatize_element_142, torb_machine_privatize_element_143,
  torb_machine_privatize_element_144, torb_machine_privatize_element_145, torb_machine_privatize_element_146, torb_machine_privatize_element_147,
  torb_machine_privatize_element_148, torb_machine_privatize_element_149, torb_machine_privatize_element_150, torb_machine_privatize_element_151,
  torb_machine_privatize_element_152, torb_machine_privatize_element_153, torb_machine_privatize_element_154, torb_machine_privatize_element_155,
  torb_machine_privatize_element_156, torb_machine_privatize_element_157, torb_machine_privatize_element_158, torb_machine_privatize_element_159,
  torb_machine_privatize_element_160, torb_machine_privatize_element_161, torb_machine_privatize_element_162, torb_machine_privatize_element_163,
  torb_machine_privatize_element_164, torb_machine_privatize_element_165, torb_machine_privatize_element_166, torb_machine_privatize_element_167,
  torb_machine_privatize_element_168, torb_machine_privatize_element_169, torb_machine_privatize_element_170, torb_machine_privatize_element_171,
  torb_machine_privatize_element_172, torb_machine_privatize_element_173, torb_machine_privatize_element_174, torb_machine_privatize_element_175,
  torb_machine_privatize_element_176, torb_machine_privatize_element_177, torb_machine_privatize_element_178, torb_machine_privatize_element_179,
  torb_machine_privatize_element_180, torb_machine_privatize_element_181, torb_machine_privatize_element_182, torb_machine_privatize_element_183,
  torb_machine_privatize_element_184, torb_machine_privatize_element_185, torb_machine_privatize_element_186, torb_machine_privatize_element_187,
  torb_machine_privatize_element_188, torb_machine_privatize_element_189, torb_machine_privatize_element_190, torb_machine_privatize_element_191,
  torb_machine_privatize_element_192, torb_machine_privatize_element_193, torb_machine_privatize_element_194, torb_machine_privatize_element_195,
  torb_machine_privatize_element_196, torb_machine_privatize_element_197, torb_machine_privatize_element_198, torb_machine_privatize_element_199,
  torb_machine_privatize_element_200, torb_machine_privatize_element_201, torb_machine_privatize_element_202, torb_machine_privatize_element_203,
  torb_machine_privatize_element_204, torb_machine_privatize_element_205, torb_machine_privatize_element_206, torb_machine_privatize_element_207,
  torb_machine_privatize_element_208, torb_machine_privatize_element_209, torb_machine_privatize_element_210, torb_machine_privatize_element_211,
  torb_machine_privatize_element_212, torb_machine_privatize_element_213, torb_machine_privatize_element_214, torb_machine_privatize_element_215,
  torb_machine_privatize_element_216, torb_machine_privatize_element_217, torb_machine_privatize_element_218, torb_machine_privatize_element_219,
  torb_machine_privatize_element_220, torb_machine_privatize_element_221, torb_machine_privatize_element_222, torb_machine_privatize_element_223,
  torb_machine_privatize_element_224, torb_machine_privatize_element_225, torb_machine_privatize_element_226, torb_machine_privatize_element_227,
  torb_machine_privatize_element_228, torb_machine_privatize_element_229, torb_machine_privatize_element_230, torb_machine_privatize_element_231,
  torb_machine_privatize_element_232, torb_machine_privatize_element_233, torb_machine_privatize_element_234, torb_machine_privatize_element_235,
  torb_machine_privatize_element_236, torb_machine_privatize_element_237, torb_machine_privatize_element_238, torb_machine_privatize_element_239,
  torb_machine_privatize_element_240, torb_machine_privatize_element_241, torb_machine_privatize_element_242, torb_machine_privatize_element_243,
  torb_machine_privatize_element_244, torb_machine_privatize_element_245, torb_machine_privatize_element_246, torb_machine_privatize_element_247,
  torb_machine_privatize_element_248, torb_machine_privatize_element_249, torb_machine_privatize_element_250, torb_machine_privatize_element_251,
  torb_machine_privatize_element_252, torb_machine_privatize_element_253, torb_machine_privatize_element_254, torb_machine_privatize_element_255,
};

/* What makes one element of a container private: nothing for a plain one, the runtime's for a text, a slot's walk. */
static bool torb_machine_privatizer_of(const torb_element *element, torb_privatize_function *found) {
  if (element == &torb_element_text) {
    *found = torb_text_privatize_place;
    return true;
  }
  if (element->retain == NULL) {
    *found = NULL;
    return true;
  }
  for (size_t slot = 0; slot < torb_machine_element_slots; slot++) {
    if (torb_machine_element_made[slot] == element) {
      *found = torb_machine_privatizers[slot];
      return true;
    }
  }
  return false;
}

static bool torb_machine_privatize_word(int64_t *value, const torb_machine_word *word) {
  int64_t *place = value + word->offset;
  switch (word->kind) {
    case TORB_COUNTED_TEXT:
      return torb_text_privatize((torb_text *)place);
    case TORB_COUNTED_LIST: {
      torb_list *list = (torb_list *)place;
      torb_privatize_function element;
      if (list->storage == NULL) {
        return true;
      }
      return torb_machine_privatizer_of(list->storage->element, &element) && torb_list_privatize(list, element);
    }
    case TORB_COUNTED_MAP:
    case TORB_COUNTED_SET: {
      torb_map *map = (torb_map *)place;
      torb_privatize_function key;
      torb_privatize_function item;
      if (map->storage == NULL) {
        return true;
      }
      return torb_machine_privatizer_of(map->storage->key, &key) &&
             torb_machine_privatizer_of(map->storage->value, &item) && torb_map_privatize(map, key, item);
    }
    case TORB_COUNTED_TASK:
    case TORB_COUNTED_CHANNEL:
      /* Shared blocks that hand out plain values alone, or the emitter would not have asked */
      return true;
    case TORB_COUNTED_ENVIRONMENT:
      /* An environment is never copied: the closure crosses where it is shared */
      return torb_closure_may_move((torb_environment *)(intptr_t)place[0]);
    case TORB_COUNTED_NESTED:
      return torb_machine_privatize_value(place, torb_machine_shape_at(word->shape));
    default:
      return false;
  }
}

static bool torb_machine_privatize_value(int64_t *value, const torb_machine_shape *shape) {
  /* A variant would need its case to be copied: it stays where it is, as in the C back end */
  if (shape->tag >= 0) {
    return false;
  }
  for (size_t index = 0; index < shape->common.count; index++) {
    if (!torb_machine_privatize_word(value, &shape->common.words[index])) {
      return false;
    }
  }
  return true;
}

/* ---------------------------------------------------------------------------------------- the program's own --- */

/*
 * The arguments of the program the VM runs, which `Process.arguments()` answers instead of the arguments of `torb`: the
 * table of thunks calls this in place of `torb_process_arguments`. Empty until the VM sets them.
 */
static torb_list torb_machine_arguments_list = { NULL, 0u, 0u };

/*
 * What `Process.executablePath()` answers inside the VM: the path of the entry file the program runs, which is the file
 * that executes where a built program's is its binary. Unset (the length of none) outside `torb run`, where it is `torb`'s.
 */
static torb_text torb_machine_executable = { NULL, 0u, 0u };
static bool torb_machine_has_executable = false;

bool torb_machine_process_executable_path(torb_text *out) {
  if (!torb_machine_has_executable) {
    return torb_process_executable_path(out);
  }
  *out = torb_text_retained(torb_machine_executable);
  return true;
}

torb_list torb_machine_process_arguments(void) {
  if (torb_machine_arguments_list.storage == NULL) {
    torb_machine_arguments_list = torb_list_new(&torb_element_text);
    torb_make_immortal(torb_machine_arguments_list.storage);
  }
  return torb_list_retained(torb_machine_arguments_list);
}

/* `count`, then the registers of the texts. The list is static data of the run, as `argv` is of a native binary. */
static void torb_machine_set_arguments(int64_t *words, int64_t base, const int64_t *operands) {
  torb_list list = torb_list_new(&torb_element_text);
  for (int64_t index = 0; index < operands[0]; index++) {
    torb_text text;
    memcpy(&text, words + base + operands[1 + index], sizeof text);
    text = torb_text_retained(text);
    torb_list_add(&list, &text);
  }
  torb_make_immortal(list.storage);
  torb_machine_arguments_list = list;
}

/* ------------------------------------------------------------------------------------------------ the table --- */

/* The numbers of `KernelOperation`, in its order. */
enum {
  TORB_OPERATION_PANIC = 0,
  TORB_OPERATION_ARITHMETIC = 1,
  TORB_OPERATION_COMPARE = 2,
  TORB_OPERATION_CONVERT = 3,
  TORB_OPERATION_TEXT_CONCAT = 4,
  TORB_OPERATION_TEXT_EQUAL = 5,
  TORB_OPERATION_TEXT_COMPARE = 6,
  TORB_OPERATION_TEXT_BYTE_LENGTH = 7,
  TORB_OPERATION_PRINT = 8,
  TORB_OPERATION_PRINT_ERROR = 9,
  TORB_OPERATION_EXIT = 10,
  TORB_OPERATION_BLOCK_NEW = 11,
  TORB_OPERATION_CONTAINER_NEW = 12,
  TORB_OPERATION_CHANNEL_NEW = 13,
  TORB_OPERATION_RETAIN = 14,
  TORB_OPERATION_RELEASE = 15,
  TORB_OPERATION_TAKE_CLOSER = 16,
  TORB_OPERATION_FINISH_RELEASE = 17,
  TORB_OPERATION_CLOSING_BEGIN = 18,
  TORB_OPERATION_CLOSING_END = 19,
  TORB_OPERATION_CONTAINER_UNIQUE = 20,
  TORB_OPERATION_BLOCK_UNIQUE = 21,
  TORB_OPERATION_ELEMENT_ADDRESS = 22,
  TORB_OPERATION_DEFINE_LOCATION = 23,
  TORB_OPERATION_DEFINE_ELEMENT = 24,
  TORB_OPERATION_DEFINE_SHAPE = 25,
  TORB_OPERATION_LIVE_BLOCKS = 26,
  TORB_OPERATION_MAKE_IMMORTAL = 27,
  TORB_OPERATION_STACK_OVERFLOW = 28,
  TORB_OPERATION_SET_ARGUMENTS = 29,
  TORB_OPERATION_BEGIN_IMMORTAL = 30,
  TORB_OPERATION_END_IMMORTAL = 31,
  TORB_OPERATION_SANDBOX_OPEN = 32,
  TORB_OPERATION_SANDBOX_CLOSE = 33,
  TORB_OPERATION_TAKE_STOP = 34,
  TORB_OPERATION_TEXT_OUT = 35,
  TORB_OPERATION_RESERVE = 36,
  TORB_OPERATION_TASK_NEW = 37,
  TORB_OPERATION_TASK_CANCELLED = 38,
  TORB_OPERATION_TASK_AWAIT = 39,
  TORB_OPERATION_TASK_SAVE = 40,
  TORB_OPERATION_TASK_OUTCOME = 41,
  TORB_OPERATION_TASK_FINISH = 42,
  TORB_OPERATION_RUN_MAIN = 43,
  TORB_OPERATION_SCHEDULER_RUN = 44,
  TORB_OPERATION_SCHEDULER_FINISH = 45,
  TORB_OPERATION_LIST_ADDRESS = 46,
  TORB_OPERATION_REGISTER_ADDRESS = 47,
  TORB_OPERATION_LIST_SLICE_COPIED = 48,
  TORB_OPERATION_STOP_IN_CALL_BACK = 49,
  TORB_OPERATION_ON_EXIT = 50,
  TORB_OPERATION_INSTALL_FOR = 51,
  TORB_OPERATION_PORTABLE = 52,
  TORB_OPERATION_CONSTANT_LOCK = 53,
  TORB_OPERATION_CONSTANT_UNLOCK = 54,
  TORB_OPERATION_TEST_FILE = 55,
  TORB_OPERATION_TEST_FINISH = 56,
  TORB_OPERATION_NARROW_LOAD = 57,
  TORB_OPERATION_NARROW_STORE = 58,
  TORB_OPERATION_WIDEN = 59,
  TORB_OPERATION_SHARE = 60,
  TORB_OPERATION_SET_EXECUTABLE = 61
};

/* A module constant's flag set with a release, so a thread that reads it set also sees the value it guards. */
static void torb_machine_publish(int64_t *flag) {
#if defined(__GNUC__) || defined(__clang__)
  __atomic_store_n(flag, (int64_t)1, __ATOMIC_RELEASE);
#else
  *(volatile int64_t *)flag = 1;
#endif
}

/* The kinds of a crossing test of `TaskNew`, as the emitter writes them (`crossingTestsOf` of bytecode/emit.trb). */
enum {
  TORB_CROSSING_TEXT = 0,
  TORB_CROSSING_LIST = 1,
  TORB_CROSSING_MAP = 2,
  TORB_CROSSING_CLOSURE = 3
};

/*
 * Whether values may cross to another worker (docs/design/CONCURRENCY.md section 16, "What crosses a worker"): `tests`
 * is what the emitter wrote - -1 where a type never crosses, otherwise how many tests follow, each a kind and the
 * register the value is in. With `transfer` the values are given up (the frame of a task that has not run), so a block
 * only they hold crosses too; without it (the captures of a closure) only what nobody counts.
 */
static bool torb_machine_tests_pass(const int64_t *words, int64_t base, const int64_t *tests, bool transfer) {
  if (tests[0] < 0) {
    return false;
  }
  for (int64_t index = 0; index < tests[0]; index++) {
    const int64_t *test = tests + 1 + 2 * index;
    const int64_t *value = words + base + test[1];
    switch (test[0]) {
      case TORB_CROSSING_TEXT: {
        torb_text text;
        memcpy(&text, value, sizeof text);
        if (!torb_text_may_move(text, transfer)) {
          return false;
        }
        break;
      }
      case TORB_CROSSING_LIST: {
        torb_list list;
        memcpy(&list, value, sizeof list);
        if (!torb_list_may_move(list, transfer)) {
          return false;
        }
        break;
      }
      case TORB_CROSSING_MAP: {
        torb_map map;
        memcpy(&map, value, sizeof map);
        if (!torb_map_may_move(map, transfer)) {
          return false;
        }
        break;
      }
      case TORB_CROSSING_CLOSURE:
        if (!torb_closure_may_move((torb_environment *)(intptr_t)value[0])) {
          return false;
        }
        break;
      default:
        return false;
    }
  }
  return true;
}

/* ------------------------------------------------------------------------------------------------- sandboxes --- */

/*
 * What `operate` answers instead of its result when the operation stopped the script: a panic, a refusal of the
 * sandbox, the memory limit or `Process.exit` (docs/design/SCRIPTS.md section 6). No operation answers it otherwise -
 * an address, a register reference, a count and a closer's chunk are never this small.
 */
#define TORB_MACHINE_STOPPED INT64_MIN

/* The stop the last recovered operation left: its kind; its message and its site are declared with the call back. */
static int64_t torb_machine_stop_kind = 0;

/* The index of a location of the table, -1 for one that is not in it (a panic of the runtime with no site). */
static int64_t torb_machine_location_index(torb_location at) {
  if (at.path == NULL) {
    return -1;
  }
  for (size_t index = 0; index < torb_machine_location_count; index++) {
    torb_location candidate = torb_machine_location_table[index];
    if (candidate.path == at.path && candidate.line == at.line && candidate.column == at.column) {
      return (int64_t)index;
    }
  }
  return -1;
}

/* The code points of UTF-8 bytes into words, one per word. Answers how many. */
static int64_t torb_machine_code_points(int64_t *target, const uint8_t *bytes, size_t length) {
  int64_t count = 0;
  size_t at = 0u;
  while (at < length) {
    uint32_t first = bytes[at];
    uint32_t point;
    size_t size;
    if (first < 0x80u) {
      point = first;
      size = 1u;
    } else if ((first & 0xE0u) == 0xC0u && at + 1u < length) {
      point = ((first & 0x1Fu) << 6) | (bytes[at + 1u] & 0x3Fu);
      size = 2u;
    } else if ((first & 0xF0u) == 0xE0u && at + 2u < length) {
      point = ((first & 0x0Fu) << 12) | ((bytes[at + 1u] & 0x3Fu) << 6) | (bytes[at + 2u] & 0x3Fu);
      size = 3u;
    } else if (at + 3u < length) {
      point = ((first & 0x07u) << 18) | ((bytes[at + 1u] & 0x3Fu) << 12) | ((bytes[at + 2u] & 0x3Fu) << 6) |
              (bytes[at + 3u] & 0x3Fu);
      size = 4u;
    } else {
      point = 0xFFFDu;
      size = 1u;
    }
    target[count] = (int64_t)point;
    count += 1;
    at += size;
  }
  return count;
}

static int64_t torb_machine_dispatch(torb_list *list, int64_t base, torb_list code, int64_t at);

/*
 * Where an operation's words are: `at` words into `code` - or, where `code` is empty, at the address `at`, which is
 * how the loop hands over an operation of its code: by address, without a list any other thread holds too.
 */
static const int64_t *torb_machine_operands(torb_list code, int64_t at) {
  if (code.length == 0u) {
    return (const int64_t *)(intptr_t)at;
  }
  return (const int64_t *)torb_list_storage_data(code.storage) + code.offset + at;
}

/*
 * While a sandbox is open every operation runs under a recovery point of its own: a panic inside it - the program's,
 * or a stop of the sandbox - lands here instead of leaving the process, and the interpreter unwinds the script from the
 * answer. It is the one C frame that is sure to be active when a script panics, because everything the interpreter
 * cannot do on words itself is an operation.
 */
static int64_t torb_machine_guarded(torb_list *list, int64_t base, torb_list code, int64_t at) {
  torb_recovery point;
  torb_recovery *previous = torb_begin_recovery(&point);
  int64_t result;
  if (setjmp(point.destination) != 0) {
    torb_end_recovery(previous);
    torb_sandbox_leave_guard();
    if (torb_machine_restopping) {
      /* The stop of a call back, passed on: its kind, message and site are the ones the inner operation recorded */
      torb_machine_restopping = false;
      return TORB_MACHINE_STOPPED;
    }
    torb_machine_stop_kind = torb_sandbox_take_stop_kind();
    snprintf(torb_machine_stop_message, sizeof torb_machine_stop_message, "%s", point.message);
    torb_machine_stop_at = point.at;
    return TORB_MACHINE_STOPPED;
  }
  torb_sandbox_enter_guard();
  torb_sandbox_check_budget();
  result = torb_machine_dispatch(list, base, code, at);
  torb_sandbox_leave_guard();
  torb_end_recovery(previous);
  if (torb_machine_stop_pending) {
    /* The script stopped inside a task this operation's scheduler resumed: its stop, recorded there, is this one's */
    torb_machine_stop_pending = false;
    return TORB_MACHINE_STOPPED;
  }
  return result;
}

/*
 * Every call of the kernel counts what it allocates and frees as the program's (`torb_count_in_machine`): the program's
 * blocks are made and freed nowhere else, so the leak report of a run of the VM is the report of the program alone.
 */
int64_t torb_machine_operate(torb_list *list, int64_t base, torb_list code, int64_t at) {
  unsigned outside = torb_count_in_machine(1u);
  uint32_t *operating = torb_machine_operating_here();
  int64_t answer;
  *operating += 1u;
  /* Closing is the host's, never the script's: it must not be stopped by the budget the script used up */
  if (torb_sandbox_active != 0 && torb_machine_operands(code, at)[0] != TORB_OPERATION_SANDBOX_CLOSE) {
    answer = torb_machine_guarded(list, base, code, at);
  } else {
    answer = torb_machine_dispatch(list, base, code, at);
  }
  *operating -= 1u;
  (void)torb_count_in_machine(outside);
  return answer;
}

static int64_t torb_machine_dispatch(torb_list *list, int64_t base, torb_list code, int64_t at) {
  int64_t *words = torb_machine_words(list);
  const int64_t *operands = torb_machine_operands(code, at);
  int64_t operation = operands[0];
  const int64_t *o = operands + 1;
  if (operation >= TORB_MACHINE_NATIVE_BASE) {
    int64_t index = operation - TORB_MACHINE_NATIVE_BASE;
    if (index >= (int64_t)torb_machine_native_count) {
      torb_panic_text("internal error: the VM called a function of the runtime the kernel does not know",
                      torb_location_unknown);
    }
    torb_machine_natives[index](words, base, o);
    return torb_machine_queued();
  }
  switch (operation) {
    case TORB_OPERATION_PANIC: {
      torb_text message;
      memcpy(&message, words + base + o[0], sizeof message);
      torb_panic(message, torb_machine_location(o[1]));
    }
    case TORB_OPERATION_ARITHMETIC: {
      int64_t kind = o[5];
      int64_t selected = o[4];
      int64_t first = words[base + o[1]];
      int64_t second = words[base + o[2]];
      torb_location location = torb_machine_location(o[3]);
      if (kind == TORB_KIND_F32 || kind == TORB_KIND_F64) {
        words[base + o[0]] = torb_machine_floating(kind, selected & 255, first, second, location);
      } else {
        words[base + o[0]] = torb_machine_integer(kind, selected & 255, selected >= 256, first, second, location);
      }
      return 0;
    }
    case TORB_OPERATION_COMPARE:
      words[base + o[0]] = torb_machine_compare(words, base, o[1], o[2], o[3], o[4]);
      return 0;
    case TORB_OPERATION_CONVERT:
      words[base + o[0]] = torb_machine_convert(words[base + o[1]], o[2], o[3]);
      return 0;
    case TORB_OPERATION_TEXT_CONCAT: {
      torb_text result = torb_machine_concat(words, base, o);
      memcpy(words + base + o[0], &result, sizeof result);
      return 0;
    }
    case TORB_OPERATION_TEXT_EQUAL:
      words[base + o[0]] = torb_machine_compare(words, base, o[1], o[2], TORB_COMPARE_EQUAL, TORB_KIND_TEXT);
      return 0;
    case TORB_OPERATION_TEXT_COMPARE: {
      torb_text a;
      torb_text b;
      memcpy(&a, words + base + o[1], sizeof a);
      memcpy(&b, words + base + o[2], sizeof b);
      words[base + o[0]] = (int64_t)torb_text_compare(a, b);
      return 0;
    }
    case TORB_OPERATION_TEXT_BYTE_LENGTH: {
      torb_text text;
      memcpy(&text, words + base + o[1], sizeof text);
      words[base + o[0]] = torb_text_byte_length(text);
      return 0;
    }
    case TORB_OPERATION_PRINT:
      torb_machine_print(words, base, o, false);
      return 0;
    case TORB_OPERATION_PRINT_ERROR:
      torb_machine_print(words, base, o, true);
      return 0;
    case TORB_OPERATION_EXIT:
      torb_process_exit(words[base + o[0]]);
      return 0;
    case TORB_OPERATION_BLOCK_NEW: {
      /* target, shape, kind, words, count, then (offset, register, width) per field */
      size_t width = (size_t)o[3];
      void *block = torb_allocate_zeroed(sizeof(torb_header) + 8u + 8u * width, (torb_block_kind)o[2]);
      int64_t *contents = torb_machine_contents(block);
      contents[0] = o[1];
      for (int64_t field = 0; field < o[4]; field++) {
        const int64_t *placed = o + 5 + 3 * field;
        memcpy(contents + 1 + placed[0], words + base + placed[1], 8u * (size_t)placed[2]);
      }
      words[base + o[0]] = (int64_t)(intptr_t)block;
      return 0;
    }
    case TORB_OPERATION_CONTAINER_NEW: {
      /* target, kind, count, elements */
      int64_t *target = words + base + o[0];
      if (o[1] == 0) {
        torb_list made = torb_list_new(torb_machine_element(o[3]));
        memcpy(target, &made, sizeof made);
      } else if (o[1] == 1) {
        torb_map made = torb_map_new(torb_machine_element(o[3]), torb_machine_element(o[4]));
        memcpy(target, &made, sizeof made);
      } else {
        torb_set made = torb_set_new(torb_machine_element(o[3]));
        memcpy(target, &made, sizeof made);
      }
      return 0;
    }
    case TORB_OPERATION_CHANNEL_NEW: {
      torb_channel *channel =
        torb_channel_new(words[base + o[1]], torb_machine_element(o[2]), torb_machine_location(o[3]));
      words[base + o[0]] = (int64_t)(intptr_t)channel;
      return 0;
    }
    case TORB_OPERATION_RETAIN:
      torb_machine_retain_value(words + base + o[0], torb_machine_shape_at(o[1]));
      return 0;
    case TORB_OPERATION_RELEASE:
      torb_machine_release_value(words + base + o[0], torb_machine_shape_at(o[1]));
      return torb_machine_queued();
    case TORB_OPERATION_TAKE_CLOSER:
      return torb_machine_take_closer(words + base + o[0]);
    case TORB_OPERATION_FINISH_RELEASE:
      torb_machine_finish_release((void *)(intptr_t)words[base + o[0]]);
      return torb_machine_queued();
    case TORB_OPERATION_CLOSING_BEGIN: {
      /* `torb_closing_begin` expects the count the last release left in C, which is zero */
      torb_header *header = (torb_header *)(intptr_t)words[base + o[0]];
      header->count = 0;
      torb_closing_begin(header);
      return 0;
    }
    case TORB_OPERATION_CLOSING_END: {
      /* `torb_closing_end` takes the lent count back to zero; `FinishRelease` frees through the last count */
      torb_header *header = (torb_header *)(intptr_t)words[base + o[0]];
      torb_closing_end(header);
      header->count = 1;
      return 0;
    }
    case TORB_OPERATION_CONTAINER_UNIQUE: {
      /* kind, the register holding the reference */
      void *place = torb_machine_address(words, words[base + o[1]]);
      if (o[0] == 0) {
        torb_list_make_unique((torb_list *)place);
      } else if (o[0] == 1) {
        torb_map_make_unique((torb_map *)place);
      } else {
        torb_set_make_unique((torb_set *)place);
      }
      return 0;
    }
    case TORB_OPERATION_BLOCK_UNIQUE:
      torb_machine_block_unique(words, words[base + o[0]]);
      return 0;
    case TORB_OPERATION_ELEMENT_ADDRESS: {
      /* the register holding the container's reference, the index, the missing text, the location */
      torb_list *list = (torb_list *)torb_machine_address(words, words[base + o[0]]);
      torb_text missing;
      memcpy(&missing, words + base + o[2], sizeof missing);
      return (int64_t)(intptr_t)torb_list_element_reference(list, words[base + o[1]], missing,
                                                            torb_machine_location(o[3]));
    }
    case TORB_OPERATION_DEFINE_LOCATION: {
      torb_text path;
      memcpy(&path, words + base + o[1], sizeof path);
      torb_machine_define_location(o[0], path, o[2], o[3]);
      return 0;
    }
    case TORB_OPERATION_DEFINE_ELEMENT:
      torb_machine_define_element(o[0], o[1], o[2], o[3], o[4], o[5]);
      return 0;
    case TORB_OPERATION_DEFINE_SHAPE:
      torb_machine_define_shape(o);
      return 0;
    case TORB_OPERATION_LIVE_BLOCKS:
      return (int64_t)torb_live_block_count();
    case TORB_OPERATION_MAKE_IMMORTAL:
      torb_make_immortal((void *)(intptr_t)words[base + o[0]]);
      return 0;
    case TORB_OPERATION_STACK_OVERFLOW:
      torb_panic_stack_overflow(torb_machine_location(o[0]));
    case TORB_OPERATION_SET_ARGUMENTS:
      torb_machine_set_arguments(words, base, o);
      return 0;
    case TORB_OPERATION_SET_EXECUTABLE: {
      /* the register of the entry file's path, immortal from here on */
      torb_text path;
      memcpy(&path, words + base + o[0], sizeof path);
      torb_machine_executable = torb_text_retained(path);
      if (torb_machine_executable.storage != NULL) {
        torb_make_immortal(torb_machine_executable.storage);
      }
      torb_machine_has_executable = true;
      return 0;
    }
    case TORB_OPERATION_BEGIN_IMMORTAL:
      torb_begin_immortal();
      return 0;
    case TORB_OPERATION_END_IMMORTAL:
      torb_end_immortal();
      return 0;
    case TORB_OPERATION_RESERVE: {
      /*
       * `capacity`: the registers get storage for that many words at once, so that they never move while a call of the
       * kernel holds a pointer into them - a call back into the interpreter may grow them in the middle of one.
       */
      /* The registers are the interpreter's, and no block of the program */
      unsigned inside = torb_count_in_machine(0u);
      torb_list grown = torb_list_with_capacity(list->storage->element, o[0], torb_location_unknown);
      torb_list_add_all(&grown, *list);
      torb_list_release(*list);
      *list = grown;
      (void)torb_count_in_machine(inside);
      return 0;
    }
    case TORB_OPERATION_TASK_NEW: {
      /* target, chunk, frame words, element, count, then (register, offset, width) per argument, the tests, the copy */
      const torb_element *result = o[3] < 0 ? &torb_element_void : torb_machine_element(o[3]);
      const int64_t *tests = o + 5 + 3 * o[4];
      const int64_t *copies = tests + (tests[0] < 0 ? 1 : 1 + 2 * tests[0]);
      bool portable = false;
      if (torb_machine_portable) {
        /* `(torb_task_copies() ? <copy> : <test>) ? portable : pinned`, as the C back end writes it */
        if (copies[0] >= 0 && torb_task_copies()) {
          portable = true;
          for (int64_t index = 0; index < copies[0] && portable; index++) {
            const int64_t *copied = copies + 1 + 2 * index;
            portable = torb_machine_privatize_value(words + base + copied[0], torb_machine_shape_at(copied[1]));
          }
        } else {
          portable = torb_machine_tests_pass(words, base, tests, true);
        }
      }
      torb_task *task = torb_machine_task_new(o[1], o[2], result);
      int64_t *frame = (int64_t *)torb_task_frame(task) + 2;
      for (int64_t argument = 0; argument < o[4]; argument++) {
        const int64_t *moved = o + 5 + 3 * argument;
        memcpy(frame + moved[1], words + base + moved[0], 8u * (size_t)moved[2]);
      }
      /* An idle worker may take it where its frame may cross; otherwise it stays on the worker that makes it */
      if (portable) {
        torb_task_start_portable(task);
      } else {
        torb_task_start(task);
      }
      words[base + o[0]] = (int64_t)(intptr_t)task;
      return 0;
    }
    case TORB_OPERATION_TASK_CANCELLED:
      return torb_task_cancelled((torb_task *)(intptr_t)words[base + o[0]]) ? 1 : 0;
    case TORB_OPERATION_TASK_AWAIT: {
      torb_task *task = (torb_task *)(intptr_t)words[base + o[0]];
      torb_task *awaited = (torb_task *)(intptr_t)words[base + o[1]];
      task->state = (uint32_t)o[2];
      /* `await()` takes over a cancellation of the awaited task, `result()` only observes it (CONCURRENCY.md 8) */
      return (int64_t)(o[3] != 0 ? torb_task_observe(task, awaited) : torb_task_await(task, awaited));
    }
    case TORB_OPERATION_TASK_SAVE: {
      torb_task *task = (torb_task *)(intptr_t)words[base + o[0]];
      memcpy((int64_t *)torb_task_frame(task) + 2, words + base, 8u * (size_t)o[1]);
      return 0;
    }
    case TORB_OPERATION_TASK_OUTCOME:
      (void)torb_task_outcome((torb_task *)(intptr_t)words[base + o[0]]);
      return 0;
    case TORB_OPERATION_TASK_FINISH: {
      torb_task *task = (torb_task *)(intptr_t)words[base + o[0]];
      if (o[2] > 0) {
        /* A narrow value is the low bytes of its word, on the little-endian machines the toolchain targets */
        size_t size = 8u * (size_t)o[2];
        if (task->result != NULL && task->result->size < size) {
          size = task->result->size;
        }
        memcpy(torb_task_result_slot(task), words + base + o[1], size);
      }
      return 0;
    }
    case TORB_OPERATION_RUN_MAIN: {
      torb_task *task = torb_machine_task_new(o[0], o[1], &torb_element_void);
      uint32_t operating = *torb_machine_operating_here();
      torb_task_start(task);
      /* The scheduler runs outside every operation: what it releases is closed at once */
      *torb_machine_operating_here() = 0u;
      torb_scheduler_run(task);
      *torb_machine_operating_here() = operating;
      /* 0, or TORB_EXIT_CANCELLED where the main task ended cancelled: the exit code of the run, as main's */
      words[base + o[2]] = (int64_t)torb_task_end_main(task);
      return torb_machine_queued();
    }
    case TORB_OPERATION_SCHEDULER_RUN: {
      uint32_t operating = *torb_machine_operating_here();
      *torb_machine_operating_here() = 0u;
      torb_scheduler_run(NULL);
      *torb_machine_operating_here() = operating;
      return torb_machine_queued();
    }
    case TORB_OPERATION_SCHEDULER_FINISH: {
      uint32_t operating = *torb_machine_operating_here();
      *torb_machine_operating_here() = 0u;
      torb_scheduler_finish();
      *torb_machine_operating_here() = operating;
      return torb_machine_queued();
    }
    case TORB_OPERATION_LIST_ADDRESS:
      /* The list this operation is read from: the interpreter's code, which it reads by address from then on */
      return (int64_t)(intptr_t)((const int64_t *)torb_list_storage_data(code.storage) + code.offset);
    case TORB_OPERATION_REGISTER_ADDRESS:
      return (int64_t)(intptr_t)words;
    case TORB_OPERATION_ON_EXIT:
      /* the chunk that releases the entry cells, or -1: what a `Process.exit` of the program runs first */
      torb_machine_exit_chunk = o[0];
      torb_process_on_exit(o[0] < 0 ? NULL : torb_machine_release_on_exit);
      return 0;
    case TORB_OPERATION_INSTALL_FOR:
      /* kind, number: 0 a worker of the ring, 1 a thread of the blocking pool, 2 every interpreter but slot 0's back */
      if (o[0] == 2) {
        torb_machine_uninstall_workers();
      } else if (o[0] == 0 && o[1] >= 0 && (uint64_t)o[1] < TORB_MACHINE_RING_SLOTS) {
        torb_machine_install_slot = (size_t)o[1];
      } else if (o[0] == 1 && o[1] >= 0 && (uint64_t)o[1] < TORB_MACHINE_BLOCKING_SLOTS) {
        torb_machine_install_slot = (size_t)TORB_MACHINE_RING_SLOTS + (size_t)o[1];
      }
      return 0;
    case TORB_OPERATION_PORTABLE:
      /* Whether a task whose frame may cross is started portable: only while every thread has an interpreter */
      torb_machine_portable = o[0] != 0;
      return 0;
    case TORB_OPERATION_CONSTANT_LOCK:
      /* The first build of a module constant, which two workers may ask for at once (torb.h, torb_constant_ready) */
      torb_constant_lock();
      return 0;
    case TORB_OPERATION_CONSTANT_UNLOCK:
      /* the address of the cell's flag, and whether the value is in place: the flag is published with a release */
      if (o[1] != 0) {
        torb_machine_publish((int64_t *)(intptr_t)o[0]);
      }
      torb_constant_unlock();
      return 0;
    case TORB_OPERATION_TEST_FILE: {
      /* the register of the file's text: the line `torb test` prints in front of the file's tests */
      torb_text file;
      memcpy(&file, words + base + o[0], sizeof file);
      torb_test_file(file.length == 0u ? "" : (const char *)file.storage->data + file.offset, (size_t)file.length);
      return 0;
    }
    case TORB_OPERATION_TEST_FINISH:
      return (int64_t)torb_test_finish();
    case TORB_OPERATION_NARROW_LOAD:
      /* target, the element's address, its narrow code */
      words[base + o[0]] = torb_machine_widen((const void *)(intptr_t)o[1], o[2]);
      return 0;
    case TORB_OPERATION_NARROW_STORE:
      /* the element's address, the register, its narrow code */
      torb_machine_narrow((void *)(intptr_t)o[0], words[base + o[1]], o[2]);
      return 0;
    case TORB_OPERATION_WIDEN: {
      /* the register of a reference to a word the runtime wrote an element's narrow bytes into, the narrow code */
      int64_t *word = (int64_t *)torb_machine_address(words, words[base + o[0]]);
      *word = torb_machine_widen(word, o[1]);
      return 0;
    }
    case TORB_OPERATION_SHARE:
      /* the register of the environment, then the tests of its captures */
      if (torb_machine_tests_pass(words, base, o + 1, false)) {
        torb_share((void *)(intptr_t)words[base + o[0]]);
      }
      return 0;
    case TORB_OPERATION_STOP_IN_CALL_BACK:
      /* The script stopped inside the call back that is answering: the call back passes the stop on (see there) */
      torb_machine_stopped_in_call_back = true;
      return 0;
    case TORB_OPERATION_LIST_SLICE_COPIED: {
      /* The operands of the thunk of `torb_list_slice` it stands in for: target, list, from, to, location */
      torb_list list;
      torb_list sliced;
      memcpy(&list, words + base + o[1], sizeof list);
      sliced = torb_list_slice_copied(list, words[base + o[2]], words[base + o[3]], torb_machine_location(o[4]));
      memcpy(words + base + o[0], &sliced, sizeof sliced);
      return 0;
    }
    case TORB_OPERATION_SANDBOX_OPEN: {
      /* the register of the grant's text */
      torb_text grant;
      memcpy(&grant, words + base + o[0], sizeof grant);
      torb_sandbox_open(grant.length == 0u ? "" : (const char *)grant.storage->data + grant.offset,
                        (size_t)grant.length);
      torb_machine_sandbox_generations += 1;
      torb_machine_sandbox_open = torb_machine_sandbox_generations;
      torb_machine_stop_pending = false;
      return 0;
    }
    case TORB_OPERATION_SANDBOX_CLOSE:
      /*
       * A script that stopped leaves the destructors it queued behind; they belong to values nothing will release, so
       * they are dropped here without running - a stop runs nothing on the way out (docs/design/SCRIPTS.md section 6).
       */
      torb_sandbox_close();
      torb_machine_sandbox_open = 0;
      torb_machine_stop_pending = false;
      torb_machine_closers_here()->first = 0;
      torb_machine_closers_here()->count = 0;
      return 0;
    case TORB_OPERATION_TAKE_STOP: {
      /* target: the location index, the length of the message, its code points; answers the kind */
      int64_t kind = torb_machine_stop_kind;
      words[base + o[0]] = torb_machine_location_index(torb_machine_stop_at);
      words[base + o[0] + 1] = torb_machine_code_points(words + base + o[0] + 2,
                                                         (const uint8_t *)torb_machine_stop_message,
                                                         strlen(torb_machine_stop_message));
      torb_machine_stop_kind = 0;
      return kind;
    }
    case TORB_OPERATION_TEXT_OUT: {
      /* target, source: the code points of the text, one per word; answers how many */
      torb_text text;
      memcpy(&text, words + base + o[1], sizeof text);
      if (text.length == 0u) {
        return 0;
      }
      return torb_machine_code_points(words + base + o[0], text.storage->data + text.offset, (size_t)text.length);
    }
    default:
      torb_panic_text("internal error: the VM asked for an operation the kernel does not have", torb_location_unknown);
  }
}
