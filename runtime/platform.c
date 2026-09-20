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
 * A child process, run to its end, with its two output streams collected.
 *
 * `popen` is what both platforms have, and it gives **one** stream - so the standard error of the child is redirected
 * into the same pipe and both come back as one text. That is a real limitation and it is the one `torb build` can live
 * with: what it needs to tell apart is "there is no C compiler" (no process at all) from "the C compiler said no" (an
 * exit code plus its message), and it prints the message either way. `Process.start` with three real pipes is 7.3's, and
 * it is what a program that has to keep them apart waits for.
 *
 * The arguments are **quoted** here and nowhere else: `popen` takes a command line and not a list, so a path with a
 * space in it (`C:/Program Files/LLVM/bin/clang.exe`) has to survive the shell that runs it. Nothing else about them is
 * interpreted - that is what "there is no shell" in `std/process` promises - so a quote inside an argument is escaped and
 * everything else is passed through.
 */
static void torb_quote_argument(const char *argument, char **into, size_t *filled, size_t *capacity) {
  size_t index;
  const size_t length = strlen(argument);
  /* Two quotes, a backslash before every quote of the argument, and one space in front of it */
  const size_t needed = *filled + length * 2u + 4u;
  if (needed > *capacity) {
    size_t grown = *capacity * 2u;
    char *buffer;
    while (grown < needed) {
      grown *= 2u;
    }
    buffer = (char *)torb_raw_allocate(grown);
    memcpy(buffer, *into, *filled);
    torb_raw_free(*into, *capacity);
    *into = buffer;
    *capacity = grown;
  }
  (*into)[(*filled)++] = ' ';
  (*into)[(*filled)++] = '"';
  for (index = 0u; index < length; index++) {
    if (argument[index] == '"' || argument[index] == '\\') {
      (*into)[(*filled)++] = '\\';
    }
    (*into)[(*filled)++] = argument[index];
  }
  (*into)[(*filled)++] = '"';
  (*into)[*filled] = '\0';
}

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
  /* Both streams into one pipe: `popen` has one, and 7.3's `Process.start` is what keeps them apart */
  {
    const char *tail = " 2>&1";
    const size_t needed = filled + strlen(tail) + 1u;
    if (needed > lineCapacity) {
      char *grown = (char *)torb_raw_allocate(needed);
      memcpy(grown, line, filled + 1u);
      torb_raw_free(line, lineCapacity);
      line = grown;
      lineCapacity = needed;
    }
    memcpy(line + filled, tail, strlen(tail) + 1u);
    filled += strlen(tail);
  }
#if defined(_WIN32)
  pipe = _popen(line, "rb");
#else
  pipe = popen(line, "r");
#endif
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
#if defined(_WIN32)
  status = _pclose(pipe);
#else
  status = pclose(pipe);
  if (status != -1) {
    /* The exit code is in the high byte of `wait`'s status, and a child killed by a signal has none at all */
    if (WIFEXITED(status)) {
      status = WEXITSTATUS(status);
    } else {
      status = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
  }
#endif
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
