---
title: Syntax cheat sheet
summary: "Every form of the language in one place: declarations, expressions, patterns, types and the call rules, with the exact spelling of each."
kind: reference
status: stable
order: 10
skill: cheat-sheet
keywords:
  - cheat sheet
  - syntax
  - forms
  - quick reference
source:
  - CONCEPT.md#lexical-structure
  - CONCEPT.md#formatter-canon
  - examples/tour/src
---

Every form the language has, with its exact spelling. Each line is the canonical form: it is what `torb canon` writes and
what the compiler accepts.

## Example

```trb
use File from "std/fs"

type Config {
  var host: String = "localhost"
  var port: Int = 8080
}

type ConfigError {
  case Missing(path: String)
  case Malformed(line: Int)
}

fn load(path: String): Result<Config, ConfigError> {
  if !File.exists(path) {
    return Fail ConfigError.Missing(path)
  }
  Ok Config()
}

match load("project.trb") {
  Ok(config) => print "{config.host}:{config.port}"
  Fail(problem) => printError "{problem}"
}
```

## Syntax

### Declarations

```text
const name = value                          an immutable binding
var name: Type = value                      a mutable binding with an annotation
fn name(a: Int, b: Int = 1): Int { ... }     a function; a default is evaluated at every call
fn name(): Int { ... }                       a method, inside a type body; it does not list `self`
var fn name() { ... }                        a method that changes its receiver in place
static fn name(): Self { ... }               a member of the type itself, reached as `Type.name()`
static name = value                          a constant of the type
fn name(var target: Counter) { ... }         a parameter the function may change
fn name(...rest: Int): Int { ... }           a variadic parameter; `rest` is a `List<Int>`
type Name { ... }                            a type
type Name = Other                            an alias for an existing type
type Name with Trait by field { ... }        the trait's required members are forwarded to field
type Name with (Trait, Trait) by field { ... }  several traits delegated to the same field
shared type Name { ... }                     a type with an identity instead of a value
trait Name { ... }                           a capability
extend Name with Trait { ... }               an implementation written afterwards
extend Name { ... }                          members added to a type
public fn name() { ... }                     exported from its file
private var field: Int = 0                   invisible outside its type
private(var) field: Int = 0                  read by everyone, written by the type only
use Name from "std/core"                     an import
use Type.Case from "./module"                a case, by its path
use Type.member from "acme/text"             a member another package's `extend` adds
use * as console from "std/console"          a namespace import
use Name as Other from "./module"            an import under a local name
public use Name from "./module"              a re-export
```

### Expressions

