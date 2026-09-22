---
title: The docs commands
summary: What torb docs check, index, skill and bundle each do, which rules they decide, and which rules only a reviewer can decide.
kind: tooling
status: stable
skill: omit
order: 60
keywords:
  - torb docs check
  - torb docs index
  - gate
  - snippets
  - canon
source:
  - compiler/src/documentation/command.trb
  - compiler/src/documentation/check.trb
---

The documentation has a gate. `torb docs check` decides everything about a page that a program can decide: the front
matter, the sections, the headings, the links, the anchors, the summaries, a short list of words that say nothing
exactly, and every code block. What it cannot decide is whether a page is true and useful, which is what a review is
for.

## Synopsis

```text
torb docs check [root]                     Check the documentation (default root: docs)
    --standard-library <path>              Where std/ is. Default: <root>/../std
    --no-snippets                          Do not type check the snippets, which is the slow part
torb docs index [root]                     Write the generated part of every index.md
    --check                                Report instead of writing, for the gate
torb docs skill <root> <out>               Write the Agent Skill of the language
torb docs bundle <root> <out>              Write llms.txt and llms-full.txt
torb docs source <path>...                 Check the doc comments of the code itself
```

Every command exits with 1 when it reports a problem and with 0 when it does not, and both run from the repository
root:

```console
torb docs check docs
torb docs index --check docs
```

The first two are gates of every change. `docs source` is about the doc comments of `std/`, `compiler/` and `examples/`
instead of about these pages, it has rules of its own, and it is not a gate yet - it has its own page,
[torb docs source](../tooling/torb-docs-source.md).

## What it does

### The shape of the tree

- Every folder has an `index.md`, and a folder that has nothing but its index is an error.
- Every `.md` file that starts with a `---` line is a page. One that does not is a plain design document and has to be
  named in the `documents` field of some index, so it is reachable and nothing is silently ignored.
- Every page is reachable from `docs/index.md`, which follows from the two rules above: the body of an index is
  generated from its children.
- A file name is lowercase and uses hyphens.

### The front matter

- The opening `---` is the first line, and the front matter is closed by a second one.
- Only the nine known fields, each at most once, in the legal subset described in
  [front-matter.md](front-matter.md). A value that needs quotes and does not have them is an error, not a guess.
- `title`, `summary`, `kind` and `status` are present. `kind` and `status` are in their vocabularies. A `reference` or
  `package` page has a `source`.
- A `title` is at most 60 characters and has no full stop. A `summary` is 40 to 240 characters, ends a sentence, and
  does not contain `this page`.
- `documents` only on an index, `prerequisites` only on a guide, and each of the four skill roles on at most one page.

### The body

- No `#` heading: the `title` is the heading of the page, so a body starts at `##`.
- Sentence case, no full stop and no colon at the end of a heading, and no skipped heading level.
- Two headings of one page may not produce the same anchor.
- The `##` sections the `kind` requires are present, in the required relative order. Other sections may stand between
  them.
- A `planned` or `draft` page opens its body with the banner of its status.
- The words `usually`, `simply`, `obviously`, `basically`, `easily`, `of course`, `as mentioned`, `see above`,
  `and so on`, `please note` and `etc.` are errors in prose. What stands between backticks is not prose, so a page
  about the rule can quote the word.

### The links

- An internal link resolves to a page of the tree or to a plain document of it, and an anchor resolves to a heading of
  the page it names. Both are errors when they do not: an agent follows a path instead of guessing.
- A link that leaves the tree (`../../CONCEPT.md`) has to exist on disk.
- A link that starts with `http://` or `https://` is not followed at all. A gate has to answer the same offline as
  online, and an unreachable host is not a defect of the documentation.
- A link inside the documentation is relative and names a `.md` file.

### The code blocks

A fence is at least three backticks and names its language: `trb`, `text`, `console`, `json`, `md`, `yaml` or `diff`.
A closing fence is at least as long as the one it closes, so a block that shows a fence opens with four backticks. A
`trb` fence may carry one marker, and the marker decides how hard the block is checked.

| Fence | What is checked |
|-------|-----------------|
| `trb` | Lexes and parses without a diagnostic, and is in the formatter canon |
| `trb check` | The same, and type checks against the real `std/` as the entry file of a package |
| `trb fragment` | Lexes without a diagnostic. For a signature or a shape that is not a whole program |
| `trb error` | Produces the diagnostics the block declares in its `// error:` lines |
| `trb skip <reason>` | Nothing. The reason is required and the gate prints every skip |

A `trb error` block writes what it expects into the code, as a comment:

````md
```trb error
const small: Int8 = 300
// error: `300` does not fit into `Int8`
```
````

The comment stays in the block, because it parses like any other comment and because an expectation that sits next to
the line it belongs to cannot drift away from it. Every expectation has to appear in some diagnostic of the block, and a
`trb error` block that produces no diagnostic at all is an error itself.

Type checking happens in **one** run of the front end for the whole documentation: the standard library is read and
resolved once, and every block that asked for it becomes a package of a synthetic workspace next to `std/`. One run
costs seconds; one run per block would cost minutes.

### The canon

Two of the rules of the formatter canon are decided on every block that parses:

- **calls**: a call is a command wherever the grammar allows it, and has parentheses everywhere else. Both directions
  are reported. A call whose callee names a field of the type it stands in is left alone, because there the parentheses
  are meaning and not style.
- **strings**: a multi-line `"""` or `r"""` is indented two spaces deeper than the line its statement starts on, with a
  closing `"""` that stands alone aligned with the content.

What is **not** checked is the layout that milestone 8's `torb format` will own: line length, blank lines, and where a
long call breaks. A snippet that passes here is in the canon of `torb canon`; the reverse is not promised.

### The indexes

`torb docs index` writes the lines between `<!-- torb:index:begin -->` and `<!-- torb:index:end -->` of every
`index.md`, from the `title`, `summary`, `order` and `status` of the children. Everything above the first marker is
untouched, byte for byte. Below the closing marker nothing is allowed.

The generated body has up to three sections: `## Sections` for the folders below, `## Pages` for the pages of the
folder, and `## Design documents` for what the `documents` field names. A child with `status: planned` or
`status: draft` is marked in its entry.

`--check` reports every index whose body is not what it would write, and writes nothing. That is the form the gate uses.

## Examples

A green run names the numbers, so a change in them is visible in a diff:

```console
$ torb docs check docs
42 pages, 15 folders, 80 snippets, no problems
```

A problem names the file, the line and the rule:

```console
$ torb docs check docs
../docs/language/types/values.md:31: A call is a command wherever the grammar allows it. Write `Ok ...` without parentheses
../docs/language/types/values.md:52: `soon` is not a status. One of: stable, draft, planned

2 problems in 42 pages
```

### What no command decides

These are the reviewer's, and they are the ones that matter:

- Whether a claim is **true** of the compiler as it is today.
- Whether the example is the **smallest** one that shows the construct.
- Whether `## What this is not` names the mistake a reader will actually make.
- Whether the `summary` is enough to decide whether to open the page.
- Whether the page is understandable when it is the **only** page loaded.

## Related

- [How this documentation is structured](structure.md) - the tree and the kinds.
- [The front matter](front-matter.md) - the nine fields and the YAML subset.
- [How to write here](writing.md) - the rules the reviewer checks.
- [Add a page](adding-a-page.md) - the steps and the templates.
- [The skill](the-skill.md) - what `docs skill` produces.
