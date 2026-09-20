---
title: Sort by more than one key
summary: Sort by a tuple key instead of a single field - a tuple's Compare is generated lexicographically by position, which a type never gets because an order is a decision, not a structure.
kind: how-to
status: stable
order: 120
keywords:
  - sort
  - sorted
  - Compare
  - tuple key
source:
  - CONCEPT.md#values
---

`sort` and `sorted` both take one key selector, not a list of them. A tuple key is how several fields become one
key: a tuple has a generated `Compare` that orders by its elements in position order, so `(level, score)` already
compares the way "first by level, then by score" reads.

## Steps

1. **Build the key as a tuple, in the order you want to compare by.** `(player.level, player.score)` compares by
   `level` first and only looks at `score` where two players have the same `level`.

   ```trb fragment
   players.sort { player => (player.level, player.score) }
   ```

2. **Use `sort` to reorder in place, `sorted` to keep the original and get a copy.** Both take the same key selector;
   `sort` needs a `var` path to the list, `sorted` works on a `const` one and answers a new `Iterable`.

   ```trb fragment
   const ranked = players.sorted { player => (player.level, player.score) }
   ```

3. **Negate a numeric key to reverse its direction inside the tuple.** There is no per-key ascending/descending flag,
   so "level ascending, score descending" is the key `(player.level, -player.score)`.

   ```trb fragment
   const ranked = players.sorted { player => (player.level, -player.score) }
   ```

4. **Turn a `Bool` field into an `Int` for the key.** `Bool` has no `Compare`, so `player.eliminated` cannot stand in
   a tuple key directly - `if player.eliminated { 1 } else { 0 }` can.

   ```trb fragment
   players.sort { player => (if player.eliminated { 1 } else { 0 }, player.score) }
   ```

## Pitfalls

- **A `type` has no generated order.** The tuple is what supplies `Compare` here; declaring your own type for the key
  would need its own `Compare` written by hand, because an order is a decision a `type` never makes on its own.
- **`Bool` is not `Compare`.** A tuple key that includes a `Bool` field directly is rejected with "`Bool` does not
  implement `Compare`" - convert it to an `Int` first, as in step 4.
- **`sort` needs a `var` path to the whole list.** `players.sort { ... }` on a `const` binding is rejected the same
  way any other verb on a `const` path is; use `sorted` for a copy or make the binding `var`.
- **Every key in the tuple has to be `Compare`.** A field that is itself a `type` with no generated `Compare` cannot
  be a tuple key element directly - pull out the `Int`, `Float` or `String` that actually orders it.
- **The key closure runs again for every comparison, not once per item.** A key that is expensive to compute should
  be read into a separate list first and sorted alongside an index, rather than recomputed on every comparison inside
  `sort`.

## Full example

```trb check
type Player {
  name: String
  score: Int
  level: Int
}

const players = [
  Player("Ada", 10, 2),
  Player("Alan", 10, 1),
  Player("Grace", 20, 1),
]

const ranked = players.sorted { player => (player.level, -player.score) }
for player in ranked {
  print player
}
```

## Related

- [Copy and equality](../language/types/copy-and-equality.md) - why a `type` has no generated `Compare` the way it
  has a generated `Equals` and `Hash`.
- [Tuples](../language/values-and-types/tuples.md) - the generated `Compare`, lexicographic by position.
- [Lists](../language/collections-and-iteration/lists.md) - `sort` and `sorted` alongside every other verb/participle pair.
