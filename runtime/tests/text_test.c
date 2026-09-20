/*
 * text_test.c - UTF-8, slices, every offset panic, concatenation, comparison, hashing, and `Show`.
 *
 * The float formatting of decided gap 4 is pinned twice: against a table of the values that catch a wrong algorithm,
 * and by a round-trip property over pseudo-random bit patterns with a fixed seed.
 */

#include "harness.h"

#include <stdlib.h>

static const torb_location somewhere = { "src/text.trb", 7, 3 };

/** A helper for the tests: an owned text from a literal. */
static torb_text text_of(const char *literal) {
  return torb_text_from_cstring(literal);
}

TORB_TEST(a_literal_is_immortal_and_costs_no_count) {
  TORB_LITERAL_BYTES(greeting, 5) = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 5u, { 'h', 'e', 'l', 'l', 'o' } };
  torb_text text = torb_text_from_storage(&greeting, 0u, 5u);
  size_t before = torb_live_block_count();
  torb_text retained = torb_text_retained(text);
  TORB_CHECK_TEXT(retained, "hello");
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
  torb_text_release(retained);
  torb_text_release(text);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

TORB_TEST(a_slice_shares_the_storage_and_keeps_it_alive) {
  torb_text whole = text_of("abcdefgh");
  torb_text part = torb_text_slice(whole, 2, 5, somewhere);
  TORB_CHECK(part.storage == whole.storage);
  TORB_CHECK_INTEGER(part.storage->header.count, 2);
  TORB_CHECK_TEXT(part, "cde");
  TORB_CHECK_INTEGER(torb_text_byte_length(part), 3);
  /* The parent goes away; the slice keeps the storage alive. */
  torb_text_release(whole);
  TORB_CHECK_INTEGER(part.storage->header.count, 1);
  TORB_CHECK_TEXT(part, "cde");
  torb_text_release(part);
}

TORB_TEST(compact_gives_a_small_slice_a_storage_of_its_own) {
  torb_text whole = text_of("0123456789abcdefghijklmnopqrstuvwxyz");
  torb_text part = torb_text_slice(whole, 0, 3, somewhere);
  torb_text small = torb_text_compact(part);
  TORB_CHECK(small.storage != whole.storage);
  TORB_CHECK_INTEGER(small.storage->capacity, 3);
  TORB_CHECK_TEXT(small, "012");
  torb_text_release(small);
  torb_text_release(part);
  torb_text_release(whole);
}

TORB_TEST(an_offset_past_the_end_panics) {
  torb_text text = text_of("abc");
  TORB_EXPECT_PANIC(torb_text_slice(text, 0, 4, somewhere));
  TORB_CHECK_PANIC_CONTAINS("the offset 4 is past the end of a text of 3 bytes");
  TORB_CHECK_PANIC_CONTAINS("at src/text.trb:7:3");
  TORB_EXPECT_PANIC(torb_text_slice(text, 5, 5, somewhere));
  TORB_EXPECT_PANIC(torb_text_slice(text, -1, 2, somewhere));
  torb_text_release(text);
}

TORB_TEST(a_reversed_range_panics) {
  torb_text text = text_of("abc");
  TORB_EXPECT_PANIC(torb_text_slice(text, 2, 1, somewhere));
  TORB_CHECK_PANIC_CONTAINS("the range 2..1 starts after it ends");
  torb_text_release(text);
}

TORB_TEST(an_offset_inside_of_a_character_panics) {
  torb_text text = text_of("a\xC3\xA4z"); /* a, U+00E4, z: four bytes */
  torb_text character = torb_text_slice(text, 1, 3, somewhere);
  TORB_CHECK_INTEGER(torb_text_byte_length(text), 4);
  TORB_CHECK_TEXT(character, "\xC3\xA4");
  torb_text_release(character);
  TORB_EXPECT_PANIC(torb_text_slice(text, 2, 3, somewhere));
  TORB_CHECK_PANIC_CONTAINS("the offset 2 is inside of a character of a text of 4 bytes");
  TORB_EXPECT_PANIC(torb_text_slice(text, 0, 2, somewhere));
  TORB_CHECK_PANIC_CONTAINS("the offset 2 is inside of a character");
  torb_text_release(text);
}

