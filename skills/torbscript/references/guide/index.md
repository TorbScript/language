---
title: Learn TorbScript
summary: A guide for a programmer who already knows another language - a 15-minute tour, then one idea per page, finishable in an evening.
kind: index
status: stable
order: 10
---

If you already write code for a living, start with
[TorbScript in 15 minutes](torbscript-in-15-minutes.md) or your language in
[Coming from another language](coming-from/index.md) - either gets you writing real programs today. The numbered pages
below go the same ground again, slower and one idea per page, for when fifteen minutes was not enough on some point.
Each has a `## Goal` that says what works at the end of it and ends with the next page. If you want the exact rule
behind something instead of a walkthrough, go straight to the language reference (skill `torbscript-language`: `references/language/index.md`).

The twelve files of `examples/tour` are the same material as running code; the path below links
into them where they help.

## What belongs here

Learning material for somebody who has never written TorbScript: a fast tour, a table per language people arrive from,
and a slower path of one page per idea, in order. A page here assumes programming experience, not TorbScript
experience, and shows what to type and what comes out. Responsibility for the outcome is the page's, not the reader's.

What does not belong here: the exact rules of a construct, which are in `language/` (skill `torbscript-language`: `references/language/index.md`); one task in
isolation, which is a [how-to](../how-to/index.md); and the argument for a design decision, which is an
explanation (skill `torbscript-language`: `references/explanation/index.md`). A page may state a rule in one sentence and link the page that has it in full.

<!-- torb:index:begin -->

## Sections

- **[Coming from another language](coming-from/index.md)** - One page per language - a table of the 10 to 15 things that map directly, the 5 that will surprise you, and what is deliberately missing.

## Pages

- **[The language in sixty seconds](the-language-in-sixty-seconds.md)** - The mental model of TorbScript in one screen: values, bindings, no null, no exceptions, traits, and calls written as commands.
- **[TorbScript in 15 minutes](torbscript-in-15-minutes.md)** - The fastest honest tour of TorbScript for a working programmer, one short example and a few sentences per idea.
- **[Run your first program](installing-and-running.md)** - Install the toolchain, run a single file, and create a project with a manifest, a source file and a test.
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

