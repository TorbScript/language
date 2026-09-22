---
title: Why there is no reflection
summary: Types never flow as values, so nothing can inspect a type at runtime, and what reflection is reached for - serialization, config mapping, debug output - is covered by one generated trait pair, Encode and Decode, instead.
kind: explanation
status: stable
order: 120
keywords:
  - reflection
  - typeof
  - Encode
  - Decode
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#types-values-and-reflection
---

> **Not built natively yet.** The generated `encode` is not built by the native back end yet, so `torb run` refuses the
> examples here that use it. `torb check` accepts them, and the rules are the language's.

Reflection promises to answer "what is this, really?" at runtime, for any value, generically. TorbScript never lets
that question be asked in the first place, and this page argues for closing the door instead of guarding it.

## The decision

**Types and values are strictly separate worlds, and there is no runtime reflection.** A type never flows as a value:
there is no `Type` type, no `typeof`, no `value is Value` over a generic `Value`, no `Class.forName`.

- The only bridges between a type and a value are syntactic and resolved at compile time: a constructor call
  (`Point(1, 2)`), a static member (`Point.origin`), a case (`Shape.Circle`), a method reference (`Point.area`).
- What reflection is otherwise needed for - serialization, config mapping, database rows, debug output - is covered by
  one generated trait pair, `Encode` and `Decode`, the same way `Equals`, `Hash` and `Show` are generated.

```trb check
type User {
  name: String
  age: Int
}

const user = User name: "Ada", age: 36
print user
print Json.encode(user)
```

## Why

**Because the same reflective operation would mean something different in every back end, and the language promises
identical behavior everywhere.** One of the language's design principles rules out anything that cannot be
implemented identically in the interpreter and in a compiled binary, see [CONCEPT.md](../../CONCEPT.md). Monomorphized generics in a native
binary and boxed generics in an interpreter would make a generic type's own reflected shape observably different -
whether `List<Int>` and `List<String>` "are the same generic type" at runtime is exactly the kind of fact an
implementation choice would leak into a program's behavior.

**Because keeping every type's metadata alive costs a compiled binary something a script does not need to pay.**
Reflection needs the compiler to keep names, field layouts and type identities around after compilation, in every
binary, for types that might never be inspected. `Encode`/`Decode` generate code only where a type actually asks for
it (`with Encode` is written on the type, or the compiler derives it because every field supports it), so nothing is
kept alive "just in case" the way a reflection table would have to be.

**Because a type describing itself to a format, once, replaces N × M implementations with N + M.** Reflection-based
serializers walk a runtime type description at the call site; `Encode`/`Decode` walk the value instead, so a
type writes what it consists of exactly once and any format - JSON, MessagePack, a database row - implements
`Encoder`/`Decoder` exactly once, without ever seeing the concrete type. See
[Encode and Decode](../language/reflection/encode-and-decode.md) for the four shapes this reduces every value to.

**Because the meta-programming style reflection invites is exactly the one this language does not want.** Reflection
lets a library branch on "what kind of thing is this at runtime," which is a second, informal type system layered on
top of the checked one - the same instinct [Why there are no macros](why-no-macros.md) rejects for syntax rejects it
here for values: a decision that can be made by the type checker should be made by the type checker, not deferred to
code that runs after it.

### What was rejected

- **General runtime reflection** (`typeof`, `Class.forName`, dynamic member lookup by name). Rejected because it could
  not be implemented identically in an interpreter and a compiled back end without either boxing every value or
  keeping full metadata alive in every binary.
- **A `Data`/`ToData`/`FromData` tree** that every type would convert to and from, as an earlier design tried. Rejected
  because it built a second, dynamically typed vocabulary (`Boolean`/`Integer`/`Text`/`Sequence`) parallel to the one
  the language already has, lost precision converting into it, and built the whole document twice for every value.
  `Encode`/`Decode` write directly to and from a format's own `Encoder`/`Decoder`, with no tree in between.
- **A pair of traits per format** (`JsonEncode`, `TomlEncode`, ...). Rejected as N × M instead of N + M, and because a
  new format could never be added for a type it does not own - the coherence rule would block it. One generic
  `Encode`/`Decode` pair per type, implemented against any format's `Encoder`/`Decoder`, avoids both problems.

## Consequences

**A type describes itself once, and every format gets it for free.** `Encode` is generated for every `type` whose
fields are all `Encode`; a hand-written `encode`/`decode` pair looks exactly like the generated one, because nothing
about the mechanism is special:

```trb check
type Email with TryFrom<String, String> {
  private value: String

  static fn tryFrom(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }
}

extend Email with Encode, Decode {
  fn encode(var encoder: Encoder) {
    encoder.string value
  }

  static fn decode(var decoder: Decoder): Result<Email, DecodeError> {
    const text = decoder.string()?
    Email.tryFrom(text).mapError { message => DecodeError message }
  }
}
```

**`Decode` is not generated for a type with a private constructor.** A type whose invariants are checked in a factory
- `Email.tryFrom` above - has to write `decode` by hand, so a decoded value cannot skip the check the constructor never
lets a caller skip either. See [Encode and Decode](../language/reflection/encode-and-decode.md).

**A format with a document model of its own - XML, HTML - gets its own traits in addition, because attributes,
namespaces and element order do not fit into "values, sequences, maps, records."** `Encode`/`Decode` stay the general
case; a format whose shape does not match it is not squeezed into naming tricks.

**There is no "any value" type to fall back to.** Something that wants to look at a document without knowing its type
uses a library type, such as `JsonValue`, which is an ordinary ADT and not a dynamically typed island in the language.

## Related

- [There is no reflection](../language/reflection/no-reflection.md) - the four syntactic bridges, in full.
- [Encode and Decode](../language/reflection/encode-and-decode.md) - the generated pair, and what a hand-written
  implementation looks like.
- [Why there are no macros](why-no-macros.md) - the same argument against code that runs ahead of the type checker,
  applied to syntax instead of values.
- [std/encoding](../standard-library/encoding.md) - `Encoder`, `Decoder` and the formats built on them.
