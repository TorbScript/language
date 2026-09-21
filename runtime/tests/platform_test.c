/*
 * platform_test.c - the boundary to the operating system: a name that is not ASCII, a path over the Windows limit, and
 * the two conversions and the one decision the Windows half is made of.
 *
 * The first two tests are portable and are the ones that matter: a path is UTF-8 above this layer on every platform, so
 * `grüße.txt` is written, listed, read and removed the same way on both and the test says so without knowing which one
 * it is on. What follows them is the only `#ifdef _WIN32` outside `platform.c` in the whole runtime, and it is here
 * because `torb_platform_wide`, `torb_platform_utf8` and `torb_platform_system_path` answer a decision of
 * `docs/PATH.md` (section 6) rather than an implementation detail: which form a path crosses the boundary in.
 *
 * Everything is written under a scratch directory in the temporary directory of the machine and removed again, deepest
 * first - `torb_platform_remove` is what a test has instead of `remove`, which is narrow on Windows and could not
 * delete the file this test writes.
 */

#include "harness.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  include <wchar.h>
#endif

/** The scratch directory of these tests, with forward slashes and no trailing separator. */
static void scratch_directory(char *into, size_t size) {
  const char *names[3] = { "TMPDIR", "TEMP", "TMP" };
  size_t index;
  size_t position;
  for (index = 0u; index < 3u; index += 1u) {
    char *value = NULL;
    size_t length = 0u;
    if (!torb_platform_environment_variable(names[index], &value, &length)) {
      continue;
    }
    snprintf(into, size, "%s/torb-platform-test", value);
    torb_raw_free(value, length + 1u);
    for (position = 0u; into[position] != '\0'; position += 1u) {
      if (into[position] == '\\') {
        into[position] = '/';
      }
    }
    return;
  }
  snprintf(into, size, "%s", "torb-platform-test");
}

/** Whether a listing holds a name. The listing is borrowed; the element `get` hands out is released again. */
static bool holds_name(torb_list entries, const char *name) {
  const torb_text wanted = torb_text_from_cstring(name);
  int64_t index;
  bool found = false;
  for (index = 0; index < torb_list_length(entries); index += 1) {
    torb_text entry = { NULL, 0u, 0u };
    (void)torb_list_get(entries, index, &entry);
    if (torb_text_compare(entry, wanted) == 0) {
      found = true;
    }
    torb_text_release(entry);
  }
  torb_text_release(wanted);
  return found;
}

TORB_TEST(a_name_that_is_not_ascii_is_written_listed_read_and_removed) {
  char directory[256];
  char path[512];
  const char *message = NULL;
  const uint8_t contents[] = "grüße\n";
  uint8_t *read = NULL;
  size_t length = 0u;
  torb_list entries = torb_list_new(&torb_element_text);

  scratch_directory(directory, sizeof directory);
  snprintf(path, sizeof path, "%s/grüße-日本.txt", directory);
  TORB_CHECK(torb_platform_create_directory(directory, &message));
  TORB_CHECK(torb_platform_write_file(path, contents, sizeof contents - 1u, &message));
  TORB_CHECK(torb_platform_path_kind(path) == TORB_PATH_FILE);
  TORB_CHECK(torb_platform_list_directory(directory, &entries, &message));
  TORB_CHECK(holds_name(entries, "grüße-日本.txt"));
  TORB_CHECK(torb_platform_read_file(path, &read, &length, &message));
  TORB_CHECK_INTEGER(length, sizeof contents - 1u);
  TORB_CHECK(memcmp(read, contents, length) == 0);
  torb_raw_free(read, length);
  TORB_CHECK(torb_platform_remove(path));
  TORB_CHECK(torb_platform_path_kind(path) == TORB_PATH_MISSING);

  torb_list_release(entries);
  (void)torb_platform_remove(directory);
}

/**
 * A path far over `MAX_PATH`: three components of 120 characters under the scratch directory, a file inside the deepest
 * one, and the whole tree removed again. Every call on the way (`_wmkdir`, `_wfopen`, `FindFirstFileW`,
 * `GetFileAttributesW`, `RemoveDirectoryW` on Windows) sees the extended-length form, which is the only form over that
 * length any of them accepts.
 */
