/*
 * iocp.c - the poller of the IO core on Windows: an IO completion port and Winsock (docs/design/NETWORK.md section 2,
 * runtime/include/torb_io.h). The natives of `std/os` that only Windows has are `windows.c`'s; this file is the IO core's.
 *
 * The whole file is one `#if defined(_WIN32)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it.
 *
 * **Winsock is loaded on first use**, with `LoadLibraryW` and `GetProcAddress`, and so are `AcceptEx` and `ConnectEx`
 * (through `WSAIoctl`, as Microsoft documents them). A program that never opens a socket never loads `ws2_32.dll`, and
 * no build needs `-lws2_32`: the seed that builds the compiler links the runtime as it always did.
 *
 * Every socket is overlapped and associated with one completion port. An operation's `OVERLAPPED` is the first member
 * of the operation, so a completion names its operation. **Every operation completes through the port**, also one the
 * system finished at once (no `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`): one path, whatever the timing. Only a call that
 * fails outright - no completion will come - completes the operation where it was submitted.
 *
 * The IO thread waits in `GetQueuedCompletionStatusEx` without a limit; the stop is a packet with a key of its own. The
 * status of a completion is the `NTSTATUS` in the `OVERLAPPED`, translated with `RtlNtStatusToDosError` of ntdll,
 * which every process has loaded.
 */

#include "torb.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#include "torb_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------------------- Winsock, loaded on first use --- */

typedef int(WSAAPI *torb_wsa_startup_function)(WORD, LPWSADATA);
typedef SOCKET(WSAAPI *torb_wsa_socket_function)(int, int, int, LPWSAPROTOCOL_INFOW, GROUP, DWORD);
typedef int(WSAAPI *torb_wsa_bind_function)(SOCKET, const struct sockaddr *, int);
typedef int(WSAAPI *torb_wsa_listen_function)(SOCKET, int);
typedef int(WSAAPI *torb_wsa_close_function)(SOCKET);
typedef int(WSAAPI *torb_wsa_set_option_function)(SOCKET, int, int, const char *, int);
typedef int(WSAAPI *torb_wsa_name_function)(SOCKET, struct sockaddr *, int *);
typedef int(WSAAPI *torb_wsa_ioctl_function)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED,
                                             LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WSAAPI *torb_wsa_receive_function)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED,
                                               LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WSAAPI *torb_wsa_send_function)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED,
                                            LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WSAAPI *torb_wsa_last_error_function)(void);
typedef int(WSAAPI *torb_wsa_shutdown_function)(SOCKET, int);
typedef INT(WSAAPI *torb_wsa_resolve_function)(PCWSTR, PCWSTR, const ADDRINFOW *, PADDRINFOW *);
typedef VOID(WSAAPI *torb_wsa_free_resolved_function)(PADDRINFOW);
typedef ULONG(WINAPI *torb_status_to_error_function)(LONG);

typedef struct torb_winsock {
  torb_wsa_startup_function startup;
  torb_wsa_socket_function socket;
  torb_wsa_bind_function bind;
  torb_wsa_listen_function listen;
  torb_wsa_close_function close;
  torb_wsa_set_option_function set_option;
  torb_wsa_name_function local_name;
  torb_wsa_name_function peer_name;
  torb_wsa_ioctl_function ioctl;
  torb_wsa_receive_function receive;
  torb_wsa_send_function send;
  torb_wsa_last_error_function last_error;
  torb_wsa_shutdown_function shutdown;
  torb_wsa_resolve_function resolve;
  torb_wsa_free_resolved_function free_resolved;
  torb_status_to_error_function status_to_error;
  /** `AcceptEx` and `ConnectEx`, per family: index 0 for IPv4, 1 for IPv6. Asked for the first time a family needs one. */
  LPFN_ACCEPTEX accept[2];
  LPFN_CONNECTEX connect[2];
  bool loaded;
} torb_winsock;

static torb_winsock torb_ws;
static HANDLE torb_iocp_port = NULL;
static torb_thread torb_iocp_thread;
/** Guards the pointers of `AcceptEx` and `ConnectEx`, which any worker may ask for first. */
static SRWLOCK torb_ws_extension_lock = SRWLOCK_INIT;

#define TORB_IOCP_SOCKET_KEY ((ULONG_PTR)0u)
#define TORB_IOCP_STOP_KEY ((ULONG_PTR)1u)

