---
title: Fields
summary: A field is const unless marked var, and private or private(var) decide who may read it and who may write it, independently of each other.
kind: reference
status: stable
order: 20
keywords:
  - field
  - var
  - private
  - visibility
  - promise
source:
  - CONCEPT.md#visibility-and-encapsulation
  - CONCEPT.md#types
---

A field is a named piece of storage declared inside a `type`. Two modifiers decide what happens to it from outside the
type: `var` decides whether it can be written at all, and `private`/`private(var)` decide who may write it, or read it.

## Example

```trb
type Server {
  host: String
  var port: Int = 8080
  private(var) connections: Int = 0
  private var log: List<String> = []

  var fn record(message: String) {
    log.append message
  }
}

var server = Server host: "localhost"
server.port = 9090
server.record "started"
print "{server.host}:{server.port} has {server.connections} connections"
```

## Syntax

```text
<field> ::= [private | private(var)] [var] <name>: <Type> [= <default>]
```

## Rules

1. **A field is `const` unless it is marked `var`.** `host: String` never changes after construction, not even through a
   `var` binding to the `Server`. `var port: Int` can be written wherever a [var path](var-paths.md) reaches it. The
   word `const` is what a field is without it, so writing it is refused - one spelling per meaning:

   ```trb error
   type Server {
     const host: String
   }
   // error: `const` is what a field is without it
   ```

2. **A member is public unless it is marked `private`.** `public` on a member says nothing and is refused the same way;
   it belongs on a top-level declaration, where the default is the other one. Reading a field cannot break an
   invariant, because values are never aliased and a `const` field never changes - so what needs protection is
   writing, and each of the four combinations answers a different question:

   | Field | Read from outside | Write from outside |
   |-------|:-----------------:|:------------------:|
   | `host: String` | yes | no, it is `const` |
   | `var port: Int` | yes | yes |
   | `private(var) connections: Int` | yes | no |
   | `private var log: List<String>` | no | no |

3. **`private(var)` hands outsiders a `const` path to a `var` field.** The field is public, its mutability is not:
   `server.connections` reads the count, and because `const` is deep a copy taken out of it cannot be written back
   either.

   ```trb
   type Server {
     private(var) connections: Int = 0

     var fn accepted() {
       connections = connections + 1
     }
   }
   ```

   `Workspace` of `std/project` is such a type, and "outside" means outside the file it is declared in (rule 5):

   ```trb error
   use Workspace from "std/project"

   var workspace = Workspace()
   workspace.memberPatterns = ["packages/*"]
   print workspace.memberPatterns
   // error: `memberPatterns` can only be written by `Workspace`
   ```

4. **`private(var)` is the whole spelling: it already contains the `var`.** Writing `var` again on the same field is an
   error, because one of the two modifiers would be saying the same thing twice.

   ```trb error
   type Server {
     private(var) var connections: Int = 0
   }
   // error: `private(var)` already says `var`
   ```

5. **`private` reaches as far as the file that declares it.** A `private` member is visible in the body of its type,
   in an `extend` of that type in the same file, and in the functions of that file - and nowhere else. Reading it from
   another file is an error, not a shorter view of it. One rule, and it is the same reach a `private` top-level
   declaration has: what is private is what its file can see.

   ```trb error
   use Path from "std/path"

   const path = Path.from "a/b"
   print path.storedComponents
   // error: `storedComponents` is private to `Path`
   ```

   Inside the file that declares it, nothing is hidden:

   ```trb check
   type Server {
     private var log: List<String> = []
   }

   fn entries(server: Server): Int {
     server.log.length()
   }

   print entries(Server())
   ```

6. **A field is a promise, not a first draft.** It is a parameter of the generated constructor, a position in a
   pattern, a parameter of `copy`, and a step of every `var` path into the type - replacing it with a method later
   changes all four. What might one day be computed, cached or validated is a method from the start.

## What this is not

**A field is not a shorthand for a getter and a setter.** There is no way to attach logic to reading or writing a
field: a field is storage, and a method is what computes something. Wanting validation on write is a sign that the
member should have been a method from the start, not a reason to reach for `private(var)`.

```trb
type Account {
  private(var) balance: Int = 0

  var fn deposit(amount: Int) {
    balance = balance + amount
  }
}
```

From another file there is no way in at all, and the deposit is the only door:

```trb error
use Workspace from "std/project"

var workspace = Workspace()
workspace.memberPatterns = ["packages/*"]
print workspace.memberPatterns
// error: `memberPatterns` can only be written by `Workspace`
```

## Related

- [Declaring a type](declaring-a-type.md) - fields alongside methods, constants and the generated constructor.
- [Mutation and var paths](var-paths.md) - what has to be `var`, from the binding down to the field.
- [Construction](construction.md) - what a field's default value is evaluated against.
- [Bindings](../values-and-types/bindings.md) - `const` and `var` on a binding, the other half of the same rule.

