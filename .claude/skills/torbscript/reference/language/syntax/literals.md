---
title: Literals
summary: An integer, a decimal, a character and a string each have exactly one literal form, and a literal adapts to the type it is expected to have.
kind: reference
status: stable
order: 2
keywords:
  - number literal
  - character literal
  - string literal
  - raw string
  - underscore
source:
  - CONCEPT.md#lexical-structure
---

A literal is the text of a value written directly into the source. There is one literal form per kind of built-in
value: a number, a character in single quotes, and text in double quotes. A literal has its own type until the
context expects a different one, and then it adapts to that type instead.

## Example

```trb check
const decimal = 10
const million = 1_000_000
const byte: UInt8 = 0xFF
const flags = 0b1010
const ratio = 3.14
const large = 1e9
const letter = 'A'
const wave = '\u{1F44B}'
const greeting = "Grüße"

print "{decimal} {million} {byte} {flags} {ratio} {large} {letter} {wave} {greeting}"
```

## Syntax

```text
10  1_000_000  0xFF  0b1010            an integer literal; `_` groups digits anywhere between them
3.14  1e9  1.5e-3                      a float literal: a fraction, an exponent, or both
'A'  '\n'  '\u{1F44B}'                 a character literal: exactly one character
"text"  "line {expression}"            a string literal, with interpolation
r"text"                                a raw string literal: no escapes, no interpolation
```

## Rules

1. **An integer literal without an expected type is `Int64`, a literal with a fraction or an exponent is `Float64`.**
   This does not follow the `Int`/`Float` aliases; it is fixed.

   ```trb check
   const wholeNumber = 7
   const withFraction = 7.0
   print "{wholeNumber} {withFraction}"
   ```

2. **A literal adapts to the type it is expected to have, and one that does not fit is a compile error at the
   literal.** The error names the literal and the type, not a range.

   ```trb check
   const byte: UInt8 = 200
   print byte
   ```

   ```trb error
   const small: Int8 = 300
   // error: `300` does not fit into `Int8`
   ```

3. **`0x` starts a hexadecimal literal, `0b` a binary one, and both take only integer types.** There is no octal
   literal form.

   ```trb check
   const byte: UInt8 = 0xFF
   const flags: UInt8 = 0b1010
   print "{byte} {flags}"
   ```

   ```trb error
   const mask: Float = 0xFF
   // error: `0xFF` is written in another base, and only an integer type takes it: `Float64` is not one
   ```

4. **`_` groups the digits of a number literal and carries no value of its own.** It may stand between digits of an
   integer or a float literal in any position; the formatter canon does not require a particular grouping.

5. **A float literal has a fraction, an exponent, or both.** `1e9` needs no fraction, and an exponent takes an
   optional `+` or `-` sign: `1.5e-3`.

6. **A character literal is exactly one character, in single quotes.** An escape sequence counts as the one
   character; anything else between the quotes is an error.

   ```trb error
   const both = 'ab'
   // error: A character literal contains exactly one character and ends with `'`
   ```

7. **A character or string literal escapes six named characters plus one general escape.** `\n`, `\r`, `\t`, `\0`,
   `\\`, `\"` and `\'` name themselves; `\u{1F44B}` names a Unicode scalar value by its hexadecimal code point. An
   escape that is none of these is an error at the backslash.

   ```trb error
   const wrong = '\q'
   // error: Unknown escape sequence `\q`
   ```

8. **A string literal interpolates `{expression}` and escapes a literal brace as `\{` or `\}`.** See
   [String interpolation](string-interpolation.md) for what may stand inside the braces.

9. **A raw string (`r"text"`) has no escapes and no interpolation.** `\` and `{` are ordinary characters inside one,
   which is what a path, a regular expression or a snippet of JSON needs.

   ```trb check
   const pattern = r"C:\Users\{name}"
   print pattern
   ```

## What this is not

**A number literal is not typed by its spelling alone.** `0xFF` is not "a hex number type"; it is an `Int64` like
`255` unless the context says otherwise, and the same literal fits any integer type it is small enough for.

```trb check
const asByte: UInt8 = 0xFF
const asWord: UInt16 = 0xFF
print "{asByte} {asWord}"
```

```trb error
const asByte: UInt8 = 0x1FF
// error: `0x1FF` does not fit into `UInt8`
```

**A character literal is not a one-character string.** `'A'` is a `Char`, `"A"` is a `String` of one character, and
neither converts to the other implicitly. See [Strings](../values-and-types/strings.md) for the boundary between the
two.

## Related

- [String interpolation](string-interpolation.md) - what `{expression}` inside a string does.
- [Multi-line strings](multi-line-strings.md) - the `"""` form and how it is dedented.
- [Integers](../values-and-types/integers.md) - the eight sized types a literal can adapt to.
- [Bindings](../values-and-types/bindings.md) - how a type annotation changes what a literal is.

