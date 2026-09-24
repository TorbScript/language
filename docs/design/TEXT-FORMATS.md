# YAML, Regular Expressions and Markdown

**Status: planned** — `std/yaml`, `std/regex` and `std/markdown` do not exist. The decisions below were made on
2026-09-21 and 2026-09-22, the scope of `std/yaml` was widened on 2026-09-24; this record is a stub that keeps them, and each package gets its full design before it is
built.

**Three text packages that the toolchain needs itself, in this order: `std/yaml`, then `std/regex`, then
`std/markdown`.** The documentation tool reads the front matter of every page and the Markdown around it with a
reader of its own (`compiler/src/documentation/markdown.trb`, deliberately not a Markdown parser); these packages
replace it, and the rest of `std`'s formats follow them in milestone 10 ([ROADMAP.md](../ROADMAP.md)).

## 1. `std/yaml`

- **A format package on `Encode` and `Decode`** ([ENCODING.md](ENCODING.md)), like `std/json`: a type reads from YAML
  and writes to it without a line of code.
- **All of YAML 1.2, and YAML 1.1.** Anchors, aliases, tags, several documents in one stream, block and flow styles,
  every scalar style and the directives. A document that says `%YAML 1.1` is read by 1.1's rules, including the merge
  key `<<` that CI definitions use; most files in circulation were written against 1.1. YAML 1.0 is not supported.
- **The target type resolves a scalar, not a schema.** Decoding into a type reads a plain scalar as what the field
  asks for: `no` is the text `"no"` in a `String` field and an error in a `Bool` field. That removes the
  surprises YAML is known for (the "Norway problem", where `no` reads as `false`) without restricting the language.
  The schemas of the specification - failsafe, JSON, core, and 1.1's types - are an option for reading a document
  without a type, into the tree: its scalars keep their text, their style and their tag until a schema resolves them.
- **Tags.** An explicit standard tag (`!!str`, `!!int`, `!!binary`, `!!timestamp`, ...) is honoured. A local
  tag such as `!Point` becomes a type only through a mapping the caller passes (tag to decoder), because there is no
  reflection; an unknown tag is an error that names it, or stays in the tree.
- **Aliases have a limit.** Expanding aliases counts the nodes it produces against a limit the caller can raise, so a
  document of nested aliases (the "billion laughs") is an error and not an out-of-memory panic.
- **Writing.** A value has no identity, so a typed value is written without anchors; the tree writes the anchors,
  aliases, tags and comments it holds, so a tool can change one value in a configuration file and keep the rest as it
  was. The writer emits YAML 1.2 and quotes every string a 1.1 reader would misread (`no`, `on`, `0777`), so its
  output reads the same under both versions.
- **It comes before `std/markdown`**, because front matter is YAML: `torb docs check` then decodes the front matter of
  a page into a type instead of reading it by hand.

## 2. `std/regex`

**A regular expression is a `Regex` value of a package, and the language gets no literal for it.**

- **No `/.../` literal**, as JavaScript has. `/` is division, and telling the two apart takes parser heuristics that
  the lexer, the highlighter, the formatter canon and the language server would all have to repeat. The language has no
  special literals for other values either (paths, durations and URIs are values), and raw strings (`raw"\d+"`) exist
  for exactly this text.
- **The check at compile time comes from the literal rule** of [URI.md](URI.md) section 9: a string *literal* whose
  expected type is `Regex` is compiled by the compiler where it is written, and an invalid pattern is a compile error at
  that line. A `String` value that becomes a `Regex` at run time answers a `Result`.
- **The engine is TorbScript, with the semantics of RE2**: linear time in the length of the input, and therefore no
  backreferences and no lookaround. It is Unicode aware, and it behaves identically in the VM and in a native binary,
  because both run the same code.
- **Named groups decode into a type** through `Decode`, so a match with the groups `year`, `month` and `day` becomes a
  value of a type with those three fields.

## 3. `std/markdown`

Slice 1 of [RELEASE.md](RELEASE.md) section 10 specifies it: CommonMark with tables and front matter, the document tree
as a value, a writer that round trips, HTML output, and the examples of the CommonMark specification as its test
suite. It is a *document* format: its model is the tree of blocks and inlines, as `XmlNode` is XML's in CONCEPT
("Types, Values and Reflection"), and no format gets a trait of its own. It can be read block by block from a
`Source`.

## 4. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `std/yaml`: the reader and the writer of YAML 1.2 and 1.1, the tree, `Encode`/`Decode` | nothing |
| 2 | `torb docs check` reads front matter through `std/yaml` | 1, and a seed refresh, because the compiler imports it |
| 3 | `std/regex`: the engine, `Regex.tryFrom(text)`, matches and groups | nothing |
| 4 | A `Regex` literal is compiled by the checker (URI.md section 9's table) | 3 |
| 5 | `std/markdown` (RELEASE.md slice 1) | 1 |
