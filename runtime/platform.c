/*
 * platform.c - the only file in the runtime with an `#ifdef _WIN32`.
 *
 * Five functions: what kind of thing a path is, the working directory, the entries of a directory, and reading and
 * writing a whole file. Everything above this file is portable.
 */

#include "torb.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
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
