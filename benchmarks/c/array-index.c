/* The C twin of array-index.trb: a struct holding 64 int64_t, passed by value and read by index. */
#include <stdint.h>
#include <stdio.h>

enum { rounds = 2000000 };

typedef struct {
  int64_t items[64];
} values_t;

static values_t filled(void) {
  values_t values;
  int64_t index;
  for (index = 0; index < 64; index += 1) {
    values.items[index] = index % 7;
  }
  return values;
}

static int64_t total(values_t values) {
  int64_t sum = 0;
  int64_t round;
  for (round = 0; round < rounds; round += 1) {
    int64_t index;
    for (index = 0; index < 64; index += 1) {
      sum = sum + values.items[index];
    }
  }
  return sum;
}

int main(void) {
  printf("array-index %lld\n", (long long)total(filled()));
  return 0;
}