/* A function of a module, as the pointer type it has: through `void (*)(void)`, the one cast C lets pass unwarned. */
static void (*torb_ws_symbol(HMODULE module, const char *name))(void) {
  return (void (*)(void))GetProcAddress(module, name);
}

/* Loads Winsock and starts it, once; the core's lock is held. False where the system has none. */
static bool torb_ws_load(void) {
  HMODULE library;
  HMODULE ntdll;
  WSADATA data;
  if (torb_ws.loaded) {
    return true;
  }
  library = LoadLibraryW(L"ws2_32.dll");
  ntdll = GetModuleHandleW(L"ntdll.dll");
  if (library == NULL || ntdll == NULL) {
    return false;
  }
  torb_ws.startup = (torb_wsa_startup_function)torb_ws_symbol(library, "WSAStartup");
  torb_ws.socket = (torb_wsa_socket_function)torb_ws_symbol(library, "WSASocketW");
  torb_ws.bind = (torb_wsa_bind_function)torb_ws_symbol(library, "bind");
  torb_ws.listen = (torb_wsa_listen_function)torb_ws_symbol(library, "listen");
  torb_ws.close = (torb_wsa_close_function)torb_ws_symbol(library, "closesocket");
  torb_ws.set_option = (torb_wsa_set_option_function)torb_ws_symbol(library, "setsockopt");
  torb_ws.local_name = (torb_wsa_name_function)torb_ws_symbol(library, "getsockname");
  torb_ws.peer_name = (torb_wsa_name_function)torb_ws_symbol(library, "getpeername");
  torb_ws.ioctl = (torb_wsa_ioctl_function)torb_ws_symbol(library, "WSAIoctl");
  torb_ws.receive = (torb_wsa_receive_function)torb_ws_symbol(library, "WSARecv");
  torb_ws.send = (torb_wsa_send_function)torb_ws_symbol(library, "WSASend");
  torb_ws.last_error = (torb_wsa_last_error_function)torb_ws_symbol(library, "WSAGetLastError");
  torb_ws.shutdown = (torb_wsa_shutdown_function)torb_ws_symbol(library, "shutdown");
  torb_ws.resolve = (torb_wsa_resolve_function)torb_ws_symbol(library, "GetAddrInfoW");
  torb_ws.free_resolved = (torb_wsa_free_resolved_function)torb_ws_symbol(library, "FreeAddrInfoW");
  torb_ws.status_to_error = (torb_status_to_error_function)torb_ws_symbol(ntdll, "RtlNtStatusToDosError");
  if (torb_ws.startup == NULL || torb_ws.socket == NULL || torb_ws.bind == NULL || torb_ws.listen == NULL
      || torb_ws.close == NULL || torb_ws.set_option == NULL || torb_ws.local_name == NULL || torb_ws.peer_name == NULL
      || torb_ws.ioctl == NULL || torb_ws.receive == NULL || torb_ws.send == NULL || torb_ws.last_error == NULL
      || torb_ws.shutdown == NULL || torb_ws.resolve == NULL || torb_ws.free_resolved == NULL
      || torb_ws.status_to_error == NULL) {
    return false;
  }
  if (torb_ws.startup(MAKEWORD(2, 2), &data) != 0) {
    return false;
  }
  torb_ws.loaded = true;
  return true;
}

/* ------------------------------------------------------------------------------------------------- failures --- */

torb_io_failure torb_io_system_failure_kind(uint32_t code) {
  switch (code) {
    case WSAECONNREFUSED:
    case ERROR_CONNECTION_REFUSED:
      return TORB_IO_REFUSED;
    case WSAECONNRESET:
    case ERROR_NETNAME_DELETED:
    case WSAENETRESET:
      return TORB_IO_RESET;
    case WSAECONNABORTED:
    case ERROR_CONNECTION_ABORTED:
    case ERROR_OPERATION_ABORTED:
      return TORB_IO_ABORTED;
    case WSAETIMEDOUT:
    case ERROR_SEM_TIMEOUT:
      return TORB_IO_TIMED_OUT;
    case WSAEADDRINUSE:
      return TORB_IO_ADDRESS_IN_USE;
    case WSAEADDRNOTAVAIL:
      return TORB_IO_ADDRESS_NOT_AVAILABLE;
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
    case WSATRY_AGAIN:
      return TORB_IO_HOST_NOT_FOUND;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_HOST_UNREACHABLE:
    case ERROR_PORT_UNREACHABLE:
      return TORB_IO_UNREACHABLE;
    case WSAENOTSOCK:
      return TORB_IO_CLOSED;
    case WSAEINVAL:
    case WSAEAFNOSUPPORT:
      return TORB_IO_INVALID;
    default:
      return TORB_IO_OTHER;
  }
}

