/*
 * file_test.c - the minimum of `std/fs`, in the temporary directory of the machine.
 *
 * The test writes two files with distinctive names, reads them back, lists the directory (which must come out sorted)
 * and removes them again with `remove` from `<stdio.h>`. `absolutePath` is text arithmetic, so it is tested without
 * touching the disk at all.
 */

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>

/** The temporary directory, with forward slashes and without a trailing separator. Result owned. */
static torb_text temporary_directory(void) {
  const char *names[3] = { "TMPDIR", "TEMP", "TMP" };
  size_t index;
  for (index = 0u; index < 3u; index += 1u) {
    const char *value = getenv(names[index]);
    if (value != NULL && value[0] != '\0') {
      torb_text raw = torb_text_from_cstring(value);
      uint32_t position;
      for (position = 0u; position < raw.length; position += 1u) {
        if (raw.storage->data[position] == '\\') {
          raw.storage->data[position] = '/';
        }
      }
      if (raw.length > 1u && raw.storage->data[raw.length - 1u] == '/') {
        torb_text trimmed = torb_text_slice(raw, 0, (int64_t)raw.length - 1, torb_location_unknown);
        torb_text_release(raw);
        return trimmed;
      }
      return raw;
    }
  }
  return torb_text_from_cstring(".");
}

static torb_text path_in(torb_text directory, const char *name) {
  torb_text parts[3];
  torb_text result;
  parts[0] = directory;
  parts[1] = torb_text_from_cstring("/");
  parts[2] = torb_text_from_cstring(name);
  result = torb_text_concat(parts, 3u);
  torb_text_release(parts[1]);
  torb_text_release(parts[2]);
  return result;
}

static void remove_path(torb_text path) {
  char buffer[1024];
  if ((size_t)path.length + 1u > sizeof buffer) {
    return;
  }
  memcpy(buffer, path.storage->data + path.offset, (size_t)path.length);
  buffer[path.length] = '\0';
  remove(buffer);
}

TORB_TEST(writing_reading_and_listing_a_directory) {
  torb_text directory = temporary_directory();
  torb_text first = path_in(directory, "torb-runtime-test-b.txt");
  torb_text second = path_in(directory, "torb-runtime-test-a.txt");
  torb_text contents = torb_text_from_cstring("hello\nworld\n");
  torb_text error = torb_text_empty();
  torb_text read_back = torb_text_empty();
  torb_list entries = torb_list_new(&torb_element_text);
  bool saw_first = false;
  bool saw_second = false;
  int64_t index;

  TORB_CHECK(torb_file_write_text(first, contents, &error));
  TORB_CHECK(torb_file_write_text(second, contents, &error));
  TORB_CHECK(torb_file_exists(first));
  TORB_CHECK(!torb_file_is_directory(first));
  TORB_CHECK(torb_file_is_directory(directory));
  TORB_CHECK(torb_file_read_text(first, &read_back, &error));
  TORB_CHECK_TEXT(read_back, "hello\nworld\n");
  torb_text_release(read_back);

  torb_list_release(entries);
  TORB_CHECK(torb_file_list(directory, &entries, &error));
  /* Sorted by bytes, always: the fixpoint test of the compiler depends on it. */
  for (index = 1; index < torb_list_length(entries); index += 1) {
    torb_text previous = *(const torb_text *)torb_list_at(entries, index - 1, torb_location_unknown);
    torb_text current = *(const torb_text *)torb_list_at(entries, index, torb_location_unknown);
    TORB_CHECK(torb_text_compare(previous, current) <= 0);
  }
  for (index = 0; index < torb_list_length(entries); index += 1) {
    torb_text entry = *(const torb_text *)torb_list_at(entries, index, torb_location_unknown);
    torb_text wanted_first = torb_text_from_cstring("torb-runtime-test-b.txt");
    torb_text wanted_second = torb_text_from_cstring("torb-runtime-test-a.txt");
    if (torb_text_equal(entry, wanted_first)) {
      saw_first = true;
    }
    if (torb_text_equal(entry, wanted_second)) {
      saw_second = true;
    }
    torb_text_release(wanted_first);
    torb_text_release(wanted_second);
  }
  TORB_CHECK(saw_first);
  TORB_CHECK(saw_second);

  remove_path(first);
  remove_path(second);
  TORB_CHECK(!torb_file_exists(first));

  torb_list_release(entries);
  torb_text_release(contents);
  torb_text_release(first);
  torb_text_release(second);
  torb_text_release(directory);
  torb_text_release(error);
}

TORB_TEST(reading_a_file_that_is_not_there_is_an_error_with_a_message) {
  torb_text path = torb_text_from_cstring("torb-runtime-test-missing-file.txt");
  torb_text out = torb_text_empty();
  torb_text error = torb_text_empty();
  TORB_CHECK(!torb_file_read_text(path, &out, &error));
  /* The message alone: the `IoError` the wrapper builds carries the path beside it */
  TORB_CHECK(torb_text_byte_length(error) > 0);
  TORB_CHECK(!torb_text_starts_with(error, path));
  TORB_CHECK(!torb_file_exists(path));
  TORB_CHECK(!torb_file_is_directory(path));
  torb_text_release(error);
  torb_text_release(path);
}

