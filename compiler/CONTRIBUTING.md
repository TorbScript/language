# Working on the Compiler

The compiler is written in TorbScript and, until it compiles itself, runs on stage 0 (`bootstrap/`), an **untyped**
tree-walking interpreter. These are the rules of the code base and the traps of stage 0. They apply to every change.

## Commands (from `bootstrap/`)

```text
cargo build --release                                   # Stage 0. Rebuild after every change of the Rust sources
cargo run --release -q -- test ../compiler/tests        # The TorbScript tests of the compiler (one process per file)
cargo run --release -q -- test ../compiler/tests --jobs 1   # ...one after another, when output order of a crash matters
cargo run --release -q -- run ../compiler check ..      # The compiler checks the whole repository: "no problems"
cargo run --release -q -- run ../compiler check --statistics ..    # Every expression has a type: "0 deferred"
cargo run --release -q -- run ../compiler check --timings ..       # The wall time of every pass, in the order they ran
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release                                    # Everything, including the differential tests (minutes)
sh ../runtime/build.sh                                  # The C runtime and its tests (gcc or clang)
cargo run --release -q -- canon --check ..              # Is the whole repository in the formatter canon?
cargo run --release -q -- canon ../std ../compiler ../examples ../bootstrap/tests    # ...write it (a minute)
```

A change is done when all of them are green and the repository still checks with "no problems". A false positive of
the checker is a bug of the checker.

## Style

- Idiomatic TorbScript, in the style of the code that is there. Full words, no abbreviations (`declaration`, not
  `decl`). No semicolons, never several statements on one line.
- **The formatter canon** (CONCEPT, "Formatter Canon"): a call is a command wherever the grammar allows it -
  `Ok value`, `return Fail problem`, `const role = Role name`, `names.map Role` - and has parentheses everywhere
  else: nested (`Ok Some(x)`), without arguments (`list.length()`), with an operator at the top level of an argument
  (`assert(sum == 3)`), over several lines, and in the head of an `if`, `for`, `while` or `match`. A multi-line `"""`
  is indented two spaces deeper than the line it starts on, closing quotes aligned with the content. `torb canon`
  (above) writes both, over the syntax tree; milestone 8's `torb format` takes over from it.
- `/** */` doc comments on public declarations that say **why**, not what. Block comments do not nest: never write a
  slash-star or a star-slash inside of a comment (not even in a glob).
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
- A case name must not shadow a prelude type (`TupleType`, `Floating`, `VoidType`, not `Tuple`, `Float`, `Void`).
- `type`, `trait`, `where`, `shared` are keywords and cannot be names (`annotation`, `capability`, ...).
- **A `public` function and a trait method never infer their result.** Without a result type they produce `Void`, which
  is why `public fn emit(var builder: Builder) { ... }` needs no annotation; a body that ends in a value or returns one
  has to declare which type that is. Everything that is not `public` infers as usual. A `fn` inside of a block is not a
  closure: it sees its parameters and the file, not the bindings around it.
- Diagnostics are data (`Diagnostic(message, span, notes)`), in the tone of the ones that exist: one root cause, one
  message, the error type absorbs what follows.
- Tests are in-memory programs (`compiler/tests/harness.trb`). Every diagnostic has a test with its exact message,
  and what a pass records in its tables is asserted at a span.
- Write source files with an editor or a script that checks for exact matches. Never generate source through shell
  heredocs or PowerShell arrays; quotes and backslashes do not survive.

## Traps of Stage 0

Stage 0 has no type checker: a mistake is found when the line runs, so **every function must be run by a test**.

1. **A `var` argument is taken before the other arguments are evaluated.** `f(checker, checker.something)`,
   `f(checker, g(checker, x))` and `checker.method(g(checker, x))` read a place that was moved out, and fail far away
   ("Void has no method ...", "a value of type Function has no method ..."). Hoist the inner read or call into a
   `const` first. `grep -nE "\(checker, [^)]*checker\."` finds most of them. **This is a limitation of stage 0, not a
   rule of the language:** exclusivity begins the `var` access of a call after all of its arguments have been evaluated
   (CONCEPT, `var` Paths), so all three lines above are legal TorbScript. The hoists stay until stage 0 is gone.
2. The same one level down: `checker.list.add(Item(checker.other.length()))` has to be two statements, and a
   `var self` method must not hand a field of `self` to another `var self` method.
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
