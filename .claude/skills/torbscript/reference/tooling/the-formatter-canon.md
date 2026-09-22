---
title: The formatter canon
summary: The one way every TorbScript source is written - a command wherever the grammar allows it, and a multi-line string indented two spaces deeper than the line it starts on.
kind: tooling
status: stable
order: 80
keywords:
  - formatter canon
  - command call
  - torb canon
  - torb format
source:
  - CONCEPT.md#formatter-canon
---

Both a command call and a parenthesized one parse, so which one is written is a question of style, and the language
answers it once instead of leaving it to a project's own preference: `torb canon` enforces it today, and it is
checked in both directions, never only against parentheses.

## Synopsis

```text
torb canon [--check] <path>...     Write, or report, the canon of the syntax tree
torb format [path]...              The formatter that replaces it (planned)
```

## What it does

### A call is a command wherever the grammar allows it

Every one of these has to hold, together: the call stands in command position - the start of a statement, the right
of `=` in a binding or an assignment, after `return`, or after `=>`; the callee is a name or a member path; it has at
least one argument, and the first one does not start with `(`, `[`, `-`, `!` or `.`; no argument has an operator at
its top level; and every argument is on one line, except a trailing closure, which may run over several.

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

See [Multi-line strings](../language/syntax/multi-line-strings.md) for the four rules of the dedent itself; this page
only says how the canon indents the source, which is a separate question from what the string is worth.

### Who enforces it, and how much of it

`torb canon` (see [torb canon](torb-canon.md)) writes both rules today, over the syntax tree and never with a regular
expression, with a safety net that drops any edit that would change what the program means. It is temporary:
milestone 8's `torb format`, written in TorbScript, takes over the same two rules and replaces it. Neither tool
decides line length, blank lines or where a long call breaks - that is layout, and it is `torb format`'s alone; a
file `torb canon --check` accepts can still be reflowed differently once `torb format` exists.

Two further rules exist in `torb canon` and are off by default, because they change the syntax tree on purpose instead
of only its formatting: `imported-case-patterns` (`.None` becomes `None` for a case a `use` imported) and
`unused-bindings` (a binding of a refutable pattern that the arm never mentions becomes `_`). Neither is part of the
canon a file has to be in - they are sweeps for a rule of the *language*, and the checker is what enforces that. See
[torb canon](torb-canon.md).

## Examples

`torb docs check` (see [the docs commands](../contributing/checks.md)) holds every `trb` code block of this
documentation to the call rule, so a snippet in the wrong style is caught the same way a wrong type is:

```console
$ torb docs check docs
178 pages, 24 folders, 484 snippets, no problems
```

## Related

- [torb canon](torb-canon.md) - the command that writes and checks this canon today.
- [Command calls](../language/syntax/command-calls.md) - the call rule, with every position it applies to.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the dedent the indentation rule sits on top of.
- [The docs commands](../contributing/checks.md) - how this documentation's own snippets are held to the canon.
