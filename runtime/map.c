/*
 * map.c - the one insertion-ordered hash table, and the set on top of it.
 *
 * `HashMap`/`TrieMap` and `HashSet`/`TrieSet` all map here (BACKEND 3.5). Open addressing over a bucket array of
 * indices, plus a separate insertion-ordered entry vector:
 *
 *   - iteration is insertion order, for every map and every set (decided gap 6),
 *   - a removal leaves a tombstone and reorders nothing,
 *   - compaction happens when the tombstones pass half of the entries.
 *
 * A set is the table with `torb_element_unit` (size zero) on the value side, so there is exactly one hash table in
 * the process. Both element descriptors are static data and drive retain, release, equality and hashing, so neither
 * the VM nor the C back end needs a template.
 *
 * The seed of the hash is fixed (`torb_hash_bytes`), so a run of the compiler is reproducible.
 */

#include "torb.h"

#include <string.h>

static size_t torb_align_up(size_t value, size_t align) {
  if (align == 0u) {
    align = 1u;
  }
  return (value + align - 1u) / align * align;
}

#define TORB_ENTRY_HASH_OFFSET 0u
#define TORB_ENTRY_ALIVE_OFFSET 8u
#define TORB_ENTRY_HEAD 12u

static uint8_t *torb_entry_at(const torb_map_storage *storage, uint32_t index) {
  return storage->entries + (size_t)index * (size_t)storage->entry_stride;
}

static uint64_t torb_entry_hash(const torb_map_storage *storage, uint32_t index) {
  uint64_t hash;
  memcpy(&hash, torb_entry_at(storage, index) + TORB_ENTRY_HASH_OFFSET, sizeof hash);
  return hash;
}

static bool torb_entry_alive(const torb_map_storage *storage, uint32_t index) {
  uint32_t alive;
  memcpy(&alive, torb_entry_at(storage, index) + TORB_ENTRY_ALIVE_OFFSET, sizeof alive);
  return alive != 0u;
}

static void torb_entry_set_alive(torb_map_storage *storage, uint32_t index, bool alive) {
  uint32_t value = alive ? 1u : 0u;
  memcpy(torb_entry_at(storage, index) + TORB_ENTRY_ALIVE_OFFSET, &value, sizeof value);
}

static uint8_t *torb_entry_key(const torb_map_storage *storage, uint32_t index) {
  return torb_entry_at(storage, index) + storage->key_offset;
}

static uint8_t *torb_entry_value(const torb_map_storage *storage, uint32_t index) {
  return torb_entry_at(storage, index) + storage->value_offset;
}

static uint32_t torb_next_power_of_two(uint32_t value) {
  uint32_t power = 8u;
  while (power < value) {
    if (power > UINT32_MAX / 2u) {
      torb_panic_text("a map that large is not supported", torb_location_unknown);
    }
    power *= 2u;
  }
  return power;
}

static void torb_map_storage_drop(void *block) {
  torb_map_storage *storage = (torb_map_storage *)block;
  uint32_t index;
  for (index = 0u; index < storage->entry_count; index += 1u) {
    if (!torb_entry_alive(storage, index)) {
      continue;
    }
    if (storage->key->release != NULL) {
      storage->key->release(torb_entry_key(storage, index));
    }
    if (storage->value->release != NULL) {
      storage->value->release(torb_entry_value(storage, index));
    }
  }
  torb_raw_free(storage->buckets, (size_t)storage->bucket_count * sizeof(int32_t));
  torb_raw_free(storage->entries, (size_t)storage->entry_capacity * (size_t)storage->entry_stride);
}

static torb_map_storage *torb_map_storage_new(const torb_element *key, const torb_element *value) {
  torb_map_storage *storage = (torb_map_storage *)torb_allocate(sizeof(torb_map_storage), TORB_BLOCK_MAP_STORAGE);
  size_t key_offset = torb_align_up(TORB_ENTRY_HEAD, key->align < 4u ? 4u : key->align);
  size_t value_offset = torb_align_up(key_offset + key->size, value->align);
  size_t stride_align = 8u;
  if (key->align > stride_align) {
    stride_align = key->align;
  }
  if (value->align > stride_align) {
    stride_align = value->align;
  }
  storage->key = key;
  storage->value = value;
  storage->buckets = NULL;
  storage->entries = NULL;
  storage->bucket_count = 0u;
  storage->entry_count = 0u;
  storage->entry_capacity = 0u;
  storage->live_count = 0u;
  storage->key_offset = (uint32_t)key_offset;
  storage->value_offset = (uint32_t)value_offset;
  storage->entry_stride = (uint32_t)torb_align_up(value_offset + value->size, stride_align);
  return storage;
}

