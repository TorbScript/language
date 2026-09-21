/* The C twin of call-depth.trb: the same recursion, so this is the calling convention alone. */
#include <stdint.h>
#include <stdio.h>

enum { depth = 40 };

static int64_t fibonacci(int64_t index) {
  if (index < 2) {
    return index;
  }
  return fibonacci(index - 1) + fibonacci(index - 2);
}

int main(void) {
  printf("call-depth %lld\n", (long long)fibonacci(depth));
  return 0;
}
