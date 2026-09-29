---
title: Cyclic imports
summary: Two modules may import each other, because nothing runs when a module is imported and its exports are computed to a fixpoint, but the same cycle between top-level statements is an error.
kind: reference
status: stable
order: 70
keywords:
  - cyclic import
  - fixpoint
  - module
source:
  - CONCEPT.md#modules-and-packages
---

Importing is not running. Two files that name each other's types are not a mistake, because a module contributes
nothing but declarations, and a declaration does not care in which order its neighbors were read.

## Example

`first.trb`:

```trb
use Second from "./second"

public type First {
  next: Second
}
```

`second.trb`:

```trb
use First from "./first"

public type Second {
  previous: First?
}
```

## Syntax

```text
// first.trb
use Second from "./second"
// second.trb
use First from "./first"
```

## Rules

1. **Imports between modules may be cyclic, exactly as imports between packages may.** `std/core` names `std/text`
   because `Show.show` answers a `String`, and `std/text` names `std/core` because a `String` is `Compare` - cutting
   that apart would need one package, or a `String` that is not a type of the standard library.

2. **A cycle is harmless because nothing runs when a module is imported.** An imported module consists of
   declarations only (see [Top-level code](top-level-code.md)), so there is no initialization order for a cycle to
   break, and the exports on both sides are computed to a fixpoint.

3. **A cycle between workspace members is still an error.** Workspaces (skill `torbscript-projects`: `references/language/modules-and-packages/workspaces.md`) build their members in the
   order their dependencies require, and a member cannot depend on itself through another member - that ordering
   question only exists between packages, not between the files inside one.

4. **A cycle of top-level statements is not the same question, and is still rejected.** Only a file nothing imports
   may hold top-level statements at all; two such files cannot import each other in the first place, because each one
   would then be imported.

## What this is not

**A cyclic import is not a design smell to break up.** Two types that reference each other are ordinary in a
program with algebraic data types, and splitting the cycle across more files does not remove it, it only moves where
the `use` lines point.

```trb
use Second from "./second"

public type First {
  next: Second
}
```

**A cycle of top-level statements is a different question, and stays an error.** The rule is not about the two
files naming each other; it is that a file which is imported may not hold a top-level statement at all, cyclic or
not - see [Top-level code](top-level-code.md) for the exact diagnostic.

```trb skip this needs a second file that imports it back, which one snippet of this documentation cannot provide; the diagnostic is: Top-level code is only allowed in entry files. '<path>' is imported
use Second from "./second"

print "loading first"
```

## Related

- [Top-level code](top-level-code.md) - why an imported module has no initialization order to protect.
- [use](use.md) - the import statement that can form the cycle.
- Workspaces (skill `torbscript-projects`: `references/language/modules-and-packages/workspaces.md`) - the one place a cycle is still rejected: between member packages.

