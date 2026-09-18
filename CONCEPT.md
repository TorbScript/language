# TorbScript Concept

> Working name. So are derived things like the file extension (`.trb`) and the CLI name (`torb`).

## Key Facts

- Functional-first, multi-paradigm scripting language. Not Haskell-style purity, but the functional principles are
  kept clean: immutable by default, expressions over statements, values over identity, mutation always explicitly marked.
- C-style syntax with elements taken from Scala, Kotlin, Groovy, Rust, TypeScript, Swift
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
   `(self: T) => R` resolves names against `T`. A closure passed to `(value: Int) => Int` may use `value` implicitly.
   An expression passed to `lazy T` is not evaluated at the call site. This one principle powers DSLs,
   custom control structures and query providers, without macros or annotations.
2. **Mutation is always visible.** `var` bindings, `var` fields (`private(var)`: only the type itself), `var` parameters,
   `var self` methods, `var type`s,
   Everything not marked is immutable.
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
torb repl           # Interactive session
torb add <package>  # Add a dependency
```

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
  always "some-library:^1.2.3"
  optional "another-library:^2.3.4"
  dev "dev-library:^3.4.5"
  test "test-library:^4.5.6"
  suggest "suggested-library:^5.6.7", because: "it provides additional optional features"
}
build {
  target "dev"
  input "src/main.trb"
  output "build/{target}/{project.name}"   // Eager interpolation: reads the fields set above
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
  dependencies.always("some-library:^1.2.3")       // `always`, `suggest`, ... are methods of Dependencies
  dependencies.suggest("suggested-library:^5.6.7", because: "it provides additional optional features")
}
configureDependencies(self.dependencies)   // `dependencies` is a field, configured in place

const configureBuild: (var self: Build) => Void = { build =>
  build.target = "dev"
  build.output = "build/{build.target}/{build.project.name}"
}
configureBuild(self.build)
```

