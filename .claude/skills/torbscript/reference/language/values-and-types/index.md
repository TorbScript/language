---
title: Values and types
summary: Bindings, the built-in types, and the type forms that are about values rather than about behaviour.
kind: index
status: stable
order: 20
---

Bindings, every built-in type, and the type forms that are about values rather than behaviour: aliases, literal types,
distinct types and arrays.

## What belongs here

What does not belong here: declaring a type of your own, which is in `types/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Built-in types](built-in-types.md)** - Every type a file has without an import - the sized numbers, Bool, Char, String, tuples, lists, maps, ranges, Option, function types, Void and Never.
- **[Bindings](bindings.md)** - A const binding never changes and nothing below it changes; a var binding can be changed in place. That one rule replaces every mutable-and-immutable type pair.
- **[Integers](integers.md)** - Eight sized integer types with fixed ranges on every platform, an unannotated literal is always Int64, and overflow is a compile error when it is written and a panic when it happens at runtime.
- **[Floating-point numbers](floating-point.md)** - On a Float every operator is IEEE-754 and `compare` is a total order that disagrees with them on `nan` and `-0.0`, and neither Float type is Hash.
- **[Decimal](decimal.md)** _(planned)_ - Decimal is designed for exact base-ten arithmetic such as money, and a decimal literal adapts to it the way it adapts to Float - but no back end implements it yet.
- **[Strings](strings.md)** - A String has no length() and no text[i], because "length" and "the i-th character" each have three different answers and two of them are slow.
- **[Tuples](tuples.md)** - A tuple is positional and accessed by .0, .1; a label makes a position easier to read but is not part of the type, so a labelled and an unlabelled tuple of the same shape are the same type.
- **[Ranges](ranges.md)** - The ends a range has are its type - Range, RangeFrom or RangeTo - so nothing is optional and nothing panics; what accepts every form takes the Bounds trait.
- **[Option](option.md)** - Absence is a value, Some(value) or None, and there is no null: a value wraps itself into Some where an Option is expected, and an Option is only ever unwrapped on purpose.
- **[Void and Never](void-and-never.md)** - Void has exactly one value, the keyword literal void, the way true and false are the values of Bool; Never has no value at all and converts to every type, which is why panic fits into any expression.
- **[Literal types](literal-types.md)** - `"tcp" | "udp"` is a type made only of specific values of one base type; only literals combine with `|`, because there are no unions of types.
- **[Checked literals](checked-literals.md)** - A string literal where a Path, a Uri, a UriTemplate, a Regex or a resource type is expected is read by the compiler where it is written, and one that is not valid is a compile error at that line; a template and a pattern are read verbatim.
- **[Type aliases](type-aliases.md)** - `type Name = Other` names an existing type rather than declaring a new one, and the two names are freely interchangeable - there is no separate `alias` keyword.
- **[Distinct types](distinct-types.md)** - A distinct type is an ordinary single-field type, and `by` forwards specific traits to that field so the wrapper costs no boilerplate - there is no separate opaque-alias feature.
- **[Arrays and const parameters](arrays.md)** - Array<Item, const Size> carries its length in the type, a const parameter is a value rather than a type, and there is no arithmetic over one.

<!-- torb:index:end -->

