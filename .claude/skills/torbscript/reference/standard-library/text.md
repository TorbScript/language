---
title: std/text
summary: Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
kind: package
status: stable
order: 20
keywords:
  - std/text
  - Char
  - String
  - UTF-8
  - Unicode
source:
  - std/text/src/lib.trb
---

`std/text` is UTF-8 text and the Unicode scalar value it is made of. `Bool` used to live here and is `std/core`'s: it is
a primitive of the language, not a thing about text. Every name below is already in scope through the prelude.

## Import

```trb fragment
use Char, String from "std/text"
```

```trb check
const shout = "hello".toUpperCase()
print shout
```

## Declarations

### Char

```trb fragment
public native type Char with Equals, Compare, Hash, Show {
  fn isDigit(): Bool
  fn isLetter(): Bool
  fn isWhitespace(): Bool
  fn byteLength(): Int
  fn toUpperCase(): Char
  fn toLowerCase(): Char
}
```

A Unicode scalar value. `byteLength()` is the number of bytes the character takes up in UTF-8 (1 to 4), which is what it
adds to an offset into a `String`. Inside another value it shows in single quotes with escapes (`'A'`); `Int.from('A')`
is the code point and belongs to `std/number`, which owns `Int64`, and `Char.tryFrom(value)` is the other direction and
belongs here, because not every `Int64` is a Unicode scalar value (surrogates, and everything above `0x10FFFF`).

### TextIndex

```trb fragment
public type TextIndex with Equals, Compare, Hash, Show {
  private offset: Int
}
```

A position in one text: before one of its characters, or at its end. Only a text hands one out - `indexOf`,
`lastIndexOf`, `start()`, `end()`, `indexAfter`, `indexBefore`, `indexedChars()` - so a position inside of a
character cannot be written, and `text[3..]` is a compile error (docs/design/PANICS.md section 4). It holds a byte
offset and shows as one; it has no arithmetic, because `at + 1` is exactly the mistake it exists to prevent. A number
from outside becomes one through `indexAt(byteOffset:)`, which answers `None` inside of a character, and one becomes a
number again through `byteOffset(of:)`.

### String

```trb fragment
public native type String
  with Equals, Compare, Hash, Show, Add, Slice<TextIndex>
{
  fn chars(): Iterate<Char>
  fn bytes(): Bytes
  fn charAt(at: TextIndex): Char?
  fn charAtByte(offset: Int): Char?
  fn start(): TextIndex
  fn end(): TextIndex
  fn indexAfter(at: TextIndex): TextIndex?
  fn indexBefore(at: TextIndex): TextIndex?
  fn indexAt(byteOffset: Int): TextIndex?
  fn byteOffset(of: TextIndex): Int
  fn indexedChars(): Iterate<(index: TextIndex, character: Char)>
  fn byteAt(offset: Int): UInt8?
  fn byteLength(): Int
  fn isEmpty(): Bool
  fn slice(range: Bounds<TextIndex>): String
  fn part(range: Bounds<TextIndex>): String?
  fn sliceBytes(from: Int, to: Int): String
  fn contains(part: String): Bool
  fn startsWith(prefix: String): Bool
  fn endsWith(suffix: String): Bool
  fn indexOf(part: String): TextIndex?
  fn lastIndexOf(part: String): TextIndex?
  fn byteOffsetOf(part: String): Int?
  fn lastByteOffsetOf(part: String): Int?
  fn substringBefore(part: String): String?
  fn substringAfter(part: String): String?
  fn withoutPrefix(part: String): String?
  fn withoutSuffix(part: String): String?
  fn splitOnce(separator: String): (before: String, after: String)?
  fn dropping(characters: Int): String
  fn droppingLast(characters: Int): String
  fn prefix(characters: Int): String
  fn suffix(characters: Int): String
  fn trim(): String
  fn toUpperCase(): String
  fn toLowerCase(): String
  fn replace(part: String, replacement: String): String
  fn split(separator: String): List<String>
  fn repeat(times: Int): String
  fn isBlank(): Bool
  fn lines(): List<String>
}
```

UTF-8 text. There is no `length()` and no `text[i]` on purpose: say what is being counted (`chars()`, `bytes()`). A
position comes from the text and is a `TextIndex`, and slicing at one is O(1) and shares the storage with the original. **A `String` is always valid UTF-8** - the only ways to build one are a literal, a slice at a character
boundary, `String.from(Iterate<Char>)` and a runtime function that validates, so reading bytes that are not UTF-8 is an
`IoError` and there is no replacement character anywhere in the language. `showNested()` is the text itself in double
quotes with escapes; `show()` (from `Show`) is the text unquoted. `String.from(characters)` and
`characters.to<String>()` come from `extend String with From<Iterate<Char>>`.

`charAt(at)` decodes the character that begins at a position, or answers `None` at the end. `slice(range)` is
`text[from..to]` between two positions and is ordinary TorbScript over `sliceBytes(from, to)`, the one native the runtime
has for it; only a position of another text - past the end of this one or inside one of its characters - and a start
after the end panic, with the offset and the length named. `part(range)` is its total twin: the same text where `slice`
would answer one, and `None` where it would panic.

**The byte-level members** are for a format that counts bytes - a lexer's spans, a protocol's offsets: `byteLength()`,
`byteAt(offset)` (a byte is never inside anything, so it never panics), `sliceBytes(from, to)`, `byteOffsetOf(part)`,
`lastByteOffsetOf(part)` and `charAtByte(offset)`. Their offsets are `Int`s the program computed, so an offset inside
of a character panics there, with the offset and the length in the message: the name says what is counted.

**`bytes()` answers `Bytes`**, the prelude's `List<UInt8>`, like every other `bytes()` of the standard library: a
copy of the UTF-8 in one allocation, which is what a sink, a socket, a file and a digest take. It is not a view like
`chars()`, because nearly every caller wants the list, and the storage itself cannot be shared, because a text's and a
list's are laid out differently. A loop that only reads and must not allocate walks `byteAt(offset)` up to
`byteLength()` instead.

```trb run
const bytes = "hé".bytes()
print bytes    // prints [104, 195, 169]
```

**The total vocabulary** cuts a text without an offset, and none of it can panic. `withoutPrefix(part)` and
`withoutSuffix(part)` answer the rest, or `None` where the text does not start or end with `part`. `splitOnce(separator)`
answers `(before:, after:)` around the first occurrence, or `None` - one search, Go's `strings.Cut`.
`dropping(characters: n)`, `droppingLast(characters: n)`, `prefix(characters: n)` and `suffix(characters: n)` count
Unicode scalar values and are total like `skip` and `take` of an `Iterate`: fewer characters than asked answers the
empty or the whole text, and a count of zero or less drops or takes nothing. They walk the count, not the text.

```trb run
const file = "main.trb"
print(file.withoutSuffix(".trb") ?? file)         // prints main
print "Grüße".dropping(characters: 2)             // prints üße
print("key=value".splitOnce("=")?.after ?? "")    // prints value
```

## Related

- [Strings](../language/values-and-types/strings.md) - why a `String` has no `length()` and no `text[i]`.
- [std/core](core.md) - `Equals`, `Compare`, `Hash` and `Show`, which every type here carries.
- [The standard library](index.md) - the other packages.

