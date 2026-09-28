---
title: Changing elements in place
summary: for var item in items binds each slot of a container in turn, so a change of item changes the container itself - over a List, an Array, a window, the values of a Map, and any type with MutableIndex.
kind: reference
status: stable
order: 65
keywords:
  - for var
  - in place
  - MutableIndex
  - keys
  - slot
  - iter_mut
source:
  - docs/design/COLLECTIONS.md
  - compiler/src/semantics/checker/slots.trb
  - compiler/src/ir/lower/statement.trb
---

The variable of a plain `for` is a copy of each item, so changing it changes nothing the collection holds.
`for var item in items` binds each **slot** of the container instead, one turn of the body at a time: a change of
`item` is a change of `items`.

## Example

```trb run
type Particle {
  var position: Float
  var velocity: Float
}

var particles = [Particle(0.0, 1.5), Particle(10.0, -2.0)]
for var particle in particles {
  particle.position = particle.position + particle.velocity
}
print particles // prints [Particle(position: 1.5, velocity: 1.5), Particle(position: 8.0, velocity: -2.0)]

var stock = ["apple": 3, "pear": 0]
for (name, var count) in stock {
  count = count + name.byteLength()
}
print stock // prints ["apple": 8, "pear": 4]
```

## Syntax

```text
for var <name> in <var path> { ... }
for (<key pattern>, var <name>) in <var path> { ... }

trait MutableIndex<Key, Value> with Index<Key, Value> {
  var fn set(key: Key, value: Value)
  fn keys(): Iterate<Key>
}
```

## Rules

1. **The container is a `var` path**: a `var` binding, a `var` parameter, `self` in a `var fn`, a path through `var`
   fields from one of those, an element `grid[row]` or a window `samples[1..4]` of one. A `const` or a temporary is
   refused, as every other change of one is.

   ```trb error
   const numbers = [1, 2, 3]
   for var number in numbers {
     number = number * 2
   }
   // error: `numbers` is a `const`. Only a `var` binding can be changed
   ```

2. **The slot is changed like a `var` parameter**: by assignment, through its fields, by a `var fn` and as the
   argument of another `var` parameter. It is a reference into the container, so it is never stored, returned or
   captured by a closure that may outlive the turn. Reading it reads the element, which is a copy like every read.

3. **The body reaches the container through the slot alone.** While a turn holds `numbers[key]` open, any other read
   or change of `numbers` is an exclusivity error - which is what guarantees that nothing grows, shrinks or rehashes
   the container under the loop.

   ```trb error
   var numbers = [1, 2, 3]
   for var number in numbers {
     numbers.append number
   }
   // error: `numbers` is being changed by `for var` right now
   ```

4. **A map keeps its keys.** The head is `(key, var value)`: the key is a `const` copy, and only the value changes.
   `for var entry in stock` is refused, because a key cannot change without moving its entry.

5. **A container without slots is refused.** A `Set` has none, because an element's place depends on its value; an
   `Iterate` (a pipeline, a range) has none at all.

   ```trb error
   var names: Set<String> = ["ada", "grace"]
   for var name in names {
     name = "{name}!"
   }
   // error: `for var` changes the slots of a container, and a `Set` has none: an element's place depends on its value
   ```

6. **`break`, `continue`, `return` and `?` mean what they mean in every `for`.** Every change was written where it
   happened, so leaving the loop keeps what the turns before changed and writes nothing back.

   ```trb run
   var numbers = [1, 2, -3, 4]
   for var number in numbers {
     if number < 0 {
       break
     }
     number = number * 10
   }
   print numbers // prints [10, 20, -3, 4]
   ```

7. **The keys come from `MutableIndex.keys()`, in its order.** A `List`, an `Array` and a window answer their
   indices, `0..length()`, and the loop counts through them without an iterator; a `Map` answers its keys in insertion
   order. A type of the program joins by implementing `MutableIndex`, which includes `keys()`:

   ```trb run
   type Pair with MutableIndex<Int, String> {
     var left: String
     var right: String

     fn get(key: Int): String? {
       match key {
         0 => left
         1 => right
         _ => None
       }
     }

     var fn set(key: Int, value: String) {
       if key == 0 {
         left = value
       } else {
         right = value
       }
     }

     fn keys(): Iterate<Int> {
       0..2
     }
   }

   var pair = Pair "a", "b"
   for (index, var side) in pair {
     side = "{side}{index}"
   }
   print "{pair.left} {pair.right}" // prints a0 b1
   ```

## What this is not

**It is not Swift's `for var`.** Swift binds a changeable *copy* of each item, and changing it changes nothing the
collection holds - exactly the copy trap of [`var` paths](../types/var-paths.md) written as a loop. In TorbScript the
slot is the element's own place, like Rust's `iter_mut()`.

**It is not a loop over a snapshot.** No copy of the container is made before the first turn; rule 3 is what makes
walking its keys sound.

## Related

- [Iterating](iterating.md) - the plain `for`, whose variable is a copy of each item.
- [`var` paths](../types/var-paths.md) - `items[index].field = value`, the access every use of the slot is.
- [Lists](lists.md) and [Maps and sets](maps-and-sets.md) - the containers with slots, and the `Set` without.
- The collections design - section 3.11, the decision and its rules.