TORB_TEST(a_path_over_the_windows_limit_is_created_written_and_read) {
  char directory[256];
  char deep[1024];
  char path[1200];
  char component[121];
  const char *message = NULL;
  const uint8_t contents[] = "deep";
  uint8_t *read = NULL;
  size_t length = 0u;
  size_t index;
  torb_list entries = torb_list_new(&torb_element_text);

  memset(component, 'd', sizeof component - 1u);
  component[sizeof component - 1u] = '\0';
  scratch_directory(directory, sizeof directory);
  snprintf(deep, sizeof deep, "%s/%s/%s/%s", directory, component, component, component);
  snprintf(path, sizeof path, "%s/written.txt", deep);
  TORB_CHECK(strlen(path) > 260u);

  TORB_CHECK(torb_platform_create_directory(deep, &message));
  TORB_CHECK(torb_platform_path_kind(deep) == TORB_PATH_DIRECTORY);
  TORB_CHECK(torb_platform_write_file(path, contents, sizeof contents - 1u, &message));
  TORB_CHECK(torb_platform_list_directory(deep, &entries, &message));
  TORB_CHECK(holds_name(entries, "written.txt"));
  TORB_CHECK(torb_platform_read_file(path, &read, &length, &message));
  TORB_CHECK_INTEGER(length, sizeof contents - 1u);
  torb_raw_free(read, length);
  TORB_CHECK(torb_platform_remove(path));

  /* Deepest first, which is what `remove` of a directory needs - and the reason the components are counted back */
  for (index = 0u; index < 3u; index += 1u) {
    char *separator = strrchr(deep, '/');
    TORB_CHECK(torb_platform_remove(deep));
    if (separator != NULL) {
      *separator = '\0';
    }
  }
  torb_list_release(entries);
  (void)torb_platform_remove(directory);
}

#if defined(_WIN32)

/** The two conversions, there and back, over every UTF-8 width including one that needs a surrogate pair. */
TORB_TEST(utf8_and_utf16_convert_in_both_directions) {
  size_t capacity = 0u;
  size_t length = 0u;
  wchar_t *wide = torb_platform_wide("a ü 日 \xF0\x9F\x8E\x89", &capacity);
  char *back;
  TORB_CHECK(wide != NULL);
  /* `a`, `ü` and `日` are one UTF-16 unit each whatever their UTF-8 width is, and the party popper is two */
  TORB_CHECK_INTEGER(wcslen(wide), 8u);
  TORB_CHECK(wide[2] == 0x00FCu);
  TORB_CHECK(wide[4] == 0x65E5u);
  TORB_CHECK(wide[6] == 0xD83Cu && wide[7] == 0xDF89u);
  back = torb_platform_utf8(wide, &length);
  TORB_CHECK(back != NULL);
  TORB_CHECK(strcmp(back, "a ü 日 \xF0\x9F\x8E\x89") == 0);
  torb_raw_free(back, length + 1u);
  torb_raw_free(wide, capacity);
}

TORB_TEST(the_empty_text_converts_in_both_directions) {
  size_t capacity = 0u;
  size_t length = 0u;
  wchar_t *wide = torb_platform_wide("", &capacity);
  char *back;
  TORB_CHECK(wide != NULL);
  TORB_CHECK(wide[0] == L'\0');
  back = torb_platform_utf8(wide, &length);
  TORB_CHECK(back != NULL);
  TORB_CHECK_INTEGER(length, 0u);
  TORB_CHECK(back[0] == '\0');
  torb_raw_free(back, length + 1u);
  torb_raw_free(wide, capacity);
}

/** Bytes that are not UTF-8 have no wide form, and a path made of them is answered like a path that is not there. */
TORB_TEST(bytes_that_are_not_utf8_have_no_wide_form) {
  size_t capacity = 0u;
  const char *message = NULL;
  torb_list entries = torb_list_new(&torb_element_text);
  TORB_CHECK(torb_platform_wide("\xFF\xFE", &capacity) == NULL);
  TORB_CHECK(torb_platform_system_path("\xFF\xFE", &capacity) == NULL);
  TORB_CHECK(torb_platform_path_kind("\xFF\xFE") == TORB_PATH_MISSING);
  TORB_CHECK(torb_platform_open_file("\xFF\xFE", false, &message) == NULL);
  TORB_CHECK(strcmp(message, "No such file or directory") == 0);
  TORB_CHECK(!torb_platform_list_directory("\xFF\xFE", &entries, &message));
  TORB_CHECK(strcmp(message, "No such file or directory") == 0);
  torb_list_release(entries);
}

