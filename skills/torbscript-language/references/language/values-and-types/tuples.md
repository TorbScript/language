---
title: Tuples
summary: A tuple is positional and accessed by .0, .1; a label makes a position easier to read but is not part of the type, so a labelled and an unlabelled tuple of the same shape are the same type.
kind: reference
status: stable
order: 15
keywords:
  - labelled tuple
  - positional access
  - tuple type
source:
  - CONCEPT.md#built-in-types
---

A tuple groups values without declaring a type for them. `(Int, String)` is two values with no names;
`(lowest: Int, highest: Int)` is the same shape with a label on each position, for the reader rather than for the
type checker.

## Example

```trb check
const point = (3, "three")
print point.0
print point.1

const bounds = (lowest: 1, highest: 9)
print bounds.lowest
print bounds.0
```

## Syntax

```text
(Int, String)                            a tuple type, two positions
(lowest: Int, highest: Int)              the same shape, with a label on each position
(1, "one")                               a tuple value
(lowest: 1, highest: 9)                  a labelled tuple value
value.0  value.1                         positional access, by index
value.lowest                             access by label
```

## Rules

1. **A tuple is accessed by position: `.0`, `.1`, up to the last position it has.** A label, when there is one, is a second name for the
   same position - `bounds.0` and `bounds.lowest` above read the same value.

2. **A label is not part of a tuple's type.** `(lowest: Int, highest: Int)`, `(highest: Int, lowest: Int)` and
   `(Int, Int)` are one type; a value of any of these three spellings is accepted wherever the type is written in any
   of the other two.

   ```trb check
   fn widened(value: (Int, Int)): Int {
     value.0 + value.1
   }

   print widened((lowest: 2, highest: 5))
   ```

3. **A label at a position that already has a different label is an error, not a silent rename.** Writing a tuple
   literal against an expected type with labels checks that a given label names the position it is actually at.

   ```trb error
   fn bounds(): (lowest: Int, highest: Int) {
     (highest: 9, lowest: 1)
   }

   print bounds()
   // error: This position is `lowest`, not `highest`
   // error: This position is `highest`, not `lowest`
   ```

4. **A position is not a name.** `.0` and `.1` always stand behind a dot - `entry.0`, `_.0`, `self.0` - and a bare `0`
   is always the number. So a closure over a tuple writes `{ _.0 * 2 }` and never `{ 0 * 2 }`, and inside a method a
   position of `self` is `self.0`, not `0`: a receiver makes the *members* of a type implicit, and a position is not one.

   ```trb check
   const pairs = [(1, "one"), (2, "two")]
   print pairs.map({ _.0 }).toList()
   ```

5. **A labelled tuple is how a pair gets names without a type.** `Map.iterate` answers
   `Iterator<(key: Key, value: Value)>`, so an entry reads as `entry.key` and `entry.value`. The labels cost nothing -
   they are not part of the type - and the positional forms keep working.

   ```trb check
   const ages = ["Ada": 36, "Alan": 41]
   print ages.all({ entry => entry.value > 0 })
   for (name, age) in ages {
     print "{name} is {age}"
   }
   ```

6. **Tuples compare and print structurally.** Two tuples are equal when every position is, and a tuple's `Show` text
   lists its positions in order, without labels: `(lowest: 1, highest: 9)` shows as `(1, 9)`, because a label is not
   part of the type and so is not part of what identifies the value.

   ```trb check
   const bounds = (lowest: 1, highest: 9)
   print bounds
   ```

## What this is not

**Reordering a tuple's labels is not the same as reordering its positions.** A labelled tuple is still positional
underneath, so writing the labels in a different order in a literal does not permute the values - it is checked
against the position the label names, and rule 3's error is exactly what happens when a label lands on the wrong one.

```trb check
fn bounds(): (lowest: Int, highest: Int) {
  (lowest: 1, highest: 9)
}

print bounds()
```

```trb error
fn bounds(): (lowest: Int, highest: Int) {
  (highest: 9, lowest: 1)
}
// error: This position is `lowest`, not `highest`
// error: This position is `highest`, not `lowest`
```

## Related

- [Built-in types](built-in-types.md) - tuples next to every other type that needs no import.
- [Bindings](bindings.md) - `const (a, b) = pair`, the pattern a binding shares with `match`.
- [Cases and match](../pattern-matching/cases-and-match.md) - `(a, b)` as a pattern inside a `match`.
- [Closures](../functions/closures.md) - `_`, `_2` and why a bare name in a closure is never a position.