static int64_t torb_ws_failed(uint32_t code) {
  return torb_io_failed(torb_io_system_failure_kind(code), code);
}

/* The last error of Winsock on this thread, as a packed failure. */
static int64_t torb_ws_last_failure(void) {
  return torb_ws_failed((uint32_t)torb_ws.last_error());
}

void torb_io_system_error_text(uint32_t code, char *buffer, size_t size) {
  wchar_t wide[512];
  DWORD length;
  int written;
  /* English where the system has it, so a message is the same on every machine that runs the program */
  length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code,
                          MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), wide, 512u, NULL);
  if (length == 0u) {
    length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code, 0u, wide,
                            512u, NULL);
  }
  if (size == 0u) {
    return;
  }
  buffer[0] = '\0';
  if (length == 0u) {
    snprintf(buffer, size, "system error %lu", (unsigned long)code);
    return;
  }
  /* Without the line break and the full stop the system ends its sentence with */
  while (length > 0u && (wide[length - 1u] == L'\r' || wide[length - 1u] == L'\n' || wide[length - 1u] == L'.'
                         || wide[length - 1u] == L' ')) {
    length -= 1u;
  }
  written = WideCharToMultiByte(CP_UTF8, 0u, wide, (int)length, buffer, (int)size - 1, NULL, NULL);
  buffer[written > 0 ? written : 0] = '\0';
}

/* ------------------------------------------------------------------------------------------------ addresses --- */

static uint16_t torb_ws_network_order(uint16_t port) {
  return (uint16_t)((port >> 8) | (port << 8));
}

/* The system's form of an address; answers its length. */
static int torb_ws_address_of(const torb_io_address *address, SOCKADDR_STORAGE *out) {
  memset(out, 0, sizeof *out);
  if (address->family == 4) {
    struct sockaddr_in *version4 = (struct sockaddr_in *)out;
    version4->sin_family = AF_INET;
    version4->sin_port = torb_ws_network_order(address->port);
    memcpy(&version4->sin_addr, address->bytes, 4u);
    return (int)sizeof(struct sockaddr_in);
  }
  {
    struct sockaddr_in6 *version6 = (struct sockaddr_in6 *)out;
    version6->sin6_family = AF_INET6;
    version6->sin6_port = torb_ws_network_order(address->port);
    memcpy(&version6->sin6_addr, address->bytes, 16u);
    return (int)sizeof(struct sockaddr_in6);
  }
}

/* The core's form of an address of the system; false for a family that is neither IPv4 nor IPv6. */
static bool torb_ws_address_from(const struct sockaddr *address, torb_io_address *out) {
  memset(out, 0, sizeof *out);
  if (address->sa_family == AF_INET) {
    const struct sockaddr_in *version4 = (const struct sockaddr_in *)(const void *)address;
    out->family = 4;
    out->port = torb_ws_network_order(version4->sin_port);
    memcpy(out->bytes, &version4->sin_addr, 4u);
    return true;
  }
  if (address->sa_family == AF_INET6) {
    const struct sockaddr_in6 *version6 = (const struct sockaddr_in6 *)(const void *)address;
    out->family = 6;
    out->port = torb_ws_network_order(version6->sin6_port);
    memcpy(out->bytes, &version6->sin6_addr, 16u);
    return true;
  }
  return false;
}

/* ------------------------------------------------------------------------------------------------ the IO thread --- */

