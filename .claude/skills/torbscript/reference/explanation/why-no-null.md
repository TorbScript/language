---
title: Why there is no null
summary: Absence is Option<Value>, an ordinary case of an ordinary type, so a value is wrapped and unwrapped on purpose and nothing can be dereferenced without checking first.
kind: explanation
status: stable
order: 30
keywords:
  - null
  - nullable
  - option
  - absence
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#error-handling
  - std/core/src/option.trb
---

Every reference in a language with `null` is a promise the type system does not check: `User` might mean a `User`, or
it might mean nothing, and the signature does not say which. This page is the argument for closing that gap with a
type instead of a keyword.

## The decision

**Absence is a value of `Option<Value>`, never the absence of a check.** `Value?` is sugar for `Option<Value>`, whose
two cases are `Some(value)` and `None`.

- There is no `null`, no `nil`, no `undefined`, and no type that is secretly two types at once.
- A value wraps itself into an `Option` where one is expected: a function that answers `Int?` returns an `Int` and
  it becomes `Some`. The way back is never implicit.
- Unwrapping is written, not assumed: `match`, `?.`, `??`, the postfix `?`, or `expect` for the case where absence is
  a bug.

```trb check
fn firstEven(values: List<Int>): Int? {
  values.filter { _ % 2 == 0 }.first()
}

const found = firstEven([1, 3, 5])
print(found ?? -1)
```

## Why

**Because a reference that might be absent is a different type from one that never is, and only one of them should
have a "the value here" API.** Tony Hoare called the null reference his billion-dollar mistake because a `User`
typed as `User` and a `User` typed as `User` are the same type, so every member access is a bet that nothing paid
attention to. `Option<Value>` is a second type: `Value` has every member `Value` has, `Option<Value>` has none of
them, and the only way from one to the other is a check the compiler can see.

**Because the check follows the same vocabulary as everywhere else instead of a new syntax.** `?.` is `Option.map`,
`??` is `Option.orElse`, and the postfix `?` returns `None` from the surrounding function - the same operator that
does the same job on `Result` (see [Why there are no exceptions](why-no-exceptions.md)). A model or a reader who has
learned what `?` does for a `Result` already knows what it does for an `Option`, because [one vocabulary](why-no-higher-kinded-types.md)
covers both instead of `Option` getting its own.

**Because `Option` is a value like any other, so it composes without a special case in the type system.** A
`List<User?>`, a field of type `Manager?`, a function parameter `lazy Value` for the fallback of `??` - none of these
need a nullable annotation on an existing type the way Kotlin's `T?` or TypeScript's `T | null` do, because
`Option<Value>` is an ordinary generic type declared in `std/core`, not a modifier the compiler understands for every
type at once. The price is one word at every call site that can fail; the payoff is that nothing else in the type
system has to know absence exists.

### What was rejected

- **A nullable annotation on every type** (`Value?` as a modifier of `Value` itself, as in Kotlin and TypeScript).
  Rejected because it needs its own subtyping and its own coercions everywhere a type is written, where `Option<Value>`
  needs none: it is a type parameter like any other.
- **An explicit `Some` everywhere.** Until 2026-09-27 a function that returned `Int?` had to write `return Some(value)`;
  `return value` was a diagnostic. The type already says that the value is the success, so the written `Some` said it
  twice, and the language now wraps it (CONCEPT, "Conversions"). What stays refused is the wrap that could hide a
  mistake: an `Option` never wraps into another one, and nothing unwraps on its own.

## Consequences

**A caller has to open the box before using what is inside.** There is no member access that can panic on absence by
surprise: `found.value` does not exist, because `found` has no field until it is matched.

```trb check
fn nickname(user: (name: String, nick: String?)): String {
  match user.nick {
    Some(nick) => nick
    None => user.name
  }
}

print nickname((name: "Ada", nick: None))
```

A value wraps itself where an `Option` is expected, but an `Option` never stands where its value is expected:

```trb error
fn find(values: List<Int>, wanted: Int): Int {
  for value in values {
    if value == wanted {
      return value
    }
  }
  values.first()
}
// error: Expected `Int64`, found `Option<Int64>`
```

**`expect` is where "this should never be absent" is written down.** It takes the message a panic would show, so a
codebase never carries a silent `.unwrap()` that means the same thing without saying it. See [Option](../language/values-and-types/option.md)
for `isSome`, `map`, `flatMap`, `filter` and `okOr`, and [Optional chaining](../language/errors/option-chaining.md) for
`?.` and `??`.

**Reaching for `Option` inside a pipeline uses `filterMap`, not a nested `Option`.** Because `Option` shares `map`'s
name with `Iterate` but is not itself an `Iterate` - its `map` runs immediately, a pipeline's runs when pulled -
a function that can fail is folded into a pipeline with `filterMap`, not with `map` followed by a flatten. See
[No higher-kinded types](../language/generics/no-higher-kinded-types.md).

## Related

- [Option](../language/values-and-types/option.md) - the type, its cases and its members.
- [Optional chaining](../language/errors/option-chaining.md) - `?.` and `??` in full.
- [The question mark operator](../language/errors/question-mark.md) - the same operator on `Result`.
- [Why there are no exceptions](why-no-exceptions.md) - the same argument applied to failure instead of absence.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the implicit-`Some` mistake, with
  the diagnostic.

