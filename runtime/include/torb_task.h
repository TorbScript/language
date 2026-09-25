/*
 * torb_task.h - tasks, the scheduler, timers and channels: the runtime half of milestone 7.3 (docs/design/CONCURRENCY.md,
 * docs/BACKEND.md 5.3). Included from torb.h; nothing includes it directly.
 *
 * # What a task is
 *
 * A `Task<Value>` is **one counted heap block** (`TORB_BLOCK_TASK`) holding, in this order:
 *
 *     torb_task     the runtime's part: the resume function, the state index, the cancellation flag, the waiter list,
 *                   the parent link, and where the task waits
 *     frame         the state machine's locals, `frame_size` bytes, zeroed at creation; the compiler's `T_frame`
 *     result        one `Value`, laid out by the task's element descriptor; written once, when the task finishes
 *
 * The block's count is **the handles plus one reference the scheduler holds while the task has not completed**. So
 * releasing the last handle of a running task releases nothing the task holds (docs/design/CONCURRENCY.md section 8,
 * "Dropping a `Task` still does not cancel it"), and a completed task's block lives as long as somebody can still read
 * its result. The frame's bytes stay in the block until then, but nothing in them is live once the task completed.
 *
 * # How an `async` function is lowered to it
 *
 * A function whose result is `Task<Value>`, and a closure passed to `spawn`, becomes a **resume function**:
 *
 *     static torb_poll f_fetch_resume(torb_task *task) {
 *       T_fetch_frame *frame = (T_fetch_frame *)torb_task_frame(task);
 *       switch (task->state) {
 *         case 0: goto state_0;
 *         case 1: goto state_1;
 *       }
 *       TORB_UNREACHABLE();
 *     state_0:
 *       if (torb_task_cancelled(task)) goto stop_0;           // every resume starts with the check
 *       frame->child = f_compute(frame->x);                    // an ordinary call that answers a Task: a handle
 *       task->state = 1;                                       // x = child.await()
 *       if (torb_task_await(task, frame->child) == TORB_WAIT_SUSPENDED) return TORB_POLL_SUSPENDED;
 *     state_1:
 *       if (torb_task_cancelled(task)) goto stop_1;           // also where `child` ended cancelled: the cascade
 *       (void)torb_task_outcome(task);                         // after every wait, READY or resumed
 *       (void)torb_task_result(frame->child, &frame->value);   // finished: a cancelled child stopped us above
 *       torb_task_release(frame->child);
 *       ...
 *       *(int64_t *)torb_task_result_slot(task) = frame->sum;  // the value moves into the result slot
 *       return TORB_POLL_FINISHED;
 *     stop_1:
 *       torb_task_release(frame->child);                       // release exactly what is live at state 1
 *     stop_0:
 *       return TORB_POLL_STOPPED;
 *     }
 *
 * and the function the program calls is the constructor of that task:
 *
 *     torb_task *f_fetch(int64_t x) {
 *       torb_task *task = torb_task_new(f_fetch_resume, sizeof(T_fetch_frame), &d_Int64);
 *       ((T_fetch_frame *)torb_task_frame(task))->x = x;       // parameters and captures move into the frame
 *       torb_task_start(task);                                  // enqueued: it runs once the caller suspends
 *       return task;                                            // the handle, owned by the caller
 *     }
 *
 * The rules the example follows are the whole contract:
 *
 * 1. **`task->state` is the one field the machine writes.** 0 is the entry. It is set before every suspension point
 *    to the state whose label follows it, and the switch at the top jumps there when the runtime resumes the task.
 * 2. **Every resume, and every point after a wait that answered `TORB_WAIT_READY`, starts with the cancellation check**
 *    `torb_task_cancelled(task)`, and so does every loop back-edge of the machine (docs/design/CONCURRENCY.md section 8). Where
 *    it is set, the machine releases what is live at that point - exactly as at a `return` - and answers
 *    `TORB_POLL_STOPPED`. The runtime releases nothing of the frame itself: it cannot know which slots are live.
 * 3. **After every wait the machine calls `torb_task_outcome` once**, before it reads anything the wait delivered. That
 *    is what hands a received channel item over to the frame; an item the machine never took (because it stopped
 *    first) is released by the runtime.
 * 4. **A suspension primitive answers `TORB_WAIT_SUSPENDED` or `TORB_WAIT_READY`.** Suspended: the task is registered
 *    where it waits, and the machine returns `TORB_POLL_SUSPENDED` at once. Ready: nothing was registered, the answer
 *    is already there, and the machine goes on at the label of the state it just set.
 * 5. **`TORB_POLL_FINISHED`: the value is in the result slot and nothing in the frame is live.**
 *    **`TORB_POLL_STOPPED`: nothing in the frame is live and there is no value**; every task that `await()`s it is
 *    cancelled in turn, and every `result()` of it answers `Fail(Cancelled)`. A machine may also stop without the flag,
 *    to end itself as cancelled - which is how `Task.all` and `both` end once they observed a cancellation.
 * 6. `torb_enter_frame`/`torb_leave_frame` stay balanced **per resume**: a resume function that enters leaves before
 *    every return, suspended or not, because the C stack is gone after it.
 * 7. **`await()` passes a cancellation on, `result()` observes it** (docs/design/CONCURRENCY.md section 8). A machine
 *    waits for a task through `torb_task_await` for `await()`: where the awaited task ends cancelled - before the wait
 *    or while the machine waits - the runtime sets the waiter's own cancellation flag, so the check that follows the
 *    wait stops the machine there, through the same stop path a `cancel()` reaches, and the waiter ends cancelled
 *    itself: the cascade. `torb_task_observe` is the wait of `result()`, which leaves the waiter alone and lets it read
 *    `torb_task_result` answering false.
 *
 * # How `Result` is built
 *
 * **The runtime never builds a `Result`, a `Cancelled` or a `TimedOut`**: all three are layouts of the program (BACKEND
 * 5.R1), and the runtime has no layout of the program in it. Reading a completed task follows the `.Fallible`
 * convention of every other native: `torb_task_result` answers `bool` and writes the value through an out parameter,
 * and the lowering builds `Ok(value)` (variant 0) or `Fail(Cancelled())` (variant 1, no fields) in its own layout of
 * `Result<Value, Cancelled>` around it - which is what `result()` answers, and what `await()` unwraps after a wait that
 * could only have ended with a value. The one native that has to put a `Result` *into* a task, `Task.within`, is
 * handed a `torb_within_shape` - two adapters the lowering emits per instance, the way it emits a `torb_element` per
 * type - and calls them.
 *
 * # The scheduler
 *
 * A fixed pool of workers, each an operating-system thread with its own FIFO run queue, timers and heap
 * (docs/design/CONCURRENCY.md sections 1 and 16). A task that is started or woken goes to the back of the queue of the
 * worker it belongs to; a timer that is due is woken before the next task is taken, in deadline order. When a worker
 * has nothing to run it takes an **unstarted** task that may move from another worker's queue (stealing), and
 * otherwise sleeps until it is woken or its first deadline comes. A task that has started never leaves its worker.
 *
 * **Which tasks may move** is decided where they are started: `torb_task_start` pins the task to the worker that
 * starts it, `torb_task_start_portable` lets an idle worker take it until it first runs. The compiler calls the second
 * one only where every value the frame holds may cross (task.c, "What crosses a worker"), so a program that starts no
 * such task never starts a second thread at all, and one worker behaves exactly as the single-threaded scheduler did:
 * `TORB_WORKERS=1` is the order the conformance suite pins.
 *
 * When nothing is queued anywhere, no timer is pending and the task being waited for has not completed, every task is
 * waiting for another one: that is a deadlock, and it panics rather than hangs.
 *
 * The generated `main` of a program that uses tasks runs its entry file as the **main task**:
 *
 *     torb_process_start(argc, argv);
 *     torb_task *main_task = torb_task_new(entry_resume, sizeof(T_entry_frame), &torb_element_void);
 *     torb_task_start(main_task);
 *     torb_scheduler_run(main_task);
 *     ended = torb_task_end_main(main_task);   // 0, or TORB_EXIT_CANCELLED where the main task ended cancelled
 *     torb_scheduler_finish();                 // every task still running is cancelled and runs to its stop
 *     torb_process_finish();
 *     return ended;
 */

