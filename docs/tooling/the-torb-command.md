---
title: The torb command
summary: Every subcommand of the toolchain, what it does today, and which of them are still planned.
kind: tooling
status: stable
order: 10
keywords:
  - torb
  - check
  - build
  - run
  - repl
  - test
  - canon
  - highlight
source:
  - compiler/src/main.trb
  - compiler/src/highlight/command.trb
  - compiler/src/cli/run.trb
  - compiler/src/cli/repl.trb
  - CONCEPT.md#toolchain
---

`torb` is a single binary: the runtime, the compiler, the package manager, the test runner, the linter, the formatter and
the language server are all in it. That keeps a project's setup to one installation and one manifest.

## Synopsis

```text
torb run <path> [...]  Run a file or a project in the VM; --native builds and runs it
torb repl              Read entries from standard input, run each in the VM, keep their bindings
torb check [path]...   Check projects, workspaces or single files
torb build [path]      Compile an entry file to a native binary through C
torb ir <path>...      Print the typed IR the back end lowers
torb parse <path>...   Check the syntax of files or directories
torb tokens <file>     Print the tokens of a file
torb ast <file>        Print the syntax tree of a file
torb highlight <file>  Print the semantic tokens of a file as JSON, for an editor
torb natives --header  Write torb_natives.h and machine_natives.c from the manifest of natives
torb manifest [path]...  Evaluate every project.trb the paths reach in the sandboxed VM
torb docs <command>    Check, index, and derive the documentation
torb canon [path]...   Write the formatter canon over the syntax tree
torb test [path]...    Run the *.test.trb files below the paths in the VM; --native builds them
```

`torb` is `build/release/torb`, what [`sh tools/bootstrap.sh`](../ARCHITECTURE.md) writes, and every command below is
run from the repository root:

```console
torb check .
torb test --native compiler/tests
torb canon --check .
```

## What it does

### `run`

Runs a file, or the `build { input }` of a project directory, in the bytecode VM inside `torb`, passing the rest of
the command line to the program as `Process.arguments()` and the program's three streams and its exit code through. A
script may hold top-level code because nothing imports it.

`torb run --native` builds it into `build/run/<key>/` instead and starts the binary [`build`](torb-build.md) would have
written; the key is a hash of every source file that was read, so an unchanged program is not rebuilt. Both run the same
typed IR on the same runtime ([torb run](torb-run.md)).

### `repl`

Reads entries from standard input - a line, or as many as it takes to close what the line opened, `> ` and `. `
prompting each on standard error - checks each against the declarations and bindings of the entries before it, runs
it in the VM and keeps what it declares and binds. The value of an entry that ends in an expression is shown value
first, `3: Int64`; messages name the entry (`<entry 3>`) and its own lines. A piped file is the same session as a
typed one ([torb repl](torb-repl.md)).

### `check`

Resolves every module, every import, every name in a type position and the type of every expression, and reports what is
wrong. It answers `<n> files, no problems` when everything holds, and otherwise one block per diagnostic with the line and
a caret under the span.

| Flag | What it adds |
|------|--------------|
| `--statistics` | How many expressions of every module have a type, and how many are deferred to a later milestone |
| `--timings` | The wall time of every pass, in the order they ran |

`check` is the gate: a false positive of it is a bug in the checker, not a reason to change the program.

### `build`

Compiles the typed IR ahead of time. The first back end prints C and hands it to a C compiler, so `torb build` needs one
on the machine; `--emit-c` writes the C and stops, which needs nothing. `--output <file>` says where the binary goes, and
the C is written next to it. Nothing observable differs between a binary and the same program under `torb run`.

### `parse`, `tokens`, `ast` and `ir`

The four windows into the front end. `parse` checks syntax only, recursively over directories. `tokens` and `ast` print the
lexer and parser output of one file in a deterministic format - the one `compiler/src/syntax/dump.trb` and the
generated `Show` of the syntax tree define. `ir` prints the typed intermediate representation the back end lowers,
with `--statistics` for the counts alone, and `--bytecode` prints what the VM runs instead: the chunks of the final IR,
disassembled.

### `highlight`

Prints the semantic tokens of one file as a single JSON document, which is what an editor colors a name with when the
TextMate grammar cannot tell a field from a local or a case from a type. `torb highlight --stdin` reads the source from
standard input instead of a file, for a buffer that was never saved.

```text
{"tokens": [[line, startCharacter, length, "kind", ["modifier", ...]], ...]}
```

`line` and `startCharacter` are 0-based, and `startCharacter` and `length` count UTF-16 code units, which is what VS
Code's semantic token protocol takes. The tokens are sorted, never overlap and never cross a line break.

