---
title: std/yaml
summary: Yaml reads and writes any Encode/Decode type as YAML 1.2 or 1.1, with the target type resolving every scalar, and YamlNode is the tree of a document with its anchors, tags, styles and comments.
kind: package
status: stable
order: 111
keywords:
  - std/yaml
  - Yaml
  - YamlNode
  - YamlDocument
  - YamlError
  - YamlSchema
  - YamlTag
  - YAML 1.1
  - merge key
  - anchors
  - aliases
  - front matter
  - Norway problem
source:
  - std/yaml/src/lib.trb
  - std/yaml/src/yaml.trb
  - std/yaml/src/node.trb
  - std/yaml/src/schema.trb
  - std/yaml/src/resolve.trb
  - std/yaml/src/writer.trb
  - docs/design/TEXT-FORMATS.md
---

`std/yaml` is YAML as a format of [std/encoding](encoding.md), written in TorbScript: all of YAML 1.2, and YAML 1.1
for a document that says `%YAML 1.1`. `Yaml` reads a document into any `Decode` type and writes any `Encode` value,
and `YamlNode` is the tree of a document as it was written - anchors, aliases, tags, scalar and collection styles and
comments included - so a tool can change one value of a configuration file and write the rest back as it was. The
reader passes all 402 cases of the [YAML test suite](https://github.com/yaml/yaml-test-suite), which are its tests. The
design is TEXT-FORMATS.md section 1.

## Import

```trb fragment
use Yaml, YamlVersion, YamlError, YamlSchema, YamlTag from "std/yaml"
use YamlNode, YamlContent, YamlEntry, YamlDocument, YamlComments, ScalarStyle, CollectionStyle from "std/yaml"
```

```trb check
use Yaml from "std/yaml"

type Service {
  name: String
  replicas: Int = 1
  ports: List<Int> = []
  enabled: Bool = true
}

const yaml = Yaml()
match yaml.decode<Service>("name: web\nports: [80, 443]\nenabled: no\n") {
  Ok(service) => print yaml.encode(service)
  Fail(problem) => print "Rejected: {problem}"
}
```

`enabled: no` is refused there: `no` is a text, and a `Bool` field wants `true` or `false`. In a `String` field the
same scalar is the text `"no"`.

## Declarations

### Yaml

```trb fragment
public type Yaml {
  naming: Naming = Naming.Unchanged
  strict: Bool = false
  schema: YamlSchema? = None
  version: YamlVersion = YamlVersion.Yaml12
  tags: List<YamlTag> = []
  aliasLimit: Int = 100000
  mergeKeys: Bool = true

  fn encode<Value: Encode>(value: Value): String
  fn decode<Value: Decode>(text: String): Result<Value, YamlError>
  fn decodeDocuments<Value: Decode>(text: String): Result<List<Value>, YamlError>
  fn decodeDocument<Value: Decode>(document: YamlDocument): Result<Value, YamlError>
  fn parse(text: String): Result<YamlDocument, YamlError>
  fn parseAll(text: String): Result<List<YamlDocument>, YamlError>
  fn resolved(document: YamlDocument): Result<EncodedValue, YamlError>
  fn value<Value: Encode>(value: Value): YamlNode
  fn write(document: YamlDocument): String
  fn writeAll(documents: List<YamlDocument>): String
}
extend Yaml with Format<YamlError>
```

A `Yaml` is the format with its options. `naming` spells every field name and `strict` makes a field of the document
that no type asked for an error, as they do for [std/json](json.md). `version` is the version of a document without a
`%YAML` directive; a document that has one is read by it. `tags` are the local tags a typed decode accepts and a typed
encode writes. `aliasLimit` is how many nodes the aliases of one document may add - the "billion laughs", nine levels
of ten aliases each, fails there instead of filling the memory. `mergeKeys` applies the merge key `<<`, which CI
definitions use; it is applied in every document, because most of them say no version.

**The target type resolves a scalar, not a schema.** `decode<Value>` reads a plain scalar as what the field asks for:
`no`, `on` and `0777` are texts in a `String` field, `no` is an error in a `Bool` field, and `0777` is 777 in an `Int`
field. A document that says `%YAML 1.1` (or a `Yaml(version: .Yaml11)`) reads by 1.1's rules: `yes`, `no`, `on` and
`off` are truth values there, `0777` is octal and `1_000` a thousand. A quoted scalar is always a text, and an explicit
tag of the core schema is honoured (`!!int "5"` is a number). An empty value is `None` for an optional field, an empty
list or map for a collection, and a record of defaults for a record.

A record is a mapping of its fields, a record that announces no field is its one value, a case without fields is its
name and a case with fields a mapping of one entry, its name (`Circle: {radius: 2}`); a `YamlTag` with a `caseName`
lets a local tag choose the case instead (`!circle {radius: 2}`). A `!!binary` scalar reads into a `List<UInt8>`.

`decode` reads a text of one document - several are an error that names `decodeDocuments`, and a text without one reads
as null. A failure is a `YamlError` with the line and the field chain:

```text
line 4: ports[1]: a whole number is needed, not `http`
```

`decodeDocument` reads a document that was parsed already, which is what a front matter reader does.

`encode` writes YAML 1.2, one field per line, indented by two spaces. **A string is quoted where a reader would take it
for something else** - by YAML 1.2's core schema or by YAML 1.1's types: `no`, `on`, `y`, `0777`, `1_000`, `12:30`,
`2024-01-01`, `~`, `<<` and every number are written in double quotes, so what `encode` writes reads the same under
both versions. That includes keys: a field `y` is written `"y":`. A text of several lines is a literal block (`|`).

`parse` and `parseAll` read the tree; `write` and `writeAll` write it back with everything it holds. `resolved` reads a
document without a type into an `EncodedValue`, its plain scalars resolved by `schema` or, where that is `None`, by the
document's version. `value` is the tree an `Encode` value is written as.

`extend Yaml with Format<YamlError>` gives `Yaml.items<Item>()`, a `Stage` that frames a stream of bytes into its
documents - at the lines that start with `---` or `...`, which YAML allows nowhere else - and decodes each one, and
`Yaml.encoded<Item>()`, which writes each item as a document that starts with `---`.

### YamlNode

```trb fragment
public type YamlNode {
  content: YamlContent
  tag: String? = None
  anchor: String? = None
  comments: YamlComments = YamlComments()
  line: Int = 0
  column: Int = 0

  static fn scalar(text: String, style: ScalarStyle = ScalarStyle.Plain): YamlNode
  static fn sequence(items: List<YamlNode>, style: CollectionStyle = CollectionStyle.Block): YamlNode
  static fn mapping(entries: List<YamlEntry>, style: CollectionStyle = CollectionStyle.Block): YamlNode
  static fn alias(name: String): YamlNode
  static fn empty(): YamlNode
  fn text(): String?
  fn items(): List<YamlNode>
  fn entries(): List<YamlEntry>
  fn isScalar(): Bool
  fn at(key: String): YamlNode?
  fn updated(key: String, value: YamlNode): YamlNode
}

public type YamlContent {
  case Scalar(text: String, style: ScalarStyle)
  case Sequence(items: List<YamlNode>, style: CollectionStyle)
  case Mapping(entries: List<YamlEntry>, style: CollectionStyle)
  case Alias(name: String)
}

public type YamlEntry {
  key: YamlNode
  value: YamlNode
}

public type YamlComments {
  leading: List<String> = []
  trailing: String? = None
}
```

One node as it was written. A scalar keeps its text - after quotes, escapes and folding - and its style, and is not
resolved: `yes` is the text `"yes"` until a schema or a type says what it is. `tag` is resolved to its full name
(`tag:yaml.org,2002:str` for `!!str`, `!point` for a local tag, `!` for the non-specific one), and `line` and `column`
count from 1. A comment on a line of its own belongs to the node below it - the key of an entry, the item of a sequence
- and a comment at the end of a line to the last node that ends there: `name: web # the name` is the value's.

`updated(key, value)` answers the mapping with one value replaced and everything else - the other entries, their order,
the comments - as it was, which is how a tool edits a file.

`ScalarStyle` is `Plain`, `SingleQuoted`, `DoubleQuoted`, `Literal` or `Folded`; `CollectionStyle` is `Block` or
`Flow`. The writer keeps a style wherever the text allows it, and quotes where it does not.

### YamlDocument

```trb fragment
public type YamlDocument {
  root: YamlNode
  version: String? = None
  tagDirectives: List<YamlTagDirective> = []
  explicitStart: Bool = false
  explicitEnd: Bool = false
  leadingComments: List<String> = []
  endComments: List<String> = []

  fn isVersion11(): Bool
}

public type YamlTagDirective {
  handle: String
  prefix: String
}
```

One document of a stream: its root, its `%YAML` and `%TAG` directives, whether it was opened with `---` and closed
with `...`, and the comments before and after its root.

### YamlSchema and YamlVersion

```trb fragment
public type YamlSchema {
  case Failsafe
  case Json
  case Core
  case Yaml11
}

public type YamlVersion {
  case Yaml12
  case Yaml11
}
```

The schemas of the specification, for `resolved`: `Failsafe` reads every scalar as a text, `Json` reads `null`, `true`,
`false` and JSON's numbers and refuses every other plain scalar, `Core` is YAML 1.2's (null in four spellings, truth
values in three, decimal, `0o` and `0x` numbers, `.inf` and `.nan`), and `Yaml11` adds 1.1's truth values, `0b` and `0`
prefixes, `_` between digits and base 60. A timestamp stays a text in every schema. `!!set` resolves to a sequence of
its keys, `!!omap` and `!!pairs` to a mapping in their order, and `!!binary` to bytes.

### YamlTag

```trb fragment
public type YamlTag {
  tag: String
  typeName: String
  caseName: String? = None
}
```

A local tag and the type it stands for, by the qualified name the type announces to a format (`"app/geometry/Point"`).
A program has no reflection, so `!point` means a type only through this mapping: a typed decode accepts a node tagged
`!point` where that type is read, and refuses a tag nobody named with an error that names it; a typed encode writes the
tag. With `caseName`, the tag chooses a case of a type with cases. `resolved` reads a mapping with a known local tag as
a `Record` of that type name.

### YamlError

```trb fragment
public type YamlError with Show, Error {
  static fn syntax(line: Int, column: Int, message: String): YamlError
  static fn content(line: Int, message: String): YamlError
  static fn decodeFailed(cause: DecodeError, line: Int = 0): YamlError
  static fn invalidText(cause: Utf8Error): YamlError
  fn isSyntaxError(): Bool
  fn line(): Int
  fn message(): String
  fn cause(): Error?
}
```

What went wrong: text that is not YAML (`syntax`, with its line and column), a document whose content cannot be
resolved - an unknown tag, a merge key on something else than a mapping, aliases beyond the limit (`content`) - a
document that does not fit the type (`decodeFailed`, with the line of the node it failed on), or bytes that are not
UTF-8.

`YamlDecoder` and `YamlEncoder` are the `Decoder` and `Encoder` behind `decode` and `encode`, exported for a format that
builds on them.

## Related

- [std/encoding](encoding.md) - `Encode`, `Decode`, `Naming` and `Format`, which `Yaml` implements.
- [std/json](json.md) - the same shape for JSON.
- [The standard library](index.md) - the other packages.

