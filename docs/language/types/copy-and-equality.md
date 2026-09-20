---
title: Copy and equality
summary: Assigning, passing or capturing a value copies it, and Equals, Hash and copy are generated for a type without being written, each only if every field supports it.
kind: reference
status: stable
order: 40
keywords:
  - copy
  - Equals
  - Hash
  - structural equality
source:
  - CONCEPT.md#values
  - std/core/src/compare.trb
---

A `type` has no identity: assigning it, passing it as an argument and capturing it in a closure all produce a copy, so
two bindings never see the same storage change. `Equals`, `Hash` and `copy` follow from that without a line of code.

## Example

```trb
type Point {
  var x: Int
  var y: Int
}

const a = Point x: 1, y: 2
var b = a                      // A copy: changing `b` never changes `a`
b.x = 9

print(a == b)                   // false: `Equals` is generated and structural
print(a == Point(x: 1, y: 2))   // true
print a.hash()                  // Hash is generated too
print a.copy(y: 5)               // Point(x: 1, y: 5)
```

## Syntax

```text
value.equals(other)      // ==
value.hash()
value.copy(<field>: <value>, ...)
```

## Rules

1. **Assigning, passing or capturing a value copies it.** `var b = a` gives `b` its own storage; nothing done to `b`
   afterwards is visible through `a`. This is what makes `const` on a `Point`, a `List` or a `Map` mean the value
   never changes, not just that the binding cannot be reassigned.

2. **`Equals`, `Hash` and `copy` are generated for every `type`, each only if every field supports it.** `==` always
   compares content, because there is no identity to compare instead. A field that is a function breaks all three,
   because a function value has none of them - calling `.equals()` directly on such a value is rejected, because the
   member was never generated.

   ```trb error
   type Button {
     label: String
     onClick: () => Void
   }

   const a = Button label: "OK", onClick: { print "clicked" }
   const b = Button label: "OK", onClick: { print "clicked" }
   print a.equals(b)
   // error: `Button` has no member `equals`
   ```

   `a == b` is rejected too, for the same reason: `==` asks for `Equals` like any other operator asks for its trait.

   ```trb error
   type Button {
     label: String
     onClick: () => Void
   }

   const a = Button label: "OK", onClick: { print "clicked" }
   const b = Button label: "OK", onClick: { print "clicked" }
   print(a == b)
   // error: `Button` does not implement `Equals`, so `a == b` has no meaning for it
   ```

3. **`copy` has the shape of the constructor, with every field optional.** `a.copy(y: 5)` answers a new value with `y`
   changed and every other field kept; a `private` field cannot be passed to `copy` from outside, for the same reason
   it cannot be passed to the constructor.

4. **Values that are equal must have equal hashes.** `Hash` is generated together with `Equals`, from the same fields
   in the same order, so the contract holds automatically instead of being an obligation on the writer.

5. **What copying costs is not part of the language.** A small value is copied directly; the storage behind a `List`
   or a `String` is shared until something writes to it. Both give the same answer to `==` and to every rule above, so
   nothing here depends on which one the implementation chose.

## What this is not

**Structural equality is not identity, and there is no way to ask for identity on a value.** Two values that hold the
same data are `==`, however they were constructed - there is no separate "same object" question for a `type`, because
a `type` never has one.

```trb
const first = Point x: 1, y: 2
const second = Point x: 1, y: 2
print(first == second)          // true: same content
```

```trb error
type Point {
  var x: Int
  var y: Int
}

const first = Point x: 1, y: 2
const second = Point x: 1, y: 2
print isSame(first, second)
// error: `isSame` compares identity, and a `Point` is a value
```

## Related

- [Declaring a type](declaring-a-type.md) - where `Equals`, `Hash`, `Show` and `copy` are introduced.
- [Construction](construction.md) - the constructor `copy` mirrors.
- [The generated Show](generated-show.md) - the other member every type gets for free.
- [Shared types](shared-types.md) - where identity replaces `Equals`, `Hash` and `copy` instead.
