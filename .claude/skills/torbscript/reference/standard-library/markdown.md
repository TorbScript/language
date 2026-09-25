---
title: std/markdown
summary: Markdown reads CommonMark with GitHub's tables and front matter into a document tree that is a value, with the lines of every block and link; HTML and Markdown are written from the tree.
kind: package
status: stable
order: 113
keywords:
  - std/markdown
  - Markdown
  - MarkdownDocument
  - MarkdownBlock
  - MarkdownInline
  - CommonMark
  - GitHub Flavored Markdown
  - tables
  - front matter
  - HTML
source:
  - std/markdown/src/lib.trb
  - std/markdown/src/markdown.trb
  - std/markdown/src/tree.trb
  - std/markdown/src/blocks.trb
  - std/markdown/src/inlines.trb
  - std/markdown/src/html.trb
  - std/markdown/src/writer.trb
  - docs/design/TEXT-FORMATS.md
  - docs/design/RELEASE.md
---

`std/markdown` is Markdown as a document format, written in TorbScript: CommonMark 0.31.2 with the tables of GitHub
Flavored Markdown and front matter. `Markdown().parse(text)` reads a text into a `MarkdownDocument`, a tree of blocks
and inlines that is a value - every text is a Markdown document, so reading never fails. HTML is a separate step over
that tree, and so is Markdown: what the writer writes reads back as the same document. All 652 examples of the
CommonMark specification and the 8 examples of GitHub's table extension are its tests, and every one of them passes -
read, rendered, and written back. The design is TEXT-FORMATS.md section 3 and slice 1 of
RELEASE.md.

## Import

```trb fragment
use Markdown, MarkdownDocument, FrontMatter, MarkdownBlock, BlockContent, MarkdownList, ListItem from "std/markdown"
use MarkdownTable, ColumnAlignment, MarkdownInline, plainText, htmlOf, markdownOf from "std/markdown"
```

```trb check
use Markdown, plainText from "std/markdown"

const document = Markdown().parse("# Values\n\nA *binding* holds [a value](values.md).\n")
for block in document.blocks {
  if const .Heading(level, inlines) = block.content {
    print "heading {level} on line {block.line}: {plainText(inlines)}"
  }
}
print document.html()
```

## Declarations

### Markdown

```trb fragment
public type Markdown {
  frontMatter: Bool = true
  tables: Bool = true

  fn parse(text: String): MarkdownDocument
  fn html(text: String): String
}
```

The reader with its options. `frontMatter` reads a first line `---`, up to the next line `---` or `...`, as front
matter instead of a thematic break; `tables` reads a paragraph's last line followed by a delimiter row
(`| --- | :-: |`) as the header of a table. With both off the reader is CommonMark as the specification has it; with
them on, a text without front matter and without a table reads exactly the same.

### MarkdownDocument and FrontMatter

```trb fragment
public type MarkdownDocument {
  blocks: List<MarkdownBlock>
  frontMatter: FrontMatter? = None

  fn html(): String
  fn markdown(): String
}

public type FrontMatter {
  text: String
  line: Int

  fn document(): Result<YamlDocument, YamlError>
}
```

A document: its blocks, and its front matter. `FrontMatter.document()` reads the front matter through
[std/yaml](yaml.md), with the lines of the Markdown file, so a problem in it names the line of the page. `html()` is
the HTML of the blocks - the front matter is not part of it - and `markdown()` the document written back as Markdown.

### MarkdownBlock and BlockContent

```trb fragment
public type MarkdownBlock {
  content: BlockContent
  line: Int = 0
  endLine: Int = 0
}

public type BlockContent {
  case Paragraph(inlines: List<MarkdownInline>)
  case Heading(level: Int, inlines: List<MarkdownInline>)
  case ThematicBreak
  case CodeBlock(info: String, text: String, fenced: Bool)
  case HtmlBlock(html: String)
  case BlockQuote(blocks: List<MarkdownBlock>)
  case ItemList(list: MarkdownList)
  case Table(table: MarkdownTable)
}

public type MarkdownList {
  ordered: Bool
  marker: String
  start: Int
  tight: Bool
  items: List<ListItem>
}

public type ListItem {
  blocks: List<MarkdownBlock>
  line: Int = 0
}

public type MarkdownTable {
  alignments: List<ColumnAlignment>
  header: List<List<MarkdownInline>>
  rows: List<List<List<MarkdownInline>>>
}

public type ColumnAlignment {
  case Unspecified
  case Left
  case Center
  case Right
}
```

A block, and the lines of the file it spans, counted from 1 - front matter included, so a tool that checks a page can
name the line of what it found. A code block keeps its info string (`trb check`) and its text as it was written,
without the fence; an indented one has an empty info string. A list's `marker` is its bullet or the delimiter after
its numbers, and a tight list is one whose paragraphs HTML writes without `<p>`. A table row has as many cells as its
header: a missing cell is empty, and an extra one is dropped, as GitHub does.

### MarkdownInline

```trb fragment
public type MarkdownInline {
  case Text(text: String)
  case Code(text: String)
  case Emphasis(children: List<MarkdownInline>)
  case Strong(children: List<MarkdownInline>)
  case Link(destination: String, title: String, children: List<MarkdownInline>, line: Int)
  case Image(destination: String, title: String, children: List<MarkdownInline>, line: Int)
  case Html(html: String)
  case SoftBreak
  case HardBreak
}

public fn plainText(inlines: List<MarkdownInline>): String
```

What a paragraph, a heading or a cell holds, with escapes and entity references resolved: a text is the text a reader
sees. A link's destination is as it was written, resolved through a reference definition where the link is a
reference; it is percent-encoded only when HTML is written. `line` is the line a link or an image starts on.
`plainText` is inlines without their markup - the `alt` of an image, the text of a heading an anchor is made of.

### `htmlOf` and `markdownOf`

```trb fragment
public fn htmlOf(document: MarkdownDocument): String
public fn markdownOf(document: MarkdownDocument): String
```

The two writers, which `html()` and `markdown()` call. `htmlOf` writes the HTML the specification's examples expect.
`markdownOf` writes one form for every construct - `*` and `**`, fenced code, `-` and `1.` lists, ATX headings where a
heading fits on one line - and escapes every character of a text that could be read as markup, so reading what it
writes gives the same document.

## Related

- [std/yaml](yaml.md) - what front matter is read by.
- [The standard library](index.md) - the other packages.