#ifndef TORB_TASK_H
#define TORB_TASK_H

/* --------------------------------------------------------------------------------------------- the task block --- */

typedef struct torb_task torb_task;
typedef struct torb_channel torb_channel;

/** What a resume function answers. */
typedef enum torb_poll {
  /** Registered where it waits (or queued again by `torb_task_pause`); `state` says where it goes on. */
  TORB_POLL_SUSPENDED = 0,
  /** The value is in the result slot; nothing in the frame is live. */
  TORB_POLL_FINISHED = 1,
  /** Stopped at a cancellation check, or ended itself as cancelled; nothing in the frame is live and there is no value. */
  TORB_POLL_STOPPED = 2
} torb_poll;

/** The state machine. `task` borrowed. Called by the scheduler only, never by the program. */
typedef torb_poll (*torb_resume_function)(torb_task *task);

/** What a suspension primitive answers. */
typedef enum torb_wait {
  /** The task is registered where it waits: return `TORB_POLL_SUSPENDED` now. */
  TORB_WAIT_SUSPENDED = 0,
  /** Nothing was registered and the answer is already there: go on, starting with the cancellation check. */
  TORB_WAIT_READY = 1
} torb_wait;

/** What the last wait of a task answered, read with `torb_task_outcome`. */
typedef enum torb_outcome {
  TORB_OUTCOME_NONE = 0,
  /**
   * `await`: the awaited task completed - finished or cancelled, which `torb_task_result` tells apart. `send`: the item
   * was taken. `receive`: an item is in the slot, owned by the frame now. `sleep`, `pause`: the time is up.
   */
  TORB_OUTCOME_READY = 1,
  /** `send`: the reading end is closed, or the writing end already ended; the item was released. `receive`: the stream
   * ended, which is `None`. */
  TORB_OUTCOME_CLOSED = 2,
  /** `torb_task_await_until`: the deadline passed first. The awaited task is untouched. */
  TORB_OUTCOME_TIMED_OUT = 3,
  /** The task was woken because it was cancelled. A machine never acts on it: its cancellation check comes first. */
  TORB_OUTCOME_CANCELLED = 4
} torb_outcome;

