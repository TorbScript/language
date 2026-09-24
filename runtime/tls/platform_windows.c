/*
 * platform_windows.c - what TLS asks of Windows (runtime/tls/torb_tls_platform.h): randomness from `BCryptGenRandom`,
 * and the verdict on a certificate chain from the chain engine of CryptoAPI and its SSL policy - the machine's roots,
 * its enterprise roots and its policies, as every other program of the machine sees them.
 *
 * Both libraries are loaded on first use (`bcrypt.dll`, `crypt32.dll`), so a program links nothing beyond what every
 * Windows C compiler links by default. Revocation is not checked: a check goes to the network and waits, and neither Go
 * nor rustls does it by default either (docs/design/NETWORK.md section 5).
 *
 * The whole file is one `#if defined(_WIN32)`, like every file of `runtime/os/`.
 */

#include "torb_tls_platform.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>


#include <stdio.h>
#include <string.h>

typedef LONG(WINAPI *torb_generate_random_function)(void *, PUCHAR, ULONG, ULONG);
typedef HCERTSTORE(WINAPI *torb_open_store_function)(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void *);
typedef BOOL(WINAPI *torb_close_store_function)(HCERTSTORE, DWORD);
typedef BOOL(WINAPI *torb_add_encoded_function)(HCERTSTORE, DWORD, const BYTE *, DWORD, DWORD, PCCERT_CONTEXT *);
typedef BOOL(WINAPI *torb_free_context_function)(PCCERT_CONTEXT);
typedef BOOL(WINAPI *torb_get_chain_function)(HCERTCHAINENGINE, PCCERT_CONTEXT, LPFILETIME, HCERTSTORE,
                                              PCERT_CHAIN_PARA, DWORD, LPVOID, PCCERT_CHAIN_CONTEXT *);
typedef BOOL(WINAPI *torb_verify_policy_function)(LPCSTR, PCCERT_CHAIN_CONTEXT, PCERT_CHAIN_POLICY_PARA,
                                                  PCERT_CHAIN_POLICY_STATUS);
typedef VOID(WINAPI *torb_free_chain_function)(PCCERT_CHAIN_CONTEXT);

typedef struct torb_crypt {
  torb_open_store_function open_store;
  torb_close_store_function close_store;
  torb_add_encoded_function add_encoded;
  torb_free_context_function free_context;
  torb_get_chain_function get_chain;
  torb_verify_policy_function verify_policy;
  torb_free_chain_function free_chain;
  bool loaded;
  bool tried;
} torb_crypt;

static torb_crypt torb_crypt_functions;
static torb_generate_random_function torb_generate_random = NULL;
static bool torb_random_tried = false;

/* A function of a module, as the pointer type it has: through `void (*)(void)`, the one cast C lets pass unwarned. */
static void (*torb_tls_symbol(HMODULE module, const char *name))(void) {
  return (void (*)(void))GetProcAddress(module, name);
}

bool torb_tls_platform_random(unsigned char *output, size_t length) {
  /* BCRYPT_USE_SYSTEM_PREFERRED_RNG: the system's generator, without an algorithm handle */
  const ULONG preferred = 0x00000002u;
  if (!torb_random_tried) {
    HMODULE library = LoadLibraryW(L"bcrypt.dll");
    torb_random_tried = true;
    if (library != NULL) {
      torb_generate_random = (torb_generate_random_function)torb_tls_symbol(library, "BCryptGenRandom");
    }
  }
  if (torb_generate_random == NULL) {
    return false;
  }
  while (length > 0u) {
    ULONG chunk = length > 0x7FFFFFFFu ? 0x7FFFFFFFu : (ULONG)length;
    if (torb_generate_random(NULL, output, chunk, preferred) != 0) {
      return false;
    }
    output += chunk;
    length -= chunk;
  }
  return true;
}

bool torb_tls_platform_verifies(void) {
  return true;
}

const char *torb_tls_platform_roots(void) {
  return NULL;
}

static bool torb_crypt_load(void) {
  HMODULE library;
  if (torb_crypt_functions.tried) {
    return torb_crypt_functions.loaded;
  }
  torb_crypt_functions.tried = true;
  library = LoadLibraryW(L"crypt32.dll");
  if (library == NULL) {
    return false;
  }
  torb_crypt_functions.open_store = (torb_open_store_function)torb_tls_symbol(library, "CertOpenStore");
  torb_crypt_functions.close_store = (torb_close_store_function)torb_tls_symbol(library, "CertCloseStore");
  torb_crypt_functions.add_encoded =
      (torb_add_encoded_function)torb_tls_symbol(library, "CertAddEncodedCertificateToStore");
  torb_crypt_functions.free_context =
      (torb_free_context_function)torb_tls_symbol(library, "CertFreeCertificateContext");
  torb_crypt_functions.get_chain = (torb_get_chain_function)torb_tls_symbol(library, "CertGetCertificateChain");
  torb_crypt_functions.verify_policy =
      (torb_verify_policy_function)torb_tls_symbol(library, "CertVerifyCertificateChainPolicy");
  torb_crypt_functions.free_chain = (torb_free_chain_function)torb_tls_symbol(library, "CertFreeCertificateChain");
  torb_crypt_functions.loaded =
      torb_crypt_functions.open_store != NULL && torb_crypt_functions.close_store != NULL
      && torb_crypt_functions.add_encoded != NULL && torb_crypt_functions.free_context != NULL
      && torb_crypt_functions.get_chain != NULL && torb_crypt_functions.verify_policy != NULL
      && torb_crypt_functions.free_chain != NULL;
  return torb_crypt_functions.loaded;
}

