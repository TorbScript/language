# The Exit of Stage 0

**Status: historical** — the exit happened on 2026-09-22; this is the record of how it was decided and done.

> **A historical record.** Stage 0 — the Rust interpreter, front end and test suites under `bootstrap/` — no longer
> exists. Sections 1 to 6 are the inventory and the plan as they were written before the deletion, in the tense of
> that moment, and section 7 is what the exit came to. What a checkout does today is in `CLAUDE.md` and
> `tools/bootstrap.sh`; what is still open from here is marked where it stands (sections 2.2 and 2.4 carry
> measurements from after the exit).

`bootstrap/crates` was 14 560 lines of Rust in three crates, and this document is the record of removing it. What
stays is the C back end and the C runtime: a C compiler is the one external tool a checkout needs, and everything
above it - the front end, the checker, the lowering, the emitter, the driver, the formatter, the highlighter - is
TorbScript compiled by TorbScript.

The interpreter requirement of the language is met by the bytecode VM of milestone 7, which is written in TorbScript
and reads the same IR the C back end reads, so "the same program runs interpreted and compiles to a native
executable" survives the deletion intact.

Below is the inventory it was decided on: what stage 0 was used for and what replaced each of those, what the native
path could not do and whether that blocked the exit, what the two implementations cost, how a checkout gets its first
compiler, the six slices, and what the exit came to (section 7).

Every number below was measured on one machine (Windows 11, gcc 13.2.0 from MinGW-W64), under the load of other work
on the same machine, and is there to be compared with the number beside it rather than with another machine's.

---

## 1. What stage 0 is used for, and what replaces it

Stage 0 was a command (`torb` from `bootstrap/`) with six subcommands, a Rust front end under it, and four `cargo test`
suites beside it. Every use of it in the repository, as it stood before the exit:

| What uses stage 0 | What replaces it | Blocks the exit? |
|---|---|---|
| `torb run ../compiler <command>` - the driver for `check`, `ir`, `build`, `natives`, `parse`, `tokens`, `ast`, `docs` | The native `torb` binary, which **is** `compiler/src/main.trb` compiled | no - done |
| `torb test ../compiler/tests` - the compiler's own 1538 tests | Native `torb test`, one binary for the whole suite | no - done |
| `torb test` for any other package (`std/*/tests`, `examples/*/tests`) | Native `torb test <path>...`, several packages in one run | no - done |
| `torb canon --check --rule ...` - the formatter canon, five rules, 1 341 lines of Rust with 561 more of tests | `canon` ported to TorbScript on the self-hosted parser (slice 3, done), then milestone 8's `torb format` | no - done |
| `torb canon ..` - writing the canon | the same | no - done |
| `torb highlight --stdin` - the editor extension's semantic tokens | The native binary's own `highlight` (slice 4) | no - done |
| `cargo test --test native` - the conformance suite, 74 programs run both ways and compared | `tools/conformance.sh`, which builds and runs each program natively and compares it with `.expected`, `.stderr`, `.exit` and the leak count (slice 2) | no - done |
| `cargo test --test suite` - stage 0's test report against the binary's, line by line | Nothing. Its subject is the agreement of two implementations, and after the exit there is one | no |
| `cargo test --test self_hosted` - the self-hosted front end's `tokens`/`ast`/`parse` against the Rust front end's, over every `.trb` of the repository | Nothing, for the same reason. What it defends (the parser accepts the whole repository) is `torb parse ..` plus the parser's own tests | no |
| `cargo test --test fixpoint` - stage 1 -> stage 2 -> stage 3, byte-identical C | `tools/bootstrap.sh`: seed -> `torb` -> `torb`, byte-identical C. The same comparison from one step fewer | no - done |
| `cargo test -p torb-syntax --test examples` - every `.trb` in the repository parses | `torb parse ..` | no |
| `cargo test -p torb-syntax --test grammar` - 347 lines of grammar unit tests in Rust | The self-hosted parser's own tests in `compiler/tests` | no, but the cases have to be **read** before the file goes, and the ones that are not covered moved |
| `bootstrap/tests/native/` - the programs of the conformance suite | Moved to `tests/conformance/` (slice 6). The programs are TorbScript and outlive the runner | no - done |
| `bootstrap/tests/scripts/` - two long programs (370 lines) run on stage 0 alone | `torb run`, natively, from `tests/language/` (slice 6). They are a smoke test of the language, not of stage 0 | no - done |
| `bootstrap/tests/native/stage-0-only/` - one program whose behaviour only stage 0 produced | The C back end lowers the `cause()` loop of a top-level `?` (slice 5); the program moved up one directory and the now-empty `stage-0-only/` is gone | no - done |
| `bootstrap/tests/lexer-cases/`, `parser-cases/` | Deliberately broken sources; they moved to `tests/lexer-cases/` and `tests/parser-cases/` (slice 6) and are what the recovery of the self-hosted parser is read against | no - done |
| `.vscode/tasks.json` - two tasks that run `cargo run --release -q -- ...` | The same two commands on the native binary (slice 6) | no - done |
| `.vscode/extensions/torbscript` - `torb highlight --stdin`, and a search for the binary | The search looks under `build/release/` and nowhere else (slices 4 and 6) | no - done |
| `benchmarks/run.sh` - `$torb` is `bootstrap/target/release/torb`, used to build each program | `build/release/torb`, called directly (slice 6) | no - done |
| `runtime/build.sh` - checks `torb_natives.h` against the headers, and names `torb natives --header` in a comment | Already independent: the check is C against C. Only the comment mentions the command | no |
| `docs/` front matter - ten pages carry `bootstrap/README.md` or a `bootstrap/crates/...` path in `source:`, and the docs gate asserts those paths exist | Repointed at the compiler's own sources, at `tools/bootstrap.sh` and at `tools/gates.sh` (slice 6) | no - done |
| `compiler/CONTRIBUTING.md`, `bootstrap/README.md`, `docs/ARCHITECTURE.md`, `docs/BACKEND.md` - the command lists and the description of the two-stage world | Rewritten in slice 6; `bootstrap/README.md` is gone and what it said lives in `compiler/CONTRIBUTING.md` and section 7 | no - done |

