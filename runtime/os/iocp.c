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
#include <iphlpapi.h>

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
typedef int(WSAAPI *torb_wsa_receive_from_function)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, struct sockaddr *, LPINT,
                                                    LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WSAAPI *torb_wsa_send_to_function)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const struct sockaddr *, int,
                                               LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int(WSAAPI *torb_wsa_connect_function)(SOCKET, const struct sockaddr *, int);
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
  torb_wsa_receive_from_function receive_from;
  torb_wsa_send_to_function send_to;
  torb_wsa_connect_function connect_datagram;
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
  torb_ws.receive_from = (torb_wsa_receive_from_function)torb_ws_symbol(library, "WSARecvFrom");
  torb_ws.send_to = (torb_wsa_send_to_function)torb_ws_symbol(library, "WSASendTo");
  torb_ws.connect_datagram = (torb_wsa_connect_function)torb_ws_symbol(library, "connect");
  torb_ws.last_error = (torb_wsa_last_error_function)torb_ws_symbol(library, "WSAGetLastError");
  torb_ws.shutdown = (torb_wsa_shutdown_function)torb_ws_symbol(library, "shutdown");
  torb_ws.resolve = (torb_wsa_resolve_function)torb_ws_symbol(library, "GetAddrInfoW");
  torb_ws.free_resolved = (torb_wsa_free_resolved_function)torb_ws_symbol(library, "FreeAddrInfoW");
  torb_ws.status_to_error = (torb_status_to_error_function)torb_ws_symbol(ntdll, "RtlNtStatusToDosError");
  if (torb_ws.startup == NULL || torb_ws.socket == NULL || torb_ws.bind == NULL || torb_ws.listen == NULL
      || torb_ws.close == NULL || torb_ws.set_option == NULL || torb_ws.local_name == NULL || torb_ws.peer_name == NULL
      || torb_ws.ioctl == NULL || torb_ws.receive == NULL || torb_ws.send == NULL || torb_ws.last_error == NULL
      || torb_ws.shutdown == NULL || torb_ws.resolve == NULL || torb_ws.free_resolved == NULL
      || torb_ws.status_to_error == NULL || torb_ws.receive_from == NULL || torb_ws.send_to == NULL
      || torb_ws.connect_datagram == NULL) {
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

static bool torb_ws_accept_lost_connection(uint32_t code);
static bool torb_iocp_accept_again(torb_io_operation *operation);

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
      case TORB_IO_RECEIVE_DATAGRAM:
        /* Where it came from, which the kernel wrote into the operation beside the bytes */
        if (!torb_ws_address_from((const struct sockaddr *)(const void *)operation->peer, &operation->address)) {
          memset(&operation->address, 0, sizeof operation->address);
        }
        result = (int64_t)bytes;
        break;
      case TORB_IO_RECEIVE:
      case TORB_IO_SEND:
      case TORB_IO_SEND_DATAGRAM:
      case TORB_IO_RESOLVE:
      default:
        result = (int64_t)bytes;
        break;
    }
  } else {
    uint32_t code = (uint32_t)torb_ws.status_to_error(status);
    /* A connection gone before its accept finished is the client's failure: the accept waits for the next one */
    if (operation->kind == (uint8_t)TORB_IO_ACCEPT && torb_ws_accept_lost_connection(code)
        && torb_iocp_accept_again(operation)) {
      return;
    }
    result = torb_ws_failed(code);
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

int64_t torb_io_system_socket(int32_t family, bool datagram) {
  SOCKET handle = torb_ws.socket(family == 6 ? AF_INET6 : AF_INET, datagram ? SOCK_DGRAM : SOCK_STREAM,
                                 datagram ? IPPROTO_UDP : IPPROTO_TCP, NULL, 0u,
                                 WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (handle == INVALID_SOCKET) {
    return torb_ws_last_failure();
  }
  if (datagram) {
    /*
     * An unconnected socket hears no "port unreachable" of a datagram it sent, as on every other system: without this,
     * Windows fails the next receive with WSAECONNRESET, whoever the unreachable peer was
     */
    BOOL report = FALSE;
    DWORD returned = 0u;
    (void)torb_ws.ioctl(handle, SIO_UDP_CONNRESET, &report, (DWORD)sizeof report, NULL, 0u, &returned, NULL, NULL);
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

int64_t torb_io_system_bind(torb_io_socket *socket, const torb_io_address *address) {
  SOCKET handle = (SOCKET)socket->system;
  SOCKADDR_STORAGE system;
  int length = torb_ws_address_of(address, &system);
  if (address->family == 6) {
    DWORD only = 1u;
    (void)torb_ws.set_option(handle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&only, (int)sizeof only);
  }
  if (torb_ws.bind(handle, (const struct sockaddr *)&system, length) != 0) {
    return torb_ws_last_failure();
  }
  return 0;
}

int64_t torb_io_system_connect_datagram(torb_io_socket *socket, const torb_io_address *address) {
  SOCKET handle = (SOCKET)socket->system;
  SOCKADDR_STORAGE system;
  int length = torb_ws_address_of(address, &system);
  BOOL report = TRUE;
  DWORD returned = 0u;
  if (torb_ws.connect_datagram(handle, (const struct sockaddr *)&system, length) != 0) {
    return torb_ws_last_failure();
  }
  /* A connected socket has one peer, and hears when its port is unreachable - as Linux reports it on a connected one */
  (void)torb_ws.ioctl(handle, SIO_UDP_CONNRESET, &report, (DWORD)sizeof report, NULL, 0u, &returned, NULL, NULL);
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

/* `CancelIoEx` of the one operation, where its socket is still open. */
static void torb_iocp_cancel_now(torb_io_operation *operation) {
  torb_io_socket *socket = operation->socket;
  if (socket != NULL && socket->system != -1) {
    (void)CancelIoEx((HANDLE)(SOCKET)socket->system, (OVERLAPPED *)(void *)operation->system);
  }
}

/*
 * Whether an accept failed for the one connection it was about to take and not for the listener: the client reset the
 * connection or gave up on it before the accept finished. `AcceptEx` says so at once with `WSAECONNRESET`, and through
 * the port with the status of a reset, which `RtlNtStatusToDosError` makes `ERROR_NETNAME_DELETED`; an aborted
 * connection is POSIX's `ECONNABORTED`. The `ERROR_OPERATION_ABORTED` of a cancellation is none of them.
 */
static bool torb_ws_accept_lost_connection(uint32_t code) {
  return code == ERROR_NETNAME_DELETED || code == WSAECONNRESET || code == WSAECONNABORTED
         || code == ERROR_CONNECTION_ABORTED;
}

/* Whether an accept whose connection was gone is made again: not on a listener that was closed, nor once cancelled. */
static bool torb_iocp_accepts_again(torb_io_operation *operation) {
  torb_io_socket *listener = operation->socket;
  bool closed;
  torb_spin_lock(&listener->lock);
  closed = listener->closed != 0u;
  torb_spin_unlock(&listener->lock);
  return !closed && torb_atomic_read_u32(&operation->cancelling) == 0u;
}

/* The socket and the address buffer of an accept that took no connection, given back before the next try. */
static void torb_iocp_forget_accepted(torb_io_operation *operation) {
  if (operation->accepted != NULL) {
    torb_io_socket_release(operation->accepted);
    operation->accepted = NULL;
  }
  free(operation->buffer);
  operation->buffer = NULL;
}

/*
 * `AcceptEx` on the listener, into a socket made for the connection. A connection that was gone before the call could
 * take it is the client's failure and not the listener's, so the call is made again, with a new socket, for the next
 * connection (`torb_ws_accept_lost_connection`). Answers 0 where the completion comes through the port, the error of a
 * call that failed outright, or -1 where the operation was completed here because no socket could be made.
 */
static int torb_iocp_accept(torb_io_operation *operation, OVERLAPPED *overlapped) {
  SOCKET handle = (SOCKET)operation->socket->system;
  LPFN_ACCEPTEX accept = torb_ws_accept_function(handle, operation->socket->family);
  for (;;) {
    int64_t accepted = torb_io_system_socket(operation->socket->family, false);
    DWORD received = 0u;
    int code;
    if (accepted < 0) {
      torb_io_complete(operation, accepted);
      return -1;
    }
    operation->accepted = torb_io_socket_new(TORB_IO_STREAM, operation->socket->family, accepted);
    operation->buffer = (uint8_t *)calloc(2u, TORB_WS_ACCEPT_ADDRESS);
    if (accept == NULL || operation->buffer == NULL) {
      torb_io_complete(operation, torb_io_failed(TORB_IO_OTHER, 0u));
      return -1;
    }
    memset(overlapped, 0, sizeof *overlapped);
    if (accept(handle, (SOCKET)accepted, operation->buffer, 0u, TORB_WS_ACCEPT_ADDRESS, TORB_WS_ACCEPT_ADDRESS,
               &received, overlapped)) {
      return 0;
    }
    code = torb_ws.last_error();
    if (!torb_ws_accept_lost_connection((uint32_t)code) || !torb_iocp_accepts_again(operation)) {
      return code;
    }
    torb_iocp_forget_accepted(operation);
  }
}

/*
 * The accept completed through the port with a connection that was gone: it is made again, for the next connection,
 * where the listener is open and nobody cancelled it. The new `AcceptEx` may complete the operation at once and let go
 * of it, so a count is held around it; and a cancel that came after the flag was read found nothing to cancel, so it
 * is repeated once the new call is made - the flag is read again in the order of the sequentially consistent
 * operations, after the call, and the cancel wrote it before its own `CancelIoEx`. Answers whether it was made again.
 */
static bool torb_iocp_accept_again(torb_io_operation *operation) {
  if (!torb_iocp_accepts_again(operation)) {
    return false;
  }
  torb_iocp_forget_accepted(operation);
  (void)torb_atomic_add_u32(&operation->count, 1u);
  torb_io_system_submit(operation);
  if (torb_atomic_read_u32(&operation->cancelling) != 0u) {
    torb_iocp_cancel_now(operation);
  }
  torb_io_operation_release(operation);
  return true;
}

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
    case TORB_IO_RECEIVE_DATAGRAM: {
      WSABUF buffer;
      DWORD flags = 0u;
      buffer.len = (ULONG)operation->capacity;
      buffer.buf = (CHAR *)operation->buffer;
      operation->peer_length = (int32_t)sizeof operation->peer;
      if (torb_ws.receive_from(handle, &buffer, 1u, NULL, &flags, (struct sockaddr *)(void *)operation->peer,
                               (LPINT)&operation->peer_length, overlapped, NULL)
          != 0) {
        code = torb_ws.last_error();
      }
      break;
    }
    case TORB_IO_SEND_DATAGRAM: {
      WSABUF buffer;
      buffer.len = (ULONG)operation->length;
      buffer.buf = (CHAR *)operation->buffer;
      if (operation->address.family == 0) {
        if (torb_ws.send(handle, &buffer, 1u, NULL, 0u, overlapped, NULL) != 0) {
          code = torb_ws.last_error();
        }
      } else {
        SOCKADDR_STORAGE remote;
        int remote_length = torb_ws_address_of(&operation->address, &remote);
        if (torb_ws.send_to(handle, &buffer, 1u, NULL, 0u, (const struct sockaddr *)&remote, remote_length, overlapped,
                            NULL)
            != 0) {
          code = torb_ws.last_error();
        }
      }
      break;
    }
    case TORB_IO_ACCEPT: {
      code = torb_iocp_accept(operation, overlapped);
      if (code < 0) {
        return;
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
  /* First the flag, then the cancel: an accept made again after the flag was read cancels itself (`torb_iocp_accept_again`) */
  (void)torb_atomic_exchange_u32(&operation->cancelling, 1u);
  torb_iocp_cancel_now(operation);
}

/* The pipes of a child are anonymous pipes without overlapped IO, which no completion port takes: runtime/stream.c reads
   them on the blocking pool. */
int64_t torb_io_system_pipe(int64_t descriptor) {
  (void)descriptor;
  return torb_io_failed(TORB_IO_INVALID, 0u);
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

/* ------------------------------------------------------------------------- the name servers, and randomness --- */

typedef ULONG(WINAPI *torb_adapters_function)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
typedef LONG(WINAPI *torb_random_function)(PVOID, PUCHAR, ULONG, ULONG);

/* `GetAdaptersAddresses` of iphlpapi and `BCryptGenRandom` of bcrypt, loaded on first use like Winsock. */
static SRWLOCK torb_helpers_lock = SRWLOCK_INIT;
static torb_adapters_function torb_adapters = NULL;
static torb_random_function torb_random = NULL;
static bool torb_adapters_tried = false;
static bool torb_random_tried = false;

/* The flag of `BCryptGenRandom` that takes the system's generator without an algorithm handle. */
#define TORB_BCRYPT_SYSTEM_PREFERRED 0x00000002u

/* Whether `address` is one of the three site-local placeholders Windows lists for an adapter without IPv6 servers. */
static bool torb_ws_placeholder_server(const torb_io_address *address) {
  static const uint8_t placeholder[15] = { 0xfe, 0xc0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0 };
  return address->family == 6 && memcmp(address->bytes, placeholder, 15u) == 0 && address->bytes[15] >= 1u
         && address->bytes[15] <= 3u;
}

int64_t torb_io_system_name_servers(torb_io_address *out, size_t capacity) {
  torb_adapters_function adapters;
  ULONG size = 16384u;
  IP_ADAPTER_ADDRESSES *list = NULL;
  IP_ADAPTER_ADDRESSES *adapter;
  ULONG answer = ERROR_BUFFER_OVERFLOW;
  size_t count = 0u;
  int attempts;
  AcquireSRWLockExclusive(&torb_helpers_lock);
  if (!torb_adapters_tried) {
    HMODULE library = LoadLibraryW(L"iphlpapi.dll");
    torb_adapters_tried = true;
    if (library != NULL) {
      torb_adapters = (torb_adapters_function)torb_ws_symbol(library, "GetAdaptersAddresses");
    }
  }
  adapters = torb_adapters;
  ReleaseSRWLockExclusive(&torb_helpers_lock);
  if (adapters == NULL) {
    return torb_io_failed(TORB_IO_OTHER, (uint32_t)GetLastError());
  }
  /* The list may grow between the call that measures it and the one that fills it: three tries */
  for (attempts = 0; attempts < 3 && answer == ERROR_BUFFER_OVERFLOW; attempts += 1) {
    free(list);
    list = (IP_ADAPTER_ADDRESSES *)malloc(size);
    if (list == NULL) {
      return torb_io_failed(TORB_IO_OTHER, 0u);
    }
    answer = adapters(AF_UNSPEC,
                      GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                          | GAA_FLAG_SKIP_FRIENDLY_NAME,
                      NULL, list, &size);
  }
  if (answer != ERROR_SUCCESS) {
    free(list);
    return answer == ERROR_NO_DATA ? 0 : torb_io_failed(TORB_IO_OTHER, (uint32_t)answer);
  }
  for (adapter = list; adapter != NULL; adapter = adapter->Next) {
    IP_ADAPTER_DNS_SERVER_ADDRESS *server;
    if (adapter->OperStatus != IfOperStatusUp) {
      continue;
    }
    for (server = adapter->FirstDnsServerAddress; server != NULL; server = server->Next) {
      torb_io_address address;
      size_t index;
      bool seen = false;
      if (server->Address.lpSockaddr == NULL || !torb_ws_address_from(server->Address.lpSockaddr, &address)
          || torb_ws_placeholder_server(&address)) {
        continue;
      }
      address.port = 53u;
      for (index = 0u; index < count; index += 1u) {
        if (out[index].family == address.family && memcmp(out[index].bytes, address.bytes, 16u) == 0) {
          seen = true;
        }
      }
      if (!seen && count < capacity) {
        out[count] = address;
        count += 1u;
      }
    }
  }
  free(list);
  return (int64_t)count;
}

bool torb_io_system_random(uint8_t *out, size_t size) {
  torb_random_function random;
  AcquireSRWLockExclusive(&torb_helpers_lock);
  if (!torb_random_tried) {
    HMODULE library = LoadLibraryW(L"bcrypt.dll");
    torb_random_tried = true;
    if (library != NULL) {
      torb_random = (torb_random_function)torb_ws_symbol(library, "BCryptGenRandom");
    }
  }
  random = torb_random;
  ReleaseSRWLockExclusive(&torb_helpers_lock);
  return random != NULL && random(NULL, (PUCHAR)out, (ULONG)size, TORB_BCRYPT_SYSTEM_PREFERRED) == 0;
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
