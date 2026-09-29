---
title: How this documentation is structured
summary: The tree, what each folder is for, the eleven kinds of page, and which source wins when two documents disagree.
kind: explanation
status: stable
skill: omit
order: 20
keywords:
  - structure
  - folders
  - kinds
  - index
  - source of truth
---

The documentation is a tree of folders under `docs/`. Every folder has an `index.md`, every page and every index has
front matter with at least a `title` and a `summary`, and the body of an index between its two marker comments is
generated from its children. An agent or a reader navigates `docs/index.md`, then a folder index, then one page, and
loads a few kilobytes on the way instead of the whole tree.

## The decision

```text
docs/
├ index.md                     The index of indexes
├ glossary.md                  Every term, one entry each, normative
├ start/                       The course for somebody who has never programmed, one lesson per page
├ guide/                       The learning path for somebody who programs already
├ language/                    The reference: one concept per page, grouped by area
├ standard-library/            One page per package of std/
├ how-to/                      Task recipes for somebody who already knows the language
├ explanation/                 Why the language is the way it is, and contrast pages
├ tooling/                     The torb command, project files, the formatter canon
├ internals/                   An index that links the compiler design documents in place
├ contributing/                How this documentation is written
├ site/                        The pages of the website around the documentation: the front page, the install page
└ translations/<language>/     The same pages in another language, and the words of the site's interface
```

