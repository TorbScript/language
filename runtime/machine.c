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

/*
 * The sites of the callers of the functions that track them (compiler/src/ir/sites.trb), one per such function that is
 * running on this thread, innermost last: the VM's form of the hidden argument the C back end passes. A call of one
 * leaves its site pending, the callee's entry pushes it and every return of the callee pops it again, and a location
 * operand of `TORB_MACHINE_CALLER_SITE` inside such a function is the site on top. A thread of the pool runs a task's
 * body to its next suspension, and no function that tracks its caller suspends, so the stack of one thread is the
 * stack of the functions it runs.
 */
#define TORB_MACHINE_CALLER_SITE (-2)

static _Thread_local int64_t *torb_machine_sites = NULL;
static _Thread_local size_t torb_machine_site_count = 0;
static _Thread_local size_t torb_machine_site_capacity = 0;
static _Thread_local int64_t torb_machine_site_pending = -1;

/*
 * The stack belongs to the thread and not to the program the VM runs, like the table of locations: it is allocated with
 * `realloc` and never counted, so the leak gate of the VM sees the program's blocks alone.
 */
static void torb_machine_site_push(void) {
  if (torb_machine_site_count == torb_machine_site_capacity) {
    size_t capacity = torb_machine_site_capacity == 0 ? 64u : torb_machine_site_capacity * 2u;
    int64_t *grown = (int64_t *)realloc(torb_machine_sites, capacity * sizeof(int64_t));
    if (grown == NULL) {
      torb_panic_out_of_memory(capacity * sizeof(int64_t));
    }
    torb_machine_sites = grown;
    torb_machine_site_capacity = capacity;
  }
  torb_machine_sites[torb_machine_site_count] = torb_machine_site_pending;
  torb_machine_site_count += 1u;
}

static int64_t torb_machine_current_site(void) {
  return torb_machine_site_count == 0 ? -1 : torb_machine_sites[torb_machine_site_count - 1u];
}

