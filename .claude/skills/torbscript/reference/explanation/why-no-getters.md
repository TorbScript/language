---
title: Why there are no properties
summary: A field is storage and a method computes, so the parentheses tell a reader which one they are looking at, and private(var) replaces the getter-and-setter pair without hiding either fact.
kind: explanation
status: stable
order: 80
keywords:
  - getter
  - setter
  - property
  - private(var)
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#visibility-and-encapsulation
---

A getter that only returns a field and a setter that only assigns one are the most common kind of code every
object-oriented codebase writes, and they say nothing a field declaration does not already say. This page argues for
removing them instead of automating them.

## The decision

**There is no `get` prefix, no computed property and no validating setter.** A member is either a field, which is
storage, or a method, which computes - and the `()` tells a reader which:

```trb check
type Account {
  owner: String
  private(var) balance: Int = 0

  var fn deposit(amount: Int) {
    balance = balance + amount
  }
}

const account = Account owner: "Ada", balance: 0
print "{account.owner}: {account.balance}"
```

- `point.x` never costs anything; `list.length()` might, and the parentheses say so.
- `private(var)` replaces the getter-and-setter pair: it hands out a read-only, `const` path to the field while
  keeping the write to the type itself, with no method written for either direction.
- There are no validating setters: a setter cannot return a `Result`, so validation lives in a factory
  (`Email.tryFrom`) or in a method that can fail (`account.withdraw(amount)`).

## Why

**Because a getter and a setter that only wrap a field are a promise with extra steps.** `getName()`/`setName(value)`
around a private `name` field communicate exactly what `public name: String` communicates, plus two more names to
read past and a call at every use site that looks like it might do something a plain field access would not.
[Members are public by default](../language/modules-and-packages/visibility.md), which is what made hiding a field
behind an accessor pointless in the first place: reading a field can never break an invariant, because nothing else
can be aliased through it (see [Why values instead of references](why-values-instead-of-references.md)) - only
writing it can, and that is exactly what `private(var)` marks.

**Because a field is more than a value - it is a position in the constructor, in patterns, in `copy`, and in the
generated `Encode`/`Decode` - so turning one into a property would cover only reading and leave the rest inconsistent.**
A property in Kotlin or C# hides a computation behind field syntax for reads, but the field is still a parameter of the
constructor, a target of `copy` and a case of pattern matching, none of which a getter alone can stand in for. Making
a field a method from the start keeps all of those consistent, and the one thing that changes when a value becomes
computed - `.x` to `.x()` - is a mechanical, whole-workspace rename the tooling can do.

**Because a setter cannot fail properly in a language without exceptions.** `set port(value: Int)` that wants to
reject a bad port has nowhere to put the failure: it cannot throw, and `Void` has no room for a `Result`. Validation
belongs in a method that names its own failure -`account.withdraw(amount): Result<Void, InsufficientFunds>` - or in a
parsing factory that never constructs the invalid value at all.

**Because `private(var)` says exactly what it means and needs no new keyword.** Swift's `private(set)` reads as "the
setter is private," which is a second word (`set`) for something the language already has a word for - the `var`
itself. `private(var)` reads as "the `var` is private": the field stays public, only its mutability does not.

### What was rejected

- **`get`/`set` accessor methods** generated or hand-written for every field. Rejected because they add two names for
  one piece of data and a call site that reads like it might compute something it does not.
- **Computed properties** (`var area: Float { width * height }`), which let a method masquerade as a field. Rejected
  because a reader could no longer tell from `.area` alone whether it costs anything, which is the one thing `()`
  exists to say.
- **`private(set)`**, Swift's spelling. Rejected as an extra keyword (`set`) for a fact the existing `var` already
  states; `private(var)` needed no addition to the grammar.
- **A `guarded` keyword** tried during design for the read-everyone/write-only-the-type case. Rejected because it is
  one more word to learn for something `private` and `var` already say together.

## Consequences

**Reading a field is always the field, never a hidden computation**, so a reader never has to check whether `.x` might
be a method call under different syntax.

```trb check
type Rectangle {
  width: Float
  height: Float

  fn area(): Float {
    width * height
  }
}

const box = Rectangle width: 2.0, height: 3.0
print "{box.width} {box.area()}"
```

**Writing a field from outside its type is rejected the same way whether the field is `private` or `private(var)`.**

"Outside its type" is outside the file that declares it, because that is how far `private` reaches:

```trb error
use Workspace from "std/project"

var workspace = Workspace()
workspace.memberPatterns = ["packages/*"]
print workspace.memberPatterns
// error: `memberPatterns` can only be written by `Workspace`
```

**What might become computed later is a method from the start.** A field that a library might want to validate,
cache, or compute one day is written as a method from its first line, so the move from `.x` to `.x()` never has to
happen as a breaking change to existing callers who already wrote `.x()`.

## Related

- [Fields](../language/types/fields.md) - `var`, `private`, `private(var)`, and the table of four.
- [Visibility](../language/modules-and-packages/visibility.md) - members public by default, and what that assumes.
- [Methods and `static fn`s](../language/types/methods.md) - the two words a member says about itself.
- [Why a method is a constant](why-one-member-namespace.md) - the one namespace a field and a method share.

