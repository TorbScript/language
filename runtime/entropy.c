/*
 * entropy.c - `Entropy` of `std/os`: bytes of the operating system's source of randomness, which keys, nonces, tokens and
 * the seed of a generator are made from (docs/design/RANDOM.md).
 *
 * The source is the platform layer's (`torb_platform_random_bytes`, in platform.c): `BCryptGenRandom` on Windows,
 * `getrandom` on Linux and FreeBSD, `getentropy` on macOS and in the browser, where emscripten answers it with
 * `crypto.getRandomValues`. This file only hands the bytes to the list, a piece at a time through a buffer on the stack,
 * so a request of any size needs no allocation of its own.
 */

#include "torb.h"

bool torb_entropy_fill(torb_list *into, int64_t count) {
  uint8_t piece[256];
  while (count > 0) {
    const size_t size = count > (int64_t)sizeof piece ? sizeof piece : (size_t)count;
    if (!torb_platform_random_bytes(piece, size)) {
      return false;
    }
    torb_list_add_plain(into, piece, size);
    count -= (int64_t)size;
  }
  return true;
}
