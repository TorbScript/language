/*
 * torb_tls.h - the natives of `std/tls` (docs/design/NETWORK.md section 5). Included from torb.h; nothing includes it
 * directly.
 *
 * **They are defined only in a program that reaches them.** Their bodies are `runtime/tls/` over the mbedTLS of
 * `runtime/vendor/mbedtls`, which the driver compiles and links only where the program's C calls a `torb_tls_`
 * function, with `TORB_WITH_TLS` defined; everywhere else these prototypes have no body, nothing calls them, and the
 * thunk of the VM for each one panics.
 *
 * TLS here is a **state machine over bytes**, and not a socket: the program feeds it the ciphertext it received
 * (`torb_tls_feed`), asks it to go on (`torb_tls_handshake`, `torb_tls_read`, `torb_tls_write`), and takes the
 * ciphertext it produced (`torb_tls_take_outgoing`) to send it. So every wait stays a wait of `std/network` in
 * TorbScript, the IO core learns nothing, and none of these calls blocks.
 *
 * A session and an identity are handles, like a socket's; an answer is an `Int64` whose negative values are packed
 * failures (`torb_io.h`): kind 12 is a failure of TLS with mbedTLS's error as the code, 13 a certificate the platform's
 * verifier rejected with the platform's error, 14 one mbedTLS rejected with its verification flags.
 */

#ifndef TORB_TLS_H
#define TORB_TLS_H

/**
 * A client session that will speak to `server_name` (sent as SNI and checked against the certificate). With `trusted`
 * empty, the certificate is checked by the platform (Windows) or against the system's roots (elsewhere); otherwise
 * against the PEM certificates in `trusted` and nothing else. The handle, or a failure.
 */
int64_t torb_tls_client(torb_text server_name, torb_text trusted);

/** What a server proves itself with: a PEM chain, its own certificate first, and the PEM private key. The handle, or a failure. */
int64_t torb_tls_identity(torb_text certificates, torb_text private_key);

/** A server session with `identity`. The handle, or a failure. */
int64_t torb_tls_server(int64_t identity);

/** Ciphertext that arrived from the peer, kept for the session to read. `bytes` borrowed, a list of `UInt8`. */
void torb_tls_feed(int64_t session, torb_list bytes);

/**
 * The handshake, as far as the ciphertext fed allows: 0 once it is done, 1 where it needs more from the peer, or a
 * failure. Whatever it wrote waits in `torb_tls_take_outgoing`.
 */
int64_t torb_tls_handshake(int64_t session);

/** Appends the ciphertext the session produced and nobody sent yet to `*into`, a list of `UInt8`. */
void torb_tls_take_outgoing(int64_t session, torb_list *into);

/**
 * Encrypts the bytes of `bytes` from `from` on, as much of them as one record takes: how many were taken, or a failure.
 * The ciphertext waits in `torb_tls_take_outgoing`.
 */
int64_t torb_tls_write(int64_t session, torb_list bytes, int64_t from);

/**
 * Decrypts what the ciphertext fed allows, at most `maximum` bytes, appended to `*into`: how many, 0 where it needs
 * more from the peer, -1 where the peer ended the session (`close_notify`), or a failure.
 */
int64_t torb_tls_read(int64_t session, torb_list *into, int64_t maximum);

/** Writes the `close_notify` that ends the session to the outgoing ciphertext. */
void torb_tls_notify_close(int64_t session);

/** Frees a session or an identity. Twice is nothing. */
void torb_tls_close(int64_t handle);

/** The version the session agreed on: `"TLSv1.3"`, `"TLSv1.2"`, or `""` before the handshake is done. Result owned. */
torb_text torb_tls_protocol(int64_t session);

/** The words for a failure a native of this header answered. Result owned. */
torb_text torb_tls_error_text(int64_t failure);

#endif /* TORB_TLS_H */
