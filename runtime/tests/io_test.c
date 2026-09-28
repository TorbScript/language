/*
 * io_test.c - the IO core over loopback (docs/design/NETWORK.md section 2): listen, accept, connect, send, receive, the
 * end of a stream, a refused connection, a name resolution, datagrams (bound, connected, refused), and the cancellation
 * of each kind of wait - with what the kernel delivered afterwards kept for the next operation, never lost.
 *
 * The natives answer tasks of the runtime, and the tests run the scheduler until the one they need has completed
 * (`torb_scheduler_run`). Every test ends with `torb_scheduler_finish()`, which stops the IO core, and then asserts that
 * no operation is alive: the IO thread let go of everything it held.
 */

#include "harness.h"

#include <stdlib.h>

#define LOOPBACK4 ((int64_t)0x7F000001)
#define CANCELLED INT64_MIN

/* Runs until `task` completed, and answers its value, or `CANCELLED`. `task` consumed. */
static int64_t answer_of(torb_task *task) {
  int64_t value = 0;
  bool finished;
  torb_scheduler_run(task);
  finished = torb_task_result(task, &value);
  torb_task_release(task);
  return finished ? value : CANCELLED;
}

/*
 * Runs every task that is queued now once, so that one that waits registers where it waits: a pause is queued behind
 * them and finishes the first time it runs.
 */
static void let_them_start(void) {
  torb_task *turn = torb_pause();
  torb_scheduler_run(turn);
  torb_task_release(turn);
}

/* The kind a packed failure carries. */
static int64_t kind_of(int64_t failure) {
  return failure >= 0 ? 0 : (int64_t)((uint64_t)(-failure) >> 32);
}

/* The port a listener was bound to. */
static int64_t port_of(int64_t handle) {
  torb_list parts = torb_list_new(&torb_element_int64);
  int64_t port = -1;
  if (torb_network_address(handle, false, &parts) == 0 && torb_list_length(parts) == 4) {
    port = *(const int64_t *)torb_list_at(parts, 3, torb_location_unknown);
  }
  torb_list_release(parts);
  return port;
}

static const torb_element element_byte = { 1u, 1u, NULL, NULL, NULL, NULL };

/* A list of the bytes of `text`. Owned. */
static torb_list bytes_of(const char *text) {
  torb_list list = torb_list_new(&element_byte);
  torb_list_add_plain(&list, text, strlen(text));
  return list;
}

/* Whether a list of bytes holds exactly `text`. */
static bool bytes_are(torb_list list, const char *text) {
  size_t length = strlen(text);
  return (size_t)torb_list_length(list) == length
         && (length == 0u || memcmp(torb_list_at(list, 0, torb_location_unknown), text, length) == 0);
}

/* A connected pair over loopback: `*server` accepted, `*client` connected. The listener is closed again. */
static bool connected_pair(int64_t *server, int64_t *client) {
  int64_t listener = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  int64_t port;
  torb_task *accepting;
  if (listener <= 0) {
    return false;
  }
  port = port_of(listener);
  accepting = torb_network_accept(listener);
  *client = answer_of(torb_network_connect(4, 0, LOOPBACK4, port));
  *server = answer_of(accepting);
  torb_network_close(listener);
  return *client > 0 && *server > 0;
}

/* Receives until `expected` bytes arrived or the stream ended. Answers the bytes; owned. */
static torb_list received_bytes(int64_t stream, size_t expected) {
  torb_list into = torb_list_new(&element_byte);
  while ((size_t)torb_list_length(into) < expected) {
    int64_t count = answer_of(torb_network_receive(stream, 4096));
    if (count <= 0) {
      break;
    }
    torb_network_take_received(stream, &into);
  }
  return into;
}

