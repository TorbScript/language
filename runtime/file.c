/*
 * file.c - `std/fs`: `readText`, `writeText`, `readBytes`, `writeBytes`, `exists`, `isDirectory`, `list` (sorted),
 * `absolutePath`, the open handle (`File.open`/`readAll`/`close`) that lets a program read a file without holding it in
 * memory as one `readText`, and the tree around the contents: `remove`, `rename`, `copy`, `metadata`, `setPermissions`,
 * symbolic links, temporary files and directories, and the replacement of a file whole (`writeBytesAtomically`).
 *
 * Nothing here branches on the operating system: every function reaches the system through `runtime/platform.c`, which
 * holds both halves, so a program - the compiler among them - is the same C for every target.
 *
 * Every function answers false on failure and puts the message of the `IoError` into `*error` (owned): the operating
 * system's words alone, because the `path` of the `IoError` is the parameter of that name and its `show` writes the two
 * together. The `IoError` record and the `Result` around it are layouts of the program, so the lowering builds them - a
 * runtime function never constructs a type of the language.
 *
 * `readText` and `File.readAll` both validate UTF-8 and fail when the bytes are not: a `String` is always valid
 * UTF-8, so a file that is not is an `IoError` and never a replacement character (decided gap 7).
 *
 * Directory listings are sorted by bytes, always. The fixpoint test of the compiler depends on it.
 */

#include "torb.h"
#include "torb_pool.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/**
 * A NUL-terminated copy of a path, for reading (`writes` false) or for writing. Owned; free with
 * `torb_raw_free(buffer, *capacity)`. Inside a sandboxed script it is the path the sandbox resolved, or the script
 * stops here (`torb_sandbox_path`, docs/design/SCRIPTS.md section 4): every function of this file reaches a path through
 * this one, so none of them can forget the check.
 */
static char *torb_path_bytes(torb_text path, bool writes, size_t *capacity) {
  return torb_sandbox_path(path, writes, capacity);
}

/** `<path>: <message>`, which is what `IoError.show()` prints. Result owned. */
static torb_text torb_io_message(const char *message) {
  return torb_text_from_cstring(message);
}

bool torb_file_read_text(torb_text path, torb_text *out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  uint8_t *bytes = NULL;
  size_t length = 0u;
  size_t bad_offset = 0u;
  bool read = torb_platform_read_file(name, &bytes, &length, &message);
  torb_raw_free(name, capacity);
  if (!read) {
    *error = torb_io_message(message);
    return false;
  }
  if (!torb_text_try_from_bytes(bytes, length, out, &bad_offset)) {
    char detail[96];
    snprintf(detail, sizeof detail, "the byte at offset %lu is not valid UTF-8", (unsigned long)bad_offset);
    *error = torb_io_message(detail);
    torb_raw_free(bytes, length);
    return false;
  }
  torb_raw_free(bytes, length);
  return true;
}

bool torb_file_write_text(torb_text path, torb_text text, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  const uint8_t *bytes = text.length == 0u ? NULL : text.storage->data + text.offset;
  bool written = torb_platform_write_file(name, bytes, (size_t)text.length, &message);
  torb_raw_free(name, capacity);
  if (!written) {
    *error = torb_io_message(message);
    return false;
  }
  return true;
}

/**
 * `File.createDirectory`: the directory and every directory above it that is missing, and nothing where one is there.
 *
 * `torb build` has to create the directory it writes the C and the binary into, which is the reason this exists at all -
 * a caller that only wants a place to write should not have to ask first, so "it is already there" is success.
 */
bool torb_file_create_directory(torb_text path, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  bool created = torb_platform_create_directory(name, &message);
  torb_raw_free(name, capacity);
  if (!created) {
    *error = torb_io_message(message);
    return false;
  }
  return true;
}

bool torb_file_exists(torb_text path) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  torb_path_kind kind = torb_platform_path_kind(name);
  torb_raw_free(name, capacity);
  return kind != TORB_PATH_MISSING;
}

bool torb_file_is_directory(torb_text path) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  torb_path_kind kind = torb_platform_path_kind(name);
  torb_raw_free(name, capacity);
  return kind == TORB_PATH_DIRECTORY;
}

