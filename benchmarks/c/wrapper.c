/* The C twin of wrapper.trb: the same one-field struct, which -O2 has to flatten away. */
#include <stdint.h>
#include <stdio.h>

enum { steps = 200000000 };

typedef struct Meters {
  double value;
} Meters;

static Meters plus(Meters self, Meters other) {
  Meters made;
  made.value = self.value + other.value;
  return made;
}

static double walked(void) {
  Meters total;
  Meters step;
  int64_t index;
  total.value = 0.0;
  step.value = 0.5;
  for (index = 0; index < steps; index += 1) {
    total = plus(total, step);
  }
  return total.value;
}

int main(void) {
  printf("wrapper %.1f\n", walked());
  return 0;
}
