/*
 * list.c - the one list implementation: contiguous growable storage with shared slices and copy on write.
 *
 * `ArrayList` and `TrieList` both map here (BACKEND 3.5), which is observable only through performance because
 * iteration order and `Show` are the same either way. It is parameterized by a static `torb_element` descriptor
 * instead of by a template, so the VM and the C back end share one implementation.
 *
 * A slice shares the storage (`offset`, `length`) and starts at index 0 again. A write prepares the storage first:
 * it goes through in place only when this list owns the whole storage alone. Otherwise the elements of the slice are
 * copied into a storage of their own, which is what makes `list = list.added(x)` change in place at the last use and
 * a shared list copy exactly once.
 *
 * An empty list still has a storage of capacity zero, because `torb_list` has no room for the descriptor.
 */

#include "torb.h"
#include "torb_pool.h"

#include <stddef.h>
#include <string.h>

size_t torb_list_storage_data_offset(const torb_element *element) {
  size_t base = sizeof(torb_list_storage);
  size_t align = element->align == 0u ? 1u : (size_t)element->align;
  return (base + align - 1u) / align * align;
}

void *torb_list_storage_data(torb_list_storage *storage) {
  return (void *)((uint8_t *)storage + torb_list_storage_data_offset(storage->element));
}

static uint8_t *torb_list_bytes(torb_list list) {
  return (uint8_t *)torb_list_storage_data(list.storage) + (size_t)list.offset * (size_t)list.storage->element->size;
}

static void torb_retain_elements(const torb_element *element, uint8_t *data, uint32_t count) {
  uint32_t index;
  if (element->retain == NULL) {
    return;
  }
  for (index = 0u; index < count; index += 1u) {
    torb_element_retain(element, data + (size_t)index * (size_t)element->size);
  }
}

/*
 * The last element first: a list is taken down in the reverse of the order it was built, so an element with a
 * destructor closes after every element that was added after it (docs/design/DESTRUCTORS.md 2a).
 */
static void torb_release_elements(const torb_element *element, uint8_t *data, uint32_t count) {
  uint32_t index;
  if (element->release == NULL) {
    return;
  }
  for (index = count; index > 0u; index -= 1u) {
    torb_element_release(element, data + (size_t)(index - 1u) * (size_t)element->size);
  }
}

static void torb_list_storage_drop(void *block) {
  torb_list_storage *storage = (torb_list_storage *)block;
  torb_release_elements(storage->element, (uint8_t *)torb_list_storage_data(storage), storage->length);
}

static torb_list_storage *torb_list_storage_new(const torb_element *element, uint32_t capacity) {
  size_t offset = torb_list_storage_data_offset(element);
  size_t bytes;
  torb_list_storage *storage;
  /* The capacity is checked in elements; in bytes it can only wrap where `size_t` is 32 bits wide */
  if (element->size != 0u && (size_t)capacity > (SIZE_MAX - offset) / (size_t)element->size) {
    torb_panic_text("a list that large is not supported", torb_location_unknown);
  }
  bytes = offset + (size_t)capacity * (size_t)element->size;
  storage = (torb_list_storage *)torb_allocate(bytes, TORB_BLOCK_LIST_STORAGE);
  storage->element = element;
  storage->length = 0u;
  storage->capacity = capacity;
  return storage;
}

static uint32_t torb_grown_capacity(uint32_t capacity, uint32_t needed) {
  uint32_t grown = capacity < 4u ? 4u : capacity;
  while (grown < needed) {
    if (grown > UINT32_MAX / 2u) {
      torb_panic_text("a list longer than 4 294 967 295 elements is not supported", torb_location_unknown);
    }
    grown *= 2u;
  }
  return grown;
}

/**
 * Make the list ready for a write of `extra` more elements: afterwards its storage has count 1, `offset` is 0, the
 * storage's length is the list's length, and there is room for `extra` more.
 */
