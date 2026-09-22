---
title: Strings
summary: A String has no length() and no text[i], because "length" and "the i-th character" each have three different answers and two of them are slow.
kind: reference
status: stable
order: 14
keywords:
  - UTF-8
  - byte offset
  - Char
  - slicing
source:
  - CONCEPT.md#strings
  - std/text/src/lib.trb
---

A `String` is UTF-8 text and a value, like everything else. It deliberately has neither `length()` nor `text[i]`:
"length" has three different answers - bytes, Unicode scalar values, what a reader sees - and "the i-th character"
needs one of those answers to even mean anything, so the type asks the caller to say which one instead of guessing.

## Example

```trb run
const text = "Grüße 👋"

print text.chars().count()      // prints 7
print text.byteLength()         // prints 12
print text.isEmpty()            // prints false
print text.indexOf("ß")         // prints Some(4)
print text[4..]                 // prints ße 👋
print text.substringAfter("ü")  // prints Some("ße 👋")
```

`ü` takes two bytes, so `ß` starts at byte 4, and `text[3..]` would cut `ü` in half - which is a panic, not a
shorter string.

## Syntax

```text
text.chars()                             an Iterate<Char>: Unicode scalar values
text.bytes()                             an Iterate<UInt8>: the raw UTF-8 bytes
text.byteLength()                        the byte count, O(1)
text.indexOf(part)                       Some(byteOffset) or None; a byte offset, from searching
text.lastIndexOf(part)                   the same for the last occurrence
text[from..to]                           a slice by byte offset, O(1), shares storage
```

## Rules

1. **Every position that comes out of `String` is a byte offset, produced by searching.** `indexOf`, `startsWith`,
   `substringBefore` and `substringAfter` all work this way; nothing counts characters to find a position.

2. **`chars()` is an `Iterate<Char>` of Unicode scalar values, and counting it is O(n).** `text.chars().count()` is
   how a caller asks for "how many characters", explicitly paying for the answer it wants.

3. **`byteLength()` is O(1) and `isEmpty()` follows from it.** These are the only two size questions a `String`
   answers without a caller choosing what to count.

4. **`text[from..to]` slices by byte offset in O(1) and shares the string's storage.** An offset greater than
   `byteLength()`, a start greater than the end, or an offset that lands on a UTF-8 continuation byte each panic,
   naming the offset and the length. See [Ranges](ranges.md) for the four range forms a slice can take.

5. **A `String` is always valid UTF-8.** The only ways to produce one are a literal, a slice at a character
   boundary, `String.from(Iterate<Char>)`, and a runtime function that validates as it reads - so a file whose bytes
   are not UTF-8 is an `IoError` when it is read, never a replacement character.

6. **`substringBefore` and `substringAfter` return `Option<String>`, `None` when the part is not found.** Most code
   that wants "everything after this marker" never sees a byte offset at all.

7. **Case mapping is one code point at a time, over ASCII and the letters of Latin-1.** `Char.toUpperCase` answers
   one code point, so a character whose upper case is *two* of them is answered unchanged - `'ß'.toUpperCase()` is `'ß'`.
   `String.toUpperCase` is that mapping per character, which is why a mapped text has exactly as many bytes as the text
   it came from. Every code point the mapping does not cover is left alone; the full Unicode tables, and with them a
   `String.toUpperCase` that may make a text longer, are still to come.

   ```trb check
   print "Grüße".toUpperCase()
   print 'ä'.toUpperCase(), 'ß'.toUpperCase()
   ```

## What this is not

**A `String` does not have `length()`.** The method a reader reaches for from another language is not here on
purpose, so that a caller has to say which length it means.

```trb check
const text = "hello"
print text.chars().count()
```

```trb error
const text = "hello"
print text.length()
// error: `String` has no member `length`
```

**A `String` is not `Indexed`.** `text[0]` is not "the first character"; it is not legal at all, because indexing by
a single position would face the same ambiguity `length()` does.

```trb error
const text = "hello"
print text[0]
// error: `String` does not implement `Indexed`, so `a[key]` has no meaning for it
```

## Related

- [Ranges](ranges.md) - the range forms `text[from..to]` accepts.
- [String interpolation](../syntax/string-interpolation.md) - `{expression}` inside a string literal.
- [Multi-line strings](../syntax/multi-line-strings.md) - the `"""` form.

