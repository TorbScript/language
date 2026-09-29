---
title: Methods and `static fn`s
summary: A member says what it is with two words - static belongs to the type, var may change - and a type has one namespace of members, so a field and a method can never share a name.
kind: reference
status: stable
order: 60
keywords:
  - method
  - static
  - var fn
  - namespace
  - receiver
source:
  - CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure
  - examples/tour/src/03-types.trb
---

A member of a `type` is either a field, or a constant that holds a function - there is nothing else. Two words say
which: `static` means the member belongs to the type rather than to a value, and `var` means it may change.

## Example

```trb check
type Rectangle {
  width: Int
  var scale: Int = 1

  fn area(): Int {
    width * height * scale
  }

  var fn grow(by: Int) {
    scale = scale + by
  }

  static fn square(size: Int): Self {
    Self size, height: size
  }

  height: Int
}

var rectangle = Rectangle 3, height: 4
print rectangle.area()
rectangle.grow 2
const bound = rectangle.area
print bound()
print Rectangle.square(5)
```

## Syntax

```text
fn <name>(<parameters>): <Type> { ... }            // A method: it reads its receiver
var fn <name>(<parameters>): <Type> { ... }        // A method that changes its receiver
static fn <name>(<parameters>): <Type> { ... }     // Of the type: there is no receiver
static <name> = <value>                            // A constant of the type
```

## Rules

1. **A method does not list `self`.** Its parameter list is what the caller writes, and `self` is put there for it.
   `area` reads `width` and `height` without qualifying them, because they are members of the same receiver.

2. **`var fn` says the method changes its receiver**, the same word a `var` binding and a `var` field use. A call of
   one needs a [`var` path](var-paths.md), and the editor underlines both the declaration and every call of it.

3. **`static` says the member belongs to the type and not to a value.** `Rectangle.square(5)` never needs a
   `Rectangle` to work on. `static var` does not exist: there is no global mutable state.

4. **`self` stays an expression inside the body.** Pass it on, write `copy(...)` with it, compare with it - what is
   gone is only the parameter that announced it.

5. **A function type still names the receiver.** `(self: Rectangle) => Int` and `(var self: Config) => Void` are what
   a [receiver closure](../configuration/receiver-closures.md) is, and a method read as a value has exactly that type:
   `Rectangle.area` is `(self: Rectangle) => Int`, while `rectangle.area` is `() => Int`, already bound.

6. **A method is a constant of the type that holds a receiver closure, not state on the instance.** There is no memory
   per value for it and no way to replace it at runtime, which is also why a `const` value with methods stays plain
   data as far as `Equals`, `Hash` and `Encode` are concerned.

7. **A type has one namespace of members.** A field and a method are both constants of the type, so nothing keeps two
   members of the same name apart the way an overload set would - a field and a method cannot share a name.

   ```trb error
   type Counter {
     var count: Int = 0

     fn count(): Int {
       count
     }
   }
   // error: `count` is already declared in `Counter`
   ```

8. **Calling a function that a field holds always takes parentheses.** A field's value might itself be callable
   (`onClick: () => Void`), and `onClick()` calls it exactly the way any other method call would, through the same
   namespace.

## What this is not

**A `static fn` is not a method with an optional receiver.** A member either belongs to the type or to a value, and
the message names the other form either way.

```trb error
type Rectangle {
  width: Int
  height: Int

  static fn square(size: Int): Self {
    Self size, size
  }
}

const rectangle = Rectangle 3, 4
print rectangle.square(5)
// error: `square` is `static`: call it as `Rectangle.square(...)`
```

**A field is not a constant of the type.** `x: Int` is a field, and `const x: Int` is refused because `const` is what a
field is without the word ([Fields](fields.md), rule 1); `static x = 0` is a constant of the type. A `const x = 0` inside
a type body is neither, and says so.

```trb error
type Probe {
  const x = 0
}
// error: `x` is neither a field nor a constant of the type
```

## Related

- [Declaring a type](declaring-a-type.md) - fields and methods together, and what the constructor generates.
- [Property commands](property-commands.md) - what a command call does to a field instead of a method.
- [Verbs and participles](verbs-and-participles.md) - naming a mutating method against the one that returns a copy.
- [Receiver closures](../configuration/receiver-closures.md) - the function type a method as a value has.
- [Traits](../traits/traits.md) - a list of members a type promises to provide.

