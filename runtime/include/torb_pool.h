/*
 * torb_pool.h - what the runtime's own files share about the worker pool (docs/design/CONCURRENCY.md, milestone 7.7).
 * The generated C never includes it: `torb.h` is the ABI, and everything here can change without the compiler knowing.
 *
 * # What a worker owns
 *
 * A worker is one operating-system thread and everything that thread alone touches: its **heap** (the block counters
 * of memory.c), its **scheduler** (the task running now, the timer heap), the **recovery point** of a panic, and the
 * bottom of its stack. The main thread is worker 0 from the first instruction on; workers 1 to N-1 exist only while the
 * pool runs (`torb_pool_start`), and a program that never starts a task that may move never starts them at all.
 *
 * Each worker also has one **run queue** that other threads may touch - a woken task is put there by whoever woke it,
 * and an idle worker takes an unstarted task out of it - so the queue sits behind the worker's `lock`, and the worker
 * sleeps on its `wake` condition when there is nothing to do. That lock and the per-object locks of task.c are the
 * only locks of the pool; the counts of values stay plain integers (BACKEND 2.5), because a value that cannot be
 * proven safe to move never leaves the worker that made it (task.c, "What crosses a worker").
 *
 * # How a thread finds its worker
 *
 * `torb_worker_current()` is on the path of every allocation, so it is one load wherever the platform allows it: a
 * thread-local pointer on POSIX (native TLS in an executable), and on 64-bit Windows the thread's slot of the TEB read
 * straight through `gs` - because MinGW's `_Thread_local` is emulated and costs a call. A thread that never set its
 * pointer is the main thread, and answers worker 0.
 */

#ifndef TORB_POOL_H
#define TORB_POOL_H

#include "torb.h"

/* ------------------------------------------------------------------------------------------------- atomics --- */

/*
 * The few atomic operations the pool needs, typed, on plain integers: a load that acquires, a store that releases,
 * and read-modify-write operations that are sequentially consistent and answer the value before them. GCC and clang
 * have the builtins; MSVC gets the interlocked intrinsics (a full barrier) and volatile accesses (acquire and release
 * on the targets it compiles for).
 */
