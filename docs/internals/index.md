---
title: How the toolchain is built
summary: "The compiler design documents, indexed where they live: the architecture, the type checker and the back end."
kind: index
status: stable
order: 80
skill: omit
documents:
  - ../ARCHITECTURE.md
  - ../TYPECHECKER.md
  - ../BACKEND.md
  - ../STREAMS.md
  - ../ENCODING.md
  - ../LINEAR.md
  - ../ECS.md

  - ../PATH.md
---

The toolchain is written in TorbScript and lives in `compiler/`. Until it compiles itself it runs on stage 0, an untyped
tree-walking interpreter written in Rust in `bootstrap/`. These are the design documents of that work, in plain Markdown
without front matter, linked here where they are instead of being copied.

## What belongs here

Links to the compiler's own design documents, and nothing else. These documents are written for somebody changing the
compiler, so they are long, they talk about milestones and gaps, and they change with the implementation rather than with
the language.

What does not belong here: anything a user of the language needs. That is in [the language reference](../language/index.md)
and in [the toolchain pages](../tooling/index.md). This section is marked `skill: omit`, because an agent writing
TorbScript has no use for the internals of the compiler that compiles it.

## What holds the two implementations to one behaviour

The language must be interpretable **and** compilable, and that is a design goal rather than a fact about a
code base - so there is a test that holds both implementations to it. The **conformance suite** in
`bootstrap/tests/native/` is one small program per behaviour, run by the interpreter and as a compiled binary, with the
standard output, the standard error and the exit code compared byte for byte. `bootstrap/tests/native/README.md` states
that contract, says how a program is added, and names what each program pins; nothing about what a program *does* is
exempt from it. A rule of the language reference that has observable run-time behaviour and no program in that suite is
a rule nothing holds either side to.

<!-- torb:index:begin -->

## Design documents

- **[TorbScript Implementation Architecture](../ARCHITECTURE.md)**
- **[The Type Checker (Milestone 4)](../TYPECHECKER.md)**
- **[The Back Ends (Milestones 5-7)](../BACKEND.md)**
- **[Streams](../STREAMS.md)**
- **[Encoding](../ENCODING.md)**
- **[Linear Algebra and Geometry](../LINEAR.md)**
- **[Entities, Components and Scenes](../ECS.md)**
- **[File Paths](../PATH.md)**

<!-- torb:index:end -->
