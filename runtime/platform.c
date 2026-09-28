/*
 * platform.c - the only file in the runtime with an `#ifdef _WIN32`.
 *
 * What kind of thing a path is, the working directory, the entries of a directory, creating one directory and
 * everything above it, opening a file, removing, renaming and copying one, what a path is (its metadata) and its
 * permissions, symbolic links, the directory for temporary files and an entry created where nothing was, reading and
 * writing a whole file and flushing one to the disk, running a child process to its end, a monotonic clock reading,
 * sleeping until a timer is due, the program's own arguments, reading and setting an environment variable, and the
 * memory limit the operating system holds the process to (with the physical memory and the size of a block that go with
 * it). Everything above this file is portable, and what it hands out and takes in is always UTF-8.
 *
 * On Windows that last sentence is the whole point of the file. A `String` of the language is UTF-8, every call of the
 * operating system comes in a narrow and a wide form, and the narrow one reads the **code page of the machine** (1252 on
 * a German one, 932 on a Japanese one) - so `grüße.txt` handed to `fopen` creates a file that is called `grÃ¼ÃŸe.txt`,
 * and a path over `MAX_PATH` cannot be opened at all whatever it is called.
 *
 * The first of those is worth being precise about, because it is the one that looks harmless: where the code page has a
 * character for every byte, the mangling is a **bijection**, so a program that creates its own files and reads them back
 * never notices - and neither does a child process it starts, because the command line was mangled the same way. It is
 * still wrong for everybody else, which is everybody: the name on the disk is not the name the program meant, so a file
 * that `git checkout` wrote cannot be opened, and what the file manager shows is mojibake. On a code page where the
 * mapping loses (932 has no character for most byte pairs) it fails outright.
 *
 * The whole Windows half therefore goes through the wide API, with one pair of helpers converting at the boundary
 * (`torb_platform_wide`, `torb_platform_utf8`) and one function deciding what form a path is handed over in
 * (`torb_platform_system_path`). The POSIX half needs none of it: a path is bytes there and a UTF-8 `String` is bytes.
 */

/* The POSIX half calls POSIX 2008 (`clock_gettime`, `nanosleep`, `setenv`), which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
/* On macOS `_POSIX_C_SOURCE` alone hides every Darwin extension (`_SC_NPROCESSORS_ONLN`, `SO_NOSIGPIPE`,
 * `pthread_cond_timedwait_relative_np`); `_DARWIN_C_SOURCE` shows them again beside POSIX. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE
#endif

#include "torb.h"
#include "torb_pool.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
/* `CommandLineToArgvW`, which `WIN32_LEAN_AND_MEAN` leaves out of `windows.h`. It lives in shell32, which every
   Windows C compiler links by default (the MSVC family through the directive below, the GCC family through its own
   default libraries) - so no build has to name a library for it. */
#  include <shellapi.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "shell32.lib")
#  endif
#  include <winioctl.h> /* FSCTL_GET_REPARSE_POINT */
#  include <direct.h>
#  include <fcntl.h>
#  include <io.h>
#  include <malloc.h> /* _msize */
#  include <sys/stat.h>
#  include <wchar.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
/*
 * The memory limit (`torb_platform_limit_memory`): which resource holds it, and how large the C allocator made a block.
 *
 * Linux counts writable private mappings against `RLIMIT_DATA` since 4.7 - the heap, `mmap`ed blocks, thread stacks -
 * and not the address space glibc only reserves (64 MiB per malloc arena, made writable as it is used), which
 * `RLIMIT_AS` would count and which grows with the number of threads, not with what a program holds. FreeBSD counts
 * only `brk` against `RLIMIT_DATA` and its allocator maps, so there it is `RLIMIT_AS`. macOS enforces neither in a way
 * that fits: its address space already holds the shared cache of several GiB before `main` runs, so the runtime counts
 * its own allocations there.
 */
#  if defined(__linux__)
#    include <malloc.h>
#    define TORB_MEMORY_RESOURCE RLIMIT_DATA
#    define TORB_ALLOCATION_SIZE(block) malloc_usable_size(block)
#  elif defined(__FreeBSD__)
#    include <malloc_np.h>
#    define TORB_MEMORY_RESOURCE RLIMIT_AS
#    define TORB_ALLOCATION_SIZE(block) malloc_usable_size(block)
#  elif defined(__APPLE__)
#    include <malloc/malloc.h>
#    define TORB_ALLOCATION_SIZE(block) malloc_size(block)
#  else
#    define TORB_ALLOCATION_SIZE(block) ((void)(block), (size_t)0u)
#  endif
/*
 * A binary built under AddressSanitizer or ThreadSanitizer (`TORB_CFLAGS`, docs/tooling/torb-build.md) has terabytes of
 * shadow memory mapped before `main` runs, and every mapping its allocator makes afterwards would fail against a limit
 * of the system: there the runtime counts its own allocations, as it does on macOS.
 */
#  if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#    undef TORB_MEMORY_RESOURCE
#  elif defined(__has_feature)
#    if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#      undef TORB_MEMORY_RESOURCE
#    endif
#  endif
#endif

/* =============================================================== the boundary to Windows: UTF-8 and UTF-16 ====== */

#if defined(_WIN32)

/**
 * A path is handed over in its **extended-length form** (`\\?\C:\...`) from this length on, and in the plain form below
 * it. `MAX_PATH - 12` is the length a directory may have for the operating system to still create files inside it, so
 * it is the one threshold that works for a path that is opened and for a path that is created in.
 */
#define TORB_PLAIN_PATH_LIMIT (MAX_PATH - 12)

/**
 * UTF-8 to UTF-16, NUL terminated. Result owned, freed with `torb_raw_free(result, *capacity)` - `*capacity` is the
 * size in bytes, not the number of characters. `NULL` where the text is not valid UTF-8, which for a path is the same
 * answer as a name that is not there (`MB_ERR_INVALID_CHARS` is what makes the call say so instead of inventing a
 * replacement character).
 */
wchar_t *torb_platform_wide(const char *text, size_t *capacity) {
  const int length = (int)strlen(text);
  int count = 0;
  wchar_t *wide;
  if (length > 0) {
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, NULL, 0);
    if (count <= 0) {
      *capacity = 0u;
      return NULL;
    }
  }
  *capacity = ((size_t)count + 1u) * sizeof(wchar_t);
  wide = (wchar_t *)torb_raw_allocate(*capacity);
  if (count > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, wide, count) != count) {
    torb_raw_free(wide, *capacity);
    *capacity = 0u;
    return NULL;
  }
  wide[count] = L'\0';
  return wide;
}

/**
 * UTF-16 back to UTF-8, NUL terminated. Result owned, freed with `torb_raw_free(result, *length + 1)`; `*length` is the
 * byte length without the NUL. `NULL` where the UTF-16 is not well formed - a name made of an unpaired surrogate has no
 * UTF-8 spelling, and a `String` is always valid UTF-8, so there is no value for such a name (`docs/design/PATH.md`).
 */
char *torb_platform_utf8(const wchar_t *wide, size_t *length) {
  const int count = (int)wcslen(wide);
  int bytes = 0;
  char *text;
  if (count > 0) {
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, count, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
      *length = 0u;
      return NULL;
    }
  }
  text = (char *)torb_raw_allocate((size_t)bytes + 1u);
  if (count > 0
      && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, count, text, bytes, NULL, NULL) != bytes) {
    torb_raw_free(text, (size_t)bytes + 1u);
    *length = 0u;
    return NULL;
  }
  text[bytes] = '\0';
  *length = (size_t)bytes;
  return text;
}

/**
 * The form of a path an operating system call gets: every `/` becomes `\`, and a path whose fully qualified form is
 * longer than `MAX_PATH - 13` characters is handed over as `\\?\C:\...` (or `\\?\UNC\server\share\...`), which is the
 * only form over that length any call accepts.
 *
 * **Only above the limit, not always.** `\\?\` does not merely lift a limit, it switches the whole path off the
 * operating system's own parsing: nothing is normalised any more, `..` and a trailing dot or space are literal name
 * parts, `NUL` and `CON` stop naming devices, and a relative path is impossible. Adding it everywhere would mean every
 * path a program opens is one *this file* resolved rather than the one the caller wrote - a relative path is resolved
 * against a current directory the operating system keeps **per drive**, and `C:file` names a file in C:'s own. So the
 * plain form stays the normal case, where the behaviour is the operating system's to the letter, and the extended form
 * appears where the plain one cannot work at all.
 *
 * The extended form exists only inside this function and never reaches a value of the program: what a program sees is
 * the `/` form of `File.absolutePath` (`docs/design/PATH.md`, section 6).
 *
 * Result owned like `torb_platform_wide`'s, and `NULL` for the same reason.
 */
wchar_t *torb_platform_system_path(const char *path, size_t *capacity) {
  size_t plain_capacity = 0u;
  wchar_t *plain = torb_platform_wide(path, &plain_capacity);
  size_t index;
  DWORD needed;
  DWORD written;
  size_t full_capacity;
  wchar_t *full;
  const wchar_t *prefix;
  size_t prefix_length;
  size_t replaced;
  wchar_t *result;

  if (plain == NULL) {
    *capacity = 0u;
    return NULL;
  }
  for (index = 0u; plain[index] != L'\0'; index += 1u) {
    if (plain[index] == L'/') {
      plain[index] = L'\\';
    }
  }
  /* A path that is already extended was normalised by whoever wrote it, and normalising it again would be wrong */
  if (wcsncmp(plain, L"\\\\?\\", 4u) == 0) {
    *capacity = plain_capacity;
    return plain;
  }
  /* The length of the fully qualified form, with the NUL - text arithmetic, so it touches no disk and no network */
  needed = GetFullPathNameW(plain, 0u, NULL, NULL);
  if (needed == 0u || needed <= (DWORD)TORB_PLAIN_PATH_LIMIT) {
    *capacity = plain_capacity;
    return plain;
  }
  full_capacity = (size_t)needed * sizeof(wchar_t);
  full = (wchar_t *)torb_raw_allocate(full_capacity);
  written = GetFullPathNameW(plain, needed, full, NULL);
  if (written == 0u || written >= needed) {
    torb_raw_free(full, full_capacity);
    *capacity = plain_capacity;
    return plain;
  }
  /* `\\server\share` becomes `\\?\UNC\server\share`: the prefix takes the place of the two leading separators */
  replaced = full[0] == L'\\' && full[1] == L'\\' ? 2u : 0u;
  prefix = replaced == 2u ? L"\\\\?\\UNC\\" : L"\\\\?\\";
  prefix_length = wcslen(prefix);
  *capacity = (prefix_length + (size_t)written - replaced + 1u) * sizeof(wchar_t);
  result = (wchar_t *)torb_raw_allocate(*capacity);
  memcpy(result, prefix, prefix_length * sizeof(wchar_t));
  memcpy(result + prefix_length, full + replaced, ((size_t)written - replaced + 1u) * sizeof(wchar_t));
  torb_raw_free(full, full_capacity);
  torb_raw_free(plain, plain_capacity);
  return result;
}

/**
 * What an `IoError` says about a call of the Windows API, in the words `strerror` uses for the same thing - so the
 * message of a failure does not depend on which call inside this file produced it. Borrowed, static.
 */
static const char *torb_windows_message(DWORD code) {
  switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:
      return "No such file or directory";
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
      return "Permission denied";
    case ERROR_DIRECTORY:
      return "Not a directory";
    case ERROR_FILENAME_EXCED_RANGE:
      return "File name too long";
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      return "Not enough space";
    case ERROR_DISK_FULL:
      return "No space left on device";
    case ERROR_DIR_NOT_EMPTY:
      return "Directory not empty";
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
      return "File exists";
    case ERROR_NOT_SAME_DEVICE:
      return "Invalid cross-device link";
    case ERROR_PRIVILEGE_NOT_HELD:
      return "Operation not permitted";
    case ERROR_NOT_A_REPARSE_POINT:
      return "Invalid argument";
    default:
      return "the operating system refused the operation";
  }
}

#endif

/* ============================================================================================ Windows ========== */

#if defined(_WIN32)

torb_path_kind torb_platform_path_kind(const char *path) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  DWORD attributes;
  if (wide == NULL) {
    return TORB_PATH_MISSING;
  }
  attributes = GetFileAttributesW(wide);
  torb_raw_free(wide, capacity);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return TORB_PATH_MISSING;
  }
  return (attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u ? TORB_PATH_DIRECTORY : TORB_PATH_FILE;
}

/* A junction and a mount point are reparse points as much as a symbolic link is, and each leads out of a directory */
bool torb_platform_is_link(const char *path) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  DWORD attributes;
  if (wide == NULL) {
    return false;
  }
  attributes = GetFileAttributesW(wide);
  torb_raw_free(wide, capacity);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return (attributes & (DWORD)FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
}

char *torb_platform_working_directory(size_t *length) {
  /* With the NUL, which is what the second call wants and what the first one answers */
  const DWORD needed = GetCurrentDirectoryW(0u, NULL);
  size_t capacity;
  wchar_t *wide;
  char *text;
  if (needed == 0u) {
    return NULL;
  }
  capacity = (size_t)needed * sizeof(wchar_t);
  wide = (wchar_t *)torb_raw_allocate(capacity);
  if (GetCurrentDirectoryW(needed, wide) == 0u) {
    torb_raw_free(wide, capacity);
    return NULL;
  }
  text = torb_platform_utf8(wide, length);
  torb_raw_free(wide, capacity);
  return text;
}

