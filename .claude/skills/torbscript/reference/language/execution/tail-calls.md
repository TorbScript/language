---
title: Tail calls and stack overflow
summary: Direct self-recursion in tail position is guaranteed to run without growing the stack, and every other call uses a frame of its task's stack; a recursion that runs out of stack panics with stack overflow instead of crashing.
kind: reference
status: stable
order: 30
keywords:
  - tail call
  - stack overflow
  - stack limit
  - recursion
source:
  - CONCEPT.md#execution-model
---

A function that calls itself as the last thing it does can recurse without growing the stack - this is the one
guarantee the language makes about recursion, and it is exactly what `retry` and every fold need to work on an
unbounded input.

## Example

```trb check
fn sum(numbers: List<Int>, index: Int, total: Int): Int {
  if index == numbers.length() {
    return total
  }
  sum(numbers, index + 1, total + numbers[index])
}

print sum([1, 2, 3, 4], 0, 0)
```

## Syntax

```text
fn <name>(...): <Type> {
  ...
  <name>(...)     // Direct self-recursion in tail position: no new stack frame
}
```

## Rules

1. **A call is a tail call when it is the last thing a function does**, with nothing left to run after it returns -
   the final expression of the function's body, or the value of a `return`.

2. **Only direct self-recursion in tail position is guaranteed.** A function calling itself this way is guaranteed
   not to grow the stack; a call to any other function, in tail position or not, uses an ordinary stack frame.

3. **Both back ends implement it as the same jump to the entry block.** The interpreter and the compiled binary
   agree on this because a tail call to oneself is compiled the same way a loop would be, not as a nested call.

4. **Every other call uses a frame of the stack of its task, and the stack is the limit.** How deep a recursion can
   go depends on how big its frames are and how big the stack is: the main thread of a program gets the stack its
   platform gives it, 1 MiB to 8 MiB, which is tens of thousands of frames of a small function. There is no frame count
   and no flag that sets one, because a count says nothing about bytes: a hundred thousand small frames fit where ten
   thousand big ones do not, so a count either stops a recursion that would fit or lets one crash that does not.

5. **Running out of stack panics with `stack overflow`.** Every function that calls another checks, when it is
   entered, that the stack still has room, and where it has not the call panics instead of running into the
   operating system's guard page. This is a `panic` like any other: it prints the message and the function that could
   not be entered to standard error, exits with **101**, and nothing runs on the way out (see
   [Result](../errors/result.md) for what a panic does and does not do).

## What this is not

**The guarantee is not general tail-call elimination for every call in tail position.** Two functions that call each
other in tail position - mutual recursion - each still use a stack frame per round trip, because neither one is
calling *itself*.

```trb check
fn sum(numbers: List<Int>, index: Int, total: Int): Int {
  if index == numbers.length() {
    return total
  }
  sum(numbers, index + 1, total + numbers[index])
}

print sum([1, 2, 3, 4], 0, 0)
```

```trb
fn isEven(n: Int): Bool {
  if n == 0 {
    return true
  }
  isOdd(n - 1)
}

fn isOdd(n: Int): Bool {
  if n == 0 {
    return false
  }
  isEven(n - 1)
}

print isEven(1_000_000)
```

The second pair type checks and runs correctly for a small `n`; nothing about it is a compile error, because the
stack is a resource a program can run into, not a rule the checker enforces. A large `n` panics with
`stack overflow` instead of finishing - rewriting one function to call the other directly, in a loop, is what avoids
that, exactly as the accumulator pattern of `sum` above does for a single function.

## Related

- [Result](../errors/result.md) - what a `panic` prints, and its exit code.
- [Evaluation order](evaluation-order.md) - the other execution-model question answered without running the program.
- [There are no destructors](no-destructors.md) - what a panic skips on its way out.

