---
title: torb canon
summary: torb canon rewrites sources into the formatter canon over the syntax tree, checks every edit against a second parse, and drops anything that would change what the program means.
kind: tooling
status: stable
order: 70
keywords:
  - torb canon
  - formatter canon
  - safety net
  - imported-case-patterns
  - unused-bindings
  - loops
source:
  - compiler/src/canon/command.trb
  - compiler/src/canon/walk.trb
---

`canon` is a command of `torb`, over the parser and the syntax tree of `compiler/src/canon`. It exists because
milestone 8's `torb format` does not exist yet, and every file in the repository has to be in the canon in the
meantime.

## Synopsis

```text
torb canon [--check] [--rule <rule>]... <path>...

Rules (calls and strings run by default):
  calls                    A call becomes a command where the grammar allows it, parentheses everywhere else
  strings                  A multi-line """ string is indented one level deeper than the line it starts on
  imported-case-patterns   .None becomes None for a case a use imported (changes the tree, off by default)
  unused-bindings          A binding of a refutable pattern that nobody reads becomes _ (changes the tree, off by default)
  loops                    while true { becomes loop { (changes the tree, off by default)

  --check   Write nothing, list what would change, leave with a non-zero code
```

## What it does

### A call is a command wherever the grammar allows it

Both a command call and a parenthesized one parse, so which one is written is a question of style, and the language
answers it once instead of leaving it to a project's own preference. Every one of these has to hold, together: the
call stands in command position - the start of a statement, the right of `=` in a binding or an assignment, after
`return`, or after `=>`; the callee is a name or a member path; it has at least one argument, and the first one does
not start with `(`, `[`, `-`, `!` or `.`; no argument has an operator at its top level; and every argument is on one
line, except a trailing closure, which may run over several.

```trb
const role = Role name
const email = Email.tryFrom text
names.map Role
print "Hello"
```

Everywhere else, a call keeps its parentheses: nested, because a command argument is an ordinary expression and does
not itself become a command; without an argument; with an operator at the top level of an argument; over several
lines; and in the head of an `if`, `for`, `while` or `match`.

```trb
Ok Some(x)                  // nested: the argument keeps its parentheses
list.length()                // no arguments
assert(sum == 3)              // an operator at the top level of the argument
if ready(now) { }              // the head of an if
```

A call on a field is a [property command](../glossary.md#property-command) instead - `port 8080` writes the field,
it does not call it - which is a fact about what the call means and not a choice of how to write it.

### A multi-line string is indented

The opening `"""` stays where it is; the content is two spaces deeper than the line the statement starts on; a
closing `"""` that stands alone is aligned with the content. Because the dedent subtracts the indentation of the
first content line, moving the whole block left or right changes nothing about the value - only how it reads next to
the code around it.

```trb
fn generated(): String {
  const header = """
    #include <stdint.h>
    """
  header
}
```

See [Multi-line strings](../language/syntax/multi-line-strings.md) for the four rules of the dedent itself; the two
rules above say how the canon indents the source, which is a separate question from what the string is worth.

Neither rule decides line length, blank lines or where a long call's arguments break across several lines - that is
layout, and it stays [`torb format`](torb-format.md)'s alone, once milestone 8 replaces `canon` with it. A file
`canon --check` accepts today can still be laid out differently once `torb format` exists.

### Rewriting, never generating

`canon` moves parentheses and indentation over the existing syntax tree; it never reprints a file, so a comment, a
blank line or a line ending it does not touch stays exactly as it was.

### The safety net

Every edit is applied on its own and the file is parsed again. The edit only stays if the tree that comes back is the
tree from before with every span and every call style erased - so an edit that would change what the program means is
dropped and reported instead of applied. A file that does not parse to begin with is skipped whole.

```console
$ torb canon --check examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 file would change: 2 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 0 endless loops
```

```console
$ torb canon examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 file changed: 2 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 0 endless loops
$ torb canon --check examples/tour/src/scratch.trb
0 of 1 file would change: 0 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 0 endless loops
```

A second run over an already-canonical file changes nothing, which is what makes `--check` a gate: a repository where
it reports `0 of <n> files would change` is a repository `canon` agrees with.

### `imported-case-patterns`

Off by default, because it changes the syntax tree on purpose (`.None` in a pattern becomes `None`) instead of only
its formatting, so it is exempt from the safety net's tree comparison and is checked by its own tests instead. Only a
payload-less case that a `use` brought into the file counts, which today is `None` from the prelude and any case a
file explicitly imports the same way.

```console
$ torb canon --check --rule imported-case-patterns examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 file would change: 0 calls to commands, 0 calls to parentheses, 0 strings indented, 1 case pattern, 0 unread bindings, 0 endless loops
```

### `unused-bindings`

Off by default for the same reason, and the mechanical half of a rule the *checker* enforces: in an arm of a `match`, in
an `if const`/`if var` and in a `while const`, a binding whose name is nowhere in the guard or the body becomes `_`.

It works without name resolution and stays on the safe side: a use is "the name occurs as a word anywhere in the text of
the guard or the body", so a comment, a string, an interpolation and a name a nested pattern binds again all count and
leave the binding exactly as it is. `_name` is never touched, and the name of a `...rest` pattern is left alone because
dropping it is more than one token. Whatever is left after a run, the checker names, and a person fixes it.

```console
$ torb canon --check --rule unused-bindings .
0 of 293 files would change: 0 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 0 endless loops
```

### `loops`

Off by default for the same reason: `while true { ... }` becomes `loop { ... }`, so "never ends" is a property of the
syntax and not of a condition that happens to be `true`. Only the head is touched - `while` and the `true` after it -
and everything from the `{` on stays byte-identical, comments and line endings included. `while false` is not an
endless loop and is left alone.

```console
$ torb canon --check --rule loops examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 file would change: 0 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 1 endless loop
```

### Exit codes

`0` when nothing needs to change, or after writing; `--check` exits `1` when a file would change, so it composes with
a shell's `&&` and with continuous integration the same way [`check`](torb-check.md) does. An argument `canon` does
not recognize prints its usage and exits `2`.

## Examples

The gate runs before every commit, with all five rules, over the whole repository:

```console
$ torb canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops .
0 of 488 files would change: 0 calls to commands, 0 calls to parentheses, 0 strings indented, 0 case patterns, 0 unread bindings, 0 endless loops
```

That is the tier A gate of [compiler/CONTRIBUTING.md](../../compiler/CONTRIBUTING.md), and `sh tools/gates.sh a` runs
it with exactly those five rules.

`torb docs check` (see [the docs commands](../contributing/checks.md)) holds every `trb` code block of this
documentation to the call rule too, so a snippet in the wrong style is caught the same way a wrong type is:

```console
$ torb docs check docs
178 pages, 24 folders, 484 snippets, no problems
```

## Related

- [torb format](torb-format.md) - the tool that replaces `canon` in milestone 8, and takes over layout as well.
- [Command calls](../language/syntax/command-calls.md) - the rule `calls` enforces.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the rule `strings` enforces.
- [Pattern forms](../language/pattern-matching/pattern-forms.md) - the language rule `unused-bindings` sweeps for.
- [The docs commands](../contributing/checks.md) - how this documentation's own snippets are held to the canon.
- [Verify your work](verifying-your-work.md) - where `canon --check` sits among `check` and `test`.
