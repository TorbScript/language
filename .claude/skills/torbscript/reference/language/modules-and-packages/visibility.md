---
title: Visibility
summary: A top-level declaration is private to its file unless marked public, and a public declaration may not expose a type that is private to its own file.
kind: reference
status: stable
order: 20
keywords:
  - public
  - top-level
  - surface
  - module
source:
  - CONCEPT.md#visibility-and-encapsulation
---

Every file starts with nothing exported. `public` in front of a top-level `fn`, `type`, `trait` or `const` is what
makes it reachable from another file, and from another package once that file is a package's `src/lib.trb`.

## Example

```trb check
public type Meters {
  value: Float64
}

public fn zero(): Meters {
  Meters 0.0
}

print zero()
```

## Syntax

```text
[public] fn <name>(...): <Type> { ... }
[public] type <Name> { ... }
[public] trait <Name> { ... }
[public] const <name> = <expression>
```

## Rules

1. **A top-level declaration is private to its file unless marked `public`.** This is a different question from a
   member's own `private`: a file's surface is opt-in, and `src/lib.trb` is what other files - and, once the package
   is a dependency, other packages - can import from it.

2. **A `public` declaration may not expose a type that is private to its file.** The parameters and result of a
   `public fn`, the fields and case fields of a `public type`, the members of a `public trait`, and the type of a
   `public const` all have to be at least as visible as the declaration itself - a caller who cannot name a type
   cannot do anything with a value of it.

   ```trb error
   type Options {
     verbose: Bool = false
   }

   public fn defaultOptions(): Options {
     Options()
   }
   // error: `defaultOptions` is `public` and names `Options`, which is private to this file
   ```

3. **`public const` is legal at the top level, `public var` is not, in an imported module.** A module exports
   constants and has no mutable state; its `const` initializers are evaluated once, at compile time, and what they
   produce is what every importer sees.

   ```trb skip a top-level var is only rejected in an imported module, and every snippet of this documentation is an unimported file that the checker treats as a script
   public var counter: Int = 0
   // A module rejects this with: A module has no mutable state, so it has no top-level `var`
   ```

4. **A `private` member of a `type` reaches as far as this rule does: its own file.** Both modifiers answer the same
   question with the same unit - `public` says which of a file's declarations another file may name, `private` says
   which of a type's members another file may name - so what is private is visible in the file that declares it, in
   an `extend` of that type written there, and in the free functions of that file. A file can `use` a type without
   being able to name its private fields either way, so hiding a field is never undone by making the file itself
   `public`. `protected var` has the same reach for writing alone: everybody reads the field, and only its own file
   writes it.

   ```trb check
   type Server {
     private var log: List<String> = []
   }

   extend Server {
     var fn record(line: String) {
       log.append line
     }
   }

   fn entries(server: Server): Int {
     server.log.length()
   }

   var server = Server()
   server.record "started"
   print entries(server)
   ```

## What this is not

**`public` on a file's own declarations is not the same question as `private` on a member.** A type can be `public`
and still keep every field to itself; the two modifiers answer "can another file import this name" and "can code
outside this type touch this member" separately.

```trb check
public type Meters {
  private value: Float64

  static fn zero(): Meters {
    Meters 0.0
  }
}

print Meters.zero()
```

```trb error
type Meters {
  private value: Float64
}

public const origin: Meters = Meters(0.0)
// error: `origin` is `public` and names `Meters`, which is private to this file
```

## Related

- [use](use.md) - what a `public` declaration can be reached with from another file.
- [Fields](../types/fields.md) - `private` and `protected var` on a member, the other half of visibility.
- [Packages](packages.md) - `src/lib.trb`, the surface a whole package exports.

