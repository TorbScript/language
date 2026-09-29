/*
 * entropy_test.c - the system's source of randomness (`torb_platform_random_bytes`) and `Entropy` of `std/os` over it
 * (`torb_entropy_fill`).
 *
 * Randomness cannot be asserted, only its absence: a source that answers zeros, the same bytes twice, or fewer bytes than
 * it was asked for is broken, and every check below would fail for such a source with certainty rather than bad luck.
 */

#include "harness.h"

#include <string.h>

static const torb_element element_byte = { 1u, 1u, NULL, NULL, NULL, NULL };

/** Whether a buffer holds nothing but zeros - what a source that wrote nothing leaves behind. */
static bool all_zero(const uint8_t *bytes, size_t size) {
  size_t index;
  for (index = 0u; index < size; index += 1u) {
    if (bytes[index] != 0u) {
      return false;
    }
  }
  return true;
}

/** 1 000 bytes, more than one `getentropy` call answers: all of them written, and two requests differ. */
TORB_TEST(the_system_answers_different_bytes_every_time) {
  uint8_t first[1000];
  uint8_t second[1000];
  memset(first, 0, sizeof first);
  memset(second, 0, sizeof second);
  TORB_CHECK(torb_platform_random_bytes(first, sizeof first));
  TORB_CHECK(torb_platform_random_bytes(second, sizeof second));
  /* The last 256 bytes are the last call of a source that answers in pieces: a piece left out is zeros */
  TORB_CHECK(!all_zero(first + sizeof first - 256u, 256u));
  TORB_CHECK(!all_zero(second + sizeof second - 256u, 256u));
  TORB_CHECK(memcmp(first, second, sizeof first) != 0);
}

/** Nothing asked for is nothing written, and succeeds. */
TORB_TEST(zero_bytes_are_an_answer) {
  uint8_t untouched = 7u;
  TORB_CHECK(torb_platform_random_bytes(&untouched, 0u));
  TORB_CHECK_INTEGER(untouched, 7);
}

/** `Entropy.bytes`: exactly as many bytes as asked for, across the pieces the native hands over, after what was there. */
TORB_TEST(entropy_appends_exactly_the_count) {
  torb_list bytes = torb_list_new(&element_byte);
  const uint8_t marker = 42u;
  uint8_t tail[88];
  int64_t index;
  torb_list_add_plain(&bytes, &marker, 1u);
  TORB_CHECK(torb_entropy_fill(&bytes, 600));
  TORB_CHECK_INTEGER(torb_list_length(bytes), 601);
  TORB_CHECK_INTEGER(*(const uint8_t *)torb_list_at(bytes, 0, torb_location_unknown), 42);
  for (index = 0; index < 88; index += 1) {
    tail[index] = *(const uint8_t *)torb_list_at(bytes, 513 + index, torb_location_unknown);
  }
  TORB_CHECK(!all_zero(tail, sizeof tail));
  TORB_CHECK(torb_entropy_fill(&bytes, 0));
  TORB_CHECK_INTEGER(torb_list_length(bytes), 601);
  torb_list_release(bytes);
}

void torb_register_entropy_tests(void) {
  TORB_ADD(the_system_answers_different_bytes_every_time);
  TORB_ADD(zero_bytes_are_an_answer);
  TORB_ADD(entropy_appends_exactly_the_count);
}
