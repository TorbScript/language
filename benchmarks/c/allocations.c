/*
 * The allocation counter, linked into a second copy of a benchmark with `--allocations`.
 *
 * `ld --wrap=malloc` routes every call of the three allocating functions through here, so the count is the program's
 * own and needs no change to the runtime. What is reported is the number of allocations and the number of bytes that
 * were asked for; a free is not wrapped, because how much is live at once is not what these programs are compared on.
 */
#include <stdio.h>
#include <stdlib.h>

static unsigned long long torb_bench_allocations = 0;
static unsigned long long torb_bench_bytes = 0;
static int torb_bench_registered = 0;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *block, size_t size);

static void torb_bench_report(void) {
  fprintf(stderr, "allocations %llu bytes %llu\n", torb_bench_allocations, torb_bench_bytes);
}

static void torb_bench_note(size_t size) {
  if (torb_bench_registered == 0) {
    torb_bench_registered = 1;
    atexit(torb_bench_report);
  }
  torb_bench_allocations += 1;
  torb_bench_bytes += (unsigned long long)size;
}

void *__wrap_malloc(size_t size) {
  torb_bench_note(size);
  return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
  torb_bench_note(count * size);
  return __real_calloc(count, size);
}

void *__wrap_realloc(void *block, size_t size) {
  torb_bench_note(size);
  return __real_realloc(block, size);
}
