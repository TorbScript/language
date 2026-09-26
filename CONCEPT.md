# TorbScript Concept

> The language is called TorbScript, its CLI is `torb`, source files end in `.trb`.

## Key Facts

- Functional-first, multi-paradigm scripting language. Not Haskell-style purity, but the functional principles are
  kept clean: immutable by default, expressions over statements, values over identity, mutation always explicitly marked.
- C-style syntax with elements taken from Scala, Kotlin, Groovy, Rust, TypeScript, Swift
- Value semantics: every type is a value, the binding (`const`/`var`) decides whether it can be changed.
  No `Point`/`MutablePoint`, no `List`/`MutableList`. Identity is the marked exception (`shared type`)
- Strongly, statically typed with local (bidirectional) type inference
- First-class and higher-order functions
- Algebraic data types and pattern matching
- No `null`, no exceptions: `Option`, `Result`, `?`
- Usable as its own configuration/DSL format (Groovy/Scala style), statically typed and sandboxed
- The language extends itself through functions, not through annotations or AST macros
- Runs interpreted (no compile step) _and_ compiles to a native executable, with identical semantics
- Convention over Configuration
- Single-binary toolchain (runtime, compiler, package manager, test framework, linter, formatter, LSP)

## Design Principles

1. **The parameter type decides how an argument is read.** A closure passed to a parameter of type
   `(self: Receiver) => Value` resolves names against `Receiver`. An expression passed to `lazy Value` is not
   evaluated at the call site. This one principle powers DSLs, custom control structures and query providers,
   without macros or annotations.
2. **Mutation is always visible.** `var` bindings, `var` fields (`private(var)`: only the type itself), `var` parameters,
   `var fn` methods. Everything not marked does not change. Values are never aliased, so a mutation happens
   exactly where it is written and nowhere else.
3. **One way to construct, many ways to create.** Constructors only initialize fields and never contain logic.
   Validation, parsing and conversion live in static factory functions (`Email.tryFrom`, `From`/`Into`).
4. **No whitespace-sensitive parsing.** Whitespace never changes the meaning of a token sequence
   (newlines end statements, that is all).
5. **Nothing observable may differ between interpreter and compiled binary.** Features that cannot be
   implemented identically in both are not part of the language.

## Toolchain

```text
torb run [file]     # Run src/main.trb (or a single script file) directly
torb build          # Build a native executable
torb test           # Run tests/
torb format         # Formatter
torb lint           # Linter
torb doc            # Documentation from the doc comments
torb repl           # Interactive session
torb add <package>  # Add a dependency
```

Resolved dependency versions are written to `project.lock.trb` (a `.trb` file like every other configuration) and
belong into version control.

### Project Layout

```text
my-project/
├ src/
├─ main.trb      # Entry point for execution (may contain top-level code)
├─ lib.trb       # Entry point for import (public surface of a library)
├ tests/
├─ *.test.trb
├ project.trb
└ README.md
```

### project.trb

