/*
 * tls.c - the natives of `std/tls` over mbedTLS 3 (docs/design/NETWORK.md section 5, runtime/include/torb_tls.h).
 *
 * Compiled only into a program that calls a `torb_tls_` function: the driver adds this directory and the library of
 * `runtime/vendor/mbedtls` to such a program, with `MBEDTLS_USER_CONFIG_FILE` naming `torb_mbedtls_user.h`, and defines
 * `TORB_WITH_TLS` for the thunks of the VM.
 *
 * # A state machine over memory
 *
 * A session reads and writes two buffers of its own through the BIO callbacks below: the ciphertext the program fed it
 * and the ciphertext it produced. Nothing here touches a socket and nothing blocks; a read that needs more from the peer
 * answers so, and the TorbScript side receives and feeds.
 *
 * # One lock
 *
 * Every native holds one mutex of the process for its whole call. PSA's key store and random generator are global in
 * mbedTLS, and without `MBEDTLS_THREADING_C` they must not be entered by two threads at once; one lock around calls that
 * never wait is the simplest thing that is correct. The price is that two handshakes on two workers take turns, which is
 * the later speed change (`MBEDTLS_THREADING_ALT` over the runtime's mutexes) once a benchmark asks for it.
 *
 * # Trust
 *
 * A client that names its own roots (`trusted`) is verified by mbedTLS against them and nothing else. Otherwise, on
 * Windows the platform decides: mbedTLS is given no roots (a trusted-certificate callback that answers none) and a
 * verification callback that hands the presented chain to `CertGetCertificateChain` and the SSL policy at depth 0; on
 * the other systems mbedTLS verifies against the system's bundle of roots, read once per process.
 */

#include "torb.h"
#include "torb_pool.h"
#include "torb_io.h"
#include "torb_tls_platform.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"
#include "mbedtls/entropy.h"
#include "mbedtls/psa_util.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORB_TLS_FAILED 12
#define TORB_TLS_PLATFORM_REJECTED 13
#define TORB_TLS_REJECTED 14

static torb_mutex torb_tls_lock = TORB_MUTEX_INITIALIZER;
static bool torb_tls_ready = false;
static int torb_tls_ready_failure = 0;

/* The system's roots where mbedTLS verifies: read the first time a client needs them, kept for the process. */
static mbedtls_x509_crt torb_tls_system_roots;
static bool torb_tls_system_roots_read = false;
static bool torb_tls_system_roots_found = false;

/* mbedTLS's entropy source where MBEDTLS_ENTROPY_HARDWARE_ALT is defined: declared in its library/entropy_poll.h. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *written);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *written) {
  (void)data;
  if (!torb_tls_platform_random(output, length)) {
    *written = 0u;
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  }
  *written = length;
  return 0;
}

static int64_t torb_tls_failed(int kind, uint32_t code) {
  return -(int64_t)(((uint64_t)(uint32_t)kind << 32) | (uint64_t)code);
}

static void *torb_tls_allocate(size_t size) {
  void *memory = calloc(1u, size);
  if (memory == NULL) {
    torb_panic_out_of_memory(size);
  }
  return memory;
}

/* PSA, once per process; the lock held. 0, or mbedTLS's error. */
static int torb_tls_prepare(void) {
  if (!torb_tls_ready) {
    psa_status_t status = psa_crypto_init();
    torb_tls_ready = true;
    torb_tls_ready_failure = status == PSA_SUCCESS ? 0 : MBEDTLS_ERR_SSL_HW_ACCEL_FAILED;
  }
  return torb_tls_ready_failure;
}

/* ------------------------------------------------------------------------------------------------- buffers --- */

typedef struct torb_tls_buffer {
  uint8_t *bytes;
  size_t start;
  size_t length;
  size_t capacity;
} torb_tls_buffer;

