/*
 * windows.c - the natives of `std/os` that only Windows has (`native type Windows`, docs/design/OS.md section 7).
 *
 * The whole file is one `#if defined(_WIN32)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 *
 * Every text crosses as UTF-8 and every call of the system is the wide one, as in `platform.c`. A function that is not
 * in every Windows this runtime runs on (`IsWow64Process2`, `GetTempPath2W`), or that is in a library no build names
 * (`RtlGetVersion` in ntdll, `CoTaskMemFree` in ole32), is looked up at the call, so linking needs nothing beyond what
 * every Windows C compiler links by default.
 */

#include "torb.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#if defined(_MSC_VER)
#  pragma comment(lib, "advapi32.lib")
#  pragma comment(lib, "shell32.lib")
#endif

/** Replaces the text a `var` parameter holds with a copy of `value`. */
static void torb_os_windows_answer(torb_text *slot, const char *value) {
  torb_text_release(*slot);
  *slot = torb_text_from_cstring(value);
}

/** Replaces the text a `var` parameter holds with a wide string, converted; false where it has no UTF-8 spelling. */
static bool torb_os_windows_answer_wide(torb_text *slot, const wchar_t *value) {
  size_t length = 0u;
  char *text = torb_platform_utf8(value, &length);
  if (text == NULL) {
    return false;
  }
  torb_os_windows_answer(slot, text);
  torb_raw_free(text, length + 1u);
  return true;
}

/** A borrowed text as a NUL-terminated wide string. Owned, `torb_raw_free(result, *capacity)`; `NULL` if not UTF-8. */
static wchar_t *torb_os_windows_wide(torb_text text, size_t *capacity) {
  const size_t bytes = (size_t)text.length + 1u;
  char *narrow = (char *)torb_raw_allocate(bytes);
  wchar_t *wide;
  if (text.length > 0u) {
    memcpy(narrow, text.storage->data + text.offset, (size_t)text.length);
  }
  narrow[text.length] = '\0';
  wide = torb_platform_wide(narrow, capacity);
  torb_raw_free(narrow, bytes);
  return wide;
}

/** The outcome of a Windows error code, with the system's message for it in `*failure`. */
static int64_t torb_os_windows_failure(DWORD code, torb_text *failure) {
  wchar_t *message = NULL;
  const DWORD written = FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    code,
    0,
    (LPWSTR)(void *)&message,
    0,
    NULL
  );
  bool answered = false;
  if (written > 0u && message != NULL) {
    DWORD end = written;
    /* The message ends in a full stop and a line break, which a sentence built around it does not want */
    while (end > 0u && (message[end - 1u] == L'\r' || message[end - 1u] == L'\n' || message[end - 1u] == L'.')) {
      end -= 1u;
    }
    message[end] = L'\0';
    answered = torb_os_windows_answer_wide(failure, message);
  }
  if (message != NULL) {
    LocalFree(message);
  }
  if (!answered) {
    char fallback[48];
    snprintf(fallback, sizeof fallback, "Windows error %lu", (unsigned long)code);
    torb_os_windows_answer(failure, fallback);
  }
  switch (code) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
      return TORB_OS_DENIED;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return TORB_OS_MISSING;
    case ERROR_CALL_NOT_IMPLEMENTED:
    case ERROR_NOT_SUPPORTED:
      return TORB_OS_UNSUPPORTED;
    default:
      return TORB_OS_FAILED;
  }
}

/** A function of a system library looked up by name, as the generic function pointer a cast starts from. */
static void (*torb_os_windows_lookup(const wchar_t *library, const char *name))(void) {
  HMODULE module = GetModuleHandleW(library);
  if (module == NULL) {
    module = LoadLibraryW(library);
  }
  if (module == NULL) {
    return NULL;
  }
  return (void (*)(void))GetProcAddress(module, name);
}

int64_t torb_os_windows_tick_count(void) {
  return (int64_t)GetTickCount64();
}

typedef LONG(WINAPI *torb_os_windows_rtl_get_version)(OSVERSIONINFOW *information);

