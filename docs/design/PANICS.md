# Panics

**Status: decided (2026-09-24), implemented except where the last section says otherwise.** — this record takes stock of every way a
TorbScript program that looks correct can end in a panic, compares the choice with nine other languages, and proposes
one principle and the changes that follow from it. Every count and every message in sections 1 to 7 was measured on
commit `1ae963fa`, before any of it was built, and section 2.1 says how; the sections "Decided" and "Implemented" at the
end say what became of it.

**Most panics of the language are the right ones, and one is not.** An index past the end of a list, a division by zero
and an integer overflow fail on a condition the program can state and check - `index < list.length()`,
`divisor != 0` - and that Rust, Swift and Go treat the same way. A byte offset inside a character is different in kind:
its condition is a property of the *data*, invisible in the program and absent from every test that uses ASCII. The
line that states the rule in CONCEPT.md is itself wrong twice (section 1). So the proposal is a text index that only a
text can make, a total form for every partial operation, a panic that names the program's line and not the standard
library's, and a compiler that removes the checks it can prove and rejects the failures it can see.

```text
            what the failure depends on                      what the language should do with it

  ┌────────────────────────────────────────────┐
  │ an assertion the program wrote             │  expect, panic, assert       → a panic. Nothing to change
  ├────────────────────────────────────────────┤
  │ a condition the program can state          │  list[i], a / b, a + b       → a panic, a total twin beside it,
  │ as an ordinary expression                  │  map[key], list[a..b]          and the compiler drops the check it
  │                                            │                                proves and refuses the failure it sees
  ├────────────────────────────────────────────┤
  │ a property of the data that no expression  │  text[3..] on "Grüße"        → must not be expressible:
  │ of the program mentions                    │  readLine() of bad bytes       a type (TextIndex) or a Result
  ├────────────────────────────────────────────┤
  │ the machine                                │  stack, memory, 4 GiB,       → a panic, as in every language
  │                                            │  deadlock
  └────────────────────────────────────────────┘
```