#if defined(__GNUC__) || defined(__clang__)
static inline uint32_t torb_atomic_load_u32(const uint32_t *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_ACQUIRE);
}
static inline uint32_t torb_atomic_peek_u32(const uint32_t *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_RELAXED);
}
static inline void torb_atomic_store_u32(uint32_t *pointer, uint32_t value) {
  __atomic_store_n(pointer, value, __ATOMIC_RELEASE);
}
static inline uint32_t torb_atomic_add_u32(uint32_t *pointer, uint32_t value) {
  return __atomic_fetch_add(pointer, value, __ATOMIC_SEQ_CST);
}
static inline uint32_t torb_atomic_sub_u32(uint32_t *pointer, uint32_t value) {
  return __atomic_fetch_sub(pointer, value, __ATOMIC_SEQ_CST);
}
static inline uint32_t torb_atomic_exchange_u32(uint32_t *pointer, uint32_t value) {
  return __atomic_exchange_n(pointer, value, __ATOMIC_SEQ_CST);
}
/** A load in the single total order of the sequentially consistent operations: the half of a Dekker handshake. */
static inline uint32_t torb_atomic_read_u32(uint32_t *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_SEQ_CST);
}
static inline uint8_t torb_atomic_load_u8(const uint8_t *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_ACQUIRE);
}
static inline void torb_atomic_store_u8(uint8_t *pointer, uint8_t value) {
  __atomic_store_n(pointer, value, __ATOMIC_RELEASE);
}
static inline int64_t torb_atomic_load_i64(const int64_t *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_ACQUIRE);
}
static inline int64_t torb_atomic_add_i64(int64_t *pointer, int64_t value) {
  return __atomic_fetch_add(pointer, value, __ATOMIC_SEQ_CST);
}
static inline void *torb_atomic_load_pointer(void *const *pointer) {
  return __atomic_load_n(pointer, __ATOMIC_ACQUIRE);
}
static inline void torb_atomic_store_pointer(void **pointer, void *value) {
  __atomic_store_n(pointer, value, __ATOMIC_RELEASE);
}
#elif defined(_MSC_VER)
#  include <intrin.h>
static inline uint32_t torb_atomic_load_u32(const uint32_t *pointer) {
  return *(const volatile uint32_t *)pointer;
}
static inline uint32_t torb_atomic_peek_u32(const uint32_t *pointer) {
  return *(const volatile uint32_t *)pointer;
}
static inline void torb_atomic_store_u32(uint32_t *pointer, uint32_t value) {
  *(volatile uint32_t *)pointer = value;
}
static inline uint32_t torb_atomic_add_u32(uint32_t *pointer, uint32_t value) {
  return (uint32_t)_InterlockedExchangeAdd((volatile long *)pointer, (long)value);
}
static inline uint32_t torb_atomic_sub_u32(uint32_t *pointer, uint32_t value) {
  return (uint32_t)_InterlockedExchangeAdd((volatile long *)pointer, -(long)value);
}
static inline uint32_t torb_atomic_exchange_u32(uint32_t *pointer, uint32_t value) {
  return (uint32_t)_InterlockedExchange((volatile long *)pointer, (long)value);
}
static inline uint32_t torb_atomic_read_u32(uint32_t *pointer) {
  return (uint32_t)_InterlockedExchangeAdd((volatile long *)pointer, 0);
}
static inline uint8_t torb_atomic_load_u8(const uint8_t *pointer) {
  return *(const volatile uint8_t *)pointer;
}
static inline void torb_atomic_store_u8(uint8_t *pointer, uint8_t value) {
  *(volatile uint8_t *)pointer = value;
}
static inline int64_t torb_atomic_load_i64(const int64_t *pointer) {
  return *(const volatile int64_t *)pointer;
}
static inline int64_t torb_atomic_add_i64(int64_t *pointer, int64_t value) {
  return _InterlockedExchangeAdd64((volatile long long *)pointer, value);
}
static inline void *torb_atomic_load_pointer(void *const *pointer) {
  return *(void *const volatile *)pointer;
}
static inline void torb_atomic_store_pointer(void **pointer, void *value) {
  *(void *volatile *)pointer = value;
}
#else
#  error "the worker pool needs the atomic builtins of GCC or clang, or the interlocked intrinsics of MSVC"
#endif

/* ---------------------------------------------------------------------------------- threads, locks, waiting --- */

#if defined(_WIN32)
/* An SRWLOCK, a CONDITION_VARIABLE and a HANDLE are each one pointer, so no header of Windows is needed here. */
typedef struct torb_mutex {
  void *opaque;
} torb_mutex;
typedef struct torb_condition {
  void *opaque;
} torb_condition;
typedef struct torb_thread {
  void *handle;
} torb_thread;
#  define TORB_MUTEX_INITIALIZER { NULL }
#else
#  include <pthread.h>
typedef struct torb_mutex {
  pthread_mutex_t mutex;
} torb_mutex;
typedef struct torb_condition {
  pthread_cond_t condition;
} torb_condition;
typedef struct torb_thread {
  pthread_t thread;
} torb_thread;
#  define TORB_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }
#endif

/* All of these are platform.c's. A mutex made with `TORB_MUTEX_INITIALIZER` needs no `torb_mutex_initialize`. */
void torb_mutex_initialize(torb_mutex *mutex);
void torb_mutex_lock(torb_mutex *mutex);
void torb_mutex_unlock(torb_mutex *mutex);
void torb_mutex_destroy(torb_mutex *mutex);
void torb_condition_initialize(torb_condition *condition);
/**
 * Waits on `condition` with `mutex` held, for at most `nanoseconds` (-1: without a limit). False where the time ran
 * out. A spurious wakeup answers true, so every caller waits in a loop over what it waits for.
 */
