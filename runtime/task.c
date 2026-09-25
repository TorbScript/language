/*
 * task.c - tasks, the worker pool, timers and channels (milestones 7.3 and 7.7).
 *
 * torb_task.h is the contract: what a task block holds and how the compiler lowers a function that answers a `Task` to
 * a resume function. This file is what runs them - a fixed pool of workers, each an operating-system thread with a FIFO
 * run queue, a binary min-heap of timers and a heap of its own (runtime/include/torb_pool.h) - plus the channels, and
 * the tasks the runtime writes itself (`sleep`, `pause`, `within`), which are resume functions under exactly the rules
 * the compiler's are.
 *
 * **Nothing here allocates per wait.** A task waits in lists threaded through its own block (`wait_previous`/`wait_next`
 * for the waiters of a task and the queues of a channel, `queue_previous`/`queue_next` for the run queue), and a
 * sender's item stays in the slot of its frame until a receiver moves it out. The only buffers are a channel's ring and
 * the timer heaps.
 *
 * **Determinism** (docs/design/CONCURRENCY.md section 7): each run queue is FIFO, a task that is woken goes to the back
 * of its worker's queue, and timers that are due are woken in deadline order, ties in the order they were set, before
 * the next task is taken. With one worker a program that does no real IO and reads no clock therefore runs its tasks in
 * one order on every machine, which is what the conformance suite pins (`TORB_WORKERS=1`). With more, the *results* of
 * a program stay what they are - the chunks of `parallel()` come back in input order, a channel delivers in the order it
 * was fed - and only the interleaving of what several tasks print at once is the machine's.
 *
 * # The pool
 *
 * The main thread is worker 0 from the first instruction on. The other workers are started the first time a task that
 * may move is started (`torb_task_start_portable`) where `Workers.count()` is more than one, and stopped - joined, their
 * block counters folded into worker 0's - at the end of the program (`torb_scheduler_finish`). A program that never
 * starts such a task never has a second thread, and pays for the pool one uncontended lock per queue operation.
 *
 * A task **belongs to the worker that runs it** (`task->worker`): its frame, its state, its timer and every value it
 * holds are touched by that thread alone. It starts on the worker that started it; until it first runs, a task that may
 * move sits in the *steal list* of that worker's queue as well, and a worker with nothing to do takes the oldest such
 * task from another worker's queue and makes it its own (`torb_steal`). A task that has run once never moves: its frame
 * holds whatever its machine made, and moving it would move a heap (docs/design/CONCURRENCY.md section 9).
 *
 * **Waking a task on another worker** is `torb_post`: the waker takes the task out of whatever it waited on under that
 * object's lock, then puts it at the back of the owner's queue under the owner's lock and signals the owner if it
 * sleeps. So a worker's own run queue is the inbox of every other worker, and the atomics of the design are exactly
 * these: the object locks, the queue locks, the cancellation flag and the completion status. The counts of values stay
 * plain integers.
 *
 * **Locks, and the one order they are taken in**: the tree lock (the parent, child and live links of every task, a
 * mutex of the process) before the lock of an object (a task's `status` and `waiters`, a channel's ring and queues -
 * spin locks in the block) before the lock of a worker's queue (a mutex, with the condition the worker sleeps on). No
 * user code runs while any of them is held.
 *
 * # What crosses a worker
 *
 * A value goes from one worker to another in exactly three ways, and each is a **transfer**, never a copy: the frame of
 * a task that is stolen before its first run, an item of a channel whose two ends are on two workers, and the result of
 * a task that ran on another worker than the one that waits for it. None of them may ever let two threads touch one
 * count, because counts are plain integers. So a value may cross only where that is provably so - and the proof is
 * made where the value is handed over, never guessed:
 *
 * - **Plain data** (`Int`, `Float`, a record of those: nothing counted inside) crosses as the bytes it is.
 * - **An immortal block** (a literal, the value of a module constant) is never retained or released by anyone, so any
 *   number of workers may hold it.
 * - **A `Task`, a `Channel`, and the environment of a closure whose captures may all cross** are *shared* blocks
 *   (`TORB_SHARED_COUNT`): their counts change atomically while there are threads. A channel carries only plain items
 *   across a worker, and a task handle crosses only where its value is plain, because either would otherwise put the
 *   same counted value in two workers' hands.
 * - **A block that only the moving value holds** - a list whose storage has count 1 and plain elements, a text whose
 *   storage has count 1 - is *re-homed*: the frame of an unstarted task is the only owner, so the worker that takes the
 *   task becomes the only thread that ever touches the block again. Nothing is copied and nothing is recorded: libc
 *   frees a block from any thread, and the block counters of the two heaps balance in their sum (memory.c).
 *
 * A text somebody else holds too, a list of strings, a record of those - a value that may be copied soundly - crosses
 * as a **copy** while the pool has more than one worker: the thread that starts the task replaces it in the frame by an
 * equal value that shares nothing (`torb_text_privatize`, `torb_list_privatize`, `torb_map_privatize`), before anybody
 * else can see the frame. Everything else - a shared object, a `Box`, a trait-typed value, a closure whose environment
 * is not shared - does not cross at all: the task that holds it is started pinned (`torb_task_start`) and runs where
 * it was made, the way every task ran before there was a pool. The compiler writes the test and the copy beside every
 * start (backend/c/body.trb, `startStatementOf`) out of the types of the frame and the functions below, and marks a
 * closure's environment shared where its captures pass the same test (`torb_share`).
 *
 * The result of a task follows from the same rule: a worker that runs a stolen task gives up the scheduler's reference
 * **before** it publishes the completion, so the last release of the task block - and with it the release of a counted
 * result - always happens on a thread that held a handle, and all of those are one worker whenever the result is
 * counted (a handle of a counted result does not cross).
 */

#include "torb.h"
#include "torb_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ the pool --- */

#define TORB_MAXIMUM_WORKERS 1024u

/** The stack of a worker thread: a reservation, committed page by page as it grows. */
#define TORB_WORKER_STACK_SIZE ((size_t)8u * 1024u * 1024u)

torb_worker torb_main_worker;
uint32_t torb_pool_threaded = 0u;

typedef struct torb_pool_state {
  /** `workers[0]` is `torb_main_worker`; `NULL` while the pool's threads are not running. */
  torb_worker **workers;
  /** How many workers exist now: 1 until the pool starts. */
  uint32_t count;
  /** `torb_workers_count()`, read once; 0 until then. */
  uint32_t configured;
  /** The runtime tests' choice for the next start, or 0. */
  uint32_t chosen;
  uint32_t started;
  uint32_t stopping;
  /** Tasks queued, tasks running and timers armed, over every worker: zero means nothing can happen any more. */
  int64_t busy;
  /** Tasks that have not completed. */
  int64_t live;
  /** Tasks in a steal list, over every worker. */
  uint32_t stealable;
  /** Workers waiting on their condition. */
  uint32_t sleeping;
  /** What the main thread's `torb_scheduler_run` waits for, so the worker that completes it can wake it. */
  torb_task *until;
  /** A `Process.exit` from another worker than the main thread, which the main thread carries out; and its code. */
  uint32_t exiting;
  /** Whether some worker asked for the exit at all: the first request is the one that counts. Under worker 0's lock. */
  uint32_t exit_claimed;
  int64_t exit_code;
  /** Every task that has not completed, under the tree lock. */
  torb_task *live_first;
  torb_task *live_last;
  /** What stopped workers did, folded in when they were joined. */
  torb_pool_statistics totals;
  /**
   * The value of `busy` at which the main thread wants to be woken because nothing can happen any more: 0, or 1 while a
   * test body inside the running main task waits for its tasks - the main task itself counts as running.
   */
  int64_t floor;
  /* ---- the test in progress on the main thread ("A panic in a task of a test"); under the tree lock ---- */
  /** Its number, 0 while no test is in progress; and the last number given out. */
  uint32_t test_current;
  uint32_t test_sequence;
  /** The task its body runs inside (the main task of an entry that waits, or `NULL`): what that makes is the test's. */
  torb_task *test_host;
  /** Its tasks that have not completed. Read without the lock by the main thread's loop. */
  int64_t test_live;
  /** The main thread waits for them (`torb_test_tasks_end`): the host is suspended below that wait. Main thread only. */
  uint32_t test_draining;
  /** Whether a task of it panicked, and the first such panic's message and site. */
  uint32_t test_failed;
  torb_location test_at;
  char test_message[1024];
} torb_pool_state;

/** Blocks copied at a crossing, over the life of the process: any thread that starts a task may count one. */
static int64_t torb_pool_copies = 0;

static torb_pool_state torb_pool = { NULL, 1u, 0u, 0u, 0u, 0u, 0, 0, 0u, 0u, NULL, 0u, 0u, 0, NULL, NULL,
                                     { 0u, 0u, 0u, 0u }, 0, 0u, 0u, NULL, 0, 0u, 0u, { NULL, 0u, 0u }, { 0 } };

/** The parent, child and live links of every task. The first lock of the order at the top of the file. */
static torb_mutex torb_tree = TORB_MUTEX_INITIALIZER;

/* The blocking pool ("The blocking pool" below), whose workers are numbered from `TORB_BLOCKING_INBOX` on (torb_pool.h). */
#define TORB_BLOCKING_DEFAULT 4u

typedef struct torb_blocking_state {
  /** The threads' workers, `count` of them; `NULL` until the pool first starts. */
  torb_worker **workers;
  /** How many threads run now: 0 until a task first moves to the pool. Published with a release. */
  uint32_t count;
  /** `torb_workers_blocking()`, read once; 0 until then. */
  uint32_t configured;
  /** The runtime tests' choice for the next start, or 0. */
  uint32_t chosen;
  /** Tasks in the inbox: what a thread of the pool that has nothing to do looks at before it sleeps. */
  uint32_t waiting;
} torb_blocking_state;

static torb_blocking_state torb_blocking;
/** The queue every thread of the blocking pool takes from. */
static torb_worker torb_blocking_inbox;
/** Starting the pool's threads, which the first two tasks to move may ask for at once. */
static torb_mutex torb_blocking_start_lock = TORB_MUTEX_INITIALIZER;

struct torb_channel {
  torb_header header;
  const torb_element *item;
  /** `capacity` items of `item->size` bytes, a ring from `head`; `NULL` where either is zero. */
  uint8_t *buffer;
  uint32_t capacity;
  uint32_t head;
  uint32_t count;
  /** The writing end is done: after what was already offered, every receive is `None`. */
  uint8_t ended;
  /** The reading end is gone: every send fails. */
  uint8_t closed;
  /** The lock of everything above and the two queues: its two ends may be on two workers. */
  uint32_t lock;
  torb_task_list senders;
  torb_task_list receivers;
};

const torb_element torb_element_void = { (uint32_t)sizeof(torb_void), (uint32_t)TORB_ALIGN_OF(torb_void),
                                         NULL, NULL, NULL, NULL };

static TORB_NORETURN void torb_internal_error(const char *message) {
  char buffer[256];
  snprintf(buffer, sizeof buffer, "internal error: %s", message);
  torb_panic_text(buffer, torb_location_unknown);
}

static torb_worker *torb_worker_at(uint32_t index) {
  if (index == 0u) {
    return &torb_main_worker;
  }
  if (index >= TORB_BLOCKING_INBOX) {
    return index == TORB_BLOCKING_INBOX ? &torb_blocking_inbox : torb_blocking.workers[index - TORB_BLOCKING_INBOX - 1u];
  }
  return torb_pool.workers[index];
}

/* A thread of the blocking pool, which takes from the inbox and is never a victim or a thief of the ring. */
static bool torb_is_blocking_worker(const torb_worker *worker) {
  return worker->index > TORB_BLOCKING_INBOX;
}

/* The lock and the condition of a worker, made once. Worker 0's the first time any task is touched. */
static void torb_worker_prepare(torb_worker *worker, uint32_t index) {
  if (worker->prepared) {
    return;
  }
  torb_mutex_initialize(&worker->lock);
  torb_condition_initialize(&worker->wake);
  worker->index = index;
  worker->prepared = true;
}

static torb_worker *torb_worker_self(void) {
  torb_worker *worker = torb_worker_current();
  if (!worker->prepared) {
    torb_worker_prepare(worker, 0u);
  }
  return worker;
}

/* Signals a worker that sleeps. `wanted` tells the signal from a spurious wakeup. */
static void torb_wake_worker(torb_worker *worker) {
  torb_mutex_lock(&worker->lock);
  worker->wanted = 1u;
  if (worker->sleeping != 0u) {
    torb_condition_signal(&worker->wake);
  }
  torb_mutex_unlock(&worker->lock);
}