static int32_t torb_compare_text_elements(const void *first, const void *second, void *context) {
  (void)context;
  return torb_text_compare(*(const torb_text *)first, *(const torb_text *)second);
}

bool torb_file_list(torb_text path, torb_list *out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  torb_list entries = torb_list_new(&torb_element_text);
  bool listed = torb_platform_list_directory(name, &entries, &message);
  torb_raw_free(name, capacity);
  if (!listed) {
    torb_list_release(entries);
    *error = torb_io_message(message);
    return false;
  }
  torb_list_sort(&entries, torb_compare_text_elements, NULL);
  *out = entries;
  return true;
}

/* ------------------------------------------------------------------------------------------------ bytes --- */

bool torb_file_read_bytes(torb_text path, torb_list *into, torb_text *failure) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  uint8_t *bytes = NULL;
  size_t length = 0u;
  bool read = torb_platform_read_file(name, &bytes, &length, &message);
  torb_raw_free(name, capacity);
  if (!read) {
    *failure = torb_io_message(message);
    return false;
  }
  if (length > 0u) {
    torb_list_add_plain(into, bytes, length);
  }
  torb_raw_free(bytes, length);
  return true;
}

/*
 * The bytes of a list of `UInt8`, borrowed from it: one byte an element, in one piece. The declaration says `UInt8`, so
 * an element of another size is a defect of the lowering and never a failure of the program.
 */
static const uint8_t *torb_list_bytes(torb_list bytes, size_t *length) {
  *length = (size_t)torb_list_length(bytes);
  if (*length == 0u) {
    return NULL;
  }
  if (torb_list_element(bytes)->size != 1u) {
    torb_panic_text("internal error: the bytes of a file are not a list of UInt8", torb_location_unknown);
  }
  return (const uint8_t *)torb_list_at(bytes, 0, torb_location_unknown);
}

bool torb_file_write_bytes(torb_text path, torb_list bytes, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  size_t length = 0u;
  const uint8_t *data = torb_list_bytes(bytes, &length);
  bool written = torb_platform_write_file(name, data, length, &message);
  torb_raw_free(name, capacity);
  if (!written) {
    *error = torb_io_message(message);
    return false;
  }
  return true;
}

/* ------------------------------------------------------------------------------- temporary names, replacing --- */

/*
 * Twelve letters and digits, a new draw each call: SplitMix64 over a seed of the platform (the clock, the process) and a
 * counter, so two calls of one process in the same tick differ as well. A name is only ever a proposal - what makes it
 * the caller's is that the file is created where nothing was, and a name that is taken is drawn again.
 */
static void torb_random_name(char *into) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  static uint32_t counter = 0u;
  uint64_t state = torb_platform_unique_seed() ^ ((uint64_t)torb_atomic_add_u32(&counter, 1u) << 48);
  size_t index;
  for (index = 0u; index < 12u; index += 1u) {
    uint64_t mixed;
    state += UINT64_C(0x9E3779B97F4A7C15);
    mixed = state;
    mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94D049BB133111EB);
    mixed ^= mixed >> 31;
    into[index] = alphabet[mixed % 36u];
  }
  into[12] = '\0';
}

/*
 * A new entry `<directory>/<prefix><random>`, made where nothing was: a `FILE *` open for writing, or the token of a
 * directory. `*made` owned: the path it has. Tries another name while one is taken, and gives up after a hundred.
 */
static void *torb_create_unique(
  const char *directory,
  const char *prefix,
  bool as_directory,
  int64_t mode,
  char **made,
  size_t *made_capacity,
  const char **message
) {
  const size_t directory_length = strlen(directory);
  const size_t prefix_length = strlen(prefix);
  const size_t capacity = directory_length + 1u + prefix_length + 12u + 1u;
  int attempt;
  char *path = (char *)torb_raw_allocate(capacity);
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1u, prefix, prefix_length);
  for (attempt = 0; attempt < 100; attempt += 1) {
    bool exists = false;
    void *created;
    torb_random_name(path + directory_length + 1u + prefix_length);
    created = torb_platform_create_new(path, as_directory, mode, &exists, message);
    if (created != NULL) {
      *made = path;
      *made_capacity = capacity;
      return created;
    }
    if (!exists) {
      torb_raw_free(path, capacity);
      return NULL;
    }
  }
  *message = "no name that is not taken was found in a hundred tries";
  torb_raw_free(path, capacity);
  return NULL;
}