The four modes of [Diátaxis](https://diataxis.fr/) map onto `guide/` (tutorial), `how-to/`, `language/` plus
`standard-library/` (reference) and `explanation/`. `tooling/`, `internals/` and `contributing/` are not modes, they are
subjects: the toolchain, the compiler's own design, and this documentation. `index.md` files are a genre of their own,
which Diátaxis has no slot for.

| Folder | Answers | Does not answer |
|--------|---------|-----------------|
| `start/` | "I have never programmed. Teach me from zero." | Anything a lesson has not shown yet |
| `guide/` | "I program already. Teach me this language in order." | "What exactly does this construct mean?" |
| `language/` | "What exactly does this construct mean?" | "How do I build a web server?" |
| `standard-library/` | "What does `std/core` contain?" | "Why is `Result` shaped like this?" |
| `how-to/` | "How do I do this one task?" | "Why does the language want it this way?" |
| `explanation/` | "Why is it this way, and what would I guess wrong?" | "What is the exact syntax?" |
| `tooling/` | "Which command, which flag, which file?" | "How does the compiler work inside?" |
| `internals/` | "How is the compiler built?" | Anything a user of the language needs |
| `contributing/` | "How do I write a page here?" | Anything about the language itself |
| `site/` | "What is TorbScript, and how do I install it?" | Anything the documentation answers |
| `translations/` | The same questions, in another language | Anything its English original does not answer |

## Why

**One artifact, one job, and the non-goals written down.** Rust's reference lists five things it is not; TypeScript's
handbook has a goals-and-non-goals page. Without that, every folder slowly becomes a tutorial. Here the mechanism is
the `## What belongs here` section that every folder index must have - the checker requires it, so the non-goals of a
folder cannot be left implicit.

**The reference mirrors the language, not the reader.** `language/` is grouped by area (`values-and-types`, `functions`,
`types`, `traits`, `generics`, `pattern-matching`, `errors`, `collections-and-iteration`, `concurrency-and-streams`,
`modules-and-packages`, `syntax`) because that is the only order somebody looking a construct up can predict. `guide/`
is the opposite: it is ordered by what can be learned first.

**One concept per page.** A page has to be understandable when it is the only page loaded. That is what makes it usable
by a search, by a retrieval system, and by an agent that follows a path. The price is repetition, and the price is worth
paying.

**Indexes are generated.** The body between `<!-- torb:index:begin -->` and `<!-- torb:index:end -->` is written by
`torb docs index` from the `title`, `summary`, `order` and `status` of the children. Above the first marker the author
writes the front matter and an introduction; below the closing marker nothing is allowed. So an index cannot be stale,
a new page appears in its index without a second edit, and `docs index --check` is a gate.

### The eleven kinds

`kind` in the front matter is a closed vocabulary. It picks the required sections, and the tool checks them.

| Kind | For | Required sections, in order |
|------|-----|----------------------------|
| `index` | The `index.md` of a folder | `What belongs here` (except the root) |
| `guide` | A step of the learning path | `Goal` first, `Next` last |
| `reference` | One construct of the language | `Example`, `Syntax`, `Rules`, `What this is not`, `Related` |
| `how-to` | One task | `Steps`, `Full example`, `Related` |
| `explanation` | Why something is the way it is | `The decision`, `Why`, `Consequences`, `Related` |
| `contrast` | "Coming from Rust" and its siblings | `At a glance`, `What changes in your code`, `Habits to unlearn`, `Related` |
| `tooling` | A command or a project file | `Synopsis`, `What it does`, `Examples`, `Related` |
| `package` | One package of `std/` | `Import`, `Declarations`, `Related` |
| `glossary` | `glossary.md` | `Terms` |
| `site` | A page of the website in `site/`, which `torb docs site` writes at the root of the site and the skills and the bundle leave out | none |
| `lesson` | A lesson of the course in `start/`, which the skills and the bundle leave out ([the shape of a lesson](levels-and-plain-words.md)) | `Exercise`, `Recap` |

Other `##` sections may stand between the required ones. A reference page is free to add `## More examples` or
`## Coming from other languages`; it may not leave out `## What this is not`.

### Which source wins

The documentation states facts about a language that is still being implemented, so the order of authority is written
down and there is no ambiguity about it:

1. **The compiler decides what is true today.** A page that disagrees with what `torb check` accepts is a defect in the
   page. This is why every `trb` block is run through the compiler's own front end.
2. **`CONCEPT.md` decides what is intended** until the compiler compiles itself. It is the design source of truth, with
   its own Decision Log. A page derived from it names the section in its `source` field, so drift can be found.
3. **This documentation is the user-facing form.** It is not a second design document. A question that `CONCEPT.md`
   leaves open does not get an answer invented here; it gets a `status: planned` page or no page.

Where `CONCEPT.md` and the compiler disagree today, the page says so in one sentence and links both. Silence about a
contradiction is worse than the contradiction.

## Consequences

- **A folder is created together with its first page.** An index whose folder has no children is an error, because an
  empty section tells a reader that something is missing when nothing is.
- **`docs/ARCHITECTURE.md`, `docs/TYPECHECKER.md` and `docs/BACKEND.md` stay where they are.** They are compiler design
  documents in plain Markdown without front matter. They are indexed in place: `internals/index.md` names them in its
  `documents` field, and the generated body links them. Any `.md` file in the tree that has no front matter has to be
  named by some index that way, or the checker reports it.
- **A page is addressed by its path.** There is no slug and there are no redirects, because nothing is published under
  a URL yet. Moving a page means fixing the links to it, which `docs check` finds.
- **An anchor that another page links to should be pinned.** Write `## A heading {#stable-anchor}` and the anchor stops
  depending on the wording of the heading.

### Open questions

These are decisions the tree does not make yet. They are listed rather than guessed.

- **Including code from real files instead of retyping it.** mdBook's `{{#include file.rs:anchor}}` and Sphinx's
  `literalinclude` exist so that a snippet cannot drift from the source it came from. Adopting it would mean putting
  region markers into `std/` and `examples/`. Verifying every block against the real front end reaches the same goal
  without that, which is why it was not adopted; it stays the better answer for long listings.
- **Deriving `status` from the toolchain.** MDN derives its status from machine-readable compatibility data. The
  equivalent here is the manifest of natives and the milestone table, and it does not exist as data yet. Until it does,
  `status` is hand-written and the banner is enforced.
- **Versioning.** The language has no released versions, so there is no `since` field and no version selector. When
  there is, the `source` field is where a version would attach.

## Related

- [What the research decided](research.md) - the evidence behind every rule here.
- [The front matter](front-matter.md) - the nine fields and the legal subset of YAML.
- [How to write here](writing.md) - the writing rules.
- [Adding a page](adding-a-page.md) - the templates and the steps.
- [The checks](checks.md) - what the commands verify.
- [The glossary](../glossary.md) - the normative terminology.