/** `RtlGetVersion`, because `GetVersionExW` answers the version the program's manifest claims to know. */
int64_t torb_os_windows_version(int64_t *major, int64_t *minor, int64_t *build, torb_text *failure) {
  const torb_os_windows_rtl_get_version get =
    (torb_os_windows_rtl_get_version)torb_os_windows_lookup(L"ntdll.dll", "RtlGetVersion");
  OSVERSIONINFOW information;
  if (get == NULL) {
    torb_os_windows_answer(failure, "ntdll.dll has no RtlGetVersion");
    return TORB_OS_UNSUPPORTED;
  }
  memset(&information, 0, sizeof information);
  information.dwOSVersionInfoSize = sizeof information;
  if (get(&information) != 0) {
    torb_os_windows_answer(failure, "RtlGetVersion failed");
    return TORB_OS_FAILED;
  }
  *major = (int64_t)information.dwMajorVersion;
  *minor = (int64_t)information.dwMinorVersion;
  *build = (int64_t)information.dwBuildNumber;
  return TORB_OS_SUCCESS;
}

int64_t torb_os_windows_registry_text(torb_text key, torb_text value, torb_text *text, torb_text *failure) {
  size_t keyCapacity = 0u;
  size_t valueCapacity = 0u;
  wchar_t *wideKey = torb_os_windows_wide(key, &keyCapacity);
  wchar_t *wideValue = torb_os_windows_wide(value, &valueCapacity);
  const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
  DWORD bytes = 0u;
  LSTATUS status;
  int64_t result = TORB_OS_SUCCESS;
  if (wideKey == NULL || wideValue == NULL) {
    torb_os_windows_answer(failure, "the name is not valid UTF-8");
    result = TORB_OS_FAILED;
  } else {
    status = RegGetValueW(HKEY_LOCAL_MACHINE, wideKey, wideValue, flags, NULL, NULL, &bytes);
    if (status == ERROR_SUCCESS) {
      wchar_t *buffer = (wchar_t *)torb_raw_allocate((size_t)bytes + sizeof(wchar_t));
      status = RegGetValueW(HKEY_LOCAL_MACHINE, wideKey, wideValue, flags, NULL, buffer, &bytes);
      if (status == ERROR_SUCCESS) {
        buffer[bytes / sizeof(wchar_t)] = L'\0';
        if (!torb_os_windows_answer_wide(text, buffer)) {
          torb_os_windows_answer(failure, "the value has no UTF-8 spelling");
          result = TORB_OS_FAILED;
        }
      }
      torb_raw_free(buffer, (size_t)bytes + sizeof(wchar_t));
    }
    if (status != ERROR_SUCCESS) {
      result = torb_os_windows_failure((DWORD)status, failure);
    }
  }
  if (wideKey != NULL) {
    torb_raw_free(wideKey, keyCapacity);
  }
  if (wideValue != NULL) {
    torb_raw_free(wideValue, valueCapacity);
  }
  return result;
}

int64_t torb_os_windows_registry_integer(torb_text key, torb_text value, int64_t *number, torb_text *failure) {
  size_t keyCapacity = 0u;
  size_t valueCapacity = 0u;
  wchar_t *wideKey = torb_os_windows_wide(key, &keyCapacity);
  wchar_t *wideValue = torb_os_windows_wide(value, &valueCapacity);
  ULONGLONG read = 0u;
  DWORD bytes = sizeof read;
  DWORD kind = 0u;
  int64_t result = TORB_OS_SUCCESS;
  if (wideKey == NULL || wideValue == NULL) {
    torb_os_windows_answer(failure, "the name is not valid UTF-8");
    result = TORB_OS_FAILED;
  } else {
    const LSTATUS status = RegGetValueW(
      HKEY_LOCAL_MACHINE,
      wideKey,
      wideValue,
      RRF_RT_REG_DWORD | RRF_RT_REG_QWORD,
      &kind,
      &read,
      &bytes
    );
    if (status != ERROR_SUCCESS) {
      result = torb_os_windows_failure((DWORD)status, failure);
    } else if (kind == REG_DWORD) {
      DWORD narrow;
      memcpy(&narrow, &read, sizeof narrow);
      *number = (int64_t)narrow;
    } else {
      *number = (int64_t)read;
    }
  }
  if (wideKey != NULL) {
    torb_raw_free(wideKey, keyCapacity);
  }
  if (wideValue != NULL) {
    torb_raw_free(wideValue, valueCapacity);
  }
  return result;
}