torb_location torb_machine_location(int64_t index) {
  if (index == TORB_MACHINE_CALLER_SITE) {
    index = torb_machine_current_site();
  }
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
 * the words decide: an element that has them gets a descriptor of its own whatever else it is, because a float or a record
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
  uint32_t count;
  if (header == NULL) {
    return;
  }
  /* Another worker may change a shared block's count at this very moment: the read is atomic, as memory.c's is */
  count = torb_atomic_peek_u32(&header->count);
  if (count == TORB_IMMORTAL_COUNT) {
    return;
  }
  if ((count & TORB_SHARED_COUNT) != 0u) {
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
 * A counted element of a container is released and retained by the runtime through the callbacks of its descriptor,
 * which receive the element and nothing else. So a descriptor the VM makes is a contextual one (torb.h, element.c),
 * whose callbacks are handed the descriptor, and it is the first member of a `torb_machine_descriptor` that says what
 * its elements are. A program has as many counted element types as it needs: the VM had a pool of callbacks written out
 * by hand before, 64 slots and then 256, and the one program `torb test --vm` makes of every test package of std/
 * outgrew the first and would have outgrown the second.
 */
typedef struct torb_machine_descriptor {
  /** First, so that the `torb_element *` the runtime holds points at the whole. */
  torb_contextual_element contextual;
  /** The shape the callbacks walk, -1 for an element with nothing counted in it. */
  int64_t shape;
  int64_t words;
  /**
   * The chunks of the program's own `equals` and `hash` the elements are compared and hashed by; -1 where the words
   * are the answer (`comparesByWords` of the bytecode: integers, `Bool`s and `Char`s).
   */
  int64_t equals;
  int64_t hash;
  /** The descriptor made before this one. */
  struct torb_machine_descriptor *next;
} torb_machine_descriptor;

/*
 * Every descriptor made so far, the newest first. It is shared by every element type of the same words, shape, `equals`
 * and `hash`: the descriptors would be the same, and a session of `torb repl` loads one continuation of the program per
 * entry, each with its own element table (the interpreter names the first of equal shapes, so a `List<Point>` of every
 * entry gets one descriptor). They live as long as the process.
 */
static torb_machine_descriptor *torb_machine_descriptors = NULL;

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

static void torb_machine_element_retain(const torb_contextual_element *contextual, void *element) {
  const torb_machine_descriptor *self = (const torb_machine_descriptor *)contextual;
  torb_machine_retain_value((int64_t *)element, torb_machine_shape_at(self->shape));
}

static void torb_machine_element_release(const torb_contextual_element *contextual, void *element) {
  const torb_machine_descriptor *self = (const torb_machine_descriptor *)contextual;
  torb_machine_release_value((int64_t *)element, torb_machine_shape_at(self->shape));
}

/* Two elements: by their words, or by the program's `equals`, which the interpreter runs. */
static bool torb_machine_element_equals(const torb_contextual_element *contextual, const void *first,
                                        const void *second) {
  const torb_machine_descriptor *self = (const torb_machine_descriptor *)contextual;
  torb_machine_request request;
  if (self->equals < 0) {
    return torb_machine_equal_words(first, second, self->words);
  }
  request.kind = TORB_REQUEST_EQUALS;
  request.chunk = self->equals;
  request.first = (int64_t)(intptr_t)first;
  request.second = (int64_t)(intptr_t)second;
  request.words = self->words;
  return torb_machine_call_back((int64_t)(intptr_t)&request) != 0;
}

/* The hash of an element: of its words, or the program's `hash`, which the interpreter runs. */
static uint64_t torb_machine_element_hash(const torb_contextual_element *contextual, const void *value) {
  const torb_machine_descriptor *self = (const torb_machine_descriptor *)contextual;
  torb_machine_request request;
  if (self->hash < 0) {
    return torb_machine_hash_words(value, self->words);
  }
  request.kind = TORB_REQUEST_HASH;
  request.chunk = self->hash;
  request.first = (int64_t)(intptr_t)value;
  request.second = 0;
  request.words = self->words;
  return (uint64_t)torb_machine_call_back((int64_t)(intptr_t)&request);
}

/*
 * The descriptor of an element of `words` words: counted by `shape` (-1 for a trivial element), and compared and
 * hashed by the program's own `equals` and `hash` where it has them, word by word where it does not - which is exact
 * for integers, `Bool`s and `Char`s, the one kind of element the bytecode hands over without them.
 */
static const torb_element *torb_machine_counted_element(int64_t words, int64_t shape, int64_t equals, int64_t hash) {
  torb_machine_descriptor *made;
  for (made = torb_machine_descriptors; made != NULL; made = made->next) {
    if (made->shape == shape && made->words == words && made->equals == equals && made->hash == hash) {
      return &made->contextual.element;
    }
  }
  made = (torb_machine_descriptor *)calloc(1, sizeof(torb_machine_descriptor));
  if (made == NULL) {
    torb_panic_out_of_memory(sizeof(torb_machine_descriptor));
  }
  made->contextual.element.size = (uint32_t)(words * 8);
  made->contextual.element.align = 8u;
  if (shape >= 0) {
    made->contextual.element.retain = torb_contextual_retain;
    made->contextual.element.release = torb_contextual_release;
  }
  made->contextual.element.equals = torb_contextual_equals;
  made->contextual.element.hash = torb_contextual_hash;
  made->contextual.retain = torb_machine_element_retain;
  made->contextual.release = torb_machine_element_release;
  made->contextual.equals = torb_machine_element_equals;
  made->contextual.hash = torb_machine_element_hash;
  made->shape = shape;
  made->words = words;
  made->equals = equals;
  made->hash = hash;
  made->next = torb_machine_descriptors;
  torb_machine_descriptors = made;
  return &made->contextual.element;
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

/*
 * A closure's environment at the crossing (the C back end's `torb_closure_privatize`): nothing where it is shared;
 * otherwise the block itself where only this closure holds it, else a block of its own with every word retained, and
 * then every capture made private in place by the shape of the contents. False where a capture cannot be copied - a
 * captured `var` is a box, which the emitter never asks to copy - and the block made for it is given back.
 */
static bool torb_machine_privatize_environment(int64_t *place) {
  void *block = (void *)(intptr_t)place[0];
  torb_header *header = (torb_header *)block;
  const torb_machine_shape *shape;
  int64_t *contents;
  void *made;
  if (torb_closure_may_move((torb_environment *)block)) {
    return true;
  }
  if (header->kind == (uint16_t)TORB_BLOCK_FRAME_ENVIRONMENT) {
    return false;
  }
  contents = torb_machine_contents(block);
  shape = torb_machine_shape_at(contents[0]);
  if (shape->closer >= 0) {
    return false;
  }
  if (header->count == 1u) {
    return torb_machine_privatize_value(contents + 1, shape);
  }
  made = torb_allocate_zeroed(sizeof(torb_header) + 8u + 8u * (size_t)shape->width, (torb_block_kind)header->kind);
  memcpy(torb_machine_contents(made), contents, 8u + 8u * (size_t)shape->width);
  torb_machine_retain_value(torb_machine_contents(made) + 1, shape);
  if (!torb_machine_privatize_value(torb_machine_contents(made) + 1, shape)) {
    torb_machine_release_block(made);
    return false;
  }
  torb_machine_release_block(block);
  place[0] = (int64_t)(intptr_t)made;
  torb_pool_count_copy();
  return true;
}

static bool torb_machine_privatize_element(const void *context, void *element) {
  const torb_machine_descriptor *self = (const torb_machine_descriptor *)context;
  return torb_machine_privatize_value((int64_t *)element, torb_machine_shape_at(self->shape));
}

/*
 * What makes one element of a container private, and what it is given beside the element: nothing for a plain one,
 * the runtime's for a text, and for a descriptor the VM made, the walk of its shape with the descriptor.
 */
static bool torb_machine_privatizer_of(const torb_element *element, torb_privatize_with_function *found,
                                       const void **context) {
  *context = element;
  if (element == &torb_element_text) {
    *found = torb_text_privatize_with;
    return true;
  }
  if (element->retain == NULL) {
    *found = NULL;
    return true;
  }
  if (element->retain == torb_contextual_retain &&
      ((const torb_contextual_element *)(const void *)element)->retain == torb_machine_element_retain) {
    *found = torb_machine_privatize_element;
    return true;
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
      torb_privatize_with_function element;
      const void *context;
      if (list->storage == NULL) {
        return true;
      }
      return torb_machine_privatizer_of(list->storage->element, &element, &context) &&
             torb_list_privatize_with(list, element, context);
    }
    case TORB_COUNTED_MAP:
    case TORB_COUNTED_SET: {
      torb_map *map = (torb_map *)place;
      torb_privatize_with_function key;
      torb_privatize_with_function item;
      const void *keyContext;
      const void *itemContext;
      if (map->storage == NULL) {
        return true;
      }
      return torb_machine_privatizer_of(map->storage->key, &key, &keyContext) &&
             torb_machine_privatizer_of(map->storage->value, &item, &itemContext) &&
             torb_map_privatize_with(map, key, keyContext, item, itemContext);
    }
    case TORB_COUNTED_TASK:
    case TORB_COUNTED_CHANNEL:
      /* Shared blocks that hand out plain values alone, or the emitter would not have asked */
      return true;
    case TORB_COUNTED_ENVIRONMENT:
      return torb_machine_privatize_environment(place);
    case TORB_COUNTED_NESTED:
      return torb_machine_privatize_value(place, torb_machine_shape_at(word->shape));
    default:
      return false;
  }
}

static bool torb_machine_privatize_value(int64_t *value, const torb_machine_shape *shape) {
  for (size_t index = 0; index < shape->common.count; index++) {
    if (!torb_machine_privatize_word(value, &shape->common.words[index])) {
      return false;
    }
  }
  /* A variant: the words of the case it is, as the C back end switches on its tag */
  if (shape->tag >= 0) {
    int64_t variant = value[shape->tag];
    if (variant >= 0 && (size_t)variant < shape->group_count) {
      const torb_machine_group *group = &shape->groups[variant];
      for (size_t index = 0; index < group->count; index++) {
        if (!torb_machine_privatize_word(value, &group->words[index])) {
          return false;
        }
      }
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
  TORB_OPERATION_SET_EXECUTABLE = 61,
  TORB_OPERATION_POOL_DEFAULTS = 62,
  TORB_OPERATION_INDEX_OUT_OF_BOUNDS = 63,
  TORB_OPERATION_SITE_PENDING = 64,
  TORB_OPERATION_SITE_PENDING_CURRENT = 65,
  TORB_OPERATION_SITE_PUSH = 66,
  TORB_OPERATION_SITE_POP = 67,
  TORB_OPERATION_LIST_ITEM_ADDRESS = 68,
  TORB_OPERATION_TAKE_INPUTS = 69,
  TORB_OPERATION_IMMORTAL_COPY = 70
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
  TORB_CROSSING_CLOSURE = 3,
  /* A variant with a counted case, walked by its shape: the kind is this plus the shape's index times 8 */
  TORB_CROSSING_VALUE = 4
};

static bool torb_machine_value_may_move(const int64_t *value, const torb_machine_shape *shape, bool transfer);

/* One counted word of a value the emitter lets cross where its tests pass: `crossingTestsOf` of bytecode/emit.trb. */
static bool torb_machine_word_may_move(const int64_t *value, const torb_machine_word *word, bool transfer) {
  const int64_t *place = value + word->offset;
  switch (word->kind) {
    case TORB_COUNTED_TEXT: {
      torb_text text;
      memcpy(&text, place, sizeof text);
      return torb_text_may_move(text, transfer);
    }
    case TORB_COUNTED_LIST: {
      torb_list list;
      memcpy(&list, place, sizeof list);
      return torb_list_may_move(list, transfer);
    }
    case TORB_COUNTED_MAP:
    case TORB_COUNTED_SET: {
      torb_map map;
      memcpy(&map, place, sizeof map);
      return torb_map_may_move(map, transfer);
    }
    case TORB_COUNTED_TASK:
    case TORB_COUNTED_CHANNEL:
      /* Shared blocks that hand out plain values alone, or the emitter would not have asked */
      return true;
    case TORB_COUNTED_ENVIRONMENT:
      return torb_closure_may_move((torb_environment *)(intptr_t)place[0]);
    case TORB_COUNTED_NESTED:
      return torb_machine_value_may_move(place, torb_machine_shape_at(word->shape), transfer);
    default:
      return false;
  }
}

/* A value by its shape: the common words, and for a variant the words of the case it is. */
static bool torb_machine_value_may_move(const int64_t *value, const torb_machine_shape *shape, bool transfer) {
  for (size_t index = 0; index < shape->common.count; index++) {
    if (!torb_machine_word_may_move(value, &shape->common.words[index], transfer)) {
      return false;
    }
  }
  if (shape->tag >= 0) {
    int64_t variant = value[shape->tag];
    if (variant >= 0 && (size_t)variant < shape->group_count) {
      const torb_machine_group *group = &shape->groups[variant];
      for (size_t index = 0; index < group->count; index++) {
        if (!torb_machine_word_may_move(value, &group->words[index], transfer)) {
          return false;
        }
      }
    }
  }
  return true;
}

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
        if (test[0] % 8 != TORB_CROSSING_VALUE ||
            !torb_machine_value_may_move(value, torb_machine_shape_at(test[0] / 8), transfer)) {
          return false;
        }
        break;
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
      /* the register holding the container's reference, the index, the location */
      torb_list *list = (torb_list *)torb_machine_address(words, words[base + o[0]]);
      return (int64_t)(intptr_t)torb_list_element_address(list, words[base + o[1]], torb_machine_location(o[2]));
    }
    case TORB_OPERATION_INDEX_OUT_OF_BOUNDS:
      /* the registers of the index and the length, the location */
      torb_panic_index_out_of_bounds(words[base + o[0]], words[base + o[1]], torb_machine_location(o[2]));
    case TORB_OPERATION_SITE_PENDING:
      /* the location a call hands the function that tracks its caller */
      torb_machine_site_pending = o[0];
      return 0;
    case TORB_OPERATION_SITE_PENDING_CURRENT:
      torb_machine_site_pending = torb_machine_current_site();
      return 0;
    case TORB_OPERATION_SITE_PUSH:
      torb_machine_site_push();
      return 0;
    case TORB_OPERATION_LIST_ITEM_ADDRESS: {
      /* the register holding the list's reference, the index, the location: an item that is only read */
      torb_list *list = (torb_list *)torb_machine_address(words, words[base + o[0]]);
      return (int64_t)(intptr_t)torb_list_at(*list, words[base + o[1]], torb_machine_location(o[2]));
    }
    case TORB_OPERATION_SITE_POP:
      if (torb_machine_site_count > 0) {
        torb_machine_site_count -= 1u;
      }
      return 0;
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
    case TORB_OPERATION_POOL_DEFAULTS:
      /* workers, blocking: `tasks { ... }` of the program's `project.trb`, before the first task of the program */
      torb_workers_prefer((uint32_t)o[0], (uint32_t)o[1]);
      return 0;
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
    case TORB_OPERATION_IMMORTAL_COPY: {
      /*
       * register, shape: what an entry cell holds, as the C back end's `immortalCopyOf` makes it - one more count on
       * every block of the value first, so the copy at the crossing finds each of them held twice and copies it, inside
       * an immortal region; then the count the register held on the original goes back
       */
      const torb_machine_shape *shape = torb_machine_shape_at(o[1]);
      int64_t *value = words + base + o[0];
      size_t bytes = 8u * (size_t)(shape->width < 1 ? 1 : shape->width);
      int64_t *original = (int64_t *)torb_raw_allocate(bytes);
      memcpy(original, value, bytes);
      torb_machine_retain_value(value, shape);
      torb_begin_immortal();
      (void)torb_machine_privatize_value(value, shape);
      torb_end_immortal();
      torb_machine_release_value(original, shape);
      torb_raw_free(original, bytes);
      return torb_machine_queued();
    }
    case TORB_OPERATION_TAKE_INPUTS: {
      /* target: what the sandbox opened last recorded (`torb_sandbox_recorded`), as a text the host reads out */
      size_t length = 0u;
      const char *recorded = torb_sandbox_recorded(&length);
      torb_text text = torb_text_from_bytes((const uint8_t *)recorded, length, torb_location_unknown);
      memcpy(words + base + o[0], &text, sizeof text);
      return 0;
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