bool torb_condition_wait(torb_condition *condition, torb_mutex *mutex, int64_t nanoseconds);
void torb_condition_signal(torb_condition *condition);
void torb_condition_destroy(torb_condition *condition);
/** A thread that runs `body(argument)` on a stack of `stack_size` bytes. False where the platform refused. */
bool torb_thread_start(torb_thread *thread, void (*body)(void *argument), void *argument, size_t stack_size);
void torb_thread_join(torb_thread *thread);
/** Lets another thread run: what a spin on a lock does after a few rounds. */
void torb_thread_yield(void);
/** Ends the calling thread where it is. Only a worker thread of the pool calls it, and only once nothing above needs to run. */
TORB_NORETURN void torb_thread_exit(void);
/** How many logical processors the process may run on. At least 1. */
uint32_t torb_platform_processor_count(void);

/* ------------------------------------------------------------------------------------------ the object lock --- */

/**
 * The lock of one task or one channel: a word in the block, held for a few instructions at a time (a waiter list, a
 * ring buffer), so it spins and yields instead of sleeping.
 */
static inline void torb_spin_lock(uint32_t *lock) {
  unsigned rounds = 0u;
  while (torb_atomic_exchange_u32(lock, 1u) != 0u) {
    while (torb_atomic_peek_u32(lock) != 0u) {
      rounds += 1u;
      if (rounds > 64u) {
        torb_thread_yield();
      }
    }
  }
}

static inline void torb_spin_unlock(uint32_t *lock) {
  torb_atomic_store_u32(lock, 0u);
}

/* ------------------------------------------------------------------------------------------------ a worker --- */

/** The block counters of one worker (memory.c). Each is written by its own thread only; the report sums them. */
typedef struct torb_heap {
  size_t live_blocks;
  size_t immortal_blocks;
  /** How many immortal regions are open on this thread (memory.c, `torb_begin_immortal`). */
  unsigned immortal_depth;
  /**
   * Whether this thread runs inside a call of the VM's kernel (machine.c): what it allocates and frees there is the
   * program's the VM runs, and is counted once more below, apart from the blocks of `torb` itself.
   */
  unsigned in_machine;
  int64_t machine_live_blocks;
  int64_t machine_immortal_blocks;
} torb_heap;

typedef struct torb_timer {
  torb_instant deadline;
  /** Breaks ties between equal deadlines: the timer set first fires first. */
  uint64_t sequence;
  torb_task *task;
} torb_timer;

/** What only the worker's own thread touches of the scheduling (task.c). */
typedef struct torb_scheduler {
  /** The task whose machine is running, which is the parent of every task it makes. */
  torb_task *current;
  torb_timer *timers;
  uint32_t timer_count;
  uint32_t timer_capacity;
  uint64_t timer_sequence;
  bool running;
} torb_scheduler;

typedef struct torb_worker {
  /* ---- the thread's own ---- */
  torb_heap heap;
  torb_scheduler scheduler;
  /**
   * Where a panic of this thread jumps instead of ending the process: `test.c` around a test body, `task.c` around the
   * resume of a task that belongs to a test.
   */
  torb_recovery *recovery;
  uint32_t index;
  /* ---- behind `lock` ---- */
  torb_mutex lock;
  torb_condition wake;
  /** Ready tasks, first in first out, through `queue_previous`/`queue_next`. */
  torb_task *queue_first;
  torb_task *queue_last;
  /** The unstarted tasks of the queue that may move, through `steal_previous`/`steal_next`: what an idle worker takes. */
  torb_task *steal_first;
  torb_task *steal_last;
  /** Waiting on `wake`; `wanted` is what a signal sets, so a spurious wakeup is told from a real one. */
  uint32_t sleeping;
  uint32_t wanted;
  bool prepared;
  /* ---- counted by the thread itself, read once the pool stopped ---- */
  uint64_t ran;
  uint64_t stole;
  torb_thread thread;
} torb_worker;

