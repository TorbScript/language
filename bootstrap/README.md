# Bootstrap (Stage 0)

A parser and a tree-walking interpreter for TorbScript, written in Rust. It exists to run the real toolchain in
[`../compiler`](../compiler) until that toolchain can compile itself, and is thrown away afterwards
(see [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)).

```text
cargo run --release -- run ../compiler tokens some-file.trb   # Run the compiler (src/main.trb of the project)
cargo run --release -- run ../compiler build some-file.trb    # A native binary, through C (needs a C compiler)
cargo run --release -- run script.trb [arguments]             # Run a single file
cargo run --release -- test ../compiler/tests                 # Run *.test.trb files (one process per file, see below)
cargo run --release -- test ../compiler/tests --jobs 1        # ...one after another, in this process
cargo run --release -- parse ..                               # Check the syntax of every .trb file
cargo run --release -- tokens file.trb                        # Tokens, in the format of compiler/src/syntax/dump.trb
cargo run --release -- ast file.trb                           # Syntax tree, as the generated Show of compiler/src/syntax/ast.trb
cargo run --release -- canon --check ..                        # Is every file in the formatter canon? (see below)
cargo run --release -- highlight file.trb                     # Semantic tokens as JSON, for the VS Code extension
cargo test                                                    # Includes the differential tests against compiler/
```

`torb canon [--check] [--rule calls|strings|imported-case-patterns|unused-bindings]... <path>...` writes TorbScript
sources in the canon of the formatter (CONCEPT, "Formatter Canon"), over the syntax tree and never with a regular
expression: `calls` puts a call in command form wherever the grammar allows it and in parentheses everywhere else,
`strings` indents a multi-line `"""`. Both run by default; the two that change the syntax *tree* have to be asked for -
`imported-case-patterns` (`.None` becomes `None`) and `unused-bindings` (a binding of a refutable pattern that the arm
never mentions becomes `_`). It is listed in `torb help` like every other command - stage 0 has no hidden ones - and it is
temporary: milestone 8's `torb format`, written in TorbScript, enforces the same canon, and this goes away with the rest
of stage 0. Every edit is applied on its own and the file is parsed again; it only stays if the syntax tree is the one
from before with every span and every `CallStyle` erased, so a run cannot change what a program means. A file that does
not parse is skipped, `tests/parser-cases/` and `tests/lexer-cases/` are not even read, and a second run over the same
tree changes nothing.

`torb highlight <file>` (or `torb highlight --stdin`, for an editor buffer that was never saved) prints one JSON
document of semantic tokens for the `.vscode/extensions/torbscript` VS Code extension:
`{"tokens": [[line, startCharacter, length, "kind", ["modifier", ...]], ...]}`, 0-based, UTF-16 code units (what
`SemanticTokensBuilder` wants), sorted, non-overlapping, single-line. `crates/torb-cli/src/highlight/resolver.rs` is
a syntactic scope resolver over `torb_syntax`'s tree - not a type checker, there is none in stage 0 - that decides
what a name is (`type`, `interface`, `typeParameter`, `enumMember`, `namespace`, `function`, `method`, `parameter`,
`variable`, `property`) and whether it is `readonly`/`mutable` (`const` vs. `var`, and a `var fn` at its
declaration as at every call), `static` (a `static` member) or `defaultLibrary` (a prelude name). It never panics - a file with syntax errors still yields every token
the parser could resolve around the damage - and its doc comment lists exactly what it cannot know without a type
checker (an arbitrary receiver's real type, a single-segment import's real kind). This is stage-0 tooling: milestone
8's language server replaces it behind the *same* JSON protocol, so the extension does not need to change again when
that happens.

| Crate              | Contains                                              |
|--------------------|-------------------------------------------------------|
| `torb-syntax`      | Lexer, syntax tree, parser. The reference for the port |
| `torb-interpreter` | Loader, values, evaluation, native standard library   |
| `torb-cli`         | The `torb` binary                                     |

## What It Does Not Do

There is no type checker. A correct program runs correctly; an incorrect one fails at runtime, or is not noticed.
Where the language lets types decide, the interpreter uses what it sees at runtime:

| Language                                              | Stage 0                                                          |
|-------------------------------------------------------|------------------------------------------------------------------|
| Literals adapt to the expected type                   | An `Int` becomes a `Float` where a `Float` is annotated, and next to a `Float` in arithmetic |
| A bare uppercase name in a pattern is a case in scope  | The parser decides by the first letter; the interpreter looks the name up when the pattern runs and fails loudly if it is nowhere |
| `.Case` where the type is expected                    | In patterns always. In expressions where the value arrives at an annotation, a parameter, a field, a result, an assignment or `==`. Inside a list, a tuple or `Some(...)` on either side of `==`/`!=`, resolved element-wise against the value at the same position on the other side; an implicit case with no counterpart to resolve against is a runtime error. Elsewhere: `TokenKind.Dot` |
| `value.into()`, `to<Target>()`, `Json.decode<T>()`    | Not available (they are chosen by the expected type). `Target.from(value)` works |
| `?` converts errors through `From`                    | By the declared return type of the function: a `from` for the type of the error, or a case that wraps it |
| Implicit closure parameters named by the function type | For declared functions with an inline function type. Native methods know `_`, `_2` only |
| Lazy pipelines                                        | Stages are eager and return lists. No infinite sources except `for x in 0..` |
| User-defined `equals`/`hash`                          | `==` and map keys are always structural                          |
| Exclusivity, dead changes, exhaustiveness, visibility | Not checked. `const`, fields without `var` and temporaries are checked when a change reaches them |
| All integer types                                     | One 64 bit integer. Overflow panics, with the language's message  |
| `Bits` (`shiftedLeft`, `bitwiseAnd`, ...)              | Not available: nothing the compiler runs on stage 0 needs one     |
| `shared type`, tasks, sandbox, `foreign`, `Expression<Value>` trees | Not available. `assert` shows the source of its condition and the values of both sides |
| `std/`, packages, workspaces                          | Not loaded. `natives.rs` and `prelude.trb` implement what the compiler uses, imports of packages are ignored |
| Visibility of `extend`                                | Every `extend` of every loaded module applies everywhere |

The compiler sources stay inside of what stage 0 and the language agree on. If the compiler needs something that is
missing here, it is added here - and nothing else is. Three natives arrived that way with `torb build` (milestone 5.3),
all three declared in `std/` and in the manifest of natives: `Process.run` (the driver runs a C compiler with it and has
to tell "there is no such program" from "it said no"), `File.createDirectory` (`mkdir -p` for the directory the C and
the binary go into) and `Environment.get` (`$TORB_CC`, `$TORB_RUNTIME`). `ProcessOutput` is in `prelude.trb`, with the
field order `std/process` declares - the natives build it positionally.

## How a Program Ends

The three ways a program can stop are three reports, and the first two are the language's - a compiled binary writes the
same bytes and leaves with the same code, which is what the conformance suite compares.

| | Report | Exit code |
|---|--------|-----------|
| A **panic** - `panic`, an overflow, a division by zero, an index out of range, `expect` on `None` | `panic: <message>`, then `  at <path>:<line>:<column>` | **101** |
| A top-level **`?`** that failed | `error: <the error through Show>`, then one `  caused by: <...>` per link of `cause()` | 1 |
| A failure of the **interpreter** - a name that is nowhere, a method a value does not have, a `var` path that was moved out | `error: <message>`, the site, and `  in <function>` for the calls it came through | 1 |

The third has no counterpart in a compiled program: the type checker of stage 1 rejects every program that reaches one.
Two things follow from the split.

- **A path of a runtime location is the stable one**, `torbscript/compiler/src/ir/print.trb`: the package's name plus the
  file below the package's directory, which is what `pathsOfModules` of the lowering interns and what a `#line` of the
  generated C carries. No working directory and no machine reaches the output, so the two reports can be compared byte
  for byte. A problem of *loading* - a syntax error, an import that is nowhere - keeps the path of the machine instead,
  because it is editor-facing.
- **A panic prints two lines and no more**, the way the release profile of a compiled program does. `TORB_FRAMES=1` adds
  the calls it came through, and the site in the program under a panic whose site is a file of `std/`; that is how a
  panic inside the toolchain is debugged. A failure of the interpreter always has its frames.

A panic that a native answers for a body of `std/` names that file and no position (`at std/core/src/option.trb`), since
stage 0 does not load `std/` and has no line for it. The conformance runner reads a position in a `std/` frame as `_:_`
on both sides, so the two agree on the file without pinning a line that a comment above it moves.

## The Conformance Suite

