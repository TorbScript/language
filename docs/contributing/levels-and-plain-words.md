---
title: Three levels, plain words
summary: Every page belongs to one of three levels - Start, Guide, Reference - and is written in plain words first, one idea per page, with the details folded.
kind: explanation
status: stable
order: 25
skill: omit
keywords:
  - levels
  - start
  - lesson
  - plain words
  - translation
source:
  - docs/tooling/torb-docs-site.md#three-levels
---

A reader comes in at one of three levels, and each page is written for the reader of its level. Six rules hold on every
page; a lesson of Start has a fixed shape on top of them.

## The decision

**Three levels, one door each.** Start (`start/`) is a course for somebody who has never programmed. The Guide
(`guide/`) is for somebody who programs in another language. The Reference is everything else: the language, the
standard library, the tools, the reasons and the records. The header of the site has one link per level, and a page
shows which level it belongs to ([torb docs site](../tooling/torb-docs-site.md#three-levels)).

**Six rules for every page:**

1. **Plain words first.** Say what a thing does for the reader before what it is called: "giving a list a second
   name makes a copy", then "value semantics".
2. **Jargon only after it is shown,** and linked to its entry in the [glossary](../glossary.md) where it first appears.
   A glossary entry opens with one plain sentence.
3. **A one-sentence summary at the top.** The `summary` of the front matter stands under the title; it says what the
   reader will know, in words the reader already has.
4. **One idea per page.** A second idea is a second page, or a link.
5. **Short paragraphs:** four lines at most, one thought each.
6. **Fold the details.** What only some readers need - an error message, an edge case, the reason behind a rule - goes
   into `<details>` with a `<summary>` that says what is inside, instead of making the page longer.

**A lesson of Start** (`kind: lesson`) takes three to five minutes and has this shape:

1. A few short paragraphs and one runnable example (`trb run`), before the first heading.
2. At most two more sections, each one short text and one runnable block.
3. `## Exercise`: the task in one or two sentences that name the expected output, the starting code (`trb exercise`,
   or `trb exercise incomplete` for code with a gap), a folded hint and a folded solution (`trb run`)
   ([the contract](../tooling/torb-docs-site.md#runnable-blocks-and-exercises)).
4. `## Recap`: one line to remember.

A lesson uses only what the lessons before it taught, and says "the computer" before it says "the compiler".

## Why

A page that opens with "functional-first, with value semantics" loses everybody who does not know those words, and
the ones who do learn nothing new from them. Showing first and naming second works for both: the beginner sees what
happens, and the expert recognizes the term when it comes.

The shape of the levels follows the sites that teach well, listed in
[torb docs site](../tooling/torb-docs-site.md#where-the-patterns-come-from): one door per level, one idea per lesson
with the editor beside the text, exercises checked by their output, and a recap at the end.

## Consequences

- **The Reference stays deep.** Its pages keep their exact rules and their required sections; what changes is that
  each opens with a plain summary.
- **A translation mirrors its original** below `translations/<language>/`, keeps its links and code, and names the
  version it was made from in `translates`; `docs check` names the hash to write and lists a translation whose original
  changed since ([Languages](../tooling/torb-docs-site.md#languages)).
- **Lessons are not in the skill.** A model reads the Reference; the lessons teach the same rules in small steps.

## Related

- [How to write a page](writing.md) - the terminology, the precision and the examples every page is held to.
- [Front matter](front-matter.md) - the fields, and the kinds a page can have.
- [Learn to program](../start/index.md) - the course these rules produced.
