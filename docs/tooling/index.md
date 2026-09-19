---
title: The toolchain
summary: The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.
kind: index
status: stable
order: 60
---

One binary does everything: running, checking, building, testing, formatting and documenting. These pages say which
command, which flag and which file.

Until the compiler compiles itself, `torb` is the stage 0 interpreter in `bootstrap/`, and every command is run as
`cargo run --release -q -- run ../compiler <command>` from there. The pages name both forms.

## What belongs here

The commands of the toolchain, their flags and their exit codes; the project files (`project.trb`, `project.lock.trb`); and
the formatter canon as the toolchain enforces it.

What does not belong here: the language itself, which is [the reference](../language/index.md), and the compiler's own
design, which is in [internals](../internals/index.md). A page here is about what you type and what comes back.

<!-- torb:index:begin -->

## Pages

- **[The torb command](the-torb-command.md)** - Every subcommand of the toolchain, what it does today, and which of them are still planned.
- **[Verify your work](verifying-your-work.md)** - The commands that decide whether TorbScript you wrote is correct and in the formatter canon, in the order to run them.

<!-- torb:index:end -->
