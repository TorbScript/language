---
title: The front matter
summary: The nine fields a page may carry, what reads each of them, and the exact subset of YAML that a page is allowed to use.
kind: reference
status: stable
skill: omit
order: 30
keywords:
  - front matter
  - YAML
  - title
  - summary
  - kind
  - status
source:
  - compiler/src/documentation/schema.trb
  - compiler/src/documentation/markdown.trb
---

Every page starts with front matter between two `---` lines. It carries four required fields and up to five optional
ones. Every field has a consumer: a reader, the index generator, the skill builder or the checker. A field without a
consumer is not in the list.

## Example

```yaml
---
title: Bindings
summary: A const binding never changes and a var binding can, and the binding decides whether the value it holds can be changed.
kind: reference
status: stable
order: 10
keywords:
  - const
  - var
  - binding
  - shadowing
source:
  - CONCEPT.md#bindings
  - examples/tour/src/01-bindings-and-values.trb
---
```

## Syntax

The front matter is a **subset** of YAML, not all of it. It is written down here in full: the toolchain reads it with
[std/yaml](../standard-library/yaml.md) and checks the subset on top, so that every other tool - a static site
generator, an editor, GitHub - reads each page the same way.

```text
front-matter ::= "---" newline field* "---" newline
field        ::= key ":" (" " value)? newline block-list?
key          ::= lowercase-letter (lowercase-letter | "-")*
value        ::= scalar | inline-list | integer
scalar       ::= quoted | plain
quoted       ::= '"' (character | '\"' | '\\')* '"'
plain        ::= a line that does not start with an indicator and does not contain ": "
inline-list  ::= "[" (scalar ("," scalar)*)? "]"
integer      ::= "-"? digit+
block-list   ::= ("  - " scalar newline)+
```

The indicator characters that a plain scalar may not start with are the ones YAML reserves:

```text
- ? : , [ ] { } # & * ! | > ' " % @ `
```

## Rules

1. **The opening `---` is the first line of the file.** A file whose first line is something else has no front matter at
   all and is treated as a plain design document.
2. **One field per line.** A key is lowercase letters and hyphens and is followed by a colon.
3. **A key appears at most once.** A repeated key is an error, not a last-one-wins.
4. **A value is a scalar, an integer, an inline list or a block list.** A block list is written under a key with no
   value, with exactly two spaces, a hyphen and a space in front of each item.
5. **A value that starts with an indicator character or contains `: ` is written in double quotes.** Inside quotes only
   `\"` and `\\` are escapes.
6. **There are no tabs, no comments, no nested maps, no anchors, no aliases and no multi-line scalars.** The YAML 1.2
   specification forbids tabs for indentation; the rest is left out so that the subset stays readable by every parser.
7. **A scalar field may be read as a one-item list.** `source: CONCEPT.md` and a block list with one item mean the same
   thing.
8. **An unknown key is an error.** A typo has to fail, because a field nobody reads is silence.

### The four required fields

| Field | Type | Consumer | Rule |
|-------|------|----------|------|
| `title` | scalar | reader, index generator, skill navigation | At most 60 characters, no full stop at the end. It is the `#` heading of the page, so the body starts at `##`. |
| `summary` | scalar | index generator, skill, and an agent deciding whether to open the page | 40 to 240 characters, one or two whole sentences, ends with `.`, `?` or `!`. It must not contain `this page`: a summary is about the subject. |
| `kind` | scalar | checker (section order), template, skill builder, site generator | One of `index`, `guide`, `reference`, `how-to`, `explanation`, `contrast`, `tooling`, `package`, `glossary`, `site`, `lesson`. A `site` page and a `lesson` are left out of the skill and the bundle. |
| `status` | scalar | reader, skill builder | One of `stable`, `draft`, `planned`. |

### The seven optional fields

| Field | Type | Consumer | Rule |
|-------|------|----------|------|
| `order` | integer | index generator | The order inside its folder, lowest first. A page without one follows every page that has one, sorted by title. |
| `source` | list | a human hunting drift | Where the facts come from: a `CONCEPT.md` section, a `std/` file, an example, a URL. Required for `kind: reference` and `kind: package`. |
| `keywords` | list | search, and the skill's reference index | The words somebody would look for that are not in the title. |
| `prerequisites` | list | reader, learning path | Paths of pages to read first. Only a `guide` page has it. |
| `documents` | list | index generator, checker | Plain Markdown files without front matter that this index links, relative to the index. Only an `index.md` has it. |
| `skill` | scalar | skill builder | One of `model`, `cheat-sheet`, `mistakes`, `verify`, `omit`. The first four each belong to exactly one page in the tree; `omit` keeps a page out of the skill. |
| `translates` | scalar | checker, site generator | The version of its English original a translation below `translations/<language>/` was made from: the twelve hexadecimal digits `docs check` names. Only a translation has it, and every translation does. |

### The two status banners

A page that is not `stable` opens its body with a fixed line, and the checker requires it. A reader who lands on the
page from a search has to see it without reading the front matter.

```md
> **Planned.** This feature is designed but not implemented. Nothing on this page works today.
```

```md
> **Draft.** This page is being written and may be wrong. Verify it against the compiler.
```

## What this is not

**It is not YAML.** A construct that a YAML library accepts is not therefore legal here. This fails:

```yaml
---
title: Bindings
tags: {a: 1}
summary: >
  A folded scalar
---
```

The nested map is not in the subset, `tags` is not a field, and a folded block scalar is not a value. Write a block
list, and quote anything that needs quoting.

**A `summary` is not an introduction.** It is read in a list next to twenty other summaries, by somebody deciding which
one to open.

```yaml
summary: This page describes the binding forms of the language and gives some examples of them.
```

```yaml
summary: A const binding never changes and a var binding can, and the binding decides whether the value it holds can be changed.
```

The first one describes the document; the second one answers the question. The checker rejects `this page` for exactly
this reason, and nothing else about the first one is decidable by a tool.

**`status: planned` is not a hedge.** It means the feature does not exist. A planned page is excluded from the generated
skill completely, because a model that reads a designed feature as an available one writes code that cannot compile.

**`kind` is not a tag.** A page has exactly one kind, and it decides the sections. A page that wants two kinds is two
pages.

## Related

- [How this documentation is structured](structure.md) - the tree and what each kind is for.
- [How to write here](writing.md) - the rules for the prose and the code.
- [Adding a page](adding-a-page.md) - a template per kind, ready to copy.
- [The checks](checks.md) - what reports a broken front matter.
