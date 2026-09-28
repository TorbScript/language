/*
 * map_test.c - the insertion-ordered hash table and the set on top of it.
 *
 * Insertion order is observable through `Show` and through iteration, so it is part of the language (decided gap 6).
 * It has to survive growth, removal, tombstone compaction and copy on write, and that is what this file pins.
 */

#include "harness.h"

static const torb_location somewhere = { "src/map.trb", 3, 8 };

static void set_text_to_whole(torb_map *map, const char *key, int64_t value) {
  torb_text text = torb_text_from_cstring(key);
  torb_map_set(map, &text, &value);
}

static bool get_whole(torb_map map, const char *key, int64_t *out) {
  torb_text text = torb_text_from_cstring(key);
  const void *found = torb_map_at(map, &text);
  torb_text_release(text);
  if (found == NULL) {
    return false;
  }
  *out = *(const int64_t *)found;
  return true;
}

static bool remove_key(torb_map *map, const char *key, int64_t *out) {
  torb_text text = torb_text_from_cstring(key);
  bool removed = torb_map_remove(map, &text, out);
  torb_text_release(text);
  return removed;
}

/** The keys in iteration order, joined with a comma. Result owned. */
static torb_text keys_in_order(torb_map map) {
  uint32_t cursor = 0u;
  const void *key = NULL;
  const void *value = NULL;
  torb_list parts = torb_list_new(&torb_element_text);
  torb_text separator = torb_text_from_cstring(",");
  torb_text result;
  torb_text joined;
  while (torb_map_next(map, &cursor, &key, &value)) {
    torb_text piece = torb_text_retained(*(const torb_text *)key);
    if (torb_list_length(parts) > 0) {
      torb_text comma = torb_text_retained(separator);
      torb_list_add(&parts, &comma);
    }
    torb_list_add(&parts, &piece);
  }
  {
    int64_t index;
    torb_text *flat = (torb_text *)torb_raw_allocate(
        (size_t)(torb_list_length(parts) == 0 ? 1 : torb_list_length(parts)) * sizeof(torb_text));
    for (index = 0; index < torb_list_length(parts); index += 1) {
      flat[index] = *(const torb_text *)torb_list_at(parts, index, somewhere);
    }
    joined = torb_text_concat(flat, (size_t)torb_list_length(parts));
    torb_raw_free(flat, 0u);
  }
  result = joined;
  torb_list_release(parts);
  torb_text_release(separator);
  return result;
}

TORB_TEST(a_map_answers_what_was_put_in) {
  torb_map map = torb_map_new(&torb_element_text, &torb_element_int64);
  int64_t value = 0;
  set_text_to_whole(&map, "a", 1);
  set_text_to_whole(&map, "b", 2);
  set_text_to_whole(&map, "c", 3);
  TORB_CHECK_INTEGER(torb_map_length(map), 3);
  TORB_CHECK(get_whole(map, "b", &value));
  TORB_CHECK_INTEGER(value, 2);
  TORB_CHECK(!get_whole(map, "z", &value));
  torb_map_release(map);
}

TORB_TEST(setting_a_key_again_keeps_its_place_in_the_order) {
  torb_map map = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_text order;
  int64_t value = 0;
  set_text_to_whole(&map, "a", 1);
  set_text_to_whole(&map, "b", 2);
  set_text_to_whole(&map, "c", 3);
  set_text_to_whole(&map, "a", 9);
  TORB_CHECK_INTEGER(torb_map_length(map), 3);
  TORB_CHECK(get_whole(map, "a", &value));
  TORB_CHECK_INTEGER(value, 9);
  order = keys_in_order(map);
  TORB_CHECK_TEXT(order, "a,b,c");
  torb_text_release(order);
  torb_map_release(map);
}

TORB_TEST(insertion_order_survives_growth) {
  torb_map map = torb_map_new(&torb_element_int64, &torb_element_int64);
  int64_t index;
  uint32_t cursor = 0u;
  const void *key = NULL;
  const void *value = NULL;
  int64_t expected = 0;
  for (index = 0; index < 500; index += 1) {
    int64_t doubled = index * 2;
    torb_map_set(&map, &index, &doubled);
  }
  TORB_CHECK_INTEGER(torb_map_length(map), 500);
  while (torb_map_next(map, &cursor, &key, &value)) {
    TORB_CHECK_INTEGER(*(const int64_t *)key, expected);
    TORB_CHECK_INTEGER(*(const int64_t *)value, expected * 2);
    expected += 1;
  }
  TORB_CHECK_INTEGER(expected, 500);
  torb_map_release(map);
}

