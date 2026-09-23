/*
 * memory.c - the block header, reference counting, make-unique, immortal values, the block counters.
 *
 * There are two counters, because there are two kinds of block. A counted block is freed when its last owner releases
 * it, and `live blocks at exit: 0` says every one of them was. An **immortal** block is never freed by construction:
 * it is born inside an immortal region (`torb_begin_immortal`), which is what the value of a module constant and the
 * tree of a quoted expression are built in, and it is reported on its own line so the leak gate stays exact.
 *
 * Counts are plain integers: nothing a worker can reach is reachable from another one, so nothing has to be atomic
 * (BACKEND 2.5). The one exception is a block with `TORB_SHARED_COUNT` - a task, a channel, the environment of a
 * closure that may cross to another worker - whose count changes atomically once the pool runs threads (torb.h, the
 * block header). The heap is libc's for now; the bump allocator with size-class free lists that section 2.5 describes
 * is a replacement behind these six functions and changes nothing above them.
 *
 * **One heap per worker, as counters.** Each worker counts what it allocates and frees in its own `torb_heap`
 * (runtime/include/torb_pool.h), written by its thread alone. A block that moved to another worker and is freed there
 * lowers that worker's counter instead of its maker's, so one counter may run below zero - they are unsigned and wrap -
 * and the **sum** over all workers is what is exact, which is what the report and the tests read. That is also why a
 * block needs no id of its heap in its header: libc frees it from any thread, and the counters balance in the sum.
 *
 * Every allocation failure panics: the language has no out-of-memory error.
 */

#include "torb.h"
#include "torb_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The heap of the calling thread's worker. A block allocated inside an immortal region (the value of a module constant,
 * the tree of a quoted expression, and every temporary the initializer made on the way) is born immortal: retaining and
 * releasing it are no-ops, it is never freed, and a write to it copies. It is counted apart from the live blocks, so
 * `live blocks at exit: 0` keeps meaning "everything that was counted was freed". Regions are per thread, because the
 * worker that builds a constant is the one whose allocations are immortal while it does.
 */
static torb_heap *torb_heap_current(void) {
  return &torb_worker_current()->heap;
}

static void *torb_payload(void *block) {
  return (void *)((uint8_t *)block + sizeof(torb_header));
}

/* `drop` and `retain_children` receive the block itself, which is what an emitted `T_x *` is. */

void *torb_allocate(size_t size, torb_block_kind kind) {
  torb_header *header;
  torb_heap *heap;
  if (size < sizeof(torb_header)) {
    size = sizeof(torb_header);
  }
  if (torb_sandbox_active != 0) {
    torb_sandbox_account(size);
  }
  header = (torb_header *)malloc(size);
  if (header == NULL) {
    torb_panic_out_of_memory(size);
  }
  header->kind = (uint16_t)kind;
  header->color = (uint16_t)TORB_COLOR_NONE;
  heap = torb_heap_current();
  if (heap->immortal_depth > 0) {
    header->count = TORB_IMMORTAL_COUNT;
    heap->immortal_blocks += 1;
    return header;
  }
  header->count = 1;
  heap->live_blocks += 1;
  return header;
}

void *torb_allocate_zeroed(size_t size, torb_block_kind kind) {
  void *block = torb_allocate(size, kind);
  if (size > sizeof(torb_header)) {
    memset(torb_payload(block), 0, size - sizeof(torb_header));
  }
  return block;
}

void *torb_raw_allocate(size_t size) {
  void *buffer;
  if (size == 0) {
    size = 1;
  }
  if (torb_sandbox_active != 0) {
    torb_sandbox_account(size);
  }
  buffer = malloc(size);
  if (buffer == NULL) {
    torb_panic_out_of_memory(size);
  }
  torb_heap_current()->live_blocks += 1;
  return buffer;
}

void *torb_raw_allocate_zeroed(size_t size) {
  void *buffer = torb_raw_allocate(size);
  memset(buffer, 0, size == 0 ? 1 : size);
  return buffer;
}

