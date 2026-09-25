---
title: torb repl
summary: torb repl reads entries from standard input, checks each against the session, runs it in the bytecode VM and keeps what it binds and declares for the next entry - a typed session and a piped file behave the same.
kind: tooling
status: stable
order: 45
keywords:
  - torb repl
  - REPL
  - interactive
  - session
  - ":type"
source:
  - compiler/src/cli/repl.trb
  - compiler/src/repl/session.trb
  - compiler/src/repl/entry.trb
  - compiler/src/repl/synthesis.trb
  - docs/design/REPL.md
---

`torb repl` is a shell for TorbScript. Every entry is checked against everything the session holds - the declarations
and the bindings of the entries before it - compiled, and run once in the VM inside `torb`; what it binds with `const`
or `var` and what it declares stay for the entries after it. It reads plain standard input, so `torb repl < setup.trb`
is the same session a person would have typed, prompts and all: `> ` before a whole new entry, `. ` while one is still
open, written to standard error so standard output stays exactly what the entries printed and showed.

## Synopsis

```text
torb repl                  Read entries from standard input until its end or :quit
    --sandbox               Every entry under SCRIPTS.md's grant of the working directory, not unrestricted
    :type <expression>     The type of an expression, checked and not run
    :load <file>           A file as one entry
    :reset                 A new session: bindings released, declarations forgotten, files read again
    :help                  The commands
    :quit                  The end of the session
```

## What it does

### An entry

An entry is one line, or as many lines as it takes to be whole: a line that leaves a bracket open, ends with an
operator or `=`, or opens a `"""` string or a `/*` comment that it does not close is continued by the next one. Two
empty lines in a row drop an entry that never became whole. Everything else that does not parse is reported as it is.

An entry may hold declarations - `fn`, `type`, `trait`, `extend`, `use` - and statements, in any order. The
declarations are kept for every later entry; the statements run once, in order. A name the top level binds with
`const` or `var` is a binding of the session once the entry ran to its end.

### What it prints

What the entry prints goes where a program's output goes. Where the last statement is an expression with a value, the
session shows it on standard output Scala/Kotlin style, value first: `1 + 2` shows `3: Int64`, through `Show`, with the
type dimmed when standard output is a terminal and plain otherwise. A printed line and a shown value are told apart by
the trailing `: Type`, and `print "hello"` shows nothing because it answers `Void`. A value whose type has no `Show`,
such as a function, is shown as its type alone, in angle brackets. Where the last statement binds instead of
evaluating - `const total = 6 * 7` - every name it binds is shown the same way, with its name in front: `total: Int64 =
42`.

Messages go to standard error, rendered as [`torb check`](torb-check.md) renders them, with the entry as the file:
`<entry 3>` is the third entry of the session, a `:load`ed file is named by its path, and the line and the column are
the entry's own. A panic stops the entry and not the session, and prints what a native program prints for it, with the
entry's line; a `?` at the top level that fails prints `error: ` and the failure, as an entry file does. Ctrl+C stops a
running entry the same uneventful way, `error: Interrupted`, and returns to the prompt with the session intact.

### Bindings and declarations

`const count = count + 1` binds a new `count`, and the old value is released; a `var` binding is changed in place by
every entry that assigns it. Declaring a function or a type again replaces it for every later entry. A binding whose
type names a type that is declared again keeps working: the old type is kept once more under a generation of its own,
and the binding is shown with it, `Point#1` then `#2` - nothing about the value is reinterpreted as the new type. A
declaration cannot see the session's bindings, as a top-level function of a program cannot see the locals of the code
that calls it; a closure can.

An entry that stops keeps the declarations it checked and the changes it made to `var` bindings before the stop, and
none of the bindings it would have made. The end of the session releases every binding, the newest first, so that a
`close()` runs as it would at the end of a program.

### What an entry may do

Everything the user may: every file, every variable of the environment, `std` and, in a workspace, the packages the
working directory's package depends on - the session is a file of the working directory that exists only in memory.
Relative paths are read against the working directory. `Process.exit(code)` ends the session with that code. What the
VM does not run yet - tasks, `test` and `group` - is refused with the reason before anything of the entry runs.

`torb repl --sandbox` narrows this to [SCRIPTS.md](../design/SCRIPTS.md)'s own grant instead: the working directory
read and write, no variable of the environment, and its default limits (1,000,000 steps, 64 MB, 2000 ms), a fresh
budget every entry - a runaway one stops itself without needing Ctrl+C. `:reset` keeps `--sandbox` for the session it
starts fresh. Not narrowed: which packages of `std` an entry may `use` - that check is for a script a *program* loads
without seeing its text first, and a person at their own prompt already does.

### Exit codes

`0` at the end of the input or after `:quit`, or the code an entry handed to `Process.exit`.

## Examples

A session, typed or piped in:

```console
$ torb repl
torb repl: one entry at a time, :help lists the commands
> const prices = [3, 5, 8]
> prices.map({ _ * 2 }).toList()
[6, 10, 16]: List<Int64>
> fn total(values: List<Int>): Int {
.   values.sum()
. }
> total(prices)
16: Int64
> :type prices
List<Int64>
> prices[5]
panic: index 5 is out of bounds for a length of 3
  at <entry 5>:1:1
>
```

## Related

- [The REPL](../design/REPL.md) - the design record: how an entry becomes a module, where the bindings live.
- [torb run](torb-run.md) - `--vm` runs a whole program in the same VM.
- [The torb command](the-torb-command.md) - every subcommand in one table.
