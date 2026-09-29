---
title: Learn TorbScript
summary: For people who already program - a fifteen-minute tour, then one short page per idea, all of it done in an evening.
kind: index
status: stable
order: 10
---

Start with [the tour](tour.md): fifteen minutes, and every example runs in the page. Then
[install TorbScript](installing-and-running.md) and go through the pages below in order. Each one says what you will
be able to do, shows one example, and names the next page. Together they take an evening.

If you know Rust, TypeScript, Python, Go, Kotlin or Swift, [Coming from another language](coming-from/index.md) maps
what you know onto TorbScript in one table.

## Who this is for

For people who can already program in another language. If you have never programmed, the
course starts from zero. If you want the exact rule behind something, the
language reference (skill `torbscript-language`: `references/language/index.md`) has one page per construct.

<!-- torb:index:begin -->

## Sections

- **[Coming from another language](coming-from/index.md)** - One page per language - a table of the 10 to 15 things that map directly, the 5 that will surprise you, and what is deliberately missing.

## Pages

- **[A tour of TorbScript](tour.md)** - The whole language in fifteen minutes for somebody who already programs - bindings, calls, functions, types, cases, errors, traits and pipelines, one short example each.
- **[Install and run](installing-and-running.md)** - Install TorbScript with one command, run a file, and make a project with a test - the five commands you will use every day.
- **[Values and bindings](values-and-bindings.md)** - A binding is const or var, a second name is always a copy, and a change goes through the path where the value lives.
- **[Functions and closures](functions-and-closures.md)** - Declare a function with typed parameters, defaults and labels, write a closure, and pass it as the last argument of a call.
- **[Types and methods](types-and-methods.md)** - Declare a type with fields, give it methods, and tell a method that changes the value from one that returns a changed copy.
- **[Cases and matching](cases-and-matching.md)** - Declare a type whose value is one of several cases, and take it apart with a match that has to handle every case.
- **[Traits](traits.md)** - Declare what a type can do as a trait, give it to a type now or later, and use the trait as a type that holds any of them.
- **[Errors](errors.md)** - A function that can fail returns a Result, a value that can be missing is an Option, and the caller handles both with match, the question mark or a fallback.
- **[Collections and pipelines](collections-and-pipelines.md)** - Build a list, a map and a set, change one in place or get a changed copy, and run values through a pipeline of steps.
- **[Control flow and your own constructs](control-flow-and-dsls.md)** - Ifs and loops work as you expect, and a new control structure or a configuration block is an ordinary function you can write yourself.
- **[Modules and packages](modules-and-packages.md)** - Split a program into files with public and use, import from the standard library, and lay out a project so that its tests reach its code.
- **[Put it together](a-small-program.md)** - One small program - a type with cases, a type with fields, a function that can fail and a pipeline - that uses what the guide taught.
- **[Idiomatic TorbScript](idiomatic-torbscript.md)** - The habits that make TorbScript read like TorbScript - names, mutation, calls, types, errors, closures, resources and tasks - each as one rule, one runnable example and the reason behind it.

<!-- torb:index:end -->

