---
title: Declaring a type
summary: One keyword declares every data type. Fields are const unless marked var, members are public unless marked private, and Equals, Hash, Show and copy are generated.
kind: reference
status: stable
order: 10
keywords:
  - type
  - field
  - method
  - constructor
  - copy
  - private
source:
  - CONCEPT.md#types
  - CONCEPT.md#construction
  - CONCEPT.md#visibility-and-encapsulation
  - examples/tour/src/03-types.trb
---

`type` declares a struct, a class, an enum and an algebraic data type - there is only one keyword, because there is only
one kind of data. A `type` is a value: the binding that holds it decides whether it can be changed.

## Example

```trb
type Point {
  var x: Int
  var y: Int

  fn area(): Int {
    x * y
  }

  var fn translate(deltaX: Int = 0, deltaY: Int = 0) {
    x = x + deltaX
    y = y + deltaY
  }

  fn translated(deltaX: Int = 0, deltaY: Int = 0): Point {
    copy(x: x + deltaX, y: y + deltaY)
  }

  static fn square(size: Int): Self {
    Self size, size
  }

  static origin = Point 0, 0
}

var point = Point x: 10, y: 20
point.translate deltaX: 5
print "{point} {point.area()} {Point.origin}"
```

## Syntax

```text
[public] [shared] type <Name>[<parameters>] [with <traits>] [where <bounds>] {
  <field>
  <constant>
  <function>
  <case>
}
```

```text
<field>     ::= [private | private(var)] [var] <name>: <Type> [= <default>]
<constant>  ::= [private] const <name> [: <Type>] = <expression>
<function>  ::= [private] fn <name>[<parameters>](<self or nothing>, <parameters>) [: <Type>] { ... }
```

## Rules

1. **A field is `const` unless it is marked `var`.** A field without `var` never changes after construction, not even
   through a `var` binding. That is how a value says which part of it is fixed.

2. **A member is public unless it is marked `private`.** Fields, methods and constants alike. Reading a field cannot break
   an invariant when values are never aliased and `const` is deep, so what needs protection is writing.

   | Field | Read from outside | Write from outside |
   |-------|:-----------------:|:------------------:|
   | `x: Value` | yes | no, it is `const` |
   | `var x: Value` | yes | yes |
   | `private(var) x: Value` | yes | no |
   | `private x: Value` | no | no |

   `private(var)` reads as "the `var` is private": the field is public, its mutability is not. It hands an outsider a
   `const` path, and `const` is deep, so `config.routes.append(...)` from outside is an error while `config.routes` can be
   read and iterated.

3. **`private` reaches as far as the type does.** A private member is visible in the body of its type and in every
   `extend` of that type in the same package, and nowhere else. The package is the unit of coherence, so it is the unit of
   privacy.

4. **Every type has exactly one constructor, generated from its fields in declaration order.** It cannot be written by
   hand and it never contains logic. Arguments may be positional or labelled, and a field with a default may be left out.

   ```trb
   type Account {
     owner: String
     var nickname: String = ""
   }

   const first = Account "Ada"
   const second = Account owner: "Alan", nickname: "al"
   print "{first} {second}"
   ```

5. **A field default is evaluated at every construction, in a scope without `self` and without the other fields.** So the
   order of the fields is not observable, and a default that depends on another field is what a factory is for.

6. **The constructor is usable from outside if and only if every `private` field has a default.** Everything else is a
   static factory function: a member declared `static`.

   ```trb
   type Email with TryFrom<String, String> {
     private value: String

     static fn tryFrom(text: String): Result<Email, String> {
       if !text.contains("@") {
         return Fail "'{text}' is not an email address"
       }
       Ok Self(text)
     }
   }

   const email = Email.tryFrom "info@example.test"
   print email
   ```

7. **`Equals`, `Hash`, `Show` and `copy` are generated**, each of them only if every field supports it. A type with a
   function in a field has no generated `Equals`. There is no identity, so `==` is always about content.

8. **`copy` has the shape of the constructor with every field optional.** `point.copy(y: 30)` answers a new value;
   `private` fields are not passable from outside.

9. **A method has a receiver, a `static fn` has none.** `value.member(args)` is `Type.member(value, args)` when the
   member reads its receiver. `var fn` marks a method that changes its receiver in place.

10. **A verb changes in place and its participle returns a changed copy.** `translate` is a `var fn` and answers
    nothing; `translated` reads its receiver and answers a `Point`. Pick verbs whose participle is a different word.

    ```trb error
    type Point {
      var x: Int
      var y: Int

      var fn translate(deltaX: Int) {
        x = x + deltaX
      }
    }

    const fixed = Point 1, 2
    fixed.translate 5
    // error: `translate` needs a `var`
    ```

11. **A type has one namespace of members.** A field and a method cannot share a name, because a method *is* a constant
    of the type that holds a closure. That is also why calling a function held in a field always needs parentheses:
    `onClick()` calls it, `onClick { ... }` assigns it.

12. **`Self` is the type itself**, usable in every type position inside the declaration and as the constructor
    (`Self(size, size)`). Inside the type all fields can be passed, `private` ones included.

13. **The generated `Show` has a fixed format**, because two implementations of the language are compared through it: a
    type shows as `Type(field: value, ...)` with all fields in declaration order, a `List` as `[a, b]`, a `Map` as
    `["k": v]` (`[:]` when empty), a `Set` as `{a, b}`, an `Option` as `Some(x)` or `None`. Inside such a value a
    `String` is quoted and a `Char` is in single quotes.

14. **A `shared type` has an identity instead of a value.** Assigning it does not copy, `Equals`, `Hash`, `copy` and
    `Encode` are not generated, and `isSame(a, b)` compares identity. Handles to the outside world are shared types;
    almost nothing else should be.

## What this is not

**There are no getters, setters or properties.** A field is storage and a method computes, and the `()` tells a reader
which one it is. There is no `get` prefix; a predicate is `isEmpty()` or `hasErrors()`, a mutator is a verb written
`var fn`.

```trb
type Account {
  private(var) balance: Int = 0

  var fn deposit(amount: Int) {
    balance = balance + amount
  }
}

var account = Account()
account.deposit 100
print account.balance
```

From another file the field is readable and nothing else - `private` reaches as far as the file that declares it:

```trb error
use Workspace from "std/project"

var workspace = Workspace()
workspace.memberPatterns = ["packages/*"]
print workspace.memberPatterns
// error: `memberPatterns` can only be written by `Workspace`
```

**A constructor is not a place for logic.** Validation, parsing and conversion are static factory functions, because a
constructor cannot fail in a language without exceptions. `Email.tryFrom` answers a `Result`; `Email(...)` cannot exist from
outside, because `value` is `private` and has no default.

**A field is not a candidate for becoming a method later.** A field is a promise: it is a parameter of the generated
constructor, a position in patterns, a parameter of `copy`, part of the generated `Encode` and a step of `var` paths.
What might one day be computed, cached or validated is a method from the start.

**`private` is not per file.** It is per type, and it extends to every `extend` of that type in the same package. A
top-level declaration's visibility is a different question, spelled `public`.

## Related

- [Bindings](../values-and-types/bindings.md) - what `const` and `var` decide about a value.
- [Cases and match](../pattern-matching/cases-and-match.md) - a type with cases, and how to take one apart.
- [Traits](../traits/traits.md) - how a type comes `with` a capability.
- [Types and methods](../../guide/values-and-bindings.md) - the same material as a learning step.
- [Why there are no properties](../../explanation/why-values-instead-of-references.md) - the argument behind rule 11.
