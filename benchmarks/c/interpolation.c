/* The C twin of interpolation.trb: one snprintf into a buffer on the stack. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { steps = 2000000 };

int main(void) {
  int64_t bytes = 0;
  int64_t index;
  char line[64];
  for (index = 0; index < steps; index += 1) {
    int written = snprintf(line, sizeof(line), "row %lld: %lld", (long long)index, (long long)(index % 97));
    if (written < 0) {
      return 1;
    }
    bytes = bytes + (int64_t)strlen(line);
  }
  printf("interpolation %lld\n", (long long)bytes);
  return 0;
}
