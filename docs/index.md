---
title: TorbScript
summary: The documentation of TorbScript, a functional-first language with value semantics that runs interpreted and compiles to native binaries.
kind: index
status: stable
---

TorbScript is a functional-first, multi-paradigm scripting language with value semantics. Every type is a value and the
binding decides whether it can be changed; there is no `null` and there are no exceptions; the same program runs
interpreted and compiles to a native executable. Its command is `torb` and its files end in `.trb`.

Start with [The language in sixty seconds](guide/the-language-in-sixty-seconds.md) if you have never seen it, or with
[the language reference](language/index.md) if you are looking a construct up.

Every page carries a `summary` in its front matter, so this index and the index of every folder are enough to find the
one page that answers a question. Every page is written to be understandable on its own.

<!-- torb:index:begin -->

## Sections

- **[Learn TorbScript](guide/index.md)** - The learning path from nothing to a working program, in order, one step per page.
- **[The language reference](language/index.md)** - One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites.
- **[The standard library](standard-library/index.md)** - One page per package of std, what each contains, and which of them are in scope everywhere without an import.
- **[Task recipes](how-to/index.md)** - One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works.
- **[Why the language is like this](explanation/index.md)** - The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.
- **[The toolchain](tooling/index.md)** - The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.
- **[Design records](design/index.md)** - The specification documents behind a language or library feature that is still being built, each opening with a status line that says how much of it exists today.
- **[How the toolchain is built](internals/index.md)** - The compiler design documents, indexed where they live: the architecture, the type checker and the back end.
- **[Writing the documentation](contributing/index.md)** - The rules, templates and commands for writing a page here, and the research they come from.

## Pages

- **[Glossary](glossary.md)** - Every term this documentation uses, one entry each, at most two sentences. The entry decides which word is correct.

<!-- torb:index:end -->
