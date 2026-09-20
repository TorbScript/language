---
title: Coherence and blanket implementations
summary: A package may implement a trait for a type only if it owns the type or the trait, and two implementations of one trait may never overlap.
kind: reference
status: stable
order: 70
keywords:
  - coherence
  - orphan rule
  - overlap
  - blanket implementation
source:
  - CONCEPT.md#traits
---

Coherence is the rule that makes `Type.member` and `value.member()` mean one thing everywhere: for one trait and one
type, there is at most one implementation in the whole program, and every package can decide which one without
looking at the others.

## Example

```trb check
trait Loud {
  fn shout(self): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(self): String {
    "{name.toUpperCase()}!"
  }
}

print Robot("wall-e").shout()
```

## Syntax

```text
extend <Type> with <Trait> { ... }                          owns Type, or owns Trait, or both
extend<Source, Target> Source with <Trait><Target> where <bounds> { ... }   a blanket implementation, owns Trait
```

## Rules

1. **`extend X with Trait` needs your package to own `X` or `Trait`.** Owning means declaring it: `Robot` above is
   declared in the same package as the `extend`, so the implementation is coherent even though `Loud` could belong to
   anybody.

2. **Neither type nor trait being yours is an error, named exactly.** `Int64` is `std/number`'s and `Show` is
   `std/core`'s, so a third package can implement neither for the other.

   ```trb error
   extend UInt8 with Negate {
     fn negate(self): UInt8 {
       self
     }
   }
   // error: `extend UInt8 with Negate`: neither `UInt8` nor `Negate` belongs to this package
   ```

3. **Two implementations of one trait for the same type may never overlap, even inside one package.** A type that
   already has a trait from its declaration cannot get it again from an `extend`.

   ```trb error
   trait Loud {
     fn shout(self): String
   }

   type Robot with Loud {
     fn shout(self): String {
       "BEEP"
     }
   }

   extend Robot with Loud {
     fn shout(self): String {
       "BOOP"
     }
   }
   // error: `Robot` already implements `Loud`
   ```

4. **A blanket implementation's target is a bare type parameter**
   (`extend<Source, Target> Source with Into<Target> where Target: From<Source>`), which covers every type. It is
   coherent only when your package owns the trait, because no type is owned by you for it to fall back on.

5. **Disjointness is proved only by different target heads, never by bounds.** Two implementations of `Show` for
   `List<Item>` and for `Map<Key, Value>` are disjoint because `List` and `Map` are different heads. Two blanket
   implementations of one trait are never disjoint this way, so a package may write at most one blanket
   implementation of a trait it owns, however its `where` clause is written.

## What this is not

**Coherence is not "last import wins".** There is no shadowing between implementations: a second implementation of a
trait a type already has is a compile error at the `extend`, not a silent replacement.

```trb check
trait Loud {
  fn shout(self): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(self): String {
    "{name.toUpperCase()}!"
  }
}

print Robot("wall-e").shout()
```

```trb error
trait Loud {
  fn shout(self): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(self): String {
    "{name.toUpperCase()}!"
  }
}

extend Robot with Loud {
  fn shout(self): String {
    "BOOP"
  }
}
// error: `Robot` already implements `Loud`
```

The second `extend` is rejected at its own line, not silently ignored and not silently replacing the first - so there
is nothing to work around by writing the extensions in a particular order or a particular file.

## Related

- [extend](extend.md) - adding members afterwards, and what only the declaration can add.
- [Traits](traits.md) - `with` at the declaration, and coherence in one sentence.
- [Trait intersections](intersections.md) - combining traits in a type position, which is unrelated to owning one.