static void torb_list_prepare(torb_list *list, uint32_t extra) {
  torb_list_storage *storage = list->storage;
  const torb_element *element = storage->element;
  uint32_t needed;
  if (extra > UINT32_MAX - list->length) {
    torb_panic_text("a list longer than 4 294 967 295 elements is not supported", torb_location_unknown);
  }
  needed = list->length + extra;
  if (torb_is_unique(storage) && list->offset == 0u && list->length == storage->length) {
    if (storage->capacity >= needed) {
      return;
    }
    {
      /* Grow in place: one fresh storage, the elements moved over without touching a count. */
      torb_list_storage *grown = torb_list_storage_new(element, torb_grown_capacity(storage->capacity, needed));
      memcpy(torb_list_storage_data(grown), torb_list_storage_data(storage),
             (size_t)storage->length * (size_t)element->size);
      grown->length = storage->length;
      storage->length = 0u; /* The elements moved; the old storage must not release them. */
      torb_release(storage, torb_list_storage_drop);
      list->storage = grown;
      return;
    }
  }
  {
    torb_list_storage *copy = torb_list_storage_new(element, torb_grown_capacity(0u, needed));
    uint8_t *source = torb_list_bytes(*list);
    memcpy(torb_list_storage_data(copy), source, (size_t)list->length * (size_t)element->size);
    copy->length = list->length;
    torb_retain_elements(element, (uint8_t *)torb_list_storage_data(copy), list->length);
    torb_release(storage, torb_list_storage_drop);
    list->storage = copy;
    list->offset = 0u;
  }
}

torb_list torb_list_new(const torb_element *element) {
  torb_list list;
  list.storage = torb_list_storage_new(element, 0u);
  list.offset = 0u;
  list.length = 0u;
  return list;
}

torb_list torb_list_with_capacity(const torb_element *element, int64_t capacity, torb_location at) {
  torb_list list;
  if (capacity < 0 || capacity > (int64_t)UINT32_MAX) {
    torb_panic_index_out_of_bounds(capacity, (int64_t)UINT32_MAX, at);
  }
  list.storage = torb_list_storage_new(element, (uint32_t)capacity);
  list.offset = 0u;
  list.length = 0u;
  return list;
}

torb_list torb_list_retained(torb_list list) {
  torb_retain(list.storage);
  return list;
}

void torb_list_release(torb_list list) {
  torb_release(list.storage, torb_list_storage_drop);
}

const torb_element *torb_list_element(torb_list list) {
  return list.storage->element;
}

int64_t torb_list_length(torb_list list) {
  return (int64_t)list.length;
}

const void *torb_list_at(torb_list list, int64_t index, torb_location at) {
  if (index < 0 || index >= (int64_t)list.length) {
    torb_panic_index_out_of_bounds(index, (int64_t)list.length, at);
  }
  return torb_list_bytes(list) + (size_t)index * (size_t)list.storage->element->size;
}

bool torb_list_get(torb_list list, int64_t index, void *out) {
  const torb_element *element = list.storage->element;
  if (index < 0 || index >= (int64_t)list.length) {
    return false;
  }
  memcpy(out, torb_list_bytes(list) + (size_t)index * (size_t)element->size, (size_t)element->size);
  if (element->retain != NULL) {
    torb_element_retain(element, out);
  }
  return true;
}

void torb_list_make_unique(torb_list *list) {
  torb_list_prepare(list, 0u);
}

/*
 * The copy at a crossing (torb_task.h). A storage somebody else holds, or a slice of one whose elements are counted -
 * the elements outside the slice would be released wherever the storage is - is copied first, which retains the
 * elements; then every element of the storage that is now this list's alone is made private in its place.
 */
bool torb_privatize_plain(const void *context, void *place) {
  return (*(const torb_privatize_function *)context)(place);
}

bool torb_list_privatize(torb_list *list, torb_privatize_function element) {
  return torb_list_privatize_with(list, element == NULL ? NULL : torb_privatize_plain, &element);
}