TORB_TEST(utf8_validation_rejects_what_is_not_shortest_or_not_a_scalar_value) {
  size_t bad = 0u;
  torb_text out;
  const uint8_t overlong[2] = { 0xC0u, 0x80u };            /* an overlong NUL */
  const uint8_t surrogate[3] = { 0xEDu, 0xA0u, 0x80u };    /* U+D800 */
  const uint8_t too_large[4] = { 0xF5u, 0x80u, 0x80u, 0x80u };
  const uint8_t truncated[2] = { 0xE2u, 0x82u };
  const uint8_t lone[1] = { 0x80u };
  const uint8_t good[4] = { 0xF0u, 0x9Fu, 0x98u, 0x80u };  /* U+1F600 */
  TORB_CHECK(!torb_utf8_validate(overlong, 2u, &bad));
  TORB_CHECK_INTEGER(bad, 0);
  TORB_CHECK(!torb_utf8_validate(surrogate, 3u, &bad));
  TORB_CHECK(!torb_utf8_validate(too_large, 4u, &bad));
  TORB_CHECK(!torb_utf8_validate(truncated, 2u, &bad));
  TORB_CHECK(!torb_utf8_validate(lone, 1u, &bad));
  TORB_CHECK(torb_utf8_validate(good, 4u, &bad));
  TORB_CHECK(torb_text_try_from_bytes(good, 4u, &out, &bad));
  TORB_CHECK_INTEGER(torb_text_byte_length(out), 4);
  torb_text_release(out);
  TORB_CHECK(!torb_text_try_from_bytes(lone, 1u, &out, &bad));
  TORB_EXPECT_PANIC(torb_text_from_bytes(lone, 1u, somewhere));
  TORB_CHECK_PANIC_CONTAINS("the byte at offset 0 is not valid UTF-8");
}

TORB_TEST(the_characters_of_a_text_come_out_one_by_one) {
  torb_text text = text_of("a\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80");
  uint32_t offset = 0u;
  torb_char character = 0u;
  TORB_CHECK(torb_text_next_char(text, &offset, &character));
  TORB_CHECK_INTEGER(character, 'a');
  TORB_CHECK(torb_text_next_char(text, &offset, &character));
  TORB_CHECK_INTEGER(character, 0xE4);
  TORB_CHECK(torb_text_next_char(text, &offset, &character));
  TORB_CHECK_INTEGER(character, 0x20AC);
  TORB_CHECK(torb_text_next_char(text, &offset, &character));
  TORB_CHECK_INTEGER(character, 0x1F600);
  TORB_CHECK(!torb_text_next_char(text, &offset, &character));
  TORB_CHECK_INTEGER(offset, 10);
  TORB_CHECK_INTEGER(torb_char_byte_length(0x1F600u), 4);
  TORB_CHECK_INTEGER(torb_char_byte_length(0xD800u), 0);
  torb_text_release(text);
}

/**
 * `charAt` and `byteAt`, which is what the TorbScript cursors of `chars()` and `bytes()` pull from: one decode per
 * character with no re-scan, false at and past the end, and a panic on an offset inside a character.
 */
