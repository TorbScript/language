/*
 * memory.c - the block header, reference counting, make-unique, immortal values, the live-block counter.
 *
 * Counts are plain integers: every task owns its heap, so nothing has to be atomic (BACKEND 2.5). The heap is libc's
 * for now; the bump allocator with size-class free lists that section 2.5 describes is a replacement behind these six
 * functions and changes nothing above them.
 *
 * Every allocation failure panics: the language has no out-of-memory error.
 */

#include "torb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t torb_live_blocks = 0;

static void *torb_payload(void *block) {
  return (void *)((uint8_t *)block + sizeof(torb_header));
}

/* `drop` and `retain_children` receive the block itself, which is what an emitted `T_x *` is. */

void *torb_allocate(size_t size, torb_block_kind kind) {
  torb_header *header;
  if (size < sizeof(torb_header)) {
    size = sizeof(torb_header);
  }
  header = (torb_header *)malloc(size);
  if (header == NULL) {
    torb_panic_out_of_memory(size);
  }
  header->count = 1;
  header->kind = (uint16_t)kind;
  header->color = (uint16_t)TORB_COLOR_NONE;
  torb_live_blocks += 1;
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
  buffer = malloc(size);
  if (buffer == NULL) {
    torb_panic_out_of_memory(size);
  }
  torb_live_blocks += 1;
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
  torb_live_blocks -= 1;
}

void torb_retain(void *block) {
  torb_header *header = (torb_header *)block;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  header->count += 1;
}

void torb_release(void *block, torb_drop_function drop) {
  torb_header *header = (torb_header *)block;
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  if (header->count == 0) {
    torb_panic_text("internal error: released a block whose count is already zero", torb_location_unknown);
  }
  header->count -= 1;
  if (header->count != 0) {
    return;
  }
  if (drop != NULL) {
    drop(block);
  }
  free(block);
  torb_live_blocks -= 1;
}

void torb_environment_release(torb_environment *environment) {
  if (environment == NULL) {
    return;
  }
  torb_release(environment, environment->drop);
}

bool torb_is_unique(const void *block) {
  const torb_header *header = (const torb_header *)block;
  if (header == NULL) {
    return true;
  }
  return header->count == 1;
}

void *torb_make_unique(void *block,
                       size_t size,
                       torb_retain_children_function retain_children,
                       torb_drop_function drop) {
  torb_header *header = (torb_header *)block;
  void *copy;
  if (header == NULL || header->count == 1) {
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
  if (header == NULL || header->count == TORB_IMMORTAL_COUNT) {
    return;
  }
  header->count = TORB_IMMORTAL_COUNT;
  /* The block is never freed again, so it leaves the live count right away. */
  torb_live_blocks -= 1;
}

size_t torb_live_block_count(void) {
  return torb_live_blocks;
}

void torb_report_leaks(void) {
  fprintf(stderr, "live blocks at exit: %lu\n", (unsigned long)torb_live_blocks);
}