/** Fills the bucket array from the entry vector. The entries keep their order, so insertion order survives. */
static void torb_map_reindex(torb_map_storage *storage) {
  uint32_t index;
  uint32_t mask;
  for (index = 0u; index < storage->bucket_count; index += 1u) {
    storage->buckets[index] = -1;
  }
  if (storage->bucket_count == 0u) {
    return;
  }
  mask = storage->bucket_count - 1u;
  for (index = 0u; index < storage->entry_count; index += 1u) {
    uint32_t bucket;
    if (!torb_entry_alive(storage, index)) {
      continue;
    }
    bucket = (uint32_t)(torb_entry_hash(storage, index) & (uint64_t)mask);
    while (storage->buckets[bucket] >= 0) {
      bucket = (bucket + 1u) & mask;
    }
    storage->buckets[bucket] = (int32_t)index;
  }
}

/** Room for `wanted` entries in total: grows the buffers, drops the tombstones, and rebuilds the buckets. */
static void torb_map_reserve(torb_map_storage *storage, uint32_t wanted) {
  uint32_t capacity = storage->entry_capacity < 8u ? 8u : storage->entry_capacity;
  uint32_t buckets;
  uint8_t *entries;
  uint32_t index;
  uint32_t target = 0u;
  while (capacity < wanted) {
    if (capacity > UINT32_MAX / 2u) {
      torb_panic_text("a map that large is not supported", torb_location_unknown);
    }
    capacity *= 2u;
  }
  entries = (uint8_t *)torb_raw_allocate_zeroed((size_t)capacity * (size_t)storage->entry_stride);
  for (index = 0u; index < storage->entry_count; index += 1u) {
    if (!torb_entry_alive(storage, index)) {
      continue;
    }
    memcpy(entries + (size_t)target * (size_t)storage->entry_stride, torb_entry_at(storage, index),
           (size_t)storage->entry_stride);
    target += 1u;
  }
  torb_raw_free(storage->entries, (size_t)storage->entry_capacity * (size_t)storage->entry_stride);
  storage->entries = entries;
  storage->entry_capacity = capacity;
  storage->entry_count = target;
  storage->live_count = target;
  buckets = torb_next_power_of_two(capacity * 2u);
  if (buckets != storage->bucket_count) {
    torb_raw_free(storage->buckets, (size_t)storage->bucket_count * sizeof(int32_t));
    storage->buckets = (int32_t *)torb_raw_allocate((size_t)buckets * sizeof(int32_t));
    storage->bucket_count = buckets;
  }
  torb_map_reindex(storage);
}

/**
 * The entry index of `key`, or -1. `free_bucket` receives the bucket an insertion would take: the first bucket of the
 * probe chain that is empty or points at a tombstone.
 */
static int32_t torb_map_lookup(const torb_map_storage *storage, const void *key, uint64_t hash, uint32_t *free_bucket) {
  uint32_t mask;
  uint32_t bucket;
  bool have_free = false;
  if (storage->bucket_count == 0u) {
    return -1;
  }
  mask = storage->bucket_count - 1u;
  bucket = (uint32_t)(hash & (uint64_t)mask);
  for (;;) {
    int32_t entry = storage->buckets[bucket];
    if (entry < 0) {
      if (!have_free) {
        *free_bucket = bucket;
      }
      return -1;
    }
    if (!torb_entry_alive(storage, (uint32_t)entry)) {
      if (!have_free) {
        *free_bucket = bucket;
        have_free = true;
      }
    } else if (torb_entry_hash(storage, (uint32_t)entry) == hash
               && storage->key->equals(torb_entry_key(storage, (uint32_t)entry), key)) {
      return entry;
    }
    bucket = (bucket + 1u) & mask;
  }
}

