/* The C twin of accumulate.trb: one growable array, pushed to. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { size = 60000 };

typedef struct Numbers {
  int64_t *data;
  size_t length;
  size_t capacity;
} Numbers;

static void append(Numbers *numbers, int64_t value) {
  if (numbers->length == numbers->capacity) {
    size_t wanted = numbers->capacity == 0 ? 8 : numbers->capacity * 2;
    int64_t *grown = (int64_t *)realloc(numbers->data, wanted * sizeof(int64_t));
    if (grown == NULL) {
      exit(1);
    }
    numbers->data = grown;
    numbers->capacity = wanted;
  }
  numbers->data[numbers->length] = value;
  numbers->length += 1;
}

int main(void) {
  Numbers numbers = { NULL, 0, 0 };
  int64_t index;
  for (index = 0; index < size; index += 1) {
    append(&numbers, index);
  }
  printf("accumulate %lld\n", (long long)numbers.length);
  free(numbers.data);
  return 0;
}
