---
title: Extensibility
summary: The language is extended by writing functions, not macros or annotations - control structures, DSLs and query providers are all ordinary functions, closures and Expression<Value> parameters.
kind: index
status: stable
order: 120
---

Everything that reads like new syntax - `unless`, a configuration block, a query written against a database - is a
function. This section is where that idea is spelled out, together with `foreign`, the one way to reach outside the
language entirely.

## What belongs here

What does not belong here: the mechanics of `Expression<Value>` itself, which are in `language/functions/`; this
folder is about what gets built on top of it. Every page in this folder is a reference page: an example first, then
the syntax, then numbered rules, then what the construct is not.
[Foreign functions](foreign-functions.md) is `status: planned` until a back end can link one.

<!-- torb:index:begin -->

## Pages

- **[Control structures are functions](control-structures.md)** - do, unless, retry and test are ordinary functions with a closure or lazy parameter, so writing your own control structure is nothing more than writing a function that takes one and calling it with a trailing closure.
- **[Reading code instead of running it](expression-trees.md)** - A query provider reads the typed tree of an Expression<Value> instead of running it, translates what it recognizes, and fails at its own runtime for a call it does not - the language cannot know in advance what a library can translate.
- **[Foreign functions](foreign-functions.md)** _(planned)_ - foreign declares functions of a C library with the ABI as the contract, available to any package unlike native, but nothing links or calls one yet and its Pointer and CString types are not declared in std/ either.

<!-- torb:index:end -->
