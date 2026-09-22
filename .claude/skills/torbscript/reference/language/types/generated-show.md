---
title: The generated Show
summary: Show is generated for every type without being written, and its text is fixed so that two implementations of the language print the same thing for the same value.
kind: reference
status: stable
order: 50
keywords:
  - Show
  - print
  - interpolation
  - format
source:
  - CONCEPT.md#values
  - std/core/src/convert.trb
---

`Show` turns a value into the `String` a reader sees: what `print` writes, and what `"{value}"` puts into an
interpolation. Every `type` gets one for free, in a format that is part of the language rather than of one
implementation, so the same value prints the same way whichever back end produced it.

## Example

```trb
type Card {
  label: String
  initial: Char
}

const card = Card label: "Ada", initial: 'A'
print card
print "{card}"
print([card, card])
```

## Syntax

```text
value.show(): String
"{value}"          // Uses show(), or showNested() where value sits inside another value's Show
```

## Rules

1. **A `type` shows as `Type(field: value, ...)`, every field in declaration order.** `Card(label: "Ada", initial: 'A')`
   - the same text `copy` and the constructor would take back.

2. **Every field is in it, `private` ones included, wherever the value is printed.** `Show` is the debug form of a
   value and `Equals` and `Hash` read the same fields, so hiding one here would make a printed value disagree with
   what the type is. A type whose field is a secret writes its own `Show` instead of relying on the generated one.

   ```trb
   type Email {
     private value: String
   }

   print Email("info@example.test")      // Email(value: "info@example.test")
   ```

3. **A case shows as `Case(field: value, ...)`, or its bare name when it has none.**

   ```trb
   type Shape {
     case Circle(radius: Float)
     case Empty
   }

   print Shape.Circle(2.0)      // Circle(radius: 2.0)
   print Shape.Empty            // Empty
   ```

4. **A collection has its own bracket, not the type's.** A `List` is `[a, b]`, a `Map` is `["k": v]` and `[:]` when it
   is empty. Both come from the collection's own `Show`, generated from the same fixed shapes so a value inside one
   nests correctly.

   ```trb
   print([1, 2, 3])
   print(["a": 1, "b": 2])
   ```

5. **Inside another value's `Show`, only `String` and `Char` differ from their own `show()`.** A `String` is quoted
   with escapes and a `Char` is in single quotes, so `["a", "b"]` stays readable while `"{name}"` is still the text
   itself with nothing added around it.

   ```trb
   print "Ada"                // Ada
   print(["Ada", "Grace"])    // ["Ada", "Grace"]
   ```

6. **`Option` shows as `Some(x)` or `None`.** These are the same names the value is matched with, so a printed value
   and a pattern for it read the same way.

7. **A `Float` always carries a decimal point or an exponent.** `1.0` shows as `1.0`, never `1`, so a reader can tell a
   `Float` from an `Int` on sight. `-0.0` keeps its sign, and a division by zero shows as `inf`, `-inf` or `nan`.

## What this is not

**`Show` is not a proxy for identity.** Two values print identically exactly when their fields do, which is a
statement about content - it says nothing about whether they are the same object, and `isSame` is a different
question with a different answer for two values that print the same.

```trb
type Card {
  label: String
  initial: Char
}

const a = Card label: "Ada", initial: 'A'
const b = Card label: "Ada", initial: 'A'
print a
print b
print(a == b)
```

```trb error
type Card {
  label: String
  initial: Char
}

const a = Card label: "Ada", initial: 'A'
const b = Card label: "Ada", initial: 'A'
print isSame(a, b)
// error: `isSame` compares identity, and a `Card` is a value
```

A `Show` is generated even where `Equals` and `Hash` are not: a function value has a `Show` of its own (a function
prints as `<function>`), so a function-typed field keeps a type from getting `Equals`, `Hash` and `copy` without
touching its `Show` - [copy and equality](copy-and-equality.md) shows the `Equals` side of that.

## Related

- [Declaring a type](declaring-a-type.md) - where `Show` is introduced alongside `Equals`, `Hash` and `copy`.
- [Copy and equality](copy-and-equality.md) - the other members that are generated together with `Show`.
- [Cases and match](../pattern-matching/cases-and-match.md) - the case syntax `Show` mirrors.