bool torb_platform_list_directory(const char *path, torb_list *out, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  size_t length;
  size_t pattern_capacity;
  wchar_t *pattern;
  WIN32_FIND_DATAW entry;
  HANDLE handle;
  bool ok = true;
  if (wide == NULL) {
    *message = "No such file or directory";
    return false;
  }
  /* `<path>\*`, and no second separator where the path is a root and already ends in one - in the extended form
     nothing collapses a doubled separator any more */
  length = wcslen(wide);
  pattern_capacity = (length + 3u) * sizeof(wchar_t);
  pattern = (wchar_t *)torb_raw_allocate(pattern_capacity);
  memcpy(pattern, wide, length * sizeof(wchar_t));
  torb_raw_free(wide, capacity);
  if (length > 0u && pattern[length - 1u] != L'\\') {
    pattern[length] = L'\\';
    length += 1u;
  }
  pattern[length] = L'*';
  pattern[length + 1u] = L'\0';
  handle = FindFirstFileW(pattern, &entry);
  torb_raw_free(pattern, pattern_capacity);
  if (handle == INVALID_HANDLE_VALUE) {
    *message = torb_windows_message(GetLastError());
    return false;
  }
  do {
    size_t name_length = 0u;
    char *name;
    if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) {
      continue;
    }
    name = torb_platform_utf8(entry.cFileName, &name_length);
    if (name == NULL) {
      *message = "a name in this directory is not valid Unicode";
      ok = false;
      break;
    }
    {
      torb_text text = torb_text_from_cstring(name);
      torb_raw_free(name, name_length + 1u);
      torb_list_add(out, &text);
    }
  } while (FindNextFileW(handle, &entry) != 0);
  FindClose(handle);
  return ok;
}

/** One directory, where everything above it is already there. False with a message, also where it is already there. */
static bool torb_make_one_directory(const char *path, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  bool made;
  if (wide == NULL) {
    *message = "No such file or directory";
    return false;
  }
  made = _wmkdir(wide) == 0;
  if (!made) {
    *message = strerror(errno);
  }
  torb_raw_free(wide, capacity);
  return made;
}

void *torb_platform_open_file(const char *path, bool writing, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  FILE *file;
  if (wide == NULL) {
    *message = "No such file or directory";
    return NULL;
  }
  file = _wfopen(wide, writing ? L"wb" : L"rb");
  if (file == NULL) {
    *message = strerror(errno);
  }
  torb_raw_free(wide, capacity);
  return file;
}

bool torb_platform_remove_entry(const char *path, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  DWORD attributes;
  bool directory;
  bool removed;
  if (wide == NULL) {
    *message = "No such file or directory";
    return false;
  }
  attributes = GetFileAttributesW(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    *message = torb_windows_message(GetLastError());
    torb_raw_free(wide, capacity);
    return false;
  }
  /* Two calls, because a directory is not a file to this platform: `DeleteFileW` refuses one. A link to a directory (and
     a junction) carries the directory attribute too, and `RemoveDirectoryW` removes the link and not its target */
  directory = (attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u;
  removed = directory ? RemoveDirectoryW(wide) != 0 : DeleteFileW(wide) != 0;
  if (!removed && GetLastError() == ERROR_ACCESS_DENIED && (attributes & (DWORD)FILE_ATTRIBUTE_READONLY) != 0u) {
    /* A read-only file is removed on POSIX wherever its directory may be written, so it is here too */
    if (SetFileAttributesW(wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY) != 0) {
      removed = directory ? RemoveDirectoryW(wide) != 0 : DeleteFileW(wide) != 0;
      if (!removed) {
        DWORD code = GetLastError();
        (void)SetFileAttributesW(wide, attributes);
        SetLastError(code);
      }
    }
  }
  if (!removed) {
    *message = torb_windows_message(GetLastError());
  }
  torb_raw_free(wide, capacity);
  return removed;
}

bool torb_platform_rename(const char *from, const char *to, const char **message) {
  size_t from_capacity = 0u;
  size_t to_capacity = 0u;
  wchar_t *wide_from = torb_platform_system_path(from, &from_capacity);
  wchar_t *wide_to = torb_platform_system_path(to, &to_capacity);
  bool moved = false;
  if (wide_from == NULL || wide_to == NULL) {
    *message = "No such file or directory";
  } else if (MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
    moved = true;
  } else {
    DWORD code = GetLastError();
    const DWORD attributes = GetFileAttributesW(wide_to);
    /* A read-only file is replaced on POSIX wherever its directory may be written, so it is here too: the attribute is
       cleared for the move, and put back where the move fails after all */
    if (code == ERROR_ACCESS_DENIED && attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & (DWORD)FILE_ATTRIBUTE_READONLY) != 0u && (attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) == 0u
        && SetFileAttributesW(wide_to, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY) != 0) {
      if (MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        moved = true;
      } else {
        code = GetLastError();
        (void)SetFileAttributesW(wide_to, attributes);
      }
    }
    if (!moved) {
      *message = torb_windows_message(code);
    }
  }
  torb_raw_free(wide_from, from_capacity);
  torb_raw_free(wide_to, to_capacity);
  return moved;
}

bool torb_platform_copy_file(const char *from, const char *to, const char **message) {
  size_t from_capacity = 0u;
  size_t to_capacity = 0u;
  wchar_t *wide_from = torb_platform_system_path(from, &from_capacity);
  wchar_t *wide_to = torb_platform_system_path(to, &to_capacity);
  bool copied = false;
  if (wide_from == NULL || wide_to == NULL) {
    *message = "No such file or directory";
  } else if (CopyFileW(wide_from, wide_to, FALSE) != 0) {
    /* `CopyFileW` carries the attributes over, the read-only one among them: the permissions, as on POSIX */
    copied = true;
  } else {
    *message = torb_windows_message(GetLastError());
  }
  torb_raw_free(wide_from, from_capacity);
  torb_raw_free(wide_to, to_capacity);
  return copied;
}

/* The `FILETIME` of Windows - hundreds of nanoseconds since 1601 - as nanoseconds since 1970. */
static int64_t torb_windows_unix_nanoseconds(FILETIME time) {
  const int64_t ticks = (int64_t)(((uint64_t)time.dwHighDateTime << 32) | (uint64_t)time.dwLowDateTime);
  return (ticks - INT64_C(116444736000000000)) * 100;
}

/*
 * The permission bits the C library of Windows makes up for `_wstat`: everybody may read, everybody may write unless the
 * file is read-only, and a directory or a program (`.exe`, `.com`, `.bat`, `.cmd`) may be executed.
 */
static int64_t torb_windows_mode(const wchar_t *path, DWORD attributes) {
  int64_t mode = 0444;
  if ((attributes & (DWORD)FILE_ATTRIBUTE_READONLY) == 0u) {
    mode |= 0222;
  }
  if ((attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u) {
    mode |= 0111;
  } else {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (dot != NULL && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".com") == 0 || _wcsicmp(dot, L".bat") == 0
                        || _wcsicmp(dot, L".cmd") == 0)) {
      mode |= 0111;
    }
  }
  return mode;
}

/*
 * Whether a reparse point is a link - a symbolic link or a junction - rather than one of the many other kinds (a file
 * of OneDrive that is not downloaded yet, a file deduplication moved, ...), which are files like any other.
 */
static bool torb_windows_is_link_tag(const wchar_t *path, DWORD attributes) {
  WIN32_FIND_DATAW entry;
  HANDLE handle;
  bool link;
  if ((attributes & (DWORD)FILE_ATTRIBUTE_REPARSE_POINT) == 0u) {
    return false;
  }
  handle = FindFirstFileW(path, &entry);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  link = entry.dwReserved0 == IO_REPARSE_TAG_SYMLINK || entry.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
  FindClose(handle);
  return link;
}

bool torb_platform_metadata(const char *path, bool follow, torb_path_metadata *out, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  WIN32_FILE_ATTRIBUTE_DATA data;
  bool link;
  if (wide == NULL) {
    *message = "No such file or directory";
    return false;
  }
  if (GetFileAttributesExW(wide, GetFileExInfoStandard, &data) == 0) {
    *message = torb_windows_message(GetLastError());
    torb_raw_free(wide, capacity);
    return false;
  }
  link = torb_windows_is_link_tag(wide, data.dwFileAttributes);
  if (link && follow) {
    /* `GetFileAttributesExW` describes the link itself; opening it follows it to what it points at */
    BY_HANDLE_FILE_INFORMATION information;
    HANDLE handle = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
      *message = torb_windows_message(GetLastError());
      torb_raw_free(wide, capacity);
      return false;
    }
    if (GetFileInformationByHandle(handle, &information) == 0) {
      *message = torb_windows_message(GetLastError());
      CloseHandle(handle);
      torb_raw_free(wide, capacity);
      return false;
    }
    CloseHandle(handle);
    data.dwFileAttributes = information.dwFileAttributes;
    data.ftLastWriteTime = information.ftLastWriteTime;
    data.nFileSizeHigh = information.nFileSizeHigh;
    data.nFileSizeLow = information.nFileSizeLow;
    link = false;
  }
  if (link) {
    out->kind = 3;
  } else if ((data.dwFileAttributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u) {
    out->kind = 2;
  } else if ((data.dwFileAttributes & (DWORD)FILE_ATTRIBUTE_DEVICE) != 0u) {
    out->kind = 4;
  } else {
    out->kind = 1;
  }
  out->size = out->kind == 2 ? 0 : (int64_t)(((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow);
  out->modified = torb_windows_unix_nanoseconds(data.ftLastWriteTime);
  out->mode = torb_windows_mode(wide, data.dwFileAttributes);
  torb_raw_free(wide, capacity);
  return true;
}

bool torb_platform_set_mode(const char *path, int64_t mode, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  DWORD attributes;
  DWORD wanted;
  bool set = true;
  if (wide == NULL) {
    *message = "No such file or directory";
    return false;
  }
  attributes = GetFileAttributesW(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    *message = torb_windows_message(GetLastError());
    torb_raw_free(wide, capacity);
    return false;
  }
  wanted = (mode & 0200) != 0 ? attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY : attributes | FILE_ATTRIBUTE_READONLY;
  if (wanted != attributes && SetFileAttributesW(wide, wanted) == 0) {
    *message = torb_windows_message(GetLastError());
    set = false;
  }
  torb_raw_free(wide, capacity);
  return set;
}

/* Where an SDK is older than Windows 10's 1703, which let a symbolic link be created without the privilege */
#  if !defined(SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
#    define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#  endif

bool torb_platform_create_symbolic_link(const char *path, const char *target, const char **message) {
  size_t link_capacity = 0u;
  size_t target_capacity = 0u;
  wchar_t *wide_link = torb_platform_system_path(path, &link_capacity);
  wchar_t *wide_target;
  DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  size_t index;
  bool made;
  if (wide_link == NULL) {
    *message = "No such file or directory";
    return false;
  }
  /* The target is stored as it is written - a relative one stays relative to the link - only with `\`, because a link
     whose target has `/` in it is followed by nothing on this platform */
  wide_target = torb_platform_wide(target, &target_capacity);
  if (wide_target == NULL) {
    *message = "No such file or directory";
    torb_raw_free(wide_link, link_capacity);
    return false;
  }
  for (index = 0u; wide_target[index] != L'\0'; index += 1u) {
    if (wide_target[index] == L'/') {
      wide_target[index] = L'\\';
    }
  }
  /* A link to a directory is a link of another kind here, so the target decides - read relative to the link's folder */
  {
    const bool absolute = wide_target[0] == L'\\' || (wide_target[0] != L'\0' && wide_target[1] == L':');
    DWORD attributes;
    if (absolute) {
      attributes = GetFileAttributesW(wide_target);
    } else {
      const size_t link_length = wcslen(wide_link);
      const size_t target_length = wcslen(wide_target);
      size_t folder = link_length;
      size_t joined_capacity;
      wchar_t *joined;
      while (folder > 0u && wide_link[folder - 1u] != L'\\') {
        folder -= 1u;
      }
      joined_capacity = (folder + target_length + 1u) * sizeof(wchar_t);
      joined = (wchar_t *)torb_raw_allocate(joined_capacity);
      memcpy(joined, wide_link, folder * sizeof(wchar_t));
      memcpy(joined + folder, wide_target, (target_length + 1u) * sizeof(wchar_t));
      attributes = GetFileAttributesW(joined);
      torb_raw_free(joined, joined_capacity);
    }
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u) {
      flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
  }
  made = CreateSymbolicLinkW(wide_link, wide_target, flags) != 0;
  if (!made && GetLastError() == ERROR_INVALID_PARAMETER) {
    /* A Windows older than 1703 does not know the flag at all */
    made = CreateSymbolicLinkW(wide_link, wide_target, flags & ~(DWORD)SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
  }
  if (!made) {
    *message = torb_windows_message(GetLastError());
  }
  torb_raw_free(wide_target, target_capacity);
  torb_raw_free(wide_link, link_capacity);
  return made;
}

/*
 * The part of `REPARSE_DATA_BUFFER` that a symbolic link and a junction have: the structure is in the driver kit's
 * `ntifs.h` and in no header of the SDK, so it is written out here.
 */
typedef struct torb_reparse_data {
  ULONG tag;
  USHORT data_length;
  USHORT reserved;
  union {
    struct {
      USHORT substitute_offset;
      USHORT substitute_length;
      USHORT print_offset;
      USHORT print_length;
      ULONG flags;
      WCHAR buffer[1];
    } symbolic_link;
    struct {
      USHORT substitute_offset;
      USHORT substitute_length;
      USHORT print_offset;
      USHORT print_length;
      WCHAR buffer[1];
    } mount_point;
  } data;
} torb_reparse_data;

#  if !defined(FSCTL_GET_REPARSE_POINT)
#    define FSCTL_GET_REPARSE_POINT 0x000900A8
#  endif

char *torb_platform_link_target(const char *path, size_t *length, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  HANDLE handle;
  const DWORD size = 16u * 1024u;
  torb_reparse_data *reparse;
  DWORD returned = 0u;
  const WCHAR *buffer;
  USHORT offset;
  USHORT bytes;
  size_t count;
  size_t target_capacity;
  wchar_t *target;
  size_t index;
  char *text;
  if (wide == NULL) {
    *message = "No such file or directory";
    return NULL;
  }
  handle = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
  torb_raw_free(wide, capacity);
  if (handle == INVALID_HANDLE_VALUE) {
    *message = torb_windows_message(GetLastError());
    return NULL;
  }
  reparse = (torb_reparse_data *)torb_raw_allocate(size);
  if (DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0u, reparse, size, &returned, NULL) == 0) {
    *message = torb_windows_message(GetLastError());
    torb_raw_free(reparse, size);
    CloseHandle(handle);
    return NULL;
  }
  CloseHandle(handle);
  if (reparse->tag == IO_REPARSE_TAG_SYMLINK) {
    buffer = reparse->data.symbolic_link.buffer;
    offset = reparse->data.symbolic_link.print_offset;
    bytes = reparse->data.symbolic_link.print_length;
    if (bytes == 0u) {
      offset = reparse->data.symbolic_link.substitute_offset;
      bytes = reparse->data.symbolic_link.substitute_length;
    }
  } else if (reparse->tag == IO_REPARSE_TAG_MOUNT_POINT) {
    buffer = reparse->data.mount_point.buffer;
    offset = reparse->data.mount_point.print_offset;
    bytes = reparse->data.mount_point.print_length;
    if (bytes == 0u) {
      offset = reparse->data.mount_point.substitute_offset;
      bytes = reparse->data.mount_point.substitute_length;
    }
  } else {
    *message = "Invalid argument";
    torb_raw_free(reparse, size);
    return NULL;
  }
  count = (size_t)bytes / sizeof(WCHAR);
  buffer += offset / sizeof(WCHAR);
  /* The name the kernel substitutes starts with `\??\`, which is the form of a call and never one a path is shown in */
  if (count >= 4u && buffer[0] == L'\\' && buffer[1] == L'?' && buffer[2] == L'?' && buffer[3] == L'\\') {
    buffer += 4;
    count -= 4u;
  }
  target_capacity = (count + 1u) * sizeof(wchar_t);
  target = (wchar_t *)torb_raw_allocate(target_capacity);
  for (index = 0u; index < count; index += 1u) {
    target[index] = buffer[index] == L'\\' ? L'/' : buffer[index];
  }
  target[count] = L'\0';
  torb_raw_free(reparse, size);
  text = torb_platform_utf8(target, length);
  torb_raw_free(target, target_capacity);
  if (text == NULL) {
    *message = "the target of this link is not valid Unicode";
  }
  return text;
}

/* `GetTempPath2W` is Windows 11's and the one that is right for a service, so it is looked up where it may be missing */
typedef DWORD(WINAPI *torb_get_temp_path)(DWORD length, LPWSTR buffer);

char *torb_platform_temporary_directory(size_t *length) {
  HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  torb_get_temp_path get = NULL;
  wchar_t buffer[MAX_PATH + 2];
  DWORD written;
  size_t index;
  if (kernel != NULL) {
    FARPROC found = GetProcAddress(kernel, "GetTempPath2W");
    memcpy(&get, &found, sizeof get);
  }
  if (get == NULL) {
    get = GetTempPathW;
  }
  written = get(MAX_PATH + 1, buffer);
  if (written == 0u || written > MAX_PATH + 1) {
    return NULL;
  }
  /* Without the separator it ends in, and with `/`: the form every path of a program has */
  while (written > 3u && buffer[written - 1u] == L'\\') {
    written -= 1u;
  }
  buffer[written] = L'\0';
  for (index = 0u; index < written; index += 1u) {
    if (buffer[index] == L'\\') {
      buffer[index] = L'/';
    }
  }
  return torb_platform_utf8(buffer, length);
}

void *torb_platform_create_new(const char *path, bool directory, int64_t mode, bool *exists, const char **message) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  void *made = NULL;
  (void)mode;
  *exists = false;
  if (wide == NULL) {
    *message = "No such file or directory";
    return NULL;
  }
  if (directory) {
    /* A directory answers a token that only says "made": the path it was handed, which the caller has anyway */
    if (CreateDirectoryW(wide, NULL) != 0) {
      made = (void *)path;
    } else {
      const DWORD code = GetLastError();
      *exists = code == ERROR_ALREADY_EXISTS;
      *message = torb_windows_message(code);
    }
  } else {
    const int descriptor = _wopen(wide, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (descriptor < 0) {
      *exists = errno == EEXIST;
      *message = strerror(errno);
    } else {
      made = _fdopen(descriptor, "wb");
      if (made == NULL) {
        *message = strerror(errno);
        _close(descriptor);
      }
    }
  }
  torb_raw_free(wide, capacity);
  return made;
}

bool torb_platform_sync_file(void *file, const char **message) {
  FILE *handle = (FILE *)file;
  if (fflush(handle) != 0) {
    *message = strerror(errno);
    return false;
  }
  if (FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(handle))) == 0) {
    *message = torb_windows_message(GetLastError());
    return false;
  }
  return true;
}

