---
title: Control structures are functions
summary: do, unless, retry and test are ordinary functions with a closure or lazy parameter, so writing your own control structure is nothing more than writing a function that takes one and calling it with a trailing closure.
kind: reference
status: stable
order: 10
keywords:
  - control structure
  - trailing closure
  - do
  - unless
  - retry
source:
  - CONCEPT.md#extensibility
  - std/core/src/control.trb
---

`if`, `for`, `while` and `match` are the only control flow built into the grammar, because they are the only forms
that bind a name or jump. Everything that reads like a keyword beyond those four - `do`, `unless`, `retry`, `test` -
is an ordinary function whose last parameter happens to be a closure.

## Example

```trb check
fn twice(body: () => Void) {
  body()
  body()
}

twice { print "hi" }
```

## Syntax

```text
fn <name>(..., body: () => <Type>) { ... }     a control structure: a function with a closure parameter
<name> <argument> { ... }                      called with a trailing closure, reading like a keyword
```

## Rules

1. **A control structure is a function whose closure or `lazy` parameter is what makes it read like syntax.**
   `unless(condition: Bool, body: () => Void)`, called as `unless ready() { print "waiting" }`, is not different from
   any other function call - it is a [command call](../syntax/command-calls.md) with a
   [trailing closure](../functions/trailing-closures.md).

2. **Writing your own is writing a function and calling it.** A function whose last parameter is `() => Value`
   becomes a block-taking construct the moment it is called with a trailing closure; nothing has to be registered or
   declared specially.

   ```trb check
   fn measured<Value>(label: String, body: () => Value): Value {
     print "starting {label}"
     const result = body()
     print "finished {label}"
     result
   }

   const total = measured "sum" {
     1 + 2 + 3
   }

   print total
   ```

3. **`retry` and `test` are written in ordinary TorbScript, out of the same pieces.** `retry` recurses in tail
   position, and neither one is special-cased by the checker. `using` is not one of them: it binds a name, so it is a
   binding form next to `const` and `var` (see [Destructors](../execution/destructors.md)).

4. **Only what binds a name or jumps is built into the grammar.** `const`, `var`, `using`, `fn`, `type`, `trait`,
   `extend` and `use` bind one; the declarations share one shape, `modifier* keyword Name clauses* { body }`, because
   binding a name is the one thing a function cannot do on a caller's behalf.

5. **There are no macros, because names are resolved with the help of types.** A receiver and an overload both need
   to know what a name refers to before a macro could rewrite anything, so a macro that ran before name resolution
   and one that ran after would not agree on what the code even says.

## What this is not

**A control structure is not a keyword you could shadow by declaring a function of the same name.** `if`, `for`,
`while` and `match` are the grammar, not names - there is no function called `if`, and a keyword is refused wherever a
name is declared, down to a parameter.

```trb check
fn unless(condition: Bool, body: () => Void) {
  if !condition {
    body()
  }
}

unless false { print "runs, because the condition is false" }
```

```trb error
fn repeat(times: Int, if: Bool) {
  print times
}
// error: `if` is a keyword and cannot be used as a name here
```

## Related

- [Trailing closures](../functions/trailing-closures.md) - the syntax that makes a control structure read like one.
- [Command calls](../syntax/command-calls.md) - the call form every control structure is written in.
- [std/core](../../standard-library/core.md) - `do`, `unless`, `retry` and `Close`.
- [Declaring a function](../functions/declaring-a-function.md) - the one thing a control structure cannot replace: binding a name.