/*
 * One thing less that could still happen. At zero - or at the floor a test that waits inside the main task set - the
 * main thread may be waiting for exactly that.
 */
static void torb_busy_less(void) {
  if (torb_atomic_add_i64(&torb_pool.busy, -1) - 1 <= torb_atomic_load_i64(&torb_pool.floor)) {
    torb_wake_worker(&torb_main_worker);
  }
}

/* ------------------------------------------------------------------------------------------- intrusive lists --- */

static void torb_waiters_append(torb_task_list *list, torb_task *task) {
  task->wait_next = NULL;
  task->wait_previous = list->last;
  if (list->last != NULL) {
    list->last->wait_next = task;
  } else {
    list->first = task;
  }
  list->last = task;
}

static void torb_waiters_unlink(torb_task_list *list, torb_task *task) {
  if (task->wait_previous != NULL) {
    task->wait_previous->wait_next = task->wait_next;
  } else {
    list->first = task->wait_next;
  }
  if (task->wait_next != NULL) {
    task->wait_next->wait_previous = task->wait_previous;
  } else {
    list->last = task->wait_previous;
  }
  task->wait_previous = NULL;
  task->wait_next = NULL;
}

static void torb_link_child(torb_task *parent, torb_task *child) {
  child->parent = parent;
  child->sibling_next = NULL;
  child->sibling_previous = parent->children_last;
  if (parent->children_last != NULL) {
    parent->children_last->sibling_next = child;
  } else {
    parent->children_first = child;
  }
  parent->children_last = child;
}

static void torb_unlink_child(torb_task *child) {
  torb_task *parent = child->parent;
  if (parent == NULL) {
    return;
  }
  if (child->sibling_previous != NULL) {
    child->sibling_previous->sibling_next = child->sibling_next;
  } else {
    parent->children_first = child->sibling_next;
  }
  if (child->sibling_next != NULL) {
    child->sibling_next->sibling_previous = child->sibling_previous;
  } else {
    parent->children_last = child->sibling_previous;
  }
  child->parent = NULL;
  child->sibling_previous = NULL;
  child->sibling_next = NULL;
}

/* ------------------------------------------------------------------------------------------- the run queues --- */

/* `worker->lock` held. The back of the queue, and of the steal list where the task may still move. */
static void torb_enqueue_locked(torb_worker *worker, torb_task *task) {
  task->queued = 1u;
  task->queue_next = NULL;
  task->queue_previous = worker->queue_last;
  if (worker->queue_last != NULL) {
    worker->queue_last->queue_next = task;
  } else {
    worker->queue_first = task;
  }
  worker->queue_last = task;
  /* Only the ring steals, and only from the ring: the queue of a blocking thread and the inbox are nobody's victims */
  if (task->portable != 0u && task->started == 0u && worker->index < TORB_BLOCKING_INBOX) {
    task->stealable = 1u;
    task->steal_next = NULL;
    task->steal_previous = worker->steal_last;
    if (worker->steal_last != NULL) {
      worker->steal_last->steal_next = task;
    } else {
      worker->steal_first = task;
    }
    worker->steal_last = task;
    (void)torb_atomic_add_u32(&torb_pool.stealable, 1u);
  }
  (void)torb_atomic_add_i64(&torb_pool.busy, 1);
}

/* `worker->lock` held. Out of the queue, and out of the steal list; `busy` stays, because it runs next. */
static void torb_unqueue_locked(torb_worker *worker, torb_task *task) {
  if (task->queue_previous != NULL) {
    task->queue_previous->queue_next = task->queue_next;
  } else {
    worker->queue_first = task->queue_next;
  }
  if (task->queue_next != NULL) {
    task->queue_next->queue_previous = task->queue_previous;
  } else {
    worker->queue_last = task->queue_previous;
  }
  task->queue_previous = NULL;
  task->queue_next = NULL;
  task->queued = 0u;
  if (task->stealable != 0u) {
    if (task->steal_previous != NULL) {
      task->steal_previous->steal_next = task->steal_next;
    } else {
      worker->steal_first = task->steal_next;
    }
    if (task->steal_next != NULL) {
      task->steal_next->steal_previous = task->steal_previous;
    } else {
      worker->steal_last = task->steal_previous;
    }
    task->steal_previous = NULL;
    task->steal_next = NULL;
    task->stealable = 0u;
    (void)torb_atomic_sub_u32(&torb_pool.stealable, 1u);
  }
}

/*
 * Puts `task` at the back of its worker's queue, from any thread, and signals that worker where it sleeps. Nothing
 * happens where the task is queued already. The worker is read again under its lock, because a task that has not run
 * yet may have been taken by another worker in between.
 */
static void torb_notify_blocking(void);

static void torb_post(torb_task *task) {
  for (;;) {
    uint32_t index = torb_atomic_load_u32(&task->worker);
    torb_worker *owner = torb_worker_at(index);
    bool inboxed = false;
    torb_mutex_lock(&owner->lock);
    if (torb_atomic_peek_u32(&task->worker) != index) {
      torb_mutex_unlock(&owner->lock);
      continue;
    }
    if (task->queued == 0u) {
      torb_enqueue_locked(owner, task);
      /* The inbox has no thread of its own: whichever thread of the blocking pool is idle takes it */
      if (index == TORB_BLOCKING_INBOX) {
        (void)torb_atomic_add_u32(&torb_blocking.waiting, 1u);
        inboxed = true;
      }
    }
    if (owner->sleeping != 0u) {
      owner->wanted = 1u;
      torb_condition_signal(&owner->wake);
    }
    torb_mutex_unlock(&owner->lock);
    if (inboxed) {
      torb_notify_blocking();
    }
    return;
  }
}

/* The oldest task of the blocking pool's inbox, made the calling thread's; `NULL` where there is none. */
static torb_task *torb_take_blocking(torb_worker *self) {
  torb_task *task;
  if (torb_atomic_read_u32(&torb_blocking.waiting) == 0u) {
    return NULL;
  }
  torb_mutex_lock(&torb_blocking_inbox.lock);
  task = torb_blocking_inbox.queue_first;
  if (task != NULL) {
    torb_unqueue_locked(&torb_blocking_inbox, task);
    (void)torb_atomic_sub_u32(&torb_blocking.waiting, 1u);
    torb_atomic_store_u32(&task->worker, self->index);
  }
  torb_mutex_unlock(&torb_blocking_inbox.lock);
  return task;
}

/* A task was put into the inbox: one thread of the blocking pool that sleeps is woken to take it. */
static void torb_notify_blocking(void) {
  uint32_t count = torb_atomic_load_u32(&torb_blocking.count);
  uint32_t index;
  for (index = 0u; index < count; index += 1u) {
    torb_worker *worker = torb_blocking.workers[index];
    if (torb_atomic_peek_u32(&worker->sleeping) == 0u) {
      continue;
    }
    torb_mutex_lock(&worker->lock);
    if (worker->sleeping != 0u && worker->wanted == 0u) {
      worker->wanted = 1u;
      torb_condition_signal(&worker->wake);
      torb_mutex_unlock(&worker->lock);
      return;
    }
    torb_mutex_unlock(&worker->lock);
  }
}

/* The next task of the calling worker's own queue, or `NULL`. */
static torb_task *torb_take_local(torb_worker *self) {
  torb_task *task;
  torb_mutex_lock(&self->lock);
  task = self->queue_first;
  if (task != NULL) {
    torb_unqueue_locked(self, task);
  }
  torb_mutex_unlock(&self->lock);
  return task;
}

/*
 * The oldest unstarted task that may move from another worker's queue, made the calling worker's: the victims are
 * tried round the ring, starting at the next worker, so thieves spread over them.
 */
static torb_task *torb_steal(torb_worker *self) {
  uint32_t count = torb_pool.count;
  uint32_t offset;
  if (torb_atomic_read_u32(&torb_pool.stealable) == 0u) {
    return NULL;
  }
  for (offset = 1u; offset < count; offset += 1u) {
    torb_worker *victim = torb_worker_at((self->index + offset) % count);
    torb_task *task;
    if (torb_atomic_peek_u32(&torb_pool.stealable) == 0u) {
      return NULL;
    }
    torb_mutex_lock(&victim->lock);
    task = victim->steal_first;
    if (task != NULL) {
      torb_unqueue_locked(victim, task);
      torb_atomic_store_u32(&task->worker, self->index);
    }
    torb_mutex_unlock(&victim->lock);
    if (task != NULL) {
      self->stole += 1u;
      return task;
    }
  }
  return NULL;
}

/* A task that may move was queued: a worker that sleeps is woken to take it. */
static void torb_notify_idle(torb_worker *self) {
  uint32_t count = torb_pool.count;
  uint32_t offset;
  if (torb_atomic_read_u32(&torb_pool.sleeping) == 0u) {
    return;
  }
  /* From a thread of the blocking pool, every worker of the ring is somebody else */
  for (offset = 0u; offset < count; offset += 1u) {
    torb_worker *worker = torb_worker_at((self->index + offset) % count);
    if (worker == self || torb_atomic_peek_u32(&worker->sleeping) == 0u) {
      continue;
    }
    torb_mutex_lock(&worker->lock);
    if (worker->sleeping != 0u && worker->wanted == 0u) {
      worker->wanted = 1u;
      torb_condition_signal(&worker->wake);
      torb_mutex_unlock(&worker->lock);
      return;
    }
    torb_mutex_unlock(&worker->lock);
  }
}

/* -------------------------------------------------------------------------------------------------- timers --- */

/* A worker's timers are its own thread's: only the worker that runs a task arms or removes its timer. */

static bool torb_timer_before(const torb_timer *first, const torb_timer *second) {
  if (first->deadline != second->deadline) {
    return first->deadline < second->deadline;
  }
  return first->sequence < second->sequence;
}

static void torb_timer_place(torb_scheduler *scheduler, uint32_t index, torb_timer timer) {
  scheduler->timers[index] = timer;
  timer.task->timer = (int32_t)index;
}

static void torb_timer_sift_up(torb_scheduler *scheduler, uint32_t index) {
  torb_timer moving = scheduler->timers[index];
  while (index > 0u) {
    uint32_t parent = (index - 1u) / 2u;
    if (!torb_timer_before(&moving, &scheduler->timers[parent])) {
      break;
    }
    torb_timer_place(scheduler, index, scheduler->timers[parent]);
    index = parent;
  }
  torb_timer_place(scheduler, index, moving);
}

static void torb_timer_sift_down(torb_scheduler *scheduler, uint32_t index) {
  torb_timer moving = scheduler->timers[index];
  for (;;) {
    uint32_t smallest = index;
    uint32_t left = index * 2u + 1u;
    uint32_t right = left + 1u;
    const torb_timer *best = &moving;
    if (left < scheduler->timer_count && torb_timer_before(&scheduler->timers[left], best)) {
      smallest = left;
      best = &scheduler->timers[left];
    }
    if (right < scheduler->timer_count && torb_timer_before(&scheduler->timers[right], best)) {
      smallest = right;
    }
    if (smallest == index) {
      break;
    }
    torb_timer_place(scheduler, index, scheduler->timers[smallest]);
    index = smallest;
  }
  torb_timer_place(scheduler, index, moving);
}

static void torb_timer_add(torb_worker *self, torb_task *task, torb_instant deadline) {
  torb_scheduler *scheduler = &self->scheduler;
  torb_timer timer;
  if (scheduler->timer_count == scheduler->timer_capacity) {
    uint32_t capacity = scheduler->timer_capacity == 0u ? 16u : scheduler->timer_capacity * 2u;
    torb_timer *grown;
    if (scheduler->timer_capacity > 0x3FFFFFFFu) {
      torb_panic_text("more than a billion timers at once are not supported", torb_location_unknown);
    }
    grown = (torb_timer *)torb_raw_allocate((size_t)capacity * sizeof(torb_timer));
    if (scheduler->timer_count > 0u) {
      memcpy(grown, scheduler->timers, (size_t)scheduler->timer_count * sizeof(torb_timer));
    }
    torb_raw_free(scheduler->timers, (size_t)scheduler->timer_capacity * sizeof(torb_timer));
    scheduler->timers = grown;
    scheduler->timer_capacity = capacity;
  }
  timer.deadline = deadline;
  timer.sequence = scheduler->timer_sequence;
  timer.task = task;
  scheduler->timer_sequence += 1u;
  scheduler->timer_count += 1u;
  torb_timer_place(scheduler, scheduler->timer_count - 1u, timer);
  torb_timer_sift_up(scheduler, scheduler->timer_count - 1u);
  (void)torb_atomic_add_i64(&torb_pool.busy, 1);
}