int64_t torb_os_windows_computer_name(torb_text *name, torb_text *failure) {
  DWORD count = 0u;
  wchar_t *buffer;
  size_t bytes;
  int64_t result = TORB_OS_SUCCESS;
  /* The first call answers the size, including the NUL, and fails with ERROR_MORE_DATA */
  (void)GetComputerNameExW(ComputerNameDnsHostname, NULL, &count);
  if (count == 0u) {
    return torb_os_windows_failure(GetLastError(), failure);
  }
  bytes = ((size_t)count + 1u) * sizeof(wchar_t);
  buffer = (wchar_t *)torb_raw_allocate(bytes);
  if (!GetComputerNameExW(ComputerNameDnsHostname, buffer, &count)) {
    result = torb_os_windows_failure(GetLastError(), failure);
  } else if (!torb_os_windows_answer_wide(name, buffer)) {
    torb_os_windows_answer(failure, "the host name has no UTF-8 spelling");
    result = TORB_OS_FAILED;
  }
  torb_raw_free(buffer, bytes);
  return result;
}

typedef BOOL(WINAPI *torb_os_windows_is_wow64_process2)(HANDLE process, USHORT *processMachine, USHORT *nativeMachine);

/** `IMAGE_FILE_MACHINE_*` as the architecture numbers of `torb_os.h`. */
static int64_t torb_os_windows_machine(USHORT machine) {
  switch (machine) {
    case 0x8664u:
      return 1;
    case 0xAA64u:
      return 2;
    case 0x014Cu:
      return 3;
    case 0x01C4u:
      return 4;
    default:
      return 0;
  }
}

/** `PROCESSOR_ARCHITECTURE_*` as the architecture numbers of `torb_os.h`. */
static int64_t torb_os_windows_processor(WORD architecture) {
  switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return 1;
    case 12: /* PROCESSOR_ARCHITECTURE_ARM64, which older headers do not name */
      return 2;
    case PROCESSOR_ARCHITECTURE_INTEL:
      return 3;
    case PROCESSOR_ARCHITECTURE_ARM:
      return 4;
    default:
      return 0;
  }
}

/**
 * `GetNativeSystemInfo` answers what the program sees, and an x86-64 program emulated on Arm64 sees x86-64. So the
 * machine comes from `IsWow64Process2` where it exists (from Windows 10 1709), which names the machine under it.
 */
int64_t torb_os_windows_system_information(
  int64_t *page_size,
  int64_t *architecture,
  int64_t *logical,
  torb_text *failure
) {
  SYSTEM_INFO information;
  const torb_os_windows_is_wow64_process2 wow =
    (torb_os_windows_is_wow64_process2)torb_os_windows_lookup(L"kernel32.dll", "IsWow64Process2");
  USHORT processMachine = 0u;
  USHORT nativeMachine = 0u;
  (void)failure;
  GetNativeSystemInfo(&information);
  *page_size = (int64_t)information.dwPageSize;
  *logical = (int64_t)information.dwNumberOfProcessors;
  *architecture = torb_os_windows_processor(information.wProcessorArchitecture);
  if (wow != NULL && wow(GetCurrentProcess(), &processMachine, &nativeMachine)) {
    const int64_t native = torb_os_windows_machine(nativeMachine);
    if (native != 0) {
      *architecture = native;
    }
  }
  return TORB_OS_SUCCESS;
}

/** Two hexadecimal digits' worth of a GUID's text, or -1 at a character that is not one. */
static int torb_os_windows_hex(char digit) {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }
  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }
  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }
  return -1;
}

