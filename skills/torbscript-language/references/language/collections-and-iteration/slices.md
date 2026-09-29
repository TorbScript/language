---
title: Slices
summary: list[from..to] answers a List that shares storage and starts at index 0 again; as a var path the same expression is a window into the original instead.
kind: reference
status: stable
order: 50
keywords:
  - Slice
  - MutableSlice
  - window
  - var path
source:
  - CONCEPT.md#collections-and-iteration
  - std/core/src/operators.trb
---

`list[from..to]` reads as a value: a new `List` that shares the original's storage and starts counting at 0, unaffected
by anything the original does afterward. The same expression used as a `var` path is the other half of `Slice`'s
contract, `MutableSlice`: it changes that part of the original in place, which is what other languages need a mutable
span type for.

## Example

```trb check
var samples = [5, 3, 9, 1, 7, 2]
const middle = samples[1..4]
samples[0] = 0
print middle

samples[1..4].sort { value => value }
print samples
```

## Syntax

```text
value[from..to]                          Slice.slice: a new value, storage shared, indices start at 0
value[from..to] = other                  MutableSlice.replace: changes this part of value in place
value[from..to].verb(...)                a var path: verb runs on this part of value, in place
value[from..to].compact()                List.compact: gives the slice storage of its own, sized exactly
```

## Rules

1. **A slice read as a value is unaffected by later changes to the original.** `samples[1..4]` copies nothing at the
   moment it is taken - the storage is shared - but it is still a value: `middle` above does not see `samples[0] = 0`,
   because that assignment never goes through `middle`'s path.

2. **The same expression as a `var` path is a window: the change lands in the original.**
   `samples[1..4].sort { value => value }` sorts exactly that part of `samples`, the same way a field step of a `var`
   path would. The part is taken out, changed and put back through `MutableSlice.replace`, exactly as an element of
   `a[key]` is, so the change costs a copy of that part and nothing of the rest. See
   [Mutation and var paths](../types/var-paths.md) for the general rule a slice follows here.

3. **A slice keeps the whole original storage alive**, so a small slice of a big value is a small view backed by a big
   buffer. `header.compact()` gives it storage of its own, exactly as big as the slice needs, for the case where a
   small slice of something big is kept for a long time.

4. **A slice takes `Bounds<Int>`, so all five spellings work**, and an end the range leaves open reads as "from the
   start" or "to the end of the value". `samples[..2]` is a `RangeTo<Int>` and `samples[3..]` a `RangeFrom<Int>` -
   [neither of them is a sequence](../values-and-types/ranges.md), and here neither has to be: what an open end means
   is decided by whoever takes the range.

   ```trb check
   var samples = [5, 3, 9, 1, 7, 2]
   print samples[..2]
   print samples[3..]
   ```

5. **A function that takes a `var` parameter works on whatever slice a caller passes**, because the parameter only
   needs a `var` path, and a slice of a `var` path is one.

   ```trb check
   fn fillWithZeros(var target: List<Int>) {
     for index in 0..target.length() {
       target[index] = 0
     }
   }

   var samples = [5, 3, 9, 1, 7, 2]
   fillWithZeros samples[0..2]
   print samples
   ```

6. **`String` and `Array` slice the same way `List` does**, because `Slice` and `MutableSlice` are traits, not
   something specific to one type.

## What this is not

**A slice through a `const` path is not a `var` path, even though the same brackets read it.** Sorting one needs a
`var` binding underneath, exactly as any other in-place change does.

```trb check
var samples = [5, 3, 9, 1, 7, 2]
samples[1..4].sort { value => value }
print samples
```

```trb error
const samples = [5, 3, 9, 1, 7, 2]
samples[1..4].sort { value => value }
// error: `sort` needs a `var`
```

## Related

- [Lists](lists.md) - the type most slices are taken from.
- [Ranges](../values-and-types/ranges.md) - the three range types, and what an open end means elsewhere.
- [Mutation and var paths](../types/var-paths.md) - the rule that makes a slice a window instead of a copy.

