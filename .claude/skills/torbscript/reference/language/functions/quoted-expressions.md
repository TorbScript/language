---
title: Quoted expressions
summary: A parameter or binding typed Expression<Value> gets the ordinary value plus the typed tree of what was written, its source text and the values it captured, which is what assert and a query provider read instead of running the code twice.
kind: reference
status: stable
order: 80
keywords:
  - Expression
  - quotation
  - ExpressionNode
  - assert
source:
  - CONCEPT.md#quoted-expressions-expressionvalue
  - std/expression/src/lib.trb
---

> **Not built natively yet.** A quoted expression (`Expression<Value>`) is not built by the native back end yet, so
> `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the language's.

A parameter or a binding typed `Expression<Value>` checks its argument as an ordinary `Value` and additionally hands
over the typed tree of what was written, its exact source text, and the values any captured variable held. `assert`
is the one function of the prelude area that every program already uses this way.

## Example

```trb check
use assert from "std/expression"

const limit = 5
const numbers = [1, 2, 3]
assert(numbers.length() < limit)
```

## Syntax

```text
<parameter>: Expression<<Type>>
const <name>: Expression<<Type>> = <expression>
```

```text
// std/expression
native type Expression<Value> {
  tree: ExpressionNode
  source: String
  location: SourceLocation

  native fn value(): Value
  native fn captures(): List<EncodedValue>
}
```

## Rules

1. **The call site of an `Expression<Type>` parameter looks exactly like a call to `Type` itself.** Nothing marks the
   argument; `assert(condition: Expression<Bool>)` is called as `assert(numbers.length() < limit)`, the same as if the
   parameter were a plain `Bool`.

2. **`value()` answers the ordinary value, evaluated at most once**, and for a quoted function type it answers the
   closure rather than calling it.

3. **`source` is the exact text that was written, and `tree` is the same thing as data**, an `ExpressionNode` built
   from literals, parameters, captured variables, field access, calls, constructors, operators, `if`/`else`, nested
   closures, list literals and string interpolation. A tuple, a map literal and a range are constructions and are
   quotable too; `?.` and `??` have no node of their own, because they are the calls on `Option` they mean.

4. **Quoting happens after name resolution and type checking.** The tree only ever contains resolved names: an
   implicit `_`, a named closure parameter, a receiver and an implicit `self` are already the explicit `Parameter`,
   `Field` or `Call` node they resolved to, with a type on every node.

5. **A quoted closure is exactly one expression**, because a quotation as a whole is one. A statement inside it -
   `const`, `var`, an assignment, a loop, `return`, or the early-return `?` - is a compile error, and so is `match`.

   ```trb check
   fn describePredicate(predicate: Expression<(Int) => Bool>): String {
     predicate.source
   }

   const minAge = 18
   print describePredicate({ age => age >= minAge })
   ```

   ```trb error
   fn describePredicate(predicate: Expression<(Int) => Bool>): String {
     predicate.source
   }

   const minAge = 18
   print describePredicate({ age =>
     const isAdult = age >= minAge
     isAdult
   })
   // error: A `const` cannot appear in a quotation: `Expression<(Int64) => Bool>` holds a single expression
   ```

   ```trb error
   fn describeCondition(condition: Expression<Bool>): String {
     condition.source
   }

   const value: Int? = Some(1)
   print describeCondition(match value {
     Some(_) => true
     None => false
   })
   // error: `match` cannot be quoted yet
   ```

6. **A captured variable has to implement `Encode`.** A provider has to be able to look at what a quotation captured
   (a SQL driver binds it as a query parameter), so capturing anything else is a compile error at the capture, naming
   the parameter that required it.

   ```trb error
   use assert from "std/expression"

   shared type Session {
     name: String
   }

   fn check(session: Session) {
     assert(session.name == "a")
   }
   // error: `session` is captured here and `Session` is not `Encode`
   ```

7. **The tree costs nothing when there is nothing captured, and one small allocation per evaluation when there is.**
   `assert` is in every test, which is why a quotation without captures is free and one with captures is not free but
   still cheap.

8. **The tree cannot be executed, only the value can.** There is no `compile()` step: an in-memory caller reads
   `value()`, a provider such as a database driver reads `tree` and translates what it understands, and fails its own
   call at its own runtime for a node it does not - the language cannot know in advance what a library can translate.

## What this is not

**`Expression<Value>` is not reflection.** `TypeReference` on a node is a name and its type arguments, and there is no
way from it back to the type itself; the tree can be matched, built and passed around like any other value, but it
cannot look anything up that was not already written at the call site.

```trb check
use nameOf from "std/expression"

type User {
  email: String
}

const user = User "ada@example.test"
print nameOf(user.email)
```

```trb error
use nameOf from "std/expression"

type User {
  email: String
}

const user = User "ada@example.test"
print nameOf(user.unknown)
// error: `User` has no member `unknown`
```

`nameOf` reads the name that was already resolved on the tree; it cannot read a name that does not exist, because the
argument is type checked as an ordinary expression before it is ever quoted.

**A quotation is not a macro.** It runs no code of its own during compilation and it cannot change what a program
means; it is ordinary data available after the fact, built from a call that has already been fully checked.

## Related

- [Parameter modes](parameter-modes.md) - `Expression<Type>` next to `var`, `lazy` and receiver closures.
- [Trailing closures](trailing-closures.md) - the closure form a quoted function type also takes.
- [Cases and match](../pattern-matching/cases-and-match.md) - why `match` needs a node set of its own before it can be quoted.

