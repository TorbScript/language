---
title: The standard library
summary: One page per package of std, what each contains, and which of them are in scope everywhere without an import.
kind: index
status: stable
order: 30
---

The standard library is a set of packages owned by `std`: `std/core`, `std/text`, `std/collections` and the rest. They
come with the toolchain and have its version, so none of them needs an entry in `dependencies`.

The **prelude** (`std/prelude`) is a package of re-exports whose public names are in scope in every file. It holds the
pure part of the library - values, text, numbers, collections, pipelines, encoding, quotations, tasks, printing,
mathematics, JSON and the time values. What a program can *touch* is deliberately not in it: `std/fs`, `std/io`,
`std/process`, `std/environment`, `std/http`, `std/sandbox` and `Clock` stay explicit imports, so that
`use File from "std/fs"` at the top of a file is the statement "this file touches files".

## What belongs here

One page per package: what it is for, how it is imported, and every public declaration with its doc comment. The
`## Declarations` section of each page stands between generator markers, so that milestone 8's `torb doc` can fill it from
the sources.

What does not belong here: the language rules that a type participates in, which are in
[the language reference](../language/index.md), and the argument for a design decision, which is in
[explanation](../explanation/index.md). A page here says what a package contains.

<!-- torb:index:begin -->

## Pages

- **[std/core](core.md)** - The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.

<!-- torb:index:end -->
