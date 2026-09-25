# YAML, Regular Expressions and Markdown

**Status: slices 1, 2, 3 and 5 built (2026-09-25)** — `std/yaml` passes all 402 cases of the YAML test suite, and
`torb docs check` reads front matter through it; `std/regex` passes RE2's search tests; `std/markdown` passes all 652
examples of CommonMark 0.31.2 and GitHub's table examples. Sections 1a, 2a and 3a are what their implementations
decided. The `Regex` literal of slice 4 is not built. The decisions below were made on 2026-09-21 and 2026-09-22, the
scope of `std/yaml` was widened on 2026-09-24.

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

## 1a. What the implementation of `std/yaml` decided

- **A scanner after libyaml's, with the rules of YAML 1.2, and a recursive parser into the tree.** Indentation becomes
  block tokens, a simple key is found when its `:` arrives, and the parser builds `YamlNode`s directly - there is no
  event stream. The test suite's events are derived from the tree (`std/yaml/tests/events.trb`).
- **The test suite is the test suite.** `std/yaml/tests/suite-cases.trb` vendors the 402 cases of
  [yaml-test-suite](https://github.com/yaml/yaml-test-suite) (MIT, data release 2022-01-17) with their notice. The
  reader passes 402 of 402: every event stream, and every error case refused. Every one of the 308 valid cases written
  back and read again gives the same events; 4 of them change a scalar's style (a single-quoted scalar of several
  lines, `:` alone as a key), never a value. 243 of the 256 single-document cases with an `in.json` resolve to that
  JSON by the core schema; the other 13 carry tags this reader resolves on purpose (below).
- **Tabs follow the specification, not libyaml**: a tab is separation after the indentation and never indentation, so
  `-\t-` and a key after a tab that opens a mapping are errors, and a flow collection inside a block is indented deeper
  than its block.
- **The merge key `<<` is applied in every document**, not only in one that says `%YAML 1.1`: CI files use it and say no
  version. `Yaml(mergeKeys: false)` turns it off; a quoted `"<<"` is an ordinary key.
- **A typed decode is lenient where YAML is terse and strict where it is ambiguous.** An empty value is `None`, an
  empty collection or a record of defaults; a quoted scalar is a text except as a map key (JSON quotes every key); a
  whole floating point number reads into an `Int`; a `!!binary` scalar reads into a `List<UInt8>`.
- **Tags**: a standard tag is honoured, a local or global tag nobody named is an error that names it (`resolved`
  included - that is 10 of the 13 JSON differences), `!!set` resolves to a sequence of its keys, `!!omap` and
  `!!pairs` to a mapping in their order, and `!!binary` to bytes (the other three). `YamlTag(tag, typeName, caseName)` is
  the caller's mapping; `typeName<Type>()` does not exist yet, so the name is spelled by hand or read from an
  `EncodedValue`.
- **The alias limit counts the nodes that expanding adds**, 100000 by default: an alias adds the size of the node its
  anchor stands on. An alias inside the node its own anchor names is an error, so a document never expands forever.
- **Comments**: one on a line of its own belongs to the node below it - to the key of an entry, the item of a sequence
  - and one at the end of a line to the last node that ends on that line; a comment after `key:` whose value starts on
  the next line belongs to the key. What no node follows is the document's `endComments`.
- **The writer keeps every style it can and quotes the rest**: a plain scalar that would read differently, including a
  key (`"y":`), is double-quoted; a plain scalar of several lines stays plain where every line can be; a folded scalar
  with more-indented lines is written without folding them; an empty collection is `[]` or `{}`. A `%YAML 1.1`
  directive in a tree is written back as it is, because the tree's plain scalars mean what 1.1 says.
- **`Yaml.items` frames at the lines that start with `---` or `...`**, which YAML allows nowhere else at the start of a
  line, so the framer needs no parser; directives are carried to the document that follows them.

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

## 2a. What the implementation of `std/regex` decided

- **A Pike VM over characters.** A pattern is parsed into a tree, compiled into split, jump, save and assertion
  instructions, and run by advancing every thread one character at a time; a thread that reaches an instruction another
  thread reached at the same place is dropped. The work is the length of the text times the number of instructions,
  and threads are kept in the order of preference, which gives RE2's and Perl's leftmost-first match. Every position is
  a byte offset of the text; `\C`, which matches one byte, is refused.
- **RE2's search tests are the tests.** `std/regex/tests/search-cases.trb` vendors `re2-search.txt` (BSD 3-Clause, with
  RE2's notice), in the copy the Go distribution keeps. For each pattern and text the first match of the whole text and
  the first match anywhere, with every group's offsets: 1808 of 1808 checks pass. The 80 checks whose pattern uses
  `\C` are left out, as Go leaves them out, and the longest-match columns are not read.
