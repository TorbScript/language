/*
 * torb_network.h - the natives of `std/network` (docs/design/NETWORK.md sections 2 and 4), over the IO core of
 * `runtime/io.c`. Included from torb.h; nothing includes it directly.
 *
 * **A socket is a handle**: an `Int64` of a table of the runtime, positive, with a generation in its high half, so a
 * handle used after its close is a failure and never memory that was freed. **An answer is an `Int64`**: zero or more is
 * the answer (a count, a handle), a negative number a failure with its kind and the system's code packed into it
 * (`-(kind << 32 | code)`, `torb_io.h`), which `std/network` turns into a `NetworkError` and `torb_network_error_text`
 * into words. **An address crosses as numbers**: the family (4 or 6), the high and the low 64 bits of the address in
 * network order (an IPv4 address is the low 32 bits of `low`), and the port.
 *
 * Every operation that waits answers a task of the runtime (`Task<Int64>` in TorbScript), like `sleep`: it is a child
 * of the task that called, cancelled with it, and what it received is taken afterwards by a synchronous native, because
 * a task of the runtime cannot answer a list of the program.
 */

#ifndef TORB_NETWORK_H
#define TORB_NETWORK_H

/**
 * A listening TCP socket bound to the address, with `backlog` connections the system may queue. The handle, or a
 * failure. Synchronous: binding never waits for the network.
 */
int64_t torb_network_listen(int64_t family, int64_t high, int64_t low, int64_t port, int64_t backlog);

/** The next connection of `listener`, as a task of its handle or a failure. Result owned. */
torb_task *torb_network_accept(int64_t listener);

/** A TCP connection to the address, as a task of its handle or a failure. Result owned. */
torb_task *torb_network_connect(int64_t family, int64_t high, int64_t low, int64_t port);

/**
 * At most `maximum` bytes of `stream`, as a task of how many arrived: 0 at the end of the stream, or a failure. The
 * bytes wait in the socket for `torb_network_take_received`. Result owned.
 */
torb_task *torb_network_receive(int64_t stream, int64_t maximum);

/** Appends every byte `stream` received and nobody took yet to `*into`, a list of `UInt8`. */
void torb_network_take_received(int64_t stream, torb_list *into);

/**
 * The bytes of `bytes` from index `from` on, written to `stream`, as a task of how many were written or a failure. The
 * bytes are copied before this returns, so the list may change afterwards. `bytes` borrowed, result owned.
 */
torb_task *torb_network_send(int64_t stream, torb_list bytes, int64_t from);

/** No more writing on `stream`: the peer reads the end of the stream. 0, or a failure. */
int64_t torb_network_shutdown(int64_t stream);

/** Closes the handle: a socket's descriptor, or what a resolution holds. What waits on it fails. Twice is nothing. */
void torb_network_close(int64_t handle);

/**
 * The local address of a socket, or with `peer` the remote one, appended to `*parts` as four numbers: the family, the
 * high and the low half, the port. 0, or a failure.
 */
int64_t torb_network_address(int64_t handle, bool peer, torb_list *parts);

/**
 * The addresses of `host`, as a task of a handle of the resolution or a failure. A literal address is `std/network`'s,
 * and never reaches this. `host` borrowed, result owned.
 */
torb_task *torb_network_resolve(torb_text host);

/** Appends the addresses of a resolution to `*parts`, three numbers each (family, high, low), and closes its handle. */
void torb_network_take_resolved(int64_t resolution, torb_list *parts);

/**
 * A UDP socket bound to the address; port 0 lets the system choose one. The handle, or a failure. Synchronous: binding
 * never waits for the network.
 */
int64_t torb_network_bind(int64_t family, int64_t high, int64_t low, int64_t port);

/**
 * Connects the UDP socket `socket` to one peer: a datagram sent without an address goes there, and only the peer's
 * datagrams arrive. Never waits. 0, or a failure.
 */
int64_t torb_network_connect_datagram(int64_t socket, int64_t family, int64_t high, int64_t low, int64_t port);

/**
 * `bytes` as one datagram to the address - or, with family 0, to the peer the socket is connected to - as a task of how
 * many bytes were sent or a failure. The bytes are copied before this returns. `bytes` borrowed, result owned.
 */
torb_task *torb_network_send_datagram(int64_t socket, torb_list bytes, int64_t family, int64_t high, int64_t low,
                                      int64_t port);

/**
 * The next datagram of `socket`, as a task of its length (0 is a datagram without bytes) or a failure. The datagram
 * waits in the socket, whole and with its sender, for `torb_network_take_datagram`. Result owned.
 */
torb_task *torb_network_receive_datagram(int64_t socket);

/**
 * Takes the oldest datagram `socket` received: its bytes appended to `*into`, a list of `UInt8`, and its sender to
 * `*from` as four numbers (the family, the high and the low half, the port). Its length, or a failure where none waits.
 */
int64_t torb_network_take_datagram(int64_t socket, torb_list *into, torb_list *from);

/**
 * The name servers the system is configured with (docs/design/DNS.md section 7), appended to `*parts` as four numbers
 * each: the family, the high and the low half, the port. Their count, or a failure.
 */
int64_t torb_network_name_servers(torb_list *parts);

/** 64 bits from the system's source of randomness: the identifier of a DNS query (RFC 5452). */
int64_t torb_network_random(void);

/** The words for a failure a native of this header answered. Result owned. */
torb_text torb_network_error_text(int64_t failure);

/** How many operations of the IO core are alive, for the tests: zero once the core stopped. */
int64_t torb_network_operations_alive(void);

#endif /* TORB_NETWORK_H */
