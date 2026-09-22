/*
 * task.c - tasks, the scheduler, timers and channels (milestone 7.3).
 *
 * torb_task.h is the contract: what a task block holds and how the compiler lowers a function that answers a `Task` to
 * a resume function. This file is the one worker that runs them - a FIFO run queue, a binary min-heap of timers, and
 * the channels - and the tasks the runtime writes itself (`sleep`, `pause`, `within`), which are resume functions under
 * exactly the rules the compiler's are.
 *
 * **Nothing here allocates per wait.** A task waits in lists threaded through its own block (`wait_previous`/`wait_next`
 * for the waiters of a task and the queues of a channel, `queue_next` for the run queue), and a sender's item stays in
 * the slot of its frame until a receiver moves it out. The only buffers are a channel's ring and the timer heap.
 *
 * **Determinism** (docs/CONCURRENCY.md section 7): the run queue is FIFO, a task that is woken goes to its back, and
 * timers that are due are woken in deadline order, ties in the order they were set, before the next task is taken. A
 * program that does no real IO and reads no clock therefore runs its tasks in one order on every machine.
 *
 * # What the thread pool slice changes here
 *
 * `torb_scheduler` is everything a worker owns apart from its heap, and `torb_scheduler_current` is the one place that
 * answers which one is running - so slices E-H give every worker thread its own and make that function answer a
 * thread-local pointer, the way memory.c's `torb_heap_current` does for the heap. Beyond that, a pool needs what is
 * deliberately not here: a waiter on another worker (an `await` across workers wakes the owner through an atomic flag
 * and its inbox instead of touching a foreign run queue), the inbox itself and the stealing of unstarted tasks, a
 * channel whose two ends are on two workers (its queues then need a lock, and an item crosses by transfer or copy,
 * BACKEND 2.5), the blocking pool, and the poller of `runtime/io.c`. The frame counter of panic.c is per thread too.
 */

#include "torb.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ the worker --- */

typedef struct torb_timer {
  torb_instant deadline;
  /** Breaks ties between equal deadlines: the timer set first fires first. */
  uint64_t sequence;
  torb_task *task;
} torb_timer;

typedef struct torb_scheduler {
  torb_task *queue_first;
  torb_task *queue_last;
  /** The task whose machine is running, which is the parent of every task it makes. */
  torb_task *current;
  torb_task *live_first;
  torb_task *live_last;
  size_t live_count;
  torb_timer *timers;
  uint32_t timer_count;
  uint32_t timer_capacity;
  uint64_t timer_sequence;
  bool running;
} torb_scheduler;

static torb_scheduler torb_process_scheduler;

/* The worker running now. One in 7.3; a thread-local pointer once there is a pool (see the top of the file). */
static torb_scheduler *torb_scheduler_current(void) {
  return &torb_process_scheduler;
}

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

static void torb_enqueue(torb_scheduler *scheduler, torb_task *task) {
  task->queued = 1u;
  task->queue_next = NULL;
  if (scheduler->queue_last != NULL) {
    scheduler->queue_last->queue_next = task;
  } else {
    scheduler->queue_first = task;
  }
  scheduler->queue_last = task;
}

