---
title: std/expression
summary: Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
kind: package
status: stable
order: 70
keywords:
  - std/expression
  - Expression
  - ExpressionNode
  - quotation
  - assert
  - nameOf
source:
  - std/expression/src/lib.trb
---

> **Not built natively yet.** A quoted expression (`Expression<Value>`) is not built by the native back end yet, so
> `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the language's.

`std/expression` is quoted expressions: if a parameter or a binding has the type `Expression<Value>`, the compiler
type checks the argument as an ordinary `Value` and then passes it together with its expression tree. `Expression`
values can only be created by the compiler; `ExpressionNode` trees are plain data and can be built, matched and
transformed by anybody. Every name below is already in scope through the prelude.

## Import

```trb fragment
use Expression, ExpressionNode, nameOf, assert from "std/expression"
```

```trb check
fn double(value: Expression<Int>): Int {
  print nameOf(value)
  value.value() * 2
}

const width = 3
print double(width)
```

## Declarations

### Expression

```trb fragment
public native type Expression<Value> {
  tree: ExpressionNode
  source: String
  location: SourceLocation

  fn value(): Value
  fn captures(): List<EncodedValue>
}
```

`tree` is the static data built at compile time, `source` is the source text of the quoted expression
(`"_.age >= minAge"`), and `value()` is the ordinary value - for a function type, the closure itself - evaluated at
most once. `captures()` answers the values of the captured variables, in the order of their `Captured.index`, so a
provider can render `"limit = 5"` next to the tree without walking the caller's scope.

### SourceLocation and TypeReference

```trb fragment
public type SourceLocation with Show {
  file: String
  line: Int
  column: Int
}

public type TypeReference with Show {
  name: String
  arguments: List<TypeReference> = []
}
```

`SourceLocation` shows as `"{file}:{line}:{column}"`. `TypeReference` is a description of a type as data, not
reflection - there is no way back from a `TypeReference` to the type it names.

### ExpressionNode

```trb fragment
public type ExpressionNode {
  case Literal(value: EncodedValue, of: TypeReference)
  case Parameter(index: Int, name: String, of: TypeReference)
  case Captured(index: Int, name: String, of: TypeReference)
  case Field(target: ExpressionNode, name: String, of: TypeReference)
  case Call(target: ExpressionNode?, owner: TypeReference, method: String, arguments: List<ExpressionNode>, of: TypeReference)
  case Construct(arguments: List<ExpressionNode>, of: TypeReference)
  case Unary(operator: UnaryOperator, operand: ExpressionNode, of: TypeReference)
  case Binary(operator: BinaryOperator, left: ExpressionNode, right: ExpressionNode, of: TypeReference)
  case Conditional(condition: ExpressionNode, then: ExpressionNode, otherwise: ExpressionNode, of: TypeReference)
  case Lambda(parameters: List<String>, body: ExpressionNode, of: TypeReference)
  case Items(items: List<ExpressionNode>, of: TypeReference)
  case Interpolation(parts: List<ExpressionNode>)

  fn capturedNodes(): List<ExpressionNode>
}
```

An ordinary algebraic data type: names are already resolved and everything is typed, so an implicit `_`, a named
closure parameter, a receiver and an implicit `self` all show up as explicit `Parameter`, `Field` and `Call` nodes.
`UnaryOperator` (`Negate`, `Not`) and `BinaryOperator` (`Add`, `Equal`, `Less`, `And`, ...) are the two small enums the
`Unary` and `Binary` cases carry. A provider matches on the cases it understands and ends with `_ => Fail(...)`,
because new node kinds arrive with new versions of the language. `capturedNodes()` collects every `Captured` node below
one node, which is what `assert` uses to render each captured value next to its name.

### `nameOf`

```trb fragment
public fn nameOf<Value>(expression: Expression<Value>): String
```

The name of what was written, never its value: `nameOf(user.email)` is `"email"`, `nameOf(limit)` is `"limit"`. For a
type there is the compile-time function `typeName<User>()` instead.

### `assert`

```trb fragment
public fn assert(condition: Expression<Bool>)
```

Panics if `condition` is `false`, with the source text and every captured value in the message:

```text
Assertion failed: adults.length() > limit   (adults = [...], limit = 5)   at main.trb:12:1
```

There are no matchers: `assert` is the one function `std/test` needs, because the expression tree already carries
enough to explain a failure.

## Related

- [Quoted expressions](../language/functions/quoted-expressions.md) - what an `Expression<Value>` parameter hands a
  function, and how it is created.
- [std/test](test.md) - `assert` in a `test`/`group` body.
- [The standard library](index.md) - the other packages.