/** Under the limit a path keeps the form the caller wrote, with `/` turned into `\` and nothing else changed. */
TORB_TEST(a_short_path_keeps_its_plain_form) {
  size_t capacity = 0u;
  wchar_t *system = torb_platform_system_path("build/native/grüße.txt", &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcscmp(system, L"build\\native\\grüße.txt") == 0);
  torb_raw_free(system, capacity);
  /* Absolute and still short: the drive letter and the separators are all that a call gets */
  system = torb_platform_system_path("C:/one/two", &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcscmp(system, L"C:\\one\\two") == 0);
  torb_raw_free(system, capacity);
}

/** A path whose fully qualified form is longer than `MAX_PATH - 13` gets the prefix, and gets it fully qualified. */
TORB_TEST(a_long_path_gets_the_extended_prefix) {
  char path[1024];
  char component[121];
  size_t capacity = 0u;
  wchar_t *system;
  memset(component, 'd', sizeof component - 1u);
  component[sizeof component - 1u] = '\0';
  snprintf(path, sizeof path, "C:/%s/%s/%s", component, component, component);
  system = torb_platform_system_path(path, &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcsncmp(system, L"\\\\?\\C:\\", 7u) == 0);
  TORB_CHECK(wcschr(system, L'/') == NULL);
  torb_raw_free(system, capacity);
}

/** A share, which is where the prefix is not just a prefix: the two leading separators become `UNC\`. */
TORB_TEST(a_long_share_path_gets_the_unc_prefix) {
  char path[1024];
  char component[121];
  size_t capacity = 0u;
  wchar_t *system;
  memset(component, 'd', sizeof component - 1u);
  component[sizeof component - 1u] = '\0';
  snprintf(path, sizeof path, "//server/share/%s/%s/%s", component, component, component);
  system = torb_platform_system_path(path, &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcsncmp(system, L"\\\\?\\UNC\\server\\share\\", 21u) == 0);
  torb_raw_free(system, capacity);
}

/** `.` and `..` are resolved on the way into the extended form, because that form has no meaning for either. */
TORB_TEST(the_extended_form_has_no_dot_components) {
  char path[1024];
  char component[121];
  size_t capacity = 0u;
  wchar_t *system;
  memset(component, 'd', sizeof component - 1u);
  component[sizeof component - 1u] = '\0';
  snprintf(path, sizeof path, "C:/%s/./%s/../%s/%s", component, component, component, component);
  system = torb_platform_system_path(path, &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcsncmp(system, L"\\\\?\\C:\\", 7u) == 0);
  TORB_CHECK(wcsstr(system, L"\\.\\") == NULL);
  TORB_CHECK(wcsstr(system, L"\\..\\") == NULL);
  torb_raw_free(system, capacity);
}

/** A path that already carries the prefix was normalised by whoever wrote it, so it is handed on untouched. */
TORB_TEST(a_path_that_is_already_extended_is_handed_on_as_it_is) {
  size_t capacity = 0u;
  wchar_t *system = torb_platform_system_path("\\\\?\\C:\\one\\two", &capacity);
  TORB_CHECK(system != NULL);
  TORB_CHECK(wcscmp(system, L"\\\\?\\C:\\one\\two") == 0);
  torb_raw_free(system, capacity);
}

#endif

void torb_register_platform_tests(void) {
  TORB_ADD(a_name_that_is_not_ascii_is_written_listed_read_and_removed);
  TORB_ADD(a_path_over_the_windows_limit_is_created_written_and_read);
#if defined(_WIN32)
  TORB_ADD(utf8_and_utf16_convert_in_both_directions);
  TORB_ADD(the_empty_text_converts_in_both_directions);
  TORB_ADD(bytes_that_are_not_utf8_have_no_wide_form);
  TORB_ADD(a_short_path_keeps_its_plain_form);
  TORB_ADD(a_long_path_gets_the_extended_prefix);
  TORB_ADD(a_long_share_path_gets_the_unc_prefix);
  TORB_ADD(the_extended_form_has_no_dot_components);
  TORB_ADD(a_path_that_is_already_extended_is_handed_on_as_it_is);
#endif
}
