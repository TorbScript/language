/*
 * platform.c - the only file in the runtime with an `#ifdef _WIN32`.
 *
 * Fourteen functions: what kind of thing a path is, the working directory, the entries of a directory, creating one
 * directory and everything above it, opening a file, removing one, reading and writing a whole file, running a child
 * process to its end, a monotonic clock reading, sleeping until a timer is due, the program's own arguments, and
 * reading and setting an environment variable. Everything above this file is portable, and what it hands out and takes in is always UTF-8.
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

/* The POSIX half calls POSIX 2008 (`clock_gettime`, `nanosleep`, `popen`, `setenv`), which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "torb.h"

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
#  include <direct.h>
#  include <io.h>
#  include <wchar.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
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
 * UTF-8 spelling, and a `String` is always valid UTF-8, so there is no value for such a name (`docs/PATH.md`).
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
 * the `/` form of `File.absolutePath` (`docs/PATH.md`, section 6).
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

bool torb_platform_remove(const char *path) {
  size_t capacity = 0u;
  wchar_t *wide = torb_platform_system_path(path, &capacity);
  DWORD attributes;
  bool removed;
  if (wide == NULL) {
    return false;
  }
  attributes = GetFileAttributesW(wide);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    torb_raw_free(wide, capacity);
    return false;
  }
  /* Two calls, because a directory is not a file to this platform: `DeleteFileW` refuses one */
  removed = (attributes & (DWORD)FILE_ATTRIBUTE_DIRECTORY) != 0u ? RemoveDirectoryW(wide) != 0 : DeleteFileW(wide) != 0;
  torb_raw_free(wide, capacity);
  return removed;
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