**Nothing in this table blocked the exit.** The one lowering gap (`error-chain.trb`, slice 5) is closed, and the
other thing that was never a use of stage 0 at all but a property of it - stage 0 is what **produced** the first
`torb` - is answered by section 4's seed. All six slices are done.

---

## 2. What the native path cannot do that stage 0 can

### 2.1 Constructs the lowering or the emitter does not translate

There is no list of unsupported constructs in a file; there are call sites of `reportUnsupported`
(`compiler/src/ir/lower/context.trb` for the lowering, `compiler/src/backend/c/emission.trb` for the emitter), and each
one produces one sentence through `unsupportedMessage` in `compiler/src/ir/unsupported.trb`. The authoritative list is
therefore what a run produces, not what a file says:

```console
$ torb ir --statistics ..
```

prints `N of M functions lowered`, then one line per construct with how many times it occurred and where the first one
is. Per part of the repository:

| What was lowered | Functions | Not supported |
|---|---|---|
| `compiler` | 12 661 of 12 661 (100%) | **none** |
| `benchmarks` | 88 of 88 (100%) | none |
| `examples` (with the `std/` modules they reach) | 3 848 of 3 883 (99%) | 35 occurrences of 19 constructs, plus 3 `native type`s |
| `std` alone | - | the run **panics**: see below |

The 19 constructs in `examples`, by how often they occur:

| Times | Construct |
|---|---|
| 11 | a body whose result is a `Task` |
| 3 | `if var`, which binds a path into its subject |
| 2 | `Task.await` - the runtime does not provide it |
| 2 | `describe` used as a function value, which the back end cannot build an instance of |
| 2 | `record`, which is not in the witness table of a trait-typed value |
| 2 | a generic function or a member of a generic type |
| 1 each | `Json.encode`; `onto` outside a witness table; `tryFrom`, which the manifest does not know; a `var` receiver through a temporary; a conversion through `From`; a derived `encode` whose `Encoder` is all `var self` members; a literal that fills an `Array` of 4 items; a quoted expression; a receiver the back end cannot reach; a value of type `File`, of type `Instant`, of type `Script`; an argument that fills no parameter |
| - | the `native type`s `File`, `Instant` and `Script` |

Thirteen of the 35 are one feature - tasks - and the other 22 are a long tail of one and two.

**Re-measured over the whole repository (`torb ir --statistics .`) on 2026-09-23**, before and after the rounds that
lowered `From`/`Into` conversions, generic trait members, `if var` and the narrowing conversions of `std/number`: from
27 384 of 27 440 functions with **56** constructs not supported and one internal error (`std/path`'s `textOf`, which made
the run leave with 1) to 28 755 of 28 797 with **42** and none. Gone are the conversion through `From`, `onto` outside a
witness table, `describe` as a function value, `if var`, `record` outside a witness table, `tryFrom` between numbers, an
argument that fills no parameter (`Point.area(p)`) and a literal built through the `from` of an `Iterate`; twenty of the
42 left are bodies whose result is a `Task`.

