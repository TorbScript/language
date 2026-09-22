---
title: Generics
summary: Type parameters, where they are declared, how a bound restricts them, what is inferred, and how a trait-typed value satisfies one at runtime.
kind: index
status: stable
order: 50
---

Generics: declaring a type parameter, writing a bound, what the compiler infers without help, why there are no
higher-kinded types, and the witness table that makes a generic member work on a trait-typed value.

## What belongs here

What does not belong here: traits themselves, which are in `traits/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Type parameters](type-parameters.md)** - A type parameter is declared in angle brackets after the name of a fn, type, trait or extend, and its name is written out like a type, never a single letter.
- **[Bounds](bounds.md)** - A bound restricts a type parameter to types that implement one or more traits, written inline or after where, and a member can carry a bound of its own that is not a requirement on every implementor.
- **[Inference](inference.md)** - A type argument is inferred from a call's arguments or its expected type, a closure's parameter types follow the same rule, and a fn's own parameter types and a public fn's result are always written out.
- **[No higher-kinded types](no-higher-kinded-types.md)** - Option, Result, Iterate and Task share method names with the same meaning as a convention of the standard library, not as a shared trait, because the language has no way to be generic over a type constructor.
- **[Witness tables](witnesses.md)** - A trait-typed value carries a witness table per trait it is known through, so a generic bound is satisfied by any trait the value's own traits require, even without knowing its concrete type.

<!-- torb:index:end -->