/** Worker 0: the main thread, from the first instruction on. */
extern torb_worker torb_main_worker;

/**
 * The blocking pool's workers are numbered above every worker of the ring (task.c, "The blocking pool"):
 * `TORB_BLOCKING_INBOX` is the queue they share - a worker without a thread - and thread `k` is
 * `TORB_BLOCKING_INBOX + 1 + k`. The VM's kernel keeps an interpreter per thread by the same numbers (machine.c).
 */
#define TORB_BLOCKING_INBOX 2048u

/**
 * Nonzero while the pool runs more than one thread: what makes a `TORB_SHARED_COUNT` block's count atomic (memory.c).
 * It changes only while one thread is left - set before the first worker thread starts, cleared after the last one was
 * joined - so reading it plainly is exact.
 */
extern uint32_t torb_pool_threaded;

#if defined(_WIN32)
/** The TLS index every worker thread stores its worker in, or `TORB_NO_SLOT` before the pool first started. */
extern unsigned long torb_worker_slot;
#  define TORB_NO_SLOT 0xFFFFFFFFul
/** `TlsGetValue(torb_worker_slot)`, for the slots the fast path below does not reach. */
void *torb_platform_slot_value(void);
void torb_platform_set_slot_value(void *value);
static inline torb_worker *torb_worker_thread(void) {
  unsigned long slot = torb_worker_slot;
  if (slot == TORB_NO_SLOT) {
    return NULL;
  }
#  if defined(_WIN64) && (defined(__x86_64__) || defined(__amd64__)) && (defined(__GNUC__) || defined(__clang__))
  /* The first 64 TLS slots of a thread are an array in its TEB at 0x1480: one `gs`-relative load. */
  if (slot < 64ul) {
    void *value;
    __asm__("movq %%gs:(%1), %0" : "=r"(value) : "r"((uintptr_t)0x1480u + (uintptr_t)slot * 8u));
    return (torb_worker *)value;
  }
#  endif
  return (torb_worker *)torb_platform_slot_value();
}
#else
extern _Thread_local torb_worker *torb_thread_worker;
static inline torb_worker *torb_worker_thread(void) {
  return torb_thread_worker;
}
#endif

/** The worker of the calling thread. Every thread that runs program code is one. */
static inline torb_worker *torb_worker_current(void) {
  torb_worker *worker = torb_worker_thread();
  return worker != NULL ? worker : &torb_main_worker;
}

/** Makes `worker` the one `torb_worker_current` answers on the calling thread (platform.c). */
void torb_platform_set_worker(torb_worker *worker);

/** The block counters summed over every worker, for memory.c's report. Exact once the other threads are quiet. */
size_t torb_pool_sum_live_blocks(void);
size_t torb_pool_sum_immortal_blocks(void);
/** The same two counts of the blocks the VM's program allocated inside the kernel (`torb_heap.in_machine`). */
int64_t torb_pool_sum_machine_live_blocks(void);
int64_t torb_pool_sum_machine_immortal_blocks(void);

/**
 * `torb_release` in two halves, for task.c's completion: the count goes down by one (atomically for a shared block
 * while there are threads) and the answer says whether it reached zero - and only then does the caller free the block,
 * with its drop, through `torb_free_counted`. Between the two the caller may still read the block it no longer counts.
 */
bool torb_count_down(void *block);
void torb_free_counted(void *block, torb_drop_function drop);

/** Sets the bottom of the calling thread's stack for the stack check (panic.c): a worker thread calls it at start. */
void torb_set_thread_stack_floor(uintptr_t floor);

/** Readies stdout, stderr and the clock before a second thread exists, so no first use races (console.c, clock.c). */
void torb_console_prepare(void);
void torb_clock_prepare(void);

/** One block copied at a crossing, for `torb_pool_statistics.copied` (text.c, list.c, map.c). From any thread. */
void torb_pool_count_copy(void);

