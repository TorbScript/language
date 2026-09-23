---
title: Literal types
summary: "`\"tcp\" | \"udp\"` is a type made only of specific values of one base type; only literals combine with `|`, because there are no unions of types."
kind: reference
status: stable
order: 19
keywords:
  - literal type
  - union
  - TryFrom
source:
  - CONCEPT.md#literal-types
  - examples/tour/src/12-type-system.trb
---

A literal type is a union of literals of one base type - `String`, `Int` or `Char`. It is for "one of these exact
values" in a signature, a configuration field or a wire format, and it is told apart from any other value of the same
base type by generated members, the same way a declared type is.

## Example

```trb check
type Status = "online" | "offline" | "away"

var status: Status = "online"
status = "away"

const text = "online"
status = Status.tryFrom(text) ?? "offline"

const label = match status {
  "online" => "here"
  "offline" => "gone"
  "away" => "back soon"
}

print "{status} {label}"
```

## Syntax

```text
type Name = "a" | "b" | "c"              a literal type of Strings
type Name = 0 | 1 | 3                    a literal type of Ints
fn f(mode: "tcp" | "udp" = "tcp") { ... } inline, in a signature
```

## Rules

1. **A literal type is a union of literals of one base type.** `String`, `Int` and `Char` are the base types this
   works for; the members are told apart by value, not by a type of their own.

2. **A literal has its ordinary type unless a literal type is expected.** `"online"` alone is a `String`; it becomes
   a `Status` only where `Status` is the expected type, the same way an integer literal adapts to `Int8`.

3. **`match` on a literal type is exhaustive without a wildcard `_`.** Every arm names one of the members, and that is
   already every value the type has.

4. **`parse`, `Show`, `Equals`, `Hash`, `Encode` and `Decode` are generated for every literal type**, the same list a
   declared `type` gets. `parse` returns `Result<Self, LiteralParseError>`; going back to the base type is
   interpolation or `.into()`.

5. **Two literal types with the same members are the same type.** The identity is structural, so a literal type has
   no owner of its own the way a declared `type` does.

6. **Only literals combine with `|`.** `Int | String` is not a type: there are no unions of types in this language,
   because nothing at runtime distinguishes them. A type with cases is the tool once behavior or non-literal data is
   needed.

   ```trb error
   type Width = Int | String
   // error: Only literals can be combined with `|` (`"online" | "offline"`). There are no unions of types: declare a type with cases
   ```

## What this is not

**A literal type is not extendable.** `extend` is rejected at its own line, before the member it would have added is
ever looked up.

```trb check
type Status = "online" | "offline" | "away"

const status: Status = "online"
print status.show()
```

```trb error
type Status = "online" | "offline" | "away"

extend Status {
  fn shout(): String {
    "status"
  }
}
// error: `"away" | "offline" | "online"` cannot be extended
```

**A literal type is not a subtype of a smaller literal type, or the other way around.** `"online" | "offline"` and
`"online" | "offline" | "away"` are two different types with no assignability between them; passing one where the
other is expected needs an explicit `match` or a fresh literal.

## Related

- [Type aliases](type-aliases.md) - `type Name = Other`, the syntax a literal type reuses.
- [Distinct types](distinct-types.md) - a type for one case with data, when a literal type stops being enough.
- [Bindings](bindings.md) - how a literal adapts to an expected type in general.

