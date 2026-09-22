---
title: Trait intersections
summary: The & operator combines two or more traits into one type, in a parameter, a field or a bound, and only traits can be combined this way.
kind: reference
status: stable
order: 50
keywords:
  - intersection
  - "&"
  - trait combination
source:
  - CONCEPT.md#traits
  - examples/tour/src/12-type-system.trb
---

`&` combines several traits into one type: a value of type `Show & Encode` is one that implements both. It is the
same `&` in a parameter's type, in a field's type, and in a bound.

## Example

```trb check
trait Loud {
  fn shout(): String
}

trait Named {
  fn name(): String
}

type Robot with Loud, Named {
  robotName: String

  fn shout(): String {
    "BEEP"
  }

  fn name(): String {
    robotName
  }
}

fn announce(entry: Loud & Named) {
  print "{entry.name()}: {entry.shout()}"
}

announce Robot("wall-e")
```

## Syntax

```text
fn <name>(<param>: <Trait> & <Trait>)          a parameter's type
<field>: <Trait> & <Trait>                     a field's type
where <Item>: <Trait> & <Trait>                a bound
```

## Rules

1. **`&` combines two or more traits into a single type.** `Loud & Named` is a type any value can coerce to if its
   own type implements both `Loud` and `Named`.

2. **Only traits can be combined with `&`.** Two different types have no values in common, so an intersection of
   types would always be empty; the compiler names both offenders.

   ```trb error
   fn combine(value: Int & String) {
     print value
   }
   // error: `Int64` is a type, not a trait
   // error: `String` is a type, not a trait
   ```

3. **An intersection is a trait type like any other**, so it stands wherever a trait type does: a parameter, a
   field, a type argument (`List<Show & Hash>`), or a bound (`where Item: Compare & Show`).

4. **A value coerces to an intersection only where every combined trait is implemented.** A type with only one of
   the combined traits does not coerce, with the same `does not implement` diagnostic a single missing trait gives:

   ```trb error
   trait Loud {
     fn shout(): String
   }

   trait Named {
     fn name(): String
   }

   type Robot with Loud {
     fn shout(): String {
       "BEEP"
     }
   }

   fn announce(entry: Loud & Named) {
     print entry.shout()
   }

   announce Robot()
   // error: `Robot` does not implement `Named`
   ```

5. **Object safety is checked per member, across every trait in the intersection.** A member of `Named` that
   mentions `Self` is still rejected on a `Loud & Named` value, exactly as it would be rejected on a bare `Named`
   value. See [Object safety](object-safety.md).

## What this is not

**`&` is not `,`.** A `with` list at a declaration separates several traits a type provides
(`type Robot with Loud, Named`); `&` builds one type out of several traits, for a position that only takes one type.

```trb fragment
type Robot with Loud, Named {
  robotName: String

  fn shout(): String {
    "BEEP"
  }

  fn name(): String {
    robotName
  }
}
```

```trb error
trait Loud {
  fn shout(): String
}

trait Named {
  fn name(): String
}

fn announce(entry: Loud, Named) {
}
// error: The parameter `Named` needs a type
// error: A parameter starts with a lowercase letter: write `named`
```

`fn announce(entry: Loud, Named)` parses as two parameters, the second one missing its name - not as one parameter
of an intersection type. `Loud & Named` is the only way to ask for both on one value.

## Related

- [Traits as types](trait-types.md) - a single trait used as a type, before combining several.
- [Bounds](../generics/bounds.md) - the same `&` restricting a type parameter.
- [Object safety](object-safety.md) - what stays uncallable on an intersection.

