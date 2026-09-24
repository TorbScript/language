/*
 * task_test.c - the scheduler driven by hand-written state machines, written exactly the way torb_task.h tells the
 * compiler to lower a function that answers a `Task`: a switch over `task->state`, the cancellation check first at
 * every resume and at every back-edge, `torb_task_outcome` after every wait, and a stop path per state that releases
 * what is live there.
 *
 * Every test ends with `torb_scheduler_finish()` and the harness's leak check, so each one also proves that a stopped
 * frame released everything it held.
 */

#include "harness.h"

/* ------------------------------------------------------------------------------------------------ the log --- */

static char task_log[512];
static size_t task_log_length = 0u;

static void log_reset(void) {
  task_log_length = 0u;
  task_log[0] = '\0';
}

static void log_add(const char *text) {
  size_t length = strlen(text);
  if (task_log_length + length + 1u < sizeof task_log) {
    memcpy(task_log + task_log_length, text, length + 1u);
    task_log_length += length;
  }
}

static bool text_is(torb_text text, const char *expected) {
  size_t length = strlen(expected);
  return text.length == length && (length == 0u || memcmp(text.storage->data + text.offset, expected, length) == 0);
}

/* ------------------------------------------------------------------------------- square(x): no wait at all --- */

typedef struct square_frame {
  int64_t input;
} square_frame;