/* Whether every address of a resolution - family, high and low, three numbers each - is a loopback address. */
static bool all_loopback(torb_list parts) {
  int64_t index;
  for (index = 0; index + 2 < torb_list_length(parts); index += 3) {
    int64_t family = *(const int64_t *)torb_list_at(parts, index, torb_location_unknown);
    int64_t high = *(const int64_t *)torb_list_at(parts, index + 1, torb_location_unknown);
    int64_t low = *(const int64_t *)torb_list_at(parts, index + 2, torb_location_unknown);
    if (family == 4 ? (low >> 24) != 127 : !(family == 6 && high == 0 && low == 1)) {
      return false;
    }
  }
  return true;
}

/*
 * The first test of the file, so nothing opened a socket before it: a name resolved before any socket once called
 * Winsock's resolver before the core had loaded Winsock, and crashed (docs/design/RELEASE.md section 7.13). Both answers
 * of the system's resolver, and neither needs a network: `localhost`, which the native asks the system for - only
 * `std/network` answers it itself - and which every system answers from its own tables, and `.invalid`, which never
 * exists (RFC 6761).
 */
TORB_TEST(test_io_resolving_before_any_socket) {
  torb_text local = torb_text_from_cstring("localhost");
  torb_text host = torb_text_from_cstring("torbscript.invalid");
  torb_list parts = torb_list_new(&torb_element_int64);
  int64_t resolution = answer_of(torb_network_resolve(local));
  TORB_CHECK(resolution > 0);
  torb_network_take_resolved(resolution, &parts);
  TORB_CHECK(torb_list_length(parts) >= 3);
  TORB_CHECK(all_loopback(parts));
  TORB_CHECK_INTEGER(kind_of(answer_of(torb_network_resolve(host))), 8);
  torb_list_release(parts);
  torb_text_release(host);
  torb_text_release(local);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_echo_over_loopback) {
  int64_t server = 0;
  int64_t client = 0;
  torb_list message = bytes_of("hello, network");
  torb_list answer;
  torb_list peer = torb_list_new(&torb_element_int64);
  TORB_CHECK(connected_pair(&server, &client));
  TORB_CHECK_INTEGER(answer_of(torb_network_send(client, message, 0)), 14);
  answer = received_bytes(server, 14u);
  TORB_CHECK(bytes_are(answer, "hello, network"));
  torb_list_release(answer);
  /* From an offset: only what lies behind it is sent */
  TORB_CHECK_INTEGER(answer_of(torb_network_send(server, message, 7)), 7);
  answer = received_bytes(client, 7u);
  TORB_CHECK(bytes_are(answer, "network"));
  torb_list_release(answer);
  /* The peer of the accepted side is the loopback address */
  TORB_CHECK_INTEGER(torb_network_address(server, true, &peer), 0);
  TORB_CHECK_INTEGER(torb_list_length(peer), 4);
  TORB_CHECK_INTEGER(*(const int64_t *)torb_list_at(peer, 0, torb_location_unknown), 4);
  TORB_CHECK_INTEGER(*(const int64_t *)torb_list_at(peer, 2, torb_location_unknown), LOOPBACK4);
  torb_list_release(peer);
  torb_list_release(message);
  torb_network_close(server);
  torb_network_close(client);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_shutdown_is_the_end_of_the_stream) {
  int64_t server = 0;
  int64_t client = 0;
  torb_list message = bytes_of("last words");
  torb_list answer;
  TORB_CHECK(connected_pair(&server, &client));
  TORB_CHECK_INTEGER(answer_of(torb_network_send(client, message, 0)), 10);
  TORB_CHECK_INTEGER(torb_network_shutdown(client), 0);
  answer = received_bytes(server, 1000u);
  TORB_CHECK(bytes_are(answer, "last words"));
  torb_list_release(answer);
  /* Every receive after the end answers the end again */
  TORB_CHECK_INTEGER(answer_of(torb_network_receive(server, 16)), 0);
  /* The other direction is still open */
  TORB_CHECK_INTEGER(answer_of(torb_network_send(server, message, 5)), 5);
  answer = received_bytes(client, 5u);
  TORB_CHECK(bytes_are(answer, "words"));
  torb_list_release(answer);
  torb_list_release(message);
  torb_network_close(server);
  torb_network_close(client);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_closed_handle_is_a_failure) {
  int64_t server = 0;
  int64_t client = 0;
  torb_text words;
  TORB_CHECK(connected_pair(&server, &client));
  torb_network_close(client);
  /* Closing twice is nothing, and a handle after its close answers "closed" everywhere */
  torb_network_close(client);
  TORB_CHECK_INTEGER(kind_of(answer_of(torb_network_receive(client, 16))), 10);
  TORB_CHECK_INTEGER(kind_of(torb_network_shutdown(client)), 10);
  words = torb_network_error_text(torb_network_shutdown(client));
  TORB_CHECK_TEXT(words, "the socket is closed");
  torb_text_release(words);
  /* The peer reads the end of the stream, or a reset: either way the receive completes */
  TORB_CHECK(answer_of(torb_network_receive(server, 16)) <= 0);
  torb_network_close(server);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_refused_connection_fails) {
  int64_t listener = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  int64_t port = port_of(listener);
  int64_t answer;
  torb_text words;
  TORB_CHECK(port > 0);
  torb_network_close(listener);
  answer = answer_of(torb_network_connect(4, 0, LOOPBACK4, port));
  TORB_CHECK_INTEGER(kind_of(answer), 2);
  words = torb_network_error_text(answer);
  TORB_CHECK(words.length > 0u);
  torb_text_release(words);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_an_address_in_use_fails) {
  int64_t first = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  int64_t second;
  TORB_CHECK(first > 0);
  second = torb_network_listen(4, 0, LOOPBACK4, port_of(first), 8);
  TORB_CHECK_INTEGER(kind_of(second), 6);
  torb_network_close(first);
  /* Nonsense is refused before the system is asked */
  TORB_CHECK_INTEGER(kind_of(torb_network_listen(5, 0, LOOPBACK4, 0, 8)), 11);
  TORB_CHECK_INTEGER(kind_of(torb_network_listen(4, 0, LOOPBACK4, 70000, 8)), 11);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_cancelled_receive_loses_nothing) {
  int64_t server = 0;
  int64_t client = 0;
  torb_list message = bytes_of("after the cancel");
  torb_list answer;
  torb_task *waiting;
  TORB_CHECK(connected_pair(&server, &client));
  /* A receive that waits, because nothing was sent: cancelled, it ends as cancelled */
  waiting = torb_network_receive(server, 64);
  let_them_start();
  TORB_CHECK(!torb_task_is_complete(waiting));
  torb_task_cancel(waiting);
  TORB_CHECK_INTEGER(answer_of(waiting), CANCELLED);
  /* What arrives afterwards is the next receive's, whole */
  TORB_CHECK_INTEGER(answer_of(torb_network_send(client, message, 0)), 16);
  answer = received_bytes(server, 16u);
  TORB_CHECK(bytes_are(answer, "after the cancel"));
  torb_list_release(answer);
  torb_list_release(message);
  torb_network_close(server);
  torb_network_close(client);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_cancelled_accept_leaves_the_connection_to_the_next) {
  int64_t listener = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  int64_t port = port_of(listener);
  int64_t client;
  int64_t server;
  torb_task *waiting;
  TORB_CHECK(port > 0);
  waiting = torb_network_accept(listener);
  let_them_start();
  torb_task_cancel(waiting);
  TORB_CHECK_INTEGER(answer_of(waiting), CANCELLED);
  client = answer_of(torb_network_connect(4, 0, LOOPBACK4, port));
  TORB_CHECK(client > 0);
  server = answer_of(torb_network_accept(listener));
  TORB_CHECK(server > 0);
  torb_network_close(server);
  torb_network_close(client);
  torb_network_close(listener);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

/*
 * Several accept loops on one listening socket (docs/design/NETWORK.md section 4): accepts that wait at once each take
 * a connection of their own, one of them can be cancelled, and a close wakes every one that still waits with "closed".
 * The readiness pollers once had room for one waiting reader a socket, and every accept after the first failed at once
 * with "not valid".
 */
TORB_TEST(test_io_several_accepts_wait_on_one_listener) {
  int64_t listener = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  int64_t port = port_of(listener);
  torb_task *first;
  torb_task *second;
  torb_task *third;
  torb_task *fourth;
  torb_task *fifth;
  int64_t clients[2];
  int64_t servers[2];
  TORB_CHECK(port > 0);
  first = torb_network_accept(listener);
  second = torb_network_accept(listener);
  let_them_start();
  TORB_CHECK(!torb_task_is_complete(first));
  TORB_CHECK(!torb_task_is_complete(second));
  clients[0] = answer_of(torb_network_connect(4, 0, LOOPBACK4, port));
  clients[1] = answer_of(torb_network_connect(4, 0, LOOPBACK4, port));
  TORB_CHECK(clients[0] > 0 && clients[1] > 0);
  /* Whichever took which connection, each took one */
  servers[0] = answer_of(first);
  servers[1] = answer_of(second);
  TORB_CHECK(servers[0] > 0 && servers[1] > 0 && servers[0] != servers[1]);
  third = torb_network_accept(listener);
  fourth = torb_network_accept(listener);
  fifth = torb_network_accept(listener);
  let_them_start();
  TORB_CHECK(!torb_task_is_complete(third));
  TORB_CHECK(!torb_task_is_complete(fourth));
  TORB_CHECK(!torb_task_is_complete(fifth));
  /* The one in the middle goes; the two around it still wait */
  torb_task_cancel(fourth);
  TORB_CHECK_INTEGER(answer_of(fourth), CANCELLED);
  torb_network_close(listener);
  TORB_CHECK_INTEGER(kind_of(answer_of(third)), 10);
  TORB_CHECK_INTEGER(kind_of(answer_of(fifth)), 10);
  torb_network_close(servers[0]);
  torb_network_close(servers[1]);
  torb_network_close(clients[0]);
  torb_network_close(clients[1]);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_closing_wakes_a_receive_that_waits) {
  int64_t server = 0;
  int64_t client = 0;
  torb_task *waiting;
  TORB_CHECK(connected_pair(&server, &client));
  waiting = torb_network_receive(server, 64);
  let_them_start();
  TORB_CHECK(!torb_task_is_complete(waiting));
  torb_network_close(server);
  /* Woken with a failure: nobody waits forever on a socket somebody closed */
  TORB_CHECK(answer_of(waiting) < 0);
  torb_network_close(client);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_the_end_of_the_program_stops_what_waits) {
  int64_t listener = torb_network_listen(4, 0, LOOPBACK4, 0, 8);
  torb_task *waiting;
  TORB_CHECK(listener > 0);
  waiting = torb_network_accept(listener);
  let_them_start();
  torb_task_release(waiting);
  /* The listener is never closed by the program: the end closes it, and the accept is cancelled */
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_resolving_localhost) {
  torb_text host = torb_text_from_cstring("localhost");
  int64_t resolution = answer_of(torb_network_resolve(host));
  torb_list parts = torb_list_new(&torb_element_int64);
  torb_text nobody = torb_text_from_cstring("");
  TORB_CHECK(resolution > 0);
  torb_network_take_resolved(resolution, &parts);
  /* At least one address, three numbers each, and every one of them a loopback address */
  TORB_CHECK(torb_list_length(parts) >= 3);
  TORB_CHECK_INTEGER(torb_list_length(parts) % 3, 0);
  TORB_CHECK(all_loopback(parts));
  /* The resolution's handle is gone once it was taken */
  torb_network_take_resolved(resolution, &parts);
  TORB_CHECK_INTEGER(kind_of(answer_of(torb_network_resolve(nobody))), 8);
  torb_list_release(parts);
  torb_text_release(host);
  torb_text_release(nobody);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_ipv6_loopback) {
  int64_t listener = torb_network_listen(6, 0, 1, 0, 8);
  int64_t port;
  int64_t client;
  int64_t server;
  torb_task *accepting;
  torb_list message = bytes_of("six");
  torb_list answer;
  if (listener < 0) {
    /* A machine without IPv6 on its loopback: nothing to test */
    torb_list_release(message);
    torb_scheduler_finish();
    return;
  }
  port = port_of(listener);
  accepting = torb_network_accept(listener);
  client = answer_of(torb_network_connect(6, 0, 1, port));
  server = answer_of(accepting);
  TORB_CHECK(client > 0 && server > 0);
  TORB_CHECK_INTEGER(answer_of(torb_network_send(client, message, 0)), 3);
  answer = received_bytes(server, 3u);
  TORB_CHECK(bytes_are(answer, "six"));
  torb_list_release(answer);
  torb_list_release(message);
  torb_network_close(server);
  torb_network_close(client);
  torb_network_close(listener);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_large_send_arrives_whole) {
  int64_t server = 0;
  int64_t client = 0;
  torb_list large = torb_list_new(&element_byte);
  torb_list answer;
  torb_task *sending;
  size_t index;
  uint8_t chunk[1024];
  bool same = true;
  for (index = 0u; index < sizeof chunk; index += 1u) {
    chunk[index] = (uint8_t)(index * 7u);
  }
  for (index = 0u; index < 2048u; index += 1u) {
    torb_list_add_plain(&large, chunk, sizeof chunk);
  }
  TORB_CHECK(connected_pair(&server, &client));
  /* Two megabytes do not fit any buffer of the system: the send completes only as the peer reads */
  sending = torb_network_send(client, large, 0);
  answer = received_bytes(server, 2048u * 1024u);
  TORB_CHECK_INTEGER(answer_of(sending), 2048 * 1024);
  TORB_CHECK_INTEGER(torb_list_length(answer), 2048 * 1024);
  for (index = 0u; index < 2048u * 1024u; index += 1u) {
    if (*(const uint8_t *)torb_list_at(answer, (int64_t)index, torb_location_unknown) != (uint8_t)(index * 7u)) {
      same = false;
      break;
    }
  }
  TORB_CHECK(same);
  torb_list_release(answer);
  torb_list_release(large);
  torb_network_close(server);
  torb_network_close(client);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

/* A UDP socket bound to loopback on a port the system chose. */
static int64_t bound_datagram(void) {
  return torb_network_bind(4, 0, LOOPBACK4, 0);
}

/* Receives one datagram on `socket`: its bytes (owned, into `*bytes`) and the port it came from. Its length, or a failure. */
static int64_t received_datagram(int64_t socket, torb_list *bytes, int64_t *port) {
  torb_list from = torb_list_new(&torb_element_int64);
  int64_t length = answer_of(torb_network_receive_datagram(socket));
  *bytes = torb_list_new(&element_byte);
  *port = -1;
  if (length >= 0) {
    length = torb_network_take_datagram(socket, bytes, &from);
    if (torb_list_length(from) == 4) {
      *port = *(const int64_t *)torb_list_at(from, 3, torb_location_unknown);
    }
  }
  torb_list_release(from);
  return length;
}

TORB_TEST(test_io_datagrams_over_loopback) {
  int64_t first = bound_datagram();
  int64_t second = bound_datagram();
  int64_t first_port = port_of(first);
  int64_t second_port = port_of(second);
  torb_list hello = bytes_of("hello, datagram");
  torb_list nothing = bytes_of("");
  torb_list answer;
  int64_t sender = 0;
  TORB_CHECK(first > 0 && second > 0 && first_port > 0 && second_port > 0);
  /* Two datagrams arrive as two, whole and in their own borders, each with where it came from */
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(first, hello, 4, 0, LOOPBACK4, second_port)), 15);
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(first, nothing, 4, 0, LOOPBACK4, second_port)), 0);
  TORB_CHECK_INTEGER(received_datagram(second, &answer, &sender), 15);
  TORB_CHECK(bytes_are(answer, "hello, datagram"));
  TORB_CHECK_INTEGER(sender, first_port);
  torb_list_release(answer);
  /* A datagram without bytes is a datagram */
  TORB_CHECK_INTEGER(received_datagram(second, &answer, &sender), 0);
  TORB_CHECK_INTEGER(torb_list_length(answer), 0);
  torb_list_release(answer);
  /* The answer goes back to where the question came from */
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(second, hello, 4, 0, LOOPBACK4, sender)), 15);
  TORB_CHECK_INTEGER(received_datagram(first, &answer, &sender), 15);
  TORB_CHECK_INTEGER(sender, second_port);
  torb_list_release(answer);
  /* Taking a datagram where none waits is a failure, and so is a send to the peer of a socket that has none */
  answer = torb_list_new(&element_byte);
  {
    torb_list from = torb_list_new(&torb_element_int64);
    TORB_CHECK_INTEGER(kind_of(torb_network_take_datagram(first, &answer, &from)), 11);
    torb_list_release(from);
  }
  torb_list_release(answer);
  TORB_CHECK_INTEGER(kind_of(answer_of(torb_network_send_datagram(first, hello, 0, 0, 0, 0))), 11);
  torb_list_release(hello);
  torb_list_release(nothing);
  torb_network_close(first);
  torb_network_close(second);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_connected_datagram_socket) {
  int64_t first = bound_datagram();
  int64_t second = bound_datagram();
  int64_t stranger = bound_datagram();
  int64_t second_port = port_of(second);
  torb_list ping = bytes_of("ping");
  torb_list noise = bytes_of("noise");
  torb_list peer = torb_list_new(&torb_element_int64);
  torb_list answer;
  int64_t sender = 0;
  TORB_CHECK(first > 0 && second > 0 && stranger > 0);
  TORB_CHECK_INTEGER(torb_network_connect_datagram(first, 4, 0, LOOPBACK4, second_port), 0);
  TORB_CHECK_INTEGER(torb_network_address(first, true, &peer), 0);
  TORB_CHECK_INTEGER(*(const int64_t *)torb_list_at(peer, 3, torb_location_unknown), second_port);
  /* Family 0 is the peer */
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(first, ping, 0, 0, 0, 0)), 4);
  TORB_CHECK_INTEGER(received_datagram(second, &answer, &sender), 4);
  TORB_CHECK(bytes_are(answer, "ping"));
  torb_list_release(answer);
  /* Only the peer's datagrams arrive: the stranger's is dropped by the system, the peer's answer is the next one */
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(stranger, noise, 4, 0, LOOPBACK4, port_of(first))), 5);
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(second, ping, 4, 0, LOOPBACK4, sender)), 4);
  TORB_CHECK_INTEGER(received_datagram(first, &answer, &sender), 4);
  TORB_CHECK(bytes_are(answer, "ping"));
  TORB_CHECK_INTEGER(sender, second_port);
  torb_list_release(answer);
  torb_list_release(peer);
  torb_list_release(ping);
  torb_list_release(noise);
  torb_network_close(first);
  torb_network_close(second);
  torb_network_close(stranger);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_connected_datagram_socket_hears_refused) {
  int64_t gone = bound_datagram();
  int64_t gone_port = port_of(gone);
  int64_t asking = bound_datagram();
  torb_list question = bytes_of("anybody?");
  torb_list answer;
  int64_t sender = 0;
  TORB_CHECK(gone_port > 0 && asking > 0);
  torb_network_close(gone);
  TORB_CHECK_INTEGER(torb_network_connect_datagram(asking, 4, 0, LOOPBACK4, gone_port), 0);
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(asking, question, 0, 0, 0, 0)), 8);
  /* Nothing listens: the system's "port unreachable" is the receive's failure, a refusal on every system */
  TORB_CHECK_INTEGER(kind_of(received_datagram(asking, &answer, &sender)), 2);
  torb_list_release(answer);
  torb_list_release(question);
  torb_network_close(asking);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_a_cancelled_datagram_receive_loses_nothing) {
  int64_t first = bound_datagram();
  int64_t second = bound_datagram();
  torb_list message = bytes_of("after the cancel");
  torb_list answer;
  torb_task *waiting;
  int64_t sender = 0;
  TORB_CHECK(first > 0 && second > 0);
  waiting = torb_network_receive_datagram(second);
  let_them_start();
  TORB_CHECK(!torb_task_is_complete(waiting));
  torb_task_cancel(waiting);
  TORB_CHECK_INTEGER(answer_of(waiting), CANCELLED);
  TORB_CHECK_INTEGER(answer_of(torb_network_send_datagram(first, message, 4, 0, LOOPBACK4, port_of(second))), 16);
  TORB_CHECK_INTEGER(received_datagram(second, &answer, &sender), 16);
  TORB_CHECK(bytes_are(answer, "after the cancel"));
  torb_list_release(answer);
  /* A receive that waits when its socket is closed is woken with "closed" */
  waiting = torb_network_receive_datagram(first);
  let_them_start();
  torb_network_close(first);
  TORB_CHECK_INTEGER(kind_of(answer_of(waiting)), 10);
  torb_list_release(message);
  torb_network_close(second);
  torb_scheduler_finish();
  TORB_CHECK_INTEGER(torb_network_operations_alive(), 0);
}

