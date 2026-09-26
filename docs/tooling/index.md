---
title: The toolchain
summary: The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.
kind: index
status: stable
order: 60
---

One binary does everything: running, checking, building, testing, formatting and documenting. These pages say which
command, which flag and which file.

`torb` is `build/release/torb`, what [`sh tools/bootstrap.sh`](../ARCHITECTURE.md) writes, and every command on
these pages is run from the repository root.

## What belongs here

The commands of the toolchain, their flags and their exit codes; the project files (`project.trb`, `project.lock.trb`); and
the formatter canon as the toolchain enforces it.

What does not belong here: the language itself, which is [the reference](../language/index.md), and the compiler's own
design, which is in [internals](../internals/index.md). A page here is about what you type and what comes back.

<!-- torb:index:begin -->

## Pages

- **[The torb command](the-torb-command.md)** - Every subcommand of the toolchain, what it does today, and which of them are still planned.
- **[Verify your work](verifying-your-work.md)** - The commands that decide whether TorbScript you wrote is correct and in the layout of the formatter, in the order to run them.
- **[torb new](torb-new.md)** - torb new scaffolds a package - project.trb, a src/main.trb that prints a greeting, and a tests/main.test.trb with one passing test - refusing where the name already exists.
- **[torb check](torb-check.md)** - torb check resolves every module, import and name in a type position, types every expression, and reports one block per diagnostic - the gate every other command trusts.
- **[torb run](torb-run.md)** - torb run runs a file or a project in the bytecode VM, or builds and runs it natively with --native, passing the rest of the command line, the three streams and the exit code through.
- **[torb repl](torb-repl.md)** - torb repl reads entries from standard input, checks each against the session, runs it in the bytecode VM and keeps what it binds and declares for the next entry - a typed session and a piped file behave the same.
- **[torb build](torb-build.md)** - torb build type checks a program, lowers it to C, and hands the C to whatever compiler it finds - one file in, one native binary out, nothing to configure.
- **[torb test](torb-test.md)** - torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees.
- **[torb canon](torb-canon.md)** - torb canon is deprecated - it runs torb format now, with a warning - and the formatter canon it enforced, every rule of it, is what torb format runs first, over the syntax tree and with a safety net.
- **[torb docs source](torb-docs-source.md)** - torb docs source checks the doc comments of the code itself - a module comment on every file, a comment on every construct that needs one, six headings, links that resolve, and examples that compile.
- **[project.trb](project-trb.md)** - The manifest of a project - name, dependencies, the workspace it belongs to, and what torb build and torb test read out of it today.
- **[project.lock.trb](project-lock-trb.md)** - The locked manifest - the exact version, tree hash and registry of every package a workspace depends on, and the evaluated settings of a published package - written deterministically and read without running anything.
- **[torb add](torb-add.md)** - torb add writes a dependency into project.trb, resolves the workspace with it, writes project.lock.trb and installs what is new - or changes nothing and explains why there is no solution.
- **[torb remove](torb-remove.md)** - torb remove takes a dependency out of project.trb, resolves the workspace again, and drops from project.lock.trb every package nothing needs any more.
- **[torb update](torb-update.md)** - torb update resolves every package of the workspace, or only the named ones, to the highest version project.trb allows, writes project.lock.trb, and refuses an update that gains a capability unless --accept-capabilities says it is wanted.
- **[torb install](torb-install.md)** - torb install fetches every package project.lock.trb pins that is not in the cache yet, checks its tree hash before a file is written, and never changes the lock.
- **[torb publish](torb-publish.md)** - torb publish builds and checks the archive of a package exactly as a registry receives it, prints its tree hash and capabilities, and writes it into a registry that is a directory; publishing over the network comes later.
- **[torb doc](torb-doc.md)** - torb doc turns the public API of a package and its doc comments into a reference - a static site, or one JSON document for an editor and the registry - and runs the examples of the doc comments as doc tests.
- **[torb format](torb-format.md)** - torb format writes TorbScript sources in the one layout of the language - the rules of the formatter canon, then indentation, spaces and blank lines - and --check fails on every file that is not in it.
- **[torb lint](torb-lint.md)** - torb lint reports the rules of style the type checker leaves alone - Self, a Bool field named as a question, an unread binding, an unlabeled literal - each with its id and, where it is certain, a fix.

<!-- torb:index:end -->