`bootstrap/tests/native/` is the conformance suite (`crates/torb-cli/tests/native.rs`): one small program per
behaviour, compiled to a native binary, run, and compared with itself on stage 0 - standard output, standard error and
exit code, byte for byte, with nothing about what a program does exempt. It is a workspace of its own whose only members
are `std/`, so `torb check ..` over the repository does not look at it; `torb run ../compiler check tests/native`
checks it, and that is a gate. **`bootstrap/tests/native/README.md` is the contract**: what is compared, what is not and
why, how a program is added, and which program pins which behaviour. It has two subdirectories, and neither is an
exception to the contract: `stage-0-only/` is a waiting room - a program lands there when the back end cannot produce
the behaviour yet and stage 0 already answers what the language says, and the runner compares it on stage 0 alone until
it can move up one directory - and `binary-only/` is the other side, where the two implementations answer
**deliberately** differently and the runner builds and runs the program as a binary alone. `cargo test --release --test
native` runs all three.

Two more runners take minutes and need a C compiler, so both are `#[ignore]`d and are run by name:

```text
cargo test --release --test fixpoint -- --ignored --nocapture   # the compiler compiles itself to the same C twice
cargo test --release --test suite    -- --ignored --nocapture   # the compiler's own tests run from the binary
```

`suite.rs` is the headline gate of milestone 5.11: it builds the compiler with stage 0, runs
`<the binary> test ../compiler/tests` - one C translation unit for all 55 test files, with a generated `main` that runs
each file's entry with the file's name in front of it - and compares the whole report line by line with
`torb test ../compiler/tests` on stage 0, plus the exit code.

`bootstrap/tests/scripts/` is the other half: long programs that run on stage 0 alone and are compared with their
`.expected` by `crates/torb-cli/tests/self_hosted.rs`. A behaviour both back ends have to agree on belongs in
`tests/native/` instead. It is a workspace of its own too, and it is checked by the same second `check`.

## Performance

Stage 0 is the edit-test cycle of the whole project, so it is worth keeping fast. It is a tree-walking interpreter and
stays one; what made it slow was not the walking but what every step around it paid for.

- **Scopes come from a pool.** A block, a turn of a loop, an arm of a `match` and a call each need a scope, 47 million
  of them per `check ..`, and allocating them (and the `Vec` of their slots) was the single biggest cost. A scope goes
  back to the pool of `Interpreter` when its reference count is one again, which is why a scope a closure captured is
  never recycled, and it keeps the capacity its slots have.
- **Every scope carries a filter of the names it declares**, one bit each. A name that is not a local - a top-level
  function, a field of `self` - used to be looked for in every scope up to the root, several times per call site;
  now the scopes that cannot have it are skipped without reading their slots.
- **Names are compared as fingerprints first.** `mark(name)` is the length and the first and the last character in one
  `u32`. Slots, fields and cases carry it, so scanning a list of names compares integers; only a fingerprint that
  matches leads to comparing the characters. The maps that are asked while the program runs use FNV-1a.
- **The buffers of a call come from a pool too**, `Text` keeps its offsets in `u32` so that `Value` is 32 bytes
  instead of 40, a `Some(x)` or `.Case(x)` pattern matches the values where they are instead of copying them out,
  and `eval` keeps its stack frame small: it runs for every node of a program, and the arms that need room for a
  `String`, a `Vec` or a message are functions of their own. The release profile links with full LTO.
- **`torb test <dir>` runs one process per test file**, as many at a time as the machine has cores (`--jobs N`, and
  `--jobs 1` is the old behaviour in this process). The interpreter is built on `Rc`, so a process is the unit of an
  independent run. The output is passed on in the order of the files, so it does not depend on which finished first.
  `crates/torb-cli/tests/self_hosted.rs` compares the files of the repository on as many threads, the same way.

None of this changes what a program does: the same output, the same messages, the same order of evaluation.

### Profiling

There is no profiler on every machine, so the interpreter can count what it does itself. The counters cost nothing in
a normal build (`is_enabled` is a compile-time `false` there and everything folds away), so they need their feature:

```text
cargo build --release --features profile
TORB_PROFILE=1 target/release/torb run ../compiler check ..
```

The report goes to standard error, sorted by time and then by count: one line per kind of syntax tree node, per native
that was called (with the time it took, *inclusive* - a native that calls a closure back counts what the closure
does), and per thing worth counting (`environment.find`, `environment.scope_walked`, `scan.fields`, `scan.cases`,
`allocate.*`, `items_of`, `copy_on_write.*`). `src/profile.rs` is where a new counter goes; `profile::count`,
`profile::add` and `profile::timed` are the three hooks.

How much stack a function needs matters as much as what it does, because the interpreter recurses through the tree.
`cargo rustc --release -p torb-interpreter --lib -- --emit asm -C lto=off` writes a `.s` file into
`target/release/deps`, where the `subq $N, %rsp` after a symbol is the size of its frame.
