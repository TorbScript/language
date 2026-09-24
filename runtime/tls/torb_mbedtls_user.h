/*
 * torb_mbedtls_user.h - what TorbScript changes in mbedTLS's default configuration (MBEDTLS_USER_CONFIG_FILE, read after
 * `mbedtls/mbedtls_config.h`). Everything not named here is mbedTLS's default: TLS 1.2 and 1.3, the X.509 parser and
 * verifier, PEM, and the ciphers and curves the default enables.
 */

/* The sockets are std/network's: mbedTLS reads and writes memory (runtime/tls/tls.c), so it needs no network code */
#undef MBEDTLS_NET_C
/* Nothing times out inside mbedTLS: a timeout is `within` on the task that waits (NETWORK.md section 3) */
#undef MBEDTLS_TIMING_C
/* No key is kept in a file by PSA: every key is the program's, and PSA's own storage would write the working directory */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C

/*
 * Randomness comes from the runtime (`mbedtls_hardware_poll` in runtime/tls/tls.c): the operating system's generator,
 * loaded on first use on Windows, so linking needs no `bcrypt` and no `advapi32`.
 */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Where the platform verifies certificates (Windows), mbedTLS is handed no roots of its own: a callback answers none */
#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK

/*
 * An IP address in a certificate's names is read by mbedTLS's own parser, not by the system's `inet_pton`, which on
 * Windows lives in ws2_32 - a library the runtime loads on first use and never links (NETWORK.md section 2).
 */
#define MBEDTLS_TEST_SW_INET_PTON