**Measured again after the tasks round (main at `e8027c1`): 32 831 of 32 869 with 38**, and after the round that
followed it **33 379 of 33 400 with 21** - **33 687 of 33 712 with 25** once the destructors round was merged, whose
tour adds `Array.filled` three times and a const parameter read as a value (`0..Rows`), both of the `Array` gap below. Closed, each with a conformance program: `Duration` and `Instant` (records of
`std/time` over an `Int64` of nanoseconds, arithmetic in TorbScript, one native reading of the monotonic clock;
`time`, and `task-within` for `Task.within` in time and timed out), `File` as a value (`RuntimeKind.FileHandle`, the
`torb_file` pointer released through its own drop; `file-values`), a `?` whose conversion is a `From` among several of
the target or goes through the bound of a generic failure, a function value `Failure.from` on such a parameter, a
witness table that took the prelude's `into()` for `Source.into(sink)` (`conversion-choice`), a member of an
implementation whose parameter only its trait names - `to<List<Int>?>()` (`collect-through-wrappers`), a `var fn` on a
freshly produced shared object (`temporary-receiver`), a receiver closure held by a constant and called through a
value (`receiver-constant`) and `tryFrom` into a literal type (`literal-try-from`). Of the 21 left, 15 are natives
the runtime does not provide yet by design (`Json.encode`/`decode`, `get` of `std/http`, the stream side of `File`,
`standardInput`, `Expression.value`, `Array.filled`) or belong to 7.4 (`Script`, a receiver script); the others are a
derived `encode` over a `var self` encoder, a quoted expression, a method taken as a bound function value
(`rectangle.area` without a call) and the `Array` literal. **`Array<Item, Size>` is the one real design gap of the
list** (BACKEND 5.9b): it has no C type yet, and giving it one is a struct per item type and size, per-item retain and
release, an instruction that fills it, element paths with bounds checks, and an iterator for `iterate()` - which
`Show`, `Equals` and `Hash` of `std/core` are written over.

**None of this blocks the exit.** A construct the back end does not lower is a gap of the back end and is tracked as
one; that stage 0 happens to interpret it is not a reason to keep 14 435 lines of Rust. The proof is the first row of
the table: **the compiler lowers completely**, so it, its tests and every gate are already independent of stage 0. The
one exception is in section 2.3.

**Found while measuring this, and it is a bug rather than a gap:** `torb ir --statistics std` (and therefore over the
whole repository) leaves with `panic: arithmetic overflow in `-`` at `compiler/src/ir/mangle.trb:251`. That is
`canonicalLiteral` writing `minus{0 - inner}` for a negative integer literal in a mangled name, and the smallest
`Int64` has no positive counterpart - the very case `tests/conformance/negate-overflow.trb` pins for the
*language*. The mangled form of that one literal needs to be written without negating it. It does not block a build
(`torb build`, which lowers only what an entry reaches, is not affected), and it belongs to the lowering follow-up.

### 2.2 The natives that are `.Planned`

`compiler/src/backend/c/natives.trb` marks a declaration of `std/` that the runtime does not implement yet as
`NativeState.Planned(milestone)`, which makes using it a clean compile error instead of a missing C symbol. This
section first said that six declarations carry it — `Process.start` and the five members of `Child`. **Measured again
on 2026-09-22** by a grep of `plannedRuntimeOf(`/`plannedIntrinsicOf(` in `natives.trb`: **44 manifest lines, 71
declarations**, because three of the lines are loops over a member list (`Float32` with 10 members and `Decimal` with
11, both milestone 8; `Array` with 9, milestone 5.9b). The other 41 are `Float32.tryFrom(String)` and
`Decimal.tryFrom(String)` (8), the stream side of `File` (`create`, `chunks`,
`add`, and `finish` — which `std/fs` now declares as `end`, so the entry is stale), the three standard streams,
`Process.start` and the five members of `Child`, `sleep`, `Task.await`/`map`/`flatMap`/`all`, `spawn`, `all`,
`Channel.source`/`sink` (milestone 7.3); `Expression.value`/`captures` (5.11); `describe` and the four members of
`Json` (5.7); `isSame` (5.9); the six members of the sandbox (7.4); and `get`/`post`/`request` of `std/http` (8).

Nothing in the compiler, its tests or the gates uses them. `torb build` runs the C compiler through
`Process.runCollecting`, and `torb run` and `torb test` run what they built through `Process.runInheriting`, which is
implemented. **Does not block the exit.**

### 2.3 Programs only one implementation runs

| Where | How many | What it is | Blocks? |
|---|---|---|---|
| `bootstrap/tests/native/` | 82 programs | The conformance suite: every program built and run with the native compiler, compared against its `.expected`/`.stderr`/`.exit`/`.leaks` - `error-chain.trb` among them since slice 5 closed the one gap that kept it out | no - done. The programs never blocked; the **runner** did, and `tools/conformance.sh` is it (slice 2) |
| `bootstrap/tests/native/binary-only/` | 2 programs | Behaviour the two implementations answer **deliberately** differently: a failing `assert` showing a non-scalar capture, and `into()` through the blanket implementation of `Into`. Both are cases stage 0 cannot answer because it has no types | no - they are already native-only, and after the exit they are ordinary programs of the suite |
| `bootstrap/tests/scripts/` | 2 programs, 370 lines | Long programs that exercise many things at once, on stage 0 alone | no - they run natively with `torb run` |

