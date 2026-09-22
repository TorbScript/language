---
title: Destructors - close() runs at the last release
summary: A shared type's close() is its destructor - the runtime runs it exactly once when the last reference goes away, user code never calls it, and using pins that moment to the end of a block.
kind: reference
status: planned
order: 40
keywords:
  - destructor
  - Close
  - close
  - using
  - reference counting
source:
  - docs/design/DESTRUCTORS.md
  - std/core/src/control.trb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page runs today: `close()` is still an
> ordinary method a program calls, and `using` is still an ordinary function over a closure. The decision and its
> reasons are in [the destructors design record](../../design/DESTRUCTORS.md).

`close()` is the one destructor of the language. The runtime's reference count triggers it, exactly once, when the
last holder of an object goes away, and `using` binds a name whose release - and with it the `close()` - happens at
the end of the block it was written in.

## Example

```trb fragment
shared type Connection with Close {
  var isOpen: Bool = true

  fn send(message: String) {
    print message
  }

  var fn close() {
    isOpen = false
    print "closed"
  }
}

fn greet() {
  using connection = Connection()
  connection.send "hello"
  print "sent"
}
```

`greet()` prints `hello`, `sent` and then `closed`: `connection` is released at the end of the block that bound it,
and the release runs `close()`.

## Syntax

```text
public shared trait Close { var fn close() }
using name = expression
```

## Rules

1. **Only a `shared type` may implement `Close`.** A value is copied on assignment, so two copies would give two
   closes of one resource; an object has one identity for the destructor to belong to.

2. **`close()` runs exactly once, when the last reference to the object goes away, and user code cannot call it.**
   To release early, let the scope end - a nested block if it has to be mid-function - or bind the object with
   `using`.

3. **A local whose type may contain a `Close` object is released at the end of its scope, in reverse declaration
   order.** Not at its last use, so the moment `close()` runs is a line in the source and never a result of an
   optimisation. A value that was moved out - returned, stored in a field - leaves nothing to release there, and it is
   closed by its last holder instead. Every other value is released where the compiler finds its last use, which
   nothing can observe.

4. **A temporary is released at the end of its statement**, in the reverse order the statement created them.

5. **A released object runs its own `close()` first and then releases its fields in reverse declaration order**; a
   collection releases its elements from the last to the first.

6. **`close()` may not keep `self`.** Storing `self`, returning it or capturing it in a closure inside `close()` is an
   error, because nothing may hold an object that is being released.

7. **`close()` returns `Void`, cannot fail and never awaits.** A type whose graceful end can fail or has to wait offers
   `end()` beside it, and a program calls and awaits that where it wants it; after `end()`, `close()` does nothing.

8. **`using name = expression` binds a name that cannot escape its block.** Storing it in a field, returning it or
   capturing it in a closure that escapes is an error. `using` never awaits anything, not even inside a task.

9. **Cancelling a task releases its frame**, at its next suspension point or at the next turn of a loop, and that
   release closes every object the frame held. Dropping the `Task` handle cancels nothing.

10. **A panic runs no `close()`.** The process ends, and the operating system takes back what the program held.

## What this is not

**Not a method to call.** Closing by hand is refused at the call, with the proposed message:

```trb fragment
var connection = Connection()
connection.close()
```

```text
error: `close` runs automatically when the last reference to this value goes away, and cannot be called directly
   = To release a value early, let its scope end, or bind it with `using`
```

**Not a garbage collector.** There is reference counting and nothing else - no tracing pass and no cycle collector.
Two shared objects that hold each other are never released and never closed; trees and graphs hold handles such as an
`Entity` instead of references, a stored callback takes its owner as a receiver instead of capturing it, and a leak is
reported with the type of every block still alive at the end of a test.

**Not an implicit wait.** The end of a `using` block runs `close()`, the abrupt end. A sink that has to be flushed is
ended by `sink.end().await()` on a line of its own, before the block ends.

## Related

- [Shared types](../types/shared-types.md) - the identity a destructor belongs to.
- [What a copy costs](copies.md) - reference counting, the mechanism that triggers `close()`.
- [Result](../errors/result.md) - what a `panic` skips on its way out.