/* `MOVEFILE_WRITE_THROUGH` already made the rename durable, and a directory has no handle to flush here */
void torb_platform_sync_directory(const char *path) {
  (void)path;
}

uint64_t torb_platform_unique_seed(void) {
  LARGE_INTEGER counter;
  FILETIME now;
  (void)QueryPerformanceCounter(&counter);
  GetSystemTimeAsFileTime(&now);
  return (uint64_t)counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ ((uint64_t)now.dwHighDateTime << 20)
         ^ (uint64_t)now.dwLowDateTime;
}

bool torb_platform_remove(const char *path) {
  const char *message = NULL;
  return torb_platform_remove_entry(path, &message);
}

int64_t torb_platform_monotonic_nanoseconds(void) {
  static LARGE_INTEGER frequency;
  static bool have_frequency = false;
  LARGE_INTEGER counter;
  int64_t seconds;
  int64_t remainder_nanoseconds;
  if (!have_frequency) {
    QueryPerformanceFrequency(&frequency);
    have_frequency = true;
  }
  QueryPerformanceCounter(&counter);
  /* Split into whole seconds and a remainder before multiplying by a billion, so a counter that has run for years
     does not overflow the way `counter.QuadPart * 1000000000` would. */
  seconds = counter.QuadPart / frequency.QuadPart;
  remainder_nanoseconds = (counter.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart;
  return seconds * 1000000000LL + remainder_nanoseconds;
}

/* `Sleep` counts milliseconds, so the span is rounded up: the scheduler reads the clock afterwards either way. */
void torb_platform_sleep(int64_t nanoseconds) {
  int64_t milliseconds;
  if (nanoseconds <= 0) {
    return;
  }
  milliseconds = nanoseconds / 1000000LL + (nanoseconds % 1000000LL != 0 ? 1 : 0);
  if (milliseconds > 0x7FFFFFFFLL) {
    milliseconds = 0x7FFFFFFFLL;
  }
  Sleep((DWORD)milliseconds);
}

/**
 * The bottom of the stack of the calling thread: the start of the region the stack was reserved in, which is the one
 * the address of a local of this very function lies in. Every Windows since the first one with threads answers it, and
 * it is the reservation and not what is committed so far - the stack grows into it page by page.
 */
bool torb_platform_stack_low(uintptr_t *low) {
  MEMORY_BASIC_INFORMATION region;
  char here = 0;
  if (VirtualQuery(&here, &region, sizeof region) == 0u || region.AllocationBase == NULL) {
    return false;
  }
  *low = (uintptr_t)region.AllocationBase;
  return true;
}

/**
 * The program's own arguments, from `GetCommandLineW` - because the `argv` of `main` is **not** UTF-8 on this platform.
 * The C runtime builds it from the wide command line through the code page of the machine, which turns `ü` into one
 * byte 0xFC (not UTF-8 at all) and `日` into a question mark (measured on a machine with code page 1252). The wide
 * command line is what the operating system really has, so that is what is read, and `CommandLineToArgvW` splits it by
 * exactly the rule `torb_append_windows_argument` below writes.
 *
 * A command line that is not well formed UTF-16 answers false **before adding anything**, and the caller falls back to
 * `argv` - degraded text rather than a program that cannot start, and no new error kind for a case a command line
 * cannot really be in. That is why the arguments are all converted first and added afterwards.
 */
bool torb_platform_arguments(torb_list *out) {
  int count = 0;
  wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
  /* The first one is the program's own name, which `Process.arguments()` does not answer */
  const size_t given = wide == NULL || count < 1 ? 0u : (size_t)count - 1u;
  size_t texts_capacity = given * sizeof(char *);
  size_t lengths_capacity = given * sizeof(size_t);
  char **texts;
  size_t *lengths;
  size_t index;
  bool ok = true;
  if (wide == NULL) {
    return false;
  }
  /* Zeroed, because a conversion that fails leaves every later slot at `NULL` and the loop below reads them all */
  texts = given == 0u ? NULL : (char **)torb_raw_allocate_zeroed(texts_capacity);
  lengths = given == 0u ? NULL : (size_t *)torb_raw_allocate_zeroed(lengths_capacity);
  for (index = 0u; index < given; index += 1u) {
    texts[index] = torb_platform_utf8(wide[index + 1u], &lengths[index]);
    if (texts[index] == NULL) {
      ok = false;
      break;
    }
  }
  LocalFree(wide);
  for (index = 0u; index < given; index += 1u) {
    if (texts[index] == NULL) {
      continue;
    }
    if (ok) {
      torb_text argument = torb_text_from_cstring(texts[index]);
      torb_list_add(out, &argument);
    }
    torb_raw_free(texts[index], lengths[index] + 1u);
  }
  if (given != 0u) {
    torb_raw_free(texts, texts_capacity);
    torb_raw_free(lengths, lengths_capacity);
  }
  return ok;
}

bool torb_platform_environment_variable(const char *name, char **value, size_t *length) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_wide(name, &capacity);
  const wchar_t *found;
  if (wide == NULL) {
    return false;
  }
  found = _wgetenv(wide);
  torb_raw_free(wide, capacity);
  if (found == NULL) {
    return false;
  }
  *value = torb_platform_utf8(found, length);
  return *value != NULL;
}

/**
 * The environment block of `GetEnvironmentStringsW`, a copy owned by this call: `NAME=VALUE` entries one after the
 * other, each NUL terminated, and an empty one at the end. The `=` between the two halves is overwritten with a NUL, so
 * each half converts on its own. The names Windows keeps for the working directory of each drive (`=C:`) begin with `=`
 * and are no variables of anybody's.
 */
void torb_platform_environment_entries(torb_list *names, torb_list *values, bool (*keep)(const char *name)) {
  wchar_t *block = GetEnvironmentStringsW();
  wchar_t *entry;
  if (block == NULL) {
    return;
  }
  for (entry = block; *entry != L'\0'; entry += wcslen(entry) + 1u) {
    wchar_t *equals = wcschr(entry + 1, L'=');
    size_t nameLength = 0u;
    size_t valueLength = 0u;
    char *name;
    char *value;
    if (entry[0] == L'=' || equals == NULL) {
      continue;
    }
    *equals = L'\0';
    name = torb_platform_utf8(entry, &nameLength);
    value = torb_platform_utf8(equals + 1, &valueLength);
    *equals = L'=';
    if (name != NULL && value != NULL && keep(name)) {
      torb_text nameText = torb_text_from_cstring(name);
      torb_text valueText = torb_text_from_cstring(value);
      torb_list_add(names, &nameText);
      torb_list_add(values, &valueText);
    }
    if (name != NULL) {
      torb_raw_free(name, nameLength + 1u);
    }
    if (value != NULL) {
      torb_raw_free(value, valueLength + 1u);
    }
  }
  FreeEnvironmentStringsW(block);
}

/**
 * `GetModuleFileNameW`, grown until the path fits. What the program sees is the `/` form every other path of this
 * runtime has, without the `\\?\` a very long path may come back with.
 */
bool torb_platform_executable_path(char **value, size_t *length) {
  DWORD capacity = MAX_PATH;
  for (;;) {
    const size_t bytes = (size_t)capacity * sizeof(wchar_t);
    wchar_t *wide = (wchar_t *)torb_raw_allocate(bytes);
    const DWORD written = GetModuleFileNameW(NULL, wide, capacity);
    char *text;
    size_t start = 0u;
    size_t index;
    if (written == 0u || written >= capacity) {
      torb_raw_free(wide, bytes);
      if (written == 0u || capacity >= 32768u) {
        return false;
      }
      capacity *= 2u;
      continue;
    }
    text = torb_platform_utf8(wide, length);
    torb_raw_free(wide, bytes);
    if (text == NULL) {
      return false;
    }
    if (*length >= 4u && memcmp(text, "\\\\?\\", 4u) == 0) {
      start = 4u;
    }
    *value = (char *)torb_raw_allocate(*length - start + 1u);
    for (index = start; index <= *length; index += 1u) {
      (*value)[index - start] = text[index] == '\\' ? '/' : text[index];
    }
    torb_raw_free(text, *length + 1u);
    *length -= start;
    return true;
  }
}

bool torb_platform_set_environment_variable(const char *name, const char *value) {
  size_t name_capacity = 0u;
  size_t value_capacity = 0u;
  wchar_t *wide_name = torb_platform_wide(name, &name_capacity);
  wchar_t *wide_value = wide_name == NULL ? NULL : torb_platform_wide(value, &value_capacity);
  bool set = false;
  if (wide_value != NULL) {
    set = _wputenv_s(wide_name, wide_value) == 0;
    torb_raw_free(wide_value, value_capacity);
  }
  if (wide_name != NULL) {
    torb_raw_free(wide_name, name_capacity);
  }
  return set;
}

