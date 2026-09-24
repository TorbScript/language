---
title: String interpolation
summary: "`{expression}` inside a string runs the expression and shows it, a literal brace is written `\\{` or `\\}`, and the expression inside the braces has to fit on one line."
kind: reference
status: stable
order: 3
keywords:
  - interpolation
  - Show
  - literal brace
source:
  - CONCEPT.md#strings
---

A `{...}` inside a string literal is not text: it is an expression, run when the string is built and shown in the
result through the same `Show.show` every other value uses. This page is about what may stand inside the braces and
how to write a literal one.

## Example

```trb check
const name = "Ada"
const remaining = 3

print "Hello, {name}. {remaining} tasks left."
print "escaped: \{not interpolated\}"
```

## Syntax

```text
"text {expression} text"                 an interpolated string
"text \{ text"                           a literal `{`
"text \} text"                           a literal `}`
```

## Rules

1. **`{expression}` is replaced by `Show.show` of that expression's value.** Any expression that produces a value with
   a `Show` implementation may stand inside the braces, from a name to a full `if` or `match`.

   ```trb check
   const name = "Ada"
   print "verdict: {if name == "Ada" { "yes" } else { "no" }}"
   ```

2. **A literal brace is written `\{` or `\}`.** There is no other way to place a `{` or a `}` into an interpolated
   string; a bare `{` always opens an interpolation.

3. **The expression inside the braces has to fit on one line.** A raw line break before the closing `}` is a lexer
   error, because the lexer only balances braces and quotes while looking for the close, and a line break ends that
   search.

   ```trb error
   const value = 1
   print "total: {value
   }"
   // error: This `{` inside of a string is never closed. A literal brace is written `\{`
   // error: Expected an expression, found `}`
   // error: This string is never closed
   ```

4. **A quote or a brace inside the expression can nest, and is not mistaken for the string's own end.** The lexer
   tracks brace depth and skips over a nested string literal as one unit while it looks for the interpolation's
   closing `}`, which is what lets a `match` or another string literal stand inside the braces.

5. **A raw string (`raw"..."`) never interpolates.** `{` is an ordinary character inside one; see
   [Literals](literals.md) for the two string forms.

## What this is not

**A literal `{` is not written by doubling it.** Some template languages use `{{` for a literal brace; here that
would open an interpolation twice, which errors as soon as the first one is never closed by a `}`.

```trb check
print "price: \{19.99\}"
```

```trb error
print "price: {{19.99}"
// error: This `{` inside of a string is never closed. A literal brace is written `\{`
```

**Interpolation is not string concatenation with a different spelling.** `"{a}{b}"` calls `Show.show` on `a` and on
`b` and places the results next to each other; it does not require either one to already be a `String`.

## Related

- [Literals](literals.md) - the two string literal forms, and the escapes a non-raw one has.
- [Multi-line strings](multi-line-strings.md) - the `"""` form, which interpolates the same way.
- [Strings](../values-and-types/strings.md) - the `String` type itself, once interpolation has produced one.
