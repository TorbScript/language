/*
 * sandbox.c - the second lock of a sandboxed receiver script (docs/design/SCRIPTS.md section 4).
 *
 * The VM opens a sandbox around the call of a script and closes it afterwards (the kernel operations `SandboxOpen` and
 * `SandboxClose` of machine.c). While one is open:
 *
 *   - every path a file function of the runtime is handed goes through `torb_sandbox_path`: a relative path is read
 *     against the base directory, the result is normalized, it has to lie inside a root of the right side, and no
 *     component below that root may be a symbolic link;
 *   - `Environment.get` answers only for a name a pattern of the grant matches (`torb_sandbox_allows_variable`);
 *   - `torb_allocate` and `torb_raw_allocate` count their bytes against the memory limit;
 *   - `Process.exit` stops the script instead of the process.
 *
 * **A stop is a panic with a kind.** `torb_sandbox_stop` records why and panics; the recovery point the kernel sets
 * around every operation while a sandbox is open (`torb_machine_operate`) catches the panic, and the interpreter turns
 * it into the `SandboxError` of the script. A stop may only jump while such a point is active, which is what the guard
 * says: an allocation of the interpreter's own code between two operations only counts, and the next operation stops.
 *
 * The grant is text, one setting per line and a tab between the word and the value, written by
 * compiler/src/vm/sandbox.trb or by `SandboxCapabilities.grant` of std/sandbox: `base`, `read` and `write`
 * (directories), `variable` (a name, or a prefix followed by `*`), `memory` (bytes, 0 for no limit) and `unrestricted`
 * (any value: every path and every variable, which is the grant of an entry of `torb repl` - its sandbox is only the
 * recovery point that turns a panic and `Process.exit` into a stop, docs/design/REPL.md section 7); other words are the
 * interpreter's. A directory is made absolute against the working directory when the sandbox opens, and it and
 * every path of the script are normalized by the same rules as `normalizePath` of compiler/src/project/path.trb, so
 * the two compare as text.
 *
 * What the sandbox keeps for itself is `malloc`ed and never counted: it is the host's, not the script's.
 */

#include "torb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int torb_sandbox_active = 0;

typedef struct torb_sandbox_texts {
  char **items;
  size_t count;
} torb_sandbox_texts;

static char *torb_sandbox_base = NULL;
static torb_sandbox_texts torb_sandbox_reads = { NULL, 0u };
static torb_sandbox_texts torb_sandbox_writes = { NULL, 0u };
static torb_sandbox_texts torb_sandbox_patterns = { NULL, 0u };
static int64_t torb_sandbox_memory_limit = 0;
static int64_t torb_sandbox_allocated = 0;
static int torb_sandbox_guards = 0;
static int64_t torb_sandbox_stop_kind = 0;
static int torb_sandbox_unrestricted = 0;

static char *torb_sandbox_absolute(char *written);

/* ------------------------------------------------------------------------------------------------ the grant --- */

static char *torb_sandbox_copy(const char *bytes, size_t length) {
  char *copy = (char *)malloc(length + 1u);
  if (copy == NULL) {
    torb_panic_out_of_memory(length + 1u);
  }
  memcpy(copy, bytes, length);
  copy[length] = '\0';
  return copy;
}

static void torb_sandbox_append(torb_sandbox_texts *texts, const char *bytes, size_t length) {
  char **grown = (char **)realloc(texts->items, (texts->count + 1u) * sizeof(char *));
  if (grown == NULL) {
    torb_panic_out_of_memory((texts->count + 1u) * sizeof(char *));
  }
  texts->items = grown;
  texts->items[texts->count] = torb_sandbox_copy(bytes, length);
  texts->count += 1u;
}

static void torb_sandbox_clear(torb_sandbox_texts *texts) {
  for (size_t index = 0; index < texts->count; index++) {
    free(texts->items[index]);
  }
  free(texts->items);
  texts->items = NULL;
  texts->count = 0u;
}

static bool torb_sandbox_word_is(const char *line, size_t length, const char *word) {
  size_t size = strlen(word);
  return length > size && memcmp(line, word, size) == 0 && line[size] == '\t';
}

