/* The C twin of arithmetic.trb: the same data dependent chain, unchecked. */
#include <stdint.h>
#include <stdio.h>

enum { rounds = 60, steps = 1000000, modulus = 1000003 };

static int64_t chain(void) {
  int64_t state = 1;
  for (int64_t round = 0; round < rounds; round += 1) {
    for (int64_t index = 0; index < steps; index += 1) {
      state = (state * 31 + index) % modulus;
    }
  }
  return state;
}

int main(void) {
  printf("arithmetic %lld\n", (long long)chain());
  return 0;
}
