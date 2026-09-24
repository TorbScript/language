---
title: Trailing closures
summary: When the last parameter of a call is a function, the closure argument can follow the call as a brace instead of sitting inside the parentheses.
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
   fn withTransform(transform: (value: Int) => Int, seed: Int): Int {
     transform(seed)
   }

   const result = withTransform({ value: Int => value + 1 }, seed: 1) { value: Int => value * 2 }
   print result
   // error: The trailing closure fills `seed`, which was already given by name
   ```

3. **The implicit parameter of a trailing closure is always `_` (`_2`, `_3`, ...), never a name taken from the
   expected function type.** `fn map<Output>(transform: (value: Item) => Output)` documents its parameter as `value`,
   but that name is not in scope inside the closure - it acts at a distance, and renaming the parameter of a library
   function would silently break every caller that relied on it. `_` or a written parameter name are the two ways to
   read the value.

   ```trb error
   fn mapped<Output>(items: List<Int>, transform: (value: Int) => Output): List<Output> {
     var result: List<Output> = []
     for item in items {
       result.append transform(item)
     }
     result
   }

   const doubled = mapped([1, 2, 3]) { value * 2 }
   // error: Cannot find `value` here
   ```

   The diagnostic's note names the fix exactly: `The parameter of this closure is \`_\`; to name it, write
   \`{ value => ... }\``. Writing `_` or naming the parameter explicitly both work.

   ```trb check
   fn mapped<Output>(items: List<Int>, transform: (value: Int) => Output): List<Output> {
     var result: List<Output> = []
     for item in items {
       result.append transform(item)
     }
     result
   }

   const doubled = mapped([1, 2, 3]) { _ * 2 }
   const named = mapped([1, 2, 3]) { value => value * 2 }
   print doubled
   print named
   ```

4. **A trailing closure always belongs to the outermost command call of the statement.** Commands do not nest, so the
   argument of a command call cannot itself contain a trailing closure - the closure the reader sees last belongs to
   whichever command started the statement.

   ```trb error
   fn transformed(items: List<Int>, transform: (Int) => Int): List<Int> {
     var result: List<Int> = []
     for item in items {
       result.append transform(item)
     }
     result
   }

   fn show(value: List<Int>) {
     print value
   }

   const numbers = [1, 2, 3, 4]
   show transformed(numbers) { item: Int => item * 2 }
   // error: The trailing closure fills `value`, which was already given by name
   // error: `transformed` takes 2 arguments, 1 was given
   ```

5. **A trailing closure is also the body of the head of `if`, `for`, `while` and `match` when the head is a command
   call**, for the same reason: a command's `{` is read as its body, exactly like the built-in statements.

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
    result.append transform(item)
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
    result.append transform(item)
  }
  result
}

const numbers = [1, 2, 3, 4]
show transformed(numbers) { item: Int => item * 2 }
// error: The trailing closure fills `value`, which was already given by name
// error: `transformed` takes 2 arguments, 1 was given
```

## Related

- [Closures](closures.md) - the one closure form a trailing closure is an argument of.
- [Arguments and labels](arguments.md) - how the rest of a call's arguments are matched to parameters.
- [Command calls](../syntax/command-calls.md) - command position, and why a command's arguments cannot nest a trailing closure.