typedef enum torb_task_status {
  TORB_TASK_PENDING = 0,
  TORB_TASK_FINISHED = 1,
  TORB_TASK_CANCELLED = 2
} torb_task_status;

/** Where a task waits. At most one of these at a time, plus at most one timer. */
typedef enum torb_task_waiting {
  TORB_WAITING_NOTHING = 0,
  TORB_WAITING_TASK = 1,
  TORB_WAITING_TIMER = 2,
  TORB_WAITING_SEND = 3,
  TORB_WAITING_RECEIVE = 4,
  /** An operation of the IO core (runtime/io.c): a socket, a name resolution. */
  TORB_WAITING_IO = 5
} torb_task_waiting;

/** An intrusive list of tasks through `wait_previous`/`wait_next`: the waiters of a task, the senders of a channel. */
typedef struct torb_task_list {
  torb_task *first;
  torb_task *last;
} torb_task_list;

/**
 * The runtime's part of a task block. **The machine reads `cancelled` (through `torb_task_cancelled`) and writes
 * `state`; every other field is the runtime's**, and is here only because the accessors below are `static inline`.
 *
 * Who may touch what, with a pool of workers: `state`, `outcome`, `timer` and the frame belong to the worker that runs
 * the task (`worker`); `waiters` and the step of `status` to complete belong to the task's own `lock`; the links of a
 * task that waits on something (`waiting`, `wait_*`) belong to the lock of what it waits on, except `wait_target`,
 * which only the worker that runs the task writes and a waker leaves as it is; the queue links and
 * `queued` belong to the lock of the worker whose queue it is in; the parent, child and live links belong to the tree
 * lock of task.c. `cancelled` and `status` are read without a lock, with an acquire.
 */
struct torb_task {
  torb_header header;
  torb_resume_function resume;
  /** The descriptor of `Value`: its size and alignment place the result slot, its `release` drops a result nobody read. */
  const torb_element *result;
  /** The state the machine goes on at. 0 is the entry. */
  uint32_t state;
  /** The byte offset of the result slot from the start of the block. */
  uint32_t result_offset;
  /** The cancellation flag: set by `torb_task_cancel`, read at every suspension point and loop back-edge. */
  uint8_t cancelled;
  uint8_t status;          /**< `torb_task_status`. */
  uint8_t waiting;         /**< `torb_task_waiting`. */
  uint8_t queued;          /**< In the run queue of its worker. */
  /** Started with `torb_task_start_portable`: an idle worker may take it until it first runs. */
  uint8_t portable;
  /** In the list of its worker's queue that an idle worker takes from (unstarted and portable). */
  uint8_t stealable;
  /**
   * A worker took it from a queue to run it, so it runs or has run at least once: it never moves again - except once,
   * to the blocking pool, where it asked for that with a turn and its frame may move (`hopping`, task.c "The blocking
   * pool"). Set under the lock of that queue.
   */
  uint8_t started;
  /** Awaits a turn to the blocking pool: its worker hands it to the pool instead of running it. Its own thread's. */
  uint8_t hopping;
  /** Its worker is completing it: a cancellation passes it by (set and read under the tree lock). */
  uint8_t completing;
  /**
   * Its last wait for a task observes a cancellation instead of taking it over (`torb_task_observe`,
   * `torb_task_await_until`). Written by the waiter before it joins a waiter list, read under the awaited task's lock.
   */
  uint8_t observing;
  int32_t outcome;         /**< `torb_outcome` of the last wait. */
  int32_t timer;           /**< The index in its worker's timer heap, or -1. */
  /** The worker that runs it: the one that started it, or the one that took it before its first run. */
  uint32_t worker;
  /** The lock of `status` and `waiters`: a spin lock, held for a few instructions. */
  uint32_t lock;
  /**
   * The test this task belongs to, 0 for none: the one in progress on the main thread where the task was made outside
   * any of that test's tasks, or its parent's (task.c, "A panic in a task of a test"). Written once, at creation.
   */
  uint32_t test;
  torb_task *queue_previous;
  torb_task *queue_next;
  torb_task *steal_previous;
  torb_task *steal_next;
  /**
   * What it waits on (a `torb_task`, a `torb_channel` or a `torb_io_waiting`), left in place by a waker, and the slot
   * of the frame a send offers or a receive fills.
   */
  void *wait_target;
  void *wait_slot;
  torb_task *wait_previous;
  torb_task *wait_next;
  /**
   * A received item that the frame has not taken yet (`torb_task_outcome` takes it), or the item of a send that was
   * cancelled while it waited, and what it is: released by the runtime when the task completes without taking it.
   */
  void *delivered;
  const torb_element *delivered_element;
  /** The tasks waiting for this one. */
  torb_task_list waiters;
  /** Structured cancellation: the task whose `spawn` made this one, and the ones this one made. */
  torb_task *parent;
  torb_task *children_first;
  torb_task *children_last;
  torb_task *sibling_previous;
  torb_task *sibling_next;
  /** Every task that has not completed, for `torb_scheduler_finish`. */
  torb_task *live_previous;
  torb_task *live_next;
};

