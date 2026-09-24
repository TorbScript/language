/*
 * torb_tls_platform.h - what the TLS glue of runtime/tls/tls.c asks of the system it runs on: randomness, and who
 * decides whether a certificate chain is trusted (docs/design/NETWORK.md section 5). `platform_windows.c` answers on
 * Windows, `platform_posix.c` on Linux, macOS and FreeBSD; each is one `#if` from its first line to its last.
 */

#ifndef TORB_TLS_PLATFORM_H
#define TORB_TLS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Fills `output` with `length` random bytes of the operating system's generator. False where it failed. */
bool torb_tls_platform_random(unsigned char *output, size_t length);

/** Whether the platform verifies a chain itself (Windows); elsewhere mbedTLS verifies against the system's roots. */
bool torb_tls_platform_verifies(void);

/**
 * The platform's verdict on a chain the server presented, its own certificate first, `count` DER certificates, for
 * `server_name` (NUL terminated). 0 where it is trusted for that name, and otherwise the platform's error code, whose
 * words `torb_tls_platform_error_text` gives.
 */
uint32_t torb_tls_platform_verify(const unsigned char *const *certificates, const size_t *lengths, size_t count,
                                  const char *server_name);

/** The words of the platform for one of its errors, into `buffer` of `size` bytes, NUL terminated. */
void torb_tls_platform_error_text(uint32_t code, char *buffer, size_t size);

/** The file of the system's trusted roots where mbedTLS verifies (not Windows), or `NULL` where none was found. */
const char *torb_tls_platform_roots(void);

#endif /* TORB_TLS_PLATFORM_H */