void torb_sandbox_open(const char *grant, size_t length) {
  size_t start = 0u;
  torb_sandbox_close();
  while (start < length) {
    size_t end = start;
    while (end < length && grant[end] != '\n') {
      end++;
    }
    {
      const char *line = grant + start;
      size_t size = end - start;
      if (torb_sandbox_word_is(line, size, "base")) {
        free(torb_sandbox_base);
        torb_sandbox_base = torb_sandbox_copy(line + 5, size - 5u);
      } else if (torb_sandbox_word_is(line, size, "read")) {
        torb_sandbox_append(&torb_sandbox_reads, line + 5, size - 5u);
      } else if (torb_sandbox_word_is(line, size, "write")) {
        torb_sandbox_append(&torb_sandbox_writes, line + 6, size - 6u);
      } else if (torb_sandbox_word_is(line, size, "variable")) {
        torb_sandbox_append(&torb_sandbox_patterns, line + 9, size - 9u);
      } else if (torb_sandbox_word_is(line, size, "unrestricted")) {
        torb_sandbox_unrestricted = 1;
      } else if (torb_sandbox_word_is(line, size, "memory")) {
        char *number = torb_sandbox_copy(line + 7, size - 7u);
        torb_sandbox_memory_limit = (int64_t)strtoll(number, NULL, 10);
        free(number);
      }
    }
    start = end + 1u;
  }
  /* A program's grant is written as the program sees paths, relative to where it runs; they are made absolute once */
  if (torb_sandbox_base == NULL) {
    torb_sandbox_base = torb_sandbox_copy(".", 1u);
  }
  torb_sandbox_base = torb_sandbox_absolute(torb_sandbox_base);
  for (size_t index = 0; index < torb_sandbox_reads.count; index++) {
    torb_sandbox_reads.items[index] = torb_sandbox_absolute(torb_sandbox_reads.items[index]);
  }
  for (size_t index = 0; index < torb_sandbox_writes.count; index++) {
    torb_sandbox_writes.items[index] = torb_sandbox_absolute(torb_sandbox_writes.items[index]);
  }
  torb_sandbox_allocated = 0;
  torb_sandbox_stop_kind = 0;
  torb_sandbox_guards = 0;
  torb_sandbox_active = 1;
}

void torb_sandbox_close(void) {
  torb_sandbox_active = 0;
  free(torb_sandbox_base);
  torb_sandbox_base = NULL;
  torb_sandbox_clear(&torb_sandbox_reads);
  torb_sandbox_clear(&torb_sandbox_writes);
  torb_sandbox_clear(&torb_sandbox_patterns);
  torb_sandbox_memory_limit = 0;
  torb_sandbox_allocated = 0;
  torb_sandbox_guards = 0;
  torb_sandbox_unrestricted = 0;
}

bool torb_sandbox_is_open(void) {
  return torb_sandbox_active != 0;
}

/* -------------------------------------------------------------------------------------------------- stopping --- */

void torb_sandbox_enter_guard(void) {
  torb_sandbox_guards += 1;
}

void torb_sandbox_leave_guard(void) {
  if (torb_sandbox_guards > 0) {
    torb_sandbox_guards -= 1;
  }
}

int64_t torb_sandbox_take_stop_kind(void) {
  int64_t kind = torb_sandbox_stop_kind;
  torb_sandbox_stop_kind = 0;
  return kind == 0 ? TORB_SANDBOX_PANIC : kind;
}

/** Records why the script stops and panics, which the kernel's recovery point catches. */
static TORB_NORETURN void torb_sandbox_stop(int64_t kind, const char *message) {
  torb_sandbox_stop_kind = kind;
  torb_panic_text(message, torb_location_unknown);
}

/* ---------------------------------------------------------------------------------------------------- memory --- */

void torb_sandbox_account(size_t size) {
  char message[128];
  if (torb_sandbox_memory_limit <= 0 || torb_sandbox_stop_kind != 0) {
    return;
  }
  torb_sandbox_allocated += (int64_t)size;
  if (torb_sandbox_allocated <= torb_sandbox_memory_limit || torb_sandbox_guards == 0) {
    return;
  }
  snprintf(message, sizeof message, "The script allocated more than %lld bytes", (long long)torb_sandbox_memory_limit);
  torb_sandbox_stop(TORB_SANDBOX_MEMORY, message);
}

void torb_sandbox_check_budget(void) {
  char message[128];
  if (torb_sandbox_memory_limit > 0 && torb_sandbox_allocated > torb_sandbox_memory_limit) {
    snprintf(message, sizeof message, "The script allocated more than %lld bytes",
             (long long)torb_sandbox_memory_limit);
    torb_sandbox_stop(TORB_SANDBOX_MEMORY, message);
  }
}

/* ------------------------------------------------------------------------------------------------------ exit --- */