bool torb_file_create_temporary(torb_text path, torb_text prefix, bool directory, torb_text *out, torb_text *error) {
  size_t directory_capacity = 0u;
  char *inside;
  size_t prefix_capacity = (size_t)prefix.length + 1u;
  char *prefix_bytes;
  char *made = NULL;
  size_t made_capacity = 0u;
  const char *message = NULL;
  void *created;
  if (path.length == 0u) {
    size_t length = 0u;
    char *system = torb_platform_temporary_directory(&length);
    torb_text found;
    if (system == NULL) {
      *error = torb_io_message("the system names no directory for temporary files");
      return false;
    }
    found = torb_text_from_bytes((const uint8_t *)system, length, torb_location_unknown);
    torb_raw_free(system, length + 1u);
    inside = torb_path_bytes(found, true, &directory_capacity);
    torb_text_release(found);
  } else {
    inside = torb_path_bytes(path, true, &directory_capacity);
  }
  prefix_bytes = (char *)torb_raw_allocate(prefix_capacity);
  if (prefix.length > 0u) {
    memcpy(prefix_bytes, prefix.storage->data + prefix.offset, (size_t)prefix.length);
  }
  prefix_bytes[prefix.length] = '\0';
  /* A separator in the prefix would put the entry somewhere else than where the caller asked for it */
  if (strchr(prefix_bytes, '/') != NULL || strchr(prefix_bytes, '\\') != NULL) {
    torb_raw_free(prefix_bytes, prefix_capacity);
    torb_raw_free(inside, directory_capacity);
    *error = torb_io_message("the prefix of a temporary name may not contain a separator");
    return false;
  }
  /* Only the owner may read it: what a program keeps in a temporary file is often what nobody else should see */
  created = torb_create_unique(inside, prefix_bytes, directory, directory ? 0700 : 0600, &made, &made_capacity, &message);
  torb_raw_free(prefix_bytes, prefix_capacity);
  torb_raw_free(inside, directory_capacity);
  if (created == NULL) {
    *error = torb_io_message(message);
    return false;
  }
  if (!directory) {
    fclose((FILE *)created);
  }
  {
    size_t index;
    for (index = 0u; made[index] != '\0'; index += 1u) {
      if (made[index] == '\\') {
        made[index] = '/';
      }
    }
  }
  *out = torb_text_from_cstring(made);
  torb_raw_free(made, made_capacity);
  return true;
}

/* The directory a path is in: everything before its last separator, `.` where it has none. Owned. */
static char *torb_folder_of(const char *path, size_t *capacity) {
  size_t length = strlen(path);
  char *folder;
  while (length > 0u && path[length - 1u] != '/' && path[length - 1u] != '\\') {
    length -= 1u;
  }
  if (length == 0u) {
    *capacity = 2u;
    folder = (char *)torb_raw_allocate(*capacity);
    memcpy(folder, ".", 2u);
    return folder;
  }
  /* `/x` is in `/` and `C:/x` in `C:/`, so a root keeps its separator; everything else loses it */
  if (length > 1u && !(length == 3u && path[1] == ':')) {
    length -= 1u;
  }
  *capacity = length + 1u;
  folder = (char *)torb_raw_allocate(*capacity);
  memcpy(folder, path, length);
  folder[length] = '\0';
  return folder;
}

/* The name of a path: everything after its last separator. Borrowed from `path`. */
static const char *torb_name_of(const char *path) {
  const char *name = path;
  const char *cursor;
  for (cursor = path; *cursor != '\0'; cursor += 1) {
    if (*cursor == '/' || *cursor == '\\') {
      name = cursor + 1;
    }
  }
  return name;
}

