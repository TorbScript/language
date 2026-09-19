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
   `(self: Receiver) => Value` resolves names against `Receiver`. A closure passed to `(value: Int) => Int` may use `value` implicitly.
   An expression passed to `lazy Value` is not evaluated at the call site. This one principle powers DSLs,
   custom control structures and query providers, without macros or annotations.
2. **Mutation is always visible.** `var` bindings, `var` fields (`private(var)`: only the type itself), `var` parameters,
   `var self` methods. Everything not marked does not change. Values are never aliased, so a mutation happens
   exactly where it is written and nowhere else.
3. **One way to construct, many ways to create.** Constructors only initialize fields and never contain logic.
   Validation, parsing and conversion live in static factory functions (`Email.parse`, `From`/`Into`).
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
- Naming is convention, not grammar: `UpperCamelCase` for types, traits, type parameters and variants,
  `lowerCamelCase` for everything else. The linter checks it, the compiler does not care.
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
- **Verbs change, participles return.** A method that changes its receiver in place is a verb and declares `var self`
  (`add`, `remove`, `sort`, `translate`). The method that returns a changed copy instead is its participle (`added`,
  `removed`, `sorted`, `translated`). Pick verbs whose participle is a different word: the standard library avoids
  `put`, `cut`, `reset` as mutators, and `set` has the counterpart `updated`. Nouns never change
  anything (`union`, `intersection`). Tooling does not offer verbs on a const path, and the compiler error names the
  participle ("`add` needs a `var`. Did you mean `added`?").
