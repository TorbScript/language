---
title: Exclusivity
summary: Two var accesses of the same call may never target the same path, so swap(a, a) and two indices the checker cannot tell apart are both compile errors, and items.swapAt is the one access that is allowed instead.
kind: reference
status: stable
order: 90
keywords:
  - exclusivity
  - var access
  - overlap
  - swapAt
source:
  - CONCEPT.md#var-paths-and-var-parameters
  - std/collections/src/list.trb
---

> **Not built natively yet.** A closure that changes a top-level `var` while a function holds another one as a `var`
> parameter is not built by the native back end yet, so `torb run` refuses the examples here that use it. `torb check`
> accepts them, and the rules are the language's.

`CONCEPT.md` describes a rule beyond the single-path checks of [var paths](var-paths.md): two `var` accesses are never
allowed to run at the same time against the same path, even when they belong to one call rather than to two nested
ones. The checker enforces it, at the top level of a file included.

## Example

```trb
var items = [3, 1, 2]
const i = 0
const j = 1
items.swapAt i, j
print items
```

## Syntax

```text
<call>(<var-argument>, ...)     // The access of the call begins once every argument above has been evaluated
```

## Rules

1. **The access of a call begins only once all of its arguments have been evaluated.** Everything an argument *reads*
   has therefore already finished by the time the access starts, so `items.removeAt(items.length() - 1)` type-checks
   as ordinary code: the length is read before anything is removed.

   ```trb check
   var items = [3, 1, 2]
   items.removeAt(items.length() - 1)
   print items
   ```

2. **Two `var` accesses of the *same* call may not target the same path.** Passing one path to two `var` parameters
   of one call (`swap(a, a)`), or two indices of one collection that the checker cannot tell apart
   (`swap(items[i], items[j])`), are both rejected. `items.swapAt(i, j)` is the one exclusive `var` access the standard
   library offers instead of either.

   ```trb error
   fn swap<Value>(var first: Value, var second: Value) {
     const value = first
     first = second
     second = value
   }

   var a = 1
   swap(a, a)
   print a
   // error: `a` is being changed by `swap` right now
   ```

3. **Two indices of one collection are rejected even when nothing but their value tells them apart**, because the
   checker never evaluates a `const` index to compare it: `items[i]` and `items[j]` overlap as far as it can tell,
   whether or not `i` and `j` happen to differ at run time.

   ```trb error
   fn swap<Value>(var first: Value, var second: Value) {
     const value = first
     first = second
     second = value
   }

   var pair = [1, 2]
   swap(pair[0], pair[0])
   print pair
   // error: `pair[...]` is being changed by `swap` right now
   ```

4. **A closure argument runs inside the access of its call, so it may not reach the path the call is changing.**
   The call's access has begun by the time the closure runs, which is what makes changing `list` inside
   `apply list { ... }` an error while two different fields of one value are fine. The same holds for a closure
   that captures a `var` binding: it may only be passed directly as an argument (see
   [var paths](var-paths.md) rule 4), so the call it is passed to is where the checker sees every access it makes.

   ```trb check
   fn apply(var target: List<Int>, action: () => Void) {
     action()
     target.add 1
   }

   var list = [1, 2]
   var log: List<String> = []
   apply list {
     log.add "applied"
   }
   print list
   print log
   ```

   ```trb error
   fn apply(var target: List<Int>, action: () => Void) {
     action()
     target.add 1
   }

   var list = [1, 2]
   apply list {
     list.add 3
   }
   print list
   // error: `list` is being changed by `apply` right now
   ```

   `items.update(0) { ... }` holds a `var` access to `items` for as long as the closure runs, and a closure that
   changes `items` itself is the second access. Different fields are fine; the same path, a path above or below it
   are not.

   ```trb error
   var items = [1, 2, 3]
   items.update(0) { value =>
     items.add 4
     value = value + 1
   }
   print items
   // error: `items` is being changed by `update` right now
   ```

   This is checked where the closure is written, and it can be, because a closure that captures a `var` binding
   never gets anywhere else: it is not bound to a name, stored or returned, and it is only handed to a parameter that
   just calls it ([Closures](../functions/closures.md), rule 8). So `const clear = { items = [] }` followed by
   `apply(items, clear)` is refused where `clear` is bound, before the call could run it inside its own access.

## What this is not

**`items.swapAt(i, j)` is not the same call as `swap(items[i], items[j])`.** `swapAt` takes both indices as plain
`Int` arguments and reaches into `items` itself, so there is only ever one `var` access - of `items`, not of two
places inside it - and no overlap to reject.

```trb
var items = [3, 1, 2]
const i = 0
const j = 1
items.swapAt i, j
print items
```

```trb error
fn swap<Value>(var first: Value, var second: Value) {
  const value = first
  first = second
  second = value
}

var items = [3, 1, 2]
const i = 0
const j = 1
swap(items[i], items[j])
print items
// error: `items[...]` is being changed by `swap` right now
```

## Related

- [Mutation and var paths](var-paths.md) - the single-path rules that this rule builds on.
- [Declaring a type](declaring-a-type.md) - `var fn`, the receiver form this rule is meant to protect.
- [Why values instead of references](../../explanation/why-values-instead-of-references.md) - the argument this rule
  serves.