```text
f a, b                                       a command call: the canonical form
f(a, b)                                      needed when a rule of the canon says so
f()                                          no arguments: always parentheses
f(a) { x => x }                              a trailing closure
f a { x => x }                               a command call with a trailing closure
f(label: value)                              a labelled argument; labels follow the positional ones
f(...values)                                 a spread of any `Iterate`
{ x: Int => x * 2 }                          a closure with a typed parameter
{ _ * 2 }                                    a closure with implicit parameters `_`, `_2`, `_3`
Type.Case(field)                             a case, written out
.Case(field)                                 a case where the expected type is known
Case(field)                                  a case that the file imports
value.member                                 a method reference, bound to `value`
Type.member                                  the member itself: `(self: Type) => ...`
value?                                       unwrap an `Ok` or a `Some`, or return early
value ?? fallback                            `OrElse.orElse`: the value, or a lazy fallback
a ** b                                       `Power.power`: tighter than `*`, to the right; `-x ** 2` is an error
a & b   a | b   a ^ b   ~a                   the bit operators of `Bits`: `&` binds like `*`, `|` and `^` like `+`
a << n   a >> n                              shifts, between `*` and `**`; arithmetic on a signed type
value?.member                                `Option.map`, or `flatMap` for an optional member
value.into()                                 a conversion chosen by the expected type
value.to<Target>()                           a conversion with the target written out
do { ... }                                   a block evaluated immediately
if condition { a } else { b }                an expression
match subject { pattern => value }           an expression, and exhaustive
"text {expression}"                          interpolation; a literal brace is `\{`
raw"text"                                    a raw string: no escapes, no interpolation
const page: Uri = "https://example.test/"     a literal the compiler reads: `Path`, `Uri`, `Regex`, ...
const digits: Regex = "\d{2,4}"               a `Regex` or `UriTemplate` literal is read verbatim
"""                                          a multi-line string, dedented by its first line
  text
  """
```

### Patterns

```text
_                                            the wildcard
name                                         binds, because it starts lowercase; a refutable arm has to read it
_name                                        binds and keeps the name; nothing has to read it
Case(field)                                  an imported case; never binds
.Case(field)                                 a case of the type being matched
Type.Case(field)                             a case written out
Type(field, other)                           a type read backwards, field by field
Type(label: value)                           a labeled sub-pattern; the labeled ones follow the positional ones
Type(field, ...)                             `...` stands for every field the pattern does not name
(a, b)                                       a tuple
[first, ...rest]                             a list, with a rest
[]                                           the empty list
1 | 2 | 3                                    alternatives
4..=9                                        a range
n if n < 0                                   a binding with a guard
```

### Types

```text
Int Int8 Int16 Int32 Int64                   signed; `Int` is an alias for `Int64`
UInt UInt8 UInt16 UInt32 UInt64              unsigned
Float Float32 Float64                        `Float` is an alias for `Float64`
Bool Char String Void Never                  the rest of the built-ins
Value?                                       `Option<Value>`
List<Item> Map<Key, Value> Set<Item>         collection traits
Array<Item, 16>                              a fixed size in the type
Range<Int>                                   `0..10`, `0..=10`, `0..`, `..10`
(Int, String)                                a tuple
(lowest: Int, highest: Int)                  a labelled tuple; a label is not part of the type
(value: Int) => Int                          a function type
(var self: Config) => Void                   a receiver closure
lazy Value                                   evaluated at most once, on first use
Expression<Bool>                             the value and its syntax tree
Show & Encode                                an intersection of traits
"tcp" | "udp"                                a union of literals of one base type
```

### Statements

```text
return value                                 an early return
for item in items { ... }                     over anything `Iterate`
for (key, value) in table { ... }             destructuring in a loop
while condition { ... }                      `break` and `continue` work
loop { ... }                                 endless: `Never` without a `break`, `Void` with one
if const Some(user) = find(id) { ... }        a pattern in a condition
if var Some(cursor) = current { ... }         binds into the place, not into a copy
place = value                                 a statement, never an expression
panic "message"                               aborts with exit code 101
```

## Rules

1. **A statement ends at the end of its line.** There are no semicolons, and two statements never share a line.
2. **A call is a command wherever the grammar allows it**: in command position, with a path as the callee, at least one
   argument whose first token is not `(`, `[`, `-`, `!` or `.`, no operator at the top level of an argument, and all
   arguments on one line. Parentheses everywhere else.
3. **A case is never bare unless the file imports it.** `Some`, `None`, `Ok` and `Fail` are bare because the prelude
   imports them.
4. **In a pattern, a lowercase name binds and an uppercase name never does.** An uppercase binding is a compile error.
5. **A binding of a refutable pattern has to be read.** In an arm of a `match`, an `if const`/`if var` and a
   `while const`, a binding the guard and the body never read is an error: write `_`, or `_name` to keep the name.
6. **A name is ASCII, and its first letter is a rule.** `[A-Za-z_][A-Za-z0-9_]*`; `A`-`Z` starts a type, a trait, a case,
   a type parameter and a type alias, and everything else starts lowercase. There is no `MAX_SIZE`.
7. **`const` is deep.** Through a `const` binding nothing changes.
8. **A field is `const` unless marked `var`; a member is public unless marked `private`; a top-level declaration is
   private to its file unless marked `public`.**
9. **A `public` function and a trait method never infer their result type.** Without one they answer `Void`.
10. **`{` in expression position is always a closure**, never a block. `do { ... }` evaluates a block immediately.
11. **A `match` is exhaustive, and an unreachable arm is an error.**
12. **An expression statement has to be `Void` or `Never`**, unless the call has a `var` receiver or a `var` argument.
13. **Mutation needs a `var` path from the binding down**: a `var` binding, `var` parameter or a `var fn` receiver, then `var`
    fields.
14. **A `var` that is changed and never read afterwards is a compile error**, and so is the discarded result of a method
    that reads its receiver.
15. **An endless loop is `loop`.** `loop { ... }` has the type `Never` without a `break` that targets it and `Void` with
    one, a `break` carries no value, and `while true` is an error that names `loop`.
16. **A `use` names what it imports, and a member of a foreign `extend` is one of those names.**
    `use String.shout from "acme/text"` is the same form as a case import; what the type's own package attaches to it
    needs no import, and what a trait puts on a foreign type needs the trait.

## What this is not

This page is every form that is right. The forms that look right and are not - `let`, a semicolon, a bare case,
`Err`, `text.length`, `a & b`, `x as Int`, `MAX_SIZE`, `while true`, `impl Trait for Type` and the rest - are one list,
each with the diagnostic it produces:
[What a model trained on other languages gets wrong](../../explanation/mistakes-models-make.md).

## Related

- [Command calls](command-calls.md) - the call rules in full, with the canon.
- [Bindings](../values-and-types/bindings.md) - `const`, `var` and the copy trap.
- [Cases and match](../pattern-matching/cases-and-match.md) - how a case is spelled and matched.
- [Result](../errors/result.md) - `Ok`, `Fail` and `?`.
- [Traits](../traits/traits.md) - `with`, `extend` and the trait names.
- [use](../modules-and-packages/use.md) - every form of an import, the path of a member included.
- [Loops](../execution/loops.md) - `for`, `while` and `loop`, and what each one produces.
- [What a model trained on other languages gets wrong](../../explanation/mistakes-models-make.md) - the forms that
  look right and are not, with their diagnostics.
