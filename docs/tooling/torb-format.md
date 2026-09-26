---
title: torb format
summary: torb format writes TorbScript sources in the one layout of the language - the rules of the formatter canon, then indentation, spaces and blank lines - and --check fails on every file that is not in it.
kind: tooling
status: stable
order: 120
keywords:
  - torb format
  - formatter
  - layout
  - format --check
  - indentation
source:
  - CONCEPT.md#formatter-canon
  - compiler/src/format/command.trb
  - compiler/src/format/layout.trb
---

`format` is the formatter of `torb`, milestone 8's replacement for [`torb canon`](torb-canon.md). It runs every rule
of the canon, then lays out the file: indentation, the spaces between tokens, blank lines and the end of the file. There
is no option: the layout is decided once, for every program, and a file is either in it or not.

## Synopsis

```text
torb format [--check] [path]...

  A path is a file or a directory (default: the current directory)
  --check   Write nothing, list the files that would change, leave with 1 if there is one
```

## What it does

A path is a file or a directory. Below a directory every `.trb` file is formatted except those in a hidden directory,
in a `build` directory - what a build, a test run or a docs check wrote - and in the two directories of files that are
broken on purpose (`parser-cases`, `lexer-cases`). A file that does not parse is left alone and named.

### First the canon

Every rule of [the formatter canon](torb-canon.md) runs, all five of them: a call is a command wherever the grammar
allows it and has parentheses everywhere else, a multi-line `"""` string is indented one level deeper than the line it
starts on, `.None` becomes `None` for a case a `use` imported, an unread binding of a refutable pattern becomes `_`,
and `while true {` becomes `loop {`. Each edit is applied on its own and parsed again, exactly as `torb canon` did.

### Then the layout

**Indentation** is two spaces per level, and it is computed, never kept:

- A line inside `(`, `[` or `{` is one level deeper than the line that opened it, however many brackets that line
  opened; a line that starts with the closing bracket is at the level of the line that opened it.
- A line that continues the one before it is one level deeper than the line its item started on, and every further
  line of the same item is at that level too. A line continues the one before where the lexer says so - it starts with
  an operator or a `.` (except an arm of a `match`), or the line before ends with one - and inside `(...)` and `[...]`
  every line the line before did not end with `,`.
- The block of an `if`, `while`, `for`, `match`, `fn` or `type` whose head runs over several lines is one level deeper
  than the head's first line, not than its last.

```trb
fn describe(items: List<Int>, limit: Int): String {
  const count = items
    .filter({ _ > limit })
    .count()
  if count > 10
    && limit > 0 {
    return "many"
  }
  "{count}"
}
```

**Spaces** between the tokens of a line:

| Where | Spaces |
|-------|--------|
| Around an infix operator (`+`, `==`, `&&`, `??`, the bit operators), `=` and `=>` | one on both sides |
| In front of `,` and `:` | none, and one behind them |
| Inside `(...)` and `[...]` | none |
| Inside `{ ... }` | one, and none in an empty `{}` |
| Around `.`, `?.`, `..` and `..=` | none |
| Behind a prefix `-`, `!`, `~`, `...` and the `.` of `.Case` | none |
| In front of a postfix `?` | none |
| Around the `<` and `>` of type arguments | none |
| Between a callee and its `(`, a value and the `[` of an index | none |
| In front of a comment behind code | as many as there were, at least one |

Which `<` compares and which one opens type arguments, and which `-` subtracts and which one negates, is read from the
syntax tree, not guessed from the spaces around it.

**Blank lines**: at most one in a row, none at the start of the file, none behind a line that ends with an opening
bracket and none in front of a line that starts with a closing one. The file ends with exactly one line break, and no
line ends with a space.

### What it keeps

- **Every line break between two tokens.** A line is never joined with the next one and never broken in two, so a call
  that runs over several lines stays laid out the way it was written - and a line that is too long stays too long. Where
  a long line breaks is not decided yet (see below).
- **Comments**, where they are. A comment on a line of its own takes the level of the line after it; a block comment
  that runs over several lines moves with its first line, and so does its margin of `*`.
- **The inside of every string**, interpolations included: a string is one token, and only the rule `strings` of the
  canon moves the lines of a multi-line one.
- **Each file's line endings.** Every line break the layout writes is the file's first one, so a file with CRLF stays
  CRLF; the line breaks inside a string or a comment are never touched.

### The safety net

The layout of every file is checked the way the canon checks an edit: the file is parsed again, and the syntax tree that
comes out - with every span and every call style erased - has to be the tree that went in. If it is not, the layout of
that file is dropped and reported as `dropped`, because a formatter that would change what a program means is a bug.

The whole of it - canon, layout, and the rule `strings` again where the layout moved a line - runs until the text stays
as it is, which is what makes it idempotent: a second run over its output changes nothing.

### Exit codes

`0` when nothing needs to change, or after writing. `--check` exits `1` when a file would change, so it composes with a
shell's `&&` and with continuous integration the same way [`check`](torb-check.md) does. Either way `1` when a safety
net dropped something. An argument `format` does not recognize prints its usage and exits `2`.

### What is not decided

**Where a long line breaks.** `torb format` owns the layout of a line but not the line breaks between lines: it has no
width it holds a line to and never reflows a call. Breaking a line is the one layout decision that changes the shape of
a whole construct - which arguments go on their own line, where a chain of calls breaks - and it is left to the author
until the language decides on a width and on the shape a broken call takes.

## Examples

A clean repository:

```console
$ torb format --check .
0 of 761 files would change
```

A file that is not in the layout:

```console
$ torb format --check examples/tour/src/scratch.trb
examples/tour/src/scratch.trb
1 of 1 file would change
$ torb format examples/tour/src/scratch.trb
examples/tour/src/scratch.trb
1 of 1 file changed
$ torb format --check examples/tour/src/scratch.trb
0 of 1 file would change
```

That is the tier A gate of [compiler/CONTRIBUTING.md](../../compiler/CONTRIBUTING.md), and `sh tools/gates.sh a` runs
it over the whole repository.

## Related

- [torb canon](torb-canon.md) - the rules of the canon `format` runs first, and the command that is an alias of it now.
- [torb lint](torb-lint.md) - the other milestone 8 tool, for the rules of style that are not layout.
- [Command calls](../language/syntax/command-calls.md) - the rule `calls`.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the rule `strings`, and the dedent it relies on.
- [Verify your work](verifying-your-work.md) - where `format --check` sits among `check` and `test`.
