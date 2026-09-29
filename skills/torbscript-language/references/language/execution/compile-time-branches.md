---
title: Compile-time branches
summary: A match, an if or an if const whose subject is a compile-time constant keeps the one arm its value selects - OperatingSystem.current is one - while every arm is still type checked on every machine and exhaustiveness is judged by the type.
kind: reference
status: stable
order: 60
keywords:
  - OperatingSystem.current
  - Architecture.current
  - ByteOrder.current
  - conditional compilation
  - target
  - cfg
  - ifdef
  - every-target
source:
  - docs/design/OS.md#2-the-construct-a-match-on-the-target
  - std/core/src/target.trb
---

A branch on the operating system is an ordinary `match` on `OperatingSystem.current`. The compiler knows the value,
so it compiles only the arm the value selects - and it still type checks every arm, on every machine, so a mistake in
the Windows arm is found on Linux.

## Example

```trb run
use OperatingSystem from "std/core"

fn searchPathSeparator(): String {
  match OperatingSystem.current {
    .Windows => ";"
    .Linux | .MacOs | .FreeBsd | .Browser => ":"
  }
}

print searchPathSeparator().byteLength()
// prints 1
```

## Syntax

```text
match <compile-time constant> {
  <pattern> => <expression>      // only the arm the value selects is compiled
  ...
}

if <compile-time constant Bool> { ... } else { ... }
if const <pattern> = <compile-time constant> { ... } else { ... }
```

## Rules

1. **`OperatingSystem.current`, `Architecture.current` and `ByteOrder.current` are compile-time constants.** The
   three types are in `std/core` and not in the prelude, so a file that branches on where it runs says so with an
   import. Their value is the target the program is compiled for: the `--target` of `torb build`, the machine `torb`
   runs on by default, and in the VM the machine the VM runs on, because the VM compiles a program where it runs it.

2. **A `match` whose subject is a compile-time constant keeps one arm.** Only the arm the value selects is compiled;
   the others produce no code, and a function only they call is not part of the program. A compile-time constant is a
   literal (`true`, `3`, `'a'`, a text without interpolation), a case without fields, a tuple of constants, `!`, `&&`,
   `||`, the comparisons and `+`, `-`, `*` of integers, `==` and `!=` of those and of two cases whose `Equals` is
   derived, a `const` of a module or a `static` of a type whose initializer is one, and the three target constants. A
   tuple decides two dimensions at once:

   ```trb run
   use OperatingSystem, Architecture from "std/core"

   fn knownPageSize(): Int? {
     match (OperatingSystem.current, Architecture.current) {
       (.MacOs, .Arm64) => 16384
       (.Windows, _) => 4096
       (_, _) => None
     }
   }

   print(knownPageSize() != 0)
   // prints true
   ```

3. **Every arm is still type checked, on every machine, for every target.** An arm another target takes is checked
   exactly like the one this build takes, so a mistake in it is an error here:

   ```trb error
   use OperatingSystem from "std/core"

   fn systemName(): String {
     match OperatingSystem.current {
       .Windows => windowsName()
       .Linux | .MacOs | .FreeBsd => "posix"
       .Browser => "browser"
     }
   }
   print systemName()
   // error: Cannot find `windowsName` here
   ```

4. **Exhaustiveness and reachability are judged by the type, never by the value.** A `match OperatingSystem.current`
   handles every operating system or writes `_`, and no arm is unreachable because the value selects another one. So
   adding an operating system to `OperatingSystem` makes every such `match` an error that names the new case - the
   list of places that have to learn about it.

   ```trb error
   use OperatingSystem from "std/core"

   fn systemName(): String {
     match OperatingSystem.current {
       .Windows => "windows"
       .Linux => "linux"
     }
   }
   print systemName()
   // error: `match` does not handle `.MacOs`, `.FreeBsd` and `.Browser`
   ```

5. **An `if` whose condition is a compile-time constant `Bool`, and an `if const` whose subject is a compile-time
   constant, are decided the same way.** They are not exhaustive, so a branch on the target is written as a `match`
   wherever a new operating system should be a compile error.

   ```trb run
   use OperatingSystem from "std/core"

   fn lineEnding(): String {
     if OperatingSystem.current == .Windows {
       return "\r\n"
     }
     "\n"
   }

   print lineEnding().endsWith("\n")
   // prints true
   ```

6. **A local `const` is not a compile-time constant**, and neither is a top-level `const` of an entry file or a script,
   which is a local of the code the file runs. After `const system = OperatingSystem.current`, `match system` is an
   ordinary `match`, and every arm is compiled. Whether an arm is compiled depends on the line the `match` is written on,
   never on data flow a reader has to follow.

7. **The patterns decide, and a guard is code.** Where the value reaches an arm with an `if` guard first, the `match`
   is an ordinary one from there, and every arm is compiled.

8. **A native that exists on some targets only may be reached only through arms those targets take.** Reaching it on
   another target is a compile error of that target, at the call:

   ```text
   error: `Windows.tickCount` exists only on Windows, and this program is built for Linux
    --> src/main.trb:4:7
     |
   4 | print Windows.tickCount()
     |       ^^^^^^^^^^^^^^^^^
     = Only the arm of `match OperatingSystem.current` that the target selects is compiled
     = Reach it only from a `.Windows` arm, or check every target with `torb check --every-target`
   ```

   `torb check --every-target` lowers every program and test once per target of the toolchain's list, without a C
   compiler, so this error is reported on any machine for a mistake only another system's build would meet.

## What this is not

**Not a preprocessor.** Every arm is TorbScript that has been parsed, resolved and type checked, and no declaration
exists on one target and not on another: a module of Windows code is checked in every build on every machine, and it
drops out of a Linux binary because nothing a Linux build compiles reaches it. `#ifdef`, `#[cfg]` and build tags
remove text before it is checked, which is how a change on one machine breaks the build of another unseen.

**Not a cost at run time.** No comparison is executed and no code for another system is in the binary. The C compiler
is not asked to remove the other arms: the lowering never writes them, because an arm that is written references its
natives, and a native of another system has no C function on this one.

## Related

- [Exhaustiveness](../pattern-matching/exhaustiveness.md) - the rules a `match` on the target is checked by.
- [Bindings](../values-and-types/bindings.md) - why a local `const` is not a compile-time constant.
- std/core (skill `torbscript-standard-library`: `references/standard-library/core.md`) - `OperatingSystem`, `Architecture` and `ByteOrder`.
- torb check (skill `torbscript`: `references/tooling/torb-check.md`) - `--every-target`.
- torb build (skill `torbscript`: `references/tooling/torb-build.md`) - `--target`.

