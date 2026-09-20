---
title: Methods and static functions
summary: Declaring self makes a function a method instead of a static function, and a type has one namespace of members, so a field and a method can never share a name.
kind: reference
status: stable
order: 60
keywords:
  - method
  - self
  - static function
  - namespace
  - receiver
source:
  - CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure
  - examples/tour/src/03-types.trb
---

A member of a `type` is either a field, or a constant that holds a function - there is nothing else. Whether that
constant is a method or a static function comes down to one thing: whether it declares `self`.

## Example

```trb
type Rectangle {
  width: Int
  height: Int

  fn area(self): Int {
    width * height
  }

  fn square(size: Int): Self {
    Self size, size
  }
}

const rectangle = Rectangle 3, 4
print rectangle.area()
print Rectangle.area(rectangle)
const bound = rectangle.area
print bound()
print Rectangle.square(5)
```

## Syntax

```text
fn <name>(self, <parameters>): <Type> { ... }             // A method: takes self, `member` access is implicit
fn <name>(<parameters>): <Type> { ... }                    // A static function: no self
```

## Rules

1. **A function declares `self` to become a method; without `self` it is a static function.** `area` reads `width` and
   `height` without qualifying them, because they are members of the same `self` the method took.

2. **`value.member(args)` is `Type.member(value, args)` when `member` declares `self`.** `rectangle.area()` and
   `Rectangle.area(rectangle)` call the same constant; the first spells the receiver before the dot, the second passes
   it as the first argument.

3. **A method named without a call is the function value, bound to its receiver.** `const bound = rectangle.area` has
   type `() => Int`; calling `bound()` later still runs against `rectangle`, because the value already carries it.

4. **A method is a constant of the type that holds a receiver closure, not state on the instance.** There is no memory
   per value for it and no way to replace it at runtime, which is also why a `const` value with methods stays plain
   data as far as `Equals`, `Hash` and `Encode` are concerned.

5. **A type has one namespace of members.** A field and a method are both constants of the type, so nothing keeps two
   members of the same name apart the way an overload set would - a field and a method cannot share a name.

   ```trb error
   type Counter {
     var count: Int = 0

     fn count(self): Int {
       count
     }
   }
   // error: `count` is already declared in `Counter`
   ```

6. **Calling a function that a field holds always takes parentheses.** A field's value might itself be callable
   (`onClick: () => Void`), and `onClick()` calls it exactly the way any other method call would, through the same
   namespace.

## What this is not

**A static function is not a method with an optional receiver.** `Rectangle.square(5)` never needs a `Rectangle` to
work on - a function either declares `self` and needs one, or it does not and never gets one. Calling
`rectangle.square(5)` on an actual value is rejected for the same reason `rectangle.area` is not a static call.

```trb
type Rectangle {
  width: Int
  height: Int

  fn square(size: Int): Self {
    Self size, size
  }
}

print Rectangle.square(5)
```

```trb error
type Rectangle {
  width: Int
  height: Int

  fn square(size: Int): Self {
    Self size, size
  }
}

const rectangle = Rectangle 3, 4
print rectangle.square(5)
// error: `square` has no `self`, so it is reached through the type: `Rectangle.square(...)`
```

## Related

- [Declaring a type](declaring-a-type.md) - fields and methods together, and what the constructor generates.
- [Property commands](property-commands.md) - what a command call does to a field instead of a method.
- [Verbs and participles](verbs-and-participles.md) - naming a mutating method against the one that returns a copy.
- [Traits](../traits/traits.md) - a list of members a type promises to provide.