/** Where the frame begins: the runtime's part, rounded up so that a frame may hold anything C aligns to 16 or less. */
#define TORB_TASK_FRAME_OFFSET ((sizeof(torb_task) + 15u) & ~(size_t)15u)

/** The machine's locals. `task` borrowed; the pointer is valid as long as the block is. */
static inline void *torb_task_frame(torb_task *task) {
  return (void *)((uint8_t *)task + TORB_TASK_FRAME_OFFSET);
}

/** Where the value goes: the machine moves it here before it answers `TORB_POLL_FINISHED`. */
static inline void *torb_task_result_slot(torb_task *task) {
  return (void *)((uint8_t *)task + task->result_offset);
}

/**
 * The cancellation check: one load and one branch, at every resume, after every ready wait, at every back-edge. The flag
 * may be set by another worker while the machine runs, so the load is an atomic one - a plain load on every target.
 */
#if defined(__GNUC__) || defined(__clang__)
static inline bool torb_task_cancelled(const torb_task *task) {
  return __atomic_load_n(&task->cancelled, __ATOMIC_RELAXED) != 0u;
}
#else
static inline bool torb_task_cancelled(const torb_task *task) {
  return *(const volatile uint8_t *)&task->cancelled != 0u;
}
#endif

/* ------------------------------------------------------------------------------------- making and holding one --- */

/**
 * A new task, not started: count 1, which is the handle the caller owns. The frame is `frame_size` zero bytes; the
 * caller moves the parameters and the captures into it and then calls `torb_task_start` - always, before anything else
 * can run, because a task that was never started cannot be released (its frame holds values only its machine knows).
 *
 * The **parent** is the task running now (none outside a task), and a task made by a cancelled parent is born
 * cancelled, so its first resume stops it before its first line (docs/design/CONCURRENCY.md section 8). `result` must be
 * static data. Panics where the block would not fit in 4 GiB.
 */
torb_task *torb_task_new(torb_resume_function resume, size_t frame_size, const torb_element *result);

/**
 * `task` borrowed. Queues it on the worker that runs the caller, **pinned** there; the scheduler takes its own reference
 * until the task completes.
 */
void torb_task_start(torb_task *task);

/**
 * The same, and until it first runs an idle worker may take the task and run it instead - which is how `spawn` and a
 * call of a task function spread over the workers. The caller vouches that **every value in the frame may cross**:
 * plain data, a `Task` or `Channel` of plain data, a closure whose environment is shared (`torb_share`), or a block that
 * nothing but the frame holds (`torb_text_may_move`, `torb_list_may_move`, `torb_map_may_move`). The compiler writes
 * the test beside the call (task.c, "What crosses a worker"). Starts the pool's threads the first time it is called,
 * where there is more than one worker.
 */
void torb_task_start_portable(torb_task *task);

/**
 * The drop of a task block, for `torb_release(task, torb_task_drop)`: releases the result if the task finished. The
 * frame holds nothing live by then - a completed machine released it, and a pending task is never dropped because the
 * scheduler holds it.
 */
void torb_task_drop(void *block);

/** `task` consumed: `Release` for a `Task` slot. `NULL` is a no-op. */
void torb_task_release(torb_task *task);

/* ------------------------------------------------------------------------------------ suspension primitives --- */

