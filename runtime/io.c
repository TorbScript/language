/*
 * io.c - the IO core: handles, operations, the tasks of `std/network`, and the resolver threads (docs/design/NETWORK.md
 * section 2; runtime/include/torb_io.h is the contract with the pollers of runtime/os/).
 *
 * # Handles
 *
 * A program holds a socket, a listener or the answer of a name resolution through a handle: `(generation << 32) |
 * (index + 1)` into one table of the process, behind one mutex. A handle whose slot was freed has another generation,
 * so using it after its close answers `TORB_IO_CLOSED` instead of touching a record that is gone. The table holds one
 * reference of the record; an operation in flight holds another.
 *
 * # Operations
 *
 * Every native that waits makes an operation and a task of the runtime whose frame holds it. The first resume of the
 * task submits the operation to the poller and waits for it (`torb_task_wait_io`); the completion - from the IO thread,
 * from a resolver thread, or from the submission itself - moves what arrived to where it belongs, wakes the task and
 * drops the kernel's reference; the task then answers. **What the kernel delivered is never lost to a cancellation**:
 * received bytes go to the socket's pending buffer at the completion, so the next receive answers them, and an accepted
 * connection whose task stopped is queued on its listener for the next accept.
 *
 * # Memory
 *
 * Every record here is `malloc`ed and not a block of any worker's heap: the IO thread frees operations and has no heap,
 * and a record's life is not the program's business. `torb_network_operations_alive` is their count, which the tests
 * and the stop of the core hold at zero.
 */

#include "torb.h"
#include "torb_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ the core --- */

/** The core runs: the poller and the IO thread were started, and are stopped by `torb_io_stop`. */
static uint32_t torb_io_running = 0u;
static torb_mutex torb_io_core_lock = TORB_MUTEX_INITIALIZER;
/** Operations alive, over every thread. */
static int64_t torb_io_alive = 0;

static void *torb_io_allocate(size_t size) {
  void *memory = calloc(1u, size);
  if (memory == NULL) {
    torb_panic_out_of_memory(size);
  }
  return memory;
}

/* Starts the poller the first time a socket is made. 0, or a packed failure. */
static int64_t torb_io_ensure_running(void) {
  int64_t failure = 0;
  if (torb_atomic_load_u32(&torb_io_running) != 0u) {
    return 0;
  }
  torb_mutex_lock(&torb_io_core_lock);
  if (torb_io_running == 0u) {
    torb_pool_prepare_thread();
    if (torb_io_system_start(&failure)) {
      torb_atomic_store_u32(&torb_io_running, 1u);
      failure = 0;
    }
  }
  torb_mutex_unlock(&torb_io_core_lock);
  return failure;
}

int64_t torb_io_operations_alive(void) {
  return torb_atomic_load_i64(&torb_io_alive);
}

int64_t torb_network_operations_alive(void) {
  return torb_io_operations_alive();
}

/* ---------------------------------------------------------------------------------------------- the records --- */

torb_io_socket *torb_io_socket_new(torb_io_record_kind kind, int32_t family, int64_t system) {
  torb_io_socket *socket = (torb_io_socket *)torb_io_allocate(sizeof(torb_io_socket));
  socket->count = 1u;
  socket->kind = (uint8_t)kind;
  socket->family = family;
  socket->system = system;
  return socket;
}

void torb_io_socket_retain(torb_io_socket *socket) {
  (void)torb_atomic_add_u32(&socket->count, 1u);
}

/* Closes the descriptor once, whoever asks first: `torb_network_close`, or the last release. */
static void torb_io_socket_close(torb_io_socket *socket) {
  bool closing;
  torb_spin_lock(&socket->lock);
  closing = socket->closed == 0u;
  socket->closed = 1u;
  torb_spin_unlock(&socket->lock);
  if (closing && socket->system != -1) {
    torb_io_system_close(socket);
  }
}

void torb_io_socket_free(torb_io_socket *socket) {
  free(socket->pending);
  free(socket->addresses);
  free(socket);
}

void torb_io_socket_release(torb_io_socket *socket) {
  torb_io_socket *accepted;
  if (torb_atomic_sub_u32(&socket->count, 1u) != 1u) {
    return;
  }
  torb_io_socket_close(socket);
  /* Connections that arrived and no accept took: nobody will */
  accepted = socket->accepted_first;
  socket->accepted_first = NULL;
  socket->accepted_last = NULL;
  while (accepted != NULL) {
    torb_io_socket *next = accepted->accepted_next;
    accepted->accepted_next = NULL;
    torb_io_socket_release(accepted);
    accepted = next;
  }
  torb_io_system_forget(socket);
}

