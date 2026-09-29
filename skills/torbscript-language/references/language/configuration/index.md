---
title: Configuration
summary: Receiver closures, the builder function around one, and the receiver script and sandbox that let a whole file play the same role - statically typed configuration without a second language.
kind: index
status: stable
order: 100
---

A receiver closure, a builder, a receiver script and a sandbox are one mechanism at four sizes: a block, a function,
a file, and the capabilities a file is allowed to reach.

## What belongs here

What does not belong here: property commands, which are a rule of the type system and live in `language/types/`.
Every page in this folder is a reference page: an example first, then the syntax, then numbered rules, then what the
construct is not. [Receiver scripts](receiver-scripts.md) and [the sandbox](the-sandbox.md) are `status: planned`
until the runtime has them.

<!-- torb:index:begin -->

## Pages

- **[Receiver closures](receiver-closures.md)** - A receiver closure is a closure whose first parameter is called self, so names inside it resolve against that receiver first, exactly as inside a method.
- **[Builders and DSLs](builders.md)** - A builder is a function that creates a value, hands it to a receiver closure to configure, and returns it, which is what makes a configuration block a statically typed value instead of a string to parse.
- **[Receiver scripts](receiver-scripts.md)** _(draft)_ - A .trb file can be loaded as the body of a receiver closure, type checked against a receiver type before it runs, and run by the sandboxed VM.
- **[The sandbox](the-sandbox.md)** _(draft)_ - A script has no IO, no network, no clock, no environment and no foreign functions by default, and only the caller of Sandbox.load can grant more, in a block that names exactly what is granted.

<!-- torb:index:end -->

