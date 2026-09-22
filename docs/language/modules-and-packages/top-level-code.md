---
title: Top-level code
summary: A statement outside every declaration is only allowed in an entry file, a script or a test file, and a top-level const of a module has to be known at compile time.
kind: reference
status: stable
order: 60
keywords:
  - top-level
  - entry file
  - script
  - compile-time constant
source:
  - CONCEPT.md#modules-and-packages
---

A file that nothing imports has no initialization order to protect, so it is free to run statements from top to
bottom like any other program. A file that is imported is not: it only ever contributes declarations.

## Example

```trb check
use File from "std/fs"

const text = File.readText("project.trb")?
print text.lines().length()
```

## Syntax

```text
const <name> = <compile-time expression>    a module's only top-level statement
<any statement>                             legal in an entry file, a script, or a *.test.trb file
```

## Rules

1. **Whether a file may have top-level code depends on being imported, not on its name.** `src/main.trb`, a receiver
   script, and every file of `examples/tour` qualify because nothing imports them; `src/lib.trb` of a named package
   never does, because it is what other packages import. `torb check src/main.trb` checks the files that file imports
   through a relative path as well, so top-level code in one of them is reported even when only the entry file is
   named, and `torb build` refuses the program.

2. **A `tests/*.test.trb` file consists of nothing but top-level `group` and `test` calls.** The test framework is
   ordinary functions, so a test file is a script like any other.

3. **In an entry file, a script or a test file, a top-level `?` ends the program with the error, printed and exited
   the same way as any other top-level failure** (see [Result](../errors/result.md) for the exact text and exit
   code).

4. **`await()` is allowed at the top level of an entry file or a script**, exactly as it is inside a function that
   returns a `Task`.

   ```trb check
   const created = spawn({ 1 + 1 })
   print created.await()
   ```

5. **A top-level `const` of a module has to be known at compile time.** Literals, unary minus, the operators of the
   built-in number types and of `Bool`, string interpolation of such, a tuple, list or map literal of such, and a
   constructor or case-constructor call whose arguments are such all qualify. A function call does not, and neither
   does a `native` call.

   ```trb skip a module cannot be produced inside one snippet of this documentation, which is always checked as an unimported file; the real diagnostic is: The initializer of a top-level `const` has to be known at compile time
   fn greeting(): String {
     "hello"
   }

   const message = greeting()
   ```

6. **A constant expression that overflows, divides by zero, or produces `nan` is a compile error at that
   expression.** This is the same rule as for any other overflow written in the source: caught where it is written,
   rather than at the moment the module happens to be loaded.

7. **A module's top-level `const` binds one name, never a pattern.** `const (quotient, remainder) = divide(7, 2)`
   destructures inside a function body or the top level of a script, but not here: `` A module's `const` binds one
   name ``, and, for a `public const`, `` An exported `const` binds one name ``. A name another file imports has to be
   one thing.

   ```trb skip a module cannot be produced inside one snippet of this documentation, which is always checked as an unimported file; the real diagnostic is: A module's `const` binds one name
   const (a, b) = (1, 2)
   ```

8. **The top-level code of the entry file is the program, and no function is called for it.** `fn main()` is an
   ordinary function: nothing calls it unless the top-level code does. An entry file with no top-level code at all -
   declarations and `const`s only - has nothing to run.

   ```trb run
   fn main() {
     print "main runs because the top-level code calls it"
   }

   main()   // prints main runs because the top-level code calls it
   ```

9. **`public const` exports the constant; there is no `public var` at the top level of a module.** A module has no
   mutable state, so nothing a top-level `var` could export exists in the first place - see
   [Visibility](visibility.md).

10. **A top-level `var` is changed by the statements of its file and by nothing else.** The statements - the
    initializers of top-level `const`s among them - run in the order they are written, and a closure written straight
    as the argument of a call that only calls it runs while its statement does. A `fn` may run anywhere, also while a
    `var` access to the declaration is open ([Exclusivity](../types/exclusivity.md)), and so may a closure that is kept:
    both read a top-level `var`, and neither changes one. What a function has to change, it takes as a `var` parameter.

    ```trb check
    fn addTo(var sum: Int, amount: Int) {
      sum = sum + amount
    }

    var total = 0
    addTo total, 5
    print total
    ```

    ```trb error
    var total = 0

    fn bump() {
      total = total + 1
    }

    bump()
    print total
    // error: `total` is a top-level `var`, and only the statements of its file change it
    ```

    ```trb error
    var total = 0
    const reset = {
      total = 0
    }
    reset()
    print total
    // error: This closure changes the top-level `var` `total` and may outlive its statement
    ```

## What this is not

**Top-level code is not something a module can opt into.** Whether a file may hold a statement outside a declaration
follows from whether anything imports it, and nothing in the file itself changes that.

```trb check
use File from "std/fs"

const text = File.readText("project.trb")?
print text
```

```trb skip a module's top-level statement can only be produced by a file something else imports, which this single-file gate cannot construct; the real diagnostic is: Top-level code is only allowed in entry files. `<path>` is imported
type Config {}

print "loading"
```

## Related

- [Visibility](visibility.md) - `public` at the top level, the other half of what a file exports.
- [Cyclic imports](cyclic-imports.md) - why a module having no initialization order is what makes a cycle harmless.
- [Result](../errors/result.md) - what a top-level `?` prints and exits with.
