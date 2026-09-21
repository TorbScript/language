/* The C twin of pipeline.trb: the fused loop the three stages stand for. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { size = 2000000, rounds = 20 };

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
    int64_t stage = 0;
    int64_t index;
    for (index = 0; index < size; index += 1) {
      int64_t doubled = numbers[index] * 2;
      if (doubled % 3 == 0) {
        stage = stage + doubled;
      }
    }
    sum = (sum + stage) % 1000003;
  }
  return sum;
}

int main(void) {
  int64_t *numbers = filled();
  if (numbers == NULL) {
    return 1;
  }
  printf("pipeline %lld\n", (long long)total(numbers));
  free(numbers);
  return 0;
}
