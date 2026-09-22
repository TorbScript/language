---
title: The prelude
summary: The prelude is the package whose public names are in scope in every file without an import, and it holds only the pure part of the standard library.
kind: reference
status: stable
order: 30
keywords:
  - prelude
  - std/prelude
  - re-export
source:
  - std/prelude/src/lib.trb
  - CONCEPT.md#modules-and-packages
---

> **Not built natively yet.** `Duration` and `Instant` are not built by the native back end yet, so `torb run` refuses
> the examples here that use them. `torb check` accepts them, and the rules are the language's.

A new file already has `Option`, `List`, `print` and every other name of [the glossary](../../glossary.md#prelude) in
scope. `use` is for everything else: the prelude is what a project decided nobody should have to import.

## Example

```trb check
const numbers = [1, 2, 3]
print numbers.map({ _ * 2 }).toList()
```

Nothing here is imported: `List`, the closure syntax, `map` and `print` are all part of `std/prelude`.

## Syntax

```text
use <Name> from "std/prelude"    // Legal, but redundant for a name the prelude already exports
```

## Rules

1. **`std/prelude` declares nothing of its own.** Its `src/lib.trb` is nothing but `public use ... from "std/..."`
   lines, so every name it re-exports can also be imported straight from the package that declares it.

   That includes members other packages attach to a type: `public use Int64.seconds from "std/time"` is why
   `2.seconds()` works everywhere, although `std/time` does not own `Int64`. See [extend](../traits/extend.md) for why
   such a member is named at all, and [use](use.md) for the form.

   ```trb check
   print 2.seconds()
   ```

2. **A project's prelude is `std/prelude` unless `project.trb` names another one.** The field is `prelude` on
   `Project`, so a teaching subset or the vocabulary of an embedded DSL can replace it for a whole project.

3. **The prelude holds the pure part of the standard library, and none of its capabilities.** Values, text, numbers,
   collections, pipelines, encoding, quotations, tasks and printing are in scope everywhere; `std/fs`,
   `std/environment`, `std/process`, `std/io`, `std/http`, `std/sandbox` and `Clock` are not.

   ```trb error
   fn checkExists(path: String): Bool {
     File.exists path
   }
   // error: Cannot find `File` here
   ```

4. **An import of a capability is a statement a reviewer can read at the top of the file.** `use File from "std/fs"`
   says "this file touches files" before a single line of its body does, which is exactly what a name that was
   already in scope could never say.

5. **A sandbox gives its script the prelude of the host plus the receiver, and nothing else.** A receiver script is
   defined that way, which is why the split between the prelude and every capability package is what makes
   [the sandbox](../configuration/the-sandbox.md) possible at all.

## What this is not

**The prelude is not "everything the standard library has".** A name that is not re-exported by `std/prelude` needs
its own `use`, however small it looks.

```trb check
use Clock from "std/time"

const start = Clock.now()
const elapsed: Duration = Clock.now() - start
print elapsed
```

```trb error
const start = Clock.now()
// error: Cannot find `Clock` here
```

`Instant` and `Duration` need no import - they are values, and every numeric field of a program can hold one. `Clock`
reads the wall clock, which is a capability, and only an explicit `use` brings it into scope.

## Related

- [use](use.md) - every form an import can take.
- [Packages](packages.md) - what a package is, and how the prelude is one.
- [Visibility](visibility.md) - `public`, which is what makes a name re-exportable at all.
