---
title: Exclusivity
summary: CONCEPT.md specifies that two var accesses of the same call may not target the same path, and today's checker accepts the textbook counter-example instead of rejecting it.
kind: reference
status: draft
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

> **Draft.** This page is being written and may be wrong. Verify it against the compiler.

`CONCEPT.md` describes a rule beyond the single-path checks of [var paths](var-paths.md): two `var` accesses are never
allowed to run at the same time against the same path, even when they belong to one call rather than to two nested
ones. Today's checker does not yet reject the textbook counter-example of that rule.

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

2. **`CONCEPT.md` specifies that two `var` accesses of the *same* call may not target the same path** - passing one
   path to two `var` parameters of one call (`swap(a, a)`), or two indices of one collection that the compiler cannot
   tell apart (`swap(items[i], items[j])`). `items.swapAt(i, j)` is the one `var self` access the standard library
   offers instead of either.

3. **Today's checker accepts both of those calls without a diagnostic.** Verified directly: a two-parameter `swap`
   function called as `swap(pair[0], pair[0])`, and the same call written as `swap(items[i], items[j])` with two
   `const` indices, both type-check with no problem reported. This is a gap between the design and the checker, not a
   change of the design - the rule may still be enforced by a later milestone.

   ```trb check
   fn swap<Value>(var first: Value, var second: Value) {
     const value = first
     first = second
     second = value
   }

   var pair = [1, 2]
   swap(pair[0], pair[0])
   print pair
   ```

## What this is not

**This gap is not a reason to write `swap(a, a)` on purpose.** The design still calls it undefined: which of the two
`var` accesses "wins" is not specified anywhere, so a program that relies on it is relying on whatever one version of
the checker and one back end happen to do today.

```trb
var items = [3, 1, 2]
const i = 0
const j = 1
items.swapAt i, j
print items
```

```trb skip the compiler does not yet reject this call; see rule 3 above
fn swap<Value>(var first: Value, var second: Value) {
  const value = first
  first = second
  second = value
}

var pair = [1, 2]
swap(pair[0], pair[0])
```

## Related

- [Mutation and var paths](var-paths.md) - the single-path rules that are enforced today.
- [Declaring a type](declaring-a-type.md) - `var self`, the receiver form this rule is meant to protect.
- [Why values instead of references](../../explanation/why-values-instead-of-references.md) - the argument this rule
  serves.
