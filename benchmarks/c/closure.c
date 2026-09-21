/* The C twin of closure.trb: a function pointer plus the context it reads its capture from. */
#include <stdint.h>
#include <stdio.h>

enum { steps = 50000000 };

typedef struct Context {
  int64_t factor;
} Context;

typedef int64_t (*Transform)(const Context *context, int64_t value);

static int64_t stepped(const Context *context, int64_t value) {
  return (value * context->factor + 1) % 1000003;
}

static int64_t applied(int64_t value, Transform transform, const Context *context) {
  int64_t carried = value;
  int64_t index;
  for (index = 0; index < steps; index += 1) {
    carried = transform(context, carried);
  }
  return carried;
}

int main(void) {
  Context context;
  context.factor = 31;
  printf("closure %lld\n", (long long)applied(1, stepped, &context));
  return 0;
}
