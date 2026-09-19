---
title: Learn TorbScript
summary: The learning path from nothing to a working program, in order, one step per page.
kind: index
status: stable
order: 10
---

One path, in order, from no TorbScript at all to a program you wrote yourself. Each step has a `## Goal` that says what
works at the end of it, and ends with the next step. If you already know the language and want a rule, go to
[the language reference](../language/index.md) instead.

The twelve files of [`examples/tour`](../../examples/tour) are the same material as running code; this path links into
them where they help.

## What belongs here

A single learning path with no branches and no alternatives. A step assumes every step before it and nothing else, tells
the reader what to type, and shows what comes out. Responsibility for the outcome is the page's, not the reader's.

What does not belong here: the exact rules of a construct, which are in [`language/`](../language/index.md); one task in
isolation, which is a [how-to](../how-to/index.md); and the argument for a design decision, which is an
[explanation](../explanation/index.md). A step may state a rule in one sentence and link the page that has it in full.

<!-- torb:index:begin -->

## Pages

- **[The language in sixty seconds](the-language-in-sixty-seconds.md)** - The mental model of TorbScript in one screen: values, bindings, no null, no exceptions, traits, and calls written as commands.
- **[Run your first program](installing-and-running.md)** - Build the toolchain, run a single file, and create a project with a manifest, a source file and a test.
- **[Values and bindings](values-and-bindings.md)** - Why const and var are the whole mutation story, what a copy costs, and the one trap that catches everybody coming from a language with references.

<!-- torb:index:end -->
