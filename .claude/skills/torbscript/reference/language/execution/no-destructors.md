---
title: There are no destructors
summary: Close is an ordinary method and using is an ordinary function, so the nesting of using blocks is the only destruction order the language ever promises - a value going out of scope runs no code at all.
kind: reference
status: stable
order: 40
keywords:
  - destructor
  - Close
  - using
  - reference counting
source:
  - CONCEPT.md#execution-model
  - std/core/src/control.trb
---

Nothing runs automatically when a value stops being reachable. `Close` and `using` give a resource a deterministic
place to clean up, and that place is the only one the language promises anything about.

## Example

```trb check
shared type Connection with Close {
  var isOpen: Bool = true

  fn send(message: String) {
    print message
  }

  var fn close() {
    isOpen = false
  }
}

using Connection() { connection => connection.send "hello" }
```

## Syntax

```text
public shared trait Close { var fn close() }
public fn using<Resource: Close, Value>(var resource: Resource, body: (var Resource) => Value): Value
```

## Rules

1. **`Close` is an ordinary method, declared by an ordinary trait.** There is no separate destructor syntax; a type
   that needs cleanup implements `Close` the same way it implements any other trait.

2. **`using` calls the body, then closes the resource, whether or not the body was reached in the usual way.**
   `using resource { body }` is `const result = body(resource); resource.close(); result`, so `close()` runs exactly
   once, right after the body finishes.

3. **The nesting of `using` blocks is the only destruction order the language promises.** Two resources opened with
   nested `using` calls close in the reverse order they were opened, because that order is written in the source;
   nothing else about when a value's storage is released has an order at all.

4. **When a reference count reaches zero is not observable, and a release never runs user code.** Only `shared type`
   objects and `var` bindings captured by closures are reference counted at all (values cannot form cycles and need
   no cycle collection), and reaching zero frees storage - it does not call `close()` or anything else.

5. **A `Close` a program forgets to call through `using` never runs at all.** There are no drop flags and no field
   order to reason about, because there is no automatic call in the first place for a flag to guard.

## What this is not

**`Close` is not called automatically when a binding goes out of scope.** A resource that is never passed to `using`
never has `close()` called on it by anything, which is why every resource with a `Close` is meant to be opened
through `using`.

```trb check
shared type Connection with Close {
  var fn close() {}
}

fn withConnection(body: (var Connection) => Void) {
  using Connection() { connection => body connection }
}

withConnection { connection => print "using it" }
```

```trb check
shared type Connection with Close {
  var fn close() {}
}

var connection = Connection()
print "using it"
```

Nothing here is a compile error: `connection` is constructed, used, and never passed to `using` at all. Its `close()`
is never called by anything - not when the binding goes out of scope, not when the last reference to it is dropped -
which is exactly why every `Close` resource is written to go through `using` from the start, rather than relying on
a cleanup step to catch it later.

## Related

- [Result](../errors/result.md) - what a `panic` skips on its way out, which includes every pending `close()`.
- [Shared types](../types/shared-types.md) - why a resource with an identity is what implements `Close`.
- [What a copy costs](copies.md) - reference counting, the mechanism `Close` deliberately stays outside of.
