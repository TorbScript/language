---
title: Use a type as a map key
summary: An ordinary type is already a legal key once every field is Hash, which the compiler generates for free; a field that cannot be Hash is the one thing that rules a type out.
kind: how-to
status: stable
order: 90
keywords:
  - Map
  - Hash
  - Equals
  - map key
source:
  - CONCEPT.md#values
---

`Map<Key, Value>`'s implementations ask for `Key: Hash`, and `Hash` is generated for a `type` together with `Equals`
from the same fields, in the same order, whenever every field supports it. Most types qualify without a line written
for it; the ones that do not are ruled out by exactly one kind of field.

## Steps

1. **Use the type as a key directly, if every field is `Hash`.** `Int`, `String`, `Bool`, another `type` whose fields
   are all `Hash`, and a `case` whose fields are all `Hash` all qualify already.

   ```trb fragment
   type Point {
     x: Int
     y: Int
   }
   ```

2. **Check the one field that rules a type out: a function value.** A field of a function type has no `Equals` or
   `Hash` of its own, so a type that has one gets neither generated - move it out of the type that is used as a key,
   or key the map on the rest of the data instead.

3. **Reach for a tuple key when the key is several values that do not already live together in one type.** A tuple's
   `Equals` and `Hash` are generated the same way a `type`'s are, from its elements in order.

   ```trb fragment
   const cell: (Int, Int) = (3, 4)
   var seen: Map<(Int, Int), Bool> = [:]
   seen[cell] = true
   ```

4. **Write the map literal or call `Map.of` once the key type is settled.** Both infer `Key: Hash` from the values
   given, so nothing about the bound has to be spelled out at the call site.

   ```trb fragment
   const distances: Map<Point, Int> = [Point(0, 0): 0, Point(3, 4): 5]
   ```

## Pitfalls

- **A `shared type` is never a map key.** It has no generated `Equals` or `Hash` at all - it has an identity instead,
  and `isSame` compares that, not content. Key on a field of it, or on an identifier the shared type carries.
- **A field that holds a `List`, `Map` or `Set` still works, as long as its own item and key types are `Hash`.** A
  collection has a generated `Hash` exactly like a `type` does, so nesting is not by itself what rules out a key.
- **Two keys that are `==` must hash equally, and the compiler guarantees it by construction.** `Hash` is generated
  from the same fields `Equals` compares, in the same order, so there is nothing to keep in sync by hand.
- **Today's checker does not yet reject `Map<Key, Value>` when `Key` has a field that breaks `Hash`** - only a direct
  call of `.hash()` on such a value is rejected, as `hash()` was never generated. Writing a map keyed on a type with a
  function field is unspecified rather than caught early; the fields listed in step 1 are what to check by hand until
  it is.

## Full example

```trb check
type Point {
  x: Int
  y: Int
}

const distances: Map<Point, Int> = [Point(0, 0): 0, Point(3, 4): 5]
print distances[Point(3, 4)]

const cell: (Int, Int) = (1, 1)
var visited: Map<(Int, Int), Bool> = [:]
visited[cell] = true
print visited.containsKey((1, 1))
```

## Related

- [Copy and equality](../language/types/copy-and-equality.md) - exactly when `Equals` and `Hash` are generated.
- [Maps and sets](../language/collections-and-iteration/maps-and-sets.md) - the literal, insertion order, and the
  implementations `Key: Hash` chooses between.
- [Tuples](../language/values-and-types/tuples.md) - the generated members a tuple gets the same way a `type` does.
- [Shared types](../language/types/shared-types.md) - why one has no generated `Hash`, and `isSame` in its place.