/**
 * A side buffer of an immortal block - the entries and the buckets of a map built inside an immortal region, as a module
 * constant or the copy an entry cell holds is (memory.c) - is never freed either, so it moves from the live count of the
 * calling thread's heap to the immortal one; and back, right before its block frees it while it is still being built
 * (a map that grows). The leak report then says `live blocks at exit: 0` for a program that holds such a map.
 */
void torb_raw_count_immortal(void);
void torb_raw_count_mortal(void);

/* ------------------------------------------------------------------------------------------ waiting on IO --- */

/*
 * A task that waits for an operation of the IO core (runtime/io.c, docs/design/NETWORK.md section 2). The operation is
 * the IO core's; this is the part of it task.c reads, and the one lock both sides take. While a task waits here it
 * counts as something that can still happen (`busy`), like an armed timer: a program whose only task waits for a
 * connection waits, and is no deadlock.
 */
typedef struct torb_io_waiting {
  /** A spin lock over the two fields below and the waiting fields of the waiter. */
  uint32_t lock;
  /** The operation completed: a wait that starts now is ready at once. */
  uint8_t done;
  /** The task waiting for it, or `NULL`. */
  torb_task *waiter;
} torb_io_waiting;

/**
 * `self` waits for the operation: ready at once where it is done, and otherwise registered and woken, with
 * `TORB_OUTCOME_READY`, by `torb_task_io_done`. The running task only, once per wait.
 */
torb_wait torb_task_wait_io(torb_task *self, torb_io_waiting *waiting);

/** The operation is done: its waiter, where it has one, is queued on its worker. From any thread, the IO thread's too. */
void torb_task_io_done(torb_io_waiting *waiting);

/**
 * io.c's: the waiter of the operation was cancelled and taken out of it, so the kernel is asked to give the operation
 * up. Called with the tree lock of task.c held; it takes no lock of task.c.
 */
void torb_io_waiter_cancelled(torb_io_waiting *waiting);

/**
 * Makes the process ready for a thread of the runtime that is no worker (the IO thread, a resolver thread), where the
 * calling thread is the only one yet: the console, the clock and the worker's slot are set up, and the counts of shared
 * blocks become atomic. What `torb_pool_start` does before it starts the workers.
 */
void torb_pool_prepare_thread(void);

/**
 * io.c's: the end of the program stops the IO core - every handle closed, every operation completed, the IO thread and
 * the resolver threads joined. True where it ran. Called by the stop of the pool, on the main thread, with no task left.
 */
bool torb_io_stop(void);

/* ------------------------------------------------------------------------------------ a page of a browser --- */

#if defined(__EMSCRIPTEN__)

/*
 * In a browser (os/browser.c) nothing may wait inside a read or on a condition: the thread has to return to the event
 * loop of its page or worker before the next message can arrive at all. So standard input that the page feeds is an
 * operation of the IO core, and where the scheduler would sleep while a task waits for it, it returns to the page
 * instead; the page calls it again with `torb_browser_resume`.
 */

/** Whether a task waits for the page to hand in standard input. */
bool torb_browser_input_waited(void);

/** Whether `waiting` is that wait, which belongs to no operation of io.c. */
bool torb_browser_is_input(const torb_io_waiting *waiting);

/** `self` waits until the page hands in more standard input, or says it ended. The running task only. */
torb_wait torb_browser_wait_input(torb_task *self);

/** Gives up the stack of `main` and returns to the page's event loop with the program alive. */
TORB_NORETURN void torb_browser_unwind(void);

/**
 * task.c's: the scheduler again after it returned to the page, until it waits for the page once more - true, with the
 * nanoseconds to its first timer in `span`, -1 for none - or until the run `main` started is over: false, with the task
 * `main` waited for in `until`, or `NULL`.
 */
bool torb_scheduler_resume(int64_t *span, torb_task **until);

#endif

#endif /* TORB_POOL_H */
