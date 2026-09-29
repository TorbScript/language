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

1. **A doc comment belongs to the declaration written directly after it.** Only code between them breaks that, not a
   blank line: the comment attaches to the next declaration even when an empty line stands in between.

   A file therefore starts with a **module comment** - the doc comment at the top, in front of the first `use`, which
   says what the module is for. The parser attaches it to that import, which is what makes it belong to the file and to
   no declaration of it.

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

4. **Six headings carry what a tag language would carry elsewhere, and there are no others.** `# Examples` holds code
   that is meant to run, indented by four spaces; `# Errors` describes the `Fail` cases a `Result`-returning function
   can produce; `# Panics` describes when it panics instead of failing; `# Pitfalls` names the mistake the construct
   invites; `# Open` names a problem that is open, in the present tense; `# Related` links the constructs that belong
   next to it. The return value itself is described in the running text, not under a heading of its own.

5. **`/**/`, the four-character empty comment, is not a doc comment.** It starts like one, but a doc comment needs at
   least one more character after the opening `/**`; `/**/` is an ordinary block comment that attaches to nothing.

## What this is not

**A doc comment is not read by the type checker.** The compiler lexes it, attaches it to its declaration and carries it
in the syntax tree; what the text says is nothing `torb check` decides. Both blocks below compile identically, although
the second one spells its heading wrong:

```trb check
/** Doubles a number. */
fn double(value: Int): Int {
  value * 2
}

print double(21)
```

```trb check
/**
 * Doubles a number.
 *
 * # Panic
 * Never.
 */
fn double(value: Int): Int {
  value * 2
}

print double(4)
```

Two commands read the text. `torb docs source` decides the headings, the links,
the examples and the module comment over a whole tree of sources, and `torb doc` (skill `torbscript-projects`: `references/tooling/torb-doc.md`) renders
the comments of a package's public API into a reference and runs the code under `# Examples` as its doc tests: an
example whose first line is `// check`, or `// check` and the reason, is type checked and not run.

## Related

- [Lexical structure](lexical-structure.md) - the other two comment forms, and where a statement ends.
- [Declaring a type](../types/declaring-a-type.md) - fields and cases, which a doc comment can attach to.
- torb docs source - the gate that reads what a doc comment says.

