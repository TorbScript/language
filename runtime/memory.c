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
 * Every allocation failure panics: the language has no out-of-memory error. It ends the program with
 * `TORB_OUT_OF_MEMORY_EXIT_CODE`, and where a memory limit is in force the message names it (the end of this file).
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

/*
 * Whether a VM ever counted a program in this process: the leak report is then about that program's blocks, which the
 * heap counts apart from `torb`'s own while the thread runs inside the VM's kernel (`torb_count_in_machine`).
 */
static uint8_t torb_counts_machine = 0u;

/* One block more or less, and the same for the VM's program where the thread runs inside its kernel. */
static void torb_count_live(torb_heap *heap, int64_t change) {
  heap->live_blocks += (size_t)change;
  if (heap->in_machine != 0u) {
    heap->machine_live_blocks += change;
  }
}

static void torb_count_immortal(torb_heap *heap) {
  heap->immortal_blocks += 1;
  if (heap->in_machine != 0u) {
    heap->machine_immortal_blocks += 1;
  }
}

/*
 * The runtime's own count of what it holds, against the memory limit, where the operating system does not enforce the
 * limit (the end of this file). Off unless `torb_memory_counting` is set, which happens before any thread starts, so
 * what every allocation and every free pays for it is the test of a flag that does not change afterwards.
 */
static int torb_memory_counting = 0;
/*
 * In a binary that hosts the VM (`TORB_HOSTS_MACHINE`): the count is of the interpreted program's blocks alone, those
 * allocated inside a call of the VM's kernel (`torb_heap.in_machine`), against the program's limit.
 */
static int torb_memory_counts_machine = 0;
static void torb_memory_count(void *block);
static void torb_memory_uncount(void *block);

static void *torb_payload(void *block) {
  return (void *)((uint8_t *)block + sizeof(torb_header));
}

/*
 * The count of a block, read before it is changed. A shared block's count is changed atomically by other workers at the
 * same time, so the read is an atomic one too - a relaxed load, which is the plain load it always was on every target
 * the toolchain builds for; only the bits it looks at first, `TORB_SHARED_COUNT` and the immortal count, decide
 * anything before an atomic operation takes over, and those never change while a second thread holds the block.
 */
static uint32_t torb_count_of(const torb_header *header) {
  return torb_atomic_peek_u32(&header->count);
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
  if (torb_memory_counting != 0) {
    torb_memory_count(header);
  }
  header->kind = (uint16_t)kind;
  header->color = (uint16_t)TORB_COLOR_NONE;
  heap = torb_heap_current();
  if (heap->immortal_depth > 0) {
    header->count = TORB_IMMORTAL_COUNT;
    torb_count_immortal(heap);
    return header;
  }
  header->count = 1;
  torb_count_live(heap, 1);
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
  if (torb_memory_counting != 0) {
    torb_memory_count(buffer);
  }
  torb_count_live(torb_heap_current(), 1);
  return buffer;
}

void *torb_raw_allocate_zeroed(size_t size) {
  void *buffer = torb_raw_allocate(size);
  memset(buffer, 0, size == 0 ? 1 : size);
  return buffer;
}

void torb_raw_count_immortal(void) {
  torb_heap *heap = torb_heap_current();
  torb_count_live(heap, -1);
  torb_count_immortal(heap);
}

void torb_raw_count_mortal(void) {
  torb_heap *heap = torb_heap_current();
  heap->immortal_blocks -= 1;
  if (heap->in_machine != 0u) {
    heap->machine_immortal_blocks -= 1;
  }
  torb_count_live(heap, 1);
}

void torb_raw_free(void *buffer, size_t size) {
  (void)size;
  if (buffer == NULL) {
    return;
  }
  if (torb_memory_counting != 0) {
    torb_memory_uncount(buffer);
  }
  free(buffer);
  torb_count_live(torb_heap_current(), -1);
}