void torb_sandbox_exit(int64_t code) {
  char message[128];
  snprintf(message, sizeof message, "The script called Process.exit(%lld)", (long long)code);
  torb_sandbox_stop(TORB_SANDBOX_EXIT, message);
}

/* ------------------------------------------------------------------------------------------------ environment --- */

bool torb_sandbox_allows_variable(const char *name) {
  if (torb_sandbox_active == 0 || torb_sandbox_unrestricted != 0) {
    return true;
  }
  for (size_t index = 0; index < torb_sandbox_patterns.count; index++) {
    const char *pattern = torb_sandbox_patterns.items[index];
    size_t length = strlen(pattern);
    if (length > 0u && pattern[length - 1u] == '*') {
      if (strncmp(name, pattern, length - 1u) == 0) {
        return true;
      }
    } else if (strcmp(name, pattern) == 0) {
      return true;
    }
  }
  return false;
}

/* ----------------------------------------------------------------------------------------------------- paths --- */

static bool torb_sandbox_is_absolute(const char *bytes, size_t length) {
  if (length >= 1u && (bytes[0] == '/' || bytes[0] == '\\')) {
    return true;
  }
  return length >= 2u && bytes[1] == ':' && ((bytes[0] >= 'A' && bytes[0] <= 'Z') || (bytes[0] >= 'a' && bytes[0] <= 'z'));
}

/**
 * `joined` normalized in place the way `normalizePath` does it: `\` is `/`, empty and `.` segments go, `..` removes the
 * segment before it. Answers false where a `..` would climb above the first segment, which no root can contain.
 */
static bool torb_sandbox_normalize(char *joined) {
  size_t length = strlen(joined);
  size_t written = 0u;
  size_t read = 0u;
  bool rooted;
  for (size_t index = 0; index < length; index++) {
    if (joined[index] == '\\') {
      joined[index] = '/';
    }
  }
  rooted = length > 0u && joined[0] == '/';
  if (rooted) {
    written = 1u;
    read = 1u;
  }
  while (read < length) {
    size_t end = read;
    size_t size;
    while (end < length && joined[end] != '/') {
      end++;
    }
    size = end - read;
    if (size == 0u || (size == 1u && joined[read] == '.')) {
      read = end + 1u;
      continue;
    }
    if (size == 2u && joined[read] == '.' && joined[read + 1u] == '.') {
      size_t floor = rooted ? 1u : 0u;
      if (written <= floor) {
        return false;
      }
      /* Back to the separator in front of the last segment, or to the root */
      while (written > floor && joined[written - 1u] != '/') {
        written--;
      }
      if (written > floor) {
        written--;
      }
      read = end + 1u;
      continue;
    }
    if (written > (rooted ? 1u : 0u)) {
      joined[written++] = '/';
    }
    memmove(joined + written, joined + read, size);
    written += size;
    read = end + 1u;
  }
  joined[written] = '\0';
  return true;
}

/**
 * A root or the base as the grant wrote it (owned, `malloc`ed), made absolute against the working directory and
 * normalized; the result replaces it. A root above the top of its disk grants nothing and becomes empty.
 */
static char *torb_sandbox_absolute(char *written) {
  size_t length = strlen(written);
  char *joined = written;
  if (!torb_sandbox_is_absolute(written, length)) {
    size_t working_length = 0u;
    char *working = torb_platform_working_directory(&working_length);
    if (working != NULL) {
      joined = (char *)malloc(working_length + 1u + length + 1u);
      if (joined == NULL) {
        torb_panic_out_of_memory(working_length + length + 2u);
      }
      memcpy(joined, working, working_length);
      joined[working_length] = '/';
      memcpy(joined + working_length + 1u, written, length + 1u);
      torb_raw_free(working, working_length + 1u);
      free(written);
    }
  }
  if (!torb_sandbox_normalize(joined)) {
    joined[0] = '\0';
  }
  return joined;
}

/** The root of `texts` the normalized path lies inside, or `NULL`. */
static const char *torb_sandbox_root_of(const torb_sandbox_texts *texts, const char *path) {
  size_t length = strlen(path);
  for (size_t index = 0; index < texts->count; index++) {
    const char *root = texts->items[index];
    size_t size = strlen(root);
    if (size == 0u || size > length || memcmp(path, root, size) != 0) {
      continue;
    }
    if (size == length || path[size] == '/' || root[size - 1u] == '/') {
      return root;
    }
  }
  return NULL;
}

