---
title: Coherence and blanket implementations
summary: A package may implement a trait for a type only if it owns the type, the trait, or a type named as an argument of the trait, and two implementations of one trait may never overlap.
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
  fn shout(): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(): String {
    "{name.toUpperCase()}!"
  }
}

print Robot("wall-e").shout()
```

## Syntax

```text
extend <Type> with <Trait> { ... }                          owns Type, or owns Trait
extend <Foreign> with <Trait><Mine> { ... }                  owns a type named as an argument of the trait
extend<Source, Target> Source with <Trait><Target> where <bounds> { ... }   a blanket implementation, owns Trait
```

## Rules

1. **`extend X with Trait` needs your package to own `X` or `Trait`.** Owning means declaring it: `Robot` above is
   declared in the same package as the `extend`, so the implementation is coherent even though `Loud` could belong to
   anybody.

2. **Or a type of yours named as an argument of the trait.** `extend Float64 with From<Celsius>` is coherent in the
   package that declares `Celsius`: neither `Float64` nor `From` is yours, but `Celsius` is, and this is how a package
   converts **its own type into a foreign one**. The implementation still has exactly one possible author - the owner
   of `Celsius` reaches it this way, the owners of `Float64` and of `From` through rule 1, and a fourth package owns
   none of the three.

   ```trb check
   type Celsius {
     degrees: Float
   }

   extend Float64 with From<Celsius> {
     static fn from(value: Celsius): Float64 {
       value.degrees
     }
   }

   const degrees: Float64 = Float64.from Celsius(21.5)
   print degrees
   ```

   Only the **top level** of an argument counts, and only a named type. `From<List<Celsius>>` is not yours: an
   argument that merely contains an owned type somewhere inside would let one package claim an implementation through
   a nesting another package wrote.

3. **None of the three being yours is an error, named exactly.** `Int64` is `std/number`'s and `Show` is
   `std/core`'s, so a third package can implement neither for the other.

   ```trb error
   extend UInt8 with Negate {
     fn negate(): UInt8 {
       self
     }
   }
   // error: `extend UInt8 with Negate`: neither `UInt8` nor `Negate` belongs to this package
   ```

   Where the trait has arguments, the note names the third way in as well:

   ```trb error
   extend Float64 with From<Int8> {
     static fn from(value: Int8): Float64 {
       1.0
     }
   }
   // error: `extend Float64 with From<Int8>`: neither `Float64` nor `From<Int8>` belongs to this package
   ```

4. **Two implementations of one trait for the same type may never overlap, even inside one package.** A type that
   already has a trait from its declaration cannot get it again from an `extend`.

   ```trb error
   trait Loud {
     fn shout(): String
   }

   type Robot with Loud {
     fn shout(): String {
       "BEEP"
     }
   }

   extend Robot with Loud {
     fn shout(): String {
       "BOOP"
     }
   }
   // error: `Robot` already implements `Loud`
   ```

   An implementation for a **trait** covers every type of that trait, so it overlaps with the one such a type writes
   itself: a `Square` would answer `describe` with its own member, and the same value seen as a `Shape` with the
   extension's - what a call does would depend on the static type it is made through. The type's own stays the answer,
   and the message stands at the `extend`.

   ```trb error
   trait Shape {
     fn area(): Float
   }

   trait Describe {
     fn describe(): String
   }

   extend Shape with Describe {
     fn describe(): String {
       "some shape"
     }
   }

   type Square with Shape, Describe {
     side: Float

     fn area(): Float {
       side * side
     }

     fn describe(): String {
       "a square"
     }
   }
   // error: `Square` implements `Describe` itself, and it is a `Shape`
   ```

5. **A blanket implementation's target is a bare type parameter**
   (`extend<Source, Target> Source with Into<Target> where Target: From<Source>`), which covers every type. It is
   coherent only when your package owns the trait: it covers every type, so no owned type and no owned argument narrows
   it to a line only you could write.

6. **Disjointness is proved only by different target heads, never by bounds.** Two implementations of `Show` for
   `List<Item>` and for `Map<Key, Value>` are disjoint because `List` and `Map` are different heads. Two blanket
   implementations of one trait are never disjoint this way, so a package may write at most one blanket
   implementation of a trait it owns, however its `where` clause is written.

7. **A trait that has a blanket implementation is not implemented by hand, and the message says which one to write
   instead.** `TryInto` comes from `TryFrom` for every type, so writing it directly overlaps the blanket, and the note
   is built from the blanket's own `where` clause with your types put in. `Into` is a rule of its own: an
   implementation of it anywhere but in its one blanket is refused whatever it overlaps, with the `From` to write -
   `From<Source>` for the target, which coherence allows wherever it would allow the `Into`, because the package of
   the source may name it as the argument of `From` (rule 4).

   ```trb error
   type Celsius {
     degrees: Float
   }

   extend Celsius with Into<Float64> {
     fn into(): Float64 {
       degrees
     }
   }
   // error: Implement `From<Celsius>` for `Float64` instead; `Into` comes from it
   ```

8. **One trait may stand twice in a `with` list, and then each instantiation needs a body of its own.** Two
   instantiations do not overlap - `Multiply<Board, Board>` beside `Multiply<Int, Board>` is how a type overloads over
   a trait argument - but a type has one namespace of members, so the body the type writes is the implementation of
   one of them and of no other. The form that has a body per implementation is `extend`.

   ```trb error
   trait Draw<Surface> {
     fn draw(on: Surface): Int
   }

   type Paper {
     weight: Int
   }

   type Screen {
     pixels: Int
   }

   type Sketch with Draw<Paper>, Draw<Screen> {
     ink: Int

     fn draw(on: Paper): Int {
       ink
     }
   }
   // error: `Sketch` comes with `Draw` more than once, and `draw` is the body of the first one
   ```

   ```trb check
   trait Draw<Surface> {
     fn draw(on: Surface): Int
   }

   type Paper {
     weight: Int
   }

   type Screen {
     pixels: Int
   }

   type Sketch with Draw<Paper> {
     ink: Int

     fn draw(on: Paper): Int {
       ink
     }
   }

   extend Sketch with Draw<Screen> {
     fn draw(on: Screen): Int {
       ink * 2
     }
   }

   print Sketch(1).ink
   ```

9. **Coherence decides which implementation exists; the file decides which member names it can write.** An
   implementation is unique in the program either way, and nothing about dispatch depends on an import - but the members
   a trait puts on a type it does not own are only *nameable* where the trait itself is a name of the file, and a
   trait-less `extend` of a foreign type is nameable only where the file imported the member. See
   [extend](extend.md) for both halves.

## What this is not

**Coherence is not "last import wins".** There is no shadowing between implementations: a second implementation of a
trait a type already has is a compile error at the `extend`, not a silent replacement.

**And visibility is not coherence.** A member that this file cannot name still exists, is still the one implementation
in the program, and is still what a generic function with the right bound calls. The import decides what *this* file may
write, never what the program means.

```trb check
trait Loud {
  fn shout(): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(): String {
    "{name.toUpperCase()}!"
  }
}

print Robot("wall-e").shout()
```

```trb error
trait Loud {
  fn shout(): String
}

type Robot {
  name: String
}

extend Robot with Loud {
  fn shout(): String {
    "{name.toUpperCase()}!"
  }
}

extend Robot with Loud {
  fn shout(): String {
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

