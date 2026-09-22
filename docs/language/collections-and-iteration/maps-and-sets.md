---
title: Maps and sets
summary: "The literal [\"a\": 1] builds a Map, a Set is built from a list literal instead of having one of its own, and both iterate in insertion order while comparing regardless of it."
kind: reference
status: stable
order: 30
keywords:
  - Map
  - Set
  - TrieMap
  - HashMap
  - insertion order
source:
  - std/collections/src/map.trb
  - CONCEPT.md#collections-and-iteration
---

`Map<Key, Value>` maps keys to values and is the type of the literal `["a": 1]`. `Set<Item>` is a collection without
duplicates and has no literal of its own: a list literal builds one wherever a `Set` is the expected type. Both
implementations iterate in the order values were inserted and compare equal regardless of it.

## Example

```trb check
var ages = ["Ada": 36]
ages["Grace"] = 45
print ages
print ages.keys().toList()

var unique: Set<Int> = [1, 2, 2, 3]
unique.insert 2
print unique
```

## Syntax

```text
["a": 1, "b": 2]                         the Map literal: TrieMap<String, Int>
[:]                                      the empty Map
map[key]                                 Indexed.at: panics if the key is absent
map.get(key)                             Indexed.get: an Option
map[key] = value                         MutableIndexed.set

const unique: Set<Int> = [1, 2, 2, 3]    a list literal, adapted because Set is From<Iterate<Item>>
Set.of(1, 2, 3)                          from arguments, needs Item: Hash
```

## Rules

1. **A `Map` has a literal of its own; a `Set` does not.** `["a": 1]` is native syntax that builds a `TrieMap` through
   `Map`'s own `From<Iterate<(Key, Value)>>`. A `Set` is built the way every other target is: a list literal adapts
   to the type expected of it, so `const unique: Set<Int> = [1, 2, 2, 3]` is `Set.from([1, 2, 2, 3])`.

2. **Every implementation of `Map` and `Set` iterates in insertion order, and removing a value does not reorder the
   rest.** The order reaches the output through `Show`: an empty `Map` shows as `[:]`, an empty `Set` as `{}` - which
   is why the order is a rule of the language and not of the implementation.

   **A `Map` iterates as a labelled pair:** `Map.iterate` answers `Iterator<(key: Key, value: Value)>`, so an entry
   reads as `entry.key` and `entry.value`. A label is not part of the type, so the positional forms mean exactly the
   same thing and a `(Key, Value)` pair goes into `Map.from` unchanged.

   ```trb check
   const ages = ["Ada": 36, "Alan": 41]
   print ages.all({ entry => entry.value > 0 })
   for (name, age) in ages {
     print "{name} is {age}"
   }
   ```

3. **Equality and hashing ignore insertion order, unlike a `List`.** Two maps with the same entries set in a
   different order are equal and hash the same, because both combine their entries with `bitwiseExclusiveOr` instead
   of folding them in sequence.

   ```trb check
   const first: Map<String, Int> = ["a": 1, "b": 2]
   const second: Map<String, Int> = ["b": 2, "a": 1]
   print(first == second)
   ```

4. **The traits ask nothing of `Key` or `Item`; only a factory or an implementation that hashes it does.** `Map.of`,
   `Set.of`, `HashMap`, `HashSet`, `TrieMap` and `TrieSet` all need `Hash`; a hand-written implementation that never
   hashes its keys would not.

5. **A `Map`'s verb for writing a key is `set`, and its participle is `updated` rather than `setted`.** `remove`/
   `removed` follows the usual pattern, and putting one map into another is a `for` over its entries with `set`. A
   `Set`'s in-place verbs are `insert`/`remove`/`insertAll`/`removeAll`/`retainAll`, with the participles
   `inserted`, `insertedAll` and `removed`; its set operations - `union`, `intersection`, `difference` - are nouns
   and never change either operand.

   ```trb check
   const ages: Map<String, Int> = ["Ada": 36]
   const older = ages.updated "Ada", 37
   print older

   const primes = Set.of 2, 3, 5, 7
   const evenPrimes = primes.intersection Set.of(2, 4, 6)
   print evenPrimes
   ```

6. **`containsKey`, `keys()`, `values()` and `mapValues` read a `Map` without changing it.**

7. **`getOrInsert(key, fallback)` reads the value, storing the fallback where there is none, and `update` changes the
   stored value in place.** The fallback is `lazy`, so it is only built on a miss - this is what other languages spell
   `entry().or_default()`, with the replacement written out because there is no `Default` trait. What `getOrInsert`
   answers is a **copy**, as every value is, so the grouping loop goes through `update`:

   ```trb check
   var groups: Map<String, List<String>> = [:]
   for name in ["Ada", "Alan", "Grace"] {
     groups.update name[0..1], [] { names => names.append name }
   }
   print groups
   ```

## What this is not

**Reading `map[key]` is not the same as `map.get(key)`.** The bracket form panics on a missing key; only `get`
answers `None`.

```trb check
const ages: Map<String, Int> = ["Ada": 36]
print ages.get("Linus")
```

```trb skip a missing key panics at runtime, which this documentation's gate does not execute
const ages: Map<String, Int> = ["Ada": 36]
print ages["Linus"]
```

## Related

- [The collection traits](collection-traits.md) - `Iterate` and `Length`, which `Map` and `Set` are built on.
- [Lists](lists.md) - the order-dependent collection `Map` and `Set` are compared against.
- [Copy and equality](../types/copy-and-equality.md) - what the generated `Equals` and `Hash` do for a field that
  holds one of these.
- [Tuples](../values-and-types/tuples.md) - the `(Key, Value)` pair a `Map` iterates as.