### 2.4 The test packages, built natively

Every test package in the repository, built and run with the native test runner (`torb test <path>`):

| Package | Result | Time | What stops it |
|---|---|---|---|
| `compiler/tests` | **1538 passed, 0 failed (55 files)** | 205 s | - |
| `std/geometry/tests` | **71 passed, 0 failed (2 files)** | 14 s | - |
| `std/linear/tests` | **98 passed, 0 failed (3 files)** | 13 s | - |
| `std/path/tests` | **59 passed, 0 failed (2 files)** | 8 s | - (built since the `From` round: `textOf<Value>(value: Value): String where String: From<Value>` calls `String.from value`, and the checker picked `String`'s own `From<Iterate<Char>>` for every `Value` because a value of a type parameter fits every candidate of the overload set. A static `from` whose argument is a type parameter now resolves through the bound the call spells, `From<Value>`, as a `Dispatch.Object` the lowering answers under the instance's arguments - `From<Path>` for `Value = Path`) |
| `std/stream/tests` | **10 passed, 0 failed (1 file)** | 9 s | - (built since the generic-member round: `lines().onto(Collected())` calls `Stage<Input, Output>.onto<Final>`, which is itself **generic**, on a trait-typed receiver. A witness table holds such a member as one slot per list of arguments the program calls it with, behind the fixed members, and every table of the trait - the ones built already and every one built later - gets the instance of its own type's member at that position (`genericSlotOf` in `ir/witness.trb`). The set is closed because the program is: a slot is instantiated for the arguments a call site decided, exactly as a generic function is) |
| `examples/encoding-lab/tests` | **37 passed, 0 failed (1 file)** | 8 s | - (built since the generic-member round: `structureOf(Order.describe)` takes the generic `static fn describe<Target: Describer>(var target: Target)` of the `Describe` implementation as a function value; the lowering reaches the implementation's member through the dispatch the checker recorded and reads `Target` off the function type the value is used as. Behind it stood `if var`, which `std/iteration`'s `FlatMappedIterator` uses: its names are paths into the subject through `Lowering.references`, not copies) |
| `examples/game-engine/tests` | **6 passed, 0 failed (1 file)** | 8 s | - (built since the `From` round: `value.into()` and `value.tryInto()` are lowered as the call of the target's `from`/`tryFrom` with the value as its argument, `lowerConversionCall` in `ir/lower/call.trb`, resolved again under the instance with the bound the source spells. Behind it stood a second gap: an `Indexed.at` instance an index path asked for was left without a call once `ir/elements.trb` made the round trip one `Element` step, and a `static` function nothing calls is an error under `-Werror` - `keptFunctions` is now a reachability over the call graph and leaves it out) |

**Re-measured on 2026-09-22, after the `From` and generic-member rounds:** every row builds and passes natively, and
`tools/gates.sh` skips none of them.

All seven build and pass; the compiler's own is the big one. The four that did not were one bug of the lowering
(`std/path`, a `where` bound that reached the wrong `From`) and three occurrences of the constructs of section 2.1 - a
conversion through `From`, and the generic-member limit twice, once in a table and once as a function value - and each
is closed, with a conformance program of its own (`conversions`, `methods-through-types`, `generic-trait-members`, `if-var`).

**None of the four blocked the exit.** They were packages whose tests only stage 0 could run, and every one of them
was a back-end gap tracked as a back-end gap. Keeping a second implementation of the language so that four test files
kept running was the trade this whole document argues against - and the gate that has to keep running, the conformance
suite, is not among them.

**Found by running this, and it is a bug of the runtime rather than a gap of the back end:** on Windows, a child
process could not be started at all when its path was **relative and spelled with forward slashes** - which is how
every path of this language is spelled. `compiler/tests/build/release/tests.exe` failed, `compiler\tests\...` started,
and an absolute path with forward slashes started too, which is what made it look like one caller's problem.
`CreateProcessW` is called without an application name so that it searches `PATH` and appends `.exe`, and it then
parses the name out of the command line by a rule that does not know `/` as a separator. It affected
`Process.run` exactly as much as the new pass-through. The program's name is now written with backslashes at the
boundary in `runtime/platform.c`, and its arguments are not.

---

## 3. What the two implementations cost

Measured on one machine, release builds on both sides. The test rows come from the `suite` gate's own report and the
build rows from the `fixpoint` gate's, each of which runs its pair in one go and therefore under one load.

| What | Stage 0 | The native binary | Factor |
|---|---|---|---|
| `check ..` - the whole repository, 341 files | 58.9 s | 8.0 s | **7.4x** |
| `check --statistics ..` | (the same pass) | 7.9 s | |
| `test ../compiler/tests` - 1538 tests in 55 files | 289.3 s (one process per file, all cores) | 205.0 s (one C compile of a 78 MiB translation unit, then one process) | **1.4x** |
| Building the compiler (`build ../compiler`) | 267.3 s | 109.1 s | **2.5x** |
| ...only emitting its C (`--emit-c`, no C compiler) | | 23.0 s | |
| `run` of a small script, first time | 0.03 s (it interprets) | 12.6 s (it builds) | |
| `run` of the same script again | 0.03 s | 0.17 s (the cache hits) | |

The last two rows are the one place where stage 0 is ahead and will stay ahead: an interpreter starts a small script
faster than a compiler can hash a workspace and start a binary. 0.17 s of a warm `torb run` is reading and hashing the
341 files of the workspace, which is what makes the answer "this binary is current" true. That is the cost of having no
interpreter, and the VM of milestone 7 is what takes it back.

These numbers are why every later round gets cheaper. A round today pays 59 s for `check` and 289 s for the compiler's
tests, twice or three times over; the same round on the native binary pays 8 s and 205 s. The gate policy of section 6
is written around that: it is worth running more of the cheap gates and fewer of the expensive ones.

The test suite is only 1.4x and not 7x, and the reason is worth naming because it decides where the next round of
effort goes: **almost all of those 205 s are the C compiler**, on one translation unit of 78 MiB. The tests themselves
run in a few seconds. Stage 0 spends its 289 s interpreting and spreads that over every core; the binary spends its
205 s in one `gcc` that cannot be spread at all. Splitting the suite into several translation units, or caching them,
is what would move that number - not anything about the language.

The build rows say the same thing from the other side, and they are the ones that measure the compiler itself. Of the
native build's 109.1 s, **23.0 s is the compiler** (that is the `--emit-c` row: everything from reading the sources to
writing the C) and the remaining 86 s is `gcc`. The interpreted build pays the same 86 s of `gcc`, so its compiler half
is about 181 s - **roughly eight times**, which is the same factor `check` shows. Everywhere the work is TorbScript
rather than C, the binary is seven to eight times stage 0.

The artifacts:

| What | Size |
|---|---|
| `program.c` - the compiler as one C file | 58 342 423 bytes (55.6 MiB) |
| the same, `gzip -9` | 3 331 499 bytes (3.2 MiB) - **5.7%**, because generated C is repetitive |
| `torb.exe` - the binary that comes out | 9 047 773 bytes (8.6 MiB) |

The compressed C is *smaller than the binary it produces*, and it is one file for every platform. That is what decides
section 4.

---

## 4. The seed

A checkout has no way to compile `compiler/` on its own, because the only thing that compiles TorbScript is written
in TorbScript. Something that already exists has to compile it once. The options:

| Option | What it costs | What it buys |
|---|---|---|
| A previous release binary per platform | One binary per platform per release, hosted somewhere. Go and Rust both do this | The fastest path: a download and one build |
| The emitted C of the compiler as a release artifact | One file per release: 55.6 MiB, **3.2 MiB compressed**, which is less than the binary it builds; a C compile of a single 58-megabyte translation unit takes minutes | **Builds anywhere a C compiler exists**, including a platform nobody has published a binary for. This is what Nim's `csources` is |
| A wasm seed, as Zig does | A wasm runtime in the build, plus the machinery to produce the wasm | One artifact for every platform. It is the right answer for a project with a wasm back end, and there is none here |
| Keeping the Rust bootstrap | 14 435 lines of a second implementation, forever, plus a Rust toolchain in every build | Nothing this project wants |

### 4.1 The decision

**The seed is a native `torb` binary kept outside git, plus the compiler's C beside it for a platform that has no
binary.**

```text
seed/                 ignored by git; not an artifact of this commit
  torb[.exe]          a `torb` that already exists: the previous commit's, or a release download
  program.c           the portable seed: the compiler as one C file, for a platform with no binary
tools/bootstrap.sh    builds `torb` from whichever of the two is there, then builds it again with itself
build/bootstrap/torb  step 1: the seed's compiler
build/release/torb    step 2: that compiler's compiler - the one that is used
```

`seed/` is ignored rather than committed because a binary in git is a binary in every clone forever, and because the
seed is not a fact about this commit: it is whatever compiler a person happens to have. What a commit promises is that
**some** recent `torb` can build it.

`tools/bootstrap.sh` is POSIX `sh` and runs in Git Bash on Windows. It does two steps and not one, because one says
nothing: the seed compiles the current sources, so what comes out was built by an older compiler; that binary compiles
the sources again, and the two `program.c` are compared byte for byte. A seed older than the code generation it built
gets a third step, so that two builds of the *same* compiler are what is compared.

**Where a seed comes from.** From the first release on it is a download: a release publishes a `torb` per platform
and the compiler's own `program.c` as one file, and `tools/bootstrap.sh` takes whichever of the two is under
`seed/`. Until then it is whatever `torb` was built last - `torb build ./compiler --output ./seed/torb` for the
binary and `torb build ./compiler --emit-c --output ./seed/torb` for `seed/program.c` - which is the maintainer's
`seed/` directory. `README.md` says this under "Building".

The chain, run:

```console
$ sh tools/bootstrap.sh
seed: seed/torb
step 1: the seed builds the compiler
wrote build/bootstrap/torb.exe
step 2: that compiler builds the compiler again
wrote build/release/torb.exe

the fixpoint holds: both steps emitted the same C.
torb: build/release/torb
```

That is the whole claim of this section, demonstrated: a checkout with a seed and a C compiler produces a `torb` that
is a fixed point of itself.

### 4.2 A breaking change

This matters because the language still changes weekly, and it is the one thing a seed-based bootstrap makes harder.
**The seed compiles the old syntax**, so a commit that changes the syntax cannot be compiled by the seed that came
before it. The answer is two commits and a migration that is part of the compiler:

1. **The commit that teaches.** The compiler learns to accept the new form *as well as* the old one, and the migration
   from the old form to the new one is added to `torb canon` (or, from milestone 8, `torb format`). The sources are
   still written in the old form, so the seed that came before this commit compiles it. A new seed is produced from it.
2. **The commit that switches.** The migration is run over the whole repository, so every source is now in the new
   form, and the old form is removed from the parser. This commit needs the seed from step 1 - which accepts both - and
   `tools/bootstrap.sh` proves the fixed point as usual.

A person who is further back than one step rebuilds forward: the seed of step 1 is a release artifact, or is built from
`seed/program.c` of that commit. This is the same dance `rustc` does with its `cfg(bootstrap)` and Go did when it
stopped compiling itself with C; naming it here is what keeps the second commit from being attempted as the first.

The same dance is what **a new native** costs. A native is declared in `std/`, entered in the manifest of
`compiler/src/backend/c/natives.trb` and implemented in `runtime/`, and a seed built before it cannot compile a
compiler that calls it, because the emitter's manifest is compiled into the seed. So a new native is step 1 and step
2 above: the commit that teaches, a fresh seed from it, then the commit that uses it.

---

## 5. The slices

Each slice is one agent, in order. The estimate is the work, not the machine time.

| # | What | Gate when it is done | Estimate |
|---|---|---|---|
| 1 | **The seed and the native driver.** `torb run` (build into a cache keyed on the sources, then execute with the arguments, the streams and the exit code passed through), `torb test` for any test package and several at once, `tools/bootstrap.sh`, `seed/` ignored, `Process.runInheriting` in the runtime | Tier A on the native binary; the chain seed -> `torb` -> `torb` with byte-identical C | **done** |
| 2 | **The gates run on the native compiler.** `tools/conformance.sh` (POSIX `sh`) replaces the three `cargo test` suites: conformance compares a native run against `.expected`/`.stderr`/`.exit`/`.leaks` and no longer against stage 0; `tools/gates.sh a`/`b` sequence tier A and tier B; the fixpoint is `tools/bootstrap.sh`; `suite` and `self_hosted` are dropped, and section 6 says what they defended | The conformance suite green from `tools/conformance.sh`, on the same 74 programs (75 with `binary-only/`, one `stage-0-only/` skipped) | **done** |
| 3 | **`canon` ported to TorbScript, done.** The five rules of `bootstrap/crates/torb-cli/src/canon` on the self-hosted parser, in `compiler/src/canon`, with the same rule flags, the same `--check`, and the same "apply one edit, parse again, keep it only if the tree is unchanged" safety - the tree comparison reads the generated `Show` of the syntax tree with every span and `CallStyle` erased, so no second dumper was needed | `torb canon --check ..` from the native binary reports the same files stage 0's reports - zero | done |
| 4 | **`highlight` ported.** `compiler/src/highlight/` - 1 717 lines of TorbScript, and 545 more for the 32 tests the Rust file carried inside it - and the extension looking for the native binary first | Both implementations answer with the same JSON over every `.trb` file of the repository | **done** |
| 5 | **The one lowering gap.** `reportFailure` walks `cause()` and writes one `  caused by:` line per link, after which `tests/conformance/stage-0-only/error-chain.trb` moved up one directory and the now-empty `stage-0-only/` is gone | The conformance suite with `error-chain.trb` built and run natively like every other program | **done** |
| 6 | **The deletion.** `bootstrap/tests/` moves to `tests/`, `bootstrap/` is deleted, and every reference is rewritten: `compiler/CONTRIBUTING.md`, `docs/ARCHITECTURE.md`, `docs/BACKEND.md`, the `source:` entries in `docs/`, `.vscode/`, `benchmarks/`, and this document. Section 7 is what it came to | Every gate green from the native binary alone, with no Rust toolchain on the machine | **done** |

**All six slices are done.** Section 7 records what the last one moved, deleted and rewrote.

### 5.1 What slice 4 decided, and what it measured

The plan above asked for the resolver's guesses to be replaced by the **checker's** answers. Measuring said no, and the
port is the syntax-only resolver with the seam for the checker written into it:

| One `highlight` request | The native binary | Stage 0 |
|---|---|---|
| 36 lines | 15 ms | 14 ms |
| 1 000 lines | 55-61 ms | 14 ms |
| 1 802 lines | 96 ms | 18 ms |
| 2 878 lines - the largest file of the repository | 140-168 ms | 20 ms |

Of the 55 ms for a thousand lines, 13 ms is starting the process and 29 ms is the lexer and the parser, which every
command pays; the resolver itself is 19 ms. **The checker is three orders of magnitude away**: `check` over the
workspace is 8 s, and even one file cannot be checked without the modules it imports. A language server changes that
and nothing else does - it holds the workspace between requests and re-checks the one file that changed - so the
decision belongs to the round that builds one. The seam is `memberTargetOf` in `compiler/src/highlight/resolve.trb`:
it classifies the target of a `.name`, today into a type of this file, a namespace of this file, or an unknown
receiver, and a checker would answer the third case instead. Every modifier below it follows from the member that is
found, so nothing else moves.

The two implementations were run over **every `.trb` file of the repository** (431 files) and over 1 308 deliberately
damaged sources - each file cut at three fractions of its length, plus fifteen hand-written malformed ones. The JSON is
**byte for byte the same**, with two differences, both explained:

- **A file that starts with a UTF-8 BOM.** The self-hosted lexer reads the BOM as the first character of a name and
  reports the diagnostic that a name is ASCII; the Rust lexer skips it. Nothing parses after that, so the port
  colours nothing in such a file while stage 0 colours it. This is a property of the **front end** and not of the
  highlighter - `torb check`, `torb parse` and `torb tokens` say the same thing about the same file - and it belongs to
  a round of the lexer. No file in the repository has a BOM.
- **`--stdin` with CRLF line breaks.** The only native that reads standard input today is `readLine`, which strips the
  line break, so the source is rebuilt with `\n` (`standardInput()` is `NativeState.Planned(7.3)`). Every position this
  command prints is a line and a column, and both survive that - the one shape that does not is a token whose span falls
  **on the line break itself**, which only a malformed `case` with no name produces, because the parser puts the
  missing name on the token that ends the line. Stage 0 puts it one column further
  right, behind the `\r`; the port puts it where it stands whatever the line breaks are. `torb highlight <path>` reads
  the bytes and is byte-exact even there.

---

## 6. The gate policy

Written into `compiler/CONTRIBUTING.md` as the rule in force; repeated here because it is a decision of this plan and
not of that file.

- **Tier A, every round**: `sh tools/gates.sh a` - the build, `check ..` ("no problems"), `check --statistics ..`
  ("0 deferred"), the compiler's own tests, the std/example tests that build natively, `canon --check` with the five
  rules, and the two docs gates.
- **Tier B, only a round that touches the IR, a back end or `runtime/`, exactly once**, by the agent and not again on
  master: `sh tools/gates.sh b` - `tools/conformance.sh` (the conformance suite), `tools/bootstrap.sh` (the fixpoint),
  and the C runtime's own tests. On master tier B runs at most once per batch of merges, in the background, and a red
  result is fixed forward.
- **`suite` and `self_hosted` are dropped, not replaced** (slice 2). Both compared stage 0's own answer with the
  binary's, and with one of the two implementations gone there is nothing left for either to compare. What each one
  defended survives elsewhere: the compiler's tests are `torb test compiler/tests` on its own, and the parser
  accepting the whole repository is `torb parse .` plus the parser's own tests in `compiler/tests`.
- **Performance numbers are recorded, not enforced**: allocation counts, the benchmark ratios of
  [docs/PERFORMANCE.md](PERFORMANCE.md) and the size of the emitted C go into a round's report so a regression is
  visible, and none of them fails a gate by itself.

The reason for the two tiers is section 3. Tier A on the native binary is seconds plus one build; tier B is most of
an hour.

---

## 7. After the exit

`bootstrap/` is gone. **45 files and 14 947 lines** left the repository: 14 560 lines of Rust in 37 `.rs` files, the
interpreter's `prelude.trb` (121 lines), `bootstrap/README.md` (182 lines), four `Cargo.toml`, `rustfmt.toml` and
`Cargo.lock`. Nothing in a checkout names Rust or Cargo, and the toolchain builds with a C compiler alone.

### 7.1 Where the tests are

| Was | Is | Why the name |
|---|---|---|
| `bootstrap/tests/native/` | **`tests/conformance/`** | What it is: one program per behaviour, compared against what is written down beside it. `tests/conformance/README.md` is the contract |
| `bootstrap/tests/scripts/` | **`tests/language/`** | Two long programs that exercise many constructs at once, run with `torb run` and compared with their `.expected`. Not conformance programs, which each pin one thing |
| `bootstrap/tests/lexer-cases/` | **`tests/lexer-cases/`** | Kept the name: `compiler/src/canon/command.trb` skips both directories by it, and they are the deliberately broken sources the parser's recovery is read against |
| `bootstrap/tests/parser-cases/` | **`tests/parser-cases/`** | the same |

`tests/conformance/` and `tests/language/` are each a workspace of their own whose only members are `../../std/*`,
and each is checked on its own (`torb check tests/conformance tests/language`). `tests/` itself carries a
`project.trb` that holds no files, so it is a package rather than a hole. The root workspace names none of them, so
`torb check .` does not sweep them in - the programs are inputs of a test rather than parts of the toolchain - and the
two case directories are checked by nothing, because what is in them is wrong on purpose.

**The root `project.trb` had to say that it has no tests.** `testInput` defaults to `"tests"`, and the root manifest
said nothing about it, so a directory called `tests/` at the root became the root package's test directory and
`torb check .` read every program below it as a module of that package. The two deliberately broken `errors.trb`
then collided with the standard library ("`Option<Value>` already implements `Show`") and `check .` reported 179
problems in 50 files. The root manifest now writes `test { input "no-tests" }` - it has no tests of its own, every
test belongs to a member - and `check .` answers `370 files, no problems` again. The trap is closed since: a workspace
manifest has no test directory unless it says `test { input }`, and the root manifest says nothing about tests.

The move cost the conformance programs one directory level, so the 33 imports that reach into the standard library by
path (`std/linear`, `std/geometry`, `std/path`) read `../../std/...` where they read `../../../std/...`. The package
is named `torbscript/conformance` after its directory, and a runtime path is **the package name plus the file below
its directory** - so nine expectation files carry that name in a panic site and were rewritten with it.

### 7.2 Where the words went

`bootstrap/README.md` said three things, and each one has a home:

- **The command list** is `compiler/CONTRIBUTING.md`, "Commands", on the native binary.
- **How a program ends** - a panic is two lines and exit code 101, a top-level `?` is `error:` and exit code 1 - is
  `tests/conformance/README.md` and the pages under `docs/language/errors/`. The third kind, a failure of the
  interpreter, has no counterpart: the type checker rejects every program that would have reached one.
- **What stage 0 could not do** was a table of approximations of rules the language decides by type. There is nothing
  left to approximate, so the rule is what the language says and the page that says so is the language reference.

**"Stage 0" still stands in the history sections** of `docs/BACKEND.md` (the "What 5.x decided" notes, the milestone
table's gate column and its measurements), in section 3 of this document, in `CONCEPT.md`'s record of decided gaps,
and in the milestone list of `docs/ARCHITECTURE.md`. Those describe how a milestone was gated at the time it was
gated, and rewriting them would make the record wrong. Everywhere a sentence says what *is*, the word is gone.

### 7.3 What still names stage 0 in the code

The 64 doc comments this section counted, in `compiler/src/`, `compiler/tests/`, `std/path`, `examples/`,
`tests/conformance/` and `runtime/test.c`, are rewritten to say what the code does today. `torb docs source
compiler/src` stays at 9 problems in 125 files and `torb docs source std --tree std` at 73 in 85, so the count did
not move. The hoists that trap 1 of stage 0 forced and that the language never required - `f(checker,
checker.something)` is legal TorbScript, and the `const` in front of it can go - are inlined at 26 sites; one is left,
a `checker.types.intern` behind a nested `var` receiver in `compiler/src/semantics/checker/expression.trb`, which is
messier than the pattern and was not worth the risk.

What still names stage 0: `std/collections/src/map.trb` and `set.trb`, and `std/stream/tests/bytes.test.trb` - a
rename round is running in those three files, so this round left them alone.

### 7.4 What a round runs

Tier A is `sh tools/gates.sh a` and tier B is `sh tools/gates.sh b`, both with `build/release/torb`, and section 6 is
the policy. `gates.sh` has no fallback: `canon --check` is the native binary's, with the five rules.