- Generics vs. comparison (`load<Config>(path)` vs. `a < b`) is decided purely syntactically, without knowing what
  the names mean (the C#/Kotlin/TypeScript approach): after a name, `<` starts a type argument list if the tokens up
  to the matching `>` form valid types **and** the token after `>` is one of `(` `.` `{` `)` `]` `,` `:` or the end
  of the line. Otherwise it is "less than". Comparison operators are non-associative (`a < b > c` is never a valid
  comparison), so the generic reading never steals a meaningful expression. In type positions (after `:`, `with`,
  `where`, ...) there is no ambiguity to begin with.
- Number literals: `10`, `1_000_000`, `0xFF`, `0b1010`, `3.14`, `1e9`
- String literals: `"text"`, interpolation with `{expr}`, literal brace with `\{`. Multi-line strings with `"""`, dedented by
  the indentation of their first line (see "Strings").
  Raw strings (`r"..."`, `r"""..."""`) have no interpolation and no escapes (JSON, regular expressions, paths).
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
- Conventional headings carry what tags carry elsewhere: `# Errors`, `# Panics`, `# Examples`. The return value is
  described in the text.
- **Examples are tests.** `torb test` compiles and runs the code under `# Examples`, so documentation cannot rot.
- `[List.add]` and `[Option]` are links. They are resolved like names in the code at that place; a link that does not
  resolve is a warning.
- The doc comment is part of the syntax tree. The language server, `torb doc` and the test runner read the same data.

## Bindings

```trb
const y = 30        // Immutable binding
var x = 20          // Mutable binding
const z: Float = 1  // Optional type annotation; literals adapt to the expected type

var list = [1, 2]   // The binding decides about the value, too: `list.add(3)` works, ...
const fixed = list  // ...`fixed.add(3)` does not. `const` is deep, and `fixed` is a copy: it never changes.

var a               // Compile error: bindings must be initialized
var b: Int          // Compile error: no implied default value
```

- A name can be shadowed in a nested scope, but not redeclared in the same scope. **The parameters of a function and
  the top level of its body are one scope**, so a `const` there may not take a parameter's name: there is no silent
  shadowing anywhere in the language, and a parameter is the most surprising place for it. A nested block and a closure
  are scopes of their own.
- **Changes that cannot have an effect are compile errors,** because with value semantics they are always a mistake:
  a `var` that is changed but never read afterwards (`var first = list[0]` followed by `first.increment()` - the
  message points to `list[0].increment()`), and the discarded result of a method that takes `self`
  (`list.added(4)` as a statement - the message points to `add`). Discard on purpose with `const _ = ...`.
- **An expression statement must have the type `Void` or `Never`,** unless the call has a `var` receiver or a `var`
  argument. That is the whole rule behind the one above: `parser.bump()` and `cursor.next()` change something and
  stay statements, `Email.parse(text)` and `1 + 2` are values that go nowhere.

`const` is deep from the perspective of the binding: through a `const` binding you can neither reassign, nor assign
fields, nor call `var self` methods. `var` means "mutable through this path".

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
const someRange = 0..10                    // Range<Int>, `0..=10` is inclusive, `0..` and `..10` are open
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
- **There are no bit operators.** `&`, `|`, `^`, `<<` and `>>` are not operators of the language (`|` already means a
  literal-type union); the integer types come `with Bits` instead, whose methods are native: `bitwiseAnd`, `bitwiseOr`,
  `bitwiseExclusiveOr`, `bitwiseNot`, `shiftedLeft(by:)` and `shiftedRight(by:)` (an arithmetic shift on the signed
  types; a shift by a negative amount or by the width of the type or more panics). `UInt64` additionally has
  `addedWrapping` and `multipliedWrapping`, the only arithmetic in the language that does not panic on overflow -
  a hash function cannot be written without it. Methods need no precedence rules and no new tokens, and keeping the
  wrapping pair off the signed types keeps "overflow panics" true everywhere a `+` is written.
- **On a `Float`, `==` is IEEE-754 and `compare` is a total order.** `nan != nan` and `0.0 == -0.0`, while `compare`
  puts `nan` above everything and treats `-0.0` as equal to `0.0`, so `sorted` terminates whatever pivot it picks.
  `Float32` and `Float64` are deliberately not `Hash`, so a float can never be a `Map` key and the `nan` key does not
  exist. Where a tolerance is meant, `isCloseTo` says so.
- **A label is not part of the type of a tuple.** `(lowest: Int, highest: Int)`, `(highest: Int, lowest: Int)` and
  `(Int, Int)` are one type, and a labeled tuple may be used where an unlabeled one is expected and back. A label at
  a position where the expected type has a different one is an error, so a swap cannot happen silently.
- `Void` is the type with exactly one value, `Never` the type of expressions that do not return (`panic`, `return`).
  **`Void` is the one name that is both a type and its only value** (as `Unit` is in Kotlin): a function without a
  result type returns `Void`, a block that ends in a statement has the value `Void`, and `Ok(Void)` passes it on.
  `Never` has no value at all and converts to every type, which is why `panic "..."` fits into any expression.
- `Void`, `Never` and `Range<Value>` are declared in the prelude like every other type. `0..10` is
  `Range(start: Some(0), end: Some(10))`, `0..=10` sets `isInclusive`, and `0..` and `..10` leave one end `None`.
  `list[from..to]` is a `Range` passed to `Slice.slice`.
- **`Range<Int>` is `Iterable<Int>` and `Length`, and the open ends are checked at runtime,** because "has a start" is
  a property of a field and not of the type: `iterator()` panics for a range without a start ("a range without a start
  has no first value"), `length()` panics for a range without both ends, and `0..` iterates forever. A compile-time
  version of that condition would need dependent types.

### Strings

A `String` is UTF-8 text and a value like everything else. It deliberately has no `length()` and no `text[i]`,
because "length" and "the i-th character" have three different answers (bytes, code points, what a reader sees) and
two of them are slow:

```trb
const text = "Grüße 👋"
text.chars().count()               // 7  - `chars()` is an Iterable<Char> (Unicode scalar values)
text.byteLength()                  // 13 - O(1)
text.isEmpty()

const at = text.indexOf("ß")       // Some(3): positions come from searching and are byte offsets
const tail = text[3..]             // Slicing with offsets is O(1). An offset inside of a character panics.
text.substringAfter("ü")           // Some("ße 👋") - most code never sees an offset
```

- **Every offset is checked.** An offset greater than `byteLength()`, a start greater than the end, and an offset on a
  UTF-8 continuation byte each panic, with the offset and the length in the message.
- **A `String` is therefore always valid UTF-8.** The only ways in are literals, slices at character boundaries,
  `String.from(Iterable<Char>)` and runtime functions that validate - so reading a file whose bytes are not UTF-8 is an
  `IoError`, never a replacement character, and neither `chars()` nor a back end needs a rule for broken text.

**A `"""`/`r"""` string is dedented by its first line,** so a block of code reads at the indentation of the call
around it instead of jammed against the left margin:

```trb
fn count(limit: Int): Int {
  var total = 0
  const source = r"""
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
status = Status.parse(someString)?         // Generated, like `Show`, `Equals`, `Hash`, `Encode`, `Decode`

fn connect(host: String, transport: "tcp" | "udp" = "tcp") { ... }   // They work inline, too
```

- A literal has its ordinary type (`"online"` is a `String`) unless a literal type is expected. So nothing is ever
  widened or narrowed, and inference is untouched.
- The members are told apart by value, not by type. That is why this fits a language without runtime types - and why
  **only literals can be combined with `|`**. There are no unions of types (`Int | String`): use a type with cases,
  or accept `<Value: Into<Width>>`.
- `match` on a literal type is exhaustive without `_`. Back to the base type: interpolation or `status.into()`.
- The generated implementation is `Parse<LiteralParseError>`: `LiteralParseError { text, expected }` names the text
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
  private var cells: Array<Array<Float, Columns>, Rows> = Array.filled(Array.filled(0.0))

  fn multiplied<const Other: Int>(self, other: Matrix<Columns, Other>): Matrix<Rows, Other> { ... }
}

const combined = Matrix<2, 3>().multiplied(Matrix<3, 4>())   // Matrix<2, 4>, checked by the compiler
```

- `Array<Item, const Size: Int>` is the array with a fixed size, as in Rust and Go: a small inline value without heap
  storage and without a reference count. An index that is known at compile time is checked at compile time.
  Everything that grows is a `List` (`ArrayList` is what `Vec` is in Rust).
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

There is no separate concept for this. A distinct type is a `type` with a single field, and `by` forwards
traits to that field so the wrapper does not cost boilerplate:

```trb
type UserId { value: Int }

type Meters with Add, Subtract, Compare by value {
  value: Float
}

type Email with Show by value {
  private value: String                          // Private: only `Email.parse` creates an Email
  fn parse(text: String): Result<Email, ParseError> { ... }
}

const distance = Meters(5.0) + Meters(2.5)     // Meters(7.5)
// Meters(5.0) + 2.5                           // Compile error: Float is not Meters
// Meters(5.0) + Seconds(2.0)                  // Compile error
const raw = distance.value                     // Explicit way out
```

- `with A, B by field` implements the listed traits by delegating to the field. Traits not listed are not available:
  `Meters * Meters` does not compile, which is the point (that would be square meters).
- Where a forwarded signature mentions `Self` (`add(self, other: Self): Self`), arguments are unwrapped and results
  wrapped again. That only works for single-field types. Traits without `Self` in arguments or results can be
  delegated by any type (`type Team with Iterable<User> by members { ... }`).
- **`by` forwards the required members, the default members come from the trait.** `distance.max(Meters(10.0))` is
  `Compare.max` over the forwarded `compare`, so it returns a `Meters` and nothing has to be rewrapped. A default
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
  methods: **they never infer it.** Without a return type they return `Void` (`fn add(var self, value: Item)`), and a
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
largest(0, ...someSet)                         // Spread works with every `Iterable<Int>`
```

A variadic parameter never accepts a collection implicitly (`List.of([1, 2])` is a `List<List<Int>>` with one
element). Spreading is always explicit.

**A parameter default is evaluated at the call site, at every call, in the scope of the declaration** - without `self`
and without the other parameters. That is the same rule as for a field default (see [Construction](#construction)), so
`limits(memory: Int = 64.megabytes())` is a call that happens where `limits` is called, and a default can never depend
on an argument order that is not visible at the call site.

Parameters are `const`. A `var` parameter allows mutation through it (`fn reset(var counter: Counter)`), the caller
must pass something mutable. `var self` is just the most common case.

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
- `return` inside a closure returns from the closure. There is no non-local return.
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

The implicit parameter can be named by the _type of the function_:

```trb
fn map<Output>(self, transform: (value: Item) => Output): Iterable<Output>

numbers.map { value * 2 }
```

If such a name would shadow a name that is visible at the closure, it is a compile error (no silent shadowing).

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
- The callee is a name or a member path (`print`, `Email.parse`, `server.route`).
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
- **Formatter canon:** a call is written as a command if it is a statement, or if it ends with a trailing closure
  (`const result = retry 3 { ... }`), and if its arguments fit on one line. Everything else gets parentheses: calls
  whose value is used without a closure (`const email = Email.parse("a@b.c")`), calls without arguments, and calls
  whose first argument starts with `(`, `[`, `-`, `!` or `.`.
- Calls without arguments always need `()`. A bare name is always a reference.

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
  native fn value(self): Value               // The ordinary value/closure. Evaluated at most once for non-functions.
  native fn captures(self): List<Encode> // Values of the captured variables
}

type ExpressionNode {
  case Literal(value: Encode, of: TypeReference)
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
  (`Error(Unsupported(...))`), the language cannot know what a library can translate.
- `ExpressionNode` is part of the language standard and an ordinary ADT. A new node kind comes with a new version of
  the language; providers that end their `match` with `_ => Error(Unsupported(...))` - and they need that arm for
  calls they do not know anyway - keep compiling.

## Blocks and Control Flow

`{ ... }` in expression position is a closure. To evaluate a block immediately, use `do` - which is an ordinary
function from the standard library (`fn do<Value>(body: () => Value): Value { body() }`), not a keyword.
The bodies of `if`/`else` and of a `match` arm (`pattern => { ... }`) are blocks, not closures: they belong to the
construct the same way the body of a `for` does.

```trb
const initialized = do {
  const base = [1, 2, 3]
  base.added(4).added(5).removed(2)
}
```

`if`, `match`, `for`, `while` are built in. Their heads are parsed without trailing closures, the `{` starts the body.
`if` and `match` are expressions.

```trb
const aOrB = if something { a } else { b }

for number in numbers { print number }
for i in 0..10 { print i }
while queue.isNotEmpty() { ... }     // `break` and `continue` work as expected
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

`using` is one of those functions, and its closure is a **receiver closure over the resource**:
`fn using<Resource: Close, Value>(var resource: Resource, body: (var Resource) => Value): Value`. So the body reaches
the members of the resource without naming it (`using File.open(path)? { writeLine "done" }`), and it may change it -
which is what a resource is for. Passing a temporary to that `var` parameter is allowed: the callee is its only owner
(see [`var` Paths](#var-paths-and-var-parameters)).

## Types

`type` is the single keyword for all data types (struct, class, enum, ADT). **A `type` is a value, and the binding
decides whether it can be changed.** There is one `Point`, not a `Point` and a `MutablePoint`. Like everywhere else in
the language: no `var`, no mutation.

### Values

```trb
type Point {
  var x: Int                   // Fields are `const` unless marked `var`
  var y: Int

  // Method: declares `self`. Member access through `self` is implicit.
  fn area(self): Int {
    x * y
  }

  // A verb changes the value in place and says so: `var self`
  fn translate(var self, deltaX: Int = 0, deltaY: Int = 0) {
    x = x + deltaX
    y = y + deltaY
  }

  // Its participle returns a changed copy
  fn translated(self, deltaX: Int = 0, deltaY: Int = 0): Point {
    copy(x: x + deltaX, y: y + deltaY)
  }

  // Static function: does not declare `self`.
  fn square(size: Int): Self {
    Self(size, size)
  }

  // Static constant
  const origin = Point(0, 0)
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
- **Mutation needs a `var` path,** from the binding down to the field: a `var` binding, `var` parameter or `var self`,
  then `var` fields all the way. A field without `var` never changes after construction, not even in a `var`
  binding (`id`, `step`). `const` is deep: through a `const` binding nothing changes, whatever the type looks like.
- Structural `Equals`, `Hash`, `Show` and `copy` are generated, there is no identity.
  (Each of them only if all fields support it: a type with a function in a field has no generated `Equals`.)
- **The generated `Show` has a fixed format,** because two implementations of the language are compared through it
  (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)): a type is `Type(field: value, ...)` with all fields in
  declaration order, a case is `Case(field: value)` or its bare name when it has none, a `List` is `[a, b]`, a `Map`
  `["k": v]` (`[:]` when it is empty), a `Set` `{a, b}` (`{}` when it is empty), a tuple `(a, b)` with the labels where there are any, an
  `Option` `Some(x)` or `None`. Inside such a value a `String` is quoted with escapes and a `Char` written in single
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

  fn send(var self, message: String) {
    sent = sent + 1
  }
}

var connection = Connection("tcp://example.test")
var same = connection          // The same object
same.send("hello")
print connection.sent          // 1

const view = connection
view.send("nope")              // Compile error: no `var` path, no mutation
```

A `shared type` is the exception for everything that has an identity: assigning it does not copy, everybody who holds
it sees the same object. Typical cases are handles to the outside world (`File`, `Socket`, `Window`), `Channel`,
`Task`, registries. Most programs declare very few of them.

- The rule stays the same: mutation needs a `var` path. A `const` binding to a shared object is a read-only view
  (the object can still change, but not through this path).
- **A read-only view cannot be widened again.** A `var` binding, `var` field or `var` argument may not be
  initialized from a `const` path to a shared object - there is no copy that would make it a different object, so
  `var writable = view` would hand out exactly what the `const` withheld. For a value it is simply a copy and fine.
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
- **References are second-class.** They only exist as a `var` parameter or `var self`, for the duration of a call.
  They cannot be stored in a field, returned, or captured by a closure that is stored. So there are no lifetimes, no
  borrow checker, and nothing can dangle. (A captured `var` binding is the one thing that outlives a call, and it is
  not a reference - see below.)
- **A closure that captures a reference may not escape, and the compiler decides that per closure.** A closure may
  capture a `var` parameter or `var self` only when it cannot outlive the call; conservatively that is a closure
  written directly as the argument of a call that does not store it, which is exactly what the receiver closures, the
  DSLs and the pipeline stages are. Everything else captures copies, or the shared box of a captured `var` binding
  (see below). The decision is recorded, because it is also what lets an implementation put the closure's environment
  on the stack.
- **Exclusivity:** while a `var` access to a path is running, the same path (or a path above or below it) cannot be
  accessed in any other way. **The access of a call begins once all of its arguments have been evaluated**, so
  everything the arguments *read* has already finished and `items.removeAt(items.length() - 1)` is ordinary code. What
  is left is what really overlaps: two `var` accesses of the **same** call (`swap(a, a)`, `move(list[0], list)`), and a
  closure argument of a call reaching the path that call is changing - the closure runs *inside* the access, which is
  what makes changing `root` inside `root.div { ... }` an error. Different fields are fine
  (`project.build { output "{project.name}" }`). Two indices or ranges of the same collection are not, because they
  cannot be compared statically (`swap(items[i], items[j])`: use `items.swapAt(i, j)`). The check is static and
  conservative: what the compiler cannot prove is an error, and there is no check at runtime.
- A temporary is not a `var` path: `iterator().next()` is a compile error, `var cursor = iterator()` comes first.
  (Changing something that is thrown away is always a mistake.) As the _argument_ of a `var` parameter a temporary
  is fine - the callee is its only owner, so "copy in, copy out" is exact and nothing is written back anywhere:
  `using File.open(path)? { ... }`. The rule is about the base of a path (`f().x = 1`), not about ownership.
- The variable of a `for` loop is a `const`. To change elements, use the path (`items[index].x = 1`,
  `items.update(index) { ... }`) or build a new collection with `map`.
- Closures capture `const` bindings as copies. A captured `var` binding is shared between the closure and its scope -
  the one place where a variable is shared. Closures passed to `spawn` cannot capture `var` bindings.
- **A captured `var` binding is a shared box, not a reference.** It may escape (a closure that captures it can be
  returned or stored), it is reference counted like a `shared type` object, and it is the only sharing of a
  variable there is. Every call that may run such a closure counts as an access to the binding, and the binding is
  exempt from the dead-change rule: the read can be anywhere.
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
- **A field default is evaluated at every construction, in a scope without `self` and without the other fields.**
  So the order of the fields is not observable, and a default that depends on another field is what a factory is for.
- Outside of the type, `private` fields cannot be passed. So the constructor is usable from outside if and only if
  every private field has a default value. Inside of the type (`Self(...)`) all fields can be passed.
- **`copy` has the shape of the constructor, with every field optional:**
  `fn copy(self, <field>: <Type> = <the current value>, ...): Self`, all fields in declaration order, and `private`
  fields not passable from outside. There is no `copy` for a `shared type` - it has an identity, not a value.

Everything else is a static factory function:

```trb
type Email {
  private value: String                // Private without default: only `Email` itself can construct an `Email`

  fn parse(text: String): Result<Email, ParseError> {
    if !text.contains("@") {
      return Error(ParseError("'{text}' is not an email address"))
    }
    Ok(Self(text))
  }
}

const email = Email.parse("info@example.test")?
```

### Conversions

Conversions follow the `From`/`Into` principle. Implementing `From` provides `Into` for free, fallible conversions
use `TryFrom`, text uses `Parse`.

```trb
extend Celsius with From<Fahrenheit> {
  fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

const a = Celsius.from(Fahrenheit(100.0))
const b: Celsius = Fahrenheit(100.0).into()
```

The `?` operator uses `From` to convert error types.

**Every type has `From<Self>`**, and that conversion is the value itself. It is not written down anywhere and could not
be: a blanket `extend<Value> Value with From<Value>` would overlap with every other implementation of `From`. It is what
lets `fn sum(self): Item where Item: Add & From<Int>` be called with a list of `Int`.

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
protection is mutation and invariants, and both have a modifier:

```trb
type Account {
  owner: String                          // Public, const
  var nickname: String = ""              // Public, writable by everyone who has a `var` path to the account
  private(var) balance: Int = 0           // Everybody reads, only Account writes
  private var history: List<String> = []                     // Invisible from outside

  fn deposit(var self, amount: Int) {
    balance = balance + amount
    history.add("deposit {amount}")
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
  `config.routes` can be read and iterated from outside, but `config.routes.add(...)` is a compile error. What
  somebody takes out of it is a copy anyway. No defensive copies by hand, no accessor methods.
- `private(var)` is a modifier of a field and of nothing else. On a member that has no `var` it has nothing to say
  and is an error. It also already *is* the `var`, so `private(var) var balance: Int` is an error: one of the two says
  it twice.
- **`private` reaches as far as the type does.** A private member is visible in the body of its type and in every
  `extend` of that type in the same package - an `extend` without a trait _is_ part of the type - and nowhere else.
  The package is the unit of coherence, so it is the unit of privacy, too.
- **There are no getters, setters or properties.** A field is storage, a method computes, and the `()` tells which one
  it is (`list.length()` may cost something, `point.x` never does). No `get` prefixes; predicates are called
  `isEmpty()`/`hasX()`, mutators are verbs with `var self`.
- **A field is a promise about data, so replacing one by a method is a breaking change** - and a property would not
  save it: a field is also a parameter of the generated constructor, a position in patterns, a parameter of `copy`,
  a part of the generated `Encode`/`Decode` and a step of `var` paths. A property would cover reading and nothing
  else. What might be computed, cached or validated one day is a method from the start; the move from `.x` to `.x()`
  is mechanical, the compiler finds every place, and the tools do it for a whole workspace (see `deprecated` in the
  Open Questions).
- There are no validating setters, because a setter cannot fail properly in a language without exceptions.
  Validation lives in types and factories (`Email.parse`, `Port.tryFrom(8080)`) or in a method that returns a `Result`
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

### Members: a method is a constant that holds a closure

A type has **one namespace** of members. A member is either an instance field, or a constant of the type (`const`,
`fn`). There is nothing else:

- A static function is a constant of the type that holds a function.
- A method is a constant of the type that holds a _receiver closure_ - the very same thing that powers the DSLs.
  `fn` is the declaration form of it (adds hoisting, recursion, generics, a return type annotation):

```trb
type Point {
  x: Int
  y: Int

  fn area(self): Int { x * y }
  // is, structurally:
  // const area: (self: Point) => Int = { x * y }
}

const p = Point(10, 20)
p.area()                       // `value.member(args)` is `Type.member(value, args)` if the member takes `self`
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

**Property commands.** A _command_ on a field never calls it, it writes it. This is what gives the configuration DSL
its Groovy look without a single hand-written setter:

```trb
port 8080                      // Field `var port: Int`:                port = 8080
onStart { print "started" }    // Field `var onStart: () => Void`:      onStart = { print "started" }
database {                     // Field `var database: DatabaseConfig`: the receiver closure is applied to the
  url "postgres://..."         //   field's value, which is configured in place
}
print "Listening on {port}"    // Reading is just the name
onStart()                      // Calling a function in a field always needs parentheses
port = 8080                    // `=` works, too
```

There is no case where the same line could mean two things: command on a method - call, command on a field - write
(a closure is assigned if the field holds a function, and configures the value in place otherwise).

Both forms need a `var` path to the field. Calling a function in a field always takes parentheses (`onClick()`,
`onClick(event)`); `onClick { ... }` sets it.

**A call of a non-callable member writes it, with or without parentheses.** `tls true` and `tls(true)` are the same
line. The parenthesized form is not optional style: the formatter's canon demands parentheses as soon as the first
argument starts with `(`, so `tls(port == 8443)` is the only way to write that value at all.

## Algebraic Data Types and Pattern Matching

Variants are declared with `case` inside of a `type`.

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(self): Float {
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
shapes.add(.Empty)                             // The parameter does
if shape == .Empty { ... }                     // The other side of the comparison does
const other = Shape.Circle(1.0)                // Nothing does: write the type

use Circle, Empty from Shape                   // Or import cases by name, like anything else
const third = Circle(3.0)
```

- `.Case` works wherever a type is expected: annotations, arguments, fields, results, `==`, the arms of a `match`
  whose result is expected, and in patterns (the type is the one of the value that is matched).
- `use Names from Type` brings cases into scope. That is all there is to `Some`, `None`, `Ok` and `Error`: the
  prelude imports them from `Option` and `Result`.
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

  fn isTimeout(self): Bool { ... }
  fn isRetryable(self): Bool { ... }
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
  [only] => "one element"
  [first, ...rest] => "many"
}
```

- **Fields are matched by position, and a label that is present must name the field at that position.**
  `Point(y: 0, x: 1)` is an error, not a silent swap: a pattern mirrors the constructor, where labels are checked.
- **An arm that can never be reached is an error** ("This arm is never reached"), for the same reason as a dead
  change or a discarded value - with value semantics it is always a mistake, never a defensive line.

Patterns also work in bindings and conditions:

```trb
const (quotient, remainder) = divide(7, 2)
const Point(x, y) = p

if const Some(user) = findUser(id) {
  print user.name
}

while const Some((next, rest)) = queue.dequeue() { ... }

if var Some(iterator) = current { iterator.next() }     // `var` instead of `const`: the binding is mutable

for (key, value) in someMap { ... }
```

**`if var P = place` binds into the place,** exactly like a `var` parameter - it does not bind a copy. The subject
must be a `var` path, and the body runs inside a `var` access to it, so `iterator.next()` advances the iterator that
`current` holds. A `var` pattern that bound a copy would be a dead change by construction.

In a pattern `_` is the wildcard, in an expression `_` is the implicit closure parameter. The positions never overlap.

**A bare name in a pattern is always a new binding.** A case is `.Case`, `Type.Case` or an imported `Case(...)`, a
type is `Point(x, y)`, and a constant is compared with a guard (`n if n == limit`). So a pattern never changes its
meaning because of what happens to be in scope: a misspelled case is an error instead of a catch-all, and renaming
a constant cannot turn an arm into one. (`None` in a pattern is `.None`. The linter flags uppercase bindings.)

## Traits

```trb
trait Shape {
  fn area(self): Float                     // Required
  fn describe(self): String {              // Default implementation
    "Shape with area {area()}"
  }
}

trait Compare with Equals {                        // Supertrait
  fn compare(self, other: Self): Ordering
}

type Square with Shape, Equals {         // Implement at the declaration...
  side: Float
  fn area(self): Float { side * side }
}

extend Point with Shape {                  // ...or afterwards
  fn area(self): Float { Float.from(x * y) }
}

extend String {                            // Extension methods without a trait
  fn shout(self): String { "{toUpperCase()}!" }
}

extend<Item> List<Item> with Show where Item: Show { ... }   // Type parameters are declared on `extend`
```

- **Naming:** a trait is a capability the type comes _with_, so a trait with a single required method is named like
  that method: `Hash` (`hash`), `Equals`, `Compare`, `Show`, `Add`, `From`, `Length`, `Close`. `type Money with Equals,
  Hash, Compare` reads as what it is. No `-able`/`-ible` adjectives. Traits that are mainly used _as types_ are nouns:
  `Iterable`, `Iterator`, `Collection`, `List`, `Map`, `Collector`, `Accumulator`.
- `with` is the only keyword for "implements" and for supertraits. Bounds use `where Item: Hash & Equals` or inline `<Item: Hash>`.
- **Type parameters of a `type` and of a `trait` can have defaults** (`trait Add<Other = Self, Output = Self>`), so
  `with Add` means `Add<Self, Self>` and nobody writes it out. A default may name earlier parameters and `Self`, and
  it is filled in, never inferred. `fn` has no defaults: its type arguments come from the call.
- **A member may carry a `where` clause of its own** (`fn toSet(self): Set<Item> where Item: Hash`). It is not a
  requirement for implementors - the member simply exists only where the clause holds, and a use that does not
  satisfy it reports the unmet bound. Same rule as for a conditional `extend`.
- **`Self` is allowed in every type position inside a trait,** including as a trait argument
  (`trait Collection<Item> with Iterable<Item>, Length, Accumulator<Item, Self>`). `Self` is a type, not a type
  constructor, so this is not the `Self<U>` that ["One Vocabulary"](#one-vocabulary-instead-of-higher-kinded-types)
  rules out, and it costs nothing.
- **Coherence:** you can only `extend X with Trait` if your package owns `X` or `Trait`.
- **Blanket implementations:** an implementation whose target is a bare type parameter
  (`extend<Source, Target> Source with Into<Target> where Target: From<Source>`) covers every type, and is allowed
  when the package owns the trait. Two implementations of one trait may never overlap. Disjointness is proved by
  different target heads, or by bounds on the same subject that no type can satisfy together - so two blanket
  implementations of one trait always overlap, whatever their bounds say.
- **`extend` without a trait:** for a type of your own package it is simply a part of the type, in whatever file it
  is written, and visible wherever the type is. For a type of another package (`extend String { fn shout(self) ... }`)
  it is visible in every file that imports the module it is declared in - no matter what it imports from it. A
  module that only consists of extensions is imported without names: `use "./text-extensions"`. If two imported
  modules bring a method of the same name for the same type, calling it is a compile error; a namespace import
  (`use * as text from "./text-extensions"`, `text.shout(value)`) says which one is meant. Nothing runs when a
  module is imported, this is purely a rule about which names are visible.
- **An `extend` adds constants and functions, nothing else.** A field or a `case` in an `extend` is an error
  ("fields and cases belong to the declaration of the type"), because exhaustiveness and the generated constructor
  have to be decidable from the declaration alone.
- Traits are implemented by values. A `shared type` can only implement a `shared trait` (`shared trait Close`), and a
  value of such a trait type counts as shared. So a `List<Item>` or an `Iterable<Item>` is always a value: nobody
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
  monomorphizes as before. **Object safety is checked per call, not per type:** a member that mentions `Self` in a
  parameter or in its result, or that has no `self`, cannot be called on a trait-typed value. So `List<Show & Hash>`
  and `fn audit(entry: Show & Encode)` stay legal, and only calls that have no meaning are rejected.
- Functions without `self` in a trait: without a body they are a requirement for the implementing types
  (`From.from`, `Parse.parse`). With a body they are functions of the trait itself - the place for factories that pick
  a default implementation (`List.of(1, 2)`, `Set.of("a")`).
- Because a trait is a type, it can be extended like one. `extend<Item> List<Item> with Show where Item: Show` makes every list
  showable, `extend<Item> List<Item> with From<Iterable<Item>>` makes `List<Item>` itself a valid target of `to<List<Item>>()`.
- **`Trait.member` reads the trait's own members first, then the members of implementations whose target is the
  trait itself.** That is what makes `List.from(...)` and `Map.from(...)` work, where `from` comes from
  `extend<Key: Hash, Value> Map<Key, Value> with From<Iterable<(Key, Value)>>`. Two such implementations are an
  ambiguity error, and the fix is to name a type (`TrieMap.from(...)`).
- Operators are traits: `+` is `Add.add`, `==` is `Equals.equals`, `<` is `Compare.compare`, `a[i]` is `Indexed.at`,
  `a[i] = v` is `MutableIndexed.set`, `a[from..to]` is `Slice.slice`, `a[from..to] = v` is `MutableSlice.replace`,
  string interpolation is `Show.show`.
- `&&`, `||` and `!` are the exception: they are built in on `Bool`, they short-circuit, and they cannot be
  overloaded. A trait method evaluates its argument, so a trait would mean something else.
- There are no bit operators and therefore no traits for them: shifting and masking are native methods of the integer
  types (see [Built-in Types](#built-in-types)).

## Types, Values and Reflection

Types and values are strictly separate worlds:

- A type never flows as a value. There is no `Type` type, no `typeof`, no `value is Value` on generic `Value`, no
  `Class.forName`. Types appear only in type positions (after `:`, in `<>`, after `with`/`where`, right of `type X =`).
  `Void` is the one name that is both a type and a value, and it is no bridge: it carries nothing to look at.
- The only bridges are syntactic: `Point(...)` (constructor), `Point.origin` / `Point.parse(...)` (static members),
  `Shape.Circle` (variants), `Point.area` (method reference).
- So there is **no runtime reflection**. It could not be implemented identically in all back ends (monomorphized vs.
  boxed generics would become observable), it keeps every type's metadata alive in compiled binaries, and it is the
  meta-programming style this language does not want.

What reflection is usually needed for (serialization, config mapping, database rows, debug output) is covered by one
more _generated trait pair_, in the same way `Equals`, `Hash` and `Show` are generated:

```trb
trait Encode { fn encode(self, var encoder: Encoder) }
trait Decode { fn decode(var decoder: Decoder): Result<Self, DecodeError> }
```

```trb
type User {
  name: String
  email: Email
  tags: List<String> = []
}

const text = Json.encode(user)                 // fn encode(value: Encode): String
const user = Json.decode<User>(text)?          // fn decode<Value: Decode>(text: String): Result<Value, JsonError>
```

A type describes itself to an `Encoder` and reads itself from a `Decoder`. A format (`Json`, `Toml`, a database
driver) implements these two traits and never sees a type. This is what the compiler generates for `User`, and
nothing in it is special:

```trb
extend User with Encode, Decode {
  fn encode(self, var encoder: Encoder) {
    encoder.record("User") { fields =>
      fields.field("name", name)
      fields.field("email", email)
      fields.field("tags", tags)
    }
  }

  fn decode(var decoder: Decoder): Result<User, DecodeError> {
    decoder.record("User") { fields =>
      Ok(User(
        name: fields.field("name")?,
        email: fields.field("email")?,
        tags: fields.fieldOr("tags", [])?,       // Fields with a default value may be missing
      ))
    }
  }
}
```

- **No tree in between.** Values are written while the type describes itself: no second representation of the whole
  document, sequences are streamed, nothing is lost (numbers keep their range, a `Set` comes back as a `Set` because
  the target type drives the decoding, `Decimal` and bytes are first-class).
- **No second vocabulary.** The methods of `Encoder` and `Decoder` are named after the types of the language (`bool`,
  `int`, `float`, `decimal`, `string`, `bytes`) plus the four shapes `sequence`, `map`, `record` and `variant`.
- **No dynamically typed island.** There is no "any value" type in the language. Who wants to look at a document
  without knowing its type uses a library type (`JsonValue` of `std/json` is an ordinary ADT, and `Encode`/`Decode` itself).
- `Encode` is generated for every `type` whose fields are all `Encode`.
- `Decode` is only generated if the constructor is usable from outside (see [Construction](#construction)).
  A type with a private constructor has invariants, so it writes `decode` by hand and the invariant holds for
  decoded values, too (`Email.decode` calls `Email.parse`).
- Different field names, skipped fields, versioning: write the two functions by hand, there are no annotations.
  What is a convention of the format and not of the type is an option of the format (`Json.encode(user, naming: .SnakeCase)`).
- **`Encode`/`Decode` are data binding, for every format whose model is "values, sequences, maps, records"**: JSON,
  MessagePack, CBOR, TOML, YAML, query strings, database rows. A type says once what it consists of, a format says
  once how that is written: N + M implementations instead of N × M, a format somebody else wrote works with every
  type that exists, and `List<Encode>` stays possible. (`length` in `sequence` and `map` is there for the binary
  formats, which write it in front.)
- **A format with a document model of its own gets its own traits, in its own package, in addition.** XML and HTML
  know attributes next to elements, namespaces, text between elements and an order that matters - none of that fits
  into four shapes, and squeezing it in ends in naming tricks (`@id`, `$text`). So `std/xml` has the tree (`XmlNode`,
  like `JsonValue`) and `XmlEncode`/`XmlDecode` for types that need the whole format. Data that merely travels as XML
  needs neither: `Xml.encode(value: Encode)` writes records as elements by convention.
- `describe(value)` renders every `Encode` value as text, for messages and debugging.
- Generated code only exists where it is used, like every generic instantiation. Nothing is kept alive "just in case".

## Error Handling

There is no `null` and there are no exceptions.

```trb
fn findUser(id: Int): User? { ... }                      // Option<User>
fn loadConfig(path: String): Result<Config, IoError> { ... }

const name = findUser(1)?.name ?? "anonymous"            // Optional chaining, default

fn start(): Result<Void, AppError> {
  const config = loadConfig("app.trb")?                  // Early return on Error. IoError -> AppError through `From`
  ...
  Ok(Void)
}

panic "unreachable"                                      // Bugs. Not catchable, aborts the program.
```

- `collection.get(i)` returns `Item?`, `collection[i]` panics when out of bounds.
- **`?.` is `Option.map`, and `Option.flatMap` when the member's result is itself an `Option`.** So `?.` never
  produces a nested Option: `first()?.position()` is a `Vector2?`, whatever `position()` returns. It is not
  defined on `Result`.
- **`??` is `orElse`, on `Option` and on `Result` alike.** The right side is `lazy` and is checked against the
  `Value`, so `Status.parse(text) ?? "offline"` needs no `.ok()` in between.
- **A panic is output, so its format is part of the language:** to standard error, `panic: <message>`, then
  `  at src/file.trb:12:5` for the panic site and, in the debug profile, the frames of the task. The exit code is
  **101**, and both back ends agree on the text to the character because the conformance suite compares it.
- **Nothing runs while a program falls over.** No destructor, no `Close`, no `using` cleanup - a panic is a bug, and
  running more code in a broken program is how bugs get worse.
- **A panic aborts the process,** because the language has no supervision. The one exception is a sandboxed script: the
  VM is interpreting it and the script has a heap of its own, so the VM stops it and reports a `SandboxError`
  (see [Receiver Scripts and the Sandbox](#receiver-scripts-and-the-sandbox)).
- **A top-level `?` is not a panic.** It prints `error: <the error through Show>` and exits with 1.

## Collections and Iteration

The collection types are **traits**, one per kind. Signatures, fields and bindings talk about traits, an
implementation is only named where something is constructed. Collections are values like everything else: the binding
decides whether they can be changed.

```text
Iterable<Item>
└─ Collection<Item>           length, isEmpty, contains, add, addAll, clear
   ├─ List<Item>              ArrayList (literal [1, 2]), TrieList
   ├─ Set<Item>               TrieSet, HashSet
   ├─ Map<Key, Value>      TrieMap (literal ["a": 1]), HashMap           a Collection<(Key, Value)>
   ├─ Stack<Item>             ArrayStack
   └─ Queue<Item>             ArrayQueue
```

```trb
const numbers = [1, 2, 3]
var buffer = numbers                                    // A copy. The storage is shared until one of them is written to.
buffer.add(4)                                           // In place. `numbers` is still [1, 2, 3].
const more = numbers.added(4).removed(2)                // Participles work everywhere

type Inventory {
  private(var) items: Map<String, Int> = [:]
}

fn lookup(table: Map<String, Int>): Int { ... }         // Any map. Read-only, and nobody changes it meanwhile.
fn describe<Item>(items: Collection<Item>): String { ... }    // Any collection
fn fill(var target: Collection<Int>) { ... }            // Fills the caller's list, set, stack, queue, ...

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

  | In place (`var self`)                       | Changed copy (`self`)                                  |
  |---------------------------------------------|--------------------------------------------------------|
  | `add`, `addAll`, `insert`                   | `added`, `addedAll`, `inserted`                        |
  | `remove`, `removeAt`                        | `removed`, `removedAt`                                 |
  | `list[i] = v`, `map[key] = v` (`set`)       | `updated(i, v)`, `updated(key, v)`                     |
  | `sort(by:)`, `reverse`                      | `sorted(by:)` (lazy stage of every Iterable), `reversed` |
  | `push`, `pop`, `enqueue`, `dequeue`         | `pushed`, `popped`, `enqueued`, `dequeued` (the last two pairs return `(element, rest)?`) |
  | `merge`, `removeAll`, `retainAll`           | `merged`, `union`, `intersection`, `difference`        |

  The participles are default methods of the traits (copy, change the copy, return it), an implementation only
  writes the verbs.
- **Slices:** `list[from..to]` is a `List` again that shares the storage and starts at index 0. It is a value, not a
  window: later changes of the original are not visible in it. As a `var` path it _is_ a window:
  `samples[0..100].sort { _ }`, `fill(buffer[offset..])`. The same holds for `String` and `Array`.
  A slice keeps the storage of the original alive. Implementations copy small slices of big storage on their own;
  `header.compact()` does it explicitly (it gives the value a storage of its own that is exactly as big as needed).
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
- Every `Collection` is an `Accumulator` and therefore a valid target for collectors and channels.
- Lists have no `+`: `Add.add` and `add(value)` would be the same member. Use `addedAll`.
- `List`, `Set`, `Map`, `Option` and `Result` are `Show` wherever their items are, in the format of the generated
  `Show` (see [Values](#values)): `[1, 2]`, `{a, b}`, `["k": v]`, `Some(x)`.
- `List`, `Set` and `Map` are `Equals`/`Hash` wherever their items are, so a `type` with a collection field can be
  compared and be a `Map` key. A `List` is equal, and hashes, in order: two lists with the same items in a different
  order differ. A `Set` or a `Map` is equal **regardless of insertion order** - iteration order is insertion order,
  equality is not - so their hash combines entries with `bitwiseExclusiveOr` instead of folding them in order.
- `for x in xs` works with everything that is `Iterable<Item>`. **The subject is evaluated once, into a temporary,**
  so it is not an open `var` access: changing `xs` inside of the loop is safe and does not affect the loop, and the
  loop variable is a `const` copy of each item.
- Creation: literals, `List.of(1, 2, 3)`, `List.of(...iterable)`, `List.from(iterable)`, `iterable.toList()`,
  `HashMap()`, `Set.of(1, 2)`.

### Pipelines and Collectors

Working with an `Iterable` has three parts, like in Java and Rust:

```trb
const adults = users                     // 1. A source: anything Iterable (collections, ranges, files, channels)
  .filter { _.age >= 18 }                // 2. Lazy stages: nothing runs, nothing is stored
  .sorted { _.name }
  .map { "{_.name} ({_.age})" }
  .take(10)
  .toList()                              // 3. One terminal operation pulls the values through
```

- **Stages are lazy and are values.** `map`, `filter`, `filterMap`, `mapWhile`, `flatMap`, `take`, `skip`, `takeWhile`, `zip`, `indexed`,
  `sorted` return an `Iterable` again. A pipeline can be stored, passed around, extended and iterated more than once.
  Values are pulled one by one and only as far as needed, so infinite sources (`1..`) and big files just work.
- **Terminal operations decide where the values end up:** `toList()`, `to<Set<String>>()` (any `From<Iterable<Item>>`),
  `fold`, `find`, `first`, `any`, `all`, `count`, `sum`, `joined(separator:)`, `forEach`, `for ... in` - and the
  general one, `collect`.
- `joined(separator: String = "")` is `Iterable` where `Item: Show`: `[1, 2].joined(separator: ", ")` is `"1, 2"`,
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
trait Collector<Item, Output> {             // A description
  fn start(self): Accumulator<Item, Output>
}

trait Accumulator<Item, Output> {           // The state of one run, held in a `var`
  fn add(var self, value: Item)
  fn finish(self): Output
}
```

- A collector is written either as a fold with a final step (`collector(initial, finish: { ... }) { state, value => ... }`),
  or, if it needs more, as a type with `var` fields that is an `Accumulator`. Every `Collection` is one out of the box.
- Accumulators are _push-based_. The same collectors therefore work for everything that produces values over time,
  not only for iterables: `channel.collect(counting())`, event streams, async sources.
- The catch of laziness: a stage with side effects does nothing until it is pulled.
  `chunks.map { spawn { ... } }` spawns nothing, `chunks.map { spawn { ... } }.toList()` spawns everything.

This is LINQ in method form and needs nothing but closures. With [`Expression<Value>`](#quoted-expressions-expressionvalue) the very same
code runs against a database: the provider's `filter` takes an `Expression<(row: Row) => Bool>` and translates the tree to SQL
(see `examples/query-provider`).

### One Vocabulary instead of Higher-Kinded Types

`Option`, `Result`, `Task` and `Iterable` share their method names, and the names mean the same everywhere:

| Method                 | Meaning                                                        | Option | Result | Task | Iterable |
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
`Accumulator<Item, Self>` names a type, not a type constructor, see [Traits](#traits).)

- The operations look alike but are not the same: an Option is a value and `map` runs immediately, an Iterable is a
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

Both "all or nothing" targets are ordinary `From<Iterable<...>>` implementations in the standard library - no new
language feature was needed for them.

## Modules and Packages

```trb
use File from "std/fs"                               // Package import: "<owner>/<name>"
use Router from "acme/http/routing"                  // A public module of a package: "<owner>/<name>/<path>"
use Vector2 from "./math/vector2"                    // Relative import, no file extension
use * as math from "std/math"                        // Namespace import
public use Stack, ArrayStack from "./collections/stack"      // Re-export
```

- `src/main.trb` is what `torb run` executes, `src/lib.trb` is what other packages import.
- A path that starts with `./` or `../` is a file. Everything else starts with the name of a package:
  `"owner/name"` is its `src/lib.trb`, `"owner/name/path"` is `src/path.trb` of it. Only `public` declarations can
  be imported from another package, and only packages that `project.trb` lists as dependencies.
- **The standard library is a set of packages of the owner `std`:** `std/prelude`, `std/fs`, `std/io`,
  `std/process`, `std/test`, `std/json`, `std/http`, ... They come with the toolchain and have its version, so they
  need no entry in `dependencies`. What a program can touch is still visible from its imports: no `std/fs`, no files.
- **The prelude is a package, too.** `Project` has the default `prelude "std/prelude"`: the public names of that
  package (`Option`, `Result`, `List`, `Map`, `print`, `do`, ...) are in scope in every file of the project.
  A project can name another one (a teaching subset, the vocabulary of an embedded DSL); a sandbox gives its scripts
  the prelude of the host plus the receiver.
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

A _receiver closure_ is a closure whose first parameter is called `self`. Inside of it, names resolve against the
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
  fn route(var self, path: String, to: String) {
    routes.add(Route(path, to))
  }
}

fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
  var config = ServerConfig()
  configure(config)
  config
}

const config = server {
  host "0.0.0.0"                           // Property command: host = "0.0.0.0"
  port 8080
  database {                               // Property command: configures the field in place
    url "postgres://localhost:5432/mydb"
    poolSize 20
  }
  route "/health", to: "health"            // Method call
}
```

Name resolution order inside of closures and methods: local scope, then the _innermost_ receiver, then the module.
**Exactly one receiver is implicit,** in a method as in a receiver closure. To reach an outer receiver, name
the parameter (`server { s => s.database { url "{s.host}/db" } }`). This prevents the scope leaking that Kotlin
needs `@DslMarker` for.

### Receiver Scripts and the Sandbox

A `.trb` file can be loaded as the body of a receiver closure. That makes TorbScript its own configuration format:

```trb
// config.trb
host "0.0.0.0"
port 8080
database {
  url "postgres://localhost:5432/mydb"
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
  `Script.apply(self, var value: Value): Result<Void, SandboxError>` runs the body against a value of the caller's own
  and reports what went wrong while it ran: a step, memory or time limit, or a panic inside the script. A sandbox whose
  failures aborted the host would not be a sandbox - and the panic that can be recovered from is exactly the one that
  happens inside an interpreter with a heap of its own.
- The path of `Sandbox.load` is relative to the **directory of the project**, not to the file that loads it: that is
  where the program runs, and where a `config.trb` next to `project.trb` sits. `use "./x"` is the other way round - an
  import is relative to the *file*, because that is a question about the source tree and not about the working
  directory.
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

- Control structures are functions with closure or `lazy` parameters (`do`, `unless`, `retry`, `using`, `test`).
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
  response.json<User>()
}

const (user, posts) = all(fetchUser(1), fetchPosts(1)).await()

const task = spawn { expensiveComputation() }    // Task<Int>, runs in parallel
const result = task.await()

const channel = Channel<Int>()
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
  (`native fn add(self, other: Int64): Int64 { ... }`), and `native { ... }` is a block of IR inside of an ordinary
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

  fn open(path: String): Result<Database, SqliteError> { ... }

  fn close(var self) {
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
- **The toolchain is written in TorbScript** (`compiler/`), the VM included. A small interpreter in Rust
  (`bootstrap/`) runs it until it compiles itself and is thrown away afterwards
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
  and which both back ends implement as the same jump to the entry block. Every other call uses the stack, and a
  per-task frame limit (100 000 by default, `--stack-limit`) panics with "stack overflow" - a counter is the only way
  the VM, which has a frame list, and a native binary, which has a C stack, can agree on when that happens. An
  unspecified crash is not a semantics.
- Memory: reference counting. Values cannot form cycles, so they need no cycle collection. Only `shared type`
  objects (and `var` bindings captured by closures) are tracked by a cycle collector. Deterministic cleanup enables
  `using file { ... }`.
- **There are no destructors.** `Close` is an ordinary method, `using` an ordinary function, and the only observable
  destruction order is the nesting of `using` blocks. When a reference count reaches zero is not observable, so a
  release never runs user code - which is why there are no drop flags, no field order rule and no question what a
  panic in a destructor would mean.
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

- Full names instead of abbreviations in the standard library: `Subtract`/`Multiply`/`Divide`/`Remainder`/`Negate`,
  `Expression<Value>`, `TypeReference`, `Result.Error` (not `Err`), `absolute`/`squareRoot`/`ceiling`. `min`/`max` (and `minBy`/`maxBy`) stay short: they are the names people know.
  Files and modules too (`iteration`, `operators`).
- Single-method traits are named like their method (`Hash`, `Equals`, `Compare`, `Length`, `Close`), not `Hashable`,
  `Equatable`, `Comparable`. That is why the keyword is `with` and not `is` or `implements`. Traits used as types are nouns.
- Iteration is a lazy pipeline with collectors (Java streams / Rust iterators) instead of eager methods that return
  lists: streaming, early exit, infinite sources, and the target is chosen at the end. One model only - there is no
  second, eager set of methods on `List`. `toList()` is the price.
- No higher-kinded types. `Option`, `Result`, `Task`, `Iterable` share a vocabulary by convention; `traverse`/`sequence`
  are collection targets (`to<Result<List<Item>, Failure>>()`), `filterMap` bridges Option-returning functions into pipelines.
  Option is deliberately not an `Iterable`: its `map` is eager, the trait promises a lazy one.
- No `Collectable`/`FromIterator` trait: a collection target is simply `From<Iterable<Item>>`, `to<Target>()` is a typed `into()`.
- Collectors are push-based (`Accumulator.add`), so they are not tied to `Iterable` and work for channels and streams.
- `const` instead of `val` as it is clearer (reading many `val` with `var` in between lets you easily miss some)
- `.trb` instead of `.scr` (`.scr` is an executable screensaver on Windows and blocked by mail filters/AV)
- `//`, `/* */`, `/** */` for docs. Block comments do not nest (they did at first: a `/*` inside of a doc comment, as
  in a glob pattern, then opens a comment nobody sees). Editors comment out code with `//`. `#` stays reserved.
- One naming scheme for primitives (`Int`, `Float`, `Bool`, `String`), no lowercase aliases. Casing is a convention
  (linter), not enforced by the compiler.
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
  a second way next to `extend`. Who wants `parser.expression()` writes `extend Parser { fn expression(var self) ... }`.
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
- No `opaque alias`. Distinct types are single-field `type`s plus trait delegation (`with Add, Compare by value`).
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
- Self-hosting: the toolchain is a TorbScript project, bootstrapped by a throwaway interpreter in Rust that has no
  type checker. Everything that lasts (type checker, IR, back ends, VM, tools) is written once, in TorbScript. The
  first native back end emits C, because the shortest path to "the compiler compiles itself" wins.
- Cases are `Type.Case` or `.Case`, never bare (Swift). Before, a bare name in a pattern was a case if one was in
  scope and a binding otherwise, and inside of a type its cases shadowed types of the same name
  (`case Keyword(keyword: Keyword)`). Now names mean one thing. The price is a rule for line breaks: a leading `.`
  continues the line above, except directly inside of a `match`, where it starts an arm.
- Doc comments on every declaration (parameters, fields, cases) instead of `@param` tags or YAML front matter;
  Markdown with conventional headings; examples are run by `torb test`. A tag language would be annotations through
  the back door.
- Extensions of foreign types follow the import of their module (Swift), extensions of own types are part of the
  type (Rust). `use "./module"` without names exists for modules that consist of extensions.
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
- Cycle collector for `shared type` objects instead of `weak` references
- Type parameters are written out (`Item`, `Key`, `Value`, `Failure`, `Output`)
- `nameOf(expression)` through `Expression<Value>`, `typeName<Value>()` as a compile-time function
- Variadics never unpack implicitly, spread (`...`) works on `Iterable`
- `self` in the signature distinguishes methods from static functions, `var self` marks mutation, access is implicit
- Members are public by default, `private` is explicit - one rule for fields and methods (was: fields private,
  methods public; 142 of 160 fields in the examples had to say `public`). Immutability made private-by-default
  pointless for reading. Top-level declarations stay opt-in (`public`).
- `private(var) x` (read for everyone, write for the type only) instead of getters/setters. Not `private(set)` as in
  Swift (there is no `set` in this language, the thing that is private is the `var`), and not an own keyword
  (`guarded` was tried: one more word to learn for something the existing two words already say). It removed
  every "private field plus accessor method of nearly the same name" pair from the examples.
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
- References are second-class (`var` parameters and `var self` only) instead of lifetimes, borrow checking or span
  types. A mutable slice is a `var` path to a range.
- No marker for `var` arguments at the call site (`fill(buffer)`, not `fill(var buffer)`): the signature and the
  tooling show it, and a marker would make DSLs and method calls inconsistent (`buffer.add(1)` has none either).
- Verbs change in place, participles return a changed copy (`sort`/`sorted`, `add`/`added`). Rejected: one name
  plus a `copy { ... }` block, Ruby's `!` suffix, Scala's symbolic operators.
- Collection types are traits, one per kind (`List`, `Set`, `Map`, `Stack`, `Queue` under `Collection`),
  implementations are named after their data structure. Defaults: `ArrayList`, `TrieMap`, `TrieSet`.
- The lazy stage `sortBy` became `sorted(by:)` to fit the verb/participle rule; `list.sort(by:)` sorts in place.
- `Option`/`Result`/`?` instead of exceptions (also: trivial to implement identically in VM and AOT)
- `with` is the only keyword for trait implementation (`implements` is gone), bounds use `where Item: Trait`
- Orphan rule for `extend ... with`
- One member namespace. A method is structurally a constant of the type that holds a receiver closure, `fn` is its
  declaration form. Not a per-instance field: methods cost no memory per instance, cannot be swapped at runtime, and
  value types stay plain data. (Rejected: separate namespaces for fields and methods, Java style.)
- Property commands make the DSL work with one namespace: a command call on a non-callable field writes it
  (`port 8080`) or configures it in place (`database { ... }`). No hand-written setters or section methods.
- Only the innermost receiver is implicit (instead of an annotation like `@DslMarker`)
- `await()` is a postfix method (composes with `?` and chaining)
- No AST macros, no annotations (for now)
- `Void` is the one name that is both a type and its only value (as `Unit` in Kotlin), and `Void`, `Never` and
  `Range<Value>` are ordinary declarations of the prelude. Rejected: a zero-field constructor call `Void()` - it
  would make the only value of the language the one that has to be called into existence, and `Ok(Void)` reads
  like what it is. `Void` carries nothing, so it is no bridge from types to values.
- A temporary is a valid _argument_ for a `var` parameter and still not a valid _base of a path_.
  `using File.open(path)? { ... }` is not an exception to "no dead changes": the callee is the only owner, so there
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
- A captured `var` binding is a shared box, not a reference: it may escape, it is reference counted, and it is
  exempt from the dead-change rule. The one exception to "references are second-class" - the cycle collector had to
  do this anyway.
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
- No bit operators, but a `Bits` trait the integer types come `with` (`bitwiseAnd`, `shiftedLeft(by:)`, ...) plus
  `addedWrapping`/`multipliedWrapping` on `UInt64`. Operators would need precedence rules and a token that `|`
  already spends on literal types; without the methods, `Hash` for a user type, UTF-8 decoding, the bytecode encoding
  and the build cache's hash cannot be written in TorbScript at all. Keeping the wrapping pair off the signed types
  keeps "overflow panics" true wherever a `+` is written.
- `Show` of a `Float` is the shortest decimal that parses back to the same value, with `.0` appended when it has
  neither `.` nor `e`. Two implementations are compared through `Show`, and `printf("%.17g")` is neither shortest nor
  the same across libcs, so the runtime carries its own conversion.
- On floats, `==` stays IEEE-754 and `compare` is a total order with `nan` on top and `-0.0` equal to `0.0`. A "fixed"
  equality would make `==` disagree with `<`, and `sorted` must not depend on the pivot. Floats are not `Hash`, so a
  `nan` key cannot happen.
- `Map` and `Set` iterate in insertion order, in every implementation. The order reaches the output through `Show`, so
  it is language, not implementation - and it is the only order a reader can predict. It costs an index vector.
- A panic prints `panic: <message>` and the site to stderr and exits with 101; nothing else runs (no destructor, no
  `Close`). The message is output, so the conformance suite compares it. A top-level `?` is not a panic: `error: ...`
  and exit code 1.
- No destructors. `Close` is a method, `using` a function, and the only observable destruction order is the nesting of
  `using` blocks. A destructor would need drop flags, a field order rule and a story for a panic inside one, and
  `using` already covers everything that has to be deterministic.
- `Sandbox.load` returns a `Script<Value>` instead of a closure, and `Script.apply(self, var value: Value)` returns a
  `Result<Void, SandboxError>`. A closure of type `(var self: Value) => Void` has nowhere to say that the step limit
  was hit or that the script panicked, and a sandbox whose failures abort the host is not a sandbox.
- A parameter default is evaluated at the call site, at every call, in the scope of the declaration - the same rule as
  for a field default. One rule for both kinds of default, and a default cannot depend on an invisible argument order.
- Tail calls are guaranteed for direct self-recursion in tail position only, and a per-task frame limit panics with
  "stack overflow". Portable C cannot guarantee a general tail call; the guarantee that can be kept is the one `retry`
  and every fold need, and a counter is the only way a frame list and a C stack agree on when the stack is full.
- Multi-line strings are dedented by their first line: the indentation of the first line with content is stripped
  from every line, a lesser or mismatched indentation is a lexer error, and a leading line break right after `"""`
  is never part of the string. This lets a code block sit at the indentation of the call around it instead of at
  the left margin. `torb format` will enforce the layout it produces.
- `String.join(parts, separator:)` became `Iterable.joined(separator:)`, now that a member can carry its own `where`
  clause (`where Item: Show`). One way to join instead of two, and it reads left to right with the rest of a
  pipeline; the collector `joining` stays for a prefix, a suffix, or a step inside `collect`.
- `&` instead of `+` for an intersection of traits (was: `+`, from Rust).

## Open Questions

- Exclusivity is conservative for now. Collect the correct programs it rejects (closures that capture a `var`
  binding and run during a `var` access, paths through `[]`) here, and decide with a compiler at hand:
  - (none yet)
- `deprecated` (and `since`): not documentation but something the compiler has to read. A modifier? Decide when the
  first API needs it. The use case that settles its shape: a field that becomes a method. The field stays for one
  version next to the new method, marked `deprecated` with its replacement, and `torb lint --fix` rewrites the callers
  (`.x` to `.x()`); the language server offers the same as a quick fix.
- Registry protocol and the exact format of `project.lock.trb`
- REPL: every input is a nested scope of the previous one (so redefining a name is ordinary shadowing). A type that
  is defined again shadows the old one, values of the old type keep it and show up as `Point#1`.
