---
title: torb doc
summary: torb doc turns the public API of a package and its doc comments into a reference - a static site, or one JSON document for an editor and the registry - and runs the examples of the doc comments as doc tests.
kind: tooling
status: stable
order: 110
keywords:
  - torb doc
  - doc comment
  - documentation generator
  - doc test
  - API reference
source:
  - compiler/src/reference/command.trb
  - compiler/src/reference/model.trb
  - compiler/src/reference/links.trb
  - compiler/src/reference/site.trb
  - compiler/src/reference/doctests.trb
  - CONCEPT.md#doc-comments
---

A doc comment (skill `torbscript-language`: `references/glossary.md`) stands in front of almost every public declaration of `std/`, and `torb
doc` is what reads it for somebody who does not read the source: one model of a package's public API with three
readers - the HTML reference, the doc tests, and the JSON a language server's hover and the registry's per-package
pages take.

## Synopsis

```text
torb doc [path]... [--output <dir>] [--json] [--check] [--no-run]

  (no flag)        Write the static site into --output (default: build/doc)
  --output <dir>   Where the site goes
  --json           Print the model as one JSON document instead, and write nothing
  --check          Fail on a broken link or a failing doc test, and write nothing
  --no-run         With --check: type check the examples without building and running them
```

## What it does

### What is documented

A path is a package, a workspace or a directory of packages (`torb doc std` documents every package of `std/`), and
every package the checked files belong to is documented. The program is checked first, and a program with a problem
is refused: the model is read out of the checked program - the syntax trees the checker holds, the scopes it built -
and not out of a second parse.

- **A package is documented by what its entry module exports**: the `public use` lines of `src/lib.trb` - or of
  `src/main.trb` in a package that has no library - name the public API, and each exported construct is shown in the
  module that declares it. A package whose entry exports nothing, an application, is documented by every `public`
  declaration of its `src/`.
- **A construct is shown with its members**: the fields, cases, `static` members and methods of a type or a trait that
  are not `private`, and every `extend` of the package with the members it adds. Tests, `project.trb` and every
  declaration that is not public are left out.
- **A signature is the source's own text**, token by token on one line: without the body, without the comments -
  the doc comment of a parameter is shown under the signature instead - and without `public`, which every construct of a
  reference is.
- **A doc comment is split into its parts**: the first sentence is the
  summary an index and the search show, the paragraphs above the first heading are the description, and `# Examples`,
  `# Errors`, `# Panics`, `# Pitfalls`, `# Open` and `# Related` each become a section.

### Links

`[Name]` and `[Type.member]` in a doc comment are resolved the way a name at that place resolves for the checker: the
file's own declarations, its imports and the prelude, and a member through the type - declared in it, added by an
`extend` anywhere in the program, or declared by a trait it comes `with`. A link that resolves to a construct of the
site becomes a link to its anchor, one that resolves to a construct without a page (a package that was not documented,
a declaration that is not public) is shown as code, and one that resolves to nothing is a problem at its line. The rules
are the ones `torb docs source` judges a link by, so the two commands agree on what is broken.

### The site

Plain files that can be opened from disk or served by any static host, and no request leaves a page:

| File | What is in it |
|---|---|
| `index.html` | Every package with its summary |
| `<package>/index.html` | The module comment of the entry module, every module, and every construct in alphabetical order |
| `<package>/<module>.html` | The module comment, a table of contents, and every construct with its signature, its doc comment, its sections and its members, each with an anchor: `#Option`, `#Option.map`, `#Option.Some` |
| `search.js`, `search-index.js` | The search as the reader types, over every construct and member, and the theme toggle |
| `search-index.json` | The same index for a tool |
| `reference.json` | The whole model, what `--json` prints |
| `style.css` | The tokens of `brand/tokens.css` and the stylesheet the documentation site shares, so the reference and the site look like one: light and dark by the system or by the toggle, Chivo where the host serves it at `/assets/fonts/`, readable at the width of a phone |

The Markdown of a comment is rendered by `std/markdown` (skill `torbscript-standard-library`: `references/standard-library/markdown.md`), and every TorbScript code
block is coloured at generation time by the lexer and the resolver behind `torb highlight` (skill `torbscript`: `references/tooling/the-torb-command.md`), so a
page colours a field and a case the way the editor does. The first line of an example that says how it is checked
(`// fragment`, `// skip <reason>`, `// check`) is for the tools and is not shown.

### The JSON

`--json` prints `{"format": 1, "packages": [...]}`: every package with its modules, every module with its constructs,
and every construct with its kind, name, anchor, signature, summary, description, sections, parameters, examples,
resolved links, members, and `deprecation` - the `reason`, `replacement` and `since` of its
`deprecated` (skill `torbscript-language`: `references/language/modules-and-packages/deprecation.md`) clause, or `null`. A page shows the same above the
construct's documentation: `Deprecated since 0.4: why. Write x() instead.` `format` is raised whenever a field changes meaning or goes away. It is the data a
language server's hover shows and the registry renders per package (RELEASE.md section 7.7).

### Doc tests

`--check` runs the examples under `# Examples` of every documented construct - the code a doc comment indents by four
spaces - as tests of the package, which is what CONCEPT's "examples are tests" asks:

| First line of the example | What `--check` does with it |
|---|---|
| (none) | Parses it, holds it to the formatter canon, type checks it, builds it natively and runs it |
| `// check` | Everything but running it: for an example that reads standard input, starts a process, ends the program or panics on purpose, or that the native back end cannot build yet |
| `// fragment` | Only lexes it: a signature or a shape that is not a program |
| `// skip <reason>` | Nothing; the reason is listed |

An example is checked as a script next to the file it documents, with that file's imports and its public names, so it
reaches what a user of the module reaches. An example that says what it prints with `// prints <line>` comments is held
to that output; every other one is held to running to its end. Every example that runs becomes one entry of **one**
native program - one C compile for all of them - whose output is cut back into one piece per example, the machinery
`torb docs check` uses for the `trb run` blocks of the documentation. A finding names the construct the example
belongs to. `--no-run` stops at the type check: for a machine without a C compiler, and for the registry, which
generates a package's pages without running any of its code.

### What `--check` does not fail on

The constructs the standard asks a doc comment of that have none are counted - `std/` has 218 of 2353 today - and the
count is printed, but they do not fail the check: until `std/` has none, the count is what shows the way there.

### How long a check takes

`torb doc --check --no-run std` checks that every link of a doc comment of std's public API resolves, and that every
example of it parses, is in the canon and type checks - about half a minute. The whole of `torb doc --check std`, which
also builds and runs the 156 examples that are programs, takes between one and two minutes, most of it the lowering and
the C compile of their one program.

## Examples

The reference of the standard library, opened from disk afterwards:

```console
$ torb doc std --output build/doc/std
33 packages, 102 modules: wrote 141 files to build/doc/std
```

The gate of a package's documentation:

```console
$ torb doc --check std
218 of 2353 public constructs have no doc comment
2541 constructs, 164 examples checked, 156 run, no problems
```

The model of one package, for a tool:

```console
$ torb doc std/json --json
{"format":1,"packages":[{"name":"std/json","page":"std/json/index.html", ...
```

## Related

- Doc comments (skill `torbscript-language`: `references/language/syntax/doc-comments.md`) - what `/** */` attaches to and the headings it uses.
- torb docs source - the gate of the doc comments of a whole tree, whose link rules `torb doc`
  shares.
- torb docs site - the website around the reference, which links to it and shares its look.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the hand-written pages of `std/`, which link to the generated
  reference.
- The torb command (skill `torbscript`: `references/tooling/the-torb-command.md`) - every subcommand.