bool torb_file_replace(torb_text path, torb_list bytes, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  size_t folder_capacity = 0u;
  char *folder = torb_folder_of(name, &folder_capacity);
  const char *base = torb_name_of(name);
  const size_t prefix_capacity = strlen(base) + 3u;
  char *prefix = (char *)torb_raw_allocate(prefix_capacity);
  char *staging = NULL;
  size_t staging_capacity = 0u;
  const char *message = NULL;
  size_t length = 0u;
  const uint8_t *data = torb_list_bytes(bytes, &length);
  torb_path_metadata before;
  const bool existed = torb_platform_metadata(name, true, &before, &message);
  FILE *file;
  bool ok = true;
  /* `.<name>.` - hidden where a leading dot hides, and beside the file, because a rename moves nothing across volumes */
  prefix[0] = '.';
  memcpy(prefix + 1, base, prefix_capacity - 3u);
  prefix[prefix_capacity - 2u] = '.';
  prefix[prefix_capacity - 1u] = '\0';
  file = (FILE *)torb_create_unique(folder, prefix, false, 0666, &staging, &staging_capacity, &message);
  torb_raw_free(prefix, prefix_capacity);
  if (file == NULL) {
    *error = torb_io_message(message);
    torb_raw_free(folder, folder_capacity);
    torb_raw_free(name, capacity);
    return false;
  }
  if (length > 0u && fwrite(data, 1u, length, file) != length) {
    message = strerror(errno);
    ok = false;
  }
  /* On the disk before the rename, or a crash right after it could leave the new name with nothing behind it */
  if (ok && !torb_platform_sync_file(file, &message)) {
    ok = false;
  }
  if (fclose(file) != 0 && ok) {
    message = strerror(errno);
    ok = false;
  }
  if (ok && existed && !torb_platform_set_mode(staging, before.mode, &message)) {
    ok = false;
  }
  if (ok && !torb_platform_rename(staging, name, &message)) {
    ok = false;
  }
  if (ok) {
    torb_platform_sync_directory(folder);
  } else {
    const char *ignored = NULL;
    (void)torb_platform_remove_entry(staging, &ignored);
    *error = torb_io_message(message);
  }
  torb_raw_free(staging, staging_capacity);
  torb_raw_free(folder, folder_capacity);
  torb_raw_free(name, capacity);
  return ok;
}

/* ----------------------------------------------------------------------- removing, renaming, copying, links --- */

bool torb_file_remove(torb_text path, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  bool removed = torb_platform_remove_entry(name, &message);
  torb_raw_free(name, capacity);
  if (!removed) {
    *error = torb_io_message(message);
  }
  return removed;
}

bool torb_file_rename(torb_text path, torb_text to, torb_text *error) {
  size_t from_capacity = 0u;
  size_t to_capacity = 0u;
  char *from_name = torb_path_bytes(path, true, &from_capacity);
  char *to_name = torb_path_bytes(to, true, &to_capacity);
  const char *message = NULL;
  bool moved = torb_platform_rename(from_name, to_name, &message);
  torb_raw_free(from_name, from_capacity);
  torb_raw_free(to_name, to_capacity);
  if (!moved) {
    *error = torb_io_message(message);
  }
  return moved;
}

bool torb_file_copy(torb_text path, torb_text to, torb_text *error) {
  size_t from_capacity = 0u;
  size_t to_capacity = 0u;
  char *from_name = torb_path_bytes(path, false, &from_capacity);
  char *to_name = torb_path_bytes(to, true, &to_capacity);
  const char *message = NULL;
  bool copied;
  /* Refused the same way everywhere: `CopyFileW` would say "access denied" about a directory, `read` "is a directory" */
  if (torb_platform_path_kind(from_name) == TORB_PATH_DIRECTORY) {
    message = "Is a directory";
    copied = false;
  } else {
    copied = torb_platform_copy_file(from_name, to_name, &message);
  }
  torb_raw_free(from_name, from_capacity);
  torb_raw_free(to_name, to_capacity);
  if (!copied) {
    *error = torb_io_message(message);
  }
  return copied;
}

