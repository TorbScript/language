---
title: What the research decided
summary: The ten rules this documentation is built on, each with the evidence and the source it comes from, and the practices that were deliberately left out.
kind: explanation
status: stable
skill: omit
order: 10
keywords:
  - research
  - Diataxis
  - agent skills
  - llms.txt
  - front matter
  - style guide
source:
  - https://diataxis.fr/
  - https://agentskills.io/specification
---

Every rule of this documentation comes from somewhere. The sources are documentation systems that have been running for
years (Rust, Go, Python, MDN, Kotlin, Swift, Gleam, Zig), the published format of Anthropic's Agent Skills, and
measurements of what agents and crawlers actually read. Where the sources disagree, the disagreement is written down
instead of resolved by preference.

## The decision

The ten rules that shaped everything else, shortest form first. Each one is argued under [Why](#why).

1. **The four modes of Diátaxis are an authoring test, not a folder layout.** Every page answers "action or
   cognition?" and "acquisition or application?". A page that cannot answer is two pages.
2. **Every page declares its kind in machine-readable front matter, and the kind decides its sections.** MDN's
   `page-type` is what makes 100 000 reference pages uniform; a mode that only lives in a style guide drifts.
3. **One artifact, one job, and the non-goals are written down.** Each folder index says what belongs in it and what
   does not, and the tie-break authority for every claim is named.
4. **Every page is self-contained.** It defines or links every term at first use and never says `see above`. A page is
   retrieved alone, by a search or by an agent, and a reference to a neighbour that was not loaded is a dead end.
5. **Examples come before prose, and every example is verified by the compiler.** A documentation for agents that
   contains one wrong snippet teaches that snippet.
6. **Index files are generated from the children's metadata.** An index that is maintained by hand is stale by the
   second page.
7. **Navigation is index, then summary, then page.** The summary of a page has to be enough to decide whether to open
   it, because deciding by opening is what fills a context window.
8. **A rule is a Good/Bad pair, never a bare prohibition.** Models flip decisions on framing alone, so "do not write
   X" loses to a prior; "write Y, because X means something else" does not.
9. **Every divergence from Rust, Swift, Kotlin and TypeScript is named explicitly, on its own page.** The failure mode
   of a new language is not ignorance, it is contamination from the languages the model does know.
10. **The gate is mechanical.** Front matter schema, controlled vocabularies, section order, link and anchor
    resolution, heading rules, summary length, forbidden words and every code block are checked by
    [`torb docs check`](checks.md). A rule that is not checked is a suggestion.

## Why

### Documentation architecture

**Diátaxis** ([diataxis.fr](https://diataxis.fr/)) classifies documentation on two axes, action against cognition and
acquisition against application, which yields tutorial, how-to guide, reference and explanation. Its own
[compass](https://diataxis.fr/compass/) is two questions, and the framework says the value is in asking them per page:
"Diátaxis changes the structure of your documentation from the inside." It also warns against the obvious
misreading - "It certainly does not mean that you should create empty structures for tutorials/howto guides/reference/
explanation with nothing in them. Don't do that. It's horrible."

*Rule derived:* use the compass as the authoring test (rule 1) and never create a folder before it has a page
(enforced: an index whose folder has no children is an error).

The documented critiques matter as much. Practitioners report that splitting one small topic across four pages makes
readers jump, and that users do not follow the links
([Kayce Basques on the Pigweed docs](https://news.ycombinator.com/item?id=42340740)). The same thread names a real gap:
Diátaxis has no slot for landing pages, and every documentation set needs them.

*Rules derived:* a short "why" note may stand inline in a reference page as long as the full argument lives in
`explanation/` and is linked; and `index` is a first-class kind with its own template, not an afterthought.

**How language projects split their documentation.** Rust ships separate books with declared jobs: The Book "will give
you an overview of the language from first principles", while
[The Reference](https://doc.rust-lang.org/reference/introduction.html) lists five explicit non-goals, including "not an
introduction" and "not a standard library reference". The Rustonomicon names the tie-break: when it and the Reference
disagree, "the Reference is correct". [TypeScript's handbook](https://www.typescriptlang.org/docs/handbook/intro.html)
is the only survey entry with a written goals-and-non-goals page for its own documentation.

*Rules derived:* rule 3. Every folder index carries a `## What belongs here` section, which is where the non-goals are
written down; and [structure.md](structure.md) names the tie-break authority for the whole tree.

[Go's specification](https://go.dev/ref/spec) and
[Zig's language reference](https://ziglang.org/documentation/master/) are single documents in grammar order. Zig gives
the reason for one page: "It is all on one page so you can search with your browser's search tool."

*Rule derived:* reference is the one mode whose order is dictated by the language rather than by the reader, so
`language/` mirrors the language. The single-page argument does not carry over: an agent greps a tree as cheaply as a
browser searches a page, and a tree lets it load one concept instead of all of them.

**Comparison pages.** TypeScript offers four entrances chosen by background, and the Java/C# one is written as
unlearning: "Rethinking the Class", "Rethinking Types". Kotlin's
[comparison to Java](https://kotlinlang.org/docs/comparison-to-java.html) is symmetric in three parts - what Java does
badly, what Java has that Kotlin lacks, what Kotlin adds. Gleam keeps one short cheatsheet per origin language.

*Rules derived:* rule 9, plus the shape of the `contrast` kind: a table at a glance, then what changes in code, then
the habits to unlearn. The middle part - what the other language has that TorbScript deliberately lacks - is what makes
the page credible instead of promotional.

**Glossaries.** MDN's rule is one term, one entry, "no more than a couple sentences", and an entry that wants to become
an article belongs elsewhere. Python's glossary earns its weight because every other document links into it.

*Rules derived:* [glossary.md](../glossary.md) is normative for terminology, every entry is at most two sentences plus
one link, and a term that needs more gets a page in `language/` with a one-line entry pointing at it.

### Documentation for LLM agents

**Agent Skills.** The format is published at
[agentskills.io/specification](https://agentskills.io/specification) and documented at
[docs.claude.com](https://platform.claude.com/docs/en/agents-and-tools/agent-skills/overview). A skill is a directory
whose `SKILL.md` has YAML front matter with a required `name` (at most 64 characters, lowercase letters, digits and
single hyphens, equal to the directory name) and a required `description` (at most 1024 characters). Loading happens on
three levels: `name` and `description` are always in context at roughly 100 tokens, the body is read when the skill
triggers and should stay under 500 lines and 5000 tokens, and bundled files cost nothing until they are read.

The guidance that shaped the generated skill, quoted where the wording carries the rule:

- The description "must say both what the Skill does and when to use it", is matched against the request, and is
  written in the third person because it is injected into the system prompt.
- "Keep SKILL.md body under 500 lines for optimal performance." Overflow becomes another file, not denser prose.
- "Keep file references one level deep from SKILL.md", because "Claude may partially read files when they're referenced
  from other referenced files" and then reads a prefix instead of the file.
- "For reference files longer than 100 lines, include a table of contents at the top."
- "Concise is key. The context window is a public good."
- Explain the reason instead of raising the volume: "If you find yourself writing ALWAYS or NEVER in all caps ... that's
  a yellow flag."
- One term per concept: "Consistency helps Claude parse and follow instructions."
- No time-sensitive statements; superseded material goes into a collapsed section.
- Evaluations before prose: three tasks that fail without the skill, then the smallest text that passes them.

*Rules derived:* [the skills](the-skill.md) are generated from templates whose bodies inline and carry pages of this
documentation (so a skill cannot drift from it), one entry skill holds what every task needs and one skill per area
what only some tasks need, `torb docs skill` fails when a body passes 500 lines, and a page with `status: planned` is
carried with its banner.

**Context-window economics.**
[Anthropic on context engineering](https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents)
names the objective as "the smallest possible set of high-signal tokens that maximize the likelihood of some desired
outcome", and the failure as context rot: recall drops as the window fills. The recommended pattern is to keep
lightweight identifiers - file paths - and load the content when it is needed, which "allows agents to incrementally
discover relevant context through exploration".

*Rules derived:* rules 6 and 7. A path is the index, a `summary` is the decision, and a page is the payload. The
generated `references/index.md` of each skill is one file that holds every path with its summary.

**Retrieval-friendly writing.** [kapa.ai's guidance](https://docs.kapa.ai/improving/writing-best-practices) is the
clearest statement of the mechanism: retrieval works on chunks, so a chunk that depends on a neighbour loses the
dependency. It names the consequences - ban `as mentioned above`, repeat the name of the thing instead of a pronoun
("terms absent from chunks won't be retrieved"), keep a constraint next to the guidance it constrains, quote exact
error messages next to their fixes, and never let a table's meaning live in merged headers.

On chunk size the sources disagree by more than their defaults admit: 128-512 tokens, 300-600, 500-800, with a peer-reviewed study at
NAACL 2025 finding fixed-size chunking beating semantic chunking on realistic documents.

*Rule derived:* do not target a token count. Target one concept per heading with a body of a few paragraphs, which
satisfies every published range at once and survives the degenerate chunker - an agent running `grep` and reading the
lines around the hit.

**Negative examples.** Bare prohibitions are weak: Anthropic's prompt guidance is "Tell Claude what to do instead of
what not to do", and the framing effect is measurable
([Yes is Harder than No, CIKM 2025](https://dl.acm.org/doi/10.1145/3746252.3761350)). Contrastive pairs, on the other
hand, work: [arXiv 2403.08211](https://arxiv.org/html/2403.08211v2) reports that models reason well over a positive
example placed next to the negative one it corrects.

*Rule derived:* rule 8, and the `## What this is not` section of every reference page, whose wrong examples are fenced
`trb error` with the exact diagnostic the compiler produces.

**llms.txt.** The [format](https://llmstxt.org/) is an H1, a blockquote summary, and H2 sections of
`[title](url): notes` links, with `## Optional` meaning "skip this under pressure". The evidence on consumption is bad:
Ahrefs found that 97% of published `llms.txt` files received no request at all in May 2026, SE Ranking found no
correlation with being cited across about 300 000 domains, and Google has said it does not support the file.
`llms-full.txt` is not part of the proposal at all - it is a convention popularised by Mintlify.

*Rule derived:* [`torb docs bundle`](checks.md) writes both files because they cost sixty lines and make the tree
quotable as a single path, and the documentation states that they are an output and never an input. The agent-facing
form of this documentation is the skill, not a text file a crawler might read.

**AGENTS.md.** [agents.md](https://agents.md/) is a schema-free Markdown file at the repository root whose nearest copy
wins in a monorepo. It is always-loaded project context, which is the opposite of a skill: a skill has a validated
schema and is loaded on demand.

*Rule derived:* the two are not merged. Repository instructions stay short and stay where they are; the language
documentation becomes a skill.

### Front matter

The field names in use disagree across systems: Docusaurus uses snake_case (`sidebar_position`, `sidebar_label`,
`draft`, `unlisted`, `keywords`), [Hugo](https://gohugo.io/content-management/front-matter/) reserves a long list and
pushes custom fields under `params`, [Starlight](https://starlight.astro.build/reference/frontmatter/) nests navigation
under `sidebar.order`, MkDocs Material requires custom `status` values to be registered before use, and
[MDN](https://developer.mozilla.org/en-US/docs/MDN/Writing_guidelines/Page_structures/Page_types) uses kebab-case with
`page-type` and a three-value `status`.

Four things were taken from that comparison:

- **A closed vocabulary validated at build time**, from MkDocs Material: a typo in a `status` has to be an error, not a
  badge that silently disappears.
- **One enum that picks the template**, from MDN's `page-type`: that is what lets a tool check that a reference page has
  a Syntax section.
- **Every field needs a consumer.** Hugo's reserved list is long because fields were added before their readers.
  [front-matter.md](front-matter.md) names the consumer of each of the nine fields.
- **Single-word, lowercase keys**, so there is no case convention to remember and no second spelling to support.

On the YAML itself: the [1.2 specification](https://yaml.org/spec/1.2.2/) forbids tabs for indentation, and a plain
scalar may not start with any of `- ? : , [ ] { } # & * ! | > ' " % @` or contain `: `. Naive parsers additionally break
on YAML 1.1 booleans (`y`, `no`, `on`), leading zeros and versions like `1.10`.

*Rule derived:* the front matter of this documentation is a **subset** with a written grammar, not "YAML". A
self-hosted toolchain has to read it without a YAML library, and a subset that a 200-line reader and a full library
agree on is the only way both can be true. The subset is specified in [front-matter.md](front-matter.md).

### Quality gates

**Tested snippets.** Rust's doctests take comma-separated fence attributes - `ignore`, `no_run`, `compile_fail`,
`should_panic`, `edition2021` - and hide setup lines that start with `# `; the rationale for hiding is that "forcing you
to write `main` for every example, no matter how small, adds friction and clutters the output". Go's testable examples
assert with a trailing `// Output:` comment and compile-only without one. Zig compiles the samples of its language
reference as part of its own test suite. Python's doctest calls the result "executable documentation".

*Rules derived:* the default for a `trb` block is the strictest thing the front end can say, every weaker level is a
named marker in the info string, and `trb skip` needs a written reason that `docs check` prints. The `compile_fail`
idea becomes `trb error`, whose expected diagnostic is written in the block as a `// error:` comment - Go's insight
that the cheapest assertion is a comment next to the code, applied to messages instead of output.

**Single sourcing.** Sphinx's `literalinclude` can select a Python object by name, mdBook includes named `ANCHOR`
regions, and remark-code-import addresses line ranges. The lesson is that line ranges drift silently while named
regions break loudly.

*Not adopted, and the reason:* including regions from `.trb` files would mean editing the sources of the standard
library and the tour to carry region markers for the benefit of the documentation. Verifying every block against the
real front end reaches the same goal - no wrong code - without putting documentation markers into the language's own
sources. Inclusion stays an open question in [structure.md](structure.md).

**Link checking.** Docusaurus throws on broken links by default and warns on broken anchors; MkDocs has a validation
table with `warn`/`info`/`ignore`; lychee has `--offline` and caches external results.

*Rule derived:* internal links and anchors are errors, and external links are not followed at all. A gate has to give
the same answer offline as online, and an unreachable host is not a documentation defect.

**Prose rules.** The [Google developer documentation style guide](https://developers.google.com/style) is specific
enough to encode: second person, present tense, active voice, sentence-case headings with no trailing punctuation, no
skipped heading levels, and a word list that rejects `simply`, `easy`, `please`, `e.g.` and `execute`. Microsoft's guide
adds "when in doubt, don't capitalize" and no terminal punctuation in headings. Vale exists to run such rules, and its
lesson is that `error` level belongs only to rules worth blocking a merge on.

*Rule derived:* the mechanically decidable part of that guidance is in `torb docs check` (sentence case, no trailing
punctuation, no skipped levels, a short list of words that say nothing exactly), and the rest is normative prose in
[writing.md](writing.md). Nothing is a warning: a warning in a gate is a comment.

**Status markers.** Rust's `#[unstable]` lives on the code and renders a banner with the tracking issue. MDN derives
`status` from browser-compat data by automation. Kotlin distinguishes unstable-to-call from unstable-to-subclass.

*Rule derived, and its limit:* the right answer is to derive `status` from the compiler, and this documentation cannot
do that yet - the language has no stability table. What is enforced instead is that a `planned` or `draft` page must
open with a fixed banner line, checked by the tool, so the marker cannot be forgotten on an individual page. Deriving
the status from the toolchain is an open question in [structure.md](structure.md).

**Stable URLs.** All five site generators separate the slug from the file path, and MDN generates its redirect table
with a tool rather than by hand.

*Rule derived, and its limit:* this documentation has no published URLs yet, so a path is its identity and there is no
redirect mechanism. Anchors can be pinned against rewording with an explicit `{#anchor}` behind a heading, which is
what any heading linked from another page should carry.

## Consequences

Four things follow for anybody writing here.

1. **The gate is the review.** [`torb docs check`](checks.md) decides the schema, the sections, the links, the headings
   and the snippets. A human review is about whether a page is *true* and *useful*, and about nothing the tool can
   already decide.
2. **A page is written to be read alone.** That costs repetition - a term defined twice, a rule restated next to a
   second example - and the repetition is intended.
3. **The wrong examples matter as much as the right ones.** A reference page without a `## What this is not` section is
   incomplete, because the mistake it fails to name is the one a model will make.
4. **Nothing is written twice by hand.** Index bodies are generated, the skill is generated, the standard-library
   declarations will be generated by milestone 8's `torb doc`. Where two places would have to agree, one of them is an
   output.

## Related

- [How this documentation is structured](structure.md) - the tree, the kinds, and the tie-break authority.
- [The front matter](front-matter.md) - the nine fields, their consumers, and the legal subset of YAML.
- [How to write here](writing.md) - the normative writing rules for humans and for agents.
- [Adding a page](adding-a-page.md) - the templates and the steps.
- [The checks](checks.md) - what every command verifies.
- [The skill](the-skill.md) - how the Agent Skill is derived.
