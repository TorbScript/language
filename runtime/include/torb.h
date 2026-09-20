/*
 * torb.h - the public ABI of the TorbScript runtime.
 *
 * Everything the C back end emits talks to this header and to nothing else. It is portable C11 with no dependency
 * beyond libc, and it is kept MSVC compatible: no GNU extension outside an `#if`, no variable length array, no
 * `__int128` without a fallback, `_Noreturn` and `_Alignof` behind a macro.
 *
 * Ownership is written down at every function. The three words are the ones docs/BACKEND.md section 2 uses:
 *
 *   borrowed  The function reads the value and does not change its reference count. The caller keeps the count and
 *             must keep the value alive for the duration of the call.
 *   owned     The result carries a count the caller now owns and must release exactly once.
 *   consumed  The function takes over the count of that argument. The caller must not release it afterwards.
 *
 * A `torb_location` is always borrowed: it points into static data the emitter wrote.
 */

#ifndef TORB_H
#define TORB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------------------------------- portability --- */

#if defined(_MSC_VER)
#  define TORB_NORETURN __declspec(noreturn)
#  define TORB_ALIGN_OF(type) __alignof(type)
#else
#  define TORB_NORETURN _Noreturn
#  define TORB_ALIGN_OF(type) _Alignof(type)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define TORB_HAS_OVERFLOW_BUILTINS 1
#else
#  define TORB_HAS_OVERFLOW_BUILTINS 0
#endif

#if defined(__SIZEOF_INT128__)
#  define TORB_HAS_INT128 1
#else
#  define TORB_HAS_INT128 0
#endif

#if defined(_MSC_VER)
#  define TORB_UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
#  define TORB_UNREACHABLE() __builtin_unreachable()
#else
#  define TORB_UNREACHABLE() ((void)0)
#endif

/* The emitter writes this after a call of a function whose result is `Never`, so no compiler misses a return. */

/* --------------------------------------------------------------------------------------- the block header --- */

/**
 * Every counted block begins with this. Eight bytes, so the payload of a block stays aligned for anything.
 *
 * `count` is the plain, non-atomic reference count: every task owns its heap, so nothing has to be atomic
 * (BACKEND 2.5). `TORB_IMMORTAL_COUNT` marks static data, which is never retained, never released and never freed -
 * so a write through a static value always copies.
 *
 * `color` exists for the cycle collector of milestone 7.7 and is `TORB_COLOR_NONE` for everything that is never
 * scanned, which is every block this milestone creates. `kind` is for the leak report and for assertions.
 */
typedef struct torb_header {
  uint32_t count;
  uint16_t kind;
  uint16_t color;
} torb_header;

#define TORB_IMMORTAL_COUNT 0xFFFFFFFFu

typedef enum torb_block_kind {
  TORB_BLOCK_RAW = 0,            /**< A side buffer of a container: no header, only counted by the leak counter. */
  TORB_BLOCK_BYTES = 1,          /**< `torb_bytes`: the storage of a `String`. */
  TORB_BLOCK_LIST_STORAGE = 2,
  TORB_BLOCK_MAP_STORAGE = 3,
  TORB_BLOCK_RECORD = 4,         /**< A `Boxed` layout the emitter allocated. */
  TORB_BLOCK_BOX = 5,            /**< `Box(Item)`: a captured `var` binding. */
  TORB_BLOCK_SHARED = 6,         /**< A `shared type` object. */
  TORB_BLOCK_LAZY = 7,
  TORB_BLOCK_OBJECT = 8,         /**< The boxed payload of a trait-typed value. */
  TORB_BLOCK_ENVIRONMENT = 9,    /**< A closure environment on the heap. */
  TORB_BLOCK_TASK = 10,          /**< Milestone 7.3. */
  TORB_BLOCK_CHANNEL = 11        /**< Milestone 7.3. */
} torb_block_kind;

typedef enum torb_color {
  TORB_COLOR_NONE = 0,
  TORB_COLOR_BLACK = 1,
  TORB_COLOR_GRAY = 2,
  TORB_COLOR_WHITE = 3,
  TORB_COLOR_PURPLE = 4
} torb_color;

/** The header the emitter writes for static data: `static const ... = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), ... }` */
#define TORB_IMMORTAL_HEADER(block_kind) \
  { TORB_IMMORTAL_COUNT, (uint16_t)(block_kind), (uint16_t)TORB_COLOR_NONE }

/**
 * The shape the emitter writes for a string literal: an immortal `torb_bytes` in read-only data, with the byte count
 * spelled out so the flexible array member of `torb_bytes` can be initialized.
 *
 *     TORB_LITERAL_BYTES(s_greeting, 5) = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 5, { 'h','e','l','l','o' } };
 *     torb_text greeting = torb_text_from_storage(&s_greeting, 0, 5);
 */
#define TORB_LITERAL_BYTES(name, byte_count) \
  static const struct { torb_header header; uint32_t capacity; uint8_t data[byte_count]; } name

/* ------------------------------------------------------------------------------------------------- locations --- */

/**
 * Where something is in the source. The path is relative to the workspace root, with forward slashes, so no path of a
 * machine ever reaches the output. Always borrowed: it points into static data.
 */
