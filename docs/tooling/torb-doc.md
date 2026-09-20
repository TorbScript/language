---
title: torb doc
summary: torb doc will render every doc comment of a package into documentation, the same way the standard-library reference's Declarations sections are filled in by hand today.
kind: tooling
status: planned
order: 110
keywords:
  - torb doc
  - doc comment
  - documentation generator
source:
  - CONCEPT.md#toolchain
  - CONCEPT.md#doc-comments
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

A [doc comment](../glossary.md#doc-comment) already exists for almost every public declaration of `std/` - it is
read by nothing but a person reading the source, because no command renders it yet.

## Synopsis

```text
torb doc   Documentation from the doc comments (planned; no command line decided yet)
```

## What it does

### What is specified

Everything that is declared can carry a `/** ... */` comment - a parameter, a field and a case included - so there
is no separate tag language to keep in sync with the signature. The text is Markdown, with the conventional headings
`# Errors`, `# Panics` and `# Examples`; `[List.add]` and `[Option]` are links, resolved like a name in the code next
to them. The doc comment is part of the syntax tree, so `torb doc`, the language server and the test runner are
specified to read the same data - one comment, three consumers.

`CONCEPT.md` also specifies that the code under a doc comment's `# Examples` heading is compiled and run by
[`torb test`](torb-test.md), so a documented example cannot silently stop working. Neither half exists today: `torb
test` runs `*.test.trb` files only, and reads nothing out of a doc comment.

### What exists today

The comment itself parses and belongs to its declaration - that part of the front end is real, and is what
[doc comments](../language/syntax/doc-comments.md) describes. What is missing is anything that turns it into a
rendered page. Every [standard-library](../standard-library/index.md) page already has the place `torb doc` will
fill: a `## Declarations` section between two markers, written by hand for now - [std/test](../standard-library/test.md)'s
in full:

````md
## Declarations

<!-- torb:declarations:begin -->

### `test`, `group`

```trb fragment
public native fn test(name: String, body: () => Void)
public native fn group(name: String, body: () => Void)
```

`group` names a closure of `test` calls; `test` names a closure whose body is the check. Both are ordinary calls in the
`.test.trb` files of `tests`, and nest freely - a `group` inside a `group` is how a suite is organized.

<!-- torb:declarations:end -->
````

## Examples

None: there is no command line to run yet. A real doc comment already in `std/`, waiting to be read by something
other than a person:

```trb fragment
/** Writes the values to standard output, separated by spaces, followed by a line break. */
public native fn print(...values: Show)
```

## Related

- [Doc comments](../language/syntax/doc-comments.md) - what `/** */` attaches to and the headings it uses.
- [torb test](torb-test.md) - the command `CONCEPT.md` specifies to run a doc comment's `# Examples`.
- [The standard library](../standard-library/index.md) - the `Declarations` sections `torb doc` will fill.
- [The torb command](the-torb-command.md) - every subcommand, and which are still planned.
