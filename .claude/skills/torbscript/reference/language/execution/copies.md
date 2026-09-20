---
title: What a copy costs
summary: A copy always behaves the same way, but what it costs depends on the shape of the type - inline for a small fixed-size value, copy-on-write for heap-backed storage, and never for a shared type.
kind: reference
status: stable
order: 20
keywords:
  - copy
  - copy-on-write
  - reference counting
  - cost model
source:
  - CONCEPT.md#execution-model
---

Assigning, passing or capturing a value always copies it, and every copy behaves the same way: nothing done to one
binding afterwards is visible through the other (see
[Copy and equality](../types/copy-and-equality.md)). What a copy costs at runtime is a different question, and it
depends entirely on the shape of the type.

## Example

```trb check
type Point {
  var x: Int
  var y: Int
}

var a = Point x: 1, y: 2
var b = a
b.x = 9
print "{a.x} {b.x}"
```

## Syntax

```text
Small, fixed size (Point, tuples, Array<Float, 16>)  stored inline, really copied
Heap-backed (ArrayList, String, tries)                copy-on-write: a copy shares storage until a write
Composed of those (a type built from other types)     the fields are copied, their storage stays shared
Recursive ADTs (trees)                                reference-counted nodes; a write copies the path
Big types                                             the compiler may box them and treat them like storage
```

## Rules

1. **A small, fixed-size value is copied inline.** `Point`, a tuple, a fixed-size `Array<Float, 16>` - nothing is
   reference counted, and mutation happens in place because there is no shared storage to protect.

2. **A type whose storage is on the heap uses copy-on-write.** `ArrayList`, `String` and the tries share their
   storage between a value and its copy until one of them writes; the write copies first, if the storage is shared,
   and writes in place otherwise. Only native types implement this.

3. **A type built out of other types costs nothing extra.** Its fields are copied the way each of them is copied on
   its own, and their own storage stays exactly as shared or as inline as it already was - there is nothing to
   implement for the type that contains them.

4. **A recursive ADT such as a tree uses reference-counted nodes.** A write copies the path from the root to the
   changed node, or mutates in place if nobody else holds that node.

5. **A `var` parameter and a `var` path are passed as references.** The "copy in, copy out" meaning of `var` is
   never executed literally - a large value behind a `var` path is not copied twice by the call, once in and once
   out, whatever its own copy strategy is.

6. **None of this is observable from the language.** Whichever strategy a type uses, `==`, `hash()`, `copy()` and
   every rule of [Mutation and var paths](../types/var-paths.md) give the same answer - a program cannot tell which
   one is in effect, and neither back end has to agree with the other about it.

## What this is not

**A shape's cost is not a promise about identity.** Every strategy above still describes a value: whatever storage
two copies happen to share, there is no way to ask whether they are "the same object", because a value never has
one. That question exists only for a [shared type](../types/shared-types.md).

```trb check
type Point {
  var x: Int
  var y: Int
}

var a = Point x: 1, y: 2
var b = a
b.x = 9
print "{a.x} {b.x}"
```

```trb error
type Point {
  var x: Int
  var y: Int
}

const a = Point x: 1, y: 2
const b = Point x: 1, y: 2
print isSame(a, b)
// error: `isSame` compares identity, and a `Point` is a value
```

## Related

- [Copy and equality](../types/copy-and-equality.md) - what a copy means, independent of what it costs.
- [Shared types](../types/shared-types.md) - the one kind of type a copy never touches.
- [Mutation and var paths](../types/var-paths.md) - the rule that stays true whichever strategy implements a copy.