typedef struct torb_location {
  const char *path;
  uint32_t line;
  uint32_t column;
} torb_location;

#define TORB_LOCATION(source_path, source_line, source_column) \
  ((torb_location){ (source_path), (uint32_t)(source_line), (uint32_t)(source_column) })

/** For a call the emitter cannot attribute to a source position, and for the runtime's own internal panics. */
extern const torb_location torb_location_unknown;

/* ---------------------------------------------------------------------------------------------------- panics --- */

/**
 * A panic writes
 *
 *     panic: <message>
 *       at path/file.trb:12:5
 *
 * to stderr and leaves with exit code 101. Nothing else runs: no release, no `Close`, no destructor (decided gap 9).
 */
#define TORB_PANIC_EXIT_CODE 101

struct torb_text;

/** `message` borrowed. Does not return. This is the `Panic` instruction of the IR. */
TORB_NORETURN void torb_panic(struct torb_text message, torb_location at);

/** The same, for the runtime's own messages. `message` is a NUL-terminated UTF-8 C string, borrowed. */
TORB_NORETURN void torb_panic_text(const char *message, torb_location at);

/** `operation` is the written operator (`"+"`, `"*"`, `"<<"`), borrowed. */
TORB_NORETURN void torb_panic_overflow(const char *operation, torb_location at);
TORB_NORETURN void torb_panic_division_by_zero(const char *operation, torb_location at);
TORB_NORETURN void torb_panic_shift_amount(int64_t amount, int64_t width, torb_location at);
TORB_NORETURN void torb_panic_index_out_of_bounds(int64_t index, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_range_reversed(int64_t from, int64_t to, torb_location at);
TORB_NORETURN void torb_panic_offset_past_end(int64_t offset, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_offset_inside_character(int64_t offset, int64_t length, torb_location at);
TORB_NORETURN void torb_panic_invalid_utf8(int64_t offset, torb_location at);
TORB_NORETURN void torb_panic_out_of_memory(size_t size);
TORB_NORETURN void torb_panic_stack_overflow(torb_location at);

/**
 * The frame counter of decided gap 8: the only portable way to make the C binary and the VM agree on when a
 * recursion is too deep. The emitter calls `torb_enter_frame` at the top of every function that is not a leaf and
 * `torb_leave_frame` before every return.
 */
extern uint32_t torb_frame_limit;
void torb_enter_frame(torb_location at);
void torb_leave_frame(void);

/**
 * A test build replaces what a panic does with this. The hook receives the whole message as it would have been
 * printed (without the trailing newline), borrowed, and is expected not to return - `runtime/tests` longjmps out of
 * it. If it does return anyway, the panic leaves with `TORB_PANIC_EXIT_CODE` after all.
 *
 * Nothing but the test harness sets one: `torb build` never installs a hook, so a panic in a real program is a panic.
 */
typedef void (*torb_panic_hook)(const char *message);
void torb_set_panic_hook(torb_panic_hook hook);

/* ------------------------------------------------------------------------------------------------ allocation --- */

/** Release the counted children of a block. Receives the block itself. `NULL` when the contents are trivial. */
typedef void (*torb_drop_function)(void *block);

/** Retain the counted children of a block, after a shallow copy. Receives the block. `NULL` when trivial. */
typedef void (*torb_retain_children_function)(void *block);

/**
 * A block of `size` bytes with a header of count 1. Result owned. Never returns `NULL`: an allocation that fails
 * panics (there is no out-of-memory error in the language).
 */
void *torb_allocate(size_t size, torb_block_kind kind);

/** The same, with the bytes after the header set to zero. Result owned. */
void *torb_allocate_zeroed(size_t size, torb_block_kind kind);

/**
 * A side buffer without a header: the bucket array and the entry vector of the hash table. Counted by the live-block
 * counter like everything else, so the leak test sees it. Result owned, freed with `torb_raw_free`.
 */
void *torb_raw_allocate(size_t size);
void *torb_raw_allocate_zeroed(size_t size);
void torb_raw_free(void *buffer, size_t size);

/** `block` borrowed; its count goes up by one. A `NULL` block (a niche `None`) and an immortal block are no-ops. */
void torb_retain(void *block);

/**
 * `block` consumed. When the count reaches zero, `drop` is called with the block so it can release the counted values
 * it holds, and then the block is freed. A `NULL` block and an immortal block are no-ops.
 */
void torb_release(void *block, torb_drop_function drop);

/** `block` borrowed. True when this is the only owner, so a write may go through in place. `NULL` is unique. */
bool torb_is_unique(const void *block);

/**
 * Make unique (BACKEND 2.1): `block` consumed, result owned and of count 1, holding the same `size` bytes.
 *
 * When the count is already 1 the same block comes back untouched. Otherwise a shallow copy is made,
 * `retain_children` runs on the copy and the old block is released with `drop`. An immortal block always copies,
 * which is what makes a write to a static value safe.
 */
void *torb_make_unique(void *block,
                       size_t size,
                       torb_retain_children_function retain_children,
                       torb_drop_function drop);

/** Turn a heap block into static data: never retained, never released, never freed. `block` consumed. */
void torb_make_immortal(void *block);

/** How many blocks (including raw side buffers) are live. The leak test asserts this is zero at the end. */
size_t torb_live_block_count(void);

/** `torb build --report-leaks`: writes the live block count to stderr. */
void torb_report_leaks(void);

/* ---------------------------------------------------------------------------------------- element descriptors --- */

/**
 * What one element type of a runtime container is, as static data (BACKEND 3.1). There is one list and one hash table
 * in C, parameterized by this instead of by a template, so the VM and the C back end share them.
 *
 * `retain` and `release` act on one element in place and are `NULL` when the element is trivial, which is what lets
 * the container skip the indirect call entirely. `equals` and `hash` are only needed for a map key or a set item.
 */
typedef struct torb_element {
  uint32_t size, align;
  void (*retain)(void *);
  void (*release)(void *);
  bool (*equals)(const void *, const void *);
  uint64_t (*hash)(const void *);
} torb_element;

/** The descriptors the runtime needs for itself. The emitter emits one per element type, prefix `d`. */
extern const torb_element torb_element_int64;
extern const torb_element torb_element_text;
/** Size zero: the value descriptor of a `Set`, which is the table with nothing on the value side. */
extern const torb_element torb_element_unit;

/* ------------------------------------------------------------------------------------- the ABI of the values --- */

/** The storage of a `String`: UTF-8 bytes with a capacity. A literal is an immortal one in read-only data. */
typedef struct torb_bytes {
  torb_header header;
  uint32_t capacity;
  uint8_t data[];
} torb_bytes;

/**
 * `String`. A slice of one `torb_bytes`, so `text[3..]` is O(1) and shares the storage, and `byteLength()` is a field
 * read. Always valid UTF-8 (decided gap 7). No small-string optimization in v1.
 */
typedef struct torb_text {
  torb_bytes *storage;
  uint32_t offset;
  uint32_t length;
} torb_text;

/** `Char`: a Unicode scalar value. */
typedef uint32_t torb_char;

/** `Void` as a value. */
typedef uint8_t torb_void;

typedef struct torb_list_storage {
  torb_header header;
  const torb_element *element;
  uint32_t length;
  uint32_t capacity;
  /* The elements begin at `torb_list_storage_data`, which rounds this struct's size up to the element's alignment. */
} torb_list_storage;

/** `ArrayList<Item>` and `TrieList<Item>`: contiguous storage, an offset and a length. A slice shares the storage. */
typedef struct torb_list {
  torb_list_storage *storage;
  uint32_t offset;
  uint32_t length;
} torb_list;

/**
 * Open addressing over a bucket array of indices, plus a separate insertion-ordered entry vector. So iteration is
 * insertion order (decided gap 6), a removal leaves a tombstone and does not reorder, and compaction happens when
 * the tombstones pass half of the entries.
 */
typedef struct torb_map_storage {
  torb_header header;
  const torb_element *key;
  const torb_element *value;
  int32_t *buckets;           /**< `bucket_count` slots; -1 is empty. Indices into the entry vector. */
  uint8_t *entries;           /**< `entry_capacity * entry_stride` bytes. */
  uint32_t bucket_count;      /**< A power of two, or zero while the table is empty. */
  uint32_t entry_count;       /**< Entry slots in use, tombstones included. The insertion order is this order. */
  uint32_t entry_capacity;
  uint32_t live_count;        /**< Entries that are not tombstones: what `length()` answers. */
  uint32_t key_offset, value_offset, entry_stride;
} torb_map_storage;

/** `HashMap<Key, Value>` and `TrieMap<Key, Value>`: one counted storage pointer. */
typedef struct torb_map {
  torb_map_storage *storage;
} torb_map;

/** `HashSet<Item>` and `TrieSet<Item>`: the table with `torb_element_unit` on the value side. */
typedef torb_map torb_set;

/**
 * A closure environment. The captures follow the header and the drop function; the emitter generates the typed struct
 * per closure and only these two fields are ABI.
 *
 * The `drop` pointer is here and not at the release site, because a closure **value** has the type of every closure of
 * its shape: which captures are inside one, and therefore which of them a release has to release, is only known to the
 * closure that built it.
 */
typedef struct torb_environment {
  torb_header header;
  torb_drop_function drop;
} torb_environment;

/**
 * The erased shape of a closure value. The emitter generates one struct per signature
 * (`struct { R (*code)(torb_environment *, ...); torb_environment *environment; }`); this is what the runtime passes
 * around when it does not need the signature, and the two are layout compatible.
 */
typedef struct torb_closure {
  void (*code)(void);
  torb_environment *environment;
} torb_closure;

/**
 * `environment` consumed: one count less, and the captures inside it released when it was the last one. This is
 * `Release` for a closure slot.
 *
 * It is a function of the runtime and not two lines in the generated code, because the drop function has to be read out
 * of the block *after* the null check - a closure without captures has no environment at all, and every call site would
 * otherwise repeat that conditional.
 */
void torb_environment_release(torb_environment *environment);

/** The boxed payload of a trait-typed value: a witness member takes `void *self`, so the payload is always boxed. */
typedef struct torb_object {
  torb_header header;
} torb_object;

/** `Box(Item)`: a captured `var` binding. The item follows the header. Colored, because a box may be in a cycle. */
typedef struct torb_box {
  torb_header header;
} torb_box;

typedef enum torb_lazy_state {
  TORB_LAZY_PENDING = 0,
  TORB_LAZY_FORCED = 1
} torb_lazy_state;

/** `lazy Value` as a parameter: a one-shot memo cell. The thunk and the value follow, typed by the emitter. */
typedef struct torb_lazy {
  torb_header header;
  uint8_t state;
} torb_lazy;

/* ------------------------------------------------------------------------------------------------------ text --- */

/** `storage` borrowed; the result borrows it too. Used by the emitter for a literal, which is immortal anyway. */
torb_text torb_text_from_storage(const void *storage, uint32_t offset, uint32_t length);

/** The empty string. Result owned (its storage is immortal, so releasing it is a no-op). */
torb_text torb_text_empty(void);

/**
 * `text` borrowed, result owned: the same slice with one more count on the storage. This is `Retain` for a `Text`
 * slot.
 */
torb_text torb_text_retained(torb_text text);

/** `text` consumed. This is `Release` for a `Text` slot. */
void torb_text_release(torb_text text);

/**
 * A fresh storage of `length` bytes, count 1. `*data_out` points at the bytes, which the caller fills with valid
 * UTF-8 before anything else looks at them. Result owned. Only the runtime builds strings this way.
 */
torb_text torb_text_allocate(uint32_t length, uint8_t **data_out);

/** `bytes` borrowed. Validates UTF-8 and panics at the first bad byte. Result owned. */
torb_text torb_text_from_bytes(const uint8_t *bytes, size_t length, torb_location at);

/**
 * `bytes` borrowed. Validates UTF-8 and answers false instead of panicking, with the byte offset of the first bad
 * byte in `*bad_offset`. `*out` owned on success. This is how reading a file becomes an `IoError` and never a
 * replacement character.
 */
bool torb_text_try_from_bytes(const uint8_t *bytes, size_t length, torb_text *out, size_t *bad_offset);

/** `text` borrowed, NUL terminated, valid UTF-8 or the call panics. Result owned. */
torb_text torb_text_from_cstring(const char *text);

int64_t torb_text_byte_length(torb_text text);
bool torb_text_is_empty(torb_text text);

/** `parts` borrowed. `Intrinsic.TextConcat`: one allocation for the whole interpolation. Result owned. */
torb_text torb_text_concat(const torb_text *parts, size_t count);

/** `String.add`. Both borrowed, result owned. */
torb_text torb_text_add(torb_text first, torb_text second);

/**
 * `text[from..to]` with byte offsets. Every offset is checked (decided gap 7): an offset greater than the byte
 * length, a start greater than the end, and an offset on a UTF-8 continuation byte each panic, with the offset and
 * the length in the message. `text` borrowed, result owned and sharing the storage.
 */
torb_text torb_text_slice(torb_text text, int64_t from, int64_t to, torb_location at);

/** A storage of its own, exactly as long as needed, so a small slice stops pinning a big buffer. Result owned. */
torb_text torb_text_compact(torb_text text);

bool torb_text_equal(torb_text first, torb_text second);
/** -1, 0 or 1: the emitter maps it to `Ordering`. Byte order, which for UTF-8 is code point order. */
int32_t torb_text_compare(torb_text first, torb_text second);
uint64_t torb_text_hash(torb_text text);

bool torb_text_contains(torb_text text, torb_text part);
bool torb_text_starts_with(torb_text text, torb_text prefix);
bool torb_text_ends_with(torb_text text, torb_text suffix);
/** `indexOf`: false when the part is not there. The lowering builds the `Int?` around it. */
bool torb_text_index_of(torb_text text, torb_text part, int64_t *out);

/** All borrowed, all results owned. */
torb_text torb_text_trim(torb_text text);
torb_text torb_text_to_upper_case(torb_text text);
torb_text torb_text_to_lower_case(torb_text text);
torb_text torb_text_replace(torb_text text, torb_text part, torb_text replacement);
torb_text torb_text_repeat(torb_text text, int64_t times, torb_location at);
/** A `List<String>`, element descriptor `torb_element_text`. Result owned. */
torb_list torb_text_split(torb_text text, torb_text separator);

/** `"a\nb"`: quoted, with escapes, as a value inside another value. Result owned. */
torb_text torb_text_show_nested(torb_text text);

/* ------------------------------------------------------------------------------------------------------ UTF-8 --- */

/** How many bytes the scalar value takes in UTF-8, 1 to 4. Zero when it is not a scalar value. */
uint32_t torb_char_byte_length(torb_char character);
/** Writes 1 to 4 bytes and answers how many. `character` must be a scalar value. */
uint32_t torb_utf8_encode(torb_char character, uint8_t out[4]);
/** True when the whole range is valid UTF-8; otherwise `*bad_offset` is the first bad byte. `bytes` borrowed. */
bool torb_utf8_validate(const uint8_t *bytes, size_t length, size_t *bad_offset);
/** True when the byte is a continuation byte, which an offset may not land on. */
bool torb_utf8_is_continuation(uint8_t byte);

/**
 * Walk the characters of a text. `*offset` starts at 0 and ends at the byte length; each call answers the character
 * at the offset and moves the offset past it. `text` borrowed. This is what `chars()` iterates.
 */
bool torb_text_next_char(torb_text text, uint32_t *offset, torb_char *character);

/**
 * The character at a byte offset, or false at (and past) the end. The one native `chars()` needs: decoding UTF-8 is
 * reading raw storage, which the language cannot do at all - there is no `text[i]` - and the cursor of `chars()` is
 * TorbScript over this plus `Char.byteLength()`, so walking a text stays O(1) per character. An offset inside a
 * character panics like every other bad offset (decided gap 7). `text` borrowed.
 */
bool torb_text_char_at(torb_text text, int64_t offset, torb_char *out);

/**
 * The byte at an offset, or false at (and past) the end: what `bytes()` iterates. The same reason as above - a `String`
 * has no index - and a byte is never inside anything, so nothing about this one can panic. `text` borrowed.
 */
bool torb_text_byte_at(torb_text text, int64_t offset, uint8_t *out);

/* ------------------------------------------------------------------------------------------------------ char --- */

bool torb_char_is_digit(torb_char character);
bool torb_char_is_letter(torb_char character);
bool torb_char_is_whitespace(torb_char character);
torb_char torb_char_to_upper_case(torb_char character);
torb_char torb_char_to_lower_case(torb_char character);
int64_t torb_char_byte_length_of(torb_char character);
/** Not every number is a scalar value (the surrogates, everything above 0x10FFFF). */
bool torb_char_try_from_i64(int64_t value, torb_char *out);

/* ------------------------------------------------------------------------------------------- Show, formatting --- */

/** Every result owned. */
torb_text torb_show_bool(bool value);
torb_text torb_show_char(torb_char character);
/** `'A'`: in single quotes, with escapes, as a value inside another value. */
torb_text torb_show_char_nested(torb_char character);
torb_text torb_show_i64(int64_t value);
torb_text torb_show_u64(uint64_t value);
torb_text torb_show_void(torb_void value);

/**
 * The shortest decimal string that parses back to the same `Float64` (decided gap 4), with `.0` appended when the
 * result contains neither `.` nor `e`. `nan`, `inf`, `-inf`; `-0.0` prints as `-0.0`.
 */
torb_text torb_show_f64(double value);
torb_text torb_show_f32(float value);

/** Digits in another base, 2 to 36. `text` borrowed; false when the text is not a number in that base. */
bool torb_parse_i64_digits(torb_text text, int64_t radix, int64_t *out);
/** `Int.parse`: an optional sign and decimal digits, nothing else. `text` borrowed. */
bool torb_parse_i64(torb_text text, int64_t *out);
bool torb_parse_u64(torb_text text, uint64_t *out);
/** `Float.parse`. `text` borrowed. */
bool torb_parse_f64(torb_text text, double *out);
bool torb_parse_bool(torb_text text, bool *out);

/* ------------------------------------------------------------------------------------------------------ list --- */

/** Where the elements of a storage begin: the struct's size rounded up to the element's alignment. */
size_t torb_list_storage_data_offset(const torb_element *element);
/** `storage` borrowed, the pointer borrowed with it. */
void *torb_list_storage_data(torb_list_storage *storage);

/** An empty list over `element`. `element` must be static data. Result owned. */
torb_list torb_list_new(const torb_element *element);
torb_list torb_list_with_capacity(const torb_element *element, int64_t capacity, torb_location at);

/** `list` borrowed, result owned: `Retain` for a list slot. */
torb_list torb_list_retained(torb_list list);
/** `list` consumed: `Release` for a list slot. */
void torb_list_release(torb_list list);

const torb_element *torb_list_element(torb_list list);
int64_t torb_list_length(torb_list list);

/**
 * A borrowed pointer to the element at `index`, bounds checked. Valid until the list is written through. This is
 * what a read of `list[i]` compiles to.
 */
const void *torb_list_at(torb_list list, int64_t index, torb_location at);

/** `get(index): Item?`: false when out of range, otherwise `*out` is a retained copy the caller owns. */
bool torb_list_get(torb_list list, int64_t index, void *out);

/**
 * Make unique and answer a writable interior pointer to the element at `index`. This is the `Element` path step: the
 * pointer is valid for exactly the duration of the access that formed it (BACKEND 2.3).
 */
void *torb_list_element_reference(torb_list *list, int64_t index, torb_location at);

/** Make the list's storage unique so a write may go through in place. */
void torb_list_make_unique(torb_list *list);

/** `value` consumed: the list takes over its count. */
void torb_list_add(torb_list *list, const void *value);
/** `values` borrowed; each element is retained into the list. */
void torb_list_add_all(torb_list *list, torb_list values);
/** `value` consumed; the old element at `index` is released. */
void torb_list_set(torb_list *list, int64_t index, const void *value, torb_location at);
/** `value` consumed. `index` may be the length, which appends. */
void torb_list_insert(torb_list *list, int64_t index, const void *value, torb_location at);
/** `removeAt(index): Item?`: false when out of range, otherwise `*out` is the removed element, owned. */
bool torb_list_remove_at(torb_list *list, int64_t index, void *out);
/** `list[from..to] = values`. `values` borrowed; its elements are retained in. */
void torb_list_replace(torb_list *list, int64_t from, int64_t to, torb_list values, torb_location at);
void torb_list_reverse(torb_list *list);
void torb_list_clear(torb_list *list);
/** A storage of its own, exactly as big as needed. */
void torb_list_compact(torb_list *list);

/** `list` borrowed, result owned and sharing the storage; the slice starts at index 0 again. */
torb_list torb_list_slice(torb_list list, int64_t from, int64_t to, torb_location at);

/** Two elements in written order; -1, 0 or 1. `context` is whatever the caller threaded through. */
typedef int32_t (*torb_compare_function)(const void *first, const void *second, void *context);

/** A stable merge sort, so the order is deterministic whatever the comparison does with equal keys. */
void torb_list_sort(torb_list *list, torb_compare_function compare, void *context);

/* ------------------------------------------------------------------------------------------------ map and set --- */

/** An empty map. Both descriptors must be static data; `key->equals` and `key->hash` must not be `NULL`. */
torb_map torb_map_new(const torb_element *key, const torb_element *value);
torb_map torb_map_with_capacity(const torb_element *key, const torb_element *value, int64_t capacity,
                                torb_location at);

/** `map` borrowed, result owned. */
torb_map torb_map_retained(torb_map map);
/** `map` consumed. */
void torb_map_release(torb_map map);

int64_t torb_map_length(torb_map map);

/** A borrowed pointer to the value for `key`, or `NULL`. `key` borrowed. Valid until the map is written through. */
const void *torb_map_at(torb_map map, const void *key);
/** `get(key): Value?`: false when the key is not there, otherwise `*out` is a retained copy the caller owns. */
bool torb_map_get(torb_map map, const void *key, void *out);
bool torb_map_contains(torb_map map, const void *key);

/**
 * `key` and `value` both consumed. An existing key keeps its position in the insertion order and its stored key; the
 * new key is released and the old value is replaced.
 */
void torb_map_set(torb_map *map, const void *key, const void *value);

/** `remove(key): Value?`. `key` borrowed. `*out` owned on success. A tombstone is left; nothing is reordered. */
bool torb_map_remove(torb_map *map, const void *key, void *out);
void torb_map_clear(torb_map *map);

/**
 * `map[key].add(x)`: take the value out, change it, put it back - without a copy (BACKEND 2.3). `take_out` moves the
 * value out and leaves the entry reserved; `put_back` stores it again. `*out` is owned between the two calls.
 */
bool torb_map_take_out(torb_map *map, const void *key, void *out);
void torb_map_put_back(torb_map *map, const void *key, const void *value);

/**
 * Iterate in insertion order, skipping the tombstones. `*cursor` starts at 0. The pointers are borrowed and valid
 * until the map is written through. `value` may be `NULL` when the caller only wants the keys.
 */
bool torb_map_next(torb_map map, uint32_t *cursor, const void **key, const void **value);

/** A set is the table with `torb_element_unit` on the value side. */
torb_set torb_set_new(const torb_element *item);
torb_set torb_set_retained(torb_set set);
void torb_set_release(torb_set set);
int64_t torb_set_length(torb_set set);
bool torb_set_contains(torb_set set, const void *item);
/** `item` consumed when it was added, released when it was already there. */
void torb_set_add(torb_set *set, const void *item);
bool torb_set_remove(torb_set *set, const void *item);
void torb_set_clear(torb_set *set);
bool torb_set_next(torb_set set, uint32_t *cursor, const void **item);

/* --------------------------------------------------------------------------------------- console and process --- */

/** `text` borrowed. Writes the bytes and a `\n` to stdout. */
void torb_print(torb_text text);
void torb_print_error(torb_text text);
/**
 * `print(...values: Show)`: the shown parts joined by one space, then a `\n`. The join lives here so the two back
 * ends cannot disagree about it. `parts` borrowed.
 */
void torb_print_parts(const torb_text *parts, size_t count);
void torb_print_error_parts(const torb_text *parts, size_t count);

/** `readLine(): String?`: false at end of input. The trailing `\n` (and a `\r` before it) is removed. */
bool torb_read_line(torb_text *out);

/** Called by the generated `main` before anything else. `argument_values` borrowed for the whole run. */
void torb_process_start(int argument_count, char **argument_values);
/**
 * Called where the program ends normally, and by `torb_process_exit`. With `TORB_REPORT_LEAKS=1` in the environment it
 * writes the live block count to stderr, which is the leak gate of the conformance suite.
 */
void torb_process_finish(void);
/** `Process.arguments()`: the program's own name is not in it. Result owned. */
torb_list torb_process_arguments(void);
TORB_NORETURN void torb_process_exit(int64_t code);
/**
 * `Process.runCollecting(command, arguments, var output, var failure)`: a program run to its end. The result is its
 * exit code, and everything it wrote is in `*output` (owned). **-1** means the program could not be started at all,
 * and then `*failure` says why (owned) - a program that ran and failed is an exit code and not a failure of `run`,
 * which is what lets `torb build` tell "there is no C compiler" from "the C compiler said no".
 *
 * The two output streams come back as **one** text: `popen` has one pipe, and `Process.start` with three real pipes
 * is 7.3's. `arguments` borrowed; what `*output` and `*failure` held before is the caller's and is not released here,
 * which is what a `var` parameter of a native means (the wrapper passes a fresh empty text).
 */
int64_t torb_process_run(torb_text command, torb_list arguments, torb_text *output, torb_text *failure);

/* -------------------------------------------------------------------------------------------------- files --- */

/**
 * Minimal file IO: what the compiler itself needs. Every function answers false on failure and puts the message of
 * the `IoError` in `*error` (owned); the lowering builds the `IoError` record and the `Result` around it. Paths are
 * borrowed and must be valid UTF-8, which a `String` always is.
 */
bool torb_file_read_text(torb_text path, torb_text *out, torb_text *error);
bool torb_file_write_text(torb_text path, torb_text text, torb_text *error);
/**
 * `File.createDirectory`: the directory and every directory above it that is missing. "It is already there" is
 * success, because a caller that only wants a place to write should not have to ask first - which is what
 * `torb build` does before it writes the generated C.
 */
bool torb_file_create_directory(torb_text path, torb_text *error);
bool torb_file_exists(torb_text path);
bool torb_file_is_directory(torb_text path);
/** The names of the entries of a directory, sorted by bytes. `.` and `..` are not in it. `*out` owned. */
bool torb_file_list(torb_text path, torb_list *out, torb_text *error);
/**
 * The path as an absolute path with `.` and `..` resolved against the working directory, with forward slashes. Text
 * arithmetic, not a lookup: the file does not have to exist and links are not followed.
 */
bool torb_file_absolute_path(torb_text path, torb_text *out, torb_text *error);

/**
 * `File`: a `native shared type` (BACKEND 3.7) with no fields the language can see, so the runtime owns its whole
 * representation - the same way `torb_list` is what `ArrayList` is. `path` is kept only so the message of an
 * `IoError` raised after the handle has already been closed can still name the file; `handle` is a `FILE *`, owned
 * while non-`NULL` and `NULL` once closed.
 *
 * The language has no destructors, so a `File` the program never closes must still not leak the OS handle:
 * `torb_file_drop` is its `torb_drop_function`, called by `torb_release` when the last reference goes away, and it
 * closes the handle if `close` never ran. `close` itself is idempotent - closing an already-closed file, and reading
 * one, both follow the ordinary `Result`/`IoError` path and never panic, exactly like every other IO failure here.
 *
 * There is no `torb_file_retain`/`torb_file_release`: the header is the first member, so the generic `torb_retain`
 * and `torb_release(block, torb_file_drop)` already work on it, the same as any other counted block.
 */
typedef struct torb_file {
  torb_header header;
  torb_text path;
  void *handle;
} torb_file;

/** `path` borrowed. Opens for reading. Result owned (a fresh block of count 1); `*error` owned on failure. */
bool torb_file_open(torb_text path, torb_file **out, torb_text *error);
/** `self` borrowed: a `shared type` value is the one heap object, never copied. Reads everything left unread. */
bool torb_file_read_all(torb_file *self, torb_text *out, torb_text *error);
/** `self` borrowed. Idempotent. */
void torb_file_close(torb_file *self);
/** The `torb_drop_function` of `torb_file`: closes the handle if `close` never ran, then releases `path`. */
void torb_file_drop(void *block);

/* ---------------------------------------------------------------------------------------- the platform layer --- */

/** Windows and POSIX behind nine functions. `runtime/platform.c` is the only file with an `#ifdef _WIN32`. */

typedef enum torb_path_kind {
  TORB_PATH_MISSING = 0,
  TORB_PATH_FILE = 1,
  TORB_PATH_DIRECTORY = 2
} torb_path_kind;

/** `path` borrowed, NUL terminated. */
torb_path_kind torb_platform_path_kind(const char *path);
/** Result owned, freed with `torb_raw_free`; `*length` is the byte length without the NUL. `NULL` on failure. */
char *torb_platform_working_directory(size_t *length);
/**
 * Every entry of a directory, appended to `*out` as texts, unsorted. False on failure with a libc message in
 * `*message` (borrowed, static).
 */
bool torb_platform_list_directory(const char *path, torb_list *out, const char **message);
/** Read a whole file. `*bytes` owned (`torb_raw_free`). False on failure with a libc message in `*message`. */
bool torb_platform_read_file(const char *path, uint8_t **bytes, size_t *length, const char **message);
bool torb_platform_write_file(const char *path, const uint8_t *bytes, size_t length, const char **message);
/** A monotonic clock reading, in nanoseconds, from an unspecified origin. Never goes backwards within one process. */
/** `mkdir -p`. False on failure with a libc message in `*message` (borrowed, static). */
bool torb_platform_create_directory(const char *path, const char **message);
/**
 * A child process, run to its end. `*code` is its exit code and `*output` its two output streams as one block, owned
 * and freed with `torb_raw_free(*output, *capacity)` - the capacity and not the length, because the buffer grows in
 * doublings and the allocator is told the size it gave out. False only where the process could not be started at all,
 * with a libc message in `*message`.
 */
bool torb_platform_run_process(
  const char *command,
  const char **arguments,
  size_t count,
  int64_t *code,
  uint8_t **output,
  size_t *length,
  size_t *capacity,
  const char **message
);
/** A monotonic clock reading, in nanoseconds, from an unspecified origin. Never goes backwards within one process. */
int64_t torb_platform_monotonic_nanoseconds(void);
/**
 * `name` and `value` borrowed, NUL terminated. Only `runtime/tests` calls this - no native sets an environment
 * variable, so there is nothing above the platform layer to route it through. False on failure.
 */
bool torb_platform_set_environment_variable(const char *name, const char *value);

/* ------------------------------------------------------------------------------------------------------- time --- */

/**
 * `Instant`: one monotonic clock reading, in nanoseconds since an unspecified per-process origin - only the
 * difference of two is ever meaningful, never the value on its own (`std/time`'s own doc comment says so).
 * `Duration`: a signed nanosecond span, `Instant - Instant` or a length of time asked for directly
 * (`Int64.seconds()`). Both are native value types with no fields the language can see, so like `Instant`/`Duration`
 * being plain numbers rather than counted blocks, nanoseconds as `int64_t` is a free choice: about 292 years fit
 * before it overflows, which a monotonic clock reading within one process never approaches.
 */
typedef int64_t torb_instant;
typedef int64_t torb_duration;

/** The monotonic clock. Needs the `std/time` capability inside a sandboxed script (7.4). */
torb_instant torb_clock_now(void);

/**
 * `Clock.milliseconds()`: monotonic milliseconds counted from the **first reading** of the process, which is the form
 * a tool that measures its own work wants (`torb check --timings`). An `Instant` and a `Duration` would be the same
 * number twice and a subtraction; only differences are meaningful either way.
 */
int64_t torb_clock_milliseconds(void);

bool torb_instant_equals(torb_instant first, torb_instant second);
/** -1, 0 or 1: the emitter maps it to `Ordering`. */
int32_t torb_instant_compare(torb_instant first, torb_instant second);
/** `first - second`. Reuses the checked `Int64` subtraction, so a difference that could never happen in practice
 * (billions of years apart) panics instead of silently wrapping. */
torb_duration torb_instant_subtract(torb_instant first, torb_instant second, torb_location at);

bool torb_duration_equals(torb_duration first, torb_duration second);
int32_t torb_duration_compare(torb_duration first, torb_duration second);
/** `"1.5s"`: fractional seconds with the shortest round-tripping decimal (`torb_show_f64`), then `s`. Result owned. */
torb_text torb_duration_show(torb_duration duration);
/** As a fractional number of seconds: `(Clock.now() - start).seconds()`. */
double torb_duration_seconds(torb_duration duration);
/** `Int64.seconds()`: whole seconds to a `Duration`. Panics on overflow like any other multiplication. */
torb_duration torb_duration_of_seconds(int64_t seconds, torb_location at);

/* ---------------------------------------------------------------------------------------------- environment --- */

/**
 * `name` borrowed. `Environment.get`: false when the variable is not set. `*out` owned on success. Capability
 * filtering for a sandboxed script (`environment "APP_*"`) is a front-end concern of milestone 7.4; every variable
 * `getenv` can see is visible here, which is what a native, non-sandboxed program expects.
 */
bool torb_environment_get(torb_text name, torb_text *out);

/* ---------------------------------------------------------------------------------------------------- math --- */

/**
 * Thin wrappers over `<math.h>`. A domain error (`naturalLog` of a non-positive number, `arcSine` outside
 * `[-1, 1]`, ...) answers `nan`, exactly like the float methods already do (`Float64.squareRoot`); none of these ever
 * panics. Results are bit-identical across platforms only where libm itself guarantees it - the runtime does not try
 * to improve on libm.
 */
double torb_math_power(double base, double exponent);
double torb_math_exponential(double value);
double torb_math_natural_log(double value);
/** `logarithm(value, base)`: `naturalLog(value) / naturalLog(base)`. */
double torb_math_logarithm(double value, double base);
double torb_math_sine(double value);
double torb_math_cosine(double value);
double torb_math_tangent(double value);
double torb_math_arc_sine(double value);
double torb_math_arc_cosine(double value);
double torb_math_arc_tangent(double value);
/** `arcTangent(y / x)`, using the sign of both to pick the correct quadrant (`atan2`). */
double torb_math_arc_tangent2(double y, double x);

/* ---------------------------------------------------------------------------------------------------- hashing --- */

/** FNV-1a-64 with the standard offset basis. The seed is fixed, so a hash is the same in every run. */
uint64_t torb_hash_bytes(const void *bytes, size_t length);
uint64_t torb_hash_i64(int64_t value);
uint64_t torb_hash_u64(uint64_t value);
uint64_t torb_hash_bool(bool value);
uint64_t torb_hash_char(torb_char character);
/** Mixes a hash into another, for a composite `Hash`. */
uint64_t torb_hash_combine(uint64_t first, uint64_t second);

#include "torb_number.h"

#endif /* TORB_H */