static void torb_timer_remove(torb_worker *self, uint32_t index) {
  torb_scheduler *scheduler = &self->scheduler;
  torb_task *task = scheduler->timers[index].task;
  torb_timer last;
  scheduler->timer_count -= 1u;
  last = scheduler->timers[scheduler->timer_count];
  task->timer = -1;
  if (index < scheduler->timer_count) {
    torb_timer_place(scheduler, index, last);
    torb_timer_sift_up(scheduler, index);
    torb_timer_sift_down(scheduler, (uint32_t)last.task->timer);
  }
  torb_busy_less();
}

/* ---------------------------------------------------------------------------------------- waiting and waking --- */

static void torb_release_item(const torb_element *item, void *value) {
  if (item->release != NULL) {
    torb_element_release(item, value);
  }
}

static void torb_move_item(const torb_element *item, void *to, const void *from) {
  if (item->size > 0u) {
    memcpy(to, from, item->size);
  }
}

/*
 * Takes a task out of the waiter list or the channel queue it waits in, where it still waits there, and answers
 * whether it did. Called on the worker the task belongs to - by a cancellation there, and by that worker when it takes
 * a task that was queued while it still waited (`torb_settle`) - so the target named by `wait_target` is alive: the
 * task's frame holds the task it awaits, and a channel wait holds its channel. Where it is `cancelling`, an item the
 * task offered is released, because the send consumed it and nobody took it.
 */
static bool torb_take_out(torb_task *task, torb_outcome outcome, bool cancelling) {
  uint8_t waiting = torb_atomic_load_u8(&task->waiting);
  bool removed = false;
  if (waiting == (uint8_t)TORB_WAITING_TASK) {
    torb_task *awaited = (torb_task *)task->wait_target;
    torb_spin_lock(&awaited->lock);
    if (task->waiting == (uint8_t)TORB_WAITING_TASK) {
      torb_waiters_unlink(&awaited->waiters, task);
      torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
      task->outcome = (int32_t)outcome;
      removed = true;
    }
    torb_spin_unlock(&awaited->lock);
  } else if (waiting == (uint8_t)TORB_WAITING_SEND || waiting == (uint8_t)TORB_WAITING_RECEIVE) {
    torb_channel *channel = (torb_channel *)task->wait_target;
    torb_spin_lock(&channel->lock);
    if (task->waiting == waiting) {
      torb_waiters_unlink(waiting == (uint8_t)TORB_WAITING_SEND ? &channel->senders : &channel->receivers, task);
      torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
      task->outcome = (int32_t)outcome;
      removed = true;
    }
    torb_spin_unlock(&channel->lock);
    if (removed) {
      if (cancelling && waiting == (uint8_t)TORB_WAITING_SEND) {
        torb_release_item(channel->item, task->wait_slot);
      }
      /* A waiting task holds its channel, because its frame need not: the last use of a channel may be the send */
      torb_channel_release(channel);
    }
  } else if (waiting == (uint8_t)TORB_WAITING_IO) {
    /* The operation is alive: the frame of the task of the runtime that waits holds it */
    torb_io_waiting *io = (torb_io_waiting *)task->wait_target;
    torb_spin_lock(&io->lock);
    if (task->waiting == (uint8_t)TORB_WAITING_IO && io->waiter == task) {
      io->waiter = NULL;
      torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
      task->outcome = (int32_t)outcome;
      removed = true;
    }
    torb_spin_unlock(&io->lock);
    if (removed) {
      /* The kernel may still write into the operation, which is the operation's and not the frame's: it lets go later */
      torb_io_waiter_cancelled(io);
      /* What waited counted as something that could still happen; the caller queues the task, or it runs already */
      torb_busy_less();
    }
  }
  if (removed) {
    task->wait_target = NULL;
    task->wait_slot = NULL;
  }
  return removed;
}

/*
 * Wakes a task the caller just took out of a list under that list's lock (so `waiting` is already nothing): its timer
 * goes where it belongs to this worker, and it is queued on its own worker.
 */
static void torb_wake_taken(torb_worker *self, torb_task *task) {
  if (torb_atomic_peek_u32(&task->worker) == self->index && task->timer >= 0) {
    torb_timer_remove(self, (uint32_t)task->timer);
  }
  torb_post(task);
}

/* A suspension primitive is called by the running task, once per wait. Anything else is a bug of the lowering. */
static void torb_expect_idle(torb_worker *self, torb_task *task) {
  if (task != self->scheduler.current) {
    torb_internal_error("a task waited while it was not the one running");
  }
  if (task->waiting != (uint8_t)TORB_WAITING_NOTHING) {
    torb_internal_error("a task waited for two things at once");
  }
}

static void torb_deliver(torb_task *task, void *slot, const torb_element *item) {
  task->delivered = slot;
  task->delivered_element = item;
}

/*
 * What the worker does with a task it took from a queue before it runs it: a task queued while it still waited was
 * queued by a cancellation on another worker, and is taken out of where it waits here, on its own thread; its timer
 * goes too.
 */
static void torb_settle(torb_worker *self, torb_task *task) {
  uint8_t waiting = torb_atomic_load_u8(&task->waiting);
  if (waiting == (uint8_t)TORB_WAITING_TIMER) {
    torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
    task->outcome = (int32_t)TORB_OUTCOME_CANCELLED;
  } else if (waiting != (uint8_t)TORB_WAITING_NOTHING) {
    (void)torb_take_out(task, TORB_OUTCOME_CANCELLED, true);
  }
  if (task->timer >= 0) {
    torb_timer_remove(self, (uint32_t)task->timer);
  }
}

/* ------------------------------------------------------------------------------------------- cancellation --- */

/*
 * The tree lock held. The flag, and the task on its way to its stop: on this worker it is taken out of where it waits
 * at once, as the single worker always did; on another one it is queued there, and that worker takes it out when it
 * takes it (`torb_settle`) - because only the worker a task belongs to may touch its timer and read what it waits on.
 */
static void torb_cancel_one(torb_worker *self, torb_task *task) {
  if (torb_atomic_load_u8(&task->status) != (uint8_t)TORB_TASK_PENDING || task->completing != 0u) {
    return;
  }
  torb_atomic_store_u8(&task->cancelled, 1u);
  if (torb_atomic_peek_u32(&task->worker) == self->index) {
    uint8_t waiting = task->waiting;
    bool removed = false;
    if (waiting == (uint8_t)TORB_WAITING_NOTHING) {
      return;
    }
    if (waiting == (uint8_t)TORB_WAITING_TIMER) {
      torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
      task->outcome = (int32_t)TORB_OUTCOME_CANCELLED;
      removed = true;
    } else {
      removed = torb_take_out(task, TORB_OUTCOME_CANCELLED, true);
    }
    if (task->timer >= 0) {
      torb_timer_remove(self, (uint32_t)task->timer);
    }
    if (removed) {
      torb_post(task);
    }
    return;
  }
  torb_post(task);
}

/* The tree lock held. The subtree in pre-order, without a stack: down to the first child, else across, else up. */
static void torb_cancel_subtree(torb_worker *self, torb_task *root) {
  torb_task *node = root;
  for (;;) {
    torb_cancel_one(self, node);
    if (node->children_first != NULL) {
      node = node->children_first;
      continue;
    }
    while (node != root && node->sibling_next == NULL) {
      node = node->parent;
    }
    if (node == root) {
      return;
    }
    node = node->sibling_next;
  }
}

void torb_task_cancel(torb_task *self) {
  torb_worker *worker = torb_worker_self();
  torb_mutex_lock(&torb_tree);
  torb_cancel_subtree(worker, self);
  torb_mutex_unlock(&torb_tree);
}

/* ----------------------------------------------------------------------------------------------- completion --- */

/*
 * The tree lock held. A task that finished hands the children still running to its own parent, so that cancelling the
 * grandparent still reaches them; a parent that is already cancelled cancels what it adopts.
 */
static void torb_hand_children_up(torb_worker *self, torb_task *task) {
  torb_task *parent = task->parent;
  torb_task *child = task->children_first;
  task->children_first = NULL;
  task->children_last = NULL;
  while (child != NULL) {
    torb_task *next = child->sibling_next;
    child->parent = NULL;
    child->sibling_previous = NULL;
    child->sibling_next = NULL;
    if (parent != NULL) {
      torb_link_child(parent, child);
      if (torb_atomic_load_u8(&parent->cancelled) != 0u) {
        torb_cancel_subtree(self, child);
      }
    }
    child = next;
  }
}

static void torb_complete(torb_worker *self, torb_task *task, bool finished) {
  torb_task *child;
  bool last;
  bool unqueued = false;
  bool last_of_test = false;
  if (task->waiting != (uint8_t)TORB_WAITING_NOTHING) {
    torb_internal_error("a task completed while it was still waiting");
  }
  /* An item a receive delivered that the machine never took, because it stopped at its check first. */
  if (task->delivered != NULL) {
    torb_release_item(task->delivered_element, task->delivered);
    task->delivered = NULL;
    task->delivered_element = NULL;
  }
  torb_mutex_lock(&torb_tree);
  /* From here no cancellation posts it any more, so the queue check below is final */
  task->completing = 1u;
  if (!finished) {
    for (child = task->children_first; child != NULL; child = child->sibling_next) {
      torb_cancel_subtree(self, child);
    }
  }
  torb_hand_children_up(self, task);
  torb_unlink_child(task);
  if (task->live_previous != NULL) {
    task->live_previous->live_next = task->live_next;
  } else {
    torb_pool.live_first = task->live_next;
  }
  if (task->live_next != NULL) {
    task->live_next->live_previous = task->live_previous;
  } else {
    torb_pool.live_last = task->live_previous;
  }
  task->live_previous = NULL;
  task->live_next = NULL;
  (void)torb_atomic_add_i64(&torb_pool.live, -1);
  /* The last task of the test in progress: the main thread may be waiting for exactly that */
  if (task->test != 0u && task->test == torb_pool.test_current
      && torb_atomic_add_i64(&torb_pool.test_live, -1) == 1 && self != &torb_main_worker) {
    last_of_test = true;
  }
  torb_mutex_unlock(&torb_tree);
  if (last_of_test) {
    torb_wake_worker(&torb_main_worker);
  }
  /* A cancellation from another worker may have queued it while it ran for the last time */
  torb_mutex_lock(&self->lock);
  if (task->queued != 0u) {
    torb_unqueue_locked(self, task);
    unqueued = true;
  }
  torb_mutex_unlock(&self->lock);
  if (unqueued) {
    torb_busy_less();
  }
  /*
   * The scheduler's reference goes **before** the completion is published: a waiter that reads a counted result and
   * drops its handle then never leaves this worker holding the last reference, so the result is released on a thread
   * that held a handle ("What crosses a worker"). Where the count reaches zero nobody holds a handle, so nobody waits.
   */
  torb_spin_lock(&task->lock);
  last = torb_count_down(task);
  torb_atomic_store_u8(&task->status, (uint8_t)(finished ? TORB_TASK_FINISHED : TORB_TASK_CANCELLED));
  while (task->waiters.first != NULL) {
    torb_task *waiter = task->waiters.first;
    torb_waiters_unlink(&task->waiters, waiter);
    /* The cascade: a waiter of `await()` is cancelled by a cancellation it waited for, and stops at its next check */
    if (!finished && waiter->observing == 0u) {
      torb_atomic_store_u8(&waiter->cancelled, 1u);
    }
    torb_atomic_store_u8(&waiter->waiting, (uint8_t)TORB_WAITING_NOTHING);
    waiter->outcome = (int32_t)TORB_OUTCOME_READY;
    waiter->wait_target = NULL;
    torb_wake_taken(self, waiter);
  }
  torb_spin_unlock(&task->lock);
  if (task == torb_pool.until && self != &torb_main_worker) {
    torb_wake_worker(&torb_main_worker);
  }
  if (last) {
    torb_free_counted(task, torb_task_drop);
  }
}

