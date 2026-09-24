/*
 * platform_posix.c - what TLS asks of Linux, macOS and FreeBSD (runtime/tls/torb_tls_platform.h): randomness from
 * `/dev/urandom`, and the file of the system's trusted roots, which mbedTLS verifies against.
 *
 * The roots are the first file that exists of `$SSL_CERT_FILE` and the places the systems keep their bundle: Debian and
 * Ubuntu, Fedora and RHEL, openSUSE, Alpine, FreeBSD, and macOS's `/etc/ssl/cert.pem`. macOS's own verifier
 * (`SecTrustEvaluateWithError`) is the later step docs/design/NETWORK.md section 5 names; until then the bundle macOS
 * ships is the trust.
 *
 * The whole file is one `#if`, like every file of `runtime/os/`.
 */

/* POSIX 2008, which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "torb_tls_platform.h"

#if defined(__unix__) || defined(__APPLE__)


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

bool torb_tls_platform_random(unsigned char *output, size_t length) {
  int descriptor = open("/dev/urandom", O_RDONLY);
  if (descriptor < 0) {
    return false;
  }
  while (length > 0u) {
    ssize_t read_now = read(descriptor, output, length);
    if (read_now < 0 && errno == EINTR) {
      continue;
    }
    if (read_now <= 0) {
      (void)close(descriptor);
      return false;
    }
    output += (size_t)read_now;
    length -= (size_t)read_now;
  }
  (void)close(descriptor);
  return true;
}

bool torb_tls_platform_verifies(void) {
  return false;
}

uint32_t torb_tls_platform_verify(const unsigned char *const *certificates, const size_t *lengths, size_t count,
                                  const char *server_name) {
  (void)certificates;
  (void)lengths;
  (void)count;
  (void)server_name;
  return 1u;
}

void torb_tls_platform_error_text(uint32_t code, char *buffer, size_t size) {
  if (size > 0u) {
    snprintf(buffer, size, "the certificate was rejected: error %u", (unsigned)code);
  }
}

const char *torb_tls_platform_roots(void) {
  static const char *const places[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/ssl/cert.pem",
    "/usr/local/share/certs/ca-root-nss.crt",
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
  };
  const char *given = getenv("SSL_CERT_FILE");
  struct stat found;
  size_t index;
  if (given != NULL && given[0] != '\0' && stat(given, &found) == 0) {
    return given;
  }
  for (index = 0u; index < sizeof places / sizeof places[0]; index += 1u) {
    if (stat(places[index], &found) == 0) {
      return places[index];
    }
  }
  return NULL;
}

#endif /* __unix__ || __APPLE__ */
