# Working on the Compiler

The compiler is written in TorbScript and **compiles itself**: `sh tools/bootstrap.sh` builds `torb` from a seed and
then builds it again with itself. Stage 0 (`bootstrap/`), the **untyped** tree-walking interpreter in Rust, is what ran
it until it could, and it is on its way out ([docs/RUST-EXIT.md](../docs/RUST-EXIT.md)) - it still owns `canon` and
`highlight`, and its traps still apply to every line the compiler's own sources contain, because it still runs them.

These are the rules of the code base and those traps.

## Commands (from `bootstrap/`)

```text
cargo build --release                                   # Stage 0. Rebuild after every change of the Rust sources
cargo run --release -q -- test ../compiler/tests        # The TorbScript tests of the compiler (one process per file)
cargo run --release -q -- test ../compiler/tests --jobs 1   # ...one after another, when output order of a crash matters
cargo run --release -q -- run ../compiler check ..      # The compiler checks the whole repository: "no problems"
cargo run --release -q -- run ../compiler check tests/native tests/scripts   # ...and the two test workspaces of stage 0
cargo run --release -q -- run ../compiler check --statistics ..    # Every expression has a type: "0 deferred"
cargo run --release -q -- run ../compiler check --timings ..       # The wall time of every pass, in the order they ran
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release                                    # Everything, including the differential tests (minutes)
cargo test --release --test fixpoint -- --ignored --nocapture   # The fixpoint of 6.2 (a quarter of an hour, see below)
sh ../runtime/build.sh                                  # The C runtime and its tests (gcc or clang)
cargo run --release -q -- canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops ..
cargo run --release -q -- canon ../std ../compiler ../examples ../bootstrap/tests    # ...write it (a minute)
cargo run --release -q -- run ../compiler docs source ../std ../compiler ../examples   # The doc comments (not a gate yet)
cargo run --release -q -- run ../compiler docs check ../docs         # The documentation: schema, links, every snippet
cargo run --release -q -- run ../compiler docs index --check ../docs # Is the generated part of every index.md current?
cargo run --release -q -- run ../compiler docs index ../docs         # ...write it
```

A change is done when the gates of its tier are green (below) and the repository still checks with "no problems". A
false positive of the checker is a bug of the checker.

## The Gates, and Which of Them a Round Runs

**The goal is that `bootstrap/crates` can be deleted** ([docs/RUST-EXIT.md](../docs/RUST-EXIT.md)). Until that is done
the gates are the two tiers below rather than the whole list, because the list costs more than half an hour a round and
most of it re-measures the Rust implementation that is being removed. They tighten again once the exit is done.

**Tier A, every round.** The build, and then, from `bootstrap/`:

```text
cargo build --release                                            # only where Rust files changed
torb run ../compiler check ..                                    # "no problems"
torb run ../compiler check --statistics ..                       # "0 deferred"
torb test ../compiler/tests                                      # the compiler's own tests
torb test ../std/<package>/tests                                 # every std package a change touched
torb canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops ..
torb run ../compiler docs check ../docs                          # where docs changed
torb run ../compiler docs index --check ../docs                  # where docs changed
```

**Tier B, only a round that touches the IR, a back end or `runtime/`, and then exactly once** - by the agent that wrote
the change, not again on master:

```text
cargo test --release --test native -- --nocapture                # the conformance suite
cargo test --release --test suite -- --ignored --nocapture       # the compiler's tests from the binary
cargo test --release --test fixpoint -- --ignored --nocapture    # the three-stage fixpoint
```

On master tier B runs at most once per batch of merges, in the background, and a red result is **fixed forward** rather
than reverted: the batch is already in and the failing piece is named and repaired in the next round.

**Stage 0 is frozen.** It receives a language feature only where the compiler's own sources or tests need it to build,
and a native only where the compiler needs one. Parity of *messages* between the two checkers is no longer a goal: the
self-hosted checker's message is the language's, and stage 0's is whatever it was. `cargo fmt --check` and
`cargo clippy` are run where Rust files changed and not otherwise.