/*
 * # A panic in a task of a test
 *
 * A test owns the tasks it starts: every task made on the main thread while a test is in progress there - outside the
 * tasks of that test, which pass it on to theirs - carries the test's number, and the test does not end before they
 * have completed (`torb_test_tasks_end`). Such a task is resumed under a recovery point of the thread that runs it, so
 * a panic in it lands here on every worker alike, the main thread included: the first panic of the test's tasks is
 * recorded as the test's failure, every task of the test is cancelled, and the one that panicked is completed as
 * cancelled without returning to its machine - so whoever awaits it reads `Fail(Cancelled)` and the pool's counts stay
 * exact. What its frame held stays allocated, exactly as after a recovered panic of a test body.
 *
 * A panic in a task that belongs to no test in progress - the top-level code's, or a test's that already ended - is not
 * a test's to report, and ends the process as it does on the main thread. Nothing of this runs outside a test run: a
 * task of no test is resumed without a recovery point.
 */

/* The tree lock held: the flag of every task of test `test`, which then stops at its next check. */
static void torb_cancel_test_locked(torb_worker *self, uint32_t test) {
  torb_task *task;
  for (task = torb_pool.live_first; task != NULL; task = task->live_next) {
    if (task->test == test) {
      torb_cancel_one(self, task);
    }
  }
}

/* The machine of a test's task, under a recovery point of this thread. False where it panicked; `point` says how. */
static bool torb_resume_recovered(torb_worker *self, torb_task *task, torb_poll *poll, torb_recovery *point) {
  torb_recovery *previous = torb_begin_recovery(point);
  if (setjmp(point->destination) != 0) {
    self->recovery = previous;
    return false;
  }
  *poll = task->resume(task);
  torb_end_recovery(previous);
  return true;
}

/* A task of a test panicked in its machine, whose frames are gone: the test's failure, or the end of the process. */
static void torb_task_panicked(torb_worker *self, torb_task *task, const torb_recovery *point) {
  bool attributed;
  torb_mutex_lock(&torb_tree);
  attributed = task->test == torb_pool.test_current;
  if (attributed) {
    if (torb_pool.test_failed == 0u) {
      torb_pool.test_failed = 1u;
      snprintf(torb_pool.test_message, sizeof torb_pool.test_message, "%s", point->message);
      torb_pool.test_at = point->at;
    }
    /* The panicking task too: one that panicked while registered somewhere is taken out of there */
    torb_cancel_test_locked(self, task->test);
  }
  torb_mutex_unlock(&torb_tree);
  if (!attributed) {
    self->recovery = NULL;
    torb_panic_text(point->message, point->at);
  }
  torb_complete(self, task, false);
}

static void torb_run_one(torb_worker *self, torb_task *task) {
  torb_scheduler *scheduler = &self->scheduler;
  torb_poll poll = TORB_POLL_SUSPENDED;
  torb_settle(self, task);
  task->started = 1u;
  scheduler->current = task;
  if (task->test == 0u) {
    poll = task->resume(task);
  } else {
    torb_recovery point;
    if (!torb_resume_recovered(self, task, &poll, &point)) {
      scheduler->current = NULL;
      self->ran += 1u;
      torb_task_panicked(self, task, &point);
      torb_busy_less();
      return;
    }
  }
  scheduler->current = NULL;
  self->ran += 1u;
  switch (poll) {
    case TORB_POLL_SUSPENDED:
      /* With threads, a waker may have taken it out already and not yet queued it, so the check is one thread's */
      if (torb_pool_threaded == 0u && task->waiting == (uint8_t)TORB_WAITING_NOTHING && task->queued == 0u) {
        torb_internal_error("a task suspended without waiting for anything");
      }
      break;
    case TORB_POLL_FINISHED:
      torb_complete(self, task, true);
      break;
    case TORB_POLL_STOPPED:
      torb_complete(self, task, false);
      break;
    default:
      torb_internal_error("a resume function answered something that is not a torb_poll");
  }
  torb_busy_less();
}

/* ------------------------------------------------------------------------------------ making and holding one --- */

torb_task *torb_task_new(torb_resume_function resume, size_t frame_size, const torb_element *result) {
  torb_worker *self = torb_worker_self();
  size_t alignment = result->align == 0u ? 1u : (size_t)result->align;
  size_t result_offset;
  size_t total;
  torb_task *task;
  torb_task *parent;
  if (frame_size > (size_t)0x7FFFFFFFu) {
    torb_panic_text("a task frame larger than 2 GiB is not supported", torb_location_unknown);
  }
  result_offset = (TORB_TASK_FRAME_OFFSET + frame_size + alignment - 1u) / alignment * alignment;
  total = result_offset + (size_t)result->size;
  if (total > (size_t)0xFFFFFFFFu) {
    torb_panic_text("a task larger than 4 GiB is not supported", torb_location_unknown);
  }
  task = (torb_task *)torb_allocate_zeroed(total, TORB_BLOCK_TASK);
  /* A handle may be held on another worker than the one that runs the task */
  torb_share(task);
  task->resume = resume;
  task->result = result;
  task->state = 0u;
  task->result_offset = (uint32_t)result_offset;
  task->status = (uint8_t)TORB_TASK_PENDING;
  task->waiting = (uint8_t)TORB_WAITING_NOTHING;
  task->outcome = (int32_t)TORB_OUTCOME_NONE;
  task->timer = -1;
  task->worker = self->index;
  parent = self->scheduler.current;
  torb_mutex_lock(&torb_tree);
  if (parent != NULL) {
    torb_link_child(parent, task);
    if (torb_atomic_load_u8(&parent->cancelled) != 0u) {
      task->cancelled = 1u;
    }
  }
  /* A test's task makes tasks of the test, and so does its body on the main thread ("A panic in a task of a test") */
  if (parent != NULL && parent->test != 0u) {
    task->test = parent->test;
  } else if (torb_pool.test_current != 0u && self == &torb_main_worker && parent == torb_pool.test_host) {
    task->test = torb_pool.test_current;
  }
  if (task->test != 0u && task->test == torb_pool.test_current) {
    (void)torb_atomic_add_i64(&torb_pool.test_live, 1);
  }
  task->live_next = NULL;
  task->live_previous = torb_pool.live_last;
  if (torb_pool.live_last != NULL) {
    torb_pool.live_last->live_next = task;
  } else {
    torb_pool.live_first = task;
  }
  torb_pool.live_last = task;
  (void)torb_atomic_add_i64(&torb_pool.live, 1);
  torb_mutex_unlock(&torb_tree);
  return task;
}

void torb_task_start(torb_task *task) {
  torb_retain(task);
  torb_post(task);
}

static void torb_pool_start(void);

void torb_task_start_portable(torb_task *task) {
  torb_worker *self = torb_worker_self();
  if (torb_pool.started == 0u) {
    torb_pool_start();
  }
  task->portable = 1u;
  torb_retain(task);
  torb_post(task);
  if (torb_pool_threaded != 0u) {
    torb_notify_idle(self);
  }
}

void torb_task_drop(void *block) {
  torb_task *task = (torb_task *)block;
  /* A worker that completed the task may still be inside its lock, publishing the completion: it leaves first */
  torb_spin_lock(&task->lock);
  torb_spin_unlock(&task->lock);
  if (task->status == (uint8_t)TORB_TASK_PENDING) {
    torb_internal_error("released a task that was never started");
  }
  if (task->status == (uint8_t)TORB_TASK_FINISHED) {
    torb_release_item(task->result, torb_task_result_slot(task));
  }
}

void torb_task_release(torb_task *task) {
  torb_release(task, torb_task_drop);
}

/* ------------------------------------------------------------------------------------ suspension primitives --- */

/*
 * `await()` and `result()`: the one wait for a task, and whether a cancellation of `awaited` is passed on to `self`
 * (docs/design/CONCURRENCY.md section 8, "The cascade"). Passing it on is setting the flag and nothing else: the check
 * the machine makes after every wait then stops it through the same stop path a `cancel()` reaches, and its children are
 * cancelled when it completes as cancelled, as for every task that stops. Where `awaited` completes later, the flag is
 * set by `torb_complete` under `awaited->lock`, before `self` is woken - so it is set before `self` runs again.
 */