/* A completion: what the kernel said, as the operation's answer. */
static void torb_iocp_finish(torb_io_operation *operation, OVERLAPPED *overlapped, DWORD bytes) {
  LONG status = (LONG)overlapped->Internal;
  int64_t result;
  if (status == 0) {
    BOOL yes = TRUE;
    switch ((torb_io_operation_kind)operation->kind) {
      case TORB_IO_ACCEPT: {
        SOCKET listener = (SOCKET)operation->socket->system;
        SOCKET accepted = (SOCKET)operation->accepted->system;
        (void)torb_ws.set_option(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (const char *)&listener,
                                 (int)sizeof listener);
        (void)torb_ws.set_option(accepted, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (int)sizeof yes);
        result = 0;
        break;
      }
      case TORB_IO_CONNECT: {
        SOCKET connected = (SOCKET)operation->socket->system;
        (void)torb_ws.set_option(connected, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        (void)torb_ws.set_option(connected, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, (int)sizeof yes);
        result = 0;
        break;
      }
      case TORB_IO_RECEIVE:
      case TORB_IO_SEND:
      case TORB_IO_RESOLVE:
      default:
        result = (int64_t)bytes;
        break;
    }
  } else {
    result = torb_ws_failed((uint32_t)torb_ws.status_to_error(status));
  }
  torb_io_complete(operation, result);
}

static void torb_iocp_main(void *argument) {
  OVERLAPPED_ENTRY entries[64];
  bool stopping = false;
  (void)argument;
  while (!stopping) {
    ULONG count = 0u;
    ULONG index;
    if (!GetQueuedCompletionStatusEx(torb_iocp_port, entries, 64u, &count, INFINITE, FALSE)) {
      continue;
    }
    for (index = 0u; index < count; index += 1u) {
      if (entries[index].lpCompletionKey == TORB_IOCP_STOP_KEY) {
        stopping = true;
        continue;
      }
      if (entries[index].lpOverlapped != NULL) {
        /* The `OVERLAPPED` is the operation's first member */
        torb_iocp_finish((torb_io_operation *)(void *)entries[index].lpOverlapped, entries[index].lpOverlapped,
                         entries[index].dwNumberOfBytesTransferred);
      }
    }
  }
}

bool torb_io_system_start(int64_t *failure) {
  if (!torb_ws_load()) {
    *failure = torb_io_failed(TORB_IO_OTHER, (uint32_t)GetLastError());
    return false;
  }
  torb_iocp_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0u, 1u);
  if (torb_iocp_port == NULL) {
    *failure = torb_io_failed(TORB_IO_OTHER, (uint32_t)GetLastError());
    return false;
  }
  if (!torb_thread_start(&torb_iocp_thread, torb_iocp_main, NULL, (size_t)256u * 1024u)) {
    CloseHandle(torb_iocp_port);
    torb_iocp_port = NULL;
    *failure = torb_io_failed(TORB_IO_OTHER, 0u);
    return false;
  }
  return true;
}

void torb_io_system_stop(void) {
  if (torb_iocp_port == NULL) {
    return;
  }
  (void)PostQueuedCompletionStatus(torb_iocp_port, 0u, TORB_IOCP_STOP_KEY, NULL);
  torb_thread_join(&torb_iocp_thread);
  CloseHandle(torb_iocp_port);
  torb_iocp_port = NULL;
}

/* -------------------------------------------------------------------------------------------------- sockets --- */

int64_t torb_io_system_socket(int32_t family) {
  SOCKET handle = torb_ws.socket(family == 6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u,
                                 WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (handle == INVALID_SOCKET) {
    return torb_ws_last_failure();
  }
  if (CreateIoCompletionPort((HANDLE)handle, torb_iocp_port, TORB_IOCP_SOCKET_KEY, 0u) == NULL) {
    uint32_t code = (uint32_t)GetLastError();
    (void)torb_ws.close(handle);
    return torb_ws_failed(code);
  }
  return (int64_t)handle;
}

int64_t torb_io_system_listen(torb_io_socket *socket, const torb_io_address *address, int64_t backlog) {
  SOCKET handle = (SOCKET)socket->system;
  SOCKADDR_STORAGE system;
  int length = torb_ws_address_of(address, &system);
  if (address->family == 6) {
    /* An IPv6 listener hears IPv6 only, on every system alike (Linux's default is the other way) */
    DWORD only = 1u;
    (void)torb_ws.set_option(handle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&only, (int)sizeof only);
  }
  if (torb_ws.bind(handle, (const struct sockaddr *)&system, length) != 0) {
    return torb_ws_last_failure();
  }
  if (torb_ws.listen(handle, (int)backlog) != 0) {
    return torb_ws_last_failure();
  }
  return 0;
}

/* `AcceptEx` for the family of `handle`, asked for once. `NULL` where the system does not give it. */
static LPFN_ACCEPTEX torb_ws_accept_function(SOCKET handle, int32_t family) {
  int slot = family == 6 ? 1 : 0;
  LPFN_ACCEPTEX found;
  AcquireSRWLockExclusive(&torb_ws_extension_lock);
  if (torb_ws.accept[slot] == NULL) {
    GUID identifier = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX function = NULL;
    DWORD returned = 0u;
    if (torb_ws.ioctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER, &identifier, (DWORD)sizeof identifier, &function,
                      (DWORD)sizeof function, &returned, NULL, NULL)
        == 0) {
      torb_ws.accept[slot] = function;
    }
  }
  found = torb_ws.accept[slot];
  ReleaseSRWLockExclusive(&torb_ws_extension_lock);
  return found;
}