bool torb_platform_list_directory(const char *path, torb_list *out, const char **message) {
  DIR *directory = opendir(path);
  struct dirent *entry;
  if (directory == NULL) {
    *message = strerror(errno);
    return false;
  }
  while ((entry = readdir(directory)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
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

bool torb_platform_remove(const char *path) {
  return remove(path) == 0;
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

bool torb_platform_set_environment_variable(const char *name, const char *value) {
  return setenv(name, value, 1) == 0;
}

/**
 * Linux names the executable in `/proc/self/exe`. Where that link is missing (macOS, the BSDs without procfs), the
 * answer is false rather than a guess from `argv[0]`, which names whatever the caller typed.
 */
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

/**
 * Room for `needed` more bytes plus the terminator, doubling. Both command line builders below grow the same way.
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

/* ================================================================================== running a child process ===== */

#if defined(_WIN32)

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
 * Both output streams still go into **one** pipe, because a `ProcessOutput` is what a caller gets and `Process.start`
 * with three real pipes is 7.3's.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  const char **message
) {
  size_t lineCapacity = 512u;
  size_t filled = 0u;
  char *line = (char *)torb_raw_allocate(lineCapacity);
  size_t wideCapacity = 0u;
  wchar_t *wideLine;
  size_t index;
  size_t outputCapacity = 65536u;
  size_t outputFilled = 0u;
  uint8_t *buffer;
  SECURITY_ATTRIBUTES inheritable;
  STARTUPINFOW startup;
  PROCESS_INFORMATION child;
  HANDLE readEnd = NULL;
  HANDLE writeEnd = NULL;
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
  startup.hStdOutput = writeEnd;
  startup.hStdError = writeEnd;
  /* No application name, so Windows searches PATH and appends `.exe` to a name without an extension */
  if (!CreateProcessW(NULL, wideLine, NULL, NULL, TRUE, 0u, NULL, NULL, &startup, &child)) {
    *message = "the program could not be started";
    CloseHandle(readEnd);
    CloseHandle(writeEnd);
    torb_raw_free(wideLine, wideCapacity);
    return false;
  }
  torb_raw_free(wideLine, wideCapacity);
  /* And our copy of the write end has to go too, for the same reason */
  CloseHandle(writeEnd);
  buffer = (uint8_t *)torb_raw_allocate(outputCapacity);
  for (;;) {
    DWORD read = 0u;
    if (outputFilled == outputCapacity) {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(outputCapacity * 2u);
      memcpy(grown, buffer, outputFilled);
      torb_raw_free(buffer, outputCapacity);
      buffer = grown;
      outputCapacity *= 2u;
    }
    if (!ReadFile(readEnd, buffer + outputFilled, (DWORD)(outputCapacity - outputFilled), &read, NULL) || read == 0u) {
      break;
    }
    outputFilled += (size_t)read;
  }
  CloseHandle(readEnd);
  WaitForSingleObject(child.hProcess, INFINITE);
  if (!GetExitCodeProcess(child.hProcess, &status)) {
    status = (DWORD)-1;
  }
  CloseHandle(child.hProcess);
  CloseHandle(child.hThread);
  *code = (int64_t)(int32_t)status;
  *output = buffer;
  *length = outputFilled;
  *capacity = outputCapacity;
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

#else

/**
 * One argument for `/bin/sh`, in single quotes, which is the one quoting a POSIX shell does not interpret at all. A
 * single quote inside the argument ends the run and is written as `'\''`.
 */
static void torb_quote_argument(const char *argument, char **into, size_t *filled, size_t *capacity) {
  size_t index;
  const size_t length = strlen(argument);
  /* Two quotes, four bytes for every quote of the argument, and one space in front of it */
  torb_reserve_line(length * 4u + 4u, into, *filled, capacity);
  (*into)[(*filled)++] = ' ';
  (*into)[(*filled)++] = '\'';
  for (index = 0u; index < length; index++) {
    if (argument[index] == '\'') {
      (*into)[(*filled)++] = '\'';
      (*into)[(*filled)++] = '\\';
      (*into)[(*filled)++] = '\'';
      (*into)[(*filled)++] = '\'';
      continue;
    }
    (*into)[(*filled)++] = argument[index];
  }
  (*into)[(*filled)++] = '\'';
  (*into)[*filled] = '\0';
}

/**
 * The same, through `popen`, which is `/bin/sh -c`. A shell here is a compromise the Windows half no longer makes: a
 * `fork` plus `execvp` would keep the promise of `std/process` exactly, and single quotes keep it in practice, because
 * nothing inside them is interpreted. It is `Process.start`'s job (7.3) to make both platforms shell free.
 *
 * Both output streams go into one pipe: `popen` has one, and a `ProcessOutput` is what a caller gets.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  const char **message
) {
  size_t lineCapacity = 512u;
  size_t filled = 0u;
  char *line = (char *)torb_raw_allocate(lineCapacity);
  size_t index;
  FILE *pipe;
  size_t outputCapacity = 65536u;
  size_t outputFilled = 0u;
  uint8_t *buffer;
  int status;
  line[0] = '\0';
  torb_quote_argument(command, &line, &filled, &lineCapacity);
  for (index = 0u; index < count; index++) {
    torb_quote_argument(arguments[index], &line, &filled, &lineCapacity);
  }
  {
    const char *tail = " 2>&1";
    torb_reserve_line(strlen(tail), &line, filled, &lineCapacity);
    memcpy(line + filled, tail, strlen(tail) + 1u);
    filled += strlen(tail);
  }
  pipe = popen(line, "r");
  if (pipe == NULL) {
    *message = strerror(errno);
    torb_raw_free(line, lineCapacity);
    return false;
  }
  buffer = (uint8_t *)torb_raw_allocate(outputCapacity);
  for (;;) {
    size_t read = fread(buffer + outputFilled, 1u, outputCapacity - outputFilled, pipe);
    outputFilled += read;
    if (outputFilled < outputCapacity) {
      break;
    }
    {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(outputCapacity * 2u);
      memcpy(grown, buffer, outputFilled);
      torb_raw_free(buffer, outputCapacity);
      buffer = grown;
      outputCapacity *= 2u;
    }
  }
  status = pclose(pipe);
  if (status != -1) {
    /* The exit code is in the high byte of `wait`'s status, and a child killed by a signal has none at all */
    if (WIFEXITED(status)) {
      status = WEXITSTATUS(status);
    } else {
      status = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
  }
  torb_raw_free(line, lineCapacity);
  if (status == -1) {
    *message = strerror(errno);
    torb_raw_free(buffer, outputCapacity);
    return false;
  }
  *code = (int64_t)status;
  *output = buffer;
  *length = outputFilled;
  *capacity = outputCapacity;
  return true;
}

/**
 * The same child process, run to its end with **this process's own three streams** instead of a pipe, and through
 * **no shell at all**: `fork` plus `execvp` hands the arguments over as the array they are, so nothing about them is
 * interpreted and nothing has to be quoted. That is the promise `std/process` makes and the one the collecting half
 * above still keeps only in practice.
 *
 * A program that could not be started has to be told apart from one that ran and left with 127, and after `fork` the
 * child cannot answer in a return value any more. So it answers through a pipe that is closed on a successful `exec`:
 * bytes on it mean the `exec` failed and carry its `errno`, and end of file means the program is running.
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

#endif
