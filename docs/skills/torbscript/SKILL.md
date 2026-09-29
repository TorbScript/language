---
name: torbscript
description: "Writes, runs and checks TorbScript, the value-semantics language of `.trb` files, and finds or installs its `torb` toolchain. Use it for any task that touches `.trb` files, a `project.trb` or a `torb` command, and before writing TorbScript from memory: it looks like Rust, Swift and Kotlin but differs from all three in mutation, errors, cases and call syntax."
license: MIT
compatibility: "Runs the torb command in a shell. Installing it downloads the official installer from torb.dev."
---

<!-- carry: guide/ tooling/ language/syntax/cheat-sheet.md explanation/mistakes-models-make.md -->
<!-- carry: how-to/index.md how-to/set-up-your-editor.md -->

# TorbScript

TorbScript is a functional-first language with value semantics. `torb` is its whole toolchain: it runs a program in its
VM, checks, formats and tests it, and builds a native binary through a C compiler. Source files end in `.trb`, and a
project is a directory with a `project.trb`. Paths in this skill are relative to the directory of this file.

## The toolchain

Before the first `torb` command of a task, run the check. It changes nothing:

```sh
sh scripts/toolchain.sh
```

In Windows PowerShell without a POSIX shell, run
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/toolchain.ps1` instead. Both print one line per fact and
exit 0 when `torb` runs, 1 when it does not:

```text
torb: /home/ada/.torb/bin/torb
version: 0.1.0
on-path: yes
c-compiler: clang
```

- `on-path: no` - call `torb` by the path the line names.
- `c-compiler: none` - `run`, `check`, `test` and `format` work; `torb build` and `--native` need a C compiler
  (`$TORB_CC`, or `clang`, `gcc`, `cc` on the `PATH`).
- `torb: missing` - install it, as below.

### Installing

**Ask the user before installing.** Installing downloads the official installer - `https://torb.dev/install.sh`, or
`install.ps1` on Windows - and runs it. The installer checks the archive against the release's SHA-256 sums and
unpacks the toolchain for the current user: into `~/.torb` on Linux, macOS and FreeBSD, into `%LOCALAPPDATA%\torb` on
Windows with the one in use in `%LOCALAPPDATA%\Programs\torb`. It takes the newest stable release, or the nightly while
there is no stable one yet. With the user's consent:

```sh
sh scripts/toolchain.sh --install
```

(`-Install` for the PowerShell script.) It runs the installer only when `torb` is missing or does not run, so a second
run changes nothing, and it prints the facts again when it is done. On Linux, macOS and FreeBSD nothing is written into
a shell profile: tell the user to add `export PATH="$HOME/.torb/bin:$PATH"` to theirs. On Windows the installer adds
its directory to the user's `PATH`, which a shell started before sees only after a restart. Later, `torb upgrade`
installs a newer release beside the current one.

## Starting and running

```text
torb new <name> --yes    A project: project.trb, src/main.trb, tests/main.test.trb
torb init --yes          The same, into the current directory
torb run [path]          Check and run a file, or the project's program, in the VM
torb test [path]         Run every *.test.trb file below the path
torb build [path]        A native binary through a C compiler, in the release profile
```

`new` and `init` start from a template of git.torb.dev (`--template app` for a program, `package` for a library) and
fall back to a built-in scaffold offline or with `--offline`. They run `git init` unless told `--no-git`, which is what
a project inside an existing repository wants. A `.trb` file that nothing imports is a script: `torb run hello.trb`
runs it without a project and without a `fn main`. [The torb command](references/tooling/the-torb-command.md) lists
every subcommand and flag.

## The language in sixty seconds

<!-- inline: guide/the-language-in-sixty-seconds.md -->

## Two files to look things up in

- [references/language/syntax/cheat-sheet.md](references/language/syntax/cheat-sheet.md) - every form of the language
  with its exact spelling: how is this written?
- [references/explanation/mistakes-models-make.md](references/explanation/mistakes-models-make.md) - the lines that
  look right and are not, each with its diagnostic: why does this not compile?

Search them instead of reading them whole: `grep -n "^###" <file>` lists the sections of either one, and a section is a
few dozen lines. Before handing over code, scan the section headings of the second one.

## Verify your work

<!-- inline: tooling/verifying-your-work.md -->

## The other TorbScript skills

Load the one a task needs, when it needs it:

<!-- skills -->

## What else this skill holds

<!-- sections -->

`references/index.md` lists every page of this skill with its summary: `grep -i "<word>" references/index.md`.
