/*
 * list_test.c - the one list: copy on write, shared slices, element descriptors with managed elements.
 *
 * The invariant this file exists for: a write goes through in place exactly when this list owns the whole storage
 * alone, and copies exactly once otherwise (BACKEND 2.1). Nothing in TorbScript can see the difference, which is why
 * it is tested here.
 */

#include "harness.h"

static const torb_location somewhere = { "src/list.trb", 11, 2 };

static int64_t whole_at(torb_list list, int64_t index) {
  return *(const int64_t *)torb_list_at(list, index, somewhere);
}

static torb_text text_at(torb_list list, int64_t index) {
  return *(const torb_text *)torb_list_at(list, index, somewhere);
}

static void add_whole(torb_list *list, int64_t value) {
  torb_list_add(list, &value);
}

static void add_text(torb_list *list, const char *value) {
  torb_text text = torb_text_from_cstring(value);
  torb_list_add(list, &text);
}

static int32_t compare_wholes(const void *first, const void *second, void *context) {
  int64_t left = *(const int64_t *)first;
  int64_t right = *(const int64_t *)second;
  (void)context;
  if (left < right) {
    return -1;
  }
  return left > right ? 1 : 0;
}

TORB_TEST(a_fresh_list_grows_and_answers_its_length) {
  torb_list list = torb_list_new(&torb_element_int64);
  int64_t index;
  TORB_CHECK_INTEGER(torb_list_length(list), 0);
  for (index = 0; index < 100; index += 1) {
    add_whole(&list, index * 2);
  }
  TORB_CHECK_INTEGER(torb_list_length(list), 100);
  TORB_CHECK_INTEGER(whole_at(list, 0), 0);
  TORB_CHECK_INTEGER(whole_at(list, 99), 198);
  torb_list_release(list);
}

TORB_TEST(reading_past_the_end_panics_and_get_answers_false) {
  torb_list list = torb_list_new(&torb_element_int64);
  int64_t value = 0;
  add_whole(&list, 7);
  TORB_CHECK(torb_list_get(list, 0, &value));
  TORB_CHECK_INTEGER(value, 7);
  TORB_CHECK(!torb_list_get(list, 1, &value));
  TORB_CHECK(!torb_list_get(list, -1, &value));
  TORB_EXPECT_PANIC(torb_list_at(list, 1, somewhere));
  TORB_CHECK_PANIC_CONTAINS("index 1 is out of bounds for a length of 1");
  torb_list_release(list);
}

TORB_TEST(a_write_goes_through_in_place_when_the_list_is_unique) {
  torb_list list = torb_list_new(&torb_element_int64);
  torb_list_storage *storage;
  int64_t replacement = 9;
  add_whole(&list, 1);
  add_whole(&list, 2);
  storage = list.storage;
  torb_list_set(&list, 0, &replacement, somewhere);
  TORB_CHECK(list.storage == storage);
  TORB_CHECK_INTEGER(whole_at(list, 0), 9);
  torb_list_release(list);
}

TORB_TEST(a_write_copies_exactly_once_when_the_storage_is_shared) {
  torb_list first = torb_list_new(&torb_element_int64);
  torb_list second;
  torb_list_storage *shared;
  int64_t replacement = 9;
  size_t blocks;
  add_whole(&first, 1);
  add_whole(&first, 2);
  second = torb_list_retained(first);
  shared = first.storage;
  blocks = torb_live_block_count();
  torb_list_set(&second, 0, &replacement, somewhere);
  TORB_CHECK(second.storage != shared);
  TORB_CHECK_INTEGER(torb_live_block_count(), blocks + 1u);
  TORB_CHECK_INTEGER(whole_at(first, 0), 1);
  TORB_CHECK_INTEGER(whole_at(second, 0), 9);
  /* The second write is in place again: the copy is unique now. */
  {
    torb_list_storage *own = second.storage;
    replacement = 10;
    torb_list_set(&second, 1, &replacement, somewhere);
    TORB_CHECK(second.storage == own);
  }
  torb_list_release(first);
  torb_list_release(second);
}