`project.trb` contains configuration and metadata of the project. It is a regular TorbScript file, executed as a
_receiver script_ (see [Configuration DSL](#configuration-dsl)) against the built-in `Project` type, inside a sandbox
without any IO capability. It is deterministic, so tools can evaluate it safely and cache the result.

```trb
name "my-project"
version "0.1.0"
authors "Author Name <author@example.com>",
  "Another Author <another@example.com>"
dependencies {
  runtime "acme/http:^1.2.3"
  development "acme/mock-server:^3.4.5"    // Tests and tools. Never part of what dependents get.
}

const binary = name.substringAfter("/") ?? name

build {
  target "dev"
  input "src/main.trb"
  output "build/{target}/{binary}"         // Eager interpolation: `target` was set one line above
}
test {
  input "tests"
  coverageThreshold 80
}
```

This is nothing but sugar-free TorbScript. Fully written out it is:

```trb
// The file body is a closure of type `(var self: Project) => Void`
self.name = "my-project"                   // `name` and `version` are `var` fields of Project
self.version = "0.1.0"
self.authors("Author Name <author@example.com>", "Another Author <another@example.com>")   // Variadic method

const configureDependencies: (var self: Dependencies) => Void = { dependencies =>
  dependencies.runtime("acme/http:^1.2.3")         // `runtime` and `development` are methods of Dependencies
}
configureDependencies(self.dependencies)   // `dependencies` is a field, configured in place

const configureBuild: (var self: Build) => Void = { build =>
  build.target = "dev"
  build.output = "build/{build.target}/{binary}"
}
configureBuild(self.build)
```

(`binary` is read before the block, because only the innermost receiver is implicit - see
[Configuration DSL](#configuration-dsl). A `Build` is a value and does not know the project it belongs to.)

### Workspaces

A project can consist of several projects. The root names its members, every member is an ordinary project with a
`project.trb` of its own:

```trb
name "acme/shop"
version "1.4.0"

workspace {
  members "packages/*", "tools/importer"
}
```

```text
shop/
├ packages/
├─ core/            name "acme/shop-core"
├─ api/             name "acme/shop-api", dependencies { runtime "acme/shop-core" }
├ tools/
├─ importer/
├ project.trb       the workspace
└ project.lock.trb  one lock file for all of them
```

- A dependency whose name is a member of the workspace is that member, from source. It needs no version inside of
  the workspace; publishing a member writes the current versions of its siblings into what is published.
- There is one `project.lock.trb`, at the root. All members share one resolution, so they cannot drift apart.
- Members inherit `version`, `authors` and the registries of the root unless they set their own.
- `torb build`, `test` and `check` at the root work on all members, in the order of their dependencies
  (`torb test packages/api` for one). Cycles between members are an error. The root may have sources of its own, or
  be nothing but the list of members.
- The toolchain is a workspace itself: `std/*`, `compiler`, `examples/*`.

### Packages and the Supply Chain

Simple to use like npm, strict like Maven. The rules exist so that adding a dependency is never a leap of faith:

- **Names are `owner/name`.** Owners are verified namespaces of a registry. A project binds owners to registries
  (`registry "acme", url: "https://packages.acme.test"`), so a public package can never take the place of a private
  one (no dependency confusion, no typo squatting on bare names).
- **Published versions are immutable.** A version can be withdrawn for new resolutions, never replaced or deleted.
- **`project.lock.trb` pins the whole graph:** exact version, content hash and registry of every package, direct or
  transitive. `torb run`, `build` and `test` never change it and fail if it does not match `project.trb`;
  only `torb add`, `torb remove` and `torb update` write it. Hashes are verified on every install.
- **Resolution:** semantic versions, `^` by default, the highest compatible version, one version of a package per
  major version in a graph.
- **Installing never runs code.** There are no install scripts and no build scripts. `project.trb` is evaluated in a
  sandbox without IO, so reading the metadata of a package is safe, too.
- **Capabilities are visible.** There is no reflection, no `eval` and no dynamic import, so the compiler knows from
  the imports alone what a package can touch: file system, network, processes, environment, foreign functions.
  `torb add` shows that (for the package and everything below it), the lock file records it, and an update that
  gains a capability needs an explicit confirmation.
- `torb audit` checks the locked graph against the advisory database of the registry.
- There are no optional or suggested dependencies and no feature flags. An optional integration is a package of its
  own (`acme/http`, `acme/http-json`).

## Lexical Structure

```trb
// Line comment
/* Block comment. It ends at the first star-slash: block comments do not nest. */
/** Doc comment, attached to the following declaration. Markdown. */
```

- Statements end at the end of the line. A statement continues on the next line if the current line ends with an
  operator, `,` or an open bracket, or if the next line starts with `.`, `?.`, a binary operator, `with` or `where`.
  There are no semicolons.
- **A name is ASCII**: `[A-Za-z_][A-Za-z0-9_]*`. A letter from another script where a name could stand is one lexer
  error for the whole word ("A name is written in ASCII letters, digits and `_`"). Strings, char literals, comments and
  doc comments are full Unicode - only names are limited, so an identifier is the same text in every editor, every
  terminal and every back end, and there is no normalization question to answer.
- **How a name is spelled is grammar, not convention.** "Uppercase" is `A` to `Z`; a name that starts with `_` or `a`
  to `z` is lowercase. `UpperCamelCase` for a `type`, a `shared type`, a `trait`, a `case`, a type parameter (a size
  parameter like `Size` too), a type alias and an `as` alias of one of those; `lowerCamelCase` for everything else - a
  `fn`, a field, a parameter, a tuple label, a `const`/`var`, a module constant, a closure parameter, a module alias
  and an `as` alias of a function or a constant. **There is no MACRO_CASE**: a module constant is `maxSize`, never
  `MAX_SIZE`. The checker reports it once per declaration, at the name ("A type starts with an uppercase letter: write
  `Point`"). The reason it is a rule and not a lint: the first letter of a name in a pattern already decides whether
  the pattern binds or names a case, so it has to mean the same thing at every declaration. A pattern binding needs no
  check of its own for exactly that reason - an uppercase name there *is* a case.
- Keywords are reserved, except where they cannot be confused: after a `.` and as argument labels they are ordinary
  names (`query.where { ... }`, `move(from: a, to: b)`). A parameter cannot be named like a keyword (it would be
  unusable inside of the function). `from`, `as` and `by` are contextual and not reserved at all.
- A `{` at the start of a line never continues the line above: a closure on its own line is a value (the result of a
  function, for example). Only the body of a `type`, `trait` or `extend` may start on its own line, after a long
  `with ...` or `where ...`.
- **Names are written out.** `Expression`, `Subtract`, `Remainder`, `absolute`, `squareRoot` - not `Expr`, `Sub`,
  `Rem`, `abs`, `sqrt`. Abbreviations are only fine where the abbreviation _is_ the name people know
  (`Html`, `Json`, `Sql`, `Http`, `Int64`, `Bool`, `Char`, `min`/`max`). This holds for the standard library, and is a
  linter hint for user code. Type parameters are written out, too: `List<Item>`, `Map<Key, Value>`,
  `Result<Value, Failure>`, `fn map<Output>(...)` - not `T`, `K`, `V`, `E`, `U`.
- **Verbs change, participles return.** A method that changes its receiver in place is a verb and is a `var fn`
  (`append`, `remove`, `sort`, `translate`). The method that returns a changed copy instead is its participle (`appended`,
  `removed`, `sorted`, `translated`). Pick verbs whose participle is a different word: the standard library avoids
  `put`, `cut`, `reset` as mutators, and `set` has the counterpart `updated`. Nouns never change
  anything (`union`, `intersection`). Tooling does not offer verbs on a const path, and the compiler error names the
  participle ("`append` needs a `var`. Did you mean `appended`?").
- **A `Bool` is an adjective, a question is a method.** A `Bool` field, parameter or binding is an adjective or a
  participle (`inclusive`, `discarded`, `signed`, `retryable`); a question that is *computed* is a method whose name
  may start with `is` or `has` (`isEmpty()`, `hasGuard()`, `isRetryable()`). So `range.inclusive` is data and
  `list.isEmpty()` is work, and a reader sees which is which without guessing where the parentheses are. It follows
  from the same rule as the verbs: a field is a promise about data, and what could ever be computed is a method from
  the start (see [Visibility and Encapsulation](#visibility-and-encapsulation)).
- Generics vs. comparison (`load<Config>(path)` vs. `a < b`) is decided purely syntactically, without knowing what
  the names mean (the C#/Kotlin/TypeScript approach): after a name, `<` starts a type argument list if the tokens up
  to the matching `>` form valid types **and** the token after `>` is one of `(` `.` `{` `)` `]` `,` `:` or the end
  of the line. Otherwise it is "less than". Comparison operators are non-associative (`a < b > c` is never a valid
  comparison), so the generic reading never steals a meaningful expression. In type positions (after `:`, `with`,
  `where`, ...) there is no ambiguity to begin with.
- Number literals: `10`, `1_000_000`, `0xFF`, `0b1010`, `3.14`, `1e9`
- String literals: `"text"`, interpolation with `{expr}`, literal brace with `\{`. Multi-line strings with `"""`, dedented by
  the indentation of their first line (see "Strings").
  Raw strings (`raw"..."`, `raw"""..."""`) have no interpolation and no escapes (JSON, regular expressions, paths).
- Char literals: `'A'`

### Doc Comments

A doc comment belongs to the declaration that follows it. **Everything that is declared can have one - parameters,
fields and cases included** - so there is no tag language that repeats names (`@param host`) and goes stale:

```trb
/**
 * Connects to a database.
 *
 * # Panics
 * If `timeout` is negative.
 *
 * # Examples
 *     const connection = connect("localhost", timeout: 5)?
 *     assert(connection.isOpen())
 */
public fn connect(
  /** Host name or address */
  host: String,
  /** Seconds to wait before giving up */
  timeout: Int = 30,
): Result<Connection, IoError> { ... }

type Shape {
  /** A circle around the origin */
  case Circle(/** Always positive */ radius: Float)
}
```

- The text is Markdown. There are no annotations and no second syntax inside of comments.
- **The first sentence says what the construct does, the sentences after it say what it is for** - when a reader reaches
  for it. Everything is in the present tense and about the code as it is.
- Six headings carry what tags carry elsewhere, and there are no others: `# Examples`, `# Errors`, `# Panics`,
  `# Pitfalls`, `# Open` (a problem that is open, stated in the present tense), `# Related` (links). The return value is
  described in the text.
- **A file starts with a module comment**: a doc comment at the top, in front of the first `use`, that says what the
  module is for, which constructs are its main ones and how they relate.
- **A comment never tells the history of its own code.** What something used to be, what changed and which plan a
  change belonged to is in the log, not at the declaration.
- **Examples are tests.** `torb test` compiles and runs the code under `# Examples`, so documentation cannot rot.
- `[List.append]` and `[Option]` are links. They are resolved like names in the code at that place; a link that does not
  resolve is a warning.
- The doc comment is part of the syntax tree. The language server, `torb doc` and the test runner read the same data.
- A `//` comment inside a body is the rare exception, for a step that is not obvious from the code. What a caller or a
  reader of the construct has to know belongs in the doc comment.

## Bindings

```trb
const y = 30        // Immutable binding
var x = 20          // Mutable binding
const z: Float = 1  // Optional type annotation; literals adapt to the expected type

var list = [1, 2]   // The binding decides about the value, too: `list.append(3)` works, ...
const fixed = list  // ...`fixed.append(3)` does not. `const` is deep, and `fixed` is a copy: it never changes.

var a               // Compile error: bindings must be initialized
var b: Int          // Compile error: no implied default value
```

- A name can be shadowed in a nested scope, but not redeclared in the same scope. **The parameters of a function and
  the top level of its body are one scope**, so a `const` there may not take a parameter's name: there is no silent
  shadowing anywhere in the language, and a parameter is the most surprising place for it. A nested block and a closure
  are scopes of their own.
- **Changes that cannot have an effect are compile errors,** because with value semantics they are always a mistake:
  a `var` that is changed but never read afterwards (`var first = list[0]` followed by `first.increment()` - the
  message points to `list[0].increment()`), and the discarded result of a method that only reads its receiver
  (`list.appended(4)` as a statement - the message points to `append`). Discard on purpose with `const _ = ...`.
- **An expression statement must have the type `Void` or `Never`,** unless the call has a `var` receiver or a `var`
  argument. That is the whole rule behind the one above: `parser.bump()` and `cursor.next()` change something and
  stay statements, `Email.tryFrom(text)` and `1 + 2` are values that go nowhere.

`const` is deep from the perspective of the binding: through a `const` binding you can neither reassign, nor assign
fields, nor call `var fn` methods. `var` means "mutable through this path".

Assignment is a statement, not an expression.

## Built-in Types

```trb
const someInt = 10                         // Int
const someFloat = 3.14                     // Float
const someChar = 'A'                       // Char (Unicode scalar value)
const someString = "Hello"                 // String (UTF-8, see below)
const someBool = true                      // Bool
const someTuple = (1, "one")               // (Int, String), access with `.0`, `.1` or destructuring
const someBounds = (lowest: 1, highest: 9) // Named tuple: `.lowest` is a name for `.0`, nothing more
const someList = [1, 2, 3]                 // List<Int>
const someMap = ["a": 1, "b": 2]           // Map<String, Int>
const someRange = 0..10                    // Range<Int>; `0..` is a RangeFrom<Int>, `..10` a RangeTo<Int>
const someOption: Int? = None            // `Value?` is sugar for `Option<Value>`
const someFunction = { x: Int => x * 2 } // (Int) => Int
const emptyList: List<Int> = []          // Empty literals need a type from context
const emptyMap: Map<String, Int> = [:]
```

- Numeric types always carry their width:

  | Signed                            | Unsigned                              | Floating point       |
  |-----------------------------------|---------------------------------------|----------------------|
  | `Int8`, `Int16`, `Int32`, `Int64` | `UInt8`, `UInt16`, `UInt32`, `UInt64` | `Float32`, `Float64` |

- The prelude declares three aliases for the defaults: `type Int = Int64`, `type UInt = UInt64`,
  `type Float = Float64`. Convention: write `Int`/`Float` for "a number", write the sized name when the width is
  the point (binary formats, FFI, memory layout) - then also `Int64`.
- Integer literals are `Int64`, decimal literals are `Float64`, unless the expected type says otherwise.
  This is fixed and does not follow aliases.
- All widths are fixed on every platform. There is no platform-dependent integer type (same program, same overflow
  behavior everywhere). A pointer-sized integer, if ever needed, belongs to the FFI module.
- `Decimal`: exact base-10 arithmetic for money and the like (`const price: Decimal = 19.99`)
- No implicit numeric conversions. Use `From`/`Into`: `Float.from(someInt)`.
- **A literal that does not fit the type it adapts to is a compile error** ("`300` does not fit into `Int8`"), and so
  is an `Array` index that is known to be out of bounds. Overflow at runtime panics; overflow that is written in the
  source is caught where it is written. Every integer type carries its own range as `minimum` and `maximum`
  (`Int8.minimum` is `-128`, `UInt64.maximum` is `18446744073709551615`), so a bound can be named instead of repeated.
- **Integer division truncates toward zero, and the remainder takes the sign of the dividend:** `-7 / 2` is `-3`,
  `-7 % 2` is `-1`. `x / 0` and `x % 0` panic, and so does dividing the smallest value of a signed type by `-1`
  (an overflow like any other).
- **The bit operators are the members of `Bits`, on the levels of Go and Swift.** `a & b` is `bitwiseAnd`, `a | b`
  `bitwiseOr`, `a ^ b` `bitwiseExclusiveOr`, `~a` `bitwiseNot`, `a << n` `shiftedLeft(by:)` and `a >> n`
  `shiftedRight(by:)` (an arithmetic shift on the signed types; a shift by a negative amount or by the width of the type
  or more panics). `&` binds like `*`, `|` and `^` like `+`, a shift between `*` and `**`, so `x & 1 == 0` is
  `(x & 1) == 0`. In a type, `&` and `|` keep their meaning (intersection, literal-type union). `UInt64` additionally has
  `addedWrapping` and `multipliedWrapping`, the only arithmetic in the language that does not panic on overflow -
  a hash function cannot be written without it. Keeping the
  wrapping pair off the signed types keeps "overflow panics" true everywhere a `+` is written.
- **On a `Float`, the operators are IEEE-754 and `compare` is a total order.** `==`, `<`, `<=`, `>` and `>=` are the
  hardware's: `nan != nan`, every comparison with a `nan` on either side is `false`, and `0.0 == -0.0`. `compare` puts
  `nan` above everything and treats `-0.0` as equal to `0.0`, so `sorted` terminates whatever pivot it picks. **A float
  is the one type where an operator and the member behind it disagree,** and it is why anything that *orders* values -
  `sort`, `sorted`, `minBy`, `maxBy` - calls `compare` and never writes `<=`: only a total order is an order at all, and
  an IEEE `<=` would leave a `nan` wherever it started. `Float32` and `Float64` are deliberately not `Hash`, so a float
  can never be a `Map` key and the `nan` key does not exist. Where a tolerance is meant, `isCloseTo` says so.
- **Case mapping and character classification are one code point at a time, over ASCII and the letters of Latin-1.** A
  `Char` is one code point and `toUpperCase` answers one, so only a mapping that is one-to-one applies: `'ß'`
  upper-cased is `'ß'`, because its uppercase is `SS` and that is two code points, and `'ÿ'` is the one Latin-1 letter
  whose partner lies above Latin-1 (`'Ÿ'`). Every code point the mapping does not cover is answered unchanged, and
  `String.toUpperCase` is the same mapping per character - so a mapped text is exactly as many bytes as it was. The full
  Unicode tables, and with them a `String.toUpperCase` that may grow a text, are milestone 8; until then both back ends
  answer the same small closed table and the conformance suite compares them.
- **A label is not part of the type of a tuple.** `(lowest: Int, highest: Int)`, `(highest: Int, lowest: Int)` and
  `(Int, Int)` are one type, and a labeled tuple may be used where an unlabeled one is expected and back. A label at
  a position where the expected type has a different one is an error, so a swap cannot happen silently.
- `Void` is the type with exactly one value, `Never` the type of expressions that do not return (`panic`, `return`).
  **The one value of `Void` is the keyword literal `void`,** as `true` and `false` are the values of `Bool`: a function
  without a result type returns `Void`, a block that ends in a statement has the value `void`, and `Ok(void)` passes it
  on. `Void` in an expression is an error that says so. `Never` has no value at all and converts to every type, which is
  why `panic "..."` fits into any expression.
- `Void`, `Never` and the three ranges are declared in the prelude like every other type. **Which ends a range has is
  its type**, chosen by the syntax: `a..b` and `a..=b` are a `Range(start, end, inclusive)`, `a..` a
  `RangeFrom(start)`, `..b` and `..=b` a `RangeTo(end, inclusive)`. No field is optional in any of them.
- **`Range<Int>` is `Iterate<Int>` and `Length`, `RangeFrom<Int>` is `Iterate<Int>` and endless, `RangeTo<Int>` is
  neither** - so `for index in ..10` and `(0..).length()` are refused where they are written, each with the reason, and
  nothing has to panic. What accepts every form takes the trait `Bounds<Value>` (`lowest()`, `highest()`,
  `includesHighest()`, `contains` as a default), which is what `list[from..to]` passes to `Slice.slice`. `inclusive`
  stays a `Bool` field: in the type as well it would give six range types for a distinction no body makes.

### Strings

A `String` is UTF-8 text and a value like everything else. It deliberately has no `length()` and no `text[i]`,
because "length" and "the i-th character" have three different answers (bytes, code points, what a reader sees) and
two of them are slow:

```trb
const text = "Grüße 👋"
text.chars().count()               // 7  - `chars()` is an Iterate<Char> (Unicode scalar values)
text.byteLength()                  // 12 - O(1): G, r, e and the space are one byte, ü and ß two, 👋 four
text.isEmpty()

const at = text.indexOf("ß")       // Some(4): a TextIndex, which only the text hands out
const tail = text[(at ?? text.end())..] // "ße 👋" - slicing at a position is O(1), sharing the storage
text.substringAfter("ü")           // Some("ße 👋") - most code never sees a position
text.dropping(characters: 3)       // "ße 👋" - counted in characters, and it cannot panic
```

- **A position is a `TextIndex`, not an `Int`.** `text[3..]` is a compile error: byte 3 is the second byte of `ü`,
  and nobody counts UTF-8 bytes by eye (see [Error Handling](#error-handling), and
  [docs/design/PANICS.md](docs/design/PANICS.md) section 4). Positions come from `indexOf`, `start()`, `end()` and
  `indexAfter`; one holds a byte offset and has no arithmetic. A number from outside becomes one through
  `indexAt(byteOffset:)`, which answers `None` inside of a character, and a format that counts bytes has
  `sliceBytes`, `byteAt` and `byteOffsetOf`, whose names say what they count. What can still panic is a position of
  *another* text, with the offset and the length in the message - a broken promise of the program, like an index of
  another list.
- **A `String` is therefore always valid UTF-8.** The only ways in are literals, slices at character boundaries,
  `String.from(Iterate<Char>)` and runtime functions that validate - so reading a file whose bytes are not UTF-8 is an
  `IoError`, never a replacement character, and neither `chars()` nor a back end needs a rule for broken text.

**A `"""`/`raw"""` string is dedented by its first line,** so a block of code reads at the indentation of the call
around it instead of jammed against the left margin:

```trb
fn count(limit: Int): Int {
  var total = 0
  const source = raw"""
    fn double(value: Int): Int {
      value * 2
    }
    """
  total
}
```

1. If the opening `"""` is directly followed by a line break (optionally after trailing spaces), that line break is
   not part of the string.
2. The indentation of the **first line that has content** is the reference. It is removed from the start of every
   line. A line with content that is indented less than the reference, or whose indentation does not start with it
   (tabs where the reference has spaces, for example), is a lexer error at that line ("This line is indented less
   than the first line of the string") - no silent guessing. Empty or whitespace-only lines become empty.
3. If the closing `"""` stands alone on its line, that line's indentation is not part of the string; the string then
   ends with the line break of the last content line.
4. `"""text"""` on one line is unchanged. Interpolated **values** are never dedented, only the literal text around
   them - an interpolation counts as content for rule 2. Escapes run after dedenting.

### Literal Types

A type can be a union of literals of one base type (`String`, `Int` or `Char`):

```trb
type Status = "online" | "offline" | "away"

var status: Status = "online"              // Literals adapt to the expected type, like `const z: Float = 1`
status = "busy"                            // Compile error: not one of the three
status = someString                        // Compile error: a String is not a Status
status = Status.tryFrom(someString)?         // Generated, like `Show`, `Equals`, `Hash`, `Encode`, `Decode`

fn connect(host: String, transport: "tcp" | "udp" = "tcp") { ... }   // They work inline, too
```

- A literal has its ordinary type (`"online"` is a `String`) unless a literal type is expected. So nothing is ever
  widened or narrowed, and inference is untouched.
- The members are told apart by value, not by type. That is why this fits a language without runtime types - and why
  **only literals can be combined with `|`**. There are no unions of types (`Int | String`): use a type with cases,
  or accept `<Value: Into<Width>>`.
- `match` on a literal type is exhaustive without `_`. Back to the base type: interpolation or `status.into()`.
- The generated implementation is `TryFrom<String, LiteralParseError>`: `LiteralParseError { text, expected }` names the text
  that did not match and the members it could have been, and shows as `'away' is not one of "online", "offline"`.
- Two literal types are the same type if they have the same members. A subset is not assignable (no subtyping).
  That identity is structural, so a literal type has no owner and **cannot be extended**: `extend "tcp" | "udp"` is
  an error, with or without a trait. Only the generated members exist. For behavior, declare a type with cases.
- For everything with data or behavior attached, a type with cases is the tool. Literal types are for "one of these
  strings" in signatures, configuration and wire formats.

### Const Parameters and `Array`

A type parameter can be a value instead of a type:

```trb
type Matrix<const Rows: Int, const Columns: Int> {
  private var cells: Array<Array<Float, Columns>, Rows>

  static fn zero(): Matrix<Rows, Columns> {
    Self Array.filled(Array.filled(0.0))
  }

  fn multiplied<const Other: Int>(other: Matrix<Columns, Other>): Matrix<Rows, Other> { ... }
}

const combined = Matrix<2, 3>.zero().multiplied(Matrix<3, 4>.zero())   // Matrix<2, 4>, checked by the compiler
```

- `Array<Item, const Size: Int>` is **the inline storage primitive**, as in Rust and Go: a small inline value without
  heap storage and without a reference count, whose size is part of its type. An index that is known at compile time is
  checked at compile time. It is not one more collection - everything that grows is a `List` (`ArrayList` is what `Vec`
  is in Rust) - and it therefore lives in `std/core`, next to the other types the language itself refers to, while
  `std/collections` holds the data structures written on top of a storage primitive. The planned heap kernel
  `Buffer<Item>` is its counterpart and belongs beside it.
- **Every way to build an Array writes its size down, and none of them computes it.** A list literal against an
  expected `Array` type is counted: `const corners: Array<Int, 4> = [1, 2, 3, 4]`. A `...` inside such a literal is
  allowed where its operand is an `Array` as well, because only then is the number of items known, and the sizes add up
  to what the annotation says. `Array.filled(value)` and `Array.generated { index => ... }` take `Size` from the
  expected type; `Array.from(iterable)` counts at run time and answers `Array<Item, Size>?`. A literal and `generated`
  write the items straight into the inline slots, so no list is built and nothing can fail. **There is no factory whose
  number of arguments becomes the `Size`:** no signature can say that, so no signature pretends to, and a call with no
  expected type is told plainly that a `const` parameter is never inferred from an argument.
- A const argument is a literal, a named `const` or another const parameter. **There is no arithmetic in types**
  (`Array<Item, Size + 1>`): the type checker compares const arguments for equality and nothing else.
- Const parameters are `Int`, `Bool`, `Char` or `String`. Inside of the type they are ordinary constants (`0..Rows`).
- This does not make types values: a value in a type is the other direction.

### Type Aliases

There is no `alias` keyword. `type Name { ... }` declares a new type, `type Name = ...` names an existing one.

```trb
type Int = Int64                                   // This is how the prelude declares `Int`
type Handler = (request: Request) => Response
type Pair<Value> = (Value, Value)                              // Aliases can be generic
public type EntityId = Int
```

- The right side of `type X =` is a _type position_, like after `:` or `with`. (`const`/`var` modifiers make no
  sense here and are not allowed.)
- An alias is transparent: `EntityId` and `Int` are the same type and freely interchangeable, in error messages
  the alias name is shown with the target (`EntityId (Int64)`). For a distinct type, declare one:
  `type UserId { value: Int }`.
- Aliases are ordinary scoped names: private to the file unless `public`, importable, and shadowable like everything
  else from the prelude. So a file _can_ declare `type Int = Int32`. The linter warns about shadowing prelude names
  though: literals stay `Int64`, and `Int` would mean different things in different files. Prefer a name that says
  why the width matters (`type Coord = Int32`, `type Sample = Float32`).

> **Why not `const X = Y`?** That needs types to be compile-time values, and the right side of `const` is an
> _expression position_. Type syntax and expression syntax overlap with different meanings:
> `(request: Request) => Response` is a function type, but also a valid lambda returning `Response`.
> `(Int, Int)` is a tuple type, but also a tuple of two types. `Int?` is `Option<Int>`, but `?` is also the
> propagation operator. `type X =` sidesteps all of this by switching the parser to type syntax.
> The bigger cost sits behind it: if types are values, generics become functions over types (`fn List(Item: Type): Type`).
> Those cannot be inferred by unification (`numbers.map { _ * 2 }` could no longer infer `Output`), bounds could only be
> checked at instantiation instead of at the declaration, and the type checker would need to run the interpreter
> while it is type checking. Declarative generics with trait bounds and inference are worth more to this language.
> If types ever become compile-time values, `type X = Y` stays valid as it is.

### Distinct Types (opaque "aliases")

There is no separate concept for this. A distinct type is a `type` with a single field, and `by` forwards traits to
that field so the wrapper does not cost boilerplate. `by` binds to **the one element of the `with` list directly in
front of it** - a trait alone, or a parenthesised group of them - never to the whole list, so a mixed line says
exactly what happens to each trait. A `with` list separates its entries with commas only; `&` (which combines traits
into one type everywhere a type is expected, `where Item: Hash & Equals`) is not a separator here and is a parse
error that names the fix, either `with Y, Z` or `(Y, Z) by f`:

```trb
type UserId { value: Int }

type Seconds with Show, (Add, Subtract) by value, Compare by value {
  value: Int
}

type Email with Show by value, TryFrom<String, ParseError> {
  private value: String                          // Private: only `Email.tryFrom` creates an Email
  static fn tryFrom(text: String): Result<Email, ParseError> { ... }
}

const total = Seconds(5) + Seconds(2)          // Seconds(7)
// Seconds(5) + 2                              // Compile error: Int is not Seconds
// Seconds(5) + Meters(2.0)                    // Compile error
const raw = total.value                        // Explicit way out
```

- `Show` above is derived, `Add`, `Subtract` and `Compare` are delegated to `value` - one line, one decision per
  trait. A trait that is neither derived, delegated nor written by hand is not available: `Seconds * Seconds` does
  not compile, which is the point (that would be square seconds).
- **`by` needs a type with exactly one field, and the name after `by` names that field.** `Money` with `amount` and
  `currency` cannot delegate `Add` to either one - what would `Add` do with `currency`? - and a type with cases
  cannot delegate at all, having no field of its own to name. The single-field rule is what keeps a delegated
  member's meaning unambiguous without saying more than the field's name.
- Where a forwarded signature mentions `Self` (`add(self, other: Self): Self`), arguments are unwrapped and results
  wrapped again. That only works for single-field types. Traits without `Self` in arguments or results can be
  delegated by any type (`type Team with Iterate<User> by members { ... }`).
- **`by` forwards the required members, the default members come from the trait.** `total.max(Seconds(10))` is
  `Compare.max` over the forwarded `compare`, so it returns a `Seconds` and nothing has to be rewrapped. A default
  that is written in terms of the required members stays correct by construction.
- Everything else comes from the existing rules: visibility of the constructor, factories, `From`/`Into`, methods,
  `extend`. `Equals`, `Hash` and `Show` are generated as for every `type`.
- A single-field `type` is guaranteed to have the representation of its field (no allocation, no indirection).
  Not observable, but it makes distinct types free.

## Functions

```trb
fn sum(a: Int, b: Int): Int {
  a + b
}
```

- `fn` declarations are hoisted within their scope, so (mutual) recursion just works.
- Type arguments can be given partially, from the left; the rest is inferred (`into<Set<Employee>>()`).
- Parameter types are mandatory on `fn`. The return type can be inferred - except for `public` functions and trait
  methods: **they never infer it.** Without a return type they return `Void` (`var fn add(value: Item)`), and a
  body that produces a value is an error at that value. Whoever calls a public function must not have to read its
  body to know what it returns.
- The last expression of the body is the return value. `return` exits early.

### Arguments

```trb
fn connect(host: String, port: Int = 5432, timeout: Int = 30): Connection { ... }

connect("localhost")                       // Defaults
connect("localhost", timeout: 10)          // Labeled arguments use `:` and follow the positional ones

fn largest(first: Int, ...rest: Int): Int { ... }   // Variadic parameter, `rest` is a `List<Int>`

largest(1, 2, 3)
largest(0, ...someSet)                         // Spread works with every `Iterate<Int>`
```

A variadic parameter never accepts a collection implicitly (`List.of([1, 2])` is a `List<List<Int>>` with one
element). Spreading is always explicit.

**A parameter default is evaluated at the call site, at every call, in the scope of the declaration** - without `self`
and without the other parameters. That is the scope rule of a field default (see [Construction](#construction)), which
is a constant on top of it, so
`limits(memory: Int = 64.megabytes())` is a call that happens where `limits` is called, and a default can never depend
on an argument order that is not visible at the call site.

Parameters are `const`. A `var` parameter allows mutation through it (`fn reset(var counter: Counter)`), the caller
must pass something mutable. The receiver of a `var fn` is just the most common case.

### Lambdas and Closures

There is exactly one form. A `{` in expression position is ALWAYS a closure, never a block.
Closures capture their surrounding scope.

```trb
const double = { x: Int => x * 2 }                 // Typed parameters, no expected type needed
const add = { a: Int, b: Int => a + b }
const triple: (Int) => Int = { _ * 3 }             // Implicit parameters: `_`, `_2`, `_3`, ...
const swap: ((Int, Int)) => (Int, Int) = { (a, b) => (b, a) }   // Patterns work as parameters

const clamp = { x: Int, low: Int, high: Int =>     // The body is a list of statements
  if x < low { return low }
  if x > high { return high }
  x
}
```

- Parameter types are inferred from the expected type. A closure without an expected type must annotate them.
- The return type is always inferred. If it needs to be spelled out, annotate the binding or use a local `fn`
  (which is the "full form" of a function anyway, and can be passed by name).
- `return` inside a closure returns from the closure. There is no non-local return. **`?` returns from the closure
  too** (enforced): the closure then has to produce an `Option` or a `Result`, checked at the `?` where the result is
  written down and once the closure is checked where it is inferred - a closure that produces anything else has nowhere
  for the failure to go. The same holds for a `fn` whose result is inferred.
- `=>` has exactly three jobs: function types (`(Int) => Int`), closure parameters (`{ x => ... }`) and match arms.
- **A `fn` inside a block is not a closure.** It is the same declaration as at the top level and sees the same things: its
  own parameters and its file. A local `fn` is therefore the way to write a helper that must not capture; a closure is
  the way to write one that must.
- The names of the parameters of a function *type* are documentation, exactly as the labels of a tuple are: `(host:
  String) => Bool` and `(String) => Bool` are the same type. A *declaration* is different - there the name is how a
  caller passes the argument.

### Trailing Closures

If the last parameter of a function is a function, a brace closure can follow the call.

```trb
numbers.map({ _ * 2 })
numbers.map { _ * 2 }
numbers.fold(0) { sum, number => sum + number }
```

The implicit parameters are always `_`, `_2`, `_3`, ... - never a name taken from the expected function type. A
closure that wants a name writes its own parameter list, exactly as it does everywhere else:

```trb
fn map<Output>(transform: (value: Item) => Output): Iterate<Output>

numbers.map { value => value * 2 }
```

### Command Calls (calls without parentheses)

```trb
print "Hello"
route "/health", to: "health"
const response = retry 3 { http.get(url) }
test "adds two numbers" {
  assert(sum(1, 2) == 3)
}
```

Rules:

- Only allowed in _command position_: at the start of a statement, on the right side of `=`, after `return` and
  after `=>`.
- Never inside parentheses, brackets, operators or argument lists. Commands do not nest: the arguments of a command
  are ordinary expressions (`print describe(numbers)`, not `print describe numbers`).
- The callee is a name or a member path (`print`, `Email.tryFrom`, `server.route`).
- Arguments are separated by `,`. The first argument must not start with `(`, `[`, `-`, `!` or `.`
  (`f [1]` is always indexing, `f -1` is always subtraction, `f .Case` is always the member `f.Case`). Use
  parentheses in these cases.
- A trailing closure always belongs to the outermost command call of the statement
  (`unless list.isEmpty() { ... }` passes the closure to `unless`). Consequently, the arguments of a command call
  cannot contain trailing closures themselves: `print numbers.map { _ * 2 }` is an error, write
  `print numbers.map({ _ * 2 })`. The same is true for the heads of `if`, `for`, `while` and `match` - a command
  reads like a built-in statement, and its `{` is its body. (`a b { }` cannot be decided by looking at it:
  `unless done { ... }` and `print numbers.map { ... }` have the same shape. One rule, the error message names the
  fix, and the formatter applies it.)
- Calls without arguments always need `()`. A bare name is always a reference.

### Formatter Canon

Both forms parse, so which one to write is a question of style - and it is decided, not left open. `torb format`
enforces this, and there is no option to turn it around.

**A call is written as a command wherever the grammar allows it.** All of this has to hold:

1. it stands in command position (start of a statement, right of `=` in a binding or an assignment, after `return`,
   after `=>`),
2. the callee is a name or a member path (`Ok`, `Email.tryFrom`, `roles.map`),
3. it has at least one argument, and the first one does not start with `(`, `[`, `-`, `!` or `.`,
4. no argument has an operator at its top level,
5. the arguments are on one line - a trailing closure may go over several.

Everything else has parentheses. That is every nested call, because only command position allows a command:

```trb
const role = Role name
const email = Email.tryFrom text
names.map Role
builder.add CStatement.Break
print "Hello"
test "adds two numbers" {
  assert(sum(1, 2) == 3)
}

fn checked(value: Int): Result<Int, Problem> {
  if value < 0 {
    return Fail Problem.Negative
  }
  Ok value
}
```

```trb
Ok Some(x)                  // A command does not nest: the argument is an ordinary expression
list.length()               // No arguments, so parentheses
assert(sum == 3)            // An operator at the top level of an argument: `assert sum == 3` reads as `(assert sum) == 3`
print(count + 1)
if ready(now) { }           // The head of an `if`, `for`, `while` or `match` is not command position
onStart(event)              // A field is written with `=`, so calling what it holds always needs parentheses
```

**A multi-line `"""` or `raw"""` string is indented.** The opening quotes stay where they are, the content is two
spaces deeper than the line the statement starts on, and a closing `"""` that stands alone is aligned with the
content. Because the first content line is the reference that is subtracted from every line (Text and Strings), the
**value** does not depend on any of this - moving a whole block left or right changes nothing about what it says:

```trb
fn generated(): String {
  const header = """
    #include <stdint.h>

    static int64_t counted(void)
    """
  header
}
```

### Parameter Modes

| Parameter type     | The argument is...                                                   | Used for                          |
|--------------------|----------------------------------------------------------------------|-----------------------------------|
| `Value`                | evaluated at the call site                                           | everything                        |
| `var name: Value`      | a `var` path. The function works on the caller's value               | in-place algorithms, builders     |
| `() => Value`          | a closure                                                            | control structures, callbacks     |
| `(self: Receiver) => Value`   | a closure whose names resolve against `Receiver` (receiver closure)         | builders, DSLs, config files      |
| `lazy Value`           | any expression, evaluated at most once, on first use                 | `opt.orElse(expensive())`, logging |
| `Expression<Value>`          | quoted: the typed expression tree, plus the value                    | query providers, `assert`         |

### Quoted Expressions (`Expression<Value>`)

If a parameter (or binding) has the type `Expression<Value>`, the argument is type checked as a normal `Value` first and then
passed _together with its expression tree_. The call site looks like any other:

```trb
const minAge = 18
const adults = users.filter { _.age >= minAge }             // List<User>:   predicate: (value: Item) => Bool
const query = db.users.filter { _.age >= minAge }           // Query<User>:  predicate: Expression<(row: Row) => Bool>
// SELECT * FROM users WHERE age >= ?    [18]

assert(adults.length() > limit)                             // fn assert(condition: Expression<Bool>)
// Assertion failed: adults.length() > limit   (limit = 5)   at main.trb:12
```

```trb
// std/expression
native type Expression<Value> {
  tree: ExpressionNode                  // Static data, created at compile time
  source: String                  // "_.age >= minAge"
  location: SourceLocation
  native fn value(): Value               // The ordinary value/closure. Evaluated at most once for non-functions.
  native fn captures(): List<EncodedValue> // Values of the captured variables, without their types
}

type ExpressionNode {
  case Literal(value: EncodedValue, of: TypeReference)
  case Parameter(index: Int, name: String, of: TypeReference)
  case Captured(index: Int, name: String, of: TypeReference)      // Index into `captures()`
  case Field(target: ExpressionNode, name: String, of: TypeReference)
  case Call(target: ExpressionNode?, owner: TypeReference, method: String, arguments: List<ExpressionNode>, of: TypeReference)
  case Construct(arguments: List<ExpressionNode>, of: TypeReference)
  case Unary(operator: UnaryOperator, operand: ExpressionNode, of: TypeReference)
  case Binary(operator: BinaryOperator, left: ExpressionNode, right: ExpressionNode, of: TypeReference)
  case Conditional(condition: ExpressionNode, then: ExpressionNode, otherwise: ExpressionNode, of: TypeReference)
  case Lambda(parameters: List<String>, body: ExpressionNode, of: TypeReference)
  case Items(items: List<ExpressionNode>, of: TypeReference)            // List literal
  case Interpolation(parts: List<ExpressionNode>)                       // "{a} and {b}": literals and expressions
}
```

- **Quotable is what is an expression:** literals, parameters, captured variables, field access, calls, operators,
  constructors, `if`/`else`, nested closures, list literals, string interpolation. A quoted closure must consist of
  a single expression. Statements (`const`, `var`, assignment, loops, `return`, `await()`) and the early return `?`
  are a compile error inside of a quotation. `match` is not quotable yet (patterns would double the node set).
- A tuple literal, a map literal and a range are **constructions** and therefore quotable: `(a, b)` and `0..2` are a
  `Construct` node, `["a": 1]` a `Call` of `Map.of`. A spread has no node and neither has a closure parameter that
  destructures, because a `Lambda` node names its parameters.
- `?.` and `??` have no nodes of their own. They are what they mean: calls of `Option.map`/`flatMap` and
  `Option.orElse`. A provider that knows `Option` knows them.
- `nameOf(user.email)` is `"email"`: an ordinary function over `Expression<Value>` that reads the last `Field` or
  `Captured` node and never evaluates the value. For types there is the compile-time function `typeName<User>()`.
- **Quoting happens after name resolution and type checking.** That is why it does not have the phase problem of
  macros: implicit `_`, named parameters, receivers and implicit `self` are already resolved, the tree only contains
  explicit `Parameter`, `Field` and `Call` nodes, each with its type.
- **The tree is data, not reflection.** `TypeReference` is a description (`name`, `arguments`), there is no way back from it
  to a type. Trees are values: they can be matched, transformed and encoded.
- **A binding of type `Expression<Value>` behaves exactly like a parameter:** the initializer is checked as a
  `Value` and quoted, with the same restrictions. `const condition: Expression<Bool> = age > 1` is the quotation of
  `age > 1`, not of some value it was computed from.
- **The tree is free, the captures are not.** The tree is static data and costs nothing at runtime, but `captures()`
  has to return the current values, so they are collected at the quotation site into a list of trait-typed values:
  a quotation without captures is free, one with captures costs one small allocation every time it is evaluated.
  `assert` is in every test, which is why the cost is written down here instead of being discovered.
- **Captured variables must be `Encode`**, because a provider has to be able to look at them (a SQL driver binds them
  as parameters with its own `Encoder`).
  Capturing anything else in a quotation is a compile error. That holds for `assert` too, so the error names it as
  the reason ("`assert` quotes its condition, so `parsed` has to be `Encode`"); what cannot be `Encode` is compared
  in a `match` instead.
- **The tree cannot be executed**, the value can. There is no `compile()` like in C#, so compiled binaries need no
  interpreter for this. An in-memory provider calls `value()`, a SQL provider reads `tree`.
- What a provider does not understand (`filter { myOwnFunction(_) }`) is the provider's error at runtime
  (`Fail(Unsupported(...))`), the language cannot know what a library can translate.
- `ExpressionNode` is part of the language standard and an ordinary ADT. A new node kind comes with a new version of
  the language; providers that end their `match` with `_ => Fail(Unsupported(...))` - and they need that arm for
  calls they do not know anyway - keep compiling.

## Blocks and Control Flow

`{ ... }` in expression position is a closure. To evaluate a block immediately, use `do` - which is an ordinary
function from the standard library (`fn do<Value>(body: () => Value): Value { body() }`), not a keyword.
The bodies of `if`/`else` and of a `match` arm (`pattern => { ... }`) are blocks, not closures: they belong to the
construct the same way the body of a `for` does.

```trb
const initialized = do {
  const base = [1, 2, 3]
  base.appended(4).appended(5).removed(2)
}
```

`if`, `match`, `for`, `while`, `loop` are built in. Their heads are parsed without trailing closures, the `{` starts the
body. `if` and `match` are expressions.

```trb
const aOrB = if something { a } else { b }

for number in numbers { print number }
for i in 0..10 { print i }
while queue.isNotEmpty() { ... }     // `break` and `continue` work as expected
```

**`loop { ... }` is the endless loop, and it is the only way to write one.** Without a `break` that targets it its type
is `Never`, so nothing after it is reached and a function that consists of one needs no other result; with a `break` it
is `Void`. There is no `break value`. `continue` works as in a `while`. `while true` is an error that says
`A loop that never ends is written `loop``, so "never ends" is a property of the syntax and not of a condition - which is
what lets the checker and both back ends see it without evaluating anything.

```trb
loop {
  const line = readLine()? ?? ""
  if line.isEmpty() {
    break
  }
  print line
}
```

Anything that does not bind names can be a library function:

```trb
fn unless(condition: Bool, body: () => Void) {
  if !condition { body() }
}

unless user.isAdmin {
  deny()
}
```

**`using` is not one of those functions: it binds a name, so it is a binding form next to `const` and `var`**
([docs/design/DESTRUCTORS.md](docs/design/DESTRUCTORS.md) section 4). `using file = File.open(path)?` binds `file` to
an object whose release - and the `close()` that release runs - happens at the end of the block, and the checker
refuses to let the name escape it: no field, no collection, no `return`, no other name, no constructor, case, `var fn`
or member of a shared object it is handed to, no closure that may outlive the block. `using` never awaits: a graceful
end such as `sink.end().await()?` is a line of its own. `using` is a keyword only in front of a name and an `=`, and
not at the top level of a file, whose block would be the whole program.

```trb
fn readConfig(path: String): Result<String, IoError> {
  using file = File.open(path)?
  file.readAll()
}
```

The checker enforces the binding, and the release at the end of the block runs `close()`. There is one `using`, not
two: the function of the same name that took a closure is gone from `std/core`.

## Types

`type` is the single keyword for all data types (struct, class, enum, ADT). **A `type` is a value, and the binding
decides whether it can be changed.** There is one `Point`, not a `Point` and a `MutablePoint`. Like everywhere else in
the language: no `var`, no mutation.

### Values

```trb
type Point {
  var x: Int                   // Fields are `const` unless marked `var`
  var y: Int

  // Method: it does not list its receiver. Member access through `self` is implicit.
  fn area(): Int {
    x * y
  }

  // A verb changes the value in place and says so: `var fn`
  var fn translate(deltaX: Int = 0, deltaY: Int = 0) {
    x = x + deltaX
    y = y + deltaY
  }

  // Its participle returns a changed copy
  fn translated(deltaX: Int = 0, deltaY: Int = 0): Point {
    copy(x: x + deltaX, y: y + deltaY)
  }

  // Of the type and not of a value: `static`.
  static fn square(size: Int): Self {
    Self(size, size)
  }

  // Static constant
  static origin = Point(0, 0)
}

var p = Point(x: 10, y: 20)    // or positional: Point(10, 20)
const q = p                    // A copy

p.x = 20
p.translate(deltaX: 5)         // `q` is still (10, 20)

q.x = 20                       // Compile error: `q` is a `const`
q.translate(deltaX: 5)         // Compile error: `translate` needs a `var`. Did you mean `translated`?
const r = q.translated(deltaX: 5)
p = p.copy(y: 30)              // `copy` is generated for every `type`
```

- **Assigning, passing and capturing a value is a copy.** Two bindings never alias, so what happens through one `var`
  cannot be observed anywhere else. That is what makes "the binding decides" sound - and why a `const` list, map or
  point really never changes.
- **Mutation needs a `var` path,** from the binding down to the field: a `var` binding, `var` parameter or a `var fn` receiver,
  then `var` fields all the way. A field without `var` never changes after construction, not even in a `var`
  binding (`id`, `step`). `const` is deep: through a `const` binding nothing changes, whatever the type looks like.
- Structural `Equals`, `Hash`, `Show` and `copy` are generated, there is no identity.
  (Each of them only if all fields support it: a type with a function in a field has no generated `Equals`.)
- **The generated `Show` has a fixed format,** because two implementations of the language are compared through it
  (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)): a type is `Type(field: value, ...)` with all fields in
  declaration order, a case is `Case(field: value)` or its bare name when it has none, a `List` is `[a, b]`, a `Map`
  `["k": v]` (`[:]` when it is empty), a `Set` `{a, b}` (`{}` when it is empty), a tuple `(a, b)` without labels (a label is not part of
  a tuple's type, so a compiled value does not carry one), `Void` is `void`, an `Option` `Some(x)` or `None`. Inside such a value a `String` is quoted with escapes and a `Char` written in single
  quotes. **A `Float` is the shortest decimal that parses back to the same value,** with `.0` appended when that text
  contains neither `.` nor `e` - so a `Float` always carries a decimal point or an exponent; the special values are
  `nan`, `inf` and `-inf`, and `-0.0` prints as `-0.0`. `"{name}"` is still the text itself: the nesting uses
  `Show.showNested`, which defaults to `show()` and is overridden by `String` and `Char` alone.
- Tuples and function types cannot be declared in TorbScript, so what a `type` gets generated they get generated
  too: `Equals`, `Hash` and `Show` for a tuple whose elements have them, `Show` for a function value (its type).
- **A tuple also has a generated `Compare`, lexicographic by position; a `type` does not.** An order is a decision and
  not a structure, so a `type` that wants one writes it - but a tuple has no declaration anybody could write it in, and
  `diagnostics.sort { (_.span.start, _.span.end) }` is how the language sorts by more than one key.
- What a copy costs is the business of the implementation and not observable (see [Execution Model](#execution-model)):
  small values are copied, the storage of collections and strings is shared until somebody writes to it.
- Values are freely passed between tasks.

### Identity (`shared type`)

```trb
shared type Connection {
  url: String
  private(var) sent: Int = 0

  var fn send(message: String) {
    sent = sent + 1
  }
}

const connection = Connection("tcp://example.test")
const same = connection        // The same object
same.send("hello")
print connection.sent          // 1

connection = Connection("tcp://other.test")   // Compile error: a `const` binding always names the same object
```

A `shared type` is the exception for everything that has an identity: assigning it does not copy, everybody who holds
it sees the same object. Typical cases are handles to the outside world (`File`, `Socket`, `Window`), `Channel`,
`Task`, registries. Most programs declare very few of them.

- **A change of an object is not a question of the path.** "Mutation needs a `var` path" is the rule for values, where
  a path is where the copy lives. An object has one identity and no copy, so its `var fn` members and its `var` fields
  change through any binding that holds it - a `const` one, a parameter, `pool[0]`. `var` on a binding of a shared type
  means **rebinding** and nothing else: a `const` binding always names the same object. For the same reason `self` of a
  shared type is never assigned and never handed to a `var` parameter - that would point the caller's binding
  elsewhere. There are no read-only views of an object: a view that is a property of the path can be widened again by
  any function that hands its argument back (`fn launder(c: Connection): Connection { c }`), so it would promise
  nothing. (Decision 2026-09-22; it replaced the read-only view, its widening rule and the rule on `var` fields filled
  from a `const` path.)
- **A `var fn` method of a `shared type` may answer a `Task`.** For a value a `var` is an exclusive in-out access whose
  "copy in, copy out" ends with the call, so a change made after the call has returned would be lost - and a `var` of a
  value on a function that answers a `Task` is therefore a compile error. For an object there is no copy: `var` is the
  *permission* to change the one object, two `var` paths to it may exist at once (as above), and a permission survives an
  `await`. That is what lets `Source.next()` of [Streams](#streams) mirror `Iterator.next()` instead of
  hiding a cursor somewhere. An ordinary `trait` counts as a value here, because a value may implement it: only a
  `shared trait` may require such a member.
- **A freshly produced object can be changed right away.** `Counter().increment()` is an error because the change would
  be lost with the temporary it was made in - that rule is about values. An object has no copy and nothing is lost, so
  `File.open(path)?.lines()` and a whole chain of wrappers read as one expression.
- `Equals`, `Hash`, `copy` and `Encode` are not generated. `==` is about content and does not exist for objects;
  `isSame(a, b)` compares identity. `Show` is a `shared trait`, so objects can be printed.
- **`isSame` only works on shared objects.** On a value the answer would expose whether the implementation shares
  storage, so the compiler rejects it - a named special case next to object safety, because the language has no bound
  that says "shared" and adding one for a single function is worse ("`isSame` compares identity, and a `Point` is a
  value. Use `==`.").
- `Shared<Value>` from the standard library is the ad hoc version: a box that puts a value in a place that several
  owners can hold.
- A value can contain a shared object. Copies of the value then refer to the same object, and the value no longer
  crosses task boundaries. The compiler derives that, there is no annotation.

### `var` Paths and `var` Parameters

```trb
fn incrementTwice(var target: Counter) {
  target.increment()
  target.increment()
}

var counter = Counter()
incrementTwice(counter)              // Changes the caller's counter
incrementTwice(counters[0])          // Every `var` path is a valid argument

world.entities[id].health = 5        // A path through fields and `[]`: changed in place
samples[1..4].sort { _ }             // A range is a path, too: sorts this part of the list in place
```

- A `var` parameter works on the caller's value. The argument has to be a `var` path. There is no marker at the call
  site - the signature says it, tooling shows it. The meaning is "copy in, copy out", so it is the same in every
  back end; implementations pass a reference.
- A path through `a[key]` (`MutableIndexed`) or `a[from..to]` (`MutableSlice`) means: take it out, change it, put it
  back - without a copy. This is what other languages need mutable slices and spans for.
- **References are second-class.** They only exist as a `var` parameter or a `var fn` receiver, for the duration of a call.
  They cannot be stored in a field, returned, or captured by a closure that is stored. So there are no lifetimes, no
  borrow checker, and nothing can dangle. A captured `var` binding follows the same rule (see below).
- **A closure that captures a `var` binding may not escape, and the compiler decides that per closure** (enforced). A
  `var` parameter, a `var fn` receiver, a local `var` and a `var` parameter of a closure around it are all the same:
  such a closure may stand straight as the argument of a call whose parameter **only calls it**, and nowhere else - not
  bound to a name, stored in a field, a collection or a case, returned, handed to `spawn` or to a parameter that keeps
  it. A parameter only calls its closure when it has a function type, its function has a body, and that body calls it,
  calls it inside a closure that itself only runs during the call, or hands it on by name to a parameter that only
  calls it - which is exactly what the receiver closures, the DSLs, `forEach` and `unless` are. A lazy stage
  (`map`, `filter`) keeps its closure in the stage it answers. Whether a parameter keeps what it is handed is a fact of
  the callee's body, so the checker answers it once every body it depends on is checked. The decision is recorded,
  because it is also what lets an implementation put the closure's environment on the stack.
- **Exclusivity:** while a `var` access to a path is running, the same path (or a path above or below it) cannot be
  accessed in any other way. **The access of a call begins once all of its arguments have been evaluated**, so
  everything the arguments *read* has already finished and `items.removeAt(items.length() - 1)` is ordinary code. What
  is left is what really overlaps: two `var` accesses of the **same** call (`swap(a, a)`, `move(list[0], list)`), and a
  closure argument of a call reaching the path that call is changing - the closure runs *inside* the access, which is
  what makes changing `root` inside `root.div { ... }` an error. Different fields are fine
  (`project.build { output "{project.name}" }`). Two indices or ranges of the same collection are not, because they
  cannot be compared statically (`swap(items[i], items[j])`: use `items.swapAt(i, j)`). The check is static and
  conservative: what the compiler cannot prove is an error, and there is no check at runtime.
- A temporary is not a `var` path: `iterate().next()` is a compile error, `var cursor = iterate()` comes first.
  (Changing something that is thrown away is always a mistake.) As the _argument_ of a `var` parameter a temporary
  is fine - the callee is its only owner, so "copy in, copy out" is exact and nothing is written back anywhere:
  `drain(File.open(path)?)` for a `fn drain(var file: File)`. The rule is about the base of a path (`f().x = 1`), not about ownership.
- The variable of a `for` loop is a `const`. To change elements, use the path (`items[index].x = 1`,
  `items.update(index) { ... }`) or build a new collection with `map`. **`for var element in items`** (decided, not
  yet implemented - [docs/design/COLLECTIONS.md](docs/design/COLLECTIONS.md) section 3.11) binds a `var` reference to each slot
  instead, for one turn of the body; see [Collections and Iteration](#collections-and-iteration).
- Closures capture `const` bindings as copies. A captured `var` binding is shared between the closure and its scope -
  the one place where a variable is shared. Closures passed to `spawn` cannot capture `var` bindings.
- **A captured `var` binding is shared only while the binding exists.** Because a closure that captures one never
  escapes (above), two copies of a value never share a variable through a closure they hold, a closure never changes a
  binding while a `var` access to it runs - the closure argument is checked against the accesses of its own call - and
  a task never reaches one. The binding is exempt from the dead-change rule: the read can be anywhere the call runs the
  closure. What has to outlive the scope is handed to a `var` parameter or returned. **State that has to outlive its scope is a `shared type`**, which
  says that it has an identity; recursion is a local `fn`, which captures nothing.
- **The copy trap** is the price of values, for everybody who comes from a language with references:

  ```trb
  var first = counters[0]            // A copy
  first.increment()                  // Changes the copy. The linter flags a `var` that is changed but never read.
  counters[0].increment()            // Through the path
  ```

### Construction

Every type has exactly one constructor. It is generated from the fields, in declaration order, and cannot be
written by hand. **Constructors never contain logic.**

- Fields with a default value can be omitted.
- **A field default is a constant, read in a scope without `self` and without the other fields:** a literal, `None`,
  a collection literal, a constructor or a case of constants, a named constant, an operator on those, or a closure
  that captures nothing. No call - `List.filled 16, None` and `Set.of()` are code, and a constructor runs none; an
  empty `Set` is `[]`. So the order of the fields is not observable, and a default that is computed or depends on
  another field is what a factory is for.
- Outside of the type, `private` fields cannot be passed. So the constructor is usable from outside if and only if
  every private field has a default value. Inside of the type (`Self(...)`) all fields can be passed.
- **`copy` has the shape of the constructor, with every field optional:**
  `fn copy(<field>: <Type> = <the current value>, ...): Self`, all fields in declaration order, and `private`
  fields not passable from outside. There is no `copy` for a `shared type` - it has an identity, not a value.

Everything else is a static factory function:

```trb
type Email with TryFrom<String, ParseError> {
  private value: String                // Private without default: only `Email` itself can construct an `Email`

  static fn tryFrom(text: String): Result<Email, ParseError> {
    if !text.contains("@") {
      return Fail(ParseError("'{text}' is not an email address"))
    }
    Ok(Self(text))
  }
}

const email = Email.tryFrom("info@example.test")?
```

### Conversions

Conversions follow the `From`/`Into` principle. Implementing `From` provides `Into` for free, fallible conversions use
`TryFrom`, which provides `TryInto` the same way. **Text is a source like any other**, so reading a value from it is a
`TryFrom<String, Failure>` and there is no `parse` on a type; a function named `parse` belongs to a format
(`Json().parse`). A type may implement `TryFrom` once per source - `Int.tryFrom("42")` reads text, `Int.tryFrom(3.0)`
narrows a number - and the argument decides which one a call means. One direction of each pair is the one to
implement: `Into` and `TryInto` come from a blanket implementation over the other, so an `extend` that writes one by
hand overlaps that blanket and is answered with the line to write instead.

```trb
extend Celsius with From<Fahrenheit> {
  static fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

const a = Celsius.from(Fahrenheit(100.0))
const b: Celsius = Fahrenheit(100.0).into()
```

The `?` operator uses `From` to convert error types. `tryInto()` reads both its target and its failure out of the
`Result` that is expected of it, so what receives the call is what says which conversion is meant; a `?` on the call
passes no target down, and then the message names `Target.tryFrom(value)` instead.

**Converting your own type into a foreign one is `extend Foreign with From<Mine>`**, which the coherence rule allows
because a type named as an argument of the trait counts as owning the implementation.

**Every type has `From<Self>`**, and that conversion is the value itself. It is not written down anywhere and could not
be: a blanket `extend<Value> Value with From<Value>` would overlap with every other implementation of `From`. It is what
lets `fn sum(): Item where Item: Add & From<Int>` be called with a list of `Int`.

The language has exactly **four coercions**, and all of them only apply where a type is expected - never to decide what
an expression means on its own, and never to solve an inference variable:

- a value where a trait type is expected (`Square` into a `Shape`),
- a trait value where fewer bounds or a supertrait are expected (`Show & Hash` into a `Show`),
- `Never` where anything is expected,
- a literal where a literal type is expected (`"online"` into a `Status`).

**There is no implicit `Some`.** A value never wraps itself into an `Option`: `fn find(...): Item?` has to write
`Some(item)`, and `const x: Int? = 1` is an error. `None` is the one value of the language that takes its type from what
is expected of it. There is no variance either: a `List<Square>` is not a `List<Shape>`.

### Visibility and Encapsulation

**Members are public unless marked `private`** - fields, methods and constants alike. In a language where values are
never aliased and `const` is deep, reading a field cannot break anything, and a value _is_ its data. What needs
protection is mutation and invariants, and both have a modifier. The defaults are never written out: `public` on a
member, `const` on a field and `const` after `static` are refused, one spelling per meaning:

```trb
type Account {
  owner: String                          // Public, const
  var nickname: String = ""              // Public, writable by everyone who has a `var` path to the account
  private(var) balance: Int = 0           // Everybody reads, only Account writes
  private var history: List<String> = []                     // Invisible from outside

  var fn deposit(amount: Int) {
    balance = balance + amount
    history.append("deposit {amount}")
  }
}

account.balance                          // 100
account.balance = 1_000_000              // Compile error: only Account can write `balance`
```

| Field                | Read from outside | Write from outside |
|----------------------|:-----------------:|:------------------:|
| `x: Value`               |        yes        |  - (const)         |
| `var x: Value`           |        yes        |  yes               |
| `private(var) x: Value`  |        yes        |  no                |
| `private x: Value` / `private var x: Value` | no |  no                |

- `private(var)` reads as "the `var` is private": the field is public, its mutability is not. It hands outsiders a _const path_ to the field, and const is deep: with `private(var) routes: List<Route>`,
  `config.routes` can be read and iterated from outside, but `config.routes.append(...)` is a compile error. What
  somebody takes out of it is a copy anyway. No defensive copies by hand, no accessor methods.
- `private(var)` is a modifier of a field and of nothing else. On a member that has no `var` it has nothing to say
  and is an error. It also already *is* the `var`, so `private(var) var balance: Int` is an error: one of the two says
  it twice.
- **`private` reaches as far as the file that declares it.** A private member is visible in the body of its type, in
  an `extend` of that type written in the same file, and in the free functions of that file - and nowhere else. One
  rule, and the same reach a `private` top-level declaration has: what is private is what its file can see. The
  package is the unit of coherence and not the unit of privacy, because an `extend` in another file of the package
  could otherwise write the fields a capsule's factory is the only way into.
- **There are no getters, setters or properties.** A field is storage, a method computes, and the `()` tells which one
  it is (`list.length()` may cost something, `point.x` never does). No `get` prefixes; predicates are called
  `isEmpty()`/`hasX()`, mutators are verbs written `var fn`.
- **A field is a promise about data, so replacing one by a method is a breaking change** - and a property would not
  save it: a field is also a parameter of the generated constructor, a position in patterns, a parameter of `copy`,
  a part of the generated `Encode`/`Decode` and a step of `var` paths. A property would cover reading and nothing
  else. What might be computed, cached or validated one day is a method from the start; the move from `.x` to `.x()`
  is mechanical, the compiler finds every place, and the tools do it for a whole workspace (see `deprecated` in the
  Open Questions).
- There are no validating setters, because a setter cannot fail properly in a language without exceptions.
  Validation lives in types and factories (`Email.tryFrom`, `Port.tryFrom(8080)`) or in a method that returns a `Result`
  (`account.withdraw(amount)`).
- **Top-level declarations are private to their file unless marked `public`.** This is a different question - the
  surface of a module is opt-in, `lib.trb` defines the API of a package.
- **A `public` declaration may not expose a type that is private to its file.** A caller who cannot name the type
  cannot do anything with the value, so the parameters, the result, the fields and the case fields of a `public`
  declaration, the members of a `public trait` and the type of a `public const` all have to be at least as visible as
  the declaration itself. A `private` *field* is not part of that surface: nobody outside can name it either way.
- **`public const` is legal at top level, `public var` is not.** A module can export a constant (its initializer is
  compile-time evaluable, see [Modules and Packages](#modules-and-packages)); a module has no mutable state, so
  there is nothing a top-level `var` could export.
- **A top-level `var` of a script is changed by the statements of its file and by nothing else** - the initializers
  of its `const`s and a closure written straight as the argument of a call that only calls it included. A `fn` and a
  closure that is kept may run while a `var` access to the declaration is open, so they read it and never change it;
  what a function has to change it takes as a `var` parameter. (Decision 2026-09-22: it closes the exclusivity bypass of
  a closure that assigns a top-level `var` and is passed beside it.)

### Members: a method is a constant that holds a closure

A type has **one namespace** of members. A member is either an instance field, or a constant of the type
(`static`). There is nothing else, and two words say which is which: `static` belongs to the type and not to a value,
`var` may change.

- A `static fn` is a constant of the type that holds a function; `static name = value` is one that holds a value.
- A method is a constant of the type that holds a _receiver closure_ - the very same thing that powers the DSLs. It
  does not list its receiver, and a `var fn` says it changes it. `fn` is the declaration form of it (adds hoisting,
  recursion, generics, a return type annotation):

```trb
type Point {
  x: Int
  y: Int

  fn area(): Int { x * y }
  // is, structurally:
  // const area: (self: Point) => Int = { x * y }
}

const p = Point(10, 20)
p.area()                       // `value.member(args)` is `Type.member(value, args)` if the member reads its receiver
Point.area(p)                  // The constant itself: (self: Point) => Int
points.map(Point.area)
const area = p.area            // Without a call: the function value, bound to `p`: () => Int
```

- Methods live on the type, not in the instance: no memory per instance, no replacing them at runtime, and const
  value types stay plain data (`Equals`, `Hash`, `Encode`). A trait is a list of constants a type has to provide.
- An instance field can hold a function, too (`public onClick: () => Void`), and is called the same way:
  `button.onClick()`. Whether `x.name(...)` calls a method or a function in a field is not visible at the call site,
  and does not need to be.
- Because there is one namespace, a field and a method cannot share a name.

**Property commands.** A field is written only with `=`, everywhere, including inside a receiver closure - the one
command call left is a trailing block on a field whose value is a *record* type, never a function, which configures
that value in place. This is what gives the configuration DSL its Groovy look without a single hand-written setter:

```trb
port = 8080                    // Field `var port: Int`: an ordinary assignment
onStart = { print "started" }  // Field `var onStart: () => Void`: an ordinary assignment, too
database {                     // Field `var database: DatabaseConfig`: the receiver closure is applied to the
  url = "postgres://..."       //   field's value, which is configured in place
}
print "Listening on {port}"    // Reading is just the name
onStart()                      // Calling a function in a field always needs parentheses
```

`port 8080` and `onStart { print "started" }` are errors now: "`port` is a field: write `port = 8080`" and "`onStart`
is a field: write `onStart = { print \"started\" }`", the fix reconstructed from what was written. Only the trailing
block on a record field is not an assignment - `database { ... }` stays exactly as it was, because a receiver closure
configuring a value in place is not the same thing as replacing that value.

Both a plain field and the field a block configures need a `var` path. Calling a function in a field always takes
parentheses (`onClick()`, `onClick(event)`); `onClick { ... }` no longer sets it, `onClick = { ... }` does.

**Only the block on a record field is still a command call; parentheses always call.** `tls(true)` calls it - an
error, because a `Bool` field has nothing to call; `onClick({ ... })` hands the closure to the function the field holds
instead of storing it. (Decision 2026-09-22: a property command is the command form only, so no line means two things
and a parenthesized call never writes. Decision 2026-09-25: a field is written only with `=`, everywhere - the
command-form write of a plain or function-typed field is removed, and the block that configures a record field in
place is what is left of the mechanism.)

## Algebraic Data Types and Pattern Matching

Variants are declared with `case` inside of a `type`.

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(): Float {
    match self {
      .Circle(radius) => Float.pi * radius * radius
      .Rectangle(width, height) => width * height
      .Empty => 0.0
    }
  }
}

const shape = Shape.Circle(2.0)
```

**A case is written with its type (`Shape.Circle`), or with a leading dot where the type is known (`.Circle`).**
Never bare: `Circle` alone is a type, a function or a variable of that name, like every other name.

```trb
const unit: Shape = .Circle(1.0)               // The annotation says which type
shapes.append(.Empty)                          // The parameter does
if shape == .Empty { ... }                     // The other side of the comparison does
const other = Shape.Circle(1.0)                // Nothing does: write the type

use Shape.Circle, Shape.Empty                  // Or import the cases, and then they need nothing in front of them
const third = Circle(3.0)
```

- `.Case` works wherever a type is expected: annotations, arguments, fields, results, `==`, the arms of a `match`
  whose result is expected, and in patterns (the type is the one of the value that is matched).
- **A case is imported by its path:** `use Option, Option.Some, Option.None from "./option"`, and without `from` the
  path is resolved in the file's own scope (`use Shape.Circle`, which is what the file that declares `Shape` writes).
  Only cases can be imported through a type - a method, a constant or a field stays `Type.member`. That is all there is
  to `Some`, `None`, `Ok` and `Fail`: the prelude imports them from `Option` and `Result`.
- **An imported case needs nothing in front of it, in an expression and in a pattern** (`Some(found) =>`, `None =>`).
  Everything that is not imported keeps its dot or its type.
- Directly inside of the braces of a `match`, a line that starts with `.` starts an arm. Everywhere else it continues
  the expression of the line above (`.filter { ... }`). So the value of an arm that spans several lines of a call
  chain goes into a block (`=> { ... }`).

A case that wraps exactly one value, of a type that no other case of the type wraps, generates `From`. That is what
makes error types cheap - `?` converts on its own, and nobody writes `extend AppError with From<ConfigError>`:

```trb
type AppError {
  case Config(cause: ConfigError)          // AppError.from(configError) is AppError.Config(configError)
  case Io(cause: IoError)
  case Startup(message: String)
}
```

`match` is an expression and must be exhaustive. There are no "open" or "non-exhaustive" types: a public ADT is a
promise, and a new variant is a breaking change that the compiler points out at every `match`. A library that wants
to stay free to add cases does not expose the ADT. It wraps it (a single-field type with a private field) and
accepts everything that converts:

```trb
public type HttpError with Show {
  private kind: HttpErrorKind            // The ADT stays private, variants can be added at any time

  fn isTimeout(): Bool { ... }
  fn isRetryable(): Bool { ... }
}

public fn fail<Failure: Into<HttpError>>(failure: Failure): HttpError {
  failure.into()
}
```

```trb
const description = match value {
  0 => "zero"                              // Literal
  1 | 2 | 3 => "small"                     // Alternatives
  4..=9 => "medium"                        // Range
  n if n < 0 => "negative"                 // Binding + guard
  _ => "large"                             // Wildcard
}

match (shape, position) {                  // Tuples
  (.Circle(r), Point(x: 0, y: 0)) => ...   // Cases, types (positional or labeled)
  (_, Point(x, y)) => ...
}

match list {
  [] => "empty"
  [only] => "one element: {only}"
  [first, ...rest] => "{first} and {rest.length()} more"
}

match config {                             // `...` stands for the fields the pattern does not name
  Config(port: 443, ...) => "secure"
  Config(host, ...) => host
}
```

- **A pattern mirrors the constructor.** A sub-pattern without a label fills the next field from the left, a labeled one
  names the field it matches, and the labeled ones follow the positional ones - the very rule the arguments of a call
  follow. A field named twice and a label that names no field are errors.
- **A pattern that does not name every field ends in `...`.** `Config(host, ...)` matches a `Config` of any number of
  fields and binds the first one; a pattern that names fewer fields without the `...` is an error whose note writes the
  pattern with it. The `...` comes last, comes once, and binds nothing - the fields it stands for decide nothing, so
  such a pattern still covers the whole type. It is the spelling a list pattern already has for the same idea.
- **An arm that can never be reached is an error** ("This arm is never reached"), for the same reason as a dead
  change or a discarded value - with value semantics it is always a mistake, never a defensive line.

Patterns also work in bindings and conditions:

```trb
const (quotient, remainder) = divide(7, 2)
const Point(x, y) = p

if const Some(user) = findUser(id) {
  print user.name
}

while const Some(next) = pending.dequeue() { ... }

if var Some(iterator) = current { iterator.next() }     // `var` instead of `const`: the binding is mutable

for (key, value) in someMap { ... }
```

**`if var P = place` binds into the place,** exactly like a `var` parameter - it does not bind a copy. The subject
must be a `var` path, and the body runs inside a `var` access to it, so `iterator.next()` advances the iterator that
`current` holds. A `var` pattern that bound a copy would be a dead change by construction.

In a pattern `_` is the wildcard, in an expression `_` is the implicit closure parameter. The positions never overlap.

**A binding in a pattern starts with a lowercase letter, and a name that starts with an uppercase one is never a
binding.** "Uppercase" is `A` to `Z` - a name is ASCII, so there is nothing else it could be. An uppercase name is
resolved through the scope: an imported case (`None`, `Some(value)`) or a type read backwards (`Point(x, y)`). A case
that is not imported keeps its dot or its type (`.Circle`, `Shape.Circle`), and a constant is compared with a guard
(`n if n == limit`). So the trap stays closed: a misspelled `Nome` is "`Nome` is not a case in scope", never a
catch-all, and renaming a constant cannot turn an arm into one. What decides is the `use` at the top of the file, never
the expected type. An uppercase binding is a compile error, not a lint (`_Found` and `_` keep their own rule and bind).
This is the rule the spelling of *every* declaration follows from (see "Lexical Structure"): the first letter has to
mean the same thing wherever a name is written, or reading a pattern would be guesswork.

**A binding of a refutable pattern that is never read is an error.** That is an arm of a `match`, an `if const`/`if
var` and a `while const` - the positions where the pattern may fail and a name is therefore a choice. A lowercase name
always binds, so `limit =>` matches *every* value and shadows the constant `limit` instead of comparing with it; the
one thing that tells that mistake from a binding somebody meant is whether the guard or the body reads it. So an
unread one says "`limit` is never read: write `_`, or `_limit` to keep the name". A name that starts with `_` is
exempt, which is what keeps `_reason` as documentation of what an arm ignores. For alternatives it is the one binding
the body sees that counts. The irrefutable positions - `const x = ...`, destructuring, `for`, closure and function
parameters - are not part of it: nothing there can be mistaken for a comparison, so an unused one is a lint
(`torb lint`) and not an error.

## Traits

```trb
trait Shape {
  fn area(): Float                     // Required
  fn describe(): String {              // Default implementation
    "Shape with area {area()}"
  }
}

trait Compare with Equals {                        // Supertrait
  fn compare(other: Self): Ordering
}

type Square with Shape, Equals {         // Implement at the declaration...
  side: Float
  fn area(): Float { side * side }
}

extend Point with Shape {                  // ...or afterwards
  fn area(): Float { Float.from(x * y) }
}

extend String {                            // Extension methods without a trait
  fn shout(): String { "{toUpperCase()}!" }
}

extend<Item> List<Item> with Show where Item: Show { ... }   // Type parameters are declared on `extend`
```

- **Naming:** a trait is a capability the type comes _with_, so a trait with a single required method is named like
  that method: `Hash` (`hash`), `Equals`, `Compare`, `Show`, `Add`, `From`, `Length`, `Close`. `type Money with Equals,
  Hash, Compare` reads as what it is. No `-able`/`-ible` adjectives. Traits that are mainly used _as types_ are nouns:
  `Iterate`, `Iterator`, `List`, `Map`, `Accumulator`, `Stage`.
- `with` is the only keyword for "implements" and for supertraits. Bounds use `where Item: Hash & Equals` or inline `<Item: Hash>`.
- **Type parameters of a `type` and of a `trait` can have defaults** (`trait Add<Other = Self, Output = Self>`), so
  `with Add` means `Add<Self, Self>` and nobody writes it out. A default may name earlier parameters and `Self`, and
  it is filled in, never inferred. `fn` has no defaults: its type arguments come from the call.
- **A member may carry a `where` clause of its own** (`fn toSet(): Set<Item> where Item: Hash`). It is not a
  requirement for implementors - the member simply exists only where the clause holds, and a use that does not
  satisfy it reports the unmet bound. Same rule as for a conditional `extend`.
- **`Self` is allowed in every type position inside a trait,** including as a trait argument
  (`trait List<Item> with Iterate<Item>, Length`). `Self` is a type, not a type
  constructor, so this is not the `Self<U>` that ["One Vocabulary"](#one-vocabulary-instead-of-higher-kinded-types)
  rules out, and it costs nothing.
- **Coherence:** you can only `extend X with Trait<Arguments...>` if your package owns `X`, or `Trait`, or a type that
  is named as an argument of the trait. The third way is what lets a package convert **its own type into a foreign
  one** (`extend Float64 with From<Celsius>`), and it keeps the implementation unique: only the owner of `Celsius`
  reaches it that way, and a fourth package owns none of the three. Only the top level of an argument counts, and
  only a named type - `From<List<Celsius>>` is not yours.
- **Blanket implementations:** an implementation whose target is a bare type parameter
  (`extend<Source, Target> Source with Into<Target> where Target: From<Source>`) covers every type, and is allowed
  when the package owns the trait. Two implementations of one trait may never overlap. Disjointness is proved by
  different target heads, or by bounds on the same subject that no type can satisfy together - so two blanket
  implementations of one trait always overlap, whatever their bounds say.
- **An extension member is named where it is used.** A member is part of a type everywhere if the package of the *type*
  attached it - `type Circle with Shape`, an `extend` in that package, in whatever file it is written - and so is one
  the *using* package wrote itself, because a file sees what its own package declares. Everything else the file names:
  - **`extend` without a trait on a foreign type** (`extend String { fn shout() ... }` in `acme/text`) is imported
    by path, the same form as a case: `use String.shout, String.slug from "acme/text"`,
    `use Int64.seconds from "std/time"`. A generic target is named by its head (`use List.totalArea from "..."`), and
    constants and `static fn`s of an `extend` are imported the same way. `as` renames the member
    (`use String.shout as yell from "acme/text"` makes it `"x".yell()`), which is how a conflict is resolved: two
    imported members of one name for one type stay a compile error at the *use*, and the message says to rename one.
  - **The members a trait puts on a type it does not own** (`extend String with Slug` in the package of `Slug`, blanket
    implementations included) are visible where the trait itself is a name of the file: imported
    (`use Slug from "acme/slug"`), from the prelude, declared in the file, or because the static type says so (a bound
    `Item: Slug`, a trait-typed value). Milder than Rust: what the type's own package brings needs no import at all.
  - A `use` **without names is not a thing**: nothing runs when a module is imported, so `use "./text-extensions"` would
    mean nothing and is a parse error that names the form to write instead.
  - The prelude re-exports members by name (`public use Int64.seconds from "std/time"`), so `2.seconds()` is everywhere.

  This is purely a rule about which *names* are visible; coherence and dispatch are untouched, and so are the operators,
  `for`, string interpolation, `?`/`??` and `into()`, which go through traits of the prelude.
- **An `extend` adds constants and functions, nothing else.** A field or a `case` in an `extend` is an error
  ("fields and cases belong to the declaration of the type"), because exhaustiveness and the generated constructor
  have to be decidable from the declaration alone.
- Traits are implemented by values. A `shared type` can only implement a `shared trait` (`shared trait Close`), and a
  value of such a trait type counts as shared. `Close` itself, and every trait that comes `with Close`, is implemented
  by a `shared type` only: a value is copied on assignment, and two copies would close one resource twice. So a `List<Item>` or an `Iterate<Item>` is always a value: nobody
  changes it while you hold it, and it can be passed to another task.
- Several traits can be one type: `fn audit(entry: Show & Encode)`, `List<Show & Hash>`. It is the `&` of bounds in
  type position, and only traits can be combined (two different types have no values in common) - `&` is to traits
  what `|` is to literals.
- A trait can be used as a type (`fn draw(shape: Shape)`). Whether this is dispatched statically or dynamically is
  up to the implementation and not observable.
- **A trait type is the one place where the language has subtyping,** and it has exactly four coercions: a value to
  a trait it implements, a trait value to fewer bounds or to a supertrait, `Never` to anything, and a literal to a
  literal type. They apply only where a type is expected and never solve an inference variable. **There is no
  variance:** `List<Square>` is not a `List<Shape>`, the list is built as one
  (`const shapes: List<Shape> = [Square(2.0), Circle(1.0)]`).
- **A generic member can be called on a trait-typed value.** Every trait-typed value carries a witness table per
  bound, and a generic call passes one witness per bound; where no trait-typed value is involved, a back end
  monomorphizes as before. That table is also what *witnesses* a bound another declaration asks for: a trait-typed
  value satisfies a bound on any trait its own traits require, which is why `Result<Void, Error>` is `Show`
  (`trait Error with Show`, and `extend<Value: Show, Failure: Show> Result<Value, Failure> with Show`). **Object safety is checked per call, not per type:** a member that mentions `Self` in a
  parameter or in its result, or that has no `self`, cannot be called on a trait-typed value. So `List<Show & Hash>`
  and `fn audit(entry: Show & Encode)` stay legal, and only calls that have no meaning are rejected.
- Functions without `self` in a trait: without a body they are a requirement for the implementing types
  (`From.from`, `TryFrom.tryFrom`). With a body they are defaults of the trait - the place for factories that answer
  `Self` (`List.of(1, 2)` on the trait picks the default implementation, `TrieList.of(1, 2)` is a `TrieList`).
- Because a trait is a type, it can be extended like one. `extend<Item> List<Item> with Show where Item: Show` makes every list
  showable, `extend<Item> List<Item> with From<Iterate<Item>>` makes `List<Item>` itself a valid target of `to<List<Item>>()`.
- **Such an `extend` implements the trait for the trait-typed value, and reaches a concrete implementer only through the
  receiver.** `show` reads its receiver, so an `ArrayList<Int>` is showable through it: the receiver coerces to `List<Int>` and
  the member is found. `From.from` has no `self` and answers `Self`, so an `ArrayList<Int>` does **not** get it - the
  `from` would answer "some `List`" where an `ArrayList` is required. So every member of the implemented trait has to
  take `self` and none of them may answer `Self`; a `Self` in a *parameter* is fine, because there the concrete value
  coerces into the trait, which is the direction that always holds. `ArrayList` therefore has exactly one
  `From<Iterate<Int>>` - its own - and `List<Int>` has the extension's.
- **`Trait.member` reads the trait's own members first, then the members of implementations whose target is the
  trait itself.** That is what makes `List.from(...)` and `Map.from(...)` work, where `from` comes from
  `extend<Key: Hash, Value> Map<Key, Value> with From<Iterate<(Key, Value)>>`. Two such implementations are an
  ambiguity error, and the fix is to name a type (`TrieMap.from(...)`).
- **An operator is a trait exactly when it is a method call.** `+` is `Add.add`, `==` is `Equals.equals`, `<` is
  `Compare.compare`, `a[i]` is `Indexed.at`, `a[i] = v` is `MutableIndexed.set`, `a[from..to]` is `Slice.slice`,
  `a[from..to] = v` is `MutableSlice.replace`, `a ?? b` is `OrElse.orElse`, string interpolation is `Show.show`. The
  three that are **not** traits are the three that are no method call: `?.` chooses between `map` and `flatMap` by the
  type of the result, which would need the `Self<Output>` that
  ["One Vocabulary"](#one-vocabulary-instead-of-higher-kinded-types) rules out, so it stays `Option`; `?` leaves the
  *enclosing function*, which no method can do, so it stays `Option`/`Result`; `&&`, `||` and `!` short-circuit. Both
  can be opened later without a break.
- `&&`, `||` and `!` are the exception: they are built in on `Bool`, they short-circuit, and they cannot be
  overloaded. A trait method evaluates its argument, so a trait would mean something else.
- The bit operators go through `Bits` (`&` is `bitwiseAnd`, `<<` is `shiftedLeft(by:)`, ...), whose members are
  native on the integer types (see [Built-in Types](#built-in-types)).

## Types, Values and Reflection

Types and values are strictly separate worlds:

- A type never flows as a value. There is no `Type` type, no `typeof`, no `value is Value` on generic `Value`, no
  `Class.forName`. Types appear only in type positions (after `:`, in `<>`, after `with`/`where`, right of `type X =`).
  The values of the built-in types are lowercase literals (`true`, `false`, `void`), never their type name.
- The only bridges are syntactic: `Point(...)` (constructor), `Point.origin` / `Point.tryFrom(...)` (static members),
  `Shape.Circle` (variants), `Point.area` (method reference).
- So there is **no runtime reflection**. It could not be implemented identically in all back ends (monomorphized vs.
  boxed generics would become observable), it keeps every type's metadata alive in compiled binaries, and it is the
  meta-programming style this language does not want.

What reflection is usually needed for (serialization, config mapping, database rows, schemas, debug output) is covered
by one principle: **a value is its constructor call.** The compiler knows every type's constructor and offers it in
three generated forms, in the same way `Equals`, `Hash` and `Show` are generated - written (`Encode`), read (`Decode`)
and described without a value (`Describe`):

```trb
trait Encode { fn encode<Target: Encoder>(var target: Target) }
trait Decode { static fn decode<Source: Decoder>(var source: Source): Result<Self, DecodeError> }
trait Describe { static fn describe<Target: Describer>(var target: Target) }
```

```trb
type User {
  name: String
  email: Email
  tags: List<String> = []
}

const json = Json()
const text = json.encode(user)                 // fn encode<Value: Encode>(value: Value): String
const user = json.decode<User>(text)?          // fn decode<Value: Decode>(text: String): Result<Value, JsonError>
```

A type writes itself into an `Encoder`, reads itself from a `Decoder` and describes itself to a `Describer`. A format
(`Json`, `Toml`, a database driver) implements these traits and never sees a type; it is a **bound**, so every call is
monomorphized per format and the chain is direct. This is what the compiler generates for `User`, and nothing in it is
special:

```trb
extend User with Encode {
  fn encode<Target: Encoder>(var target: Target) {
    target.record "app/User"                   // the declaration's qualified name: what a format maps a type by
    target.field "name"
    name.encode target
    target.field "email"
    email.encode target
    target.field "tags"
    tags.encode target
    target.finish()
  }
}
```

`decode` reads the same fields back in the same order, and a field with a default may be missing: the default is
evaluated only then. `docs/design/ENCODING.md` is the design in full.

- **Derivation is one condition.** All three forms are generated when the constructor is usable from outside, over
  exactly its parameters, and every parameter has the trait. So what is written can always be read back. A `private`
  field with a default is no parameter from outside and is in none of the forms - a cache stays out without an
  annotation.
- **No tree in between.** Values are written while the type describes itself: no second representation of the whole
  document, sequences are streamed, nothing is lost (numbers keep their range, a `Set` comes back as a `Set` because
  the target type drives the decoding, `Decimal` and bytes are first-class).
- **No second vocabulary.** The methods of `Encoder` and `Decoder` are named after the types of the language (`bool`,
  `int`, `float`, `decimal`, `string`, `bytes`) plus the four shapes `sequence`, `map`, `record` and `variant`, which
  open, `field` for the name of the value that follows, and `finish`, which closes.
- **No dynamically typed island.** There is no "any value" type in the language. A value held without its type is an
  `EncodedValue`, built by an ordinary encoder, and who wants to look at a document without knowing its type uses a
  library type (`JsonValue` of `std/json` is an ordinary ADT).
- **A type whose constructor is closed from outside is a capsule, and its three forms come from its one
  conversion pair** instead of from its fields (see [Construction](#construction)). The pair is the one type `Source`
  for which both directions exist: `Self` has `TryFrom<Source, Failure>` or `From<Source>`, and `Source` has
  `From<Self>`; the reflexive `From<Self>` never counts. A value is written as its `Source` and read by decoding a
  `Source` and handing it to the way in, so the check the factory exists to force runs for a decoded value too, and a
  `TryFrom` that refuses becomes a `DecodeError` that carries its `show()`. With no pair or with several candidates
  there is no `Decode` and no `Describe`, and the message names the rule and the candidates; the way out stays the
  field-wise one, which cannot break an invariant and is what a message shows.
- **The adjustment ladder.** What is a convention of the format and not of the type is an option of the format
  (`Json(naming: .SnakeCase).encode(user)`); a field that is not data says so with `private` and a default; a
  different representation in every format is `encode` and `decode` written by hand. There are no annotations.
- **`Encode`/`Decode` are data binding, for every format whose model is "values, sequences, maps, records"**: JSON,
  MessagePack, CBOR, TOML, YAML, query strings, database rows. A type says once what it consists of, a format says
  once how that is written: N + M implementations instead of N × M, and a format somebody else wrote works with every
  type that exists. (`length` in `sequence` and `map` is there for the binary formats, which write it in front.)
- **What is special about one type in one format is a value in that format's own DSL.** An XML attribute, a column
  type, a field number: a format offers a mapping keyed by the qualified type name `record(typeName)` carries, and the
  type knows about none of them. No format gets a trait of its own; XML is a mapping value and a tree (`XmlNode`,
  like `JsonValue`) in `std/xml`.
- `rendered(value)` renders every `Encode` value as text, for messages and debugging, and `structureOf<Value>()` is a
  type's description as a tree, which is what a schema format walks.
- Generated code only exists where it is used, like every generic instantiation. Nothing is kept alive "just in case".

## Error Handling

There is no `null` and there are no exceptions.

```trb
fn findUser(id: Int): User? { ... }                      // Option<User>
fn loadConfig(path: String): Result<Config, IoError> { ... }

const name = findUser(1)?.name ?? "anonymous"            // Optional chaining, default

fn start(): Result<Void, AppError> {
  const config = loadConfig("app.trb")?                  // Early return on Fail. IoError -> AppError through `From`
  ...
  Ok(void)
}

fn main(): Result<Void, Error> {                         // Any error, handed up: `Error` is a trait
  start()?                                               // AppError -> Error, because `AppError` carries the trait
  Ok(void)
}

panic "unreachable"                                      // Bugs. Not catchable, aborts the program.
```

- `collection.get(i)` returns `Item?`, `collection[i]` panics when out of bounds.
- **Where a program may panic, and where it answers a `Result` or an `Option` instead** - the rule every API is
  judged against ([docs/design/PANICS.md](docs/design/PANICS.md)):
  1. **A panic is a broken promise of the program, never a property of the data it was given.** An operation may
     panic only where its precondition is something the program can state as an ordinary expression -
     `index < list.length()`, `divisor != 0`, `map.contains(key)` - and chose not to. Where the precondition is
     invisible - a byte inside a character, the encoding of bytes from outside - the operation does not exist in a
     form that can fail: a type makes it unwritable, or it answers a `Result`. So `readLine()` answers a `Result` for
     bytes that are not UTF-8, and a text is cut with `withoutPrefix`, `splitOnce` and `dropping(characters:)`
     rather than with a number counted by eye.
  2. **Every partial operation has a total twin, in the same vocabulary:** `list[i]` and `list.get(i)`,
     `list[a..b]` and `list.part(a..b)`, `a + b` and `a.addedChecked(b)`, `wholeOf` and `tryFrom`, `expect` and `??`.
     The twin stands next to the partial one, and it answers an `Option` or a `Result`.
  3. **The compiler takes back what it can prove, and nothing it cannot.** A check it proves cannot fire is not
     emitted; a failure it proves certain - `10 / 0`, `[1, 2, 3][5]`, `"abc"[5..]`, `Int.maximum + 1` - is a
     compile error. Neither changes what a program means.
  4. **A panic names the line of the program that broke the promise,** not the line of the standard library that
     noticed it: `map[key]` that finds no key says which key, and where the program asked for it.

  The short forms keep their meaning: `list[i]` says "I know `i` is inside", as in Rust and Swift, because that
  precondition is visible. What stays a panic besides them is the machine (stack, memory, the size limits, a deadlock)
  and what the program asserted itself (`expect`, `assert`, `panic`).
- **`?.` is `Option.map`, and `Option.flatMap` when the member's result is itself an `Option`.** So `?.` never
  produces a nested Option: `first()?.position()` is a `Vector2?`, whatever `position()` returns. It is not
  defined on `Result`.
- **`??` is `OrElse.orElse`, on `Option` and on `Result` alike.** The right side is `lazy` and is checked against the
  `Value`, so `Status.tryFrom(text) ?? "offline"` needs no `.ok()` in between. It is the trait that decides, not the two
  types: a type of a program that comes `with OrElse<Value>` gets `??`, and one that does not hears "`X` does not
  implement `OrElse`, so `a ?? b` has no meaning for it".
- **A panic is output, so its format is part of the language:** to standard error, `panic: <message>`, then
  `  at src/file.trb:12:5` for the panic site and, in the debug profile, the frames of the task. The exit code is
  **101**, and both back ends agree on the text to the character because the conformance suite compares it.
- **Nothing runs while a program falls over.** No `close()`, no `using` cleanup - a panic is a bug, and running more
  code in a broken program is how bugs get worse. The destructor ([docs/design/DESTRUCTORS.md](docs/design/DESTRUCTORS.md)) runs on
  releases, and a panic releases nothing: the process ends.
- **A panic aborts the process,** because the language has no supervision. The one exception is a sandboxed script: the
  VM is interpreting it and the script has a heap of its own, so the VM stops it and reports a `SandboxError`
  (see [Receiver Scripts and the Sandbox](#receiver-scripts-and-the-sandbox)).
- **`Error` is a trait, not a base type.** `public trait Error with Show { fn cause(): Error? { None } }` in the
  prelude. Every error type that carries it fits into `Result<Value, Error>`, which is the "some error, hand it up"
  signature: `?` needs no new rule for that, because a concrete failure becomes the trait value through the ordinary
  conversion of a value to a trait-typed value. **Precise error types stay the norm for libraries** - a caller can
  only `match` on what a signature names - and the trait is for the layers above, where the only thing left to do
  with a failure is to report it. Nothing is generated: `with Error` is written down, and a type that has nothing to
  add inherits the default `cause()`.
- **`cause()` is the chain.** An error that wraps another one hands it out, so a report can unwind it. That is all
  the trait promises; there is no stack in an error value, because a value would then carry something that is neither
  deterministic nor comparable.
- **A top-level `?` is not a panic.** It prints `error: <the error through Show>` and exits with 1 - and it walks
  `cause()`, one `  caused by: <...>` line per link:

  ```text
  error: the server did not start
    caused by: app.trb: no such file
  ```

  In the **debug profile** every `?` that hands an error on also records where it did, and those locations are printed
  under the chain (`  at src/config.trb:12:31`), the way Zig's error return traces work. The release profile emits
  nothing for it, so it is free there and no error type has to change.

## Collections and Iteration

The collection types are **traits**, one per kind. Signatures, fields and bindings talk about traits, an
implementation is only named where something is constructed. Collections are values like everything else: the binding
decides whether they can be changed.

**Every kind has its own words** (owner, 2026-09-22 - [docs/design/COLLECTIONS.md](docs/design/COLLECTIONS.md) section 6b): `List`
`append`, `Set` `insert`/`remove`, `Map` `set`/`remove`, `Stack` `push`/`pop`/`peek`, `Queue` `enqueue`/`dequeue`/`peek`.
"`add` for everything is a sledgehammer": on top of a stack, at the back of a list and somewhere in a set are different
meanings, and the word at the call says which one it is. There is no `Collection` trait above the five kinds.

```text
                      Iterate<Item>             Length
        ┌──────────────┬──────────────┬──────────────┬──────────────┐
     List<Item>     Set<Item>    Map<Key, Value>  Stack<Item>    Queue<Item>
     append         insert       set              push           enqueue
     removeAt       remove       remove           pop, peek      dequeue, peek
     ArrayList      TrieSet      TrieMap          ArrayStack     ArrayQueue
     TrieList       HashSet      HashMap
```

```trb
const numbers = [1, 2, 3]
var buffer = numbers                                    // A copy. The storage is shared until one of them is written to.
buffer.append(4)                                        // In place. `numbers` is still [1, 2, 3].
const more = numbers.appended(4).removed(2)             // Participles work everywhere

type Inventory {
  private(var) items: Map<String, Int> = [:]
}

fn lookup(table: Map<String, Int>): Int { ... }         // Any map. Read-only, and nobody changes it meanwhile.
fn describe<Item>(items: Iterate<Item> & Length): String { ... }    // Any collection: the smallest bound it uses
fn fill(var target: Set<Int>) { ... }                   // Fills the caller's set, whichever implementation it is

var index: Map<String, Int> = HashMap()                 // Trait as the type, implementation at construction
```

- **Iteration order is insertion order, for every `Map` and `Set` implementation.** Removing an entry does not reorder
  the rest, and an empty map shows as `[:]`. The order reaches the output through `Show`, so it is part of the language
  and not of an implementation - and insertion order is the only one a reader can predict. It costs a hash table an
  index vector (which makes iterating it faster anyway) and a trie a vector next to the table.
- **One trait per kind.** There is no `MutableList`, no `ImmutableList`, no read-only view: a `const` binding or a
  parameter without `var` _is_ the immutable list, a `var` is the mutable one, and because values are never aliased
  nobody can change a collection while somebody else reads it.
- **Verbs and participles:**

  | In place (`var fn`)                         | Changed copy (`fn`)                                    |
  |---------------------------------------------|--------------------------------------------------------|
  | `append`, `appendAll`, `insert` of a `List`  | `appended`, `appendedAll`, `inserted`                  |
  | `insert`, `insertAll` of a `Set`            | `inserted`, `insertedAll`                              |
  | `remove`, `removeAt`                        | `removed`, `removedAt`                                 |
  | `list[i] = v`, `map[key] = v` (`set`)       | `updated(i, v)`, `updated(key, v)`                     |
  | `sort(by:)`, `reverse`                      | `sorted(by:)` (a `List` again; lazy on a bare Iterate), `reversed` |
  | `removeAll`, `retainAll`                    | `union`, `intersection`, `difference`                  |

  The participles are default methods of the traits (copy, change the copy, return it), an implementation only
  writes the verbs. A `Stack` and a `Queue` have none: `pop` and `dequeue` answer the item they take, a participle
  would have to answer the pair beside the rest, and a `var` copy does that without a second word.
- **Slices:** `list[from..to]` is a `List` again that shares the storage and starts at index 0. It is a value, not a
  window: later changes of the original are not visible in it. As a `var` path it _is_ a window:
  `samples[0..100].sort { _ }`, `fill(buffer[offset..])`. The same holds for `String` and `Array`.
  A slice keeps the storage of the original alive. Implementations copy small slices of big storage on their own;
  `header.compact()` does it explicitly (`List.compact`: it gives the value a storage of its own that is exactly
  as big as needed).
- The traits do not constrain their type parameters, the implementations do: `TrieMap<Key: Hash, Value>`, a sorted
  map needs `Key: Compare`. Only the factories (`Map.of`, `Map.from`, literals) ask for `Hash`, because they pick `TrieMap`.
- Implementations are named after their data structure. Defaults: `ArrayList` (contiguous - the fastest for the
  common case), `TrieMap`/`TrieSet` (a write to a shared map copies one path instead of the whole table, so keeping
  many versions of a big map is cheap - undo, history, snapshots). Alternatives: `TrieList`, `HashMap`, `HashSet`.
  `ArrayStack` and `ArrayQueue` (ring buffer) are written in plain TorbScript. Your own implementation is a
  type `with Map<Key, Value>` and works everywhere.
- Until the tries are implemented, `TrieList`, `TrieMap` and `TrieSet` are documented aliases of the array and hash
  implementations. That is observable through performance only: iteration order and `Show` are the same either way.
- The native collections (`ArrayList`, `TrieMap`, `HashMap`, ...) are the one place where "share the storage, copy on
  write" is implemented. Every type that is built from them is a value without doing anything for it (`ArrayQueue`
  is a ring buffer in a `List`). `Array<Item, Size>` is not a collection but a small inline value, see
  [Const Parameters](#const-parameters-and-array).
- A collection is **not** an `Accumulator`, and there is no trait for "something with `add`": a container that is
  merely filled has no result of a run to give. What gathers a pipeline into one is a type beside it
  (`ListAccumulator`, `into<Target>()`), the way `Collector`/`Collectors.toList()` are in Java.
- Lists have no `+`: combining two lists is `appendedAll`, which says which end the second one goes to.
- `List`, `Set`, `Map`, `Option` and `Result` are `Show` wherever their items are, in the format of the generated
  `Show` (see [Values](#values)): `[1, 2]`, `{a, b}`, `["k": v]`, `Some(x)`.
- `List`, `Set` and `Map` are `Equals`/`Hash` wherever their items are, so a `type` with a collection field can be
  compared and be a `Map` key. A `List` is equal, and hashes, in order: two lists with the same items in a different
  order differ. A `Set` or a `Map` is equal **regardless of insertion order** - iteration order is insertion order,
  equality is not - so their hash combines entries with `bitwiseExclusiveOr` instead of folding them in order.
- `for x in xs` works with everything that is `Iterate<Item>`. **The subject is evaluated once, into a temporary,**
  so it is not an open `var` access: changing `xs` inside of the loop is safe and does not affect the loop, and the
  loop variable is a `const` copy of each item.
- **`for var element in container` changes every element in place** (decided, not yet implemented -
  [docs/design/COLLECTIONS.md](docs/design/COLLECTIONS.md) section 3.11). It binds a `var` reference to each slot for one turn of the
  body - Rust's `iter_mut`, not Swift's `for var`, which binds a mutable copy. It is sugar over `MutableIndexed` plus
  its `keys()`, so a `List`, an `Array`, a slice and the values of a `Map` (`for (key, var value) in map`) work, and a
  user container joins by implementing that one trait; a `Set` and a plain `Iterate` are rejected with a message that
  says why. The container has to be a `var` path, the body may not touch it any other way (exclusivity), and the loop
  is lowered to an index loop over element paths - no iterator object, no copy per element.
- Creation: literals, `List.of(1, 2, 3)`, `List.of(...iterable)`, `List.from(iterable)`, `iterable.toList()`,
  `HashMap()`, `Set.of(1, 2)`.
- **A collection literal adapts to the type that is expected of it,** exactly as a number literal does, and the target
  decides what is built:

  ```trb
  const numbers = [1, 2, 3]                         // The default list: ArrayList<Int>
  const unique: Set<Int> = [1, 2, 2, 3]             // Set.from([1, 2, 2, 3]) - any From<Iterate<Item>> target
  const corners: Array<Int, 4> = [1, 2, 3, 4]       // Filled inline: no list, no iterator, and exactly 4 items
  const index: Map<String, Int> = ["a": 1]          // The default map: TrieMap
  const origin: Point = [1, 2]                      // Compile error: a Point is not From<Iterate<Item>>
  ```

  So there are exactly three answers: the collection the expected type names, built directly (the trait itself, or one
  of its implementations); an `Array<Item, Size>`, whose items go into the inline slots and whose **number of items has
  to be `Size`**; and any `From<Iterate<Item>>` target, built from the list - the same targets `to<Target>()` accepts.
  Anything else is a compile error that names what a target has to be. A spread inside an array literal is only allowed
  where its operand is an `Array` too, because the size has to be known: `[...half, 3, 4]`.

### Pipelines and Collectors

Working with an `Iterate` has three parts, like in Java and Rust:

```trb
const adults = users                     // 1. A source: anything Iterate (collections, ranges, files, channels)
  .filter { _.age >= 18 }                // 2. Lazy stages: nothing runs, nothing is stored
  .sorted { _.name }
  .map { "{_.name} ({_.age})" }
  .take(10)
  .toList()                              // 3. One terminal operation pulls the values through
```

- **Stages are lazy and are values.** `map`, `filter`, `filterMap`, `mapWhile`, `flatMap`, `take`, `skip`, `takeWhile`, `zip`, `indexed`,
  `sorted` return an `Iterate` again. A pipeline can be stored, passed around, extended and iterated more than once.
  Values are pulled one by one and only as far as needed, so infinite sources (`1..`) and big files just work.
- **Terminal operations decide where the values end up:** `toList()`, `to<Set<String>>()` (any `From<Iterate<Item>>`),
  `fold`, `find`, `first`, `any`, `all`, `count`, `sum`, `joined(separator:)`, `forEach`, `for ... in` - and the
  general one, `collect`.
- `joined(separator: String = "")` is `Iterate` where `Item: Show`: `[1, 2].joined(separator: ", ")` is `"1, 2"`,
  `show()` of each item, joined. For `String` items `show()` is the text itself. The collector `joining` below is
  for the same thing plugged into a pipeline that needs a prefix or a suffix, or that combines with another
  collector; a plain join is `joined`, not `collect(joining(...))`.
- **Collectors** are reusable, composable descriptions of "what to do with the values":

```trb
const payroll = employees.collect(summing { _.salary })
const names = employees.map { _.name }.joined(separator: ", ")
const (veterans, others) = employees.collect(partitioningBy { _.age >= 40 })

const salaryByDepartment = employees.collect(groupingBy { _.department }.then(averaging { _.salary }))
const teams = employees.collect(groupingBy { _.department }.then(into<Set<Employee>>()))
```

```trb
trait Accumulator<Item, Output> {           // The description of a run AND its state: a value, so a copy is a run
  var fn add(value: Item)
  fn finish(): Output
  fn isDone(): Bool { false }
}
```

- An accumulator is written either as a fold with a final step (`collector(initial, finish: { ... }) { state, value => ... }`),
  or, if it needs more, as a type with `var` fields. There is no `start()`: `collect` fills a **copy** of what it was
  given, so one value drives as many independent runs as it is handed to, and `groupingBy(...).then(downstream)`
  copies the downstream per group.
- Accumulators are _push-based_. The same collectors therefore work for everything that produces values over time,
  not only for iterables: `channel.source().collect(counting())`, a `Source` of [Streams](#streams), event streams.
- The catch of laziness: a stage with side effects does nothing until it is pulled.
  `chunks.map { spawn { ... } }` spawns nothing, `chunks.map { spawn { ... } }.toList()` spawns everything.

This is LINQ in method form and needs nothing but closures. With [`Expression<Value>`](#quoted-expressions-expressionvalue) the very same
code runs against a database: the provider's `filter` takes an `Expression<(row: Row) => Bool>` and translates the tree to SQL
(see `examples/query-provider`).

### One Vocabulary instead of Higher-Kinded Types

`Option`, `Result`, `Task` and `Iterate` share their method names, and the names mean the same everywhere:

| Method                 | Meaning                                                        | Option | Result | Task | Iterate |
|------------------------|----------------------------------------------------------------|:------:|:------:|:----:|:--------:|
| `map`                  | Transform what is inside, keep the shape                       |   x    |   x    |  x   |    x     |
| `flatMap`              | Transform into the same shape, flatten one level               |   x    |   x    |  x   |    x     |
| `filter`               | Keep what matches                                              |   x    |        |      |    x     |
| `forEach`              | Do something with what is inside                               |   x    |   x    |      |    x     |
| `orElse` / `??`        | The value, or a fallback (`lazy`)                              |   x    |   x    |      |          |
| `toList()`             | Into the world of pipelines                                    |   x    |   x    |      |    x     |

(`Task` is part of the concurrency draft and not in the standard library yet.)

This is a convention of the standard library, not an abstraction of the language. There are **no higher-kinded types**
(`Functor<F<_>>`, `Monad`), no `Self<U>`, no F-bounded tricks. (`Self` as a trait _argument_ is fine -
`From<Iterate<Item>>` on `Self` names a type, not a type constructor, see [Traits](#traits).)

- The operations look alike but are not the same: an Option is a value and `map` runs immediately, an Iterate is a
  pipeline and `map` runs when it is pulled. An abstraction over both would hide exactly that difference.
- A kind system, partially applied type constructors (`Result<_, Failure>`) and higher-order unification would cost the
  local type inference, readable error messages and a simple mental model - for code that scripts rarely need.

What higher-kinded types are typically used for is covered by things that already exist:

| Need                                         | Solution                                                                       |
|----------------------------------------------|--------------------------------------------------------------------------------|
| Chaining fallible steps                      | `?` for Option and Result, `await()` for Task                                  |
| `List<Result<Value, Failure>>` to `Result<List<Value>, Failure>` | A collection target: `.to<Result<List<Int>, ParseError>>()`, stops at the first error |
| `List<Value?>` to `List<Value>?`                     | `.to<List<User>?>()`, stops at the first `None`                                |
| A function returning an Option as a stage    | `ids.filterMap { findUser(_) }`                                                |
| An Option or Result inside a pipeline        | `users.flatMap { _.manager.toList() }`                                         |

Both "all or nothing" targets are ordinary `From<Iterate<...>>` implementations in the standard library - no new
language feature was needed for them.

## Streams

A stream is one flow in one direction. It has two ends, and what a signature names is one of them - never "the
stream", because at any point in a program you hold one end and not both. The two ends are the asynchronous siblings of
`Iterator` and `Accumulator` and carry the same verbs, so nothing new has to be learned:

```trb
shared trait Source<Item, Failure> with Close {              // reading: `next`, like an Iterator
  var fn next(): Task<Result<Item?, Failure>>
}

shared trait Sink<Item, Failure> with Close {                // writing: `add`, like an Accumulator
  var fn add(item: Item): Task<Result<Void, Failure>>
  var fn end(): Task<Result<Void, Failure>>                   // the graceful end, answering no result
}
```

```trb
use File from "std/fs"
use * as http from "std/http"

fn importUsers(url: String, path: String): Task<Result<Int, ImportError>> {
  var response = http.get(url).await()?
  var active = response.body
    .mapFailure(ImportError.requestFailed)
    .through(Json.items<User>())               // a resumable framer: memory is one `User`
    .checked()                                 // `Result` items become the stream's failure
    .filter { _.active }
  active.count().await()
}

// The loop. `await()` and `?` are both visible, which is why there is no `for` over a source.
var lines = file.lines()
while const Some(line) = lines.next().await()? {
  print line
}
```

- **A stream has an identity and is consumed once,** so both ends are `shared type`s, and the verbs that consume are
  `var fn`s exactly as `Iterator.next` and `Accumulator.add` are. A `var fn` method may answer a `Task` because for an
  object `var` is a permission and not an exclusive access ([Identity](#identity-shared-type)). So **a source changes
  through any binding that holds it**, a `const` one included, because there is no read-only view of an object. A consequence
  worth knowing: a source belongs to the task that made it, because shared objects do not cross task boundaries.
- **Whoever reads needs the permission - now or later.** `next`, `collect`, `toList`, `count`, `find` and `into` read
  items, and `map`, `filter`, `through`, `then` and `checked` hand the source to a wrapper that reads it from then on:
  all of them are `var fn`s. One rule: a member that consumes the source is a `var fn`. A chain is one expression, because a
  change of an object is not a question of the path it is reached through ([Identity](#identity-shared-type)).

  ```trb
  const all = body.through(Json.items<User>()).checked().toList().await()?
  ```
- **A failure ends the stream and stands in the type,** on both ends. An end that cannot fail is
  `Source<Item, Never>`. After a failure a source never delivers again, and after `Ok(None)` the end is final.
- **Backpressure is the shape of the protocol.** At the reading end it is the pull - nothing is read until somebody
  asks. At the writing end it is the `await` on `add`, which finishes when the target has taken the item. There is no
  credit protocol, no high-water mark, no `poll_ready`.
- **Buffering is always a wrapper.** `sink.buffered(capacity:)` answers a `Buffered` with its own `flush()`;
  `end()` flushes and `close()` does not. "When was it actually written" has to be answerable.
- **`close()` releases what is above or below,** synchronously and without failing, which is what a destructor needs:
  it is the one the last release runs ([docs/design/DESTRUCTORS.md](docs/design/DESTRUCTORS.md)), and no program calls it. Every
  derived end holds the one it came from in a field, which its release releases after its own `close()`, so a reader that stops early (`take(5)`, a `find` that found it, an abandoned loop) never leaves a file
  handle open. `end()` is the graceful counterpart and can fail; `close()` is the abrupt one and cannot. A sink that
  is closed without being ended may have written less than it was given, and nothing ends a sink implicitly - not
  even `using`.

### The middle is written once: `Stage`

A stage does not hang on the source, it hangs on the target:

```trb
trait Stage<Input, Output> {
  fn onto<Final>(downstream: Accumulator<Output, Final>): Accumulator<Input, Final>
  fn then<Final>(other: Stage<Output, Final>): Stage<Input, Final>
}
```

It turns an accumulator into an accumulator - a transducer. Because it never asks where its values come from, **every
stage exists exactly once**, synchronously, and a pipeline is an ordinary value that can be named, stored and applied
more than once:

```trb
const activeNames: Stage<User, String> = filtering<User>({ _.active }).then(mapping { _.name })

const fromList = users.through(activeNames).toList()                 // a list
const fromBody = body.through(activeNames).toList().await()?         // an HTTP body
```

- `Accumulator` has `fn isDone(): Bool { false }` for it. A driver asks before the first value and after every
  `add`, so `taking(10)`, `first()` and `find(...)` end a pipeline over an infinite or expensive source without pulling
  one value they will not deliver.
- **The difference between the two worlds shrinks to two drivers:** `Iterate.through(stage)` (a loop) and
  `Source.through(stage)` (a loop with `await`). `collect` runs *fused* - the stage wraps the accumulator itself,
  no queue - and `next()`/`iterate()` runs through a small queue, because one value pushed in can become many coming
  out while the caller asks for one. `map`, `filter`, `take`, ... on both traits are one-liners over `through`.
- **`Accumulator` is shared between both worlds,** so every terminal operation is written once:
  `source.collect(accumulator)`, `toList`, `count`, `fold`, `find`, `forEach`.
- **What exists:** `mapping`, `filtering`, `filterMapping`, `mappingWhile`, `flatMapping`, `taking`, `takingWhile`,
  `skipping`, `indexing`, `chunking` in `std/iteration`; `lines`, `decodedText`, `encodedText` in `std/stream`; and
  whatever a format provides. `zip` reads two sources and is therefore a driver, not a stage; `sorted` collects and
  then delivers.
- **A stage that can fail answers `Result` items,** because it is synchronous and knows nothing about the stream around
  it. `source.checked()` lifts them into the stream's failure and ends it there (`where Failure: From<Problem>`); on an
  `Iterate` the existing `to<Result<List<Item>, Failure>>()` does the same job.
- **Only what must wait is asynchronous:** `Source`, `Sink`, `source.then { ... }` (a step whose function answers a
  `Task`) and `source.into(sink)`.

### Producing, and what it is made of

```trb
Source.from(items)                              // everything an Iterate has
Source.pulling { ... }                          // the closure *is* `next`
Source.produce { sink => ... }                  // a task of its own plus a channel; capacity 0 is lock-step
```

There are no generators and no `yield` (an [open question](#open-questions)); `produce` covers what they are used for,
and `capacity: 0` hands every item over directly, which is exactly a generator's behaviour. Closing the source makes
the producer's next `add` fail with `ChannelClosed`, which ends its body - that is the whole cancellation story.

- **`Channel<Item>` is a stream in memory of which one holder has both ends:** `channel.source()` is a
  `Source<Item, Never>`, `channel.sink()` a `Sink<Item, ChannelClosed>`, and the two can be handed out separately.
  Everything bidirectional - a socket, a child process, a WebSocket - is a type with a `source` and a `sink`.
- **`Bytes` is `List<UInt8>`**, an alias and not a type of its own. A chunk is a value, so whoever receives one may keep
  it: there is no borrowed buffer that has to be copied before the next `await`.
- **Chunk borders are nobody's choice,** so the stages that turn bytes into text (`lines`, `decodedText`) are resumable
  and put a character or a line back together across two chunks.
- **`Encode`/`Decode` and `Encoder`/`Decoder` stay synchronous and unchanged.** Streaming happens at the level at which
  it happens in practice - the element: `trait Format<Failure>` gives every format `items<Item>()` and
  `encoded<Item>()` as `Stage`s, and a resumable framer finds where one element ends while the element itself is decoded
  synchronously. Memory is one element. A decoder that could wait would have to be written differently for every source
  and would colour the whole standard library, and streaming a single huge value into a type saves only the text it came
  from - what that case needs is an event level (`Json.events`), which is later work.

The whole specification - the contracts of both ends, the table of what is synchronous, the drivers, and what was taken
from Rust, C#, Swift, Scala, Java, Node, Web Streams and Bun and what was not - is in
[docs/design/STREAMS.md](docs/design/STREAMS.md).

## Modules and Packages

```trb
use File from "std/fs"                               // Package import: "<owner>/<name>"
use Router from "acme/http/routing"                  // A public module of a package: "<owner>/<name>/<path>"
use Vector2 from "./math/vector2"                    // Relative import, no file extension
use * as console from "std/console"                  // Namespace import
use IoError as FileProblem, File from "std/fs"           // Any name of the list may get a local name of its own
use Option, Option.Some, Option.None from "./option"     // A case of a type, by its path
use String.shout as yell from "acme/text"                // A member another package attaches to a type, renamed
use Shape.Circle                                         // Without `from`: the path is resolved in this file's scope
public use Stack, ArrayStack from "./collections/stack"      // Re-export
```

- `src/main.trb` is what `torb run` executes, `src/lib.trb` is what other packages import.
- A path that starts with `./` or `../` is a file. Everything else starts with the name of a package:
  `"owner/name"` is its `src/lib.trb`, `"owner/name/path"` is `src/path.trb` of it. Only `public` declarations can
  be imported from another package, and only packages that `project.trb` lists as dependencies.
- **After `from` there is always a module.** A path names a case of the type it belongs to
  (`use Option.Some from "./option"`) or a member another package attaches to it with an `extend`
  (`use Int64.seconds from "std/time"`, see [Traits](#traits)); a method, a constant or a field of the type's own body
  stays `Type.member`. The type in front of the dot is a name of the **importing** file - `String` comes from the
  prelude, not from `acme/text` - and the `from` names the package the member has to come from. Without `from` the path
  is resolved in the file's own scope (`use Shape.Circle`), which is what a file that declares the type itself writes.
  There is no `use Option.*` - it is the one import form under which a file would change because a dependency gained a
  case - and no brace group.
- **A `use` always names what it imports.** `use "./text-extensions"` is a parse error: nothing runs when a module is
  imported, so a `use` without names would mean nothing at all.
- **`as` renames an import.** Any item of a `use` list may take a local name of its own (`use Option.None as Nothing
  from "./option"` works for a case too), and `public use X as Y from "..."` re-exports it under the new name. From
  there on the local name is the only one the file has: it is what shadows, what collides with a second import, and
  what a "did you mean" note offers. Nothing else changes - an alias is a name in one file, not a second export.
- **The standard library is a set of packages of the owner `std`:** `std/core`, `std/text`, `std/number`,
  `std/collections`, `std/iteration`, `std/encoding`, `std/expression`, `std/task`, `std/console`,
  `std/json`, `std/time`, `std/fs`, `std/io`, `std/process`, `std/test`, `std/http`, `std/sandbox`, ... They come with
  the toolchain and have its version, so they need no entry in `dependencies`. What a program can touch is still
  visible from its imports: no `std/fs`, no files.
- **Imports between packages may be cyclic, exactly as imports between modules may.** Nothing runs when a module is
  imported, so a cycle is only a reason not to recurse: the exports are computed to a fixpoint. The standard library
  makes use of that - `Show.show` answers a `String`, so `std/core` names `std/text`, and `String` is comparable, so
  `std/text` names `std/core`. Cutting that apart would mean one package or a `String` that is not a type of the
  library, and neither is better than a cycle nobody can observe.
- **The prelude is a package of re-exports.** `Project` has the default `prelude "std/prelude"`: the public names of
  that package (`Option`, `Result`, `List`, `Map`, `print`, `do`, ...) are in scope in every file of the project.
  `std/prelude` **declares nothing of its own** - its `src/lib.trb` is nothing but `public use ... from "std/..."`,
  including the members other packages attach to a type (`public use Int64.seconds from "std/time"`) -
  so every name in it can be imported directly as well, and that file is where "what one always needs" is decided.
  A project can name another one (a teaching subset, the vocabulary of an embedded DSL); a sandbox gives its scripts
  the prelude of the host plus the receiver.
- **The prelude is the pure part of the standard library, and capabilities are not in it.** Values, text, numbers,
  collections, pipelines, encoding, quotations, tasks, printing, `std/json` and the time *values*
  (`Duration`, `Instant`) are in scope everywhere, because unused names cost nothing and these are needed everywhere.
  `std/fs`, `std/os`, `std/process`, `std/io`, `std/http`, `std/sandbox` and `Clock` are **not**, and that is
  not about size: (1) `use File from "std/fs"` at the top of a file is the statement "this file touches files", for a
  reviewer and for `torb add`; (2) a receiver script and a sandbox are defined as "the prelude and the receiver, and
  nothing else", which would need a second, trimmed prelude if `File` were in this one; (3) on a target that has no
  file system a missing import is a compile error at one line, while a prelude name that is sometimes there is not.
- **Top-level code is only allowed in entry files (`src/main.trb`), scripts, receiver scripts and
  `tests/*.test.trb`.** A test file consists of nothing but top-level `group` and `test` calls, and the test
  framework is ordinary functions, so its files are scripts. Everything that is imported consists of declarations
  only. So there is no module initialization order, and cyclic imports are unproblematic.
- The rule is about being *imported*, not about the file name: a file that nothing imports cannot create an
  initialization order, so it is a script and may hold top-level code. That is what the twelve files of
  `examples/tour` are. The `src/lib.trb` of a named package is always a module, because it is what others import.
- In exactly those files a top-level `?` ends the program with the error, and top-level `await()` is allowed.
- Top-level `const` initializers of modules must be **compile-time evaluable**: literals, unary minus, the
  arithmetic, comparison and logical operators of the built-in number types and of `Bool`, string interpolation of
  such, tuple/list/map literals of such, constructor and case-constructor calls whose arguments are such, and other
  compile-time constants. `const frameTime = 1.0 / 60.0` and `const origin = Point(0, 0)` fit. **No function calls
  and no `native` calls** - and `+` on `String` is a function call like any other, interpolation is not - so the
  checker needs no evaluator beyond the operators it knows anyway.
- **A constant whose evaluation overflows, divides by zero or produces a `nan` from a literal expression is a compile
  error at that expression.** It is the same rule as "overflow that is written in the source is caught where it is
  written", it is free once the checker evaluates these operators anyway, and a program that cannot start is worse than
  one that does not compile.
- `public const` exports the constant. There is no `public var` at top level: a module has no mutable state.

## Configuration DSL

A _receiver closure_ is a closure whose function type names its first parameter `self`. Inside of it, names resolve against the
receiver, exactly like inside of a method. Together with command calls, trailing closures and property commands this
gives Groovy-style builders that are completely statically typed.

```trb
type DatabaseConfig {
  var url: String = ""
  var poolSize: Int = 10
}

type ServerConfig {
  var host: String = "localhost"
  var port: Int = 8080
  var database: DatabaseConfig = DatabaseConfig()
  private var routes: List<Route> = []

  // Only what is more than "set a field" or "configure a field" needs a method
  var fn route(path: String, to: String) {
    routes.append(Route(path, to))
  }
}

fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
  var config = ServerConfig()
  configure(config)
  config
}

const config = server {
  host = "0.0.0.0"                         // A field is written with `=`
  port = 8080
  database {                               // Property command: configures the field in place
    url = "postgres://localhost:5432/mydb"
    poolSize = 20
  }
  route "/health", to: "health"            // Method call
}
```

Name resolution order inside of closures and methods: local scope, then the _innermost_ receiver, then the module.
**Exactly one receiver is implicit,** in a method as in a receiver closure. To reach an outer receiver, name
the parameter (`server { s => s.database { url = "{s.host}/db" } }`). This prevents the scope leaking that Kotlin
needs `@DslMarker` for.

### Receiver Scripts and the Sandbox

A `.trb` file can be loaded as the body of a receiver closure. That makes TorbScript its own configuration format:

```trb
// config.trb
host = "0.0.0.0"
port = 8080
database {
  url = "postgres://localhost:5432/mydb"
}
for name in ["users", "orders"] {
  route "/api/{name}", to: name
}
```

```trb
const script = Sandbox.load<ServerConfig>("./config.trb")?      // Script<ServerConfig>

var config = ServerConfig()
script.apply(config)?                                           // The body of the file runs here
```

- The file is type checked against `ServerConfig` (errors with line numbers, autocompletion in the editor).
- **Loading and running are two steps, and each has its own failures.**
  `Sandbox.load<Value>(path, capabilities): Result<Script<Value>, SandboxError>` reports what is wrong with the file:
  a syntax error, a type error against the receiver, a module the script may not import.
  `Script.apply(var value: Value): Result<Void, SandboxError>` runs the body against a value of the caller's own
  and reports what went wrong while it ran: a step, memory or time limit, or a panic inside the script. A sandbox whose
  failures aborted the host would not be a sandbox - and the panic that can be recovered from is exactly the one that
  happens inside an interpreter with a heap of its own.
- The path of `Sandbox.load` is relative to the **directory of the project**, not to the file that loads it: that is
  where the program runs, and where a `config.trb` next to `project.trb` sits. `use Name from "./x"` is the other way
  round - an import is relative to the *file*, because that is a question about the source tree and not about the
  working directory.
- The script is checked as the body of `(var self: ServerConfig) => Void`, with the file scope "the prelude, and
  nothing else". The type argument is the whitelist. There is no purity analysis behind that: the prelude has no IO
  to begin with, and what a script may reach beyond it is the **module allowlist** below (`modules "std/text"`).
- **Everything else is granted at the call site,** never in a project file - whoever loads a script decides what it
  can do:

  ```trb
  const script = Sandbox.load<ServerConfig>("./config.trb") {
    modules "std/text", "std/time"             // Additional parts of the standard library
    files readOnly: "./config"                 // File system roots
    environment "APP_*"                        // Visible environment variables
    limits steps: 1_000_000, memory: 64.megabytes(), time: 2.seconds()
  }?
  ```

  Without the block a script has no IO, no network, no clock, no environment, no foreign functions and no limits
  other than the defaults. Foreign functions can never be granted to a script.
- Because the type argument is known statically, a compiled binary knows exactly which types need to be callable from
  interpreted code. No annotations or reflection needed.
- `project.trb` and `project.lock.trb` are exactly this mechanism with the receiver `Project`.

## Extensibility

The language is extended by functions, not by macros or annotations:

- Control structures are functions with closure or `lazy` parameters (`do`, `unless`, `retry`, `test`); `using` binds
  a name, so it is a binding form instead - see [Blocks and Control Flow](#blocks-and-control-flow).
- DSLs are functions with receiver closures.
- Operators are traits.
- Code that needs to be _looked at_ instead of executed (query providers, `assert`, validation rules, change
  tracking) uses [`Expression<Value>`](#quoted-expressions-expressionvalue) parameters.
- Declarations (`const`, `var`, `fn`, `type`, `trait`, `extend`, `use`) are the fixed core, because they bind names.
  They all share the uniform shape `modifier* keyword Name clauses* { body }`, which reads like a command call.
- One possible future step: compile-time functions that _produce_ types (`type Row = Schema.rowOf("users.sql")`),
  evaluated by the interpreter that exists anyway. As an addition to declarative generics, never as their
  replacement - see the note under [Type Aliases](#type-aliases) for what that replacement would cost.

AST macros are a non-goal: names in TorbScript are resolved with the help of types (receivers, named implicit
parameters), macros would have to run before name resolution. The two do not go together. `Expression<Value>` is the opposite
of a macro: it reads code _after_ it was resolved and type checked, and it cannot generate any.

## Concurrency (Draft)

```trb
fn fetchUser(id: Int): Task<Result<User, HttpError>> {
  const response = http.get("/users/{id}").await()?
  response.json<User>().await()                  // the body is a stream, so reading it is a Task too
}

const (user, posts) = both(fetchUser(1), fetchPosts(1)).await()

const task = spawn { expensiveComputation() }    // Task<Int>, runs in parallel
const result = task.await()

const channel = Channel<Int>(capacity: 8)        // a stream in memory: `channel.source()`, `channel.sink()`
```

- **Asynchrony lives in the type system, not in a keyword.** There is no `async`. A function that returns `Task<Value>`
  may call `await()`, and its body produces the `Value` - the same way a function that returns `Result` may use `?`.
  `Task` belongs to the vocabulary of `Option` and `Result` (`map`, `flatMap`, `all`).
- `await()` is a method on `Task`. It is allowed in functions that return a `Task`, in closures passed to `spawn`, and
  at the top level of entry files and scripts. Everywhere else it is a compile error: a function that waits says so
  in its return type. (A blocking `await()` anywhere would need a stack per task in every back end and invites
  deadlocks; tasks are compiled to state machines instead, identically in the interpreter and in binaries.)
- Values are passed freely between tasks: a task gets copies, so there is nothing to race for. `shared type` objects
  (and values that contain one) are confined to the task that created them; `Channel` and `Task` are the exceptions
  that connect tasks. Closures passed to `spawn` cannot capture `var` bindings. Data races are impossible by construction.
- **A task takes only what it can see is a value** (enforced). A type parameter may be filled with a `shared type`, a
  function value does not say what it captured, and a `shared type` may stand behind a `shared trait` - so a `spawn`
  closure captures none of the three, whatever the bounds say. The one function value that crosses is a function-typed
  **parameter** the `spawn` closure captures: whatever fills it crosses too, so every call of the function is held to
  the same rule (a closure it hands in may capture only what a task may take; a function value whose captures are not
  visible is refused). `isSame` asks the mirror question and takes only a `shared type` itself: on a type parameter or
  a `shared trait` value a value may stand behind it. One answer to "is this an object" per rule, and each rule takes
  the reading that cannot go wrong.
- **A `Channel` hands out the two ends of a stream:** `channel.source()` and `channel.sink()` are ordinary
  `Source`/`Sink` values, so everything of [Streams](#streams) works between two tasks without a second vocabulary. Each
  end is read or written through a `var` binding, because its verbs change it.
- **A `var fn` method may answer a `Task` when its type is shared, and only then** - see
  [Identity](#identity-shared-type). This is what lets a task change an object it holds; a value would have to write its
  change back when the call returns, which is before the task has run.
- **Every task is cancellable** ([docs/design/CONCURRENCY.md](docs/design/CONCURRENCY.md) section 8). `task.cancel()` sets a flag,
  and the task stops at its next suspension point or cancellation check: the compiler puts a check at every loop
  back-edge of a function that answers a `Task`, so a loop that never awaits stops too, while a synchronous callee runs
  to its end - there is no unwinding. Stopping releases the task's frame like a finished one's, which closes every
  object it held ("cancellation is drop"). Dropping the `Task` *handle* cancels nothing: the handle is not the frame.
- **A cancellation is passed on, not answered.** `await()` answers the `Value`: a task that awaits a task that ended
  cancelled is cancelled itself at that `await()` - its scopes end, its `using`s are closed - and whoever awaits *it*
  is cancelled in turn; at the top level of an entry file the program ends with exit code 130. So an IO line is one
  `.await()?`, the `?` being the failure of the work. `task.result()` is the one wait that answers `Fail(Cancelled)`
  instead, for a supervisor or for code that cancelled a task and wants to confirm it
  ([docs/design/CONCURRENCY.md](docs/design/CONCURRENCY.md) section 8, "The cascade").

## Foreign Functions (Draft)

`native` and `foreign` are two different things:

- `native` marks declarations that are implemented _by the compiler and its runtime_: `Array`, `String`,
  the collections, IO. Only the standard library can use it. The interpreter looks them up in a built-in table, the
  compiler links the same functions.
- In a `native type`, a required trait member without a body is a requirement on the runtime, not a missing
  implementation: `public native type Int64 with Signed, Hash {}` asks the runtime for `compare`, `hash` and the
  arithmetic. The checker records what was asked for, so a back end reports a missing intrinsic instead of losing
  it silently.
- **`native type` versus `native fn`:** a `native type` says that the representation comes from the runtime - the
  type declares no fields and has no generated constructor, and the traits after `with` come with it. `native fn`
  says that this one function does. A native type can have ordinary members written in TorbScript on top of the
  native ones, and that is the direction: natives stay few.
- **A `native` declaration the runtime does not implement yet is marked as planned, and using one is a compile error
  that names the milestone** - never a link error with a mangled name in it. The manifest of natives is therefore also
  the list of what does not exist yet (`Decimal`, `Float32` arithmetic and `std/http` today).
- **Natives stay few**, because every back end has to provide every one of them (C, the VM, later JavaScript and
  PHP). What can be written in TorbScript on top of the natives that exist is written in TorbScript; a back end may
  still replace an ordinary function of the standard library by something faster, which is its private business and
  not visible in the source.
- _Planned, after the compiler compiles itself:_ **the body of a `native fn` is IR**
  (`native fn add(other: Int64): Int64 { ... }`), and `native { ... }` is a block of IR inside of an ordinary
  function. Then the standard library itself says which operation `Int64.add` is, instead of a table of names in
  the compiler, and the kernel every back end must provide is a short list of IR intrinsics. A `native fn` without a
  body stays what the platform provides (files, clock, processes). `native` is to TorbScript what `unsafe` is to
  Rust - the one place where the guarantees do not come from the type checker (the IR verifier checks form, slot
  types and ownership, not meaning) - which is why it stays reserved for `std/` until the IR format is versioned, and
  becomes a capability like `foreign` afterwards: visible in `project.trb`, shown by `torb add`, kept in the lock file.
- `foreign` declares functions of a C library. It is available to every package:

```trb
foreign "sqlite3" {
  fn sqlite3_open(path: CString, database: Pointer<Pointer<Void>>): Int32
  fn sqlite3_close(database: Pointer<Void>): Int32
}

public shared type Database with Close {
  private handle: Pointer<Void>

  static fn open(path: String): Result<Database, SqliteError> { ... }

  var fn close() {
    sqlite3_close(handle)
  }
}
```

- The C ABI is the contract, so both back ends behave the same: the interpreter calls through a generic trampoline,
  the compiler links directly.
- Foreign types are `Pointer<Value>`, `CString`, the sized numbers and `foreign type` structs with C layout.
  Pointers are not values in the sense of the language, so they are wrapped in a `shared type` and do not leak
  into an API.
- A package that contains `foreign` declarations says so in `project.trb`, where logical library names are mapped to
  files per platform. `torb add` shows it, and a `Sandbox` never grants it:

  ```trb
  foreign {
    library "sqlite3", windows: "sqlite3.dll", linux: "libsqlite3.so.0", macos: "libsqlite3.dylib"
  }
  ```
- **Memory that C allocated is never freed by the runtime.** It is owned by a `shared type` with `Close`, and
  `using` makes the cleanup deterministic.
- **Callbacks:** a closure without captures can be passed where C expects a function pointer. Closures with captures
  cannot (they would need a lifetime that C does not know); the usual C pattern of a function pointer plus a
  user-data pointer is wrapped by the library in a `shared type`.
- `foreign type` declares a struct with C layout (field order and alignment of the platform ABI). It can only contain
  foreign types and sized numbers.

## Execution Model

```text
Source -> Parse -> Resolve + Typecheck -> Typed IR -+-> Bytecode VM          (torb run, repl, sandbox, compile-time)
                                                    +-> Native code           (torb build)
```

- One front end, one typed IR. The semantics of the language are defined on the IR. A conformance test suite runs
  every test against all back ends.
- `torb run` type checks the whole program before it starts. The IR is cached.
- `torb build` compiles the typed IR ahead of time. The first back end prints C, a back end that emits machine code
  can follow behind the same IR. Nothing in the language depends on which one it is.
- **The toolchain is written in TorbScript** (`compiler/`), the VM included, and it compiles itself from a seed
  (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)). So the language is its own first big program: what is awkward
  to write in it shows up in the compiler first.
- Binaries that use `Sandbox.load` embed the front end and the VM. Interpreted code calls compiled methods through a
  bridge that is generated for the receiver types.
- Generics may be monomorphized or boxed. Not observable (no `sizeof`, no layout, no reflection on type parameters).
- Integer overflow panics, in every back end, and so do division by zero and a remainder by zero
  (see [Built-in Types](#built-in-types)).
- **Evaluation order is source order:** the receiver first, then the arguments **in written order** - labeled arguments
  are reordered into declaration order after they were evaluated, not before - then the parameter defaults in
  declaration order. In `place = value` the subexpressions of the place come first, then the value. `&&` and `||`
  short-circuit, and `??` evaluates its right side only when it is needed. "Left to right" can only mean the order a
  reader sees; anything else makes a side effect in an argument unpredictable.
- **Tail calls are guaranteed for direct self-recursion in tail position,** which is what `retry` and every fold need,
  and which both back ends implement as the same jump to the entry block. Every other call uses the stack of its
  task, and a call that finds the stack nearly used up panics with "stack overflow": a function that calls another
  compares the stack pointer with a limit worked out once from the real bounds of the stack. An unspecified crash is
  not a semantics. There is no frame count and no `--stack-limit`: a count says nothing about how many bytes a frame
  takes, so it cannot keep a C stack from overflowing, and the check that can is one comparison per call.
- Memory: reference counting, and nothing else - no tracing collector and no cycle collector, now or later. Values
  cannot form cycles, and a closure that captures a `var` binding may not escape its scope, so only `shared type`
  objects can; [docs/design/DESTRUCTORS.md](docs/design/DESTRUCTORS.md) section 9 is how they are kept out: trees and graphs hold
  handles instead of references, a stored callback takes its owner as a receiver, a leak is reported with the types
  still alive, and `Weak<Target>` comes to `std` only if those reports show a need.
- **`close()` is the one destructor** ([docs/design/DESTRUCTORS.md](docs/design/DESTRUCTORS.md)).
  Only a `shared type` may implement `Close`; the last release runs `close()` exactly once; user code cannot call it,
  and `self` cannot escape it. A slot whose type may contain a `Close` object is released at the end of its scope, in
  reverse declaration order, and a temporary at the end of its statement - so the moment `close()` runs is a line in
  the source and not a result of the liveness pass; every other slot is still released at its last use, which nothing
  can observe. A released value closes itself first and then releases its fields in reverse declaration order.
  `close()` never fails and never awaits: a graceful end is `end()`, called and awaited explicitly, and `using` never
  awaits. A cancelled task's frame is released the same way, the last declared first.
- Value semantics say _what_ happens, not _how_. "A copy" is implemented depending on the shape of the type, and
  none of it is observable:

  | Shape                                            | Implementation                                                         |
  |--------------------------------------------------|------------------------------------------------------------------------|
  | Small, fixed size (`Point`, `Date`, tuples, `Array<Float, 16>`) | Stored inline, really copied. No reference count, mutation in place |
  | Storage on the heap (`ArrayList`, `String`, tries) | Reference counted storage. A copy shares it, a write copies it first if it is shared (copy on write). Only native types do this |
  | Types built from those (`ArrayQueue`, `World`)   | The fields are copied, their storage is shared. Nothing to implement   |
  | Recursive ADTs (trees)                           | Reference counted nodes. A write copies the path, or happens in place if nobody else holds the node |
  | Big types                                        | The compiler may box them and treat them like storage                  |

  The interpreter may box everything, the compiler lays out inline. `var` parameters and `var` paths (`a[i].x = 1`)
  are passed as references, their "copy in, copy out" meaning is never executed literally.
- `native` declarations are implemented by the runtime. The interpreter resolves them through a built-in table,
  compiled binaries link them statically. Same ABI.

## Decision Log

- **A string literal adapts to a closed list of checked types, and a template or a pattern is read verbatim**
  (2026-09-25; docs/design/URI.md section 9, docs/language/values-and-types/checked-literals.md). Where a `Path`, a
  `Uri`, a `UriReference`, a `UriTemplate`, a `Regex` or a resource type of `std/resource` is expected, a string literal
  is read by the compiler where it is written - with the type's own parser, which the compiler imports - and one that is
  none is an error at that line; a `String` value never converts. The list is closed and written in the compiler: a
  user's `TryFrom<String, _>` would make the checker run the program it checks, and a trait as the trigger would act at
  a distance. Where a `UriTemplate` or a `Regex` is expected, the literal is read without interpolation and without
  escape sequences, because its braces and backslashes are that grammar's (`"/orders/{id}"`, `"\d{2,4}"`) and `raw` is
  not needed; every other checked literal cannot be interpolated. A template typed against a record or a case
  (`route("/orders/{id}", to: Route.Order)`) is checked against its fields at the literal.
- **A panic is a broken promise of the program, never a property of its data** (2026-09-24; docs/design/PANICS.md).
  An operation panics only where its precondition is an expression the program could have written; every partial
  operation has a total twin beside it; the compiler removes the checks it proves and refuses the failures it sees;
  and a panic names the program's line. `list[i]` stays partial as in Rust and Swift, `map[key]` keeps panicking and
  names the key, text is sliced at a `TextIndex` that only a text hands out, and `readLine()` answers a `Result`.
  Variants weighed and refused: `[]` answering an `Option`, partiality marked in signatures, wrapping or
  profile-dependent overflow.
- **`a ** b` is the power, and `^` is no operator** (2026-09-23; docs/language/traits/operators.md). `**` binds tighter
  than `*` and groups to the right, goes through `Power<Exponent, Output>`, and a `-` or `!` directly on its base is
  an error that shows both readings, as JavaScript does. `^` is read as a power by half of the readers and as exclusive
  or by the other half, so it stays unused and its message names `**` and `bitwiseExclusiveOr`. With the operator
  and `Real.exponential`, `naturalLogarithm`, `logarithm(base:)` and `Real.e`, **`std/math` is gone**: every function
  of it was a second spelling of a member of the number, and a free function of `Float64` could not be generic over
  `Real` (docs/standard-library/number.md).
- **A case may stand for a fixed number, and a set of such cases is `Flags<Case>`** (2026-09-19; planned,
  docs/design/FLAGS.md). A type whose cases have no fields may give all of them a constant (`case Read = 1`), and gets
  `rawValue()` and `fromRawValue(value)`; `Flags<Case>` in `std/collections` is a set of such cases stored as one
  `UInt64` mask, with the vocabulary of a `Set`, `bits()` and `fromBits(mask)`, and every value a power of two. Bit
  masks get no operators and no construct of their own: the two pieces serve C enums, protocol codes and database
  columns as well.
- **A regular expression is a value of `std/regex`, and there is no literal for it** (2026-09-22;
  docs/design/TEXT-FORMATS.md). A `/.../` literal collides with division, and every tool that reads `.trb` would have
  to repeat the heuristic that tells them apart. The check at compile time comes from the rule that a string literal
  adapts to a checked type (docs/design/URI.md section 9): a literal where a `Regex` is expected is compiled where it
  is written, and an invalid pattern is a compile error. The engine is TorbScript with RE2's semantics, linear in the
  input. `std/yaml` reads YAML 1.2's core schema without anchors, aliases and tags.
- **A type has one form, the block.** `type Point(x: Int, y: Int)` beside `type Point { x: Int, y: Int }` would be two
  spellings close enough that every author asks which is better; a positional `type Point(Int, Int)` with `.0` is what
  a tuple is for; methods per case are a second spelling of `match self`. A `closed trait` (implementable only in its
  own package, so a `match` over its types is exhaustive) is the honest form of "cases with an identity" and is not
  planned either. Cases are not types: `Circle(1.0)` having the type `Circle` would need a join of `Circle` and
  `Rectangle` in inference, which is the subtyping the language does not have.
- **`match` always has a subject, and `_` stays both the wildcard and the implicit parameter.** `match { ... }` as a
  short form of `match _ { ... }` would be a second spelling for something the repository needs twice; `*` or `?` as
  the wildcard would give an operator another meaning, and `it` as the closure parameter was weighed and refused. The
  two uses of `_` never meet in one position.
- **The application framework of `std` is wired at compile time** (2026-09-19; planned, docs/design/FRAMEWORK.md):
  the layers and words of Spring Boot and Symfony, dependency injection through traits, constructors and a module DSL,
  routes and mappings as DSLs over quoted expressions - never annotations, reflection or scanning.
- **A branch on a compile-time constant keeps one arm, and every arm is still checked** (2026-09-23;
  docs/design/OS.md section 2, docs/language/execution/compile-time-branches.md). A `match`, an `if` or an `if const`
  whose subject is a compile-time constant - a literal, a case without fields, a tuple of constants, the operators on
  `Bool` and the integers, a module `const` or a `static` of such, and the build's `OperatingSystem.current`,
  `Architecture.current` and `ByteOrder.current` of `std/core` - is decided by the lowering: only the selected arm is
  compiled, so a function only another arm calls and a native of another operating system are not in the binary. The
  checker checks every arm on every machine and judges exhaustiveness by the type, so adding an operating system is a
  list of compile errors. A local `const` is not a constant for this rule, and neither is a top-level `const` of an
  entry file: whether an arm is compiled depends on the line the `match` is written on. A native bound to some systems
  (`availableOn` in the manifest) reached on another target is an error of that target, and `torb check --every-target`
  lowers every program once per target without a C compiler. `cfg`, build tags, a per-target source directory,
  `expect`/`actual` and a trait registry were weighed and refused: each removes code before it is checked or puts
  every system's natives into every binary.
- **A value is its constructor call, and format-specific facts live in the format** (2026-09-23;
  docs/design/ENCODING.md). `Encode`, `Decode` and `Describe` are the constructor written, read and described without a
  value; all three are derived when the constructor is usable from outside, over exactly its parameters, so a `private`
  field with a default is in none of them and a capsule goes through its conversion pair. The format is a bound
  (`encode<Target: Encoder>(var target: Target)`), so the vocabulary is one flat trait per direction and a derived form
  is a direct call chain per (type, format). A convention of a format is an option of the format
  (`Json(naming: .SnakeCase)`), what is special about one type in one format is a mapping value keyed by the qualified
  type name, and no format gets a trait of its own. `EncodedValue` replaces `Encode` as a type where a program holds a
  value without its type, and `describe(value)` is `rendered(value)`.
- **`close()` is the destructor, and a release that runs one has a line** (2026-09-22; docs/design/DESTRUCTORS.md). Only a
  `shared type` implements `Close`, the last release runs it once, user code cannot call it and `self` cannot escape
  it. A slot whose type may contain a `Close` object is released at the end of its scope in reverse declaration order,
  not at its last use, because otherwise where `close()` runs relative to the output would follow the liveness pass
  and, through the Owned/Borrowed summary of a call, the bodies of callees; every other slot keeps last-use release.
  Temporaries go at the end of their statement, fields in reverse declaration order, and a slice of storage that holds
  `Close` objects copies instead of sharing. `using` is one binding form (the function goes) and never awaits; a
  graceful end is an explicit `end().await()`. A back end with a garbage collector (JavaScript, PHP) still counts the
  types that may contain `Close`.
- **Cancellation is drop of the frame, not of the handle** (2026-09-22; docs/design/CONCURRENCY.md section 8). A cancelled
  task's frame is released at its next suspension point or cancellation check; the compiler inserts a check at every
  loop back-edge of a function that answers a `Task`, so every task is cancellable even when a loop never awaits, and a
  synchronous callee runs to its end because there is nothing to unwind. Releasing a `Task` handle still cancels
  nothing.
- **A closure that captures a `var` binding may not escape its scope** (2026-09-22), the rule `var` parameters have.
  The escaping box gave pure value code aliasing, races through `spawn`, an exclusivity bypass and cycles. State that
  has to escape is a `shared type`. Decided, not yet enforced by the checker.
- **`for var element in container`** (2026-09-22; docs/design/COLLECTIONS.md section 3.11) is a `var` reference to each slot,
  sugar over `MutableIndexed` plus `keys()`, lowered to an index loop over element paths. Not Swift's mutable copy.
  Decided, not yet implemented.
- **Every collection kind keeps its own words, and `Iterable` is `Iterate`** (owner, 2026-09-22; docs/design/COLLECTIONS.md
  section 6b). `iterate()`, `List.append`, `Set.insert`/`remove`, `Map.set`/`remove`, `Stack.push`/`pop`/`peek`,
  `Queue.enqueue`/`dequeue`/`peek`; the `Collection` trait is deleted and `Add` stays (`Plus` is no longer planned).
  "`add` for everything is a sledgehammer": on top, at the back and somewhere are different meanings. A single-method
  trait is named like its method, and nothing ends in `-able`. Participles stay where they read naturally (`List`,
  `Set`, `Map`); a `Stack` and a `Queue` have none, because `popped(): (Item, Stack)?` is a pair and a `var` copy is not.
- **A sink ends with `end()`** (was `finish()`): `finish` is the `Accumulator`'s word for the result of a run, and
  `close()` is the destructor, so the graceful end of a flow needed a word of its own.
- **An extension member is named where it is used.** Nothing a foreign package attaches is implicitly visible: a member
  belongs to the type everywhere when the package of the *type* attached it, and everywhere else the file writes
  `use String.shout from "acme/text"` or names the trait the member comes from. The earlier rule - "visible in every file
  that imports the module it is declared in, whatever it imports from it" - made a name appear because of an import that
  said nothing about it, which is the one thing an import should never do. Rust's answer is a trait per bundle; this is
  milder in two places, because what the type's own package brings needs no import at all and a single member can be
  named on its own. `as` renames a member, which replaces the namespace call form (`text.shout(value)`) - that form was
  UFCS in disguise and is gone. A `use` without names goes with it: nothing runs at an import, so it meant nothing.
- **`loop { ... }` is the endless loop, and `while true` is an error.** "Never ends" becomes a property of the syntax
  instead of a property of a condition the checker has to recognise as a literal, which is what the special case in the
  checker was. Without a `break` its type is `Never`, with one `Void`; no `break value`, which can be added later without
  a break. `torb format` rewrites the old spelling (the canon's rule `loops`).
- **An operator is a trait exactly when it is a method call**, so `a ?? b` is `OrElse.orElse` and the trait is
  `public trait OrElse<Value>` in `std/core`, in the prelude, implemented by `Option` and `Result`. `?.` and `?` stay
  what they are: the first would need `Self<Output>`, the second leaves the enclosing function. Rust's `Try` has been
  unstable since 2016 for exactly the second reason, and both can be opened later without a break.
- **`Result` is `Ok` or `Fail`** (was: `Error`): `Error` is the trait every error type implements, and `return Fail
  problem` reads as what it does. The field keeps its name (`case Fail(error: Failure)`), and so do `isError` and
  `mapError`, which are about the error the case carries.
- **The prelude is a package of re-exports over one package per area** (`std/core`, `std/text`, `std/number`,
  `std/collections`, `std/iteration`, `std/encoding`, `std/expression`, `std/task`, `std/console`), instead of a
  package that declares everything. A file then imports `std/collections` when that is what it is about, the areas and
  what depends on what are visible, and `std/prelude/src/lib.trb` is a single readable list of what is in scope
  everywhere. The cut follows what the pieces are, not what a dependency graph would need: the packages are cyclic, and
  that is allowed. **`panic` is in `std/core`** and not with `print`, because `Result.expect` and every out-of-bounds
  index need it; `Bool` is in `std/core` and not with `String`, where it only ever sat because `Char` needed it.
- **`Error` is a trait, not a base type and not a concrete error.** A base type would need inheritance, and one
  concrete type (`anyhow::Error`) would lose the precise types libraries want. The trait costs nothing to implement
  (`with Error`, and `cause()` has a default), `Result<Value, Error>` is the existing coercion of a value to a
  trait-typed value, and `cause()` is the only thing it adds. `?` return traces live in the debug profile instead of
  in the error value: a value that carried a stack would be neither deterministic nor comparable.
- Full names instead of abbreviations in the standard library: `Subtract`/`Multiply`/`Divide`/`Remainder`/`Negate`,
  `Expression<Value>`, `TypeReference`, `Result.Fail` (not `Err`), `absolute`/`squareRoot`/`ceiling`. `min`/`max` (and `minBy`/`maxBy`) stay short: they are the names people know.
  Files and modules too (`iteration`, `operators`).
- Single-method traits are named like their method (`Hash`, `Equals`, `Compare`, `Length`, `Close`), not `Hashable`,
  `Equatable`, `Comparable`. That is why the keyword is `with` and not `is` or `implements`. Traits used as types are nouns.
- Iteration is a lazy pipeline with collectors (Java streams / Rust iterators) instead of eager methods that return
  lists: streaming, early exit, infinite sources, and the target is chosen at the end. One model only - there is no
  second, eager set of methods on `List`. `toList()` is the price.
- No higher-kinded types. `Option`, `Result`, `Task`, `Iterate` share a vocabulary by convention; `traverse`/`sequence`
  are collection targets (`to<Result<List<Item>, Failure>>()`), `filterMap` bridges Option-returning functions into pipelines.
  Option is deliberately not an `Iterate`: its `map` is eager, the trait promises a lazy one.
- No `Collectable`/`FromIterator` trait: a collection target is simply `From<Iterate<Item>>`, `to<Target>()` is a typed `into()`.
- **A collection literal adapts to the expected type, to an `Array<Item, Size>` and to every `From<Iterate<Item>>`
  target** - and to nothing else, which is why `const origin: Point = [1, 2]` is an error instead of silently claiming to
  be a `Point`. A literal and `to<Target>()` therefore accept the same targets, and no new protocol was needed for it:
  **a fast path is the compiler's job for a literal** (it knows the items, so it fills an array inline and builds the
  expected implementation directly) **and a type pattern inside `from` for a value** - `from` is generic over what
  arrives, so an implementation may recognize the type it is handed and take its storage instead of iterating it, decided
  at compile time like every other generic call. Never a second trait that every collection would implement twice.
- Collectors are push-based (`Accumulator.add`), so they are not tied to `Iterate` and work for channels and streams.
- **A stream is a word, not a type: what a signature names is one of its two ends,** `Source<Item, Failure>` or
  `Sink<Item, Failure>`. They are the asynchronous siblings of `Iterator` and `Accumulator` and carry the same verbs
  (`next`, `add`), and both are `shared trait`s with `Close`, because a stream has an identity and is consumed once. A
  sink ends with `end()` (was: `finish`, which is the `Accumulator`'s word for the result of a run - the end of a flow
  is not a result). `next` and `add` are `var fn`s: for an object `var` is a permission and not an exclusive access, so
  it may stay open across an `await` (the entry on `var fn` answering a `Task` below).
- **A failure ends a stream and stands in the type, on both ends** (`Never` for an end that cannot fail), instead of in
  the item as in Rust. There, "what happens after an `Err`" has to be answered per implementation and every combinator
  exists twice.
- **Backpressure is the shape of the protocol,** not a mechanism: pulling at the source, the `await` on `add` at the
  sink. No `poll_ready`/`start_send`/`poll_flush`, no `request(n)`, no high-water mark. Buffering is an explicit wrapper
  (`sink.buffered(capacity:)`) with its own `flush()`, because "when was it actually written" has to be answerable.
- **`Stage<Input, Output>` is the middle piece, written once for both worlds.** It turns an `Accumulator<Output, Final>`
  into an `Accumulator<Input, Final>` (a transducer), so it never asks where its values come from: `map`, `filter`,
  `take`, framing and codecs exist exactly once, a pipeline is a value that fits a list as well as an HTTP body, and the
  difference between synchronous and asynchronous shrinks to two drivers. `Accumulator` has `isDone()` for early exit.
  It lives in `std/iteration`, because it is synchronous and because what it turns into what is that package's
  vocabulary. The stage factories are free functions with gerund names (`mapping`, `taking`) like the collectors,
  because a member of a trait used as a namespace cannot fix the trait's own type arguments for a stage whose answer
  says nothing about `Output`.
- **Making everything asynchronous was rejected** (fs2, Web Streams): it would make `list.map(...).toList()` answer a
  task. So was effect polymorphism over both worlds - that is Rust's unsolved "keyword generics", and the language has
  no higher-kinded types on purpose. `Stage` shares the logic without sharing the names.
- **`Encode`/`Decode` stay synchronous.** Streaming happens at the element, through a resumable framer
  (`Format.items<Item>()`); a decoder that could wait would colour every derived implementation in the language, and
  streaming one huge value into a type saves only the text it came from.
- **`Channel` hands out its two ends** (`source()`, `sink()`) instead of having `send`/`receive`/`close`/`collect`. Two
  natives instead of four, a producer never sees the reading end, and everything bidirectional is a type with a `source`
  and a `sink`.
- **No `for` over a source in v1:** a `for` head has no place for the `?` that the pull needs, and
  `while const Some(item) = source.next().await()? { ... }` keeps both `await` and `?` where they happen.
- **A `var fn` method of a `shared type` may answer a `Task`; for a value it is an error.** "A `var` access cannot
  stay open across an `await`" holds for values, where `var` is an exclusive in-out access that ends with the call. For an
  object `var` is a permission, and the language already hands out two `var` paths to one object. So `Source.next` and
  `Sink.add` are `var fn`s like their synchronous siblings, every stateful end of `std/stream` is an ordinary shared
  type with `var` fields, and *reading* a stream needs a `var` binding while *wrapping* one hands it over and only
  reads - which is what keeps a pipeline one expression.
- **Every type can be made from a `Never`** (`extend<Target> Target with From<Never>` in `std/core`), so `?` works on a
  `Result<Value, Never>`. The conversion is total and its body is forced, and without it the infallible case - the
  reading end of a `Channel` - would be the awkward one.
- **A freshly produced object is a `var` path, and there is no "handed over" modifier.** "A temporary is not a `var`
  path" protects values: the change would be lost with the copy. An object has no copy and nobody else holds a view of
  what was just made, so the full permission is the caller's to give - which is what lets every stream member that reads
  be a `var fn` and a pipeline still be one expression. With that, "a read-only view cannot be widened" holds in all
  four places (binding, field through the generated constructor, argument, trait-typed value of a `shared trait`) and
  the standard library needs none of the gaps that were there before. The view stays a promise about a *path*: a
  function that hands an object out hands out a `var`, because there are no const types.
- `const` instead of `val` as it is clearer (reading many `val` with `var` in between lets you easily miss some)
- `.trb` instead of `.scr` (`.scr` is an executable screensaver on Windows and blocked by mail filters/AV)
- `//`, `/* */`, `/** */` for docs. Block comments do not nest (they did at first: a `/*` inside of a doc comment, as
  in a glob pattern, then opens a comment nobody sees). Editors comment out code with `//`. `#` stays reserved.
- One naming scheme for primitives (`Int`, `Float`, `Bool`, `String`), no lowercase aliases. Casing is enforced by the
  checker (was: a convention for a linter), because the first letter of a name in a pattern already decides whether it
  binds or names a case - see [Lexical Structure](#lexical-structure).
- Length numeric names (`Int32`) instead of C-style names (`Short`, `Long`, `Double`): the C# scheme pins `Int` to
  32 bit, but the default integer should be 64 bit in a scripting language.
- The numeric types themselves always carry their width (`Int64`, never a special unsuffixed type). `Int`, `UInt`,
  `Float` are plain prelude aliases, declared with the same `type X = Y` syntax everybody can use. No `Byte`.
- No `alias` keyword: `type X = Y` names an existing type, `type X { }` declares a new one. Not `const X = Y`,
  because types are not compile-time values (see the note under "Type Aliases").
- Types are not compile-time values. Generics stay declarative (`<Item: Bound>`), so they can be inferred and checked
  at the declaration.
- `Expression<Value>` is part of the language from the start. The quotation carries the static tree _and_ the ordinary value,
  so there is no runtime `compile()` (C#) and no interpreter in binaries because of it. Captures are separate from
  the tree (`captures()`), so the tree is a compile-time constant. Quoting an expression without captures is free;
  the captures are collected at the quotation site, which is one small allocation per evaluation.
- Types and values are strictly separate, no runtime reflection. Generated `Encode`/`Decode` replace the usual
  reflection use cases. `Decode` is not generated for types with a private constructor, so invariants survive
  deserialization.
- `Encode`/`Decode` are event-based (a type describes itself to an `Encoder`, like serde) instead of converting to a
  `Data` tree (was: `ToData`/`FromData` over a closed `Data` ADT). The tree had a parallel vocabulary
  (`Boolean`/`Integer`/`Text`/`Sequence`), built every document twice, lost precision, and was a dynamically typed
  island. Document trees are library types now (`JsonValue`).
- No uniform function call syntax. `value.f(x)` never means `f(value, x)`: it would turn every function name into a
  possible member, against "one namespace of members" and against the visibility rule of extensions, and it would be
  a second way next to `extend`. Who wants `parser.expression()` writes `extend Parser { var fn expression() ... }`.
- No overloading by parameter type. A call has one signature, and that signature says how its arguments are read:
  `.Case`, `None`, a list literal as an `Array`, `{ _ + 1 }`, a `lazy` or quoted parameter, a `var` place and the
  receiver of a receiver closure all get their meaning from the one parameter they are passed to. With several
  candidates the argument would decide the candidate and the candidate the argument. The language has the two forms
  that stay coherent: a name per **receiver** (`List.first`, `Queue.first` - methods and `extend`) and a trait with a
  **parameter** (`From<Source>`, `Multiply<Other, Output>`: `Int.from(small)`, `matrix * vector`, `matrix * 2.0`).
  Arity is a default parameter, and a second constructor is a named static function (`Color.hex("...")`).
- **Decided and in force:** a member of a type says two things with two words. `static` says it belongs to the
  type and not to a value (`static origin = Point(0, 0)`, `static fn square(size: Int): Self`); `var` says it may
  change (`var y: Int`, `var fn translate(deltaX: Int)`). A method does not list `self`: its parameter list is what
  the caller writes, `self` is still an expression inside the body, and a function *type* keeps it
  (`(self: Point) => Int`, `(var self: Config) => Void`), which is what a receiver closure is. `const` is optional on
  a field and on a static value (`x: Int` is `const x: Int`), exactly as a parameter is constant unless it says `var`;
  `static var` does not exist, because there is no global mutable state. Before this, `const` in a type body meant
  "of the type" while `var` meant "a mutable field", and a function was static by *not* declaring `self` - two
  questions answered by one word, and one question answered in two ways.
- No `default` keyword and no `Default` trait. A value of a type comes from its constructor and from nowhere else:
  a zeroed value (`default(T)` of C#) skips the constructor, which breaks every validated type and is `null` under
  another name. A `Default` trait puts five meanings under one word, and each has its own: a configuration is
  **field defaults** (`Config(port: 1)`), a replacement is written where it is used (`value ?? 0`,
  `groups.getOrInsert(key, [])`), the neutral element is the **literal** (`0` and `1` adapt to a `Scalar: Numeric`),
  a missing field of a decoded record is its field default, and an absent value is `None`. `Value: Default` as a bound
  says "any value", which is not what an algorithm asks for: `sum` wants zero and `product` wants one.
- `native` bodies will be IR and stay reserved for `std/` until the IR format is stable, then become a capability like
  `foreign` (no second keyword for "unstable"). Natives stay few: a fast path of a back end is not a `native`.
- One generic `Encode`/`Decode` instead of a pair per format (`JsonEncode`, `TomlEncode`, ...): a pair per format is
  N × M implementations and a new format could not be retrofitted onto foreign types (coherence). Document formats
  (XML, HTML) get their own traits in addition, because their model does not fit into values, sequences, maps and records.
- Tests use `assert` only. It takes an `Expression<Bool>`, so a failure shows the source and the values of both
  sides; there is no matcher vocabulary (`expect(x).toEqual y`) to learn. `test` and `group` are ordinary functions.
- `Show` is a `shared trait` (objects can be printed), `Equals` and `Hash` are not: `==` always means content,
  identity is `isSame(a, b)`.
- `List.hash` folds its items in order (`combineHashes`, a wrapping native next to `Hash`); `Set.hash` and `Map.hash`
  combine their entries with `bitwiseExclusiveOr` instead, because their equality does not see insertion order and
  their hash must not either.
- Dependencies are `runtime` or `development`, nothing else (was: `always`/`optional`/`dev`/`test`/`suggest`).
  Optional integrations are separate packages.
- The capabilities of a `Sandbox` are a closed list defined by the runtime. What a library wants to offer to a script
  goes through the receiver type, which already is the whitelist.
- Literal defaults (`Int64`, `Float64`) are fixed and do not follow a shadowed `Int` alias
- No `opaque alias`. Distinct types are single-field `type`s plus trait delegation (`with (Add, Compare) by value`).
  Scala-3-style opaque types (transparent inside the declaring scope, opaque outside) make type identity depend on
  the scope, which hurts error messages and the coherence rules of `extend`. Go-style `type X Y` would need its own
  rules for construction, visibility and which operations carry over - the `type` already has all of them.
- `<>` for generics, disambiguated syntactically by the token after `>`. Not symbol-table based (the parser would
  need name resolution, which breaks formatter/highlighter and fights with hoisting), and not `[]` (`foo[Int](x)`
  vs. `foo[i](x)` would both be valid and truly ambiguous, because `[]` is already indexing).
- `fn` keyword for all function declarations (`name(args) { }` collides with a call plus trailing closure)
- `{ }` in expression position is always a closure, immediate blocks use `do { }`
- One closure form. No arrow lambdas (`(x) => x * 2`): they needed a special rule (`=> {` starts a block, the JS
  object-literal trap), unbounded lookahead to tell `(a, b) => ...` from a tuple, and a style decision at every use.
  Typed parameters go into the braces (`{ x: Int => x * 2 }`), return type annotations are what local `fn` is for.
  The only `=> {` that starts a block is a match arm.
- No struct literals (`Point { x: 1 }` collides with trailing closures). Generated constructors with labeled arguments instead.
- No hand-written constructors, no logic in constructors. Factories, `parse`, `From`/`Into`/`TryFrom`.
- Labeled arguments use `:` (`=` would collide with assignment)
- `route "/users", to: "users"` is a labeled argument, there are no infix word chains
- Command calls only in command position, arguments cannot start with `(`, `[`, `-`, `!` (no whitespace sensitivity).
  Commands do not nest (was: `a b c` is `a(b(c))`), so "the `{` belongs to the command" has no exception.
- The standard library is not one package but many, of the owner `std`, and the prelude is one of them, named by a
  default in `project.trb`. So `"std/fs"` is an ordinary `owner/name` and no special case of the import rules, and
  capabilities stay visible per import. Workspaces (one root, many projects, one lock file) because the toolchain is
  the first project that needs them.
- Self-hosting: the toolchain is a TorbScript project that compiles itself from a seed. Everything that lasts (type
  checker, IR, back ends, VM, tools) is written once, in TorbScript. The first native back end emits C, because the
  shortest path to "the compiler compiles itself" wins.
- Cases are `Type.Case` or `.Case`, never bare (Swift). Before, a bare name in a pattern was a case if one was in
  scope and a binding otherwise, and inside of a type its cases shadowed types of the same name
  (`case Keyword(keyword: Keyword)`). Now names mean one thing. The price is a rule for line breaks: a leading `.`
  continues the line above, except directly inside of a `match`, where it starts an arm.
- **A case is imported by its path** (`use Option, Option.Some from "./option"`), so behind `from` there is always a
  module - one step instead of two, and the import reads like the name in the code. Rejected: `use Option.*`, the one
  import form under which a file changes because a dependency gained a case, and `use Option.{Some, None}`, a second
  grammar for a repeated `Option.`.
- **In a pattern the first letter decides:** an uppercase name is a case that is in scope or a type, a lowercase one is
  a binding, and an uppercase binding is a compile error. That is what lets `Some(found)` and `None` read the same way
  (before, the parentheses made the difference), and it keeps the trap that "cases are never bare" closed - what a
  pattern name means hangs on the `use` at the top of the file, never on the expected type. Checked: Rust (a bare name
  over the scope, where a misspelled case becomes a catch-all), Haskell and Elm (capitalization decides, which is this),
  and "the case of the expected type wins" for expressions, which would make `Transform(a, b)` mean different things in
  different places.
- Doc comments on every declaration (parameters, fields, cases) instead of `@param` tags or YAML front matter;
  Markdown with conventional headings; examples are run by `torb test`. A tag language would be annotations through
  the back door.
- Extensions of a foreign type are named where they are used - one member per `use` line, or the trait they come with
  (Rust's model, milder) - and extensions of an own type are part of the type.
- Keywords are ordinary names after `.` and as labels
- Standard streams are functions: `print`, `printError` (prelude), `readLine()` (`std/io`)
- The test API stays `test`, `group`, `assert`. Shared setup is a `const` in the group or a function, an async test
  returns its `Task`. No `beforeEach`/`afterEach`: hidden state between tests is what they produce
- Literal types, restricted to unions of literals of one base type and typed by expectation only. That keeps what
  makes them attractive (`transport: "tcp" | "udp"`) and leaves out what makes them expensive in TypeScript
  (a type per literal, widening, subtyping). No unions of types: they would have to be told apart by type at runtime.
- Const parameters without arithmetic, from the start, because `Array` has its size in the type (inline, no heap).
  Growing storage is `List`/`ArrayList`; there is no public raw buffer type, native collections manage their own.
- `TraitA & TraitB` as a type; named tuples; `From` is generated for cases that wrap one value of a unique type
- The parser (milestone 1 of the implementation, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)) checks every
  example. What it found the first time: `print [a, b].map(...)` (indexing by the rules of command calls),
  `print list.filter { ... }.collect(...)` (the `{` belongs to `print`), a parameter named `type`, and two rules the
  concept did not spell out (a `{` on its own line, blocks as the bodies of match arms).
- No `open type` (ADTs whose matches need a `_` arm, was used for `ExpressionNode`). One kind of ADT, exhaustive
  matches without exception. Evolving APIs hide their ADT behind a single-field type and take `Into<...>`.
- A command on a field never calls it (`onStart { ... }` assigns). Removes the one ambiguity of property commands.
- Strings have no `length()` and no `text[i]`: `chars()`, `bytes()`, `byteLength()` say what is counted; positions
  come from searching (`indexOf`) and are byte offsets for slicing. Identical and O(1) in every back end.
- No `async` keyword. The return type `Task<Value>` is what makes a function asynchronous (was: `async fn`).
- Sandbox capabilities are granted at the call site of `Sandbox.load`, not in project configuration
- `foreign` (C ABI, for everybody) next to `native` (implemented by the runtime, standard library only)
- Dead changes are compile errors (changed but never read, discarded result of a `self` method)
- `shared type`s only implement `shared trait`s, so a trait-typed value is a value
- No cycle collector and no `weak` keyword (was: a cycle collector for `shared type` objects instead of `weak`
  references). A tracing pass would make the moment a destructor runs non-deterministic again; trees hold handles,
  stored callbacks are receiver closures, leaks are reported by type, and `Weak<Target>` in `std` only if needed
  (docs/design/DESTRUCTORS.md section 9)
- Type parameters are written out (`Item`, `Key`, `Value`, `Failure`, `Output`)
- `nameOf(expression)` through `Expression<Value>`, `typeName<Value>()` as a compile-time function
- Variadics never unpack implicitly, spread (`...`) works on `Iterate`
- `static` marks what belongs to the type, `var fn` marks mutation, access through the receiver is implicit
- Members are public by default, `private` is explicit - one rule for fields and methods (was: fields private,
  methods public; 142 of 160 fields in the examples had to say `public`). Immutability made private-by-default
  pointless for reading. Top-level declarations stay opt-in (`public`).
- `private(var) x` (read for everyone, write for the type only) instead of getters/setters. Not `private(set)` as in
  Swift (there is no `set` in this language, the thing that is private is the `var`), and not an own keyword
  (`guarded` was tried: one more word to learn for something the existing two words already say). It removed
  every "private field plus accessor method of nearly the same name" pair from the examples.
- **`private` reaches one file, not one package.** An `extend Path` in another file of `std/path` could otherwise
  write `Self(rootValue: ..., componentValues: ...)` and walk around the parser that is the only way into the
  capsule. The package stays the unit of coherence; it is not the unit of privacy. It is also *one* rule instead of
  two: a `private` member and a `private` top-level declaration now reach exactly as far as each other, so a free
  function of the declaring file sees the private member as well - the border "only the type body and an `extend`"
  is gone. Measured over the repository when the rule went on: one place, an `extend Column<Node>` of
  `examples/ecs-probe-2` that wrote a `private(var)` field of another file, and it goes through the public members
  now.
- No properties, no computed getters, no validating setters (a setter cannot return a `Result`)
- Fields are `const` by default
- **Mutable value semantics: every `type` is a value, the binding decides about mutation** (Swift structs, Hylo).
  Was: immutable `type` plus mutable reference `var type`, which tripled every concept that exists in both worlds
  (`List`/`MutableList`/`ListView`, builders next to their results, snapshots with `toList()`), and made "is this a
  `type` or a `var type`?" the first question for every type. Now there is one rule - no `var` path, no mutation - and
  because values are never aliased, a `const` really never changes. The price is the copy trap
  (`var x = list[0]` is a copy), which is local and lintable; aliasing bugs are neither.
- `shared type` for identity (was `var type`, which would be misleading now that every type can be changed through a
  `var`). Same mutation rule as for values - unlike Swift, where `let` does not protect the content of a class.
  No inheritance, no `weak`, no actors (for now).
- References are second-class (`var` parameters and `var fn` only) instead of lifetimes, borrow checking or span
  types. A mutable slice is a `var` path to a range.
- No marker for `var` arguments at the call site (`fill(buffer)`, not `fill(var buffer)`): the signature and the
  tooling show it, and a marker would make DSLs and method calls inconsistent (`buffer.append(1)` has none either).
- Verbs change in place, participles return a changed copy (`sort`/`sorted`, `append`/`appended`). Rejected: one name
  plus a `copy { ... }` block, Ruby's `!` suffix, Scala's symbolic operators.
- Collection types are traits, one per kind (`List`, `Set`, `Map`, `Stack`, `Queue`, each `with Iterate<Item>, Length`),
  implementations are named after their data structure. Defaults: `ArrayList`, `TrieMap`, `TrieSet`.
- The lazy stage `sortBy` became `sorted(by:)` to fit the verb/participle rule; `list.sort(by:)` sorts in place.
- `Option`/`Result`/`?` instead of exceptions (also: trivial to implement identically in VM and AOT)
- `with` is the only keyword for trait implementation (`implements` is gone), bounds use `where Item: Trait`
- Orphan rule for `extend ... with`, counting a type named as an argument of the trait as owning it (Rust RFC 2451 is
  the model; the stricter reading, so a nested `List<Mine>` does not count)
- A trait that has a blanket implementation is never implemented by hand, and the message is built from the blanket's
  own `where` clause with the concrete types put in. `Into` is a rule of its own (Rust's `from_over_into`, decided
  2026-09-23): `extend Celsius with Into<Float64>` answers "Implement `From<Celsius>` for `Float64` instead; `Into`
  comes from it", whatever else holds, because coherence allows the `From` wherever it allows the `Into`
- **No trait for text.** A value is read from text through `TryFrom<String, Failure>` like any other fallible
  conversion, so there is exactly one way and no rule anybody has to watch. Rejected: a `Parse<Failure>` trait beside
  `Show`. `Show` is the language's `toString()` - display and debugging in one - and not the partner of a parser; the
  derived `Show` of a record reads nothing back either, so the pairing was the weaker argument, and keeping
  `TryFrom<String, _>` and `Parse` apart would have needed a lint to hold. A function named `parse` belongs to a
  **format** (`Json().parse`) and stays. Under a `?` the annotation says the target alone and the failure follows from
  the one `TryFrom` the target has for that source (`const port: Int = text.tryInto()?`)
- One member namespace. A method is structurally a constant of the type that holds a receiver closure, `fn` is its
  declaration form. Not a per-instance field: methods cost no memory per instance, cannot be swapped at runtime, and
  value types stay plain data. (Rejected: separate namespaces for fields and methods, Java style.)
- Property commands make the DSL work with one namespace: a field is written with `=` (`port = 8080`), and the one
  command call left on a field configures it in place (`database { ... }`). No hand-written setters or section
  methods. (Decision 2026-09-25: a field is written only with `=`, everywhere - was: a command call on a field wrote
  it, `port 8080`. Removed because a receiver closure writing every field by looking like a fresh declaration was the
  one place the language read differently from every other block of statements.)
- Only the innermost receiver is implicit (instead of an annotation like `@DslMarker`)
- `await()` is a postfix method (composes with `?` and chaining)
- No AST macros, no annotations (for now)
- **The one value of `Void` is the keyword literal `void`** (`Ok(void)`, `return void`), and `Void`, `Never` and
  `Range<Value>` are ordinary declarations of the standard library. A built-in type says its values in lowercase, as
  `Bool` says `true` and `false`; an uppercase name is a type or a case, without exception. Rejected: `Void` as its own
  value (it was the one name in the language that was both, and an exception costs more than a keyword), and a
  zero-field constructor call `Void()` (it would make the only value of the language the one that has to be called into
  existence).
- A temporary is a valid _argument_ for a `var` parameter and still not a valid _base of a path_.
  `drain(File.open(path)?)` is not an exception to "no dead changes": the callee is the only owner, so there
  is nowhere the change could have to be written back to.
- `if var P = place` binds into the place, exactly like a `var` parameter. A mutable copy would make every `if var`
  a dead change by construction, starting with `FlatMappedIterator`.
- Blanket implementations (`extend<Source, Target> Source with Into<Target> where Target: From<Source>`) are
  allowed for traits the package owns. Overlap is decided conservatively - different target heads, or bounds no
  type can satisfy together - so two blanket implementations of one trait always collide. Conservative keeps
  resolution decidable and the error messages readable.
- Generic members on a trait-typed value work through witness tables, one per bound, and object safety is checked
  per call, not per type. So `List<Show & Hash>` stays a type and only the calls that have no meaning are rejected.
- Coercion to a trait type is the only subtyping in the language: four coercions, never solving an inference
  variable, and no variance (`List<Square>` is not a `List<Shape>`). Written down because it is where inference and
  error messages would otherwise become unpredictable.
- Labels in patterns are kept in the tree and checked; fields still match by position. A label that is not checked
  is worse than none - `Point(y: 0, x: 1)` must not silently match something else.
- An expression statement must have the type `Void` or `Never`, unless the call has a `var` receiver or a `var`
  argument. One rule instead of a list of discarded-value cases, and everything a `var` makes effectful stays
  writable (`parser.bump()`).
- A captured `var` binding follows the rule of a reference: a closure that captures one does not escape (only the
  argument of a parameter that just calls it), and it is exempt from the dead-change rule. Was: a shared box that may
  escape - which let two copies of a value share a variable, let a bound closure change a binding during a `var`
  access to it, carried a variable into a task through a function value, and leaked a closure that captured itself.
- **The `var` access of a call begins after all of its arguments have been evaluated** (the model of Swift), not when
  the path is formed. So the reads inside the arguments have ended before it starts, and
  `items.removeAt(items.length() - 1)` and `f(checker, checker.count)` are legal; what is left is what really overlaps
  in time - two `var` accesses of the same call, and a closure argument of the call, which runs inside the access.
  The stricter reading rejected the most ordinary line there is ("pop the last item") and its fix - hoisting the read
  into a `const` - bought no safety, because the copy is what the argument would have been anyway.
- An unreachable `match` arm is an error. Dead changes and discarded values are errors for the same reason.
- Compile-time evaluable is: literals, the operators of the built-in numbers and of `Bool`, interpolation,
  collection literals, constructor calls - and no function calls. So `const frameTime = 1.0 / 60.0` and
  `const origin = Point(0, 0)` work, and the checker needs no evaluator of its own.
- `Trait.member` also finds the members of implementations whose target is the trait itself. That is all
  `List.from` and `Map.from` are (`List.of` is the trait's own); a trait is a namespace because it is a type.
- Evaluation order is source order, spelled out: receiver, then the arguments as they are written, then the defaults
  in declaration order; the place before the value in an assignment. "Left to right" can only mean the order a reader
  sees, and the reorder of labeled arguments into declaration order happens after the evaluation, into slots.
- Integer division truncates toward zero and the remainder takes the sign of the dividend, division and remainder by
  zero panic, and so does the smallest signed value divided by `-1`. C11 and Rust agree on exactly this, so every back
  end gets it for one instruction instead of a correction.
- Bit operators `&`, `|`, `^`, `~`, `<<`, `>>` on the levels of Go and Swift (`&` like `*`, `|` and `^` like `+`, a shift
  between `*` and `**`), desugared to the `Bits` trait the integer types come `with`, plus
  `addedWrapping`/`multipliedWrapping` on `UInt64` (2026-09-25; replaced "no bit operators, only methods"). C's
  precedence is refused for its trap (`x & 1 == 0`); `&` and `|` keep their meaning in a type, which the parser reads
  apart from an expression. Keeping the wrapping pair off the signed types keeps "overflow panics" true wherever a `+`
  is written.
- `Show` of a `Float` is the shortest decimal that parses back to the same value, with `.0` appended when it has
  neither `.` nor `e`. Two implementations are compared through `Show`, and `printf("%.17g")` is neither shortest nor
  the same across libcs, so the runtime carries its own conversion.
- On floats, every *operator* stays IEEE-754 - `==`, `<`, `<=`, `>`, `>=`, all of them `false` next to a `nan` - and
  `compare` is a total order with `nan` on top and `-0.0` equal to `0.0`. A "fixed" equality would make `==` disagree
  with `<`, and `sorted` must not depend on the pivot. It is the one type where the operator and the member behind it
  disagree, so everything that orders values calls `compare`. Floats are not `Hash`, so a `nan` key cannot happen.
- `Char.toUpperCase` and `Char.toLowerCase` are the *simple* case mapping - one code point in, one code point out -
  over ASCII and the letters of Latin-1, and every other code point is answered unchanged. A `Char` cannot hold the `SS`
  that `ß` upper-cases to, so answering `'S'` would be wrong and answering a `String` would make the type of the result
  depend on the value. Full Unicode tables are milestone 8, and a compiled program would have to carry them.
- `Map` and `Set` iterate in insertion order, in every implementation. The order reaches the output through `Show`, so
  it is language, not implementation - and it is the only order a reader can predict. It costs an index vector.
- A panic prints `panic: <message>` and the site to stderr and exits with 101; nothing else runs (no `close()`). The
  message is output, so the conformance suite compares it. A top-level `?` is not a panic: `error: ...` and exit
  code 1.
- ~~No destructors. `Close` is a method, `using` a function, and the only observable destruction order is the nesting
  of `using` blocks.~~ **Superseded:** `close()` is the destructor, run by the last release, on a `shared type` only
  (docs/design/DESTRUCTORS.md, and the entries at the top of this log). The three objections have answers there: the slot is
  its own drop flag, fields go in reverse declaration order, and a panic runs no `close()`.
- `Sandbox.load` returns a `Script<Value>` instead of a closure, and `Script.apply(var value: Value)` returns a
  `Result<Void, SandboxError>`. A closure of type `(var self: Value) => Void` has nowhere to say that the step limit
  was hit or that the script panicked, and a sandbox whose failures abort the host is not a sandbox.
- A parameter default is evaluated at the call site, at every call, in the scope of the declaration - the same rule as
  for a field default. One rule for both kinds of default, and a default cannot depend on an invisible argument order.
- Tail calls are guaranteed for direct self-recursion in tail position only, and running out of stack panics with
  "stack overflow". Portable C cannot guarantee a general tail call; the guarantee that can be kept is the one `retry`
  and every fold need. The limit is the stack itself, checked against its real bounds at the entry of every function
  that calls another (decided 2026-09-22, instead of the frame counter of 100 000 that was never built: a count cannot
  keep frames of unknown size inside a C stack).
- Multi-line strings are dedented by their first line: the indentation of the first line with content is stripped
  from every line, a lesser or mismatched indentation is a lexer error, and a leading line break right after `"""`
  is never part of the string. This lets a code block sit at the indentation of the call around it instead of at
  the left margin. `torb format` will enforce the layout it produces.
- `String.join(parts, separator:)` became `Iterate.joined(separator:)`, now that a member can carry its own `where`
  clause (`where Item: Show`). One way to join instead of two, and it reads left to right with the rest of a
  pipeline; the collector `joining` stays for a prefix, a suffix, or a step inside `collect`.
- `&` instead of `+` for an intersection of traits (was: `+`, from Rust).
- `by` belongs to one element of the `with` list and needs a single-field type (was: one `by` for the whole list).
  `with Show, (Add, Subtract) by value, Compare by value` derives `Show` and delegates the other two, each to
  `value`; an element may be a parenthesised group, so the group delegates together. The old form (`with A, B by
  field` for the whole list) is gone rather than kept as a shorthand, so that "which trait goes where" is always in
  the line and never implied by what the type happens to have.
- A `with` list separates its entries with commas only; several traits delegated to the same field are grouped with
  parentheses instead of `&` (decided 2026-09-24, was: `&` meant the same as `,` in a `with` list, and also grouped a
  delegation, e.g. `with Show, Add & Subtract by value`). `&` still combines traits everywhere a type is expected
  (`where Item: Hash & Equals`); reusing it as the `with` list's separator made one token mean two different things
  depending on position, and reading a mixed line (derives, delegates alone, delegates as a group) needed the
  precedence of `,` and `&` to be kept in your head. `&` in a `with` list is now a parse error that names the fix.
- **A name is ASCII** (`[A-Za-z_][A-Za-z0-9_]*`), while strings, char literals and comments stay full Unicode. An
  identifier is then the same text everywhere - in an editor, in a terminal, in every back end's symbol table - and
  there is no Unicode normalization question to answer about whether two names are the same name. A letter from
  another script where a name could stand is one lexer error for the whole word, not one per character.
- **How a name is spelled is a rule of the language, reported at the declaration**, and not a lint. It follows from a
  rule that was there already: the first letter of a name in a pattern decides whether the pattern binds or names a
  case. A first letter that means one thing in a pattern and nothing anywhere else would make reading a pattern
  guesswork, so the checker says it once per declaration instead. **There is no MACRO_CASE**: a module constant is
  `maxSize`, and the message about `MAX_SIZE` says that rather than only naming the first letter. A pattern binding
  needs no check of its own, because the parser reads an uppercase name there as a case to begin with.
- **A pattern that does not name every field of what it matches ends in `...`** (`Config(host, ...)`), and a labeled
  sub-pattern names its field instead of documenting a position. Before this, a pattern had to name every field, so a
  field added to a type broke every pattern over it while a constructor call was free to leave a defaulted field out -
  "a pattern mirrors the constructor" was true of labels and false of arity. Silent omission was rejected for the
  reason `_` exists at all: leaving something out has to be written down, or a pattern that was meant to be complete
  and a pattern that was not read the same. The spelling is the one a list pattern already has, and the `...` binds
  nothing there either: the fields behind it are heterogeneous and named, so there is no rest value to give a name to.
- **An optional field without a default is required, and `None` is written at the call.** `Person("Ada")` for
  `nickname: String?` is an error whose message shows both fixes. "May be absent" and "may be left out of the call" are
  two different promises, and only the declaration makes the second one: a field that is meant to be optional at every
  call writes `= None` once, and everything else says at the call site that it knows the value is missing. Making `?`
  imply a default would also make one field's type decide something about every caller of the type.
- **The generated constructor is a function value with its labels and its defaults** (`names.map User`,
  `const make = Config`), and a call of such a value may leave a defaulted field out. A default belongs to a
  declaration and never to a type, so the value carries one exactly while the declaration behind it is known - which
  is what a name bound to a constructor is. Where two declarations could have minted the same function type, the back
  end refuses the call instead of guessing which defaults are meant.
- **A type that contains itself by value is an error at the declaration.** `type Node { value: Int, next: Node }` needs
  a `Node` before one can be built, and the answer is `Node?`, a `List<Node>`, or a case that holds no `Node` - which
  is how a recursive type is written anyway. It was previously diagnosed nowhere: stage 0 overflowed its stack and the
  back end reported an internal error.
- **A binding of a refutable pattern that is never read is an error** - in an arm of a `match`, in an `if const`/`if
  var`, in a `while const`. A lowercase name in a pattern always binds, so `limit =>` matches every value and shadows
  the constant `limit` instead of comparing with it, and whether the guard or the body reads the binding is the only
  thing that tells that mistake from a binding somebody meant. `_` discards, `_limit` keeps the name as documentation.
  The irrefutable positions are deliberately left out: nothing there can be mistaken for a comparison, so an unused
  binding is `torb lint`'s business.
- **A capsule is a standard and not a construct, and one rule makes it affordable.** A constructor stays generated,
  total and free of code - `static fn new` as an overridable constructor was weighed and refused, because a
  constructor that may contain logic is a constructor that has to be read. What a type with an invariant writes
  instead is four ordinary things: `private` fields without a default, which close the constructor from outside, a
  `static fn` factory, accessors, and one conversion pair. The rule is that the pair then *is* the type's
  `Encode`/`Decode`: the one `Source` for which `Self` has `TryFrom<Source, Failure>` or `From<Source>` and `Source`
  has `From<Self>` - reflexive `From<Self>` and `From<Never>` excluded, because they hold for every type and would
  be no decision of this one. Without it a capsule paid for its invariant by leaving every format, which is why
  `std/path` kept a public constructor and a documented pitfall until this rule existed; with it, `Path` is a capsule
  whose component list cannot be handed in wholesale and whose text form is what a JSON document holds. No pair or several is no
  `Decode` and a message that names the rule and the candidates. The way out is not refused with it: `Encode`
  cannot break an invariant, it is what `rendered(value)` and a failing `assert` show, and the generated `Show` of
  the same type already prints private fields - so without a pair it stays the field-wise one. The price of the rule
  is that a capsule reaches a format without a type name, so a mapping keyed by one cannot pick it out.
- **A type `torb repl` declares again is kept, and a value of the old type keeps working, shown as `Point#1`**
  (2026-09-24, docs/design/REPL.md section 6). Every input is already a nested scope of the one before it, so
  redeclaring a name is ordinary shadowing everywhere else; a type is the one declaration whose old value can outlive
  it, because a binding of it has the old layout. The session keeps the old declaration once more under a generation
  of its own - a mangled name nothing a person writes can collide with - so the binding's parameter is still a real,
  checkable type in every later entry's module; a person reads the generation as `Point#1`, then `#2`, and the
  session rewrites what the type's own derived `Show` would otherwise print with the mangled name before it is shown.
  This is the checker holding two types of one name without actually having to: each generation is its own type of
  the module, under its own name, and nothing about the value is reinterpreted.

## Open Questions

- Exclusivity is conservative for now. Collect the correct programs it rejects (closures that capture a `var`
  binding and run during a `var` access, paths through `[]`) here, and decide with a compiler at hand:
  - (none yet)
- `deprecated` (and `since`): not documentation but something the compiler has to read. A modifier? Decide when the
  first API needs it. The use case that settles its shape: a field that becomes a method. The field stays for one
  version next to the new method, marked `deprecated` with its replacement, and `torb lint --fix` rewrites the callers
  (`.x` to `.x()`); the language server offers the same as a quick fix.
- `yield`: a function that answers a `Source` and produces items with `yield` would be the same state machine `Task`
  already is, so it costs little. Not in v1, because `Source.produce { sink => ... }` covers the cases (and with
  `capacity: 0` it *is* lock-step generation), and a second way to write a producer is worth less than one obvious way.
  Decide when a real generator is awkward to write with `produce`. See docs/design/LOOPS.md, which examines a loop expression
  as the spelling that would carry it.
- `for` over a `Source`: there is no place in a `for` head for the `?` the pull needs, so v1 has
  `while const Some(item) = source.next().await()? { ... }`. Swift needs `for try await` for exactly this. Reconsider if
  a spelling turns up that keeps `await` and `?` visible without a keyword combination. See docs/design/LOOPS.md section 8 for
  one such spelling and what it would cost.
- `_` as a type argument, meaning "infer this one": `Array<Int, _>`, `Map<String, _>`. A CANDIDATE, nothing more. The
  case for it is the inline storage primitive, where the item type is worth writing and the size is not
  (`const zeros: Array<Int, _> = [0, 0, 0]`), and the same shape turns up wherever one argument of several is obvious.
  What has to be decided first: whether it may stand in a signature or only at a binding (Rust allows it in a body and
  not in a signature, for a good reason), what it means in a `type` field, and whether it reads as an admission that
  the argument list is too long. Until then the size is written out.
- Registry protocol and the exact format of `project.lock.trb`
- `const Some(found) = lookup(key) else { return ... }` (let-else): it removes the most frequent awkward form, a
  `match` whose only purpose is to leave at `None`, in a function whose result is not an `Option` (where `?` does the
  job). A proposal, to be decided with examples from the repository.
- Variadic type parameters, in their smallest form: a pack that expands only as the element list of a tuple and as the
  subject of a bound. Until then a query over several components is spelled per arity (`pairs`, `triples`,
  `quadruples`; docs/design/ECS.md section 4).
- Fields on one line: `type P { x: Int, y: Int }` does not parse, because fields are separated by line ends. Whether
  the comma form is allowed inside the block is a question of layout only; the head form `type P(x: Int, y: Int)` stays
  refused (Decision Log).
- A type whose fields are readable from outside but whose constructor, `copy` and field assignment are its own: one
  line (`private constructor`) instead of private fields plus accessors. A candidate, taken up when the accessors of a
  capsule turn out to hurt in practice.
- `type fn` and `type const` instead of `static` (Swift's "type methods"). A candidate; the condition is that `const` is
  then mandatory after `type`, because `type origin = Point(0, 0)` beside the alias `type Meters = Float` would differ
  only in the first letter, and the place for nested or associated types would be taken. The keyword stands at one
  place in the parser, so a later switch is one `canon` rule.
- The head of a generic `extend` is hard to read (`extend<Value, Target: From<Iterable<Value>>> Option<Target> with
  From<Iterable<Value?>>`). Two candidates, both left as they are for now: a convention with a `canon` rule that puts
  every bound with type arguments of its own into the `where` clause on a line of its own; or Swift's rule that the
  parameters of the extended type are in scope by their declared names (`extend Option with Show where Value: Show`),
  at the price that those names become part of the interface.
- `Into` written by hand on one's own type (`extend Celsius with Into<Float64>` beside `From`): buildable as a second
  spelling of one conversion, with two lookups and a capsule rule that counts both forms. The recommendation is not to
  build it: the package that declares `Celsius` already writes `extend Float64 with From<Celsius>`, because a type named as a trait
  argument counts for coherence, and a hand-written `Into` gets a message that says so.
