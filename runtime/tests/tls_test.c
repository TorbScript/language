/*
 * tls_test.c - the TLS natives of runtime/tls/ without a socket: a client session and a server session hand each other
 * their ciphertext in memory, so a whole handshake, the data both ways and the end of the session run inside one test
 * (docs/design/NETWORK.md section 5). Compiled only where runtime/build.sh builds the TLS part (`TORB_WITH_TLS`).
 *
 * The certificates are tls_certificates.h's: a test root and a certificate for `localhost` it signed. A client that
 * trusts the root accepts it; one that trusts another root, or leaves the verdict to the platform - which has never seen
 * the test root - rejects it, and says who did.
 */

#include "harness.h"

#if defined(TORB_WITH_TLS)

#include "tls_certificates.h"

static const torb_element element_byte = { 1u, 1u, NULL, NULL, NULL, NULL };

static torb_text text_of(const char *value) {
  return torb_text_from_cstring(value);
}

static int64_t kind_of(int64_t failure) {
  return failure >= 0 ? 0 : (int64_t)((uint64_t)(-failure) >> 32);
}

/* Moves the ciphertext `from` produced into `to`; answers how many bytes moved. */
static int64_t carry(int64_t from, int64_t to) {
  torb_list bytes = torb_list_new(&element_byte);
  int64_t length;
  torb_tls_take_outgoing(from, &bytes);
  length = torb_list_length(bytes);
  if (length > 0) {
    torb_tls_feed(to, bytes);
  }
  torb_list_release(bytes);
  return length;
}

/* Runs both handshakes until neither moves: the client's answer, and the server's through `*server_answer`. */
static int64_t handshake_both(int64_t client, int64_t server, int64_t *server_answer) {
  int64_t client_answer = 1;
  int rounds;
  *server_answer = 1;
  for (rounds = 0; rounds < 50; rounds += 1) {
    if (client_answer == 1) {
      client_answer = torb_tls_handshake(client);
    }
    (void)carry(client, server);
    if (*server_answer == 1) {
      *server_answer = torb_tls_handshake(server);
    }
    (void)carry(server, client);
    if (client_answer != 1 && *server_answer != 1) {
      break;
    }
    if (client_answer < 0 || *server_answer < 0) {
      break;
    }
  }
  return client_answer;
}

/* Writes `message` on `from`, carries it, and reads it on `to`: whether it arrived whole. */
static bool send_through(int64_t from, int64_t to, const char *message) {
  torb_list bytes = torb_list_new(&element_byte);
  torb_list read = torb_list_new(&element_byte);
  size_t length = strlen(message);
  int64_t written = 0;
  int64_t answer;
  bool same;
  int rounds;
  torb_list_add_plain(&bytes, message, length);
  while (written < (int64_t)length) {
    int64_t now = torb_tls_write(from, bytes, written);
    if (now <= 0) {
      break;
    }
    written += now;
  }
  (void)carry(from, to);
  for (rounds = 0; rounds < 10 && torb_list_length(read) < (int64_t)length; rounds += 1) {
    answer = torb_tls_read(to, &read, 65536);
    (void)carry(to, from);
    if (answer <= 0) {
      break;
    }
  }
  same = torb_list_length(read) == (int64_t)length
         && memcmp(torb_list_at(read, 0, torb_location_unknown), message, length) == 0;
  torb_list_release(bytes);
  torb_list_release(read);
  return same;
}