/** Which call failed and the system's error code, for the one line `torb_memory_limit_start` may write. Static. */
static const char *torb_job_failure(const char *call) {
  static char message[96];
  snprintf(message, sizeof message, "%s failed with error %lu", call, (unsigned long)GetLastError());
  return message;
}

/**
 * A job object of the process's own, which the process joins: `JOB_OBJECT_LIMIT_PROCESS_MEMORY` holds what it commits
 * to `bytes`, and a commit over it fails - `malloc` answers `NULL` - instead of paging the machine to a standstill.
 *
 * A process that is already in a job (a terminal, an IDE, a CI runner puts it in one) joins a **nested** job, which
 * Windows has done since Windows 8; where joining fails anyway the answer is false and the runtime counts instead.
 * `JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK` keeps the children out of it: a C compiler that a program starts needs what it
 * needs, and a child that is a TorbScript program sets a limit of its own. Such a child also leaves every job above
 * this one that allows breaking away, and stays in the first one that does not.
 *
 * The handle is never closed: the job lives as long as the process is in it either way, and there is nothing to gain.
 */
bool torb_platform_limit_memory(uint64_t bytes, const char **message) {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
  HANDLE job = CreateJobObjectW(NULL, NULL);
  if (job == NULL) {
    *message = torb_job_failure("CreateJobObject");
    return false;
  }
  memset(&limits, 0, sizeof limits);
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
  limits.ProcessMemoryLimit = bytes > (uint64_t)SIZE_MAX ? SIZE_MAX : (SIZE_T)bytes;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
    *message = torb_job_failure("SetInformationJobObject");
    CloseHandle(job);
    return false;
  }
  if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
    *message = torb_job_failure("AssignProcessToJobObject");
    CloseHandle(job);
    return false;
  }
  return true;
}

uint64_t torb_platform_physical_memory(void) {
  MEMORYSTATUSEX status;
  status.dwLength = sizeof status;
  if (!GlobalMemoryStatusEx(&status)) {
    return 0u;
  }
  return (uint64_t)status.ullTotalPhys;
}

size_t torb_platform_allocation_size(void *block) {
  return _msize(block);
}

/* ============================================================================================== POSIX ========== */

#else

torb_path_kind torb_platform_path_kind(const char *path) {
  struct stat information;
  if (stat(path, &information) != 0) {
    return TORB_PATH_MISSING;
  }
  if ((information.st_mode & (unsigned)S_IFMT) == (unsigned)S_IFDIR) {
    return TORB_PATH_DIRECTORY;
  }
  return TORB_PATH_FILE;
}

bool torb_platform_is_link(const char *path) {
  struct stat information;
  if (lstat(path, &information) != 0) {
    return false;
  }
  return (information.st_mode & (unsigned)S_IFMT) == (unsigned)S_IFLNK;
}

char *torb_platform_working_directory(size_t *length) {
  size_t capacity = 512u;
  for (;;) {
    char *buffer = (char *)torb_raw_allocate(capacity);
    if (getcwd(buffer, capacity) != NULL) {
      *length = strlen(buffer);
      return buffer;
    }
    torb_raw_free(buffer, capacity);
    if (capacity >= 65536u) {
      return NULL;
    }
    capacity *= 2u;
  }
}

/*
 * The message for a name that is not valid UTF-8, which the caller copies into its error right away: the bytes, the
 * printable ASCII ones as they are and every other one as `\xNN`, so a program can tell which entry it was. One buffer
 * is enough, because the runtime has one worker and the message is read before anything lists a directory again.
 */
static char torb_unreadable_name[320];

static const char *torb_describe_unreadable_name(const char *name) {
  static const char prefix[] = "a name in this directory is not valid UTF-8: ";
  size_t used = sizeof prefix - 1u;
  size_t index;
  memcpy(torb_unreadable_name, prefix, used);
  for (index = 0u; name[index] != '\0'; index += 1u) {
    unsigned char byte = (unsigned char)name[index];
    /* Room for one escaped byte, the ellipsis that says the name went on, and the terminator. */
    if (used + 4u + 3u + 1u > sizeof torb_unreadable_name) {
      memcpy(torb_unreadable_name + used, "...", 3u);
      used += 3u;
      break;
    }
    if (byte >= 0x20u && byte < 0x7Fu && byte != (unsigned char)'\\') {
      torb_unreadable_name[used] = (char)byte;
      used += 1u;
    } else {
      snprintf(torb_unreadable_name + used, 5u, "\\x%02X", (unsigned int)byte);
      used += 4u;
    }
  }
  torb_unreadable_name[used] = '\0';
  return torb_unreadable_name;
}

/*
 * A name that is not valid UTF-8 ends the listing with an error instead of becoming a value (docs/design/PATH.md, the
 * directory entry that is not UTF-8): a `String` is always UTF-8, and no replacement character is invented for it.
 */
bool torb_platform_list_directory(const char *path, torb_list *out, const char **message) {
  DIR *directory = opendir(path);
  struct dirent *entry;
  if (directory == NULL) {
    *message = strerror(errno);
    return false;
  }
  while ((entry = readdir(directory)) != NULL) {
    size_t bad = 0u;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!torb_utf8_validate((const uint8_t *)entry->d_name, strlen(entry->d_name), &bad)) {
      *message = torb_describe_unreadable_name(entry->d_name);
      closedir(directory);
      return false;
    }
    {
      torb_text name = torb_text_from_cstring(entry->d_name);
      torb_list_add(out, &name);
    }
  }
  closedir(directory);
  return true;
}

/** One directory, where everything above it is already there. False with a message, also where it is already there. */
static bool torb_make_one_directory(const char *path, const char **message) {
  if (mkdir(path, 0777) == 0) {
    return true;
  }
  *message = strerror(errno);
  return false;
}

void *torb_platform_open_file(const char *path, bool writing, const char **message) {
  FILE *file = fopen(path, writing ? "wb" : "rb");
  if (file == NULL) {
    *message = strerror(errno);
  }
  return file;
}

