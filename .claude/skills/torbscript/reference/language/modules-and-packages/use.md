---
title: use
summary: use brings names into scope from a package or a file. Everything after from names a module; a path brings in a case of a type or a member another package attaches to it, and a use without names is an error.
kind: reference
status: stable
order: 10
keywords:
  - use
  - import
  - from
  - module
  - re-export
source:
  - CONCEPT.md#modules-and-packages
---

> **Not built natively yet.** `Duration` is not built by the native back end yet, so `torb run` refuses the examples
> here that use it. `torb check` accepts them, and the rules are the language's.

A file starts with no names but the ones the [prelude](the-prelude.md) already brought in. `use` adds more of them,
one declaration per statement, and where they come from is always written next to them.

## Example

```trb check
use File as Files from "std/fs"
use * as math from "std/math"

fn areaOfCircle(radius: Float64): Float64 {
  math.pi * math.power(radius, 2.0)
}

print areaOfCircle(2.0)
print Files.exists("project.trb")
```

## Syntax

```text
use <Name> from "<owner>/<name>"                    a package: its src/lib.trb
use <Name> from "<owner>/<name>/<path>"             a module of a package: its src/<path>.trb
use <Name> from "./<path>"                          a relative import: a file, no extension
use * as <alias> from "<module>"                    a namespace import
use <Name> as <Alias> from "<module>"               any name may take a local alias
use <Type>.<Case> from "<module>"                   a case, imported through its type
use <Type>.<member> from "<module>"                 a member an `extend` of that package adds to the type
use <Type>.<member> as <name> from "<module>"       ...under a name of this file's choosing
use <Type>.<Case>                                   without from: resolved in this file's own scope
public use <Name> from "<module>"                   a re-export
```

## Rules

1. **After `from` there is always a module.** A path starting with `./` or `../` is a file, written without its
   `.trb` extension; every other path starts with a package name. `"owner/name"` is that package's `src/lib.trb`,
   `"owner/name/path"` is its `src/path.trb`.

2. **Only `public` declarations can be imported from another package, and only from a package that `project.trb`
   lists as a dependency.** A name that is private to its file, that does not exist, or that is reachable only
   through a type it belongs to is not directly exported by the module.

   ```trb error
   use Cats from "std/core"
   // error: `Cats` is not exported by `std/core`
   ```

3. **A path brings in a case of the type, or a member another package attaches to it with an `extend`.** A method, a
   constant or a field of the type's *own body* is never imported: it stays `Type.member` at every use. Naming something
   the type does not have is an error either way.

   ```trb error
   use Option.Maybe from "std/core"
   // error: `Option` has no case `Maybe`
   ```

   ```trb check
   use Int64.seconds from "std/time"

   print 2.seconds()
   ```

   `seconds` is a member `std/time` adds to `Int64`, which `std/number` owns - so the file that calls it names where it
   comes from. See [extend](../traits/extend.md) for the rule and for what needs no import at all. The type in front of
   the dot is a name of *this* file (`Int64` comes from the prelude, not from `std/time`), and the `from` names the
   package the member has to come from:

   ```trb error
   use Int64.minutes from "std/time"
   // error: `std/time` adds no member `minutes` to `Int64`
   ```

4. **`as` gives an import a local name of its own, a case included.** From that line on the local name is the only
   one the file has: it is what shadows an outer name, what a second `use` of the same name collides with, and what
   a "did you mean" note offers, and it is also the name a pattern matches the case with.

   ```trb check
   use Option.Some as Present from "std/core"

   const value = Present 3
   print value

   fn describe(value: Int?): String {
     match value {
       Present(found) => "there is {found}"
       _ => "nothing"
     }
   }
   ```

5. **Without `from`, the path is resolved in the file's own scope.** `use Shape.Circle` is what a file that declares
   `Shape` itself writes to bring its own case into scope the same way an importer would.

6. **There is no `use Option.*` and no brace group.** Every name a file uses is written out, one per item of the
   list after `use`, so a dependency gaining a case is never by itself a reason this file changes.

7. **A `use` always names what it imports.** Nothing runs when a module is imported, so a `use` with a path and no names
   would mean nothing at all:

   ```trb error
   use "std/text"
   // error: A `use` names what it imports
   ```

8. **`public use` re-exports, members included.** `public use Stack, ArrayStack from "./collections/stack"` makes both
   names part of this file's own public surface, under their own name or, with `as`, under a new one; and
   `public use Int64.seconds from "std/time"` hands the member on, which is what [the prelude](the-prelude.md) does with
   it.

## What this is not

**`use` is not a namespace you can dot into for everything a package has.** Every name a file reads has to be
written after `use`, once; there is no `std.fs.File`-style path to a name nobody imported.

```trb check
use File from "std/fs"

print File.exists("project.trb")
```

```trb error
print std.fs.File.exists("project.trb")
// error: Cannot find `std` here
```

## Related

- [Visibility](visibility.md) - what has to be `public` before `use` can reach it.
- [The prelude](the-prelude.md) - the names already in scope before the first `use`.
- [Packages](packages.md) - what `"owner/name"` after `from` names.
- [Cases and match](../pattern-matching/cases-and-match.md) - why an imported case is written bare in a pattern.