static torb_task *torb_dequeue(torb_scheduler *scheduler) {
  torb_task *task = scheduler->queue_first;
  if (task == NULL) {
    return NULL;
  }
  scheduler->queue_first = task->queue_next;
  if (scheduler->queue_first == NULL) {
    scheduler->queue_last = NULL;
  }
  task->queue_next = NULL;
  task->queued = 0u;
  return task;
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

/* -------------------------------------------------------------------------------------------------- timers --- */

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

static void torb_timer_add(torb_scheduler *scheduler, torb_task *task, torb_instant deadline) {
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
}

static void torb_timer_remove(torb_scheduler *scheduler, uint32_t index) {
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
}

/* ---------------------------------------------------------------------------------------- waiting and waking --- */

static void torb_release_item(const torb_element *item, void *value) {
  if (item->release != NULL) {
    item->release(value);
  }
}

static void torb_move_item(const torb_element *item, void *to, const void *from) {
  if (item->size > 0u) {
    memcpy(to, from, item->size);
  }
}

/*
 * Takes the task out of wherever it waits: the waiter list of a task, the queue of a channel, the timer heap. Where
 * it is `cancelling`, an item it offered to a channel is released, because the send consumed it and nobody took it.
 */
static void torb_unregister(torb_scheduler *scheduler, torb_task *task, bool cancelling) {
  torb_channel *channel = NULL;
  switch ((torb_task_waiting)task->waiting) {
    case TORB_WAITING_TASK:
      torb_waiters_unlink(&((torb_task *)task->wait_target)->waiters, task);
      break;
    case TORB_WAITING_SEND:
      channel = (torb_channel *)task->wait_target;
      torb_waiters_unlink(&channel->senders, task);
      if (cancelling) {
        torb_release_item(channel->item, task->wait_slot);
      }
      break;
    case TORB_WAITING_RECEIVE:
      channel = (torb_channel *)task->wait_target;
      torb_waiters_unlink(&channel->receivers, task);
      break;
    case TORB_WAITING_TIMER:
    case TORB_WAITING_NOTHING:
      break;
  }
  if (task->timer >= 0) {
    torb_timer_remove(scheduler, (uint32_t)task->timer);
  }
  task->waiting = (uint8_t)TORB_WAITING_NOTHING;
  task->wait_target = NULL;
  task->wait_slot = NULL;
  /* A waiting task holds its channel, because its frame need not: the last use of a channel may be the send itself. */
  if (channel != NULL) {
    torb_channel_release(channel);
  }
}

static void torb_wake(torb_scheduler *scheduler, torb_task *task, torb_outcome outcome) {
  torb_unregister(scheduler, task, false);
  task->outcome = (int32_t)outcome;
  torb_enqueue(scheduler, task);
}

/* A suspension primitive is called by the running task, once per wait. Anything else is a bug of the lowering. */
static void torb_expect_idle(torb_scheduler *scheduler, torb_task *self) {
  if (self != scheduler->current) {
    torb_internal_error("a task waited while it was not the one running");
  }
  if (self->waiting != (uint8_t)TORB_WAITING_NOTHING || self->queued != 0u) {
    torb_internal_error("a task waited for two things at once");
  }
}

static void torb_deliver(torb_task *self, void *slot, const torb_element *item) {
  self->delivered = slot;
  self->delivered_element = item;
}

/* ------------------------------------------------------------------------------------------- cancellation --- */

static void torb_cancel_one(torb_scheduler *scheduler, torb_task *task) {
  if (task->status != (uint8_t)TORB_TASK_PENDING) {
    return;
  }
  task->cancelled = 1u;
  if (task->waiting != (uint8_t)TORB_WAITING_NOTHING) {
    torb_unregister(scheduler, task, true);
    task->outcome = (int32_t)TORB_OUTCOME_CANCELLED;
    torb_enqueue(scheduler, task);
  }
}

void torb_task_cancel(torb_task *self) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_task *node = self;
  /* The subtree in pre-order, without a stack: down to the first child, else across to the next sibling, else up. */
  for (;;) {
    torb_cancel_one(scheduler, node);
    if (node->children_first != NULL) {
      node = node->children_first;
      continue;
    }
    while (node != self && node->sibling_next == NULL) {
      node = node->parent;
    }
    if (node == self) {
      return;
    }
    node = node->sibling_next;
  }
}

/* ----------------------------------------------------------------------------------------------- completion --- */

/*
 * A task that finished hands the children still running to its own parent, so that cancelling the grandparent still
 * reaches them; a parent that is already cancelled cancels what it adopts.
 */
static void torb_hand_children_up(torb_task *task) {
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
      if (parent->cancelled != 0u) {
        torb_task_cancel(child);
      }
    }
    child = next;
  }
}

static void torb_complete(torb_scheduler *scheduler, torb_task *task, bool finished) {
  torb_task *child;
  if (task->waiting != (uint8_t)TORB_WAITING_NOTHING || task->queued != 0u) {
    torb_internal_error("a task completed while it was still waiting");
  }
  /* An item a receive delivered that the machine never took, because it stopped at its check first. */
  if (task->delivered != NULL) {
    torb_release_item(task->delivered_element, task->delivered);
    task->delivered = NULL;
    task->delivered_element = NULL;
  }
  task->status = (uint8_t)(finished ? TORB_TASK_FINISHED : TORB_TASK_CANCELLED);
  if (!finished) {
    for (child = task->children_first; child != NULL; child = child->sibling_next) {
      torb_task_cancel(child);
    }
  }
  torb_hand_children_up(task);
  torb_unlink_child(task);
  if (task->live_previous != NULL) {
    task->live_previous->live_next = task->live_next;
  } else {
    scheduler->live_first = task->live_next;
  }
  if (task->live_next != NULL) {
    task->live_next->live_previous = task->live_previous;
  } else {
    scheduler->live_last = task->live_previous;
  }
  task->live_previous = NULL;
  task->live_next = NULL;
  scheduler->live_count -= 1u;
  while (task->waiters.first != NULL) {
    torb_wake(scheduler, task->waiters.first, TORB_OUTCOME_READY);
  }
  /* The scheduler's own reference: the block stays while a handle can still read the result. */
  torb_task_release(task);
}

