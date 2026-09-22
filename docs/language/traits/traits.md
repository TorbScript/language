---
title: Traits
summary: A trait is a capability a type comes with. A single-method trait is named after its method, there is no inheritance, and operators are traits.
kind: reference
status: stable
order: 10
keywords:
  - trait
  - with
  - extend
  - supertrait
  - intersection
  - delegation
source:
  - CONCEPT.md#traits
  - std/core/src/compare.trb
  - examples/tour/src/05-traits.trb
---

A trait is a list of members a type provides. A type comes `with` a trait at its declaration, or gains it afterwards
through `extend`. There is no inheritance, no base class and no `implements` keyword: `with` is the only word for it.

## Example

```trb
trait Area {
  fn area(): Float

  fn describe(): String {
    "a shape with area {area()}"
  }
}

type Square with Area {
  side: Float

  fn area(): Float {
    side * side
  }
}

extend Square with Show {
  fn show(): String {
    "Square({side})"
  }
}

const square = Square 2.0
print "{square} {square.describe()}"
```

## Syntax

```text
[public] trait <Name>[<parameters>] [with <supertraits>] {
  fn <name>(...): <Type>                    a requirement
  fn <name>(...): <Type> { ... }            a default member
  var fn <name>(...): <Type>                a requirement that changes the receiver
}

type <Name> with <Trait>, <Trait> & <Trait> by <field> { ... }
extend <Name> with <Trait> { ... }
extend <Name> { ... }
extend<Item> List<Item> with <Trait> where Item: <Bound> { ... }

fn draw(shape: Area)                        a trait as a type
fn audit(entry: Show & Encode)              an intersection of traits
fn sum<Item: Add>(values: List<Item>)       a bound
```

## Rules

1. **A trait with one required method is named after that method.** `Hash` has `hash`, `Equals` has `equals`, `Compare`
   has `compare`, `Show` has `show`, `Add` has `add`, `Close` has `close`, `Iterate` has `iterate`. There are no `-able` adjectives: `type Money
   with Equals, Hash, Compare` reads as what it is. A trait that is mainly used *as a type* is a noun instead: `Iterator`,
   `Accumulator`, `Source`, `Sink`. `Iterate` has one required method, `iterate`, and is named after it.

2. **`with` is the only keyword for "implements" and for supertraits.** `trait Compare with Equals` says that every type
   with `Compare` also has `Equals`.

3. **A member without a body is a requirement; a member with a body is a default.** A default written in terms of the
   required members stays correct for every implementor.

4. **A bound is written `where Item: Hash & Equals` or inline as `<Item: Hash>`.** A member may carry a `where` clause of
   its own, and then the member exists only where the clause holds - it is not a requirement on implementors.

   ```trb fragment
   fn toSet(): Set<Item> where Item: Hash
   ```

5. **`extend` adds constants and functions, and nothing else.** A field or a `case` in an `extend` is an error, because
   exhaustiveness and the generated constructor have to be decidable from the declaration alone.

   ```trb
   extend String {
     fn shout(): String {
       "{toUpperCase()}!"
     }
   }

   print "hello".shout()
   ```

6. **Coherence: you may write `extend X with Trait` only if your package owns `X` or owns `Trait`.** Two implementations
   of one trait may never overlap.

7. **`extend` without a trait is part of the type** when the type belongs to your package - or to this one - in
   whatever file it is written. For a type of another package the file that uses the member names it,
   `use String.shout from "acme/text"`, the same form a case import takes; and what a *trait* puts on a type it does not
   own is visible where the trait itself is a name of the file. See [extend](extend.md) for both halves.

8. **A trait can be used as a type.** Whether the call is dispatched statically or dynamically is the implementation's
   business and is not observable. `&` intersects traits in a type position, and only traits can be combined - `&` is to
   traits what `|` is to literal types.

9. **Coercion to a trait type is the only subtyping in the language**, and there is no variance. A `List<Square>` is not a
   `List<Shape>`; the list is built as one:

   ```trb
   trait Area {
     fn area(): Float
   }

   type Square with Area {
     side: Float

     fn area(): Float {
       side * side
     }
   }

   const shapes: List<Area> = [Square(2.0), Square(3.0)]
   print shapes.length()
   ```

10. **Object safety is checked per call, not per type.** A member that mentions `Self` in a parameter or in its result, or
    that is `static`, cannot be called on a trait-typed value - but the type stays usable as a type, so
    `List<Show & Hash>` is legal and only the calls that have no meaning are rejected.