bool torb_list_privatize_with(torb_list *list, torb_privatize_with_function element, const void *context) {
  torb_list_storage *storage = list->storage;
  uint8_t *data;
  uint32_t index;
  if (storage == NULL || storage->header.count == TORB_IMMORTAL_COUNT) {
    return true;
  }
  if (element == NULL && storage->header.count == 1u) {
    return true;
  }
  if (!(torb_is_unique(storage) && list->offset == 0u && list->length == storage->length)) {
    torb_list_prepare(list, 0u);
    torb_pool_count_copy();
  }
  if (element == NULL) {
    return true;
  }
  storage = list->storage;
  data = (uint8_t *)torb_list_storage_data(storage);
  for (index = 0u; index < storage->length; index += 1u) {
    if (!element(context, data + (size_t)index * (size_t)storage->element->size)) {
      return false;
    }
  }
  return true;
}

void *torb_list_element_address(torb_list *list, int64_t index, torb_location at) {
  if (index < 0 || index >= (int64_t)list->length) {
    torb_panic_index_out_of_bounds(index, (int64_t)list->length, at);
  }
  torb_list_prepare(list, 0u);
  return torb_list_bytes(*list) + (size_t)index * (size_t)list->storage->element->size;
}

void *torb_list_element_reference(torb_list *list, int64_t index, torb_text missing, torb_location at) {
  if (index < 0 || index >= (int64_t)list->length) {
    torb_panic(missing, at);
  }
  return torb_list_element_address(list, index, at);
}

void torb_list_add(torb_list *list, const void *value) {
  const torb_element *element = list->storage->element;
  torb_list_prepare(list, 1u);
  memcpy(torb_list_bytes(*list) + (size_t)list->length * (size_t)element->size, value, (size_t)element->size);
  list->length += 1u;
  list->storage->length = list->length;
}

void torb_list_add_all(torb_list *list, torb_list values) {
  const torb_element *element = list->storage->element;
  uint8_t *target;
  torb_list_prepare(list, values.length);
  target = torb_list_bytes(*list) + (size_t)list->length * (size_t)element->size;
  memcpy(target, torb_list_bytes(values), (size_t)values.length * (size_t)element->size);
  torb_retain_elements(element, target, values.length);
  list->length += values.length;
  list->storage->length = list->length;
}

void torb_list_add_plain(torb_list *list, const void *values, size_t count) {
  const torb_element *element = list->storage->element;
  if (count == 0u) {
    return;
  }
  if (count > (size_t)UINT32_MAX) {
    torb_panic_text("a list longer than 4 294 967 295 elements is not supported", torb_location_unknown);
  }
  if (element->retain != NULL || element->release != NULL) {
    torb_panic_text("internal error: plain elements appended to a list of counted ones", torb_location_unknown);
  }
  torb_list_prepare(list, (uint32_t)count);
  memcpy(torb_list_bytes(*list) + (size_t)list->length * (size_t)element->size, values, count * (size_t)element->size);
  list->length += (uint32_t)count;
  list->storage->length = list->length;
}

void torb_list_set(torb_list *list, int64_t index, const void *value, torb_location at) {
  const torb_element *element = list->storage->element;
  uint8_t *target;
  if (index < 0 || index >= (int64_t)list->length) {
    torb_panic_index_out_of_bounds(index, (int64_t)list->length, at);
  }
  torb_list_prepare(list, 0u);
  target = torb_list_bytes(*list) + (size_t)index * (size_t)element->size;
  if (element->release != NULL) {
    torb_element_release(element, target);
  }
  memcpy(target, value, (size_t)element->size);
}