void torb_raw_free(void *buffer, size_t size) {
  (void)size;
  if (buffer == NULL) {
    return;
  }
  free(buffer);
  torb_heap_current()->live_blocks -= 1;
}

void torb_retain(void *block) {
  torb_header *header = (torb_header *)block;
  uint32_t count;
  if (header == NULL) {
    return;
  }
  count = header->count;
  if (count >= TORB_SHARED_COUNT) {
    if (count == TORB_IMMORTAL_COUNT) {
      return;
    }
    if (torb_pool_threaded != 0u) {
      (void)torb_atomic_add_u32(&header->count, 1u);
      return;
    }
  }
  header->count = count + 1u;
}

void torb_release(void *block, torb_drop_function drop) {
  torb_header *header = (torb_header *)block;
  uint32_t count;
  if (header == NULL) {
    return;
  }
  count = header->count;
  if (count == TORB_IMMORTAL_COUNT) {
    return;
  }
  if ((count & TORB_SHARED_COUNT) != 0u && torb_pool_threaded != 0u) {
    /* Whoever takes the count from one to zero frees the block, on whichever worker that is */
    uint32_t before = torb_atomic_sub_u32(&header->count, 1u);
    if (before == TORB_SHARED_COUNT) {
      torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
    }
    if (before != TORB_SHARED_COUNT + 1u) {
      return;
    }
  } else {
    if ((count & ~TORB_SHARED_COUNT) == 0u) {
      torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
    }
    count -= 1u;
    header->count = count;
    if ((count & ~TORB_SHARED_COUNT) != 0u) {
      return;
    }
  }
  if (drop != NULL) {
    drop(block);
  }
  free(block);
  torb_heap_current()->live_blocks -= 1;
}

bool torb_count_down(void *block) {
  torb_header *header = (torb_header *)block;
  uint32_t count = header->count;
  if (count == TORB_IMMORTAL_COUNT) {
    return false;
  }
  if ((count & TORB_SHARED_COUNT) != 0u && torb_pool_threaded != 0u) {
    uint32_t before = torb_atomic_sub_u32(&header->count, 1u);
    if (before == TORB_SHARED_COUNT) {
      torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
    }
    return before == TORB_SHARED_COUNT + 1u;
  }
  if ((count & ~TORB_SHARED_COUNT) == 0u) {
    torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
  }
  header->count = count - 1u;
  return ((count - 1u) & ~TORB_SHARED_COUNT) == 0u;
}

void torb_free_counted(void *block, torb_drop_function drop) {
  if (drop != NULL) {
    drop(block);
  }
  free(block);
  torb_heap_current()->live_blocks -= 1;
}

void torb_closing_begin(void *block) {
  torb_header *header = (torb_header *)block;
  header->count = 1;
}

void torb_closing_end(void *block) {
  torb_header *header = (torb_header *)block;
  if (header->count != 1) {
    torb_panic_text("internal error: `close` kept the object it was releasing", torb_location_unknown);
  }
  header->count = 0;
}

void torb_environment_on_frame(torb_environment *environment, torb_drop_function drop) {
  environment->header.count = 1u;
  environment->header.kind = (uint16_t)TORB_BLOCK_FRAME_ENVIRONMENT;
  environment->header.color = (uint16_t)TORB_COLOR_NONE;
  environment->drop = drop;
}

void torb_environment_release(torb_environment *environment) {
  if (environment == NULL) {
    return;
  }
  /* The storage of a frame environment is a local of the caller, so the count runs out and nothing is freed. */
  if (environment->header.kind == (uint16_t)TORB_BLOCK_FRAME_ENVIRONMENT) {
    if (environment->header.count == 0u) {
      torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
    }
    environment->header.count -= 1u;
    if (environment->header.count == 0u && environment->drop != NULL) {
      environment->drop(environment);
    }
    return;
  }
  torb_release(environment, environment->drop);
}