/* ---------------------------------------------------------------------------------------------- the table --- */

typedef struct torb_io_table {
  torb_io_socket **records;
  uint32_t *generations;
  uint32_t capacity;
  /** Free slots, a stack of indices. */
  uint32_t *free;
  uint32_t free_count;
} torb_io_table;

static torb_io_table torb_io_handles = { NULL, NULL, 0u, NULL, 0u };
static torb_mutex torb_io_table_lock = TORB_MUTEX_INITIALIZER;

/* A handle for `socket`, whose reference the table takes over. */
static int64_t torb_io_register(torb_io_socket *socket) {
  uint32_t index;
  int64_t handle;
  torb_mutex_lock(&torb_io_table_lock);
  if (torb_io_handles.free_count == 0u) {
    uint32_t capacity = torb_io_handles.capacity == 0u ? 64u : torb_io_handles.capacity * 2u;
    uint32_t added;
    torb_io_socket **records;
    uint32_t *generations;
    uint32_t *free_slots;
    if (torb_io_handles.capacity >= 0x40000000u) {
      torb_mutex_unlock(&torb_io_table_lock);
      torb_panic_text("more than a billion sockets at once are not supported", torb_location_unknown);
    }
    records = (torb_io_socket **)realloc(torb_io_handles.records, (size_t)capacity * sizeof(torb_io_socket *));
    generations = (uint32_t *)realloc(torb_io_handles.generations, (size_t)capacity * sizeof(uint32_t));
    free_slots = (uint32_t *)realloc(torb_io_handles.free, (size_t)capacity * sizeof(uint32_t));
    if (records == NULL || generations == NULL || free_slots == NULL) {
      torb_panic_out_of_memory((size_t)capacity * sizeof(torb_io_socket *));
    }
    torb_io_handles.records = records;
    torb_io_handles.generations = generations;
    torb_io_handles.free = free_slots;
    /* Pushed so that the lowest index is taken first */
    for (added = capacity; added > torb_io_handles.capacity; added -= 1u) {
      records[added - 1u] = NULL;
      generations[added - 1u] = 1u;
      free_slots[torb_io_handles.free_count] = added - 1u;
      torb_io_handles.free_count += 1u;
    }
    torb_io_handles.capacity = capacity;
  }
  torb_io_handles.free_count -= 1u;
  index = torb_io_handles.free[torb_io_handles.free_count];
  torb_io_handles.records[index] = socket;
  handle = ((int64_t)torb_io_handles.generations[index] << 32) | (int64_t)(index + 1u);
  torb_mutex_unlock(&torb_io_table_lock);
  return handle;
}

/* The slot of a live handle, or -1. The table lock held. */
static int64_t torb_io_slot_of(int64_t handle) {
  uint32_t index;
  uint32_t generation;
  if (handle <= 0) {
    return -1;
  }
  index = (uint32_t)(handle & 0xFFFFFFFF) - 1u;
  generation = (uint32_t)((uint64_t)handle >> 32);
  if (index >= torb_io_handles.capacity || torb_io_handles.records[index] == NULL
      || torb_io_handles.generations[index] != generation) {
    return -1;
  }
  return (int64_t)index;
}

/* The record of a live handle of `kind`, retained; `NULL` for a handle that is closed or of another kind. */
static torb_io_socket *torb_io_lookup(int64_t handle, torb_io_record_kind kind) {
  torb_io_socket *socket = NULL;
  int64_t slot;
  torb_mutex_lock(&torb_io_table_lock);
  slot = torb_io_slot_of(handle);
  if (slot >= 0 && torb_io_handles.records[slot]->kind == (uint8_t)kind) {
    socket = torb_io_handles.records[slot];
    torb_io_socket_retain(socket);
  }
  torb_mutex_unlock(&torb_io_table_lock);
  return socket;
}

