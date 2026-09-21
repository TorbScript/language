/* The C twin of record-write.trb: one field of a struct in an array. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { size = 1000000, rounds = 30 };

typedef struct Point {
  int64_t x;
  int64_t y;
} Point;

int main(void) {
  Point *made = (Point *)malloc((size_t)size * sizeof(Point));
  int64_t index;
  int64_t round;
  int64_t sum = 0;
  if (made == NULL) {
    return 1;
  }
  for (index = 0; index < size; index += 1) {
    made[index].x = index;
    made[index].y = 0;
  }
  for (round = 0; round < rounds; round += 1) {
    for (index = 0; index < size; index += 1) {
      made[index].y = made[index].x + 1;
    }
  }
  for (index = 0; index < size; index += 1) {
    sum = (sum + made[index].y) % 1000003;
  }
  printf("record-write %lld\n", (long long)sum);
  free(made);
  return 0;
}
