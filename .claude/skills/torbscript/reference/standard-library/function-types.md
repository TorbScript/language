---
title: Predicate, Action and Transform
summary: Three aliases in the prelude for the closure shapes signatures take most - a question about one value, an effect on one value, and a conversion of one value into another.
kind: reference
status: stable
order: 15
keywords:
  - Predicate
  - Action
  - Transform
  - function type
  - closure type
  - type alias
source:
  - std/core/src/functions.trb
  - std/prelude/src/lib.trb
---

`Predicate`, `Action` and `Transform` name the three closure shapes that turn up in almost every signature that takes a
closure: a yes-or-no question about one value, an effect on one value, and the conversion of one value into another.
They are declared in `std/core` and re-exported by the prelude, so every file has them without an import. `filter`,
`forEach` and `map` of the standard library are written with them.

## Example

```trb run
fn countWhere(numbers: List<Int>, predicate: Predicate<Int>): Int {
  numbers.filter(predicate).count()
}

fn visit(names: List<String>, action: Action<String>) {
  names.forEach action
}

fn labels<Output>(numbers: List<Int>, transform: Transform<Int, Output>): List<Output> {
  numbers.map(transform).toList()
}

fn isEven(number: Int): Bool {
  number % 2 == 0
}

const numbers = [3, 8, 5, 6]
const names = ["Ada", "Alan"]
const large = countWhere numbers { _ > 4 }
const even = countWhere numbers, isEven
const shown = labels numbers { "#{_}" }
print large    // prints 3
print even     // prints 2
print shown    // prints ["#3", "#8", "#5", "#6"]
visit names { print "hello {_}" }
// prints hello Ada
// prints hello Alan
```

## Syntax

```trb fragment
public type Predicate<Value> = (value: Value) => Bool
public type Action<Value> = (value: Value) => Void
public type Transform<Input, Output> = (value: Input) => Output
```

## Rules

1. **An alias is transparent: it is another name for the function type, and adds no type of its own.**
   `Predicate<Int>` and `(value: Int) => Bool` are one type, so a value of either passes where the other is expected,
   and `Predicate<Int>` and `Transform<Int, Bool>` are that type as well. A diagnostic spells the shape out, because
   the shape is what did not fit.

   ```trb error
   fn countWhere(numbers: List<Int>, predicate: Predicate<Int>): Int {
     numbers.filter(predicate).count()
   }

   fn shout(text: String): String {
     text + "!"
   }

   const count = countWhere([1, 2], shout)
   // error: Expected `(value: Int64) => Bool`, found `(text: String) => String`
   ```

2. **Any closure or function of the shape fits.** The parameter is called `value` in the alias, but a closure names
   its own parameter (`{ number => number > 4 }`) or uses `_`, and a function whose parameter has another name
   (`isEven(number: Int)`) is passed as it is. A trailing closure fills a parameter written with an alias exactly as it
   fills one written out, and the closure's parameter type comes from the alias.

3. **A type parameter is inferred through the alias.** `labels` above takes a `Transform<Int, Output>`, and the
   closure `{ "#{_}" }` makes `Output` a `String` - the same inference the spelled-out `(value: Int) => Output` gets.
   The output of a `Transform` is any type: `filterMap` takes a `Transform<Item, Output?>`, `flatMap` a
   `Transform<Item, Iterate<Output>>`.

4. **A signature that takes one of the three shapes writes the alias.** `predicate: Predicate<Item>` says what the
   closure is for before it says what it looks like, and reads like the standard library.

5. **`Predicate` is a question, as in logic and in Java's and C#'s `Predicate`.** It answers `true` or `false` about the
   one value it is given, and asks nothing else.

6. **`Action` is a closure called for its effect.** It is not called `Consumer`: to consume already means something in
   TorbScript - taking the values out of a `Source` or a channel, which happens once.

7. **`Transform` is named after the `transform` parameter of `map`.** It is not called `Function` or `Func`, because
   those words name every function, and a closure without parameters is a function as much as this one is.

8. **A closure without parameters has no alias.** `() => Value` and `() => Void` are already as short as a name
   would be, so `do`, `unless` and `retry` spell them out.

## What this is not

**Not a family of functional interfaces.** There is no alias for two parameters (`fold` takes
`(State, Item) => State`), none for a `var` parameter (`List.update` takes `(var element: Item) => Void`), and none for
a closure that answers a `Task` (the step of `Source.then`). Those shapes are spelled out where they occur.

**Not a type of its own.** An alias has no members and no trait to implement: nothing that is not a closure or a
function of the shape becomes a `Predicate`, and a `Predicate` cannot be told apart from the function type it names.

**Not a change to closures.** A closure is still written `{ parameters => body }`; the alias only names the type of a
parameter, a field or a binding that holds one.

## Related

- [Closures](../language/functions/closures.md) - the closure literal and how its parameter types are inferred.
- [Trailing closures](../language/functions/trailing-closures.md) - which parameter a trailing closure fills.
- [std/core](core.md) - the package that declares the three aliases.
- [std/prelude](prelude.md) - the re-exports that put them in scope everywhere.
- [Idiomatic TorbScript](../guide/idiomatic-torbscript.md#closure-types) - the habit of writing the alias.