/* Takes the record out of the table: its reference is the caller's now. `NULL` for a handle that is not live. */
static torb_io_socket *torb_io_unregister(int64_t handle) {
  torb_io_socket *socket = NULL;
  int64_t slot;
  torb_mutex_lock(&torb_io_table_lock);
  slot = torb_io_slot_of(handle);
  if (slot >= 0) {
    socket = torb_io_handles.records[slot];
    torb_io_handles.records[slot] = NULL;
    torb_io_handles.generations[slot] = torb_io_handles.generations[slot] >= 0x7FFFFFFFu
                                           ? 1u
                                           : torb_io_handles.generations[slot] + 1u;
    torb_io_handles.free[torb_io_handles.free_count] = (uint32_t)slot;
    torb_io_handles.free_count += 1u;
  }
  torb_mutex_unlock(&torb_io_table_lock);
  return socket;
}

/* ------------------------------------------------------------------------------------------ the operations --- */

static torb_io_operation *torb_io_operation_new(torb_io_operation_kind kind, torb_io_socket *socket) {
  torb_io_operation *operation = (torb_io_operation *)torb_io_allocate(sizeof(torb_io_operation));
  operation->count = 1u;
  operation->kind = (uint8_t)kind;
  operation->socket = socket;
  (void)torb_atomic_add_i64(&torb_io_alive, 1);
  return operation;
}

void torb_io_operation_release(torb_io_operation *operation) {
  if (torb_atomic_sub_u32(&operation->count, 1u) != 1u) {
    return;
  }
  if (operation->accepted != NULL) {
    torb_io_socket *accepted = operation->accepted;
    operation->accepted = NULL;
    /* A connection whose accept was cancelled after it arrived waits on its listener for the next accept */
    if (operation->kind == (uint8_t)TORB_IO_ACCEPT && operation->result >= 0 && operation->socket != NULL) {
      torb_io_socket *listener = operation->socket;
      bool queued = false;
      torb_spin_lock(&listener->lock);
      if (listener->closed == 0u) {
        if (listener->accepted_last != NULL) {
          listener->accepted_last->accepted_next = accepted;
        } else {
          listener->accepted_first = accepted;
        }
        listener->accepted_last = accepted;
        queued = true;
      }
      torb_spin_unlock(&listener->lock);
      if (!queued) {
        torb_io_socket_release(accepted);
      }
    } else {
      torb_io_socket_release(accepted);
    }
  }
  if (operation->socket != NULL) {
    torb_io_socket_release(operation->socket);
  }
  free(operation->buffer);
  free(operation->host);
  free(operation->addresses);
  free(operation);
  (void)torb_atomic_add_i64(&torb_io_alive, -1);
}

/* Appends `length` bytes to what `socket` received and nobody took. The socket's lock held. */
static void torb_io_keep_received(torb_io_socket *socket, const uint8_t *bytes, size_t length) {
  if (socket->pending_length + length > socket->pending_capacity) {
    size_t capacity = socket->pending_capacity == 0u ? length : socket->pending_capacity * 2u;
    uint8_t *grown;
    while (capacity < socket->pending_length + length) {
      capacity *= 2u;
    }
    grown = (uint8_t *)realloc(socket->pending, capacity);
    if (grown == NULL) {
      torb_panic_out_of_memory(capacity);
    }
    socket->pending = grown;
    socket->pending_capacity = capacity;
  }
  memcpy(socket->pending + socket->pending_length, bytes, length);
  socket->pending_length += length;
}

void torb_io_complete(torb_io_operation *operation, int64_t result) {
  operation->result = result;
  if (operation->kind == (uint8_t)TORB_IO_RECEIVE && result > 0 && operation->buffer != NULL
      && (size_t)result <= operation->capacity) {
    torb_io_socket *socket = operation->socket;
    torb_spin_lock(&socket->lock);
    torb_io_keep_received(socket, operation->buffer, (size_t)result);
    torb_spin_unlock(&socket->lock);
  }
  torb_task_io_done(&operation->waiting);
  torb_io_operation_release(operation);
}

/* ---------------------------------------------------------------------------------- the resolver threads --- */

#define TORB_IO_RESOLVERS 4u

/* Behind `torb_io_resolver_lock`; the condition is made the first time a name is resolved. */
typedef struct torb_io_resolvers {
  torb_condition wake;
  bool prepared;
  torb_io_operation *first;
  torb_io_operation *last;
  uint32_t threads;
  uint32_t idle;
  uint32_t stopping;
  torb_thread handles[TORB_IO_RESOLVERS];
} torb_io_resolvers;

static torb_mutex torb_io_resolver_lock = TORB_MUTEX_INITIALIZER;
static torb_io_resolvers torb_io_resolver;

