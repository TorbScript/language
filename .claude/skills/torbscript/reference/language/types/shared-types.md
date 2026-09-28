---
title: Shared types
summary: A shared type has an identity instead of a value, so assigning it never copies, a change of the object needs no var path, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
kind: reference
status: stable
order: 100
keywords:
  - shared type
  - identity
  - isSame
  - rebinding
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
  protected var sent: Int = 0

  var fn send(message: String) {
    sent = sent + 1
  }
}

const connection = Connection "tcp://example.test"
const same = connection        // The same object, not a copy
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

1. **Assigning a `shared type` never copies it.** `const same = connection` gives `same` the same object `connection`
   names; a change through one is visible through the other, which is never true of an ordinary `type`.

2. **A change of an object is not a question of the path it is reached through.** An object has one identity and no
   copy, so its `var fn` members and its `var` fields change through any binding that holds it: a `const` one, a
   parameter, an element of a `const` list. What still decides is the object's own type - a field that is not `var`
   never changes, and `protected var` is written only by the file of the type.

   ```trb
   shared type Connection {
     url: String
     protected var sent: Int = 0

     var fn send(message: String) {
       sent = sent + 1
     }
   }

   const connection = Connection "tcp://example.test"
   const pool = [connection]
   connection.send "hello"
   pool[0].send "again"
   print connection.sent
   ```

3. **`var` on a binding of a shared type means rebinding, and nothing else.** A `const` binding always names the same
   object, and a `var` one may be pointed at another. That is also why `self` of a shared type is never replaced: a
   method changes the object everybody holds, and `self = ...` - or handing `self` to a `var` parameter - would point
   the caller's binding somewhere else instead.

   ```trb error
   shared type Connection {
     url: String

     var fn reconnect() {
       self = Connection url
     }
   }
   // error: `self` is the object itself, and a method of a shared type cannot replace it
   ```

   ```trb error
   shared type Connection {
     url: String
   }

   const connection = Connection "tcp://example.test"
   connection = Connection "tcp://other.test"
   print connection.url
   // error: `connection` is a `const`. Only a `var` binding can be changed
   ```

4. **`Equals`, `Hash` and `copy` are not generated for a `shared type`.** `==` is about content and does not exist for
   an object; `Show` is a `shared trait`, so a shared object can still be printed.

5. **`isSame` compares identity, and only a `shared type` has one.** Calling it on an ordinary value is rejected,
   because the answer would expose whether that value's implementation happens to share storage. A type parameter and
   a value of a `shared trait` are rejected too: a value may stand behind either, and the checker cannot tell - the
   same reason [`spawn`](../concurrency-and-streams/tasks.md) refuses them from the other side.

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

   ```trb error
   fn same<Value>(first: Value, second: Value): Bool {
     isSame(first, second)
   }
   // error: `isSame` compares identity, and a `Value` may be a value
   ```

6. **A `var fn` method of a `shared type` may answer a `Task`; the same method on a value may not.** A `var` on a
   value is "copy in, copy out" that ends when the call returns, so a change made after the call answers would be
   lost - a `var fn` method that answers a `Task` is only sound where there is one object and no copy to lose.

   ```trb error
   type Counter {
     var count: Int = 0

     var fn tick(): Task<Void> {
       count = count + 1
     }
   }
   // error: `tick` changes `self` and answers a `Task`, and `Counter` is a value
   ```

7. **A shared object stays in the task that made it.** `spawn` takes only what it can see is a value, so a shared
   object, a value that holds one, and anything that may hide one - a type parameter, a function value, a value of a
   `shared trait` - are refused where the closure captures them (see [Tasks](../concurrency-and-streams/tasks.md),
   rule 5). A value of an ordinary trait may cross, so it may not hide an object either: a value whose type holds one
   does not become a value of an ordinary trait that does not show it (`Iterate<Counter>` shows it, a `shared trait`
   is an object itself). The one residue is an object captured by a closure that a trait value holds - a function
   value holds nothing by its type - and the runtime keeps such a task on the worker that made the object.

   ```trb error
   shared type Counter {
     var count: Int = 0
   }

   trait Action {
     fn run(): Int
   }

   type Bumper with Action {
     counter: Counter

     fn run(): Int {
       counter.count
     }
   }

   const action: Action = Bumper(Counter())
   // error: `Bumper` holds an object, and a value of `Action` would hide it
   ```

8. **Only a `shared type` implements `Close`.** The destructor belongs to one object, and a value is copied on
   assignment, so two copies would close one resource twice. The same holds for a trait that comes `with Close`, such
   as `Sink` and `Source` (see [Destructors](../execution/destructors.md)).

   ```trb error
   type Ticket with Close {
     var fn close() {}
   }
   // error: `Close` may only be implemented by a `shared type`
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

**A `const` binding to an object is not a read-only view of it.** It fixes which object the name holds, the way a
`let` holds a class instance in Swift; the object changes through it like through every other holder. What cannot
change through a `const` is a **value** - which is why almost nothing is a shared type.

```trb error
type Point {
  var x: Int
}

const origin = Point 0
origin.x = 1
// error: `origin` is a `const`. Only a `var` binding can be changed
```

## Related

- [Declaring a type](declaring-a-type.md) - the value form `shared type` is the exception to.
- [Copy and equality](copy-and-equality.md) - `Equals`, `Hash` and `copy`, generated for a value and not for an
  object.
- [Mutation and var paths](var-paths.md) - the `var` path rule a value follows and an object does not need.

