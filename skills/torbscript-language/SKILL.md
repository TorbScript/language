---
name: torbscript-language
description: "Looks up the exact rules of every TorbScript construct - types, traits, generics, patterns, closures, errors, collections, modules, configuration blocks - with the reasons behind them and recipes. Use it with the torbscript skill when TorbScript code goes beyond the basics or a `torb check` diagnostic needs the rule behind it."
license: MIT
compatibility: "Reference only. Verifying code needs the torb command, which the torbscript skill checks and installs."
---

# The TorbScript language

One page per construct: a minimal example, the syntax, numbered rules, and what the construct is not. The `torbscript`
skill has the mental model, the cheat sheet of every form and the mistakes that look right; this skill has the rule
behind each of them. Paths are relative to the directory of this file.

## How to find a rule

1. Pick the section below, open its index, then the one page that answers the question. Every page is self-contained.
2. Or search: `grep -il "<word>" -r references/` names the pages, and `grep -i "<word>" references/index.md` finds a
   page by its title and summary.
3. A diagnostic of `torb check` names the construct; the page of that construct has the rule and, under its rules, the
   mistakes it invites.

A page marked `status: draft` may still be wrong, so verify it against the compiler. A page marked `status: planned`
describes a designed feature that does not compile yet, and its first line says so.

## Sections

- [The language reference](references/language/index.md) - One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites.
- [Syntax](references/language/syntax/index.md) - How TorbScript is written: where a statement ends, how a call is spelled, and what a literal looks like.
- [Values and types](references/language/values-and-types/index.md) - Bindings, the built-in types, and the type forms that are about values rather than about behaviour.
- [Types](references/language/types/index.md) - Declaring a type, its fields, its methods, what is generated for it, and how one is changed.
- [Functions](references/language/functions/index.md) - Declaring a function, its arguments and defaults, variadic parameters, closures, trailing closures, parameter modes and quoted expressions.
- [Traits](references/language/traits/index.md) - How a capability is declared, how a type comes with one, and how a trait is used as a type.
- [Cases and pattern matching](references/language/pattern-matching/index.md) - How a type with cases is declared, and every place a pattern can stand.
- [Generics](references/language/generics/index.md) - Type parameters, where they are declared, how a bound restricts them, what is inferred, and how a trait-typed value satisfies one at runtime.
- [Errors](references/language/errors/index.md) - How a function says it can fail, how a caller handles it, and what a panic is for.
- [Collections and iteration](references/language/collections-and-iteration/index.md) - List, Map, Set, Stack and Queue as traits over a shared Iterate, plus slices, pipelines and collectors.
- [Modules and packages](references/language/modules-and-packages/index.md) - How a file brings in names from elsewhere, what a package is, and the two rules - visibility and top-level code - that decide what a module may contain.
- [Reflection](references/language/reflection/index.md) - Why there is no runtime reflection, the four syntactic bridges that connect a type to a value instead, and the generated Encode and Decode pair that covers serialization.
- [Configuration](references/language/configuration/index.md) - Receiver closures, the builder function around one, and the receiver script and sandbox that let a whole file play the same role - statically typed configuration without a second language.
- [Execution](references/language/execution/index.md) - The parts of running a program that are a rule of the language rather than an implementation detail - evaluation order, copies, tail calls, destructors, and which arm of a branch on a compile-time constant is compiled.
- [Extensibility](references/language/extensibility/index.md) - The language is extended by writing functions, not macros or annotations - control structures, DSLs and query providers are all ordinary functions, closures and Expression<Value> parameters.
- [Why the language is like this](references/explanation/index.md) - The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.

The recipes - collect a pipeline, convert between types, define an error type, parse text into a type, sort by more
than one key, use a type as a map key, write a builder, write a configuration file - are in `references/how-to/`, one
page each with the steps, the pitfalls and a complete program.
