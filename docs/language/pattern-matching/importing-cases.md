---
title: Importing cases
summary: A case is imported through the type it belongs to, and only a case can be; once imported it needs nothing in front of it, in an expression and in a pattern.
kind: reference
status: stable
order: 40
keywords:
  - use
  - import
  - from
  - re-export
  - as
source:
  - CONCEPT.md#modules-and-packages
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

`Some`, `None`, `Ok` and `Fail` are written bare everywhere in this documentation for one reason only: the prelude
imports them. Importing a case anywhere else works the same way.

## Example

```trb check
use Option.Some, Option.None from "std/core"

fn describe(value: Int?): String {
  match value {
    Some(found) => "there is {found}"
    None => "nothing"
  }
}

print describe(Some(3))
```

## Syntax

```text
use <Type>.<Case> from "<path>"                     one case, imported through its type
use <Type>, <Type>.<Case>, ... from "<path>"         the type and one or more of its cases, in one list
use <Type>.<Case>                                    without `from`: resolved in this file's own scope
public use <Type>.<Case> from "<path>"               re-exported under the same name
```

## Rules

1. **A case is imported through the type it belongs to.** `use Option.Some from "std/core"` imports the case; a method,
   a constant or a field of the type's own body is never imported this way and stays written `Type.member`.

   The **first letter** decides which of the two a path names: a segment that starts uppercase is a case, a lowercase one
   is a member another package attaches to the type with an `extend` (`use Int64.seconds from "std/time"`, see
   [extend](../traits/extend.md)). So `use Option.Maybe from "std/core"` is still "`Option` has no case `Maybe`" and
   never a message about a missing member.

2. **`from "<path>"` names a module: a package (`"std/core"`), a public module of one (`"acme/http/routing"`), or a
   relative file (`"./option"`, no extension).** Everything after it in the list is imported from that one module,
   whichever mix of types, functions and cases it is.

3. **Without `from`, the path is resolved in the file's own scope.** `use Shape.Circle` is what the file that declares
   `Shape` itself writes, one case at a time, so that the file that introduces a type can also use its own cases bare.

4. **An imported case needs nothing in front of it, in an expression and in a pattern.** `Some(found)` and `None` are
   legal both as values and as patterns the moment the case is imported; every case that is not imported keeps its
   dot or its type in both positions, as [Cases and match](cases-and-match.md) describes.

5. **There is no `use Type.*`.** A case list is not a wildcard: every case a file can name bare is named on its own
   line, which is what keeps a file's meaning independent of how many cases a dependency happens to have.

   ```trb error
   use Option.* from "std/core"

   fn empty(): Int? {
     None
   }
   // error: There is no `use Option.*`
   ```

6. **`public use` re-exports what it imports, case included.** `public use Stack.Empty from "./collections/stack"`
   makes `Empty` part of what this file's own importers can see, the same as any other re-exported name.

7. **Renaming a case with `as` works exactly as it does for a type or a function, in an expression and in a
   pattern.** `use Option.None as Nothing from "std/core"` makes `Nothing` the case's name in this file, everywhere
   `None` would otherwise be written.

   ```trb check
   use Option.None as Nothing from "std/core"

   fn empty(): Int? {
     Nothing
   }

   fn describe(value: Int?): String {
     match value {
       Nothing => "nothing"
       _ => "something"
     }
   }

   print empty()
   print describe(Nothing)
   ```

8. **An alias is spelled like the name it renames.** A case starts with an uppercase letter, so an alias of one does
   too; an alias of a function or a constant starts with a lowercase letter. The checker reports it at the alias - see
   [Naming](../syntax/naming.md).

   ```trb error
   use Option.None as nothing from "std/core"

   fn empty(): Int? {
     nothing
   }
   print empty()
   // error: A type, trait or case starts with an uppercase letter: write `Nothing`
   ```

## What this is not

**Importing a case is not opening the type's namespace.** Importing `Option.Some` does not also make `None` bare, and
it does not touch a method or a `static fn` of `Option` at all - those are still written `Option.member`, whether
or not the type itself is imported:

```trb check
use Option.Some from "std/core"

fn firstOf(numbers: List<Int>): Int? {
  Some numbers.first().expect("at least one")
}

print firstOf([1, 2])
```

```trb error
use Option.Some, Option.None from "std/core"

fn describe(value: Int?): String {
  match value {
    Nome => "there is nothing"
    _ => "something"
  }
}
print describe(None)
// error: `Nome` is not a case in scope
```

## Related

- [Cases and match](cases-and-match.md) - what changes once a case is imported.
- [The language reference](../index.md) - where the full grammar of `use` will live.
- [std/core](../../standard-library/core.md) - `Option` and `Result`, whose cases the prelude imports this way.
- [Naming](../syntax/naming.md) - how an alias is spelled, and why the first letter is a rule.