TORB_TEST(a_removal_leaves_the_rest_in_order) {
  torb_map map = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_text order;
  int64_t value = 0;
  set_text_to_whole(&map, "a", 1);
  set_text_to_whole(&map, "b", 2);
  set_text_to_whole(&map, "c", 3);
  set_text_to_whole(&map, "d", 4);
  TORB_CHECK(remove_key(&map, "b", &value));
  TORB_CHECK_INTEGER(value, 2);
  TORB_CHECK(!remove_key(&map, "b", &value));
  TORB_CHECK_INTEGER(torb_map_length(map), 3);
  order = keys_in_order(map);
  TORB_CHECK_TEXT(order, "a,c,d");
  torb_text_release(order);
  /* A key added after a removal goes to the end, not into the hole. */
  set_text_to_whole(&map, "e", 5);
  order = keys_in_order(map);
  TORB_CHECK_TEXT(order, "a,c,d,e");
  torb_text_release(order);
  torb_map_release(map);
}

TORB_TEST(tombstone_compaction_keeps_the_order_and_finds_everything) {
  torb_map map = torb_map_new(&torb_element_int64, &torb_element_int64);
  int64_t index;
  int64_t removed = 0;
  uint32_t cursor = 0u;
  const void *key = NULL;
  const void *value = NULL;
  int64_t previous = -1;
  int64_t seen = 0;
  for (index = 0; index < 200; index += 1) {
    torb_map_set(&map, &index, &index);
  }
  for (index = 0; index < 200; index += 2) {
    TORB_CHECK(torb_map_remove(&map, &index, &removed));
  }
  TORB_CHECK_INTEGER(torb_map_length(map), 100);
  for (index = 1; index < 200; index += 2) {
    const void *found = torb_map_at(map, &index);
    TORB_CHECK(found != NULL);
    TORB_CHECK_INTEGER(*(const int64_t *)found, index);
  }
  while (torb_map_next(map, &cursor, &key, &value)) {
    TORB_CHECK(*(const int64_t *)key > previous);
    previous = *(const int64_t *)key;
    seen += 1;
  }
  TORB_CHECK_INTEGER(seen, 100);
  /* Everything can be put back and is still found. */
  for (index = 0; index < 200; index += 2) {
    torb_map_set(&map, &index, &index);
  }
  TORB_CHECK_INTEGER(torb_map_length(map), 200);
  torb_map_release(map);
}

TORB_TEST(a_shared_map_copies_on_the_first_write) {
  torb_map first = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_map second;
  torb_map_storage *shared;
  int64_t value = 0;
  set_text_to_whole(&first, "a", 1);
  set_text_to_whole(&first, "b", 2);
  second = torb_map_retained(first);
  shared = first.storage;
  set_text_to_whole(&second, "c", 3);
  TORB_CHECK(second.storage != shared);
  TORB_CHECK_INTEGER(torb_map_length(first), 2);
  TORB_CHECK_INTEGER(torb_map_length(second), 3);
  TORB_CHECK(!get_whole(first, "c", &value));
  TORB_CHECK(get_whole(second, "a", &value));
  TORB_CHECK_INTEGER(value, 1);
  {
    torb_text order = keys_in_order(second);
    TORB_CHECK_TEXT(order, "a,b,c");
    torb_text_release(order);
  }
  torb_map_release(first);
  torb_map_release(second);
}

TORB_TEST(clear_empties_a_map_without_touching_a_copy) {
  torb_map first = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_map second;
  set_text_to_whole(&first, "a", 1);
  second = torb_map_retained(first);
  torb_map_clear(&second);
  TORB_CHECK_INTEGER(torb_map_length(second), 0);
  TORB_CHECK_INTEGER(torb_map_length(first), 1);
  torb_map_release(first);
  torb_map_release(second);
}

/* The descriptor the emitter writes for `Map<String, List<String>>`. */
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

