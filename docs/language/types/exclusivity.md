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
