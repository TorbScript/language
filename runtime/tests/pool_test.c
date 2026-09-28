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
#include "torb_pool.h"

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
  /* Kept in the frame, because an optimizer drops a computation whose value nobody reads, and with it the spin */
  frame->in = work_value(1, 20000000);
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

/*
 * One game of two thousand rounds: answers whether every answer was right, and the ponger's count of rounds. Which
 * workers took the two players is the machine's: `pinger_ran_on` and `ponger_ran_on` say it afterwards.
 */
static bool play_ping_pong(int64_t *rounds) {
  torb_channel *ping;
  torb_channel *pong;
  torb_task *pinger;
  torb_task *ponger;
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
  *rounds = result_of(ponger);
  torb_task_release(pinger);
  torb_task_release(ponger);
  return pinger_mistakes == 0;
}

/**
 * Two thousand rounds of ping and pong over two rendezvous channels of `Int64`, the two players taken by two different
 * workers: every item crosses a worker, every answer is the successor of its question, and the counts of the two channel
 * blocks - changed atomically from both threads - come out at zero. Idle workers take the players, so on a machine
 * that is busy with something else both may land on one; the game is played again until they do not, and every game
 * has to be right either way.
 */
TORB_TEST(a_channel_carries_items_between_two_workers) {
  int64_t attempt;
  bool crossed = false;
  pool_begin();
  for (attempt = 0; attempt < 50 && !crossed; attempt += 1) {
    int64_t rounds = -1;
    bool right = play_ping_pong(&rounds);
    if (!right || rounds != PING_ROUNDS) {
      pool_end();
      TORB_CHECK(right);
      TORB_CHECK_INTEGER(rounds, PING_ROUNDS);
    }
    crossed = pinger_ran_on != ponger_ran_on;
  }
  pool_end();
  TORB_CHECK(crossed);
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
  /* Several spinners may start at once on several workers */
  __atomic_store_n(&spinner_ran_on, torb_worker_index(), __ATOMIC_RELAXED);
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
  TORB_CHECK(__atomic_load_n(&spinner_ran_on, __ATOMIC_RELAXED) != 0u);
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
    char expected[48];
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

/* ------------------------------------------------------------------ a panic in a task that belongs to a test --- */

/* A few milliseconds of work, then a panic - where the task runs on another worker than the main thread, or always. */
typedef struct panicky_frame {
  int64_t rounds;
  bool always;
} panicky_frame;

static torb_poll panicky_resume(torb_task *task) {
  panicky_frame *frame = (panicky_frame *)torb_task_frame(task);
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  if (frame->always || torb_worker_index() != 0u) {
    torb_panic_text("a task of the test panicked", torb_location_unknown);
  }
  *(int64_t *)torb_task_result_slot(task) = work_value(0, frame->rounds);
  return TORB_POLL_FINISHED;
}

static void panicky(int64_t rounds, bool always, bool portable) {
  torb_task *task = torb_task_new(panicky_resume, sizeof(panicky_frame), &torb_element_int64);
  panicky_frame *frame = (panicky_frame *)torb_task_frame(task);
  frame->rounds = rounds;
  frame->always = always;
  if (portable) {
    torb_task_start_portable(task);
  } else {
    torb_task_start(task);
  }
  torb_task_release(task);
}

/**
 * Sixty-four tasks started inside a test, each of which panics on any worker but the main thread: the panic lands in
 * the recovery point of the worker that runs it instead of ending the process, it is the test's failure with its
 * message, and the test's other tasks are cancelled and stop. The pool is whole afterwards: the next test's tasks run.
 */
TORB_TEST(a_panic_in_a_task_on_another_worker_fails_its_test) {
  torb_recovery failure;
  bool failed;
  bool second_failed;
  int64_t index;
  pool_begin();
  TORB_CHECK(torb_test_tasks_begin());
  for (index = 0; index < 64; index += 1) {
    panicky(2000000, false, true);
  }
  failed = torb_test_tasks_end(false, &failure);
  TORB_CHECK(torb_task_live_count() == 0u);
  TORB_CHECK(torb_test_tasks_begin());
  for (index = 0; index < 8; index += 1) {
    torb_task_release(work(index, 1000));
  }
  second_failed = torb_test_tasks_end(false, NULL);
  pool_end();
  TORB_CHECK(failed);
  TORB_CHECK(strcmp(failure.message, "a task of the test panicked") == 0);
  TORB_CHECK(!second_failed);
}

/** The same on the main thread, with one worker: the task that panics is resumed under its own recovery point too. */
TORB_TEST(a_panic_in_a_task_on_the_main_thread_fails_its_test) {
  torb_recovery failure;
  bool failed;
  torb_pool_set_workers(1u);
  TORB_CHECK(torb_test_tasks_begin());
  panicky(10, false, false);
  panicky(10, true, false);
  panicky(10, false, false);
  failed = torb_test_tasks_end(false, &failure);
  TORB_CHECK(torb_task_live_count() == 0u);
  pool_end();
  TORB_CHECK(failed);
  TORB_CHECK(strcmp(failure.message, "a task of the test panicked") == 0);
}

/**
 * A test ends only once the tasks it started have completed, whichever worker ran them; a task the test's task makes
 * belongs to the test as well. A test that nothing panicked in is not failed, and a second scope inside the first opens
 * nothing.
 */
TORB_TEST(a_test_waits_for_the_tasks_it_started) {
  torb_task *tasks[16];
  torb_task *collector;
  int64_t index;
  bool complete = true;
  bool failed;
  pool_begin();
  TORB_CHECK(torb_test_tasks_begin());
  TORB_CHECK(!torb_test_tasks_begin());
  for (index = 0; index < 16; index += 1) {
    tasks[index] = work(index, 200000);
  }
  {
    torb_task **handed = (torb_task **)torb_raw_allocate(16u * sizeof(torb_task *));
    for (index = 0; index < 16; index += 1) {
      torb_retain(tasks[index]);
      handed[index] = tasks[index];
    }
    collector = collect(handed, 16);
  }
  failed = torb_test_tasks_end(false, NULL);
  for (index = 0; index < 16; index += 1) {
    complete = complete && torb_task_is_complete(tasks[index]);
    torb_task_release(tasks[index]);
  }
  complete = complete && torb_task_is_complete(collector);
  TORB_CHECK_INTEGER(result_of(collector), expected_sum(16, 200000));
  torb_task_release(collector);
  pool_end();
  TORB_CHECK(!failed);
  TORB_CHECK(complete);
}

/* ---------------------------------------------------------------------------------- the copy at the crossing --- */

static uint32_t count_of(const void *block) {
  return ((const torb_header *)block)->count;
}

/** A text nobody else holds is private already; one somebody else holds too is copied, and the original keeps its count. */
TORB_TEST(a_text_is_copied_only_where_somebody_else_holds_it) {
  torb_text text = torb_text_from_cstring("a text of some length");
  torb_text other;
  torb_bytes *before = text.storage;
  uint64_t copied = torb_pool_statistics_now().copied;
  TORB_CHECK(torb_text_privatize(&text));
  TORB_CHECK(text.storage == before);
  TORB_CHECK(torb_pool_statistics_now().copied == copied);
  other = torb_text_retained(text);
  TORB_CHECK(torb_text_privatize(&other));
  TORB_CHECK(other.storage != before);
  TORB_CHECK_INTEGER(count_of(before), 1);
  TORB_CHECK_INTEGER(count_of(other.storage), 1);
  TORB_CHECK(torb_text_equal(text, other));
  TORB_CHECK(torb_pool_statistics_now().copied == copied + 1u);
  torb_text_release(text);
  torb_text_release(other);
}

/**
 * A list of texts that is held twice: the copy gets a storage of its own and a copy of every text, and the first list
 * is left exactly as it was - every one of its texts back at a count of 1.
 */
TORB_TEST(a_list_of_texts_held_twice_is_copied_down_to_every_text) {
  torb_list first = torb_list_new(&torb_element_text);
  torb_list second;
  int64_t index;
  bool apart = true;
  for (index = 0; index < 3; index += 1) {
    torb_text text = torb_show_i64(1000 + index);
    torb_list_add(&first, &text);
  }
  second = torb_list_retained(first);
  TORB_CHECK(torb_list_privatize(&second, torb_text_privatize_place));
  TORB_CHECK(second.storage != first.storage);
  TORB_CHECK_INTEGER(count_of(first.storage), 1);
  for (index = 0; index < 3; index += 1) {
    const torb_text *mine = (const torb_text *)torb_list_at(first, index, torb_location_unknown);
    const torb_text *theirs = (const torb_text *)torb_list_at(second, index, torb_location_unknown);
    apart = apart && mine->storage != theirs->storage && count_of(mine->storage) == 1u
            && count_of(theirs->storage) == 1u && torb_text_equal(*mine, *theirs);
  }
  TORB_CHECK(apart);
  torb_list_release(first);
  torb_list_release(second);
}

/** A map of texts held twice: the copy's keys and values are its own, and its plain side is left alone. */
TORB_TEST(a_map_held_twice_is_copied_down_to_every_key) {
  torb_map first = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_map second;
  torb_text key = torb_text_from_cstring("seven");
  int64_t value = 7;
  const void *found_key;
  const void *found_value;
  uint32_t cursor = 0u;
  torb_map_set(&first, &key, &value);
  second = torb_map_retained(first);
  TORB_CHECK(torb_map_privatize(&second, torb_text_privatize_place, NULL));
  TORB_CHECK(second.storage != first.storage);
  TORB_CHECK(torb_map_next(second, &cursor, &found_key, &found_value));
  TORB_CHECK(count_of(((const torb_text *)found_key)->storage) == 1u);
  TORB_CHECK_INTEGER(*(const int64_t *)found_value, 7);
  TORB_CHECK(torb_map_contains(second, &key));
  torb_map_release(first);
  torb_map_release(second);
}

/* The environment of a closure over one text, and the drop and the copy the emitter would write for it (`PE_<layout>`). */
typedef struct text_environment {
  torb_header header;
  torb_drop_function drop;
  torb_environment_copy privatize;
  torb_text text;
} text_environment;

static void text_environment_drop(void *block) {
  torb_text_release(((text_environment *)block)->text);
}

static torb_environment *text_environment_copy(torb_environment *environment) {
  text_environment *value = (text_environment *)environment;
  if (environment->header.count != 1u) {
    text_environment *copy = (text_environment *)torb_allocate(sizeof(text_environment), TORB_BLOCK_ENVIRONMENT);
    torb_header header = copy->header;
    *copy = *value;
    copy->header = header;
    copy->text = torb_text_retained(copy->text);
    value = copy;
  }
  if (!torb_text_privatize(&value->text)) {
    return NULL;
  }
  return (torb_environment *)value;
}

static text_environment *text_environment_new(torb_text text, torb_environment_copy copy) {
  text_environment *made = (text_environment *)torb_allocate(sizeof(text_environment), TORB_BLOCK_ENVIRONMENT);
  made->drop = text_environment_drop;
  made->privatize = copy;
  made->text = text;
  return made;
}

/**
 * An environment only its closure holds is kept and its capture made private in place; one held twice is copied, and
 * the original keeps what it held. One without a copy - a closure over a captured `var` - is never copied.
 */
TORB_TEST(an_environment_is_copied_only_where_somebody_else_holds_it) {
  torb_text text = torb_text_from_cstring("a capture of some length");
  torb_text held = torb_text_retained(text);
  text_environment *alone = text_environment_new(text, text_environment_copy);
  torb_environment *place = (torb_environment *)alone;
  text_environment *shared;
  torb_environment *other;
  uint64_t copied = torb_pool_statistics_now().copied;
  /* The environment is the closure's alone, its text is not: the text is copied, the block kept */
  TORB_CHECK(torb_closure_privatize(&place));
  TORB_CHECK(place == (torb_environment *)alone);
  TORB_CHECK(alone->text.storage != held.storage);
  TORB_CHECK_INTEGER(count_of(held.storage), 1);
  /* Held twice: a block of its own, and the first one still holds its text */
  torb_retain(alone);
  other = (torb_environment *)alone;
  TORB_CHECK(torb_closure_privatize(&other));
  TORB_CHECK(other != (torb_environment *)alone);
  TORB_CHECK_INTEGER(count_of(alone), 1);
  TORB_CHECK_INTEGER(count_of(other), 1);
  TORB_CHECK(torb_text_equal(((text_environment *)other)->text, held));
  TORB_CHECK(torb_pool_statistics_now().copied > copied);
  /* Without a copy the closure stays where it is */
  shared = text_environment_new(torb_text_retained(held), NULL);
  torb_retain(shared);
  place = (torb_environment *)shared;
  TORB_CHECK(!torb_closure_privatize(&place));
  TORB_CHECK(place == (torb_environment *)shared);
  torb_environment_release((torb_environment *)shared);
  torb_environment_release((torb_environment *)shared);
  torb_environment_release((torb_environment *)alone);
  torb_environment_release(other);
  torb_text_release(held);
}

/* A task that adds up the lengths of the texts of its list. */
typedef struct lengths_frame {
  torb_list texts;
} lengths_frame;

static torb_poll lengths_resume(torb_task *task) {
  lengths_frame *frame = (lengths_frame *)torb_task_frame(task);
  int64_t total = 0;
  int64_t index;
  if (torb_task_cancelled(task)) {
    torb_list_release(frame->texts);
    return TORB_POLL_STOPPED;
  }
  for (index = 0; index < torb_list_length(frame->texts); index += 1) {
    /* A text the task keeps a while: its count changes on this thread */
    torb_text text = torb_text_retained(*(const torb_text *)torb_list_at(frame->texts, index, torb_location_unknown));
    total += (int64_t)text.length;
    torb_text_release(text);
  }
  torb_list_release(frame->texts);
  *(int64_t *)torb_task_result_slot(task) = total;
  return TORB_POLL_FINISHED;
}

/**
 * Thirty-two tasks over lists of texts the main thread still holds: the test of the runtime refuses each frame, the
 * copy makes it private, and the tasks run on several workers - reading texts nobody else can touch - while the lists
 * of the main thread stay what they were. Every block is freed, whichever heap made it.
 */
TORB_TEST(frames_of_texts_somebody_else_holds_cross_after_the_copy) {
  torb_list lists[32];
  torb_task *tasks[32];
  int64_t index;
  int64_t expected = 0;
  int64_t sum = 0;
  uint64_t copied = torb_pool_statistics_now().copied;
  pool_begin();
  for (index = 0; index < 32; index += 1) {
    int64_t item;
    lists[index] = torb_list_new(&torb_element_text);
    for (item = 0; item <= index; item += 1) {
      torb_text text = torb_show_i64(100000 + item);
      expected += (int64_t)text.length;
      torb_list_add(&lists[index], &text);
    }
  }
  for (index = 0; index < 32; index += 1) {
    torb_task *task = torb_task_new(lengths_resume, sizeof(lengths_frame), &torb_element_int64);
    lengths_frame *frame = (lengths_frame *)torb_task_frame(task);
    frame->texts = torb_list_retained(lists[index]);
    TORB_CHECK(!torb_list_may_move(frame->texts, true));
    TORB_CHECK(torb_task_copies() && torb_list_privatize(&frame->texts, torb_text_privatize_place));
    torb_task_start_portable(task);
    tasks[index] = task;
  }
  for (index = 0; index < 32; index += 1) {
    torb_scheduler_run(tasks[index]);
    sum += result_of(tasks[index]);
    torb_task_release(tasks[index]);
    TORB_CHECK_INTEGER(count_of(lists[index].storage), 1);
    torb_list_release(lists[index]);
  }
  pool_end();
  TORB_CHECK_INTEGER(sum, expected);
  TORB_CHECK(torb_pool_statistics_now().copied >= copied + 32u * 2u);
}

/* ------------------------------------------------------------------------------------------ the blocking pool --- */

/* The body asks, the task on the worker answers: one flag each way, written by one thread and read by the other. */
static uint32_t blocking_asked = 0u;
static uint32_t blocking_answered = 0u;

typedef struct offloaded_frame {
  torb_task *turn;
  /** Whether the body blocks until the task on the worker answers. */
  bool waits;
} offloaded_frame;

/*
 * `offload` as the lowering writes it: the turn, its await, and then the body. The body blocks - it sleeps until the
 * other task answers, for at most five seconds - and the task answers 1 where it ran on the blocking pool and was
 * answered, 2 where it ran on its worker, 0 where it was never answered.
 */
static torb_poll offloaded_resume(torb_task *task) {
  offloaded_frame *frame = (offloaded_frame *)torb_task_frame(task);
  int64_t waited = 0;
  bool on_pool;
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
  frame->turn = torb_blocking_turn();
  task->state = 1u;
  if (torb_task_await(task, frame->turn) == TORB_WAIT_SUSPENDED) {
    return TORB_POLL_SUSPENDED;
  }
state_1:
  if (torb_task_cancelled(task)) {
    torb_task_release(frame->turn);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  torb_task_release(frame->turn);
  frame->turn = NULL;
  on_pool = torb_worker_is_blocking();
  if (!frame->waits) {
    *(int64_t *)torb_task_result_slot(task) = on_pool ? 1 : 2;
    return TORB_POLL_FINISHED;
  }
  torb_atomic_store_u32(&blocking_asked, 1u);
  while (torb_atomic_load_u32(&blocking_answered) == 0u && waited < 5000) {
    torb_platform_sleep(1000000LL);
    waited += 1;
  }
  *(int64_t *)torb_task_result_slot(task) = torb_atomic_load_u32(&blocking_answered) == 0u ? 0 : (on_pool ? 1 : 2);
  return TORB_POLL_FINISHED;
}

static torb_task *offloaded(bool waits, bool portable) {
  torb_task *task = torb_task_new(offloaded_resume, sizeof(offloaded_frame), &torb_element_int64);
  ((offloaded_frame *)torb_task_frame(task))->waits = waits;
  if (portable) {
    torb_task_start_portable(task);
  } else {
    torb_task_start(task);
  }
  return task;
}

/* The task on the worker: it lets the others run until the body asked, then answers. */
static torb_poll answerer_resume(torb_task *task) {
  if (torb_task_cancelled(task)) {
    return TORB_POLL_STOPPED;
  }
  if (task->state == 1u) {
    (void)torb_task_outcome(task);
  }
  if (torb_atomic_load_u32(&blocking_asked) == 0u) {
    task->state = 1u;
    if (torb_task_pause(task) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  torb_atomic_store_u32(&blocking_answered, 1u);
  *(torb_void *)torb_task_result_slot(task) = 0u;
  return TORB_POLL_FINISHED;
}

/**
 * With one worker, a body that blocks until a task of that very worker answers: it can only ever be answered because
 * it runs on a thread of the blocking pool and the worker goes on with its other tasks.
 */
TORB_TEST(a_body_on_the_blocking_pool_does_not_stall_its_worker) {
  torb_task *body;
  torb_task *answerer;
  torb_atomic_store_u32(&blocking_asked, 0u);
  torb_atomic_store_u32(&blocking_answered, 0u);
  torb_pool_set_workers(1u);
  torb_pool_set_blocking(2u);
  body = offloaded(true, true);
  answerer = torb_task_new(answerer_resume, 0u, &torb_element_void);
  torb_task_start(answerer);
  torb_scheduler_run(body);
  TORB_CHECK_INTEGER(result_of(body), 1);
  torb_task_release(body);
  torb_task_release(answerer);
  torb_scheduler_finish();
  torb_pool_set_workers(0u);
  torb_pool_set_blocking(0u);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
}

/**
 * A task started pinned may not move - its frame was never proven movable - so its turn is only a turn and its body runs
 * on its own worker; a hundred portable ones over four workers all reach the pool and come back.
 */
TORB_TEST(only_a_task_whose_frame_may_move_turns_to_the_blocking_pool) {
  torb_task *pinned;
  torb_task *tasks[100];
  int64_t index;
  bool all = true;
  pool_begin();
  torb_pool_set_blocking(3u);
  pinned = offloaded(false, false);
  for (index = 0; index < 100; index += 1) {
    tasks[index] = offloaded(false, true);
  }
  torb_scheduler_run(pinned);
  TORB_CHECK_INTEGER(result_of(pinned), 2);
  torb_task_release(pinned);
  for (index = 0; index < 100; index += 1) {
    torb_scheduler_run(tasks[index]);
    all = all && result_of(tasks[index]) == 1;
    torb_task_release(tasks[index]);
  }
  pool_end();
  torb_pool_set_blocking(0u);
  TORB_CHECK(all);
  TORB_CHECK_INTEGER(torb_task_live_count(), 0);
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
  TORB_ADD(a_panic_in_a_task_on_another_worker_fails_its_test);
  TORB_ADD(a_panic_in_a_task_on_the_main_thread_fails_its_test);
  TORB_ADD(a_test_waits_for_the_tasks_it_started);
  TORB_ADD(a_text_is_copied_only_where_somebody_else_holds_it);
  TORB_ADD(a_list_of_texts_held_twice_is_copied_down_to_every_text);
  TORB_ADD(a_map_held_twice_is_copied_down_to_every_key);
  TORB_ADD(an_environment_is_copied_only_where_somebody_else_holds_it);
  TORB_ADD(frames_of_texts_somebody_else_holds_cross_after_the_copy);
  TORB_ADD(a_body_on_the_blocking_pool_does_not_stall_its_worker);
  TORB_ADD(only_a_task_whose_frame_may_move_turns_to_the_blocking_pool);
}