bool torb_file_metadata(
  torb_text path,
  bool follow,
  int64_t *kind,
  int64_t *size,
  int64_t *modified,
  int64_t *mode,
  torb_text *failure
) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  torb_path_metadata metadata;
  bool found = torb_platform_metadata(name, follow, &metadata, &message);
  torb_raw_free(name, capacity);
  if (!found) {
    *failure = torb_io_message(message);
    return false;
  }
  *kind = metadata.kind;
  *size = metadata.size;
  *modified = metadata.modified;
  *mode = metadata.mode;
  return true;
}

bool torb_file_set_mode(torb_text path, int64_t mode, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  bool set = torb_platform_set_mode(name, mode, &message);
  torb_raw_free(name, capacity);
  if (!set) {
    *error = torb_io_message(message);
  }
  return set;
}

/*
 * The target is text the link stores and not a path this program reaches, so it is not the sandbox's to resolve: what
 * the sandbox checks is the place of the link, and what the link leads to is checked where it is followed - every
 * component of a path a script reaches is refused where it is a link (runtime/sandbox.c).
 */
bool torb_file_create_symbolic_link(torb_text path, torb_text target, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const size_t target_capacity = (size_t)target.length + 1u;
  char *target_bytes = (char *)torb_raw_allocate(target_capacity);
  const char *message = NULL;
  bool made;
  if (target.length > 0u) {
    memcpy(target_bytes, target.storage->data + target.offset, (size_t)target.length);
  }
  target_bytes[target.length] = '\0';
  made = torb_platform_create_symbolic_link(name, target_bytes, &message);
  torb_raw_free(target_bytes, target_capacity);
  torb_raw_free(name, capacity);
  if (!made) {
    *error = torb_io_message(message);
  }
  return made;
}

bool torb_file_symbolic_link_target(torb_text path, torb_text *out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  size_t length = 0u;
  char *target = torb_platform_link_target(name, &length, &message);
  torb_raw_free(name, capacity);
  if (target == NULL) {
    *error = torb_io_message(message);
    return false;
  }
  *out = torb_text_from_bytes((const uint8_t *)target, length, torb_location_unknown);
  torb_raw_free(target, length + 1u);
  return true;
}

/* ------------------------------------------------------------------------------------------- absolute paths --- */

static bool torb_path_is_absolute(const char *bytes, size_t length) {
  if (length >= 1u && (bytes[0] == '/' || bytes[0] == '\\')) {
    return true;
  }
  if (length >= 2u && bytes[1] == ':'
      && ((bytes[0] >= 'A' && bytes[0] <= 'Z') || (bytes[0] >= 'a' && bytes[0] <= 'z'))) {
    return true;
  }
  return false;
}

/**
 * Text arithmetic, not a lookup: `.` and `..` are resolved against the working directory, the file does not have to
 * exist and links are not followed. Separators come out as forward slashes, on every platform, so no path of a
 * machine ever differs between them beyond the drive letter.
 *
 * Inside a sandboxed script it is a read like any other: the path is resolved against the sandbox's base directory and
 * has to lie inside a root, because the working directory of the host is not the script's to learn.
 */
static bool torb_file_absolute_path_of(torb_text path, torb_text *out, torb_text *error);

bool torb_file_absolute_path(torb_text path, torb_text *out, torb_text *error) {
  if (torb_sandbox_active != 0) {
    size_t capacity = 0u;
    char *resolved = torb_path_bytes(path, false, &capacity);
    torb_text inside = torb_text_from_cstring(resolved);
    bool answered;
    torb_raw_free(resolved, capacity);
    /* Absolute now, so the text arithmetic below never reads the working directory */
    answered = torb_file_absolute_path_of(inside, out, error);
    torb_text_release(inside);
    return answered;
  }
  return torb_file_absolute_path_of(path, out, error);
}

