/*
 * pool_test.c - the worker pool: tasks that may move run on several operating-system threads, an idle worker steals
 * unstarted ones, a channel carries items between two workers, a cancellation reaches a task on another worker, and a
 * result comes back from the worker that computed it - with the harness's leak check after every test, which is the sum
 * of the block counters of every worker.
 *
 * The machines are written by hand, the way torb_task.h tells the compiler to lower a function that answers a `Task`,
 * and started with `torb_task_start_portable` wherever their frames hold only what may cross a worker. Every test runs
 * with four workers and ends in `torb_scheduler_finish()`, which stops and joins the threads.
 */

#include "harness.h"

#define POOL_WORKERS 4u

static void pool_begin(void) {
  torb_pool_set_workers(POOL_WORKERS);
}

static void pool_end(void) {
  torb_scheduler_finish();
  torb_pool_set_workers(0u);
}

/* ------------------------------------------------------------------------------ work: a computation, no wait --- */

#define WORK_TASKS_MAXIMUM 10000

/* Where each task ran, one slot per task: every task writes its own, so no two threads write the same place. */
static uint32_t work_ran_on[WORK_TASKS_MAXIMUM];

static int64_t work_value(int64_t index, int64_t rounds) {
  uint64_t state = (uint64_t)index + 1u;
  int64_t round;
  for (round = 0; round < rounds; round += 1) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
  }
  return (int64_t)(state >> 33);
}

typedef struct work_frame {
  int64_t index;
  int64_t rounds;
} work_frame;

static torb_poll work_resume(torb_task *task) {
  work_frame *frame = (work_frame *)torb_task_frame(task);
  uint64_t state;
  int64_t round;
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  state = (uint64_t)frame->index + 1u;
  for (round = 0; round < frame->rounds; round += 1) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    if ((round & 1023) == 1023 && torb_task_cancelled(task)) { /* the back-edge */
      return TORB_POLL_STOPPED;
    }
  }
  work_ran_on[frame->index] = torb_worker_index();
  *(int64_t *)torb_task_result_slot(task) = (int64_t)(state >> 33);
  return TORB_POLL_FINISHED;
}

static torb_task *work(int64_t index, int64_t rounds) {
  torb_task *task = torb_task_new(work_resume, sizeof(work_frame), &torb_element_int64);
  work_frame *frame = (work_frame *)torb_task_frame(task);
  frame->index = index;
  frame->rounds = rounds;
  torb_task_start_portable(task);
  return task;
}

/* ----------------------------------------------------------- collect: awaits every task in order and sums them --- */

typedef struct collect_frame {
  torb_task **tasks;
  int64_t count;
  int64_t index;
  int64_t sum;
} collect_frame;

static void collect_free(collect_frame *frame) {
  int64_t index;
  for (index = frame->index; index < frame->count; index += 1) {
    torb_task_release(frame->tasks[index]);
  }
  torb_raw_free(frame->tasks, (size_t)frame->count * sizeof(torb_task *));
  frame->tasks = NULL;
}