/** `` `a` or `b` ``: the roots of one side, for a message. */
static void torb_sandbox_roots_text(char *buffer, size_t size, const torb_sandbox_texts *first,
                                    const torb_sandbox_texts *second) {
  size_t used = 0u;
  buffer[0] = '\0';
  for (int side = 0; side < 2; side++) {
    const torb_sandbox_texts *texts = side == 0 ? first : second;
    if (texts == NULL) {
      continue;
    }
    for (size_t index = 0; index < texts->count; index++) {
      int written = snprintf(buffer + used, size - used, "%s`%s`", used == 0u ? "" : " or ", texts->items[index]);
      if (written < 0 || (size_t)written >= size - used) {
        return;
      }
      used += (size_t)written;
    }
  }
}

static TORB_NORETURN void torb_sandbox_refuse_outside(const char *verb, const char *written, bool writes) {
  char roots[640];
  char message[1024];
  torb_sandbox_roots_text(roots, sizeof roots, &torb_sandbox_writes, writes ? NULL : &torb_sandbox_reads);
  if (roots[0] == '\0') {
    snprintf(message, sizeof message, "The script may not %s `%s`: no directory is granted for %s", verb, written,
             writes ? "writing" : "reading");
  } else {
    snprintf(message, sizeof message, "The script may not %s `%s`: it is not inside %s", verb, written, roots);
  }
  torb_sandbox_stop(TORB_SANDBOX_REFUSED, message);
}

char *torb_sandbox_path(torb_text path, bool writes, size_t *capacity) {
  const char *bytes = path.length == 0u ? "" : (const char *)path.storage->data + path.offset;
  size_t length = (size_t)path.length;
  const char *verb = writes ? "write" : "read";
  char *written;
  char *joined;
  size_t base_length;
  const char *root;
  if (torb_sandbox_active == 0 || torb_sandbox_unrestricted != 0) {
    char *copy = (char *)torb_raw_allocate(length + 1u);
    if (length > 0u) {
      memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    *capacity = length + 1u;
    return copy;
  }
  written = torb_sandbox_copy(bytes, length);
  base_length = torb_sandbox_base == NULL ? 0u : strlen(torb_sandbox_base);
  if (torb_sandbox_is_absolute(bytes, length) || base_length == 0u) {
    joined = torb_sandbox_copy(bytes, length);
  } else {
    joined = (char *)malloc(base_length + 1u + length + 1u);
    if (joined == NULL) {
      torb_panic_out_of_memory(base_length + length + 2u);
    }
    memcpy(joined, torb_sandbox_base, base_length);
    joined[base_length] = '/';
    memcpy(joined + base_length + 1u, bytes, length);
    joined[base_length + 1u + length] = '\0';
  }
  if (!torb_sandbox_normalize(joined)) {
    free(joined);
    {
      /* The message names the path as the script wrote it, so it is copied onto the stack before the jump */
      char kept[512];
      snprintf(kept, sizeof kept, "%s", written);
      free(written);
      torb_sandbox_refuse_outside(verb, kept, writes);
    }
  }
  root = torb_sandbox_root_of(&torb_sandbox_writes, joined);
  if (root == NULL && !writes) {
    root = torb_sandbox_root_of(&torb_sandbox_reads, joined);
  }
  if (root == NULL) {
    char kept[512];
    snprintf(kept, sizeof kept, "%s", written);
    free(written);
    free(joined);
    torb_sandbox_refuse_outside(verb, kept, writes);
  }
  /* Every component below the root that exists must not lead somewhere else */
  {
    size_t total = strlen(joined);
    size_t at = strlen(root);
    while (at < total) {
      size_t end = at;
      if (joined[end] == '/') {
        end++;
      }
      while (end < total && joined[end] != '/') {
        end++;
      }
      {
        char kept = joined[end];
        joined[end] = '\0';
        if (torb_platform_is_link(joined)) {
          char message[1024];
          const char *below = joined + strlen(root);
          while (*below == '/') {
            below++;
          }
          snprintf(message, sizeof message, "The script may not %s `%s`: `%s` is a symbolic link", verb, written,
                   below);
          free(written);
          free(joined);
          torb_sandbox_stop(TORB_SANDBOX_REFUSED, message);
        }
        joined[end] = kept;
      }
      at = end;
    }
  }
  free(written);
  {
    size_t size = strlen(joined);
    char *result = (char *)torb_raw_allocate(size + 1u);
    memcpy(result, joined, size + 1u);
    free(joined);
    *capacity = size + 1u;
    return result;
  }
}
