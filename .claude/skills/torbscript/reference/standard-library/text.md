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

<!-- torb:declarations:begin -->

### Char

```trb fragment
public native type Char with Equals, Compare, Hash, Show {
  fn isDigit(self): Bool
  fn isLetter(self): Bool
  fn isWhitespace(self): Bool
  fn byteLength(self): Int
  fn toUpperCase(self): Char
  fn toLowerCase(self): Char
}
```

A Unicode scalar value. `byteLength()` is the number of bytes the character takes up in UTF-8 (1 to 4), which is what it
adds to an offset into a `String`. Inside another value it shows in single quotes with escapes (`'A'`); `Int.from('A')`
is the code point and belongs to `std/number`, which owns `Int64`, and `Char.tryFrom(value)` is the other direction and
belongs here, because not every `Int64` is a Unicode scalar value (surrogates, and everything above `0x10FFFF`).

### String

```trb fragment
public native type String
  with Equals, Compare, Hash, Show, Add, Slice
{
  fn chars(self): Iterable<Char>
  fn bytes(self): Iterable<UInt8>
  fn byteLength(self): Int
  fn isEmpty(self): Bool
  fn slice(self, range: Bounds<Int>): String
  fn contains(self, part: String): Bool
  fn startsWith(self, prefix: String): Bool
  fn endsWith(self, suffix: String): Bool
  fn indexOf(self, part: String): Int?
  fn lastIndexOf(self, part: String): Int?
  fn substringBefore(self, part: String): String?
  fn substringAfter(self, part: String): String?
  fn trim(self): String
  fn toUpperCase(self): String
  fn toLowerCase(self): String
  fn replace(self, part: String, replacement: String): String
  fn split(self, separator: String): List<String>
  fn repeat(self, times: Int): String
  fn isBlank(self): Bool
  fn lines(self): List<String>
}
```

UTF-8 text. There is no `length()` and no `text[i]` on purpose: say what is being counted (`chars()`, `bytes()`). A
position comes from searching and is a byte offset, and slicing with one is O(1) and shares the storage with the
original. **A `String` is always valid UTF-8** - the only ways to build one are a literal, a slice at a character
boundary, `String.from(Iterable<Char>)` and a runtime function that validates, so reading bytes that are not UTF-8 is an
`IoError` and there is no replacement character anywhere in the language. `showNested()` is the text itself in double
quotes with escapes; `show()` (from `Show`) is the text unquoted. `String.from(characters)` and
`characters.to<String>()` come from `extend String with From<Iterable<Char>>`.

<!-- torb:declarations:end -->

## Related

- [Strings](../language/values-and-types/strings.md) - why a `String` has no `length()` and no `text[i]`.
- [std/core](core.md) - `Equals`, `Compare`, `Hash` and `Show`, which every type here carries.
- [The standard library](index.md) - the other packages.