- **`x*` of an `x` that can match nothing is compiled as `(x+)?`**, as Go does since golang.org/issue/46123: otherwise
  the empty iteration comes back to the loop, is dropped there and loses the preference it has.
- **Unicode as far as the tables reach.** `\d`, `\w`, `\s` and `\b` are ASCII, as in RE2. `(?i)` folds ASCII, Latin-1,
  Latin Extended-A, Greek and Cyrillic, with the Kelvin sign, the long s, the Ångström sign, the micro sign and the final
  sigma. `\p{...}` knows `Any`, `L`, `Lu`, `Ll`, `N`, `Nd`, `Z`, `Zs`, `Latin`, `Greek` and `Cyrillic` from tables in
  TorbScript, and refuses any other name; the full tables are the Unicode natives of milestone 8.
- **`findAll`, `replace` and `split` follow Go's regexp**: an empty match right after the previous match does not count,
  a replacement names groups as `$1`, `${1}`, `$name`, `${name}` and `$$`, and a split makes no empty first piece of an
  empty match at the start.
- **Named groups decode through `ValueDecoder`**, lenient, so a group's text is read as whatever its field asks for, and
  a group that took no part is an absent field. A `Regex` is a capsule whose conversion pair is its text, so it is
  written and read as its pattern in every format.
- **The literal of slice 4 is not built.** It is URI.md section 9's rule, and it waits for the checker's literal
  parameter kind, as the `Uri` literal does.

## 3. `std/markdown`

Slice 1 of [RELEASE.md](RELEASE.md) section 10 specifies it: CommonMark with tables and front matter, the document tree
as a value, a writer that round trips, HTML output, and the examples of the CommonMark specification as its test
suite. It is a *document* format: its model is the tree of blocks and inlines, as `XmlNode` is XML's in CONCEPT
("Types, Values and Reflection"), and no format gets a trait of its own. It can be read block by block from a
`Source`.

## 3a. What the implementation of `std/markdown` decided

- **The algorithms of the specification's appendix, as commonmark.js implements them**: a block parser that continues
  the open blocks line by line, then tries the starts of new ones, then continues a paragraph lazily; an inline parser
  with a delimiter stack for emphasis and a bracket stack for links. Both keep their nodes in a list and link them by
  index while they move them around; the tree of values is built at the end.
- **The specification is the test suite.** `std/markdown/tests/spec-examples.trb` vendors the 652 examples of CommonMark
  0.31.2 and the 8 examples of GitHub's table extension (CC BY-SA 4.0, with the notice). All 652 render as specified
  with tables off and with them on, all 8 table examples pass, and all 652 written back by the writer and read again
  render as the same HTML.
- **Tables after GitHub**: the last line of a paragraph is the header row when the next line is a delimiter row with a
  `|` and as many cells; a row is every following line up to a blank one or the start of another block; a row has as
  many cells as the header, and `\|` is a pipe in a cell, in a code span too.
- **Front matter** is a first line `---` up to the next line `---` or `...`. It stays text in the tree, and
  `FrontMatter.document()` reads it through `std/yaml` with the lines of the whole file.
- **Positions**: every block has its first and last line and every link and image its line, counted in the file.
- **The named references are the WHATWG's table**, all 2125 names that end in `;`, generated into
  `std/markdown/src/entities.trb`. Unicode punctuation (the categories P and S) and the case folding of link labels
  are TorbScript tables over the blocks the examples reach, until the Unicode natives of milestone 8.
- **The writer writes one form for each construct** - `*` and `**` (`_` for emphasis right inside emphasis), fenced
  code with a fence longer than any run in it, `-` and `1.` lists, ATX headings and setext only for a heading of several
  lines - and escapes every character of a text that could be read as markup; a line break inside a text and
  whitespace a line would lose at its start become numeric references.

## 4. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `std/yaml`: the reader and the writer of YAML 1.2 and 1.1, the tree, `Encode`/`Decode` - **built** | nothing |
| 2 | `torb docs check` reads front matter through `std/yaml` - **built**; the seed has to be refreshed at slice 1's commit or later first, because the compiler imports `std/yaml` | 1, and a seed refresh, because the compiler imports it |
| 3 | `std/regex`: the engine, `Regex.tryFrom(text)`, matches and groups - **built** | nothing |
| 4 | A `Regex` literal is compiled by the checker (URI.md section 9's table) - waits for the checker's literal parameter kind | 3 |
| 5 | `std/markdown` (RELEASE.md slice 1) - **built** | 1 |