static void torb_run_one(torb_scheduler *scheduler, torb_task *task) {
  torb_poll poll;
  scheduler->current = task;
  poll = task->resume(task);
  scheduler->current = NULL;
  switch (poll) {
    case TORB_POLL_SUSPENDED:
      if (task->waiting == (uint8_t)TORB_WAITING_NOTHING && task->queued == 0u) {
        torb_internal_error("a task suspended without waiting for anything");
      }
      return;
    case TORB_POLL_FINISHED:
      torb_complete(scheduler, task, true);
      return;
    case TORB_POLL_STOPPED:
      torb_complete(scheduler, task, false);
      return;
  }
  torb_internal_error("a resume function answered something that is not a torb_poll");
}

/* ------------------------------------------------------------------------------------ making and holding one --- */

torb_task *torb_task_new(torb_resume_function resume, size_t frame_size, const torb_element *result) {
  torb_scheduler *scheduler = torb_scheduler_current();
  size_t alignment = result->align == 0u ? 1u : (size_t)result->align;
  size_t result_offset;
  size_t total;
  torb_task *task;
  if (frame_size > (size_t)0x7FFFFFFFu) {
    torb_panic_text("a task frame larger than 2 GiB is not supported", torb_location_unknown);
  }
  result_offset = (TORB_TASK_FRAME_OFFSET + frame_size + alignment - 1u) / alignment * alignment;
  total = result_offset + (size_t)result->size;
  if (total > (size_t)0xFFFFFFFFu) {
    torb_panic_text("a task larger than 4 GiB is not supported", torb_location_unknown);
  }
  task = (torb_task *)torb_allocate_zeroed(total, TORB_BLOCK_TASK);
  task->resume = resume;
  task->result = result;
  task->state = 0u;
  task->result_offset = (uint32_t)result_offset;
  task->cancelled = 0u;
  task->status = (uint8_t)TORB_TASK_PENDING;
  task->waiting = (uint8_t)TORB_WAITING_NOTHING;
  task->queued = 0u;
  task->outcome = (int32_t)TORB_OUTCOME_NONE;
  task->timer = -1;
  task->worker = 0u;
  task->queue_next = NULL;
  task->wait_target = NULL;
  task->wait_slot = NULL;
  task->wait_previous = NULL;
  task->wait_next = NULL;
  task->delivered = NULL;
  task->delivered_element = NULL;
  task->waiters.first = NULL;
  task->waiters.last = NULL;
  task->parent = NULL;
  task->children_first = NULL;
  task->children_last = NULL;
  task->sibling_previous = NULL;
  task->sibling_next = NULL;
  if (scheduler->current != NULL) {
    torb_link_child(scheduler->current, task);
    if (scheduler->current->cancelled != 0u) {
      task->cancelled = 1u;
    }
  }
  task->live_next = NULL;
  task->live_previous = scheduler->live_last;
  if (scheduler->live_last != NULL) {
    scheduler->live_last->live_next = task;
  } else {
    scheduler->live_first = task;
  }
  scheduler->live_last = task;
  scheduler->live_count += 1u;
  return task;
}

void torb_task_start(torb_task *task) {
  torb_retain(task);
  torb_enqueue(torb_scheduler_current(), task);
}