TORB_TEST(test_io_the_system_names_its_name_servers) {
  torb_list parts = torb_list_new(&torb_element_int64);
  int64_t count = torb_network_name_servers(&parts);
  int64_t index;
  /* A machine may have no network at all, but the answer is never a failure, and every server is on port 53 */
  TORB_CHECK(count >= 0);
  TORB_CHECK_INTEGER(torb_list_length(parts), count * 4);
  for (index = 0; index < count; index += 1) {
    int64_t family = *(const int64_t *)torb_list_at(parts, index * 4, torb_location_unknown);
    TORB_CHECK(family == 4 || family == 6);
    TORB_CHECK_INTEGER(*(const int64_t *)torb_list_at(parts, index * 4 + 3, torb_location_unknown), 53);
  }
  torb_list_release(parts);
}

TORB_TEST(test_io_randomness_differs) {
  int64_t first = torb_network_random();
  int64_t second = torb_network_random();
  int64_t third = torb_network_random();
  /* Three equal draws of 64 bits are a broken source, not bad luck */
  TORB_CHECK(!(first == second && second == third));
}

void torb_register_io_tests(void) {
  TORB_ADD(test_io_resolving_before_any_socket);
  TORB_ADD(test_io_echo_over_loopback);
  TORB_ADD(test_io_shutdown_is_the_end_of_the_stream);
  TORB_ADD(test_io_a_closed_handle_is_a_failure);
  TORB_ADD(test_io_a_refused_connection_fails);
  TORB_ADD(test_io_an_address_in_use_fails);
  TORB_ADD(test_io_a_cancelled_receive_loses_nothing);
  TORB_ADD(test_io_a_cancelled_accept_leaves_the_connection_to_the_next);
  TORB_ADD(test_io_several_accepts_wait_on_one_listener);
  TORB_ADD(test_io_closing_wakes_a_receive_that_waits);
  TORB_ADD(test_io_the_end_of_the_program_stops_what_waits);
  TORB_ADD(test_io_resolving_localhost);
  TORB_ADD(test_io_ipv6_loopback);
  TORB_ADD(test_io_a_large_send_arrives_whole);
  TORB_ADD(test_io_datagrams_over_loopback);
  TORB_ADD(test_io_a_connected_datagram_socket);
  TORB_ADD(test_io_a_connected_datagram_socket_hears_refused);
  TORB_ADD(test_io_a_cancelled_datagram_receive_loses_nothing);
  TORB_ADD(test_io_the_system_names_its_name_servers);
  TORB_ADD(test_io_randomness_differs);
}