TORB_TEST(a_character_and_a_byte_are_read_at_an_offset) {
  torb_text text = text_of("a\xC3\xA4\xE2\x82\xAC");
  torb_char character = 0u;
  uint8_t byte = 0u;
  TORB_CHECK(torb_text_char_at(text, 0, &character));
  TORB_CHECK_INTEGER(character, 'a');
  TORB_CHECK(torb_text_char_at(text, 1, &character));
  TORB_CHECK_INTEGER(character, 0xE4);
  TORB_CHECK(torb_text_char_at(text, 3, &character));
  TORB_CHECK_INTEGER(character, 0x20AC);
  TORB_CHECK(!torb_text_char_at(text, 6, &character));
  TORB_CHECK(!torb_text_char_at(text, 7, &character));
  TORB_CHECK(!torb_text_char_at(text, -1, &character));
  TORB_CHECK(torb_text_byte_at(text, 0, &byte));
  TORB_CHECK_INTEGER(byte, 'a');
  TORB_CHECK(torb_text_byte_at(text, 1, &byte));
  TORB_CHECK_INTEGER(byte, 0xC3);
  TORB_CHECK(!torb_text_byte_at(text, 6, &byte));
  TORB_EXPECT_PANIC(torb_text_char_at(text, 2, &character));
  TORB_CHECK_PANIC_CONTAINS("the byte at offset 2 is not valid UTF-8");
  torb_text_release(text);
}

/** A slice reads from its own offset, so a cursor over one never sees the storage in front of it. */
TORB_TEST(a_character_of_a_slice_is_read_from_the_slice) {
  torb_text text = text_of("abcdef");
  torb_text tail = torb_text_slice(text, 2, 5, somewhere);
  torb_char character = 0u;
  uint8_t byte = 0u;
  TORB_CHECK(torb_text_char_at(tail, 0, &character));
  TORB_CHECK_INTEGER(character, 'c');
  TORB_CHECK(torb_text_byte_at(tail, 2, &byte));
  TORB_CHECK_INTEGER(byte, 'e');
  TORB_CHECK(!torb_text_char_at(tail, 3, &character));
  torb_text_release(tail);
  torb_text_release(text);
}

TORB_TEST(concatenation_is_one_allocation) {
  torb_text parts[3];
  torb_text joined;
  size_t before;
  parts[0] = text_of("a");
  parts[1] = text_of("bb");
  parts[2] = text_of("ccc");
  before = torb_live_block_count();
  joined = torb_text_concat(parts, 3u);
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 1u);
  TORB_CHECK_TEXT(joined, "abbccc");
  torb_text_release(joined);
  torb_text_release(parts[0]);
  torb_text_release(parts[1]);
  torb_text_release(parts[2]);
  /* Nothing at all: the empty result is the immortal empty storage. */
  before = torb_live_block_count();
  joined = torb_text_concat(parts, 0u);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
  TORB_CHECK_TEXT(joined, "");
  torb_text_release(joined);
}

TORB_TEST(comparison_is_by_bytes_and_hashing_is_deterministic) {
  torb_text first = text_of("apple");
  torb_text second = text_of("apples");
  torb_text third = text_of("apple");
  TORB_CHECK(torb_text_equal(first, third));
  TORB_CHECK(!torb_text_equal(first, second));
  TORB_CHECK_INTEGER(torb_text_compare(first, second), -1);
  TORB_CHECK_INTEGER(torb_text_compare(second, first), 1);
  TORB_CHECK_INTEGER(torb_text_compare(first, third), 0);
  TORB_CHECK(torb_text_hash(first) == torb_text_hash(third));
  TORB_CHECK(torb_text_hash(first) != torb_text_hash(second));
  /* The seed is fixed, so this number is the same in every run and on every platform. */
  TORB_CHECK(torb_hash_bytes("", 0u) == 14695981039346656037ULL);
  torb_text_release(first);
  torb_text_release(second);
  torb_text_release(third);
}

TORB_TEST(searching_a_text) {
  torb_text text = text_of("the quick brown fox");
  torb_text part = text_of("brown");
  torb_text missing = text_of("cat");
  int64_t at = -1;
  TORB_CHECK(torb_text_contains(text, part));
  TORB_CHECK(!torb_text_contains(text, missing));
  TORB_CHECK(torb_text_index_of(text, part, &at));
  TORB_CHECK_INTEGER(at, 10);
  TORB_CHECK(!torb_text_index_of(text, missing, &at));
  {
    torb_text prefix = text_of("the");
    torb_text suffix = text_of("fox");
    TORB_CHECK(torb_text_starts_with(text, prefix));
    TORB_CHECK(torb_text_ends_with(text, suffix));
    TORB_CHECK(!torb_text_ends_with(text, prefix));
    torb_text_release(prefix);
    torb_text_release(suffix);
  }
  torb_text_release(text);
  torb_text_release(part);
  torb_text_release(missing);
}

