---
title: Design records
summary: The specification documents behind a language or library feature that is still being built, each opening with a status line that says how much of it exists today.
kind: index
status: stable
order: 70
documents:
  - STREAMS.md
  - ENCODING.md
  - LINEAR.md
  - ECS.md
  - CONCURRENCY.md
  - COLLECTIONS.md
  - LOOPS.md
  - PATH.md
  - PROJECT.md
  - RESOURCES.md
  - URI.md
  - DESTRUCTORS.md
  - OS.md
  - VM.md
  - SCRIPTS.md
---

A design record is written before a feature is finished, sometimes before a line of it is implemented. It works out the
vocabulary, the trait shapes, the open questions and the gaps against the real checker, in the depth a single
[language](../language/index.md) page cannot carry. Every one of them opens with a status line - proposed, partly
implemented, or done - so a reader knows how much of what follows exists yet.

## What belongs here

The specification of a feature that spans several packages or several `language/` pages: collections, streams,
concurrency, encoding, paths, URIs, linear algebra, entities and components, resources, the project file,
destructors, the operating system, the VM and the sandbox of receiver scripts. Each is plain Markdown without front
matter, linked here rather than copied, and stays where the feature it describes keeps changing.

What does not belong here: the compiler's own implementation, which is in [`internals/`](../internals/index.md); the
settled rules of the language, which are in [`language/`](../language/index.md) once a feature has a page there; and
the reasons behind a decision that no longer needs a whole record, which belong in
[`explanation/`](../explanation/index.md).

<!-- torb:index:begin -->

## Design documents

- **[Streams](STREAMS.md)**
- **[Encoding](ENCODING.md)**
- **[Linear Algebra and Geometry](LINEAR.md)**
- **[Entities, Components and Scenes](ECS.md)**
- **[Concurrency and Parallelism](CONCURRENCY.md)**
- **[Collections](COLLECTIONS.md)**
- **[Loops as Expressions](LOOPS.md)**
- **[File Paths](PATH.md)**
- **[The Project File](PROJECT.md)**
- **[Resources](RESOURCES.md)**
- **[Uniform Resource Identifiers](URI.md)**
- **[Destructors, `close()` and `using`](DESTRUCTORS.md)**
- **[The Operating System](OS.md)**
- **[The Bytecode VM](VM.md)**
- **[Receiver Scripts and the Sandbox](SCRIPTS.md)**

<!-- torb:index:end -->
