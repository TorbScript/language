---
title: Contributing
summary: How to build the toolchain from source and cut a release, and the rules, templates and commands for writing a page of this documentation.
kind: index
status: stable
order: 90
skill: omit
---

For working on TorbScript itself: [building the toolchain from source](building-from-source.md), cutting a release,
and everything about how this documentation is written - the rules, the templates, the checks. Read
[How to write here](writing.md) and [The front matter](front-matter.md) before adding a page; both are short, and
together they are the whole contract.

## What belongs here

Pages about **this documentation**: its structure, its front matter, its writing rules, its commands, its skill, and the
research behind all of it - and the two about the toolchain a contributor meets beside them: how it is built from
source, and how a release is cut.

What does not belong here: anything about the language itself, which is in `language/`, and anything about the compiler's
internals, which is in `internals/`. A page here is marked `skill: omit`, because an agent writing TorbScript has no use
for the rules of the documentation that describes it.

A page that states a rule with observable run-time behaviour has a second gate besides `torb docs check`: the
**conformance suite** in `tests/conformance/`, one small program per behaviour, built and run and compared byte for
byte with what is written down beside it. `tests/conformance/README.md` says what each program pins and how to
add one. A rule that a page states and no program pins is a rule that will drift, so a page that decides something new
about what a program *does* comes with a program there.

<!-- torb:index:begin -->

## Pages

- **[What the research decided](research.md)** - The ten rules this documentation is built on, each with the evidence and the source it comes from, and the practices that were deliberately left out.
- **[How this documentation is structured](structure.md)** - The tree, what each folder is for, the eleven kinds of page, and which source wins when two documents disagree.
- **[Three levels, plain words](levels-and-plain-words.md)** - Every page belongs to one of three levels - Start, Guide, Reference - and is written in plain words first, one idea per page, with the details folded.
- **[The front matter](front-matter.md)** - The nine fields a page may carry, what reads each of them, and the exact subset of YAML that a page is allowed to use.
- **[How to write here](writing.md)** - The writing rules for pages that both a person and a language model have to be able to trust, with the reason behind each one.
- **[Add a page](adding-a-page.md)** - The seven steps from an empty file to a page that passes the gate, with a template for every kind of page to copy.
- **[The docs commands](checks.md)** - What torb docs check, index, skill and bundle each do, which rules they decide, and which rules only a reviewer can decide.
- **[Build the toolchain from source](building-from-source.md)** - A checkout builds torb with one script - it takes a seed, the torb of an earlier commit, builds the compiler with it, builds it again with the result, and compares the two.
- **[The Agent Skills](the-skill.md)** - How torb docs skill turns this documentation into the TorbScript Agent Skills below skills/, which templates declare them, which page goes into which skill, and how the result is installed and checked.
- **[Cut a release](releasing.md)** - The monthly minor from its release candidate a week before, a patch from the release branch, the signed release on git.torb.dev and in the extension stores, what the owner sets up once, and what to do when a job fails.

<!-- torb:index:end -->
