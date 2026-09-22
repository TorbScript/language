---
title: The language reference
summary: One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites.
kind: index
status: stable
order: 20
---

The reference. One concept per page, each page understandable on its own: a minimal example first, then the syntax, then
numbered rules, then what the construct is **not**. The grouping follows the language rather than the reader, because
that is the only order somebody looking a construct up can predict.

If you are new, read [the learning path](../guide/index.md) instead. If you want to know *why* a rule is the way it is,
the argument is in [explanation](../explanation/index.md).

## What belongs here

The exact behaviour of the language: every construct, every form it takes, every rule that decides whether a program
compiles, and every diagnostic it can produce. Neutral and complete; a page here tells you what is true, not what to do.

What does not belong here: a learning order, which is [`guide/`](../guide/index.md); a task, which is
[`how-to/`](../how-to/index.md); the argument for a decision, which is [`explanation/`](../explanation/index.md); and the
contents of a package, which is [`standard-library/`](../standard-library/index.md). A rule may carry one sentence of
reason and a link to the explanation that carries the rest.

Where the compiler and [`CONCEPT.md`](../../CONCEPT.md) disagree, the compiler is right and the page says so.

<!-- torb:index:begin -->

## Sections

- **[Syntax](syntax/index.md)** - How TorbScript is written: where a statement ends, how a call is spelled, and what a literal looks like.
- **[Values and types](values-and-types/index.md)** - Bindings, the built-in types, and the type forms that are about values rather than about behaviour.
- **[Types](types/index.md)** - Declaring a type, its fields, its methods, what is generated for it, and how one is changed.
- **[Functions](functions/index.md)** - Declaring a function, its arguments and defaults, variadic parameters, closures, trailing closures, parameter modes and quoted expressions.
- **[Traits](traits/index.md)** - How a capability is declared, how a type comes with one, and how a trait is used as a type.
- **[Cases and pattern matching](pattern-matching/index.md)** - How a type with cases is declared, and every place a pattern can stand.
- **[Generics](generics/index.md)** - Type parameters, where they are declared, how a bound restricts them, what is inferred, and how a trait-typed value satisfies one at runtime.
- **[Errors](errors/index.md)** - How a function says it can fail, how a caller handles it, and what a panic is for.
- **[Collections and iteration](collections-and-iteration/index.md)** - List, Map, Set, Stack and Queue as traits over a shared Iterate, plus slices, pipelines and collectors.
- **[Concurrency and streams](concurrency-and-streams/index.md)** - Task, Channel, Source and Sink - asynchrony in the type system instead of a keyword - designed and type-checked today, but not yet run by any back end.
- **[Modules and packages](modules-and-packages/index.md)** - How a file brings in names from elsewhere, what a package is, and the two rules - visibility and top-level code - that decide what a module may contain.
- **[Reflection](reflection/index.md)** - Why there is no runtime reflection, the four syntactic bridges that connect a type to a value instead, and the generated Encode and Decode pair that covers serialization.
- **[Configuration](configuration/index.md)** - Receiver closures, the builder function around one, and the receiver script and sandbox that let a whole file play the same role - statically typed configuration without a second language.
- **[Execution](execution/index.md)** - The parts of running a program that are a rule of the language rather than an implementation detail - evaluation order, what a copy costs, the tail-call guarantee, and when a destructor runs.
- **[Extensibility](extensibility/index.md)** - The language is extended by writing functions, not macros or annotations - control structures, DSLs and query providers are all ordinary functions, closures and Expression<Value> parameters.

<!-- torb:index:end -->