/* The loop of a resolver thread: the oldest request, `getaddrinfo`, the completion; until the core stops. */
static void torb_io_resolver_main(void *argument) {
  (void)argument;
  torb_mutex_lock(&torb_io_resolver_lock);
  for (;;) {
    torb_io_operation *operation = torb_io_resolver.first;
    if (operation == NULL) {
      if (torb_io_resolver.stopping != 0u) {
        break;
      }
      torb_io_resolver.idle += 1u;
      (void)torb_condition_wait(&torb_io_resolver.wake, &torb_io_resolver_lock, -1);
      torb_io_resolver.idle -= 1u;
      continue;
    }
    torb_io_resolver.first = operation->queued_next;
    if (torb_io_resolver.first == NULL) {
      torb_io_resolver.last = NULL;
    }
    operation->queued_next = NULL;
    operation->in_queue = 0u;
    torb_mutex_unlock(&torb_io_resolver_lock);
    {
      torb_io_address *addresses = NULL;
      int64_t answer = torb_io_system_resolve(operation->host, &addresses);
      if (answer >= 0) {
        operation->addresses = addresses;
        operation->address_count = (size_t)answer;
      } else {
        free(addresses);
      }
      torb_io_complete(operation, answer);
    }
    torb_mutex_lock(&torb_io_resolver_lock);
  }
  torb_mutex_unlock(&torb_io_resolver_lock);
}

/* Queues a resolution, and starts a thread where none is idle and fewer than four run. */
static void torb_io_resolver_submit(torb_io_operation *operation) {
  torb_mutex_lock(&torb_io_resolver_lock);
  if (!torb_io_resolver.prepared) {
    torb_condition_initialize(&torb_io_resolver.wake);
    torb_io_resolver.prepared = true;
  }
  operation->in_queue = 1u;
  operation->queued_next = NULL;
  if (torb_io_resolver.last != NULL) {
    torb_io_resolver.last->queued_next = operation;
  } else {
    torb_io_resolver.first = operation;
  }
  torb_io_resolver.last = operation;
  if (torb_io_resolver.idle == 0u && torb_io_resolver.threads < TORB_IO_RESOLVERS) {
    /*
     * A resolution may come before any socket, so before the IO thread: the process is readied for a second thread here
     * as well - the console and the clock made ready, and the counts of shared blocks atomic from now on.
     */
    torb_pool_prepare_thread();
    if (torb_thread_start(&torb_io_resolver.handles[torb_io_resolver.threads], torb_io_resolver_main, NULL,
                          (size_t)256u * 1024u)) {
      torb_io_resolver.threads += 1u;
    }
  }
  torb_condition_signal(&torb_io_resolver.wake);
  torb_mutex_unlock(&torb_io_resolver_lock);
}

/* A resolution whose waiter was cancelled: taken out of the queue where no thread has it yet, and completed as such. */
static void torb_io_resolver_cancel(torb_io_operation *operation) {
  bool removed = false;
  torb_mutex_lock(&torb_io_resolver_lock);
  if (operation->in_queue != 0u) {
    torb_io_operation *previous = NULL;
    torb_io_operation *current = torb_io_resolver.first;
    while (current != NULL && current != operation) {
      previous = current;
      current = current->queued_next;
    }
    if (current == operation) {
      if (previous != NULL) {
        previous->queued_next = operation->queued_next;
      } else {
        torb_io_resolver.first = operation->queued_next;
      }
      if (torb_io_resolver.last == operation) {
        torb_io_resolver.last = previous;
      }
      operation->queued_next = NULL;
      operation->in_queue = 0u;
      removed = true;
    }
  }
  torb_mutex_unlock(&torb_io_resolver_lock);
  if (removed) {
    torb_io_complete(operation, torb_io_failed(TORB_IO_ABORTED, 0u));
  }
}

static void torb_io_resolver_stop(void) {
  uint32_t index;
  uint32_t threads;
  torb_mutex_lock(&torb_io_resolver_lock);
  if (!torb_io_resolver.prepared) {
    torb_mutex_unlock(&torb_io_resolver_lock);
    return;
  }
  torb_io_resolver.stopping = 1u;
  threads = torb_io_resolver.threads;
  for (index = 0u; index < threads; index += 1u) {
    torb_condition_signal(&torb_io_resolver.wake);
  }
  torb_mutex_unlock(&torb_io_resolver_lock);
  for (index = 0u; index < threads; index += 1u) {
    torb_thread_join(&torb_io_resolver.handles[index]);
  }
  torb_mutex_lock(&torb_io_resolver_lock);
  torb_io_resolver.threads = 0u;
  torb_io_resolver.stopping = 0u;
  torb_mutex_unlock(&torb_io_resolver_lock);
}

