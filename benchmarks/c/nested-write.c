/* The C twin of nested-write.trb: rows of their own, written through two index steps. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { side = 800, rounds = 6 };

int main(void) {
  int64_t **rows = (int64_t **)malloc((size_t)side * sizeof(int64_t *));
  int64_t row;
  int64_t round;
  int64_t sum = 0;
  if (rows == NULL) {
    return 1;
  }
  for (row = 0; row < side; row += 1) {
    int64_t column;
    rows[row] = (int64_t *)malloc((size_t)side * sizeof(int64_t));
    if (rows[row] == NULL) {
      return 1;
    }
    for (column = 0; column < side; column += 1) {
      rows[row][column] = 0;
    }
  }
  for (round = 0; round < rounds; round += 1) {
    for (row = 0; row < side; row += 1) {
      int64_t column;
      for (column = 0; column < side; column += 1) {
        rows[row][column] = rows[row][column] + 1;
      }
    }
  }
  for (row = 0; row < side; row += 1) {
    sum = sum + rows[row][side - 1];
  }
  printf("nested-write %lld\n", (long long)sum);
  for (row = 0; row < side; row += 1) {
    free(rows[row]);
  }
  free(rows);
  return 0;
}