- **[1. The question, and the line that answers it wrong](#1-the-question-and-the-line-that-answers-it-wrong)**
- **[2. Inventory](#2-inventory)** — every panic a correct-looking program can reach, with its message, its total twin and its count
- **[3. How other languages handle it](#3-how-other-languages-handle-it)** — Rust, Swift, Kotlin and Java, Go, Zig and Odin, Elm, Haskell and Gleam, Pony, Scala
- **[4. Text](#4-text)** — whether an offset into a text should be an `Int` at all
- **[5. A principle for the whole language](#5-a-principle-for-the-whole-language)** — the wording, and six variants weighed
- **[6. Recommendations](#6-recommendations)** — ordered by value, each with its cost and its migration
- **[7. What this is not](#7-what-this-is-not)**
- **[8. Open](#8-open)** — the owner's decisions, each with a recommendation

---

## 1. The question, and the line that answers it wrong

The owner, on CONCEPT.md's section "Strings":

> `const tail = text[3..]  // Slicing with offsets is O(1). An offset inside of a character panics.` - I find that
> somehow bad; I have the feeling that a lot can simply panic if you do it wrong.

The example around that line, run as it is written:

```trb
const text = "Grüße 👋"
print text.indexOf("ß")
print text.byteLength()
const tail = text[3..]
print tail
```

```console
$ torb run concept-strings.trb
Some(4)
12
panic: the offset 3 is inside of a character of a text of 12 bytes
  at std/text/src/lib.trb:131:5
```

CONCEPT.md says `Some(3)` and `13`. `G` and `r` are one byte each and `ü` is two, so `ß` begins at byte 4; the text is
`1 + 1 + 2 + 2 + 1 + 1 + 4 = 12` bytes. The offset 3 is the second byte of `ü`, and the line that explains the rule
panics on the text it was written next to. Nobody made a careless mistake here: counting UTF-8 bytes by eye is exactly
the thing people cannot do, which is the whole argument of section 4. The owner's feeling is right about text; section 2
checks whether it is right about the rest.

## 2. Inventory

### 2.1 How it was counted

- **Messages and sites** are the output of programs built with `torb build --profile dev` (what `torb run` builds) and
  run on Windows x64, one per failure, not quoted from the documentation.
- **Syntactic counts** come from a script over all 611 `.trb` files of the repository (161 773 lines) that blanks out
  comments and the literal parts of strings but keeps interpolated code. `a[i]` is an index read when the `[` follows a
  name, a `)` or a `]` directly and the brackets hold no top-level `..`; with one it is a slice; followed by `=` it is a
  write. The script cannot tell a `List` from a `Map` or an `Array`, so those three are one number.
- **Text slices are exact.** `torb ir compiler/src/main.trb` (60 MB of IR) names every call of `String.slice` with its
  site: 137 sites, 2 of them inside `std/text` itself, **135 in the compiler**.
- The columns are the compiler (`compiler/src`, 155 files, 91 843 lines), `std/` without its tests (111 files,
  16 794 lines), the tests (`tests/`, `compiler/tests`, the tests of `std/`, 266 files, 42 581 lines) and `examples/`
  (60 files, 10 014 lines). Nineteen other files (`tools/`, `benchmarks/`) are in the totals only.

### 2.2 Arithmetic and conversions

| Operation | Defined in | Message | Site printed | Total twin | Uses |
|---|---|---|---|---|---|
| `+`, `-`, `*` past the range of the type | `runtime/include/torb_number.h` | ``arithmetic overflow in `+` `` | the program's | **none** on the signed types; `UInt64` alone has `addedWrapping` and `multipliedWrapping` | every `+`; 0 calls of the wrapping pair |
| unary `-` and `absolute()` of the smallest value | `torb_number.h` | ``arithmetic overflow in `-` `` - for `absolute()` too | `std/number/src/lib.trb:51:6` for `absolute()` | none | 46 `absolute()` |
| `/` and `%` by zero, the smallest signed value `/ -1` | `torb_number.h` | ``division by zero in `/` `` | the program's | none | 161 `/`, 53 of them by a literal; 76 `%`, 58 by a literal |
| `shiftedLeft(by:)`, `shiftedRight(by:)` outside `0..width` | `torb_number.h` | `shift by 65, which is not between 0 and 63` | the program's | none | 9 |
| narrowing and float-to-integer conversions | `std/number` | - | - | **all of them are `TryFrom`** and answer a `Result` | 137 `tryFrom` |
| `wholeOf(Float)` | `std/linear/src/vector2.trb` | the `NumberRangeError`'s message | `std/linear` | `Int.tryFrom` | 17, all in `std/linear` |

Conversions are already where the rest should be: nothing converts a number by panicking except the one convenience
`wholeOf`, and it names its total twin in its documentation.

### 2.3 Indexing and slicing of collections

| Operation | Defined in | Message | Site printed | Total twin | Uses (compiler / std / tests / examples, total) |
|---|---|---|---|---|---|
| `list[i]`, `array[i]`, `map[key]` read | `Indexed.at` in `std/core/src/operators.trb`: `get(key).expect("Key does not exist")` | `Key does not exist` - no index, no length, no key | `std/core/src/option.trb:172:15`, for all three | `get` answers an `Option` | **2 297** reads (1 664 / 131 / 309 / 183) against **505** `.get(` (397 / 2 / 43 / 62) |
| `list[i] = value` | `ArrayList.set`, native `torb_list_set` | `index 6 is out of bounds for a length of 3` | `std/collections/src/list.trb:539:17` | none | 82 writes `a[i] = v` of all three kinds (1 / 14 / 39 / 27); the compiler writes through paths such as `a[i].field = v` instead |
| `array[i] = value` | `Array.set`, `std/core/src/array.trb` | the same | `std/core/src/array.trb:165:7` | none; a literal index out of bounds is a compile error | in the 82 |
| `map[key] = value` | `HashMap.set`, `TrieMap.set` | - | - | never panics | in the 82 |
| `list[from..to]` past the end, or reversed | `windowOf`, `std/collections/src/list.trb` | `index 8 is out of bounds for a length of 3`, `the range 2..1 starts after it ends` | `std/collections/src/list.trb:430:5` | **none** | about 30 in the compiler (165 slices, 135 of them texts); 245 slices of lists and texts together |
| `list.insert(i, value)` with `i > length()` | native `torb_list_insert` | `index 6 is out of bounds for a length of 3` | `std/collections/src/list.trb:542:17` | none | 48 |
| `list.removeAt(i)` | native | - | - | answers `Item?`, never panics | 55 |
| `swapAt`, `update(index)` | through `self[index]` | `Key does not exist` | `std/core/src/option.trb` | none | 2, 7 |

Of the compiler's 1 318 reads of the simple form `receiver[name]` or `receiver[0]`, **409 (31 %) stand in a file with
`for name in 0..receiver.length()`** - a loop that proves them - 221 have a literal index (185 of them `[0]`, beside a
`first()` that answers an `Option`), and 688 index with something else: an identifier's number into a table, a key.

### 2.4 Text

| Operation | Defined in | Message | Site printed | Total twin | Uses |
|---|---|---|---|---|---|
| `text[from..to]`, `text.slice(range)` with an offset inside a character | `String.slice`, native `torb_text_slice` in `runtime/text.c` | `the offset 3 is inside of a character of a text of 12 bytes` | `std/text/src/lib.trb:131:5` | **none** | **135 in the compiler**, exact |
| the same past the end, or reversed | the same | `the offset 7 is past the end of a text of 3 bytes`, `the range 2..1 starts after it ends` | the same | none | in the 135 |
| `text.charAt(offset)` inside a character | native `torb_text_char_at` | `the byte at offset 1 is not valid UTF-8` - **the wrong message, and no site** | none | none; it answers `None` past the end but panics inside | 7 |
| `text.byteAt(offset)` | native | - | - | never panics: a byte is inside nothing | 2 |
| `text.repeat(times)` with a negative count | native `torb_text_repeat` | `index -1 is out of bounds for a length of 0` - a message about an index that does not exist | the program's | none | 10 |
| `indexOf`, `lastIndexOf` | natives | - | - | answer `Int?`; the offsets they answer are what the slices above take | 44 |
| `substringBefore`, `substringAfter` | `std/text` | - | - | answer `String?` | 20 |
| `readLine()` of bytes that are not UTF-8 | `runtime/console.c` | `the byte at offset 2 is not valid UTF-8` | none | **none: input from outside the program panics** | 3 |

The last row contradicts CONCEPT's own promise that "reading a file whose bytes are not UTF-8 is an `IoError`":
`printf 'ab\xffcd\n' | program` ends the program with 101. It is the clearest case in this inventory of a panic that
depends on nothing but input.

### 2.5 `Option`, `Result`, assertions and `panic`

| Operation | Defined in | Message | Site printed | Uses |
|---|---|---|---|---|
| `Option.expect(message)` on `None` | `std/core/src/option.trb` | the message | `std/core/src/option.trb:172:15` | **104** `expect` (compiler 2, std 1, tests 99, examples 2) against **487** `??` and **50** `?.` |
| `Result.expect(message)` on `Fail` | `std/core/src/result.trb` | `<message>: <error>` | `std/core/src/result.trb:161` | in the 104 |
| `assert(condition)` | `std/expression/src/lib.trb` | `Assertion failed: <source> (<captures>) at <site>` | `std/expression`, with the program's site in the message | 4 244, 4 162 of them in tests |
| `panic "..."` | `std/core/src/void.trb`, native | the message | the program's | 124 (compiler 3, std 11, tests 97, examples 13) |

There is no `unwrap`: the one way to assert that an `Option` holds a value is `expect`, and it has to say why. The
compiler uses it twice in 91 843 lines. That part of the design works.

### 2.6 The standard library

Thirty-seven places in the doc comments of `std/` name a panic. Besides the rows above:

- **`std/linear`**: `Fixed.approximating` of a `nan` or of a magnitude above about 1.4·10¹⁴, `Fixed` multiplication
  and division past the range of an `Int`, `Fixed.arcSine` outside `[-1, 1]`, a vector divided by a zero scalar or
  component, the projection onto a zero vector, the angle between zero vectors, `Angle.tangent` at a quarter turn, and a
  matrix's `cell`, `row` and `column` outside `0..n`. None of them has a total twin; `Float64` answers `nan` or an
  infinity in the same places.
- **`std/time`**: the sum of two `Duration`s that leaves the range of an `Int64`, "as every `Int64` addition does".
- **`std/task`**: a channel with a negative capacity (`the capacity of a channel cannot be negative, and it is -1`), a
  task that awaits itself, and a deadlock - every task waiting and none left to wake one.

### 2.7 The machine, tasks and destructors

| Failure | Message | Where |
|---|---|---|
| Stack overflow | `stack overflow: the recursion is deeper than the stack of the thread` | one comparison at the entry of every function that calls another (`runtime/panic.c`, PERFORMANCE F15) |
| Out of memory | `out of memory: <n> bytes could not be allocated`, no site | `runtime/memory.c` |
| A text over 4 GiB, a list over 4 294 967 295 items, a map, a channel or a task frame over its limit | `a text longer than 4 GiB is not supported`, and so on | `runtime/text.c`, `list.c`, `map.c`, `task.c` |
| A panic inside a task | the panic, then exit 101: it does not become the awaiter's `Result` | `docs/design/CONCURRENCY.md` section 8 |
| A panic while a `close()` is due | nothing runs: no `close()`, no `using` cleanup | `docs/design/DESTRUCTORS.md` |
| `internal error: ...` | about a dozen invariants of the runtime (a count released twice, a `close()` that kept its object) | not reachable by a correct program; the checker rules out the one a program could provoke (`self` escaping `close()`) |

A panic inside a sandboxed script is the one that is recovered, as a `SandboxError` for the host; under `torb test` a
panic fails its test and the next test runs.

### 2.8 What the checker already refuses

- a literal that does not fit its type (`` `300` does not fit into `Int8` ``);
- a literal index into an `Array<Item, Size>` past its size, with a note that names `get` ("How many items there are is
  known here, so an index that is written out is checked instead of panicking");
- `for index in ..10` and `(0..).length()`, "so nothing has to panic" (CONCEPT.md, the ranges).

What it lets through, each checked with `torb check` on this commit: `10 / 0`, `[1, 2, 3][5]`, `["a": 1]["b"]`,
`"abc"[5..]` and `Int.maximum + 1`. Every one of them is certain to panic, and the compiler can see that it is.

### 2.9 Where a panic points

A panic prints one site, and for most of this inventory the site is a line of `std/`:

```console
$ ./program list
panic: Key does not exist
  at std/core/src/option.trb:172:15
```

The program has no way to tell which of its 1 664 index reads it was. The operators (`+`, `/`, the shifts) and a few
natives (`repeat`) name the program's line; `Indexed.at`, every slice, both `expect`s, `Array.set` and the list natives
reached through `List` name the standard library's. CONCEPT.md promises "in the debug profile, the frames of the task";
the `dev` profile, which is what `torb run` builds, printed the one site and nothing else. The runtime natives already
take the site as an argument (`torb_location at`), which is why `repeat` gets it right; a native reached through the
`List` trait is handed the site of its own declaration instead, and a function written in TorbScript has no such
argument at all. The machinery exists and is not used where most panics are.

### 2.10 Summary

| Category | Partial uses | Total uses | Twin exists |
|---|---:|---:|:---:|
| Index read (lists, arrays and maps) | 2 297 | 505 `get` | yes |
| Index write (lists and arrays) | part of 82 | - | no |
| Slice of a list | about 30 in the compiler | - | **no** |
| Slice of a text | 135 in the compiler | 20 `substringBefore`/`After` | **no** |
| Slices outside the compiler, lists and texts together | 80 | - | **no** |
| `expect` | 104 (2 in the compiler) | 487 `??`, 50 `?.` | yes |
| Division and remainder | 237 (111 by a literal) | - | no |
| Overflow | every `+`, `-`, `*` | 0 wrapping calls | only for `UInt64` |
| Conversion | 17 `wholeOf` | 137 `tryFrom` | yes |
| Input that is not UTF-8 | 3 `readLine()` | - | **no** |

Of the operations that panic on a value the program computed, three have a total twin: the index read (`get`), the
conversions (`tryFrom`) and `expect` (`??`, `?`). The slices, the index writes, `insert`, the arithmetic, `charAt`
and `repeat` have none, and `readLine()` has none and panics on input.

## 3. How other languages handle it

| Language | Index out of range | Map lookup | Text offsets | Integer overflow | Division by zero |
|---|---|---|---|---|---|
| **Rust** | panics; `get(i)` and `get(a..b)` answer `Option` | `map[&key]` panics, `get` answers `Option` | byte offsets; `&s[3..]` panics "byte index 3 is not a char boundary; it is inside 'ü'"; `s.get(3..)` answers `None` | panics with debug assertions, **wraps in release**; `checked_*`, `wrapping_*`, `saturating_*`, `overflowing_*`, `Wrapping<T>` | panics |
| **Swift** | traps; no `Optional` element accessor besides `first`/`last` | **`dict[key]` answers `Value?`**; `dict[key, default: 0] += 1` | opaque `String.Index`, obtained from the string; `index(_:offsetBy:)` is O(n); an index of another string is checked at run time | **traps in every build** except `-Ounchecked`; `&+`, `&-`, `&*` wrap; `addingReportingOverflow` | traps |
| **Kotlin, Java** | `IndexOutOfBoundsException`; `getOrNull`, `firstOrNull` | Kotlin: **`map[key]` answers `V?`**, `getValue` throws; Java: `get` answers `null` | UTF-16 code units; `substring` can cut a surrogate pair and **silently** produce a broken string | **wraps silently**; `Math.addExact` throws | `ArithmeticException` |
| **Go** | panics; the compiler's prove pass drops checks it can prove | **answers the zero value**; `v, ok := m[k]` | bytes; slicing never checks a boundary and **silently** produces invalid UTF-8 | **wraps silently** | panics; a constant zero is a compile error |
| **Zig** | checked in Debug and ReleaseSafe, **undefined behaviour** in ReleaseFast | - | bytes, no characters at all | checked in the safe modes, undefined otherwise; `+%` wraps, `+\|` saturates, `std.math.add` answers an error | the same |
| **Odin** | checked by default; `#no_bounds_check` per block, `-no-bounds-check` per build | - | UTF-8 bytes; `for r in s` decodes | wraps (defined) | - |
| **Elm** | no index operator; `Array.get` answers `Maybe` | `Dict.get` answers `Maybe` | `String.slice` clamps and never fails | - | `//` by zero answers **0** |
| **Haskell** | `!!`, `head` throw; GHC 9.8 warns on `head` and `tail` | `Map.!` throws, `lookup` answers `Maybe` | `Data.Text` counts characters; `take`, `drop`, `splitAt` clamp | wraps | `div` throws |
| **Gleam** | no index operator; `list.first` answers a `Result` | `dict.get` answers a `Result` | `string.slice` counts graphemes and clamps | none on the BEAM (arbitrary precision) | `/` answers **0**; `int.divide` answers a `Result` |
| **Pony** | `array(i)?` - a **partial function**, marked `?` in its signature and at every call | `map(key)?`, partial | - | wraps; `+?` is the partial form, `+~` the unchecked one | `/` answers **0**; `/?` is partial |
| **Scala** | throws; `lift(i)` answers `Option`, `headOption` | `map(key)` throws, `get` answers `Option` | UTF-16, as Java | wraps | throws |

What each choice costs and what it prevents:

- **Rust** is today's TorbScript: the short syntax asserts, the named method asks. It prevents nothing at compile time
  and costs nothing in ergonomics; what makes it work is that a panic reports the *caller's* line - `unwrap`, `expect`
  and indexing are `#[track_caller]` - and that the unsound corners are opt-in lints (Clippy's restriction group:
  `indexing_slicing`, `string_slice`, `arithmetic_side_effects`, `unwrap_used`). Its overflow choice - checked in debug,
  wrapped in release - is the one TorbScript deliberately rejected: the same program behaves differently per profile.
- **Swift** made byte-offset errors impossible to *write*: an `Int` is not a `String.Index`, and the only indices are
  the ones the string hands out. The price is the most complained-about API of the language, and most of that price is
  not the opaque index but what Swift put behind it: a `Character` is a grapheme cluster, so even `index(after:)` has to
  run the segmentation rules, and there is no cheap numeric escape at all. Swift also chose a total `Dictionary`
  subscript and a partial `Array` subscript - the same split Kotlin and Go arrived at.
- **Kotlin and Java** make every failure catchable, which TorbScript rejected with exceptions as a whole
  ([why no exceptions](../explanation/why-no-exceptions.md)); their `…OrNull` naming convention is what makes the total
  twin easy to find. Their strings show the cost of pretending offsets are simple: a broken surrogate pair, with no
  error at all.
- **Go** trades panics for silence: a map answers zero, a string slice produces invalid UTF-8, an overflow wraps. It
  prevents no bug; it hides them.
- **Zig and Odin** make safety a build setting. That is the right answer for a language without a promise of identical
  behaviour, and the wrong one for TorbScript, whose integers behave "the same everywhere" and whose checks are
  semantics, not diagnostics (PERFORMANCE 1.5).
- **Elm and Gleam** remove the partial operation, and where a partial one is unavoidable they invent an answer (`x // 0`
  is `0`). That prevents every panic and turns some of them into a wrong number, which is worse for a program that
  computes. What they prove is that a language without an index operator is pleasant when the vocabulary around it is
  rich: `head`, `get`, `slice` that clamps.
- **Haskell** is the counterexample that teaches: its partial `head` is thirty years old, generally regretted, and now
  warned about by the compiler. A partial function in the most convenient position gets used.
- **Pony** marks partiality in the type: `fun apply(i: USize): A ?`. That is checked exceptions in a small form - every
  caller must handle it or become partial itself - and for a language whose `+` can overflow it would mark nearly
  everything.
- **Scala** is Rust's split (`apply` throws, `lift`/`get` answer `Option`) on top of Java's exceptions.

The lesson that holds across all of them: **the safe languages differ less in whether things fail than in whether the
failure can be written by accident, and in what the error points at.** Every one of them that has an index operator
keeps it partial for arrays. Most of them make a map lookup total. Only Swift makes a text offset unforgeable, and only
Rust names the caller.

## 4. Text

### 4.1 Why text is different

`list[5]` on a list of three fails on a condition every reader of the line has in mind: the list is too short.
`text[3..]` fails on a condition that no reader has in mind and no ordinary test exercises: that the text holds a
letter outside ASCII before byte 3. The same line passes on `"Hello"` and panics on `"Grüße"`. A precondition that is
invisible in the program and in the test data is the definition of a panic nobody will find before a user does - and
section 1 shows it happening to the very sentence that explains the rule.

### 4.2 What the compiler does with offsets

The 135 text slices of the compiler, read one by one:

| Where the offsets come from | Sites | Example |
|---|---:|---|
| A prefix or suffix of a known length is cut off | 56 | `name[0..name.byteLength() - 4]` after `endsWith(".trb")`; `trimmed[2..]` after `startsWith("//")` |
| Between two offsets a scanner produced: spans, line starts | 42 | `source[span.start..span.end]` |
| Around the result of a search, plus the length of an ASCII separator | 32 | `line.slice((at + 1)..line.byteLength())` after `indexOf(":")` |
| Other: two clamps with `min`, a cut at a byte count of a name escaped to ASCII, a digit of an ASCII alphabet, an indentation measured with `byteLength()` | 5 | `closeText.slice(0..closeColumn.min(closeText.byteLength()))` |

Every one of the 135 is correct, and every one is correct because of something *outside* the slice: a `startsWith` two
lines up, a lexer that only ever stops at a character boundary, an escaping pass three hundred lines away in
`ir/mangle.trb`. The compiler cannot see any of it. Eighty-eight of the 135 - the affixes and the searches - are not
about positions at all; they are "without the `.trb`" and "after the first `:`", written in offsets because offsets are
the only vocabulary the text has.

### 4.3 The options

**(a) An opaque index that only the text makes (Swift).** `indexOf` answers a `TextIndex?`, `text[from..to]` takes a
`Bounds<TextIndex>`, and an `Int` is not a `TextIndex`. `text[3..]` becomes a compile error. What is left at run time is
an index of *another* text used on this one - checked by the same one-byte test as today, and a genuine broken
invariant. Cost: nothing at run time, because a one-field type vanishes (PERFORMANCE section 2: `benchmarks/wrapper` is
0.97x against a bare `double`); in ergonomics, whatever the vocabulary around it fails to provide.

**(b) Character offsets (Python, `Data.Text`).** `text[3..]` means three characters. Correct for the reader, and O(n):
a lexer that slices spans deep into a file becomes quadratic. It also has to answer which "character" - a scalar value
or a grapheme cluster - and TorbScript deliberately has no `length()` because that question has three answers.
Rejected as the meaning of `[]`; kept, under an honest name, for (e).

**(c) Byte offsets that are checked and answer an `Option` (Rust's `get`).** Total, and O(1). But every one of the 135
correct slices becomes `?? ""` or `.expect("...")`, which is noise where the offset is right and a panic again where
it is wrong. As the *only* form it is worse than today; as the checked conversion from an `Int` it is exactly right.

**(d) Slicing by `Range<TextIndex>`.** Not a separate option: it is how (a) is spelled, and `Bounds` is already
generic.

**(e) Keep `Int`, and make the common cases need no offset.** `withoutPrefix`, `withoutSuffix`, the first and the last
`n` characters, `splitOnce` around a separator - all total, all counted in characters where they count. This removes
the 88 sites of 4.2 that were never about positions, and leaves `text[3..]` as it is.

**(f) Clamp, or round to the nearest boundary.** Never fails and silently answers a different text than was asked
for. Rejected: a wrong answer is worse than a panic, which is the argument CONCEPT makes against a replacement
character.

### 4.4 Recommendation: (a) and (e) together

**Byte offsets stay the representation, and stop being `Int`.** The text hands out `TextIndex` values - from its
searches, from its start and end, from walking it - and slices at them in O(1), sharing the storage as today. A number
becomes an index only through a check that answers an `Option`. And the vocabulary of (e) makes most programs never see
an index at all.

```trb fragment
const text = "Grüße 👋"

match text.indexOf("ß") {
  Some(at) => print text[at..]
  None => print "no ß"
}
print text.dropping(characters: 3)
print text.withoutPrefix("Gr") ?? text
```

and the line of section 1:

```text
error: A text is sliced at a `TextIndex`, not at an `Int`: a number can point inside a character
 --> concept-strings.trb:4:19
  |
4 | const tail = text[3..]
  |                   ^
  = `text.dropping(characters: 3)` drops three characters; `text.index(byteOffset: 3)` checks an offset and answers
    a `TextIndex?`
```

The members, with the decisions this record makes about each:

| Member | Answers | Cost | Note |
|---|---|---|---|
| `indexOf(part)`, `lastIndexOf(part)` | `TextIndex?` | as today | the type changes, nothing else |
| `start()`, `end()` | `TextIndex` | O(1) | `text[..at]`, `text[at..]` need neither, as today |
| `index(after: at)`, `index(before: at)` | `TextIndex?` | O(1): one lead byte | `None` at the end and at the start |
| `indexedChars()` | `Iterate<(index: TextIndex, character: Char)>` | the decoding `chars()` already does | what a scanner walks |
| `index(byteOffset: Int)` | `TextIndex?` | O(1) | the checked conversion, option (c) in its right place: for offsets from outside, a file format, a language server |
| `byteOffset(of: at)` | `Int` | O(1) | for arithmetic on lines and columns; one way only |
| `text[from..to]` with `TextIndex` bounds | `String` | O(1) | panics only for an index of another text: past this one's end, or inside one of its characters |
| `dropping(characters: n)`, `droppingLast(characters: n)`, `prefix(characters: n)`, `suffix(characters: n)` | `String` | O(n) in the count, not in the text | total: fewer characters than asked is the whole or the empty text, as `take` and `skip` of `Iterate` already are |
| `withoutPrefix(part)`, `withoutSuffix(part)` | `String?` | O(part) | `None` where the text does not start or end with it |
| `splitOnce(separator)` | `(before: String, after: String)?` | one search | Go's `strings.Cut`; the 32 search sites of 4.2 |
| `charAt(at: TextIndex)` | `Char?` | O(1) | cannot be inside a character any more |

**`TextIndex` is `Equals`, `Compare`, `Hash` and `Show`** (it shows its byte offset), so spans can hold it, sort by it
and print it. It has no arithmetic: `at + 1` is the mistake the type exists to prevent, and `index(after:)` is what it
means. Characters are scalar values, the `Char` of `chars()`; grapheme clusters wait for the Unicode tables of
milestone 8, and whatever counts them will get its own name.

**It leaves the encoding to the back end.** A JavaScript back end (milestone 9) would hold its strings as UTF-16. With
an opaque index it can keep code-unit offsets inside `TextIndex` and no program notices; with `Int` byte offsets in the
language, every `indexOf` on that target is a conversion.

### 4.5 What it costs

- **Two commits** (CLAUDE.md, "Seed and breaking changes"). `Slice` takes `Bounds<Int>` today, and the checker and the
  lowering of `a[from..to]` find it by name (`checker/expression.trb`, `ir/lower/collection.trb`). Commit one teaches
  them a `Slice` over an index type (`Slice<Index = Int>`, `slice(range: Bounds<Index>)`) beside today's and refreshes
  the seed; commit two changes `String` and migrates every caller.
- **The migration**: the 135 text slices of the compiler, the texts among the 80 slices of `std/`, the tests and the
  examples, 44 `indexOf`/`lastIndexOf`, 7 `charAt`, and the compiler's `Span`, whose `start` and `end` become
  `TextIndex` or are converted where the source is sliced. Doing (e) first, as its own round, shrinks this: the 56
  affix sites and the 32 search sites of 4.2 move to `withoutSuffix` and `splitOnce` and stop being part of it.
- **Nothing at run time.** Section 4.3 (a).

## 5. A principle for the whole language

### 5.1 The wording

CONCEPT.md already says the first half: errors are values, and a panic is for bugs. What it does not say is **whose**
bug, and section 2 shows the gap - `readLine()` and `text[3..]` panic on data, not on a bug. The proposal:

1. **A panic is a broken promise of the program, never a property of the data it was given.** An operation may panic
   only where its precondition is something the program can state as an ordinary expression - `index < list.length()`,
   `divisor != 0`, `map.contains(key)` - and chose not to. Where the precondition is invisible - a byte inside a
   character, the encoding of bytes from outside - the operation must not exist in a form that can fail: a type makes
   it unwritable, or it answers a `Result`.
2. **Every partial operation has a total twin, in the same vocabulary**: `[i]` and `get(i)`, `[a..b]` and its `Option`
   form, `/` and a division that answers an `Option`, `wholeOf` and `tryFrom`. The twin is found next to the partial one
   and its name says it answers an `Option`.
3. **The compiler takes back what it can prove, and nothing it cannot.** A check it proves cannot fire is not emitted;
   a failure it proves certain is a compile error; neither changes what a program means (BACKEND 3.3 - a dropped check
   that could fire is a missing panic, "the one mistake" `ir/ranges.trb` may not make).
4. **A panic names the line of the program that broke the promise,** not the line of the standard library that noticed.

The short syntax keeps its meaning under this principle: `list[i]` still says "I know `i` is inside", as it does in
Rust and Swift, because that precondition is visible and, in a third of the compiler's reads, proven by the loop around
it. What changes is everything around it: the twin, the proof, the site, and the one place where the precondition was
never visible.

### 5.2 The variants

| Variant | What it prevents | Ergonomics | Performance | Against the goals | Verdict |
|---|---|---|---|---|---|
| **A. `[]` answers an `Option` everywhere** (Elm, Gleam) | every index panic | 2 297 reads grow a `?? x` or an `.expect(...)`, and a grid read becomes `grid[row]?.[column]`; the writes have no sensible `Option` form | neutral: `at` is `get` plus `expect` already (PERFORMANCE section 2), so the `Option` is there either way | moves the panic into `expect` rather than removing it; a counted loop that is provably safe pays the noise for nothing | **rejected** for lists and arrays; for maps, see 8.3 |
| **B. `[]` panics, the compiler proves what it can** | the checks it proves (409 of 1 318 compiler reads are loop-bounded) and the failures it sees (`10 / 0`, `"abc"[5..]`) | none | a gain: the bounds check is the open half of PERFORMANCE F8, and a sum over a list cannot vectorize while it stands | exactly clause 3 | **adopted** |
| **C. `torb check` notes every unproven partial operation** (Clippy's restriction group) | nothing on its own; it makes the partial sites visible | as a default: 688 + 221 notes in the compiler alone, which teaches everyone to ignore notes | none | a checker that cries wolf contradicts "a false positive of the checker is a checker bug" | **opt-in only**: an audit flag, not a note |
| **D. Partial functions marked in their signature** (Pony's `?`) | an unnoticed partial call | every caller handles it or becomes partial; with overflow, nearly every function is | none | this is checked exceptions, which TorbScript rejected | **rejected**; the `# Panics` section of a doc comment is the mark, and the documentation gate can check it |
| **E. Overflow: wrapping by default, or wrapping types** | overflow panics | none | the check goes away (2.55x on `call-depth`, PERFORMANCE F8) | a wrong number instead of a stop; "overflow panics" true everywhere a `+` is written is a stated decision | **rejected**: keep checked, add the `Option` twins, let the range analysis do the rest |
| **F. Checks only in a safe profile** (Zig, Odin) | nothing | none | the whole cost goes away in release | "same program, same behaviour everywhere"; the checks are semantics | **rejected** |

### 5.3 What it keeps

Stack overflow, out of memory, the size limits, a deadlock, a task that awaits itself, a panic in a task ending the
process, no `close()` on the way out, `expect`, `assert` and `panic` itself - all of them stay. They are either the
machine or an assertion the program wrote. A `Result` on every allocation is not possible in a language whose values
allocate when they are appended to and interpolated; the sandbox, where limits *are* values, is where that answer
already lives.

## 6. Recommendations

Ordered by value for the cost. "Two commits" is the procedure of CLAUDE.md for a change the seed has to learn first.

| # | Change | Cost | Migration | Two commits |
|---|---|---|---|:---:|
| 1 | **Correct CONCEPT.md's string example** (`Some(4)`, `12`), whatever becomes of the line in 8.2 | minutes | one paragraph | no |
| 2 | **`readLine()` answers a `Result`** (`Result<String?, IoError>`) instead of panicking on bytes that are not UTF-8, as CONCEPT already promises for files | a native's signature, both back ends | 3 calls | no |
| 3 | **Fix the four wrong messages**: `list[i]` and `array[i]` say `index 9 is out of bounds for a length of 3` as a slice and `set` already do, a map read says `the key "Alan" is not in the map`, without the key where its type is not `Show`; `charAt` inside a character says so and names its site; `repeat` says `a text cannot be repeated -1 times`; `absolute()` names `absolute` | small: the message is a static the lowering interns next to `Indexed.at`'s | 3 expected outputs name `Key does not exist` | no |
| 4 | **A panic raised in `std/` names the program's line.** A function of `std/` that can reach a `panic` gets the call site as a hidden argument, the way the natives already get `torb_location at` and Rust's `#[track_caller]` works; a call inside `std/` passes its own on. Plus the frames CONCEPT promises for the `dev` profile, as a later slice | one pass in the lowering and its twin in the VM; one integer argument on calls of such functions, which are the small ones gcc inlines | 9 expected outputs of `tests/` name a site in `std/` | no |
| 5 | **The certain failures are compile errors**: a literal zero divisor, an index or a slice of a literal past its end, constant arithmetic that overflows (`Int.maximum + 1`) - the rule `requireIndexInBounds` already applies to arrays | small, in the checker | none: nothing in the repository does it | no |
| 6 | **The range analysis drops a list's bounds check** where `for index in 0..list.length()` bounds it and the body does not change the list - the open half of PERFORMANCE F8 | moderate, `ir/ranges.trb` learns the length of an unchanged list | none; 409 compiler reads get faster | no |
| 7 | **The total text vocabulary** of 4.4 (e): `withoutPrefix`, `withoutSuffix`, `splitOnce`, the first and last `n` characters | small, TorbScript over the existing natives | optional; 88 compiler sites become clearer | no |
| 8 | **The missing total twins**: a slice that answers an `Option` for lists and texts, and `Option`-answering arithmetic on every integer type (`addedChecked`, `subtractedChecked`, `multipliedChecked`, `dividedChecked`, `remainderChecked`, beside `UInt64`'s `addedWrapping`) | small: TorbScript, over natives whose checks `torb_number.h` already has | none | no |
| 9 | **`TextIndex`** (section 4.4) | moderate: a trait parameter, a type, a migration | 135 compiler slices, the texts among 80 slices elsewhere, 44 searches, 7 `charAt`, the compiler's `Span`; less after 7 | **yes** |
| 10 | **The documentation gate checks `# Panics`**: `torb docs source` reports a public function whose body reaches `panic` or `expect`, directly or through a function that has the section, and whose doc comment does not have it | moderate: a call graph over the package | the sections `std/` has exist; the gate will find the ones that are missing | no |
| 11 | **`torb check --partial`** lists every partial operation the compiler did not prove, per file - an audit for code that must not stop, never a default | small once 6 exists | none | no |

What is **not** recommended: `[]` answering an `Option` on lists and arrays (variant A), a `?` in signatures (D),
wrapping or profile-dependent overflow (E, F), and any change to `expect`, `assert`, `panic` or the machine's limits.

## 7. What this is not

- **Not a change.** No file outside `docs/` was touched; every recommendation is a round of its own.
- **Not a recoverable panic.** Nothing here makes a panic catchable; the sandbox and `torb test` stay the only places
  one is contained.
- **Not a proof system.** Clause 3 is a local interval analysis and a handful of rules about literals, not refinement
  types. What it cannot prove keeps its check, as `ir/ranges.trb` already guarantees.
- **Not grapheme clusters.** `TextIndex` and the character-counted members work on `Char`, the scalar value. What a
  reader sees as one character is a question for the Unicode tables of milestone 8.

## 8. Open

Everything technical above is decided: the inventory, the order, the shape of `TextIndex` and of its members, the site
of a panic, what becomes a compile error. These are the owner's, as questions of direction or of taste.

1. **The principle of 5.1.** Adopt it into CONCEPT.md's error handling, next to "`panic` is for bugs"? It is the
   sentence every later API decision would be judged against. **Recommended: yes.**
2. **`TextIndex` (recommendation 9).** An `Int` byte offset is what Rust does and what the repository's 135 correct
   slices are written in; an opaque index is what Swift does and what makes section 1's line a compile error. It is a
   direction for the language, not a detail. **Recommended: yes, after recommendation 7**, which makes it smaller.
3. **`map[key]` reading a `Value?`** (Swift, Kotlin, Go) or panicking as today (Rust, Python, Scala). A key usually
   comes from data, which argues for the `Option`, and `ages[name] ?? 0` reads well. Against it: `[]` would mean two
   things across the collections, the `var` path `groups[key].append(value)` would need a form with a default
   (`groups[key, default: []]`, a syntax change), and the migration touches every map read. **Recommended: keep the
   panic, with the key in its message (recommendation 3), and revisit once recommendation 11 shows how many map reads
   in real programs are unproven.** This is the one question where both answers are defensible.
4. **The names.** `TextIndex` or `TextPosition`; `dropping(characters:)` and `prefix(characters:)` or one pair of verbs
   shared with `Iterate`'s `skip` and `take`; `withoutPrefix` or `removingPrefix`; `addedChecked` following
   `addedWrapping`, or `checkedAdd`; the total slice as `part(range)`, `sliceIfInside(range)` or another word. The
   language has no overloading, so `get(range)` beside `get(index)` is not available. **Recommended:** `TextIndex`,
   `dropping(characters:)` and `prefix(characters:)` (the label says what is counted, which is why `String` has no
   `length()`), `withoutPrefix`, `addedChecked`, and `part(range)`.

## Decided (2026-09-24)

- The principle goes into CONCEPT.md's error handling, as proposed.
- `TextIndex` is adopted, after the total text vocabulary (recommendation 7) has removed most of the compiler's text
  slices.
- `map[key]` keeps panicking, and its message names the key.
- The names are adopted as proposed: `TextIndex`, `dropping(characters:)`, `prefix(characters:)`, `withoutPrefix`,
  `withoutSuffix`, `addedChecked`, `part(range)`.

## Implemented

Built in the order the decisions above gave, each its own commit:

- **The principle** is in CONCEPT.md's error handling, and the example of section 1 is corrected (`Some(4)`, 12 bytes).
- **The total text vocabulary** (recommendation 7): `withoutPrefix`, `withoutSuffix`, `splitOnce`,
  `dropping(characters:)`, `droppingLast(characters:)`, `prefix(characters:)`, `suffix(characters:)`, and the affix and
  search sites of section 4.2 in the compiler and `std/` moved to them.
- **Messages** (recommendation 3): `list[i]` and `array[i]` say `index 9 is out of bounds for a length of 3` - the
  `at` of `ArrayList`, `TrieList` and `Array`, and the `Element` path step, which carries no message of its own any
  more - and `map[key]` says `the key "Alan" is not in the map` (the key through `showNested`, cut after 60 characters,
  `the key` alone where its type has no `Show`: `describedKey` of `std/collections`, which the lowering writes as
  `describedShownKey(key.showNested())` where the type has one).
- **The twins** (recommendation 8): `Integer` of `std/number` with `addedChecked`, `subtractedChecked`,
  `multipliedChecked`, `dividedChecked` and `remainderChecked`, and `part(range)` on lists and texts.
- **`readLine()`** (recommendation 2) answers `Result<String?, IoError>`, over the native `readLineOrEnd` (two commits).
- **The caller's line** (recommendation 4): `compiler/src/ir/sites.trb`. A function of a library module of `std/` that
  can reach a panic gets a copy with a hidden argument for its caller's site - a `const torb_location *` in C, a stack
  of sites in the kernel of the VM - and every direct call and every witness-table thunk calls the copy. A call through
  a closure still names the line of `std/`; the frames of the `dev` profile are not built.
- **Compile errors** (recommendation 5): a divisor known to be zero, arithmetic on known operands that leaves its type,
  a range outside a list literal, a key that none of a map literal's keys is.
- **`TextIndex`** (recommendation 9, two commits): `Slice<Index = Int>`, and `String` is a `Slice<TextIndex>`.
  `indexOf`, `lastIndexOf`, `charAt`, `start()`, `end()`, `indexAfter`, `indexBefore`, `indexAt(byteOffset:)`,
  `byteOffset(of:)` and `indexedChars()` are as section 4.4 names them (`index(after:)` became `indexAfter`, a label
  cannot tell two members of one name apart); `text[3..]` is a compile error with a message of its own. The byte-level
  forms keep their `Int`s under names that say so - `sliceBytes`, `byteOffsetOf`, `lastByteOffsetOf`, `charAtByte` -
  and that is what the compiler's spans and the formats of `std/` (URI, YAML, Markdown, HTTP) were moved to: their
  offsets come from a lexer, which is recommendation 9's "converted where the source is sliced". `Span` keeps `Int`.

Then the rest of the roadmap item, each its own commit again:

- **The last messages of recommendation 3**: `charAtByte` at an offset inside a character says
  `the offset 3 is inside of a character of a text of 7 bytes` at the caller's line, a negative `repeat` says
  `a text cannot be repeated -1 times`, and `Int8.minimum.absolute()` says
  ``arithmetic overflow in `absolute`: -128 has no positive counterpart`` in both back ends.
- **The bounds check a loop proves** (recommendation 6): `compiler/src/ir/bounds.trb`. A read `list[index]` of the `at`
  of a list of `std/collections`, whose index a guard `index < list.length()` of the same, unchanged list and a start at
  zero or above bound, becomes an `Element` step without a check - `torb_list_item` in C, `ListItemAddress` in the
  kernel of the VM. `for index in 0..values.length()` is that shape.
- **`torb check --partial`** (recommendation 11): `compiler/src/cli/partial.trb` lists every operation of the checked
  files that can panic and that the compiler did not prove - an index, a slice, a division, an overflow - with the line
  it is written on; the reads of recommendation 6 and the arithmetic the range analysis proves are not among them.
- **The documentation gate for `# Panics`** (recommendation 10): `compiler/src/documentation/panics.trb`. `torb docs
  source` reports a public function of `std/` whose body calls `panic`, calls `expect`, or calls a function of the
  same file that documents a panic, and whose comment has no `# Panics` section; a section that begins with `Never`
  says the `panic` guards a state the function rules out itself (`Task.await`). Tier A runs
  `torb docs source --panics std`. The call graph is one file's, read off the syntax tree: a member of another type,
  another file and an operator are not followed, which is what keeps the gate from crying wolf.
- **The frames of the `dev` profile** (the later slice of recommendation 4): a native binary of the `dev` profile
  prints the frames of the running task below the site, innermost first, each with the line it is in the middle of
  (`runtime/include/torb.h`, `torb_frame`). Every emitted function starts with `TORB_ENTER_FRAME("name")`, every call
  of program code is preceded by `TORB_FRAME_SITE(location)`, and both compile to nothing outside of `dev`, so the C
  is the same under every profile. A `CallClosure` carries the site it is written at for this.

Not built, and why:

- **A call through a closure still names the line of `std/`**, where the closure's body is a function of `std/`:
  `values.sum()` overflows inside the closure `{ a, b => a + b }` that `sum` hands to `fold`. Handing the site on
  would make the closure capture its creator's site: in C that site is the address of a compound literal of a caller's
  frame, which a closure that outlives the call - every lazy adapter of an iterator - cannot hold. It would take a
  location that is a value (a new kind of capture in the IR, a field of the environment in both back ends) or static
  locations for every call in the C, for the one case the frames of the `dev` profile already answer: their last line
  is the program's (`in the top level, at src/main.trb:2:7`).
- **The VM prints the site and no frames.** It is held to the release binary's standard error byte for byte by the
  conformance suite, and a frame's line needs a location on every call word of the bytecode. `torb run --native` and
  `torb test --native` are where the frames are.