TORB_TEST(trim_upper_lower_replace_and_repeat) {
  torb_text padded = text_of("  hi\t\n");
  torb_text trimmed = torb_text_trim(padded);
  torb_text upper = torb_text_to_upper_case(trimmed);
  torb_text lower = torb_text_to_lower_case(upper);
  torb_text source = text_of("a-b-c");
  torb_text part = text_of("-");
  torb_text replacement = text_of("__");
  torb_text replaced = torb_text_replace(source, part, replacement);
  torb_text repeated = torb_text_repeat(part, 3, somewhere);
  TORB_CHECK_TEXT(trimmed, "hi");
  TORB_CHECK_TEXT(upper, "HI");
  TORB_CHECK_TEXT(lower, "hi");
  TORB_CHECK_TEXT(replaced, "a__b__c");
  TORB_CHECK_TEXT(repeated, "---");
  torb_text_release(padded);
  torb_text_release(trimmed);
  torb_text_release(upper);
  torb_text_release(lower);
  torb_text_release(source);
  torb_text_release(part);
  torb_text_release(replacement);
  torb_text_release(replaced);
  torb_text_release(repeated);
}

TORB_TEST(split_answers_a_list_of_slices) {
  torb_text source = text_of("a,bb,,c");
  torb_text separator = text_of(",");
  torb_list parts = torb_text_split(source, separator);
  TORB_CHECK_INTEGER(torb_list_length(parts), 4);
  TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(parts, 0, somewhere), "a");
  TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(parts, 1, somewhere), "bb");
  TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(parts, 2, somewhere), "");
  TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(parts, 3, somewhere), "c");
  /* Every piece is a slice of the one storage. */
  TORB_CHECK(((const torb_text *)torb_list_at(parts, 1, somewhere))->storage == source.storage);
  torb_list_release(parts);
  torb_text_release(source);
  torb_text_release(separator);
}

TORB_TEST(show_of_the_primitives) {
  torb_text shown;
  shown = torb_show_bool(true);
  TORB_CHECK_TEXT(shown, "true");
  torb_text_release(shown);
  shown = torb_show_bool(false);
  TORB_CHECK_TEXT(shown, "false");
  torb_text_release(shown);
  shown = torb_show_i64(0);
  TORB_CHECK_TEXT(shown, "0");
  torb_text_release(shown);
  shown = torb_show_i64(INT64_MIN);
  TORB_CHECK_TEXT(shown, "-9223372036854775808");
  torb_text_release(shown);
  shown = torb_show_u64(UINT64_MAX);
  TORB_CHECK_TEXT(shown, "18446744073709551615");
  torb_text_release(shown);
  shown = torb_show_void(0u);
  TORB_CHECK_TEXT(shown, "void");
  torb_text_release(shown);
  shown = torb_show_char(0x20ACu);
  TORB_CHECK_TEXT(shown, "\xE2\x82\xAC");
  torb_text_release(shown);
  shown = torb_show_char_nested('A');
  TORB_CHECK_TEXT(shown, "'A'");
  torb_text_release(shown);
  shown = torb_show_char_nested('\n');
  TORB_CHECK_TEXT(shown, "'\\n'");
  torb_text_release(shown);
  shown = torb_show_char_nested('\'');
  TORB_CHECK_TEXT(shown, "'\\''");
  torb_text_release(shown);
}

