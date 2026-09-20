---
title: torb lint
summary: torb lint will check the naming and style rules the type checker does not - full words instead of abbreviations, is/has for a computed question, an adjective for a Bool field - none of which is enforced today.
kind: tooling
status: planned
order: 130
keywords:
  - torb lint
  - naming conventions
  - style
  - lint --fix
source:
  - CONCEPT.md#toolchain
  - compiler/CONTRIBUTING.md
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

A program with an abbreviated name or a `Bool` field named as a question type checks exactly as well as one that
follows [the naming conventions](../language/syntax/naming-conventions.md) - nothing today tells the two apart.

## Synopsis

```text
torb lint [path]...   The naming and style rules the compiler does not care about (planned; no command line decided yet)
```

## What it does

### What it is specified to check

`torb lint` is named next to `torb format` as the tool for what the type checker deliberately leaves alone: naming
conventions rather than rules the checker enforces. The compiler's own style guide is the clearest example of the
kind of thing it is for, because the codebase already names one lint before the tool exists: a literal `true`,
`false` or `None` passed to a parameter that is *declared* as `Bool` or as an optional should be labeled
(`hasCapacity: false`, not a bare `false`) everywhere except the two cases a reader can tell without the label - and
telling those two cases apart today is a person's judgement, with the fix left for a future lint.

### What exists today

Nothing checks this. `torb check` resolves and types every one of these calls without objecting to an unlabeled
`false` or an abbreviated name, because neither is a type error - see
[Naming conventions](../language/syntax/naming-conventions.md) for which of them the parser and the checker do
enforce today, and which are a convention only a person or, eventually, `torb lint` follows.

```trb check
fn listEntries(entries: Int, kind: String, hasCapacity: Bool = true): Int {
  entries
}

print listEntries(3, "ArrayList", false)
```

The call above type checks exactly as it would with the label written (`hasCapacity: false`); nothing today prefers
one over the other.

## Examples

None: there is no command line to run yet.

## Related

- [Naming conventions](../language/syntax/naming-conventions.md) - which rules are conventions and which the parser
  and the checker enforce.
- [torb format](torb-format.md) - the other stage-8 tool, for layout instead of naming.
- [The torb command](the-torb-command.md) - every subcommand, and which are still planned.
