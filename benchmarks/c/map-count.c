/* The C twin of map-count.trb: open addressing over FNV-1a, one probe per key. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { distinct = 50000, rounds = 60, buckets = 1 << 17 };

typedef struct Entry {
  const char *key;
  int64_t count;
} Entry;

static uint64_t hashed(const char *key) {
  uint64_t hash = 1469598103934665603ULL;
  const unsigned char *cursor = (const unsigned char *)key;
  while (*cursor != 0) {
    hash ^= (uint64_t)*cursor;
    hash *= 1099511628211ULL;
    cursor += 1;
  }
  return hash;
}

int main(void) {
  char **keys = (char **)malloc((size_t)distinct * sizeof(char *));
  Entry *table = (Entry *)calloc((size_t)buckets, sizeof(Entry));
  int64_t index;
  int64_t round;
  int64_t used = 0;
  if (keys == NULL || table == NULL) {
    return 1;
  }
  for (index = 0; index < distinct; index += 1) {
    char made[32];
    snprintf(made, sizeof(made), "key-%lld", (long long)index);
    keys[index] = (char *)malloc(strlen(made) + 1);
    if (keys[index] == NULL) {
      return 1;
    }
    memcpy(keys[index], made, strlen(made) + 1);
  }
  for (round = 0; round < rounds; round += 1) {
    for (index = 0; index < distinct; index += 1) {
      const char *key = keys[index];
      size_t slot = (size_t)(hashed(key) & (uint64_t)(buckets - 1));
      while (table[slot].key != NULL && strcmp(table[slot].key, key) != 0) {
        slot = (slot + 1) & (size_t)(buckets - 1);
      }
      if (table[slot].key == NULL) {
        table[slot].key = key;
        table[slot].count = 0;
        used += 1;
      }
      table[slot].count += 1;
    }
  }
  printf("map-count %lld\n", (long long)used);
  for (index = 0; index < distinct; index += 1) {
    free(keys[index]);
  }
  free(keys);
  free(table);
  return 0;
}