static void torb_tls_buffer_add(torb_tls_buffer *buffer, const uint8_t *bytes, size_t length) {
  if (buffer->start > 0u && buffer->start == buffer->length) {
    buffer->start = 0u;
    buffer->length = 0u;
  }
  if (buffer->length + length > buffer->capacity) {
    size_t capacity = buffer->capacity == 0u ? 16384u : buffer->capacity;
    uint8_t *grown;
    /* What was consumed at the front is dropped before the buffer grows */
    if (buffer->start > 0u) {
      memmove(buffer->bytes, buffer->bytes + buffer->start, buffer->length - buffer->start);
      buffer->length -= buffer->start;
      buffer->start = 0u;
    }
    while (capacity < buffer->length + length) {
      capacity *= 2u;
    }
    if (capacity != buffer->capacity) {
      grown = (uint8_t *)realloc(buffer->bytes, capacity);
      if (grown == NULL) {
        torb_panic_out_of_memory(capacity);
      }
      buffer->bytes = grown;
      buffer->capacity = capacity;
    }
  }
  if (length > 0u) {
    memcpy(buffer->bytes + buffer->length, bytes, length);
  }
  buffer->length += length;
}

static size_t torb_tls_buffer_waiting(const torb_tls_buffer *buffer) {
  return buffer->length - buffer->start;
}

/* ------------------------------------------------------------------------------------------ the records --- */

typedef struct torb_tls_identity_record {
  uint32_t count;
  mbedtls_x509_crt chain;
  mbedtls_pk_context key;
} torb_tls_identity_record;

typedef struct torb_tls_session {
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_x509_crt trusted;
  torb_tls_identity_record *identity;
  torb_tls_buffer incoming;
  torb_tls_buffer outgoing;
  char *server_name;
  /** The platform's error where it rejected the chain, 0 otherwise. */
  uint32_t platform_error;
} torb_tls_session;

typedef enum torb_tls_kind { TORB_TLS_SESSION = 1, TORB_TLS_IDENTITY = 2 } torb_tls_kind;

typedef struct torb_tls_slot {
  void *record;
  uint32_t generation;
  uint8_t kind;
} torb_tls_slot;

static torb_tls_slot *torb_tls_slots = NULL;
static uint32_t torb_tls_slot_count = 0u;

/* A handle for a record; the lock held. */
static int64_t torb_tls_register(void *record, torb_tls_kind kind) {
  uint32_t index;
  for (index = 0u; index < torb_tls_slot_count; index += 1u) {
    if (torb_tls_slots[index].record == NULL) {
      break;
    }
  }
  if (index == torb_tls_slot_count) {
    uint32_t count = torb_tls_slot_count == 0u ? 32u : torb_tls_slot_count * 2u;
    torb_tls_slot *grown = (torb_tls_slot *)realloc(torb_tls_slots, (size_t)count * sizeof(torb_tls_slot));
    uint32_t added;
    if (grown == NULL) {
      torb_panic_out_of_memory((size_t)count * sizeof(torb_tls_slot));
    }
    for (added = torb_tls_slot_count; added < count; added += 1u) {
      grown[added].record = NULL;
      grown[added].generation = 1u;
      grown[added].kind = 0u;
    }
    torb_tls_slots = grown;
    torb_tls_slot_count = count;
  }
  torb_tls_slots[index].record = record;
  torb_tls_slots[index].kind = (uint8_t)kind;
  return ((int64_t)torb_tls_slots[index].generation << 32) | (int64_t)(index + 1u);
}

/* The record of a live handle of `kind`; `NULL` otherwise. The lock held. */
static void *torb_tls_lookup(int64_t handle, torb_tls_kind kind) {
  uint32_t index;
  if (handle <= 0) {
    return NULL;
  }
  index = (uint32_t)(handle & 0xFFFFFFFF) - 1u;
  if (index >= torb_tls_slot_count || torb_tls_slots[index].record == NULL
      || torb_tls_slots[index].generation != (uint32_t)((uint64_t)handle >> 32)
      || torb_tls_slots[index].kind != (uint8_t)kind) {
    return NULL;
  }
  return torb_tls_slots[index].record;
}

static void torb_tls_identity_release(torb_tls_identity_record *identity) {
  identity->count -= 1u;
  if (identity->count == 0u) {
    mbedtls_x509_crt_free(&identity->chain);
    mbedtls_pk_free(&identity->key);
    free(identity);
  }
}

static void torb_tls_session_free(torb_tls_session *session) {
  mbedtls_ssl_free(&session->ssl);
  mbedtls_ssl_config_free(&session->config);
  mbedtls_x509_crt_free(&session->trusted);
  if (session->identity != NULL) {
    torb_tls_identity_release(session->identity);
  }
  free(session->incoming.bytes);
  free(session->outgoing.bytes);
  free(session->server_name);
  free(session);
}