void torb_retain(void *block) {
  torb_header *header = (torb_header *)block;
  uint32_t count;
  if (header == NULL) {
    return;
  }
  count = torb_count_of(header);
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
  count = torb_count_of(header);
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
  if (torb_memory_counting != 0) {
    torb_memory_uncount(block);
  }
  free(block);
  torb_count_live(torb_heap_current(), -1);
}

bool torb_count_down(void *block) {
  torb_header *header = (torb_header *)block;
  uint32_t count = torb_count_of(header);
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
  if (torb_memory_counting != 0) {
    torb_memory_uncount(block);
  }
  free(block);
  torb_count_live(torb_heap_current(), -1);
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
  torb_count_live(heap, -1);
  torb_count_immortal(heap);
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
  if (torb_atomic_load_u8(&torb_counts_machine) != 0u) {
    fprintf(stderr, "live blocks at exit: %lld\n", (long long)torb_pool_sum_machine_live_blocks());
    fprintf(stderr, "immortal blocks at exit: %lld\n", (long long)torb_pool_sum_machine_immortal_blocks());
    return;
  }
  fprintf(stderr, "live blocks at exit: %lu\n", (unsigned long)torb_pool_sum_live_blocks());
  fprintf(stderr, "immortal blocks at exit: %lu\n", (unsigned long)torb_pool_sum_immortal_blocks());
}