/**
 * `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` as a GUID, without `CLSIDFromString` and the COM library it is in. The first
 * three groups are numbers and the last two are bytes in the order they are written, as the text form defines.
 */
static bool torb_os_windows_guid(torb_text text, GUID *guid) {
  static const int groups[] = { 8, 4, 4, 4, 12 };
  const char *data = (const char *)text.storage->data + text.offset;
  uint8_t bytes[16];
  size_t at = 1u;
  size_t written = 0u;
  size_t group;
  if (text.length != 38u || data[0] != '{' || data[37] != '}') {
    return false;
  }
  for (group = 0u; group < 5u; group += 1u) {
    int digit;
    if (group > 0u) {
      if (data[at] != '-') {
        return false;
      }
      at += 1u;
    }
    for (digit = 0; digit < groups[group]; digit += 2) {
      const int high = torb_os_windows_hex(data[at]);
      const int low = torb_os_windows_hex(data[at + 1u]);
      if (high < 0 || low < 0) {
        return false;
      }
      bytes[written] = (uint8_t)(high * 16 + low);
      written += 1u;
      at += 2u;
    }
  }
  guid->Data1 = ((unsigned long)bytes[0] << 24) | ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[2] << 8)
                | (unsigned long)bytes[3];
  guid->Data2 = (unsigned short)((bytes[4] << 8) | bytes[5]);
  guid->Data3 = (unsigned short)((bytes[6] << 8) | bytes[7]);
  memcpy(guid->Data4, bytes + 8, 8u);
  return true;
}

typedef void(WINAPI *torb_os_windows_co_task_mem_free)(void *memory);

int64_t torb_os_windows_known_folder(torb_text identifier, torb_text *path, torb_text *failure) {
  GUID folder;
  wchar_t *found = NULL;
  HRESULT status;
  int64_t result = TORB_OS_SUCCESS;
  const torb_os_windows_co_task_mem_free release =
    (torb_os_windows_co_task_mem_free)torb_os_windows_lookup(L"ole32.dll", "CoTaskMemFree");
  if (!torb_os_windows_guid(identifier, &folder)) {
    torb_os_windows_answer(failure, "the folder is not named by a GUID in braces");
    return TORB_OS_FAILED;
  }
  status = SHGetKnownFolderPath(&folder, KF_FLAG_DONT_VERIFY, NULL, &found);
  if (SUCCEEDED(status) && found != NULL) {
    if (!torb_os_windows_answer_wide(path, found)) {
      torb_os_windows_answer(failure, "the path has no UTF-8 spelling");
      result = TORB_OS_FAILED;
    }
  } else if (status == E_INVALIDARG || status == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
    torb_os_windows_answer(failure, "this machine has no such folder");
    result = TORB_OS_MISSING;
  } else {
    result = torb_os_windows_failure((DWORD)(status & 0xFFFF), failure);
  }
  /* The buffer is the caller's to free even where the call failed */
  if (found != NULL && release != NULL) {
    release(found);
  }
  return result;
}

typedef DWORD(WINAPI *torb_os_windows_get_temp_path)(DWORD length, wchar_t *buffer);

int64_t torb_os_windows_temporary_directory(torb_text *path, torb_text *failure) {
  torb_os_windows_get_temp_path get =
    (torb_os_windows_get_temp_path)torb_os_windows_lookup(L"kernel32.dll", "GetTempPath2W");
  wchar_t buffer[MAX_PATH + 2];
  DWORD written;
  if (get == NULL) {
    get = GetTempPathW;
  }
  written = get(MAX_PATH + 1, buffer);
  if (written == 0u || written > MAX_PATH + 1) {
    return torb_os_windows_failure(GetLastError(), failure);
  }
  buffer[written] = L'\0';
  if (!torb_os_windows_answer_wide(path, buffer)) {
    torb_os_windows_answer(failure, "the path has no UTF-8 spelling");
    return TORB_OS_FAILED;
  }
  return TORB_OS_SUCCESS;
}

#endif /* _WIN32 */