/**
 * `awaited.await()`, the suspension half. Both borrowed. Ready where `awaited` has already completed; otherwise `self`
 * joins the waiters of `awaited` and is woken, `TORB_OUTCOME_READY`, when it completes either way. Read the answer with
 * `torb_task_result(awaited, ...)`, so the frame keeps its handle of `awaited` across the suspension. A task awaiting
 * itself panics, because nothing could ever wake it.
 *
 * **Where `awaited` ends cancelled, `self` is cancelled too**: its cancellation flag is set before it goes on - at once
 * where `awaited` had already ended cancelled, and when it is woken otherwise - so the check that follows the wait
 * stops it there, and it ends cancelled in turn (docs/design/CONCURRENCY.md section 8, "The cascade"). Only the flag is
 * set here: the children of `self` are cancelled when it stops, exactly as for any task that stops.
 */
torb_wait torb_task_await(torb_task *self, torb_task *awaited);

/**
 * `awaited.result()`, the suspension half: `torb_task_await` without the cascade. Where `awaited` ends cancelled `self`
 * goes on, and `torb_task_result` answers false - the one way a waiter observes a cancellation as a value.
 */
torb_wait torb_task_observe(torb_task *self, torb_task *awaited);

/**
 * `torb_task_observe` with a deadline on the monotonic clock (`torb_clock_now`): whichever comes first wakes `self`, and
 * the outcome is `TORB_OUTCOME_READY` or `TORB_OUTCOME_TIMED_OUT`. A timeout does nothing to `awaited`, and neither does
 * a cancellation of `awaited` do anything to `self`. Ready at once when
 * `awaited` has completed (`READY`) or the deadline has passed (`TIMED_OUT`). It is what `within` is made of.
 */
torb_wait torb_task_await_until(torb_task *self, torb_task *awaited, torb_instant deadline);

/**
 * The answer of a completed task: true where it finished, with a copy of its value in `*out` (owned - retained
 * through the descriptor, because a `Task` may be awaited by any number of tasks); false where it was cancelled, which
 * the lowering turns into `Fail(Cancelled())`. After `torb_task_await` it is always true, because a waiter whose task
 * ended cancelled stopped at its check. `task` borrowed. Panics where the task has not completed.
 */
bool torb_task_result(torb_task *task, void *out);

/** Whether the task has completed, finished or cancelled. `task` borrowed. */
bool torb_task_is_complete(const torb_task *task);

/**
 * What the last wait of `self` answered. Call it once after every wait, before reading what it delivered: a received
 * item is the frame's from this call on, and until it the runtime releases it if the task stops.
 */
torb_outcome torb_task_outcome(torb_task *self);

/**
 * `pause().await()` without the task in between: `self` goes to the back of the run queue. Always suspends; the
 * outcome is `TORB_OUTCOME_READY`. This is the one-state-split lowering docs/design/CONCURRENCY.md section 9 describes.
 */
torb_wait torb_task_pause(torb_task *self);

/** `self` sleeps until the deadline. Ready at once where it has passed. The outcome is `TORB_OUTCOME_READY`. */
torb_wait torb_task_sleep_until(torb_task *self, torb_instant deadline);

/* ------------------------------------------------------------------------------------------------- the natives --- */

/**
 * `sleep(seconds: Float64): Task<Void>`. A task that finishes once the time is up, counted from this call. A negative
 * or `nan` number of seconds is zero (it still finishes only when the scheduler gets to it); a number too big for the
 * clock is the end of the clock. Result owned.
 */
torb_task *torb_sleep(double seconds);

/** `pause(): Task<Void>`: a task that finishes the first time it runs, so awaiting it lets every queued task run. Owned. */
torb_task *torb_pause(void);

/**
 * `Task.cancel()`. `self` borrowed. A request and not a kill; asking twice, or asking a completed task, changes nothing.
 *
 * The flag is set on the task and on every task below it (its children, theirs, ...). A task that waits is taken out
 * of where it waits - the waiter list, the channel, the timer heap; an item it offered to a channel is released - and
 * queued, so it stops the next time the worker reaches it. A queued or running task stops at its next check.
 */
void torb_task_cancel(torb_task *self);

/**
 * What `within` needs of the program: the descriptor of `Result<Value, TimedOut>` and the two ways to build one in the
 * program's layout. The lowering emits one per instance, as static data.
 */
typedef struct torb_within_shape {
  const torb_element *result;
  /** Writes `Ok(value)` at `result`. `value` consumed: the adapter moves it in. */
  void (*finished)(void *result, void *value);
  /** Writes `Fail(TimedOut(limit))` at `result`. */
  void (*timed_out)(void *result, torb_duration limit);
} torb_within_shape;