static torb_poll square_resume(torb_task *task) {
  square_frame *frame = (square_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  log_add("s");
  *(int64_t *)torb_task_result_slot(task) = frame->input * frame->input;
  return TORB_POLL_FINISHED;
}

static torb_task *square(int64_t input) {
  torb_task *task = torb_task_new(square_resume, sizeof(square_frame), &torb_element_int64);
  ((square_frame *)torb_task_frame(task))->input = input;
  torb_task_start(task);
  return task;
}

/* ------------------------------------------------ awaiting(task): awaits one task and says what it answered --- */

typedef struct awaiting_frame {
  torb_task *awaited;
} awaiting_frame;

/*
 * What the last `awaiting` task read: whether the awaited one finished, and its value where it is an `Int64`. It waits
 * the way `result()` does (`torb_task_observe`), so a cancellation of what it awaits is an answer and not its own stop.
 */
static bool awaited_finished = false;
static int64_t awaited_value = 0;

static torb_poll awaiting_resume(torb_task *task) {
  awaiting_frame *frame = (awaiting_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  task->state = 1u;
  if (torb_task_observe(task, frame->awaited) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  awaited_value = -1;
  awaited_finished = torb_task_result(frame->awaited, &awaited_value);
  torb_task_release(frame->awaited);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop:
  torb_task_release(frame->awaited);
  return TORB_POLL_STOPPED;
}

/* `awaited` consumed: the frame owns the handle from here on. Only for tasks whose value is an `Int64` or `Void`. */
static torb_task *awaiting(torb_task *awaited) {
  torb_task *task = torb_task_new(awaiting_resume, sizeof(awaiting_frame), &torb_element_void);
  ((awaiting_frame *)torb_task_frame(task))->awaited = awaited;
  torb_task_start(task);
  return task;
}

/* ----------------------------------------------------------------------------------------- spawn and await --- */

typedef struct parent_frame {
  torb_task *child;
  int64_t value;
} parent_frame;

static torb_poll parent_resume(torb_task *task) {
  parent_frame *frame = (parent_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  log_add("p");
  frame->child = square(7);
  task->state = 1u;
  if (torb_task_await(task, frame->child) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->child);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  if (!torb_task_result(frame->child, &frame->value)) {
    frame->value = -1;
  }
  torb_task_release(frame->child);
  log_add("P");
  *(int64_t *)torb_task_result_slot(task) = frame->value + 1;
  return TORB_POLL_FINISHED;
}

TORB_TEST(spawn_and_await) {
  torb_task *parent;
  int64_t value = 0;
  log_reset();
  parent = torb_task_new(parent_resume, sizeof(parent_frame), &torb_element_int64);
  torb_task_start(parent);
  torb_scheduler_run(parent);
  TORB_CHECK(torb_task_is_complete(parent));
  TORB_CHECK(torb_task_result(parent, &value));
  TORB_CHECK_INTEGER(value, 50);
  /* The parent ran until its await, the child ran, and the parent went on. */
  TORB_CHECK(strcmp(task_log, "psP") == 0);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(parent);
  torb_scheduler_finish();
}

/* A value read by two waiters is retained for each: a `Task` may be awaited by any number of tasks. */
typedef struct text_frame {
  torb_text text;
} text_frame;

static torb_poll text_resume(torb_task *task) {
  text_frame *frame = (text_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_text_release(frame->text);
    return TORB_POLL_STOPPED;
  }
  *(torb_text *)torb_task_result_slot(task) = frame->text;
  return TORB_POLL_FINISHED;
}

TORB_TEST(a_result_is_copied_for_every_reader) {
  torb_task *task = torb_task_new(text_resume, sizeof(text_frame), &torb_element_text);
  torb_text first;
  torb_text second;
  ((text_frame *)torb_task_frame(task))->text = torb_show_i64(123456);
  torb_task_start(task);
  torb_scheduler_run(task);
  TORB_CHECK(torb_task_result(task, &first));
  TORB_CHECK(torb_task_result(task, &second));
  TORB_CHECK(text_is(first, "123456"));
  TORB_CHECK(first.storage == second.storage);
  TORB_CHECK_INTEGER(first.storage->header.count, 3);
  torb_text_release(first);
  torb_text_release(second);
  /* The last handle frees the block and the result in it. */
  torb_task_release(task);
  torb_scheduler_finish();
}

/* ---------------------------------------------------------------------------------------------- ordering --- */

typedef struct turns_frame {
  char name;
  int32_t round;
} turns_frame;

static torb_poll turns_resume(torb_task *task) {
  turns_frame *frame = (turns_frame *)torb_task_frame(task);
  char entry[3];
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  if (task->state == 1u) {
    (void)torb_task_outcome(task);
  }
  if (frame->round == 3) {
    *(torb_void *)torb_task_result_slot(task) = 0u;
    return TORB_POLL_FINISHED;
  }
  entry[0] = frame->name;
  entry[1] = (char)('0' + frame->round);
  entry[2] = '\0';
  log_add(entry);
  frame->round += 1;
  task->state = 1u;
  return torb_task_pause(task) == TORB_WAIT_SUSPENDED ? TORB_POLL_SUSPENDED : TORB_POLL_FINISHED;
}

static torb_task *turns(char name) {
  torb_task *task = torb_task_new(turns_resume, sizeof(turns_frame), &torb_element_void);
  ((turns_frame *)torb_task_frame(task))->name = name;
  torb_task_start(task);
  return task;
}

TORB_TEST(the_run_queue_is_first_in_first_out) {
  torb_task *first;
  torb_task *second;
  torb_task *third;
  log_reset();
  first = turns('a');
  second = turns('b');
  third = turns('c');
  torb_scheduler_run(NULL);
  TORB_CHECK(strcmp(task_log, "a0b0c0a1b1c1a2b2c2") == 0);
  torb_task_release(first);
  torb_task_release(second);
  torb_task_release(third);
  torb_scheduler_finish();
}

/* ------------------------------------------------------------------------------------------- cancellation --- */

/*
 * A loop that never waits. At its tenth turn a synchronous callee cancels the task - somebody who holds the handle,
 * the only way to reach a task that does not suspend on one worker - and the check at the back-edge stops it.
 */
typedef struct grind_frame {
  torb_text held;
  int64_t turn;
} grind_frame;

static int64_t grind_turns = 0;

static void somebody_cancels(torb_task *task) {
  torb_task_cancel(task);
}

static torb_poll grind_resume(torb_task *task) {
  grind_frame *frame = (grind_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  frame->held = torb_show_i64(987654321);
  for (frame->turn = 0; frame->turn < 1000000; frame->turn += 1) {
    if (torb_task_cancelled(task)) { /* the back-edge */
      goto stop;
    }
    grind_turns = frame->turn;
    if (frame->turn == 10) {
      somebody_cancels(task);
    }
  }
  torb_text_release(frame->held);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop:
  torb_text_release(frame->held);
  return TORB_POLL_STOPPED;
}

TORB_TEST(a_loop_stops_at_its_back_edge) {
  torb_task *grind = torb_task_new(grind_resume, sizeof(grind_frame), &torb_element_void);
  torb_task *waiter;
  torb_task_start(grind);
  torb_retain(grind);
  waiter = awaiting(grind);
  awaited_finished = true;
  torb_scheduler_run(waiter);
  /* The turn that asked ran to its end, and the next one never started. */
  TORB_CHECK_INTEGER(grind_turns, 10);
  TORB_CHECK(!awaited_finished);
  TORB_CHECK_INTEGER(grind->status, TORB_TASK_CANCELLED);
  torb_task_release(grind);
  torb_task_release(waiter);
  torb_scheduler_finish();
}

/* The same loop, with a `pause()` every fourth turn: cancelled by another task while it is queued, it stops when it is
   resumed, before its next line. */
typedef struct polite_frame {
  torb_text held;
  int64_t turn;
} polite_frame;

static torb_poll polite_resume(torb_task *task) {
  polite_frame *frame = (polite_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  frame->held = torb_show_i64(42);
  for (frame->turn = 0; frame->turn < 1000; frame->turn += 1) {
    grind_turns = frame->turn;
    if (frame->turn % 4 == 3) {
      task->state = 1u;
      if (torb_task_pause(task) == TORB_WAIT_SUSPENDED) {
        return TORB_POLL_SUSPENDED;
      }
    state_1:
      if (torb_task_cancelled(task)) {
        goto stop;
      }
      (void)torb_task_outcome(task);
    }
    if (torb_task_cancelled(task)) {
      goto stop;
    }
  }
  torb_text_release(frame->held);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop:
  torb_text_release(frame->held);
  return TORB_POLL_STOPPED;
}

typedef struct canceller_frame {
  torb_task *target;
} canceller_frame;

static torb_poll canceller_resume(torb_task *task) {
  canceller_frame *frame = (canceller_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->target);
    return TORB_POLL_STOPPED;
  }
  log_add("k");
  torb_task_cancel(frame->target);
  torb_task_release(frame->target);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

/* `target` borrowed: the canceller keeps a handle of its own. */
static torb_task *canceller(torb_task *target) {
  torb_task *task = torb_task_new(canceller_resume, sizeof(canceller_frame), &torb_element_void);
  torb_retain(target);
  ((canceller_frame *)torb_task_frame(task))->target = target;
  torb_task_start(task);
  return task;
}

TORB_TEST(a_queued_loop_stops_when_it_is_resumed) {
  torb_task *polite = torb_task_new(polite_resume, sizeof(polite_frame), &torb_element_void);
  torb_task *stopper;
  log_reset();
  torb_task_start(polite);
  stopper = canceller(polite);
  torb_scheduler_run(NULL);
  /* Turns 0 to 3 ran, the pause let the canceller in, and the resume after it stopped. */
  TORB_CHECK_INTEGER(grind_turns, 3);
  TORB_CHECK_INTEGER(polite->status, TORB_TASK_CANCELLED);
  torb_task_release(polite);
  torb_task_release(stopper);
  torb_scheduler_finish();
}

/* A task that waits on a channel nobody writes to: the one wait that only a cancellation ends. */
typedef struct stuck_frame {
  torb_channel *channel;
  torb_text slot;
} stuck_frame;

static torb_poll stuck_resume(torb_task *task) {
  stuck_frame *frame = (stuck_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_channel_receive(task, frame->channel, &frame->slot) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  if (torb_task_outcome(task) == TORB_OUTCOME_READY) {
    torb_text_release(frame->slot);
  }
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

static torb_task *stuck(void) {
  torb_task *task = torb_task_new(stuck_resume, sizeof(stuck_frame), &torb_element_void);
  ((stuck_frame *)torb_task_frame(task))->channel = torb_channel_new(0, &torb_element_text, torb_location_unknown);
  torb_task_start(task);
  return task;
}

/* Awaits a task and holds a counted value across the wait, so a stop that forgot it would leak. */
typedef struct holding_frame {
  torb_task *awaited;
  torb_text held;
} holding_frame;

static torb_poll holding_resume(torb_task *task) {
  holding_frame *frame = (holding_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->awaited);
    return TORB_POLL_STOPPED;
  }
  frame->held = torb_show_i64(31415926);
  task->state = 1u;
  if (torb_task_await(task, frame->awaited) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    torb_text_release(frame->held);
    torb_task_release(frame->awaited);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  log_add("h");
  torb_text_release(frame->held);
  torb_task_release(frame->awaited);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(cancelling_a_waiter_takes_it_off_the_waiter_list) {
  torb_task *blocked;
  torb_task *holder;
  torb_task *stopper;
  torb_task *observer;
  log_reset();
  blocked = stuck();
  holder = torb_task_new(holding_resume, sizeof(holding_frame), &torb_element_void);
  torb_retain(blocked);
  ((holding_frame *)torb_task_frame(holder))->awaited = blocked;
  torb_task_start(holder);
  stopper = canceller(holder);
  torb_retain(holder);
  observer = awaiting(holder);
  awaited_finished = true;
  torb_scheduler_run(NULL);
  /* The holder was on the blocked task's waiter list and is not any more; it never went on after its await. */
  TORB_CHECK(blocked->waiters.first == NULL);
  TORB_CHECK(!torb_task_is_complete(blocked));
  TORB_CHECK_INTEGER(holder->status, TORB_TASK_CANCELLED);
  TORB_CHECK(strcmp(task_log, "k") == 0);
  /* Its waiter read `Fail(Cancelled)`. */
  TORB_CHECK(torb_task_is_complete(observer));
  TORB_CHECK(!awaited_finished);
  TORB_CHECK_INTEGER(torb_task_live_count(), 1);
  torb_task_release(holder);
  torb_task_release(stopper);
  torb_task_release(observer);
  /* The blocked one is still waiting: the end of the program cancels it, and its frame goes with it. */
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(blocked->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(blocked);
}

/* A parent that spawns and is cancelled before its children ran: they never run a line. */
typedef struct family_frame {
  torb_task *children[2];
  torb_channel *channel;
  torb_text slot;
} family_frame;

static torb_poll family_resume(torb_task *task) {
  family_frame *frame = (family_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->children[0]);
    torb_task_release(frame->children[1]);
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    log_add("f");
    frame->children[0] = square(2);
    frame->children[1] = square(3);
    frame->channel = torb_channel_new(0, &torb_element_text, torb_location_unknown);
    task->state = 1u;
    if (torb_channel_receive(task, frame->channel, &frame->slot) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  (void)torb_task_outcome(task);
  torb_task_release(frame->children[0]);
  torb_task_release(frame->children[1]);
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(a_cancelled_parent_cancels_its_children_before_their_first_line) {
  torb_task *family = torb_task_new(family_resume, sizeof(family_frame), &torb_element_void);
  torb_task *stopper;
  log_reset();
  torb_task_start(family);
  stopper = canceller(family);
  torb_scheduler_run(NULL);
  /* "f" is the parent and "k" the canceller; a child that ran would have written "s". */
  TORB_CHECK(strcmp(task_log, "fk") == 0);
  TORB_CHECK_INTEGER(family->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(family);
  torb_task_release(stopper);
  torb_scheduler_finish();
}

TORB_TEST(dropping_the_handle_does_not_cancel) {
  log_reset();
  torb_task_release(square(5));
  TORB_CHECK_INTEGER(torb_task_live_count(), 1);
  torb_scheduler_run(NULL);
  TORB_CHECK(strcmp(task_log, "s") == 0);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_scheduler_finish();
}

/* ------------------------------------------------------------------------------------------------ timers --- */

typedef struct napping_frame {
  const char *name;
  double seconds;
  torb_task *sleep;
} napping_frame;

static torb_poll napping_resume(torb_task *task) {
  napping_frame *frame = (napping_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  frame->sleep = torb_sleep(frame->seconds);
  task->state = 1u;
  if (torb_task_await(task, frame->sleep) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->sleep);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  log_add(frame->name);
  torb_task_release(frame->sleep);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

static torb_task *napping(const char *name, double seconds) {
  torb_task *task = torb_task_new(napping_resume, sizeof(napping_frame), &torb_element_void);
  napping_frame *frame = (napping_frame *)torb_task_frame(task);
  frame->name = name;
  frame->seconds = seconds;
  torb_task_start(task);
  return task;
}

TORB_TEST(sleeps_end_in_the_order_of_their_deadlines) {
  torb_task *tasks[4];
  torb_instant started = torb_clock_now();
  size_t index;
  log_reset();
  tasks[0] = napping("a", 0.030);
  tasks[1] = napping("b", 0.010);
  tasks[2] = napping("c", 0.020);
  tasks[3] = napping("d", -1.0); /* nothing to wait for, and still only when the scheduler gets to it */
  torb_scheduler_run(NULL);
  TORB_CHECK(strcmp(task_log, "dbca") == 0);
  TORB_CHECK(torb_clock_now() - started >= 30000000);
  for (index = 0u; index < 4u; index += 1u) {
    torb_task_release(tasks[index]);
  }
  torb_scheduler_finish();
}

/* ------------------------------------------------------------------------------------------------ within --- */

/* The program's layout of `Result<Int64, TimedOut>`, and the two adapters the lowering would emit for it. */
typedef struct int_or_timed_out {
  uint32_t tag;
  union {
    int64_t value;
    torb_duration limit;
  } payload;
} int_or_timed_out;

static void int_finished(void *result, void *value) {
  int_or_timed_out *built = (int_or_timed_out *)result;
  built->tag = 0u;
  built->payload.value = *(int64_t *)value;
}

static void int_timed_out(void *result, torb_duration limit) {
  int_or_timed_out *built = (int_or_timed_out *)result;
  built->tag = 1u;
  built->payload.limit = limit;
}

static const torb_element int_or_timed_out_element = {
  (uint32_t)sizeof(int_or_timed_out), (uint32_t)TORB_ALIGN_OF(int_or_timed_out), NULL, NULL, NULL, NULL
};

static const torb_within_shape int_within = { &int_or_timed_out_element, int_finished, int_timed_out };

/* A task that sleeps and then answers 5. */
typedef struct slow_frame {
  torb_instant deadline;
} slow_frame;

static torb_poll slow_resume(torb_task *task) {
  slow_frame *frame = (slow_frame *)torb_task_frame(task);
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
  log_add("w");
  *(int64_t *)torb_task_result_slot(task) = 5;
  return TORB_POLL_FINISHED;
}

static torb_task *slow(double seconds) {
  torb_task *task = torb_task_new(slow_resume, sizeof(slow_frame), &torb_element_int64);
  ((slow_frame *)torb_task_frame(task))->deadline = torb_clock_now() + (torb_duration)(seconds * 1e9);
  torb_task_start(task);
  return task;
}

TORB_TEST(within_cancels_a_task_that_takes_too_long) {
  torb_task *target;
  torb_task *raced;
  int_or_timed_out answer;
  torb_instant started = torb_clock_now();
  log_reset();
  target = slow(2.0);
  raced = torb_task_within(target, 10000000, &int_within);
  torb_scheduler_run(raced);
  TORB_CHECK(torb_task_result(raced, &answer));
  TORB_CHECK_INTEGER(answer.tag, 1);
  TORB_CHECK_INTEGER(answer.payload.limit, 10000000);
  TORB_CHECK(torb_clock_now() - started < 1000000000);
  /* The target was cancelled; it stops when the worker reaches it, and never writes its "w". */
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(target->status, TORB_TASK_CANCELLED);
  TORB_CHECK(strcmp(task_log, "") == 0);
  torb_task_release(target);
  torb_task_release(raced);
  torb_scheduler_finish();
}

TORB_TEST(within_passes_on_a_value_in_time) {
  torb_instant started = torb_clock_now();
  torb_task *target = slow(0.02);
  torb_task *raced = torb_task_within(target, 5000000000LL, &int_within);
  int_or_timed_out answer;
  log_reset();
  torb_scheduler_run(NULL);
  TORB_CHECK(torb_task_result(raced, &answer));
  TORB_CHECK_INTEGER(answer.tag, 0);
  TORB_CHECK_INTEGER(answer.payload.value, 5);
  TORB_CHECK(strcmp(task_log, "w") == 0);
  /* The five-second timer went with the wait it belonged to: the queue ran dry without sleeping that long. */
  TORB_CHECK(torb_clock_now() - started < 1000000000);
  torb_task_release(target);
  torb_task_release(raced);
  torb_scheduler_finish();
}

TORB_TEST(within_of_a_task_somebody_else_cancelled_is_cancelled) {
  torb_task *target = slow(2.0);
  torb_task *raced = torb_task_within(target, 5000000000LL, &int_within);
  torb_task *stopper = canceller(target);
  int_or_timed_out answer;
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(target->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(raced->status, TORB_TASK_CANCELLED);
  TORB_CHECK(!torb_task_result(raced, &answer));
  torb_task_release(target);
  torb_task_release(raced);
  torb_task_release(stopper);
  torb_scheduler_finish();
}

/* ---------------------------------------------------------------------------------------------- channels --- */

typedef struct pinger_frame {
  torb_channel *ping;
  torb_channel *pong;
  int64_t round;
  torb_text out;
  torb_text in;
} pinger_frame;

static int64_t pinger_mismatches = 0;

static torb_poll pinger_resume(torb_task *task) {
  pinger_frame *frame = (pinger_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    case 2u:
      goto state_2;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  frame->round = 0;
  for (;;) {
    if (frame->round == 100) {
      break;
    }
    frame->out = torb_show_i64(frame->round);
    task->state = 1u;
    if (torb_channel_send(task, frame->ping, &frame->out) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  state_1:
    if (torb_task_cancelled(task)) {
      goto stop;
    }
    if (torb_task_outcome(task) != TORB_OUTCOME_READY) {
      pinger_mismatches += 1;
    }
    task->state = 2u;
    if (torb_channel_receive(task, frame->pong, &frame->in) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  state_2:
    if (torb_task_cancelled(task)) {
      goto stop;
    }
    if (torb_task_outcome(task) != TORB_OUTCOME_READY) {
      pinger_mismatches += 1;
      break;
    }
    {
      torb_text expected = torb_show_i64(frame->round);
      if (!torb_text_equal(expected, frame->in)) {
        pinger_mismatches += 1;
      }
      torb_text_release(expected);
    }
    torb_text_release(frame->in);
    frame->round += 1;
    if (torb_task_cancelled(task)) { /* the back-edge */
      goto stop;
    }
  }
  torb_channel_end(frame->ping);
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  *(int64_t *)torb_task_result_slot(task) = frame->round;
  return TORB_POLL_FINISHED;
stop:
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  return TORB_POLL_STOPPED;
}

typedef struct ponger_frame {
  torb_channel *ping;
  torb_channel *pong;
  int64_t rounds;
  torb_text item;
} ponger_frame;

static torb_poll ponger_resume(torb_task *task) {
  ponger_frame *frame = (ponger_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    case 2u:
      goto state_2;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  for (;;) {
    task->state = 1u;
    if (torb_channel_receive(task, frame->ping, &frame->item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  state_1:
    if (torb_task_cancelled(task)) {
      goto stop;
    }
    if (torb_task_outcome(task) == TORB_OUTCOME_CLOSED) {
      break;
    }
    task->state = 2u;
    if (torb_channel_send(task, frame->pong, &frame->item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  state_2:
    if (torb_task_cancelled(task)) {
      goto stop;
    }
    (void)torb_task_outcome(task);
    frame->rounds += 1;
    if (torb_task_cancelled(task)) { /* the back-edge */
      goto stop;
    }
  }
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  *(int64_t *)torb_task_result_slot(task) = frame->rounds;
  return TORB_POLL_FINISHED;
stop:
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  return TORB_POLL_STOPPED;
}

TORB_TEST(channel_ping_pong) {
  torb_channel *ping = torb_channel_new(0, &torb_element_text, torb_location_unknown);
  torb_channel *pong = torb_channel_new(0, &torb_element_text, torb_location_unknown);
  torb_task *pinger = torb_task_new(pinger_resume, sizeof(pinger_frame), &torb_element_int64);
  torb_task *ponger = torb_task_new(ponger_resume, sizeof(ponger_frame), &torb_element_int64);
  int64_t sent = 0;
  int64_t echoed = 0;
  pinger_mismatches = 0;
  torb_retain(ping);
  torb_retain(pong);
  ((pinger_frame *)torb_task_frame(pinger))->ping = ping;
  ((pinger_frame *)torb_task_frame(pinger))->pong = pong;
  ((ponger_frame *)torb_task_frame(ponger))->ping = ping;
  ((ponger_frame *)torb_task_frame(ponger))->pong = pong;
  torb_task_start(pinger);
  torb_task_start(ponger);
  torb_scheduler_run(NULL);
  TORB_CHECK(torb_task_result(pinger, &sent));
  TORB_CHECK(torb_task_result(ponger, &echoed));
  TORB_CHECK_INTEGER(sent, 100);
  TORB_CHECK_INTEGER(echoed, 100);
  TORB_CHECK_INTEGER(pinger_mismatches, 0);
  torb_task_release(pinger);
  torb_task_release(ponger);
  torb_scheduler_finish();
}

/* A writer into a channel of capacity 2, and a reader that takes one item and then closes the reading end. */
typedef struct writer_frame {
  torb_channel *channel;
  int32_t index;
  torb_text item;
} writer_frame;

static torb_poll writer_resume(torb_task *task) {
  writer_frame *frame = (writer_frame *)torb_task_frame(task);
  static const char *const names[] = { "a", "b", "c", "d" };
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 1u) {
    goto resumed;
  }
  for (frame->index = 0; frame->index < 4; frame->index += 1) {
    /* A fresh storage and not a literal, so a release that never happened is a leak the harness sees. */
    frame->item = torb_text_from_cstring(names[frame->index]);
    task->state = 1u;
    if (torb_channel_send(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  resumed:
    if (torb_task_cancelled(task)) {
      torb_channel_release(frame->channel);
      return TORB_POLL_STOPPED;
    }
    log_add(torb_task_outcome(task) == TORB_OUTCOME_READY ? "R" : "C");
  }
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

typedef struct closer_frame {
  torb_channel *channel;
  torb_text item;
} closer_frame;

static torb_poll closer_resume(torb_task *task) {
  closer_frame *frame = (closer_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    case 2u:
      goto state_2;
    case 3u:
      goto state_3;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  task->state = 1u;
  if (torb_channel_receive(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  if (torb_task_outcome(task) == TORB_OUTCOME_READY) {
    log_add("<");
    log_add(text_is(frame->item, "a") ? "a" : "?");
    torb_text_release(frame->item);
  }
  task->state = 2u;
  if (torb_task_pause(task) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_2:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  log_add("x");
  torb_channel_close(frame->channel);
  task->state = 3u;
  if (torb_channel_receive(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_3:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  if (torb_task_outcome(task) == TORB_OUTCOME_CLOSED) {
    log_add("-");
  }
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop:
  torb_channel_release(frame->channel);
  return TORB_POLL_STOPPED;
}

TORB_TEST(a_closed_channel_fails_its_writers_and_drops_its_items) {
  torb_channel *channel = torb_channel_new(2, &torb_element_text, torb_location_unknown);
  torb_task *writer = torb_task_new(writer_resume, sizeof(writer_frame), &torb_element_void);
  torb_task *closer = torb_task_new(closer_resume, sizeof(closer_frame), &torb_element_void);
  log_reset();
  ((writer_frame *)torb_task_frame(writer))->channel = channel;
  torb_retain(channel);
  ((closer_frame *)torb_task_frame(closer))->channel = channel;
  torb_task_start(writer);
  torb_task_start(closer);
  torb_scheduler_run(NULL);
  /*
   * a and b go into the buffer (R R), c waits for room; the reader takes a, which moves c in, and pauses; the writer
   * hears c was taken (R) and d waits; the reader closes (x), which drops b and c and fails d, and its next read is
   * the end (-); then the writer hears that d failed (C).
   */
  TORB_CHECK(strcmp(task_log, "RR<aRx-C") == 0);
  torb_task_release(writer);
  torb_task_release(closer);
  torb_scheduler_finish();
}

/* A writer that ends its stream: the reader gets everything offered before the end, then `None`. */
typedef struct draining_frame {
  torb_channel *channel;
  torb_text item;
} draining_frame;

static torb_poll draining_resume(torb_task *task) {
  draining_frame *frame = (draining_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  for (;;) {
    if (task->state == 0u) {
      task->state = 1u;
      if (torb_channel_receive(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
        return TORB_POLL_SUSPENDED;
      }
    }
    if (torb_task_cancelled(task)) {
      torb_channel_release(frame->channel);
      return TORB_POLL_STOPPED;
    }
    task->state = 0u;
    if (torb_task_outcome(task) == TORB_OUTCOME_CLOSED) {
      log_add("-");
      break;
    }
    log_add(text_is(frame->item, "7") ? "7" : "?");
    torb_text_release(frame->item);
  }
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

typedef struct ending_frame {
  torb_channel *channel;
  int32_t sent;
  torb_text item;
} ending_frame;

static torb_poll ending_resume(torb_task *task) {
  ending_frame *frame = (ending_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 1u) {
    goto resumed;
  }
  for (frame->sent = 0; frame->sent < 3; frame->sent += 1) {
    frame->item = torb_show_i64(7);
    task->state = 1u;
    if (torb_channel_send(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  resumed:
    if (torb_task_cancelled(task)) {
      torb_channel_release(frame->channel);
      return TORB_POLL_STOPPED;
    }
    (void)torb_task_outcome(task);
    log_add(">");
  }
  torb_channel_end(frame->channel);
  /* A send after the end fails at once, and its item is released. */
  frame->item = torb_show_i64(8);
  task->state = 2u;
  if (torb_channel_send(task, frame->channel, &frame->item) == TORB_WAIT_READY
      && torb_task_outcome(task) == TORB_OUTCOME_CLOSED) {
    log_add("C");
  }
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(an_ended_channel_delivers_what_was_sent_and_then_ends) {
  torb_channel *channel = torb_channel_new(0, &torb_element_text, torb_location_unknown);
  torb_task *reader = torb_task_new(draining_resume, sizeof(draining_frame), &torb_element_void);
  torb_task *writer = torb_task_new(ending_resume, sizeof(ending_frame), &torb_element_void);
  log_reset();
  ((draining_frame *)torb_task_frame(reader))->channel = channel;
  torb_retain(channel);
  ((ending_frame *)torb_task_frame(writer))->channel = channel;
  torb_task_start(reader);
  torb_task_start(writer);
  torb_scheduler_run(NULL);
  /*
   * A rendezvous: the first item goes to the waiting reader (>), the second waits for it; the reader logs the first,
   * takes the second from the waiting writer in the same turn and logs it too (77); the writer hears it was taken (>),
   * hands the third to the waiting reader (>), ends, and fails a send after the end (C); the reader logs the third and
   * reads the end (7-).
   */
  TORB_CHECK(strcmp(task_log, ">77>>C7-") == 0);
  torb_task_release(reader);
  torb_task_release(writer);
  torb_scheduler_finish();
}

/* Hands an item to a waiting receiver and cancels it in the same turn, before the receiver could take the item. */
typedef struct betraying_frame {
  torb_channel *channel;
  torb_task *target;
  torb_text item;
} betraying_frame;

static torb_poll betraying_resume(torb_task *task) {
  betraying_frame *frame = (betraying_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    torb_task_release(frame->target);
    return TORB_POLL_STOPPED;
  }
  frame->item = torb_show_i64(99);
  if (torb_channel_send(task, frame->channel, &frame->item) == TORB_WAIT_READY
      && torb_task_outcome(task) == TORB_OUTCOME_READY) {
    log_add("d");
  }
  torb_task_cancel(frame->target);
  torb_channel_release(frame->channel);
  torb_task_release(frame->target);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(an_item_delivered_to_a_task_that_stops_first_is_released) {
  torb_task *blocked = stuck();
  torb_task *betrayer;
  betraying_frame *frame;
  log_reset();
  torb_scheduler_run(NULL);
  betrayer = torb_task_new(betraying_resume, sizeof(betraying_frame), &torb_element_void);
  frame = (betraying_frame *)torb_task_frame(betrayer);
  frame->channel = ((stuck_frame *)torb_task_frame(blocked))->channel;
  torb_retain(frame->channel);
  frame->target = blocked;
  torb_retain(blocked);
  torb_task_start(betrayer);
  torb_scheduler_run(NULL);
  /* The item reached the receiver's slot, the receiver stopped at its check, and the runtime released the item. */
  TORB_CHECK(strcmp(task_log, "d") == 0);
  TORB_CHECK_INTEGER(blocked->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(blocked);
  torb_task_release(betrayer);
  torb_scheduler_finish();
}

TORB_TEST(a_negative_capacity_panics) {
  TORB_EXPECT_PANIC(torb_channel_new(-1, &torb_element_text, torb_location_unknown));
  TORB_CHECK_PANIC_CONTAINS("the capacity of a channel cannot be negative, and it is -1");
}

/* ----------------------------------------------------------------------------------------- many and the end --- */

typedef struct worker_frame {
  torb_text name;
} worker_frame;

static torb_poll worker_resume(torb_task *task) {
  worker_frame *frame = (worker_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_text_release(frame->name);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    if (torb_task_pause(task) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  (void)torb_task_outcome(task);
  *(int64_t *)torb_task_result_slot(task) = (int64_t)frame->name.length;
  torb_text_release(frame->name);
  return TORB_POLL_FINISHED;
}

#define MANY_TASKS 10000

typedef struct crowd_frame {
  torb_list children;
  int64_t index;
  int64_t sum;
} crowd_frame;

static const torb_element task_handle_element = {
  (uint32_t)sizeof(torb_task *), (uint32_t)TORB_ALIGN_OF(torb_task *), NULL, NULL, NULL, NULL
};

static torb_poll crowd_resume(torb_task *task) {
  crowd_frame *frame = (crowd_frame *)torb_task_frame(task);
  torb_task *child;
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  frame->children = torb_list_new(&task_handle_element);
  for (frame->index = 0; frame->index < MANY_TASKS; frame->index += 1) {
    child = torb_task_new(worker_resume, sizeof(worker_frame), &torb_element_int64);
    ((worker_frame *)torb_task_frame(child))->name = torb_show_i64(frame->index);
    torb_task_start(child);
    torb_list_add(&frame->children, &child);
  }
  frame->sum = 0;
  for (frame->index = 0; frame->index < MANY_TASKS; frame->index += 1) {
    child = *(torb_task *const *)torb_list_at(frame->children, frame->index, torb_location_unknown);
    task->state = 1u;
    if (torb_task_await(task, child) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  state_1:
    child = *(torb_task *const *)torb_list_at(frame->children, frame->index, torb_location_unknown);
    if (torb_task_cancelled(task)) {
      goto stop;
    }
    (void)torb_task_outcome(task);
    {
      int64_t length = 0;
      if (torb_task_result(child, &length)) {
        frame->sum += length;
      }
    }
  }
  for (frame->index = 0; frame->index < MANY_TASKS; frame->index += 1) {
    torb_task_release(*(torb_task *const *)torb_list_at(frame->children, frame->index, torb_location_unknown));
  }
  torb_list_release(frame->children);
  *(int64_t *)torb_task_result_slot(task) = frame->sum;
  return TORB_POLL_FINISHED;
stop:
  for (frame->index = 0; frame->index < MANY_TASKS; frame->index += 1) {
    torb_task_release(*(torb_task *const *)torb_list_at(frame->children, frame->index, torb_location_unknown));
  }
  torb_list_release(frame->children);
  return TORB_POLL_STOPPED;
}

TORB_TEST(ten_thousand_tasks_leave_nothing_behind) {
  torb_task *crowd = torb_task_new(crowd_resume, sizeof(crowd_frame), &torb_element_int64);
  int64_t sum = 0;
  torb_task_start(crowd);
  torb_scheduler_run(crowd);
  TORB_CHECK(torb_task_result(crowd, &sum));
  /* 10 one-digit names, 90 of two digits, 900 of three and 9000 of four. */
  TORB_CHECK_INTEGER(sum, 10 + 180 + 2700 + 36000);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(crowd);
  torb_scheduler_finish();
}

/* Offers an item nobody takes: the send consumed it, so a cancellation has to release it. */
typedef struct offering_frame {
  torb_channel *channel;
  torb_text item;
} offering_frame;

static torb_poll offering_resume(torb_task *task) {
  offering_frame *frame = (offering_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    torb_channel_release(frame->channel);
    return TORB_POLL_STOPPED;
  }
  if (task->state == 0u) {
    frame->item = torb_show_i64(271828);
    task->state = 1u;
    if (torb_channel_send(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  (void)torb_task_outcome(task);
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(the_end_of_the_program_stops_every_task) {
  torb_task *blocked = stuck();
  torb_task *offering = torb_task_new(offering_resume, sizeof(offering_frame), &torb_element_void);
  torb_task *sleeper;
  ((offering_frame *)torb_task_frame(offering))->channel = torb_channel_new(0, &torb_element_text,
                                                                            torb_location_unknown);
  torb_task_start(offering);
  sleeper = napping("z", 10.0);
  torb_task *quick = square(1);
  torb_instant started = torb_clock_now();
  log_reset();
  /* The three before it suspend: a receive, a send, and the task of a ten-second sleep. */
  torb_scheduler_run(quick);
  torb_task_release(quick);
  TORB_CHECK_INTEGER(torb_task_live_count(), 4);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  TORB_CHECK_INTEGER(blocked->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(offering->status, TORB_TASK_CANCELLED);
  torb_task_release(offering);
  TORB_CHECK_INTEGER(sleeper->status, TORB_TASK_CANCELLED);
  /* The timer went with the task: nothing waited ten seconds. */
  TORB_CHECK(torb_clock_now() - started < 1000000000);
  TORB_CHECK(strcmp(task_log, "s") == 0);
  torb_task_release(blocked);
  torb_task_release(sleeper);
}

TORB_TEST(a_deadlock_panics_instead_of_hanging) {
  torb_task *blocked = stuck();
  TORB_EXPECT_PANIC(torb_scheduler_run(blocked));
  TORB_CHECK_PANIC_CONTAINS("deadlock");
  torb_scheduler_finish();
  torb_task_release(blocked);
}

/* ------------------------------------------------------------------ the tasks `std/task` is written over --- */

TORB_TEST(completed_within_says_whether_the_deadline_came_first) {
  torb_task *late = slow(2.0);
  torb_task *early = slow(0.01);
  torb_task *first = torb_task_completed_within(late, 10000000);
  torb_task *second = torb_task_completed_within(early, 5000000000LL);
  torb_instant started = torb_clock_now();
  bool answer = true;
  log_reset();
  torb_scheduler_run(NULL);
  TORB_CHECK(torb_task_result(first, &answer));
  TORB_CHECK(!answer);
  TORB_CHECK_INTEGER(late->status, TORB_TASK_CANCELLED);
  TORB_CHECK(torb_task_result(second, &answer));
  TORB_CHECK(answer);
  TORB_CHECK_INTEGER(early->status, TORB_TASK_FINISHED);
  /* Only the early one wrote its "w", and nothing waited for the two seconds of the late one. */
  TORB_CHECK(strcmp(task_log, "w") == 0);
  TORB_CHECK(torb_clock_now() - started < 1000000000);
  torb_task_release(late);
  torb_task_release(early);
  torb_task_release(first);
  torb_task_release(second);
  torb_scheduler_finish();
}

TORB_TEST(offered_and_received_items_cross_a_channel_once) {
  torb_channel *channel = torb_channel_new(1, &torb_element_text, torb_location_unknown);
  torb_task *offers[3];
  torb_task *receipts[4];
  torb_text item;
  torb_text read;
  size_t index;
  for (index = 0u; index < 3u; index += 1u) {
    item = torb_show_i64((int64_t)(index + 1u) * 111);
    offers[index] = torb_channel_offered(channel, &item);
  }
  for (index = 0u; index < 3u; index += 1u) {
    receipts[index] = torb_channel_received(channel);
  }
  torb_scheduler_run(NULL);
  for (index = 0u; index < 3u; index += 1u) {
    TORB_CHECK_INTEGER(offers[index]->status, TORB_TASK_FINISHED);
    TORB_CHECK(torb_task_result(receipts[index], &read));
    TORB_CHECK_INTEGER(read.length, 3);
    torb_text_release(read);
  }
  /* After the end nothing will come: the receipt stops, which a source reads as `None`. */
  torb_channel_end(channel);
  receipts[3] = torb_channel_received(channel);
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(receipts[3]->status, TORB_TASK_CANCELLED);
  for (index = 0u; index < 3u; index += 1u) {
    torb_task_release(offers[index]);
  }
  for (index = 0u; index < 4u; index += 1u) {
    torb_task_release(receipts[index]);
  }
  torb_channel_release(channel);
  torb_scheduler_finish();
}

TORB_TEST(an_offer_to_a_closed_channel_stops_and_releases_its_item) {
  torb_channel *channel = torb_channel_new(0, &torb_element_text, torb_location_unknown);
  torb_text item = torb_show_i64(424242);
  torb_task *waiting = torb_channel_offered(channel, &item);
  torb_task *late;
  /* The first offer waits for a reader, and closing the reading end fails it. */
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(waiting->status, TORB_TASK_PENDING);
  torb_channel_close(channel);
  item = torb_show_i64(434343);
  late = torb_channel_offered(channel, &item);
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(waiting->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(late->status, TORB_TASK_CANCELLED);
  torb_task_release(waiting);
  torb_task_release(late);
  torb_channel_release(channel);
  torb_scheduler_finish();
}

/* ------------------------------------------------------------------------------------------------ the cascade --- */

/*
 * `await()` of a task that ended cancelled cancels the waiter (docs/design/CONCURRENCY.md section 8, "The cascade"): it
 * stops at the check after its wait, through its stop path - where the lowering releases the frame and runs the
 * `close()` of every `using` in it, which "u" stands for here - and ends cancelled, so whoever awaits *it* is cancelled
 * in turn, and whoever observes it with `result()` reads the cancellation.
 */
typedef struct cascading_frame {
  torb_task *awaited;
  torb_text held;
} cascading_frame;

static torb_poll cascading_resume(torb_task *task) {
  cascading_frame *frame = (cascading_frame *)torb_task_frame(task);
  switch (task->state) {
    case 0u:
      goto state_0;
    case 1u:
      goto state_1;
    default:
      TORB_UNREACHABLE();
  }
state_0:
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->awaited);
    return TORB_POLL_STOPPED;
  }
  frame->held = torb_show_i64(27182818);
  log_add("a");
  task->state = 1u;
  if (torb_task_await(task, frame->awaited) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop_1;
  }
  (void)torb_task_outcome(task);
  /* The line after the `await()`: never reached where the awaited task ended cancelled. */
  log_add("A");
  torb_text_release(frame->held);
  torb_task_release(frame->awaited);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop_1:
  log_add("u");
  torb_text_release(frame->held);
  torb_task_release(frame->awaited);
  return TORB_POLL_STOPPED;
}

/* `awaited` consumed: a task that awaits it the way `await()` does. */
static torb_task *cascading(torb_task *awaited) {
  torb_task *task = torb_task_new(cascading_resume, sizeof(cascading_frame), &torb_element_void);
  ((cascading_frame *)torb_task_frame(task))->awaited = awaited;
  torb_task_start(task);
  return task;
}

TORB_TEST(a_task_that_awaits_a_cancelled_task_is_cancelled_in_turn) {
  torb_task *child;
  torb_task *parent;
  torb_task *grandparent;
  torb_task *supervisor;
  torb_task *stopper;
  log_reset();
  child = stuck();
  torb_retain(child);
  parent = cascading(child);
  torb_retain(parent);
  grandparent = cascading(parent);
  torb_retain(grandparent);
  supervisor = awaiting(grandparent);
  awaited_finished = true;
  torb_scheduler_run(NULL);
  /* Both wait, nobody went on yet. */
  TORB_CHECK(strcmp(task_log, "aa") == 0);
  stopper = canceller(child);
  torb_scheduler_run(NULL);
  /* The child was cancelled, the parent stopped at its await and released its frame, and the grandparent after it. */
  TORB_CHECK(strcmp(task_log, "aakuu") == 0);
  TORB_CHECK_INTEGER(child->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(parent->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(grandparent->status, TORB_TASK_CANCELLED);
  /* The supervisor observed it with `result()`: it read the cancellation and finished itself. */
  TORB_CHECK(!awaited_finished);
  TORB_CHECK_INTEGER(supervisor->status, TORB_TASK_FINISHED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(child);
  torb_task_release(parent);
  torb_task_release(grandparent);
  torb_task_release(supervisor);
  torb_task_release(stopper);
  torb_scheduler_finish();
}

TORB_TEST(awaiting_a_task_that_already_ended_cancelled_stops_at_once) {
  torb_task *child = stuck();
  torb_task *parent;
  log_reset();
  torb_task_cancel(child);
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(child->status, TORB_TASK_CANCELLED);
  torb_retain(child);
  parent = cascading(child);
  torb_scheduler_run(NULL);
  /* The wait answered READY, and the check right after it stopped the parent before its next line. */
  TORB_CHECK(strcmp(task_log, "au") == 0);
  TORB_CHECK_INTEGER(parent->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(child);
  torb_task_release(parent);
  torb_scheduler_finish();
}

TORB_TEST(a_finished_task_does_not_cascade) {
  torb_task *parent;
  log_reset();
  parent = cascading(square(3));
  torb_scheduler_run(parent);
  TORB_CHECK(strcmp(task_log, "saA") == 0);
  TORB_CHECK_INTEGER(parent->status, TORB_TASK_FINISHED);
  torb_task_release(parent);
  torb_scheduler_finish();
}

/* A cascade cancels the children of the task it stops, as every cancellation does. */
typedef struct brood_frame {
  torb_task *awaited;
  torb_task *sibling;
} brood_frame;

static torb_task *brood_sibling = NULL;

static torb_poll brood_resume(torb_task *task) {
  brood_frame *frame = (brood_frame *)torb_task_frame(task);
  if (task->state == 0u) {
    if (torb_task_cancelled(task)) {
      torb_task_release(frame->awaited);
      return TORB_POLL_STOPPED;
    }
    /* A child of this task that would run forever: only the cascade's cancellation of this task ends it. */
    frame->sibling = stuck();
    torb_retain(frame->sibling);
    brood_sibling = frame->sibling;
    task->state = 1u;
    if (torb_task_await(task, frame->awaited) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->awaited);
    torb_task_release(frame->sibling);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  torb_task_release(frame->awaited);
  torb_task_release(frame->sibling);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

TORB_TEST(a_cascade_cancels_the_children_of_the_task_it_stops) {
  torb_task *awaited = stuck();
  torb_task *brood = torb_task_new(brood_resume, sizeof(brood_frame), &torb_element_void);
  torb_task *stopper;
  log_reset();
  torb_retain(awaited);
  ((brood_frame *)torb_task_frame(brood))->awaited = awaited;
  torb_task_start(brood);
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(brood_sibling->status, TORB_TASK_PENDING);
  stopper = canceller(awaited);
  torb_scheduler_run(NULL);
  TORB_CHECK_INTEGER(brood->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(brood_sibling->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  torb_task_release(brood_sibling);
  torb_task_release(awaited);
  torb_task_release(brood);
  torb_task_release(stopper);
  torb_scheduler_finish();
}

/* The generated `main`: 0 after a main task that finished, 130 after one that ended cancelled (and a line on stderr). */
TORB_TEST(the_end_of_a_cancelled_main_task_is_exit_code_130) {
  torb_task *finished = square(2);
  torb_task *blocked = stuck();
  torb_task *cancelled;
  torb_scheduler_run(finished);
  TORB_CHECK_INTEGER(torb_task_end_main(finished), 0);
  torb_retain(blocked);
  cancelled = cascading(blocked);
  torb_scheduler_run(NULL);
  torb_task_cancel(blocked);
  torb_scheduler_run(cancelled);
  TORB_CHECK_INTEGER(cancelled->status, TORB_TASK_CANCELLED);
  TORB_CHECK_INTEGER(torb_task_end_main(cancelled), TORB_EXIT_CANCELLED);
  torb_task_release(blocked);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
}

void torb_register_task_tests(void) {
  TORB_ADD(spawn_and_await);
  TORB_ADD(a_result_is_copied_for_every_reader);
  TORB_ADD(the_run_queue_is_first_in_first_out);
  TORB_ADD(a_loop_stops_at_its_back_edge);
  TORB_ADD(a_queued_loop_stops_when_it_is_resumed);
  TORB_ADD(cancelling_a_waiter_takes_it_off_the_waiter_list);
  TORB_ADD(a_cancelled_parent_cancels_its_children_before_their_first_line);
  TORB_ADD(dropping_the_handle_does_not_cancel);
  TORB_ADD(sleeps_end_in_the_order_of_their_deadlines);
  TORB_ADD(within_cancels_a_task_that_takes_too_long);
  TORB_ADD(within_passes_on_a_value_in_time);
  TORB_ADD(within_of_a_task_somebody_else_cancelled_is_cancelled);
  TORB_ADD(channel_ping_pong);
  TORB_ADD(a_closed_channel_fails_its_writers_and_drops_its_items);
  TORB_ADD(an_ended_channel_delivers_what_was_sent_and_then_ends);
  TORB_ADD(an_item_delivered_to_a_task_that_stops_first_is_released);
  TORB_ADD(a_negative_capacity_panics);
  TORB_ADD(ten_thousand_tasks_leave_nothing_behind);
  TORB_ADD(the_end_of_the_program_stops_every_task);
  TORB_ADD(a_deadlock_panics_instead_of_hanging);
  TORB_ADD(completed_within_says_whether_the_deadline_came_first);
  TORB_ADD(offered_and_received_items_cross_a_channel_once);
  TORB_ADD(an_offer_to_a_closed_channel_stops_and_releases_its_item);
  TORB_ADD(a_task_that_awaits_a_cancelled_task_is_cancelled_in_turn);
  TORB_ADD(awaiting_a_task_that_already_ended_cancelled_stops_at_once);
  TORB_ADD(a_finished_task_does_not_cascade);
  TORB_ADD(a_cascade_cancels_the_children_of_the_task_it_stops);
  TORB_ADD(the_end_of_a_cancelled_main_task_is_exit_code_130);
}