**Numbers are recorded, not enforced**, until the exit is done: allocation counts, the benchmark ratios of
[docs/PERFORMANCE.md](../docs/PERFORMANCE.md) and the size of the emitted C go into the round's report so that a
regression is visible, and none of them fails a gate by itself.

**Run tier A with the native binary.** `check ..` costs 8 s there against 59 s on stage 0, and the compiler's tests are
one binary instead of 55 processes. From the repository root, with a `torb` built by `sh tools/bootstrap.sh` (or the
seed itself, which is enough for everything but a change to the compiler):

```text
sh tools/bootstrap.sh                                 # seed -> torb -> torb, and the C of both compared
./build/release/torb check .                          # "no problems"
./build/release/torb check --statistics .             # "0 deferred"
./build/release/torb test compiler/tests
./build/release/torb test std/<package>/tests
./build/release/torb docs check docs
./build/release/torb docs index --check docs
```

`canon` has no native form yet, so it stays stage 0's until slice 3 of the exit.

**The fixpoint is not in the list above, and it is what says the compiler is correct about itself.** It builds the
compiler with stage 1, builds it again with the binary that came out, compares the two `program.c` byte for byte and lets
stage 3 emit a third one - roughly a quarter of an hour, so it is `#[ignore]`d and run when a change could move the
emitted C (the emitter, the lowering, the ownership pass, `std/`). A difference is reported as the first differing byte
with the text around it in both files. `docs/BACKEND.md` ("What 6.2 decided") says what it has caught.

**A cost that stage 0 hides is a cost only a compiled program pays.** Everything `natives.rs` answers with Rust while
`std/` carries a TorbScript body for the same declaration is invisible to every test that runs on stage 0 - that is how a
quadratic `joined` sat in `std/iteration` until the binary tried to join the 786457 lines of its own `program.c`. When a
`std/` body loops over something that grows, measure it with a **compiled** probe and never on stage 0.

`docs/` is the user-facing documentation of the language and has its own rules and its own gate
([docs/contributing](../docs/contributing/index.md)): every page carries front matter, the body of every `index.md` is
generated from its children, and every `trb` code block is verified by this front end. A change that changes what the
language *means* changes the page that says so - `docs check` names the page whose `source` points at what you touched.

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
- Cases: `.Case` in patterns, `Type.Case` in expressions unless stage 0 can see the expected type (the table in
  `bootstrap/README.md`). An **imported** case needs nothing in front of it, in an expression and in a pattern
  (`Some(found) =>`, `None =>`); a pattern name that starts with a lowercase letter binds, an uppercase one never does.
  Positional arguments come before named ones.
- Prefer the short form where the type is expected and stage 0 can see it: `addNode(graph, .SwitchCase(path))` for a
  parameter of a declared function, `const kind: TokenKind = .Dot`, `kinds == [.Dot, .Name]`. Write `Type.Case` only
  where stage 0 cannot (trap 8).
