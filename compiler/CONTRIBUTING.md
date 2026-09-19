# Working on the Compiler

The compiler is written in TorbScript and, until it compiles itself, runs on stage 0 (`bootstrap/`), an **untyped**
tree-walking interpreter. These are the rules of the code base and the traps of stage 0. They apply to every change.

## Commands (from `bootstrap/`)

```text
cargo build --release                                   # Stage 0. Rebuild after every change of the Rust sources
cargo run --release -q -- test ../compiler/tests        # The TorbScript tests of the compiler
cargo run --release -q -- run ../compiler check ..      # The compiler checks the whole repository: "no problems"
cargo run --release -q -- run ../compiler check --statistics ..
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release                                    # Everything, including the differential tests (minutes)
sh ../runtime/build.sh                                  # The C runtime and its tests (gcc or clang)
```

A change is done when all of them are green and the repository still checks with "no problems". A false positive of
the checker is a bug of the checker.

## Style

- Idiomatic TorbScript, in the style of the code that is there. Full words, no abbreviations (`declaration`, not
  `decl`). No semicolons, never several statements on one line.
- `/** */` doc comments on public declarations that say **why**, not what. Block comments do not nest: never write a
  slash-star or a star-slash inside of a comment (not even in a glob).
- Small values plus free functions that take a `var` parameter (`var parser: Parser`, `var checker: Checker`,
  `var program: IrProgram`). Values have no identity, so program-wide data lives in lists and is addressed by
  integer ids (`ModuleId`, `SymbolId`, `TypeId`, ...).
- Cases: `.Case` in patterns, `Type.Case` in expressions unless stage 0 can see the expected type (the table in
  `bootstrap/README.md`). A bare name in a pattern always binds. Positional arguments come before named ones.
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
   `const` first. `grep -nE "\(checker, [^)]*checker\."` finds most of them. (The exclusivity rule of the language
   rejects exactly this; stage 0 does not check it.)
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

If the compiler legitimately needs a native that stage 0 lacks, add it minimally in
`bootstrap/crates/torb-interpreter/src/natives.rs` **and** declare it in `std/`. Stage 0 gets no language features
and no type checker.