void torb_io_waiter_cancelled(torb_io_waiting *waiting) {
  torb_io_operation *operation =
      (torb_io_operation *)(void *)((uint8_t *)waiting - offsetof(torb_io_operation, waiting));
  if (operation->kind == (uint8_t)TORB_IO_RESOLVE) {
    torb_io_resolver_cancel(operation);
  } else {
    torb_io_system_cancel(operation);
  }
}

/* ------------------------------------------------------------------------------- submitting and answering --- */

/*
 * Starts an operation on the worker whose task waits for it. What needs no system call answers at once: bytes that
 * already arrived, a connection that already waits on its listener, a socket that is closed.
 */
static void torb_io_start(torb_io_operation *operation) {
  torb_io_socket *socket = operation->socket;
  operation->count = 2u;
  if (operation->kind == (uint8_t)TORB_IO_RESOLVE) {
    torb_io_resolver_submit(operation);
    return;
  }
  torb_spin_lock(&socket->lock);
  if (socket->closed != 0u) {
    torb_spin_unlock(&socket->lock);
    torb_io_complete(operation, torb_io_failed(TORB_IO_CLOSED, 0u));
    return;
  }
  if (operation->kind == (uint8_t)TORB_IO_RECEIVE && socket->pending_length > 0u) {
    size_t length = socket->pending_length;
    torb_spin_unlock(&socket->lock);
    /* The bytes are in the socket already: nothing is to be kept from the buffer */
    free(operation->buffer);
    operation->buffer = NULL;
    operation->capacity = 0u;
    torb_io_complete(operation, (int64_t)length);
    return;
  }
  if (operation->kind == (uint8_t)TORB_IO_ACCEPT && socket->accepted_first != NULL) {
    operation->accepted = socket->accepted_first;
    socket->accepted_first = operation->accepted->accepted_next;
    if (socket->accepted_first == NULL) {
      socket->accepted_last = NULL;
    }
    operation->accepted->accepted_next = NULL;
    torb_spin_unlock(&socket->lock);
    torb_io_complete(operation, 0);
    return;
  }
  torb_spin_unlock(&socket->lock);
  torb_io_system_submit(operation);
}

/* What the task answers, on its worker, once the operation completed. */
static int64_t torb_io_answer(torb_io_operation *operation) {
  int64_t result = operation->result;
  if (result < 0) {
    return result;
  }
  switch ((torb_io_operation_kind)operation->kind) {
    case TORB_IO_ACCEPT:
    case TORB_IO_CONNECT: {
      torb_io_socket *socket = operation->accepted;
      if (socket == NULL) {
        return torb_io_failed(TORB_IO_OTHER, 0u);
      }
      operation->accepted = NULL;
      return torb_io_register(socket);
    }
    case TORB_IO_RESOLVE: {
      torb_io_socket *resolution = torb_io_socket_new(TORB_IO_RESOLUTION, 0, -1);
      resolution->addresses = operation->addresses;
      resolution->address_count = operation->address_count;
      operation->addresses = NULL;
      return torb_io_register(resolution);
    }
    case TORB_IO_RECEIVE:
    case TORB_IO_SEND:
      return result;
  }
  return result;
}

/* --------------------------------------------------------------------------------------- the tasks --- */

typedef struct torb_io_frame {
  /** The operation, or `NULL` where the answer was known before anything was submitted. */
  torb_io_operation *operation;
  int64_t answer;
} torb_io_frame;

