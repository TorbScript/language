---
title: Trailing closures
summary: When the last parameter of a call is a function, the closure argument can follow the call as a brace instead of sitting inside the parentheses, and it can name its parameter after the function type instead of using _.
kind: reference
status: stable
order: 60
keywords:
  - trailing closure
  - implicit closure parameter
source:
  - CONCEPT.md#trailing-closures
  - examples/tour/src/02-functions.trb
---

If the last parameter of a function is itself a function, a call may write that argument as a `{ ... }` after the
call instead of inside its parentheses.

## Example

```trb check
const numbers = [1, 2, 3, 4]
const doubled = numbers.map({ _ * 2 })
const same = numbers.map { _ * 2 }
const total = numbers.fold 0 { sum, number => sum + number }

print doubled.toList()
print same.toList()
print total
```

## Syntax

```text
<call>(<other arguments>) { <closure> }
<call> <other arguments> { <closure> }
```

## Rules

1. **A closure that is the last parameter of a call may be written after it, as a brace.** `numbers.map({ _ * 2 })`
   and `numbers.map { _ * 2 }` fill the same parameter.

2. **A trailing closure fills the last parameter of the call, whatever position it was declared in.** If that
   parameter already has a value - positionally or by label - the trailing closure is a compile error, because it
   would fill something that is already filled.

   ```trb error
   fn withTransform(transform: (Int) => Int, seed: Int): Int {
     transform(seed)
   }

   const result = withTransform({ _ + 1 }, seed: 1) { _ * 2 }
   // error: The trailing closure fills `seed`, which was already given by name
   ```

3. **The implicit parameter of a trailing closure can be named after the parameter name the function type itself
   carries**, instead of `_`. `fn map<Output>(self, transform: (value: Item) => Output)` lets a caller write
   `numbers.map { value * 2 }`, because the function type names its own parameter `value`.

   ```trb check
   fn mapped<Output>(items: List<Int>, transform: (value: Int) => Output): List<Output> {
     var result: List<Output> = []
     for item in items {
       result.add transform(item)
     }
     result
   }

   const doubled = mapped([1, 2, 3]) { value * 2 }
   print doubled
   ```

4. **A trailing closure always belongs to the outermost command call of the statement.** Commands do not nest, so the
   argument of a command call cannot itself contain a trailing closure - the closure the reader sees last belongs to
   whichever command started the statement.

   ```trb error
   fn transformed(items: List<Int>, transform: (Int) => Int): List<Int> {
     var result: List<Int> = []
     for item in items {
       result.add transform(item)
     }
     result
   }

   fn show(value: List<Int>) {
     print value
   }

   const numbers = [1, 2, 3, 4]
   show transformed(numbers) { _ * 2 }
   // error: The trailing closure fills `value`, which was already given by name
   ```

5. **A trailing closure is also the body of the head of `if`, `for`, `while` and `match` when the head is a command
   call**, for the same reason: a command's `{` is read as its body, exactly like the built-in statements.

6. **Naming the implicit parameter is rejected when it would shadow a local that is already visible.** There is no
   silent shadowing: if a parameter or a binding named `amount` is already in scope, `increase { amount + 1 }` is an
   error instead of quietly reading the outer `amount`, because nothing in the source would say which one was meant.

   ```trb error
   fn probe(amount: Int): Int {
     increase { amount + 1 }
   }

   fn increase(transform: (amount: Int) => Int): Int {
     transform 1
   }
   // error: The implicit parameter `amount` would shadow `amount`
   ```

   Naming the closure's parameter something else is the fix, and then the outer `amount` is reachable again inside
   the closure body.

   ```trb check
   fn probe(amount: Int): Int {
     increase { item => item + amount }
   }

   fn increase(transform: (amount: Int) => Int): Int {
     transform 1
   }

   print probe(5)
   ```

## What this is not

**A trailing closure is not the closure of an inner call written with parentheses.** Passing a closure to a call
that is itself an argument needs the parentheses form for the inner call, so that the trailing `{` at the end of the
statement is unambiguous about which call it belongs to.

```trb check
fn show(value: List<Int>) {
  print value
}

fn transformed(items: List<Int>, transform: (Int) => Int): List<Int> {
  var result: List<Int> = []
  for item in items {
    result.add transform(item)
  }
  result
}

const numbers = [1, 2, 3, 4]
show transformed(numbers, { _ * 2 })
```

```trb error
fn show(value: List<Int>) {
  print value
}

fn transformed(items: List<Int>, transform: (Int) => Int): List<Int> {
  var result: List<Int> = []
  for item in items {
    result.add transform(item)
  }
  result
}

const numbers = [1, 2, 3, 4]
show transformed(numbers) { _ * 2 }
// error: The trailing closure fills `value`, which was already given by name
```

**Naming the implicit parameter is not always available.** It only works where the callee's own function type names
its parameter, as `map`'s `transform: (value: Item) => Output` does; a call whose function type carries no parameter
name still needs `_` or a written name.

## Related

- [Closures](closures.md) - the one closure form a trailing closure is an argument of.
- [Arguments and labels](arguments.md) - how the rest of a call's arguments are matched to parameters.
- [Command calls](../syntax/command-calls.md) - command position, and why a command's arguments cannot nest a trailing closure.
