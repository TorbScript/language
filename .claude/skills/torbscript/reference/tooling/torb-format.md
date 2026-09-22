---
title: torb format
summary: torb format will take over the formatter canon from torb canon and own the layout the canon does not decide - line length, blank lines, and where a long call breaks.
kind: tooling
status: planned
order: 120
keywords:
  - torb format
  - formatter
  - layout
  - line length
source:
  - CONCEPT.md#formatter-canon
  - compiler/src/canon/command.trb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

`torb format`, written in TorbScript, is milestone 8's replacement for [`torb canon`](torb-canon.md), the tool that
stands in for it until then.

## Synopsis

```text
torb format [path]...   The formatter (planned; no command line decided yet)
```

## What it does

### What `torb canon` already covers

[The formatter canon](torb-canon.md)'s two decided rules - a call is a command wherever the grammar allows
it, and a multi-line `"""` string is indented two spaces deeper than the line it starts on - are enforced today by
`torb canon`, over the syntax tree, with a safety net that refuses any edit that would change what a program means.
`torb format` is specified to enforce the same two rules the same way; nothing about them changes on the day it
replaces `torb canon`.

### What only `torb format` will decide

Line length, blank lines, and where a long call's arguments break across several lines are not part of the canon
`torb canon --check` enforces today - a file it accepts can still be laid out differently once `torb format` exists.
A multi-line string's dedent is specified against this gap directly: stripping the first line's indentation from
every line "lets a code block sit at the indentation of the call around it instead of at the left margin", and
`torb format` is what will keep that call itself at a readable width.

### Why it waits

`torb canon`'s two rules moved parentheses and indentation only, which the safety net can verify by parsing the
result again and comparing syntax trees. Layout is a bigger surface with no such cheap proof, so it belongs to
`torb format` rather than to `canon`.

## Examples

None: there is no command line to run yet. What is checked in its place until `torb format` exists:

```console
$ torb canon --check .
0 of 282 files would change: 0 calls became commands, 0 got parentheses, 0 strings were indented, 0 case patterns
```

## Related

- [torb canon](torb-canon.md) - the command that enforces the same two rules today, and every rule of the canon in
  one place.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the dedent whose layout `torb format` will own.
- [The torb command](the-torb-command.md) - every subcommand, and which are still planned.