unsigned torb_count_in_machine(unsigned inside) {
  torb_heap *heap = torb_heap_current();
  unsigned before = heap->in_machine;
  heap->in_machine = inside;
  /* Set once, by the first thread that enters the kernel; every call after it only reads it */
  if (inside != 0u && torb_atomic_load_u8(&torb_counts_machine) == 0u) {
    torb_atomic_store_u8(&torb_counts_machine, 1u);
  }
  return before;
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

/* ------------------------------------------------------------------------------------------ the memory limit --- */

/*
 * A runaway program must never take the machine down with it: a test binary once committed 88 GB, and Windows paged
 * until nothing answered any more. So every process has a memory limit, set here before the program runs (torb.h, the
 * memory limit): `TORB_MEMORY_LIMIT` where it is set, the dev default where the binary was built with
 * `TORB_PROFILE_DEV`, and none otherwise.
 *
 * The **operating system** enforces it wherever it can (`torb_platform_limit_memory`), because only the system sees
 * every byte - the C library's own, a thread's stack, what a library maps - and because a limit the system holds costs
 * the allocator nothing. An allocation over it fails, `malloc` answers `NULL`, and the panic below names the limit.
 * Where the system does not take it, the runtime **counts** instead: every block of `torb_allocate` and
 * `torb_raw_allocate` at the size the C allocator really gave it, added on the way in and taken off on the way out, and
 * an allocation that would go over the limit is freed again and ends the program the same way. That count sees only
 * the runtime's own blocks, which is almost all of a program's memory and none of the C library's.
 *
 * The dev default is the smaller of 8 GiB and half the physical memory: a test suite that legitimately needs more than
 * that is rare (the compiler's own peaks far below it), and a machine is still usable with half its memory taken. A
 * release binary has no default, because what it is allowed is its user's decision and not the toolchain's.
 */

#if defined(TORB_PROFILE_DEV)
#  define TORB_LIMITS_MEMORY_BY_DEFAULT 1
#else
#  define TORB_LIMITS_MEMORY_BY_DEFAULT 0
#endif

#define TORB_MEBIBYTE ((uint64_t)1024u * 1024u)
#define TORB_DEFAULT_LIMIT_CEILING ((uint64_t)8u * 1024u * TORB_MEBIBYTE)

/** The limit in force: 0 for none. Written once, before any thread starts, and by the runtime's tests. */
static uint64_t torb_memory_limit_bytes = 0u;
/** Whether the limit came from `TORB_MEMORY_LIMIT` (1) or is the dev default (0), which the message tells apart. */
static int torb_memory_limit_given = 0;
/** What the runtime holds against the limit where it counts: the sum of the sizes of its live blocks. */
static int64_t torb_memory_counted = 0;

bool torb_memory_size_parse(const char *text, uint64_t *bytes) {
  uint64_t value = 0u;
  uint64_t unit = 1u;
  size_t index = 0u;
  if (strcmp(text, "none") == 0) {
    *bytes = 0u;
    return true;
  }
  if (text[0] < '0' || text[0] > '9') {
    return false;
  }
  while (text[index] >= '0' && text[index] <= '9') {
    const uint64_t digit = (uint64_t)(text[index] - '0');
    if (value > (UINT64_MAX - digit) / 10u) {
      return false;
    }
    value = value * 10u + digit;
    index += 1u;
  }
  switch (text[index]) {
    case 'K':
    case 'k':
      unit = (uint64_t)1024u;
      break;
    case 'M':
    case 'm':
      unit = TORB_MEBIBYTE;
      break;
    case 'G':
    case 'g':
      unit = (uint64_t)1024u * TORB_MEBIBYTE;
      break;
    case 'T':
    case 't':
      unit = TORB_MEBIBYTE * TORB_MEBIBYTE;
      break;
    default:
      break;
  }
  if (unit != 1u) {
    index += 1u;
    if (text[index] == 'i' || text[index] == 'I') {
      index += 1u;
      if (text[index] != 'B' && text[index] != 'b') {
        return false;
      }
    }
  }
  if (text[index] == 'B' || text[index] == 'b') {
    index += 1u;
  }
  if (text[index] != '\0' || value > UINT64_MAX / unit) {
    return false;
  }
  *bytes = value * unit;
  return true;
}

/** `8 GiB`, `64 MiB`, `1536 KiB`, and `1000 bytes` for a size that is no whole number of KiB. */
static void torb_memory_size_text(char *buffer, size_t capacity, uint64_t bytes) {
  static const char *const names[] = { "TiB", "GiB", "MiB", "KiB" };
  static const unsigned shifts[] = { 40u, 30u, 20u, 10u };
  size_t index;
  for (index = 0u; index < sizeof shifts / sizeof shifts[0]; index++) {
    const uint64_t unit = (uint64_t)1u << shifts[index];
    if (bytes >= unit && bytes % unit == 0u) {
      snprintf(buffer, capacity, "%llu %s", (unsigned long long)(bytes / unit), names[index]);
      return;
    }
  }
  snprintf(buffer, capacity, "%llu bytes", (unsigned long long)bytes);
}

/** The smaller of 8 GiB and half the physical memory, in whole MiB; 8 GiB where the platform does not say. */
static uint64_t torb_memory_default_limit(void) {
  uint64_t half = torb_platform_physical_memory() / 2u;
  half -= half % TORB_MEBIBYTE;
  if (half == 0u || half > TORB_DEFAULT_LIMIT_CEILING) {
    return TORB_DEFAULT_LIMIT_CEILING;
  }
  return half;
}

void torb_memory_limit_start(void) {
  const char *given = getenv("TORB_MEMORY_LIMIT");
  const char *message = NULL;
  uint64_t limit = 0u;
#if defined(TORB_HOSTS_MACHINE)
  /*
   * A binary that runs programs in the VM - `torb` itself - gives `TORB_MEMORY_LIMIT` to the programs it runs: around
   * `torb run` the variable means the program, as it means the binary `torb build` wrote, and a limit that small would
   * stop the host while it compiles. The program's blocks are counted against it (they are the ones allocated inside a
   * call of the kernel), with the dev default where the variable is not set, because `torb run` builds the dev profile.
   * The host itself keeps the default of its own profile only.
   */
  if (given != NULL && given[0] != '\0') {
    if (!torb_memory_size_parse(given, &limit)) {
      fprintf(stderr,
              "error: TORB_MEMORY_LIMIT must be a number of bytes with an optional K, M, G or T (\"512M\", \"8G\"), "
              "or 0 or none for no limit, and it is \"%s\"\n",
              given);
      exit(2);
    }
    torb_memory_limit_given = 1;
  } else {
    limit = torb_memory_default_limit();
  }
  if (TORB_LIMITS_MEMORY_BY_DEFAULT) {
    (void)torb_platform_limit_memory(torb_memory_default_limit(), &message);
  }
  if (limit != 0u) {
    torb_memory_limit_bytes = limit;
    torb_memory_counted = 0;
    torb_memory_counts_machine = 1;
    torb_memory_counting = 1;
  }
  return;
#endif
  if (given != NULL && given[0] != '\0') {
    if (!torb_memory_size_parse(given, &limit)) {
      fprintf(stderr,
              "error: TORB_MEMORY_LIMIT must be a number of bytes with an optional K, M, G or T (\"512M\", \"8G\"), "
              "or 0 or none for no limit, and it is \"%s\"\n",
              given);
      exit(2);
    }
    torb_memory_limit_given = 1;
  } else if (TORB_LIMITS_MEMORY_BY_DEFAULT) {
    limit = torb_memory_default_limit();
  }
  if (limit == 0u) {
    return;
  }
  torb_memory_limit_bytes = limit;
  if (torb_platform_limit_memory(limit, &message)) {
    return;
  }
  torb_memory_counted = 0;
  torb_memory_counting = 1;
  /* Said once and only for a limit somebody asked for: the dev default is a safety net, not a request */
  if (message != NULL && torb_memory_limit_given != 0) {
    fprintf(stderr,
            "note: the operating system did not take TORB_MEMORY_LIMIT (%s), so the runtime counts its own "
            "allocations against it\n",
            message);
  }
}

uint64_t torb_memory_limit(void) {
  return torb_memory_limit_bytes;
}

void torb_memory_limit_counted(uint64_t limit) {
  torb_memory_limit_bytes = limit;
  torb_memory_limit_given = 1;
  torb_memory_counted = 0;
  torb_memory_counting = limit != 0u ? 1 : 0;
}

/* A block the C allocator gave out: counted, and freed again and the end of the program where it goes over the limit */
static void torb_memory_count(void *block) {
  int64_t size;
  if (torb_memory_counts_machine != 0 && torb_heap_current()->in_machine == 0u) {
    return;
  }
  size = (int64_t)torb_platform_allocation_size(block);
  const int64_t after = torb_atomic_add_i64(&torb_memory_counted, size) + size;
  if (after > 0 && (uint64_t)after > torb_memory_limit_bytes) {
    (void)torb_atomic_add_i64(&torb_memory_counted, -size);
    free(block);
    torb_panic_out_of_memory((size_t)size);
  }
}

static void torb_memory_uncount(void *block) {
  if (torb_memory_counts_machine != 0 && torb_heap_current()->in_machine == 0u) {
    return;
  }
  (void)torb_atomic_add_i64(&torb_memory_counted, -(int64_t)torb_platform_allocation_size(block));
}

void torb_memory_describe_exhaustion(char *buffer, size_t capacity, size_t size) {
  char limit[64];
  const char *source = torb_memory_limit_given != 0
                           ? "TORB_MEMORY_LIMIT"
                           : "the default of a dev build; TORB_MEMORY_LIMIT sets another, 0 none";
  if (torb_memory_limit_bytes == 0u) {
    snprintf(buffer, capacity, "out of memory: %llu bytes could not be allocated", (unsigned long long)size);
    return;
  }
  torb_memory_size_text(limit, sizeof limit, torb_memory_limit_bytes);
  if ((uint64_t)size > torb_memory_limit_bytes) {
    snprintf(buffer, capacity, "out of memory: %llu bytes were asked for at once, more than the limit of %s (%s)",
             (unsigned long long)size, limit, source);
    return;
  }
  snprintf(buffer, capacity, "out of memory: the limit of %s was reached (%s)", limit, source);
}
