---
title: Destructors - close() runs at the last release
summary: A shared type's close() is its destructor - it runs exactly once at the last release, user code never calls it, and a binding that holds one is released at the end of its block, the last declared first.
kind: reference
status: stable
order: 40
keywords:
  - destructor
  - Close
  - close
  - using
  - reference counting
  - scope
source:
  - docs/design/DESTRUCTORS.md
  - std/core/src/control.trb
  - compiler/src/semantics/checker/close.trb
  - compiler/src/ir/lower/scope.trb
  - compiler/src/ir/ownership.trb
---

`close()` is the one destructor of the language. The runtime's reference count triggers it, exactly once, when the
last holder of an object goes away. A binding whose value holds such an object is released at the end of the block it
was declared in, the last declared first, so the moment an object closes is a line of the source.

## Example

```trb check
shared type Connection with Close {
  var open: Bool = true

  fn send(message: String) {
    print message
  }

  var fn close() {
    open = false
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
using name: Type = expression
```

`using` is a keyword only in front of a name and an `=` (or a `:` and a type); everywhere else it is an ordinary name.

## Rules

1. **Only a `shared type` may implement `Close`.** A value is copied on assignment, so two copies would give two
   closes of one resource; an object has one identity for the destructor to belong to. A capsule is a value too, and
   a trait that comes `with Close` - `Sink`, `Source` - counts as `Close`.

   ```trb error
   type Ticket with Close {
     var used: Bool = false

     var fn close() {
       used = true
     }
   }
   // error: `Close` may only be implemented by a `shared type`
   ```

2. **`close()` runs exactly once, when the last reference to the object goes away, and user code cannot call it.**
   Not from outside, not through a trait-typed value, and not from inside the type - `close()` of a field included,
   because the release takes the fields down right after the object's own `close()`. To release early, let the scope
   end - a nested block if it has to be mid-function - or bind the object with `using`.

   ```trb error
   shared type Connection with Close {
     var fn close() {}
   }

   fn finish() {
     const connection = Connection()
     connection.close()
   }
   // error: `close` runs automatically when the last reference to this value goes away, and cannot be called directly
   ```

   A `close()` of a type that does not implement `Close` is an ordinary method.

3. **A binding whose value may hold a `Close` object is released at the end of its block, in reverse declaration
   order.** Not at its last use: in the example below `first` is used for the last time before `second` and still
   closes after it. Every way out of the block ends it the same way - the end of the block, a `return`, a `?` that
   hands a failure on, a `break` or a `continue`. The names a pattern binds belong to the block they are bound for:
   the `then` block of an `if const`, the arm of a `match`, the body of a `for` or a `while const`, which ends them
   every round.

   ```trb check
   shared type Probe with Close {
     name: String

     fn touch() {
       print "touch {name}"
     }

     var fn close() {
       print "close {name}"
     }
   }

   fn twoBindings() {
     const first = Probe "first"
     const second = Probe "second"
     first.touch()
     second.touch()
     print "end of the block"
   }
   // touch first, touch second, end of the block, close second, close first
   ```

   "May hold" is a fact of the type: an object of a type that implements `Close`, a record, a case, a tuple or a
   collection with one inside, and a trait-typed value that may carry one. Every other binding is released where the
   compiler finds its last use, which nothing can observe.

4. **A value that is moved out leaves nothing behind.** A binding that is returned, or stored where it outlives the
   block, is not released at the end of the block: its new holder closes it, wherever its own last reference goes.
   Where one path moves the value and another does not, the other path releases it at the end of the block.

5. **A temporary is released at the end of its statement**, the last made first.
   `print(described(Probe("x")), "rest")` closes the probe after the whole line was printed, not after `described`
   returned.

6. **A released object runs its own `close()` first and then releases its fields in reverse declaration order**; a
   collection releases its elements from the last to the first. A value is taken down in the reverse of the order it
   was built, so something built on another thing is gone before the thing it was built on.

7. **`close()` may not keep `self`.** Inside `close()`, `self` may be read, may call the object's members and may be
   handed to a function that only uses it. Storing it, returning it, binding it to another name, handing it to a
   `var fn` or to a member of a shared object (either may keep it), or capturing it in a closure that outlives the
   call is an error, because nothing may hold an object that is being released.

   ```trb error
   shared type Node with Close {
     var others: List<Node> = []

     var fn close() {
       others.append self
     }
   }
   // error: `close` may not keep `self`: the object is being released
   ```

8. **`close()` returns `Void`, cannot fail and never awaits.** A type whose graceful end can fail or has to wait offers
   `end()` beside it, and a program calls and awaits that where it wants it; after `end()`, `close()` does nothing.

9. **`using name = expression` binds a `const` that cannot escape its block.** It may be read, be the receiver of a
   member and be handed to a function that only uses it. Storing it in a field or a collection, returning it, binding
   it to another name, handing it to a constructor, a case, a `var fn` or a member of a shared object, or capturing
   it in a closure that may outlive the block is an error. What a call answers is not followed: a function that
   returns what it was handed can still carry the object out. `using` never awaits anything, not even inside a task.

   ```trb error
   shared type Connection with Close {
     var fn close() {}
   }

   type Holder {
     var held: Connection
   }

   fn keep(var holder: Holder) {
     using connection = Connection()
     holder.held = connection
   }
   // error: `connection` is bound by `using`, so it cannot be stored in a field
   ```

10. **`using` belongs to a block.** At the top level of a file its block would be the whole program, so it is an
    error there; inside a function, a closure or any block of a script it is allowed. The top level of an entry file
    is a block of its own: its bindings end where the file does, the last declared first.

11. **Cancelling a task releases its frame**, at its next suspension point or at the next turn of a loop, and that
    release closes every object the frame held, the last declared first, before anybody waiting for the task hears
    that it was cancelled. Dropping the `Task` handle cancels nothing.

12. **A panic runs no `close()`.** The process ends, and the operating system takes back what the program held.

## Pitfalls

- **A closure retains what it captures.** A binding a closure captured is still held by its own slot until its block
  ends, so a writing end captured by a `spawn` closure does not end its channel before the block that bound it ends.
  Bind it in a block of its own, or make it inside the task.
- **A temporary that only one path of its statement makes** - an arm of an `if` expression, the fallback of a `??` -
  is released where it was used for the last time, because the end of the statement is not a line every path has made
  it at.

## What this is not

**Not a method to call.** Rule 2 refuses every call, which is also why `std` has none: a wrapper such as a buffered
sink holds the sink below it in a field, and releasing the wrapper releases that field after the wrapper's own
`close()`.

**Not a garbage collector.** There is reference counting and nothing else - no tracing pass and no cycle collector.
Two shared objects that hold each other are never released and never closed; trees and graphs hold handles such as an
`Entity` instead of references, and a stored callback takes its owner as a receiver instead of capturing it.

**Not an implicit wait.** The end of a `using` block runs `close()`, the abrupt end. A sink that has to be flushed is
ended by `sink.end().await()` on a line of its own, before the block ends.

**Not a cost for anything else.** A type without `Close` pays nothing: its release is what it always was, field by
field, and a binding of it is released at its last use.

## Related

- [Shared types](../types/shared-types.md) - the identity a destructor belongs to.
- [What a copy costs](copies.md) - reference counting, the mechanism that triggers `close()`.
- [Result](../errors/result.md) - what a `panic` skips on its way out.
