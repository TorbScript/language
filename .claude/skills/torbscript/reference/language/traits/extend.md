---
title: extend
summary: extend adds constants and functions to a type after its declaration, with a trait or without one, never adds a field or a case, and is named by the file that uses it when it targets a type of another package.
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

4. **An `extend` of a type from another package is named by the file that uses it**, by the path of the member - the
   same form a case import takes. `use Int64.seconds from "std/time"` makes `2.seconds()` work in that file and nowhere
   else. A generic target is named by its head (`use List.totalArea from "acme/shapes"`), and a constant or a static
   function of the `extend` is imported the same way. A member the file never names is a message that writes the line:

   ```text
   error: `std/time` adds `seconds` to `Int64`, and this file does not name it
     = Write `use Int64.seconds from "std/time"` at the top of the file
   ```

   An `extend` that this package wrote itself needs nothing, whichever of its files it stands in: a file sees what its
   own package declares.

5. **`as` renames an imported member, and that is how a conflict is resolved.**
   `use String.shout as yell from "acme/text"` makes it `"x".yell()`, and the declared name is no longer in this file.
   Two members of one name for one type stay an error at the *use*:

   ```text
   error: `shout` comes from `acme/one` and from `acme/two`
     = A member is renamed where it is imported: `use ... as another from "acme/two"`
   ```

6. **What a trait puts on a type it does not own is visible where the trait is.** `extend String with Slug` in the
   package of `Slug` adds `slug` to every `String`, but only a file that has `Slug` as a name - imported, from the
   prelude, declared here - can call it; a bound `Item: Slug` and a trait-typed value name it already. What the type's
   own package attaches (`type Circle with Shape`, `extend Circle with Shape` there) stays visible everywhere, which is
   what keeps the operators, `for`, interpolation, `?`/`??` and `into()` working without a single import.

7. **`extend` takes its own type parameters and its own `where` clause**, separate from the type it targets; see
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
- [use](../modules-and-packages/use.md) - every form of an import, including the path of a member.