static bool torb_file_absolute_path_of(torb_text path, torb_text *out, torb_text *error) {
  size_t joined_capacity;
  char *joined;
  size_t joined_length = 0u;
  char *result;
  size_t result_capacity;
  size_t result_length = 0u;
  uint32_t *starts;
  size_t start_capacity;
  size_t depth = 0u;
  size_t position;
  size_t root_length = 0u;

  if (torb_path_is_absolute(path.length == 0u ? "" : (const char *)(path.storage->data + path.offset),
                            (size_t)path.length)) {
    joined_capacity = (size_t)path.length + 1u;
    joined = (char *)torb_raw_allocate(joined_capacity);
    memcpy(joined, path.storage->data + path.offset, (size_t)path.length);
    joined_length = (size_t)path.length;
  } else {
    size_t working_length = 0u;
    char *working = torb_platform_working_directory(&working_length);
    if (working == NULL) {
      *error = torb_io_message("the working directory could not be read");
      return false;
    }
    joined_capacity = working_length + (size_t)path.length + 2u;
    joined = (char *)torb_raw_allocate(joined_capacity);
    memcpy(joined, working, working_length);
    joined_length = working_length;
    torb_raw_free(working, working_length + 1u);
    joined[joined_length] = '/';
    joined_length += 1u;
    if (path.length > 0u) {
      memcpy(joined + joined_length, path.storage->data + path.offset, (size_t)path.length);
      joined_length += (size_t)path.length;
    }
  }
  for (position = 0u; position < joined_length; position += 1u) {
    if (joined[position] == '\\') {
      joined[position] = '/';
    }
  }
  /* An extended-length path (`\\?\C:\x`, `\\?\UNC\server\share`) loses that prefix here: it is the form a *call* of
     the operating system takes and never a form a path is shown in, and one can arrive from anything that canonicalized
     a path on Windows. `UNC\` takes the place of the two separators of a share, so putting them back is what undoes it. */
  if (joined_length >= 4u && memcmp(joined, "//?/", 4u) == 0) {
    size_t skipped = 4u;
    if (joined_length >= 8u && memcmp(joined + 4u, "UNC/", 4u) == 0) {
      skipped = 6u;
      joined[6] = '/';
      joined[7] = '/';
    }
    joined_length -= skipped;
    memmove(joined, joined + skipped, joined_length);
  }

  result_capacity = joined_length + 2u;
  result = (char *)torb_raw_allocate(result_capacity);
  start_capacity = (joined_length + 2u) * sizeof(uint32_t);
  starts = (uint32_t *)torb_raw_allocate(start_capacity);

  position = 0u;
  if (joined_length >= 2u && joined[1] == ':') {
    result[0] = joined[0] >= 'a' && joined[0] <= 'z' ? (char)(joined[0] - ('a' - 'A')) : joined[0];
    result[1] = ':';
    result[2] = '/';
    result_length = 3u;
    /* `C:` alone ends here, and `C:foo` is read as `C:/foo` (docs/design/PATH.md section 2): past the colon only a separator
       that is really there is skipped. */
    position = joined_length >= 3u && joined[2] == '/' ? 3u : 2u;
  } else {
    result[0] = '/';
    result_length = 1u;
    while (position < joined_length && joined[position] == '/') {
      position += 1u;
    }
  }
  root_length = result_length;

  while (position < joined_length) {
    size_t start = position;
    size_t length;
    while (position < joined_length && joined[position] != '/') {
      position += 1u;
    }
    length = position - start;
    while (position < joined_length && joined[position] == '/') {
      position += 1u;
    }
    if (length == 0u || (length == 1u && joined[start] == '.')) {
      continue;
    }
    if (length == 2u && joined[start] == '.' && joined[start + 1u] == '.') {
      if (depth > 0u) {
        depth -= 1u;
        result_length = (size_t)starts[depth];
        if (result_length > root_length) {
          result_length -= 1u; /* Drop the separator this component was written after. */
        }
      }
      continue;
    }
    if (result_length > root_length) {
      result[result_length] = '/';
      result_length += 1u;
    }
    starts[depth] = (uint32_t)result_length;
    depth += 1u;
    memcpy(result + result_length, joined + start, length);
    result_length += length;
  }

  *out = torb_text_from_bytes((const uint8_t *)result, result_length, torb_location_unknown);
  torb_raw_free(starts, start_capacity);
  torb_raw_free(result, result_capacity);
  torb_raw_free(joined, joined_capacity);
  return true;
}