- **A literal that sets an option is labeled.** `true`, `false` and `None` have no name of their own, so where one is
  passed to a parameter that is *declared* as `Bool` or as an optional, the label is the only thing that says what it
  means: `listEntries entries, "ArrayList", hasCapacity: false`, never `listEntries entries, "ArrayList", false`. Two
  cases need none: a call with a single argument (`setEnabled(true)`, `assert(false)` - the function's name says it),
  and a literal that is the *data* and not an option, which is the case exactly when the parameter's declared type is a
  type parameter (`flags.set key, true`, `Some(true)`, `list.add(None)`). Labeled arguments follow the positional ones,
  so options are declared last. The same goes for a number literal whose meaning the call does not show
  (`connect("localhost", timeout: 10)`); that half is judgement, the `Bool`/`None` half will be a lint with a fix.
- **A capsule names its field for the storage and its method for the answer.** A type whose constructor is closed from
  outside - a `private` field without a default - reads through accessors, and a field and a method never share a name,
  so the field takes `stored` in front of the accessor's name: `private storedComponents: List<String>` next to
  `fn components(): List<String>`, `private storedRoot` next to `fn root()`. The prefix is on the field and never on
  the method, because the method is what every caller writes. Where the two say different things the accessor keeps
  its own word instead (`private storedDegrees` answered by `degrees()`, but `private kind` answered by
  `isSyntaxError()`). The four parts of a capsule and the conversion pair its `Encode`/`Decode` come from are
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
  start" became the type instead of an `expect` in `iterator()`. A panic a body **writes** is a promise: its docblock
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
3. **Links are names**: `[Iterator]`, `[List.add]`, `[Option.Some]`. They resolve like a name at that place in the code -
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
running; `torb run ../compiler docs source ../std ../compiler ../examples --statistics` is what measures how far they
have come. It becomes a mandatory gate when they are done, and from then on it is run like every other gate.

## Traps of Stage 0

Stage 0 has no type checker: a mistake is found when the line runs, so **every function must be run by a test**. What it
prints when that happens is in [bootstrap/README.md](../bootstrap/README.md) ("How a Program Ends"): a failure of the
interpreter carries the calls it came through, and a **panic** prints two lines and no more unless `TORB_FRAMES=1` asks
for them - which is what to set when a panic inside the toolchain has to be found.

1. **A `var` argument is taken before the other arguments are evaluated.** `f(checker, checker.something)`,
   `f(checker, g(checker, x))` and `checker.method(g(checker, x))` read a place that was moved out, and fail far away
   ("Void has no method ...", "a value of type Function has no method ..."). Hoist the inner read or call into a
   `const` first. `grep -nE "\(checker, [^)]*checker\."` finds most of them. **This is a limitation of stage 0, not a
   rule of the language:** exclusivity begins the `var` access of a call after all of its arguments have been evaluated
   (CONCEPT, `var` Paths), so all three lines above are legal TorbScript. The hoists stay until stage 0 is gone.
2. The same one level down: `checker.list.add(Item(checker.other.length()))` has to be two statements, and a
   `var fn` method must not hand a field of `self` to another `var fn` method.
3. **Copy on write is O(n).** Never hold a second live copy of a big table across a write. Big tables live in one
   `var` owner and are passed as `var` parameters. Interning is a `Map` lookup, never a scan.
4. Directly inside of the braces of a `match`, a line that starts with `.` is a new arm. A chain over several lines
   as the value of an arm goes into a block.
5. A function that returns `Value?` writes `Some(value)` explicitly when the value comes out of a binding.
6. A brace inside of a string of a test source needs `\{`: the test file interpolates first.
7. `harness.trb`'s `spanOf` finds the first occurrence of a text. Put the use in front of the declaration.
8. `.Case` in an expression only works where stage 0 sees the type (annotation, parameter, field, result,
   assignment, `==`). Elsewhere write `Type.Case`.
9. **A failing `assert` evaluates its expression a second time** to show the values in it. A call inside one that takes a
   `var` argument therefore *runs twice*, and a pass that is written to run once is then reported as if it had run twice.
   Hoist it into a `const` first, or let the question take its argument by value.

**Natives stay few.** Every `native` declaration has to be rebuilt by every back end (C today, the VM, later
JavaScript and PHP). Do not add a `native` to `std/` or a function to `runtime/` for something that can be written in
TorbScript on top of the natives that exist; a new one needs a reason (the operating system, raw storage, a number
operation) in its doc comment. After the fixpoint the runtime shrinks to a small kernel of intrinsics, and everything
else becomes TorbScript with an optional native fast path.

If the compiler legitimately needs a native that stage 0 lacks, add it minimally in
`bootstrap/crates/torb-interpreter/src/natives.rs` **and** declare it in `std/`. Stage 0 gets no language features
and no type checker.