uint32_t torb_tls_platform_verify(const unsigned char *const *certificates, const size_t *lengths, size_t count,
                                  const char *server_name) {
  HCERTSTORE store;
  PCCERT_CONTEXT leaf = NULL;
  PCCERT_CHAIN_CONTEXT chain = NULL;
  CERT_CHAIN_PARA parameters;
  CERT_CHAIN_POLICY_PARA policy;
  CERT_CHAIN_POLICY_STATUS status;
  SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
  LPSTR usages[1];
  wchar_t name[512];
  uint32_t answer = 0u;
  size_t index;
  if (count == 0u) {
    return (uint32_t)CERT_E_CHAINING;
  }
  if (!torb_crypt_load()) {
    return (uint32_t)GetLastError();
  }
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, server_name, -1, name, 512) <= 0) {
    return (uint32_t)CERT_E_CN_NO_MATCH;
  }
  /* The certificates the server presented, in a store of this call: the intermediates the chain engine may use */
  store = torb_crypt_functions.open_store(CERT_STORE_PROV_MEMORY, 0u, 0u, 0u, NULL);
  if (store == NULL) {
    return (uint32_t)GetLastError();
  }
  for (index = 0u; index < count; index += 1u) {
    PCCERT_CONTEXT added = NULL;
    if (!torb_crypt_functions.add_encoded(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certificates[index],
                                          (DWORD)lengths[index], CERT_STORE_ADD_ALWAYS,
                                          index == 0u ? &added : NULL)) {
      answer = (uint32_t)GetLastError();
      break;
    }
    if (index == 0u) {
      leaf = added;
    }
  }
  if (answer == 0u) {
    memset(&parameters, 0, sizeof parameters);
    parameters.cbSize = sizeof parameters;
    usages[0] = (LPSTR)szOID_PKIX_KP_SERVER_AUTH;
    parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    parameters.RequestedUsage.Usage.cUsageIdentifier = 1u;
    parameters.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    if (!torb_crypt_functions.get_chain(NULL, leaf, NULL, store, &parameters, 0u, NULL, &chain)) {
      answer = (uint32_t)GetLastError();
    }
  }
  if (answer == 0u) {
    memset(&ssl, 0, sizeof ssl);
    ssl.cbSize = sizeof ssl;
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = name;
    memset(&policy, 0, sizeof policy);
    policy.cbSize = sizeof policy;
    policy.pvExtraPolicyPara = &ssl;
    memset(&status, 0, sizeof status);
    status.cbSize = sizeof status;
    if (!torb_crypt_functions.verify_policy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) {
      answer = (uint32_t)GetLastError();
    } else {
      answer = (uint32_t)status.dwError;
    }
  }
  if (chain != NULL) {
    torb_crypt_functions.free_chain(chain);
  }
  if (leaf != NULL) {
    torb_crypt_functions.free_context(leaf);
  }
  torb_crypt_functions.close_store(store, 0u);
  return answer;
}

void torb_tls_platform_error_text(uint32_t code, char *buffer, size_t size) {
  wchar_t wide[512];
  DWORD length;
  int written;
  if (size == 0u) {
    return;
  }
  length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code,
                          MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), wide, 512u, NULL);
  if (length == 0u) {
    length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code, 0u, wide,
                            512u, NULL);
  }
  if (length == 0u) {
    snprintf(buffer, size, "the certificate was rejected: error 0x%08lx", (unsigned long)code);
    return;
  }
  while (length > 0u && (wide[length - 1u] == L'\r' || wide[length - 1u] == L'\n' || wide[length - 1u] == L'.'
                         || wide[length - 1u] == L' ')) {
    length -= 1u;
  }
  written = snprintf(buffer, size, "the certificate was rejected: ");
  if (written < 0 || (size_t)written >= size) {
    return;
  }
  {
    int converted = WideCharToMultiByte(CP_UTF8, 0u, wide, (int)length, buffer + written, (int)(size - (size_t)written) - 1,
                                        NULL, NULL);
    buffer[written + (converted > 0 ? converted : 0)] = '\0';
  }
}

#endif /* _WIN32 */
