---
title: Evaluation order
summary: Evaluation order is source order - the receiver first, then the arguments as they are written, then the parameter defaults - so a side effect in an argument is exactly as predictable as reading the line.
kind: reference
status: stable
order: 10
keywords:
  - evaluation order
  - short-circuit
  - side effect
source:
  - CONCEPT.md#execution-model
---

"Left to right" can only mean the order a reader sees. Every place several subexpressions could run in more than one
order is pinned to that one order, so a side effect inside an argument is never a question a reader has to think
about.

## Example

```trb check
fn traced(label: String, value: Int): Int {
  print label
  value
}

fn combine(a: Int, b: Int): Int {
  a + b
}

print combine(traced("first", 1), traced("second", 2))
```

`"first"` prints before `"second"`, because the arguments run in the order they are written, left to right.

## Syntax

```text
receiver.method(argument, argument)     receiver, then the arguments in written order
place = value                           the subexpressions of place, then value
condition1 && condition2                condition1, then condition2 only if condition1 is true
condition1 || condition2                condition1, then condition2 only if condition1 is false
value ?? fallback                       value, then fallback only if value is absent or a Fail
```

## Rules

1. **A call evaluates its receiver first, then its arguments in the order they are written.** This holds whether the
   call is a command or has parentheses; the two are the same call with a different spelling.

2. **A labelled argument is evaluated where it is written, and only reordered into declaration order afterwards.**
   The label decides which parameter gets the value, not when the value runs.

   ```trb check
   fn traced(label: String, value: Int): Int {
     print label
     value
   }

   fn point(x: Int, y: Int): (Int, Int) {
     (x, y)
   }

   print point(y: traced("y", 2), x: traced("x", 1))
   ```

   `"y"` prints before `"x"`, because that is the order they are written, even though `x` is declared first.

3. **A parameter default runs after every explicit argument, in the declaration order of the parameters that were
   left out.** A default that reads another parameter reads the value that parameter ended up with, never a value
   still being computed.

4. **In `place = value`, every subexpression of `place` is evaluated before `value` is.** `list[index()] = compute()`
   calls `index()` before it calls `compute()`.

5. **`&&` and `||` short-circuit.** The right side of `&&` runs only if the left side is `true`; the right side of
   `||` runs only if the left side is `false`.

   ```trb check
   fn traced(label: String, value: Bool): Bool {
     print label
     value
   }

   print(traced("left", false) && traced("right", true))
   ```

   Only `"left"` prints: the left side is `false`, so `&&` never evaluates the right side at all.

6. **`??` evaluates its right side only when the left side needs it.** The right side is a `lazy` parameter, so an
   expensive fallback that is never used never runs (see [Parameter modes](../functions/parameter-modes.md)).

## What this is not

**Evaluation order is not "whichever is convenient for the back end."** Both back ends - the interpreter and the
compiled binary - produce the same order, because the order is a rule of the language and not an implementation
detail either one is free to pick.

```trb check
fn traced(label: String, value: Int): Int {
  print label
  value
}

const first = traced "a", 1
const second = traced "b", 2
print(first + second)
```

```trb error
fn traced(label: String, value: Int): Int {
  print label
  value
}

print(traced("a", 1) && traced("b", 2))
// error: Expected `Bool`, found `Int64`
```

## Related

- [Parameter modes](../functions/parameter-modes.md) - `lazy`, whose whole point is to change when an expression runs.
- [What a copy costs](copies.md) - the other execution-model question a reader needs answered without running the program.
- [Tail calls and the frame limit](tail-calls.md) - what a call in tail position is guaranteed, order aside.