TORB_TEST(test_tls_a_handshake_and_data_both_ways) {
  torb_text certificates = text_of(test_server_certificate);
  torb_text key = text_of(test_server_key);
  torb_text name = text_of("localhost");
  torb_text trusted = text_of(test_root);
  int64_t identity = torb_tls_identity(certificates, key);
  int64_t client;
  int64_t server;
  int64_t server_answer = 0;
  torb_text version;
  torb_list nothing = torb_list_new(&element_byte);
  TORB_CHECK(identity > 0);
  client = torb_tls_client(name, trusted);
  server = torb_tls_server(identity);
  TORB_CHECK(client > 0 && server > 0);
  TORB_CHECK_INTEGER(handshake_both(client, server, &server_answer), 0);
  TORB_CHECK_INTEGER(server_answer, 0);
  version = torb_tls_protocol(client);
  TORB_CHECK_TEXT(version, "TLSv1.3");
  torb_text_release(version);
  TORB_CHECK(send_through(client, server, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  TORB_CHECK(send_through(server, client, "HTTP/1.1 204 No Content\r\n\r\n"));
  /* close_notify ends the stream: the other side reads the end, not a failure */
  torb_tls_notify_close(client);
  (void)carry(client, server);
  TORB_CHECK_INTEGER(torb_tls_read(server, &nothing, 1024), -1);
  torb_tls_close(client);
  torb_tls_close(server);
  torb_tls_close(identity);
  /* A handle after its close is closed */
  TORB_CHECK_INTEGER(kind_of(torb_tls_handshake(client)), 10);
  torb_list_release(nothing);
  torb_text_release(certificates);
  torb_text_release(key);
  torb_text_release(name);
  torb_text_release(trusted);
}

TORB_TEST(test_tls_a_certificate_from_another_root_is_rejected) {
  torb_text certificates = text_of(test_server_certificate);
  torb_text key = text_of(test_server_key);
  torb_text name = text_of("localhost");
  torb_text other = text_of(test_other_root);
  int64_t identity = torb_tls_identity(certificates, key);
  int64_t client = torb_tls_client(name, other);
  int64_t server = torb_tls_server(identity);
  int64_t server_answer = 0;
  int64_t answer = handshake_both(client, server, &server_answer);
  torb_text words = torb_tls_error_text(answer);
  TORB_CHECK_INTEGER(kind_of(answer), 14);
  TORB_CHECK(words.length > 0u);
  torb_text_release(words);
  torb_tls_close(client);
  torb_tls_close(server);
  torb_tls_close(identity);
  torb_text_release(certificates);
  torb_text_release(key);
  torb_text_release(name);
  torb_text_release(other);
}

TORB_TEST(test_tls_a_certificate_for_another_name_is_rejected) {
  torb_text certificates = text_of(test_server_certificate);
  torb_text key = text_of(test_server_key);
  torb_text name = text_of("example.test");
  torb_text trusted = text_of(test_root);
  int64_t identity = torb_tls_identity(certificates, key);
  int64_t client = torb_tls_client(name, trusted);
  int64_t server = torb_tls_server(identity);
  int64_t server_answer = 0;
  TORB_CHECK_INTEGER(kind_of(handshake_both(client, server, &server_answer)), 14);
  torb_tls_close(client);
  torb_tls_close(server);
  torb_tls_close(identity);
  torb_text_release(certificates);
  torb_text_release(key);
  torb_text_release(name);
  torb_text_release(trusted);
}

TORB_TEST(test_tls_the_platform_decides_where_nothing_is_trusted_by_name) {
  torb_text certificates = text_of(test_server_certificate);
  torb_text key = text_of(test_server_key);
  torb_text name = text_of("localhost");
  torb_text none = text_of("");
  int64_t identity = torb_tls_identity(certificates, key);
  int64_t client = torb_tls_client(name, none);
  int64_t server = torb_tls_server(identity);
  int64_t server_answer = 0;
  int64_t answer;
  /* Where the system has no roots at all the client cannot even start; otherwise the test root is unknown to it */
  if (client > 0) {
    answer = handshake_both(client, server, &server_answer);
    TORB_CHECK(kind_of(answer) == 13 || kind_of(answer) == 14);
    torb_tls_close(client);
  }
  torb_tls_close(server);
  torb_tls_close(identity);
  torb_text_release(certificates);
  torb_text_release(key);
  torb_text_release(name);
  torb_text_release(none);
}

TORB_TEST(test_tls_a_key_that_does_not_belong_to_the_certificate_is_refused) {
  torb_text certificates = text_of(test_other_root);
  torb_text key = text_of(test_server_key);
  torb_text garbage = text_of("not a certificate");
  TORB_CHECK_INTEGER(kind_of(torb_tls_identity(certificates, key)), 12);
  TORB_CHECK_INTEGER(kind_of(torb_tls_identity(garbage, key)), 12);
  torb_text_release(certificates);
  torb_text_release(key);
  torb_text_release(garbage);
}

void torb_register_tls_tests(void) {
  TORB_ADD(test_tls_a_handshake_and_data_both_ways);
  TORB_ADD(test_tls_a_certificate_from_another_root_is_rejected);
  TORB_ADD(test_tls_a_certificate_for_another_name_is_rejected);
  TORB_ADD(test_tls_the_platform_decides_where_nothing_is_trusted_by_name);
  TORB_ADD(test_tls_a_key_that_does_not_belong_to_the_certificate_is_refused);
}

#else

void torb_register_tls_tests(void) {}

#endif /* TORB_WITH_TLS */