/**
 * `Task.within(limit): Task<Result<Value, TimedOut>>`. `self` borrowed, `shape` static data, result owned. The limit
 * counts from this call. The new task (a child of the caller, like any other) races `self` against the limit:
 *
 *     self finished first            Ok(value), through `shape->finished`
 *     the limit passed first         `self` is cancelled, and Fail(TimedOut(limit)) through `shape->timed_out`
 *     self was cancelled by another  the new task stops too, so its `await()` answers Fail(Cancelled)
 *
 * Cancelling the new task does not cancel `self`: the deadline is gone, the work is not.
 *
 * `std/task` does not call this: its `Task.within` is TorbScript over `torb_task_completed_within` below, which builds
 * no `Result` and so needs no shape. This one stays for a caller that has the adapters at hand.
 */
torb_task *torb_task_within(torb_task *self, torb_duration limit, const torb_within_shape *shape);

/** The descriptor of `Void` as a task result: one byte, which a finished `Task<Void>` holds as 0. */
extern const torb_element torb_element_void;

/* --------------------------------------------------------------------------------------------------- channels --- */

/**
 * `Channel<Item>(capacity:)`: a counted block (`TORB_BLOCK_CHANNEL`), count 1, owned. `capacity` 0 hands every item
 * over directly; a negative one panics at `at`. `item` must be static data.
 *
 * The `Source` and the `Sink` of `std/task` are shared types of the program over these five calls - they are
 * trait-typed values with witness tables, which the runtime cannot build.
 */
torb_channel *torb_channel_new(int64_t capacity, const torb_element *item, torb_location at);

/**
 * `sink.add(item)`, the suspension half. `self` and `channel` borrowed; **`item` consumed** - the slot is dead after
 * the call whatever happens, and while `self` waits, the runtime keeps the item in that slot of the frame (which is why
 * it must be a slot of the frame and not a C local). Ready, `TORB_OUTCOME_READY`, where a waiting receiver took it or
 * the buffer had room; ready, `TORB_OUTCOME_CLOSED`, where the reading end is closed or the writing end already ended,
 * and then the item was released. Otherwise `self` waits until a receiver takes it (READY) or the reading end is
 * closed (CLOSED): `add` finishes when the reader has taken the item (docs/design/STREAMS.md section 3).
 */
torb_wait torb_channel_send(torb_task *self, torb_channel *channel, void *item);

/**
 * `source.next()`, the suspension half. `self` and `channel` borrowed; `out` a slot of the frame. Ready,
 * `TORB_OUTCOME_READY`, with an item in `*out` where one was buffered or a sender waited; ready, `TORB_OUTCOME_CLOSED`
 * (`None`), where the writing end ended and nothing is left, or the reading end was closed. Otherwise `self` waits.
 * The item is the frame's once `torb_task_outcome` has been called.
 */
torb_wait torb_channel_receive(torb_task *self, torb_channel *channel, void *out);

/**
 * `sink.end()`, and the sink's `close()`: the writing end is done. Items already offered are still delivered, then
 * every receive answers `None`; a later send is `TORB_OUTCOME_CLOSED`. Idempotent. `channel` borrowed.
 */
void torb_channel_end(torb_channel *channel);

/**
 * `source.close()`: the reading end is gone. Every buffered item is released, every waiting sender fails with
 * `TORB_OUTCOME_CLOSED` (its item released), and so does every later send - which is how a producer learns that
 * nobody wants its items (`ChannelClosed`). Idempotent. `channel` borrowed.
 */
void torb_channel_close(torb_channel *channel);

/** The drop of a channel block: releases what is still buffered. No task waits on a channel whose count reached 0. */
void torb_channel_drop(void *block);

/** `channel` consumed: `Release` for a `Channel` slot. */
void torb_channel_release(torb_channel *channel);

/* ------------------------------------------------------------------ the tasks `std/task` is written over --- */

/*
 * A resume function of the program waits through `torb_task_await` alone, so the three other waits `std/task` needs
 * are each a task the runtime writes, which the TorbScript side awaits like any other.
 */

/**
 * The next item of the channel, as a task: it finishes with the item, and ends as cancelled where the stream ended - no
 * item will come - or where it was cancelled. `Channel.source().next()` reads the second as `None`. `channel` borrowed
 * (the task holds its own reference), result owned.
 */
torb_task *torb_channel_received(torb_channel *channel);

/**
 * `item` offered to the channel, as a task: it finishes once a reader took the item, and ends as cancelled where the
 * reading end is closed, the writing end already ended, or it was cancelled - the item released in each of those.
 * `Channel.sink().add(item)` reads the stop as `ChannelClosed`. `channel` borrowed; **`item` consumed**, moved into the
 * task's frame by address. Result owned.
 */
torb_task *torb_channel_offered(torb_channel *channel, const void *item);

/**
 * Whether `self` completes within `limit`, as a task of a `Bool`: `true` once `self` finished or was cancelled, and
 * `false` once the limit passed first - after which `self` is cancelled. What `Task.within` is written over; cancelling
 * this task does not cancel `self`. `self` borrowed, result owned.
 */
