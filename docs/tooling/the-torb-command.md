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
  - format
  - lint
  - rename
  - lsp
  - canon
  - highlight
source:
  - compiler/src/main.trb
  - compiler/src/highlight/command.trb
  - compiler/src/cli/run.trb
  - compiler/src/cli/repl.trb
  - compiler/src/format/command.trb
  - compiler/src/lint/command.trb
  - compiler/src/lint/rename-command.trb
  - compiler/src/language-server/command.trb
  - CONCEPT.md#toolchain
---

`torb` is a single binary: the runtime, the compiler, the package manager, the test runner, the linter, the formatter and
the language server are all in it. That keeps a project's setup to one installation and one manifest.

## Synopsis

```text
torb run [path|name] [...]  Run a file or a program in the VM; --native builds and runs it
torb repl              Read entries from standard input, run each in the VM, keep their bindings
torb check [path]...   Check projects, workspaces or single files
torb build [path|name]  Compile every program below the path, or the one named, through C
torb ir <path>...      Print the typed IR the back end lowers
torb parse <path>...   Check the syntax of files or directories
torb tokens <file>     Print the tokens of a file
torb ast <file>        Print the syntax tree of a file
torb highlight <file>  Print the semantic tokens of a file as JSON, for an editor
torb natives --header  Write torb_natives.h and machine_natives.c from the manifest of natives
torb manifest [path]...  Evaluate every project.trb the paths reach in the sandboxed VM
torb docs <command>    Check, index, and derive the documentation
torb doc [path]...     The reference of a package: its public API and doc comments as a static site
torb format [path]...  Write sources in the layout of the language; --check only reports
torb lint [path]...    The rules of style the checker leaves alone; --fix writes their fixes
torb rename <Type>.<field>=<name>... [path]...  Rename fields everywhere the checker resolves them
torb lsp               The language server, over standard input and output, for an editor
torb canon [path]...   Deprecated: runs torb format
torb test [path]...    Run the *.test.trb files below the paths in the VM; --native builds them
torb add <package>...  Add a dependency to project.trb, resolve, lock and install it
torb remove <package>...  Remove a dependency from project.trb and project.lock.trb
torb update [package]...  Resolve every package, or the named ones, to the highest version allowed
torb install           Fetch and verify every package project.lock.trb pins into the cache
torb publish           Build and check the archive of a package, and publish it (--dry-run: build only)
torb lock [--check]    Write the settings of every member into project.lock.trb; --check only compares
torb --version         Print `torb <version>`: the one number of the toolchain, the language and std

Every command: --color auto|always|never   colour where the output is a terminal, always, or never
```

`torb` is `build/release/torb`, what [`sh tools/bootstrap.sh`](../ARCHITECTURE.md) writes, and every command below is
run from the repository root:

```console
torb check .
torb test --native compiler/tests
torb format --check .
```

An argument that starts with `--` and is none of the flags of its subcommand is refused before anything runs, with
`error: unknown flag <argument>`, a pointer to `torb --help` and exit code 2, rather than looked for as a file or a
program of that name; a path that starts with `--` is written `./--name`. Behind the path of `run` every argument is
the program's own. `torb --help` prints the usage.

## What it does

### `--version`

Prints one line to standard output, `torb 0.1.0`, and leaves with 0. The number is the one of
[RELEASE.md](../design/RELEASE.md) section 3: the toolchain, the language and the standard library share it, so it is
also what the language server names in its `serverInfo` and what the `language` line of a release in the index of a
registry is compared with.

### `--color`

Every command takes `--color auto`, `--color always` or `--color never` (also written `--color=never`), in front of the
command or anywhere among its own arguments - but not behind the path of `torb run`, where every argument is the
program's. `torb` colours what its output means, in the 16 colours of the terminal's own theme:

| What | Colour |
|------|--------|
| the word `error` | bold red, and its carets `^^^` red |
| the word `warning` of `torb lint` | bold yellow, and its carets yellow |
| the message after the word | bold |
| ` --> `, the bar of the gutter and the line numbers | faint |
| the `=` in front of a note | bold cyan |
| a summary that found nothing, `3 files, no problems` | green |
| a summary that counts problems, `1 problem in 1 of 1 file` | bold |
| `ok` and `FAILED` of `torb test`, and its summary | green, bold red, and green or bold |
| the name in `torb --version`, a type the REPL shows | bold, faint |

Colour never carries anything alone: every diagnostic starts with its word, the carets mark its span and every summary
counts in words, so a run without colour loses nothing - and it writes exactly the bytes it wrote before `torb` had
colours, which is what every golden file compares. The first of these rules that applies decides:

1. `--color always` or `--color never`.
2. `FORCE_COLOR` or `CLICOLOR_FORCE` set to anything but empty or `0`: colour.
3. `NO_COLOR` set to anything but empty: no colour ([no-color.org](https://no-color.org)).
4. `auto`, the default: colour where the output is a terminal whose `TERM` is not `dumb`. Today standard error is
   coloured where standard output is a terminal, as an interactive `torb` has one terminal for both.

On Windows a console shows the colours once `torb` has turned on its processing of escape sequences, which every
console of Windows 10 and later has; one that refuses gets plain text. Where `COLORTERM` is `truecolor` or `24bit` and
the ground of the terminal is known - `TORB_BACKGROUND=light` or `dark`, or the last field of `COLORFGBG` - `torb`
writes the text colours of the brand in 24 bits instead ([BRAND.md](../design/BRAND.md) section 10): a 24-bit colour
chosen without knowing the background can vanish on it. The red of an error is never the brand's red.

`torb test --native` hands `--color always` or `--color never` to the test binary, which writes its own report; with
`auto` the binary decides the same way, on the same streams.

```console
$ torb check --color always src/main.trb 2>&1 | cat -v
^[[1;31merror^[[0m^[[1m: Cannot find `prnt` here^[[0m
 ^[[2m-->^[[0m src/main.trb:1:1
  ^[[2m|^[[0m
^[[2m1 |^[[0m prnt "hello"
  ^[[2m|^[[0m ^[[31m^^^^^[[0m

^[[1m1 problem in 1 of 1 file^[[0m
```

### `run`

Runs a file, the only program below a directory (the working directory where nothing is named), or the program a name
names, in the bytecode VM inside `torb`, passing the rest of the command line to the program as `Process.arguments()`
and the program's three streams and its exit code through. An argument with a `/` or a `\`, one that ends in `.trb`,
and `.` and `..` are paths; anything else is a program name. A script may hold top-level code because nothing imports
it.

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
| `--every-target` | Every program and test lowered once per target of the toolchain, without C: a native reached on a system it does not exist on |
| `--partial` | Every operation of the checked files that can panic and that the compiler did not prove: a call of `std/` that can panic (an index, a slice, `expect`, a key of a map), integer arithmetic that still carries its check, an index step that still compares. An audit for code that must not stop, one line per site; it changes no answer of `check` |

`check` is the gate: a false positive of it is a bug in the checker, not a reason to change the program.

### `build`

Compiles the typed IR ahead of time: every program below the path (default `.`) - each package's `src/main.trb` and
its `program` lines - or the one a name names, each into `build/<profile>/<program>` of its package; a library is
checked and builds nothing. The first back end prints C and hands it to a C compiler, so `torb build` needs one on the
machine; `--emit-c` writes the C and stops, which needs nothing. `--output <file>` says where the binary of one program
goes, and the C is written next to it. Nothing observable differs between a binary and the same program under
`torb run` ([torb build](torb-build.md)).

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
empty list, and the only exit code is `0`. The language server's semantic tokens are these, sharpened by the checker;
the VS Code extension asks `highlight` where the language server does not run.

### `lsp`

The language server: the Language Server Protocol over standard input and output, which is what an editor starts. It
holds the workspace between two requests and answers from the checker's tables - the diagnostics of `check` with their
ranges, hover, go to definition, completion, semantic tokens, and the fixes of `lint` as quick fixes - and a keystroke
checks the file that changed and the open files that import it, not the workspace ([torb lsp](torb-lsp.md),
[Set up your editor](../how-to/set-up-your-editor.md)).

### `format`

Writes the one layout of the language over the syntax tree, never with a regular expression: first every rule of the
formatter canon - a call becomes a command wherever the grammar allows it and gets parentheses everywhere else, a
multi-line `"""` string is indented, and the three rules that rewrite a small form of the tree - then indentation, the
spaces between tokens and blank lines. Line breaks between tokens, comments and each file's line endings are kept. Every
edit and every layout is parsed again and only stays if the tree is the one from before with every span and call style
erased, so a run cannot change what a program means ([torb format](torb-format.md)).

| Flag | What it does |
|------|--------------|
| `--check` | Report the files that are not in the layout, write nothing, and leave with 1 if there is one |

### `lint`

Reports the rules of style the type checker leaves alone, each finding in the format of `check` with `warning` in
front and the id of its rule under it: the own name of a type where `Self` means the same (`self-name`), a `Bool` field
named as a question (`question-field`, which runs the checker for the rename it fixes with), a binding of an
irrefutable pattern that nothing reads (`unread-binding`),
`true`, `false` or `None` for a `Bool` or an optional without its label (`labeled-literal`, which runs the checker),
a written `Some` or `Ok` the value would become on its own (`redundant-wrap`, which runs the checker), and a
`private(var)` field, which is spelled `protected var` (`protected-field`) ([torb lint](torb-lint.md)).

| Flag | What it does |
|------|--------------|
| `--fix` | Write every fix a rule is certain of, then lint again and report what is left |
| `--rule <id>` | Run this rule; without it every rule runs |
| `--skip <id>` | Leave this rule out |

### `rename`

Renames fields at their declaration and at every use the checker resolves to them - a member, a bare name inside the
type, the label of a constructor, a `copy`, a case or a pattern, a `with ... by` delegate, a doc link - and nothing
else: a parameter or a local of the same name keeps it. A rename that cannot be made is printed and nothing is written
([torb rename](torb-rename.md)).

### `canon`

Deprecated: `torb canon` prints a warning and runs `torb format` with the same paths and `--check`; `--rule` is accepted
and ignored, because `format` runs every rule of the canon ([torb canon](torb-canon.md)).

### `test`

Runs every `*.test.trb` file below the paths it is given - any test package, and several of them at once, which is
still one binary and one report - and without a path every one of the package the working directory is in. The output
is the name of a file, then one line per test of it, then the next file - **in the order of the files**, which is the
order their paths sort in - and at the end a blank line and `N passed, M failed (K files)`. The command leaves with 0
where nothing failed and 1 otherwise.

**One binary for all of them**, and not one per file, because every test file imports its harness and through it
whatever it tests: one binary per file would be one C compile of a translation unit that size per file, and the C
compiler is where the time of a build goes. The whole suite together is about the size of one such translation unit.
That is also why `--jobs` means nothing here - one binary is one process - and it is accepted and ignored rather than
rejected. What runs a suite on several cores is `--shard k/n`: the k-th of every n files, so n processes of the one
binary run it between them, and `--shards n` builds the binary once and runs all n of them at the same time.
`--filter <name>` runs the test of one full name, or every test of a group, and `--report json` writes JSON Lines in
place of the lines a person reads, an event per file and per test, which is what an editor's list of tests reads
([torb test](torb-test.md)).

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

### `add`, `remove`, `update`, `install`, `publish` and `lock`

The package manager. `add`, `remove` and `update` edit `project.trb` where they must, resolve every member of the
workspace together by PubGrub - the highest version every requirement allows, and an explanation where there is none -
and write [project.lock.trb](project-lock-trb.md); `install` fetches what the lock pins into the cache and checks every
tree hash; `publish` builds the archive of a package exactly as a registry receives it and writes it into a `file:`
registry; `lock` writes the `settings` block of every member from its evaluated manifest and leaves the graph alone,
and `lock --check` writes nothing and fails where the file is not what `lock` would write. Every one of them takes
`--project <directory>` and `--offline`. See [torb add](torb-add.md), [torb remove](torb-remove.md),
[torb update](torb-update.md), [torb install](torb-install.md), [torb publish](torb-publish.md) and
[torb lock](torb-lock.md).

### `docs`

Four commands over the documentation: `check` is the gate, `index` writes the generated part of every `index.md`, `skill`
derives the Agent Skill, and `bundle` writes `llms.txt` and `llms-full.txt`. See
[the docs commands](../contributing/checks.md).

### `doc`

Turns the public API of a package and its doc comments into a reference: a static site with an index per package and
a page per module (`--output`, default `build/doc`), or the same model as one JSON document for an editor or a registry
(`--json`). `--check` writes nothing and fails on a link of a doc comment that resolves to nothing and on an example
under `# Examples` that does not compile or run ([torb doc](torb-doc.md)).

### What is still planned

These are in the design and not in the binary. A page about one of them carries `status: planned`.

| Command | What it will do |
|---------|-----------------|
| `torb audit`, `yank`, `login`, `owner`, `vendor` | The rest of the package manager, the advisory database, and publishing to a registry on the network |

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
  [torb test](torb-test.md), [torb format](torb-format.md), [torb lint](torb-lint.md), [torb lsp](torb-lsp.md),
  [torb doc](torb-doc.md) - one page per command, in depth.
- [Command calls](../language/syntax/command-calls.md) - the rule of the canon that `format` writes first.
- [The docs commands](../contributing/checks.md) - the four `docs` subcommands.