TORB_TEST(a_slice_shares_the_storage_and_keeps_it_alive) {
  torb_list list = torb_list_new(&torb_element_int64);
  torb_list window;
  int64_t index;
  for (index = 0; index < 10; index += 1) {
    add_whole(&list, index);
  }
  window = torb_list_slice(list, 3, 6, somewhere);
  TORB_CHECK(window.storage == list.storage);
  TORB_CHECK_INTEGER(torb_list_length(window), 3);
  TORB_CHECK_INTEGER(whole_at(window, 0), 3);
  torb_list_release(list);
  /* The parent is gone; the window still reads. */
  TORB_CHECK_INTEGER(whole_at(window, 2), 5);
  /* A write to a slice copies, because the slice does not own the whole storage. */
  {
    int64_t replacement = 99;
    torb_list_set(&window, 0, &replacement, somewhere);
    TORB_CHECK_INTEGER(window.offset, 0);
    TORB_CHECK_INTEGER(torb_list_length(window), 3);
    TORB_CHECK_INTEGER(whole_at(window, 0), 99);
    TORB_CHECK_INTEGER(whole_at(window, 1), 4);
  }
  torb_list_release(window);
}

TORB_TEST(a_copied_slice_holds_exactly_its_own_elements) {
  torb_list list = torb_list_new(&torb_element_text);
  torb_list window;
  add_text(&list, "first");
  add_text(&list, "second");
  add_text(&list, "third");
  window = torb_list_slice_copied(list, 1, 3, somewhere);
  TORB_CHECK(window.storage != list.storage);
  TORB_CHECK_INTEGER(window.offset, 0);
  TORB_CHECK_INTEGER(torb_list_length(window), 2);
  TORB_CHECK_TEXT(text_at(window, 0), "second");
  /* The parent is released on its own: the copy retained its two elements and nothing else. */
  torb_list_release(list);
  TORB_CHECK_TEXT(text_at(window, 1), "third");
  torb_list_release(window);
}

TORB_TEST(adding_to_a_slice_does_not_reach_the_parent) {
  torb_list list = torb_list_new(&torb_element_int64);
  torb_list window;
  add_whole(&list, 1);
  add_whole(&list, 2);
  add_whole(&list, 3);
  window = torb_list_slice(list, 0, 2, somewhere);
  add_whole(&window, 42);
  TORB_CHECK_INTEGER(torb_list_length(window), 3);
  TORB_CHECK_INTEGER(whole_at(window, 2), 42);
  TORB_CHECK_INTEGER(torb_list_length(list), 3);
  TORB_CHECK_INTEGER(whole_at(list, 2), 3);
  torb_list_release(list);
  torb_list_release(window);
}

TORB_TEST(an_element_reference_makes_the_storage_unique_first) {
  torb_list first = torb_list_new(&torb_element_int64);
  torb_list second;
  torb_text missing = torb_text_from_cstring("Key does not exist");
  int64_t *reference;
  add_whole(&first, 5);
  second = torb_list_retained(first);
  reference = (int64_t *)torb_list_element_reference(&second, 0, missing, somewhere);
  *reference = 6;
  TORB_CHECK_INTEGER(whole_at(first, 0), 5);
  TORB_CHECK_INTEGER(whole_at(second, 0), 6);
  torb_list_release(first);
  torb_list_release(second);
  torb_text_release(missing);
}

/*
 * The panic of an index out of range is the caller's, word for word: the element step carries the message `Index.at`
 * would have handed to `expect`, so the same program says the same thing whether the write went through a copy or
 * through the interior pointer.
 */
TORB_TEST(an_element_reference_out_of_range_panics_with_the_message_it_was_given) {
  torb_list list = torb_list_new(&torb_element_int64);
  torb_text missing = torb_text_from_cstring("Key does not exist");
  add_whole(&list, 5);
  TORB_EXPECT_PANIC(torb_list_element_reference(&list, 3, missing, somewhere));
  TORB_CHECK_PANIC_CONTAINS("Key does not exist");
  TORB_CHECK_PANIC_CONTAINS("src/list.trb:11:2");
  TORB_EXPECT_PANIC(torb_list_element_reference(&list, -1, missing, somewhere));
  TORB_CHECK_PANIC_CONTAINS("Key does not exist");
  torb_list_release(list);
  torb_text_release(missing);
}