It answers from the syntax tree alone and opens no other file, so it costs a parse and not a type check - tens of
milliseconds for a thousand lines, which is what lets an editor ask on every pause in typing. That is also the list of
what it cannot know: the type of an arbitrary receiver, and what a single-segment `use` names in the module it comes
from. A name it cannot place gets **no token**, never a guess, so the grammar's own color stands.

The command never fails: a file with syntax errors is colored as far as it parsed, a file that cannot be read prints an
empty list, and the only exit code is `0`.

### `canon`

Writes the formatter canon over the syntax tree, never with a regular expression: a call becomes a command wherever the
grammar allows it and gets parentheses everywhere else, and a multi-line `"""` string is indented. Every edit is applied on
its own and the file is parsed again; it only stays if the tree is the one from before with every span and call style
erased, so a run cannot change what a program means.

| Flag | What it does |
|------|--------------|
| `--check` | Report the files that are not in the canon, and write nothing |
| `--rule calls` | Only the call form |
| `--rule strings` | Only the indentation of multi-line strings |
| `--rule imported-case-patterns` | `.None` becomes `None`. Has to be asked for |
| `--rule unused-bindings` | A binding of a refutable pattern that nobody reads becomes `_`. Has to be asked for |

`canon` is temporary. Milestone 8's `torb format` enforces the same canon and takes over from it.

### `test`

Runs every `*.test.trb` file below the paths it is given - any test package, and several of them at once, which is
still one binary and one report. The output is the name of a file, then one line per test of it, then the next file -
**in the order of the files**, which is the order their paths sort in - and at the end a blank line and
`N passed, M failed (K files)`. The command leaves with 0 where nothing failed and 1 otherwise.

**One binary for all of them**, and not one per file, because every test file imports its harness and through it
whatever it tests: one binary per file would be one C compile of a translation unit that size per file, and the C
compiler is where the time of a build goes. The whole suite together is about the size of one such translation unit.
That is also why `--jobs` means nothing here - one binary is one process - and it is accepted and ignored rather than
rejected.

`test` and `group` themselves are not a command's: they are ordinary functions of `std/test`, and the report comes
from one place - one line per test, `  ok      ` or `  FAILED  ` with the group names in front of it, and the counts
of the summary. So a test file that is **built** on its own (`torb build one.test.trb`) runs its tests the same way,
which is what the language's own conformance suite compares.

### `manifest`

Evaluates every `project.trb` the paths reach - a manifest named directly, or every package of a workspace below a
directory - as the receiver script it is: in the VM, inside the sandbox a project file gets, against the `Project` of
`std/project` (see [project.trb](project-trb.md)). It prints the settings each one configured, as literal command
calls. With `--check` it prints nothing but a summary, and fails where the static reader the other commands use reads
anything else out of those settings than out of the file itself; `tools/gates.sh a` runs it over every manifest of the
repository.

### `docs`

Four commands over the documentation: `check` is the gate, `index` writes the generated part of every `index.md`, `skill`
derives the Agent Skill, and `bundle` writes `llms.txt` and `llms-full.txt`. See
[the docs commands](../contributing/checks.md).

### What is still planned

These are in the design and not in the binary. A page about one of them carries `status: planned`.

| Command | What it will do |
|---------|-----------------|
| `torb format` | The formatter, taking over from `canon` |
| `torb lint` | The naming and style rules the compiler does not care about |
| `torb doc` | Documentation from the doc comments, including the standard-library pages |
| `torb add`, `remove`, `update`, `audit` | The package manager and the advisory database |

## Examples

Check the whole repository and see that every expression has a type:

```console
$ torb check .
255 files, no problems
$ torb check --statistics .
149982 of 149982 expressions typed (100%), 0 deferred
```

Build a native binary, and look at the C first - see [torb build](torb-build.md) for what the back end does not
lower yet:

```console
$ torb build examples/tour/src/scratch.trb --emit-c --output build/dev/scratch
wrote ../build/dev/program.c
$ torb build examples/tour/src/scratch.trb --output build/dev/scratch
wrote ../build/dev/scratch.exe
```

## Related

- [Verify your work](verifying-your-work.md) - the commands to run before you are done.
- [Run your first program](../guide/installing-and-running.md) - the first use of `run` and `check`.
- [torb check](torb-check.md), [torb run](torb-run.md), [torb repl](torb-repl.md), [torb build](torb-build.md),
  [torb test](torb-test.md), [torb canon](torb-canon.md) - one page per command, in depth.
- [Command calls](../language/syntax/command-calls.md) - the canon that `canon` enforces.
- [The docs commands](../contributing/checks.md) - the four `docs` subcommands.
