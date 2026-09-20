---
title: torb docs source
summary: torb docs source checks the doc comments of the code itself - a module comment on every file, a comment on every construct that needs one, six headings, links that resolve, and examples that compile.
kind: tooling
status: stable
order: 75
keywords:
  - torb docs source
  - doc comment
  - module comment
  - documentation gate
source:
  - compiler/src/documentation/source.trb
  - compiler/CONTRIBUTING.md
---

`docs check` decides the pages of this documentation. `docs source` decides the other half: the documentation that lives
in the `.trb` files, where a reader of the code and an agent editing it both find it.

## Synopsis

```text
torb docs source <path>...                 The doc comments of every .trb file below the paths
    --statistics                           How much of every tree is documented
    --no-examples                          Do not type check the examples, which is the slow part
    --tree <std|compiler|examples|tests>    Judge every file by these rules instead of deriving them from the path
```

It exits with 1 when it reports a finding and with 0 when it does not. From `bootstrap/`, where stage 0 lives:

```console
cargo run --release -q -- run ../compiler docs source ../std ../compiler ../examples
```

Hidden directories, `target`, `node_modules` and `build` are skipped, and a `project.trb` is a manifest and not a
module, so it is skipped too.

## What it does

### What it decides

- **A module comment on every file**: the doc comment the file begins with, in front of the first `use`.
- **A doc comment on every construct the tree asks for.** `std/` asks for every `public` declaration, every `extend`, and
  every member, field and case of a public type that is not `private`; `compiler/src` asks for every `public`
  declaration; `examples/` asks for every top-level declaration; a test file asks for its file comment only.
- **Six headings and no others**: `# Examples`, `# Errors`, `# Panics`, `# Pitfalls`, `# Open`, `# Related`. A heading
  outside the vocabulary and a heading with nothing under it are both findings.
- **Every link resolves.** `[Iterator]`, `[List.add]` and `[Option.Some]` are resolved like a name at that place in the
  code: what the file declares, what it imports, the prelude, and `Type.member` through the type.
- **Every example compiles.** The code indented by four spaces below `# Examples` is parsed, held to the
  [formatter canon](the-formatter-canon.md) and type checked.
- **No comment tells the history of its own code.** `used to`, `formerly`, `previously`, `originally`, `at first`,
  `no longer`, `legacy`, `was renamed`, `milestone`, and a plan number behind `gap` or `round` are findings in a doc
  comment and in a `//` comment alike. What stands between backticks is quoted and not prose, so a comment about the
  rule can name the words.
- **A first sentence that only repeats the name** ("The parser." on `type Parser`) is a finding.

The rules themselves are in [`compiler/CONTRIBUTING.md`](../../compiler/CONTRIBUTING.md), and
`std/core/src/option.trb` is the reference every other file is written after.

### How an example is checked

An example is checked as if it stood in a file next to the one it documents: it sees what that file imports and the
file's own public declarations, and it is a script, so it may `print`. Every example of the whole walk is type checked in
**one** run of the front end, the way `docs check` checks the snippets of the pages.

An example that cannot stand alone says so in its first line:

| First line          | What is checked                                                          |
|---------------------|--------------------------------------------------------------------------|
| nothing             | It parses, it is in the canon, and it type checks                        |
| `// fragment`       | It lexes. For a signature or a shape that is not a program               |
| `// skip <reason>`  | Nothing. The reason is required, and the report prints every skip        |

### Where it stands among the gates

`docs source` is **not** one of the gates a change has to pass yet: the writing of the repository's documentation is
still running, so the repository does not pass it. `--statistics` is what measures how far it has come, and the command
becomes mandatory once the writing is done.

## Examples

A clean file names its numbers, so a change in them is visible in a diff:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler docs source ../std/core/src/option.trb
1 files, 15 declarations, 6 examples, no problems
```

A finding names the file, the line and what to do about it:

```console
$ cargo run --release -q -- run ../compiler docs source ../std/text
../std/text/src/lib.trb:2: `used to`: a comment says what the code does now, not what it did before
../std/text/src/lib.trb:12: `fn isDigit` has no doc comment. Say what it does, then what it is for

2 problems in 1 files
```

The progress of every tree, one line each:

```console
$ cargo run --release -q -- run ../compiler docs source ../std ../compiler ../examples --statistics --no-examples
std: 32 of 48 files, 360 of 897 declarations (40%), 9 examples (0 skipped), 7 history words
compiler: 8 of 106 files, 854 of 1009 declarations (84%), 0 examples (0 skipped), 395 history words
examples: 3 of 25 files, 10 of 156 declarations (6%), 0 examples (0 skipped), 0 history words
tests: 1 of 56 files, 0 of 0 declarations (100%), 0 examples (0 skipped), 75 history words
```

The first number of a line is the files that have their module comment, the second the declarations that have a doc
comment. Over the whole repository one run takes about a minute, and about a minute and a half with the type check of
the examples.

## Related

- [The docs commands](../contributing/checks.md) - `docs check`, `index`, `skill` and `bundle`, the gate of the pages.
- [Doc comments](../language/syntax/doc-comments.md) - what `/** */` attaches to, and the headings it carries.
- [torb canon](torb-canon.md) - the canon that an example is held to.
- [Verify your work](verifying-your-work.md) - where the gates of the repository sit.
