# The Operating System

**Status: decided; slice 0 (the construct) is implemented, slices 1 to 7 are not.** `OperatingSystem`,
`Architecture` and `ByteOrder` are in `std/core/src/target.trb`; the lowering keeps the one arm of a `match`, an `if` or
an `if const` on a compile-time constant (`compiler/src/ir/fold.trb`); a manifest row names the systems its native
exists on (`availableOn`), and reaching one on another target is an error of that target at the call; `torb build
--target <os>-<arch>` sets the constants and `torb check --every-target` lowers every program and test once per target
without C; `runtime/os/` holds one guarded file per family and `runtime/include/torb_os.h` every prototype. `std/os` is
a skeleton: its `lib.trb` re-exports the three types, and `windows/native.trb` and `posix/native.trb` hold one native each
(`Windows.tickCount`, `Posix.effectiveUserIdentifier`), which the conformance program `tests/conformance/target-branch.trb`
calls from the arms of its own system. `std/environment` still exists, and `Process.executablePath()` in `std/process`
still answers on Windows and Linux only.

**An operating-system branch is a `match`, and the compiler decides it.** That is the whole design of `std/os`.
Operating systems differ and always will, so the language does not hide the branch in C or in a build file: it is
written in TorbScript, where it happens, as a `match` over a constant the compiler knows. Every arm is type checked on
every machine, the arms the target does not take are never compiled, and adding an operating system is a list of
compile errors that names every place that has to learn about it.

```text
   std/core/target        OperatingSystem.current   Architecture.current   ByteOrder.current
                          compile-time constants: the build's --target, or the machine the VM runs on
                                       │
                                       ▼
   std/os                 lib.trb ── system  processor  memory  volumes  network
   (OS-neutral surface)              environment  user  directories  process        each a `match OperatingSystem.current`
                                       │
               ┌───────────────┬───────┴───────┬───────────────┬──────────────┐
               ▼               ▼               ▼               ▼              ▼
   one directory per       windows/        linux/          macos/         freebsd/
   operating system        registry,       /proc and       mach,          sysctl
                           Known Folders   /sys parsers    Library/       names
                                               │               │              │
   one directory per                           └──── posix/ ───┴──── bsd/ ────┘     x86/  (cpuid, per architecture)
   standard they share                               uname, statvfs,  sysctl,
                                                     getifaddrs       getmntinfo
                                       │
                                       ▼
   runtime/os/*.c         windows.c  linux.c  posix.c  bsd.c  macos.c  freebsd.c  x86.c
                          thin natives, one guard per file, prototypes compiled on every machine
```

