---
title: Shared types
summary: A shared type has an identity instead of a value, so assigning it never copies, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
kind: reference
status: stable
order: 100
keywords:
  - shared type
  - identity
  - isSame
  - read-only view
source:
  - CONCEPT.md#identity-shared-type
  - std/core/src/shared.trb
---

Every ordinary `type` is a value: assigning it copies it. `shared type` is the exception, for the few things that
have an identity - a connection, a file, a window - where everybody holding it has to see the same object.

## Example

```trb
shared type Connection {
  url: String
  private(var) sent: Int = 0

  fn send(var self, message: String) {
    sent = sent + 1
  }
}

var connection = Connection "tcp://example.test"
var same = connection          // The same object, not a copy
same.send "hello"
print connection.sent          // 1
```

## Syntax

```text
shared type <Name> {
  <field>
  <function>
}
```

## Rules

1. **Assigning a `shared type` never copies it.** `var same = connection` gives `same` the same object `connection`
   names; a change through one is visible through the other, which is never true of an ordinary `type`.

2. **Mutation still needs a `var` path.** A `const` binding to a shared object is a read-only view: the object can
   still change through somebody else's `var` path, but not through this one.

   ```trb error
   shared type Connection {
     url: String
     private(var) sent: Int = 0

     fn send(var self, message: String) {
       sent = sent + 1
     }
   }

   const view = Connection "tcp://example.test"
   view.send "nope"
   // error: `send` needs a `var`
   ```

3. **A read-only view cannot be widened back into a `var`.** There is no copy that would make it a different object,
   so a `var` binding, field or argument may not be initialized from a `const` path to a shared object.

   ```trb error
   fn widen(view: Connection): Connection {
     var writable = view
     writable
   }
   // error: `view` is a read-only view of a shared `Connection`, so no `var` comes out of it
   ```

4. **`Equals`, `Hash` and `copy` are not generated for a `shared type`.** `==` is about content and does not exist for
   an object; `Show` is a `shared trait`, so a shared object can still be printed.

5. **`isSame` compares identity, and only a `shared type` has one.** Calling it on an ordinary value is rejected,
   because the answer would expose whether that value's implementation happens to share storage.

   ```trb error
   type Point {
     var x: Int
     var y: Int
   }

   const a = Point x: 1, y: 2
   const b = Point x: 1, y: 2
   print isSame(a, b)
   // error: `isSame` compares identity, and a `Point` is a value
   ```

6. **A `var self` method of a `shared type` may answer a `Task`; the same method on a value may not.** A `var` on a
   value is "copy in, copy out" that ends when the call returns, so a change made after the call answers would be
   lost - a `var self` method that answers a `Task` is only sound where there is one object and no copy to lose.

   ```trb error
   type Counter {
     var count: Int = 0

     fn tick(var self): Task<Void> {
       count = count + 1
       spawn { void }
     }
   }
   // error: `tick` changes `self` and answers a `Task`, and `Counter` is a value
   ```

## What this is not

**A `shared type` is not the default choice for anything that gets passed around.** Almost nothing needs one: a value
copied and passed everywhere is exactly what keeps two bindings from ever aliasing by accident. `shared type` is for
the few things whose whole point is that everybody sees the same one - a handle to something outside the program.

```trb
type Point {
  var x: Int
  var y: Int
}

var a = Point x: 1, y: 2
var b = a               // A copy: changing `b` never changes `a`
b.x = 9
print "{a.x} {b.x}"     // 1 9
```

```trb error
type Point {
  var x: Int
  var y: Int
}

const a = Point x: 1, y: 2
const b = Point x: 1, y: 2
print isSame(a, b)
// error: `isSame` compares identity, and a `Point` is a value
```

## Related

- [Declaring a type](declaring-a-type.md) - the value form `shared type` is the exception to.
- [Copy and equality](copy-and-equality.md) - `Equals`, `Hash` and `copy`, generated for a value and not for an
  object.
- [Mutation and var paths](var-paths.md) - the `var` path rule a shared object still follows.
