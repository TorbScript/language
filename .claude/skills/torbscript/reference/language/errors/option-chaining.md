---
title: Optional chaining
summary: "`?.` is Option.map, or Option.flatMap when the member itself answers an Option, so chaining never nests. `??` is the trait OrElse, which gives a lazy fallback for an absent Option, a failed Result or any type that comes with it."
kind: reference
status: stable
order: 20
keywords:
  - optional chaining
  - fallback
  - orElse
source:
  - CONCEPT.md#error-handling
  - examples/tour/src/06-errors.trb
---

`?.` and `??` exist so that reading through several `Option`s in a row does not need a `match` at every step. Neither
is a new kind of value: both are ordinary method calls. `??` is the one of the two that is a **trait**, because which
method it calls never depends on anything but the receiver.

## Example

```trb check
type User {
  name: String
  manager: User? = None
}

fn findUser(id: Int): User? {
  if id == 1 { Some User("Ada") } else { None }
}

const managerName: String? = findUser(2)?.manager?.name
const name = findUser(1)?.name ?? "anonymous"
print "{managerName} {name}"
```

## Syntax

```text
<option-expression>?.<member>          Option.map, or Option.flatMap if <member> itself answers an Option
<expression> ?? <fallback>             OrElse.orElse: the value, or <fallback> (evaluated only if it is needed)

trait OrElse<Value> { fn orElse(fallback: lazy Value): Value }
```

## Rules

1. **`?.` is `Option.map`.** `findUser(1)?.name` is `findUser(1).map { _.name }`: `None` stays `None`, `Some(user)`
   becomes `Some(user.name)`.

2. **`?.` becomes `Option.flatMap` when the member itself answers an `Option`, so chaining never produces a nested
   one.** `findUser(2)?.manager?.name` is a plain `String?`, never a `String??`, whatever `manager` and `name` each
   answer.

3. **`?.` is not defined on `Result`.** A `Result` needs `ok()` first, because mapping over its `Ok` side while
   discarding `Fail` would throw away the reason for the failure - which `Option.map` on an `Option` never had to
   begin with.

   ```trb error
   fn parse(text: String): Result<Int, String> {
     Ok text.byteLength()
   }

   const length = parse("hi")?.isOk()
   print length
   // error: `?.` needs an `Option`, and `Result<Int64, String>` is not one
   ```

4. **`??` is `OrElse.orElse`, and `Option` and `Result` both come with it.** `option ?? fallback` and
   `result ?? fallback` both answer the `Value`, never the `Option` or the `Result` itself. It is the trait that decides,
   not the two types: a type of your own that comes `with OrElse<Value>` gets `??`, and one that does not hears so.

   ```trb error
   const value = 1 ?? 0
   print value
   // error: `Int64` does not implement `OrElse`, so `a ?? b` has no meaning for it
   ```

5. **The right side of `??` is `lazy`: it is evaluated only when the left side is absent or failed.** A fallback that
   is expensive to compute, or that has a side effect, only runs when it is actually needed.

   ```trb check
   fn parseOr(text: String, fallback: Int): Int {
     Int.tryFrom(text) ?? fallback
   }

   print parseOr("12", 0)
   print parseOr("nonsense", 0)
   ```

6. **The right side of `??` is checked against the `Value`, not against the whole `Option` or `Result`.** `Int.tryFrom(text)
   ?? 0` needs a plain `Int` on the right, not a `Some(0)` or an `Ok(0)`. The `Value` is the argument of the receiver's
   `OrElse<Value>`.

7. **`?.` is a trait for nobody, and that is deliberate.** An operator is a trait exactly when it is a method call, and
   `?.` is two: `map` when the member answers a plain value, `flatMap` when it answers an `Option`. A trait that covered
   both would have to name `Self<Output>` - the higher-kinded form this language does not have - so `?.` stays defined on
   `Option` alone. `?` is the same question with a different answer: it leaves the *enclosing function*, and no method can
   do that. Both can be opened later without breaking anything.

## What this is not

**`?.` is not a null-conditional operator that stops the statement.** In C# and TypeScript, `a?.b?.c` short-circuits
the entire expression to `undefined`/`null` the moment any link is absent, but the statement around it keeps running
with that value. Here `?.` produces an ordinary `Option` value, nothing more:

```trb check
type User {
  name: String
  manager: User? = None
}

fn managerNameOf(user: User): String? {
  user.manager?.name
}

print managerNameOf(User("Ada"))
```

```trb error
type User {
  name: String
  manager: User? = None
}

fn managerNameOf(user: User): String {
  user.manager?.name
}
print managerNameOf(User("Ada"))
// error: Expected `String`, found `Option<String>`
```

The second version tries to hand a `String?` to a function that promises a plain `String`; there is no implicit
"or crash" the way `!` in Swift or Kotlin would provide. Write `?? "default"`, or change the result type to `String?`.

**`??` is not the same as `||` on a `Bool`, and it is not a truthiness check.** The left side has to be an `Option` or
a `Result`; a `Bool`, an empty `String` or a zero `Int` is never "falsy" here, so `??` never silently activates for a
value that only looks empty.

## Related

- [Result](result.md) - `?`, and why `??` is defined the same way there.
- [Option](../values-and-types/option.md) - `map`, `flatMap` and `orElse` themselves.
- [The question mark operator](question-mark.md) - the other way to leave early instead of falling back.
- [Operators are traits](../traits/operators.md) - which operator is which trait, and the three that are none.

