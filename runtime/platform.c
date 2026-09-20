/*
 * platform.c - the only file in the runtime with an `#ifdef _WIN32`.
 *
 * Nine functions: what kind of thing a path is, the working directory, the entries of a directory, creating a
 * directory and everything above it, reading and writing a whole file, running a child process to its end, a monotonic
 * clock reading, and setting an environment variable (for `runtime/tests` only - no native ever sets one). Everything
 * above this file is portable.
 */

#include "torb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  include <sys/stat.h>
#  define TORB_STAT struct _stat
#  define TORB_STAT_CALL _stat
#  define TORB_DIRECTORY_BIT _S_IFDIR
#  define TORB_FILE_TYPE_MASK _S_IFMT
#  define TORB_GET_WORKING_DIRECTORY _getcwd
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
#  define TORB_STAT struct stat
#  define TORB_STAT_CALL stat
#  define TORB_DIRECTORY_BIT S_IFDIR
#  define TORB_FILE_TYPE_MASK S_IFMT
#  define TORB_GET_WORKING_DIRECTORY getcwd
#endif

torb_path_kind torb_platform_path_kind(const char *path) {
  TORB_STAT information;
  if (TORB_STAT_CALL(path, &information) != 0) {
    return TORB_PATH_MISSING;
  }
  if ((information.st_mode & (unsigned)TORB_FILE_TYPE_MASK) == (unsigned)TORB_DIRECTORY_BIT) {
    return TORB_PATH_DIRECTORY;
  }
  return TORB_PATH_FILE;
}

char *torb_platform_working_directory(size_t *length) {
  size_t capacity = 512u;
  for (;;) {
    char *buffer = (char *)torb_raw_allocate(capacity);
    if (TORB_GET_WORKING_DIRECTORY(buffer, (int)capacity) != NULL) {
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

#if defined(_WIN32)

bool torb_platform_list_directory(const char *path, torb_list *out, const char **message) {
  size_t length = strlen(path);
  size_t capacity = length + 3u;
  char *pattern = (char *)torb_raw_allocate(capacity);
  struct _finddata_t entry;
  intptr_t handle;
  memcpy(pattern, path, length);
  pattern[length] = '\\';
  pattern[length + 1u] = '*';
  pattern[length + 2u] = '\0';
  handle = _findfirst(pattern, &entry);
  torb_raw_free(pattern, capacity);
  if (handle == -1) {
    *message = strerror(errno);
    return false;
  }
  do {
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
      continue;
    }
    {
      torb_text name = torb_text_from_cstring(entry.name);
      torb_list_add(out, &name);
    }
  } while (_findnext(handle, &entry) == 0);
  _findclose(handle);
  return true;
}

#else

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

#endif

/**
 * `mkdir -p`: every directory of the path that is missing, and nothing where one is already there. The separator is
 * both `/` and `\` on Windows, because a path that came through `absolutePath` is all forward slashes and one a user
 * typed may not be.
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
#if defined(_WIN32)
        if (_mkdir(buffer) != 0 && torb_platform_path_kind(buffer) != TORB_PATH_DIRECTORY) {
#else
        if (mkdir(buffer, 0777) != 0 && torb_platform_path_kind(buffer) != TORB_PATH_DIRECTORY) {
#endif
          *message = strerror(errno);
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
  FILE *file = fopen(path, "rb");
  size_t capacity = 65536u;
  size_t filled = 0u;
  uint8_t *buffer;
  if (file == NULL) {
    *message = strerror(errno);
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
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    *message = strerror(errno);
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
 * A child process, run to its end, with its two output streams collected - through `CreateProcess` and a pipe, and
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
  size_t index;
  size_t outputCapacity = 65536u;
  size_t outputFilled = 0u;
  uint8_t *buffer;
  SECURITY_ATTRIBUTES inheritable;
  STARTUPINFOA startup;
  PROCESS_INFORMATION child;
  HANDLE readEnd = NULL;
  HANDLE writeEnd = NULL;
  DWORD status = 0u;
  line[0] = '\0';
  torb_append_windows_argument(command, &line, &filled, &lineCapacity);
  for (index = 0u; index < count; index++) {
    torb_append_windows_argument(arguments[index], &line, &filled, &lineCapacity);
  }
  inheritable.nLength = (DWORD)sizeof(inheritable);
  inheritable.lpSecurityDescriptor = NULL;
  inheritable.bInheritHandle = TRUE;
  if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0u)) {
    *message = "the pipe for the output of the child process could not be created";
    torb_raw_free(line, lineCapacity);
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
  if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0u, NULL, NULL, &startup, &child)) {
    *message = "the program could not be started";
    CloseHandle(readEnd);
    CloseHandle(writeEnd);
    torb_raw_free(line, lineCapacity);
    return false;
  }
  torb_raw_free(line, lineCapacity);
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

#endif

#if defined(_WIN32)

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

bool torb_platform_set_environment_variable(const char *name, const char *value) {
  return _putenv_s(name, value) == 0;
}

#else

int64_t torb_platform_monotonic_nanoseconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

bool torb_platform_set_environment_variable(const char *name, const char *value) {
  return setenv(name, value, 1) == 0;
}

#endif
