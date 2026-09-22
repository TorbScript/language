---
title: Variadic parameters
summary: A parameter written ...name collects every remaining positional argument into a List, and a collection is only unpacked into it when the call spreads it with the same three dots.
kind: reference
status: stable
order: 40
keywords:
  - variadic
  - spread
  - ellipsis
  - rest parameter
source:
  - CONCEPT.md#arguments
  - examples/tour/src/02-functions.trb
---

A variadic parameter, written `...name: Type`, takes any number of positional arguments of `Type` and collects them
into a `List<Type>`. A caller who already holds a collection spreads it into the call with the same `...`.

## Example

```trb check
fn sumAll(...numbers: Int): Int {
  numbers.fold 0 { a, b => a + b }
}

const values = [1, 2, 3]
print sumAll(1, 2, 3)
print sumAll(...values)
```

## Syntax

```text
fn <name>(..., ...<name>: <Type>): <ReturnType> { ... }

<call>(..., ...<expression>)
```

## Rules

1. **`...name: Type` inside a parameter list declares a variadic parameter, and its body sees `name` as a
   `List<Type>`.** It is written last, and every positional argument from its position onward fills it.

2. **A call spreads an `Iterable<Type>` into a variadic parameter with `...expression`.** Spreading works with any
   type that implements the `Iterable` trait, not only `List`, so a `Set` spreads the same way a `List` does.

   ```trb check
   fn sumAll(...numbers: Int): Int {
     numbers.fold 0 { a, b => a + b }
   }

   const someSet = Set.of 4, 5, 6
   print sumAll(1, ...someSet)
   ```

   `...` only fills a variadic parameter: spreading into an ordinary one is rejected, naming the parameter it was
   aimed at.

   ```trb error
   fn configure(port: Int) {
     print port
   }

   const settings = [8080]
   configure(...settings)
   // error: `port` is not a variadic parameter, so `...` cannot spread into it
   ```

3. **A variadic parameter that receives no argument becomes an empty list**, not a missing-argument error.

   ```trb check
   fn sumAll(...numbers: Int): Int {
     numbers.fold 0 { a, b => a + b }
   }

   print sumAll()
   ```

4. **A parameter declared after a variadic one can only be filled by label.** Every positional argument from the
   variadic parameter's position onward fills it, so a later parameter is never reached positionally.

   ```trb error
   fn describe(...numbers: Int, label: String): String {
     "{label}: {numbers}"
   }

   print describe(1, 2, "totals")
   // error: `describe` has no argument for `label`
   // error: Expected `Int64`, found `String`
   ```

## What this is not

**A collection is not unpacked by standing in a positional argument on its own.** Only `...` unpacks it; without it,
the whole collection becomes the one element the variadic parameter collects.

```trb check
fn sumAll(...numbers: Int): Int {
  numbers.fold 0 { a, b => a + b }
}

const values = [1, 2, 3]
print sumAll(...values)
```

```trb check
fn wrapped(...numbers: List<Int>): List<List<Int>> {
  numbers
}

const values = [1, 2, 3]
print wrapped(values)
```

The second program is not an error - it type checks, because `values` is one argument of type `List<Int>` and the
parameter's element type is `List<Int>` too. Its result is `[[1, 2, 3]]`, a list of one list, which is rarely what was
meant. Spreading (`...values`) is what turns the collection's own elements into the arguments.

**`...rest` is not `*args` from a language with no other way to pass many arguments.** Every parameter before it is
still ordinary and still labelled the same way, and only the first variadic parameter of a signature can ever collect
anything positionally (see rule 4).

## Related

- [Arguments and labels](arguments.md) - how a positional argument is matched to a parameter.
- [Declaring a function](declaring-a-function.md) - the rest of the parameter list.

