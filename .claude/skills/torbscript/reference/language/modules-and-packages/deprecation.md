---
title: Deprecation
summary: A deprecated clause above a declaration or a member keeps it working for its callers, warns at every use outside its file, and names the replacement that torb lint --fix writes.
kind: reference
status: stable
order: 25
keywords:
  - deprecated
  - since
  - replacement
  - warning
  - torb lint --fix
  - migration
source:
  - docs/design/DEPRECATION.md
  - CONCEPT.md#decision-log
  - compiler/src/semantics/checker/deprecation.trb
---

A declaration that turned out wrong is not removed while somebody still calls it: it is marked. The mark says why, what
to write instead and since which version, the checker warns at every use, and `torb lint --fix` moves the uses to the
replacement. That is how the standard library grows within a major version without breaking a program.

## Example

```trb check
public type Point {
  /** The horizontal position, as it was stored. */
  deprecated("it is computed from the polar form now", replacement: "x()", since: "0.4")
  x: Int = 0
  radius: Int

  /** The horizontal position. */
  fn x(): Int {
    radius * 2
  }

  /** The distance from the origin, under its old name. */
  deprecated("it says what it is under its new name", replacement: "length")
  fn size(): Int {
    radius
  }

  /** The distance from the origin. */
  fn length(): Int {
    radius
  }
}

/** Reads a point, under its old name. */
deprecated("the name says too little", replacement: "parsePoint", since: "0.4")
public fn parse(text: String): Point {
  parsePoint text
}

/** Reads a point from the length of a text. */
public fn parsePoint(text: String): Point {
  Point radius: text.byteLength()
}
```

A file that imports these and uses the old names gets one warning per use, and still builds:

```text
warning: `Point.x` is deprecated since 0.4: it is computed from the polar form now
 --> src/main.trb:4:13
  |
4 | print point.x
  |             ^
  = Write `x()` instead; `torb lint --fix --rule deprecated` rewrites it

1 file, no problems, 1 warning
```

## Syntax

```text
/** <doc comment> */
deprecated("<why>"[, replacement: "<name>" | "<name>()"][, since: "<major>.<minor>[.<patch>]"])
<declaration or member>
```

## Rules

1. **The clause stands on a line of its own, below the doc comment and above what it marks.** A doc comment between
   the clause and the declaration is an error; the comment belongs above the clause.

2. **The reason is required and comes first.** A warning prints it after "is deprecated since 0.4:", so it reads as the
   rest of that sentence.

3. **`replacement:` names what the declaration's own scope has**: another member of the same type for a member, a name
   the module declares or imports for a declaration of the module. `()` behind the name says that a use becomes a call
   without arguments, and is only allowed behind a method or a function. A name that does not resolve is an error at
   the clause:

   ```trb error
   type Point {
     deprecated("the name was a mistake", replacement: "horizontal")
     x: Int
   }

   print Point(1).x
   // error: `horizontal` is no member of `Point`
   ```

4. **`since:` is the version the declaration became deprecated in**, two or three numbers: `"0.4"`, `"1.2.3"`.

5. **Every argument is a string literal without an interpolation.** The compiler reads the clause; nothing of it runs.

6. **It marks what somebody names**: a `fn`, a `type`, a `trait`, a type alias, a `const` of a module, and a field, a
   method, a constant or a case of a type, a trait, an `extend` or a `foreign` block. On a `use`, an `extend`, a
   `foreign` block or a declaration inside a body it is an error:

   ```trb error
   fn outer() {
     deprecated("nobody outside calls it")
     fn inner() {
     }
     inner()
   }

   outer()
   // error: A declaration inside a body cannot be deprecated
   ```

7. **Every use outside the file that declares it is a warning.** A use is a name the checker resolved to the
   declaration: a member behind a `.`, a bare name, a case, the label of a constructor, a `copy` or a case, a name in a
   type position. Three places are no use: the file that declares it, a declaration that is deprecated itself, and a
   pattern - a `match` has to name every case for as long as the case exists.

8. **A warning is no problem.** `torb check` prints the warnings after the problems and counts them in its last line,
   `torb build` prints them and builds, and `torb run` and `torb test` print nothing of them: their output is the
   program's.

9. **A deprecated field may share its name with the method that replaces it.** A type has one namespace of members, and
   this is the one pair that may share a name: a field whose replacement is `"x()"` and a method `x` that takes nothing.
   `point.x` reads the field, with the warning, and `point.x()` calls the method. Without the clause, or with a method
   that takes an argument, the two names collide as they always do.

10. **`torb lint --fix --rule deprecated` writes the replacement at every use it can stand in**, and renames the `use`
    that imported a deprecated declaration. A read of the field `x` becomes `x()`; a method as the target of an
    assignment or the label of a constructor has no fix, and the finding stays.

## What this is not

**It is not a comment.** Go marks a deprecated declaration with a `Deprecated:` paragraph that only some tools read;
here the compiler reads the clause, checks what it names, and every tool - `torb check`, `torb lint`, `torb doc`, the
language server - says the same thing about it. A doc comment keeps saying what the declaration does.

**It is not an annotation.** The language has none (CONCEPT, Decision Log), and `deprecated` is no general mechanism
for attaching data to a declaration: it is one clause with three arguments, whose meaning the compiler knows.

**It is not a removal.** A deprecated declaration builds, runs and behaves as it did; it goes away in the next major
version, and until then the warning and the fix are how its callers learn and move.

## Related

- [Visibility](visibility.md) - what a declaration's `public` promises, which is what a deprecation keeps.
- [Why a method is a constant](../../explanation/why-one-member-namespace.md) - the one namespace of members, and the
  one pair rule 9 lets share a name.
- [torb lint](../../tooling/torb-lint.md) - the rule `deprecated` and its fix.
- [torb doc](../../tooling/torb-doc.md) - where a page says that a construct is deprecated.