static torb_poll torb_io_resume(torb_task *task) {
  torb_io_frame *frame = (torb_io_frame *)torb_task_frame(task);
  torb_io_operation *operation = frame->operation;
  if (torb_task_cancelled(task)) {
    if (operation != NULL) {
      torb_io_operation_release(operation);
    }
    return TORB_POLL_STOPPED;
  }
  if (operation == NULL) {
    *(int64_t *)torb_task_result_slot(task) = frame->answer;
    return TORB_POLL_FINISHED;
  }
  if (task->state == 0u) {
    task->state = 1u;
    torb_io_start(operation);
    if (torb_task_wait_io(task, &operation->waiting) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  (void)torb_task_outcome(task);
  *(int64_t *)torb_task_result_slot(task) = torb_io_answer(operation);
  torb_io_operation_release(operation);
  return TORB_POLL_FINISHED;
}

/* The task of an operation, started: it submits the operation the first time it runs. */
static torb_task *torb_io_task(torb_io_operation *operation) {
  torb_task *task = torb_task_new(torb_io_resume, sizeof(torb_io_frame), &torb_element_int64);
  torb_io_frame *frame = (torb_io_frame *)torb_task_frame(task);
  frame->operation = operation;
  frame->answer = 0;
  torb_task_start(task);
  return task;
}

/* A task that answers `answer` the first time it runs: a failure found before anything could be submitted. */
static torb_task *torb_io_answered(int64_t answer) {
  torb_task *task = torb_task_new(torb_io_resume, sizeof(torb_io_frame), &torb_element_int64);
  torb_io_frame *frame = (torb_io_frame *)torb_task_frame(task);
  frame->operation = NULL;
  frame->answer = answer;
  torb_task_start(task);
  return task;
}

/* ------------------------------------------------------------------------------------------ the natives --- */

/* The address the natives pass as numbers, in the core's form. False for a family or a port that is none. */
static bool torb_io_address_of(int64_t family, int64_t high, int64_t low, int64_t port, torb_io_address *out) {
  int index;
  memset(out, 0, sizeof *out);
  if ((family != 4 && family != 6) || port < 0 || port > 65535) {
    return false;
  }
  out->family = (int32_t)family;
  out->port = (uint16_t)port;
  if (family == 4) {
    uint64_t value = (uint64_t)low;
    if (value > 0xFFFFFFFFu || high != 0) {
      return false;
    }
    for (index = 0; index < 4; index += 1) {
      out->bytes[index] = (uint8_t)(value >> (24 - 8 * index));
    }
  } else {
    for (index = 0; index < 8; index += 1) {
      out->bytes[index] = (uint8_t)((uint64_t)high >> (56 - 8 * index));
      out->bytes[8 + index] = (uint8_t)((uint64_t)low >> (56 - 8 * index));
    }
  }
  return true;
}

/* Appends an address to `*parts` as the numbers the natives pass: family, high, low, and the port where `port`. */
static void torb_io_add_address(torb_list *parts, const torb_io_address *address, bool port) {
  int64_t numbers[4];
  uint64_t high = 0u;
  uint64_t low = 0u;
  int index;
  if (address->family == 4) {
    for (index = 0; index < 4; index += 1) {
      low = (low << 8) | address->bytes[index];
    }
  } else {
    for (index = 0; index < 8; index += 1) {
      high = (high << 8) | address->bytes[index];
      low = (low << 8) | address->bytes[8 + index];
    }
  }
  numbers[0] = (int64_t)address->family;
  numbers[1] = (int64_t)high;
  numbers[2] = (int64_t)low;
  numbers[3] = (int64_t)address->port;
  torb_list_add_plain(parts, numbers, port ? 4u : 3u);
}

int64_t torb_network_listen(int64_t family, int64_t high, int64_t low, int64_t port, int64_t backlog) {
  torb_io_address address;
  torb_io_socket *socket;
  int64_t system;
  int64_t failure;
  if (!torb_io_address_of(family, high, low, port, &address)) {
    return torb_io_failed(TORB_IO_INVALID, 0u);
  }
  failure = torb_io_ensure_running();
  if (failure != 0) {
    return failure;
  }
  system = torb_io_system_socket((int32_t)family);
  if (system < 0) {
    return system;
  }
  socket = torb_io_socket_new(TORB_IO_LISTENER, (int32_t)family, system);
  failure = torb_io_system_listen(socket, &address, backlog < 1 ? 1 : (backlog > 65535 ? 65535 : backlog));
  if (failure != 0) {
    torb_io_socket_release(socket);
    return failure;
  }
  return torb_io_register(socket);
}

torb_task *torb_network_accept(int64_t listener) {
  torb_io_socket *socket = torb_io_lookup(listener, TORB_IO_LISTENER);
  if (socket == NULL) {
    return torb_io_answered(torb_io_failed(TORB_IO_CLOSED, 0u));
  }
  return torb_io_task(torb_io_operation_new(TORB_IO_ACCEPT, socket));
}

torb_task *torb_network_connect(int64_t family, int64_t high, int64_t low, int64_t port) {
  torb_io_address address;
  torb_io_operation *operation;
  int64_t system;
  int64_t failure;
  if (!torb_io_address_of(family, high, low, port, &address) || port == 0) {
    return torb_io_answered(torb_io_failed(TORB_IO_INVALID, 0u));
  }
  failure = torb_io_ensure_running();
  if (failure != 0) {
    return torb_io_answered(failure);
  }
  system = torb_io_system_socket((int32_t)family);
  if (system < 0) {
    return torb_io_answered(system);
  }
  /* The operation works on the new socket, and answers it: one reference for each */
  operation = torb_io_operation_new(TORB_IO_CONNECT, torb_io_socket_new(TORB_IO_STREAM, (int32_t)family, system));
  torb_io_socket_retain(operation->socket);
  operation->accepted = operation->socket;
  operation->address = address;
  return torb_io_task(operation);
}

torb_task *torb_network_receive(int64_t stream, int64_t maximum) {
  torb_io_socket *socket = torb_io_lookup(stream, TORB_IO_STREAM);
  torb_io_operation *operation;
  if (socket == NULL) {
    return torb_io_answered(torb_io_failed(TORB_IO_CLOSED, 0u));
  }
  if (maximum < 1 || maximum > 0x7FFFFFFF) {
    torb_io_socket_release(socket);
    return torb_io_answered(torb_io_failed(TORB_IO_INVALID, 0u));
  }
  operation = torb_io_operation_new(TORB_IO_RECEIVE, socket);
  operation->buffer = (uint8_t *)torb_io_allocate((size_t)maximum);
  operation->capacity = (size_t)maximum;
  return torb_io_task(operation);
}

void torb_network_take_received(int64_t stream, torb_list *into) {
  torb_io_socket *socket = torb_io_lookup(stream, TORB_IO_STREAM);
  uint8_t *bytes;
  size_t length;
  if (socket == NULL) {
    return;
  }
  torb_spin_lock(&socket->lock);
  bytes = socket->pending;
  length = socket->pending_length;
  socket->pending = NULL;
  socket->pending_length = 0u;
  socket->pending_capacity = 0u;
  torb_spin_unlock(&socket->lock);
  torb_list_add_plain(into, bytes, length);
  free(bytes);
  torb_io_socket_release(socket);
}

torb_task *torb_network_send(int64_t stream, torb_list bytes, int64_t from) {
  torb_io_socket *socket = torb_io_lookup(stream, TORB_IO_STREAM);
  torb_io_operation *operation;
  int64_t length = torb_list_length(bytes);
  if (socket == NULL) {
    return torb_io_answered(torb_io_failed(TORB_IO_CLOSED, 0u));
  }
  if (from < 0 || from > length || torb_list_element(bytes)->size != 1u) {
    torb_io_socket_release(socket);
    return torb_io_answered(torb_io_failed(TORB_IO_INVALID, 0u));
  }
  operation = torb_io_operation_new(TORB_IO_SEND, socket);
  operation->length = (size_t)(length - from);
  operation->buffer = (uint8_t *)torb_io_allocate(operation->length == 0u ? 1u : operation->length);
  if (operation->length > 0u) {
    memcpy(operation->buffer, torb_list_at(bytes, from, torb_location_unknown), operation->length);
  }
  if (operation->length == 0u) {
    torb_io_operation_release(operation);
    return torb_io_answered(0);
  }
  return torb_io_task(operation);
}

int64_t torb_network_shutdown(int64_t stream) {
  torb_io_socket *socket = torb_io_lookup(stream, TORB_IO_STREAM);
  int64_t answer;
  if (socket == NULL) {
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  answer = torb_io_system_shutdown(socket);
  torb_io_socket_release(socket);
  return answer;
}

void torb_network_close(int64_t handle) {
  torb_io_socket *socket = torb_io_unregister(handle);
  if (socket == NULL) {
    return;
  }
  torb_io_socket_close(socket);
  torb_io_socket_release(socket);
}

int64_t torb_network_address(int64_t handle, bool peer, torb_list *parts) {
  torb_io_socket *socket = torb_io_lookup(handle, TORB_IO_STREAM);
  torb_io_address address;
  int64_t answer;
  if (socket == NULL) {
    socket = torb_io_lookup(handle, TORB_IO_LISTENER);
  }
  if (socket == NULL) {
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  answer = torb_io_system_address(socket, peer, &address);
  if (answer == 0) {
    torb_io_add_address(parts, &address, true);
  }
  torb_io_socket_release(socket);
  return answer;
}

torb_task *torb_network_resolve(torb_text host) {
  torb_io_operation *operation;
  const uint8_t *bytes = host.length == 0u ? NULL : host.storage->data + host.offset;
  if (host.length == 0u || memchr(bytes, 0, host.length) != NULL) {
    return torb_io_answered(torb_io_failed(TORB_IO_HOST_NOT_FOUND, 0u));
  }
  operation = torb_io_operation_new(TORB_IO_RESOLVE, NULL);
  operation->host = (char *)torb_io_allocate((size_t)host.length + 1u);
  memcpy(operation->host, bytes, host.length);
  return torb_io_task(operation);
}

void torb_network_take_resolved(int64_t resolution, torb_list *parts) {
  torb_io_socket *record = torb_io_unregister(resolution);
  size_t index;
  if (record == NULL) {
    return;
  }
  if (record->kind == (uint8_t)TORB_IO_RESOLUTION) {
    for (index = 0u; index < record->address_count; index += 1u) {
      torb_io_add_address(parts, &record->addresses[index], false);
    }
  }
  torb_io_socket_release(record);
}

torb_text torb_network_error_text(int64_t failure) {
  char buffer[512];
  uint64_t packed = failure < 0 ? (uint64_t)(-failure) : 0u;
  torb_io_failure kind = (torb_io_failure)(packed >> 32);
  uint32_t code = (uint32_t)(packed & 0xFFFFFFFFu);
  buffer[0] = '\0';
  if (code != 0u) {
    torb_io_system_error_text(code, buffer, sizeof buffer);
  }
  if (buffer[0] == '\0') {
    const char *words;
    switch (kind) {
      case TORB_IO_REFUSED: words = "the connection was refused"; break;
      case TORB_IO_RESET: words = "the connection was reset by the peer"; break;
      case TORB_IO_ABORTED: words = "the operation was aborted"; break;
      case TORB_IO_TIMED_OUT: words = "the operation timed out"; break;
      case TORB_IO_ADDRESS_IN_USE: words = "the address is in use"; break;
      case TORB_IO_ADDRESS_NOT_AVAILABLE: words = "the address is not available"; break;
      case TORB_IO_HOST_NOT_FOUND: words = "the host was not found"; break;
      case TORB_IO_UNREACHABLE: words = "the network or the host is unreachable"; break;
      case TORB_IO_CLOSED: words = "the socket is closed"; break;
      case TORB_IO_INVALID: words = "the address or the argument is not valid"; break;
      case TORB_IO_OTHER:
      default: words = "the network operation failed"; break;
    }
    snprintf(buffer, sizeof buffer, "%s", words);
  }
  return torb_text_from_cstring(buffer);
}

/* ---------------------------------------------------------------------------------------------- the stop --- */

bool torb_io_stop(void) {
  uint32_t index;
  torb_instant waited;
  if (torb_atomic_load_u32(&torb_io_running) == 0u) {
    torb_io_resolver_stop();
    return false;
  }
  /* Whatever a program still holds is closed: every operation in flight on it completes, failed */
  for (;;) {
    int64_t handle = 0;
    torb_mutex_lock(&torb_io_table_lock);
    for (index = 0u; index < torb_io_handles.capacity; index += 1u) {
      if (torb_io_handles.records[index] != NULL) {
        handle = ((int64_t)torb_io_handles.generations[index] << 32) | (int64_t)(index + 1u);
        break;
      }
    }
    torb_mutex_unlock(&torb_io_table_lock);
    if (handle == 0) {
      break;
    }
    torb_network_close(handle);
  }
  torb_io_resolver_stop();
  /* A cancelled operation's completion comes promptly; a second is far more than the kernel takes */
  waited = torb_clock_now();
  while (torb_io_operations_alive() > 0 && torb_clock_now() - waited < (torb_instant)1000000000) {
    torb_thread_yield();
  }
  torb_io_system_stop();
  free(torb_io_handles.records);
  free(torb_io_handles.generations);
  free(torb_io_handles.free);
  torb_io_handles.records = NULL;
  torb_io_handles.generations = NULL;
  torb_io_handles.free = NULL;
  torb_io_handles.capacity = 0u;
  torb_io_handles.free_count = 0u;
  torb_atomic_store_u32(&torb_io_running, 0u);
  return true;
}
