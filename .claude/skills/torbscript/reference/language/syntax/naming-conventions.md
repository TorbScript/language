---
title: Naming conventions
summary: A type is UpperCamelCase and everything else is lowerCamelCase, a name is written out rather than abbreviated, and today nothing enforces either rule.
kind: reference
status: stable
order: 6
keywords:
  - UpperCamelCase
  - lowerCamelCase
  - abbreviation
  - convention
source:
  - CONCEPT.md#lexical-structure
---

Two conventions decide how a name in this language looks: which case it is written in, and whether it is
abbreviated. Both are conventions rather than grammar - the compiler accepts a name in any case and of any length -
and this page says exactly what each one asks for.

## Example

```trb check
type Connection {
  var timeout: Int = 30
}

fn absoluteDistance(from: Int, to: Int): Int {
  (from - to).absolute()
}

const html = "<p>ok</p>"
print "{Connection().timeout} {absoluteDistance(from: 3, to: 10)} {html}"
```

## Syntax

```text
UpperCamelCase       a type, a trait, a type parameter, a case
lowerCamelCase        a function, a method, a field, a binding, a module-level constant
```

## Rules

1. **A type, a trait, a type parameter and a case are `UpperCamelCase`.** `Connection`, `Compare`, `Item`, `Circle`.

2. **Everything else is `lowerCamelCase`.** A function, a method, a field, a parameter and a binding: `timeout`,
   `absoluteDistance`, `from`.

3. **A name is written out, not abbreviated.** `Expression`, not `Expr`; `absolute`, not `abs`; `squareRoot`, not
   `sqrt`. This rule holds for `std/`, so an abbreviated name never appears in the standard library.

4. **An abbreviation is allowed where it already is the name people know.** `Html`, `Json`, `Sql`, `Http`, `Int64`,
   `Bool`, `Char`, `min` and `max` are not spelled out further, because spelling them out would produce a longer name
   nobody uses.

   ```trb check
   fn describe(html: String): String {
     "raw: {html}"
   }

   print describe("<p>ok</p>")
   ```

5. **A type parameter is written out like any other name.** `List<Item>`, `Map<Key, Value>`,
   `Result<Value, Failure>` - never `List<T>`, `Map<K, V>`, `Result<V, E>`.

6. **Neither rule is enforced by the compiler today.** A type named `point` and a function named `Distance` both
   compile; the compiler does not read a name's case or its length.

   ```trb check
   type point {
     var x: Int = 0
   }

   fn Distance(value: point): Int {
     value.x
   }

   print Distance(point(x: 5))
   ```

## What this is not

**A name in the wrong case is not a compile error.** The example directly above type checks without a diagnostic,
which is the opposite of what a reader coming from a language with a case-checking compiler expects.

```trb check
type Connection {
  var timeout: Int = 30
}

print Connection().timeout
```

```trb check
type connection {
  var Timeout: Int = 30
}

print connection().Timeout
```

**An abbreviation used elsewhere is not automatically legal here.** `Config`, `Env` and `Msg` are common
abbreviations in other codebases; none of them is one of the names this page's rule 4 lists, so `std/` spells them
`Configuration`, `Environment` and `Message`.

## Related

- [Lexical structure](lexical-structure.md) - what the compiler does enforce about a name: it cannot be a keyword.
- [Declaring a type](../types/declaring-a-type.md) - where a type's name and its cases are written.
- [Angle brackets or comparison](generics-or-comparison.md) - how a written-out type parameter is told apart from a
  comparison.
