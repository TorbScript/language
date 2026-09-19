---
title: Task recipes
summary: "One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works."
kind: index
status: stable
order: 40
---

Recipes for one task each. A page here assumes you can read TorbScript and tells you what to write; it forks where your
situation forks, and it ends with one complete program that type checks.

If you do not know the language yet, [the learning path](../guide/index.md) is the place to start.

## What belongs here

One task per page, titled with the verb (`Read a file`, `Add a dependency`). The reader is competent and in a hurry: the
steps come first, the full program comes at the end, and the pitfalls are named rather than explained.

What does not belong here: a learning order, which is [`guide/`](../guide/index.md); the complete rules of a construct,
which are in [`language/`](../language/index.md); and an argument, which is in
[`explanation/`](../explanation/index.md).

<!-- torb:index:begin -->

## Pages

- **[Read a file](read-a-file.md)** - Read a whole file or its lines, hand the failure to the caller with the question mark operator, and turn an IoError into your own error type.
- **[Write a configuration file](write-a-configuration-file.md)** - Declare a type for the configuration, write the file as TorbScript against it, and load it through the sandbox with the capabilities you grant.

<!-- torb:index:end -->