void torb_task_drop(void *block) {
  torb_task *task = (torb_task *)block;
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

torb_wait torb_task_await(torb_task *self, torb_task *awaited) {
  torb_scheduler *scheduler = torb_scheduler_current();
  if (self == awaited) {
    torb_panic_text("a task cannot await itself: nothing could ever wake it", torb_location_unknown);
  }
  torb_expect_idle(scheduler, self);
  if (awaited->status != (uint8_t)TORB_TASK_PENDING) {
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  self->waiting = (uint8_t)TORB_WAITING_TASK;
  self->wait_target = awaited;
  torb_waiters_append(&awaited->waiters, self);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_task_await_until(torb_task *self, torb_task *awaited, torb_instant deadline) {
  torb_scheduler *scheduler = torb_scheduler_current();
  if (self == awaited) {
    torb_panic_text("a task cannot await itself: nothing could ever wake it", torb_location_unknown);
  }
  torb_expect_idle(scheduler, self);
  if (awaited->status != (uint8_t)TORB_TASK_PENDING) {
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (deadline <= torb_clock_now()) {
    self->outcome = (int32_t)TORB_OUTCOME_TIMED_OUT;
    return TORB_WAIT_READY;
  }
  self->waiting = (uint8_t)TORB_WAITING_TASK;
  self->wait_target = awaited;
  torb_waiters_append(&awaited->waiters, self);
  torb_timer_add(scheduler, self, deadline);
  return TORB_WAIT_SUSPENDED;
}

bool torb_task_result(torb_task *task, void *out) {
  if (task->status == (uint8_t)TORB_TASK_FINISHED) {
    torb_move_item(task->result, out, torb_task_result_slot(task));
    if (task->result->retain != NULL) {
      task->result->retain(out);
    }
    return true;
  }
  if (task->status == (uint8_t)TORB_TASK_CANCELLED) {
    return false;
  }
  torb_internal_error("read the result of a task that has not completed");
}

bool torb_task_is_complete(const torb_task *task) {
  return task->status != (uint8_t)TORB_TASK_PENDING;
}

torb_outcome torb_task_outcome(torb_task *self) {
  self->delivered = NULL;
  self->delivered_element = NULL;
  return (torb_outcome)self->outcome;
}

torb_wait torb_task_pause(torb_task *self) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_expect_idle(scheduler, self);
  self->outcome = (int32_t)TORB_OUTCOME_READY;
  torb_enqueue(scheduler, self);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_task_sleep_until(torb_task *self, torb_instant deadline) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_expect_idle(scheduler, self);
  if (deadline <= torb_clock_now()) {
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  self->waiting = (uint8_t)TORB_WAITING_TIMER;
  torb_timer_add(scheduler, self, deadline);
  return TORB_WAIT_SUSPENDED;
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
  channel->item = item;
  channel->buffer = NULL;
  channel->capacity = (uint32_t)capacity;
  channel->head = 0u;
  channel->count = 0u;
  channel->ended = 0u;
  channel->closed = 0u;
  channel->senders.first = NULL;
  channel->senders.last = NULL;
  channel->receivers.first = NULL;
  channel->receivers.last = NULL;
  if (capacity > 0 && item->size > 0u) {
    channel->buffer = (uint8_t *)torb_raw_allocate((size_t)capacity * item->size);
  }
  return channel;
}

torb_wait torb_channel_send(torb_task *self, torb_channel *channel, void *item) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_task *receiver;
  torb_expect_idle(scheduler, self);
  if (channel->closed != 0u || channel->ended != 0u) {
    torb_release_item(channel->item, item);
    self->outcome = (int32_t)TORB_OUTCOME_CLOSED;
    return TORB_WAIT_READY;
  }
  receiver = channel->receivers.first;
  if (receiver != NULL) {
    torb_move_item(channel->item, receiver->wait_slot, item);
    torb_deliver(receiver, receiver->wait_slot, channel->item);
    torb_wake(scheduler, receiver, TORB_OUTCOME_READY);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (channel->count < channel->capacity) {
    torb_move_item(channel->item, torb_channel_slot(channel, channel->count), item);
    channel->count += 1u;
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  torb_retain(channel);
  self->waiting = (uint8_t)TORB_WAITING_SEND;
  self->wait_target = channel;
  self->wait_slot = item;
  torb_waiters_append(&channel->senders, self);
  return TORB_WAIT_SUSPENDED;
}

torb_wait torb_channel_receive(torb_task *self, torb_channel *channel, void *out) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_task *sender;
  torb_expect_idle(scheduler, self);
  if (channel->count > 0u) {
    torb_channel_pop(channel, out);
    /* The room that made is the first waiting sender's. */
    sender = channel->senders.first;
    if (sender != NULL) {
      torb_move_item(channel->item, torb_channel_slot(channel, channel->count), sender->wait_slot);
      channel->count += 1u;
      torb_wake(scheduler, sender, TORB_OUTCOME_READY);
    }
    torb_deliver(self, out, channel->item);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  sender = channel->senders.first;
  if (sender != NULL) {
    torb_move_item(channel->item, out, sender->wait_slot);
    torb_wake(scheduler, sender, TORB_OUTCOME_READY);
    torb_deliver(self, out, channel->item);
    self->outcome = (int32_t)TORB_OUTCOME_READY;
    return TORB_WAIT_READY;
  }
  if (channel->ended != 0u || channel->closed != 0u) {
    self->outcome = (int32_t)TORB_OUTCOME_CLOSED;
    return TORB_WAIT_READY;
  }
  torb_retain(channel);
  self->waiting = (uint8_t)TORB_WAITING_RECEIVE;
  self->wait_target = channel;
  self->wait_slot = out;
  torb_waiters_append(&channel->receivers, self);
  return TORB_WAIT_SUSPENDED;
}

void torb_channel_end(torb_channel *channel) {
  torb_scheduler *scheduler = torb_scheduler_current();
  if (channel->ended != 0u) {
    return;
  }
  channel->ended = 1u;
  /* A receiver only waits where nothing is buffered and no sender waits, so what it waits for now is the end. */
  while (channel->receivers.first != NULL) {
    torb_wake(scheduler, channel->receivers.first, TORB_OUTCOME_CLOSED);
  }
}

void torb_channel_close(torb_channel *channel) {
  torb_scheduler *scheduler = torb_scheduler_current();
  if (channel->closed != 0u) {
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
    torb_wake(scheduler, sender, TORB_OUTCOME_CLOSED);
  }
  while (channel->receivers.first != NULL) {
    torb_wake(scheduler, channel->receivers.first, TORB_OUTCOME_CLOSED);
  }
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

/* ----------------------------------------------------------------------------------------------- the scheduler --- */

static void torb_fire_timers(torb_scheduler *scheduler) {
  torb_instant now;
  if (scheduler->timer_count == 0u) {
    return;
  }
  now = torb_clock_now();
  while (scheduler->timer_count > 0u && scheduler->timers[0].deadline <= now) {
    torb_task *task = scheduler->timers[0].task;
    torb_outcome outcome =
      task->waiting == (uint8_t)TORB_WAITING_TIMER ? TORB_OUTCOME_READY : TORB_OUTCOME_TIMED_OUT;
    torb_wake(scheduler, task, outcome);
  }
}

void torb_scheduler_run(torb_task *until) {
  torb_scheduler *scheduler = torb_scheduler_current();
  if (scheduler->running) {
    torb_internal_error("the scheduler was run from inside a task");
  }
  scheduler->running = true;
  for (;;) {
    torb_task *task;
    if (until != NULL && until->status != (uint8_t)TORB_TASK_PENDING) {
      break;
    }
    torb_fire_timers(scheduler);
    task = torb_dequeue(scheduler);
    if (task != NULL) {
      torb_run_one(scheduler, task);
      continue;
    }
    if (scheduler->timer_count > 0u) {
      torb_platform_sleep(scheduler->timers[0].deadline - torb_clock_now());
      continue;
    }
    if (until == NULL) {
      break;
    }
    scheduler->running = false;
    torb_panic_text("deadlock: every task is waiting for another one, and nothing is left that could wake one",
                    torb_location_unknown);
  }
  scheduler->running = false;
}

void torb_scheduler_finish(void) {
  torb_scheduler *scheduler = torb_scheduler_current();
  torb_task *task;
  /* Cancelling changes no live list, only completing does - and nothing completes before the loop below runs. */
  for (task = scheduler->live_first; task != NULL; task = task->live_next) {
    torb_task_cancel(task);
  }
  torb_scheduler_run(NULL);
  if (scheduler->live_count != 0u) {
    torb_internal_error("a cancelled task did not stop");
  }
  torb_raw_free(scheduler->timers, (size_t)scheduler->timer_capacity * sizeof(torb_timer));
  scheduler->timers = NULL;
  scheduler->timer_capacity = 0u;
  scheduler->timer_sequence = 0u;
}

size_t torb_task_live_count(void) {
  return torb_scheduler_current()->live_count;
}

torb_task *torb_task_current(void) {
  return torb_scheduler_current()->current;
}