static torb_wait torb_await_task(torb_task *self, torb_task *awaited, bool observing) {
  torb_worker *worker = torb_worker_self();
  uint8_t status;
  if (self == awaited) {
    torb_panic_text("a task cannot await itself: nothing could ever wake it", torb_location_unknown);
  }
  torb_expect_idle(worker, self);
  self->observing = observing ? 1u : 0u;
  torb_spin_lock(&awaited->lock);
  status = torb_atomic_load_u8(&awaited->status);
  if (status != (uint8_t)TORB_TASK_PENDING) {
    torb_spin_unlock(&awaited->lock);
    if (status == (uint8_t)TORB_TASK_CANCELLED && !observing) {
      torb_atomic_store_u8(&self->cancelled, 1u);
    }
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  self->wait_target = awaited;
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_TASK);
  torb_waiters_append(&awaited->waiters, self);
  torb_spin_unlock(&awaited->lock);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_task_await(torb_task *self, torb_task *awaited) {
  return torb_await_task(self, awaited, false);
}

torb_wait torb_task_observe(torb_task *self, torb_task *awaited) {
  return torb_await_task(self, awaited, true);
}

torb_wait torb_task_await_until(torb_task *self, torb_task *awaited, torb_instant deadline) {
  torb_worker *worker = torb_worker_self();
  if (self == awaited) {
    torb_panic_text("a task cannot await itself: nothing could ever wake it", torb_location_unknown);
  }
  torb_expect_idle(worker, self);
  self->observing = 1u;
  if (torb_atomic_load_u8(&awaited->status) != (uint8_t)TORB_TASK_PENDING) {
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (deadline <= torb_clock_now()) {
    self->outcome = (int32_t)TORB_OUTCOME_TIMED_OUT;
    return TORB_WAIT_READY;
  }
  torb_spin_lock(&awaited->lock);
  if (torb_atomic_load_u8(&awaited->status) != (uint8_t)TORB_TASK_PENDING) {
    torb_spin_unlock(&awaited->lock);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  self->wait_target = awaited;
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_TASK);
  torb_waiters_append(&awaited->waiters, self);
  torb_spin_unlock(&awaited->lock);
  /* Where the awaited task completes before this line, the timer is armed for a task already queued: taking it removes it */
  torb_timer_add(worker, self, deadline);
  return TORB_WAIT_SUSPENDED;
}

bool torb_task_result(torb_task *task, void *out) {
  uint8_t status = torb_atomic_load_u8(&task->status);
  if (status == (uint8_t)TORB_TASK_FINISHED) {
    torb_move_item(task->result, out, torb_task_result_slot(task));
    if (task->result->retain != NULL) {
      torb_element_retain(task->result, out);
    }
    return true;
  }
  if (status == (uint8_t)TORB_TASK_CANCELLED) {
    return false;
  }
  torb_internal_error("read the result of a task that has not completed");
}

bool torb_task_is_complete(const torb_task *task) {
  return torb_atomic_load_u8(&task->status) != (uint8_t)TORB_TASK_PENDING;
}

torb_outcome torb_task_outcome(torb_task *self) {
  self->delivered = NULL;
  self->delivered_element = NULL;
  return (torb_outcome)self->outcome;
}

torb_wait torb_task_pause(torb_task *self) {
  torb_worker *worker = torb_worker_self();
  torb_expect_idle(worker, self);
  self->outcome = (int32_t)TORB_OUTCOME_READY;
  torb_post(self);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_task_sleep_until(torb_task *self, torb_instant deadline) {
  torb_worker *worker = torb_worker_self();
  torb_expect_idle(worker, self);
  if (deadline <= torb_clock_now()) {
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_TIMER);
  torb_timer_add(worker, self, deadline);
  return TORB_WAIT_SUSPENDED;
}

/*
 * # Waiting on IO
 *
 * A task of the IO core (runtime/io.c) waits for an operation of it: a receive, a send, an accept, a connect, a name
 * resolution. The operation is completed by the IO thread or a resolver thread - threads without a worker, which run no
 * task and touch no count - and they wake the waiter here, under the operation's lock: the waiter is taken out and
 * queued on its own worker **before** the lock is let go, so a cancellation on another worker, which takes the same lock
 * to take it out, finds either a waiting task or one that is queued already, never one in between. While it waits the
 * task counts in `busy` like an armed timer, so a program whose tasks all wait for the network waits and is no deadlock.
 */

torb_wait torb_task_wait_io(torb_task *self, torb_io_waiting *waiting) {
  torb_worker *worker = torb_worker_self();
  torb_expect_idle(worker, self);
  torb_spin_lock(&waiting->lock);
  if (waiting->done != 0u) {
    torb_spin_unlock(&waiting->lock);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  waiting->waiter = self;
  self->wait_target = waiting;
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_IO);
  (void)torb_atomic_add_i64(&torb_pool.busy, 1);
  torb_spin_unlock(&waiting->lock);
  return TORB_WAIT_SUSPENDED;
}

void torb_task_io_done(torb_io_waiting *waiting) {
  torb_task *waiter;
  torb_spin_lock(&waiting->lock);
  waiting->done = 1u;
  waiter = waiting->waiter;
  waiting->waiter = NULL;
  if (waiter != NULL) {
    torb_atomic_store_u8(&waiter->waiting, (uint8_t)TORB_WAITING_NOTHING);
    waiter->outcome = (int32_t)TORB_OUTCOME_READY;
    waiter->wait_target = NULL;
    torb_post(waiter);
  }
  torb_spin_unlock(&waiting->lock);
  if (waiter != NULL) {
    /* Queued first, counted out second, so `busy` never reads zero in between */
    torb_busy_less();
  }
}

void torb_pool_prepare_thread(void) {
  if (torb_pool_threaded != 0u) {
    return;
  }
  torb_console_prepare();
  torb_clock_prepare();
  (void)torb_worker_self();
  torb_platform_set_worker(&torb_main_worker);
  torb_pool_threaded = 1u;
}

/* --------------------------------------------------------------------------- the tasks the runtime writes --- */

/* A deadline `span` nanoseconds from now, saturated at the end of the clock. */
static torb_instant torb_deadline_after(torb_duration span) {
  torb_instant now = torb_clock_now();
  if (span <= 0) {
    return now;
  }
  if (span > INT64_MAX - now) {
    return INT64_MAX;
  }
  return now + span;
}

typedef struct torb_sleep_frame {
  torb_instant deadline;
} torb_sleep_frame;

static torb_poll torb_sleep_resume(torb_task *task) {
  torb_sleep_frame *frame = (torb_sleep_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_task_sleep_until(task, frame->deadline) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  (void)torb_task_outcome(task);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

torb_task *torb_sleep(double seconds) {
  torb_duration span = 0;
  torb_task *task;
  /* `!(seconds > 0)` is also true for `nan`. 9.2e9 seconds is past the end of an `int64_t` nanosecond clock. */
  if (seconds > 0.0) {
    span = seconds >= 9.2e9 ? INT64_MAX : (torb_duration)(seconds * 1e9);
  }
  task = torb_task_new(torb_sleep_resume, sizeof(torb_sleep_frame), &torb_element_void);
  ((torb_sleep_frame *)torb_task_frame(task))->deadline = torb_deadline_after(span);
  torb_task_start(task);
  return task;
}

static torb_poll torb_pause_resume(torb_task *task) {
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

torb_task *torb_pause(void) {
  torb_task *task = torb_task_new(torb_pause_resume, 0u, &torb_element_void);
  torb_task_start(task);
  return task;
}

/*
 * # The blocking pool
 *
 * `offload` runs a body that blocks - a file, a child process, a C library - on a thread of its own instead of on a
 * worker, so the worker's other tasks go on (docs/design/CONCURRENCY.md section 16, "The blocking pool, as built"). The
 * pool is `Workers.blocking()` threads (`TORB_BLOCKING`, default 4), started the first time a task moves to it and
 * joined with the workers at the end of the program.
 *
 * **A thread of the blocking pool is a worker without a place in the ring**: it has a heap (its block counters), a
 * scheduler and a run queue of its own, and runs the loop every worker runs - but nobody steals from it and it steals
 * from nobody. What it takes instead is the **inbox**, one queue behind one lock that every thread of the pool shares
 * (a worker struct without a thread, `TORB_BLOCKING_INBOX`). A task gets there by a **turn** (`torb_blocking_turn`): it
 * awaits a task of the runtime that finishes at once, and when its worker takes it from its queue after that, the worker
 * hands it to the inbox instead of running it (`torb_hand_to_blocking`). The first thread of the pool that is idle takes
 * it, makes it its own, and runs the rest of its machine - the body - to its end.
 *
 * **Only a task whose frame may move turns**: one that was started portable, so every value its frame held at its start
 * was proven movable where it was handed over (or copied, "What crosses a worker"), and the one thing it made since is
 * the turn's handle, a task of nothing. A task started pinned takes the turn in place: it goes on on its own worker, and
 * its body blocks that worker exactly as before there was a pool - correct, and sequential. A task that already runs on
 * a thread of the pool stays there.
 *
 * **Cancelling a task of the blocking pool** is the flag, as everywhere. One that waits in the inbox is taken by a thread
 * and stops at its first check without running its body; one whose body runs is not interrupted - the thread runs the
 * body to its end, and the machine stops at its next check and releases its frame there, on that thread (section 7, the
 * row of the blocking pool: the worker is free at once, the frame at the next honest moment).
 */

static void torb_worker_main(void *argument);

/* Starts the threads of the blocking pool, once; the first two tasks to turn may ask at the same time. */
static void torb_blocking_start(void) {
  uint32_t count;
  uint32_t index;
  torb_worker **workers;
  if (torb_atomic_load_u32(&torb_blocking.count) != 0u) {
    return;
  }
  torb_mutex_lock(&torb_blocking_start_lock);
  if (torb_blocking.count != 0u) {
    torb_mutex_unlock(&torb_blocking_start_lock);
    return;
  }
  count = torb_blocking.chosen != 0u ? torb_blocking.chosen : (uint32_t)torb_workers_blocking();
  /* No thread but this one yet (a pool of one worker): what `torb_pool_start` readies before a second thread exists */
  if (torb_pool_threaded == 0u) {
    torb_console_prepare();
    torb_clock_prepare();
    (void)torb_worker_self();
    torb_platform_set_worker(&torb_main_worker);
    torb_pool_threaded = 1u;
  }
  torb_worker_prepare(&torb_blocking_inbox, TORB_BLOCKING_INBOX);
  workers = (torb_worker **)calloc(count, sizeof(torb_worker *));
  if (workers == NULL) {
    torb_panic_out_of_memory((size_t)count * sizeof(torb_worker *));
  }
  for (index = 0u; index < count; index += 1u) {
    torb_worker *worker = (torb_worker *)calloc(1u, sizeof(torb_worker));
    if (worker == NULL) {
      torb_panic_out_of_memory(sizeof(torb_worker));
    }
    torb_worker_prepare(worker, TORB_BLOCKING_INBOX + 1u + index);
    workers[index] = worker;
  }
  torb_blocking.workers = workers;
  torb_atomic_store_u32(&torb_blocking.count, count);
  for (index = 0u; index < count; index += 1u) {
    if (!torb_thread_start(&workers[index]->thread, torb_worker_main, workers[index], TORB_WORKER_STACK_SIZE)) {
      torb_panic_text("the operating system refused to start a thread of the blocking pool", torb_location_unknown);
    }
  }
  torb_mutex_unlock(&torb_blocking_start_lock);
}

static torb_poll torb_turn_resume(torb_task *task) {
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

torb_task *torb_blocking_turn(void) {
  torb_worker *self = torb_worker_self();
  torb_task *current = self->scheduler.current;
  torb_task *turn = torb_task_new(torb_turn_resume, 0u, &torb_element_void);
  if (current != NULL && current->portable != 0u && !torb_is_blocking_worker(self)) {
    torb_blocking_start();
    current->hopping = 1u;
  }
  torb_task_start(turn);
  return turn;
}

/*
 * A task whose turn completed was taken from this worker's queue: it goes to the inbox of the blocking pool instead of
 * running here. Not where it still waits for something or was cancelled - that one runs here and stops at its check. Its
 * new worker is set under this worker's lock, so a waker that read the old one retries with the inbox; it was taken, so
 * it counted as running, which the queue of the inbox counts instead.
 */
static bool torb_hand_to_blocking(torb_worker *self, torb_task *task) {
  task->hopping = 0u;
  if (torb_atomic_load_u8(&task->waiting) != (uint8_t)TORB_WAITING_NOTHING || task->timer >= 0
      || torb_task_cancelled(task)) {
    return false;
  }
  torb_mutex_lock(&self->lock);
  torb_atomic_store_u32(&task->worker, TORB_BLOCKING_INBOX);
  torb_mutex_unlock(&self->lock);
  torb_post(task);
  torb_busy_less();
  return true;
}

typedef struct torb_within_frame {
  torb_task *target;
  const torb_within_shape *shape;
  torb_duration limit;
  torb_instant deadline;
} torb_within_frame;

static torb_poll torb_within_resume(torb_task *task) {
  torb_within_frame *frame = (torb_within_frame *)torb_task_frame(task);
  torb_outcome outcome;
  bool finished;
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->target);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_task_await_until(task, frame->target, frame->deadline) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  outcome = torb_task_outcome(task);
  if (outcome == TORB_OUTCOME_TIMED_OUT) {
    torb_task_cancel(frame->target);
    frame->shape->timed_out(torb_task_result_slot(task), frame->limit);
    torb_task_release(frame->target);
    return TORB_POLL_FINISHED;
  }
  {
    /* The value passes through here on its way from one result slot to the other: `finished` consumes it. */
    union {
      int64_t integer;
      double floating;
      void *pointer;
      uint8_t bytes[64];
    } local;
    size_t size = (size_t)frame->target->result->size;
    void *value = size <= sizeof local.bytes ? (void *)local.bytes : torb_raw_allocate(size);
    finished = torb_task_result(frame->target, value);
    if (finished) {
      frame->shape->finished(torb_task_result_slot(task), value);
    }
    if (value != (void *)local.bytes) {
      torb_raw_free(value, size);
    }
  }
  torb_task_release(frame->target);
  /* A target somebody else cancelled: this task has no value either, so its `await()` answers Fail(Cancelled). */
  return finished ? TORB_POLL_FINISHED : TORB_POLL_STOPPED;
}

torb_task *torb_task_within(torb_task *self, torb_duration limit, const torb_within_shape *shape) {
  torb_task *task = torb_task_new(torb_within_resume, sizeof(torb_within_frame), shape->result);
  torb_within_frame *frame = (torb_within_frame *)torb_task_frame(task);
  torb_retain(self);
  frame->target = self;
  frame->shape = shape;
  frame->limit = limit;
  frame->deadline = torb_deadline_after(limit);
  torb_task_start(task);
  return task;
}

/* --------------------------------------------------------------------------------------------------- channels --- */

/*
 * A channel's ring and queues are behind its lock, because its two ends may be on two workers. An item crosses by
 * transfer - moved from the sender's frame into the ring or straight into the receiver's slot - and a channel whose
 * ends are on two workers carries only plain items ("What crosses a worker"), so the move is all there is to it.
 */

static void *torb_channel_slot(torb_channel *channel, uint32_t index) {
  return channel->buffer + (size_t)((channel->head + index) % channel->capacity) * channel->item->size;
}

static void torb_channel_pop(torb_channel *channel, void *out) {
  torb_move_item(channel->item, out, torb_channel_slot(channel, 0u));
  channel->head = (channel->head + 1u) % channel->capacity;
  channel->count -= 1u;
}

torb_channel *torb_channel_new(int64_t capacity, const torb_element *item, torb_location at) {
  torb_channel *channel;
  if (capacity < 0) {
    char message[128];
    snprintf(message, sizeof message, "the capacity of a channel cannot be negative, and it is %lld",
             (long long)capacity);
    torb_panic_text(message, at);
  }
  if (capacity > (int64_t)0xFFFFFFFFu || (item->size > 0u && (uint64_t)capacity > (uint64_t)SIZE_MAX / item->size)) {
    torb_panic_text("a channel that large is not supported", at);
  }
  channel = (torb_channel *)torb_allocate_zeroed(sizeof(torb_channel), TORB_BLOCK_CHANNEL);
  torb_share(channel);
  channel->item = item;
  channel->buffer = NULL;
  channel->capacity = (uint32_t)capacity;
  channel->head = 0u;
  channel->count = 0u;
  channel->ended = 0u;
  channel->closed = 0u;
  channel->lock = 0u;
  channel->senders.first = NULL;
  channel->senders.last = NULL;
  channel->receivers.first = NULL;
  channel->receivers.last = NULL;
  if (capacity > 0 && item->size > 0u) {
    channel->buffer = (uint8_t *)torb_raw_allocate((size_t)capacity * item->size);
  }
  return channel;
}

/* `channel->lock` held: takes the first task out of a queue of the channel, with the outcome it wakes up with. */
static torb_task *torb_channel_take_first(torb_task_list *list, torb_outcome outcome) {
  torb_task *task = list->first;
  torb_waiters_unlink(list, task);
  torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
  task->outcome = (int32_t)outcome;
  task->wait_target = NULL;
  return task;
}

torb_wait torb_channel_send(torb_task *self, torb_channel *channel, void *item) {
  torb_worker *worker = torb_worker_self();
  torb_task *receiver;
  torb_expect_idle(worker, self);
  torb_spin_lock(&channel->lock);
  if (channel->closed != 0u || channel->ended != 0u) {
    torb_spin_unlock(&channel->lock);
    torb_release_item(channel->item, item);
    self->outcome = (int32_t)TORB_OUTCOME_CLOSED;
    return TORB_WAIT_READY;
  }
  receiver = channel->receivers.first;
  if (receiver != NULL) {
    void *slot = receiver->wait_slot;
    torb_move_item(channel->item, slot, item);
    torb_deliver(receiver, slot, channel->item);
    receiver->wait_slot = NULL;
    (void)torb_channel_take_first(&channel->receivers, TORB_OUTCOME_READY);
    torb_spin_unlock(&channel->lock);
    torb_channel_release(channel);
    torb_post(receiver);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (channel->count < channel->capacity) {
    torb_move_item(channel->item, torb_channel_slot(channel, channel->count), item);
    channel->count += 1u;
    torb_spin_unlock(&channel->lock);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  torb_retain(channel);
  self->wait_target = channel;
  self->wait_slot = item;
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_SEND);
  torb_waiters_append(&channel->senders, self);
  torb_spin_unlock(&channel->lock);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_channel_receive(torb_task *self, torb_channel *channel, void *out) {
  torb_worker *worker = torb_worker_self();
  torb_task *sender;
  torb_expect_idle(worker, self);
  torb_spin_lock(&channel->lock);
  if (channel->count > 0u) {
    torb_channel_pop(channel, out);
    /* The room that made is the first waiting sender's. */
    sender = channel->senders.first;
    if (sender != NULL) {
      torb_move_item(channel->item, torb_channel_slot(channel, channel->count), sender->wait_slot);
      channel->count += 1u;
      sender->wait_slot = NULL;
      (void)torb_channel_take_first(&channel->senders, TORB_OUTCOME_READY);
    }
    torb_spin_unlock(&channel->lock);
    if (sender != NULL) {
      torb_channel_release(channel);
      torb_post(sender);
    }
    torb_deliver(self, out, channel->item);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  sender = channel->senders.first;
  if (sender != NULL) {
    torb_move_item(channel->item, out, sender->wait_slot);
    sender->wait_slot = NULL;
    (void)torb_channel_take_first(&channel->senders, TORB_OUTCOME_READY);
    torb_spin_unlock(&channel->lock);
    torb_channel_release(channel);
    torb_post(sender);
    torb_deliver(self, out, channel->item);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (channel->ended != 0u || channel->closed != 0u) {
    torb_spin_unlock(&channel->lock);
    self->outcome = (int32_t)TORB_OUTCOME_CLOSED;
    return TORB_WAIT_READY;
  }
  torb_retain(channel);
  self->wait_target = channel;
  self->wait_slot = out;
  torb_atomic_store_u8(&self->waiting, (uint8_t)TORB_WAITING_RECEIVE);
  torb_waiters_append(&channel->receivers, self);
  torb_spin_unlock(&channel->lock);
  return TORB_WAIT_SUSPENDED;
}

void torb_channel_end(torb_channel *channel) {
  torb_spin_lock(&channel->lock);
  if (channel->ended != 0u) {
    torb_spin_unlock(&channel->lock);
    return;
  }
  channel->ended = 1u;
  /* A receiver only waits where nothing is buffered and no sender waits, so what it waits for now is the end. The caller
     holds the channel, so releasing what a waiting receiver held never frees it here. */
  while (channel->receivers.first != NULL) {
    torb_task *receiver = torb_channel_take_first(&channel->receivers, TORB_OUTCOME_CLOSED);
    receiver->wait_slot = NULL;
    torb_channel_release(channel);
    torb_post(receiver);
  }
  torb_spin_unlock(&channel->lock);
}

void torb_channel_close(torb_channel *channel) {
  torb_spin_lock(&channel->lock);
  if (channel->closed != 0u) {
    torb_spin_unlock(&channel->lock);
    return;
  }
  channel->closed = 1u;
  while (channel->count > 0u) {
    torb_release_item(channel->item, torb_channel_slot(channel, 0u));
    channel->head = (channel->head + 1u) % channel->capacity;
    channel->count -= 1u;
  }
  while (channel->senders.first != NULL) {
    torb_task *sender = channel->senders.first;
    torb_release_item(channel->item, sender->wait_slot);
    sender->wait_slot = NULL;
    (void)torb_channel_take_first(&channel->senders, TORB_OUTCOME_CLOSED);
    torb_channel_release(channel);
    torb_post(sender);
  }
  while (channel->receivers.first != NULL) {
    torb_task *receiver = torb_channel_take_first(&channel->receivers, TORB_OUTCOME_CLOSED);
    receiver->wait_slot = NULL;
    torb_channel_release(channel);
    torb_post(receiver);
  }
  torb_spin_unlock(&channel->lock);
}

void torb_channel_drop(void *block) {
  torb_channel *channel = (torb_channel *)block;
  if (channel->senders.first != NULL || channel->receivers.first != NULL) {
    torb_internal_error("a channel was freed while a task waited on it");
  }
  while (channel->count > 0u) {
    torb_release_item(channel->item, torb_channel_slot(channel, 0u));
    channel->head = (channel->head + 1u) % channel->capacity;
    channel->count -= 1u;
  }
  if (channel->buffer != NULL) {
    torb_raw_free(channel->buffer, (size_t)channel->capacity * channel->item->size);
  }
}

void torb_channel_release(torb_channel *channel) {
  torb_release(channel, torb_channel_drop);
}

/* ------------------------------------------------------------------ the tasks `std/task` is written over --- */

/*
 * `Channel.source().next()` and `Channel.sink().add(item)` are TorbScript in `std/task`, over one task each that the
 * runtime writes: a machine can only wait for a task through `torb_task_await`, so the two channel waits are wrapped in
 * a task that waits for the channel instead. It costs one task block per item, and it keeps the lowering to one kind
 * of suspension point. Both are pinned to the worker of the task that asks, which is the one that reads the answer.
 */

typedef struct torb_received_frame {
  torb_channel *channel;
} torb_received_frame;

static torb_poll torb_received_resume(torb_task *task) {
  torb_received_frame *frame = (torb_received_frame *)torb_task_frame(task);
  torb_outcome outcome;
  /* A delivered item this machine did not take yet is released by the runtime when it stops. */
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_channel_receive(task, frame->channel, torb_task_result_slot(task)) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  outcome = torb_task_outcome(task);
  torb_channel_release(frame->channel);
  /* The end of the stream has no value: the task ends as cancelled, which the source reads as `None`. */
  return outcome == TORB_OUTCOME_READY ? TORB_POLL_FINISHED : TORB_POLL_STOPPED;
}

torb_task *torb_channel_received(torb_channel *channel) {
  torb_task *task = torb_task_new(torb_received_resume, sizeof(torb_received_frame), channel->item);
  torb_retain(channel);
  ((torb_received_frame *)torb_task_frame(task))->channel = channel;
  torb_task_start(task);
  return task;
}

typedef struct torb_offered_frame {
  torb_channel *channel;
} torb_offered_frame;

/* Where the item sits in the frame of an offer: after the channel, aligned for anything C aligns to 16 or less. */
#define TORB_OFFERED_ITEM_OFFSET ((sizeof(torb_offered_frame) + 15u) & ~(size_t)15u)

static torb_poll torb_offered_resume(torb_task *task) {
  torb_offered_frame *frame = (torb_offered_frame *)torb_task_frame(task);
  void *item = (uint8_t *)frame + TORB_OFFERED_ITEM_OFFSET;
  torb_outcome outcome;
  if (torb_task_cancelled(task)) {
    /* Before the send the item is still the frame's; after it, the send consumed it either way. */
    if (task->state == 0u) {
      torb_release_item(frame->channel->item, item);
    }
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_channel_send(task, frame->channel, item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  outcome = torb_task_outcome(task);
  torb_channel_release(frame->channel);
  if (outcome != TORB_OUTCOME_READY) {
    /* Nobody reads any more: the item was released, and the sink reads the stop as `ChannelClosed`. */
    return TORB_POLL_STOPPED;
  }
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

torb_task *torb_channel_offered(torb_channel *channel, const void *item) {
  torb_task *task = torb_task_new(torb_offered_resume, TORB_OFFERED_ITEM_OFFSET + (size_t)channel->item->size,
                                  &torb_element_void);
  torb_offered_frame *frame = (torb_offered_frame *)torb_task_frame(task);
  torb_retain(channel);
  frame->channel = channel;
  torb_move_item(channel->item, (uint8_t *)frame + TORB_OFFERED_ITEM_OFFSET, item);
  torb_task_start(task);
  return task;
}

/* The descriptor of a `Bool` as a task result: one byte, trivial. */
static const torb_element torb_element_flag = { (uint32_t)sizeof(bool), (uint32_t)TORB_ALIGN_OF(bool),
                                                NULL, NULL, NULL, NULL };

typedef struct torb_deadline_frame {
  torb_task *target;
  torb_instant deadline;
} torb_deadline_frame;

static torb_poll torb_deadline_resume(torb_task *task) {
  torb_deadline_frame *frame = (torb_deadline_frame *)torb_task_frame(task);
  torb_outcome outcome;
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->target);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_task_await_until(task, frame->target, frame->deadline) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  outcome = torb_task_outcome(task);
  if (outcome == TORB_OUTCOME_TIMED_OUT) {
    torb_task_cancel(frame->target);
  }
  *(bool *)torb_task_result_slot(task) = outcome != TORB_OUTCOME_TIMED_OUT;
  torb_task_release(frame->target);
  return TORB_POLL_FINISHED;
}

torb_task *torb_task_completed_within(torb_task *self, torb_duration limit) {
  torb_task *task = torb_task_new(torb_deadline_resume, sizeof(torb_deadline_frame), &torb_element_flag);
  torb_deadline_frame *frame = (torb_deadline_frame *)torb_task_frame(task);
  torb_retain(self);
  frame->target = self;
  frame->deadline = torb_deadline_after(limit);
  torb_task_start(task);
  return task;
}

/* ----------------------------------------------------------------------------------------------- the scheduler --- */

/* The timers of this worker that are due, in deadline order: the ones set first first among equal deadlines. */
static void torb_fire_timers(torb_worker *self) {
  torb_scheduler *scheduler = &self->scheduler;
  torb_instant now;
  if (scheduler->timer_count == 0u) {
    return;
  }
  now = torb_clock_now();
  while (scheduler->timer_count > 0u && scheduler->timers[0].deadline <= now) {
    torb_task *task = scheduler->timers[0].task;
    uint8_t waiting = torb_atomic_load_u8(&task->waiting);
    /* Queued first, removed second, so `busy` never reads zero in between */
    if (waiting == (uint8_t)TORB_WAITING_TIMER) {
      torb_atomic_store_u8(&task->waiting, (uint8_t)TORB_WAITING_NOTHING);
      task->outcome = (int32_t)TORB_OUTCOME_READY;
      torb_post(task);
    } else if (waiting == (uint8_t)TORB_WAITING_TASK) {
      if (torb_take_out(task, TORB_OUTCOME_TIMED_OUT, false)) {
        torb_post(task);
      }
    }
    /* Otherwise it was woken already and is queued; its timer goes all the same */
    torb_timer_remove(self, 0u);
  }
}

/* How long this worker may sleep: until its first timer, or without a limit (-1). */
static int64_t torb_sleep_span(torb_worker *self) {
  torb_scheduler *scheduler = &self->scheduler;
  int64_t span;
  if (scheduler->timer_count == 0u) {
    return -1;
  }
  span = scheduler->timers[0].deadline - torb_clock_now();
  return span < 0 ? 0 : span;
}

/*
 * Waits on the worker's condition until somebody queues a task here, a task that may move is queued anywhere, the first
 * timer is due - or, for the main thread, until nothing can happen any more or what it waits for completed (`until`,
 * or with `for_test` the last task of the test in progress), and for every other worker, until the pool stops. Each of
 * those is checked again under the worker's lock after `sleeping` is set, which is what makes the signal of whoever
 * changes one of them impossible to miss.
 */
static void torb_idle(torb_worker *self, torb_task *until, bool is_main, bool for_test) {
  int64_t span = torb_sleep_span(self);
  bool may_sleep;
  torb_mutex_lock(&self->lock);
  self->sleeping = 1u;
  (void)torb_atomic_add_u32(&torb_pool.sleeping, 1u);
  may_sleep = self->queue_first == NULL && self->wanted == 0u;
  /* What the thread would take if it were awake: the ring's steal lists, or the blocking pool's inbox */
  if (torb_is_blocking_worker(self)) {
    may_sleep = may_sleep && torb_atomic_read_u32(&torb_blocking.waiting) == 0u;
  } else {
    may_sleep = may_sleep && torb_atomic_read_u32(&torb_pool.stealable) == 0u;
  }
  if (is_main) {
    may_sleep = may_sleep && torb_atomic_load_i64(&torb_pool.busy) > torb_atomic_load_i64(&torb_pool.floor)
                && (until == NULL || torb_atomic_load_u8(&until->status) == (uint8_t)TORB_TASK_PENDING)
                && (!for_test || torb_atomic_load_i64(&torb_pool.test_live) != 0)
                && torb_atomic_load_u32(&torb_pool.exiting) == 0u;
  } else {
    may_sleep = may_sleep && torb_atomic_load_u32(&torb_pool.stopping) == 0u;
  }
  if (may_sleep) {
    (void)torb_condition_wait(&self->wake, &self->lock, span);
  }
  (void)torb_atomic_sub_u32(&torb_pool.sleeping, 1u);
  self->sleeping = 0u;
  self->wanted = 0u;
  torb_mutex_unlock(&self->lock);
}

/*
 * The loop of a worker. The main thread's runs until `until` completed, or - with `NULL` - until nothing is queued,
 * running or waiting on a timer anywhere; with `for_test`, until the last task of the test in progress completed or
 * nothing is left that could still happen. Every other worker's runs until the pool stops.
 */
static void torb_worker_loop(torb_worker *self, torb_task *until, bool is_main, bool for_test) {
  for (;;) {
    torb_task *task;
    if (until != NULL && torb_atomic_load_u8(&until->status) != (uint8_t)TORB_TASK_PENDING) {
      return;
    }
    if (for_test && torb_atomic_load_i64(&torb_pool.test_live) == 0) {
      return;
    }
    if (!is_main && torb_atomic_load_u32(&torb_pool.stopping) != 0u) {
      return;
    }
    /* A task on another worker called `Process.exit`: the main thread ends the program on its behalf */
    if (is_main && torb_atomic_load_u32(&torb_pool.exiting) != 0u) {
      /* Once: the exit runs this loop again to drain the pool, and must not find the request a second time */
      torb_atomic_store_u32(&torb_pool.exiting, 0u);
      torb_process_exit(torb_pool.exit_code);
    }
    torb_fire_timers(self);
    task = torb_take_local(self);
    /* The turn of a task to the blocking pool completed: it goes there instead of running here */
    if (task != NULL && task->hopping != 0u && torb_hand_to_blocking(self, task)) {
      continue;
    }
    if (task == NULL && torb_pool_threaded != 0u) {
      task = torb_is_blocking_worker(self) ? torb_take_blocking(self) : torb_steal(self);
    }
    if (task != NULL) {
      torb_run_one(self, task);
      continue;
    }
    if (is_main && torb_atomic_load_i64(&torb_pool.busy) <= torb_atomic_load_i64(&torb_pool.floor)) {
      /* A test's tasks that wait for what never comes are cancelled at the end of the program, as before tests waited */
      if (for_test || until == NULL || torb_atomic_load_u8(&until->status) != (uint8_t)TORB_TASK_PENDING) {
        return;
      }
      self->scheduler.running = false;
      torb_pool.until = NULL;
      torb_panic_text("deadlock: every task is waiting for another one, and nothing is left that could wake one",
                      torb_location_unknown);
    }
    torb_idle(self, until, is_main, for_test);
  }
}

void torb_scheduler_run(torb_task *until) {
  torb_worker *self = torb_worker_self();
  if (self != &torb_main_worker) {
    torb_internal_error("the scheduler was run from a worker thread");
  }
  if (self->scheduler.running) {
    torb_internal_error("the scheduler was run from inside a task");
  }
  self->scheduler.running = true;
  torb_pool.until = until;
  torb_worker_loop(self, until, true, false);
  torb_pool.until = NULL;
  self->scheduler.running = false;
}

static void torb_pool_stop(void);

void torb_scheduler_finish(void) {
  torb_worker *self = torb_worker_self();
  torb_task *task;
  /* Cancelling changes no live list, only completing does - and nothing completes while the tree lock is held. */
  torb_mutex_lock(&torb_tree);
  for (task = torb_pool.live_first; task != NULL; task = task->live_next) {
    torb_cancel_subtree(self, task);
  }
  torb_mutex_unlock(&torb_tree);
  torb_scheduler_run(NULL);
  if (torb_atomic_load_i64(&torb_pool.live) != 0) {
    torb_internal_error("a cancelled task did not stop");
  }
  torb_pool_stop();
  torb_raw_free(self->scheduler.timers, (size_t)self->scheduler.timer_capacity * sizeof(torb_timer));
  self->scheduler.timers = NULL;
  self->scheduler.timer_capacity = 0u;
  self->scheduler.timer_sequence = 0u;
}

/*
 * `Process.exit` from a task on another worker than the main thread. The program's `main` is inside
 * `torb_scheduler_run` on the main thread and only that thread may end it, so the exit is handed over: the running task
 * is completed as cancelled here, the main thread is told the code and woken, and this worker goes on running what is
 * queued on it - the cancelled tasks have to stop on the worker they belong to - until the pool stops. Then the thread
 * ends where it is, because there is no machine to return to; the frames above hold nothing counted, since the lowering
 * released everything in front of a call that answers `Never`.
 */
static TORB_NORETURN void torb_exit_from_worker(torb_worker *self, torb_task *current) {
  if (current != NULL) {
    self->scheduler.current = NULL;
    torb_complete(self, current, false);
    torb_busy_less();
  }
  torb_wake_worker(&torb_main_worker);
  torb_worker_loop(self, NULL, false, false);
  torb_raw_free(self->scheduler.timers, (size_t)self->scheduler.timer_capacity * sizeof(torb_timer));
  self->scheduler.timers = NULL;
  self->scheduler.timer_capacity = 0u;
  torb_thread_exit();
}

void torb_scheduler_exit(int64_t code) {
  torb_worker *self = torb_worker_self();
  torb_task *current = self->scheduler.current;
  torb_task *awaited = self == &torb_main_worker ? torb_pool.until : NULL;
  /* A test body inside a task of the main thread that waits for the test's tasks: that task never returns either */
  torb_task *host = self == &torb_main_worker && torb_pool.test_draining != 0u ? torb_pool.test_host : NULL;
  torb_task *task;
  if (torb_atomic_load_i64(&torb_pool.live) == 0 && awaited == NULL) {
    return;
  }
  /* The program ends from here on: a panic on the way is the process's, never a jump back into a test that is left */
  self->recovery = NULL;
  /* Told first, while this task still counts as running, so the main thread cannot see the pool go quiet before it */
  if (self != &torb_main_worker) {
    torb_mutex_lock(&torb_main_worker.lock);
    if (torb_pool.exit_claimed == 0u) {
      torb_pool.exit_claimed = 1u;
      torb_pool.exit_code = code;
      torb_atomic_store_u32(&torb_pool.exiting, 1u);
    }
    torb_mutex_unlock(&torb_main_worker.lock);
  }
  torb_mutex_lock(&torb_tree);
  for (task = torb_pool.live_first; task != NULL; task = task->live_next) {
    torb_cancel_subtree(self, task);
  }
  torb_mutex_unlock(&torb_tree);
  if (self != &torb_main_worker) {
    torb_exit_from_worker(self, current);
  }
  /*
   * The running task leaves through the exit and never returns to its machine, so it cannot take its own stop path.
   * The lowering released what its frame held in front of the call, as it does in front of every call that answers
   * `Never`, so completing it as cancelled is all that is left: its waiters were cancelled above and stop on their own.
   * It counted as running, and its run never ends, so it stops counting here.
   */
  if (current != NULL) {
    self->scheduler.current = NULL;
    torb_complete(self, current, false);
    torb_busy_less();
  }
  if (host != NULL && host != current) {
    torb_complete(self, host, false);
    torb_busy_less();
  }
  torb_pool.test_draining = 0u;
  (void)torb_atomic_add_i64(&torb_pool.floor, -torb_atomic_load_i64(&torb_pool.floor));
  self->scheduler.running = false;
  torb_pool.until = NULL;
  torb_scheduler_finish();
  if (awaited != NULL) {
    torb_task_release(awaited);
  }
}

int torb_task_end_main(torb_task *main_task) {
  const bool cancelled = torb_atomic_load_u8(&main_task->status) == (uint8_t)TORB_TASK_CANCELLED;
  torb_task_release(main_task);
  if (!cancelled) {
    return 0;
  }
  {
    /* As a panic writes its line: what the program printed first, then the line through the path `print` takes */
    static const char line[] = "cancelled: the program waited for a task that was cancelled";
    fflush(stdout);
    torb_write_line_error(line, sizeof line - 1u);
    fflush(stderr);
  }
  return TORB_EXIT_CANCELLED;
}

size_t torb_task_live_count(void) {
  return (size_t)torb_atomic_load_i64(&torb_pool.live);
}

torb_task *torb_task_current(void) {
  return torb_worker_current()->scheduler.current;
}

/* ------------------------------------------------------------------------------------------- the tasks of a test --- */

bool torb_test_tasks_begin(void) {
  torb_worker *self = torb_worker_current();
  if (self != &torb_main_worker) {
    return false;
  }
  (void)torb_worker_self();
  torb_mutex_lock(&torb_tree);
  if (torb_pool.test_current != 0u) {
    torb_mutex_unlock(&torb_tree);
    return false;
  }
  torb_pool.test_sequence += 1u;
  if (torb_pool.test_sequence == 0u) {
    torb_pool.test_sequence = 1u;
  }
  torb_pool.test_current = torb_pool.test_sequence;
  torb_pool.test_host = self->scheduler.current;
  torb_pool.test_failed = 0u;
  torb_mutex_unlock(&torb_tree);
  return true;
}

/*
 * The test's tasks run here, on the main thread and every other worker, until the last of them completed or nothing is
 * left that could still happen - a task that waits for what never comes then waits on, and is cancelled at the end of
 * the program as it always was. Where the body runs inside a task of the main thread (an entry file that waits), that
 * task counts as running for as long as this waits, so "nothing is left" is one thing that could still happen, not zero.
 */
bool torb_test_tasks_end(bool abandon, torb_recovery *failure) {
  torb_worker *self = torb_worker_current();
  bool failed;
  if (abandon) {
    torb_mutex_lock(&torb_tree);
    torb_cancel_test_locked(self, torb_pool.test_current);
    torb_mutex_unlock(&torb_tree);
  }
  if (torb_atomic_load_i64(&torb_pool.test_live) != 0) {
    torb_task *host = self->scheduler.current;
    bool running = self->scheduler.running;
    int64_t floor = host != NULL ? 1 : 0;
    (void)torb_atomic_add_i64(&torb_pool.floor, floor);
    torb_pool.test_draining = 1u;
    self->scheduler.running = true;
    torb_worker_loop(self, NULL, true, true);
    self->scheduler.running = running;
    self->scheduler.current = host;
    torb_pool.test_draining = 0u;
    (void)torb_atomic_add_i64(&torb_pool.floor, -floor);
  }
  torb_mutex_lock(&torb_tree);
  failed = torb_pool.test_failed != 0u;
  if (failed && failure != NULL) {
    snprintf(failure->message, sizeof failure->message, "%s", torb_pool.test_message);
    failure->at = torb_pool.test_at;
  }
  torb_pool.test_current = 0u;
  torb_pool.test_host = NULL;
  torb_pool.test_failed = 0u;
  (void)torb_atomic_add_i64(&torb_pool.test_live, -torb_atomic_load_i64(&torb_pool.test_live));
  torb_mutex_unlock(&torb_tree);
  return failed;
}

/* ------------------------------------------------------------------------------------------ the worker pool --- */

int64_t torb_workers_count(void) {
  if (torb_pool.configured == 0u) {
    const char *given = getenv("TORB_WORKERS");
    uint32_t count = 0u;
    if (given != NULL) {
      const char *digit = given;
      bool valid = *digit != '\0';
      for (; *digit != '\0'; digit += 1) {
        if (*digit < '0' || *digit > '9' || count > TORB_MAXIMUM_WORKERS) {
          valid = false;
          break;
        }
        count = count * 10u + (uint32_t)(*digit - '0');
      }
      if (!valid || count < 1u || count > TORB_MAXIMUM_WORKERS) {
        fflush(stdout);
        fprintf(stderr, "error: TORB_WORKERS must be a whole number from 1 to 1024, and it is \"%s\"\n", given);
        fflush(stderr);
        exit(2);
      }
    } else {
      count = torb_platform_processor_count();
      if (count > TORB_MAXIMUM_WORKERS) {
        count = TORB_MAXIMUM_WORKERS;
      }
    }
    torb_pool.configured = count;
  }
  return (int64_t)torb_pool.configured;
}

int64_t torb_workers_blocking(void) {
  if (torb_blocking.configured == 0u) {
    const char *given = getenv("TORB_BLOCKING");
    uint32_t count = TORB_BLOCKING_DEFAULT;
    if (given != NULL) {
      const char *digit = given;
      bool valid = *digit != '\0';
      count = 0u;
      for (; *digit != '\0'; digit += 1) {
        if (*digit < '0' || *digit > '9' || count > TORB_MAXIMUM_WORKERS) {
          valid = false;
          break;
        }
        count = count * 10u + (uint32_t)(*digit - '0');
      }
      if (!valid || count < 1u || count > TORB_MAXIMUM_WORKERS) {
        fflush(stdout);
        fprintf(stderr, "error: TORB_BLOCKING must be a whole number from 1 to 1024, and it is \"%s\"\n", given);
        fflush(stderr);
        exit(2);
      }
    }
    torb_blocking.configured = count;
  }
  return (int64_t)torb_blocking.configured;
}

uint32_t torb_worker_index(void) {
  return torb_worker_current()->index;
}

bool torb_worker_is_blocking(void) {
  return torb_is_blocking_worker(torb_worker_current());
}

void torb_pool_set_workers(uint32_t count) {
  if (torb_pool.started != 0u) {
    torb_internal_error("the number of workers was changed while the pool runs");
  }
  torb_pool.chosen = count > TORB_MAXIMUM_WORKERS ? TORB_MAXIMUM_WORKERS : count;
}

void torb_pool_set_blocking(uint32_t count) {
  if (torb_blocking.count != 0u) {
    torb_internal_error("the size of the blocking pool was changed while it runs");
  }
  torb_blocking.chosen = count > TORB_MAXIMUM_WORKERS ? TORB_MAXIMUM_WORKERS : count;
}

torb_pool_statistics torb_pool_statistics_now(void) {
  torb_pool_statistics now = torb_pool.totals;
  uint32_t index;
  now.resumed += torb_main_worker.ran;
  for (index = 1u; index < torb_pool.count; index += 1u) {
    now.resumed += torb_pool.workers[index]->ran;
    now.stolen += torb_pool.workers[index]->stole;
  }
  for (index = 0u; index < torb_blocking.count; index += 1u) {
    now.resumed += torb_blocking.workers[index]->ran;
  }
  now.stolen += torb_main_worker.stole;
  now.copied = (uint64_t)torb_atomic_load_i64(&torb_pool_copies);
  return now;
}

void torb_pool_count_copy(void) {
  (void)torb_atomic_add_i64(&torb_pool_copies, 1);
}

bool torb_task_copies(void) {
  uint32_t count = torb_pool.chosen != 0u ? torb_pool.chosen : (uint32_t)torb_workers_count();
  return count > 1u;
}

size_t torb_pool_sum_live_blocks(void) {
  size_t sum = torb_main_worker.heap.live_blocks;
  uint32_t index;
  for (index = 1u; index < torb_pool.count; index += 1u) {
    sum += torb_pool.workers[index]->heap.live_blocks;
  }
  for (index = 0u; index < torb_blocking.count; index += 1u) {
    sum += torb_blocking.workers[index]->heap.live_blocks;
  }
  return sum;
}

size_t torb_pool_sum_immortal_blocks(void) {
  size_t sum = torb_main_worker.heap.immortal_blocks;
  uint32_t index;
  for (index = 1u; index < torb_pool.count; index += 1u) {
    sum += torb_pool.workers[index]->heap.immortal_blocks;
  }
  for (index = 0u; index < torb_blocking.count; index += 1u) {
    sum += torb_blocking.workers[index]->heap.immortal_blocks;
  }
  return sum;
}

int64_t torb_pool_sum_machine_live_blocks(void) {
  int64_t sum = torb_main_worker.heap.machine_live_blocks;
  uint32_t index;
  for (index = 1u; index < torb_pool.count; index += 1u) {
    sum += torb_pool.workers[index]->heap.machine_live_blocks;
  }
  for (index = 0u; index < torb_blocking.count; index += 1u) {
    sum += torb_blocking.workers[index]->heap.machine_live_blocks;
  }
  return sum;
}

int64_t torb_pool_sum_machine_immortal_blocks(void) {
  int64_t sum = torb_main_worker.heap.machine_immortal_blocks;
  uint32_t index;
  for (index = 1u; index < torb_pool.count; index += 1u) {
    sum += torb_pool.workers[index]->heap.machine_immortal_blocks;
  }
  for (index = 0u; index < torb_blocking.count; index += 1u) {
    sum += torb_blocking.workers[index]->heap.machine_immortal_blocks;
  }
  return sum;
}

/* The thread of worker 1 to N-1: its pointer, the bottom of its stack, the loop, and its timer heap freed at the end. */
static void torb_worker_main(void *argument) {
  torb_worker *self = (torb_worker *)argument;
  char top = 0;
  torb_platform_set_worker(self);
  /* What a POSIX thread knows of its stack is the size it was made with; a quarter of a megabyte is left for what is
     above this frame. Windows reads the bottom out of the thread's TEB instead. */
  torb_set_thread_stack_floor((uintptr_t)&top - (TORB_WORKER_STACK_SIZE - (size_t)256u * 1024u));
  torb_worker_loop(self, NULL, false, false);
  torb_raw_free(self->scheduler.timers, (size_t)self->scheduler.timer_capacity * sizeof(torb_timer));
  self->scheduler.timers = NULL;
  self->scheduler.timer_capacity = 0u;
}

/*
 * Starts workers 1 to N-1, on the main thread and while it is still the only one: the flag that makes shared counts
 * atomic is set before the first thread exists, and everything the threads would otherwise race to set up first - the
 * console cache, the clock, the TLS slot - is set up here.
 */
static void torb_pool_start(void) {
  uint32_t count = torb_pool.chosen != 0u ? torb_pool.chosen : (uint32_t)torb_workers_count();
  torb_worker **workers;
  uint32_t index;
  torb_pool.started = 1u;
  if (count <= 1u) {
    return;
  }
  torb_console_prepare();
  torb_clock_prepare();
  (void)torb_worker_self();
  torb_platform_set_worker(&torb_main_worker);
  workers = (torb_worker **)calloc(count, sizeof(torb_worker *));
  if (workers == NULL) {
    torb_panic_out_of_memory((size_t)count * sizeof(torb_worker *));
  }
  workers[0] = &torb_main_worker;
  for (index = 1u; index < count; index += 1u) {
    torb_worker *worker = (torb_worker *)calloc(1u, sizeof(torb_worker));
    if (worker == NULL) {
      torb_panic_out_of_memory(sizeof(torb_worker));
    }
    torb_worker_prepare(worker, index);
    workers[index] = worker;
  }
  torb_pool.workers = workers;
  torb_pool.count = count;
  torb_pool_threaded = 1u;
  torb_pool.totals.starts += 1u;
  for (index = 1u; index < count; index += 1u) {
    if (!torb_thread_start(&workers[index]->thread, torb_worker_main, workers[index], TORB_WORKER_STACK_SIZE)) {
      torb_panic_text("the operating system refused to start a worker thread", torb_location_unknown);
    }
  }
}

/*
 * Stops the pool at the end of the program, once every task has completed: the threads are told, woken and joined, and
 * what each counted - its blocks, its resumes, its thefts - is folded into worker 0, so the report of memory.c still
 * sums to the exact number. After it the process has one thread again, and a later task starts the pool anew.
 */
/* A stopped thread's worker: what it counted folded into worker 0, and its struct freed. */
static void torb_fold_worker(torb_worker *worker) {
  torb_main_worker.heap.live_blocks += worker->heap.live_blocks;
  torb_main_worker.heap.immortal_blocks += worker->heap.immortal_blocks;
  torb_main_worker.heap.machine_live_blocks += worker->heap.machine_live_blocks;
  torb_main_worker.heap.machine_immortal_blocks += worker->heap.machine_immortal_blocks;
  torb_pool.totals.resumed += worker->ran;
  torb_pool.totals.stolen += worker->stole;
  torb_condition_destroy(&worker->wake);
  torb_mutex_destroy(&worker->lock);
  free(worker);
}

static void torb_pool_stop(void) {
  uint32_t index;
  uint32_t count = torb_pool.count;
  uint32_t blocking = torb_blocking.count;
  /* The IO core first: its threads wake tasks, and every task has completed by now, so what it still holds is closed */
  bool io = torb_io_stop();
  if (io && count <= 1u && blocking == 0u) {
    /* Its threads were the only other ones: the counts of shared blocks need not be atomic any more */
    torb_pool_threaded = 0u;
  }
  if (torb_pool.started == 0u && blocking == 0u) {
    return;
  }
  if (count > 1u || blocking > 0u) {
    torb_atomic_store_u32(&torb_pool.stopping, 1u);
    for (index = 1u; index < count; index += 1u) {
      torb_wake_worker(torb_pool.workers[index]);
    }
    for (index = 0u; index < blocking; index += 1u) {
      torb_wake_worker(torb_blocking.workers[index]);
    }
    for (index = 1u; index < count; index += 1u) {
      torb_thread_join(&torb_pool.workers[index]->thread);
    }
    for (index = 0u; index < blocking; index += 1u) {
      torb_thread_join(&torb_blocking.workers[index]->thread);
    }
    for (index = 1u; index < count; index += 1u) {
      torb_fold_worker(torb_pool.workers[index]);
    }
    for (index = 0u; index < blocking; index += 1u) {
      torb_fold_worker(torb_blocking.workers[index]);
    }
    if (count > 1u) {
      free(torb_pool.workers);
      torb_pool.workers = NULL;
      torb_pool.count = 1u;
    }
    if (blocking > 0u) {
      free(torb_blocking.workers);
      torb_blocking.workers = NULL;
      torb_atomic_store_u32(&torb_blocking.count, 0u);
    }
    torb_pool_threaded = 0u;
    torb_pool.stopping = 0u;
  }
  torb_pool.totals.resumed += torb_main_worker.ran;
  torb_pool.totals.stolen += torb_main_worker.stole;
  torb_main_worker.ran = 0u;
  torb_main_worker.stole = 0u;
  torb_pool.started = 0u;
}

/* -------------------------------------------------------------------------------- what may cross a worker --- */

bool torb_closure_may_move(torb_environment *environment) {
  /* Shared, or immortal - which has the bit too */
  return environment == NULL || (environment->header.count & TORB_SHARED_COUNT) != 0u;
}

/* Nobody else can change a count this reads: an immortal one never changes, and a count of 1 is this value's alone. */
static bool torb_block_may_move(const void *block, bool transfer) {
  const torb_header *header = (const torb_header *)block;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return true;
  }
  return transfer && header->count == 1u;
}

static bool torb_element_is_plain(const torb_element *element) {
  return element->retain == NULL && element->release == NULL;
}

bool torb_text_may_move(torb_text text, bool transfer) {
  return torb_block_may_move(text.storage, transfer);
}

bool torb_list_may_move(torb_list list, bool transfer) {
  if (list.storage == NULL || list.storage->header.count == TORB_IMMORTAL_COUNT) {
    return true;
  }
  return transfer && list.storage->header.count == 1u && torb_element_is_plain(list.storage->element);
}

bool torb_map_may_move(torb_map map, bool transfer) {
  if (map.storage == NULL || map.storage->header.count == TORB_IMMORTAL_COUNT) {
    return true;
  }
  return transfer && map.storage->header.count == 1u && torb_element_is_plain(map.storage->key)
         && torb_element_is_plain(map.storage->value);
}
