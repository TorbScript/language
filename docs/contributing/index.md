---
title: Writing the documentation
summary: The rules, templates and commands for writing a page here, and the research they come from.
kind: index
status: stable
order: 90
skill: omit
---

Everything about how this documentation is written: the rules, the templates, the checks, and the plan of what is still
missing. Read [How to write here](writing.md) and [The front matter](front-matter.md) before adding a page; both are
short, and together they are the whole contract.

## What belongs here

Pages about **this documentation**: its structure, its front matter, its writing rules, its commands, its skill, and the
research behind all of it.

What does not belong here: anything about the language itself, which is in `language/`, and anything about the compiler's
internals, which is in `internals/`. A page here is marked `skill: omit`, because an agent writing TorbScript has no use
for the rules of the documentation that describes it.

A page that states a rule with observable run-time behaviour has a second gate besides `torb docs check`: the
**conformance suite** in `bootstrap/tests/native/`, one small program per behaviour, run by the interpreter and as a
compiled binary and compared byte for byte. `bootstrap/tests/native/README.md` says what each program pins and how to
add one. A rule that a page states and no program pins is a rule that will drift, so a page that decides something new
about what a program *does* comes with a program there.

<!-- torb:index:begin -->

## Pages

- **[What the research decided](research.md)** - The ten rules this documentation is built on, each with the evidence and the source it comes from, and the practices that were deliberately left out.
- **[How this documentation is structured](structure.md)** - The tree, what each folder is for, the nine kinds of page, and which source wins when two documents disagree.
- **[The front matter](front-matter.md)** - The nine fields a page may carry, what reads each of them, and the exact subset of YAML that a page is allowed to use.
- **[How to write here](writing.md)** - The writing rules for pages that both a person and a language model have to be able to trust, with the reason behind each one.
- **[Add a page](adding-a-page.md)** - The seven steps from an empty file to a page that passes the gate, with a template for every kind of page to copy.
- **[The docs commands](checks.md)** - What torb docs check, index, skill and bundle each do, which rules they decide, and which rules only a reviewer can decide.
- **[The Agent Skill](the-skill.md)** - How torb docs skill turns this documentation into an Agent Skill, what it copies, what it leaves out, and how to install the result.
- **[The page inventory](inventory.md)** - Every page the complete documentation needs, with its path, kind, scope and sources, grouped so that independent writers can each take one package.

<!-- torb:index:end -->
