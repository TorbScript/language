/*
 * torb.h - the public ABI of the TorbScript runtime.
 *
 * Everything the C back end emits talks to this header and to nothing else. It is portable C11 with no dependency
 * beyond libc, and it is kept MSVC compatible: no GNU extension outside an `#if`, no variable length array, no
 * `__int128` without a fallback, `_Noreturn` and `_Alignof` behind a macro.
 *
 * Ownership is written down at every function. The three words are the ones docs/BACKEND.md section 2 uses:
 *
 *   borrowed  The function reads the value and does not change its reference count. The caller keeps the count and
 *             must keep the value alive for the duration of the call.
 *   owned     The result carries a count the caller now owns and must release exactly once.
 *   consumed  The function takes over the count of that argument. The caller must not release it afterwards.
 *
 * A `torb_location` is always borrowed: it points into static data the emitter wrote.
 */

#ifndef TORB_H
#define TORB_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------------------------------- portability --- */

#if defined(_MSC_VER)
#  define TORB_NORETURN __declspec(noreturn)
#  define TORB_ALIGN_OF(type) __alignof(type)
#else
#  define TORB_NORETURN _Noreturn
#  define TORB_ALIGN_OF(type) _Alignof(type)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define TORB_HAS_OVERFLOW_BUILTINS 1
#else
#  define TORB_HAS_OVERFLOW_BUILTINS 0
#endif

#if defined(__SIZEOF_INT128__)
#  define TORB_HAS_INT128 1
#else
#  define TORB_HAS_INT128 0
#endif

/*
 * gcc 16 and later: no `-Warray-bounds`. What it reports in the generated C is a path gcc wrote itself.
 * `-fspeculatively-call-stored-functions` (new in 16, on at -O2) sees that a closure's `code` is stored in one place of
 * the program as a function it can name, and turns *every* call through a `torb_closure` into "if the code is that
 * function, run its inlined body, else call the code". The guessed function has another type than the one the call
 * site converts the code back to - a `bool` result read as the call's `torb_text` or record - so the guessed branch
 * reads past a `_Bool`, and gcc reports it ("array subscript 'torb_text[0]' is partly outside array bounds of
 * '_Bool[1]'"). The branch is never taken: a closure's type says what its code is, and a call site only ever holds
 * closures of its own type. So the warning is a false positive about gcc's own speculation, and it is off for gcc 16
 * and later rather than for every compiler; a real out-of-bounds access still has clang and gcc 13 to find it.
 */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16
#  pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#if defined(_MSC_VER)
#  define TORB_UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
#  define TORB_UNREACHABLE() __builtin_unreachable()
#else
#  define TORB_UNREACHABLE() ((void)0)
#endif

/* The emitter writes this after a call of a function whose result is `Never`, so no compiler misses a return. */

/* --------------------------------------------------------------------------------------- the block header --- */

/**
 * Every counted block begins with this. Eight bytes, so the payload of a block stays aligned for anything.
 *
 * `count` is the plain, non-atomic reference count: every task owns its heap, so nothing has to be atomic
 * (BACKEND 2.5). `TORB_IMMORTAL_COUNT` marks static data, which is never retained, never released and never freed -
 * so a write through a static value always copies.
 *
 * `TORB_SHARED_COUNT` is the one exception, a bit in the count of the few blocks that may be held on two workers at
 * once: a `Task`, a `Channel`, and the environment of a closure whose captures may all cross (docs/design/CONCURRENCY.md
 * section 16, "What crosses a worker"). While the worker pool runs more than one thread, retaining and releasing such a
 * block is an atomic operation; every other count stays a plain integer, and none of them ever has the bit.
 *
 * `color` is `TORB_COLOR_NONE` for every block. It was reserved for a cycle collector, and there will be none
 * (docs/design/DESTRUCTORS.md section 9); the field stays until the header is next reorganised, because removing it changes
 * the layout. `kind` is for the leak report and for assertions.
 */
typedef struct torb_header {
  uint32_t count;
  uint16_t kind;
  uint16_t color;
} torb_header;

#define TORB_IMMORTAL_COUNT 0xFFFFFFFFu
/** The bit of a count that says "retained and released atomically once there are threads". Never set on an immortal. */
#define TORB_SHARED_COUNT 0x80000000u

typedef enum torb_block_kind {
  TORB_BLOCK_RAW = 0,            /**< A side buffer of a container: no header, only counted by the leak counter. */
  TORB_BLOCK_BYTES = 1,          /**< `torb_bytes`: the storage of a `String`. */
  TORB_BLOCK_LIST_STORAGE = 2,
  TORB_BLOCK_MAP_STORAGE = 3,
  TORB_BLOCK_RECORD = 4,         /**< A `Boxed` layout the emitter allocated. */
  TORB_BLOCK_BOX = 5,            /**< `Box(Item)`: a captured `var` binding. */
  TORB_BLOCK_SHARED = 6,         /**< A `shared type` object. */
  TORB_BLOCK_LAZY = 7,
  TORB_BLOCK_OBJECT = 8,         /**< The boxed payload of a trait-typed value. */
  TORB_BLOCK_ENVIRONMENT = 9,    /**< A closure environment on the heap. */
  TORB_BLOCK_TASK = 10,          /**< `torb_task`: a `Task`, its frame and its result (torb_task.h). */
  TORB_BLOCK_CHANNEL = 11,       /**< `torb_channel`: a `Channel` (torb_task.h). */
  /**
   * A closure environment that lives on the frame of the function that made it.
   *
   * It is counted like any other environment - the captures inside it are released when the last closure value that
   * holds it goes away - and it is never freed, because the storage is a local of a C function and not a block.
   */
  TORB_BLOCK_FRAME_ENVIRONMENT = 12
} torb_block_kind;

typedef enum torb_color {
  TORB_COLOR_NONE = 0,
  TORB_COLOR_BLACK = 1,
  TORB_COLOR_GRAY = 2,
  TORB_COLOR_WHITE = 3,
  TORB_COLOR_PURPLE = 4
} torb_color;

/** The header the emitter writes for static data: `static const ... = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), ... }` */
#define TORB_IMMORTAL_HEADER(block_kind) \
  { TORB_IMMORTAL_COUNT, (uint16_t)(block_kind), (uint16_t)TORB_COLOR_NONE }

/**
 * The shape the emitter writes for a string literal: an immortal `torb_bytes` in read-only data, with the byte count
 * spelled out so the flexible array member of `torb_bytes` can be initialized.
 *
 *     TORB_LITERAL_BYTES(s_greeting, 5) = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 5, { 'h','e','l','l','o' } };
 *     torb_text greeting = torb_text_from_storage(&s_greeting, 0, 5);
 */
#define TORB_LITERAL_BYTES(name, byte_count) \
  static const struct { torb_header header; uint32_t capacity; uint8_t data[byte_count]; } name

/* ------------------------------------------------------------------------------------------------- locations --- */

/**
 * Where something is in the source. The path is relative to the workspace root, with forward slashes, so no path of a
 * machine ever reaches the output. Always borrowed: it points into static data.
 */
typedef struct torb_location {
  const char *path;
  uint32_t line;
  uint32_t column;
} torb_location;

#define TORB_LOCATION(source_path, source_line, source_column) \
  ((torb_location){ (source_path), (uint32_t)(source_line), (uint32_t)(source_column) })

/** For a call the emitter cannot attribute to a source position, and for the runtime's own internal panics. */
extern const torb_location torb_location_unknown;

/* ---------------------------------------------------------------------------------------------------- panics --- */

/**
 * A panic writes
 *
 *     panic: <message>
 *       at path/file.trb:12:5
 *
 * to stderr and leaves with exit code 101. Nothing else runs: no release, no `Close`, no destructor (decided gap 9).
 */
#define TORB_PANIC_EXIT_CODE 101

/**
 * Running out of memory ends the program the way a panic does - `panic: out of memory: ...` on stderr, nothing else
 * runs - but with this exit code, so a script can tell a program that hit its memory limit (`TORB_MEMORY_LIMIT`,
 * memory.c) from one that failed an assertion. No recovery point catches it: a recovered panic frees nothing, so every
 * test after it would run at the limit as well.
 */
#define TORB_OUT_OF_MEMORY_EXIT_CODE 102

struct torb_text;

/** `message` borrowed. Does not return. This is the `Panic` instruction of the IR. */
TORB_NORETURN void torb_panic(struct torb_text message, torb_location at);

/** The same, for the runtime's own messages. `message` is a NUL-terminated UTF-8 C string, borrowed. */
TORB_NORETURN void torb_panic_text(const char *message, torb_location at);