(`{project.name}` instead of `{name}`, because only the innermost receiver is implicit - see
[Configuration DSL](#configuration-dsl).)

## Lexical Structure

```trb
// Line comment
/* Block comment /* they nest */ so code containing comments can be commented out */
/** Doc comment, attached to the following declaration. Markdown. */
```

- Statements end at the end of the line. A statement continues on the next line if the current line ends with an
  operator, `,` or an open bracket, or if the next line starts with `.`, `?.`, a binary operator, `with` or `where`.
  There are no semicolons.
- Naming is convention, not grammar: `UpperCamelCase` for types, traits, type parameters and variants,
  `lowerCamelCase` for everything else. The linter checks it, the compiler does not care.
- **Names are written out.** `Expression`, `Subtract`, `Remainder`, `absolute`, `squareRoot` - not `Expr`, `Sub`,
  `Rem`, `abs`, `sqrt`. Abbreviations are only fine where the abbreviation _is_ the name people know
  (`Html`, `Json`, `Sql`, `Http`, `Int64`, `min`/`max`). This holds for the standard library, and is a linter hint for user code.
- Generics vs. comparison (`load<Config>(path)` vs. `a < b`) is decided purely syntactically, without knowing what
  the names mean (the C#/Kotlin/TypeScript approach): after a name, `<` starts a type argument list if the tokens up
  to the matching `>` form valid types **and** the token after `>` is one of `(` `.` `{` `)` `]` `,` `:` or the end
  of the line. Otherwise it is "less than". Comparison operators are non-associative (`a < b > c` is never a valid
  comparison), so the generic reading never steals a meaningful expression. In type positions (after `:`, `with`,
  `where`, ...) there is no ambiguity to begin with.
- Number literals: `10`, `1_000_000`, `0xFF`, `0b1010`, `3.14`, `1e9`
- String literals: `"text"`, interpolation with `{expr}`, literal brace with `\{`. Multi-line strings with `"""`.
  Raw strings (`r"..."`, `r"""..."""`) have no interpolation and no escapes (JSON, regular expressions, paths).
- Char literals: `'A'`

## Bindings

```trb
const y = 30        // Immutable binding
var x = 20          // Mutable binding
const z: Float = 1  // Optional type annotation; literals adapt to the expected type

var a               // Compile error: bindings must be initialized
var b: Int          // Compile error: no implied default value
```

`const` is deep from the perspective of the binding: through a `const` binding you can neither reassign, nor assign
fields, nor call `var self` methods. `var` means "mutable through this path".

Assignment is a statement, not an expression.

## Built-in Types

```trb
const someInt = 10                         // Int
const someFloat = 3.14                     // Float
const someChar = 'A'                       // Char (Unicode scalar value)
const someString = "Hello"                 // String (UTF-8)
const someBool = true                      // Bool
const someTuple = (1, "one")               // (Int, String), access with `.0`, `.1` or destructuring
const someList = [1, 2, 3]                 // List<Int>          (persistent, immutable)
const someMap = ["a": 1, "b": 2]           // Map<String, Int>   (persistent, immutable)
const someRange = 0..10                    // Range<Int>, `0..=10` is inclusive, `0..` is open-ended (infinite)
const someOption: Int? = None            // `T?` is sugar for `Option<T>`
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
- `Void` is the type with exactly one value, `Never` the type of expressions that do not return (`panic`, `return`).

### Type Aliases

There is no `alias` keyword. `type Name { ... }` declares a new type, `type Name = ...` names an existing one.

```trb
type Int = Int64                                   // This is how the prelude declares `Int`
type Handler = (request: Request) => Response
type Pair<T> = (T, T)                              // Aliases can be generic
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
> The bigger cost sits behind it: if types are values, generics become functions over types (`fn List(T: Type): Type`).
> Those cannot be inferred by unification (`numbers.map { _ * 2 }` could no longer infer `U`), bounds could only be
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
- Parameter types are mandatory on `fn`. The return type can be inferred, but is mandatory for `public` functions
  and trait methods.
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

### Trailing Closures

If the last parameter of a function is a function, a brace closure can follow the call.

```trb
numbers.map({ _ * 2 })
numbers.map { _ * 2 }
numbers.fold(0) { sum, number => sum + number }
```

The implicit parameter can be named by the _type of the function_:

```trb
fn map<U>(self, transform: (value: T) => U): Iterable<U>

numbers.map { value * 2 }
```

If such a name would shadow a name that is visible at the closure, it is a compile error (no silent shadowing).

### Command Calls (calls without parentheses)

```trb
print "Hello"
route "/health", to: "health"
const email = Email.parse "info@example.test"
test "adds two numbers" {
  expect(sum(1, 2)).toBe 3
}
```

Rules:

- Only allowed in _command position_: at the start of a statement, on the right side of `=`, after `return`,
  after `=>`, or as the last argument of another command call (`a b c` is `a(b(c))`).
- Never inside parentheses, brackets, operators or regular argument lists.
- The callee is a name or a member path (`print`, `Email.parse`, `expect(x).toBe`).
- Arguments are separated by `,`. The first argument must not start with `(`, `[`, `-` or `!`
  (`f [1]` is always indexing, `f -1` is always subtraction). Use parentheses in these cases.
- A trailing closure always belongs to the outermost command call of the statement
  (`unless list.isEmpty() { ... }` passes the closure to `unless`). Consequently, the arguments of a command call
  cannot contain trailing closures themselves: `print numbers.map { _ * 2 }` is an error, write
  `print numbers.map({ _ * 2 })`. The same is true for the heads of `if`, `for`, `while` and `match`.
- Calls without arguments always need `()`. A bare name is always a reference.

### Parameter Modes

| Parameter type     | The argument is...                                                   | Used for                          |
|--------------------|----------------------------------------------------------------------|-----------------------------------|
| `T`                | evaluated at the call site                                           | everything                        |
| `() => T`          | a closure                                                            | control structures, callbacks     |
| `(self: R) => T`   | a closure whose names resolve against `R` (receiver closure)         | builders, DSLs, config files      |
| `lazy T`           | any expression, evaluated at most once, on first use                 | `opt.orElse(expensive())`, logging |
| `Expression<T>`          | quoted: the typed expression tree, plus the value                    | query providers, `assert`         |

### Quoted Expressions (`Expression<T>`)

If a parameter (or binding) has the type `Expression<T>`, the argument is type checked as a normal `T` first and then
passed _together with its expression tree_. The call site looks like any other:

```trb
const minAge = 18
const adults = users.filter { _.age >= minAge }             // List<User>:   predicate: (value: T) => Bool
const query = db.users.filter { _.age >= minAge }           // Query<User>:  predicate: Expression<(row: T) => Bool>
// SELECT * FROM users WHERE age >= ?    [18]

assert(adults.length() > limit)                             // fn assert(condition: Expression<Bool>)
// Assertion failed: adults.length() > limit   (limit = 5)   at main.trb:12
```

```trb
// std/expression
native type Expression<T> {
  tree: ExpressionNode                  // Static data, created at compile time. Quoting costs nothing at runtime.
  source: String                  // "_.age >= minAge"
  location: SourceLocation
  native fn value(self): T               // The ordinary value/closure. Evaluated at most once for non-functions.
  native fn captures(self): List<Data>   // Values of the captured variables, converted on demand
}

open type ExpressionNode {
  case Literal(value: Data, of: TypeReference)
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
}
```

- **Quotable is what is an expression:** literals, parameters, captured variables, field access, calls, operators,
  constructors, `if`/`else`, nested closures, list literals. A quoted closure must consist of a single expression.
  Statements (`const`, `var`, assignment, loops, `return`, `await()`) are a compile error inside of a quotation.
- **Quoting happens after name resolution and type checking.** That is why it does not have the phase problem of
  macros: implicit `_`, named parameters, receivers and implicit `self` are already resolved, the tree only contains
  explicit `Parameter`, `Field` and `Call` nodes, each with its type.
- **The tree is data, not reflection.** `TypeReference` is a description (`name`, `arguments`), there is no way back from it
  to a type. Trees are const values: they can be matched, transformed, compared, hashed, serialized (`ToData`).
- **Captured variables must be `ToData`**, because a provider has to be able to look at them (SQL parameters).
  Capturing anything else in a quotation is a compile error.
- **The tree cannot be executed**, the value can. There is no `compile()` like in C#, so compiled binaries need no
  interpreter for this. An in-memory provider calls `value()`, a SQL provider reads `tree`.
- What a provider does not understand (`filter { myOwnFunction(_) }`) is the provider's error at runtime
  (`Error(Unsupported(...))`), the language cannot know what a library can translate.
- `ExpressionNode` is part of the language standard. It is an _open_ type: matches on it need a `_` arm, so new node kinds
  do not break existing providers.

## Blocks and Control Flow

`{ ... }` in expression position is a closure. To evaluate a block immediately, use `do` - which is an ordinary
function from the standard library (`fn do<T>(body: () => T): T { body() }`), not a keyword.

```trb
const initialized = do {
  const base = [1, 2, 3]
  base.add(4).add(5).remove(2)
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

## Types

`type` is the single keyword for all data types (struct, class, enum, ADT). A `type` is an immutable value type, `var type` makes it a mutable reference type. Like everywhere else in the language: what is not marked `var` does not change.

### Value Types (the default)

```trb
type Point {
  x: Int
  y: Int

  // Method: declares `self`. Member access through `self` is implicit.
  fn area(self): Int {
    x * y
  }

  // Static function: does not declare `self`.
  fn square(size: Int): Self {
    Self(size, size)
  }

  // Static constant
  const origin = Point(0, 0)
}

var p = Point(x: 10, y: 20)    // or positional: Point(10, 20)
p.x = 20                       // Compile error
p = p.copy(y: 30)              // `copy` is generated for every `type`
print "The area is {p.area()}"
```

- A `type` is deeply immutable: it has no `var` fields, no `var self` methods, and none of its fields is a `var type`.
  (A generic `type Box<T>` is exactly as immutable as its `T`.)
- It has value semantics: structural `Equals`, `Hash` and `Show` are generated, there is no identity.
  (Each of them only if all fields support it: a type with a function in a field has no generated `Equals`.)
- Const values can be freely shared between tasks.

### Mutable Reference Types (`var type`)

```trb
var type Counter {
  var count: Int = 0    // Fields are `const` unless marked `var`
  step: Int = 1

  fn increment(var self) {     // Mutating methods declare `var self`
    count = count + step
  }
}

var counter = Counter()
counter.increment()

const frozen = Counter()
frozen.increment()             // Compile error: `var self` method through a `const` binding
```

A `var type` has identity and reference semantics: two counters with the same count are different counters. `Equals`, `Hash`, `copy` and `ToData` are not generated for it. It can hold values of plain `type`s, but not the other way around. Most types of a program should not need it - typical cases are builders, caches, iterators, buffers and handles to the outside world.

### Construction

Every type has exactly one constructor. It is generated from the fields, in declaration order, and cannot be
written by hand. **Constructors never contain logic.**

- Fields with a default value can be omitted.
- Outside of the type, `private` fields cannot be passed. So the constructor is usable from outside if and only if
  every private field has a default value. Inside of the type (`Self(...)`) all fields can be passed.

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

### Visibility and Encapsulation

**Members are public unless marked `private`** - fields, methods and constants alike. In a language where a `type` is
immutable, reading a field cannot break anything, and a value type _is_ its data. What needs protection is mutation
and invariants, and both have a modifier:

```trb
var type Account {
  owner: String                          // Public, const
  var nickname: String = ""              // Public, writable by everyone who has a `var` path to the account
  private(var) balance: Int = 0           // Everybody reads, only Account writes
  private var history: MutableList<String> = ArrayList()     // Invisible from outside

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
| `x: T`               |        yes        |  - (const)         |
| `var x: T`           |        yes        |  yes               |
| `private(var) x: T`  |        yes        |  no                |
| `private x: T` / `private var x: T` | no |  no                |

- `private(var)` reads as "the `var` is private": the field is public, its mutability is not. It hands outsiders a _const path_ to the field, and const is deep: with `private(var) routes: MutableList<Route>`,
  `config.routes` can be read and iterated from outside, but `config.routes.add(...)` is a compile error. No defensive
  copies, no accessor methods.
- **There are no getters, setters or properties.** A field is storage, a method computes, and the `()` tells which one
  it is (`list.length()` may cost something, `point.x` never does). No `get` prefixes; predicates are called
  `isEmpty()`/`hasX()`, mutators are verbs with `var self`.
- There are no validating setters, because a setter cannot fail properly in a language without exceptions.
  Validation lives in types and factories (`Email.parse`, `Port.tryFrom(8080)`) or in a method that returns a `Result`
  (`account.withdraw(amount)`).
- **Top-level declarations are private to their file unless marked `public`.** This is a different question - the
  surface of a module is opt-in, `lib.trb` defines the API of a package.

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
  value types stay plain data (`Equals`, `Hash`, `ToData`). A trait is a list of constants a type has to provide.
- An instance field can hold a function, too (`public onClick: () => Void`), and is called the same way:
  `button.onClick()`. Whether `x.name(...)` calls a method or a function in a field is not visible at the call site,
  and does not need to be.
- Because there is one namespace, a field and a method cannot share a name.

**Property commands.** A call (usually written as a command call) on a member that is a _field and not callable_ is
a write to that field.
This is what gives the configuration DSL its Groovy look without a single hand-written setter:

```trb
port 8080                      // Field `var port: Int`:              port = 8080
database {                     // Field `var database: DatabaseConfig`: the receiver closure is applied to the
  url "postgres://..."         //   field's value, which is configured in place
}
print "Listening on {port}"    // Reading is just the name
```

Both forms need a `var` path to the field. A callable field is called, not assigned (`onClick { ... }` calls it, use
`onClick = { ... }` to set it).

## Algebraic Data Types and Pattern Matching

Variants are declared with `case` inside of a `type`.

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(self): Float {
    match self {
      Circle(radius) => Float.pi * radius * radius
      Rectangle(width, height) => width * height
      Empty => 0.0
    }
  }
}

const shape = Shape.Circle(2.0)
```

Variants are namespaced (`Shape.Circle`). In patterns, and wherever the expected type is known, the prefix can be
omitted. `Some`, `None`, `Ok`, `Error` are always in scope.

`match` is an expression and must be exhaustive. For an `open type`, exhaustive means "has a `_` arm": the
owner may add variants without breaking its users (used by `ExpressionNode`, useful for error types of libraries).

```trb
const description = match value {
  0 => "zero"                              // Literal
  1 | 2 | 3 => "small"                     // Alternatives
  4..=9 => "medium"                        // Range
  n if n < 0 => "negative"                 // Binding + guard
  _ => "large"                             // Wildcard
}

match (shape, position) {                  // Tuples
  (Circle(r), Point(x: 0, y: 0)) => ...    // Variants, types (positional or labeled)
  (_, Point(x, y)) => ...
}

match list {
  [] => "empty"
  [only] => "one element"
  [first, ...rest] => "many"
}
```

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

In a pattern `_` is the wildcard, in an expression `_` is the implicit closure parameter. The positions never overlap.

A bare name in a pattern refers to a variant or constant if one with that name is in scope, otherwise it is a new
binding. A misspelled variant would silently become a catch-all binding, so the compiler reports unreachable arms
as errors and the linter flags uppercase bindings in patterns.

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

extend<T> List<T> with Show where T: Show { ... }   // Type parameters are declared on `extend`
```

- **Naming:** a trait is a capability the type comes _with_, so a trait with a single required method is named like
  that method: `Hash` (`hash`), `Equals`, `Compare`, `Show`, `Add`, `From`, `Length`, `Close`. `type Money with Equals,
  Hash, Compare` reads as what it is. No `-able`/`-ible` adjectives. Traits that are mainly used _as types_ are nouns:
  `Iterable`, `Iterator`, `Collection`, `List`, `Map`, `Collector`, `Accumulator`.
- `with` is the only keyword for "implements" and for supertraits. Bounds use `where T: Hash + Equals` or inline `<T: Hash>`.
- **Coherence:** you can only `extend X with Trait` if your package owns `X` or `Trait`. Extension methods without a
  trait are only visible where they are imported.
- A trait can be used as a type (`fn draw(shape: Shape)`). Whether this is dispatched statically or dynamically is
  up to the implementation and not observable.
- Functions without `self` in a trait: without a body they are a requirement for the implementing types
  (`From.from`, `Parse.parse`). With a body they are functions of the trait itself - the place for factories that pick
  a default implementation (`List.of(1, 2)`, `MutableSet.of("a")`).
- Because a trait is a type, it can be extended like one. `extend<T> List<T> with Show where T: Show` makes every list
  showable, `extend<T> List<T> with From<Iterable<T>>` makes `List<T>` itself a valid target of `to<List<T>>()`.
- Operators are traits: `+` is `Add.add`, `==` is `Equals.equals`, `<` is `Compare.compare`, `a[i]` is `Indexed.at`,
  `a[i] = v` is `MutableIndexed.set`, string interpolation is `Show.show`.

## Types, Values and Reflection

Types and values are strictly separate worlds:

- A type never flows as a value. There is no `Type` type, no `typeof`, no `value is T` on generic `T`, no
  `Class.forName`. Types appear only in type positions (after `:`, in `<>`, after `with`/`where`, right of `type X =`).
- The only bridges are syntactic: `Point(...)` (constructor), `Point.origin` / `Point.parse(...)` (static members),
  `Shape.Circle` (variants), `Point.area` (method reference).
- So there is **no runtime reflection**. It could not be implemented identically in all back ends (monomorphized vs.
  boxed generics would become observable), it keeps every type's metadata alive in compiled binaries, and it is the
  meta-programming style this language does not want.

What reflection is usually needed for (serialization, config mapping, database rows, diffing, debug output) is
covered by one more _generated trait pair_, in the same way `Equals`, `Hash` and `Show` are generated:

```trb
type Data {                          // A small, closed data model (think: what JSON/TOML/rows have in common)
  case Nothing
  case Boolean(value: Bool)
  case Integer(value: Int)
  case Number(value: Float)
  case Text(value: String)
  case Sequence(items: List<Data>)
  case Record(fields: Map<String, Data>)
  case Variant(name: String, fields: Map<String, Data>)
}

trait ToData   { fn toData(self): Data }
trait FromData { fn fromData(data: Data): Result<Self, DataError> }
```

```trb
type User {
  name: String
  email: Email
  tags: List<String> = []
}

const text = Json.encode(user)                 // fn encode<T: ToData>(value: T): String
const user = Json.decode<User>(text)?          // fn decode<T: FromData>(text: String): Result<T, JsonError>
```

- `ToData` is generated for every `type` whose fields are all `ToData`.
- `FromData` is only generated if the constructor is usable from outside (see [Construction](#construction)).
  A type with a private constructor has invariants, so it writes `fromData` by hand and the invariant holds for
  decoded values, too (`Email.fromData` calls `Email.parse`).
- Formats (`Json`, `Toml`, `Yaml`, database drivers, ...) are ordinary libraries over `Data`. They never see types.
- Different field names, skipped fields, versioning: write the two functions by hand, there are no annotations.
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

panic "unreachable"                                      // Bugs. Not catchable, aborts the task.
```

- `collection.get(i)` returns `T?`, `collection[i]` panics when out of bounds.

## Collections and Iteration

The collection types are **traits**. Signatures, fields and bindings talk about traits, an implementation is only
named where something is constructed.

```text
Iterable<T>
└─ Collection<T>              length, isEmpty, contains
   ├─ ListView<T>             any list, for reading: get, [], first, last, indexOf
   │  ├─ List<T>              a value: every "change" returns a new List                 TrieList
   │  └─ MutableList<T>       an object: changed in place                                ArrayList
   ├─ SetView<T>              Set<T> (TrieSet), MutableSet<T> (HashSet)
   ├─ MapView<Key, Value>     Map<Key, Value> (TrieMap), MutableMap<Key, Value> (HashMap)
   ├─ StackView<T>            Stack<T> (ListStack), MutableStack<T> (ArrayStack)
   ├─ QueueView<T>            Queue<T> (ListQueue), MutableQueue<T> (ArrayQueue)
   └─ MutableCollection<T>    everything that can be filled: add, addAll, clear (all Mutable*)
```

```trb
type Inventory {
  items: Map<String, Int> = [:]                           // A value in a value
}

var type Scheduler {
  private var pending: MutableQueue<Job> = ArrayQueue()   // Trait as the type, implementation at construction
}

fn lookup(table: MapView<String, Int>): Int { ... }       // Any map, read only
fn describe<T>(items: Collection<T>): String { ... }      // Any collection, read only
fn fill(var target: MutableCollection<Int>) { ... }       // Anything that can be filled: list, set, stack, queue, ...
```

- **Every collection exists twice,** as a value and as an object - the same split as `type` and `var type`.
  The persistent half is the default (literals, `toList()`, `groupBy`). The mutable half is for building things up,
  for caches and for hot loops; a mutable stack or queue is often simply the right tool.
- **Every kind has three traits, and the name says what you know:** `Map` is a value (nobody changes it behind your
  back), `MutableMap` is an object that is changed in place, `MapView` is "one of the two": you can read it, and it may
  change while you hold it. Parameters of functions that only read are `View`s, fields and results are usually values.
- **The halves are siblings: a `MutableList` is not a `List`.** That a `List<T>` never changes is what allows it as
  a field of a `type` and lets it cross task boundaries. `mutable.toList()` takes the snapshot.
- The short name belongs to the value, because that is what literals, fields and results are in a functional-first
  language. (Kotlin gives the short name to the view, and `List` means "probably does not change".)
- Methods have the same names in both halves (`add`, `set`, `remove`, `push`, `enqueue`). The signature tells
  them apart: `fn add(self, value: T): List<T>` versus `fn add(var self, value: T)`.
- The traits do not constrain their type parameters, the implementations do: `TrieMap<Key: Hash, Value>`, a sorted
  map needs `Key: Compare`. Only the factories (`Map.of`, `Map.from`, literals) ask for `Hash`, because they pick `TrieMap`.
- Implementations are named after their data structure: `TrieList`/`TrieMap`/`TrieSet` (persistent tries),
  `ArrayList`, `HashMap`, `HashSet`, `ListStack`/`ListQueue` (persistent, on top of `List`), `ArrayStack`,
  `ArrayQueue` (ring buffer). Your own implementation is a type `with Map<Key, Value>` and works everywhere.
  `Array` (fixed size) is the low-level building block and a `Collection`, too.
- Every `MutableCollection` is an `Accumulator` and therefore a valid target for collectors and channels.
- Lists have no `+`: `Add.add` and `add(value)` would be the same member. Use `addAll`.
- `for x in xs` works with everything that is `Iterable<T>`.
- Creation: literals, `List.of(1, 2, 3)`, `List.of(...iterable)`, `List.from(iterable)`, `iterable.toList()`,
  `ArrayList<Int>()`, `MutableSet.of(1, 2)`.

### Pipelines and Collectors

Working with an `Iterable` has three parts, like in Java and Rust:

```trb
const adults = users                     // 1. A source: anything Iterable (collections, ranges, files, channels)
  .filter { _.age >= 18 }                // 2. Lazy stages: nothing runs, nothing is stored
  .sortBy { _.name }
  .map { "{_.name} ({_.age})" }
  .take(10)
  .toList()                              // 3. One terminal operation pulls the values through
```

- **Stages are lazy and are values.** `map`, `filter`, `filterMap`, `mapWhile`, `flatMap`, `take`, `skip`, `takeWhile`, `zip`, `indexed`,
  `sortBy` return an `Iterable` again. A pipeline can be stored, passed around, extended and iterated more than once.
  Values are pulled one by one and only as far as needed, so infinite sources (`1..`) and big files just work.
- **Terminal operations decide where the values end up:** `toList()`, `to<Set<String>>()` (any `From<Iterable<T>>`),
  `fold`, `find`, `first`, `any`, `all`, `count`, `sum`, `forEach`, `for ... in` - and the general one, `collect`.
- **Collectors** are reusable, composable descriptions of "what to do with the values":

```trb
const payroll = employees.collect(summing { _.salary })
const names = employees.map { _.name }.collect(joining(", "))
const (veterans, others) = employees.collect(partitioningBy { _.age >= 40 })

const salaryByDepartment = employees.collect(groupingBy { _.department }.then(averaging { _.salary }))
const teams = employees.collect(groupingBy { _.department }.then(into<Set<Employee>>()))
```

```trb
trait Collector<T, Result> {             // An immutable description
  fn start(self): Accumulator<T, Result>
}

trait Accumulator<T, Result> {           // The state of one run, a `var type`
  fn add(var self, value: T)
  fn finish(self): Result
}
```

- A collector is written either as a fold with a final step (`collector(initial, finish: { ... }) { state, value => ... }`),
  or, if it needs more, as a `var type` that is an `Accumulator`. Every `MutableCollection` is one out of the box.
- Accumulators are _push-based_. The same collectors therefore work for everything that produces values over time,
  not only for iterables: `channel.collect(counting())`, event streams, async sources.
- The catch of laziness: a stage with side effects does nothing until it is pulled.
  `chunks.map { spawn { ... } }` spawns nothing, `chunks.map { spawn { ... } }.toList()` spawns everything.

This is LINQ in method form and needs nothing but closures. With [`Expression<T>`](#quoted-expressions-expressiont) the very same
code runs against a database: the provider's `filter` takes an `Expression<(row: T) => Bool>` and translates the tree to SQL
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

(`Task` is part of the concurrency draft and not in the example standard library yet.)

This is a convention of the standard library, not an abstraction of the language. There are **no higher-kinded types**
(`Functor<F<_>>`, `Monad`), no `Self<U>`, no F-bounded tricks:

- The operations look alike but are not the same: an Option is a value and `map` runs immediately, an Iterable is a
  pipeline and `map` runs when it is pulled. An abstraction over both would hide exactly that difference.
- A kind system, partially applied type constructors (`Result<_, E>`) and higher-order unification would cost the
  local type inference, readable error messages and a simple mental model - for code that scripts rarely need.

What higher-kinded types are typically used for is covered by things that already exist:

| Need                                         | Solution                                                                       |
|----------------------------------------------|--------------------------------------------------------------------------------|
| Chaining fallible steps                      | `?` for Option and Result, `await()` for Task                                  |
| `List<Result<T, E>>` to `Result<List<T>, E>` | A collection target: `.to<Result<List<Int>, ParseError>>()`, stops at the first error |
| `List<T?>` to `List<T>?`                     | `.to<List<User>?>()`, stops at the first `None`                                |
| A function returning an Option as a stage    | `ids.filterMap { findUser(_) }`                                                |
| An Option or Result inside a pipeline        | `users.flatMap { _.manager.toList() }`                                         |

Both "all or nothing" targets are ordinary `From<Iterable<...>>` implementations in the standard library - no new
language feature was needed for them.

## Modules and Packages

```trb
use List, MutableList from "std/collections"   // Package import: "<package>/<path>"
use Vector2 from "./math/vector2"                    // Relative import, no file extension
use * as math from "std/math"                  // Namespace import
public use Stack, MutableStack from "./collections/stack"    // Re-export
```

- `src/main.trb` is what `torb run` executes, `src/lib.trb` is what other packages import.
- The prelude (`Option`, `Result`, `List`, `Map`, `print`, `do`, ...) is always in scope.
- In entry files and scripts, a top-level `?` ends the program with the error, and top-level `await()` is allowed.
- **Top-level code is only allowed in entry files and scripts.** Imported modules consist of declarations only,
  top-level `const` initializers of modules must be compile-time evaluable. So there is no module initialization
  order, and cyclic imports are unproblematic.

## Configuration DSL

A _receiver closure_ is a closure whose first parameter is called `self`. Inside of it, names resolve against the
receiver, exactly like inside of a method. Together with command calls, trailing closures and property commands this
gives Groovy-style builders that are completely statically typed.

```trb
var type DatabaseConfig {
  var url: String = ""
  var poolSize: Int = 10
}

var type ServerConfig {
  var host: String = "localhost"
  var port: Int = 8080
  var database: DatabaseConfig = DatabaseConfig()
  private var routes: MutableList<Route> = ArrayList()

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

Name resolution order inside of closures and methods: local scope, then the _innermost_ receiver, then the
surrounding `self`, then the module. **Only the innermost receiver is implicit.** To reach an outer receiver, name
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
const sandbox = Sandbox()                                   // No capabilities: no IO, no network, no clock
const configure = sandbox.load<ServerConfig>("./config.trb")?   // (var self: ServerConfig) => Void
const config = server(configure)
```

- The file is type checked against `ServerConfig` (errors with line numbers, autocompletion in the editor).
- The script can only reach what the receiver type exposes, plus the pure parts of the prelude. The type argument is
  the whitelist.
- Limits for steps/memory/time can be set on the `Sandbox`.
- Because the type argument is known statically, a compiled binary knows exactly which types need to be callable from
  interpreted code. No annotations or reflection needed.
- `project.trb` is exactly this mechanism with the receiver `Project`.

## Extensibility

The language is extended by functions, not by macros or annotations:

- Control structures are functions with closure or `lazy` parameters (`do`, `unless`, `retry`, `using`, `test`).
- DSLs are functions with receiver closures.
- Operators are traits.
- Code that needs to be _looked at_ instead of executed (query providers, `assert`, validation rules, change
  tracking) uses [`Expression<T>`](#quoted-expressions-expressiont) parameters.
- Declarations (`const`, `var`, `fn`, `type`, `trait`, `extend`, `use`) are the fixed core, because they bind names.
  They all share the uniform shape `modifier* keyword Name clauses* { body }`, which reads like a command call.
- One possible future step: compile-time functions that _produce_ types (`type Row = Schema.rowOf("users.sql")`),
  evaluated by the interpreter that exists anyway. As an addition to declarative generics, never as their
  replacement - see the note under [Type Aliases](#type-aliases) for what that replacement would cost.

AST macros are a non-goal: names in TorbScript are resolved with the help of types (receivers, named implicit
parameters), macros would have to run before name resolution. The two do not go together. `Expression<T>` is the opposite
of a macro: it reads code _after_ it was resolved and type checked, and it cannot generate any.

## Concurrency (Draft)

```trb
async fn fetchUser(id: Int): Result<User, HttpError> {
  const response = http.get("/users/{id}").await()?
  response.json<User>()
}

const (user, posts) = all(fetchUser(1), fetchPosts(1)).await()

const task = spawn { expensiveComputation() }    // Task<T>, runs in parallel
const result = task.await()

const channel = Channel<Int>()
```

- Calling an `async fn` returns a `Task<T>`. `await()` is a method on `Task`, only callable inside of `async` code.
- Const values are shared freely between tasks. Mutable objects are confined to the task that created them, tasks
  communicate through channels. Data races are impossible by construction.

## Execution Model

```text
Source -> Parse -> Resolve + Typecheck -> Typed IR -+-> Bytecode VM          (torb run, repl, sandbox, compile-time)
                                                    +-> Native code           (torb build)
```

- One front end, one typed IR. The semantics of the language are defined on the IR. A conformance test suite runs
  every test against all back ends.
- `torb run` type checks the whole program before it starts. The IR is cached.
- `torb build` stage 1: runtime stub + embedded bytecode (single file, trivial cross compilation, available from day
  one). Stage 2: real AOT from the same IR (Cranelift/LLVM/C). Nothing in the language depends on the stage.
- Binaries that use `Sandbox.load` embed the front end and the VM. Interpreted code calls compiled methods through a
  bridge that is generated for the receiver types.
- Generics may be monomorphized or boxed. Not observable (no `sizeof`, no layout, no reflection on type parameters).
- Integer overflow panics, in every back end. Evaluation order is left to right. Tail calls in tail position are guaranteed.
- Memory: reference counting. Deeply immutable values cannot form cycles, so `type` values need no cycle
  collection. Only `var type` objects are tracked by a cycle collector. Deterministic cleanup enables
  `using file { ... }`.
- `native` declarations are implemented by the runtime. The interpreter resolves them through a built-in table,
  compiled binaries link them statically. Same ABI.

## Decision Log

- Full names instead of abbreviations in the standard library: `Subtract`/`Multiply`/`Divide`/`Remainder`/`Negate`,
  `Expression<T>`, `TypeReference`, `Result.Error` (not `Err`), `absolute`/`squareRoot`/`ceiling`. `min`/`max` (and `minBy`/`maxBy`) stay short: they are the names people know.
  Files and modules too (`iteration`, `operators`).
- Single-method traits are named like their method (`Hash`, `Equals`, `Compare`, `Length`, `Close`), not `Hashable`,
  `Equatable`, `Comparable`. That is why the keyword is `with` and not `is` or `implements`. Traits used as types are nouns.
- Iteration is a lazy pipeline with collectors (Java streams / Rust iterators) instead of eager methods that return
  lists: streaming, early exit, infinite sources, and the target is chosen at the end. One model only - there is no
  second, eager set of methods on `List`. `toList()` is the price.
- No higher-kinded types. `Option`, `Result`, `Task`, `Iterable` share a vocabulary by convention; `traverse`/`sequence`
  are collection targets (`to<Result<List<T>, E>>()`), `filterMap` bridges Option-returning functions into pipelines.
  Option is deliberately not an `Iterable`: its `map` is eager, the trait promises a lazy one.
- No `Collectable`/`FromIterator` trait: a collection target is simply `From<Iterable<T>>`, `to<Target>()` is a typed `into()`.
- Collectors are push-based (`Accumulator.add`), so they are not tied to `Iterable` and work for channels and streams.
- `const` instead of `val` as it is clearer (reading many `val` with `var` in between lets you easily miss some)
- `.trb` instead of `.scr` (`.scr` is an executable screensaver on Windows and blocked by mail filters/AV)
- `//`, nestable `/* */`, `/** */` for docs. `#` stays reserved.
- One naming scheme for primitives (`Int`, `Float`, `Bool`, `String`), no lowercase aliases. Casing is a convention
  (linter), not enforced by the compiler.
- Length numeric names (`Int32`) instead of C-style names (`Short`, `Long`, `Double`): the C# scheme pins `Int` to
  32 bit, but the default integer should be 64 bit in a scripting language.
- The numeric types themselves always carry their width (`Int64`, never a special unsuffixed type). `Int`, `UInt`,
  `Float` are plain prelude aliases, declared with the same `type X = Y` syntax everybody can use. No `Byte`.
- No `alias` keyword: `type X = Y` names an existing type, `type X { }` declares a new one. Not `const X = Y`,
  because types are not compile-time values (see the note under "Type Aliases").
- Types are not compile-time values. Generics stay declarative (`<T: Bound>`), so they can be inferred and checked
  at the declaration.
- `Expression<T>` is part of the language from the start. The quotation carries the static tree _and_ the ordinary value,
  so there is no runtime `compile()` (C#) and no interpreter in binaries because of it. Captures are separate from
  the tree (`captures()`), so the tree is a compile-time constant and quoting is free.
- Types and values are strictly separate, no runtime reflection. Generated `ToData`/`FromData` (a closed data model)
  replace the usual reflection use cases. `FromData` is not generated for types with a private constructor, so
  invariants survive deserialization.
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
- `suggest "lib", because: "reason"` is a labeled argument, there are no infix word chains
- Command calls only in command position, arguments cannot start with `(`, `[`, `-`, `!` (no whitespace sensitivity)
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
- Fields are `const` by default, even in a `var type`
- `type` is an immutable value type by default, `var type` is the explicit opt-in to mutability and identity (was: `const type` / `type`). Same rule as for bindings, fields, parameters and `self`: no `var`, no mutation. `copy` is generated instead of a hand-written `with`.
- Default collections are persistent, mutable ones are named `Mutable*`
- Collection types are traits (`List`, `Map`, ..., `MutableList`, `MutableMap`, ...), implementations are named after
  their data structure (was: concrete native types only, nothing abstract to refer to, `Collection` without a job).
  Every kind exists persistent and mutable, including stack and queue.
- Three traits per kind: `ListView` (any list, read only) with the siblings `List` (value) and `MutableList` (object).
  Not Kotlin's `MutableList : List`, where a `List` can change while you hold it. Here `List<T>` stays a value, the
  same method names work in both halves, and "any list" has its own, honest name.
- `Option`/`Result`/`?` instead of exceptions (also: trivial to implement identically in VM and AOT)
- `with` is the only keyword for trait implementation (`implements` is gone), bounds use `where T: Trait`
- Orphan rule for `extend ... with`
- One member namespace. A method is structurally a constant of the type that holds a receiver closure, `fn` is its
  declaration form. Not a per-instance field: methods cost no memory per instance, cannot be swapped at runtime, and
  value types stay plain data. (Rejected: separate namespaces for fields and methods, Java style.)
- Property commands make the DSL work with one namespace: a command call on a non-callable field writes it
  (`port 8080`) or configures it in place (`database { ... }`). No hand-written setters or section methods.
- Only the innermost receiver is implicit (instead of an annotation like `@DslMarker`)
- `await()` is a postfix method (composes with `?` and chaining)
- No AST macros, no annotations (for now)

## Open Questions

- Name of the language, the CLI and the file extension
- Final keyword list. Keywords cannot be used as names, which already hurts for `with`, `where`, `from`, `to`
  (contextual keywords? allow keywords after `.` and as labels?)
- The "no trailing closures inside command arguments" rule bites in practice (`print numbers.map { ... }`).
  Good enough with a clear error message, or restrict command calls further?
- Are property commands (`port 8080` == `port = 8080`, `database { }` == configure in place) too magical?
  Alternative: require `=` for fields in DSLs and hand-written section methods. The Groovy look depends on them.
- Should named implicit closure parameters (`{ value * 2 }`) stay, or only `_`?
- Mixed-type operators (`Vector2 * Float`): default type parameters on operator traits (`Multiply<Rhs = Self, Output = Self>`)
  or associated types?
- String model details: is `length()` chars or grapheme clusters, how are strings indexed and sliced?
- Value traits: `List<T>` promises to be a value, but nothing stops a `var type` from implementing it. Should a trait
  be able to demand that (only `type`s may implement it)? The same question decides what trait-typed fields mean for
  "a `type` is deeply immutable" and for sharing between tasks.
- Aliasing of mutable objects: a `const` binding and a `var` binding can point to the same `var type` object.
  The `const` view is read-only, not frozen. Is that enough?
- Concurrency: is `async`/`await` (colored functions) acceptable or should tasks be colorless (needs stackful
  coroutines in all back ends)?
- How are capabilities passed to a `Sandbox` (file system roots, environment, clock)?
- Test framework API, doc generator, formatter canon (when does the formatter use command calls?)
- Package registry, lock file format, version resolution, semantics of `optional` and `suggest` dependencies
- FFI for user code (beyond `native` in the standard library)
- `ExpressionNode`: is the node set right (`match` expressions? string interpolation as its own node? `?`/`??`/`?.`)?
  "Open types" (matches need a `_` arm) are a new concept - also useful for error ADTs of libraries?
- `Data` model details: `Decimal`/`Bytes`/date-time cases? Is `Variant` needed or is it a `Record` with a tag field?
  Streaming for large documents?
- Is a compile-time `typeName<T>(): String` (for error messages and logging) acceptable, or already too much?
- REPL semantics (redefinition of `const`, top-level `await`)
