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
- **[Functions and closures](functions-and-closures.md)** - How to declare a function, when it must spell out its return type, and the one closure form the language has.
- **[Types and methods](types-and-methods.md)** - How to declare a type, add methods to it, and tell a verb that changes it from the participle that answers a copy.
- **[Cases and matching](cases-and-matching.md)** - How to declare a type with more than one shape, and take it apart with a match that has to cover every case.
- **[Traits](traits.md)** - How to declare a capability, give it to a type, and use the trait itself as a type that hides which concrete type it is.
- **[Errors](errors.md)** - How a function says it can fail with Result, and how a caller handles that with match or the question mark operator.
- **[Collections and pipelines](collections-and-pipelines.md)** - How to build a list, map and set, change one in place or get a changed copy, and pull values through a lazy pipeline.
- **[Control flow and your own constructs](control-flow-and-dsls.md)** - if, for, while and loop as you would expect, and why unless is an ordinary function you could have written yourself.
- **[Modules and packages](modules-and-packages.md)** - How use brings a name in from another file or the standard library, and what public means for a top-level declaration.
- **[Tests and the toolchain](tests-and-tooling.md)** - How to write a test with test, group and assert, and the two commands that check whether what you wrote is correct.
- **[Put it together](a-small-program.md)** - One small program - a type with cases, a function that can fail, and a pipeline - that uses everything this path taught.
- **[Idiomatic TorbScript](idiomatic-torbscript.md)** - The habits that make TorbScript read like TorbScript - names, mutation, calls, types, errors, closures, resources and tasks - each as one rule, one runnable example and the reason behind it.

<!-- torb:index:end -->
