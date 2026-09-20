---
title: Doc comments
summary: A `/** */` comment attaches to the declaration written directly after it, and everything that can be declared - including a parameter, a field or a case - can have one.
kind: reference
status: stable
order: 8
keywords:
  - documentation comment
  - Markdown
  - Errors heading
  - Panics heading
  - Examples heading
source:
  - CONCEPT.md#doc-comments
---

A doc comment is a `/** ... */` block comment that belongs to whatever is declared directly after it. Unlike a tag
language that repeats a name (`@param host`), a doc comment is written where the thing it describes already is, so
there is nothing to keep in sync by hand.

## Example

```trb check
/**
 * Connects to a database.
 *
 * # Panics
 * If `timeout` is negative.
 */
fn connect(
  /** Host name or address */
  host: String,
  /** Seconds to wait before giving up */
  timeout: Int = 30,
): String {
  host
}

print connect("localhost")
```

## Syntax

```text
/** text */                              a doc comment, attached to the declaration right after it
/**
 * text
 */                                       the same comment over several lines; the leading `*` is stripped
```

## Rules

1. **A doc comment belongs to the declaration written directly after it.** A blank line, another statement or the end
   of the file between them means there is nothing to attach to, and the comment is an ordinary block comment instead.

2. **Everything that can be declared can have one - a parameter and a field included.** `host` and `timeout` above
   each carry their own doc comment; so does a `case`.

   ```trb check
   type Shape {
     /** A circle around the origin */
     case Circle(/** Always positive */ radius: Float)
   }

   print Shape.Circle(1.0)
   ```

3. **The text is Markdown, and nothing else.** There is no separate tag syntax (no `@param`, no `@returns`); a
   parameter is documented by attaching a comment to the parameter itself, as rule 2 shows.

4. **Three headings carry what a tag language would carry elsewhere: `# Errors`, `# Panics`, `# Examples`.** `# Errors`
   describes the `Fail` cases a `Result`-returning function can produce, `# Panics` describes when it panics instead
   of failing, and `# Examples` holds a code block meant to run. The return value itself is described in the running
   text, not under a heading of its own.

5. **`/**/`, the four-character empty comment, is not a doc comment.** It starts like one, but a doc comment needs at
   least one more character after the opening `/**`; `/**/` is an ordinary block comment that attaches to nothing.

## What this is not

**A doc comment is not verified by this documentation's toolchain today.** CONCEPT.md describes two things this page
does not show working: that `torb test` compiles and runs the code under `# Examples`, and that `[List.add]` and
`[Option]` are links resolved like names in the code. Neither is built yet - there is no command that reads a doc
comment's text at all - so a doc comment is prose the reader has to trust, the same as a line comment.

```trb check
/** Doubles a number. */
fn double(value: Int): Int {
  value * 2
}

print double(21)
```

```trb skip a heading spelled wrong is not rejected by anything today, so there is no diagnostic this block could show
/**
 * Doubles a number.
 *
 * # Panic
 * Never.
 */
fn double(value: Int): Int {
  value * 2
}
```

The second block spells the heading `# Panic` instead of `# Panics`; nothing catches that today; both blocks compile
identically because a doc comment's text is not read by the checker at all.

## Related

- [Lexical structure](lexical-structure.md) - the other two comment forms, and where a statement ends.
- [Declaring a type](../types/declaring-a-type.md) - fields and cases, which a doc comment can attach to.
