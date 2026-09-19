---
title: Why the language is like this
summary: The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.
kind: index
status: stable
order: 50
---

Why TorbScript is the way it is. Each page takes one decision, gives the argument, names what was rejected, and says what
somebody writing TorbScript has to do differently because of it. This is the one part of the documentation where
alternatives, counter-examples and history belong.

The **contrast** pages are the most valuable ones here, and not only for people. A model that has read a great deal of
Rust, Swift, Kotlin and TypeScript will write TorbScript that looks like those languages and does not compile. Each
contrast page names the habits that break and what replaces them.

## What belongs here

An argument per page: the decision, why it was made, what was considered instead, and the consequence for code. Titles are
noun phrases (`Why values instead of references`), except the contrast pages, which are titled `Coming from <language>`.

What does not belong here: the syntax or the rules of a construct, which are in
[the language reference](../language/index.md), and a task, which is a [how-to](../how-to/index.md). A page here links the
reference instead of restating it.

<!-- torb:index:begin -->

## Pages

- **[Why values instead of references](why-values-instead-of-references.md)** - Every type is a value and the binding decides about mutation, which removes every mutable-and-immutable type pair at the price of one local, lintable trap.
- **[What a model trained on other languages gets wrong](mistakes-models-make.md)** - The mistakes a language model makes in TorbScript because it has read Rust, Swift, Kotlin and TypeScript, each with the wrong line, the right line and the diagnostic.
- **[Coming from Rust](coming-from-rust.md)** - What carries over from Rust, what looks the same and is not, and what Rust has that TorbScript deliberately does not.

<!-- torb:index:end -->
