---
title: torb canon
summary: torb canon is deprecated - it runs torb format now, with a warning - and the formatter canon it enforced, every rule of it, is what torb format runs first, over the syntax tree and with a safety net.
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
  - compiler/src/main.trb
---

**`torb canon` is deprecated.** It was the command that held every file to the formatter canon until milestone 8's
[`torb format`](torb-format.md) existed. It is an alias of `torb format` now: it prints a warning, then formats with
every rule of the canon and the layout, and `--rule` is accepted and ignored. The canon itself stays - it is the first
thing `torb format` runs - and this page is where its rules are written down.

## Synopsis

```text
torb canon [--check] [--rule <rule>]... <path>...     Deprecated: runs torb format [--check] <path>...

The rules of the canon, every one of which torb format runs:
  calls                    A call becomes a command where the grammar allows it, parentheses everywhere else
  strings                  A multi-line """ string is indented one level deeper than the line it starts on
  imported-case-patterns   .None becomes None for a case a use imported (changes the tree)
  unused-bindings          A binding of a refutable pattern that nobody reads becomes _ (changes the tree)
  loops                    while true { becomes loop { (changes the tree)
```

## What it does

`torb canon <path>...` prints `warning: torb canon is deprecated` on standard error and then does exactly what
`torb format <path>...` does, `--check` included. What follows are the rules of the canon, which `torb format` applies
before its layout.

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

A field is written only with `=`, not by a [property command](../glossary.md#property-command) - `port 8080` is a
compile error, and `port = 8080` is the fix. The one property command left is a trailing block on a field whose value
is a record type: `database { ... }` configures the value in place, which is a fact about what the call means and not
a choice of how to write it.

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

See [Multi-line strings](../language/syntax/multi-line-strings.md) for the four rules of the dedent itself; the rule
above says how the canon indents the source, which is a separate question from what the string is worth.

### `imported-case-patterns`

It changes the syntax tree on purpose (`.None` in a pattern becomes `None`) instead of only its formatting, so it is
exempt from the safety net's tree comparison and is checked by its own tests instead. Only a payload-less case that a
`use` brought into the file counts, which today is `None` from the prelude and any case a file explicitly imports the
same way.

### `unused-bindings`

The mechanical half of a rule the *checker* enforces: in an arm of a `match`, in an `if const`/`if var` and in a
`while const`, a binding whose name is nowhere in the guard or the body becomes `_`.

It works without name resolution and stays on the safe side: a use is "the name occurs as a word anywhere in the text of
the guard or the body", so a comment, a string, an interpolation and a name a nested pattern binds again all count and
leave the binding exactly as it is. `_name` is never touched, and the name of a `...rest` pattern is left alone because
dropping it is more than one token. Whatever is left after a run, the checker names, and a person fixes it. The same
question asked of the *irrefutable* positions - a `const`, a `for`, a closure - is [`torb lint`](torb-lint.md)'s rule
`unread-binding`.

### `loops`

`while true { ... }` becomes `loop { ... }`, so "never ends" is a property of the syntax and not of a condition that
happens to be `true`. Only the head is touched - `while` and the `true` after it - and everything from the `{` on stays
byte-identical, comments and line endings included. `while false` is not an endless loop and is left alone.

### Rewriting, never generating

The canon moves parentheses and indentation over the existing syntax tree; it never reprints a file, so a comment, a
blank line or a line ending it does not touch stays exactly as it was. The layout of the whole line - indentation,
spaces, blank lines - is `torb format`'s, which runs after it.

### The safety net

Every edit is applied on its own and the file is parsed again. The edit only stays if the tree that comes back is the
tree from before with every span and every call style erased - so an edit that would change what the program means is
dropped and reported instead of applied. A file that does not parse to begin with is skipped whole. `torb format`
checks its layout the same way, once per file.

## Examples

The deprecated command, which formats:

```console
$ torb canon --check --rule calls .
warning: `torb canon` is deprecated: it runs `torb format` now, with every rule of the canon (`--rule` is ignored)
0 of 761 files would change
```

`torb docs check` (see the docs commands) holds every `trb` code block of this
documentation to the rules `calls` and `strings`, so a snippet in the wrong style is caught the same way a wrong type is.

## Related

- [torb format](torb-format.md) - the command that runs these rules now, and the layout after them.
- [Command calls](../language/syntax/command-calls.md) - the rule `calls`.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the rule `strings`.
- [Pattern forms](../language/pattern-matching/pattern-forms.md) - the language rule `unused-bindings` sweeps for.
- The docs commands - how this documentation's own snippets are held to the canon.