/* ------------------------------------------------------------------------------------------ the callbacks --- */

static int torb_tls_send(void *context, const unsigned char *bytes, size_t length) {
  torb_tls_session *session = (torb_tls_session *)context;
  if (length > (size_t)0x7FFFFFFF) {
    length = (size_t)0x7FFFFFFF;
  }
  torb_tls_buffer_add(&session->outgoing, bytes, length);
  return (int)length;
}

static int torb_tls_receive(void *context, unsigned char *bytes, size_t length) {
  torb_tls_session *session = (torb_tls_session *)context;
  size_t waiting = torb_tls_buffer_waiting(&session->incoming);
  if (waiting == 0u) {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  if (length > waiting) {
    length = waiting;
  }
  if (length > (size_t)0x7FFFFFFF) {
    length = (size_t)0x7FFFFFFF;
  }
  memcpy(bytes, session->incoming.bytes + session->incoming.start, length);
  session->incoming.start += length;
  return (int)length;
}

/* No roots for mbedTLS where the platform verifies: every chain ends untrusted, and the callback below decides. */
static int torb_tls_no_roots(void *context, mbedtls_x509_crt const *child, mbedtls_x509_crt **candidates) {
  (void)context;
  (void)child;
  *candidates = NULL;
  return 0;
}

/* The platform's verdict at depth 0, over the whole chain the server presented; mbedTLS's own flags are the platform's to set. */
static int torb_tls_platform_check(void *context, mbedtls_x509_crt *certificate, int depth, uint32_t *flags) {
  torb_tls_session *session = (torb_tls_session *)context;
  const unsigned char *certificates[16];
  size_t lengths[16];
  size_t count = 0u;
  const mbedtls_x509_crt *current;
  if (depth > 0) {
    *flags = 0u;
    return 0;
  }
  for (current = certificate; current != NULL && current->raw.p != NULL && count < 16u; current = current->next) {
    certificates[count] = current->raw.p;
    lengths[count] = current->raw.len;
    count += 1u;
  }
  session->platform_error = torb_tls_platform_verify(certificates, lengths, count, session->server_name);
  *flags = session->platform_error == 0u ? 0u : MBEDTLS_X509_BADCERT_NOT_TRUSTED;
  return 0;
}

/* ------------------------------------------------------------------------------------------ the natives --- */

/* A text as a NUL-terminated buffer, and its length with the NUL: how mbedTLS reads PEM. */
static unsigned char *torb_tls_terminated(torb_text text, size_t *length) {
  unsigned char *copy = (unsigned char *)torb_tls_allocate((size_t)text.length + 1u);
  if (text.length > 0u) {
    memcpy(copy, text.storage->data + text.offset, text.length);
  }
  *length = (size_t)text.length + 1u;
  return copy;
}

/* The roots of the system where mbedTLS verifies; the lock held. `NULL` where none were found. */
static mbedtls_x509_crt *torb_tls_system_roots_chain(void) {
  if (!torb_tls_system_roots_read) {
    const char *file = torb_tls_platform_roots();
    torb_tls_system_roots_read = true;
    mbedtls_x509_crt_init(&torb_tls_system_roots);
    /* A bundle with a few certificates mbedTLS cannot parse still gives the rest */
    if (file != NULL && mbedtls_x509_crt_parse_file(&torb_tls_system_roots, file) >= 0
        && torb_tls_system_roots.raw.p != NULL) {
      torb_tls_system_roots_found = true;
    }
  }
  return torb_tls_system_roots_found ? &torb_tls_system_roots : NULL;
}

/* A session with its buffers and its configuration's defaults; the lock held. */
static torb_tls_session *torb_tls_session_new(int endpoint, int *failure) {
  torb_tls_session *session = (torb_tls_session *)torb_tls_allocate(sizeof(torb_tls_session));
  mbedtls_ssl_init(&session->ssl);
  mbedtls_ssl_config_init(&session->config);
  mbedtls_x509_crt_init(&session->trusted);
  *failure = mbedtls_ssl_config_defaults(&session->config, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
  if (*failure == 0) {
    mbedtls_ssl_conf_rng(&session->config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
    mbedtls_ssl_conf_min_tls_version(&session->config, MBEDTLS_SSL_VERSION_TLS1_2);
  }
  return session;
}

/* The session set up with its configuration, and a handle for it; the lock held. */
static int64_t torb_tls_session_start(torb_tls_session *session) {
  int failure = mbedtls_ssl_setup(&session->ssl, &session->config);
  if (failure == 0 && session->server_name != NULL) {
    failure = mbedtls_ssl_set_hostname(&session->ssl, session->server_name);
  }
  if (failure != 0) {
    torb_tls_session_free(session);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  mbedtls_ssl_set_bio(&session->ssl, session, torb_tls_send, torb_tls_receive, NULL);
  return torb_tls_register(session, TORB_TLS_SESSION);
}

int64_t torb_tls_client(torb_text server_name, torb_text trusted) {
  torb_tls_session *session;
  int64_t answer;
  int failure;
  size_t length = 0u;
  torb_mutex_lock(&torb_tls_lock);
  failure = torb_tls_prepare();
  if (failure != 0) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  session = torb_tls_session_new(MBEDTLS_SSL_IS_CLIENT, &failure);
  session->server_name = (char *)torb_tls_terminated(server_name, &length);
  if (failure == 0) {
    mbedtls_ssl_conf_authmode(&session->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    if (trusted.length > 0u) {
      unsigned char *pem = torb_tls_terminated(trusted, &length);
      failure = mbedtls_x509_crt_parse(&session->trusted, pem, length);
      free(pem);
      if (failure == 0) {
        mbedtls_ssl_conf_ca_chain(&session->config, &session->trusted, NULL);
      } else if (failure > 0) {
        /* Some certificates of `trusted` could not be read: a trust list is all of what was named, or nothing */
        failure = MBEDTLS_ERR_X509_INVALID_FORMAT;
      }
    } else if (torb_tls_platform_verifies()) {
      mbedtls_ssl_conf_ca_cb(&session->config, torb_tls_no_roots, NULL);
      mbedtls_ssl_conf_verify(&session->config, torb_tls_platform_check, session);
    } else {
      mbedtls_x509_crt *roots = torb_tls_system_roots_chain();
      if (roots == NULL) {
        failure = MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;
      } else {
        mbedtls_ssl_conf_ca_chain(&session->config, roots, NULL);
      }
    }
  }
  if (failure != 0) {
    torb_tls_session_free(session);
    torb_mutex_unlock(&torb_tls_lock);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  answer = torb_tls_session_start(session);
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

int64_t torb_tls_identity(torb_text certificates, torb_text private_key) {
  torb_tls_identity_record *identity;
  unsigned char *pem;
  size_t length = 0u;
  int failure;
  int64_t answer;
  torb_mutex_lock(&torb_tls_lock);
  failure = torb_tls_prepare();
  if (failure != 0) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  identity = (torb_tls_identity_record *)torb_tls_allocate(sizeof(torb_tls_identity_record));
  identity->count = 1u;
  mbedtls_x509_crt_init(&identity->chain);
  mbedtls_pk_init(&identity->key);
  pem = torb_tls_terminated(certificates, &length);
  failure = mbedtls_x509_crt_parse(&identity->chain, pem, length);
  free(pem);
  if (failure > 0) {
    failure = MBEDTLS_ERR_X509_INVALID_FORMAT;
  }
  if (failure == 0) {
    pem = torb_tls_terminated(private_key, &length);
    failure = mbedtls_pk_parse_key(&identity->key, pem, length, NULL, 0u, mbedtls_psa_get_random,
                                   MBEDTLS_PSA_RANDOM_STATE);
    mbedtls_platform_zeroize(pem, length);
    free(pem);
  }
  if (failure == 0) {
    failure = mbedtls_pk_check_pair(&identity->chain.pk, &identity->key, mbedtls_psa_get_random,
                                    MBEDTLS_PSA_RANDOM_STATE);
  }
  if (failure != 0) {
    torb_tls_identity_release(identity);
    torb_mutex_unlock(&torb_tls_lock);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  answer = torb_tls_register(identity, TORB_TLS_IDENTITY);
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

int64_t torb_tls_server(int64_t identity_handle) {
  torb_tls_identity_record *identity;
  torb_tls_session *session;
  int failure;
  int64_t answer;
  torb_mutex_lock(&torb_tls_lock);
  identity = (torb_tls_identity_record *)torb_tls_lookup(identity_handle, TORB_TLS_IDENTITY);
  if (identity == NULL) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  session = torb_tls_session_new(MBEDTLS_SSL_IS_SERVER, &failure);
  identity->count += 1u;
  session->identity = identity;
  if (failure == 0) {
    mbedtls_ssl_conf_authmode(&session->config, MBEDTLS_SSL_VERIFY_NONE);
    failure = mbedtls_ssl_conf_own_cert(&session->config, &identity->chain, &identity->key);
  }
  if (failure != 0) {
    torb_tls_session_free(session);
    torb_mutex_unlock(&torb_tls_lock);
    return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
  }
  answer = torb_tls_session_start(session);
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

void torb_tls_feed(int64_t handle, torb_list bytes) {
  torb_tls_session *session;
  int64_t length = torb_list_length(bytes);
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session != NULL && length > 0 && torb_list_element(bytes)->size == 1u) {
    torb_tls_buffer_add(&session->incoming, (const uint8_t *)torb_list_at(bytes, 0, torb_location_unknown),
                        (size_t)length);
  }
  torb_mutex_unlock(&torb_tls_lock);
}

/* A failure of mbedTLS as the answer of a native: a rejected certificate says who rejected it. The lock held. */
static int64_t torb_tls_failure_of(torb_tls_session *session, int failure) {
  if (failure == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
    if (session->platform_error != 0u) {
      return torb_tls_failed(TORB_TLS_PLATFORM_REJECTED, session->platform_error);
    }
    return torb_tls_failed(TORB_TLS_REJECTED, mbedtls_ssl_get_verify_result(&session->ssl));
  }
  return torb_tls_failed(TORB_TLS_FAILED, (uint32_t)-failure);
}

int64_t torb_tls_handshake(int64_t handle) {
  torb_tls_session *session;
  int64_t answer;
  int failure;
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session == NULL) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  failure = mbedtls_ssl_handshake(&session->ssl);
  if (failure == 0) {
    answer = 0;
  } else if (failure == MBEDTLS_ERR_SSL_WANT_READ || failure == MBEDTLS_ERR_SSL_WANT_WRITE) {
    answer = 1;
  } else {
    answer = torb_tls_failure_of(session, failure);
  }
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

void torb_tls_take_outgoing(int64_t handle, torb_list *into) {
  torb_tls_session *session;
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session != NULL) {
    size_t waiting = torb_tls_buffer_waiting(&session->outgoing);
    torb_list_add_plain(into, session->outgoing.bytes + session->outgoing.start, waiting);
    session->outgoing.start = 0u;
    session->outgoing.length = 0u;
  }
  torb_mutex_unlock(&torb_tls_lock);
}

int64_t torb_tls_write(int64_t handle, torb_list bytes, int64_t from) {
  torb_tls_session *session;
  int64_t length = torb_list_length(bytes);
  int64_t answer;
  int written;
  if (from < 0 || from > length || torb_list_element(bytes)->size != 1u) {
    return torb_io_failed(TORB_IO_INVALID, 0u);
  }
  if (from == length) {
    return 0;
  }
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session == NULL) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  written = mbedtls_ssl_write(&session->ssl, (const unsigned char *)torb_list_at(bytes, from, torb_location_unknown),
                              (size_t)(length - from));
  if (written >= 0) {
    answer = (int64_t)written;
  } else if (written == MBEDTLS_ERR_SSL_WANT_READ || written == MBEDTLS_ERR_SSL_WANT_WRITE) {
    answer = 0;
  } else {
    answer = torb_tls_failure_of(session, written);
  }
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

int64_t torb_tls_read(int64_t handle, torb_list *into, int64_t maximum) {
  torb_tls_session *session;
  unsigned char *buffer;
  int64_t answer = 0;
  int read;
  if (maximum < 1) {
    return torb_io_failed(TORB_IO_INVALID, 0u);
  }
  if (maximum > 65536) {
    maximum = 65536;
  }
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session == NULL) {
    torb_mutex_unlock(&torb_tls_lock);
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  buffer = (unsigned char *)torb_tls_allocate((size_t)maximum);
  for (;;) {
    read = mbedtls_ssl_read(&session->ssl, buffer, (size_t)maximum);
    /* A ticket for resuming the session later is no data: the read goes on */
    if (read == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
      continue;
    }
    break;
  }
  if (read > 0) {
    torb_list_add_plain(into, buffer, (size_t)read);
    answer = (int64_t)read;
  } else if (read == 0 || read == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    answer = -1;
  } else if (read == MBEDTLS_ERR_SSL_WANT_READ || read == MBEDTLS_ERR_SSL_WANT_WRITE) {
    answer = 0;
  } else {
    answer = torb_tls_failure_of(session, read);
  }
  free(buffer);
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

void torb_tls_notify_close(int64_t handle) {
  torb_tls_session *session;
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session != NULL) {
    (void)mbedtls_ssl_close_notify(&session->ssl);
  }
  torb_mutex_unlock(&torb_tls_lock);
}

void torb_tls_close(int64_t handle) {
  uint32_t index;
  torb_mutex_lock(&torb_tls_lock);
  if (handle > 0) {
    index = (uint32_t)(handle & 0xFFFFFFFF) - 1u;
    if (index < torb_tls_slot_count && torb_tls_slots[index].record != NULL
        && torb_tls_slots[index].generation == (uint32_t)((uint64_t)handle >> 32)) {
      void *record = torb_tls_slots[index].record;
      uint8_t kind = torb_tls_slots[index].kind;
      torb_tls_slots[index].record = NULL;
      torb_tls_slots[index].generation =
          torb_tls_slots[index].generation >= 0x7FFFFFFFu ? 1u : torb_tls_slots[index].generation + 1u;
      if (kind == (uint8_t)TORB_TLS_SESSION) {
        torb_tls_session_free((torb_tls_session *)record);
      } else {
        torb_tls_identity_release((torb_tls_identity_record *)record);
      }
    }
  }
  torb_mutex_unlock(&torb_tls_lock);
}

torb_text torb_tls_protocol(int64_t handle) {
  torb_tls_session *session;
  const char *version = "";
  torb_text answer;
  torb_mutex_lock(&torb_tls_lock);
  session = (torb_tls_session *)torb_tls_lookup(handle, TORB_TLS_SESSION);
  if (session != NULL && mbedtls_ssl_is_handshake_over(&session->ssl)) {
    version = mbedtls_ssl_get_version(&session->ssl);
  }
  answer = torb_text_from_cstring(version);
  torb_mutex_unlock(&torb_tls_lock);
  return answer;
}

torb_text torb_tls_error_text(int64_t failure) {
  char buffer[512];
  uint64_t packed = failure < 0 ? (uint64_t)(-failure) : 0u;
  uint32_t kind = (uint32_t)(packed >> 32);
  uint32_t code = (uint32_t)(packed & 0xFFFFFFFFu);
  buffer[0] = '\0';
  if (kind == TORB_TLS_PLATFORM_REJECTED) {
    torb_tls_platform_error_text(code, buffer, sizeof buffer);
  } else if (kind == TORB_TLS_REJECTED) {
    char details[400];
    int written = mbedtls_x509_crt_verify_info(details, sizeof details, "", code);
    /* One line: the flags are listed one per line, and a message is one */
    size_t index;
    for (index = 0u; written > 0 && index < (size_t)written && details[index] != '\0'; index += 1u) {
      if (details[index] == '\n') {
        details[index] = (index + 1u < (size_t)written && details[index + 1u] != '\0') ? ';' : '\0';
      }
    }
    snprintf(buffer, sizeof buffer, "the certificate was rejected: %s", written > 0 ? details : "not trusted");
  } else if (kind == TORB_TLS_FAILED) {
    char details[300];
    mbedtls_strerror(-(int)code, details, sizeof details);
    snprintf(buffer, sizeof buffer, "TLS failed: %s", details);
  } else {
    snprintf(buffer, sizeof buffer, "TLS failed");
  }
  return torb_text_from_cstring(buffer);
}