TORB_TEST(a_map_from_string_to_list_releases_the_whole_tree) {
  size_t before = torb_live_block_count();
  torb_map map = torb_map_new(&torb_element_text, &element_list_of_text);
  torb_map copy;
  int64_t group;
  for (group = 0; group < 6; group += 1) {
    char name[32];
    torb_text key;
    torb_list values = torb_list_new(&torb_element_text);
    int64_t index;
    for (index = 0; index < 3; index += 1) {
      char item[48];
      torb_text text;
      snprintf(item, sizeof item, "item-%lld-%lld", (long long)group, (long long)index);
      text = torb_text_from_cstring(item);
      torb_list_add(&values, &text);
    }
    snprintf(name, sizeof name, "group-%lld", (long long)group);
    key = torb_text_from_cstring(name);
    torb_map_set(&map, &key, &values);
  }
  TORB_CHECK_INTEGER(torb_map_length(map), 6);
  {
    torb_text key = torb_text_from_cstring("group-2");
    const torb_list *values = (const torb_list *)torb_map_at(map, &key);
    TORB_CHECK(values != NULL);
    TORB_CHECK_INTEGER(torb_list_length(*values), 3);
    TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(*values, 2, somewhere), "item-2-2");
    torb_text_release(key);
  }
  /* Copy on write retains every inner list exactly once more. */
  copy = torb_map_retained(map);
  {
    torb_text key = torb_text_from_cstring("group-7");
    torb_list values = torb_list_new(&torb_element_text);
    torb_map_set(&copy, &key, &values);
  }
  TORB_CHECK_INTEGER(torb_map_length(map), 6);
  TORB_CHECK_INTEGER(torb_map_length(copy), 7);
  /* `take out`, change, `put back`: the value moves out and back without a copy. */
  {
    torb_text key = torb_text_from_cstring("group-1");
    torb_list values;
    TORB_CHECK(torb_map_take_out(&copy, &key, &values));
    {
      torb_text extra = torb_text_from_cstring("extra");
      torb_list_add(&values, &extra);
    }
    torb_map_put_back(&copy, &key, &values);
    {
      const torb_list *found = (const torb_list *)torb_map_at(copy, &key);
      TORB_CHECK_INTEGER(torb_list_length(*found), 4);
      TORB_CHECK_TEXT(*(const torb_text *)torb_list_at(*found, 3, somewhere), "extra");
    }
    {
      torb_text original = torb_text_from_cstring("group-1");
      const torb_list *found = (const torb_list *)torb_map_at(map, &original);
      TORB_CHECK_INTEGER(torb_list_length(*found), 3);
      torb_text_release(original);
    }
    torb_text_release(key);
  }
  torb_map_release(copy);
  torb_map_release(map);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

TORB_TEST(a_set_is_the_table_with_nothing_on_the_value_side) {
  torb_set set = torb_set_new(&torb_element_text);
  uint32_t cursor = 0u;
  const void *item = NULL;
  int64_t seen = 0;
  const char *names[3] = { "gamma", "alpha", "beta" };
  size_t index;
  for (index = 0u; index < 3u; index += 1u) {
    torb_text text = torb_text_from_cstring(names[index]);
    torb_set_add(&set, &text);
  }
  {
    /* Adding again must not grow the set, and must not leak the second spelling. */
    torb_text again = torb_text_from_cstring("alpha");
    torb_set_add(&set, &again);
  }
  TORB_CHECK_INTEGER(torb_set_length(set), 3);
  {
    torb_text text = torb_text_from_cstring("beta");
    TORB_CHECK(torb_set_contains(set, &text));
    torb_text_release(text);
  }
  {
    torb_text text = torb_text_from_cstring("delta");
    TORB_CHECK(!torb_set_contains(set, &text));
    torb_text_release(text);
  }
  /* Iteration is insertion order, not sorted and not hash order. */
  while (torb_set_next(set, &cursor, &item)) {
    TORB_CHECK_TEXT(*(const torb_text *)item, names[seen]);
    seen += 1;
  }
  TORB_CHECK_INTEGER(seen, 3);
  {
    torb_text text = torb_text_from_cstring("gamma");
    TORB_CHECK(torb_set_remove(&set, &text));
    TORB_CHECK(!torb_set_remove(&set, &text));
    torb_text_release(text);
  }
  TORB_CHECK_INTEGER(torb_set_length(set), 2);
  torb_set_release(set);
}

/**
 * `torb_map_entry_after` is what `MapIterator.next` reaches, and it is the one shape of the walk that hands the caller
 * **owned** copies: the retains it does are what makes a cursor of a map of strings safe to keep past a write to the map.
 */
TORB_TEST(the_entry_cursor_skips_tombstones_and_retains_what_it_answers) {
  torb_map map = torb_map_new(&torb_element_text, &torb_element_int64);
  int64_t cursor = 0;
  torb_text key;
  int64_t value = 0;
  int64_t seen = 0;
  set_text_to_whole(&map, "a", 1);
  set_text_to_whole(&map, "b", 2);
  set_text_to_whole(&map, "c", 3);
  TORB_CHECK(remove_key(&map, "b", &value));
  TORB_CHECK_INTEGER(value, 2);
  /* The hole `b` left is skipped, and the rest keeps its order. */
  TORB_CHECK(torb_map_entry_after(map, &cursor, &key, &value));
  TORB_CHECK_TEXT(key, "a");
  TORB_CHECK_INTEGER(value, 1);
  torb_text_release(key);
  seen += 1;
  TORB_CHECK(torb_map_entry_after(map, &cursor, &key, &value));
  TORB_CHECK_TEXT(key, "c");
  TORB_CHECK_INTEGER(value, 3);
  /* The key is a retained copy: releasing the map leaves it readable. */
  torb_map_release(map);
  TORB_CHECK_TEXT(key, "c");
  torb_text_release(key);
  seen += 1;
  TORB_CHECK_INTEGER(seen, 2);
}

