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
 *       if (torb_task_cancelled(task)) goto stop_1;
 *       (void)torb_task_outcome(task);                         // after every wait, READY or resumed
 *       if (torb_task_result(frame->child, &frame->value)) { ...Ok(frame->value)... } else { ...Fail(Cancelled)... }
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
 *    **`TORB_POLL_STOPPED`: nothing in the frame is live and there is no value**; every `await()` of the task answers
 *    `Fail(Cancelled)`. A machine may also stop without the flag, to end itself as cancelled - which is how a task that
 *    awaited a cancelled task and has no failure of its own to report passes the cancellation on (`Task.map`, `within`).
 * 6. `torb_enter_frame`/`torb_leave_frame` stay balanced **per resume**: a resume function that enters leaves before
 *    every return, suspended or not, because the C stack is gone after it.
 *
 * # How `Result` is built
 *
 * **The runtime never builds a `Result`, a `Cancelled` or a `TimedOut`**: all three are layouts of the program (BACKEND
 * 5.R1), and the runtime has no layout of the program in it. `await()` follows the `.Fallible` convention of every
 * other native: `torb_task_result` answers `bool` and writes the value through an out parameter, and the lowering
 * builds `Ok(value)` (variant 0) or `Fail(Cancelled())` (variant 1, no fields) in its own layout of
 * `Result<Value, Cancelled>` around it. The one native that has to put a `Result` *into* a task, `Task.within`, is
 * handed a `torb_within_shape` - two adapters the lowering emits per instance, the way it emits a `torb_element` per
 * type - and calls them.
 *
 * # The scheduler
 *
 * One worker, one FIFO run queue, one heap (the pool of docs/design/CONCURRENCY.md is slices E to H of 7.7). A task that is
 * started or woken goes to the back of the queue; a timer that is due is woken before the next task is taken, in
 * deadline order. When the queue is empty and a timer is pending, the worker sleeps until the first deadline. When the
 * queue is empty, no timer is pending and the task being waited for has not completed, every task is waiting for
 * another one: that is a deadlock, and it panics rather than hangs.
 *
 * The generated `main` of a program that uses tasks runs its entry file as the **main task**:
 *
 *     torb_process_start(argc, argv);
 *     torb_task *main_task = torb_task_new(entry_resume, sizeof(T_entry_frame), &torb_element_void);
 *     torb_task_start(main_task);
 *     torb_scheduler_run(main_task);
 *     torb_task_release(main_task);
 *     torb_scheduler_finish();                 // every task still running is cancelled and runs to its stop
 *     torb_process_finish();
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
  TORB_WAITING_RECEIVE = 4
} torb_task_waiting;

/** An intrusive list of tasks through `wait_previous`/`wait_next`: the waiters of a task, the senders of a channel. */
typedef struct torb_task_list {
  torb_task *first;
  torb_task *last;
} torb_task_list;

/**
 * The runtime's part of a task block. **The machine reads `cancelled` (through `torb_task_cancelled`) and writes
 * `state`; every other field is the runtime's**, and is here only because the accessors below are `static inline`.
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
  uint8_t queued;          /**< In the run queue. */
  int32_t outcome;         /**< `torb_outcome` of the last wait. */
  int32_t timer;           /**< The index in the timer heap, or -1. */
  /** The worker whose heap the block is in. Always 0 until 7.7: a started task never leaves its worker. */
  uint32_t worker;
  torb_task *queue_next;
  /** What it waits on (a `torb_task` or a `torb_channel`) and the slot of the frame a send offers or a receive fills. */
  void *wait_target;
  void *wait_slot;
  torb_task *wait_previous;
  torb_task *wait_next;
  /** A received item that the frame has not taken yet (`torb_task_outcome` takes it), and what it is. */
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

/** The cancellation check: one load and one branch, at every resume, after every ready wait, at every back-edge. */
static inline bool torb_task_cancelled(const torb_task *task) {
  return task->cancelled != 0u;
}

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

/** `task` borrowed. Queues it; the scheduler takes its own reference until the task completes. */
void torb_task_start(torb_task *task);

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
 */
torb_wait torb_task_await(torb_task *self, torb_task *awaited);

/**
 * The same, with a deadline on the monotonic clock (`torb_clock_now`): whichever comes first wakes `self`, and the
 * outcome is `TORB_OUTCOME_READY` or `TORB_OUTCOME_TIMED_OUT`. A timeout does nothing to `awaited`. Ready at once when
 * `awaited` has completed (`READY`) or the deadline has passed (`TIMED_OUT`). It is what `within` is made of.
 */
torb_wait torb_task_await_until(torb_task *self, torb_task *awaited, torb_instant deadline);

/**
 * `awaited.await()`, the answer: true where the task finished, with a copy of its value in `*out` (owned - retained
 * through the descriptor, because a `Task` may be awaited by any number of tasks); false where it was cancelled, which
 * the lowering turns into `Fail(Cancelled())`. `task` borrowed. Panics where the task has not completed.
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
 * released - so the leak report of the exit sees what the end of `main` would have seen. A no-op without tasks.
 */
void torb_scheduler_exit(void);

/** How many tasks have not completed yet. For the tests and the leak report. */
size_t torb_task_live_count(void);

/** The task whose machine is running now, or `NULL` outside every task. Borrowed. */
torb_task *torb_task_current(void);

#endif /* TORB_TASK_H */
