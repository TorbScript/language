---
name: torbscript-language
description: "Looks up the exact rules of every TorbScript construct - types, traits, generics, patterns, closures, errors, collections, modules, configuration blocks - with the reasons behind them and recipes. Use it with the torbscript skill when TorbScript code goes beyond the basics or a `torb check` diagnostic needs the rule behind it."
license: MIT
compatibility: "Reference only. Verifying code needs the torb command, which the torbscript skill checks and installs."
---

<!-- carry: language/ explanation/ glossary.md -->
<!-- carry: how-to/collect-a-pipeline.md how-to/convert-between-types.md how-to/define-an-error-type.md -->
<!-- carry: how-to/parse-text-into-a-type.md how-to/sort-by-more-than-one-key.md how-to/use-a-type-as-a-map-key.md -->
<!-- carry: how-to/write-a-builder.md how-to/write-a-configuration-file.md -->

# The TorbScript language

One page per construct: a minimal example, the syntax, numbered rules, and what the construct is not. The `torbscript`
skill has the mental model, the cheat sheet of every form and the mistakes that look right; this skill has the rule
behind each of them. Paths are relative to the directory of this file.

## How to find a rule

1. Pick the section below, open its index, then the one page that answers the question. Every page is self-contained.
2. Or search: `grep -il "<word>" -r references/` names the pages, and `grep -i "<word>" references/index.md` finds a
   page by its title and summary.
3. A diagnostic of `torb check` names the construct; the page of that construct has the rule and, under its rules, the
   mistakes it invites.

A page marked `status: draft` may still be wrong, so verify it against the compiler. A page marked `status: planned`
describes a designed feature that does not compile yet, and its first line says so.

## Sections

<!-- sections -->

The recipes - collect a pipeline, convert between types, define an error type, parse text into a type, sort by more
than one key, use a type as a map key, write a builder, write a configuration file - are in `references/how-to/`, one
page each with the steps, the pitfalls and a complete program.