TORB_TEST(show_nested_quotes_and_escapes) {
  torb_text plain = text_of("a\nb\"c\\d");
  torb_text shown = torb_text_show_nested(plain);
  torb_text empty = text_of("");
  torb_text shown_empty = torb_text_show_nested(empty);
  TORB_CHECK_TEXT(shown, "\"a\\nb\\\"c\\\\d\"");
  TORB_CHECK_TEXT(shown_empty, "\"\"");
  torb_text_release(plain);
  torb_text_release(shown);
  torb_text_release(empty);
  torb_text_release(shown_empty);
}

TORB_TEST(character_classification) {
  TORB_CHECK(torb_char_is_digit('7'));
  TORB_CHECK(!torb_char_is_digit('x'));
  TORB_CHECK(torb_char_is_letter('x'));
  TORB_CHECK(torb_char_is_letter(0xE4u));
  TORB_CHECK(!torb_char_is_letter('7'));
  TORB_CHECK(!torb_char_is_letter(0xD7u));
  TORB_CHECK(torb_char_is_whitespace(' '));
  TORB_CHECK(torb_char_is_whitespace(0xA0u));
  TORB_CHECK(!torb_char_is_whitespace('x'));
  TORB_CHECK_INTEGER(torb_char_to_upper_case('a'), 'A');
  TORB_CHECK_INTEGER(torb_char_to_lower_case('A'), 'a');
  {
    torb_char character = 0u;
    torb_text message = torb_text_empty();
    TORB_CHECK(torb_char_try_from_i64(65, &character, &message));
    TORB_CHECK_INTEGER(character, 65);
    TORB_CHECK(!torb_char_try_from_i64(0xD800, &character, &message));
    /* The `message` of the `NumberRangeError` is the value that went out of range, which only the runtime has */
    TORB_CHECK_TEXT(message, "55296");
    torb_text_release(message);
    TORB_CHECK(!torb_char_try_from_i64(0x110000, &character, &message));
    torb_text_release(message);
    TORB_CHECK(!torb_char_try_from_i64(-1, &character, &message));
    TORB_CHECK_TEXT(message, "-1");
    torb_text_release(message);
  }
}

/* --- float formatting ---------------------------------------------------------------------------------------- */

typedef struct float_case {
  double value;
  const char *expected;
} float_case;

TORB_TEST(float_formatting_is_the_shortest_round_trip) {
  double nothing = 0.0;
  static const float_case cases[] = {
    { 0.0, "0.0" },
    { 1.0, "1.0" },
    { 6.0, "6.0" },
    { -1.5, "-1.5" },
    { 0.1, "0.1" },
    { 0.3, "0.3" },
    { 0.1 + 0.2, "0.30000000000000004" },
    { 3.141592653589793, "3.141592653589793" },
    { 2.718281828459045, "2.718281828459045" },
    { 123456789.125, "123456789.125" },
    { 1e21, "1e21" },
    { 1e22, "1e22" },
    { 1e-7, "1e-7" },
    { 2.5e-5, "0.000025" },
    { 5e-324, "5e-324" },
    { 1.7976931348623157e308, "1.7976931348623157e308" },
    { 2.2250738585072014e-308, "2.2250738585072014e-308" },
    { 9007199254740993.0, "9007199254740992.0" },
    { 1e16, "10000000000000000.0" },
    { 1e20, "100000000000000000000.0" },
    { 100.0, "100.0" },
    { -1e-6, "-0.000001" }
  };
  size_t index;
  for (index = 0u; index < sizeof cases / sizeof cases[0]; index += 1u) {
    torb_text shown = torb_show_f64(cases[index].value);
    if ((size_t)shown.length != strlen(cases[index].expected)
        || memcmp(shown.storage->data + shown.offset, cases[index].expected, (size_t)shown.length) != 0) {
      char message[256];
      snprintf(message, sizeof message, "%.*s, expected %s", (int)shown.length,
               (const char *)(shown.storage->data + shown.offset), cases[index].expected);
      torb_test_failed(__FILE__, __LINE__, message);
      torb_text_release(shown);
      return;
    }
    torb_text_release(shown);
  }
  {
    torb_text shown = torb_show_f64(-0.0);
    TORB_CHECK_TEXT(shown, "-0.0");
    torb_text_release(shown);
  }
  {
    torb_text shown = torb_show_f64(nothing / nothing);
    TORB_CHECK_TEXT(shown, "nan");
    torb_text_release(shown);
  }
  {
    torb_text shown = torb_show_f64(1.0 / nothing);
    TORB_CHECK_TEXT(shown, "inf");
    torb_text_release(shown);
  }
  {
    torb_text shown = torb_show_f64(-1.0 / nothing);
    TORB_CHECK_TEXT(shown, "-inf");
    torb_text_release(shown);
  }
}

