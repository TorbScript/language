---
title: Traits
summary: How a capability is declared, how a type comes with one, and how a trait is used as a type.
kind: index
status: stable
order: 40
---

Traits: declaring one, implementing it with `with` or `extend`, supertraits, bounds, intersections, delegation with
`by`, coherence and object safety.

## What belongs here

What does not belong here: type parameters themselves, which are in `generics/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Traits](traits.md)** - A trait is a capability a type comes with. A single-method trait is named after its method, there is no inheritance, and operators are traits.
- **[extend](extend.md)** - extend adds constants and functions to a type after its declaration, with a trait or without one, and never adds a field or a case.
- **[Supertraits](supertraits.md)** - A trait declared with a supertrait requires every implementing type to also implement that supertrait, and a default member can call the supertrait's members directly.
- **[Traits as types](trait-types.md)** - A trait can stand wherever a type can, a value coerces to it automatically, and that coercion is the only subtyping the language has, with no variance for the types built from it.
- **[Trait intersections](intersections.md)** - The & operator combines two or more traits into one type, in a parameter, a field or a bound, and only traits can be combined this way.
- **[Delegation with by](delegation.md)** - by forwards a trait's required members to the one field of a single-field type, binding only to the trait or & group written directly in front of it.
- **[Coherence and blanket implementations](coherence.md)** - A package may implement a trait for a type only if it owns the type or the trait, and two implementations of one trait may never overlap.
- **[Operators are traits](operators.md)** - Every operator except &&, || and ! is a trait method, so writing an operator on your own type means implementing the trait it stands for.
- **[Object safety](object-safety.md)** - A member that mentions Self in a parameter or its result, or that has no self, cannot be called on a trait-typed value, even though the trait stays a legal type.

<!-- torb:index:end -->
