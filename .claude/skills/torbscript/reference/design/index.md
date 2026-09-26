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
  - RELEASE.md
  - NETWORK.md
  - DNS.md
  - WEB.md
  - REPL.md
  - PANICS.md
  - FLAGS.md
  - TEXT-FORMATS.md
  - RANDOM.md
  - CLI.md
  - COMPUTE.md
  - FRAMEWORK.md
  - LANGUAGE-MODELS.md
  - JAVASCRIPT-AND-PHP.md
  - ../ROADMAP.md
---

A design record is written before a feature is finished, sometimes before a line of it is implemented. It works out the
vocabulary, the trait shapes, the open questions and the gaps against the real checker, in the depth a single
[language](../language/index.md) page cannot carry. Every one of them opens with a status line - proposed, partly
implemented, or done - so a reader knows how much of what follows exists yet.

## What belongs here

The specification of a feature that spans several packages or several `language/` pages: collections, streams,
concurrency, encoding, paths, URIs, linear algebra, entities and components, resources, the project file,
destructors, the operating system, the VM, the sandbox of receiver scripts, the REPL, networking with TLS and HTTP, the
Domain Name System, the web layer on top of the network, the public release with its website and package registry,
where a program may panic, cases with fixed values and bit flags, YAML and regular expressions, random numbers, command
lines, tensors and gradients, the application framework, and the JavaScript and PHP back ends. Each is plain Markdown without front
matter, linked here rather than copied, and stays where the feature it describes keeps changing. The roadmap
orders them into the milestones that are still ahead.

What does not belong here: the compiler's own implementation, which is in `internals/`; the
settled rules of the language, which are in [`language/`](../language/index.md) once a feature has a page there; and
the reasons behind a decision that no longer needs a whole record, which belong in
[`explanation/`](../explanation/index.md).

<!-- torb:index:begin -->

## Design documents

- **Streams**
- **Encoding**
- **Linear Algebra and Geometry**
- **Entities, Components and Scenes**
- **Concurrency and Parallelism**
- **Collections**
- **Loops as Expressions**
- **File Paths**
- **The Project File**
- **Resources**
- **Uniform Resource Identifiers**
- **Destructors, `close()` and `using`**
- **The Operating System**
- **The Bytecode VM**
- **Receiver Scripts and the Sandbox**
- **Releasing TorbScript**
- **Networking, TLS and HTTP**
- **The Domain Name System**
- **Web: Handlers, HTML and a Live UI**
- **The REPL**
- **Panics**
- **Cases with Fixed Values and Flags**
- **YAML, Regular Expressions and Markdown**
- **Random Numbers**
- **Command Lines**
- **Tensors, Gradients and the GPU**
- **The Application Framework**
- **Language Models and Agents**
- **JavaScript and PHP Back Ends**
- **Roadmap**

<!-- torb:index:end -->