torb_task *torb_task_completed_within(torb_task *self, torb_duration limit);

/* ----------------------------------------------------------------------------------------------- the scheduler --- */

/**
 * Runs the worker loop until `until` has completed, or, with `NULL`, until nothing is queued and no timer is pending.
 * `until` borrowed. Panics on a deadlock (nothing queued, no timer, `until` pending), and when called from inside a
 * task, because the loop is not reentrant.
 */
void torb_scheduler_run(torb_task *until);

/**
 * The end of the program: every task that has not completed is cancelled and run to its stop, which releases every
 * frame - and runs the `close()` of every value in one - so the live-block count stays exact. Then the scheduler's own
 * buffers are freed. Calling it with nothing left is a no-op.
 */
void torb_scheduler_finish(void);

/**
 * `Process.exit` while tasks are alive, maybe from inside one: every task is cancelled and run to its stop, the running
 * one is completed as cancelled without returning to it, and the main task the program's `main` waits for is
 * released - so the leak report of the exit sees what the end of `main` would have seen. A no-op without tasks. Called
 * on a worker other than the main thread it hands the exit and its `code` to the main thread, which is the one inside
 * the program's `main`, and never returns: that worker keeps running its own tasks to their stop until the pool stops,
 * and then its thread ends.
 */
void torb_scheduler_exit(int64_t code);

/**
 * What a program exits with where its main task ended cancelled: 128 plus `SIGINT`, the code a shell reports for a
 * program somebody stopped, and never the 101 of a panic - a cancellation is a request that was honoured, not a bug.
 */
#define TORB_EXIT_CANCELLED 130

/**
 * The end of the main task, in the generated `main` right after `torb_scheduler_run(main_task)`: releases the handle
 * and answers the exit code the program ends with - 0 where the main task finished, and `TORB_EXIT_CANCELLED` where it
 * ended cancelled, which only a top-level `await()` of a cancelled task does, and then this line went to standard
 * error first:
 *
 *     cancelled: the program waited for a task that was cancelled
 *
 * The program still ends the ordinary way after it: `torb_scheduler_finish` stops the other tasks, every frame's
 * `close()` runs and the leak report stays exact. `main_task` consumed.
 */
int torb_task_end_main(torb_task *main_task);

/** How many tasks have not completed yet, on every worker. For the tests and the leak report. */
size_t torb_task_live_count(void);

/** The task whose machine is running now on the calling thread, or `NULL` outside every task. Borrowed. */
torb_task *torb_task_current(void);

/* ------------------------------------------------------------------------------------------- the tasks of a test --- */

/*
 * A test owns the tasks it starts (task.c, "A panic in a task of a test"): `test.c` opens a scope around one test body,
 * every task made in it - and every task one of those makes - belongs to it, and closing the scope waits until they
 * have completed. A task of the test that panics, on whichever worker, is completed as cancelled, the other tasks of the
 * test are cancelled, and the first such panic is the test's failure. Only the main thread opens a scope.
 */

/** Opens the scope of a test on the main thread. False where one is open already or this is another thread. */
bool torb_test_tasks_begin(void);

/**
 * Closes the scope `torb_test_tasks_begin` opened. With `abandon` (the body itself panicked) every task of the test is
 * cancelled first. Then the calling thread runs tasks until every task of the test has completed or nothing that could
 * still happen is left. Answers true where a task of the test panicked, with its message and site in `failure`
 * (`failure` may be `NULL` where the caller does not want them).
 */
bool torb_test_tasks_end(bool abandon, torb_recovery *failure);

/* -------------------------------------------------------------------------------------------- the worker pool --- */

/**
 * `Workers.count()`: how many workers this process runs - `TORB_WORKERS` where it is set, and otherwise the number of
 * logical processors, capped at 1024. Fixed for the life of the process. A `TORB_WORKERS` that is not a whole number
 * from 1 to 1024 makes the program refuse to go on, with one line that names the variable and exit code 2, rather than
 * fall back to a default in silence (docs/design/CONCURRENCY.md section 3). `torb_process_start` asks it first.
 */
int64_t torb_workers_count(void);

/** The index of the worker running the calling thread: 0 on the main thread. For the tests. */
uint32_t torb_worker_index(void);

/**
 * `Workers.blocking()`: how many threads the blocking pool has - `TORB_BLOCKING` where it is set, and otherwise 4. Fixed
 * for the life of the process; a `TORB_BLOCKING` that is not a whole number from 1 to 1024 makes the program refuse to
 * go on, with one line that names the variable and exit code 2, as `TORB_WORKERS` does.
 */
int64_t torb_workers_blocking(void);

/**
 * The turn that moves the running task to the blocking pool (`offload` in `std/task`): a task that finishes at once,
 * and whose completion hands the task that awaits it to a thread of the blocking pool instead of back to its worker -
 * where that task was started portable, so its frame may move. Anywhere else it is only a turn, and the awaiting task
 * goes on where it is. The pool's threads are started the first time a task moves. Result owned.
 */