/** Past the end it answers false and leaves the cursor where nothing more can be found. */
TORB_TEST(the_entry_cursor_ends_and_stays_ended) {
  torb_map map = torb_map_new(&torb_element_int64, &torb_element_int64);
  int64_t cursor = 0;
  int64_t key = 0;
  int64_t value = 0;
  int64_t one = 1;
  int64_t two = 2;
  torb_map_set(&map, &one, &two);
  TORB_CHECK(torb_map_entry_after(map, &cursor, &key, &value));
  TORB_CHECK_INTEGER(key, 1);
  TORB_CHECK_INTEGER(value, 2);
  TORB_CHECK(!torb_map_entry_after(map, &cursor, &key, &value));
  TORB_CHECK(!torb_map_entry_after(map, &cursor, &key, &value));
  torb_map_release(map);
}

/** A set is the table with nothing on the value side, so its cursor writes one out parameter and no second one. */
TORB_TEST(the_item_cursor_walks_a_set_in_insertion_order) {
  torb_set set = torb_set_new(&torb_element_text);
  const char *names[] = { "alpha", "beta" };
  int64_t cursor = 0;
  torb_text item;
  size_t index;
  int64_t seen = 0;
  for (index = 0; index < 2u; index += 1) {
    torb_text text = torb_text_from_cstring(names[index]);
    torb_set_add(&set, &text);
  }
  while (torb_set_item_after(set, &cursor, &item)) {
    TORB_CHECK_TEXT(item, names[(size_t)seen]);
    torb_text_release(item);
    seen += 1;
  }
  TORB_CHECK_INTEGER(seen, 2);
  torb_set_release(set);
}

/*
 * The copy an entry cell holds (docs/BACKEND.md, "The entry cell"): a map somebody else holds too, made private inside
 * an immortal region. The copy's storage, its keys and its two buffers are immortal and never freed, so nothing of it
 * may stay in the live count - the buffers of an immortal storage count as immortal (torb_pool.h).
 */
TORB_TEST(a_map_copied_inside_an_immortal_region_leaves_nothing_live) {
  size_t before = torb_live_block_count();
  size_t immortal = torb_immortal_block_count();
  torb_map map = torb_map_new(&torb_element_text, &torb_element_int64);
  torb_map copy;
  int64_t index;
  int64_t found = 0;
  for (index = 0; index < 20; index += 1) {
    char key[32];
    snprintf(key, sizeof key, "key-%lld", (long long)index);
    set_text_to_whole(&map, key, index);
  }
  copy = torb_map_retained(map);
  torb_begin_immortal();
  TORB_CHECK(torb_map_privatize(&copy, torb_text_privatize_place, NULL));
  torb_end_immortal();
  TORB_CHECK(copy.storage != map.storage);
  TORB_CHECK(torb_immortal_block_count() > immortal);
  torb_map_release(map);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
  TORB_CHECK(get_whole(copy, "key-17", &found));
  TORB_CHECK_INTEGER(found, 17);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
  /* Releasing the immortal copy does nothing: it is never freed */
  torb_map_release(copy);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

void torb_register_map_tests(void) {
  TORB_ADD(a_map_answers_what_was_put_in);
  TORB_ADD(setting_a_key_again_keeps_its_place_in_the_order);
  TORB_ADD(insertion_order_survives_growth);
  TORB_ADD(a_removal_leaves_the_rest_in_order);
  TORB_ADD(tombstone_compaction_keeps_the_order_and_finds_everything);
  TORB_ADD(a_shared_map_copies_on_the_first_write);
  TORB_ADD(clear_empties_a_map_without_touching_a_copy);
  TORB_ADD(a_map_from_string_to_list_releases_the_whole_tree);
  TORB_ADD(a_set_is_the_table_with_nothing_on_the_value_side);
  TORB_ADD(the_entry_cursor_skips_tombstones_and_retains_what_it_answers);
  TORB_ADD(the_entry_cursor_ends_and_stays_ended);
  TORB_ADD(the_item_cursor_walks_a_set_in_insertion_order);
  TORB_ADD(a_map_copied_inside_an_immortal_region_leaves_nothing_live);
}
