/* The C twin of list-index.trb: a growable array of int64_t, read by index. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { size = 2000000, rounds = 40 };

static int64_t *filled(void) {
  int64_t *numbers = (int64_t *)malloc((size_t)size * sizeof(int64_t));
  int64_t index;
  if (numbers == NULL) {
    return NULL;
  }
  for (index = 0; index < size; index += 1) {
    numbers[index] = index % 97;
  }
  return numbers;
}

static int64_t total(const int64_t *numbers) {
  int64_t sum = 0;
  int64_t round;
  for (round = 0; round < rounds; round += 1) {
    int64_t index;
    for (index = 0; index < size; index += 1) {
      sum = sum + numbers[index];
    }
  }
  return sum;
}

int main(void) {
  int64_t *numbers = filled();
  if (numbers == NULL) {
    return 1;
  }
  printf("list-index %lld\n", (long long)total(numbers));
  free(numbers);
  return 0;
}