torb_task *torb_blocking_turn(void);

/** Whether the calling thread is a thread of the blocking pool. For the tests. */
bool torb_worker_is_blocking(void);

/** For the runtime's tests: the size of the blocking pool the next time it starts; 0 goes back to `torb_workers_blocking`. */
void torb_pool_set_blocking(uint32_t count);

/**
 * For the runtime's tests: the number of workers the pool starts with the next time it starts, in place of
 * `torb_workers_count()`. Only while the pool is not running; 0 goes back to the count the process was given.
 */
void torb_pool_set_workers(uint32_t count);

/** What the pool did so far, summed over every worker and every run of the pool. For the tests and the benchmark. */
typedef struct torb_pool_statistics {
  /** Resumes of a task, on every worker. */
  uint64_t resumed;
  /** Tasks a worker took from another one's queue before their first run. */
  uint64_t stolen;
  /** How many times the pool's threads were started. */
  uint64_t starts;
  /** Blocks copied at a crossing (`torb_text_privatize` and its siblings): what a program pays for moving a value. */
  uint64_t copied;
} torb_pool_statistics;

torb_pool_statistics torb_pool_statistics_now(void);

/* ------------------------------------------------------------------------------- what may cross a worker --- */

/*
 * The tests the compiler writes beside `torb_task_start_portable` and `torb_share`, one per value of a frame or a
 * closure whose type alone does not answer it. Each is true where handing the value to another thread can never touch a
 * count that this thread may still touch: nothing counted inside, an immortal block, a shared (atomic) block, or - for
 * the frame of a task, which runs once and on one thread - a block that only this value holds and whose elements are
 * plain. All values borrowed.
 */

/** A closure: no environment, or a shared one. */
bool torb_closure_may_move(torb_environment *environment);
/** A `String`: immortal storage; with `transfer`, also storage that only this text holds. */
bool torb_text_may_move(torb_text text, bool transfer);
/** A list: no storage or immortal storage; with `transfer`, also storage only this list holds, with plain elements. */
bool torb_list_may_move(torb_list list, bool transfer);
/** A map or a set: the same, with a plain key and a plain value. */
bool torb_map_may_move(torb_map map, bool transfer);

/*
 * The copy at the crossing (docs/design/CONCURRENCY.md section 16, "The copy at the crossing"). Where a frame's values
 * fail the tests above but may be copied soundly, the compiler writes beside the start of a task, while the pool has
 * more than one worker (`torb_task_copies`), one of these per value of the frame, and starts the task portable where all
 * of them answer true. They run on the thread that starts the task, before anybody else can see the frame.
 *
 * A **privatize function** takes a place that holds an owned value and leaves in it a value equal to it that shares no
 * counted block with anything outside it - every block it reaches is its own with a count of 1, immortal, or shared -
 * releasing the count the place held on the original. Nothing is copied that is private already. False where the value
 * reaches something that cannot be copied soundly (a closure whose environment is not shared); the place then still
 * holds a valid owned value, maybe copied in part, and the task stays on its worker.
 */
typedef bool (*torb_privatize_function)(void *place);

/** Whether the pool has more than one worker, so a copy at a crossing can pay off. */
bool torb_task_copies(void);
/** A `String`: its own storage where somebody else holds it too. Always true. `text` in and out. */
bool torb_text_privatize(torb_text *text);
/** The same through a `void *`: the element function of a list, a map or a set of `String`s. */
bool torb_text_privatize_place(void *place);
/**
 * A list: storage of its own, with `element` run on every element of it; `NULL` for plain elements. `list` in and out.
 */
bool torb_list_privatize(torb_list *list, torb_privatize_function element);
/** A map or a set: a table of its own, with `key` and `value` run on every entry; `NULL` for a plain side. */
bool torb_map_privatize(torb_map *map, torb_privatize_function key, torb_privatize_function value);

/**
 * An element function that is handed a context beside the place: the VM's, whose one function serves every element type
 * it made and is told which by the context. The two below are the list and the map above with it.
 */
typedef bool (*torb_privatize_with_function)(const void *context, void *place);
bool torb_list_privatize_with(torb_list *list, torb_privatize_with_function element, const void *context);
bool torb_map_privatize_with(torb_map *map, torb_privatize_with_function key, const void *keyContext,
                             torb_privatize_with_function value, const void *valueContext);
/** A `torb_privatize_function` as one of those: `context` points at the function. */
bool torb_privatize_plain(const void *context, void *place);
/** `torb_text_privatize_place` as one of those; the context is not read. */
bool torb_text_privatize_with(const void *context, void *place);

#endif /* TORB_TASK_H */