11. **Operators are traits.** `+` is `Add.add`, `-` is `Subtract.subtract`, `==` is `Equals.equals`, `<` is
    `Compare.compare`, `a[i]` is `Indexed.at`, `a[i] = v` is `MutableIndexed.set`, `a[from..to]` is `Slice.slice`, and
    string interpolation is `Show.show`. `&&`, `||` and `!` are the exception: they are built into `Bool`, they
    short-circuit, and they cannot be overloaded.

12. **A type parameter of a `type` or a `trait` may have a default** (`trait Add<Other = Self, Output = Self>`), which is
    why `with Add` means `Add<Self, Self>` and nobody writes it out. A `fn` has no such defaults: its type arguments come
    from the call.

13. **One type may carry a trait with a parameter several times, and each of them is one `extend`.** That is the
    overload form the language has ([Where are my overloads](../../explanation/where-are-my-overloads.md)), and which
    one a call means is decided by **everything the call says**: every argument that is not a closure, and the type the
    call is expected to produce. Two bodies of two instantiations cannot stand in one `type` body, because a type has
    one namespace of members.

    ```trb check
    trait Store<Component> {
      fn valueOf(): Component?
      fn attach(value: Component): Int
    }

    type Game {
      number: Int
      text: String
    }

    extend Game with Store<Int> {
      fn valueOf(): Int? {
        Some number
      }

      fn attach(value: Int): Int {
        value
      }
    }

    extend Game with Store<String> {
      fn valueOf(): String? {
        Some text
      }

      fn attach(value: String): Int {
        value.byteLength()
      }
    }

    const game = Game 1, "one"
    const named: String? = game.valueOf()
    print "{game.attach(7)} {named}"
    ```

    Where nothing in the call tells them apart, the checker names them instead of taking the first:
    `` `valueOf` fits more than one implementation here ``.

14. **`by` delegates a trait to the one field of a single-field type**, and it binds to the element of the `with` list
    directly in front of it - which may be an `&` group - never to the whole list.

    ```trb
    type Seconds with Show, Add & Subtract by value, Compare by value {
      value: Int
    }

    const total = Seconds(5) + Seconds(2)
    print total
    ```

    `Show` is derived here; `Add`, `Subtract` and `Compare` are forwarded to `value`. A trait that is neither derived,
    delegated nor written by hand is not available, which is the point: `Seconds * Seconds` does not compile, because that
    would be square seconds.

15. **`by` needs a type with exactly one field**, and the name after `by` names that field. A type with two fields cannot
    delegate `Add` to either one, and a type with cases has no field of its own to name.

16. **`Trait.member` reads the trait's own members first, then the members of implementations whose target is the trait
    itself.** That is what makes `List.of(1, 2)` and `List.from(iterable)` work. Two such implementations are an ambiguity
    error, and the fix is to name a type.

17. **A `shared type` can only implement a `shared trait`.** So a value of a trait type is always a value: nobody changes
    it while you hold it, and it can be passed to another task.

## What this is not

**A trait is not an interface with inheritance.** There is no base class, no `super`, no overriding of a concrete
implementation. A type either provides a member or takes the trait's default.

**A trait is not `Hashable`.** The name is the method, not an adjective:

```trb
type Money with Equals, Hash {
  cents: Int

  fn equals(other: Money): Bool {
    cents == other.cents
  }

  fn hash(): Int {
    cents
  }
}

print Money(1).equals(Money(1))
```

```trb error
type Money with Hashable {
  cents: Int
}
// error: Unknown type `Hashable`
```

**`with` does not mean "and also these type parameters".** `with` introduces traits. Type parameters go in angle
brackets, and bounds go after `where` or inline.

**An `extend` is not a monkey patch.** For a type of another package the file that uses the member names it, and two
members of one name for one type make calling it an error until `as` renames one of them where it is imported.

**A blanket implementation is not a fallback.** `extend<Source, Target> Source with Into<Target> where Target:
From<Source>` covers every type, is allowed only for a trait the package owns, and can never coexist with a second
blanket implementation of the same trait - overlap is decided conservatively, so two of them always collide.

## Related

- [Declaring a type](../types/declaring-a-type.md) - the type a trait is given to.
- [Cases and match](../pattern-matching/cases-and-match.md) - the other half of what a `type` can be.
- [std/core](../../standard-library/core.md) - where `Equals`, `Compare`, `Hash`, `Show` and the operator traits live.
- [Coming from Rust](../../explanation/coming-from-rust.md) - `impl Trait for Type` against `with` and `extend`.
