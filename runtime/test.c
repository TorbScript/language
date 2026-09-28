/*
 * test.c - `test` and `group` of `std/test`.
 *
 *     group "Vector2" {
 *       test "adds component-wise" { ... }
 *     }
 *
 * writes one line per test to standard output, `  ok      Vector2 > adds component-wise` or `  FAILED  ` plus the
 * message and the site.
 *
 * Both are functions of the runtime, because the runtime can call a closure: a closure value's `code` points at a thunk
 * with an erased environment, so a cast and a call are all that is needed. What is not ordinary is the **recovery
 * point** a test needs - a body that panics has to be reported and the next test has to run - and that is
 * `torb_begin_recovery` in `panic.c`, which makes a panic land here instead of leaving the process.
 *
 * **A recovered panic runs nothing on the way out**, exactly as an ordinary panic runs nothing, so a run with a failed
 * test leaks what the aborted frames held and the leak gate does not apply to one.
 *
 * **A test owns the tasks it starts.** The body runs inside a scope of the pool (`torb_test_tasks_begin`), and the test
 * ends only once every task made in it has completed (`torb_test_tasks_end`), wherever the pool ran them. A panic in one
 * of them - on the main thread or on any other worker - is caught by a recovery point of the thread that runs it and
 * becomes this test's failure, with its message and site, exactly as a panic of the body itself; the test's other tasks
 * are cancelled, and the next test runs. A body that panics cancels the tasks it started and waits for them to stop.
 *
 * `torb test` over a directory is **one binary for every test file**, so the counts of the run and the summary line
 * live here as well: `torb_test_file` writes the name of the file whose tests come next and `torb_test_finish` writes
 * the blank line, `N passed, M failed (K files)`, and the exit code. A test file that fails does not stop the file
 * after it - that is what the recovery point is for - so the counts are what say whether the run was green.
 *
 * **The options of the run come off the command line of the process**, in the binary and in the VM inside `torb`
 * alike, read once (`torb_test_read_options`):
 *
 * - `--shard 2/4` runs the second of every four files, counted in the order they come, and the summary says which
 *   shard it was. A file of another shard is passed over whole - its name is not printed, and `test` and `group` return
 *   at once - and only the top-level code of its entry still runs, which in a test file declares constants.
 * - `--filter <name>`, any number of them, runs the tests whose full name (`Outer > Inner > name`) is one of the names
 *   or lies below one of them, and a `group` that is not on the way to any of them is passed over whole: its body does
 *   not run. Files still print their line, and the counts count what ran.
 * - `--report json` writes JSON Lines instead of the human report (`torb_test_json_*`): one object per line, an event
 *   of the run, flushed as it is written so that an editor reads each one while the suite runs.
 * - `--color auto|always|never` colours the human report (`torb_test_decide_colors`): `ok` green, `FAILED` bold red,
 *   the summary green without failures and bold with them. `auto` asks the environment and whether standard output is
 *   a terminal. Colour is always redundant - every line says its outcome in a word - and a plain report is the same
 *   bytes it was before the report had colours.
 */

#include "torb.h"

#include <stdio.h>
#include <string.h>

/** How deeply groups may nest. A suite that needs more than this is not organized, it is nested. */
#define TORB_TEST_GROUP_DEPTH 32
#define TORB_TEST_NAME_SIZE 1024

/** The names of the groups that are open, outermost first: what goes in front of a test's own name. */
static torb_text torb_test_groups[TORB_TEST_GROUP_DEPTH];
static uint32_t torb_test_depth = 0;

/**
 * What the summary line counts. They live here and not in the driver for the same reason the format of one line does:
 * one binary holds every test file of a run, and the two implementations of `torb test` print one summary.
 */
static int64_t torb_test_passed = 0;
static int64_t torb_test_failed = 0;
static int64_t torb_test_files = 0;

/**
 * The shard of the run, `--shard <index>/<count>` (torb.h, `torb_test_file`): the files whose position counted from 0
 * leaves `index - 1` divided by `count` run, and the others are passed over. A count of 0 is a run of every file, the
 * default.
 */
static int64_t torb_test_shard_index = 0;
static int64_t torb_test_shard_count = 0;
/** How many files came so far, of every shard, and whether the one whose tests come now is of this one. */
static int64_t torb_test_seen = 0;
static bool torb_test_selected = true;