/** Copy on write: after this the storage has count 1 and belongs to this map alone. */
static void torb_map_prepare(torb_map *map) {
  torb_map_storage *storage = map->storage;
  torb_map_storage *copy;
  uint32_t index;
  if (torb_is_unique(storage)) {
    return;
  }
  copy = torb_map_storage_new(storage->key, storage->value);
  if (storage->live_count > 0u) {
    torb_map_reserve(copy, storage->live_count);
  }
  for (index = 0u; index < storage->entry_count; index += 1u) {
    uint8_t *target;
    if (!torb_entry_alive(storage, index)) {
      continue;
    }
    target = torb_entry_at(copy, copy->entry_count);
    memcpy(target, torb_entry_at(storage, index), (size_t)storage->entry_stride);
    if (copy->key->retain != NULL) {
      copy->key->retain(target + copy->key_offset);
    }
    if (copy->value->retain != NULL) {
      copy->value->retain(target + copy->value_offset);
    }
    copy->entry_count += 1u;
    copy->live_count += 1u;
  }
  torb_map_reindex(copy);
  torb_release(storage, torb_map_storage_drop);
  map->storage = copy;
}

torb_map torb_map_new(const torb_element *key, const torb_element *value) {
  torb_map map;
  if (key->equals == NULL || key->hash == NULL) {
    torb_panic_text("internal error: a map key needs `equals` and `hash`", torb_location_unknown);
  }
  map.storage = torb_map_storage_new(key, value);
  return map;
}

torb_map torb_map_with_capacity(const torb_element *key, const torb_element *value, int64_t capacity,
                               torb_location at) {
  torb_map map = torb_map_new(key, value);
  if (capacity < 0 || capacity > (int64_t)UINT32_MAX / 4) {
    torb_panic_index_out_of_bounds(capacity, (int64_t)UINT32_MAX / 4, at);
  }
  if (capacity > 0) {
    torb_map_reserve(map.storage, (uint32_t)capacity);
  }
  return map;
}

torb_map torb_map_retained(torb_map map) {
  torb_retain(map.storage);
  return map;
}

void torb_map_release(torb_map map) {
  torb_release(map.storage, torb_map_storage_drop);
}

int64_t torb_map_length(torb_map map) {
  return (int64_t)map.storage->live_count;
}

const void *torb_map_at(torb_map map, const void *key) {
  uint32_t free_bucket = 0u;
  int32_t entry = torb_map_lookup(map.storage, key, map.storage->key->hash(key), &free_bucket);
  if (entry < 0) {
    return NULL;
  }
  return torb_entry_value(map.storage, (uint32_t)entry);
}

bool torb_map_contains(torb_map map, const void *key) {
  uint32_t free_bucket = 0u;
  return torb_map_lookup(map.storage, key, map.storage->key->hash(key), &free_bucket) >= 0;
}

bool torb_map_get(torb_map map, const void *key, void *out) {
  const void *found = torb_map_at(map, key);
  if (found == NULL) {
    return false;
  }
  if (map.storage->value->size > 0u) {
    memcpy(out, found, (size_t)map.storage->value->size);
  }
  if (map.storage->value->retain != NULL) {
    map.storage->value->retain(out);
  }
  return true;
}

void torb_map_set(torb_map *map, const void *key, const void *value) {
  torb_map_storage *storage;
  uint64_t hash;
  uint32_t free_bucket = 0u;
  int32_t entry;
  torb_map_prepare(map);
  storage = map->storage;
  hash = storage->key->hash(key);
  entry = torb_map_lookup(storage, key, hash, &free_bucket);
  if (entry >= 0) {
    /* The key keeps its place in the insertion order and its stored spelling; only the value changes. */
    uint8_t *stored = torb_entry_value(storage, (uint32_t)entry);
    if (storage->key->release != NULL) {
      storage->key->release((void *)key);
    }
    if (storage->value->release != NULL) {
      storage->value->release(stored);
    }
    if (storage->value->size > 0u) {
      memcpy(stored, value, (size_t)storage->value->size);
    }
    return;
  }
  if (storage->entry_count == storage->entry_capacity) {
    torb_map_reserve(storage, storage->live_count + 1u);
    hash = storage->key->hash(key);
    entry = torb_map_lookup(storage, key, hash, &free_bucket);
    (void)entry;
  }
  {
    uint32_t index = storage->entry_count;
    uint8_t *target = torb_entry_at(storage, index);
    memcpy(target + TORB_ENTRY_HASH_OFFSET, &hash, sizeof hash);
    torb_entry_set_alive(storage, index, true);
    memcpy(target + storage->key_offset, key, (size_t)storage->key->size);
    if (storage->value->size > 0u) {
      memcpy(target + storage->value_offset, value, (size_t)storage->value->size);
    }
    storage->buckets[free_bucket] = (int32_t)index;
    storage->entry_count += 1u;
    storage->live_count += 1u;
  }
}