/** `operation` is the written operator (`"+"`, `"*"`, `"<<"`), borrowed. */
TORB_NORETURN void torb_panic_overflow(const char *operation, torb_location at);
TORB_NORETURN void torb_panic_division_by_zero(const char *operation, torb_location at);
TORB_NORETURN void torb_panic_shift_amount(int64_t amount, int64_t width, torb_location at);
TORB_NORETURN void torb_panic_negative_exponent(int64_t exponent, torb_location at);
TORB_NORETURN void torb_panic_index_out_of_bounds(int64_t index, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_range_reversed(int64_t from, int64_t to, torb_location at);
TORB_NORETURN void torb_panic_offset_past_end(int64_t offset, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_offset_inside_character(int64_t offset, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_invalid_utf8(int64_t offset, torb_location at);
TORB_NORETURN void torb_panic_out_of_memory(size_t size);
TORB_NORETURN void torb_panic_stack_overflow(torb_location at);

/**
 * The stack check. `TORB_CHECK_STACK(at)` is the first statement of every emitted function that calls program code, and
 * it panics with `stack overflow` where the stack of the running thread has come closer to its end than a reserve: the
 * room the panic itself and a leaf below the check need. So a recursion that is too deep is a panic with exit code 101
 * and a site, and never the crash the operating system answers when a frame lands on its guard page.
 *
 * The limit is **per thread**, because every worker of the pool runs on a stack of its own: the bottom of the running
 * thread's stack (`torb_stack_floor()`) plus `torb_stack_reserve`. On 64-bit Windows the bottom is read out of the
 * thread's TEB (the start of the stack's reservation, which is what `GetCurrentThreadStackLimits` answers too), one
 * `gs`-relative load; elsewhere it is a thread-local the thread sets when it starts - the main thread in
 * `torb_process_start` from the real bounds of its stack (`torb_platform_stack_low`), a worker from the size it was
 * created with. The reserve is zero until `torb_process_start` ran, so a runtime test never fires the check, and so does
 * a thread of a platform that says nothing about its stack (its bottom stays zero).
 *
 * Two loads and a comparison and no write, which is cheaper than a frame counter: a counter needs a decrement on every
 * way out of a function and a reset on every recovered panic, and it still crashes where frames are bigger than it
 * assumed, because a count says nothing about bytes (docs/PERFORMANCE.md, F9).
 */
extern uintptr_t torb_stack_reserve;
#if defined(_WIN64) && (defined(__x86_64__) || defined(__amd64__)) && (defined(__GNUC__) || defined(__clang__))
/* `DeallocationStack` of the TEB: the lowest address of the running thread's stack reservation. */
static inline uintptr_t torb_stack_floor(void) {
  uintptr_t floor;
  __asm__("movq %%gs:0x1478, %0" : "=r"(floor));
  return floor;
}
#elif defined(_WIN64) && defined(_M_X64) && defined(_MSC_VER)
unsigned __int64 __readgsqword(unsigned long offset);
#  pragma intrinsic(__readgsqword)
static __inline uintptr_t torb_stack_floor(void) {
  return (uintptr_t)__readgsqword(0x1478ul);
}
#else
#  if defined(_MSC_VER)
extern __declspec(thread) uintptr_t torb_thread_stack_floor;
#  else
extern _Thread_local uintptr_t torb_thread_stack_floor;
#  endif
static inline uintptr_t torb_stack_floor(void) {
  return torb_thread_stack_floor;
}
#endif
#if defined(__GNUC__) || defined(__clang__)
#  define TORB_STACK_EXHAUSTED(address) __builtin_expect((address) < torb_stack_floor() + torb_stack_reserve, 0)
#else
#  define TORB_STACK_EXHAUSTED(address) ((address) < torb_stack_floor() + torb_stack_reserve)
#endif
#define TORB_CHECK_STACK(at)                                     \
  do {                                                           \
    char torb_stack_probe;                                       \
    if (TORB_STACK_EXHAUSTED((uintptr_t)&torb_stack_probe)) {    \
      torb_panic_stack_overflow(at);                             \
    }                                                            \
  } while (0)
/** What `torb_process_start` calls: turns the check on, with the bottom of the calling thread's stack. */
void torb_set_stack_limit(void);

/**
 * A test build replaces what a panic does with this. The hook receives the whole message as it would have been
 * printed (without the trailing newline), borrowed, and is expected not to return - `runtime/tests` longjmps out of
 * it. If it does return anyway, the panic leaves with `TORB_PANIC_EXIT_CODE` after all.
 *
 * Nothing but the test harness sets one: `torb build` never installs a hook, so a panic in a real program is a panic.
 */
typedef void (*torb_panic_hook)(const char *message);
void torb_set_panic_hook(torb_panic_hook hook);

/**
 * Where a panic goes instead of leaving the process: the recovery point a test runner sets up around one test body, so
 * that a test that panics is reported and the next test still runs.
 *
 * **A recovered panic runs nothing on the way out** - no release, no `Close`, no destructor, exactly as an ordinary
 * panic runs nothing - so everything the aborted frames held stays allocated. A program that recovers therefore leaks
 * by construction and the leak gate of the conformance suite does not apply to a run with a failed test
 * (`tests/conformance/README.md`).
 *
 * `message` and `at` are filled in before the jump. The message is the panic's own, without the `panic: ` in front of
 * it, so a runner can print it in its own format.
 */
typedef struct torb_recovery {
  jmp_buf destination;
  char message[1024];
  torb_location at;
} torb_recovery;

/**
 * Makes `point` the recovery point of every panic until it is ended, and answers the one that was active before.
 * The caller calls `setjmp(point->destination)` itself, because `setjmp` may only be used in the frame that owns it.
 */
torb_recovery *torb_begin_recovery(torb_recovery *point);

/** Restores the recovery point `torb_begin_recovery` answered. Pass `NULL` to leave a panic leaving the process. */
void torb_end_recovery(torb_recovery *previous);

/* --------------------------------------------------------------------------------------------------- sandbox --- */

/*
 * The second lock of a sandboxed receiver script (sandbox.c, docs/design/SCRIPTS.md section 4). Only the VM's kernel
 * opens one; a native program never has one open, and then every function below lets everything through.
 */

/** Why a script stopped: what `torb_sandbox_take_stop_kind` answers after the kernel recovered a panic. */
enum {
  TORB_SANDBOX_PANIC = 1,
  TORB_SANDBOX_REFUSED = 2,
  TORB_SANDBOX_MEMORY = 3,
  TORB_SANDBOX_EXIT = 4
};

/** Non-zero while a sandbox is open: what the allocation functions test before they count. */
extern int torb_sandbox_active;

/** Opens a sandbox from the text of a grant (`grant` borrowed, `length` bytes). A sandbox that is open is closed first. */
void torb_sandbox_open(const char *grant, size_t length);
void torb_sandbox_close(void);
bool torb_sandbox_is_open(void);
/** A recovery point of the kernel is active from here on, so a stop may jump. The two nest. */
void torb_sandbox_enter_guard(void);
void torb_sandbox_leave_guard(void);
/** Stops the script if it allocated more than its limit while no recovery point was active. */
void torb_sandbox_check_budget(void);
/** The kind of the stop the kernel just recovered, `TORB_SANDBOX_PANIC` for a panic of the program itself. */
int64_t torb_sandbox_take_stop_kind(void);
/** Counts an allocation against the memory limit, and stops the script past it. */
void torb_sandbox_account(size_t size);
/** `Process.exit` inside a script: the script stops, the process goes on. */
TORB_NORETURN void torb_sandbox_exit(int64_t code);
/** Whether `Environment.get` may answer for the variable. True where no sandbox is open. */
bool torb_sandbox_allows_variable(const char *name);
/**
 * The NUL-terminated path a file function hands the operating system: the text itself where no sandbox is open, and
 * otherwise the path read against the base directory, normalized, checked against the roots of the side `writes`
 * names and against links - or a stop of the script. Owned; free with `torb_raw_free(result, *capacity)`.
 */
char *torb_sandbox_path(struct torb_text path, bool writes, size_t *capacity);

/* ------------------------------------------------------------------------------------------------ allocation --- */

/** Release the counted children of a block. Receives the block itself. `NULL` when the contents are trivial. */
typedef void (*torb_drop_function)(void *block);

/** Retain the counted children of a block, after a shallow copy. Receives the block. `NULL` when trivial. */
typedef void (*torb_retain_children_function)(void *block);

/**
 * A block of `size` bytes with a header of count 1. Result owned. Never returns `NULL`: an allocation that fails
 * panics (there is no out-of-memory error in the language).
 */
void *torb_allocate(size_t size, torb_block_kind kind);

/** The same, with the bytes after the header set to zero. Result owned. */
void *torb_allocate_zeroed(size_t size, torb_block_kind kind);

/**
 * A side buffer without a header: the bucket array and the entry vector of the hash table. Counted by the live-block
 * counter like everything else, so the leak test sees it. Result owned, freed with `torb_raw_free`.
 */
void *torb_raw_allocate(size_t size);
void *torb_raw_allocate_zeroed(size_t size);
void torb_raw_free(void *buffer, size_t size);

/** `block` borrowed; its count goes up by one. A `NULL` block (a niche `None`) and an immortal block are no-ops. */
void torb_retain(void *block);

/**
 * `block` consumed. When the count reaches zero, `drop` is called with the block so it can release the counted values
 * it holds, and then the block is freed. A `NULL` block and an immortal block are no-ops.
 */
void torb_release(void *block, torb_drop_function drop);

/**
 * The two halves around the destructor of an object (docs/design/DESTRUCTORS.md 2 and 8). The drop function of a
 * `shared type` that implements `Close` calls `torb_closing_begin`, then the type's `close()`, then `torb_closing_end`,
 * and only then releases the fields. `block` borrowed by both, and its count is zero when the drop function runs.
 *
 * `torb_closing_begin` lends the object a count of one while `close()` runs, so a retain and a release of `self` inside
 * it - handing it to a function that only uses it - never reach zero a second time and never run the destructor again.
 * `torb_closing_end` takes that count back, and panics where `close()` left the object with more: a new holder of an
 * object that is being released, which the checker refuses and which would otherwise be a use after free.
 */
void torb_closing_begin(void *block);
void torb_closing_end(void *block);

/** `block` borrowed. True when this is the only owner, so a write may go through in place. `NULL` is unique. */
bool torb_is_unique(const void *block);

/**
 * Make unique (BACKEND 2.1): `block` consumed, result owned and of count 1, holding the same `size` bytes.
 *
 * When the count is already 1 the same block comes back untouched. Otherwise a shallow copy is made,
 * `retain_children` runs on the copy and the old block is released with `drop`. An immortal block always copies,
 * which is what makes a write to a static value safe.
 */
void *torb_make_unique(void *block,
                       size_t size,
                       torb_retain_children_function retain_children,
                       torb_drop_function drop);

/** Turn a heap block into static data: never retained, never released, never freed. `block` consumed. */
void torb_make_immortal(void *block);

/**
 * Opens an immortal region: every block `torb_allocate` hands out until the matching `torb_end_immortal` is born
 * immortal - never retained, never released, never freed, and a write to it copies.
 *
 * It is what a module constant whose value is no static data is built in, exactly once, so that reading the constant
 * costs one load instead of one build (BACKEND, the immortal counted static). The temporaries the initializer made on
 * the way are immortal too, which is why a region is opened around a value that is built once and never around
 * ordinary code. Regions nest, because one constant may read another.
 */
void torb_begin_immortal(void);
void torb_end_immortal(void);

/**
 * The accessor of an immortal counted static (a module constant that is no static data) builds the value the first
 * time it is asked, and two workers may ask at the same time. So the first build happens under one recursive lock of the
 * process - recursive, because the initializer of one constant may read another - and the flag that says it happened is
 * published with a release and read with an acquire:
 *
 *     if (!torb_constant_ready(&k_ready)) {
 *       torb_constant_lock();
 *       if (!k_ready) {
 *         torb_begin_immortal();
 *         k_cell = k_build();
 *         torb_end_immortal();
 *         torb_constant_publish(&k_ready);
 *       }
 *       torb_constant_unlock();
 *     }
 *
 * Every later call is the one load of the first line: an acquire is a plain load on x86, and the value itself is
 * immortal, so handing it to any worker touches no count.
 */
#if defined(__GNUC__) || defined(__clang__)
static inline bool torb_constant_ready(const bool *ready) {
  return __atomic_load_n(ready, __ATOMIC_ACQUIRE);
}
#else
static inline bool torb_constant_ready(const bool *ready) {
  return *(const volatile bool *)ready;
}
#endif
void torb_constant_lock(void);
void torb_constant_unlock(void);
/** `*ready = true` with a release, so a worker that reads it also sees the cell it guards. */
void torb_constant_publish(bool *ready);

/**
 * Gives a block `TORB_SHARED_COUNT`: from here on it may be held on several workers, and its count is changed
 * atomically while the pool runs threads. `block` borrowed, and held by nobody but the caller - which is why it is
 * called right after the block was made. The emitter calls it on the environment of a closure whose captures may all
 * cross to another worker; a `Task` and a `Channel` are made with it.
 */
void torb_share(void *block);

/** How many counted blocks (including raw side buffers) are live. The leak test asserts this is zero at the end. */
size_t torb_live_block_count(void);

/** How many blocks are immortal. They are never freed by construction, so they are no part of the live count. */
size_t torb_immortal_block_count(void);

/**
 * `torb build --report-leaks`: writes the live and the immortal block count to stderr, one line each. Once the VM
 * counted a program (`torb_count_in_machine`), the counts are that program's alone, as its native binary would report
 * them, and not those of the `torb` it runs in.
 */
void torb_report_leaks(void);

/**
 * The VM's accounting (runtime/machine.c): with `inside` set, what this thread allocates and frees is also counted as
 * the program's the VM runs - the kernel sets it for its calls and clears it while it calls back into the interpreter.
 * Answers what it was before, so a call can put it back.
 */
unsigned torb_count_in_machine(unsigned inside);
/* ------------------------------------------------------------------------------------------- the memory limit --- */

/**
 * The memory limit of the process, which `torb_process_start` sets before the program runs (memory.c):
 *
 * - `TORB_MEMORY_LIMIT` in the environment: bytes, with an optional `K`, `M`, `G` or `T` (powers of 1024, a `B` or
 *   `iB` behind it allowed) - `512M`, `8G`, `1073741824`. `0` and `none` mean no limit. Anything else makes the program
 *   refuse to start, with one line that names the variable and exit code 2, as `TORB_WORKERS` does.
 * - Where it is not set, a binary of the `dev` profile (`torb test`, `torb run`: compiled with `TORB_PROFILE_DEV`) takes
 *   the smaller of 8 GiB and half the physical memory, and every other binary takes none.
 *
 * The operating system enforces it where it can (`torb_platform_limit_memory`); where it cannot, the runtime counts
 * what `torb_allocate` and `torb_raw_allocate` hold against it. Either way an allocation over it ends the program with
 * `panic: out of memory: the limit of ... was reached` and `TORB_OUT_OF_MEMORY_EXIT_CODE`.
 */
void torb_memory_limit_start(void);

/** The limit in force, in bytes: 0 for none. */
uint64_t torb_memory_limit(void);

/**
 * A size as `TORB_MEMORY_LIMIT` writes it, in `*bytes` (0 for `0` and `none`). False for anything that is not one,
 * or that does not fit 64 bits. `text` borrowed, NUL terminated.
 */
bool torb_memory_size_parse(const char *text, uint64_t *bytes);

/**
 * Makes the runtime count its own allocations against `limit` (0: stop counting), the way it does where the operating
 * system does not take the limit. For `runtime/tests`: the count starts at zero, so only what is allocated from here on
 * is held against it.
 */
void torb_memory_limit_counted(uint64_t limit);

/**
 * Writes what an allocation of `size` bytes that failed says into `buffer`: the limit that was reached where there is
 * one, the size otherwise. For `torb_panic_out_of_memory`.
 */
void torb_memory_describe_exhaustion(char *buffer, size_t capacity, size_t size);

/* ---------------------------------------------------------------------------------------- element descriptors --- */

/**
 * What one element type of a runtime container is, as static data (BACKEND 3.1). There is one list and one hash table
 * in C, parameterized by this instead of by a template, so the VM and the C back end share them.
 *
 * `retain` and `release` act on one element in place and are `NULL` when the element is trivial, which is what lets
 * the container skip the indirect call entirely. `equals` and `hash` are only needed for a map key or a set item.
 */
typedef struct torb_element {
  uint32_t size, align;
  void (*retain)(void *);
  void (*release)(void *);
  bool (*equals)(const void *, const void *);
  uint64_t (*hash)(const void *);
} torb_element;

/*
 * A descriptor whose callbacks are told which descriptor they serve (element.c). The callbacks of `element` are
 * `torb_contextual_retain` and its siblings, which every such descriptor shares, and they call the four below with the
 * descriptor that `torb_element_calling` names. That is what the VM needs: it makes its descriptors at run time, one per
 * element type of the program, and it cannot make a C function for each (machine.c, "counted elements").
 */
typedef struct torb_contextual_element torb_contextual_element;
struct torb_contextual_element {
  /** First, so that a `torb_element *` of it points at the whole. */
  torb_element element;
  void (*retain)(const torb_contextual_element *self, void *element);
  void (*release)(const torb_contextual_element *self, void *element);
  bool (*equals)(const torb_contextual_element *self, const void *first, const void *second);
  uint64_t (*hash)(const torb_contextual_element *self, const void *value);
};
void torb_contextual_retain(void *element);
void torb_contextual_release(void *element);
bool torb_contextual_equals(const void *first, const void *second);
uint64_t torb_contextual_hash(const void *value);
/** The descriptor whose callback this thread is calling; set by the four functions below, for a contextual one. */
#if defined(_MSC_VER)
extern __declspec(thread) const torb_element *torb_element_calling;
#else
extern _Thread_local const torb_element *torb_element_calling;
#endif

/*
 * A call of the runtime through a descriptor goes through these four and never straight to the function pointer, so
 * a contextual descriptor learns which one it is. Any other descriptor costs one comparison.
 */
static inline void torb_element_retain(const torb_element *element, void *value) {
  if (element->retain == torb_contextual_retain) {
    torb_element_calling = element;
  }
  element->retain(value);
}

static inline void torb_element_release(const torb_element *element, void *value) {
  if (element->release == torb_contextual_release) {
    torb_element_calling = element;
  }
  element->release(value);
}

static inline bool torb_element_equals(const torb_element *element, const void *first, const void *second) {
  if (element->equals == torb_contextual_equals) {
    torb_element_calling = element;
  }
  return element->equals(first, second);
}

static inline uint64_t torb_element_hash(const torb_element *element, const void *value) {
  if (element->hash == torb_contextual_hash) {
    torb_element_calling = element;
  }
  return element->hash(value);
}

/** The descriptors the runtime needs for itself. The emitter emits one per element type, prefix `d`. */
extern const torb_element torb_element_int64;
extern const torb_element torb_element_text;
/** Size zero: the value descriptor of a `Set`, which is the table with nothing on the value side. */
extern const torb_element torb_element_unit;

/* ------------------------------------------------------------------------------------- the ABI of the values --- */

/** The storage of a `String`: UTF-8 bytes with a capacity. A literal is an immortal one in read-only data. */
typedef struct torb_bytes {
  torb_header header;
  uint32_t capacity;
  uint8_t data[];
} torb_bytes;

/**
 * `String`. A slice of one `torb_bytes`, so `text[3..]` is O(1) and shares the storage, and `byteLength()` is a field
 * read. Always valid UTF-8 (decided gap 7). No small-string optimization in v1.
 */
typedef struct torb_text {
  torb_bytes *storage;
  uint32_t offset;
  uint32_t length;
} torb_text;

/** `Char`: a Unicode scalar value. */
typedef uint32_t torb_char;

/** `Void` as a value. */
typedef uint8_t torb_void;

typedef struct torb_list_storage {
  torb_header header;
  const torb_element *element;
  uint32_t length;
  uint32_t capacity;
  /* The elements begin at `torb_list_storage_data`, which rounds this struct's size up to the element's alignment. */
} torb_list_storage;

/** `ArrayList<Item>` and `TrieList<Item>`: contiguous storage, an offset and a length. A slice shares the storage. */
typedef struct torb_list {
  torb_list_storage *storage;
  uint32_t offset;
  uint32_t length;
} torb_list;

/**
 * Open addressing over a bucket array of indices, plus a separate insertion-ordered entry vector. So iteration is
 * insertion order (decided gap 6), a removal leaves a tombstone and does not reorder, and compaction happens when
 * the tombstones pass half of the entries.
 */
typedef struct torb_map_storage {
  torb_header header;
  const torb_element *key;
  const torb_element *value;
  int32_t *buckets;           /**< `bucket_count` slots; -1 is empty. Indices into the entry vector. */
  uint8_t *entries;           /**< `entry_capacity * entry_stride` bytes. */
  uint32_t bucket_count;      /**< A power of two, or zero while the table is empty. */
  uint32_t entry_count;       /**< Entry slots in use, tombstones included. The insertion order is this order. */
  uint32_t entry_capacity;
  uint32_t live_count;        /**< Entries that are not tombstones: what `length()` answers. */
  uint32_t key_offset, value_offset, entry_stride;
} torb_map_storage;

/** `HashMap<Key, Value>` and `TrieMap<Key, Value>`: one counted storage pointer. */
typedef struct torb_map {
  torb_map_storage *storage;
} torb_map;

/** `HashSet<Item>` and `TrieSet<Item>`: the table with `torb_element_unit` on the value side. */
typedef torb_map torb_set;

/**
 * A closure environment. The captures follow the header and the drop function; the emitter generates the typed struct
 * per closure and only these two fields are ABI.
 *
 * The `drop` pointer is here and not at the release site, because a closure **value** has the type of every closure of
 * its shape: which captures are inside one, and therefore which of them a release has to release, is only known to the
 * closure that built it.
 */
typedef struct torb_environment {
  torb_header header;
  torb_drop_function drop;
} torb_environment;

/**
 * The erased shape of a closure value. The emitter generates one struct per signature
 * (`struct { R (*code)(torb_environment *, ...); torb_environment *environment; }`); this is what the runtime passes
 * around when it does not need the signature, and the two are layout compatible.
 */
typedef struct torb_closure {
  void (*code)(void);
  torb_environment *environment;
} torb_closure;

/**
 * `environment` consumed: one count less, and the captures inside it released when it was the last one. This is
 * `Release` for a closure slot.
 *
 * It is a function of the runtime and not two lines in the generated code, because the drop function has to be read out
 * of the block *after* the null check - a closure without captures has no environment at all, and every call site would
 * otherwise repeat that conditional.
 */
void torb_environment_release(torb_environment *environment);

/**
 * The header of an environment that the emitter put on the **frame** of the function that made the closure: count
 * one, `TORB_BLOCK_FRAME_ENVIRONMENT`, and the drop of its layout.
 *
 * It is a whole environment in every other way - the captures in it are owned by it and released when the last
 * closure value that holds it goes away - and only the storage is different, which is what makes a closure the callee
 * cannot keep cost no allocation at all (docs/PERFORMANCE.md, finding 7). The emitter only takes this shape where the
 * IR proves the closure does not leave the frame, because a pointer to a local outlives nothing.
 */
void torb_environment_on_frame(torb_environment *environment, torb_drop_function drop);

/** The boxed payload of a trait-typed value: a witness member takes `void *self`, so the payload is always boxed. */
typedef struct torb_object {
  torb_header header;
} torb_object;

/** `Box(Item)`: a captured `var` binding. The item follows the header. Colored, because a box may be in a cycle. */
typedef struct torb_box {
  torb_header header;
} torb_box;

typedef enum torb_lazy_state {
  TORB_LAZY_PENDING = 0,
  TORB_LAZY_FORCED = 1
} torb_lazy_state;

/** `lazy Value` as a parameter: a one-shot memo cell. The thunk and the value follow, typed by the emitter. */
typedef struct torb_lazy {
  torb_header header;
  uint8_t state;
} torb_lazy;

/* ------------------------------------------------------------------------------------------------------ text --- */

/** `storage` borrowed; the result borrows it too. Used by the emitter for a literal, which is immortal anyway. */
torb_text torb_text_from_storage(const void *storage, uint32_t offset, uint32_t length);

/** The empty string. Result owned (its storage is immortal, so releasing it is a no-op). */
torb_text torb_text_empty(void);

/**
 * `text` borrowed, result owned: the same slice with one more count on the storage. This is `Retain` for a `Text`
 * slot.
 */
torb_text torb_text_retained(torb_text text);

/** `text` consumed. This is `Release` for a `Text` slot. */
void torb_text_release(torb_text text);

/**
 * A fresh storage of `length` bytes, count 1. `*data_out` points at the bytes, which the caller fills with valid
 * UTF-8 before anything else looks at them. Result owned. Only the runtime builds strings this way.
 */
torb_text torb_text_allocate(uint32_t length, uint8_t **data_out);

/** `bytes` borrowed. Validates UTF-8 and panics at the first bad byte. Result owned. */
torb_text torb_text_from_bytes(const uint8_t *bytes, size_t length, torb_location at);

/**
 * `bytes` borrowed. Validates UTF-8 and answers false instead of panicking, with the byte offset of the first bad
 * byte in `*bad_offset`. `*out` owned on success. This is how reading a file becomes an `IoError` and never a
 * replacement character.
 */
bool torb_text_try_from_bytes(const uint8_t *bytes, size_t length, torb_text *out, size_t *bad_offset);

/** `text` borrowed, NUL terminated, valid UTF-8 or the call panics. Result owned. */
torb_text torb_text_from_cstring(const char *text);

int64_t torb_text_byte_length(torb_text text);
bool torb_text_is_empty(torb_text text);

/** `parts` borrowed. `Intrinsic.TextConcat`: one allocation for the whole interpolation. Result owned. */
torb_text torb_text_concat(const torb_text *parts, size_t count);

/** Which member of a `torb_text_part` holds the value, and therefore how the runtime writes it. */
typedef enum torb_part_kind {
  TORB_PART_TEXT = 0,
  TORB_PART_SIGNED = 1,     /**< `Int8` ... `Int64`, widened. */
  TORB_PART_UNSIGNED = 2,   /**< `UInt8` ... `UInt64`, widened. */
  TORB_PART_FLOATING = 3,   /**< `Float32` widened to `Float64`, which is what its `Show` does as well. */
  TORB_PART_BOOLEAN = 4,    /**< `signed_value` is 0 or 1. */
  TORB_PART_CHARACTER = 5,  /**< `unsigned_value` is the code point. */
  TORB_PART_VOID = 6
} torb_part_kind;

/**
 * One part of an interpolation: a text, or a primitive the runtime formats straight into the result.
 *
 * The members are separate rather than a union, because the emitter writes a designated initializer per part and a
 * union member would need a name in it either way. Every part that is not `TORB_PART_TEXT` is a value that no
 * `String` was ever built for, which is what makes an interpolation one allocation and not one per part
 * (docs/PERFORMANCE.md, finding 6).
 */
typedef struct torb_text_part {
  int32_t kind;
  torb_text text;
  int64_t signed_value;
  uint64_t unsigned_value;
  double floating;
} torb_text_part;

/**
 * `parts` borrowed. The same as `torb_text_concat` for parts that may still be numbers: the result is measured once
 * and every part is written into it, so an interpolation of any number of parts is **one** allocation. Result owned.
 *
 * The formatting is the same as the matching `torb_show_*` writes, byte for byte, because the conformance suite
 * compares the two back ends on exactly these strings (`tests/conformance/interpolation.trb`, `floats.trb`).
 */
torb_text torb_text_concat_parts(const torb_text_part *parts, size_t count);

/** `String.add`. Both borrowed, result owned. */
torb_text torb_text_add(torb_text first, torb_text second);

/**
 * `text[from..to]` with byte offsets. Every offset is checked (decided gap 7): an offset greater than the byte
 * length, a start greater than the end, and an offset on a UTF-8 continuation byte each panic, with the offset and
 * the length in the message. `text` borrowed, result owned and sharing the storage.
 */
torb_text torb_text_slice(torb_text text, int64_t from, int64_t to, torb_location at);

/** A storage of its own, exactly as long as needed, so a small slice stops pinning a big buffer. Result owned. */
torb_text torb_text_compact(torb_text text);

bool torb_text_equal(torb_text first, torb_text second);
/** -1, 0 or 1: the emitter maps it to `Ordering`. Byte order, which for UTF-8 is code point order. */
int32_t torb_text_compare(torb_text first, torb_text second);
uint64_t torb_text_hash(torb_text text);

bool torb_text_contains(torb_text text, torb_text part);
bool torb_text_starts_with(torb_text text, torb_text prefix);
bool torb_text_ends_with(torb_text text, torb_text suffix);
/** `indexOf`: false when the part is not there. The lowering builds the `Int?` around it. */
bool torb_text_index_of(torb_text text, torb_text part, int64_t *out);
/** `lastIndexOf`: the same for the last occurrence. An empty part is at the end of the text. */
bool torb_text_last_index_of(torb_text text, torb_text part, int64_t *out);

/** All borrowed, all results owned. */
torb_text torb_text_trim(torb_text text);
torb_text torb_text_to_upper_case(torb_text text);
torb_text torb_text_to_lower_case(torb_text text);
torb_text torb_text_replace(torb_text text, torb_text part, torb_text replacement);
torb_text torb_text_repeat(torb_text text, int64_t times, torb_location at);
/** A `List<String>`, element descriptor `torb_element_text`. Result owned. */
torb_list torb_text_split(torb_text text, torb_text separator);

/** `"a\nb"`: quoted, with escapes, as a value inside another value. Result owned. */
torb_text torb_text_show_nested(torb_text text);

/* ------------------------------------------------------------------------------------------------------ UTF-8 --- */

/** How many bytes the scalar value takes in UTF-8, 1 to 4. Zero when it is not a scalar value. */
uint32_t torb_char_byte_length(torb_char character);
/** Writes 1 to 4 bytes and answers how many. `character` must be a scalar value. */
uint32_t torb_utf8_encode(torb_char character, uint8_t out[4]);
/** True when the whole range is valid UTF-8; otherwise `*bad_offset` is the first bad byte. `bytes` borrowed. */
bool torb_utf8_validate(const uint8_t *bytes, size_t length, size_t *bad_offset);
/** True when the byte is a continuation byte, which an offset may not land on. */
bool torb_utf8_is_continuation(uint8_t byte);

/**
 * Walk the characters of a text. `*offset` starts at 0 and ends at the byte length; each call answers the character
 * at the offset and moves the offset past it. `text` borrowed. This is what `chars()` iterates.
 */
bool torb_text_next_char(torb_text text, uint32_t *offset, torb_char *character);

/**
 * The character at a byte offset, or false at (and past) the end. The one native `chars()` needs: decoding UTF-8 is
 * reading raw storage, which the language cannot do at all - there is no `text[i]` - and the cursor of `chars()` is
 * TorbScript over this plus `Char.byteLength()`, so walking a text stays O(1) per character. An offset inside a
 * character panics like every other bad offset (decided gap 7). `text` borrowed.
 */
bool torb_text_char_at(torb_text text, int64_t offset, torb_char *out);

/**
 * The byte at an offset, or false at (and past) the end: what `bytes()` iterates. The same reason as above - a `String`
 * has no index - and a byte is never inside anything, so nothing about this one can panic. `text` borrowed.
 */
bool torb_text_byte_at(torb_text text, int64_t offset, uint8_t *out);

/* ------------------------------------------------------------------------------------------------------ char --- */

bool torb_char_is_digit(torb_char character);
bool torb_char_is_letter(torb_char character);
bool torb_char_is_whitespace(torb_char character);
torb_char torb_char_to_upper_case(torb_char character);
torb_char torb_char_to_lower_case(torb_char character);
int64_t torb_char_byte_length_of(torb_char character);
/**
 * Not every number is a scalar value (the surrogates, everything above 0x10FFFF). `message` is the `.Fallible`
 * convention: the `NumberRangeError` of the failure carries one and no parameter names it, so the runtime writes the
 * value that went out of range. It is set on failure alone.
 */
bool torb_char_try_from_i64(int64_t value, torb_char *out, torb_text *message);

/* ------------------------------------------------------------------------------------------- Show, formatting --- */

/** Every result owned. */
torb_text torb_show_bool(bool value);
torb_text torb_show_char(torb_char character);
/** `'A'`: in single quotes, with escapes, as a value inside another value. */
torb_text torb_show_char_nested(torb_char character);
torb_text torb_show_i64(int64_t value);
torb_text torb_show_u64(uint64_t value);
torb_text torb_show_void(torb_void value);

/**
 * The shortest decimal string that parses back to the same `Float64` (decided gap 4), with `.0` appended when the
 * result contains neither `.` nor `e`. `nan`, `inf`, `-inf`; `-0.0` prints as `-0.0`.
 */
torb_text torb_show_f64(double value);
torb_text torb_show_f32(float value);

/** Digits in another base, 2 to 36. `text` borrowed; false when the text is not a number in that base. */
bool torb_parse_i64_digits(torb_text text, int64_t radix, int64_t *out);
/** `Int.parse`: an optional sign and decimal digits, nothing else. `text` borrowed. */
bool torb_parse_i64(torb_text text, int64_t *out);
bool torb_parse_u64(torb_text text, uint64_t *out);
/** `Float.parse`. `text` borrowed. */
bool torb_parse_f64(torb_text text, double *out);
bool torb_parse_bool(torb_text text, bool *out);

/* ------------------------------------------------------------------------------------------------------ list --- */

/** Where the elements of a storage begin: the struct's size rounded up to the element's alignment. */
size_t torb_list_storage_data_offset(const torb_element *element);
/** `storage` borrowed, the pointer borrowed with it. */
void *torb_list_storage_data(torb_list_storage *storage);

/** An empty list over `element`. `element` must be static data. Result owned. */
torb_list torb_list_new(const torb_element *element);
torb_list torb_list_with_capacity(const torb_element *element, int64_t capacity, torb_location at);

/** `list` borrowed, result owned: `Retain` for a list slot. */
torb_list torb_list_retained(torb_list list);
/** `list` consumed: `Release` for a list slot. */
void torb_list_release(torb_list list);

const torb_element *torb_list_element(torb_list list);
int64_t torb_list_length(torb_list list);

/**
 * A borrowed pointer to the element at `index`, bounds checked. Valid until the list is written through. This is
 * what a read of `list[i]` compiles to.
 */
const void *torb_list_at(torb_list list, int64_t index, torb_location at);

/** `get(index): Item?`: false when out of range, otherwise `*out` is a retained copy the caller owns. */
bool torb_list_get(torb_list list, int64_t index, void *out);

/**
 * Make unique and answer a writable interior pointer to the element at `index`. This is the `Element` path step: the
 * pointer is valid for exactly the duration of the access that formed it (BACKEND 2.3).
 *
 * `missing` is the message an index out of range panics with, and `at` the site it is reported at. Both are the
 * **language's**: `a[key]` is `Indexed.at`, whose body is `get(key).expect("Key does not exist")`, so the step hands
 * over the very static that `expect` would have been given. A runtime message of its own would make the same program
 * say two different things depending on whether the write went through a copy or through this pointer.
 */
void *torb_list_element_reference(torb_list *list, int64_t index, torb_text missing, torb_location at);

/** Make the list's storage unique so a write may go through in place. */
void torb_list_make_unique(torb_list *list);

/** `value` consumed: the list takes over its count. */
void torb_list_add(torb_list *list, const void *value);
/** `values` borrowed; each element is retained into the list. */
void torb_list_add_all(torb_list *list, torb_list values);
/**
 * `count` elements of a plain element type (no `retain`, no `release`) appended from `values`, which is `count` times
 * the element's size in bytes: how the IO core hands received bytes to an `ArrayList<UInt8>`. `values` borrowed.
 */
void torb_list_add_plain(torb_list *list, const void *values, size_t count);
/** `value` consumed; the old element at `index` is released. */
void torb_list_set(torb_list *list, int64_t index, const void *value, torb_location at);
/** `value` consumed. `index` may be the length, which appends. */
void torb_list_insert(torb_list *list, int64_t index, const void *value, torb_location at);
/** `removeAt(index): Item?`: false when out of range, otherwise `*out` is the removed element, owned. */
bool torb_list_remove_at(torb_list *list, int64_t index, void *out);
/** `list[from..to] = values`. `values` borrowed; its elements are retained in. */
void torb_list_replace(torb_list *list, int64_t from, int64_t to, torb_list values, torb_location at);
void torb_list_reverse(torb_list *list);
void torb_list_clear(torb_list *list);
/** A storage of its own, exactly as big as needed. */
void torb_list_compact(torb_list *list);

/** `list` borrowed, result owned and sharing the storage; the slice starts at index 0 again. */
torb_list torb_list_slice(torb_list list, int64_t from, int64_t to, torb_location at);
/**
 * The same slice in a storage of its own that retains exactly its elements: what the C back end calls instead of
 * `torb_list_slice` where an element may hold an object with a destructor, so no element outlives its last holder
 * because a slice keeps the storage around it alive (docs/design/DESTRUCTORS.md 2a).
 */
torb_list torb_list_slice_copied(torb_list list, int64_t from, int64_t to, torb_location at);

/** Two elements in written order; -1, 0 or 1. `context` is whatever the caller threaded through. */
typedef int32_t (*torb_compare_function)(const void *first, const void *second, void *context);

/** A stable merge sort, so the order is deterministic whatever the comparison does with equal keys. */
void torb_list_sort(torb_list *list, torb_compare_function compare, void *context);

/* ----------------------------------------------------------------------------------------------------- array --- */

/**
 * The index of an `Element` step into an `Array<Item, Size>`, checked against the size its type says: `index` itself,
 * or a panic with `missing` at `at` - the message the step carries, which is the language's and not the runtime's.
 *
 * An array is a struct of the program with its items inline (`value.items[index]`), so the check is all the runtime
 * adds, and it is inline so that a C compiler folds it away where a comparison in front of it already decided it.
 */
static inline int64_t torb_array_index(int64_t index, int64_t length, torb_text missing, torb_location at) {
  if (index < 0 || index >= length) {
    torb_panic(missing, at);
  }
  return index;
}

/* ------------------------------------------------------------------------------------------------ map and set --- */

/** An empty map. Both descriptors must be static data; `key->equals` and `key->hash` must not be `NULL`. */
torb_map torb_map_new(const torb_element *key, const torb_element *value);
torb_map torb_map_with_capacity(const torb_element *key, const torb_element *value, int64_t capacity,
                                torb_location at);

/** `map` borrowed, result owned. */
torb_map torb_map_retained(torb_map map);
/** `map` consumed. */
void torb_map_release(torb_map map);

int64_t torb_map_length(torb_map map);

/** A borrowed pointer to the value for `key`, or `NULL`. `key` borrowed. Valid until the map is written through. */
const void *torb_map_at(torb_map map, const void *key);
/** `get(key): Value?`: false when the key is not there, otherwise `*out` is a retained copy the caller owns. */
bool torb_map_get(torb_map map, const void *key, void *out);
bool torb_map_contains(torb_map map, const void *key);

/**
 * `key` and `value` both consumed. An existing key keeps its position in the insertion order and its stored key; the
 * new key is released and the old value is replaced.
 */
void torb_map_set(torb_map *map, const void *key, const void *value);

/** `remove(key): Value?`. `key` borrowed. `*out` owned on success. A tombstone is left; nothing is reordered. */
bool torb_map_remove(torb_map *map, const void *key, void *out);
void torb_map_clear(torb_map *map);

/**
 * Make the table's storage unique so a write may go through in place, which is what a `var` path through a map needs
 * before it is formed. Every write of the table does the same thing on its own, so this is only what the IR's
 * `MakeUnique` of a map-typed place becomes.
 */
void torb_map_make_unique(torb_map *map);
void torb_set_make_unique(torb_set *set);

/**
 * `map[key].add(x)`: take the value out, change it, put it back - without a copy (BACKEND 2.3). `take_out` moves the
 * value out and leaves the entry reserved; `put_back` stores it again. `*out` is owned between the two calls.
 */
bool torb_map_take_out(torb_map *map, const void *key, void *out);
void torb_map_put_back(torb_map *map, const void *key, const void *value);

/**
 * Iterate in insertion order, skipping the tombstones. `*cursor` starts at 0. The pointers are borrowed and valid
 * until the map is written through. `value` may be `NULL` when the caller only wants the keys.
 */
bool torb_map_next(torb_map map, uint32_t *cursor, const void **key, const void **value);

/**
 * The same walk in the compiler's own convention, which is what `MapIterator.next` reaches: `*cursor` is the entry to
 * start at and is left one past the entry that was answered, and `key` and `value` receive **retained copies** the
 * caller owns. `map` is borrowed.
 *
 * It is a native and not TorbScript because the entry vector and its tombstones are the storage's own business: a
 * position in it is not a position in the map, so nothing an implementation of `Iterator` could compute from `length`
 * and `get` walks a table with a hole in it.
 */
bool torb_map_entry_after(torb_map map, int64_t *cursor, void *key, void *value);
bool torb_set_item_after(torb_set set, int64_t *cursor, void *item);

/** A set is the table with `torb_element_unit` on the value side. */
torb_set torb_set_new(const torb_element *item);
torb_set torb_set_retained(torb_set set);
void torb_set_release(torb_set set);
int64_t torb_set_length(torb_set set);
bool torb_set_contains(torb_set set, const void *item);
/** `item` consumed when it was added, released when it was already there. */
void torb_set_add(torb_set *set, const void *item);
bool torb_set_remove(torb_set *set, const void *item);
void torb_set_clear(torb_set *set);
bool torb_set_next(torb_set set, uint32_t *cursor, const void **item);

/* --------------------------------------------------------------------------------------- console and process --- */

/** `text` borrowed. Writes the bytes and a `\n` to stdout. */
void torb_print(torb_text text);
void torb_print_error(torb_text text);
/**
 * `print(...values: Show)`: the shown parts joined by one space, then a `\n`. The join lives here so the two back
 * ends cannot disagree about it. `parts` borrowed.
 */
void torb_print_parts(const torb_text *parts, size_t count);
void torb_print_error_parts(const torb_text *parts, size_t count);

/** `readLine(): String?`: false at end of input. The trailing `\n` (and a `\r` before it) is removed. */
bool torb_read_line(torb_text *out);

/** `isTerminal()`: whether standard output is a live console rather than a pipe or a file. */
bool torb_is_terminal(void);

/** `printErrorRaw(text: String)`: `text` to standard error exactly as given - no join, no trailing `\n`. `text` borrowed. */
void torb_print_error_raw(torb_text text);

/**
 * `installInterruptHandler()`: from this call on, Ctrl+C sets the flag `torb_take_interrupt` answers and clears,
 * instead of ending the process. Idempotent.
 */
void torb_install_interrupt_handler(void);

/** `interrupted()`: whether Ctrl+C arrived since the last call to this or to `torb_install_interrupt_handler`. */
bool torb_take_interrupt(void);

/**
 * One line of raw UTF-8 bytes plus a `\n`, through the same dispatch `print` goes through: a live Windows console
 * sees the text, and a pipe or a file sees exactly these bytes. `bytes` may hold `\n` of its own.
 *
 * This is what the runtime's *own* reports use - a panic and the lines of the test report - because those are
 * rendered into fixed buffers and are never a `torb_text`: a report that has to allocate is a report that cannot be
 * written when the heap is gone. `bytes` borrowed.
 */
void torb_write_line_out(const char *bytes, size_t length);
void torb_write_line_error(const char *bytes, size_t length);

/* ------------------------------------------------------------------------------------------------- the tests --- */

/**
 * `test "name" { ... }` and `group "name" { ... }` of `std/test`. `name` borrowed, `body` borrowed.
 *
 * One line per test to stdout, `  ok      <group> > <name>` or `  FAILED  <name>` plus the message and the site: the
 * format lives in the runtime so that the interpreter and the binary print one report. A body that panics is caught by
 * a recovery point around it and the next test runs - and because a recovered panic releases nothing, a run with a
 * failed test leaks what the aborted frames held. The test ends once the tasks its body started have completed, and a
 * panic in one of them, on whichever worker, is the test's failure in the same words (`torb_test_tasks_end`).
 */
void torb_test_case(torb_text name, torb_closure body);
void torb_test_group(torb_text name, torb_closure body);

/**
 * `torb test <directory>` is **one binary for every test file**, so the counts of a whole run live here too.
 *
 * The generated `main` calls `torb_test_file` with the name of the file whose tests come next - which is the line
 * `torb test` prints in front of them - and `torb_test_finish` at the end, which writes the blank line and
 * `N passed, M failed (K files)` and answers the exit code of the run: 0 where nothing failed and 1 otherwise.
 * `path` borrowed.
 */
void torb_test_file(const char *path, size_t length);
int torb_test_finish(void);

/** Called by the generated `main` before anything else. `argument_values` borrowed for the whole run. */
void torb_process_start(int argument_count, char **argument_values);
/**
 * Called where the program ends normally, and by `torb_process_exit`. With `TORB_REPORT_LEAKS=1` in the environment it
 * writes the live block count to stderr, which is the leak gate of the conformance suite.
 */
void torb_process_finish(void);
/**
 * What `torb_process_exit` runs before the leak report, once: the generated `main` hands it the function that gives the
 * count of every entry cell back (`entry_cells_release`), which a program that ends in `Process.exit` never reaches
 * `main`'s own call of. `NULL` runs nothing.
 */
void torb_process_on_exit(void (*release)(void));
/** `Process.arguments()`: the program's own name is not in it. Result owned. */
torb_list torb_process_arguments(void);
TORB_NORETURN void torb_process_exit(int64_t code);
/**
 * `Process.runCollecting(command, arguments, var output, var failure)`: a program run to its end. The result is its
 * exit code, what it wrote to standard output is in `*output` and what it wrote to standard error in `*failure` (both
 * owned). **-1** means the program could not be started at all, and then `*failure` says why (owned) - a program that
 * ran and failed is an exit code and not a failure of `run`, which is what lets `torb build` tell "there is no C
 * compiler" from "the C compiler said no".
 *
 * `*failure` carries two things because the exit code says which one it is, and because a native's parameters are
 * fixed by the manifest: the reason where nothing ran, the standard error where something did. `arguments` borrowed; what `*output` and `*failure` held before is the caller's and is not released here,
 * which is what a `var` parameter of a native means (the wrapper passes a fresh empty text).
 */
int64_t torb_process_run(torb_text command, torb_list arguments, torb_text *output, torb_text *failure);
/**
 * `Process.runFeeding(command, arguments, input, var output, var failure)`: `torb_process_run` with `input` as the whole
 * of the child's standard input, which ends after it - an empty `input` is an input that ends at once. The child never
 * reads the terminal this program was started from. `input` borrowed.
 */
int64_t torb_process_run_feeding(
  torb_text command,
  torb_list arguments,
  torb_text input,
  torb_text *output,
  torb_text *failure
);
/**
 * `Process.runInheriting(command, arguments, var failure)`: a program run to its end with **this program's own three
 * streams**. The result is its exit code, nothing is collected, and **-1** means it could not be started at all, with
 * the reason in `*failure` (owned).
 *
 * This is what a driver runs a program with. `torb run` hands a built binary the console it has, so the binary's
 * output arrives while it is produced, its two streams stay apart and in order, and a program that reads standard
 * input reads the one the user is typing into.
 */
int64_t torb_process_run_inheriting(torb_text command, torb_list arguments, torb_text *failure);
/**
 * `Process.executablePath()`: the absolute path of the running program's executable into `*out` (owned), or false
 * where the operating system does not say. It is how a toolchain finds the files it was installed beside.
 */
bool torb_process_executable_path(torb_text *out);

/* -------------------------------------------------------------------------------------------------- files --- */

/**
 * Minimal file IO: what the compiler itself needs. Every function answers false on failure and puts the message of
 * the `IoError` in `*error` (owned); the lowering builds the `IoError` record and the `Result` around it. Paths are
 * borrowed and must be valid UTF-8, which a `String` always is.
 */
bool torb_file_read_text(torb_text path, torb_text *out, torb_text *error);
bool torb_file_write_text(torb_text path, torb_text text, torb_text *error);
/**
 * `File.createDirectory`: the directory and every directory above it that is missing. "It is already there" is
 * success, because a caller that only wants a place to write should not have to ask first - which is what
 * `torb build` does before it writes the generated C.
 */
bool torb_file_create_directory(torb_text path, torb_text *error);
bool torb_file_exists(torb_text path);
bool torb_file_is_directory(torb_text path);
/** The names of the entries of a directory, sorted by bytes. `.` and `..` are not in it. `*out` owned. */
bool torb_file_list(torb_text path, torb_list *out, torb_text *error);
/**
 * The path as an absolute path with `.` and `..` resolved against the working directory, with forward slashes. Text
 * arithmetic, not a lookup: the file does not have to exist and links are not followed.
 */
bool torb_file_absolute_path(torb_text path, torb_text *out, torb_text *error);

/**
 * `File`: a `native shared type` (BACKEND 3.7) with no fields the language can see, so the runtime owns its whole
 * representation - the same way `torb_list` is what `ArrayList` is. `path` is kept only so the message of an
 * `IoError` raised after the handle has already been closed can still name the file; `handle` is a `FILE *`, owned
 * while non-`NULL` and `NULL` once closed.
 *
 * The language has no destructors, so a `File` the program never closes must still not leak the OS handle:
 * `torb_file_drop` is its `torb_drop_function`, called by `torb_release` when the last reference goes away, and it
 * closes the handle if `close` never ran. `close` itself is idempotent - closing an already-closed file, and reading
 * one, both follow the ordinary `Result`/`IoError` path and never panic, exactly like every other IO failure here.
 *
 * There is no `torb_file_retain`/`torb_file_release`: the header is the first member, so the generic `torb_retain`
 * and `torb_release(block, torb_file_drop)` already work on it, the same as any other counted block.
 */
typedef struct torb_file {
  torb_header header;
  torb_text path;
  void *handle;
  /** What a read of `chunks()` got and its reader has not taken yet (stream.c), `NULL` before the first read. */
  void *pending;
} torb_file;

/** `path` borrowed. Opens for reading. Result owned (a fresh block of count 1); `*error` owned on failure. */
bool torb_file_open(torb_text path, torb_file **out, torb_text *error);
/**
 * `self` borrowed, and passed as the place of the receiver the way every `var fn` receiver is: a `shared type` value is
 * the one heap object, never copied. Reads everything left unread. On failure `*path` and `*error` are owned: the two
 * fields of the `IoError`.
 */
bool torb_file_read_all(torb_file **self, torb_text *out, torb_text *path, torb_text *error);
/** `self` borrowed, as the place of the receiver. Idempotent. */
void torb_file_close(torb_file **self);
/** The `torb_drop_function` of `torb_file`: closes the handle if `close` never ran, then releases `path`. */
void torb_file_drop(void *block);

/* ---------------------------------------------------------------------------------------- the platform layer --- */

/**
 * Windows and POSIX behind fourteen functions. `runtime/platform.c` is the only file with an `#ifdef _WIN32`.
 *
 * Every path and every text here is **UTF-8**, on both platforms. On Windows the file converts to UTF-16 and calls the
 * wide API, because the narrow one reads the code page of the machine and a `String` is UTF-8 (`docs/design/PATH.md`,
 * section 6); on POSIX a path is bytes and there is nothing to convert.
 */

typedef enum torb_path_kind {
  TORB_PATH_MISSING = 0,
  TORB_PATH_FILE = 1,
  TORB_PATH_DIRECTORY = 2
} torb_path_kind;

/** `path` borrowed, NUL terminated. */
torb_path_kind torb_platform_path_kind(const char *path);
/**
 * Whether the path names a symbolic link itself, not what it points at: a reparse point of any kind on Windows (a
 * symbolic link, a junction, a mount point), `S_ISLNK` of `lstat` elsewhere. False for a path that does not exist.
 * `path` borrowed, NUL terminated. The sandbox refuses a component that is one (docs/design/SCRIPTS.md section 4).
 */
bool torb_platform_is_link(const char *path);
/** Result owned, freed with `torb_raw_free`; `*length` is the byte length without the NUL. `NULL` on failure. */
char *torb_platform_working_directory(size_t *length);
/**
 * Every entry of a directory, appended to `*out` as texts, unsorted. False on failure with a libc message in
 * `*message` (borrowed, static) - including for an entry whose name has no UTF-8 spelling at all, because a `String`
 * always has one.
 */
bool torb_platform_list_directory(const char *path, torb_list *out, const char **message);
/**
 * A file opened for reading, or created for writing where `writing` is true. The result is a `FILE *`, as `void *` so
 * that no caller of this header has to have `<stdio.h>`; `NULL` on failure with a libc message in `*message`.
 */
void *torb_platform_open_file(const char *path, bool writing, const char **message);
/**
 * One file, or one directory that is empty, removed. Only `runtime/tests` calls this: `std/fs` has no `delete` and
 * `docs/design/PATH.md` does not give it one, so there is no native above the platform layer to route it through. It is here
 * because a test that writes a file with a non-ASCII name cannot remove it with `remove` from `<stdio.h>` - that is the
 * narrow call, and the whole point of this layer is that the narrow calls are gone.
 */
bool torb_platform_remove(const char *path);
/** Read a whole file. `*bytes` owned (`torb_raw_free`). False on failure with a libc message in `*message`. */
bool torb_platform_read_file(const char *path, uint8_t **bytes, size_t *length, const char **message);
bool torb_platform_write_file(const char *path, const uint8_t *bytes, size_t length, const char **message);
/** A monotonic clock reading, in nanoseconds, from an unspecified origin. Never goes backwards within one process. */
/** `mkdir -p`. False on failure with a libc message in `*message` (borrowed, static). */
bool torb_platform_create_directory(const char *path, const char **message);
/**
 * A child process, run to its end. `*code` is its exit code, `*output` what it wrote to standard output and `*errors`
 * what it wrote to standard error, each owned and freed with `torb_raw_free(*output, *capacity)` - the capacity and not
 * the length, because the buffer grows in doublings and the allocator is told the size it gave out. False only where
 * the process could not be started at all, with a libc message in `*message`, and then neither buffer is handed out.
 *
 * `input` `NULL` hands the child this process's own standard input; otherwise the child reads the `inputLength` bytes
 * (borrowed) and then the end of its input, from a temporary file - which never makes either side wait, as a pipe that
 * nobody drains would.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  const uint8_t *input,
  size_t inputLength,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  uint8_t **errors,
  size_t *errorsLength,
  size_t *errorsCapacity,
  const char **message
);
/**
 * The same, with **this process's own three streams** handed to the child instead of a pipe: nothing is collected,
 * what the child writes appears where this program's output appears while it writes it, and what it reads comes from
 * the same place. `*code` is its exit code. False only where the process could not be started at all, with a libc
 * message in `*message` (borrowed, static).
 */
bool torb_platform_run_inheriting(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  const char **message
);
/**
 * A child process with three pipes (runtime/stream.c, `Process.start`), through no shell: the child's handle and the
 * three ends this process holds, as the pointers the stream layer keeps. False where it could not be started, with a
 * message in `*message` (borrowed, static).
 */
bool torb_platform_child_start(
  const char *command,
  const char **arguments,
  size_t count,
  void **process,
  void **input,
  void **output,
  void **errors,
  const char **message
);
/** At most `maximum` bytes as soon as any arrived: their number, 0 at the end, or minus the system's code. */
int64_t torb_platform_pipe_read(void *pipe, uint8_t *buffer, size_t maximum);
/** All of `bytes`: their number, or minus the system's code. */
int64_t torb_platform_pipe_write(void *pipe, const uint8_t *bytes, size_t length);
void torb_platform_pipe_close(void *pipe);
/** Waits for the child to end: its exit code, or false and the system's code in `*failure`. */
bool torb_platform_child_wait(void *process, int64_t *code, int64_t *failure);
/** Lets go of the child's handle without waiting for it; the child runs on. */
void torb_platform_child_forget(void *process);
/** A code of the system (`errno` on POSIX, `GetLastError` on Windows) in its own words. Owned. */
torb_text torb_platform_failure_text(int64_t code);
/**
 * Standard input, bytes as they come (console.c): their number, 0 at the end, or minus the system's code. A console
 * on Windows is read as text and handed on as its UTF-8.
 */
int64_t torb_read_standard_bytes(uint8_t *buffer, size_t maximum);
/** Bytes to standard output or, with `error`, standard error, through the dispatch `print` takes. */
bool torb_write_standard_bytes(bool error, const uint8_t *bytes, size_t length);
/** The buffer a file's reads keep what they got in, given back when the file goes (stream.c). */
void torb_file_forget_pending(torb_file *file);

/** A monotonic clock reading, in nanoseconds, from an unspecified origin. Never goes backwards within one process. */
int64_t torb_platform_monotonic_nanoseconds(void);
/**
 * Blocks the calling thread for at least this many nanoseconds, and for none at zero or less. What the scheduler does
 * when its run queue is empty and a timer is pending; it reads the clock again afterwards, so waking early is harmless.
 */
void torb_platform_sleep(int64_t nanoseconds);
/**
 * The lowest address the stack of the calling thread can grow down to, in `*low`. False where the platform does not
 * say, and then the stack check stays off.
 */
bool torb_platform_stack_low(uintptr_t *low);
/**
 * The program's own arguments, without the program's name, appended to `*out` as texts - where the platform has a
 * source for them of its own. False where it has none and the `argv` of `main` is what there is, and then **nothing was
 * added**. Windows has one (the wide command line), POSIX has not.
 */
bool torb_platform_arguments(torb_list *out);
/**
 * One environment variable. `*value` owned, freed with `torb_raw_free(*value, *length + 1)`. False where the variable is
 * not set (and then nothing is allocated).
 */
bool torb_platform_environment_variable(const char *name, char **value, size_t *length);
/**
 * Every environment variable whose name `keep` accepts, appended to `*names` and `*values` as texts, one pair per
 * variable. A variable that has no UTF-8 spelling is left out, and so are the per-drive names Windows keeps under a
 * leading `=`. `keep` is handed a NUL-terminated name it borrows.
 */
void torb_platform_environment_entries(torb_list *names, torb_list *values, bool (*keep)(const char *name));
/**
 * `name` and `value` borrowed, NUL terminated. Only `runtime/tests` calls this - no native sets an environment
 * variable, so there is nothing above the platform layer to route it through. False on failure.
 */
bool torb_platform_set_environment_variable(const char *name, const char *value);
/**
 * The absolute path of the running executable, with `/` as the separator. `*value` owned, freed with
 * `torb_raw_free(*value, *length + 1)`. False where the operating system does not say (and then nothing is allocated).
 */
bool torb_platform_executable_path(char **value, size_t *length);
/** The same on macOS (`os/macos.c`): `_NSGetExecutablePath` with every link resolved. Defined on macOS only. */
bool torb_os_macos_executable_path(char **value, size_t *length);
/**
 * Has the operating system hold this process to `bytes` of memory it commits, for the rest of its life; a child it
 * starts afterwards is not held to it (a TorbScript child sets its own). True where the system took the limit. False
 * where it did not, with why in `*message` - or `*message` `NULL` where this system has no mechanism the runtime uses
 * (macOS), which is not a failure. Called once, before any thread is started.
 *
 * Windows: a job object of its own with `JOB_OBJECT_LIMIT_PROCESS_MEMORY` and silent breakaway for children. Linux:
 * `RLIMIT_DATA`, which counts what is mapped writable and private - the heap and thread stacks, not a reservation.
 * FreeBSD: `RLIMIT_AS`, because its `RLIMIT_DATA` counts `brk` alone and its allocator maps.
 */
bool torb_platform_limit_memory(uint64_t bytes, const char **message);
/** The physical memory of the machine in bytes, or 0 where the platform does not say. */
uint64_t torb_platform_physical_memory(void);
/**
 * How many bytes the C allocator really reserved for `block` (at least what was asked for), or 0 where the platform
 * cannot say. `block` borrowed, from `malloc`. What the runtime counts a block as, where it counts its own allocations.
 */
size_t torb_platform_allocation_size(void *block);

#if defined(_WIN32)

/**
 * The three functions of the boundary to Windows. They are in the header because `runtime/tests/platform_test.c` reads
 * them: what they answer is a decision of `docs/design/PATH.md` and not an implementation detail, so it is tested directly.
 * Nothing above `runtime/platform.c` calls them.
 */

/** UTF-8 to UTF-16, NUL terminated. Owned, `torb_raw_free(result, *capacity)`. `NULL` for text that is not UTF-8. */
wchar_t *torb_platform_wide(const char *text, size_t *capacity);
/** UTF-16 to UTF-8, NUL terminated. Owned, `torb_raw_free(result, *length + 1)`. `NULL` for ill-formed UTF-16. */
char *torb_platform_utf8(const wchar_t *wide, size_t *length);
/** The form of a path a Windows call gets: backslashes, and `\\?\` where the plain form would be too long. */
wchar_t *torb_platform_system_path(const char *path, size_t *capacity);

#endif

/* ------------------------------------------------------------------------------------------------------- time --- */

/**
 * The runtime's own names for the two spans of time it waits on: `torb_instant` one monotonic clock reading, in
 * nanoseconds since an unspecified per-process origin, and `torb_duration` a signed nanosecond span. `Instant` and
 * `Duration` of `std/time` are records of the program over exactly this `Int64`, so a limit crosses the boundary as
 * the number (`Task.within`). About 292 years fit before it overflows, which a monotonic clock reading within one
 * process never approaches.
 */
typedef int64_t torb_instant;
typedef int64_t torb_duration;

/**
 * The monotonic clock in nanoseconds: what `Clock.now()` wraps. Needs the `std/time` capability inside a sandboxed
 * script (7.4).
 */
int64_t torb_clock_now(void);

/**
 * `Clock.milliseconds()`: monotonic milliseconds counted from the **first reading** of the process, which is the form
 * a tool that measures its own work wants (`torb check --timings`). An `Instant` and a `Duration` would be the same
 * number twice and a subtraction; only differences are meaningful either way.
 */
int64_t torb_clock_milliseconds(void);

/* ---------------------------------------------------------------------------------------------- environment --- */

/**
 * `name` borrowed. `Environment.get`: false when the variable is not set. `*out` owned on success. Capability
 * filtering for a sandboxed script (`environment "APP_*"`) is a front-end concern of milestone 7.4; every variable
 * `getenv` can see is visible here, which is what a native, non-sandboxed program expects.
 */
bool torb_environment_get(torb_text name, torb_text *out);
/**
 * `Environment.entries`: every variable a sandboxed script's patterns match, and every variable at all outside a
 * sandbox, appended to `*names` and `*values` as two parallel lists of texts.
 */
void torb_environment_entries(torb_list *names, torb_list *values);

/* ---------------------------------------------------------------------------------------------------- hashing --- */

/** FNV-1a-64 with the standard offset basis. The seed is fixed, so a hash is the same in every run. */
uint64_t torb_hash_bytes(const void *bytes, size_t length);
uint64_t torb_hash_i64(int64_t value);
uint64_t torb_hash_u64(uint64_t value);
uint64_t torb_hash_bool(bool value);
uint64_t torb_hash_char(torb_char character);
/** Mixes a hash into another, for a composite `Hash`. */
uint64_t torb_hash_combine(uint64_t first, uint64_t second);

#include "torb_number.h"
#include "torb_task.h"
#include "torb_network.h"
#include "torb_tls.h"
#include "torb_os.h"

/* ------------------------------------------------------------------------------------------ the VM's words --- */

/*
 * `Machine.load` and `Machine.store` of `std/machine`: one word at an address - a register of the bytecode VM, a field of
 * one of its blocks, a word of its code. They are natives like every other, and they are defined here, `static inline`,
 * because the interpreter's loop reads and writes every register through them: the declaration `torb_natives.h` writes
 * after this one takes this one's internal linkage (C11 6.2.2), so a call of either is one load or one store and no
 * call at all.
 */
static inline int64_t torb_machine_load(int64_t address) {
  int64_t value;
  memcpy(&value, (const void *)(intptr_t)address, sizeof value);
  return value;
}

static inline void torb_machine_store(int64_t address, int64_t value) {
  memcpy((void *)(intptr_t)address, &value, sizeof value);
}

#endif /* TORB_H */
