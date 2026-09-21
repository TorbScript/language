/* The C twin of list-iterate.trb: the same array, walked with a pointer. */
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
    const int64_t *cursor = numbers;
    const int64_t *end = numbers + size;
    while (cursor != end) {
      sum = sum + *cursor;
      cursor += 1;
    }
  }
  return sum;
}

int main(void) {
  int64_t *numbers = filled();
  if (numbers == NULL) {
    return 1;
  }
  printf("list-iterate %lld\n", (long long)total(numbers));
  free(numbers);
  return 0;
}