TORB_TEST(insert_remove_and_replace) {
  torb_list list = torb_list_new(&torb_element_int64);
  int64_t value = 0;
  int64_t inserted = 0;
  torb_list values = torb_list_new(&torb_element_int64);
  add_whole(&list, 1);
  add_whole(&list, 3);
  inserted = 2;
  torb_list_insert(&list, 1, &inserted, somewhere);
  TORB_CHECK_INTEGER(torb_list_length(list), 3);
  TORB_CHECK_INTEGER(whole_at(list, 1), 2);
  inserted = 0;
  torb_list_insert(&list, 0, &inserted, somewhere);
  TORB_CHECK_INTEGER(whole_at(list, 0), 0);
  inserted = 4;
  torb_list_insert(&list, 4, &inserted, somewhere);
  TORB_CHECK_INTEGER(whole_at(list, 4), 4);
  TORB_CHECK(torb_list_remove_at(&list, 0, &value));
  TORB_CHECK_INTEGER(value, 0);
  TORB_CHECK_INTEGER(torb_list_length(list), 4);
  TORB_CHECK(!torb_list_remove_at(&list, 9, &value));
  add_whole(&values, 7);
  add_whole(&values, 8);
  add_whole(&values, 9);
  torb_list_replace(&list, 1, 3, values, somewhere);
  TORB_CHECK_INTEGER(torb_list_length(list), 5);
  TORB_CHECK_INTEGER(whole_at(list, 0), 1);
  TORB_CHECK_INTEGER(whole_at(list, 1), 7);
  TORB_CHECK_INTEGER(whole_at(list, 3), 9);
  TORB_CHECK_INTEGER(whole_at(list, 4), 4);
  torb_list_release(values);
  torb_list_release(list);
}

TORB_TEST(reverse_clear_and_compact) {
  torb_list list = torb_list_with_capacity(&torb_element_int64, 64, somewhere);
  int64_t index;
  for (index = 0; index < 5; index += 1) {
    add_whole(&list, index);
  }
  torb_list_reverse(&list);
  TORB_CHECK_INTEGER(whole_at(list, 0), 4);
  TORB_CHECK_INTEGER(whole_at(list, 4), 0);
  TORB_CHECK_INTEGER(list.storage->capacity, 64);
  torb_list_compact(&list);
  TORB_CHECK_INTEGER(list.storage->capacity, 5);
  torb_list_clear(&list);
  TORB_CHECK_INTEGER(torb_list_length(list), 0);
  torb_list_release(list);
}

TORB_TEST(sorting_is_stable_and_deterministic) {
  torb_list list = torb_list_new(&torb_element_int64);
  int64_t index;
  for (index = 0; index < 50; index += 1) {
    add_whole(&list, (index * 37) % 50);
  }
  torb_list_sort(&list, compare_wholes, NULL);
  for (index = 0; index < 50; index += 1) {
    TORB_CHECK_INTEGER(whole_at(list, index), index);
  }
  torb_list_release(list);
}

TORB_TEST(a_list_of_strings_retains_and_releases_every_element) {
  size_t before = torb_live_block_count();
  torb_list list = torb_list_new(&torb_element_text);
  torb_list copy;
  add_text(&list, "alpha");
  add_text(&list, "beta");
  add_text(&list, "gamma");
  /* Three storages plus the list's own. */
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 4u);
  copy = torb_list_retained(list);
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 4u);
  /* Writing through the copy copies the buffer and retains every element. */
  {
    torb_text replacement = torb_text_from_cstring("delta");
    torb_list_set(&copy, 0, &replacement, somewhere);
  }
  TORB_CHECK_TEXT(text_at(list, 0), "alpha");
  TORB_CHECK_TEXT(text_at(copy, 0), "delta");
  TORB_CHECK_INTEGER(text_at(list, 1).storage->header.count, 2);
  torb_list_release(copy);
  TORB_CHECK_INTEGER(text_at(list, 1).storage->header.count, 1);
  torb_list_release(list);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

/* The descriptor the emitter writes for `List<List<String>>`: one indirect call per element. */
static void element_list_retain(void *element) {
  torb_retain(((torb_list *)element)->storage);
}

static void element_list_release(void *element) {
  torb_list_release(*(torb_list *)element);
}