/* `ConnectEx` for the family of `handle`, asked for once. `NULL` where the system does not give it. */
static LPFN_CONNECTEX torb_ws_connect_function(SOCKET handle, int32_t family) {
  int slot = family == 6 ? 1 : 0;
  LPFN_CONNECTEX found;
  AcquireSRWLockExclusive(&torb_ws_extension_lock);
  if (torb_ws.connect[slot] == NULL) {
    GUID identifier = WSAID_CONNECTEX;
    LPFN_CONNECTEX function = NULL;
    DWORD returned = 0u;
    if (torb_ws.ioctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER, &identifier, (DWORD)sizeof identifier, &function,
                      (DWORD)sizeof function, &returned, NULL, NULL)
        == 0) {
      torb_ws.connect[slot] = function;
    }
  }
  found = torb_ws.connect[slot];
  ReleaseSRWLockExclusive(&torb_ws_extension_lock);
  return found;
}

/* The size AcceptEx wants per address: the largest address, plus sixteen bytes it documents. */
#define TORB_WS_ACCEPT_ADDRESS ((DWORD)(sizeof(SOCKADDR_STORAGE) + 16u))

void torb_io_system_submit(torb_io_operation *operation) {
  OVERLAPPED *overlapped = (OVERLAPPED *)(void *)operation->system;
  SOCKET handle = (SOCKET)operation->socket->system;
  int code = 0;
  memset(overlapped, 0, sizeof *overlapped);
  switch ((torb_io_operation_kind)operation->kind) {
    case TORB_IO_RECEIVE: {
      WSABUF buffer;
      DWORD flags = 0u;
      buffer.len = (ULONG)operation->capacity;
      buffer.buf = (CHAR *)operation->buffer;
      if (torb_ws.receive(handle, &buffer, 1u, NULL, &flags, overlapped, NULL) != 0) {
        code = torb_ws.last_error();
      }
      break;
    }
    case TORB_IO_SEND: {
      WSABUF buffer;
      buffer.len = (ULONG)(operation->length - operation->offset);
      buffer.buf = (CHAR *)(operation->buffer + operation->offset);
      if (torb_ws.send(handle, &buffer, 1u, NULL, 0u, overlapped, NULL) != 0) {
        code = torb_ws.last_error();
      }
      break;
    }
    case TORB_IO_ACCEPT: {
      LPFN_ACCEPTEX accept = torb_ws_accept_function(handle, operation->socket->family);
      int64_t accepted = torb_io_system_socket(operation->socket->family);
      DWORD received = 0u;
      if (accepted < 0) {
        torb_io_complete(operation, accepted);
        return;
      }
      operation->accepted = torb_io_socket_new(TORB_IO_STREAM, operation->socket->family, accepted);
      operation->buffer = (uint8_t *)calloc(2u, TORB_WS_ACCEPT_ADDRESS);
      if (accept == NULL || operation->buffer == NULL) {
        torb_io_complete(operation, torb_io_failed(TORB_IO_OTHER, 0u));
        return;
      }
      if (!accept(handle, (SOCKET)accepted, operation->buffer, 0u, TORB_WS_ACCEPT_ADDRESS, TORB_WS_ACCEPT_ADDRESS,
                  &received, overlapped)) {
        code = torb_ws.last_error();
      }
      break;
    }
    case TORB_IO_CONNECT: {
      LPFN_CONNECTEX connect = torb_ws_connect_function(handle, operation->socket->family);
      SOCKADDR_STORAGE local;
      SOCKADDR_STORAGE remote;
      torb_io_address any;
      int remote_length = torb_ws_address_of(&operation->address, &remote);
      int local_length;
      memset(&any, 0, sizeof any);
      any.family = operation->address.family;
      local_length = torb_ws_address_of(&any, &local);
      if (connect == NULL) {
        torb_io_complete(operation, torb_io_failed(TORB_IO_OTHER, 0u));
        return;
      }
      /* ConnectEx wants a bound socket: any address, any port */
      if (torb_ws.bind(handle, (const struct sockaddr *)&local, local_length) != 0) {
        torb_io_complete(operation, torb_ws_last_failure());
        return;
      }
      if (!connect(handle, (const struct sockaddr *)&remote, remote_length, NULL, 0u, NULL, overlapped)) {
        code = torb_ws.last_error();
      }
      break;
    }
    case TORB_IO_RESOLVE:
    default:
      torb_io_complete(operation, torb_io_failed(TORB_IO_INVALID, 0u));
      return;
  }
  /* Pending or done, the completion comes through the port; anything else means none will */
  if (code != 0 && code != WSA_IO_PENDING && code != ERROR_IO_PENDING) {
    torb_io_complete(operation, torb_ws_failed((uint32_t)code));
  }
}

