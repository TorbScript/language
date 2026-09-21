---
title: Naming
summary: A name is written in ASCII letters, digits and `_`, a type starts with an uppercase letter and everything else with a lowercase one, and the compiler reports both at the declaration.
kind: reference
status: stable
order: 6
keywords:
  - UpperCamelCase
  - lowerCamelCase
  - ASCII
  - MACRO_CASE
  - abbreviation
source:
  - CONCEPT.md#lexical-structure
---

Three things decide how a name in this language looks: which characters it may contain, which case its first letter
is, and whether it is abbreviated. The first two are rules the compiler enforces - the first letter of a name in a
pattern already decides whether the pattern binds or names a case, so it has to mean the same thing at every
declaration. Only the third is a convention.

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
[A-Za-z_][A-Za-z0-9_]*   every name: a letter or `_`, then letters, digits and `_`
UpperCamelCase           a type, a trait, a case, a type parameter, a type alias
lowerCamelCase           a function, a method, a field, a parameter, a binding, a module constant
```

## Rules

1. **A name is written in ASCII letters, digits and `_`.** The lexer reads the whole word and reports one problem for
   the name, whatever the letters in it are.

   ```trb error
   const größe = 1
   // error: A name is written in ASCII letters, digits and `_`
   ```

2. **Text is not.** A string, a character literal, a comment and a doc comment may contain anything Unicode has.

   ```trb check
   /** Grüßt zurück. 👋 */
   const greeting = "Grüße 👋"

   print greeting
   ```

3. **A type, a trait, a case, a type parameter and a type alias start with an uppercase letter.** "Uppercase" is `A`
   to `Z` and nothing else, because a name is ASCII. `Connection`, `Compare`, `Circle`, `Item`, `Bytes`.

   ```trb error
   type point {
     var x: Int = 0
   }
   // error: A type starts with an uppercase letter: write `Point`
   ```

4. **Everything else starts with a lowercase letter or `_`.** A function, a method, a `static fn`, a field, a
   parameter, a tuple label, a `const`, a `var`, a module constant, a closure parameter.

   ```trb error
   fn Distance(value: Int): Int {
     value
   }
   // error: A function starts with a lowercase letter: write `distance`
   ```

5. **There is no `MACRO_CASE`.** A module constant is spelled like any other name, and the message says so instead of
   only naming the first letter.

   ```trb error
   const MAX_SIZE = 1024
   // error: A constant starts with a lowercase letter: TorbScript has no `MAX_SIZE` spelling, write `maxSize`
   ```

6. **An `as` alias is spelled like the name it renames.** `use Option.Some as Present` renames a case, so the alias
   starts uppercase; `use area as surface` renames a function, so it starts lowercase. A module alias
   (`use * as http`) is a value and starts lowercase.

7. **A name in a pattern needs no rule of its own.** An uppercase name in a pattern *is* a case, so a binding, a
   closure parameter and a `for` binding cannot be written uppercase at all - see
   [Pattern forms](../pattern-matching/pattern-forms.md).

8. **A name is written out, not abbreviated.** `Expression`, not `Expr`; `absolute`, not `abs`; `squareRoot`, not
   `sqrt`. This is the one rule here that is a convention rather than a diagnostic; it holds for `std/`, so an
   abbreviated name never appears in the standard library.

9. **An abbreviation is allowed where it already is the name people know.** `Html`, `Json`, `Sql`, `Http`, `Int64`,
   `Bool`, `Char`, `min` and `max` are not spelled out further, because spelling them out would produce a longer name
   nobody uses.

   ```trb check
   fn describe(html: String): String {
     "raw: {html}"
   }

   print describe("<p>ok</p>")
   ```

10. **A type parameter is written out like any other name.** `List<Item>`, `Map<Key, Value>`,
    `Result<Value, Failure>` - never `List<T>`, `Map<K, V>`, `Result<V, E>`.

## What this is not

**A name in the wrong case is not a style warning.** It is an error of the checker, reported at the declaration, and
there is no flag that turns it off.

```trb error
type connection {
  var Timeout: Int = 30
}
// error: A type starts with an uppercase letter: write `Connection`
```

**The rule is not about the whole name, only about its first letter.** `HTTPServer`, `ioError` and `x2` all pass:
nothing reads the rest of a name.

```trb check
type HTTPServer {
  var port: Int = 8080
}

const ioError = "closed"
const x2 = 2
print "{HTTPServer().port} {ioError} {x2}"
```

**An abbreviation used elsewhere is not automatically legal here.** `Config`, `Env` and `Msg` are common
abbreviations in other codebases; none of them is one of the names rule 9 lists, so `std/` spells them
`Configuration`, `Environment` and `Message`.

## Related

- [Lexical structure](lexical-structure.md) - the other thing a name cannot be: a keyword.
- [Pattern forms](../pattern-matching/pattern-forms.md) - where the first letter of a name decides what a pattern
  does.
- [Declaring a type](../types/declaring-a-type.md) - where a type's name and its cases are written.
- [Angle brackets or comparison](generics-or-comparison.md) - how a written-out type parameter is told apart from a
  comparison.