/* --------------------------------------------------------------------------------------------- open handles --- */

/**
 * `File.open`/`readAll`/`close`: the `torb_file` `shared type` of torb.h. `open` is read-only: nothing in `std/fs`
 * writes through an open handle, only `File.writeText` on a path.
 */

bool torb_file_open(torb_text path, torb_file **out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, false, &capacity);
  const char *message = NULL;
  /* Through the platform layer, so the path is handed to the operating system in that platform's own form */
  FILE *handle = (FILE *)torb_platform_open_file(name, false, &message);
  torb_raw_free(name, capacity);
  if (handle == NULL) {
    *error = torb_io_message(message);
    return false;
  }
  {
    torb_file *file = (torb_file *)torb_allocate(sizeof(torb_file), TORB_BLOCK_SHARED);
    file->path = torb_text_retained(path);
    file->handle = handle;
    file->pending = NULL;
    *out = file;
  }
  return true;
}

/* Creates or empties the file for writing, the counterpart of `torb_file_open` that `add` and `end` write through. */
bool torb_file_create(torb_text path, torb_file **out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, true, &capacity);
  const char *message = NULL;
  FILE *handle = (FILE *)torb_platform_open_file(name, true, &message);
  torb_raw_free(name, capacity);
  if (handle == NULL) {
    *error = torb_io_message(message);
    return false;
  }
  {
    torb_file *file = (torb_file *)torb_allocate(sizeof(torb_file), TORB_BLOCK_SHARED);
    file->path = torb_text_retained(path);
    file->handle = handle;
    file->pending = NULL;
    *out = file;
  }
  return true;
}

/* The path a stream's `IoError` names. Owned. */
torb_text torb_file_path(torb_file *file) {
  return torb_text_retained(file->path);
}

bool torb_file_read_all(torb_file **self, torb_text *out, torb_text *path, torb_text *error) {
  torb_file *file = *self;
  size_t capacity = 65536u;
  size_t filled = 0u;
  uint8_t *buffer;
  size_t bad_offset = 0u;
  if (file->handle == NULL) {
    *path = torb_text_retained(file->path);
    *error = torb_io_message("the file is already closed");
    return false;
  }
  buffer = (uint8_t *)torb_raw_allocate(capacity);
  for (;;) {
    size_t read = fread(buffer + filled, 1u, capacity - filled, (FILE *)file->handle);
    filled += read;
    if (filled < capacity) {
      if (ferror((FILE *)file->handle)) {
        *path = torb_text_retained(file->path);
        *error = torb_io_message(strerror(errno));
        torb_raw_free(buffer, capacity);
        return false;
      }
      break;
    }
    /* A full buffer past the limit of a text can never become one, and doubling it could wrap */
    if (capacity > (size_t)UINT32_MAX || capacity > SIZE_MAX / 2u) {
      torb_panic_text("a text longer than 4 GiB is not supported", torb_location_unknown);
    }
    {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
      memcpy(grown, buffer, filled);
      torb_raw_free(buffer, capacity);
      buffer = grown;
      capacity *= 2u;
    }
  }
  if (!torb_text_try_from_bytes(buffer, filled, out, &bad_offset)) {
    char detail[96];
    snprintf(detail, sizeof detail, "the byte at offset %lu is not valid UTF-8", (unsigned long)bad_offset);
    *path = torb_text_retained(file->path);
    *error = torb_io_message(detail);
    torb_raw_free(buffer, capacity);
    return false;
  }
  torb_raw_free(buffer, capacity);
  return true;
}

static void torb_file_close_handle(torb_file *file) {
  if (file->handle != NULL) {
    fclose((FILE *)file->handle);
    file->handle = NULL;
  }
}

void torb_file_close(torb_file **self) {
  torb_file_close_handle(*self);
}

void torb_file_drop(void *block) {
  torb_file *file = (torb_file *)block;
  torb_file_close_handle(file);
  torb_file_forget_pending(file);
  torb_text_release(file->path);
}