void torb_list_insert(torb_list *list, int64_t index, const void *value, torb_location at) {
  const torb_element *element = list->storage->element;
  uint8_t *base;
  if (index < 0 || index > (int64_t)list->length) {
    torb_panic_index_out_of_bounds(index, (int64_t)list->length, at);
  }
  torb_list_prepare(list, 1u);
  base = torb_list_bytes(*list);
  memmove(base + (size_t)(index + 1) * (size_t)element->size, base + (size_t)index * (size_t)element->size,
          (size_t)((int64_t)list->length - index) * (size_t)element->size);
  memcpy(base + (size_t)index * (size_t)element->size, value, (size_t)element->size);
  list->length += 1u;
  list->storage->length = list->length;
}

bool torb_list_remove_at(torb_list *list, int64_t index, void *out) {
  const torb_element *element = list->storage->element;
  uint8_t *base;
  if (index < 0 || index >= (int64_t)list->length) {
    return false;
  }
  torb_list_prepare(list, 0u);
  base = torb_list_bytes(*list);
  memcpy(out, base + (size_t)index * (size_t)element->size, (size_t)element->size);
  memmove(base + (size_t)index * (size_t)element->size, base + (size_t)(index + 1) * (size_t)element->size,
          (size_t)((int64_t)list->length - index - 1) * (size_t)element->size);
  list->length -= 1u;
  list->storage->length = list->length;
  return true;
}

void torb_list_replace(torb_list *list, int64_t from, int64_t to, torb_list values, torb_location at) {
  const torb_element *element = list->storage->element;
  int64_t removed;
  int64_t difference;
  uint8_t *base;
  if (from < 0 || to > (int64_t)list->length) {
    torb_panic_index_out_of_bounds(from < 0 ? from : to, (int64_t)list->length, at);
  }
  if (from > to) {
    torb_panic_range_reversed(from, to, at);
  }
  removed = to - from;
  difference = (int64_t)values.length - removed;
  if (difference > 0) {
    torb_list_prepare(list, (uint32_t)difference);
  } else {
    torb_list_prepare(list, 0u);
  }
  base = torb_list_bytes(*list);
  torb_release_elements(element, base + (size_t)from * (size_t)element->size, (uint32_t)removed);
  memmove(base + (size_t)(to + difference) * (size_t)element->size, base + (size_t)to * (size_t)element->size,
          (size_t)((int64_t)list->length - to) * (size_t)element->size);
  memcpy(base + (size_t)from * (size_t)element->size, torb_list_bytes(values),
         (size_t)values.length * (size_t)element->size);
  torb_retain_elements(element, base + (size_t)from * (size_t)element->size, values.length);
  list->length = (uint32_t)((int64_t)list->length + difference);
  list->storage->length = list->length;
}

void torb_list_reverse(torb_list *list) {
  const torb_element *element = list->storage->element;
  uint8_t scratch[64];
  uint8_t *buffer = element->size <= sizeof scratch ? scratch : (uint8_t *)torb_raw_allocate(element->size);
  uint8_t *base;
  uint32_t low = 0u;
  uint32_t high;
  torb_list_prepare(list, 0u);
  base = torb_list_bytes(*list);
  high = list->length == 0u ? 0u : list->length - 1u;
  while (low < high) {
    memcpy(buffer, base + (size_t)low * (size_t)element->size, (size_t)element->size);
    memcpy(base + (size_t)low * (size_t)element->size, base + (size_t)high * (size_t)element->size,
           (size_t)element->size);
    memcpy(base + (size_t)high * (size_t)element->size, buffer, (size_t)element->size);
    low += 1u;
    high -= 1u;
  }
  if (buffer != scratch) {
    torb_raw_free(buffer, element->size);
  }
}

void torb_list_clear(torb_list *list) {
  const torb_element *element = list->storage->element;
  if (torb_is_unique(list->storage) && list->offset == 0u && list->length == list->storage->length) {
    torb_release_elements(element, (uint8_t *)torb_list_storage_data(list->storage), list->length);
    list->length = 0u;
    list->storage->length = 0u;
    return;
  }
  {
    torb_list_storage *empty = torb_list_storage_new(element, 0u);
    torb_release(list->storage, torb_list_storage_drop);
    list->storage = empty;
    list->offset = 0u;
    list->length = 0u;
  }
}

