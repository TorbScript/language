# Working on the Compiler

The compiler is written in TorbScript and **compiles itself**: `sh tools/bootstrap.sh` builds `torb` from a seed and
then builds it again with itself. A C compiler is the one external tool a checkout needs; everything above it - the
front end, the checker, the lowering, the emitter, the driver, `canon` (`compiler/src/canon/`) and `highlight`
(`compiler/src/highlight/`) - is TorbScript compiled by TorbScript.

These are the rules of the code base and its traps.

## Commands

`torb` below is `build/release/torb`, and every command is run from the repository root.

```text
sh tools/bootstrap.sh                                   # Build it: seed -> torb -> torb, and compare the two program.c
sh tools/gates.sh a                                     # Tier A (below); tier B is `sh tools/gates.sh b`
torb test --native compiler/tests                       # The TorbScript tests of the compiler (profile dev)
torb test --native compiler/tests/calls.test.trb        # ...one file
torb test std/path/tests                                # A test package, in the VM (the default of torb test)
torb check .                                            # The compiler checks the whole repository: "no problems"
torb check tests/conformance tests/language             # ...and the two test workspaces, which stand outside it
torb check --every-target std/os                        # Lowered once per target: no native reached on a wrong system
torb manifest --check .                                 # Every project.trb evaluated in the VM agrees with the static reader
torb check scratch.trb                                  # One file, wherever it is: it gets the toolchain's std/
torb check --statistics .                               # Every expression has a type: "0 deferred"
torb check --timings .                                  # The wall time of every pass, in the order they ran
torb run scratch.trb                                    # Run it in the VM, which needs no C compiler
torb run --native scratch.trb                           # Build it into build/run/dev/ and run it (profile dev)
torb build --profile dev path/to/main.trb               # A binary with -O1 instead of -O2 (default: release)
sh runtime/build.sh                                     # The C runtime and its tests ($TORB_CC, clang, gcc, cc)
torb canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops .
torb canon std compiler examples tests                  # ...write it (a minute)
torb docs source std compiler examples                  # The doc comments (not a gate yet)
torb docs source --panics std                           # ...except its `# Panics` rule, which is one
torb docs check docs                                    # The documentation: schema, links, every snippet
torb docs index --check docs                            # Is the generated part of every index.md current?
torb docs index docs                                    # ...write it
torb docs skill docs .claude/skills/torbscript --check  # Is the committed skill what docs/ generates?
torb docs skill docs .claude/skills/torbscript          # ...write it, and commit it with the docs change
sh tools/refresh-seed.sh                                # build/release -> seed/, the old seed archived (below)
```

**Profiles.** `dev` compiles the one C file with `-O1`, `release` with `-O2`; the C is the same under both, so the
fixpoint does not depend on the profile. `torb test` and `torb run` run in the VM; with `--native` they build `dev`
unless told otherwise (`--profile release`, or `--release`, which build natively too), `torb build` builds `release`,
and so do `tools/bootstrap.sh` and the seed. The compiler's own suite stays native: each of its tests checks and lowers
whole programs, which the VM interprets many times slower than the one C compile of the suite takes. Measured on the
compiler's own test suite (86 MB of C, gcc 13, a machine with other builds running): `-O2` is 134 s of gcc and 65 s of
tests, `-O1` 92 s and 77 s, `-O0` 70 s and 171 s - `-O1` is the fastest `torb test compiler/tests` from end to end
(197 s against 225 s for `--release`). On the compiler's own C (65 MB) gcc takes 101 s at `-O2`, 68 s at `-O1` and 51 s
at `-O0`, with a peak of 1.6-1.7 GB at every level, and the compiler that comes out checks the repository in 9.3 s,
10.6 s and 25 s.

A change is done when the gates of its tier are green (below) and the repository still checks with "no problems". A
false positive of the checker is a bug of the checker.

## The Gates, and Which of Them a Round Runs

**`tools/gates.sh` runs both tiers with the native binary**, from the repository root:

```text
sh tools/gates.sh a    # every round
sh tools/gates.sh b    # a round that touches the IR, a back end or runtime/ - once, not again on main
```

One line per gate, with its time; the first red gate stops the run there and shows its output, so nothing after it
runs.

**Tier A is `sh tools/gates.sh a`**: it bootstraps `build/release/torb` with `sh tools/bootstrap.sh` if that is missing
or older than a file it is built from (`compiler/src`, `std/`, the runtime's `.c`/`.h`, the manifests - never anything
under a `build/` directory), then `check .` ("no problems"), `check --statistics .` ("0 deferred"),
`check tests/conformance tests/language`, `check --every-target std/os` (every program and test of the package lowered
once per target, docs/design/OS.md section 2), `manifest --check` over every `project.trb` of the repository (evaluated in
the sandboxed VM, it has to read as the static reader reads the file, docs/design/SCRIPTS.md section 7),
`test --native compiler/tests` (which pins the recovery of the lexer and the parser
over `tests/lexer-cases/` and `tests/parser-cases/` as well), `test` of every std/example test package in both back
ends (all of them build natively; one that waits for a back-end gap is named in
[docs/RUST-EXIT.md](../docs/RUST-EXIT.md) section 2.4 and in the `broken` list of `gates.sh`, which skips it), every
program of `tests/language/` run with `torb run --native` and with `torb run` against its `.expected`, the three docs gates
(`docs check`, `docs index --check`, `docs skill --check`), and `canon --check` with the five rules. Everything it
builds only to run it once is built with the `dev` profile.

**Tier B is `sh tools/gates.sh b`**: `tools/conformance.sh` (the conformance suite - every program under
`tests/conformance/`, and `binary-only/`, built and run natively and compared against its
`.expected`/`.stderr`/`.exit`/`.leaks`, each run in a work directory of its own; `--jobs`, `--filter` and `--update`
are its own flags), `tools/conformance.sh --vm` (every program of the suite and of `tests/conformance/vm-only/` run by the bytecode VM
with `torb run --vm` and compared with the same expectation files, docs/design/VM.md section 8; a program with a
`.workers` file runs on that many workers of the pool and again on one, as natively), `torb test --vm` of the test
packages of `std/` and `examples/` (the report of tier A's native run, from the VM), `tools/bootstrap.sh`
(the fixpoint: seed -> `torb` -> `torb`, byte-identical C), and `sh runtime/build.sh` (the C runtime's own tests, built
into `build/runtime/`).

On main tier B runs at most once per batch of merges, in the background, and a red result is **fixed forward** rather
than reverted: the batch is already in and the failing piece is named and repaired in the next round.

**Numbers are recorded, not enforced**: allocation counts, the benchmark ratios of
[docs/PERFORMANCE.md](../docs/PERFORMANCE.md) and the size of the emitted C go into the round's report so that a
regression is visible, and none of them fails a gate by itself.

**The fixpoint is what says the compiler is correct about itself.** `tools/bootstrap.sh` builds the compiler with the
seed, builds it again with the binary that came out, and compares the two `program.c` byte for byte. A difference is
reported as the first differing byte with the text around it in both files. `docs/BACKEND.md` ("What 6.2 decided")
says what it has caught.

**Measure a `std/` body with a compiled probe.** A body that loops over something that grows costs what the binary
pays for it, and a quadratic `joined` sat in `std/iteration` until the binary tried to join the 786457 lines of its own
`program.c`. The probe is a program that is built and run, never a snippet read by eye.

`docs/` is the user-facing documentation of the language and has its own rules and its own gate
([docs/contributing](../docs/contributing/index.md)): every page carries front matter, the body of every `index.md` is
generated from its children, and every `trb` code block is verified by this front end. A change that changes what the
language *means* changes the page that says so - `docs check` names the page whose `source` points at what you touched.

## Repository Operations

**Commit messages** follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):
`<type>(<scope>): <summary>`, at most 100 characters, the summary in the imperative and lower case, a body after a
blank line when the change needs one. The types are `feat`, `fix`, `perf`, `refactor`, `docs`, `test`,
`build`, `ci`, `chore` and `revert`; `!` after the scope, or a `BREAKING CHANGE:` line in the body, marks a
change that breaks a program. The scope is the part the change is about: `checker`, `parser`, `lower`, `backend`,
`vm`, `repl`, `runtime`, `std/<package>`, `docs`, `tools`, `ci` - or none when it spans many.

**The history is linear.** `main` has no merge commits: a branch lands rebased onto `main` (each of its commits a
Conventional Commit that builds) or squashed into one commit, and `main` only moves forward. The two commits of a
change that needs a seed refresh stay two commits. `sh tools/check-commits.sh <base>` checks a range - no merge
commit, every subject a Conventional Commit - and CI runs it on every push and pull request.

**The seed.** `seed/` holds `torb(.exe)` and `program.c`, is not in git, and exists only in the main checkout of this
machine; a worktree has none and bootstraps with `TORB_SEED=<main checkout>/seed/torb.exe sh tools/bootstrap.sh` (a
relative `TORB_SEED` works too). After a merge whose tier A is green, `sh tools/refresh-seed.sh` in the main checkout
copies `build/release/torb(.exe)` and `build/release/program.c` into `seed/` - only after the new binary has checked a
one-line program - and records the commit in `seed/commit`. The seed it replaces goes to
`../torbscript-seeds/<commit>-<date>/` beside the checkout, the five newest are kept, and a broken seed is rolled back
by copying one of them into `seed/`. Never delete `seed/`, and never refresh it from a red tree.

**The published seed.** A checkout without `seed/` - a fresh clone, a CI runner - bootstraps from a *published* seed:
`torb-seed-<commit>.tar.gz`, the compiler's `program.c` and the runtime of the same commit plus `build.sh`, an asset of
the GitHub release `seeds`, whose `seeds.txt` lists every seed with its SHA-256, the newest first.
`sh tools/fetch-seed.sh` downloads the newest (or `sh tools/fetch-seed.sh <commit>` a named one) with curl or wget,
checks the hash, compiles `program.c` with the local C compiler (`tools/build-seed.sh`, `$TORB_CC`) into
`build/seed/torb`, and keeps it only when it checks a one-line program. `tools/bootstrap.sh` takes its seed from
`$TORB_SEED`, `seed/`, the archives and `build/seed/`, in that order, and runs `tools/fetch-seed.sh` itself when there
is none (`TORB_SEED_FETCH=0` forbids it). A private repository needs `GH_TOKEN` for the download. The nightly and every
release publish the seed of main once linux-x64 passes tier A and tier B; the `seed` workflow (Actions -> seed -> Run
workflow) publishes one at once, which is the second half of the rule below.

**Two commits for a breaking change.** The checkout builds from the seed, so the seed has to understand the sources. A
syntax change, a new native, or renaming a std name the compiler looks up by string
(`semantics/checker/wellknown.trb`, the operator traits in `checker/expression.trb`, the member lookups in
`ir/lower/collection.trb`) is two commits: the first teaches both forms (or adds the native without using it), the
seed is refreshed from it, and the second migrates (docs/design/COLLECTIONS.md 6a, docs/RUST-EXIT.md 4.2). A new flag of the
driver is the same: `tools/bootstrap.sh` passes the seed nothing it may not know yet. CI bootstraps from the published
seed, so between the two commits the `seed` workflow publishes the seed of the first one, and the second is pushed only
after it.

**Build slots.** A C file of 8 MB or more - the compiler, a test suite of it - is compiled through
`tools/build-slot.sh`, which `torb build` finds beside the runtime: at most `$TORB_BUILD_SLOTS` (default 3) such
compiles run at the same time on the machine, whichever checkout they come from, each one a directory
`torb-build-slots/slot-<n>` under `$TMPDIR`/`$TEMP` taken with `mkdir`. A slot whose holder died is taken over (a dead
process id, or older than an hour). `torb` prints one line while it waits. `cc1: out of memory` should not happen any
more; if it does, lower `TORB_BUILD_SLOTS` and retry alone.

**Gate slots.** `tools/gates.sh` and `tools/bootstrap.sh` run themselves through `tools/gate-slot.sh`: at most
`$TORB_GATE_SLOTS` (default 2) gate runs or bootstraps at the same time on the machine, each one a directory
`torb-gate-slots/slot-<n>` beside the build slots, held from the first gate to the last and taken over when its holder
died (or after six hours). A run inside a slot takes no second one (`TORB_GATE_SLOT_HELD=1`), so the bootstrap of a
gate run never waits for itself. Six gate runs at once once exhausted the machine's processes (`fork: Resource
temporarily unavailable`); a run that waits says so in one line. The conformance runner keeps to two programs at a
time and waits while the machine has less than 2 GiB of memory free.

**The memory limit.** Every binary of the `dev` profile - the compiler's test suite, the std packages' tests, `torb
run --native` - and every program the VM runs stops at the smaller of 8 GiB and half the physical memory, with `panic: out of memory: the limit of ... was
reached` and exit code 102; the operating system enforces it (a job object on Windows, `RLIMIT_DATA` on Linux,
`runtime/README.md`). A test binary that ran away once committed 88 GB and took the machine down. `TORB_MEMORY_LIMIT`
sets another limit for every TorbScript process that sees it (`16G`, `512M`, `0` or `none` for none); in a binary
that hosts the VM (`torb`, the compiler's tests) it limits the programs the VM interprets, counted by the kernel, and
the host keeps its own profile's default. The compiler is a release binary and has no limit of its own. Measured on 2026-09-24 (Windows, peak commit): `torb build ./compiler` about 0.7 GB, building the test
suite 0.75 GB, the compiler's test suite itself 0.36 GB - the default leaves ten times that. A C compiler `torb` starts
is never held to a limit (`cc1` over the compiler's C needs several GB).

**Parallel work in one checkout.** `tools/bootstrap.sh` builds in `build/staging-<pid>/` and moves the binary into
`build/release/` only when the fixpoint holds, renaming a running `torb.exe` out of the way instead of overwriting
it; a red bootstrap leaves `build/release/` as it was and keeps its staging directory. The conformance programs run in
work directories under the system's temporary directory. `torb test <path>` still writes `<path>/build/dev/`, so two
runs of the same suite in one checkout share a binary: give each agent its own worktree.

**CRLF files.** Some files are CRLF (four checker files among them). Keep each file's line endings; a bulk edit goes
through a script that asserts an exact match for every replacement and writes the line endings back, never through
PowerShell arrays or a heredoc.

**Scratch programs.** A file named on the command line is checked wherever it lies: at the repository root, in a
temporary directory, in a project that is nobody's member. Its `std/` is the workspace's when the workspace has one,
and otherwise the toolchain's, found above the named path, the working directory and `torb` itself (`$TORB_STD`
overrides); the runtime `torb run` links is found the same way (`$TORB_RUNTIME` overrides). `torb check <path>` that
reaches no file at all is an error, never "0 files, no problems".

## Style

- Idiomatic TorbScript, in the style of the code that is there. Full words, no abbreviations (`declaration`, not
  `decl`). No semicolons, never several statements on one line.
- **A name is ASCII** - `[A-Za-z_][A-Za-z0-9_]*`, and the lexer says so. Strings, characters, comments and
  documentation stay full Unicode.
- **How a name is spelled is a rule, not a convention**, and the checker reports it at the declaration. A `type`, a
  `shared type`, a `trait`, a `case`, a type parameter (a size parameter too), a type alias and an `as` alias of one of
  those start with `A`-`Z`. Everything else starts with `_` or `a`-`z`: `fn`, fields, parameters, tuple labels,
  `const`/`var`, a module constant (there is no `MAX_SIZE` spelling - write `maxSize`), a module alias.
- **A binding of a refutable pattern has to be read.** In an arm of a `match`, in an `if const`/`if var` and in a
  `while const`, a name the guard or the body never reads is an error: write `_`, or `_name` to keep the name as
  documentation. `torb canon --rule unused-bindings` does the mechanical half of a sweep.
- **The formatter canon** (CONCEPT, "Formatter Canon"): a call is a command wherever the grammar allows it -
  `Ok value`, `return Fail problem`, `const role = Role name`, `names.map Role` - and has parentheses everywhere
  else: nested (`Ok Some(x)`), without arguments (`list.length()`), with an operator at the top level of an argument
  (`assert(sum == 3)`), over several lines, and in the head of an `if`, `for`, `while` or `match`. A multi-line `"""`
  is indented two spaces deeper than the line it starts on, closing quotes aligned with the content. `torb canon`
  (above) writes both, over the syntax tree; milestone 8's `torb format` takes over from it.
- Doc comments follow **the documentation standard** below. Block comments do not nest: never write a slash-star or a
  star-slash inside of a comment (not even in a glob).
- Small values plus free functions that take a `var` parameter (`var parser: Parser`, `var checker: Checker`,
  `var program: IrProgram`). Values have no identity, so program-wide data lives in lists and is addressed by
  integer ids (`ModuleId`, `SymbolId`, `TypeId`, ...).
- **A `Bool` is an adjective, a question is a method.** A `Bool` field, parameter or binding is an adjective or a
  participle (`inclusive`, `discarded`, `exported`); a question that is computed is a method with `is`/`has`
  (`isEmpty()`, `hasGuard()`). The public standard library follows it (`Range.inclusive`). **The compiler's own
  `is...` fields stay as they are for now:** about 60 of them at 135 places are named after keywords (`isVar`,
  `isStatic`, `isPublic`, `isNative`, `isShared`, `isConst` - `var: Bool` is not a name), so each one needs its own
  decision (another word, or better a type instead of a flag: `Visibility` exists). That runs after the fixpoint,
  with the method conversion and a checked rename instead of a text replacement.
- Cases: `.Case` in patterns, `Type.Case` in an expression where no expected type says which type is meant. An
  **imported** case needs nothing in front of it, in an expression and in a pattern (`Some(found) =>`, `None =>`); a
  pattern name that starts with a lowercase letter binds, an uppercase one never does. Positional arguments come before
  named ones.
- Prefer the short form where the expected type says which type is meant: `addNode(graph, .SwitchCase(path))` for a
  parameter of a declared function, `const kind: TokenKind = .Dot`, `kinds == [.Dot, .Name]`.
- **A literal that sets an option is labeled.** `true`, `false` and `None` have no name of their own, so where one is
  passed to a parameter that is *declared* as `Bool` or as an optional, the label is the only thing that says what it
  means: `listEntries entries, "ArrayList", hasCapacity: false`, never `listEntries entries, "ArrayList", false`. Two
  cases need none: a call with a single argument (`setEnabled(true)`, `assert(false)` - the function's name says it),
  and a literal that is the *data* and not an option, which is the case exactly when the parameter's declared type is a
  type parameter (`flags.set key, true`, `Some(true)`, `list.append(None)`). Labeled arguments follow the positional ones,
  so options are declared last. The same goes for a number literal whose meaning the call does not show
  (`connect("localhost", timeout: 10)`); that half is judgement, the `Bool`/`None` half will be a lint with a fix.
- **A capsule's field is named for its storage, never for the accessor's word, because a field and a method never
  share a name.** A type whose constructor is closed from outside - a `private` field without a default - reads
  through accessors. One stored field is named `value`, its unit or meaning said in the field's doc comment
  (`/** In nanoseconds. */ private value: Int64` behind `fn nanoseconds(): Int64`). Several stored fields are named
  for the method each answers, plus `Value`, or `Values` for a plural: `private rootValue` next to `fn root()`,
  `private componentValues: List<String>` next to `fn components(): List<String>`, `private lengthValue` next to
  `fn length()`. Where a field's meaning is not what the accessor answers, the accessor keeps its own word instead
  (a token's `private kind` answered by `isSyntaxError()`). The four parts of a capsule and the conversion pair its
  `Encode`/`Decode` come from are
  [docs/language/types/data-or-capsule](../docs/language/types/data-or-capsule.md).
- A case name must not shadow a prelude type (`TupleType`, `Floating`, `VoidType`, not `Tuple`, `Float`, `Void`).
- `type`, `trait`, `where`, `shared` are keywords and cannot be names (`annotation`, `capability`, ...).
- **A `public` function and a trait method never infer their result.** Without a result type they produce `Void`, which
  is why `public fn emit(var builder: Builder) { ... }` needs no annotation; a body that ends in a value or returns one
  has to declare which type that is. Everything that is not `public` infers as usual. A `fn` inside of a block is not a
  closure: it sees its parameters and the file, not the bindings around it.
- **What panics in `std/`.** A panic there is only for a caller's mistake that **no type can express**: an index out of
  range, an integer overflow, a division by zero. Where a type *can* say it, the type says it - an `Option`, a
  `Result`, a narrower parameter type, or a split type as with `Range`/`RangeFrom`/`RangeTo`, where "this range has a
  start" became the type instead of an `expect` in `iterate()`. A panic a body **writes** is a promise: its docblock
  carries a `# Panics` section that names the condition, and the conformance suite pins the message word for word,
  because the two back ends may not disagree about it. A member that only passes one on - an overflow of the `Int`
  arithmetic under it, an index it hands to a container - says so in its sentence instead. `Option.expect` and
  `Result.expect` are the escape hatch a *caller* reaches for, and `a[key]` is the operator whose contract is the
  panic: `get(key)` is the typed answer beside it.
- Diagnostics are data (`Diagnostic(message, span, notes)`), in the tone of the ones that exist: one root cause, one
  message, the error type absorbs what follows.
- Tests are in-memory programs (`compiler/tests/harness.trb`). Every diagnostic has a test with its exact message,
  and what a pass records in its tables is asserted at a span.
- Write source files with an editor or a script that checks for exact matches. Never generate source through shell
  heredocs or PowerShell arrays; quotes and backslashes do not survive.

## The Documentation Standard

The documentation of this repository lives **at the code**, in the doc comments, for a human and for an agent at the
same time: what a construct does now and what it is for, the smallest example that shows the use case, the constructs
that belong next to it, and the pitfalls and the open problems where they bite. `torb docs source <path>...` is the gate
that asks for all of it ([docs/tooling/torb-docs-source](../docs/tooling/torb-docs-source.md)).

1. **The first sentence says what it does. The sentences after it say what it is for** - when a reader reaches for this
   construct instead of another. Present tense, about the code as it is.
2. **Six headings, and no others**: `# Examples` (the code indented by four spaces below it - it is parsed, canonized
   and type checked), `# Errors` (the `Fail` cases), `# Panics`, `# Pitfalls`, `# Open` (a problem that is open, in the
   present tense), `# Related` (links). Every heading that is written has something under it.
3. **Links are names**: `[Iterator]`, `[List.append]`, `[Option.Some]`. They resolve like a name at that place in the code -
   what the file declares, what it imports, the prelude - and `Type.member` through the type.
4. **Every file starts with a module comment**: a doc comment at the top, in front of the first `use`. What the module
   is for, its main constructs, how they relate. (The parser attaches it to that import, which is what makes it belong
   to the file and to no declaration.)
5. **What needs a comment.** In `std/`: every file, every `public` declaration, every `extend`, and every member, field
   and case of a public type that is not `private` (the members of an `extend ... with Trait` are documented at the
   trait). In `compiler/src`: every file and every `public` declaration. In `examples/`: every file and every top-level
   declaration. A test file (`*.test.trb`, everything under `tests/`): the file comment, and nothing else.
6. **No history.** A comment says what the code does now - never what it used to do, what changed, or which milestone,
   round or gap a change belonged to. The gate flags those words in doc comments and in `//` comments alike; what stands
   between backticks is quoted and not prose, so a comment about this rule may name them.
7. **The canon holds in an example**: no semicolons, never a squeezed one-liner, a call is a command wherever the
   grammar allows it.
8. **A `//` comment inside a body is the rare exception**, for a step that is not obvious from the code - a pitfall at
   the line it bites, an invariant the next lines rely on, why an order matters. Everything a caller or a reader of the
   construct needs is in the doc comment, never only in a `//` comment.

`std/core/src/option.trb` and `std/core/src/result.trb` are the reference: read one of them before writing
documentation anywhere else. One comment in full:

```trb
/**
 * The value, or the fallback if there is none. This is the `??` operator, and the way out of the type.
 *
 * The fallback is `lazy`, so it is only evaluated when it is needed: a fallback that reads a file or counts costs
 * nothing while the value is there.
 *
 * # Examples
 *
 *     const missing: Int? = None
 *     print missing.orElse(0)
 *
 * # Related
 *
 * - [Option.okOr] - the same step with a reason, for a caller that answers with a `Result`.
 */
fn orElse(fallback: lazy Value): Value { ... }
```

An example is checked as if it stood in a file next to the one it documents: it sees what that file imports and the
file's own public declarations, and it is a script, so it may `print`. An example that cannot stand alone says so in its
first line - `// fragment` for a signature or a shape that only has to lex, `// skip <reason>` for one that is checked
by nothing and counted in the report.

**`torb docs source` is not in the list above yet.** The repository does not pass it while the writing waves are
running; `torb docs source std compiler examples --statistics` is what measures how far they have come. It becomes a
mandatory gate when they are done, and from then on it is run like every other gate. One rule of it already is, in
tier A: `torb docs source --panics std`, a public function of `std/` that can panic says when
([docs/tooling/torb-docs-source.md](../docs/tooling/torb-docs-source.md)).

## Traps of the Code Base

A **panic** prints `panic: <message>` and the site it happened at, two lines and no more, and leaves with 101. A
panic inside the toolchain is found by narrowing what is compiled, not by a stack trace - or by building the compiler
with `--profile dev`, whose binary prints the frames of the panic below the site (`runtime/include/torb.h`).

1. **Copy on write is O(n).** Never hold a second live copy of a big table across a write. Big tables live in one
   `var` owner and are passed as `var` parameters. Interning is a `Map` lookup, never a scan.
2. Directly inside of the braces of a `match`, a line that starts with `.` is a new arm. A chain over several lines
   as the value of an arm goes into a block.
3. A function that returns `Value?` writes `Some(value)` explicitly when the value comes out of a binding.
4. A brace inside of a string of a test source needs `\{`: the test file interpolates first.
5. `harness.trb`'s `spanOf` finds the first occurrence of a text. Put the use in front of the declaration.

**Natives stay few.** Every `native` declaration has to be rebuilt by every back end (C today, the VM, later
JavaScript and PHP). Do not add a `native` to `std/` or a function to `runtime/` for something that can be written in
TorbScript on top of the natives that exist; a new one needs a reason (the operating system, raw storage, a number
operation) in its doc comment. After the fixpoint the runtime shrinks to a small kernel of intrinsics, and everything
else becomes TorbScript with an optional native fast path.

A native the compiler legitimately needs is declared in `std/`, entered in the manifest
(`compiler/src/backend/c/natives.trb`) and implemented in `runtime/`. A `native type` meets the requirements of the
traits it implements with a body or with a row of the manifest (`Int64.add`), and the checker holds every requirement
against both: a missing row is a message at the type, never a link error. A new native is compiled into the binary that
emits it, so the seed a checkout builds from has to be one that already knows it (docs/RUST-EXIT.md section 4.2).