void torb_io_system_cancel(torb_io_operation *operation) {
  torb_io_socket *socket = operation->socket;
  if (socket != NULL && socket->system != -1) {
    (void)CancelIoEx((HANDLE)(SOCKET)socket->system, (OVERLAPPED *)(void *)operation->system);
  }
}

int64_t torb_io_system_shutdown(torb_io_socket *socket) {
  if (torb_ws.shutdown((SOCKET)socket->system, SD_SEND) != 0) {
    return torb_ws_last_failure();
  }
  return 0;
}

void torb_io_system_close(torb_io_socket *socket) {
  (void)torb_ws.close((SOCKET)socket->system);
}

void torb_io_system_forget(torb_io_socket *socket) {
  /* No completion names a record, only an operation, and every operation holds its socket: it goes at once */
  torb_io_socket_free(socket);
}

int64_t torb_io_system_address(torb_io_socket *socket, bool peer, torb_io_address *out) {
  SOCKADDR_STORAGE system;
  int length = (int)sizeof system;
  int answer = peer ? torb_ws.peer_name((SOCKET)socket->system, (struct sockaddr *)&system, &length)
                    : torb_ws.local_name((SOCKET)socket->system, (struct sockaddr *)&system, &length);
  if (answer != 0) {
    return torb_ws_last_failure();
  }
  if (!torb_ws_address_from((const struct sockaddr *)&system, out)) {
    return torb_io_failed(TORB_IO_INVALID, 0u);
  }
  return 0;
}

int64_t torb_io_system_resolve(const char *host, torb_io_address **out) {
  ADDRINFOW hints;
  ADDRINFOW *found = NULL;
  ADDRINFOW *entry;
  wchar_t *wide;
  int length;
  int code;
  size_t count = 0u;
  size_t capacity = 0u;
  *out = NULL;
  length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, NULL, 0);
  if (length <= 0) {
    return torb_io_failed(TORB_IO_HOST_NOT_FOUND, 0u);
  }
  /* A plain `malloc`: this is a resolver thread, which has no heap */
  wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
  if (wide == NULL) {
    return torb_io_failed(TORB_IO_OTHER, 0u);
  }
  (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, wide, length);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  code = torb_ws.resolve(wide, NULL, &hints, &found);
  free(wide);
  if (code != 0) {
    return torb_ws_failed((uint32_t)code);
  }
  for (entry = found; entry != NULL; entry = entry->ai_next) {
    torb_io_address address;
    size_t index;
    bool seen = false;
    if (entry->ai_addr == NULL || !torb_ws_address_from(entry->ai_addr, &address)) {
      continue;
    }
    address.port = 0u;
    for (index = 0u; index < count; index += 1u) {
      if ((*out)[index].family == address.family && memcmp((*out)[index].bytes, address.bytes, 16u) == 0) {
        seen = true;
      }
    }
    if (seen) {
      continue;
    }
    if (count == capacity) {
      size_t grown = capacity == 0u ? 4u : capacity * 2u;
      torb_io_address *larger = (torb_io_address *)realloc(*out, grown * sizeof(torb_io_address));
      if (larger == NULL) {
        break;
      }
      *out = larger;
      capacity = grown;
    }
    (*out)[count] = address;
    count += 1u;
  }
  torb_ws.free_resolved(found);
  if (count == 0u) {
    return torb_io_failed(TORB_IO_HOST_NOT_FOUND, 0u);
  }
  return (int64_t)count;
}

#endif /* _WIN32 */
