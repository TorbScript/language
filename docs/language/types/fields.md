---
title: Fields
summary: A field is const unless marked var, and private or protected var decide who may read it and who may write it, independently of each other.
kind: reference
status: stable
order: 20
keywords:
  - field
  - var
  - private
  - protected
  - write-protected
  - visibility
  - promise
source:
  - CONCEPT.md#visibility-and-encapsulation
  - CONCEPT.md#types
---

A field is a named piece of storage declared inside a `type`. Two modifiers decide what happens to it from outside the
type: `var` decides whether it can be written at all, and `private`/`protected` decide who may write it, or read it.

## Example

```trb
type Server {
  host: String
  var port: Int = 8080
  protected var connections: Int = 0
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
<field> ::= [private] [var] <name>: <Type> [= <default>]
          | protected var <name>: <Type> [= <default>]
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
   | `protected var connections: Int` | yes | no |
   | `private var log: List<String>` | no | no |

3. **`protected var` is write-protected: everybody reads the field, and only the file that declares its type writes
   it.** That is what `protected` means here. TorbScript has no inheritance, so there is no subclass the word could open
   a field to. Outside the file, `server.connections` reads the count, and because `const` is deep a copy taken out of
   it cannot be written back either. Inside the file - the body of the type, an `extend` of it there, and the functions
   of that file - it is an ordinary `var` field. It is not `private`, so the constructor and `copy` still take it.

   ```trb
   type Server {
     protected var connections: Int = 0

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
   // error: `memberPatterns` is write-protected: only the file that declares `Workspace` writes it
   ```

4. **`protected` goes with `var`, on a field, and nowhere else.** A field without `var` is written by nobody already, so
   `protected` in front of it has nothing to protect and is an error - one spelling per meaning. On a method, a case, a
   `static` member or a top-level declaration it is refused the same way, and a member has one visibility, so
   `private protected` is an error too.

   ```trb error
   type Server {
     protected connections: Int = 0
   }
   // error: `protected` needs `var`: a const field is write-protected already
   ```

   `private(var) connections: Int` is the spelling `protected var` replaces. It still compiles and means the same, and
   `torb lint --fix --rule protected-field` rewrites it.

5. **`private` reaches as far as the file that declares it.** A `private` member is visible in the body of its type,
   in an `extend` of that type in the same file, and in the functions of that file - and nowhere else. Reading it from
   another file is an error, not a shorter view of it. One rule, and it is the same reach a `private` top-level
   declaration has: what is private is what its file can see.

   ```trb error
   use Path from "std/path"

   const path = Path.from "a/b"
   print path.componentValues
   // error: `componentValues` is private to `Path`
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

6. **A field may be named after a reserved word, and is then reached through a `.`.** `type: String` declares a field
   `type`; outside the type it is `event.type` and `Event(type: "click")`, inside it is `self.type`, because a bare
   keyword is always the keyword. The derived `Encode` and `Decode` use the field's name as the key, so a JSON document
   with a `"type"` needs no renamed field. The same holds for a method and for the field of a case. See
   [Lexical structure](../syntax/lexical-structure.md), rule 6. `protected` is not reserved at all: it is a word only
   in front of a member, and a name everywhere else, bare or after a `.` (`protected: Bool`, `self.protected`).

   ```trb check
   type Event {
     type: String
     var in: Int = 0

     fn describe(): String {
       "{self.type} ({self.in})"
     }
   }

   print Event(type: "click", in: 2).describe()
   ```

7. **A field is a promise, not a first draft.** It is a parameter of the generated constructor, a position in a
   pattern, a parameter of `copy`, and a step of every `var` path into the type - replacing it with a method later
   changes all four. What might one day be computed, cached or validated is a method from the start.

## What this is not

**A field is not a shorthand for a getter and a setter.** There is no way to attach logic to reading or writing a
field: a field is storage, and a method is what computes something. Wanting validation on write is a sign that the
member should have been a method from the start, not a reason to reach for `protected var`.

```trb
type Account {
  protected var balance: Int = 0

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
// error: `memberPatterns` is write-protected: only the file that declares `Workspace` writes it
```

**`protected` is not the `protected` of Java, C#, Kotlin, TypeScript or C++.** There it opens a member to
subclasses. TorbScript has no inheritance, so the word means only this: write-protected.

## Related

- [Declaring a type](declaring-a-type.md) - fields alongside methods, constants and the generated constructor.
- [Mutation and var paths](var-paths.md) - what has to be `var`, from the binding down to the field.
- [Construction](construction.md) - what a field's default value is evaluated against.
- [Bindings](../values-and-types/bindings.md) - `const` and `var` on a binding, the other half of the same rule.