static torb_poll collect_resume(torb_task *task) {
  collect_frame *frame = (collect_frame *)torb_task_frame(task);
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
  frame->index = 0;
next:
  if (frame->index >= frame->count) {
    collect_free(frame);
    *(int64_t *)torb_task_result_slot(task) = frame->sum;
    return TORB_POLL_FINISHED;
  }
  task->state = 1u;
  if (torb_task_await(task, frame->tasks[frame->index]) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  {
    int64_t value = 0;
    if (torb_task_result(frame->tasks[frame->index], &value)) {
      frame->sum += value;
    }
  }
  torb_task_release(frame->tasks[frame->index]);
  frame->index += 1;
  goto next;
stop:
  collect_free(frame);
  return TORB_POLL_STOPPED;
}

/* `tasks` consumed, an array from `torb_raw_allocate` of `count` handles; the collector is pinned where it starts. */
static torb_task *collect(torb_task **tasks, int64_t count) {
  torb_task *task = torb_task_new(collect_resume, sizeof(collect_frame), &torb_element_int64);
  collect_frame *frame = (collect_frame *)torb_task_frame(task);
  frame->tasks = tasks;
  frame->count = count;
  torb_task_start(task);
  return task;
}

static int64_t result_of(torb_task *task) {
  int64_t value = -1;
  if (!torb_task_result(task, &value)) {
    return -1;
  }
  return value;
}

/* Runs `count` works of `rounds` each on the pool, and answers the sum the collector read, or -1. */
static int64_t run_works(int64_t count, int64_t rounds) {
  torb_task **tasks = (torb_task **)torb_raw_allocate((size_t)count * sizeof(torb_task *));
  torb_task *collector;
  int64_t sum;
  int64_t index;
  for (index = 0; index < count; index += 1) {
    tasks[index] = work(index, rounds);
  }
  collector = collect(tasks, count);
  torb_scheduler_run(collector);
  sum = result_of(collector);
  torb_task_release(collector);
  return sum;
}

static int64_t expected_sum(int64_t count, int64_t rounds) {
  int64_t sum = 0;
  int64_t index;
  for (index = 0; index < count; index += 1) {
    sum += work_value(index, rounds);
  }
  return sum;
}

static uint32_t distinct_workers(int64_t count) {
  bool seen[POOL_WORKERS] = { false };
  uint32_t distinct = 0u;
  int64_t index;
  for (index = 0; index < count; index += 1) {
    uint32_t worker = work_ran_on[index];
    if (worker < POOL_WORKERS && !seen[worker]) {
      seen[worker] = true;
      distinct += 1u;
    }
  }
  return distinct;
}

/**
 * Sixty-four computations of a few milliseconds each, started from the main thread: the three idle workers take them
 * from the main worker's queue, so they run on more than one thread, and the collector reads every value in input order.
 */
TORB_TEST(portable_tasks_run_on_several_workers) {
  const int64_t rounds = 2000000;
  torb_pool_statistics before = torb_pool_statistics_now();
  torb_pool_statistics after;
  int64_t sum;
  pool_begin();
  memset(work_ran_on, 0xFF, sizeof work_ran_on);
  sum = run_works(64, rounds);
  after = torb_pool_statistics_now();
  pool_end();
  TORB_CHECK_INTEGER(sum, expected_sum(64, rounds));
  TORB_CHECK(distinct_workers(64) > 1u);
  TORB_CHECK(after.stolen > before.stolen);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
}

/** Ten thousand small tasks over the workers, three times over: every value arrives and nothing is left behind. */
TORB_TEST(ten_thousand_tasks_over_the_workers) {
  int64_t attempt;
  for (attempt = 0; attempt < 3; attempt += 1) {
    int64_t sum;
    pool_begin();
    sum = run_works(WORK_TASKS_MAXIMUM, 200);
    pool_end();
    TORB_CHECK_INTEGER(sum, expected_sum(WORK_TASKS_MAXIMUM, 200));
    TORB_CHECK_INTEGER(torb_task_live_count(), 0);
  }
}

/** With one worker the pool starts no thread: every task runs on the main thread, in the order it was started. */
TORB_TEST(one_worker_runs_everything_on_the_main_thread) {
  int64_t sum;
  torb_pool_set_workers(1u);
  memset(work_ran_on, 0xFF, sizeof work_ran_on);
  sum = run_works(32, 1000);
  torb_scheduler_finish();
  torb_pool_set_workers(0u);
  TORB_CHECK_INTEGER(sum, expected_sum(32, 1000));
  TORB_CHECK_INTEGER(distinct_workers(32), 1u);
  TORB_CHECK_INTEGER(work_ran_on[0], 0u);
}

/* ------------------------------------------------------ ping and pong: a channel between two workers --- */

#define PING_ROUNDS 2000

static uint32_t pinger_ran_on = 0u;
static uint32_t ponger_ran_on = 0u;
static int64_t pinger_mistakes = 0;

typedef struct pinger_frame {
  torb_channel *ping;
  torb_channel *pong;
  int64_t round;
  int64_t out;
  int64_t in;
} pinger_frame;

/* Spins a while first, so that the ponger is taken by another worker before the first ball is served. */
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
  pinger_ran_on = torb_worker_index();
  (void)work_value(1, 20000000);
  frame->round = 0;
serve:
  if (frame->round >= PING_ROUNDS) {
    torb_channel_end(frame->ping);
    torb_channel_release(frame->ping);
    torb_channel_release(frame->pong);
    *(torb_void *)torb_task_result_slot(task) = 0u;
    return TORB_POLL_FINISHED;
  }
  frame->out = frame->round;
  task->state = 1u;
  if (torb_channel_send(task, frame->ping, &frame->out) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  task->state = 2u;
  if (torb_channel_receive(task, frame->pong, &frame->in) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_2:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  if (torb_task_outcome(task) != TORB_OUTCOME_READY || frame->in != frame->round + 1) {
    pinger_mistakes += 1;
  }
  frame->round += 1;
  goto serve;
stop:
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  return TORB_POLL_STOPPED;
}

typedef struct ponger_frame {
  torb_channel *ping;
  torb_channel *pong;
  int64_t in;
  int64_t out;
  int64_t rounds;
} ponger_frame;

/* Answers every ping with its successor until the pings end; its value is how many it answered. */
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
  ponger_ran_on = torb_worker_index();
listen:
  task->state = 1u;
  if (torb_channel_receive(task, frame->ping, &frame->in) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  if (torb_task_outcome(task) != TORB_OUTCOME_READY) {
    torb_channel_release(frame->ping);
    torb_channel_release(frame->pong);
    *(int64_t *)torb_task_result_slot(task) = frame->rounds;
    return TORB_POLL_FINISHED;
  }
  frame->out = frame->in + 1;
  task->state = 2u;
  if (torb_channel_send(task, frame->pong, &frame->out) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_2:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  frame->rounds += 1;
  goto listen;
stop:
  torb_channel_release(frame->ping);
  torb_channel_release(frame->pong);
  return TORB_POLL_STOPPED;
}

/**
 * Two thousand rounds of ping and pong over two rendezvous channels of `Int64`, the two players taken by two different
 * workers: every item crosses a worker, every answer is the successor of its question, and the counts of the two channel
 * blocks - changed atomically from both threads - come out at zero.
 */
TORB_TEST(a_channel_carries_items_between_two_workers) {
  torb_channel *ping;
  torb_channel *pong;
  torb_task *pinger;
  torb_task *ponger;
  pool_begin();
  pinger_mistakes = 0;
  ping = torb_channel_new(0, &torb_element_int64, torb_location_unknown);
  pong = torb_channel_new(0, &torb_element_int64, torb_location_unknown);
  pinger = torb_task_new(pinger_resume, sizeof(pinger_frame), &torb_element_void);
  torb_retain(ping);
  torb_retain(pong);
  ((pinger_frame *)torb_task_frame(pinger))->ping = ping;
  ((pinger_frame *)torb_task_frame(pinger))->pong = pong;
  torb_task_start_portable(pinger);
  ponger = torb_task_new(ponger_resume, sizeof(ponger_frame), &torb_element_int64);
  ((ponger_frame *)torb_task_frame(ponger))->ping = ping;
  ((ponger_frame *)torb_task_frame(ponger))->pong = pong;
  torb_task_start_portable(ponger);
  torb_scheduler_run(ponger);
  torb_scheduler_run(pinger);
  TORB_CHECK_INTEGER(result_of(ponger), PING_ROUNDS);
  TORB_CHECK_INTEGER(pinger_mistakes, 0);
  TORB_CHECK(pinger_ran_on != ponger_ran_on);
  torb_task_release(pinger);
  torb_task_release(ponger);
  pool_end();
}

/* -------------------------------------------------------------------------- cancelling across workers --- */

static uint32_t spinner_started = 0u;
static uint32_t spinner_ran_on = 0u;

typedef struct spinner_frame {
  torb_text held;
  uint64_t turns;
} spinner_frame;

/* A loop that never waits and never ends on its own: only the check at its back-edge stops it. */
static torb_poll spinner_resume(torb_task *task) {
  spinner_frame *frame = (spinner_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  spinner_ran_on = torb_worker_index();
  frame->held = torb_show_i64(424242);
  __atomic_store_n(&spinner_started, 1u, __ATOMIC_RELEASE);
  for (;;) {
    if (torb_task_cancelled(task)) { /* the back-edge */
      torb_text_release(frame->held);
      return TORB_POLL_STOPPED;
    }
    frame->turns += 1u;
  }
}

/*
 * The main thread waits for a flag without running the scheduler, so the task it waits for can only have been taken by
 * another worker. Ten seconds is the end of any patience a test has.
 */
static bool wait_for_flag(uint32_t *flag) {
  int64_t waited;
  for (waited = 0; waited < 10000; waited += 1) {
    if (__atomic_load_n(flag, __ATOMIC_ACQUIRE) != 0u) {
      return true;
    }
    torb_platform_sleep(1000000);
  }
  return false;
}

/**
 * A loop on another worker is cancelled from the main thread: the flag is set on one thread and read at the back-edge
 * on another, the loop releases what it held, and the task ends as cancelled.
 */
TORB_TEST(a_cancellation_stops_a_loop_on_another_worker) {
  torb_task *spinner;
  bool started;
  pool_begin();
  __atomic_store_n(&spinner_started, 0u, __ATOMIC_RELEASE);
  spinner = torb_task_new(spinner_resume, sizeof(spinner_frame), &torb_element_void);
  torb_task_start_portable(spinner);
  started = wait_for_flag(&spinner_started);
  torb_task_cancel(spinner);
  torb_scheduler_run(spinner);
  TORB_CHECK(started);
  TORB_CHECK(spinner_ran_on != 0u);
  TORB_CHECK_INTEGER(spinner->status, TORB_TASK_CANCELLED);
  torb_task_release(spinner);
  pool_end();
}

static uint32_t listener_waiting = 0u;

typedef struct listener_frame {
  torb_channel *channel;
  int64_t item;
} listener_frame;

/* Waits for an item that never comes. */
static torb_poll listener_resume(torb_task *task) {
  listener_frame *frame = (listener_frame *)torb_task_frame(task);
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
  if (torb_channel_receive(task, frame->channel, &frame->item) == TORB_WAIT_SUSPENDED) {
    __atomic_store_n(&listener_waiting, 1u, __ATOMIC_RELEASE);
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    goto stop;
  }
  (void)torb_task_outcome(task);
  torb_channel_release(frame->channel);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
stop:
  torb_channel_release(frame->channel);
  return TORB_POLL_STOPPED;
}

/**
 * A task that waits on a channel on another worker is cancelled from the main thread: its own worker takes it out of the
 * channel's queue when it takes it, and the reference the wait held on the channel is released there.
 */
TORB_TEST(a_cancellation_reaches_a_waiter_on_another_worker) {
  torb_channel *channel;
  torb_task *listener;
  bool waiting;
  pool_begin();
  __atomic_store_n(&listener_waiting, 0u, __ATOMIC_RELEASE);
  channel = torb_channel_new(0, &torb_element_int64, torb_location_unknown);
  listener = torb_task_new(listener_resume, sizeof(listener_frame), &torb_element_void);
  ((listener_frame *)torb_task_frame(listener))->channel = channel;
  torb_task_start_portable(listener);
  waiting = wait_for_flag(&listener_waiting);
  torb_task_cancel(listener);
  torb_scheduler_run(listener);
  TORB_CHECK(waiting);
  TORB_CHECK_INTEGER(listener->status, TORB_TASK_CANCELLED);
  torb_task_release(listener);
  pool_end();
}

/* ------------------------------------------------------------------- a parent and children on four workers --- */

typedef struct brood_frame {
  torb_task *children[8];
  int64_t count;
} brood_frame;

/* Starts eight spinners that may move, then waits for the first: cancelling it must reach all eight. */
static torb_poll brood_resume(torb_task *task) {
  brood_frame *frame = (brood_frame *)torb_task_frame(task);
  int64_t index;
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
  for (index = 0; index < 8; index += 1) {
    frame->children[index] = torb_task_new(spinner_resume, sizeof(spinner_frame), &torb_element_void);
    torb_task_start_portable(frame->children[index]);
  }
  frame->count = 8;
  task->state = 1u;
  if (torb_task_await(task, frame->children[0]) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  for (index = 0; index < frame->count; index += 1) {
    torb_task_release(frame->children[index]);
  }
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

/**
 * A parent on one worker with eight spinning children spread over the others: cancelling the parent from the main
 * thread stops every child at its back-edge, wherever it runs, and every frame is released.
 */
TORB_TEST(cancelling_a_parent_stops_its_children_on_every_worker) {
  torb_task *brood;
  bool started;
  pool_begin();
  __atomic_store_n(&spinner_started, 0u, __ATOMIC_RELEASE);
  brood = torb_task_new(brood_resume, sizeof(brood_frame), &torb_element_void);
  torb_task_start_portable(brood);
  started = wait_for_flag(&spinner_started);
  torb_task_cancel(brood);
  torb_scheduler_run(brood);
  TORB_CHECK(started);
  TORB_CHECK_INTEGER(brood->status, TORB_TASK_CANCELLED);
  torb_task_release(brood);
  pool_end();
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
}

/* ----------------------------------------------------------------- a counted result comes back from a worker --- */

typedef struct speaker_frame {
  int64_t number;
} speaker_frame;

/* Builds a text of its own, on whichever worker runs it: a counted value, made in that worker's heap. */
static torb_poll speaker_resume(torb_task *task) {
  speaker_frame *frame = (speaker_frame *)torb_task_frame(task);
  torb_text shown;
  torb_text parts[2];
  torb_text twice;
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  (void)work_value(frame->number, 100000);
  shown = torb_show_i64(frame->number);
  parts[0] = shown;
  parts[1] = shown;
  twice = torb_text_concat(parts, 2u);
  torb_text_release(shown);
  *(torb_text *)torb_task_result_slot(task) = twice;
  return TORB_POLL_FINISHED;
}

/**
 * Thirty-two tasks each build a `String` on the worker that runs them; the main thread reads every one, in order, and
 * releases the handles - so the last release of each task block and of its counted result is on the thread that held
 * the handle, and the counters of the heaps balance in their sum.
 */
TORB_TEST(a_counted_result_comes_back_from_the_worker_that_made_it) {
  torb_task *speakers[32];
  int64_t index;
  bool right = true;
  pool_begin();
  for (index = 0; index < 32; index += 1) {
    torb_task *task = torb_task_new(speaker_resume, sizeof(speaker_frame), &torb_element_text);
    ((speaker_frame *)torb_task_frame(task))->number = 1000 + index;
    torb_task_start_portable(task);
    speakers[index] = task;
  }
  for (index = 0; index < 32; index += 1) {
    torb_text value = torb_text_empty();
    char expected[32];
    torb_scheduler_run(speakers[index]);
    snprintf(expected, sizeof expected, "%lld%lld", (long long)(1000 + index), (long long)(1000 + index));
    if (!torb_task_result(speakers[index], &value) || value.length != strlen(expected)
        || memcmp(value.storage->data + value.offset, expected, value.length) != 0) {
      right = false;
    }
    torb_text_release(value);
    torb_task_release(speakers[index]);
  }
  pool_end();
  TORB_CHECK(right);
}

void torb_register_pool_tests(void) {
  TORB_ADD(portable_tasks_run_on_several_workers);
  TORB_ADD(ten_thousand_tasks_over_the_workers);
  TORB_ADD(one_worker_runs_everything_on_the_main_thread);
  TORB_ADD(a_channel_carries_items_between_two_workers);
  TORB_ADD(a_cancellation_stops_a_loop_on_another_worker);
  TORB_ADD(a_cancellation_reaches_a_waiter_on_another_worker);
  TORB_ADD(cancelling_a_parent_stops_its_children_on_every_worker);
  TORB_ADD(a_counted_result_comes_back_from_the_worker_that_made_it);
}