bool torb_platform_remove_entry(const char *path, const char **message) {
  struct stat information;
  if (lstat(path, &information) != 0) {
    *message = strerror(errno);
    return false;
  }
  /* `lstat`, so a link to a directory is a link and `unlink` removes it, never what it points at */
  if (S_ISDIR(information.st_mode) ? rmdir(path) != 0 : unlink(path) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

bool torb_platform_remove(const char *path) {
  const char *message = NULL;
  return torb_platform_remove_entry(path, &message);
}

bool torb_platform_rename(const char *from, const char *to, const char **message) {
  if (rename(from, to) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

/* Until every byte is written: `write` may take fewer than it is handed, and a signal may interrupt it. */
static bool torb_write_all(int descriptor, const uint8_t *bytes, size_t length) {
  while (length > 0u) {
    const ssize_t written = write(descriptor, bytes, length);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    bytes += (size_t)written;
    length -= (size_t)written;
  }
  return true;
}

bool torb_platform_copy_file(const char *from, const char *to, const char **message) {
  struct stat information;
  uint8_t buffer[65536];
  int source = open(from, O_RDONLY);
  int target;
  bool ok = true;
  if (source < 0) {
    *message = strerror(errno);
    return false;
  }
  if (fstat(source, &information) != 0) {
    *message = strerror(errno);
    close(source);
    return false;
  }
  if (S_ISDIR(information.st_mode)) {
    *message = strerror(EISDIR);
    close(source);
    return false;
  }
  target = open(to, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)(information.st_mode & 0777));
  if (target < 0) {
    *message = strerror(errno);
    close(source);
    return false;
  }
  for (;;) {
    const ssize_t read_count = read(source, buffer, sizeof buffer);
    if (read_count == 0) {
      break;
    }
    if (read_count < 0) {
      if (errno == EINTR) {
        continue;
      }
      *message = strerror(errno);
      ok = false;
      break;
    }
    if (!torb_write_all(target, buffer, (size_t)read_count)) {
      *message = strerror(errno);
      ok = false;
      break;
    }
  }
  /* A file that was there keeps the mode `open` does not change: the permissions of the source are set on it */
  if (ok && fchmod(target, (mode_t)(information.st_mode & 07777)) != 0) {
    *message = strerror(errno);
    ok = false;
  }
  if (close(target) != 0 && ok) {
    *message = strerror(errno);
    ok = false;
  }
  close(source);
  return ok;
}

/* The time a file was last written, as nanoseconds since 1970: `st_mtim` of POSIX 2008, `st_mtimespec` on macOS. */
static int64_t torb_modified_nanoseconds(const struct stat *information) {
#  if defined(__APPLE__)
  return (int64_t)information->st_mtimespec.tv_sec * INT64_C(1000000000) + (int64_t)information->st_mtimespec.tv_nsec;
#  else
  return (int64_t)information->st_mtim.tv_sec * INT64_C(1000000000) + (int64_t)information->st_mtim.tv_nsec;
#  endif
}

bool torb_platform_metadata(const char *path, bool follow, torb_path_metadata *out, const char **message) {
  struct stat information;
  if ((follow ? stat(path, &information) : lstat(path, &information)) != 0) {
    *message = strerror(errno);
    return false;
  }
  if (S_ISREG(information.st_mode)) {
    out->kind = 1;
  } else if (S_ISDIR(information.st_mode)) {
    out->kind = 2;
  } else if (S_ISLNK(information.st_mode)) {
    out->kind = 3;
  } else {
    out->kind = 4;
  }
  out->size = (int64_t)information.st_size;
  out->modified = torb_modified_nanoseconds(&information);
  out->mode = (int64_t)(information.st_mode & 07777);
  return true;
}

bool torb_platform_set_mode(const char *path, int64_t mode, const char **message) {
  if (chmod(path, (mode_t)(mode & 07777)) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

bool torb_platform_create_symbolic_link(const char *path, const char *target, const char **message) {
  if (symlink(target, path) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

char *torb_platform_link_target(const char *path, size_t *length, const char **message) {
  size_t capacity = 256u;
  for (;;) {
    char *buffer = (char *)torb_raw_allocate(capacity);
    const ssize_t count = readlink(path, buffer, capacity);
    if (count < 0) {
      *message = strerror(errno);
      torb_raw_free(buffer, capacity);
      return NULL;
    }
    /* A target that filled the buffer may have been cut, so it is read again into one twice the size */
    if ((size_t)count < capacity) {
      size_t bad = 0u;
      char *text;
      if (!torb_utf8_validate((const uint8_t *)buffer, (size_t)count, &bad)) {
        *message = "the target of this link is not valid UTF-8";
        torb_raw_free(buffer, capacity);
        return NULL;
      }
      text = (char *)torb_raw_allocate((size_t)count + 1u);
      memcpy(text, buffer, (size_t)count);
      text[count] = '\0';
      torb_raw_free(buffer, capacity);
      *length = (size_t)count;
      return text;
    }
    torb_raw_free(buffer, capacity);
    if (capacity >= 65536u) {
      *message = strerror(ENAMETOOLONG);
      return NULL;
    }
    capacity *= 2u;
  }
}

char *torb_platform_temporary_directory(size_t *length) {
  const char *given = getenv("TMPDIR");
  size_t count;
  char *text;
#  if defined(__APPLE__)
  char darwin[1024];
#  endif
  if (given == NULL || given[0] != '/') {
    given = "/tmp";
#  if defined(__APPLE__)
    /* The user's own directory under `/var/folders`, which `TMPDIR` names in every session of a user anyway */
    {
      const size_t needed = confstr(_CS_DARWIN_USER_TEMP_DIR, darwin, sizeof darwin);
      if (needed > 1u && needed <= sizeof darwin) {
        given = darwin;
      }
    }
#  endif
  }
  count = strlen(given);
  while (count > 1u && given[count - 1u] == '/') {
    count -= 1u;
  }
  text = (char *)torb_raw_allocate(count + 1u);
  memcpy(text, given, count);
  text[count] = '\0';
  *length = count;
  return text;
}

void *torb_platform_create_new(const char *path, bool directory, int64_t mode, bool *exists, const char **message) {
  *exists = false;
  if (directory) {
    /* A directory answers a token that only says "made": the path it was handed, which the caller has anyway */
    if (mkdir(path, (mode_t)(mode & 07777)) == 0) {
      return (void *)path;
    }
    *exists = errno == EEXIST;
    *message = strerror(errno);
    return NULL;
  }
  {
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, (mode_t)(mode & 07777));
    FILE *file;
    if (descriptor < 0) {
      *exists = errno == EEXIST;
      *message = strerror(errno);
      return NULL;
    }
    file = fdopen(descriptor, "wb");
    if (file == NULL) {
      *message = strerror(errno);
      close(descriptor);
    }
    return file;
  }
}

bool torb_platform_sync_file(void *file, const char **message) {
  FILE *handle = (FILE *)file;
  if (fflush(handle) != 0 || fsync(fileno(handle)) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

/* Best effort: a rename is durable once its directory is, and some file systems refuse to sync a directory at all */
void torb_platform_sync_directory(const char *path) {
  const int descriptor = open(path, O_RDONLY);
  if (descriptor >= 0) {
    (void)fsync(descriptor);
    close(descriptor);
  }
}

uint64_t torb_platform_unique_seed(void) {
  struct timespec now;
  (void)clock_gettime(CLOCK_REALTIME, &now);
  return ((uint64_t)now.tv_sec << 30) ^ (uint64_t)now.tv_nsec ^ ((uint64_t)getpid() << 40);
}

int64_t torb_platform_monotonic_nanoseconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

void torb_platform_sleep(int64_t nanoseconds) {
  struct timespec span;
  if (nanoseconds <= 0) {
    return;
  }
  span.tv_sec = (time_t)(nanoseconds / 1000000000LL);
  span.tv_nsec = (long)(nanoseconds % 1000000000LL);
  /* A signal cuts the sleep short and leaves what is left in `span`. */
  while (nanosleep(&span, &span) != 0 && errno == EINTR) {
  }
}

/**
 * The bottom of the stack of the calling thread, for the main thread: the limit of `RLIMIT_STACK` below an address near
 * its top, which is how far the kernel lets that stack grow. An unlimited stack is taken as 8 MiB, the usual default -
 * a check that fires too early is a panic with a message, one that never fires is the crash it is there to prevent.
 */
bool torb_platform_stack_low(uintptr_t *low) {
  struct rlimit limit;
  char top;
  uintptr_t size = (uintptr_t)8u * 1024u * 1024u;
  if (getrlimit(RLIMIT_STACK, &limit) != 0) {
    return false;
  }
  if (limit.rlim_cur != RLIM_INFINITY && (uintptr_t)limit.rlim_cur < (uintptr_t)&top) {
    size = (uintptr_t)limit.rlim_cur;
  }
  *low = (uintptr_t)&top - size;
  return true;
}

/** The `argv` of `main` is what there is here, and it is bytes - which is what a path and a `String` both are. */
bool torb_platform_arguments(torb_list *out) {
  (void)out;
  return false;
}

bool torb_platform_environment_variable(const char *name, char **value, size_t *length) {
  const char *found = getenv(name);
  if (found == NULL) {
    return false;
  }
  *length = strlen(found);
  *value = (char *)torb_raw_allocate(*length + 1u);
  memcpy(*value, found, *length + 1u);
  return true;
}

/** Declared by no header of strict POSIX, and defined by every C library of it. */
extern char **environ;

/**
 * `environ`, entry by entry. A name or a value that is not UTF-8 is left out: a `String` is always valid UTF-8, and a
 * variable whose bytes are something else has no value a program could be handed.
 */
void torb_platform_environment_entries(torb_list *names, torb_list *values, bool (*keep)(const char *name)) {
  char **entry;
  if (environ == NULL) {
    return;
  }
  for (entry = environ; *entry != NULL; entry += 1) {
    const char *text = *entry;
    const char *equals = strchr(text, '=');
    size_t nameLength;
    char *name;
    torb_text nameText = torb_text_empty();
    torb_text valueText = torb_text_empty();
    size_t bad = 0u;
    if (equals == NULL || equals == text) {
      continue;
    }
    nameLength = (size_t)(equals - text);
    name = (char *)torb_raw_allocate(nameLength + 1u);
    memcpy(name, text, nameLength);
    name[nameLength] = '\0';
    if (keep(name) && torb_text_try_from_bytes((const uint8_t *)text, nameLength, &nameText, &bad)) {
      if (torb_text_try_from_bytes((const uint8_t *)(equals + 1), strlen(equals + 1), &valueText, &bad)) {
        torb_list_add(names, &nameText);
        torb_list_add(values, &valueText);
      } else {
        torb_text_release(nameText);
      }
    }
    torb_raw_free(name, nameLength + 1u);
  }
}

bool torb_platform_set_environment_variable(const char *name, const char *value) {
  return setenv(name, value, 1) == 0;
}

/**
 * Linux names the executable in `/proc/self/exe`, and macOS answers `_NSGetExecutablePath` (`os/macos.c`). Where
 * neither is there (the BSDs without procfs), the answer is false rather than a guess from `argv[0]`, which names
 * whatever the caller typed.
 */
#  if defined(__APPLE__)
bool torb_platform_executable_path(char **value, size_t *length) {
  return torb_os_macos_executable_path(value, length);
}
#  else
bool torb_platform_executable_path(char **value, size_t *length) {
  size_t capacity = 256u;
  for (;;) {
    char *buffer = (char *)torb_raw_allocate(capacity);
    const ssize_t written = readlink("/proc/self/exe", buffer, capacity);
    if (written < 0) {
      torb_raw_free(buffer, capacity);
      return false;
    }
    if ((size_t)written < capacity) {
      *length = (size_t)written;
      *value = (char *)torb_raw_allocate(*length + 1u);
      memcpy(*value, buffer, *length);
      (*value)[*length] = '\0';
      torb_raw_free(buffer, capacity);
      return true;
    }
    torb_raw_free(buffer, capacity);
    if (capacity >= 65536u) {
      return false;
    }
    capacity *= 2u;
  }
}
#  endif

#  if defined(TORB_MEMORY_RESOURCE)

/**
 * What the limit was before this process lowered it, and whether it did: a child gets it back (below), because a limit
 * set with `setrlimit` is inherited and a C compiler that a program starts needs what it needs.
 */
static struct rlimit torb_memory_before;
static bool torb_memory_lowered = false;

/**
 * The soft limit of `TORB_MEMORY_RESOURCE`, lowered to `bytes` and never raised: a hard limit below `bytes` is already
 * the stricter one, and the soft one becomes that. An allocation over it fails, and `malloc` answers `NULL`.
 */
bool torb_platform_limit_memory(uint64_t bytes, const char **message) {
  struct rlimit limit;
  if (getrlimit(TORB_MEMORY_RESOURCE, &limit) != 0) {
    *message = strerror(errno);
    return false;
  }
  torb_memory_before = limit;
  if (limit.rlim_max != RLIM_INFINITY && (uint64_t)limit.rlim_max < bytes) {
    limit.rlim_cur = limit.rlim_max;
  } else {
    limit.rlim_cur = (rlim_t)bytes;
  }
  if (setrlimit(TORB_MEMORY_RESOURCE, &limit) != 0) {
    *message = strerror(errno);
    return false;
  }
  torb_memory_lowered = true;
  return true;
}

/* In a child between `fork` and `exec`: the limit as it was before this process lowered it. Async-signal-safe. */
static void torb_memory_restore_for_child(void) {
  if (torb_memory_lowered) {
    (void)setrlimit(TORB_MEMORY_RESOURCE, &torb_memory_before);
  }
}

#  else

/* No mechanism that fits this system: the runtime counts its own allocations, and that is no failure to report */
bool torb_platform_limit_memory(uint64_t bytes, const char **message) {
  (void)bytes;
  *message = NULL;
  return false;
}

static void torb_memory_restore_for_child(void) {
}

#  endif

#  if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
uint64_t torb_platform_physical_memory(void) {
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long size = sysconf(_SC_PAGESIZE);
  if (pages <= 0 || size <= 0) {
    return 0u;
  }
  return (uint64_t)pages * (uint64_t)size;
}
#  else
uint64_t torb_platform_physical_memory(void) {
  return 0u;
}
#  endif

size_t torb_platform_allocation_size(void *block) {
  return TORB_ALLOCATION_SIZE(block);
}

#endif

/* ============================================================================ the same on both platforms ======== */

/**
 * `mkdir -p`: every directory of the path that is missing, and nothing where one is already there. The separator is
 * both `/` and `\` on Windows, because a path that came through `absolutePath` is all forward slashes and one a user
 * typed may not be - and neither byte can be part of a UTF-8 sequence, so splitting the text on them is safe.
 */
bool torb_platform_create_directory(const char *path, const char **message) {
  size_t length = strlen(path);
  char *buffer;
  size_t index;
  bool ok = true;
  if (length == 0u) {
    *message = "the path is empty";
    return false;
  }
  buffer = (char *)torb_raw_allocate(length + 1u);
  memcpy(buffer, path, length + 1u);
  for (index = 1u; index <= length && ok; index++) {
    const bool isSeparator = buffer[index] == '/' || buffer[index] == '\\';
    if (index != length && !isSeparator) {
      continue;
    }
    {
      const char kept = buffer[index];
      buffer[index] = '\0';
      /* A drive letter (`C:`) is no directory anybody creates, and neither is one that is already there */
      if (buffer[index - 1u] != ':' && torb_platform_path_kind(buffer) == TORB_PATH_MISSING) {
        const char *failure = NULL;
        if (!torb_make_one_directory(buffer, &failure) && torb_platform_path_kind(buffer) != TORB_PATH_DIRECTORY) {
          *message = failure;
          ok = false;
        }
      }
      buffer[index] = kept;
    }
  }
  torb_raw_free(buffer, length + 1u);
  return ok;
}

bool torb_platform_read_file(const char *path, uint8_t **bytes, size_t *length, const char **message) {
  FILE *file = (FILE *)torb_platform_open_file(path, false, message);
  size_t capacity = 65536u;
  size_t filled = 0u;
  uint8_t *buffer;
  if (file == NULL) {
    return false;
  }
  buffer = (uint8_t *)torb_raw_allocate(capacity);
  for (;;) {
    size_t read = fread(buffer + filled, 1u, capacity - filled, file);
    filled += read;
    if (filled < capacity) {
      if (ferror(file)) {
        *message = strerror(errno);
        torb_raw_free(buffer, capacity);
        fclose(file);
        return false;
      }
      break;
    }
    {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
      memcpy(grown, buffer, filled);
      torb_raw_free(buffer, capacity);
      buffer = grown;
      capacity *= 2u;
    }
  }
  fclose(file);
  *bytes = buffer;
  *length = filled;
  return true;
}

bool torb_platform_write_file(const char *path, const uint8_t *bytes, size_t length, const char **message) {
  FILE *file = (FILE *)torb_platform_open_file(path, true, message);
  if (file == NULL) {
    return false;
  }
  if (length > 0u && fwrite(bytes, 1u, length, file) != length) {
    *message = strerror(errno);
    fclose(file);
    return false;
  }
  if (fclose(file) != 0) {
    *message = strerror(errno);
    return false;
  }
  return true;
}

/* ================================================================================== running a child process ===== */

#if defined(_WIN32)

/**
 * Room for `needed` more bytes plus the terminator, doubling. The command line below grows this way.
 */
static void torb_reserve_line(size_t needed, char **into, size_t filled, size_t *capacity) {
  size_t grown = *capacity;
  char *buffer;
  if (filled + needed + 1u <= *capacity) {
    return;
  }
  while (grown < filled + needed + 1u) {
    grown *= 2u;
  }
  buffer = (char *)torb_raw_allocate(grown);
  memcpy(buffer, *into, filled + 1u);
  torb_raw_free(*into, *capacity);
  *into = buffer;
  *capacity = grown;
}

/**
 * One argument of a Windows command line, quoted the way `CommandLineToArgvW` reads it back - which is the rule the
 * child's own C runtime undoes, so the argument arrives as exactly the text that was passed.
 *
 * The rule has one subtlety: a backslash is only an escape **in front of a quote**, so a run of backslashes is doubled
 * where a quote follows it (including the closing one) and passed through everywhere else. `C:\Program Files\` as the
 * last argument would otherwise end as `C:\Program Files\"` and swallow the quote.
 *
 * An argument that needs no quoting is written bare, which keeps a command line readable in a debugger.
 *
 * The line is built in UTF-8 like everything else in the runtime and converted once, at the call.
 */
static void torb_append_windows_argument(const char *argument, char **into, size_t *filled, size_t *capacity) {
  const size_t length = strlen(argument);
  size_t index;
  bool needsQuotes = length == 0u;
  for (index = 0u; index < length; index++) {
    if (argument[index] == ' ' || argument[index] == '\t' || argument[index] == '"') {
      needsQuotes = true;
      break;
    }
  }
  /* Two quotes, one space, and in the worst case two bytes per byte of the argument */
  torb_reserve_line(length * 2u + 3u, into, *filled, capacity);
  if (*filled > 0u) {
    (*into)[(*filled)++] = ' ';
  }
  if (!needsQuotes) {
    memcpy(*into + *filled, argument, length);
    *filled += length;
    (*into)[*filled] = '\0';
    return;
  }
  (*into)[(*filled)++] = '"';
  index = 0u;
  while (index < length) {
    size_t slashes = 0u;
    while (index < length && argument[index] == '\\') {
      slashes++;
      index++;
    }
    if (index == length) {
      /* In front of the closing quote, so every backslash is doubled */
      slashes *= 2u;
    } else if (argument[index] == '"') {
      slashes = slashes * 2u + 1u;
    }
    while (slashes > 0u) {
      (*into)[(*filled)++] = '\\';
      slashes--;
    }
    if (index < length) {
      (*into)[(*filled)++] = argument[index];
      index++;
    }
  }
  (*into)[(*filled)++] = '"';
  (*into)[*filled] = '\0';
}

/**
 * The program's own name, with every `/` written as `\`. Only the name, never an argument.
 *
 * `CreateProcessW` is called without an application name, so that it searches `PATH` and appends `.exe` the way a
 * command line does - and then it parses the name out of the command line itself, by a rule that does not know that
 * `/` separates directories. A **relative** path spelled with forward slashes therefore names nothing:
 * `compiler/tests/build/release/tests.exe` could not be started at all while `compiler\tests\...` started. An absolute
 * one works either way, which is what made it look like a path problem of one caller rather than of the layer.
 *
 * A `String` of this language spells a path with forward slashes everywhere (`normalizePath`, `File.absolutePath`), so
 * the conversion belongs here, at the boundary, and nowhere above it.
 */
static char *torb_windows_command(const char *command, size_t *capacity) {
  const size_t length = strlen(command);
  size_t index;
  char *copy = (char *)torb_raw_allocate(length + 1u);
  for (index = 0u; index < length; index++) {
    copy[index] = command[index] == '/' ? '\\' : command[index];
  }
  copy[length] = '\0';
  *capacity = length + 1u;
  return copy;
}

/**
 * Everything that can still be read from a handle, into a buffer that grows in doublings. Owned: freed with
 * `torb_raw_free(*bytes, *capacity)`. Ends at the end of a pipe or a file, and at the first failed read.
 */
static void torb_read_handle(HANDLE handle, uint8_t **bytes, size_t *length, size_t *capacity) {
  size_t bufferCapacity = 65536u;
  size_t filled = 0u;
  uint8_t *buffer = (uint8_t *)torb_raw_allocate(bufferCapacity);
  for (;;) {
    DWORD read = 0u;
    if (filled == bufferCapacity) {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(bufferCapacity * 2u);
      memcpy(grown, buffer, filled);
      torb_raw_free(buffer, bufferCapacity);
      buffer = grown;
      bufferCapacity *= 2u;
    }
    if (!ReadFile(handle, buffer + filled, (DWORD)(bufferCapacity - filled), &read, NULL) || read == 0u) {
      break;
    }
    filled += (size_t)read;
  }
  *bytes = buffer;
  *length = filled;
  *capacity = bufferCapacity;
}

/**
 * A temporary file a child can inherit as its standard error, deleted by the system when the last handle to it is
 * closed - so nothing is left behind whichever way this process ends. `INVALID_HANDLE_VALUE` where none can be made.
 */
static HANDLE torb_errors_file(SECURITY_ATTRIBUTES *inheritable) {
  wchar_t directory[MAX_PATH + 1];
  wchar_t path[MAX_PATH + 1];
  HANDLE file;
  const DWORD length = GetTempPathW(MAX_PATH + 1, directory);
  if (length == 0u || length > MAX_PATH) {
    return INVALID_HANDLE_VALUE;
  }
  if (GetTempFileNameW(directory, L"trb", 0u, path) == 0u) {
    return INVALID_HANDLE_VALUE;
  }
  file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (file == INVALID_HANDLE_VALUE) {
    DeleteFileW(path);
  }
  return file;
}

/**
 * A child process, run to its end, with its two output streams collected - through `CreateProcessW` and a pipe, and
 * through **no shell at all**.
 *
 * `_popen` would be four lines instead of forty, and it was what this did. It cannot work: `_popen` runs `cmd.exe /c`
 * with a command line, and `cmd` re-parses the quotes by a rule that depends on where the first quote stands and on
 * whether what precedes it names an executable file (`cmd /?`, "processing of quote characters"). Measured on this
 * machine: `"gcc" "--version"` arrives at `cmd` as one command *named* `gcc" "--version`, so **`torb build` from the
 * compiled compiler reported "no C compiler found"** while the interpreter, which uses no shell, found gcc on the same
 * PATH. There is no quoting that survives both that rule and a nested `cmd /c` - which is what
 * `Process.run("cmd", ["/c", "echo torb"])` is - so the shell has to go.
 *
 * What that buys, beyond the bug: **"there is no shell" in `std/process` is now true on this platform**, and a program
 * that cannot be started at all is a failure again instead of `cmd`'s own exit code 1 - which is the difference
 * `findCompiler` reads and what the interpreter answers.
 *
 * The **wide** call is what carries the command line, so a program name and an argument may hold any character a
 * `String` can - the narrow one would hand them to the code page of the machine and lose them.
 *
 * Standard output goes into a pipe and standard error into a temporary file that is deleted when it is closed. Two
 * pipes would need two readers at once - a child that fills the one nobody is reading waits forever - and a file never
 * makes the child wait, so it is read after the child has ended. Where no temporary file can be made, both streams go
 * into the pipe and the errors come back empty, which is what a `ProcessOutput` said before it kept them apart.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  const uint8_t *input,
  size_t inputLength,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  uint8_t **errors,
  size_t *errorsLength,
  size_t *errorsCapacity,
  const char **message
) {
  size_t lineCapacity = 512u;
  size_t filled = 0u;
  char *line = (char *)torb_raw_allocate(lineCapacity);
  size_t wideCapacity = 0u;
  wchar_t *wideLine;
  size_t index;
  SECURITY_ATTRIBUTES inheritable;
  STARTUPINFOW startup;
  PROCESS_INFORMATION child;
  HANDLE readEnd = NULL;
  HANDLE writeEnd = NULL;
  HANDLE errorsFile = INVALID_HANDLE_VALUE;
  HANDLE inputFile = INVALID_HANDLE_VALUE;
  DWORD status = 0u;
  size_t nameCapacity = 0u;
  char *name = torb_windows_command(command, &nameCapacity);
  line[0] = '\0';
  torb_append_windows_argument(name, &line, &filled, &lineCapacity);
  torb_raw_free(name, nameCapacity);
  for (index = 0u; index < count; index++) {
    torb_append_windows_argument(arguments[index], &line, &filled, &lineCapacity);
  }
  wideLine = torb_platform_wide(line, &wideCapacity);
  torb_raw_free(line, lineCapacity);
  if (wideLine == NULL) {
    *message = "the program could not be started";
    return false;
  }
  inheritable.nLength = (DWORD)sizeof(inheritable);
  inheritable.lpSecurityDescriptor = NULL;
  inheritable.bInheritHandle = TRUE;
  if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0u)) {
    *message = "the pipe for the output of the child process could not be created";
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  /* Our end of the pipe must not reach the child, or the read below never sees the pipe close */
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0u);
  memset(&startup, 0, sizeof(startup));
  memset(&child, 0, sizeof(child));
  startup.cb = (DWORD)sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  /* Our own standard input, so a child that reads one still can - and no handle at all where this process has none,
     because `STARTF_USESTDHANDLES` with an invalid one would fail the whole call */
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  if (startup.hStdInput == INVALID_HANDLE_VALUE) {
    startup.hStdInput = NULL;
  }
  /* Input of its own: the bytes in a temporary file, read from its start, and the end of the input after them */
  if (input != NULL) {
    DWORD written = 0u;
    inputFile = torb_errors_file(&inheritable);
    if (inputFile == INVALID_HANDLE_VALUE
        || (inputLength > 0u && (!WriteFile(inputFile, input, (DWORD)inputLength, &written, NULL)
                                 || (size_t)written != inputLength))) {
      *message = "the input for the child process could not be written to a temporary file";
      if (inputFile != INVALID_HANDLE_VALUE) {
        CloseHandle(inputFile);
      }
      CloseHandle(readEnd);
      CloseHandle(writeEnd);
      torb_raw_free(wideLine, wideCapacity);
      return false;
    }
    SetFilePointer(inputFile, 0, NULL, FILE_BEGIN);
    startup.hStdInput = inputFile;
  }
  errorsFile = torb_errors_file(&inheritable);
  startup.hStdOutput = writeEnd;
  startup.hStdError = errorsFile != INVALID_HANDLE_VALUE ? errorsFile : writeEnd;
  /* No application name, so Windows searches PATH and appends `.exe` to a name without an extension */
  if (!CreateProcessW(NULL, wideLine, NULL, NULL, TRUE, 0u, NULL, NULL, &startup, &child)) {
    *message = "the program could not be started";
    CloseHandle(readEnd);
    CloseHandle(writeEnd);
    if (errorsFile != INVALID_HANDLE_VALUE) {
      CloseHandle(errorsFile);
    }
    if (inputFile != INVALID_HANDLE_VALUE) {
      CloseHandle(inputFile);
    }
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  torb_raw_free(wideLine, wideCapacity);
  /* And our copy of the write end has to go too, for the same reason; the child has its own handle of the input */
  CloseHandle(writeEnd);
  if (inputFile != INVALID_HANDLE_VALUE) {
    CloseHandle(inputFile);
  }
  torb_read_handle(readEnd, output, length, capacity);
  CloseHandle(readEnd);
  WaitForSingleObject(child.hProcess, INFINITE);
  if (!GetExitCodeProcess(child.hProcess, &status)) {
    status = (DWORD)-1;
  }
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  if (errorsFile != INVALID_HANDLE_VALUE) {
    /* The child wrote through the same file position, so what it wrote starts at the beginning */
    SetFilePointer(errorsFile, 0, NULL, FILE_BEGIN);
    torb_read_handle(errorsFile, errors, errorsLength, errorsCapacity);
    CloseHandle(errorsFile);
  } else {
    *errorsCapacity = 16u;
    *errors = (uint8_t *)torb_raw_allocate(*errorsCapacity);
    *errorsLength = 0u;
  }
  *code = (int64_t)(int32_t)status;
  return true;
}

/**
 * The same child process, run to its end with **this process's own three streams** instead of a pipe: what it writes
 * appears where this program's output appears, while it writes it, and what it reads comes from the same place.
 *
 * Without `STARTF_USESTDHANDLES` the child is given the handles this process holds, which is the whole difference. It
 * is what a driver needs and what collecting cannot be: a program that runs for a minute prints nothing until it ends
 * when its output is collected, the two streams arrive merged and in the wrong order, and a program that asks a
 * question has nobody to ask.
 */
bool torb_platform_run_inheriting(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  const char **message
) {
  size_t lineCapacity = 512u;
  size_t filled = 0u;
  char *line = (char *)torb_raw_allocate(lineCapacity);
  size_t wideCapacity = 0u;
  wchar_t *wideLine;
  size_t index;
  STARTUPINFOW startup;
  PROCESS_INFORMATION child;
  DWORD status = 0u;
  size_t nameCapacity = 0u;
  char *name = torb_windows_command(command, &nameCapacity);
  line[0] = '\0';
  torb_append_windows_argument(name, &line, &filled, &lineCapacity);
  torb_raw_free(name, nameCapacity);
  for (index = 0u; index < count; index++) {
    torb_append_windows_argument(arguments[index], &line, &filled, &lineCapacity);
  }
  wideLine = torb_platform_wide(line, &wideCapacity);
  torb_raw_free(line, lineCapacity);
  if (wideLine == NULL) {
    *message = "the program could not be started";
    return false;
  }
  memset(&startup, 0, sizeof(startup));
  memset(&child, 0, sizeof(child));
  startup.cb = (DWORD)sizeof(startup);
  /* No application name, so Windows searches PATH and appends `.exe` to a name without an extension */
  if (!CreateProcessW(NULL, wideLine, NULL, NULL, TRUE, 0u, NULL, NULL, &startup, &child)) {
    *message = "the program could not be started";
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  torb_raw_free(wideLine, wideCapacity);
  WaitForSingleObject(child.hProcess, INFINITE);
  if (!GetExitCodeProcess(child.hProcess, &status)) {
    status = (DWORD)-1;
  }
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  *code = (int64_t)(int32_t)status;
  return true;
}

/* ------------------------------------------------------------------------------ a child with three pipes --- */

/*
 * `Process.start` (runtime/stream.c): a child process whose three streams are pipes this process holds the other ends
 * of, started through no shell. The child's ends are inheritable and ours are not, and ours are closed in the child's
 * place only once it has its own copies, so the end of a pipe is the end of what the child wrote.
 */
bool torb_platform_child_start(
  const char *command,
  const char **arguments,
  size_t count,
  void **process,
  void **input,
  void **output,
  void **errors,
  const char **message
) {
  size_t lineCapacity = 512u;
  size_t filled = 0u;
  char *line = (char *)torb_raw_allocate(lineCapacity);
  size_t wideCapacity = 0u;
  wchar_t *wideLine;
  size_t index;
  SECURITY_ATTRIBUTES inheritable;
  STARTUPINFOW startup;
  PROCESS_INFORMATION child;
  HANDLE childInput = NULL;
  HANDLE ourInput = NULL;
  HANDLE ourOutput = NULL;
  HANDLE childOutput = NULL;
  HANDLE ourErrors = NULL;
  HANDLE childErrors = NULL;
  size_t nameCapacity = 0u;
  char *name = torb_windows_command(command, &nameCapacity);
  line[0] = '\0';
  torb_append_windows_argument(name, &line, &filled, &lineCapacity);
  torb_raw_free(name, nameCapacity);
  for (index = 0u; index < count; index++) {
    torb_append_windows_argument(arguments[index], &line, &filled, &lineCapacity);
  }
  wideLine = torb_platform_wide(line, &wideCapacity);
  torb_raw_free(line, lineCapacity);
  if (wideLine == NULL) {
    *message = "the program could not be started";
    return false;
  }
  inheritable.nLength = (DWORD)sizeof(inheritable);
  inheritable.lpSecurityDescriptor = NULL;
  inheritable.bInheritHandle = TRUE;
  if (!CreatePipe(&childInput, &ourInput, &inheritable, 0u)) {
    *message = "the pipes of the child process could not be created";
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  if (!CreatePipe(&ourOutput, &childOutput, &inheritable, 0u)) {
    *message = "the pipes of the child process could not be created";
    CloseHandle(childInput);
    CloseHandle(ourInput);
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  if (!CreatePipe(&ourErrors, &childErrors, &inheritable, 0u)) {
    *message = "the pipes of the child process could not be created";
    CloseHandle(childInput);
    CloseHandle(ourInput);
    CloseHandle(ourOutput);
    CloseHandle(childOutput);
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  SetHandleInformation(ourInput, HANDLE_FLAG_INHERIT, 0u);
  SetHandleInformation(ourOutput, HANDLE_FLAG_INHERIT, 0u);
  SetHandleInformation(ourErrors, HANDLE_FLAG_INHERIT, 0u);
  memset(&startup, 0, sizeof(startup));
  memset(&child, 0, sizeof(child));
  startup.cb = (DWORD)sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = childInput;
  startup.hStdOutput = childOutput;
  startup.hStdError = childErrors;
  /* No application name, so Windows searches PATH and appends `.exe` to a name without an extension */
  if (!CreateProcessW(NULL, wideLine, NULL, NULL, TRUE, 0u, NULL, NULL, &startup, &child)) {
    *message = "the program could not be started";
    CloseHandle(childInput);
    CloseHandle(ourInput);
    CloseHandle(ourOutput);
    CloseHandle(childOutput);
    CloseHandle(ourErrors);
    CloseHandle(childErrors);
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  torb_raw_free(wideLine, wideCapacity);
  CloseHandle(childInput);
  CloseHandle(childOutput);
  CloseHandle(childErrors);
  CloseHandle(child.hThread);
  *process = (void *)child.hProcess;
  *input = (void *)ourInput;
  *output = (void *)ourOutput;
  *errors = (void *)ourErrors;
  return true;
}

int64_t torb_platform_pipe_read(void *pipe, uint8_t *buffer, size_t maximum) {
  DWORD read = 0u;
  if (!ReadFile((HANDLE)pipe, buffer, (DWORD)(maximum > 0x7FFFFFFFu ? 0x7FFFFFFFu : maximum), &read, NULL)) {
    const DWORD failure = GetLastError();
    /* The other end closed: the end of the stream, which is no failure */
    if (failure == ERROR_BROKEN_PIPE || failure == ERROR_HANDLE_EOF) {
      return 0;
    }
    return -(int64_t)failure;
  }
  return (int64_t)read;
}

int64_t torb_platform_pipe_write(void *pipe, const uint8_t *bytes, size_t length) {
  size_t done = 0u;
  while (done < length) {
    DWORD written = 0u;
    const size_t step = length - done > 0x7FFFFFFFu ? 0x7FFFFFFFu : length - done;
    if (!WriteFile((HANDLE)pipe, bytes + done, (DWORD)step, &written, NULL)) {
      return -(int64_t)GetLastError();
    }
    done += (size_t)written;
  }
  return (int64_t)done;
}

void torb_platform_pipe_close(void *pipe) {
  CloseHandle((HANDLE)pipe);
}

/* An anonymous pipe is a handle and no descriptor any poller of the IO core takes. */
int64_t torb_platform_pipe_descriptor(void *pipe) {
  (void)pipe;
  return -1;
}

bool torb_platform_child_wait(void *process, int64_t *code, int64_t *failure) {
  DWORD status = 0u;
  if (WaitForSingleObject((HANDLE)process, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess((HANDLE)process, &status)) {
    *failure = (int64_t)GetLastError();
    return false;
  }
  *code = (int64_t)(int32_t)status;
  return true;
}

void torb_platform_child_forget(void *process) {
  CloseHandle((HANDLE)process);
}

torb_text torb_platform_failure_text(int64_t code) {
  char buffer[512];
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code, 0u,
                                buffer, (DWORD)sizeof buffer, NULL);
  /* The system's message ends in a line break and often a full stop, which the message of an `IoError` has not */
  while (length > 0u && (buffer[length - 1u] == '\n' || buffer[length - 1u] == '\r' || buffer[length - 1u] == '.')) {
    length -= 1u;
  }
  if (length == 0u) {
    snprintf(buffer, sizeof buffer, "the operation failed with the system's code %lld", (long long)code);
    return torb_text_from_cstring(buffer);
  }
  buffer[length] = '\0';
  return torb_text_from_cstring(buffer);
}

#else

/** Where the bytes read from one descriptor go: a buffer that grows in doublings. Owned: freed with `torb_raw_free`. */
typedef struct torb_collected {
  uint8_t *bytes;
  size_t length;
  size_t capacity;
} torb_collected;

static void torb_collected_start(torb_collected *collected) {
  collected->capacity = 4096u;
  collected->bytes = (uint8_t *)torb_raw_allocate(collected->capacity);
  collected->length = 0u;
}

/** One read of what `descriptor` has. False at its end, and at a failure that is not an interruption. */
static bool torb_collect_some(int descriptor, torb_collected *collected) {
  ssize_t got;
  if (collected->length == collected->capacity) {
    uint8_t *grown = (uint8_t *)torb_raw_allocate(collected->capacity * 2u);
    memcpy(grown, collected->bytes, collected->length);
    torb_raw_free(collected->bytes, collected->capacity);
    collected->bytes = grown;
    collected->capacity *= 2u;
  }
  do {
    got = read(descriptor, collected->bytes + collected->length, collected->capacity - collected->length);
  } while (got < 0 && errno == EINTR);
  if (got <= 0) {
    return false;
  }
  collected->length += (size_t)got;
  return true;
}

/** Closes a descriptor that is still open and marks it closed. */
static void torb_close_descriptor(int *descriptor) {
  if (*descriptor >= 0) {
    close(*descriptor);
    *descriptor = -1;
  }
}

/**
 * The input of a child as a file: a temporary one, removed from the directory at once, with `bytes` in it and its
 * offset back at the start. The open descriptor keeps it alive until the child and this process have closed it. -1
 * with the reason in `*message` where it cannot be made.
 *
 * A file and not a pipe, because a child that ends without reading all of a pipe makes the next write of this process
 * a `SIGPIPE`, which ends the process - and a file is never full, so feeding it cannot wait for the child either.
 */
static int torb_input_file(const uint8_t *bytes, size_t length, const char **message) {
  char path[] = "/tmp/torb-input-XXXXXX";
  size_t written = 0u;
  const int file = mkstemp(path);
  if (file < 0) {
    *message = strerror(errno);
    return -1;
  }
  (void)unlink(path);
  while (written < length) {
    const ssize_t step = write(file, bytes + written, length - written);
    if (step < 0 && errno == EINTR) {
      continue;
    }
    if (step <= 0) {
      *message = "the input for the child process could not be written to a temporary file";
      close(file);
      return -1;
    }
    written += (size_t)step;
  }
  if (lseek(file, 0, SEEK_SET) != 0) {
    *message = strerror(errno);
    close(file);
    return -1;
  }
  return file;
}

/**
 * In the child between `fork` and `exec`: `from` becomes the descriptor `to`. Where the two are one already (this
 * process was started with that stream closed, and the pipe took its number) only the close-on-exec flag goes, which
 * `dup2` would otherwise have cleared. Async-signal-safe.
 */
static bool torb_child_descriptor(int from, int to) {
  if (from == to) {
    return fcntl(to, F_SETFD, 0) == 0;
  }
  return dup2(from, to) == to;
}

/**
 * A child process, run to its end, with its two output streams collected - through `fork` plus `execvp` and a pipe
 * each, and through **no shell at all**, like the Windows half above: the arguments are handed over as the array they
 * are, so nothing about them is interpreted and nothing has to be quoted.
 *
 * It went through `popen` - `/bin/sh -c` with every argument in single quotes - and that made the platforms disagree:
 * a program that was nowhere was the shell's exit code 127 and not a failure, which is the difference `findCompiler`
 * reads and the conformance suite asserts (`files`, `process-run-input`).
 *
 * A program that could not be started has to be told apart from one that ran and left with 127, and after `fork` the
 * child cannot answer in a return value any more. So it answers through a pipe that is closed on a successful `exec`:
 * bytes on it mean the `exec` failed and carry its `errno`, and end of file means the program is running.
 *
 * The two output streams are two pipes, read as they fill with `poll`, so a child that writes much to either one never
 * waits for a reader. `input` `NULL` hands the child this process's own standard input; otherwise it reads a temporary
 * file with the bytes in it (`torb_input_file`). Every descriptor made here is closed on `exec`, so a child that
 * another thread starts at the same moment keeps none of these pipes open.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  const uint8_t *input,
  size_t inputLength,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  uint8_t **errors,
  size_t *errorsLength,
  size_t *errorsCapacity,
  const char **message
) {
  const size_t argumentBytes = (count + 2u) * sizeof(char *);
  char **argumentValues;
  int inputFile = -1;
  int outputPipe[2] = { -1, -1 };
  int errorsPipe[2] = { -1, -1 };
  int report[2] = { -1, -1 };
  torb_collected outputs;
  torb_collected errorOutputs;
  pid_t child;
  int status = 0;
  int failed = 0;
  ssize_t told;
  size_t index;
  if (input != NULL) {
    inputFile = torb_input_file(input, inputLength, message);
    if (inputFile < 0) {
      return false;
    }
    (void)fcntl(inputFile, F_SETFD, FD_CLOEXEC);
  }
  if (pipe(outputPipe) != 0 || pipe(errorsPipe) != 0 || pipe(report) != 0) {
    *message = strerror(errno);
    torb_close_descriptor(&inputFile);
    for (index = 0u; index < 2u; index++) {
      torb_close_descriptor(&outputPipe[index]);
      torb_close_descriptor(&errorsPipe[index]);
      torb_close_descriptor(&report[index]);
    }
    return false;
  }
  for (index = 0u; index < 2u; index++) {
    (void)fcntl(outputPipe[index], F_SETFD, FD_CLOEXEC);
    (void)fcntl(errorsPipe[index], F_SETFD, FD_CLOEXEC);
    (void)fcntl(report[index], F_SETFD, FD_CLOEXEC);
  }
  argumentValues = (char **)torb_raw_allocate(argumentBytes);
  argumentValues[0] = (char *)command;
  for (index = 0u; index < count; index++) {
    argumentValues[index + 1u] = (char *)arguments[index];
  }
  argumentValues[count + 1u] = NULL;
  child = fork();
  if (child == 0) {
    if ((inputFile < 0 || torb_child_descriptor(inputFile, 0)) && torb_child_descriptor(outputPipe[1], 1)
        && torb_child_descriptor(errorsPipe[1], 2)) {
      torb_memory_restore_for_child();
      execvp(command, argumentValues);
    }
    failed = errno;
    (void)!write(report[1], &failed, sizeof(failed));
    _exit(127);
  }
  if (child < 0) {
    *message = strerror(errno);
  }
  torb_raw_free(argumentValues, argumentBytes);
  torb_close_descriptor(&inputFile);
  torb_close_descriptor(&outputPipe[1]);
  torb_close_descriptor(&errorsPipe[1]);
  torb_close_descriptor(&report[1]);
  if (child < 0) {
    torb_close_descriptor(&outputPipe[0]);
    torb_close_descriptor(&errorsPipe[0]);
    torb_close_descriptor(&report[0]);
    return false;
  }
  do {
    told = read(report[0], &failed, sizeof(failed));
  } while (told < 0 && errno == EINTR);
  torb_close_descriptor(&report[0]);
  torb_collected_start(&outputs);
  torb_collected_start(&errorOutputs);
  /* Both streams until both have ended; a child that could not be started has closed them already */
  while (outputPipe[0] >= 0 || errorsPipe[0] >= 0) {
    struct pollfd watched[2];
    watched[0].fd = outputPipe[0];
    watched[0].events = POLLIN;
    watched[0].revents = 0;
    watched[1].fd = errorsPipe[0];
    watched[1].events = POLLIN;
    watched[1].revents = 0;
    if (poll(watched, 2u, -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (watched[0].revents != 0 && !torb_collect_some(outputPipe[0], &outputs)) {
      torb_close_descriptor(&outputPipe[0]);
    }
    if (watched[1].revents != 0 && !torb_collect_some(errorsPipe[0], &errorOutputs)) {
      torb_close_descriptor(&errorsPipe[0]);
    }
  }
  torb_close_descriptor(&outputPipe[0]);
  torb_close_descriptor(&errorsPipe[0]);
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      *message = strerror(errno);
      torb_raw_free(outputs.bytes, outputs.capacity);
      torb_raw_free(errorOutputs.bytes, errorOutputs.capacity);
      return false;
    }
  }
  if (told == (ssize_t)sizeof(failed)) {
    *message = strerror(failed);
    torb_raw_free(outputs.bytes, outputs.capacity);
    torb_raw_free(errorOutputs.bytes, errorOutputs.capacity);
    return false;
  }
  /* The exit code is in the high byte of `wait`'s status, and a child killed by a signal has none at all */
  if (WIFEXITED(status)) {
    *code = (int64_t)WEXITSTATUS(status);
  } else {
    *code = (int64_t)(128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0));
  }
  *output = outputs.bytes;
  *length = outputs.length;
  *capacity = outputs.capacity;
  *errors = errorOutputs.bytes;
  *errorsLength = errorOutputs.length;
  *errorsCapacity = errorOutputs.capacity;
  return true;
}

/**
 * The same child process, run to its end with **this process's own three streams** instead of pipes, and through
 * no shell either. A program that could not be started is told apart from one that ran and left with 127 the same way
 * as above: through the pipe that a successful `exec` closes.
 */
bool torb_platform_run_inheriting(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  const char **message
) {
  const size_t argumentBytes = (count + 2u) * sizeof(char *);
  char **argumentValues = (char **)torb_raw_allocate(argumentBytes);
  int report[2];
  pid_t child;
  int status = 0;
  int failed = 0;
  ssize_t told;
  size_t index;
  argumentValues[0] = (char *)command;
  for (index = 0u; index < count; index++) {
    argumentValues[index + 1u] = (char *)arguments[index];
  }
  argumentValues[count + 1u] = NULL;
  if (pipe(report) != 0) {
    *message = strerror(errno);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  (void)fcntl(report[1], F_SETFD, FD_CLOEXEC);
  child = fork();
  if (child < 0) {
    *message = strerror(errno);
    close(report[0]);
    close(report[1]);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  if (child == 0) {
    close(report[0]);
    torb_memory_restore_for_child();
    execvp(command, argumentValues);
    failed = errno;
    (void)!write(report[1], &failed, sizeof(failed));
    _exit(127);
  }
  close(report[1]);
  torb_raw_free(argumentValues, argumentBytes);
  do {
    told = read(report[0], &failed, sizeof(failed));
  } while (told < 0 && errno == EINTR);
  close(report[0]);
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      *message = strerror(errno);
      return false;
    }
  }
  if (told == (ssize_t)sizeof(failed)) {
    *message = strerror(failed);
    return false;
  }
  /* The exit code is in the high byte of `wait`'s status, and a child killed by a signal has none at all */
  if (WIFEXITED(status)) {
    *code = (int64_t)WEXITSTATUS(status);
  } else {
    *code = (int64_t)(128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0));
  }
  return true;
}

/* ------------------------------------------------------------------------------ a child with three pipes --- */

/* A descriptor as the pointer the stream layer keeps: one more than the descriptor, so descriptor 0 is no `NULL`. */
static void *torb_descriptor_pointer(int descriptor) {
  return (void *)(intptr_t)(descriptor + 1);
}

static int torb_pointer_descriptor(void *pipe) {
  return (int)((intptr_t)pipe - 1);
}

/*
 * `Process.start` (runtime/stream.c): a child process whose three streams are pipes this process holds the other ends
 * of, through `fork` and `execvp` and no shell. A program that could not be started answers through a pipe that is
 * closed on a successful `exec`, as `torb_platform_run_inheriting` does. A write into the input of a child that is gone
 * must fail and not end this process, so `SIGPIPE` is ignored from the first child on.
 */
bool torb_platform_child_start(
  const char *command,
  const char **arguments,
  size_t count,
  void **process,
  void **input,
  void **output,
  void **errors,
  const char **message
) {
  const size_t argumentBytes = (count + 2u) * sizeof(char *);
  char **argumentValues = (char **)torb_raw_allocate(argumentBytes);
  int toChild[2];
  int fromChild[2];
  int errorsOfChild[2];
  int report[2];
  pid_t child;
  int failed = 0;
  ssize_t told;
  size_t index;
  argumentValues[0] = (char *)command;
  for (index = 0u; index < count; index++) {
    argumentValues[index + 1u] = (char *)arguments[index];
  }
  argumentValues[count + 1u] = NULL;
  (void)signal(SIGPIPE, SIG_IGN);
  if (pipe(toChild) != 0) {
    *message = strerror(errno);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  if (pipe(fromChild) != 0) {
    *message = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  if (pipe(errorsOfChild) != 0) {
    *message = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  if (pipe(report) != 0) {
    *message = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    close(errorsOfChild[0]);
    close(errorsOfChild[1]);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  (void)fcntl(toChild[1], F_SETFD, FD_CLOEXEC);
  (void)fcntl(fromChild[0], F_SETFD, FD_CLOEXEC);
  (void)fcntl(errorsOfChild[0], F_SETFD, FD_CLOEXEC);
  (void)fcntl(report[1], F_SETFD, FD_CLOEXEC);
  child = fork();
  if (child < 0) {
    *message = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    close(errorsOfChild[0]);
    close(errorsOfChild[1]);
    close(report[0]);
    close(report[1]);
    torb_raw_free(argumentValues, argumentBytes);
    return false;
  }
  if (child == 0) {
    close(report[0]);
    (void)dup2(toChild[0], 0);
    (void)dup2(fromChild[1], 1);
    (void)dup2(errorsOfChild[1], 2);
    close(toChild[0]);
    close(fromChild[1]);
    close(errorsOfChild[1]);
    (void)signal(SIGPIPE, SIG_DFL);
    torb_memory_restore_for_child();
    execvp(command, argumentValues);
    failed = errno;
    (void)!write(report[1], &failed, sizeof(failed));
    _exit(127);
  }
  close(report[1]);
  close(toChild[0]);
  close(fromChild[1]);
  close(errorsOfChild[1]);
  torb_raw_free(argumentValues, argumentBytes);
  do {
    told = read(report[0], &failed, sizeof(failed));
  } while (told < 0 && errno == EINTR);
  close(report[0]);
  if (told == (ssize_t)sizeof(failed)) {
    int status = 0;
    *message = strerror(failed);
    close(toChild[1]);
    close(fromChild[0]);
    close(errorsOfChild[0]);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return false;
  }
  *process = (void *)(intptr_t)child;
  *input = torb_descriptor_pointer(toChild[1]);
  *output = torb_descriptor_pointer(fromChild[0]);
  *errors = torb_descriptor_pointer(errorsOfChild[0]);
  return true;
}

int64_t torb_platform_pipe_read(void *pipe, uint8_t *buffer, size_t maximum) {
  ssize_t got;
  do {
    got = read(torb_pointer_descriptor(pipe), buffer, maximum);
  } while (got < 0 && errno == EINTR);
  return got < 0 ? -(int64_t)errno : (int64_t)got;
}

int64_t torb_platform_pipe_write(void *pipe, const uint8_t *bytes, size_t length) {
  size_t done = 0u;
  while (done < length) {
    const ssize_t written = write(torb_pointer_descriptor(pipe), bytes + done, length - done);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -(int64_t)errno;
    }
    done += (size_t)written;
  }
  return (int64_t)done;
}

void torb_platform_pipe_close(void *pipe) {
  close(torb_pointer_descriptor(pipe));
}

int64_t torb_platform_pipe_descriptor(void *pipe) {
  return (int64_t)torb_pointer_descriptor(pipe);
}

bool torb_platform_child_wait(void *process, int64_t *code, int64_t *failure) {
  int status = 0;
  while (waitpid((pid_t)(intptr_t)process, &status, 0) < 0) {
    if (errno != EINTR) {
      *failure = (int64_t)errno;
      return false;
    }
  }
  /* The exit code is in the high byte of `wait`'s status, and a child killed by a signal has none at all */
  if (WIFEXITED(status)) {
    *code = (int64_t)WEXITSTATUS(status);
  } else {
    *code = (int64_t)(128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0));
  }
  return true;
}

/* A child nobody waited for is collected whenever it ends, so it never stays a zombie of this process for long. */
void torb_platform_child_forget(void *process) {
  int status = 0;
  (void)waitpid((pid_t)(intptr_t)process, &status, WNOHANG);
}

torb_text torb_platform_failure_text(int64_t code) {
  return torb_text_from_cstring(strerror((int)code));
}

#endif

/* ====================================================================== threads, locks and waiting (7.7) ====== */

/*
 * What the worker pool of task.c stands on (runtime/include/torb_pool.h): a thread with a stack of a given size, a
 * mutex, a condition with a timeout on the monotonic clock, the number of processors, and the one pointer per thread
 * that says which worker it is. Slim reader/writer locks and condition variables on Windows (Vista and later), pthreads
 * everywhere else.
 */

#if defined(_WIN32)

unsigned long torb_worker_slot = TORB_NO_SLOT;

void torb_mutex_initialize(torb_mutex *mutex) {
  InitializeSRWLock((PSRWLOCK)&mutex->opaque);
}

void torb_mutex_lock(torb_mutex *mutex) {
  AcquireSRWLockExclusive((PSRWLOCK)&mutex->opaque);
}

void torb_mutex_unlock(torb_mutex *mutex) {
  ReleaseSRWLockExclusive((PSRWLOCK)&mutex->opaque);
}

void torb_mutex_destroy(torb_mutex *mutex) {
  (void)mutex;
}

void torb_condition_initialize(torb_condition *condition) {
  InitializeConditionVariable((PCONDITION_VARIABLE)&condition->opaque);
}

bool torb_condition_wait(torb_condition *condition, torb_mutex *mutex, int64_t nanoseconds) {
  DWORD milliseconds = INFINITE;
  if (nanoseconds >= 0) {
    int64_t rounded = nanoseconds / 1000000LL + (nanoseconds % 1000000LL != 0 ? 1 : 0);
    milliseconds = rounded >= (int64_t)0x7FFFFFFF ? (DWORD)0x7FFFFFFF : (DWORD)rounded;
  }
  if (SleepConditionVariableSRW((PCONDITION_VARIABLE)&condition->opaque, (PSRWLOCK)&mutex->opaque, milliseconds, 0)) {
    return true;
  }
  return GetLastError() != ERROR_TIMEOUT;
}

void torb_condition_signal(torb_condition *condition) {
  WakeConditionVariable((PCONDITION_VARIABLE)&condition->opaque);
}

void torb_condition_destroy(torb_condition *condition) {
  (void)condition;
}

typedef struct torb_thread_start_record {
  void (*body)(void *argument);
  void *argument;
} torb_thread_start_record;

static DWORD WINAPI torb_thread_entry(LPVOID parameter) {
  torb_thread_start_record record = *(torb_thread_start_record *)parameter;
  free(parameter);
  record.body(record.argument);
  return 0u;
}

bool torb_thread_start(torb_thread *thread, void (*body)(void *argument), void *argument, size_t stack_size) {
  /* A plain `malloc`: the record is the platform's and not a block of any worker's heap. */
  torb_thread_start_record *record = (torb_thread_start_record *)malloc(sizeof *record);
  HANDLE handle;
  if (record == NULL) {
    return false;
  }
  record->body = body;
  record->argument = argument;
  handle = CreateThread(NULL, stack_size, torb_thread_entry, record, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
  if (handle == NULL) {
    free(record);
    return false;
  }
  thread->handle = handle;
  return true;
}

void torb_thread_join(torb_thread *thread) {
  WaitForSingleObject((HANDLE)thread->handle, INFINITE);
  CloseHandle((HANDLE)thread->handle);
  thread->handle = NULL;
}

void torb_thread_yield(void) {
  SwitchToThread();
}

void torb_thread_exit(void) {
  ExitThread(0u);
}

uint32_t torb_platform_processor_count(void) {
  DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return count == 0u ? 1u : (uint32_t)count;
}

void *torb_platform_slot_value(void) {
  return TlsGetValue((DWORD)torb_worker_slot);
}

void torb_platform_set_slot_value(void *value) {
  TlsSetValue((DWORD)torb_worker_slot, value);
}

/* The slot is taken on the first call, which the pool makes on the main thread before a second thread exists. */
void torb_platform_set_worker(torb_worker *worker) {
  if (torb_worker_slot == TORB_NO_SLOT) {
    DWORD slot = TlsAlloc();
    if (slot == TLS_OUT_OF_INDEXES) {
      torb_panic_text("the operating system has no thread-local slot left for a worker", torb_location_unknown);
    }
    torb_worker_slot = (unsigned long)slot;
  }
  torb_platform_set_slot_value(worker);
}

#else

#  include <sched.h>

_Thread_local torb_worker *torb_thread_worker = NULL;

void torb_mutex_initialize(torb_mutex *mutex) {
  pthread_mutex_init(&mutex->mutex, NULL);
}

void torb_mutex_lock(torb_mutex *mutex) {
  pthread_mutex_lock(&mutex->mutex);
}

void torb_mutex_unlock(torb_mutex *mutex) {
  pthread_mutex_unlock(&mutex->mutex);
}

void torb_mutex_destroy(torb_mutex *mutex) {
  pthread_mutex_destroy(&mutex->mutex);
}

void torb_condition_initialize(torb_condition *condition) {
#  if defined(__APPLE__)
  pthread_cond_init(&condition->condition, NULL);
#  else
  pthread_condattr_t attributes;
  pthread_condattr_init(&attributes);
  pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
  pthread_cond_init(&condition->condition, &attributes);
  pthread_condattr_destroy(&attributes);
#  endif
}

bool torb_condition_wait(torb_condition *condition, torb_mutex *mutex, int64_t nanoseconds) {
  struct timespec until;
  if (nanoseconds < 0) {
    pthread_cond_wait(&condition->condition, &mutex->mutex);
    return true;
  }
#  if defined(__APPLE__)
  until.tv_sec = (time_t)(nanoseconds / 1000000000LL);
  until.tv_nsec = (long)(nanoseconds % 1000000000LL);
  return pthread_cond_timedwait_relative_np(&condition->condition, &mutex->mutex, &until) != ETIMEDOUT;
#  else
  {
    int64_t deadline = torb_platform_monotonic_nanoseconds();
    deadline = nanoseconds > INT64_MAX - deadline ? INT64_MAX : deadline + nanoseconds;
    until.tv_sec = (time_t)(deadline / 1000000000LL);
    until.tv_nsec = (long)(deadline % 1000000000LL);
    return pthread_cond_timedwait(&condition->condition, &mutex->mutex, &until) != ETIMEDOUT;
  }
#  endif
}

void torb_condition_signal(torb_condition *condition) {
  pthread_cond_signal(&condition->condition);
}

void torb_condition_destroy(torb_condition *condition) {
  pthread_cond_destroy(&condition->condition);
}

typedef struct torb_thread_start_record {
  void (*body)(void *argument);
  void *argument;
} torb_thread_start_record;

static void *torb_thread_entry(void *parameter) {
  torb_thread_start_record record = *(torb_thread_start_record *)parameter;
  free(parameter);
  record.body(record.argument);
  return NULL;
}

bool torb_thread_start(torb_thread *thread, void (*body)(void *argument), void *argument, size_t stack_size) {
  torb_thread_start_record *record = (torb_thread_start_record *)malloc(sizeof *record);
  pthread_attr_t attributes;
  int failed;
  if (record == NULL) {
    return false;
  }
  record->body = body;
  record->argument = argument;
  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, stack_size);
  failed = pthread_create(&thread->thread, &attributes, torb_thread_entry, record);
  pthread_attr_destroy(&attributes);
  if (failed != 0) {
    free(record);
    return false;
  }
  return true;
}

void torb_thread_join(torb_thread *thread) {
  pthread_join(thread->thread, NULL);
}

void torb_thread_yield(void) {
  sched_yield();
}

void torb_thread_exit(void) {
  pthread_exit(NULL);
}

uint32_t torb_platform_processor_count(void) {
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count < 1 ? 1u : (uint32_t)count;
}

void torb_platform_set_worker(torb_worker *worker) {
  torb_thread_worker = worker;
}

#endif