TORB_TEST(every_float_bit_pattern_round_trips) {
  uint64_t state = 0x853C49E6748FEA9BULL; /* A fixed seed: the property is the same in every run. */
  size_t checked = 0u;
  size_t attempts;
  for (attempts = 0u; attempts < 50000u && checked < 20000u; attempts += 1u) {
    double value;
    double parsed = 0.0;
    torb_text shown;
    char buffer[64];
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    {
      uint64_t bits = state * 2685821657736338717ULL;
      memcpy(&value, &bits, sizeof value);
    }
    if (value != value || value > 1.0e308 * 1.9 || value < -1.0e308 * 1.9) {
      continue; /* `nan` and the infinities have their own spelling and no round trip to check. */
    }
    shown = torb_show_f64(value);
    if (shown.length >= sizeof buffer) {
      torb_text_release(shown);
      torb_test_failed(__FILE__, __LINE__, "a formatted float does not fit in 64 bytes");
      return;
    }
    memcpy(buffer, shown.storage->data + shown.offset, (size_t)shown.length);
    buffer[shown.length] = '\0';
    parsed = strtod(buffer, NULL);
    if (memcmp(&parsed, &value, sizeof value) != 0) {
      char message[256];
      snprintf(message, sizeof message, "%s does not parse back to the same float", buffer);
      torb_text_release(shown);
      torb_test_failed(__FILE__, __LINE__, message);
      return;
    }
    torb_text_release(shown);
    checked += 1u;
  }
  TORB_CHECK(checked >= 20000u);
}

TORB_TEST(parsing_floats) {
  double value = 0.0;
  torb_text text = text_of("3.5");
  torb_text bad = text_of("3.5x");
  TORB_CHECK(torb_parse_f64(text, &value));
  TORB_CHECK(value == 3.5);
  TORB_CHECK(!torb_parse_f64(bad, &value));
  torb_text_release(text);
  torb_text_release(bad);
}

void torb_register_text_tests(void) {
  TORB_ADD(a_literal_is_immortal_and_costs_no_count);
  TORB_ADD(a_slice_shares_the_storage_and_keeps_it_alive);
  TORB_ADD(compact_gives_a_small_slice_a_storage_of_its_own);
  TORB_ADD(an_offset_past_the_end_panics);
  TORB_ADD(a_reversed_range_panics);
  TORB_ADD(an_offset_inside_of_a_character_panics);
  TORB_ADD(utf8_validation_rejects_what_is_not_shortest_or_not_a_scalar_value);
  TORB_ADD(the_characters_of_a_text_come_out_one_by_one);
  TORB_ADD(a_character_and_a_byte_are_read_at_an_offset);
  TORB_ADD(a_character_of_a_slice_is_read_from_the_slice);
  TORB_ADD(concatenation_is_one_allocation);
  TORB_ADD(comparison_is_by_bytes_and_hashing_is_deterministic);
  TORB_ADD(searching_a_text);
  TORB_ADD(trim_upper_lower_replace_and_repeat);
  TORB_ADD(split_answers_a_list_of_slices);
  TORB_ADD(show_of_the_primitives);
  TORB_ADD(show_nested_quotes_and_escapes);
  TORB_ADD(character_classification);
  TORB_ADD(float_formatting_is_the_shortest_round_trip);
  TORB_ADD(every_float_bit_pattern_round_trips);
  TORB_ADD(parsing_floats);
}