static const torb_element element_list_of_text = {
  (uint32_t)sizeof(torb_list), (uint32_t)TORB_ALIGN_OF(torb_list),
  element_list_retain, element_list_release, NULL, NULL
};

TORB_TEST(a_list_of_lists_of_strings_releases_the_whole_tree) {
  size_t before = torb_live_block_count();
  torb_list outer = torb_list_new(&element_list_of_text);
  torb_list shared;
  int64_t group;
  for (group = 0; group < 5; group += 1) {
    torb_list inner = torb_list_new(&torb_element_text);
    int64_t index;
    for (index = 0; index < 4; index += 1) {
      char name[48];
      snprintf(name, sizeof name, "name-%lld-%lld", (long long)group, (long long)index);
      add_text(&inner, name);
    }
    torb_list_add(&outer, &inner);
  }
  TORB_CHECK_INTEGER(torb_list_length(outer), 5);
  TORB_CHECK_TEXT(text_at(*(const torb_list *)torb_list_at(outer, 2, somewhere), 1), "name-2-1");
  /* Sharing the outer list and writing through the copy retains every inner list exactly once more. */
  shared = torb_list_retained(outer);
  {
    torb_list replacement = torb_list_new(&torb_element_text);
    add_text(&replacement, "only");
    torb_list_set(&shared, 0, &replacement, somewhere);
  }
  TORB_CHECK_TEXT(text_at(*(const torb_list *)torb_list_at(outer, 0, somewhere), 0), "name-0-0");
  TORB_CHECK_TEXT(text_at(*(const torb_list *)torb_list_at(shared, 0, somewhere), 0), "only");
  TORB_CHECK_INTEGER(((const torb_list *)torb_list_at(outer, 3, somewhere))->storage->header.count, 2);
  torb_list_release(shared);
  TORB_CHECK_INTEGER(((const torb_list *)torb_list_at(outer, 3, somewhere))->storage->header.count, 1);
  torb_list_release(outer);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

/* A descriptor whose release writes down which element went, so the order a list is taken down in can be read. */
static int64_t released_order[4];
static int64_t released_count = 0;

static void element_recording_release(void *element) {
  if (released_count < 4) {
    released_order[released_count] = *(int64_t *)element;
  }
  released_count += 1;
}

static const torb_element element_recording = {
  (uint32_t)sizeof(int64_t), (uint32_t)TORB_ALIGN_OF(int64_t), NULL, element_recording_release, NULL, NULL
};

TORB_TEST(a_list_releases_its_elements_from_the_last_to_the_first) {
  torb_list list = torb_list_new(&element_recording);
  released_count = 0;
  add_whole(&list, 1);
  add_whole(&list, 2);
  add_whole(&list, 3);
  torb_list_release(list);
  TORB_CHECK_INTEGER(released_count, 3);
  TORB_CHECK_INTEGER(released_order[0], 3);
  TORB_CHECK_INTEGER(released_order[1], 2);
  TORB_CHECK_INTEGER(released_order[2], 1);
}

void torb_register_list_tests(void) {
  TORB_ADD(a_list_releases_its_elements_from_the_last_to_the_first);
  TORB_ADD(a_fresh_list_grows_and_answers_its_length);
  TORB_ADD(reading_past_the_end_panics_and_get_answers_false);
  TORB_ADD(a_write_goes_through_in_place_when_the_list_is_unique);
  TORB_ADD(a_write_copies_exactly_once_when_the_storage_is_shared);
  TORB_ADD(a_slice_shares_the_storage_and_keeps_it_alive);
  TORB_ADD(a_copied_slice_holds_exactly_its_own_elements);
  TORB_ADD(adding_to_a_slice_does_not_reach_the_parent);
  TORB_ADD(an_element_reference_makes_the_storage_unique_first);
  TORB_ADD(an_element_reference_out_of_range_panics_with_the_message_it_was_given);
  TORB_ADD(insert_remove_and_replace);
  TORB_ADD(reverse_clear_and_compact);
  TORB_ADD(sorting_is_stable_and_deterministic);
  TORB_ADD(a_list_of_strings_retains_and_releases_every_element);
  TORB_ADD(a_list_of_lists_of_strings_releases_the_whole_tree);
}