/**
 * A text that grows as it is written, for what has no bound: a full name, a line of the JSON report. The bytes stay NUL
 * terminated. Owned by whoever made it, given back with `torb_test_buffer_release`.
 */
typedef struct torb_test_buffer {
  char *bytes;
  size_t length;
  size_t capacity;
} torb_test_buffer;

/** Whether the options are read, and what they said besides the shard. */
static bool torb_test_options_read = false;
static bool torb_test_reports_json = false;

/**
 * How the human report is coloured (docs/design/BRAND.md section 10): not at all, in the 16 colours of the terminal's
 * theme, or in 24 bits for a known light or dark ground. Decided once, with the options (`torb_test_decide_colors`).
 */
typedef enum torb_test_palette {
  TORB_TEST_PLAIN,
  TORB_TEST_SIXTEEN,
  TORB_TEST_LIGHT,
  TORB_TEST_DARK,
} torb_test_palette;
static torb_test_palette torb_test_colors = TORB_TEST_PLAIN;

/** What a coloured part of the report means: `ok`, `FAILED`, a summary without failures, one with failures. */
typedef enum torb_test_role {
  TORB_TEST_PASSED,
  TORB_TEST_FAILED,
  TORB_TEST_CLEAN,
  TORB_TEST_PROBLEMS,
} torb_test_role;
/** The names of `--filter`, each an owned copy; none is a run of every test. */
static torb_test_buffer *torb_test_filters = NULL;
static size_t torb_test_filter_count = 0;
static size_t torb_test_filter_capacity = 0;
/** The path of the file whose tests come now, as its line names it: every event of the JSON report names it too. */
static torb_test_buffer torb_test_path = {NULL, 0u, 0u};

