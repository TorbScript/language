/*
 * element.c - the callbacks every contextual descriptor shares (`torb_contextual_element`, torb.h).
 *
 * The runtime calls a descriptor's callbacks with the element and nothing else. A contextual descriptor's are these
 * four, and each hands the call on to the descriptor's own function together with the descriptor, which
 * `torb_element_retain` and its siblings named in `torb_element_calling` just before. Each reads it first: what the
 * descriptor's function calls may call another descriptor's and set it again.
 */

#include "torb.h"

#if defined(_MSC_VER)
__declspec(thread) const torb_element *torb_element_calling = NULL;
#else
_Thread_local const torb_element *torb_element_calling = NULL;
#endif

static const torb_contextual_element *torb_contextual_called(void) {
  return (const torb_contextual_element *)(const void *)torb_element_calling;
}

void torb_contextual_retain(void *element) {
  const torb_contextual_element *self = torb_contextual_called();
  self->retain(self, element);
}

void torb_contextual_release(void *element) {
  const torb_contextual_element *self = torb_contextual_called();
  self->release(self, element);
}

bool torb_contextual_equals(const void *first, const void *second) {
  const torb_contextual_element *self = torb_contextual_called();
  return self->equals(self, first, second);
}

uint64_t torb_contextual_hash(const void *value) {
  const torb_contextual_element *self = torb_contextual_called();
  return self->hash(self, value);
}