- **[1. What exists today](#1-what-exists-today)** — one native, one half-portable path, and `platform.c`
- **[2. The construct: a `match` on the target](#2-the-construct-a-match-on-the-target)** — the centrepiece, and a proposed language rule
- **[3. The package](#3-the-package)** — the module tree, the three kinds of directory, and who owns what
- **[4. Units, errors and waiting](#4-units-errors-and-waiting)** — `ByteSize`, `Frequency`, `OsError`, and which calls are a `Task`
- **[5. The signatures](#5-the-signatures)** — every topic, written out
- **[6. What moves](#6-what-moves)** — `std/environment`, `Process.executablePath`, `megabytes`
- **[7. The natives](#7-the-natives)** — per family, and what is TorbScript above them
- **[8. The sandbox](#8-the-sandbox)** — a capability per module, none by default
- **[9. What the compiler and the runtime must provide](#9-what-the-compiler-and-the-runtime-must-provide)**
- **[10. Slices](#10-slices)** — eight, in the order others need them
- **[11. What this is not](#11-what-this-is-not)**
- **[12. Open](#12-open)**

`std/os` replaces `std/environment` (one type, one native), takes `executablePath` out of `std/process`, and gives a
home to everything a program asks about the machine it runs on: which operating system and version, how many
processors and how busy they are, how much memory, which volumes and how full, which network interfaces with which
addresses, who the user is, and where that user's configuration, data, cache and temporary files belong.

---

## 1. What exists today

| Where | What | The problem it leaves |
|---|---|---|
| `std/environment` | `native type Environment { native static fn get(name: String): String? }` | Nothing else about the environment: no listing, no search path, no way to hand a child a changed one |
| `std/process` | `Process.executablePath(): String?` | Answers `None` on macOS and the BSDs, because `runtime/platform.c` knows only `/proc/self/exe` and `GetModuleFileNameW` |
| `std/time` | `Clock`, `Instant`, `Duration`, `sleep` | No wall-clock point, so there is nothing a boot time could be |
| `std/sandbox` | `extend Int64 { fn megabytes(): Int64 }` | A byte count is a bare `Int64`, and its one unit member lives in the sandbox |
| `runtime/platform.c` | fourteen functions behind `#if defined(_WIN32)`, the only file with an `#ifdef` | Two halves only: Windows and "POSIX", which in practice means Linux |
| `compiler/src/backend/c/natives.trb` | every `native` of `std/`, keyed `Owner.member`, `.Ready` or `.Planned(milestone)` | No notion of a native that exists on one target and not on another |

The C runtime already has the discipline this design extends: every OS difference is behind one function of the
platform layer, UTF-8 goes in and comes out on both halves, and the Windows half calls the wide API
(`runtime/platform.c`, its header comment). What it does not have is a place for a third and a fourth operating
system, and a way for TorbScript code to say "on Windows, this; on Linux, that" without pushing the whole difference
into C, where it is compiled on one machine only and cannot be read next to the code that uses it.

`docs/design/PROJECT.md` section 5 already separates the **target** (an operating system and an architecture, chosen
with `torb build --target linux-x64`, the host by default) from the **profile**. It says a package that works on one
target only "says so by failing to compile there". This design is what makes that failure precise.

## 2. The construct: a `match` on the target

### The target constants

Three types in `std/core`, in a module of their own (`std/core/src/target.trb`), each with one `static` constant:

```trb
/** The operating systems the toolchain builds for. A case is added when a target is. */
public type OperatingSystem with Show, Equals, Hash {
  case Windows
  case Linux
  case MacOs
  case FreeBsd

  /**
   * The operating system this program is compiled for: the `--target` of the build, the host by default, and in the
   * VM the machine the VM runs on. A compile-time constant: a `match` on it keeps one arm (section 2).
   */
  static current: OperatingSystem = targetOperatingSystem()

  /** Whether the system follows POSIX, which is every case but `Windows`. */
  fn isPosix(): Bool {
    match self {
      .Windows => false
      .Linux | .MacOs | .FreeBsd => true
    }
  }

  /** The name as its vendor writes it: `Windows`, `Linux`, `macOS`, `FreeBSD`. */
  fn show(): String {
    match self {
      .Windows => "Windows"
      .Linux => "Linux"
      .MacOs => "macOS"
      .FreeBsd => "FreeBSD"
    }
  }
}

/** The processor architectures the toolchain builds for. */
public type Architecture with Show, Equals, Hash {
  case X64
  case Arm64

  /** The architecture this program is compiled for. A compile-time constant, like [OperatingSystem.current]. */
  static current: Architecture = targetArchitecture()

  /** `x86-64`, `arm64`. */
  fn show(): String {
    match self {
      .X64 => "x86-64"
      .Arm64 => "arm64"
    }
  }
}

/** The order of the bytes of a number in memory, for binary formats that say "native order". */
public type ByteOrder with Show, Equals, Hash {
  case LittleEndian
  case BigEndian

  /** The byte order of the target. A compile-time constant, like [OperatingSystem.current]. */
  static current: ByteOrder = targetByteOrder()
}

/** The build's operating system. An intrinsic the constant evaluator reads from the build: it runs no code. */
native fn targetOperatingSystem(): OperatingSystem

/** The build's architecture, read the same way. */
native fn targetArchitecture(): Architecture

/** The build's byte order, read the same way. */
native fn targetByteOrder(): ByteOrder
```

**`std/core` and not `std/os`, and not the prelude.** Knowing what a program was compiled for touches nothing, so it
is not a capability and does not belong in the package whose import says "this file asks the operating system". It is
needed by `std/fs`, `std/process` and the compiler itself (which today infers `.exe` and the C compiler from the host),
none of which should depend on `std/os` for a constant. `std/os` re-exports all three, so a program that uses `std/os`
writes one import. They are not in the prelude, because a branch on the target is rare in application code and the
import is the statement "this file branches on where it runs".

**Pointer width is not a constant, and not a dimension.** `Int` is `Int64` on every target, and no value of the
language has a size that depends on the machine, so nothing a TorbScript program can observe changes with the pointer
width. It matters inside the C runtime and nowhere above it. `ByteOrder.current` is included although every target
today is little endian, because the programs that need it are exactly the ones this package contains: a
`kern.boottime` from `sysctl` is a C `struct timeval` in native order, and the TorbScript that decodes it says which
order it assumes instead of assuming it silently.

### One branch, written where it happens

In an expression:

```trb
/** The separator between the entries of `PATH`. */
fn searchPathSeparator(): String {
  match OperatingSystem.current {
    .Windows => ";"
    .Linux | .MacOs | .FreeBsd => ":"
  }
}
```

Over two dimensions at once, because a tuple of constants is a constant:

```trb
/** The size of a memory page, where the target fixes it and no call is needed. */
fn knownPageSize(): Int? {
  match (OperatingSystem.current, Architecture.current) {
    (.MacOs, .Arm64) => Some 16384
    (.Windows, _) => Some 4096
    (_, _) => None
  }
}
```

As a whole function body, handing each operating system to the code that knows it:

```trb
use * as windows from "./windows/memory"
use * as linux from "./linux/memory"
use * as macos from "./macos/memory"
use * as freebsd from "./freebsd/memory"

/** The memory of this machine, now. */
public fn currentMemory(): Result<Memory, OsError> {
  match OperatingSystem.current {
    .Windows => windows.memory()
    .Linux => linux.memory()
    .MacOs => macos.memory()
    .FreeBsd => freebsd.memory()
  }
}
```

**The guideline for where an arm's code lives:** an arm that is one expression stays in place; an arm that needs a
function goes to the directory of its operating system, and the central module only chooses. A reader of
`memory.trb` sees in six lines that there are four answers and where each one is; a reader of `linux/memory.trb` sees
only Linux.

**Probe, type checks and builds today** — the tuple match and the namespace-imported per-OS modules above, with
`static current: OperatingSystem = OperatingSystem.Linux` standing in for the intrinsic, in a scratch package with a
`windows.trb` and a `linux.trb` beside `src/main.trb`: `torb check .` answers `3 files, no problems`, `canon --check`
changes nothing, and `torb run .` prints `4096`, `:` and `true`. Everything this section proposes beyond that is the
compiler knowing the value and dropping the arms it does not select.

### A module that belongs to one operating system

**A declaration never exists on one target and not on another.** That is the rule that makes everything else work,
and it is the one thing `cfg`, build tags and `#if` all give up. The Windows directory's code is ordinary TorbScript:
it is parsed, resolved and type checked in every build on every machine, and `linux/memory.trb` can import a type from
`windows/` without a build breaking anywhere. What makes a module *Windows code* is two facts, one readable and one
enforced:

- **Readable:** it lives in `windows/`, and the only calls into it are in `.Windows` arms.
- **Enforced:** its natives exist on Windows only, the manifest of natives says so (`availableOn`, section 9), and a
  native that is reached on a target it does not exist on is a compile error for that target. `torb check
  --every-target` lowers every program and test once per target, on any machine, without a C compiler, so that error
  is seen on Linux for a mistake that only a Windows build would have hit.

So the whole of `windows/` drops out of a Linux binary without anybody deleting it: `torb build` seeds the worklist
with the entry function alone (`compiler/src/ir/lower/lower.trb`), the only references to `windows/` are in arms that
are not lowered, and a function nothing reaches is never lowered. No module is ever "excluded"; it is simply not
reached.

**What stays OS-neutral in shape is every declaration; what differs is behaviour, and behaviour is an expression.** A
type whose representation would differ per system — a `stat` structure for a foreign call, a registry value — is two
declarations with two names (`LinuxStatus`, `MacOsStatus`), each used in its own arm, and never one name with two
shapes.

### What the compiler does

1. **The checker does nothing new.** It already checks every arm of every `match`, judges exhaustiveness by the type
   of the subject, and reports an arm as unreachable only when a pattern above it covers it
   (`docs/language/pattern-matching/exhaustiveness.md`, rule 5). An arm that another target takes is reachable by type,
   so it is never reported. `torb check` needs no target at all.
2. **The constant evaluator knows three more values.** `compiler/src/ir/constant.trb` evaluates what
   `docs/TYPECHECKER.md` 5.6 calls compile-time evaluable — literals, constructors and cases of such, tuples, references
   to other constants, arithmetic and comparison of numbers and `Bool` — and no call. It gains the three target
   intrinsics, which are not code but parameters of the build, and `==` and `!=` between two cases without fields.
   `OperatingSystem.current` is then a compile-time constant by the existing rule: a `static` whose initializer is
   evaluable. *As built:* the checker's evaluator (`semantics/checker/constant.trb`) accepts the three calls, so a
   module `const` may read the target; the decision itself has an evaluator of its own, `compiler/src/ir/fold.trb`,
   because `ir/constant.trb` produces static data and a case of a variant layout is none - the fold needs only to know
   which arm, and emits nothing.
3. **The lowering decides a `match` whose subject is a compile-time constant.** It lowers the arm the value selects,
   binds its pattern, and lowers nothing else: no block, no test, no call. An `if` whose condition is a constant `Bool`
   and an `if const` whose subject is a constant are decided the same way. This is not an optimization the C compiler
   might perform — it has to happen in the lowering, because an arm that is lowered references its natives, and a
   native of another operating system has no C function on this one.
4. **The VM does the same at load.** The VM compiles a program on the machine it runs it on, so its target is that
   machine, the constants have that machine's values, and the arms for other systems are never turned into bytecode.
   A compiled binary and the VM therefore run the same arm on the same machine, which is CONCEPT's principle 5 held by
   construction rather than by testing. *For the VM:* the fold is part of the lowering, so a VM that reads the typed IR
   gets it by building its `Lowering` with the machine it runs on as `target` (`hostTarget()` in
   `compiler/src/project/target.trb`); a VM that compiles from the checked tree itself calls `knownValueOf` and
   `selectionOf` of `ir/fold.trb` where it compiles a `match`, an `if` or an `if const`, and answers a `.Target` native
   (`NativeTarget.Target` in the manifest) with the case of its machine.

**The cost at run time is zero**, in both implementations: no comparison is executed, no table is consulted, no code
for another system is in the binary, and nothing is loaded lazily.

### Every arm, on every machine

Three levels, and a mistake in the `.Windows` arm is caught on a Linux machine at the first one that applies:

| Level | What it runs | What it catches on a machine that is not the target |
|---|---|---|
| `torb check` | the type checker over every module, every arm | a wrong type, a missing function, a non-exhaustive `match`, a misspelt field — in every arm, for every system |
| `torb check --every-target` | the lowering, once per target in the toolchain's list, stopping before C | a native reached on a target it does not exist on, and every other lowering error of a target |
| `torb test` of the parsers | the pure TorbScript that reads `/proc/meminfo`, `os-release`, a `sysctl` structure, a registry product name | a parser that misreads its input: its tests run with a copy of the file on every machine |

What is left unverified off the target is the C body of each native in `runtime/os/<family>.c`. Its prototype is
compiled everywhere (section 7), so a signature cannot drift; its body is compiled on its own system only. **That is
the reason the natives are thin and the parsing is TorbScript**: every line moved from C into a TorbScript parser is a
line that is checked, lowered and tested on every machine, and a line left in C is checked on one.

The diagnostic of the second level names the native, the target and the path to it, and says what to write:

```text
error: `Windows.memoryStatus` exists only on Windows, and this program is built for Linux
  --> std/os/src/memory.trb:31:17
   |
31 |       .Linux => windows.memory()
   |                 ^^^^^^^^^^^^^^^^
   = reached through `Memory.current` → `windows.memory`
   = reach it only from a `.Windows` arm of `match OperatingSystem.current`
```

### Adding an operating system

A new case of `OperatingSystem` is one line in `std/core/src/target.trb`, and then **every
`match OperatingSystem.current` in the workspace is a compile error** that names the missing case, on every machine.
The list of errors is the work list: every place where std/os has to learn about the new system, and every place in a
user's program that branches on the target. An arm written `_` is the deliberate "the others behave like this", and it is the one place the new
system is silently included — which is why std/os itself writes `_` only where the answer is genuinely the same for
every system that will ever exist (a `Windows` arm and a `_` for "everything POSIX" is not that, and is written out).

The same is true of a public enum anywhere in the language, and the consequence is the same: **adding a case to
`OperatingSystem` is a breaking change of `std/core`**, and a program that must survive it writes `_` or asks
`isPosix()`.

### What was weighed

| Option | For | Against | Verdict |
|---|---|---|---|
| **(a) every difference in C**, one native per question, `#ifdef` inside it | works today; TorbScript stays one piece | the owner's direction: the branch belongs in TorbScript. And C is compiled on its own system only, so a Linux edit breaks the Windows half unseen; parsing `/proc` in C is the least checked code in the repository | **rejected** as the construct; kept as a rule for the C side: natives are thin |
| **(b) the manifest selects a source directory per target** (`target "windows" { input "src/windows" }`) | Go and Cargo users know it | only the selected directory is compiled, so a Linux edit breaks Windows unseen; names exist on one target and not another; it is conditional compilation, moved into the project file; the VM would need the manifest to know which files a script may see | **rejected** |
| **(c) a `match` on a compile-time constant**, decided by the compiler | no new syntax, no new concept: `match`, exhaustiveness and compile-time constants all exist; every arm checked everywhere; zero cost; identical in the VM | the lowering must decide constant matches (a small, general change), and the constant evaluator must know the target | **chosen** |
| **(d) a trait per topic, one implementing type per system, and a registry value** (`const system: Kernel = WindowsKernel()`) | a named contract for what a new system has to implement | a trait-typed value dispatches through a witness at run time unless devirtualized, and a list or map of implementations reaches every system's witness table — so every system's natives are in every binary. The contract it would add is already there: an arm must call something of the right type, so a missing function is a type error on every machine | **rejected** as the selection. Nothing stops a topic from using a trait internally where it has one |
| **(e) Kotlin's `expect` / `actual`**, a declaration per system with a common signature | the signature is checked against each implementation | two new declaration modifiers for what an exhaustive `match` already says; each system is compiled separately, so the checking happens per target build | **rejected** |

**Option (c) is also the one that reaches below std/os.** A `match OperatingSystem.current` works the same in a user's
program, in `std/fs`, in the compiler, and in a `foreign` binding: a `foreign` declaration reached only through an arm
the target does not take is not emitted, so its library is not linked either.

### What other languages do

| Language | The construct | Other targets type checked on this machine | Exhaustive | A name can exist on one target only | New syntax |
|---|---|---|---|---|---|
| **C, C++** | `#ifdef _WIN32` | no — the preprocessor removes the text before the compiler sees it | no | yes | a preprocessor |
| **Rust** | `#[cfg(windows)]` on items, `cfg!(windows)` as an expression | `#[cfg]` items: no, and `cargo check --target` needs that target's standard library. `cfg!()`: yes, both branches — so the dead branch may only call what exists on *every* target | no | yes | attributes |
| **Zig** | `switch (builtin.os.tag)`, comptime | **no** — analysis is lazy, a branch that is never taken is never analysed, so a Linux edit can break the Windows build unseen | yes | yes, through comptime declarations | none |
| **Go** | `_windows.go` suffixes, `//go:build` lines | not by default; `GOOS=windows go vet` checks another target cheaply | no — a missing file is "undefined" on that target only | yes, per file | constraint comments |
| **Swift** | `#if os(Linux)` | no — an inactive block is parsed, not checked | no | yes | a directive |
| **Kotlin Multiplatform** | `expect` / `actual` per source set | per target compilation | each `expect` needs an `actual` per target | no | two modifiers |
| **Nim** | `when defined(windows)` | no | no | yes | none |
| **C# / .NET** | `OperatingSystem.IsWindows()`, which the JIT folds, plus `[SupportedOSPlatform]` and the CA1416 analyzer | yes — it is an ordinary call | no | no | attributes for the analyzer |
| **TorbScript** | `match OperatingSystem.current` | **yes, every arm, always** | **yes** | **no** | **none** |

**What is taken.** Zig's idea that the target is a value and the branch is an ordinary `switch` is the core of it —
TorbScript's `match` is the same idea with its exhaustiveness. .NET is the closest ancestor in behaviour: the check is
an ordinary expression that every compilation type checks, the JIT removes the branch, and an analyzer reports a
platform-specific API reached without a guard. Go's `GOOS=windows go vet` is the model for `--every-target`: checking a
target must not need that target's toolchain.

**What is left.** Everything that removes text before it is checked (`#ifdef`, `#if os()`, `#[cfg]`, `when`), because a
branch that is not checked on the machine that edits it is the branch that breaks. Zig's laziness, for the same
reason. Go's per-file selection, because a name that exists on one target is a name whose absence is discovered by
somebody else. .NET's attributes, because the language has none — and it does not need them: the only things that are
truly bound to one operating system are natives and foreign declarations, the toolchain has a manifest of the natives
already, and "may this be called on this target" is therefore a row of a table rather than a mark on user code.

### The rule, as it would read in `docs/language/`

**A proposal for the owner.** It is the one language addition of this design, and it is general: it is not about
operating systems, it is about a `match` whose subject the compiler already knows. It would be a new page,
`docs/language/execution/compile-time-branches.md`:

> **A `match` on a compile-time constant is decided when the program is compiled.**
>
> 1. **`OperatingSystem.current`, `Architecture.current` and `ByteOrder.current` are compile-time constants.** Their
>    value is the target the program is compiled for: the `--target` of `torb build`, the machine `torb` runs on by
>    default, and in the VM the machine the VM runs on, because the VM compiles a program where it runs it.
> 2. **A `match` whose subject is a compile-time constant keeps one arm.** A compile-time constant is what a module's
>    `const` may be (literals, cases and constructors of constants, tuples of constants, references to other
>    constants, the operators on numbers and `Bool`, `==` and `!=` between cases without fields) plus the three target
>    constants. Only the arm the value selects is compiled; the others produce no code, and a function that only they
>    call is not part of the program.
> 3. **Every arm is still type checked, on every machine, for every target.** A mistake in the `.Windows` arm is
>    reported by `torb check` on Linux.
> 4. **Exhaustiveness and reachability are judged by the type, never by the value.** A `match OperatingSystem.current`
>    handles every operating system or writes `_`, and no arm is unreachable because the value selects another one.
> 5. **An `if` whose condition is a compile-time constant `Bool`, and an `if const` whose subject is a compile-time
>    constant, are decided the same way.** They are not exhaustive, so a branch on the target is written as a `match`
>    wherever a new operating system should be a compile error.
> 6. **A local `const` is not a compile-time constant** (see [Bindings](../values-and-types/bindings.md)): after
>    `const system = OperatingSystem.current`, `match system` is an ordinary `match`, and every arm is compiled.
> 7. **A native that exists on some targets only may be reached only through arms those targets take.** Reaching it on
>    another target is a compile error for that target that names the native and the path to it;
>    `torb check --every-target` reports it on any machine.
>
> **What this is not:** a preprocessor. Every arm is TorbScript that has been parsed, resolved and checked, and no
> declaration exists on one target and not on another.

Rule 6 is deliberate and not a limitation to lift later. Propagating a constant through a local binding would make
"is this arm compiled" depend on data flow the reader has to reconstruct; the rule as written makes it depend on the
line the `match` is written on.

### The C side mirrors it

`runtime/os/` holds one file per family (section 7), and **each file is guarded as a whole**: the first line after its
includes is `#if defined(_WIN32)` (or `__linux__`, `__APPLE__`, `__FreeBSD__`, the POSIX set, the BSD set,
`__x86_64__ || _M_X64`) and the last line is its `#endif`. No function has an `#ifdef` inside it. So `runtime/build.sh`
and the driver compile every file of `runtime/os/` on every machine, each compiles to nothing where it does not
belong, and the C side has the same shape as the TorbScript side: one place per system, chosen once, never interleaved.

`runtime/platform.c` stays what it is — the runtime's own portability layer, for what the runtime itself needs (files,
processes, the clock, the environment). **The rule for where a native's C goes:** into `platform.c` when the runtime
or a package below `std/os` needs it, into `runtime/os/<family>.c` when only `std/os` does.

## 3. The package

### The module tree

```text
std/os/
  project.trb
  src/
    lib.trb                  re-exports the OS-neutral surface and the three target constants
    error.trb                OsError, and the one helper that turns a native's outcome into a Result
    system.trb               System: version, host name, uptime, boot time, page size, machine architecture
    processor.trb            Processor, ProcessorDescription, ProcessorTimes, ProcessorUsage, ProcessorFeature,
                             ProcessorCache, Frequency
    memory.trb               Memory, Swap
    volumes.trb              Volume, VolumeKind, VolumeSpace
    network.trb              NetworkInterface, InterfaceAddress, InterfaceStatus, InterfaceKind, InterfaceTraffic
    environment.trb          Environment, EnvironmentVariables
    user.trb                 User
    directories.trb          Directories
    process.trb              CurrentProcess, ProcessResources

    windows/                 native.trb (type Windows), registry.trb, system.trb, processor.trb, memory.trb,
                             volumes.trb, network.trb, user.trb, directories.trb, process.trb
    linux/                   native.trb (type Linux), files.trb (readSystemFile), system.trb, processor.trb,
                             memory.trb, volumes.trb, network.trb, process.trb, control.trb (cgroups)
    macos/                   native.trb (type MacOs), system.trb, processor.trb, memory.trb, directories.trb,
                             process.trb
    freebsd/                 native.trb (type FreeBsd), system.trb, processor.trb, memory.trb, process.trb

    posix/                   native.trb (type Posix): uname, sysconf, statvfs, getifaddrs, getpwuid_r, getrusage
                             system.trb, volumes.trb, network.trb, user.trb, process.trb
    bsd/                     native.trb (type Bsd): sysctl, getmntinfo — shared by macOS and FreeBSD
                             sysctl.trb (the structure decoders), volumes.trb, network.trb
    xdg/                     directories.trb: the XDG base directories and user-dirs.dirs — Linux and FreeBSD
    x86/                     native.trb (type X86): cpuid, xgetbv — processor.trb decodes vendor, brand, features
  tests/
    windows.test.trb, linux.test.trb, macos.test.trb, freebsd.test.trb, bsd.test.trb, xdg.test.trb, x86.test.trb
                             the parsers and decoders over copies of real inputs, run on every machine
    host.test.trb            the neutral surface against the machine the test runs on
```

### Three kinds of directory, and one rule for naming them

1. **One directory per case of `OperatingSystem`** — `windows/`, `linux/`, `macos/`, `freebsd/`. What only that system
   has. A new case of the type is a new directory.
2. **One directory per standard that several systems share** — `posix/`, `bsd/`, `xdg/`. Named after the standard it
   implements, never after the set of systems that happens to use it today: there is no `unix/` and no `others/`,
   because "the systems that are not Windows" is not a thing code can be written against, and POSIX, the BSD `sysctl`
   interface and the freedesktop.org base directories are.
3. **One directory per architecture whose instructions answer an OS-neutral question** — `x86/`, because `cpuid`
   reads the vendor, the brand string and the feature bits the same way on every operating system. On Arm64 the same
   questions go to the operating system (`getauxval` on Linux, `hw.optional.*` on macOS, `IsProcessorFeaturePresent` on
   Windows), so there is no `arm64/`.

**Every per-system module exports the functions its central module calls, under the same names and with the same
signatures.** That is a convention, and the central `match` enforces it: `.Windows => windows.memory()` does not type
check on any machine if `windows/memory.trb` has no `memory` or answers something else. There is no trait that
repeats the list, because the list is already written once, as the arms (option (d) above).

**The per-system modules are reachable, and that is intended.** TorbScript has no package-private visibility, so
`use Windows from "std/os/windows/native"` compiles. A program that needs what only one system has — a registry read,
a cgroup limit — imports it from that directory and reaches it from that system's arm; the import says "this file has
Windows-only code" and `--every-target` holds it to that. The `native.trb` modules are the raw layer and not part of
the documented surface; `docs/standard-library/os.md` documents the neutral modules and names the per-system ones.

### How a topic is brought together

The central module holds the types and chooses; the per-system modules know one system each. For memory:

```trb
// std/os/src/memory.trb
use OperatingSystem from "std/core"
use OsError from "./error"
use * as windows from "./windows/memory"
use * as linux from "./linux/memory"
use * as macos from "./macos/memory"
use * as freebsd from "./freebsd/memory"

/** How much memory the machine has, and how much of it a new allocation could still get without swapping. */
public type Memory with Show, Equals {
  total: ByteSize
  available: ByteSize
  swap: Swap

  /** What is not available: `total - available`. */
  fn used(): ByteSize {
    total - available
  }

  /** The memory of this machine, now. Every system keeps these numbers in the kernel, so it never waits. */
  static fn current(): Result<Memory, OsError> {
    match OperatingSystem.current {
      .Windows => windows.memory()
      .Linux => linux.memory()
      .MacOs => macos.memory()
      .FreeBsd => freebsd.memory()
    }
  }
}
```

```trb
// std/os/src/linux/memory.trb
use Memory, Swap from "../memory"
use OsError from "../error"
use readSystemFile from "./files"

/** `/proc/meminfo`, read into what [Memory] needs. */
public fn memory(): Result<Memory, OsError> {
  const text = readSystemFile("/proc/meminfo")?
  parseMemory text
}

/**
 * The parser on its own, so that its tests run on every machine with a copy of the file. The kernel writes every
 * value in kibibytes, whatever the unit column says, and `MemAvailable` is its own estimate of what an allocation can
 * get without swapping (since Linux 3.14; before it, free plus the page cache).
 */
public fn parseMemory(text: String): Result<Memory, OsError> {
  var fields: Map<String, Int64> = [:]
  for line in text.lines() {
    const parts = line.split(":")
    if parts.length() != 2 {
      continue
    }
    const number = parts[1].trim().split(" ")[0]
    if const Ok(kibibytes) = Int64.tryFrom(number) {
      fields[parts[0]] = kibibytes
    }
  }
  const total = fields.get("MemTotal") ?? 0
  if total == 0 {
    return Fail OsError.Malformed("/proc/meminfo", "there is no MemTotal line")
  }
  const cached = (fields.get("Cached") ?? 0) + (fields.get("Buffers") ?? 0)
  const available = fields.get("MemAvailable") ?? ((fields.get("MemFree") ?? 0) + cached)
  const swapTotal = fields.get("SwapTotal") ?? 0
  const swapUsed = swapTotal - (fields.get("SwapFree") ?? 0)
  Ok Memory(total.kibibytes(), available.kibibytes(), Swap(swapTotal.kibibytes(), swapUsed.kibibytes()))
}
```

### Who owns what

**`std/os` answers questions about the machine and about this process's place on it. It never reads or writes the
contents of a file, never starts or ends a program, and never does path arithmetic.**

| Package | Owns | Does not own |
|---|---|---|
| `std/core` | the target constants: `OperatingSystem`, `Architecture`, `ByteOrder` and their `current` | anything that asks the machine |
| `std/os` | identity and version, processors, memory, volumes, network interfaces, the environment, the user, well-known directories, what the machine knows *about* the current process (id, parent, executable, working directory, uptime, resources, limits) | files (`std/fs`), running programs (`std/process`), paths (`std/path`) |
| `std/process` | what a program *does* with processes: its arguments, `exit`, starting and running children (with the environment they get), and signals | information about the current process, which moves to `std/os` |
| `std/fs` | the contents of files and directories, existence, creation, listing | how full a volume is, which is `std/os` (a volume is not a file) |
| `std/path` | `Path` as a value | where a directory is by convention, which is `std/os` answering a `Path` |
| `std/time` | `Duration`, `Instant`, and (proposed, section 9) `Timestamp` | uptime and boot time, which `std/os` answers in those types |
| `std/number` | `ByteSize` (section 4), a quantity like `Duration` | — |
| `std/network` (new, pure) | `IpAddress`, `Ipv4Address`, `Ipv6Address`, `MacAddress`, `Subnet` as values | interfaces (`std/os`), sockets (a future `std/socket`) |

**Why `std/network` is a package of its own and pure.** An IP address is a value that `std/os` produces, that a URI's
host may be, and that a socket will take. The split is the one `std/path` and `std/fs` made: the value package reads
nothing and its import says nothing about capabilities; the package that touches the network is a different import.
It holds no socket, and a future socket layer is `std/socket`, so that `use IpAddress from "std/network"` never reads
as "this file opens connections".

## 4. Units, errors and waiting

### `ByteSize`

A count of bytes is a quantity with a unit, exactly as a `Duration` is a count of nanoseconds, and it gets the same
shape: one private `Int64`, members that read it in a unit, and members on `Int64` that make one.

```trb
/**
 * A number of bytes: the size of a memory, of a volume, of a limit. Signed, like [Duration], so a difference can be
 * negative.
 */
public type ByteSize with Show, Compare, Add, Subtract, Multiply<Int64, ByteSize> {
  private storedBytes: Int64

  /** Exactly what it holds. */
  fn bytes(): Int64

  /** As a fraction of `whole`: `memory.used().fraction(of: memory.total)` is between 0 and 1. */
  fn fraction(of: ByteSize): Float64

  /** In decimal units, as a disk vendor counts: 1 kilobyte is 1000 bytes. */
  fn kilobytes(): Float64
  fn megabytes(): Float64
  fn gigabytes(): Float64
  fn terabytes(): Float64

  /** In binary units, as a memory is counted: 1 kibibyte is 1024 bytes. */
  fn kibibytes(): Float64
  fn mebibytes(): Float64
  fn gibibytes(): Float64
  fn tebibytes(): Float64

  /** The largest binary unit with at least one of it, to one decimal: `512 B`, `1.5 KiB`, `15.9 GiB`. */
  fn show(): String
}

extend Int64 {
  fn bytes(): ByteSize
  fn kilobytes(): ByteSize
  fn megabytes(): ByteSize
  fn gigabytes(): ByteSize
  fn terabytes(): ByteSize
  fn kibibytes(): ByteSize
  fn mebibytes(): ByteSize
  fn gibibytes(): ByteSize
  fn tebibytes(): ByteSize
}
```

**In `std/number`, re-exported by the prelude beside `Duration`.** It is needed by packages that have nothing to do
with each other — `std/os` for memory and volumes, `std/sandbox` for its memory limit, `std/http` for a body limit —
so it cannot live in any one of them, and a quantity belongs with the numbers. Because `std/number` owns `Int64`,
`64.megabytes()` needs no import line anywhere, and `std/sandbox`'s own `extend Int64 { fn megabytes(): Int64 }` is
removed in the same slice (section 6): two members of one name on `Int64` would be ambiguous, and the sandbox's limit
becomes `memory: ByteSize = 64.megabytes()`.

**Both unit families, named for what they are.** `megabytes` is 10⁶ and `mebibytes` is 2²⁰, as the SI and IEC names
say. A single family would be wrong for half the callers: a disk is sold and reported in decimal units, a memory in
binary ones, and a `megabytes()` that meant 2²⁰ would contradict the existing sandbox member, which means 10⁶.

### `Frequency`

```trb
/** A processor clock: a count of hertz. */
public type Frequency with Show, Compare {
  private storedHertz: Int64

  static fn ofHertz(hertz: Int64): Frequency
  static fn ofMegahertz(megahertz: Int64): Frequency

  fn hertz(): Int64
  fn megahertz(): Float64
  fn gigahertz(): Float64

  /** `3.6 GHz`, `800 MHz`. */
  fn show(): String
}
```

In `std/os`, next to `Processor`, because `std/os` is the only package that produces one. **The rule:** a unit type
lives with its only producer and moves to `std/number` when a second package needs it, the way `ByteSize` does now.

### Time: `Duration` and `Timestamp`

Uptime, processor times and the age of the process are `Duration`s from `std/time`. A **boot time** is a point on the
calendar, and `std/time` has no such type: `Instant` is monotonic with an origin the process chooses. This design
needs `std/time` to gain one, and it is `std/time`'s design, not this one's:

```trb
/** A point on the wall clock: nanoseconds since 1970-01-01 00:00:00 UTC. It can jump, where an [Instant] cannot. */
public type Timestamp with Show, Compare, Subtract<Timestamp, Duration>, Add<Duration, Timestamp> {
  private storedNanoseconds: Int64
}
```

Until it exists, `System.bootTime()` is not declared and `System.uptime()` is the answer. The boot time is not the
wall clock minus the uptime computed by a caller, because the two readings are not taken at one moment and the wall
clock may have jumped since boot; each system has a stored boot time (`kern.boottime`, `btime` in `/proc/stat`), and
Windows is the exception that does compute it (`GetTickCount64` against the system time), which its arm says.

### `OsError`

Every `std/…` failure type is named after its package (`IoError`, `PathError`, `UriError`, `HttpError`), so this one
is `OsError`.

```trb
/**
 * What asking the operating system went wrong with. `question` is what was asked: `memory`,
 * `the volume at /mnt/nas`.
 */
public type OsError with Show, Error {
  /** This operating system cannot answer the question at all: the current clock of a processor on Apple silicon. */
  case Unsupported(question: String, operatingSystem: OperatingSystem)
  /** The operating system refused: a permission, a container that hides `/proc`, a policy. */
  case Denied(question: String, reason: String)
  /** The question has no answer on this machine: no `HOME` and no account entry, no such volume. */
  case Missing(question: String, reason: String)
  /** The call failed, in the operating system's own words. */
  case Failed(question: String, message: String)
  /** An answer came back in a form its reader did not expect: a `/proc` line, a registry value of the wrong type. */
  case Malformed(source: String, detail: String)
  /** A query that waits was cancelled. What `From<Cancelled>` produces. */
  case Stopped
}

extend OsError with From<Cancelled> {
  static fn from(value: Cancelled): OsError {
    OsError.Stopped
  }
}
```

**A field the system may not know is an `Option`; a question it cannot answer is `Unsupported`.** The maximum
frequency of a processor is `Frequency?` inside `ProcessorDescription`, because the description is still worth having
without it. The current frequency of every processor is its own question, and on a system that has no way to tell,
the answer is `Fail OsError.Unsupported("processor frequencies", .MacOs)`. A caller can tell "this machine did not say"
from "something went wrong", which an empty list or a zero would hide.

**No OS error code is exposed.** A code is a number of one system, and a neutral API that carried it would invite
`if code == 5` in portable code. The per-system natives classify it (`ERROR_ACCESS_DENIED` and `EACCES` both become
`Denied`) and the message keeps the system's words.

### How a native answers

A runtime function may not build a type of the program (`natives.trb`, its header), so every per-system native of this
design has one shape, the one `Process.runCollecting` established:

```trb
/** GlobalMemoryStatusEx. `0` on success; otherwise one of the outcomes of [outcome], with the reason in `failure`. */
native static fn memoryStatus(
  var total: Int64,
  var available: Int64,
  var failure: String,
): Int
```

The answer is an `Int` outcome — `0` success, `1` failed, `2` denied, `3` missing, `4` unsupported — and the payload
comes back through `var` parameters; a table comes back as parallel `var` lists (`ArrayList<String>`,
`ArrayList<Int64>`), one entry per row. One function in `error.trb` turns the outcome into a `Result`:

```trb
/** The `Result` of a native's outcome: `Ok` for `0`, otherwise the case its number names. */
public fn outcome(code: Int, question: String, failure: String): Result<Void, OsError> {
  match code {
    0 => Ok void
    2 => Fail OsError.Denied(question, failure)
    3 => Fail OsError.Missing(question, failure)
    4 => Fail OsError.Unsupported(question, OperatingSystem.current)
    _ => Fail OsError.Failed(question, failure)
  }
}
```

That needs no new convention in the manifest and no new wrapper in the lowering: every native here is a `.Direct`
runtime entry, and the only manifest change of the whole design is `availableOn` (section 9).

### Which calls are a `Task`

**A call is a `Task` when it can wait on something outside the machine, or waits on purpose.** A call that reads
kernel state from memory is synchronous, the way `Clock.now()` is.

| Call | Why |
|---|---|
| `Processor.usage(interval:)`, `NetworkInterface.throughput(interval:)` | they sample twice and sleep between: waiting is what they do |
| `Volume.space()`, `Volume.containing(path)` | `statvfs` and `GetDiskFreeSpaceExW` on a network mount wait for the server, and a dead NFS server makes them wait forever |
| `User.current()` | `getpwuid_r` goes through NSS, which may ask LDAP; `GetUserNameExW(NameDisplay)` may ask a domain controller |
| everything else | kernel state: `/proc`, `sysctl`, `GlobalMemoryStatusEx`, `getifaddrs`, the environment block |

The waiting calls run their native on the blocking pool through `offload` (`docs/design/CONCURRENCY.md` section 7),
so a stuck NFS mount blocks a pool thread and not a worker, and `cancel()` frees the waiting task's core at once. Until
the pool exists (7.7), the body calls the native directly, as `File.readText` does today; the signature is already the
`Task`, so nothing changes for a caller when the pool lands.

## 5. The signatures

Declarations are shown with their doc comments' first sentence and without bodies where the body is the `match` of
section 3. Every snapshot type is a value with derived `Equals` and a derived `Show` unless a secret or a unit says
otherwise.

### 5.1 The system

```trb
/** Which system this is, which version, and how long it has been running. */
public type System {
  /** The operating system's name and version, as its vendor writes them. */
  static fn version(): Result<SystemVersion, OsError>

  /** The machine's host name: `GetComputerNameExW(ComputerNameDnsHostname)`, `gethostname`. Never asks DNS. */
  static fn hostName(): Result<String, OsError>

  /** How long since the system started, not counting the time it was asleep where the system can tell. */
  static fn uptime(): Result<Duration, OsError>

  /** When the system started, on the wall clock. Needs `Timestamp` in `std/time` (section 4). */
  static fn bootTime(): Result<Timestamp, OsError>

  /** The size of a memory page: 4 KiB on most machines, 16 KiB on Apple silicon. */
  static fn pageSize(): ByteSize

  /**
   * The architecture of the machine, which is not always [Architecture.current]: an x86-64 program under emulation on
   * an Arm64 Windows machine or under Rosetta on a Mac runs on `.Arm64`.
   */
  static fn machineArchitecture(): Result<Architecture, OsError>
}

/** The name and version of the running system. */
public type SystemVersion with Show, Equals {
  /** For people: `Windows 11 Pro`, `Ubuntu 24.04.1 LTS`, `macOS 15.1`, `FreeBSD 14.1-RELEASE`. */
  name: String
  /** For programs, stable and lower case: `windows`, `ubuntu`, `fedora`, `macos`, `freebsd` (`ID` of `os-release`). */
  identifier: String
  /** The product's own version: `24H2`, `24.04`, `15.1`, `14.1`. */
  release: String
  /** The build, where the product has one: `26100.2314`, `24B83`. */
  build: String?
  /** The kernel's release: `10.0.26100`, `6.8.0-45-generic`, `24.1.0`, `14.1-RELEASE`. */
  kernel: String

  /** The leading numbers of [SystemVersion.kernel], for comparing: `[6, 8, 0]`. */
  fn kernelNumbers(): List<Int>
}
```

| | Windows | Linux | macOS | FreeBSD |
|---|---|---|---|---|
| `name`, `release` | registry `ProductName`, `DisplayVersion` — and `Windows 10` becomes `Windows 11` from build 22000, because the registry still says 10 | `/etc/os-release` (`PRETTY_NAME`, `VERSION_ID`), then `/usr/lib/os-release` | `kern.osproductversion`, and the name from a table of major versions | `uname` and `kern.osrelease` |
| `build` | `CurrentBuildNumber` `.` `UBR` | — | `kern.osversion` | — |
| `kernel` | `RtlGetVersion` | `uname().release` | `kern.osrelease` | `uname().release` |
| `hostName` | `GetComputerNameExW` | `gethostname` | `gethostname` | `gethostname` |
| `uptime` | `GetTickCount64` | `/proc/uptime` | `kern.boottime` against the clock | `kern.boottime` against the clock |
| `pageSize` | `GetNativeSystemInfo` | `sysconf(PAGESIZE)` | `sysconf(PAGESIZE)` | `sysconf(PAGESIZE)` |

### 5.2 Processors

```trb
/** The processors of this machine. */
public type Processor {
  /** How many logical processors the system runs threads on, hyper-threads included. */
  static fn logicalCount(): Result<Int, OsError>

  /** How many physical cores. */
  static fn physicalCount(): Result<Int, OsError>

  /** Vendor, model, topology, caches, frequencies and features, read once. */
  static fn describe(): Result<ProcessorDescription, OsError>

  /** The time every processor together has spent in each state since the system started. */
  static fn times(): Result<ProcessorTimes, OsError>

  /** The same, for each logical processor, in the system's order. */
  static fn timesOfEach(): Result<List<ProcessorTimes>, OsError>

  /** How busy the processors were over `interval`: two readings of [Processor.times] and a sleep between them. */
  static fn usage(interval: Duration = 1.seconds()): Task<Result<ProcessorUsage, OsError>>

  /** The current clock of each logical processor, where the system can tell. */
  static fn frequencies(): Result<List<Frequency>, OsError>
}

public type ProcessorDescription with Show, Equals {
  /** `GenuineIntel`, `AuthenticAMD`, `Apple`, `ARM`. */
  vendor: String
  /** The brand string: `AMD Ryzen 9 7950X 16-Core Processor`, `Apple M3 Pro`. */
  model: String
  packages: Int
  physicalCores: Int
  logicalProcessors: Int
  baseFrequency: Frequency?
  maximumFrequency: Frequency?
  caches: List<ProcessorCache>
  features: Set<ProcessorFeature>
}

public type ProcessorCache with Show, Equals {
  /** 1, 2 or 3. */
  level: Int
  kind: "data" | "instruction" | "unified"
  size: ByteSize
  lineSize: ByteSize
}

/** An instruction-set extension a program may dispatch on. Named after the extension, as its vendor spells it. */
public type ProcessorFeature with Show, Equals, Hash {
  case Sse2
  case Sse3
  case Ssse3
  case Sse41
  case Sse42
  case Avx
  case Avx2
  case Fma
  case Bmi1
  case Bmi2
  case Avx512Foundation
  case Aes
  case Sha
  case Neon
  case Crc32
  case DotProduct
  case Sve
  case Sve2
}

/** Cumulative time in each state. The difference of two readings is what [Processor.usage] reports. */
public type ProcessorTimes with Show, Equals, Subtract {
  user: Duration
  system: Duration
  idle: Duration
  /** Waiting for input or output with nothing else to run: Linux `iowait`. Zero where the system does not count it. */
  waiting: Duration
  /** Everything else the system counts: `nice`, interrupts, stolen time. */
  other: Duration

  fn total(): Duration
}

/** Fractions of the interval, each between 0 and 1. */
public type ProcessorUsage with Show, Equals {
  busy: Float64
  user: Float64
  system: Float64
  each: List<Float64>

  /** The fractions of two differences of [ProcessorTimes]: all processors together, and each one. */
  static fn between(total: ProcessorTimes, each: List<ProcessorTimes>): ProcessorUsage
}
```

`usage` is TorbScript over `times`, on every system:

```trb
static fn usage(interval: Duration = 1.seconds()): Task<Result<ProcessorUsage, OsError>> {
  const before = Processor.times()?
  const beforeEach = Processor.timesOfEach()?
  sleep(interval.seconds()).await()?
  const after = Processor.times()?
  const afterEach = Processor.timesOfEach()?
  Ok ProcessorUsage.between(after - before, afterEach.zip(beforeEach).map({ _.0 - _.1 }).toList())
}
```

**The feature list is closed and grows with its users.** It holds what `std/linear`, the ML packages of the std vision
and the runtime would dispatch on; a case is added when something reads it. On x86-64 every feature comes from `cpuid`
(and `xgetbv` for whether the operating system saves the AVX registers), the same on every system, which is why that
code is `x86/` and not four copies. On Arm64 it comes from the system: `getauxval(AT_HWCAP)` on Linux,
`hw.optional.*` on macOS, `IsProcessorFeaturePresent` on Windows.

**`Processor.logicalCount()` is the machine; what this process may use is `CurrentProcess.processors()`** (5.9), which
honours the affinity mask and a container's CPU quota. `Workers.count()` of `docs/design/CONCURRENCY.md` section 3 is
a third number — how many workers the runtime started — and its default is the second one, read by the same C.

### 5.3 Memory

```trb
public type Memory with Show, Equals {
  total: ByteSize
  /** What a new allocation could still get without swapping: the system's own estimate where it has one. */
  available: ByteSize
  swap: Swap

  fn used(): ByteSize

  static fn current(): Result<Memory, OsError>
}

public type Swap with Show, Equals {
  total: ByteSize
  used: ByteSize

  fn free(): ByteSize
}
```

| | `total` | `available` | `swap` |
|---|---|---|---|
| Windows | `GlobalMemoryStatusEx().ullTotalPhys` | `ullAvailPhys` | the page files, from `NtQuerySystemInformation(SystemPageFileInformation)` — **not** the commit limit, which is memory plus page files |
| Linux | `MemTotal` | `MemAvailable`, or `MemFree + Buffers + Cached` before 3.14 | `SwapTotal`, `SwapTotal - SwapFree` |
| macOS | `hw.memsize` | free, inactive, speculative and purgeable pages of `host_statistics64` | `vm.swapusage` |
| FreeBSD | `hw.physmem` | `v_free_count + v_inactive_count` pages | `vm.swap_info`, summed over devices |

**Pitfall, and the doc comment says it:** macOS and Linux use free memory as a cache on purpose, so `total - free` is
not "used" on either. `available` is the number to watch, and `used()` is defined as `total - available` for that
reason, never read from the system.

### 5.4 Volumes

```trb
/** A mounted file system. */
public type Volume with Show, Equals {
  /** Where it is mounted: `C:/`, `/`, `/home`, `/Volumes/Backup`. */
  mountPoint: Path
  /** What is mounted: `\\?\Volume{…}\`, `/dev/nvme0n1p2`, `tmpfs`, `server:/export`. */
  device: String
  /** As the system names it: `NTFS`, `ext4`, `apfs`, `tmpfs`, `nfs4`. */
  fileSystem: String
  label: String?
  kind: VolumeKind
  readOnly: Bool

  fn isRemovable(): Bool

  /** How big it is and how much is free. Waits: a network mount answers when its server does. */
  fn space(): Task<Result<VolumeSpace, OsError>>

  /** The mounted file systems, from the mount table. It never waits, because it asks no file system anything. */
  static fn all(includeVirtual: Bool = false): Result<List<Volume>, OsError>

  /** The volume `path` is on. Waits, because telling needs a `stat` of the path. */
  static fn containing(path: Path): Task<Result<Volume, OsError>>
}

public type VolumeKind with Show, Equals {
  case Fixed
  case Removable
  case Network
  case Optical
  /** Backed by memory: `tmpfs`, a RAM disk. */
  case Memory
  /** A view of the kernel rather than storage: `proc`, `sysfs`, `devfs`. Left out of [Volume.all] unless asked for. */
  case Virtual
  case Unknown
}

public type VolumeSpace with Show, Equals {
  total: ByteSize
  free: ByteSize
  /** What this user may still write: less than `free` where the system reserves blocks for its administrator. */
  available: ByteSize

  fn used(): ByteSize
}
```

| | the mount table (`all`) | `kind` | `space` |
|---|---|---|---|
| Windows | `FindFirstVolumeW` + `GetVolumePathNamesForVolumeNameW`, so a volume mounted on a folder is found too | `GetDriveTypeW` | `GetDiskFreeSpaceExW` |
| Linux | `/proc/self/mountinfo`, with its `\040` escapes decoded in TorbScript | the file system name, and `/sys/block/<device>/removable` | `statvfs` |
| macOS, FreeBSD | `getmntinfo(MNT_NOWAIT)`, which does not wait on a dead mount | `MNT_LOCAL`, `MNT_REMOVABLE` (macOS) | `statvfs` |

**The mount point is a `Path`, the device is a `String`.** A mount point is a place in the file system and is used as
one (`volume.mountPoint.joined("backup")`); a device is a name in the system's own namespace that no `Path` operation
means anything on.

### 5.5 Network

```trb
/** A network interface and its addresses. */
public type NetworkInterface with Show, Equals {
  /** `eth0`, `en0`, and on Windows the friendly name: `Ethernet 2`, `WLAN`. */
  name: String
  /** The adapter's description where the system has one: `Intel(R) Ethernet Controller I225-V`. */
  description: String?
  /** The system's index, what a scoped IPv6 address and a socket option refer to. */
  index: Int
  hardwareAddress: MacAddress?
  addresses: List<InterfaceAddress>
  mtu: Int
  status: InterfaceStatus
  kind: InterfaceKind

  /** Bytes, packets and errors since the interface came up. */
  fn traffic(): Result<InterfaceTraffic, OsError>

  /** Bytes per second each way over `interval`: two readings of [NetworkInterface.traffic] and a sleep. */
  fn throughput(interval: Duration = 1.seconds()): Task<Result<(received: ByteSize, sent: ByteSize), OsError>>

  static fn all(): Result<List<NetworkInterface>, OsError>
}

/** One address of an interface, with the length of its network prefix: `192.168.1.23/24`, `fe80::1/64`. */
public type InterfaceAddress with Show, Equals {
  address: IpAddress
  prefixLength: Int

  /** The network the address is in: `192.168.1.0/24`. */
  fn subnet(): Subnet
}

public type InterfaceStatus with Show, Equals {
  case Up
  case Down
  /** The system does not say, or the interface is being brought up. */
  case Unknown
}

public type InterfaceKind with Show, Equals {
  case Ethernet
  case Wireless
  case Loopback
  case Tunnel
  case Other
}

public type InterfaceTraffic with Show, Equals, Subtract {
  received: ByteSize
  sent: ByteSize
  receivedPackets: Int64
  sentPackets: Int64
  receiveErrors: Int64
  sendErrors: Int64
}
```

**The address values, in the new `std/network`** (section 3), proposed here because this is their first producer:

```trb
/** An IPv4 address: four octets, and a value like any other. */
public type Ipv4Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError> {
  private storedValue: UInt32

  static fn of(first: UInt8, second: UInt8, third: UInt8, fourth: UInt8): Ipv4Address
  /** `127.0.0.1`. A constant built with the constructor, because a constant may not call `of`. */
  static loopback = Ipv4Address(0x7F000001)
  /** `0.0.0.0`. */
  static unspecified = Ipv4Address(0)

  fn octets(): Array<UInt8, 4>
  fn isLoopback(): Bool
  fn isPrivate(): Bool
  fn isLinkLocal(): Bool
  fn isMulticast(): Bool
}

/**
 * An IPv6 address: eight 16-bit segments. `show()` is the RFC 5952 form: `fe80::1`, lower case, the longest run of
 * zeros collapsed.
 */
public type Ipv6Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError> {
  private storedHigh: UInt64
  private storedLow: UInt64

  fn segments(): Array<UInt16, 8>
  fn isLoopback(): Bool
  fn isLinkLocal(): Bool
  fn isUniqueLocal(): Bool
  fn isMulticast(): Bool
  /** The IPv4 address inside an IPv4-mapped one (`::ffff:192.0.2.1`). */
  fn mappedVersion4(): Ipv4Address?
}

/** Either version. Parsing tells them apart by the text. */
public type IpAddress with Show, Equals, Hash, Compare, TryFrom<String, AddressError> {
  case Version4(address: Ipv4Address)
  case Version6(address: Ipv6Address)
}

/** A link-layer address. `show()` is lower case with colons: `3c:22:fb:0a:1b:9e`. */
public type MacAddress with Show, Equals, Hash, TryFrom<String, AddressError> {
  private storedValue: UInt64

  fn octets(): Array<UInt8, 6>
}

/** An address and a prefix length that together name a network: `10.0.0.0/8`. */
public type Subnet with Show, Equals, Hash, TryFrom<String, AddressError> {
  address: IpAddress
  prefixLength: Int

  fn contains(address: IpAddress): Bool
}
```

The case names are `Version4` and `Version6`, not `V4` and `V6`, by the full-word rule. A scoped IPv6 address
(`fe80::1%eth0`) keeps its zone out of the value, as Rust and Go do: the zone is the interface, and an
`InterfaceAddress` already belongs to one. These five are candidates for `docs/design/URI.md` section 9's closed list
of types a string literal adapts to (`const gateway: IpAddress = "192.168.1.1"`), which that section decides.

### 5.6 The environment

```trb
/** The environment this process was started with: read, listed, and handed on to children as a value. */
public native type Environment {
  /** The value of `name`, or `None` where it is not set or the sandbox has not granted it. */
  native static fn get(name: String): String?

  /** Every variable the caller may see, as a value that can be changed and given to a child. */
  static fn variables(): EnvironmentVariables

  /** `PATH` split at the system's separator, each entry a `Path`, empty entries dropped. */
  static fn searchPath(): List<Path>
}

/**
 * A set of environment variables as a value. Names compare the way the target compares them: without regard to case
 * on Windows, exactly everywhere else — so `set("PATH", …)` on Windows replaces an inherited `Path`.
 */
public type EnvironmentVariables with Show, Equals {
  private storedEntries: Map<String, (name: String, value: String)>

  static fn empty(): EnvironmentVariables

  fn get(name: String): String?
  fn names(): List<String>
  var fn set(name: String, value: String)
  var fn remove(name: String)

  /** The names only: an environment carries secrets, and a `print` of it must not. */
  fn show(): String
}
```

The one difference between systems is a one-line arm, and it stays in place:

```trb
/** The key a name is stored under: folded on Windows, where `Path` and `PATH` are one variable. */
fn keyOf(name: String): String {
  match OperatingSystem.current {
    .Windows => name.toUpperCase()
    .Linux | .MacOs | .FreeBsd => name
  }
}
```

**There is no `Environment.set`.** Setting a variable of the running process is global mutable state, which the
language does not have, and on POSIX it is also a data race: `setenv` may reallocate `environ` while another thread's
`getenv` reads it, and with workers per core (`docs/design/CONCURRENCY.md` section 1) there are always other threads.
What setting a variable is for is almost always a child process, and that is a value handed to it:

```trb
use Environment from "std/os"
use Process from "std/process"

var variables = Environment.variables()
variables.set "RUST_LOG", "debug"
variables.remove "NO_COLOR"
const output = Process.run("cargo", ["test"], environment: variables)?
```

`Process.run` and `Process.start` gain `environment: EnvironmentVariables? = None`, where `None` inherits this
process's environment unchanged. `std/process` depends on `std/os/environment` for the type. The C function
`torb_platform_set_environment_variable` stays what its comment says it is — used by `runtime/tests` only.

**The runtime's own variables are read before `main`.** `TORB_WORKERS`, `TORB_BLOCKING` and `TORB_REPORT_LEAKS` are
read by the C runtime at start, so a program that cannot change its environment cannot change them either, which is
the point of reading them there.

### 5.7 The user

```trb
/** The account this process runs as. */
public type User with Show, Equals {
  /** The login name: `ada`, and on Windows the account name without its domain. */
  name: String
  /** The name for people: the GECOS field, `GetUserNameExW(NameDisplay)`. */
  fullName: String?
  /** `1000` on POSIX, the SID `S-1-5-21-…` on Windows: text, because the two have nothing else in common. */
  identifier: String
  home: Path
  /** Root on POSIX, an elevated token on Windows. */
  isElevated: Bool

  /** Waits: the account database may be a directory service on the network. */
  static fn current(): Task<Result<User, OsError>>
}
```

### 5.8 Well-known directories

```trb
/**
 * Where a user's files belong, by the convention of the system. Each answers the base: an application joins its own
 * name.
 */
public type Directories {
  static fn home(): Result<Path, OsError>
  static fn configuration(): Result<Path, OsError>
  static fn data(): Result<Path, OsError>
  static fn state(): Result<Path, OsError>
  static fn cache(): Result<Path, OsError>
  /** Always answers: every system has a temporary directory, and `/tmp` is the last resort. */
  static fn temporary(): Path

  static fn documents(): Result<Path, OsError>
  static fn downloads(): Result<Path, OsError>
  static fn desktop(): Result<Path, OsError>
  static fn pictures(): Result<Path, OsError>
  static fn music(): Result<Path, OsError>
  static fn videos(): Result<Path, OsError>
}
```

| | Linux, FreeBSD (`xdg/`) | Windows (Known Folders) | macOS |
|---|---|---|---|
| `home` | `$HOME`, else the account entry | `FOLDERID_Profile` | `$HOME`, else the account entry |
| `configuration` | `$XDG_CONFIG_HOME`, else `~/.config` | `FOLDERID_RoamingAppData` | `~/Library/Application Support` |
| `data` | `$XDG_DATA_HOME`, else `~/.local/share` | `FOLDERID_RoamingAppData` | `~/Library/Application Support` |
| `state` | `$XDG_STATE_HOME`, else `~/.local/state` | `FOLDERID_LocalAppData` | `~/Library/Application Support` |
| `cache` | `$XDG_CACHE_HOME`, else `~/.cache` | `FOLDERID_LocalAppData` | `~/Library/Caches` |
| `temporary` | `$TMPDIR`, else `/tmp` | `GetTempPath2W` (`GetTempPathW` before Windows 11) | `$TMPDIR`, else `confstr(_CS_DARWIN_USER_TEMP_DIR)` |
| `documents` and the rest | `XDG_DOCUMENTS_DIR` and its siblings in `user-dirs.dirs`, else `~/Documents` | `FOLDERID_Documents`, `FOLDERID_Downloads`, … | `~/Documents`, `~/Downloads`, … |

**The base, not a per-application directory.** `Directories.cache().joined("torb")` is one join, and it is the same
join on every system; a helper that also took an application and an organization name would have to decide how
Windows nests them (`%APPDATA%\Organization\Application`) and how macOS names them (a bundle identifier), which is
three conventions for a string the program already has. An XDG variable holding a relative path is ignored, as the
XDG specification requires, and that rule is TorbScript in `xdg/directories.trb`.

**`home` is a `Result`, not a `Path?`.** When it fails, the reason is worth reading — "`HOME` is not set and there is
no account entry for uid 1000" — and a caller writes `Directories.home()?` in the same place it would have written
`?? fallback`.

### 5.9 The current process

```trb
/**
 * What the operating system knows about this process. What the process does — arguments, exit, children — is
 * `std/process`.
 */
public type CurrentProcess {
  static fn identifier(): Int
  static fn parentIdentifier(): Result<Int, OsError>

  /** The absolute path of the running executable. Answers on every system, macOS and FreeBSD included. */
  static fn executable(): Result<Path, OsError>

  static fn workingDirectory(): Result<Path, OsError>

  /** How long this process has been running: the runtime notes the monotonic clock before `main`. */
  static fn uptime(): Duration

  /** Processor time and memory this process has used. */
  static fn resources(): Result<ProcessResources, OsError>

  /** How many processors this process may run on: the affinity mask, and a container's CPU quota rounded up. */
  static fn processors(): Result<Int, OsError>

  /** The memory this process is held to — a cgroup's `memory.max`, a job object's limit — or `None`. */
  static fn memoryLimit(): Result<ByteSize?, OsError>
}

public type ProcessResources with Show, Equals {
  userTime: Duration
  systemTime: Duration
  resident: ByteSize
  peakResident: ByteSize
}
```

`resources()` shows why an arm is sometimes the right size for a unit: `getrusage` reports the peak resident size in
kibibytes on Linux and FreeBSD and in bytes on macOS, and the POSIX module says so where it converts:

```trb
/** `ru_maxrss`, which POSIX leaves without a unit. */
fn peakResidentOf(reported: Int64): ByteSize {
  match OperatingSystem.current {
    .MacOs => reported.bytes()
    .Linux | .FreeBsd | .Windows => reported.kibibytes()
  }
}
```

(The `.Windows` arm is never taken, since Windows does not reach the POSIX module; it is written out rather than
folded into `_` so that a new system has to say which unit its `getrusage` uses.)

**There is no `CurrentProcess.setWorkingDirectory`.** Like the environment, the working directory is process-wide
mutable state, and a relative path resolved by one task would change meaning under another. A child gets a working
directory as a parameter (`Process.run(workingDirectory:)`, `docs/design/PATH.md` section 6).

### 5.10 Signals and Ctrl+C

**Not in `std/os`: they belong to `std/process` and to the scheduler.** A signal is an event of the running program's
lifecycle, like its exit, and what a program does with one — finish the current request, flush, stop — is a task
waiting for it. That makes it a source of the run queue, which is `docs/design/CONCURRENCY.md` section 7's subject: a
self-pipe or `signalfd` read by the poller on POSIX, `SetConsoleCtrlHandler` posting to the completion port on
Windows. The shape, for that design to decide:

```trb
/** Why the program is being asked to stop. */
public type Interruption with Show, Equals {
  /** Ctrl+C: `SIGINT`, `CTRL_C_EVENT`. */
  case Interrupt
  /** The system or a supervisor: `SIGTERM`, `CTRL_CLOSE_EVENT`, `CTRL_SHUTDOWN_EVENT`. */
  case Terminate
  /** The terminal went away: `SIGHUP`, `CTRL_LOGOFF_EVENT`. */
  case Hangup
}

extend Process {
  /** Every request to stop, from the moment this is called. Without it, the system's default applies. */
  static fn interruptions(): Source<Interruption, Cancelled>
}
```

## 6. What moves

**`std/environment` is removed, not kept as a re-export.** Two import paths for one capability would make the sandbox
grant ambiguous (`modules "std/environment"` or `modules "std/os/environment"`, and a script granted one importing
the other), and there is nobody outside the repository to keep compatible. The migration is one slice:

| Where | Today | After |
|---|---|---|
| `compiler/src/cli/build.trb`, `documentation/command.trb`, `documentation/native.trb`, `project/toolchain.trb` | `use Environment from "std/environment"` | `use Environment from "std/os"` |
| `examples/config-dsl/src/main.trb`, `tests/conformance/*.trb` (four files) | the same | the same change |
| `std/prelude/src/lib.trb` | its comment names `std/environment` among the imports that stay explicit | names `std/os` |
| `compiler/src/backend/c/natives.trb` | `environmentEntries`, keyed `Environment.get` | unchanged: the manifest is keyed by owner and member, not by package, so the row survives the move as it is |
| `docs/standard-library/environment.md` | the package page | replaced by `os.md`; `index.md`, `sandbox.md`, `the-prelude.md` follow |
| `docs/design/PROJECT.md` section 8 | `modules "std/fs", "std/text", "std/environment"` and "without `std/environment` in `modules`" | `"std/os/environment"` in both places |
| `docs/design/URI.md` section 11 | `use Environment from "std/environment"` in the registry example | `from "std/os"` |
| `CONCEPT.md`, `docs/BACKEND.md`, `docs/internals/inventory.md`, `runtime/README.md` | mention the package | the new name |

**`Process.executablePath()` moves to `CurrentProcess.executable()`** and answers a `Path` instead of a `String?`. The
toolchain's one caller (`compiler/src/project/toolchain.trb`, which finds `std/` and `runtime/` beside the binary)
changes with it, and macOS and FreeBSD start answering: `_NSGetExecutablePath` and `KERN_PROC_PATHNAME` are each one
native in their system's directory, which is the first thing the per-system structure buys.

**`std/sandbox`'s `extend Int64 { fn megabytes(): Int64 }` is removed** when `ByteSize` lands in `std/number`, and
`SandboxCapabilities.limits(memory:)` takes a `ByteSize`. All six sandbox natives are `.Planned` for 7.4, so this
changes a signature and no behaviour; `docs/language/configuration/the-sandbox.md` loses its
`use Int64.megabytes from "std/sandbox"` line.

**What does not move.** `Process.arguments()` and `Process.exit` stay in `std/process` — they are the program's input
and its control flow, not facts about the machine. `File.absolutePath` stays in `std/fs` and keeps reading the working
directory through `platform.c`; `CurrentProcess.workingDirectory()` reads it through the same function, so the two
cannot disagree.

## 7. The natives

Every native is a `.Direct` runtime entry of the manifest with the outcome shape of section 4, unless it is marked
otherwise. The C function is `torb_os_<family>_<name>`, declared in `runtime/include/torb_os.h` — **all of them, on
every machine** — and defined in `runtime/os/<family>.c` under that file's guard. `torb natives --header` writes every
row into `torb_natives.h` as it does today, so `runtime/tests/natives_header_test.c` compares the prototypes of every
system on every machine.

### OS-neutral

| Native | Where | What |
|---|---|---|
| `targetOperatingSystem`, `targetArchitecture`, `targetByteOrder` | `.Intrinsic`, no C | the build's target, read by the constant evaluator |
| `Environment.get` | `platform.c`, exists | unchanged |
| `Environment.entries(var names: ArrayList<String>, var values: ArrayList<String>)` | `platform.c`, new | `GetEnvironmentStringsW` / `environ`, filtered by the sandbox's patterns |
| `CurrentProcess.workingDirectory` | `platform.c`, exists as `torb_platform_working_directory` | a manifest row over it |
| `CurrentProcess.startedNanoseconds(): Int64` | `process.c`, new | the monotonic reading the runtime takes before `main` |

### Windows — `runtime/os/windows.c`, `native type Windows`

| Native | API | Used by |
|---|---|---|
| `version(var major, var minor, var build, var failure)` | `RtlGetVersion` | `System.version` |
| `registryText(key, value, var text, var failure)` | `RegGetValueW`, `REG_SZ` | product name, display version, processor brand and vendor |
| `registryInteger(key, value, var number, var failure)` | `RegGetValueW`, `REG_DWORD` / `REG_QWORD` | `UBR`, `~MHz` |
| `computerName(var name, var failure)` | `GetComputerNameExW` | `System.hostName` |
| `tickCount(): Int64` | `GetTickCount64` | `System.uptime` |
| `systemInformation(var pageSize, var architecture, var logical, var failure)` | `GetNativeSystemInfo` | `pageSize`, `machineArchitecture` |
| `processorTopology(var packages, var cores, var logical, var cacheLevels, var cacheKinds, var cacheSizes, var lineSizes, var failure)` | `GetLogicalProcessorInformationEx` | `Processor` counts and caches |
| `systemTimes(var idle, var kernel, var user, var failure)` | `GetSystemTimes` | `Processor.times` |
| `processorTimesOfEach(var idle, var kernel, var user, var failure)` — parallel lists | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` | `Processor.timesOfEach` |
| `processorPower(var current, var maximum, var failure)` — parallel lists, MHz | `CallNtPowerInformation(ProcessorInformation)` | frequencies |
| `processorFeature(feature: Int): Bool` | `IsProcessorFeaturePresent` | Arm64 features |
| `memoryStatus(var total, var available, var failure)` | `GlobalMemoryStatusEx` | `Memory` |
| `pageFiles(var totals, var used, var failure)` | `NtQuerySystemInformation(SystemPageFileInformation)` | `Swap` |
| `volumeMountPoints(var paths, var failure)` | `FindFirstVolumeW`, `GetVolumePathNamesForVolumeNameW` | `Volume.all` |
| `volumeInformation(root, var device, var label, var fileSystem, var driveType, var readOnly, var failure)` | `GetVolumeInformationW`, `GetDriveTypeW` | `Volume.all` |
| `volumeSpace(root, var total, var free, var available, var failure)` | `GetDiskFreeSpaceExW` | `Volume.space`, offloaded |
| `adapters(var names, var descriptions, var indexes, var hardware, var mtus, var states, var kinds, var failure)` | `GetAdaptersAddresses` | `NetworkInterface.all` |
| `adapterAddresses(var indexes, var addresses, var prefixLengths, var failure)` | the unicast list of the same call | addresses |
| `interfaceTraffic(index, var received, var sent, var receivedPackets, var sentPackets, var receiveErrors, var sendErrors, var failure)` | `GetIfEntry2` | `traffic` |
| `knownFolder(identifier, var path, var failure)` | `SHGetKnownFolderPath`, the GUID as text | `Directories` |
| `temporaryDirectory(var path, var failure)` | `GetTempPath2W`, else `GetTempPathW` | `Directories.temporary` |
| `account(var name, var fullName, var identifier, var elevated, var failure)` | `GetUserNameW`, `GetUserNameExW`, `GetTokenInformation`, `ConvertSidToStringSidW` | `User.current`, offloaded |
| `processIdentity(var identifier, var parent, var failure)` | `GetCurrentProcessId`, `CreateToolhelp32Snapshot` | `CurrentProcess` |
| `processResources(var user, var kernel, var workingSet, var peakWorkingSet, var failure)` | `GetProcessTimes`, `GetProcessMemoryInfo` | `resources` |
| `processorsAvailable(var count, var failure)` | `GetProcessGroupAffinity`, the job's CPU rate | `processors` |
| `jobMemoryLimit(var limit, var failure)` | `QueryInformationJobObject` | `memoryLimit` |
| `executablePath(var path, var failure)` | `GetModuleFileNameW`, moved from `platform.c` | `executable` |

TorbScript above them: the Known Folder GUIDs, the product-name correction for Windows 11, the drive-type and
adapter-kind tables, the 100-nanosecond conversion of every Windows time into a `Duration`, and the boot time.

### Linux — `runtime/os/linux.c`, `native type Linux`

| Native | What |
|---|---|
| `readSystemFile(path, var text, var failure)` | reads until end of file — a `/proc` file reports size 0, so a read by size answers nothing. Accepts `/proc/`, `/sys/`, `/etc/os-release`, `/usr/lib/os-release` and the `user-dirs.dirs` of the configuration directory, and refuses every other path, so it is not a way around `std/fs` or the sandbox |
| `readSystemLink(path, var target, var failure)` | `readlink` below `/proc/` only: `/proc/self/exe` |
| `listSystemDirectory(path, var names, var failure)` | below `/sys/` only: `/sys/class/net`, `/sys/devices/system/cpu`, `/sys/block` |
| `auxiliaryValue(kind: Int): Int64` | `getauxval`: `AT_HWCAP`, `AT_HWCAP2` for Arm64 features. Plain `Direct`, no outcome |
| `affinityCount(var count, var failure)` | `sched_getaffinity` |

Everything else on Linux is TorbScript over those and `posix/`: `/proc/meminfo`, `/proc/stat` (processor times and
`btime`), `/proc/cpuinfo` (the model name where `cpuid` is not available), `/proc/uptime`, `/proc/self/mountinfo`,
`/proc/net/dev`, `/proc/self/stat` (the parent), `/sys/class/net/<name>/{mtu,operstate,type}`,
`/sys/devices/system/cpu/cpu<n>/cpufreq/*`, the cgroup v2 files (`cpu.max`, `memory.max`) with the v1 fallback, and
`os-release`.

### POSIX — `runtime/os/posix.c`, `native type Posix`, on Linux, macOS and FreeBSD

| Native | What |
|---|---|
| `systemNames(var system, var node, var release, var version, var machine, var failure)` | `uname` |
| `hostName(var name, var failure)` | `gethostname` |
| `configuration(name: String): Int64` | `sysconf` **by name** (`"PAGESIZE"`, `"NPROCESSORS_ONLN"`, `"CLK_TCK"`), `-1` where unknown — because the numeric `_SC_*` constants differ between systems and must not appear in TorbScript |
| `fileSystemSpace(path, var blockSize, var blocks, var free, var available, var readOnly, var failure)` | `statvfs`, offloaded |
| `interfaceAddresses(var names, var families, var addresses, var prefixLengths, var flags, var failure)` | `getifaddrs`; addresses as `inet_ntop` text that `std/network` parses; flags translated in C to this design's own bits (up, running, loopback, point-to-point) |
| `hardwareAddresses(var names, var addresses, var failure)` | `AF_PACKET` on Linux, `AF_LINK` on the BSDs, from the same list |
| `interfaceMtu(name, var mtu, var failure)` | `ioctl(SIOCGIFMTU)` |
| `account(var identifier, var name, var fullName, var home, var failure)` | `getuid`, `getpwuid_r`, offloaded |
| `effectiveUserIdentifier(): Int64` | `geteuid` |
| `processIdentity(var identifier, var parent)` | `getpid`, `getppid` |
| `resourceUsage(var userMicroseconds, var systemMicroseconds, var peakResident, var failure)` | `getrusage(RUSAGE_SELF)` — the unit of `peakResident` differs, and section 5.9's arm converts it |

### BSD — `runtime/os/bsd.c`, `native type Bsd`, on macOS and FreeBSD

| Native | What |
|---|---|
| `sysctlInteger(name, var number, var failure)` | `sysctlbyname`, any integer width widened to `Int64` |
| `sysctlText(name, var text, var failure)` | `sysctlbyname` of a string |
| `sysctlBytes(name, var bytes: Bytes, var failure)` | the raw structure: `kern.boottime` (`struct timeval`), `vm.swapusage` (`struct xsw_usage`), `kern.cp_time` and `kern.cp_times` (arrays of `long`). `bsd/sysctl.trb` decodes them with `ByteOrder.current` |
| `mountedFileSystems(var mountPoints, var devices, var fileSystems, var flags, var failure)` | `getmntinfo(MNT_NOWAIT)` |
| `linkTraffic(name, var received, var sent, var receivedPackets, var sentPackets, var receiveErrors, var sendErrors, var failure)` | the `if_data` of the `AF_LINK` entry of `getifaddrs` |

### macOS — `runtime/os/macos.c`, `native type MacOs`

| Native | What |
|---|---|
| `virtualMemory(var free, var active, var inactive, var wired, var compressed, var speculative, var purgeable, var failure)` | `host_statistics64(HOST_VM_INFO64)`, in pages |
| `processorLoad(var user, var system, var idle, var nice, var failure)` | `host_statistics(HOST_CPU_LOAD_INFO)`, in ticks |
| `processorLoadOfEach(var user, var system, var idle, var nice, var failure)` — parallel lists | `host_processor_info(PROCESSOR_CPU_LOAD_INFO)` |
| `executablePath(var path, var failure)` | `_NSGetExecutablePath`, then `realpath` |
| `userTemporaryDirectory(var path, var failure)` | `confstr(_CS_DARWIN_USER_TEMP_DIR)` |

Everything else on macOS is `Bsd.sysctl*` by name (`hw.memsize`, `hw.physicalcpu`, `hw.logicalcpu`,
`machdep.cpu.brand_string`, `hw.optional.arm.*`, `kern.osproductversion`, `kern.osversion`) and `posix/`.

### FreeBSD — `runtime/os/freebsd.c`, `native type FreeBsd`

| Native | What |
|---|---|
| `executablePath(var path, var failure)` | `sysctl` of `{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1}`, a numeric name `sysctlbyname` cannot express |
| `swapDevices(var totals, var used, var failure)` — parallel lists | `vm.swap_info`, one `struct xswdev` per device |
| `affinityCount(var count, var failure)` | `cpuset_getaffinity` |

Memory (`vm.stats.vm.v_free_count` and its siblings, `hw.physmem`, `hw.pagesize`), processor times (`kern.cp_time`)
and the rest are `Bsd.sysctl*` by name.

### x86 — `runtime/os/x86.c`, `native type X86`, on `.X64`

| Native | What |
|---|---|
| `cpuid(leaf: Int, subleaf: Int, var eax: Int64, var ebx: Int64, var ecx: Int64, var edx: Int64)` | the instruction, through the compiler's `<cpuid.h>` or `__cpuidex`. Plain `Direct`, no outcome. The parameters carry the registers' names, which are the names the vendors' manuals use |
| `extendedControlRegister(var low: Int64, var high: Int64): Bool` | `xgetbv(0)`, false where `OSXSAVE` is not set |

`x86/processor.trb` decodes leaf 0 (vendor), leaves `0x80000002`–`0x80000004` (brand string), leaves 1 and 7 (feature
bits) and leaf 4 or `0x8000001D` (caches) — pure TorbScript over four integers, with tests that feed it register
values captured from real processors.

## 8. The sandbox

**`std/os` is a capability, granted per module, and a sandboxed script has none of it by default** — the same default
as "no IO, no network, no clock, no environment" (`docs/language/configuration/the-sandbox.md`, rule 1). A grant is a
module path, which the `modules` capability already takes, because `use X from "std/os/memory"` names a module
(`docs/language/modules-and-packages/use.md`, rule 1):

| Grant | What the script learns | Why it is its own grant |
|---|---|---|
| `modules "std/core/target"` | which operating system, architecture and byte order it runs on | reads nothing; granted where a script branches on the target |
| `modules "std/os/environment"` plus `environment "APP_*"` | the variables matching the patterns, and nothing else | variables carry secrets, so the module alone shows **none**: `Environment.get` answers `None` and `variables()` is empty until patterns are granted, as today |
| `modules "std/os/directories"` | where the user's files belong — and therefore the user's name, which is in every path | a path is not a secret, but it identifies a person |
| `modules "std/os/system"` | version, host name, uptime | the host name identifies a machine |
| `modules "std/os/processor"`, `"std/os/memory"` | counts, load, memory | harmless alone, and a fingerprint together |
| `modules "std/os/volumes"`, `"std/os/network"` | mount points, device names, addresses | the network grant reveals where the machine is |
| `modules "std/os/user"`, `"std/os/process"` | the account, the executable's path, the process's resources | the host process's details |
| `modules "std/os"` | all of the above, with the environment still filtered by its patterns | the explicit "trust this script with the machine" |

**The per-system directories are never grantable.** A grant of `std/os/windows/registry` or `std/os/linux/native`
is refused at `Sandbox.load`, the way foreign functions are (`the-sandbox.md`, rule 7): those modules hold raw natives,
and the sandbox grants OS-neutral questions only. A granted neutral module still reaches its per-system code — that
is its implementation, not the script's import, and the `modules` capability is about the script's `use` lines.

**`project.trb`'s grant becomes `modules "std/fs", "std/text", "std/os/environment"`** with `environment "*"`, exactly
the grant of `docs/design/PROJECT.md` section 8 with the module renamed; a dependency's manifest still loads without
it, and the refusal still names the module.

**Nothing is enforced before milestone 7.4**, like every sandbox capability: the grants above are what 7.4 checks, and
this design adds only the rule that a module path inside `std/os` is a valid grant and a per-system one is not.

## 9. What the compiler and the runtime must provide

1. **The target intrinsics.** Three rows of the manifest, `.Intrinsic`, and three cases in the constant evaluator
   (`compiler/src/ir/constant.trb`) that answer the case of the build's target. The target comes from `--target`
   (PROJECT.md section 5), the host by default.
2. **`==` and `!=` between two field-less cases in the constant evaluator**, so that an `if` on the target folds.
3. **The lowering decides a constant `match`, `if` and `if const`** (`compiler/src/ir/lower/match.trb` and the `if`
   lowering): evaluate the subject with `evaluateConstant`, and where it answers, lower the selected arm only. This is
   the general rule of section 2 and helps every `const verbose = false` as much as the target.
4. **`NativeEntry.availableOn`** in `compiler/src/backend/c/natives.trb`: the operating systems and architectures a
   native exists on, empty for "every target". A call of a native whose `availableOn` excludes the build's target is
   the error of section 2, reported where the lowering reaches it, with the chain of callers that led there.
5. **`torb check --every-target`**: after the type check, lower every program and test of the path once per target
   of the toolchain's list, stop before C, and report per target. Added to `tools/gates.sh a` for `std/os`.
6. **`runtime/os/`** and `runtime/include/torb_os.h`; `runtime/build.sh` and the driver compile `runtime/os/*.c` on
   every machine, each file guarded as a whole.
7. **`Timestamp` in `std/time`**, owned by that package's own design; `System.bootTime` waits for it.
8. **The blocking pool and `offload`** (CONCURRENCY section 7, milestone 7.7) for the waiting calls of section 4.
   *Built* (CONCURRENCY section 16, "The blocking pool, as built"); the waiting calls of section 4 still call their
   native directly, and move under `offload` when they are written.
9. **Not required, noted:** package-private visibility would let the `native.trb` modules be unreachable from outside
   `std/os`. Section 3 explains why the design does not need it.

Items 1 to 5 are **a new native and a new flag the build uses**, so they take the two commits `CLAUDE.md` names: teach
the compiler and refresh the seed, then write code that depends on them.

## 10. Slices

Each slice lands with `tools/gates.sh a` green, its own tests, and `docs check` clean. The order is what others need
first: the construct, then the environment, identity and directories that the toolchain itself uses, then the rest
"as needed".

| # | Slice | Files | Depends on | Risk |
|---|---|---|---|---|
| 0 | **Done (2026-09-23).** **The construct.** `std/core/src/target.trb` with the three types and intrinsics; the constant evaluator; the lowering of a constant `match`/`if`; `availableOn` and its diagnostic; `torb check --every-target`; `runtime/os/` with `torb_os.h` and empty guarded files; the language page of section 2 | `std/core`, `compiler/src/ir/{constant.trb,lower/match.trb,lower/statement.trb}`, `compiler/src/backend/c/natives.trb`, `compiler/src/main.trb` (the `check` subcommand), `runtime/{build.sh,os/,include/torb_os.h}`, `docs/language/execution/` | — | **Medium.** It touches the lowering and the manifest, so it needs `gates.sh b` and the two-commit seed refresh. The fold itself is small; the test is a program whose untaken arm calls a native that does not exist on the host, built on the host |
| 1 | **Environment, identity, directories.** `std/os` created; `Environment` moved with `variables()` and `searchPath()`; `EnvironmentVariables`; `OsError` and `outcome`; `System.version`, `hostName`, `uptime`, `pageSize`, `machineArchitecture`; `Directories` (the base six); the Windows, Linux, macOS, POSIX and XDG natives these need; `std/environment` removed and every importer migrated (section 6) | `std/os/**`, `std/environment/` (deleted), `compiler/src/{cli,documentation,project}/*`, `examples/config-dsl`, `tests/conformance/*`, `runtime/os/{windows,linux,posix,macos}.c`, `runtime/platform.c`, `docs/standard-library/os.md` and the pages of section 6 | 0 | **Medium.** The compiler imports the package, so it is part of the fixpoint; the natives are small. FreeBSD's arms are written and answer `Unsupported` until slice 7 |
| 2 | **`ByteSize`, memory and the current process.** `ByteSize` in `std/number` and the prelude, the sandbox's `megabytes` replaced; `Memory`, `Swap`; `CurrentProcess` whole, with `Process.executablePath` moved | `std/number`, `std/prelude`, `std/sandbox`, `std/process`, `std/os/src/{memory,process}.trb` and the per-system halves, `compiler/src/project/toolchain.trb`, `runtime/os/*.c`, `runtime/process.c` | 1 | **Low.** `megabytes` changes type, and every `64.megabytes()` in docs and tests follows |
| 3 | **Processors.** Counts, `describe` with `x86/`, `times`, `timesOfEach`, `usage` (a `Task` over `sleep`), `frequencies`, `Frequency` | `std/os/src/processor.trb`, `x86/`, per-system halves, `runtime/os/{x86,windows,linux,macos,bsd}.c` | 2 | **Low.** The decoders are pure and tested on every machine with captured inputs |
| 4 | **Volumes.** `Volume.all`, `space`, `containing`, the `/proc/self/mountinfo` parser, `getmntinfo` | `std/os/src/volumes.trb`, per-system halves, `runtime/os/{windows,posix,bsd}.c` | 2 | **Low**, and `space` runs synchronously until 7.7 |
| 5 | **`std/network` and interfaces.** The five address values with parsing and RFC 5952 display; `NetworkInterface.all`, `traffic`, `throughput` | `std/network/**`, `std/os/src/network.trb`, per-system halves, `runtime/os/{windows,posix,bsd}.c` | 2 | **Medium**, for the parsers: IPv6 text has many spellings and each needs a test |
| 6 | **The user, the user folders, a child's environment.** `User.current`, `documents()` and the other five, `user-dirs.dirs`; `Process.run`/`start` gain `environment:` | `std/os/src/{user,directories}.trb`, `xdg/`, `std/process`, `runtime/platform.c`, `runtime/os/*.c` | 1 | **Low.** `environment:` is new behaviour in `torb_platform_run_process` on both halves |
| 7 | **FreeBSD.** `runtime/os/freebsd.c`, the `freebsd/` directory, every `Unsupported` arm of slices 1 to 6 replaced | `std/os/src/freebsd/`, `bsd/`, `runtime/os/{freebsd,bsd}.c` | 1–6 | **Medium**, because nobody here runs FreeBSD: the TorbScript is checked and lowered for it everywhere, the C is compiled only on FreeBSD |

**How slice 0 landed.** The compiler does not read the target constants of its own build and uses no native of
`runtime/os/`, so no seed refresh was needed: the seed compiles `std/core/src/target.trb` without ever lowering it.
`hostTarget()` asks the environment on Windows and `uname` elsewhere; reading `OperatingSystem.current` there instead is
the first use that takes the two commits. The raw layer has one native per family so far, enough for the conformance
program and for the error of a native reached on the wrong target. A target that is not the host builds with `--emit-c`
only, because the C compiler `torb` finds builds for the host.

`System.bootTime` lands with `std/time`'s `Timestamp`, whenever that is; the sandbox grants of section 8 are checked
from 7.4; the waiting calls move onto the blocking pool with 7.7; signals are `std/process`'s and CONCURRENCY's.

## 11. What this is not

- **Not conditional compilation.** Every arm is parsed, resolved and type checked on every machine, and no declaration
  exists on one target and not on another. What the compiler removes is code it has already checked.
- **Not a process manager.** Listing and inspecting *other* processes (`ps`, `tasklist`) is about processes, so it
  would be `std/process` (`Process.all()`), and it is not part of this design.
- **Not hardware monitoring.** Temperatures, fans, batteries and GPUs are not here. A battery is a later topic of
  `std/os` if something needs one; a GPU belongs to the `std/gpu` of the std vision, which will read the processor
  features from here.
- **Not a registry or `sysctl` API.** Both are reachable from their system's directory for a program that needs them,
  and neither is part of the OS-neutral surface, because neither exists on the other systems.
- **Not a way to change the machine or the process.** No setting of variables, of the working directory, of the host
  name, of limits. `std/os` reads; the one thing a program changes is a child's environment, and that is a value.
- **Not a promise that a number means the same everywhere.** `available` memory is each system's own estimate and the
  doc comment says so; `ProcessorTimes.waiting` is zero where the system does not count it. The types are the same,
  and the tables of section 5 say where the numbers come from.

## 12. Open

Everything technical above is decided. These are questions of taste or direction, and only the owner answers them.

**Answered by the owner (2026-09-23):** 1 - the rule is accepted as written. 2 - `OperatingSystem.current` (one
`static current` per type). 3 - FreeBSD is in from the start, a fourth case every branch handles (slice 7 moves up
with the others). 4 - `~/Library/Application Support` on macOS. 5 - `ByteSize` shows binary units (`15.9 GiB`).

1. **The construct's rule itself (section 2, "The rule").** It is the one language addition of this design: a `match`
   on a compile-time constant keeps one arm, every arm is still checked, exhaustiveness is by type. It is general — it
   also folds `const verbose = false` — and the document assumes it is accepted.
2. **`OperatingSystem.current` or `Target.operatingSystem`?** The document puts one `static current` on each of three
   types (`OperatingSystem.current`, `Architecture.current`, `ByteOrder.current`), so the type and its constant are
   one import. The alternative groups the three under one `Target` type, which reads as "the build's target" at the
   branch (`match Target.operatingSystem`) and costs a fourth name. The case names `MacOs` and `FreeBsd` follow the
   repository's camel-cased acronyms (`IoError`, `Utf8Error`); `show()` answers `macOS` and `FreeBSD`.
3. **FreeBSD as a fourth family now, or later?** It is in the type and the tree because it is the one POSIX system
   without `/proc`, which keeps `posix/` honest, and because the runtime's POSIX half already compiles there. The cost
   is a case every `match` must handle and a slice (7) nobody here can run. Leaving it out makes `OperatingSystem`
   three cases today and a breaking change later.
4. **macOS: `~/Library/Application Support` or `~/.config` for configuration?** The document follows the platform's
   convention. Many command-line tools (git, and most tools written in Go and Rust) use `~/.config` on macOS as well,
   and `torb` itself is one; a caller that wants that joins it from `Directories.home()`.
5. **How `ByteSize` shows itself.** The document shows binary units with their IEC symbols (`15.9 GiB`), because they
   are unambiguous and memory is counted in them. Decimal units (`17.1 GB`) are what disks and most file managers
   show; both are one member call away (`gigabytes()`, `gibibytes()`), and only the default of `print` is the question.