static void torb_test_buffer_add(torb_test_buffer *buffer, const char *bytes, size_t count) {
  if (buffer->length + count + 1u > buffer->capacity) {
    size_t capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    char *grown;
    while (capacity < buffer->length + count + 1u) {
      capacity *= 2u;
    }
    grown = (char *)torb_raw_allocate(capacity);
    if (buffer->length > 0u) {
      memcpy(grown, buffer->bytes, buffer->length);
    }
    if (buffer->bytes != NULL) {
      torb_raw_free(buffer->bytes, buffer->capacity);
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
  }
  if (count > 0u && bytes != NULL) {
    memcpy(buffer->bytes + buffer->length, bytes, count);
  }
  buffer->length += count;
  buffer->bytes[buffer->length] = '\0';
}

static void torb_test_buffer_add_cstring(torb_test_buffer *buffer, const char *text) {
  torb_test_buffer_add(buffer, text, strlen(text));
}

static void torb_test_buffer_add_text(torb_test_buffer *buffer, torb_text text) {
  const char *bytes = text.storage == NULL ? NULL : (const char *)text.storage->data + text.offset;
  torb_test_buffer_add(buffer, bytes, (size_t)text.length);
}

static void torb_test_buffer_release(torb_test_buffer *buffer) {
  if (buffer->bytes != NULL) {
    torb_raw_free(buffer->bytes, buffer->capacity);
  }
  buffer->bytes = NULL;
  buffer->length = 0u;
  buffer->capacity = 0u;
}

/** `2/4` as index 2 of 4, or false for anything else: an index from 1 to the count, both written in decimal digits. */
static bool torb_test_parse_shard(const char *text, size_t length, int64_t *index, int64_t *count) {
  int64_t numbers[2] = {0, 0};
  int part = 0;
  bool digits = false;
  size_t at;
  for (at = 0u; at < length; at += 1u) {
    const char character = text[at];
    if (character >= '0' && character <= '9') {
      if (numbers[part] > 100000) {
        return false;
      }
      numbers[part] = numbers[part] * 10 + (character - '0');
      digits = true;
    } else if (character == '/' && part == 0 && digits) {
      part = 1;
      digits = false;
    } else {
      return false;
    }
  }
  if (part != 1 || !digits || numbers[0] < 1 || numbers[0] > numbers[1]) {
    return false;
  }
  *index = numbers[0];
  *count = numbers[1];
  return true;
}

static bool torb_test_text_is(torb_text text, const char *expected) {
  const size_t length = strlen(expected);
  return (size_t)text.length == length &&
         (length == 0u || memcmp((const char *)text.storage->data + text.offset, expected, length) == 0);
}

static void torb_test_add_filter(torb_text name) {
  if (torb_test_filter_count == torb_test_filter_capacity) {
    const size_t capacity = torb_test_filter_capacity == 0u ? 4u : torb_test_filter_capacity * 2u;
    torb_test_buffer *grown = (torb_test_buffer *)torb_raw_allocate_zeroed(capacity * sizeof(torb_test_buffer));
    if (torb_test_filter_count > 0u) {
      memcpy(grown, torb_test_filters, torb_test_filter_count * sizeof(torb_test_buffer));
    }
    if (torb_test_filters != NULL) {
      torb_raw_free(torb_test_filters, torb_test_filter_capacity * sizeof(torb_test_buffer));
    }
    torb_test_filters = grown;
    torb_test_filter_capacity = capacity;
  }
  torb_test_filters[torb_test_filter_count].bytes = NULL;
  torb_test_filters[torb_test_filter_count].length = 0u;
  torb_test_filters[torb_test_filter_count].capacity = 0u;
  torb_test_buffer_add_text(&torb_test_filters[torb_test_filter_count], name);
  torb_test_filter_count += 1u;
}

/* ------------------------------------------------------------------------------------------------- the colours --- */

/** An environment variable of up to `size - 1` bytes, or the empty text where it is not set or longer than that. */
static void torb_test_variable(const char *name, char *buffer, size_t size) {
  char *value = NULL;
  size_t length = 0u;
  buffer[0] = '\0';
  if (!torb_platform_environment_variable(name, &value, &length)) {
    return;
  }
  if (length < size) {
    memcpy(buffer, value, length + 1u);
  }
  torb_raw_free(value, length + 1u);
}

/** Whether a variable of the kind of `FORCE_COLOR` forces colour: set, and not to `0`. */
static bool torb_test_forces(const char *value) {
  return value[0] != '\0' && strcmp(value, "0") != 0;
}

/**
 * The palette of the report, by the rules `compiler/src/cli/color.trb` colours the rest of `torb`'s output with - the
 * first that applies: `--color always` or `never` (`choice` 1 or 2; 0 is `auto`, the default), `FORCE_COLOR` or
 * `CLICOLOR_FORCE` set to anything but empty or `0`, `NO_COLOR` set to anything but empty, and otherwise whether
 * standard output is a terminal whose `TERM` is not `dumb`. Coloured, it is 24 bits only where `COLORTERM` is
 * `truecolor` or `24bit` and `TORB_BACKGROUND` (`light`, `dark`) or the last field of `COLORFGBG` (0 to 6 and 8 dark,
 * 7 and 9 to 15 light) says the ground.
 */
static torb_test_palette torb_test_decide_colors(int choice) {
  char value[64];
  char other[64];
  bool colored;
  bool one_digit;
  bool two_digits;
  const char *field;
  if (choice == 1) {
    colored = true;
  } else if (choice == 2) {
    colored = false;
  } else {
    torb_test_variable("FORCE_COLOR", value, sizeof value);
    torb_test_variable("CLICOLOR_FORCE", other, sizeof other);
    if (torb_test_forces(value) || torb_test_forces(other)) {
      colored = true;
    } else {
      torb_test_variable("NO_COLOR", value, sizeof value);
      torb_test_variable("TERM", other, sizeof other);
      colored = value[0] == '\0' && strcmp(other, "dumb") != 0 && torb_is_terminal();
    }
  }
  if (!colored) {
    return TORB_TEST_PLAIN;
  }
  torb_test_variable("COLORTERM", value, sizeof value);
  if (strcmp(value, "truecolor") != 0 && strcmp(value, "24bit") != 0) {
    return TORB_TEST_SIXTEEN;
  }
  torb_test_variable("TORB_BACKGROUND", value, sizeof value);
  if (strcmp(value, "light") == 0) {
    return TORB_TEST_LIGHT;
  }
  if (strcmp(value, "dark") == 0) {
    return TORB_TEST_DARK;
  }
  torb_test_variable("COLORFGBG", value, sizeof value);
  field = strrchr(value, ';');
  field = field == NULL ? value : field + 1;
  one_digit = field[0] >= '0' && field[0] <= '9' && field[1] == '\0';
  two_digits = field[0] >= '0' && field[0] <= '9' && field[1] >= '0' && field[1] <= '9' && field[2] == '\0';
  if (one_digit || two_digits) {
    const int number = one_digit ? field[0] - '0' : (field[0] - '0') * 10 + (field[1] - '0');
    if (number <= 6 || number == 8) {
      return TORB_TEST_DARK;
    }
    if (number <= 15) {
      return TORB_TEST_LIGHT;
    }
  }
  return TORB_TEST_SIXTEEN;
}

/**
 * The SGR parameters of a role in the palette of the run: `ok` and a clean summary green, `FAILED` bold red, a summary
 * with failures bold - in 24 bits the success and error text tokens of the brand and ink or paper, never the brand red.
 */
static const char *torb_test_sgr(torb_test_role role) {
  switch (torb_test_colors) {
    case TORB_TEST_LIGHT:
      return role == TORB_TEST_FAILED     ? "1;38;2;176;9;33"
             : role == TORB_TEST_PROBLEMS ? "1;38;2;20;17;15"
                                          : "38;2;0;110;48";
    case TORB_TEST_DARK:
      return role == TORB_TEST_FAILED     ? "1;38;2;255;166;163"
             : role == TORB_TEST_PROBLEMS ? "1;38;2;249;246;245"
                                          : "38;2;146;215;160";
    default:
      return role == TORB_TEST_FAILED ? "1;31" : role == TORB_TEST_PROBLEMS ? "1" : "32";
  }
}

/** `text` into `buffer` in the colour of `role`, reset behind it, or as it is where the report is plain. */
static void torb_test_paint(char *buffer, size_t size, torb_test_role role, const char *text) {
  if (torb_test_colors == TORB_TEST_PLAIN) {
    snprintf(buffer, size, "%s", text);
  } else {
    snprintf(buffer, size, "\x1b[%sm%s\x1b[0m", torb_test_sgr(role), text);
  }
}

/**
 * Reads `--shard`, `--filter`, `--report` and `--color` off the command line, once. Through `torb_process_arguments`
 * and not the `argv` of `main`: on Windows that `argv` is not UTF-8 (process.c), and the name of a test that a filter
 * names may hold any character. An option and its value are two arguments, read as a pair wherever they stand, which is
 * what `torb test` checks before it runs anything (`testOptionsOf` in `compiler/src/main.trb`) and what it hands a
 * native binary; the other arguments - `torb`'s own subcommand and paths, in the VM - are passed over. A value that says
 * nothing an option could mean is a mistake of the caller; `--color` also takes `--color=<when>`, as `torb` does.
 */
static void torb_test_read_options(void) {
  torb_list arguments;
  int64_t count;
  int64_t index = 0;
  int color = 0;
  if (torb_test_options_read) {
    return;
  }
  torb_test_options_read = true;
  arguments = torb_process_arguments();
  count = torb_list_length(arguments);
  while (index + 1 < count) {
    const torb_text option = *(const torb_text *)torb_list_at(arguments, index, torb_location_unknown);
    const torb_text value = *(const torb_text *)torb_list_at(arguments, index + 1, torb_location_unknown);
    const char *bytes = value.storage == NULL ? "" : (const char *)value.storage->data + value.offset;
    if (torb_test_text_is(option, "--shard")) {
      if (!torb_test_parse_shard(bytes, (size_t)value.length, &torb_test_shard_index, &torb_test_shard_count)) {
        torb_panic_text("`--shard` takes `<index>/<count>`, an index from 1 to the count: `--shard 2/4`",
                        torb_location_unknown);
      }
    } else if (torb_test_text_is(option, "--filter")) {
      torb_test_add_filter(value);
    } else if (torb_test_text_is(option, "--report")) {
      if (!torb_test_text_is(value, "json")) {
        torb_panic_text("`--report` takes `json`, the one report besides the human one: `--report json`",
                        torb_location_unknown);
      }
      torb_test_reports_json = true;
    } else {
      index += 1;
      continue;
    }
    index += 2;
  }
  /* `--color <when>` or `--color=<when>`, the last one; `torb` refused any other value before the run began, so one it
     does not know is `auto` here rather than a panic */
  for (index = 0; index < count; index += 1) {
    const torb_text option = *(const torb_text *)torb_list_at(arguments, index, torb_location_unknown);
    torb_text value = option;
    if (torb_test_text_is(option, "--color") && index + 1 < count) {
      value = *(const torb_text *)torb_list_at(arguments, index + 1, torb_location_unknown);
      index += 1;
    } else if (option.length > 8 && memcmp((const char *)option.storage->data + option.offset, "--color=", 8u) == 0) {
      value.offset += 8;
      value.length -= 8;
    } else {
      continue;
    }
    color = torb_test_text_is(value, "always") ? 1 : torb_test_text_is(value, "never") ? 2 : 0;
  }
  torb_test_colors = torb_test_reports_json ? TORB_TEST_PLAIN : torb_test_decide_colors(color);
  torb_list_release(arguments);
}

/** Appends the bytes of a text to `buffer`, which stays NUL terminated whatever it does not fit. */
static void torb_test_append(char *buffer, size_t size, size_t *length, const char *bytes, size_t count) {
  size_t room = size - 1 - *length;
  if (count > room) {
    count = room;
  }
  if (count > 0 && bytes != NULL) {
    memcpy(buffer + *length, bytes, count);
    *length += count;
  }
  buffer[*length] = '\0';
}

static void torb_test_append_text(char *buffer, size_t size, size_t *length, torb_text text) {
  const char *bytes = text.storage == NULL ? NULL : (const char *)text.storage->data + text.offset;
  torb_test_append(buffer, size, length, bytes, (size_t)text.length);
}

/** `Outer > Inner > what the test is called`, which is the name one line of the report names. */
static void torb_test_full_name(char *buffer, size_t size, torb_text name) {
  size_t length = 0;
  uint32_t index;
  buffer[0] = '\0';
  for (index = 0; index < torb_test_depth; index += 1) {
    torb_test_append_text(buffer, size, &length, torb_test_groups[index]);
    torb_test_append(buffer, size, &length, " > ", 3);
  }
  torb_test_append_text(buffer, size, &length, name);
}

/** The same name whole, without the bound of the line: what a filter and the JSON report compare and write. */
static void torb_test_full_name_into(torb_test_buffer *buffer, torb_text name) {
  uint32_t index;
  for (index = 0; index < torb_test_depth; index += 1) {
    torb_test_buffer_add_text(buffer, torb_test_groups[index]);
    torb_test_buffer_add(buffer, " > ", 3u);
  }
  torb_test_buffer_add_text(buffer, name);
}

/** Whether `name` starts with `prefix` followed by ` > `: `prefix` names a group that `name` lies below. */
static bool torb_test_is_below(const char *name, size_t name_length, const char *prefix, size_t prefix_length) {
  return name_length > prefix_length + 3u && memcmp(name, prefix, prefix_length) == 0 &&
         memcmp(name + prefix_length, " > ", 3u) == 0;
}

/** Whether a test of this full name runs: it is what a filter names, or lies below a group a filter names. */
static bool torb_test_is_wanted(const torb_test_buffer *full) {
  size_t index;
  if (torb_test_filter_count == 0u) {
    return true;
  }
  for (index = 0u; index < torb_test_filter_count; index += 1u) {
    const torb_test_buffer *filter = &torb_test_filters[index];
    if ((full->length == filter->length && memcmp(full->bytes, filter->bytes, filter->length) == 0) ||
        torb_test_is_below(full->bytes, full->length, filter->bytes, filter->length)) {
      return true;
    }
  }
  return false;
}

/**
 * Whether the body of a group of this full name runs: a filter names it, lies below it (the group is on the way there),
 * or names a group it lies below. Every other group is passed over whole, as a file of another shard is.
 */
static bool torb_test_is_group_wanted(const torb_test_buffer *full) {
  size_t index;
  if (torb_test_is_wanted(full)) {
    return true;
  }
  for (index = 0u; index < torb_test_filter_count; index += 1u) {
    const torb_test_buffer *filter = &torb_test_filters[index];
    if (torb_test_is_below(filter->bytes, filter->length, full->bytes, full->length)) {
      return true;
    }
  }
  return false;
}

/**
 * One line of the report, through the same path `print` goes through (`torb_write_line_out` in `console.c`): a name or
 * a message with a non-ASCII character in it reads correctly on a live Windows console, and a pipe keeps exactly the
 * bytes `torb test` compares between the two implementations.
 */
static void torb_test_line(const char *line) {
  torb_write_line_out(line, strlen(line));
}

/** The same, with `prefix` in front of `body`, which is the one shape every line of this report has. */
static void torb_test_line_of(const char *prefix, const char *body, size_t length) {
  char line[TORB_TEST_NAME_SIZE + 64];
  size_t filled = 0u;
  torb_test_append(line, sizeof line, &filled, prefix, strlen(prefix));
  torb_test_append(line, sizeof line, &filled, body, length);
  torb_write_line_out(line, filled);
}

/** The message of a failure, with every line after the first indented to the column the first one starts in. */
static void torb_test_print_indented(const char *message) {
  const char *line = message;
  for (;;) {
    const char *end = strchr(line, '\n');
    if (end == NULL) {
      torb_test_line_of("          ", line, strlen(line));
      return;
    }
    torb_test_line_of("          ", line, (size_t)(end - line));
    line = end + 1;
  }
}

/* ------------------------------------------------------------------------------------------ the JSON report --- */

/**
 * A JSON string of UTF-8 bytes: `"` and `\` escaped, a control character as `\n`, `\t`, `\r` or `\u00XX`, and every
 * other byte - the bytes of a character outside ASCII among them - as it is, which JSON allows.
 */
static void torb_test_json_string(torb_test_buffer *line, const char *bytes, size_t length) {
  static const char digits[] = "0123456789abcdef";
  size_t from = 0u;
  size_t at;
  torb_test_buffer_add(line, "\"", 1u);
  for (at = 0u; at < length; at += 1u) {
    const unsigned char byte = (unsigned char)bytes[at];
    char escape[6];
    size_t width = 2u;
    if (byte == '"' || byte == '\\') {
      escape[0] = '\\';
      escape[1] = (char)byte;
    } else if (byte == '\n') {
      memcpy(escape, "\\n", 2u);
    } else if (byte == '\t') {
      memcpy(escape, "\\t", 2u);
    } else if (byte == '\r') {
      memcpy(escape, "\\r", 2u);
    } else if (byte < 0x20u) {
      memcpy(escape, "\\u00", 4u);
      escape[4] = digits[byte >> 4];
      escape[5] = digits[byte & 15u];
      width = 6u;
    } else {
      continue;
    }
    torb_test_buffer_add(line, bytes + from, at - from);
    torb_test_buffer_add(line, escape, width);
    from = at + 1u;
  }
  torb_test_buffer_add(line, bytes + from, length - from);
  torb_test_buffer_add(line, "\"", 1u);
}

static void torb_test_json_text(torb_test_buffer *line, torb_text text) {
  const char *bytes = text.storage == NULL ? "" : (const char *)text.storage->data + text.offset;
  torb_test_json_string(line, bytes, (size_t)text.length);
}

static void torb_test_json_cstring(torb_test_buffer *line, const char *text) {
  torb_test_json_string(line, text, strlen(text));
}

/** One line of the JSON report, written and flushed at once, and the buffer given back. */
static void torb_test_json_write(torb_test_buffer *line) {
  torb_write_line_out(line->bytes, line->length);
  fflush(stdout);
  torb_test_buffer_release(line);
}

/**
 * `{"event":"<event>","file":...,"groups":[...],"name":...,"fullName":...`, without the closing brace: what the two
 * events of one test begin with.
 */
static void torb_test_json_test_head(torb_test_buffer *line, const char *event, torb_text name,
                                     const torb_test_buffer *full) {
  uint32_t index;
  torb_test_buffer_add_cstring(line, "{\"event\":\"");
  torb_test_buffer_add_cstring(line, event);
  torb_test_buffer_add_cstring(line, "\",\"file\":");
  torb_test_json_string(line, torb_test_path.bytes == NULL ? "" : torb_test_path.bytes, torb_test_path.length);
  torb_test_buffer_add_cstring(line, ",\"groups\":[");
  for (index = 0; index < torb_test_depth; index += 1) {
    if (index > 0) {
      torb_test_buffer_add(line, ",", 1u);
    }
    torb_test_json_text(line, torb_test_groups[index]);
  }
  torb_test_buffer_add_cstring(line, "],\"name\":");
  torb_test_json_text(line, name);
  torb_test_buffer_add_cstring(line, ",\"fullName\":");
  torb_test_json_string(line, full->bytes, full->length);
}

/** The event after a test: its outcome, how long it took, and for a failure the message, the site and the frames. */
static void torb_test_json_outcome(torb_text name, const torb_test_buffer *full, bool passed, int64_t nanoseconds,
                                   const torb_recovery *point) {
  torb_test_buffer line = {NULL, 0u, 0u};
  char number[64];
  torb_test_json_test_head(&line, "test", name, full);
  torb_test_buffer_add_cstring(&line, passed ? ",\"outcome\":\"passed\"" : ",\"outcome\":\"failed\"");
  snprintf(number, sizeof number, ",\"duration\":%.3f", (double)nanoseconds / 1000000.0);
  torb_test_buffer_add_cstring(&line, number);
  if (!passed) {
    torb_test_buffer_add_cstring(&line, ",\"message\":");
    torb_test_json_cstring(&line, point->message);
    if (point->at.path != NULL) {
      torb_test_buffer_add_cstring(&line, ",\"location\":{\"path\":");
      torb_test_json_cstring(&line, point->at.path);
      snprintf(number, sizeof number, ",\"line\":%u,\"column\":%u}", point->at.line, point->at.column);
      torb_test_buffer_add_cstring(&line, number);
    }
    if (point->frames[0] != '\0') {
      torb_test_buffer_add_cstring(&line, ",\"frames\":");
      torb_test_json_cstring(&line, point->frames);
    }
  }
  torb_test_buffer_add(&line, "}", 1u);
  torb_test_json_write(&line);
}

/* ---------------------------------------------------------------------------------------------- test, group --- */

void torb_test_case(torb_text name, torb_closure body) {
  char full[TORB_TEST_NAME_SIZE];
  torb_test_buffer whole = {NULL, 0u, 0u};
  torb_recovery point;
  torb_recovery *previous;
  bool scoped;
  bool passed = false;
  int64_t started;
  torb_test_read_options();
  if (!torb_test_selected) {
    return;
  }
  if (torb_test_filter_count > 0u || torb_test_reports_json) {
    torb_test_full_name_into(&whole, name);
    if (!torb_test_is_wanted(&whole)) {
      torb_test_buffer_release(&whole);
      return;
    }
  }
  if (torb_test_reports_json) {
    torb_test_buffer line = {NULL, 0u, 0u};
    torb_test_json_test_head(&line, "start", name, &whole);
    torb_test_buffer_add(&line, "}", 1u);
    torb_test_json_write(&line);
  }
  /* The duration is the body's and the wait for its tasks, the whole of what the test is */
  started = torb_platform_monotonic_nanoseconds();
  scoped = torb_test_tasks_begin();
  previous = torb_begin_recovery(&point);
  if (setjmp(point.destination) == 0) {
    ((void (*)(torb_environment *))body.code)(body.environment);
    torb_end_recovery(previous);
    /* A task the body started that panicked, on whichever worker, fails the test as the body would have */
    passed = !scoped || !torb_test_tasks_end(false, &point);
  } else {
    torb_end_recovery(previous);
    if (scoped) {
      (void)torb_test_tasks_end(true, NULL);
    }
    passed = false;
  }
  if (passed) {
    torb_test_passed += 1;
  } else {
    torb_test_failed += 1;
  }
  if (torb_test_reports_json) {
    torb_test_json_outcome(name, &whole, passed, torb_platform_monotonic_nanoseconds() - started, &point);
    torb_test_buffer_release(&whole);
    return;
  }
  torb_test_buffer_release(&whole);
  torb_test_full_name(full, sizeof full, name);
  if (passed) {
    char word[48];
    char prefix[64];
    torb_test_paint(word, sizeof word, TORB_TEST_PASSED, "ok");
    snprintf(prefix, sizeof prefix, "  %s      ", word);
    torb_test_line_of(prefix, full, strlen(full));
    return;
  }
  {
    char word[48];
    char prefix[64];
    torb_test_paint(word, sizeof word, TORB_TEST_FAILED, "FAILED");
    snprintf(prefix, sizeof prefix, "  %s  ", word);
    torb_test_line_of(prefix, full, strlen(full));
  }
  torb_test_print_indented(point.message);
  if (point.at.path != NULL) {
    char site[TORB_TEST_NAME_SIZE];
    snprintf(site, sizeof site, "          at %s:%u:%u", point.at.path, point.at.line, point.at.column);
    torb_test_line(site);
  }
  /* The frames of the `dev` profile, below the site as a panic that ends the process prints them */
  if (point.frames[0] != '\0') {
    torb_test_print_indented(point.frames);
  }
  fflush(stdout);
}

void torb_test_file(const char *path, size_t length) {
  torb_test_read_options();
  torb_test_selected =
    torb_test_shard_count == 0 || torb_test_seen % torb_test_shard_count == torb_test_shard_index - 1;
  torb_test_seen += 1;
  if (!torb_test_selected) {
    return;
  }
  torb_test_files += 1;
  if (torb_test_reports_json) {
    torb_test_buffer line = {NULL, 0u, 0u};
    torb_test_buffer_release(&torb_test_path);
    torb_test_buffer_add(&torb_test_path, path, length);
    torb_test_buffer_add_cstring(&line, "{\"event\":\"file\",\"path\":");
    torb_test_json_string(&line, path, length);
    torb_test_buffer_add(&line, "}", 1u);
    torb_test_json_write(&line);
    return;
  }
  torb_write_line_out(path, length);
}

int torb_test_finish(void) {
  char shard[64] = "";
  char summary[256];
  torb_test_read_options();
  if (torb_test_reports_json) {
    if (torb_test_shard_count != 0) {
      snprintf(shard, sizeof shard, ",\"shard\":{\"index\":%lld,\"count\":%lld}", (long long)torb_test_shard_index,
               (long long)torb_test_shard_count);
    }
    snprintf(summary, sizeof summary, "{\"event\":\"summary\",\"passed\":%lld,\"failed\":%lld,\"files\":%lld%s}",
             (long long)torb_test_passed, (long long)torb_test_failed, (long long)torb_test_files, shard);
  } else {
    char counts[192];
    if (torb_test_shard_count != 0) {
      snprintf(shard, sizeof shard, ", shard %lld of %lld", (long long)torb_test_shard_index,
               (long long)torb_test_shard_count);
    }
    snprintf(counts, sizeof counts, "%lld passed, %lld failed (%lld %s%s)", (long long)torb_test_passed,
             (long long)torb_test_failed, (long long)torb_test_files, torb_test_files == 1 ? "file" : "files", shard);
    summary[0] = '\n';
    torb_test_paint(summary + 1, sizeof summary - 1, torb_test_failed == 0 ? TORB_TEST_CLEAN : TORB_TEST_PROBLEMS,
                    counts);
  }
  torb_write_line_out(summary, strlen(summary));
  fflush(stdout);
  torb_test_buffer_release(&torb_test_path);
  while (torb_test_filter_count > 0u) {
    torb_test_filter_count -= 1u;
    torb_test_buffer_release(&torb_test_filters[torb_test_filter_count]);
  }
  if (torb_test_filters != NULL) {
    torb_raw_free(torb_test_filters, torb_test_filter_capacity * sizeof(torb_test_buffer));
    torb_test_filters = NULL;
    torb_test_filter_capacity = 0u;
  }
  return torb_test_failed == 0 ? 0 : 1;
}

void torb_test_group(torb_text name, torb_closure body) {
  torb_test_read_options();
  if (!torb_test_selected) {
    return;
  }
  if (torb_test_depth >= TORB_TEST_GROUP_DEPTH) {
    torb_panic_text("a group inside more than 32 groups", torb_location_unknown);
  }
  if (torb_test_filter_count > 0u) {
    torb_test_buffer whole = {NULL, 0u, 0u};
    bool wanted;
    torb_test_full_name_into(&whole, name);
    wanted = torb_test_is_group_wanted(&whole);
    torb_test_buffer_release(&whole);
    if (!wanted) {
      return;
    }
  }
  torb_test_groups[torb_test_depth] = name;
  torb_test_depth += 1;
  ((void (*)(torb_environment *))body.code)(body.environment);
  torb_test_depth -= 1;
}