TORB_TEST(reading_a_file_whose_bytes_are_not_utf8_is_an_error) {
  torb_text directory = temporary_directory();
  torb_text path = path_in(directory, "torb-runtime-test-bad-utf8.bin");
  torb_text out = torb_text_empty();
  torb_text error = torb_text_empty();
  char name[1024];
  const uint8_t bytes[3] = { 'a', 0x80u, 'b' };
  const char *message = NULL;
  memcpy(name, path.storage->data + path.offset, (size_t)path.length);
  name[path.length] = '\0';
  TORB_CHECK(torb_platform_write_file(name, bytes, 3u, &message));
  TORB_CHECK(!torb_file_read_text(path, &out, &error));
  {
    torb_text part = torb_text_from_cstring("the byte at offset 1 is not valid UTF-8");
    TORB_CHECK(torb_text_contains(error, part));
    torb_text_release(part);
  }
  remove_path(path);
  torb_text_release(error);
  torb_text_release(path);
  torb_text_release(directory);
}

TORB_TEST(absolute_paths_are_text_arithmetic) {
  torb_text error = torb_text_empty();
  torb_text out = torb_text_empty();
  torb_text absolute = torb_text_from_cstring("/one/two/../three/./four");
  torb_text expected = torb_text_from_cstring("/one/three/four");
  torb_text relative = torb_text_from_cstring("a/b");
  TORB_CHECK(torb_file_absolute_path(absolute, &out, &error));
  TORB_CHECK_TEXT(out, "/one/three/four");
  torb_text_release(out);
  /* A relative path is resolved against the working directory, whatever that is, and comes out absolute. */
  TORB_CHECK(torb_file_absolute_path(relative, &out, &error));
  TORB_CHECK(torb_text_byte_length(out) > torb_text_byte_length(relative));
  {
    torb_text tail = torb_text_from_cstring("/a/b");
    torb_text backslash = torb_text_from_cstring("\\");
    TORB_CHECK(torb_text_ends_with(out, tail));
    /* Separators come out as forward slashes on every platform. */
    TORB_CHECK(!torb_text_contains(out, backslash));
    torb_text_release(tail);
    torb_text_release(backslash);
  }
  torb_text_release(out);
  /* `..` at the root cannot climb above it. */
  {
    torb_text climbing = torb_text_from_cstring("/../../x");
    TORB_CHECK(torb_file_absolute_path(climbing, &out, &error));
    TORB_CHECK_TEXT(out, "/x");
    torb_text_release(out);
    torb_text_release(climbing);
  }
  /* An extended-length path loses its prefix: `\\?\` is what a *call* of Windows takes, never a form a path is shown
     in - and one arrives from anything that canonicalized a path there. A drive keeps its upper case. */
  {
    torb_text extended = torb_text_from_cstring("\\\\?\\c:\\one\\two");
    torb_text share = torb_text_from_cstring("\\\\?\\UNC\\server\\share\\file.txt");
    TORB_CHECK(torb_file_absolute_path(extended, &out, &error));
    TORB_CHECK_TEXT(out, "C:/one/two");
    torb_text_release(out);
    TORB_CHECK(torb_file_absolute_path(share, &out, &error));
    TORB_CHECK_TEXT(out, "/server/share/file.txt");
    torb_text_release(out);
    torb_text_release(extended);
    torb_text_release(share);
  }
  /* A drive alone is its root, and a drive-relative path is read as absolute on that drive (docs/design/PATH.md section 2). */
  {
    torb_text drive = torb_text_from_cstring("c:");
    torb_text relative_on_drive = torb_text_from_cstring("C:foo\\bar");
    TORB_CHECK(torb_file_absolute_path(drive, &out, &error));
    TORB_CHECK_TEXT(out, "C:/");
    torb_text_release(out);
    TORB_CHECK(torb_file_absolute_path(relative_on_drive, &out, &error));
    TORB_CHECK_TEXT(out, "C:/foo/bar");
    torb_text_release(out);
    torb_text_release(drive);
    torb_text_release(relative_on_drive);
  }
  torb_text_release(absolute);
  torb_text_release(expected);
  torb_text_release(relative);
  torb_text_release(error);
}

/* --------------------------------------------------------------------------------------------- open handles --- */

TORB_TEST(opening_a_missing_file_is_an_error) {
  torb_text path = torb_text_from_cstring("torb-runtime-test-missing-handle.txt");
  torb_file *file = NULL;
  torb_text error = torb_text_empty();
  TORB_CHECK(!torb_file_open(path, &file, &error));
  /* The message is the operating system's words alone: the `IoError` around it carries the path */
  TORB_CHECK(torb_text_byte_length(error) > 0);
  TORB_CHECK(!torb_text_starts_with(error, path));
  torb_text_release(error);
  torb_text_release(path);
}

