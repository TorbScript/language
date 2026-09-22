---
title: Lists
summary: List is the ordered, indexable sequence behind the literal [1, 2, 3], with ArrayList as the default implementation and a verb paired with a participle for every change.
kind: reference
status: stable
order: 20
keywords:
  - List
  - ArrayList
  - TrieList
  - verb
  - participle
source:
  - std/collections/src/list.trb
  - CONCEPT.md#collections-and-iteration
---

`List<Item>` is an ordered sequence, addressable by index, and the type of the literal `[1, 2, 3]`. Every change has a
verb that changes the list in place and a participle that answers a changed copy, so the same operation is available
whether the binding is `var` or `const`.

## Example

```trb check
var numbers = [3, 1, 2]
numbers.append 4
numbers[0] = 5
const doubled = numbers.appended(6).sorted { value => value }.map { _ * 2 }.toList()

print numbers
print doubled
```

## Syntax

```text
[1, 2, 3]                                the literal: ArrayList<Int>
List.of(1, 2, 3)                         from arguments
List.of(...anyIterate)                   from a spread
List.from(anyIterate)                    from any Iterate
anyIterate.toList()                      from a pipeline
list[index]                              Indexed.at: panics if index is out of range
list.get(index)                          Indexed.get: an Option
list[index] = value                      MutableIndexed.set
```

## Rules

1. **A verb changes the list in place through a `var` path, and its participle answers a changed copy and works on a
   `const` list too.** `append`/`appended`, `insert`/`inserted`, `remove`/`removed`, `removeAt`/`removedAt`,
   `sort`/`sorted`, `reverse`/`reversed`. A participle is built from its verb once, as a default method of `List`:
   `appended` copies the list into a `var`, calls `append` on the copy and answers it.

2. **Reading `list[index]` panics if the index is out of range; `list.get(index)` answers an `Option` instead.**
   `[index]` is `Indexed.at`, which calls `get` and panics on `None`; `get` itself never panics.

   ```trb check
   const numbers = [1, 2, 3]
   print numbers.get(10)
   print numbers[0]
   ```

3. **`Equals`, `Hash` and `Show` are generated wherever `Item` supports them, and a `List` is order-dependent in all
   three.** Two lists with the same items in a different order are unequal, hash differently, and print differently -
   unlike [Maps and sets](maps-and-sets.md), which ignore insertion order for both.

4. **Creation goes through `List.of`, `List.from`, a literal, or `toList()` on any `Iterate`**, and every one of them
   answers the default implementation, `ArrayList`. `List.of(...items)` accepts a spread of any `Iterate`, and a
   nested list needs its own brackets: `List.of([1, 2], [3])` is a `List<List<Int>>`, because a variadic parameter
   never unpacks by itself.

5. **`first()`, `last()` and `indexOf(value)` read without changing anything**, and answer an `Option`: `first()` and
   `last()` are `None` for an empty list, `indexOf` is `None` when the value is not present.

6. **`ArrayList` is the default and is contiguous; `TrieList` is a bit-partitioned trie whose write to a shared list
   copies one path instead of the whole buffer.** Until the trie is implemented, `TrieList` is a documented alias of
   `ArrayList`: only performance differs, iteration order and `Show` are the same either way.

## What this is not

**A `List` has no `+`.** Combining two lists is `appendedAll`, a word that says which end the second list goes to,
not an operator.

```trb check
const numbers = [1, 2, 3]
const combined = numbers.appendedAll([4, 5])
print combined
```

```trb error
const numbers = [1, 2, 3]
const combined = numbers + [4, 5]
// error: `List<Int64>` does not implement `Add`, so `a + b` has no meaning for it
```

## Related

- [The collection traits](collection-traits.md) - `Iterate` and `Length`, which `List` is built on.
- [Slices](slices.md) - `list[from..to]`, a `List` again, as a value and as a `var` path.
- [Verbs and participles](../types/verbs-and-participles.md) - the naming rule behind `append`/`appended` and its siblings.
- [Mutation and var paths](../types/var-paths.md) - why a verb needs a `var` path and a participle does not.