bool torb_map_remove(torb_map *map, const void *key, void *out) {
  torb_map_storage *storage;
  uint32_t free_bucket = 0u;
  int32_t entry;
  torb_map_prepare(map);
  storage = map->storage;
  entry = torb_map_lookup(storage, key, storage->key->hash(key), &free_bucket);
  if (entry < 0) {
    return false;
  }
  if (storage->value->size > 0u) {
    memcpy(out, torb_entry_value(storage, (uint32_t)entry), (size_t)storage->value->size);
  }
  if (storage->key->release != NULL) {
    storage->key->release(torb_entry_key(storage, (uint32_t)entry));
  }
  torb_entry_set_alive(storage, (uint32_t)entry, false);
  storage->live_count -= 1u;
  /* A tombstone; nothing is reordered. Compaction happens when they reach half of the entries. */
  if (storage->entry_count > 16u && storage->live_count * 2u <= storage->entry_count) {
    torb_map_reserve(storage, storage->live_count == 0u ? 1u : storage->live_count);
  }
  return true;
}

void torb_map_clear(torb_map *map) {
  torb_map_storage *empty = torb_map_storage_new(map->storage->key, map->storage->value);
  torb_release(map->storage, torb_map_storage_drop);
  map->storage = empty;
}

bool torb_map_take_out(torb_map *map, const void *key, void *out) {
  torb_map_storage *storage;
  uint32_t free_bucket = 0u;
  int32_t entry;
  torb_map_prepare(map);
  storage = map->storage;
  entry = torb_map_lookup(storage, key, storage->key->hash(key), &free_bucket);
  if (entry < 0) {
    return false;
  }
  if (storage->value->size > 0u) {
    uint8_t *stored = torb_entry_value(storage, (uint32_t)entry);
    memcpy(out, stored, (size_t)storage->value->size);
    /* The entry stays live and its value bytes are zero until `put_back`. Exclusivity (TYPECHECKER 5.2) means
       nothing can look at it in between. */
    memset(stored, 0, (size_t)storage->value->size);
  }
  return true;
}

void torb_map_put_back(torb_map *map, const void *key, const void *value) {
  torb_map_storage *storage = map->storage;
  uint32_t free_bucket = 0u;
  int32_t entry = torb_map_lookup(storage, key, storage->key->hash(key), &free_bucket);
  if (entry < 0) {
    torb_panic_text("internal error: `put back` without a matching `take out`", torb_location_unknown);
  }
  if (storage->value->size > 0u) {
    memcpy(torb_entry_value(storage, (uint32_t)entry), value, (size_t)storage->value->size);
  }
}

bool torb_map_next(torb_map map, uint32_t *cursor, const void **key, const void **value) {
  torb_map_storage *storage = map.storage;
  while (*cursor < storage->entry_count) {
    uint32_t index = *cursor;
    *cursor += 1u;
    if (!torb_entry_alive(storage, index)) {
      continue;
    }
    *key = torb_entry_key(storage, index);
    if (value != NULL) {
      *value = torb_entry_value(storage, index);
    }
    return true;
  }
  return false;
}

/* ----------------------------------------------------------------------------------------------------- sets --- */

torb_set torb_set_new(const torb_element *item) {
  return torb_map_new(item, &torb_element_unit);
}

torb_set torb_set_retained(torb_set set) {
  return torb_map_retained(set);
}

void torb_set_release(torb_set set) {
  torb_map_release(set);
}

int64_t torb_set_length(torb_set set) {
  return torb_map_length(set);
}

bool torb_set_contains(torb_set set, const void *item) {
  return torb_map_contains(set, item);
}

void torb_set_add(torb_set *set, const void *item) {
  static const uint8_t nothing = 0u;
  torb_map_set(set, item, &nothing);
}

bool torb_set_remove(torb_set *set, const void *item) {
  uint8_t nothing = 0u;
  return torb_map_remove(set, item, &nothing);
}

void torb_set_clear(torb_set *set) {
  torb_map_clear(set);
}

bool torb_set_next(torb_set set, uint32_t *cursor, const void **item) {
  return torb_map_next(set, cursor, item, NULL);
}
