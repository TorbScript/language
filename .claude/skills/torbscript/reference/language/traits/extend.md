---
title: extend
summary: extend adds constants and functions to a type after its declaration, with a trait or without one, and never adds a field or a case.
kind: reference
status: stable
order: 20
keywords:
  - extend
  - extension method
  - implement
  - monkey patch
source:
  - CONCEPT.md#traits
  - examples/tour/src/05-traits.trb
---

`extend` adds members to a type after its declaration. With a trait, it is how a type gains a capability it was not
declared `with`; without one, it is a plain extension method. Either form can target a type of another package, which
is the only way to give somebody else's type new behaviour.

## Example

```trb check
trait Shape {
  fn area(self): Float
}

type Circle {
  radius: Float
}

extend Circle with Shape {
  fn area(self): Float {
    Float.pi * radius * radius
  }
}

extend String {
  fn shout(self): String {
    "{toUpperCase()}!"
  }
}

const circle = Circle 2.0
print "{circle.area()} {"hello".shout()}"
```

## Syntax

```text
extend <Name> with <Trait> { <constant or function>* }
extend <Name> { <constant or function>* }
extend<<parameters>> <Name><<arguments>> [with <Trait>] [where <bounds>] { ... }
```

## Rules

1. **`extend` adds constants and functions, and nothing else.** A field or a `case` belongs to the declaration of the
   type, because exhaustiveness and the generated constructor have to be decidable from it alone.

   ```trb error
   type Point {
     x: Float
     y: Float
   }

   extend Point {
     z: Float
   }
   // error: An `extend` cannot add a field
   ```

2. **`extend X with Trait` needs your package to own `X` or `Trait`.** This is the same rule a trait declaration
   follows; see [Coherence](coherence.md) for what "own" means and what happens otherwise.

3. **An `extend` of your own type is part of the type, wherever it is written.** It is visible everywhere the type is,
   in every file of the package, exactly as if it had been written inside the declaration.

4. **An `extend` of a type from another package is visible only where its module is imported**, no matter what that
   import names. `use "./text-extensions"` with no names imports a module purely for the extensions it adds.

5. **Two imported modules that add a member of the same name to the same type make calling it an error**, until a
   namespace import says which one is meant: `use * as text from "./text-extensions"`, then `text.shout(value)`.

6. **`extend` takes its own type parameters and its own `where` clause**, separate from the type it targets; see
   [Type parameters](../generics/type-parameters.md) for where else they can stand. A condition on an `extend`
   applies only to the types that satisfy it.

   ```trb check
   trait Loud {
     fn shout(self): String
   }

   type Box<Item> {
     item: Item
   }

   extend<Item> Box<Item> with Loud where Item: Show {
     fn shout(self): String {
       "Box({item})"
     }
   }

   print Box(5).shout()
   ```

## What this is not

**`extend` is not a monkey patch.** For a type of your own package it behaves exactly as if it had been written
inside the declaration - nothing is patched at runtime, and the compiler sees one type with all of its members from
the start.

```trb check
extend String {
  fn shout(self): String {
    "{toUpperCase()}!"
  }
}

print "hi".shout()
```

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

The second example fails for a different reason than a monkey patch would: `Robot` already has `Loud` from its
declaration, and two implementations of one trait may never overlap, even inside one package. See
[Coherence](coherence.md).

## Related

- [Traits](traits.md) - what a trait is, and `with` at the declaration.
- [Coherence and blanket implementations](coherence.md) - which package may write an `extend`.
- [Declaring a type](../types/declaring-a-type.md) - fields and cases, which only the declaration can add.