TORB_TEST(opening_reading_and_double_closing_a_file) {
  torb_text directory = temporary_directory();
  torb_text path = path_in(directory, "torb-runtime-test-handle.txt");
  torb_text contents = torb_text_from_cstring("line one\nline two\n");
  torb_text error = torb_text_empty();
  torb_file *file = NULL;
  torb_text read_back = torb_text_empty();
  torb_text failed_path = torb_text_empty();

  TORB_CHECK(torb_file_write_text(path, contents, &error));
  TORB_CHECK(torb_file_open(path, &file, &error));
  TORB_CHECK(torb_file_read_all(&file, &read_back, &failed_path, &error));
  TORB_CHECK_TEXT(read_back, "line one\nline two\n");
  torb_text_release(read_back);

  torb_file_close(&file);
  /* Reading a closed file follows the ordinary `Result`/`IoError` path, never a panic. */
  read_back = torb_text_empty();
  TORB_CHECK(!torb_file_read_all(&file, &read_back, &failed_path, &error));
  TORB_CHECK(torb_text_equal(failed_path, path));
  torb_text_release(failed_path);
  torb_text_release(error);
  /* Closing an already-closed file is a no-op, not a panic. */
  torb_file_close(&file);

  torb_release(file, torb_file_drop);
  remove_path(path);
  TORB_CHECK(!torb_file_exists(path));

  torb_text_release(contents);
  torb_text_release(path);
  torb_text_release(directory);
}

TORB_TEST(releasing_a_file_without_closing_it_still_closes_the_handle) {
  torb_text directory = temporary_directory();
  torb_text path = path_in(directory, "torb-runtime-test-handle-unclosed.txt");
  torb_text contents = torb_text_from_cstring("content");
  torb_text error = torb_text_empty();
  torb_file *file = NULL;

  TORB_CHECK(torb_file_write_text(path, contents, &error));
  TORB_CHECK(torb_file_open(path, &file, &error));
  /* `close` is never called: the drop function must close the OS handle by itself, or the next line - which an OS
     that locks open files (Windows) would refuse if the handle had leaked - fails. */
  torb_release(file, torb_file_drop);
  remove_path(path);
  TORB_CHECK(!torb_file_exists(path));

  torb_text_release(contents);
  torb_text_release(path);
  torb_text_release(directory);
  torb_text_release(error);
}

/**
 * `createDirectory` is `mkdir -p`: two levels at once, and "it is already there" is success - which is what
 * `torb build` needs of it before it writes the generated C into a directory nobody has made yet.
 */
TORB_TEST(creating_a_directory_makes_every_level_and_succeeds_twice) {
  torb_text directory = temporary_directory();
  torb_text outer = path_in(directory, "torb-runtime-test-made");
  torb_text inner = path_in(outer, "one/two");
  torb_text path = path_in(inner, "written.txt");
  torb_text contents = torb_text_from_cstring("content");
  torb_text error = torb_text_empty();

  TORB_CHECK(torb_file_create_directory(inner, &error));
  TORB_CHECK(torb_file_is_directory(inner));
  /* Again: a directory that is there is not a failure, so a caller never has to ask first */
  TORB_CHECK(torb_file_create_directory(inner, &error));
  TORB_CHECK(torb_file_write_text(path, contents, &error));
  TORB_CHECK(torb_file_exists(path));

  remove_path(path);
  remove_path(inner);
  {
    torb_text middle = path_in(outer, "one");
    remove_path(middle);
    torb_text_release(middle);
  }
  remove_path(outer);
  torb_text_release(contents);
  torb_text_release(path);
  torb_text_release(inner);
  torb_text_release(outer);
  torb_text_release(directory);
  torb_text_release(error);
}

/** A directory under a *file* cannot be created, and the failure carries the libc message. */
TORB_TEST(creating_a_directory_under_a_file_is_an_error_with_a_message) {
  torb_text directory = temporary_directory();
  torb_text path = path_in(directory, "torb-runtime-test-blocking.txt");
  torb_text under = path_in(path, "inner");
  torb_text contents = torb_text_from_cstring("content");
  torb_text error = torb_text_empty();

  TORB_CHECK(torb_file_write_text(path, contents, &error));
  TORB_CHECK(!torb_file_create_directory(under, &error));
  TORB_CHECK(torb_text_byte_length(error) > 0);

  remove_path(path);
  torb_text_release(contents);
  torb_text_release(under);
  torb_text_release(path);
  torb_text_release(directory);
  torb_text_release(error);
}

void torb_register_file_tests(void) {
  TORB_ADD(writing_reading_and_listing_a_directory);
  TORB_ADD(reading_a_file_that_is_not_there_is_an_error_with_a_message);
  TORB_ADD(reading_a_file_whose_bytes_are_not_utf8_is_an_error);
  TORB_ADD(absolute_paths_are_text_arithmetic);
  TORB_ADD(opening_a_missing_file_is_an_error);
  TORB_ADD(opening_reading_and_double_closing_a_file);
  TORB_ADD(releasing_a_file_without_closing_it_still_closes_the_handle);
  TORB_ADD(creating_a_directory_makes_every_level_and_succeeds_twice);
  TORB_ADD(creating_a_directory_under_a_file_is_an_error_with_a_message);
}