bool torb_is_unique(const void *block) {
  const torb_header *header = (const torb_header *)block;
  if (header == NULL) {
    return true;
  }
  /* An immortal count masks to 0x7FFFFFFF, so static data is never unique and a write to it always copies */
  return (header->count & ~TORB_SHARED_COUNT) == 1u;
}

void torb_share(void *block) {
  torb_header *header = (torb_header *)block;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  header->count |= TORB_SHARED_COUNT;
}

void *torb_make_unique(void *block,
                       size_t size,
                       torb_retain_children_function retain_children,
                       torb_drop_function drop) {
  torb_header *header = (torb_header *)block;
  void *copy;
  if (header == NULL || torb_is_unique(block)) {
    return block;
  }
  if (size < sizeof(torb_header)) {
    size = sizeof(torb_header);
  }
  copy = torb_allocate(size, (torb_block_kind)header->kind);
  if (size > sizeof(torb_header)) {
    memcpy(torb_payload(copy), torb_payload(block), size - sizeof(torb_header));
  }
  if (retain_children != NULL) {
    retain_children(copy);
  }
  torb_release(block, drop);
  return copy;
}

void torb_make_immortal(void *block) {
  torb_header *header = (torb_header *)block;
  torb_heap *heap;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  header->count = TORB_IMMORTAL_COUNT;
  /* The block is never freed again, so it leaves the live count and joins the immortal one. */
  heap = torb_heap_current();
  heap->live_blocks -= 1;
  heap->immortal_blocks += 1;
}

void torb_begin_immortal(void) {
  torb_heap_current()->immortal_depth += 1;
}

void torb_end_immortal(void) {
  torb_heap *heap = torb_heap_current();
  if (heap->immortal_depth == 0) {
    torb_panic_text("internal error: ended an immortal region that was never begun", torb_location_unknown);
  }
  heap->immortal_depth -= 1;
}

size_t torb_live_block_count(void) {
  return torb_pool_sum_live_blocks();
}

size_t torb_immortal_block_count(void) {
  return torb_pool_sum_immortal_blocks();
}

void torb_report_leaks(void) {
  fprintf(stderr, "live blocks at exit: %lu\n", (unsigned long)torb_pool_sum_live_blocks());
  fprintf(stderr, "immortal blocks at exit: %lu\n", (unsigned long)torb_pool_sum_immortal_blocks());
}

/* ------------------------------------------------------------------------------- building a constant once --- */

/*
 * The lock around the first build of an immortal counted static (torb.h, `torb_constant_ready`). Recursive by hand -
 * one mutex, the worker holding it and how deep - because the initializer of one constant may read another, which
 * locks again on the same thread. `owner` is the worker's index plus one, and a thread only ever compares it with its
 * own, which no other thread writes.
 */
static torb_mutex torb_constant_mutex = TORB_MUTEX_INITIALIZER;
static uint32_t torb_constant_owner = 0u;
static unsigned torb_constant_depth = 0u;

void torb_constant_lock(void) {
  uint32_t self = torb_worker_current()->index + 1u;
  if (torb_atomic_peek_u32(&torb_constant_owner) == self) {
    torb_constant_depth += 1u;
    return;
  }
  torb_mutex_lock(&torb_constant_mutex);
  torb_atomic_store_u32(&torb_constant_owner, self);
  torb_constant_depth = 1u;
}

void torb_constant_unlock(void) {
  torb_constant_depth -= 1u;
  if (torb_constant_depth == 0u) {
    torb_atomic_store_u32(&torb_constant_owner, 0u);
    torb_mutex_unlock(&torb_constant_mutex);
  }
}

void torb_constant_publish(bool *ready) {
#if defined(__GNUC__) || defined(__clang__)
  __atomic_store_n(ready, true, __ATOMIC_RELEASE);
#else
  *(volatile bool *)ready = true;
#endif
}