void torb_list_compact(torb_list *list) {
  const torb_element *element = list->storage->element;
  torb_list_storage *copy;
  if (torb_is_unique(list->storage) && list->offset == 0u && list->length == list->storage->length
      && list->storage->capacity == list->length) {
    return;
  }
  copy = torb_list_storage_new(element, list->length);
  memcpy(torb_list_storage_data(copy), torb_list_bytes(*list), (size_t)list->length * (size_t)element->size);
  copy->length = list->length;
  torb_retain_elements(element, (uint8_t *)torb_list_storage_data(copy), list->length);
  torb_release(list->storage, torb_list_storage_drop);
  list->storage = copy;
  list->offset = 0u;
}

torb_list torb_list_slice_copied(torb_list list, int64_t from, int64_t to, torb_location at) {
  torb_list result = torb_list_slice(list, from, to, at);
  torb_list_compact(&result);
  return result;
}

torb_list torb_list_slice(torb_list list, int64_t from, int64_t to, torb_location at) {
  torb_list result;
  if (from < 0 || to > (int64_t)list.length) {
    torb_panic_index_out_of_bounds(from < 0 ? from : to, (int64_t)list.length, at);
  }
  if (from > to) {
    torb_panic_range_reversed(from, to, at);
  }
  result.storage = list.storage;
  result.offset = list.offset + (uint32_t)from;
  result.length = (uint32_t)(to - from);
  torb_retain(result.storage);
  return result;
}

/* A stable merge sort, so the order is deterministic whatever the comparison does with equal keys. */
static void torb_merge(uint8_t *data, uint8_t *scratch, uint32_t low, uint32_t middle, uint32_t high, uint32_t size,
                       torb_compare_function compare, void *context) {
  uint32_t left = low;
  uint32_t right = middle;
  uint32_t target = low;
  while (left < middle && right < high) {
    if (compare(data + (size_t)right * size, data + (size_t)left * size, context) < 0) {
      memcpy(scratch + (size_t)target * size, data + (size_t)right * size, size);
      right += 1u;
    } else {
      memcpy(scratch + (size_t)target * size, data + (size_t)left * size, size);
      left += 1u;
    }
    target += 1u;
  }
  while (left < middle) {
    memcpy(scratch + (size_t)target * size, data + (size_t)left * size, size);
    left += 1u;
    target += 1u;
  }
  while (right < high) {
    memcpy(scratch + (size_t)target * size, data + (size_t)right * size, size);
    right += 1u;
    target += 1u;
  }
  memcpy(data + (size_t)low * size, scratch + (size_t)low * size, (size_t)(high - low) * size);
}

static void torb_sort_range(uint8_t *data, uint8_t *scratch, uint32_t low, uint32_t high, uint32_t size,
                            torb_compare_function compare, void *context) {
  uint32_t middle;
  if (high - low < 2u) {
    return;
  }
  middle = low + (high - low) / 2u;
  torb_sort_range(data, scratch, low, middle, size, compare, context);
  torb_sort_range(data, scratch, middle, high, size, compare, context);
  torb_merge(data, scratch, low, middle, high, size, compare, context);
}

void torb_list_sort(torb_list *list, torb_compare_function compare, void *context) {
  const torb_element *element = list->storage->element;
  size_t bytes;
  uint8_t *scratch;
  if (list->length < 2u) {
    return;
  }
  torb_list_prepare(list, 0u);
  bytes = (size_t)list->length * (size_t)element->size;
  scratch = (uint8_t *)torb_raw_allocate(bytes);
  torb_sort_range(torb_list_bytes(*list), scratch, 0u, list->length, element->size, compare, context);
  torb_raw_free(scratch, bytes);
}
