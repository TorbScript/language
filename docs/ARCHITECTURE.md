# TorbScript Implementation Architecture

**TorbScript is written in TorbScript.** The toolchain in [`compiler/`](../compiler) is the real one. The Rust code in
[`bootstrap/`](../bootstrap) exists for a single reason: something has to run the compiler until the compiler can
compile itself. It is temporary, deliberately small, and gets no feature the compiler does not need.

The language itself is specified in [CONCEPT.md](../CONCEPT.md).

## Repository

| Directory    | Contains                                                                                   | Language   |
|--------------|--------------------------------------------------------------------------------------------|------------|
| `compiler/`  | The toolchain: front end, type checker, back ends, tools. A normal TorbScript project        | TorbScript |
| `std/`       | The standard library: one package per directory (`std/prelude`, `std/fs`, `std/io`, ...)      | TorbScript |
| `examples/`  | Tour, example projects. With `std/` and `compiler/` the conformance suite of every stage     | TorbScript |
| `bootstrap/` | Stage 0: parser and tree-walking interpreter. Thrown away after the compiler compiles itself | Rust       |
| `docs/`      | This file                                                                                    |            |

The root `project.trb` makes the repository a workspace of `std/*`, `compiler` and `examples/*`. That is how the
compiler finds the standard library: `"std/fs"` is a member of the workspace it runs in.

## Stages

```text
stage 0   bootstrap (Rust)          runs compiler/ from source, without type checking
stage 1   compiler/ on stage 0      type checks and compiles compiler/ to a native `torb`
stage 2   that `torb`               compiles compiler/ again. Same output as stage 1: the fixpoint test
```

After stage 2 works, a release of `torb` builds the next one (as Go and Rust do it). `bootstrap/` is then frozen: it
stays only as the way to build the very first binary from source, and it never has to understand language features
that came later, because the path from it is "bootstrap builds version N, N builds N+1".

### What Stage 0 Is, and Is Not

- A hand-written lexer and recursive descent parser (`torb-syntax`). It was written first and is the **reference for
  the port**: `compiler/src/syntax` is tested against it token by token (and later tree by tree) on every `.trb` file
  in the repository.
- A tree-walking interpreter without a type checker (`torb-interpreter`). Values are reference counted and copied on
  write, `var` paths are "take out, change, put back" - the memory model of the language, without any optimization.
- It implements the part of the standard library the compiler needs natively and does **not** load `std/`.
- Where the language lets types decide something, stage 0 approximates it with what it sees at runtime
  (see [bootstrap/README.md](../bootstrap/README.md) for the list). The compiler sources stay inside of what both
  agree on. That subset is ordinary, valid TorbScript - nothing in `compiler/` is written "for the bootstrap".
- It will not get a type checker, a bytecode VM, tasks, the sandbox or FFI. Those are written once, in TorbScript.

## Pipeline of the Compiler

```text
source ─► lexer ─► parser ─► AST ─► name resolution ─► type checker ─► typed IR ─┬─► C ─► native          (torb build)
                                                                                 └─► bytecode ─► VM      (torb run)
```

Everything up to the typed IR is shared. That is what "nothing observable differs between interpreter and compiled
binary" rests on: both back ends consume the same, fully resolved program, and every rule of the language
(exclusivity, dead changes, exhaustiveness, quoting) is decided before they run.

| Module (`compiler/src/`) | Contains                                                              | State    |
|--------------------------|-----------------------------------------------------------------------|----------|
| `syntax/`                | Source text, spans, diagnostics, tokens, lexer, AST, parser           | done, verified against stage 0 |
| `project/`               | Paths, the source tree, `project.trb`, workspaces and their members   | done     |
| `semantics/`             | Modules, symbols, visibility, names in type positions (`torb check`)  | done     |
| `semantics/checker/`     | The type checker ([docs/TYPECHECKER.md](TYPECHECKER.md))              | started: types and signatures (4.1), statements and monomorphic expressions (4.2) |
| `cli/`                   | Collecting files, rendering diagnostics                               | started  |
| `ir/`                    | Typed IR, lowering, last-use analysis                                 | started: the IR, layouts, mangling, the verifier, the text format (5.1) |
| `backend/c/`             | Typed IR to C                                                         | planned  |
| `backend/bytecode/`, `vm/` | Bytecode and the VM that runs it (`torb run`, sandbox, REPL)        | planned  |
| `tools/`                 | Formatter, test runner, package manager, language server              | planned  |

### Front End

- **Hand-written lexer and recursive descent parser** with a Pratt loop for operators. Command calls, "the `{`
  belongs to the command", generics decided by the token after `>`, and string interpolation are context rules that
  are a few lines of code by hand and a fight with every generator. Error messages and recovery are better by hand,
  which matters for the language server.
- **Newlines are resolved in one place.** A single filter removes the line breaks that do not end a statement (inside
  `(`/`[`, after an operator or `,`, before `.`, `?.`, an operator, `with`, `where`). The parser only sees the ones that
  count.
- **The lexer works on characters, spans are byte offsets.** `SourceText` holds the characters of a file and the byte
  offset of each. That keeps the lexer free of UTF-8 arithmetic, and spans are what `text[from..to]` takes.
- **Speculation instead of lookahead tables.** Generic arguments in expressions, closure parameter lists and command
  calls are parsed by trying. The parser is a value: `const before = parser`, try, and `parser = before` if it did not
  work out. No checkpoint type, nothing to forget to restore.
- **The grammar is functions, the parser is a small value.** `Parser` is the cursor over the tokens (`bump`, `eat`,
  `expect`, `commaSeparated`). The grammar lives in functions that take a `var parser: Parser` (`parseExpression`,
  `parsePattern`, ...), one file per part of the language, imported by name. A type with sixty methods spread over
  five files would need a rule for that; functions need none.
- **Doc comments are kept aside and attached.** The lexer collects `/** ... */` next to the tokens, the parser hands
  each one to the declaration, member, parameter or case field that follows it. They never influence parsing.
- **One context rule lives in the lexer:** directly inside of the braces of a `match`, a line that starts with `.` is
  an arm (`.Circle(r) => ...`), everywhere else it continues the line above. The newline filter tracks which `{`
  belongs to a `match` for that.
- Diagnostics are data (`Diagnostic(message, span, notes)`). Rendering is the business of the CLI and the LSP.
- **IO happens at the edge.** `project/read.trb` reads every file that could matter - the `project.trb` of every
  directory above the given paths, and every `.trb` below the outermost of them - into a `SourceTree`. Everything
  after that (workspace, module graph, symbols, name resolution) is a pure function of that value. So the tests build
  whole projects in memory, and a language server can hand in text that is newer than the disk.
- **`project.trb` is read from its syntax tree, not evaluated.** Top-level command calls with literal arguments and
  the blocks the toolchain knows; everything else in the file is ignored silently. It is a receiver script, and the
  sandboxed VM will evaluate it properly in milestone 7 - the static reading is what lets the compiler find its own
  standard library before there is a VM.
- **Program-wide things are ids, not references.** Values have no identity in TorbScript, so modules and symbols live
  in lists and are referred to by `ModuleId` and `SymbolId`. Anything a later pass has to look up (what a name in a
  type position resolved to) is a side table keyed by module and span, never a field in the syntax tree - the tree
  stays what the parser produced, and the differential tests stay meaningful.
- **Exports are computed to a fixpoint.** `lib.trb` of a package is usually nothing but `public use`, and imports may
  be cyclic. Nothing runs when a module is imported, so a cycle is only a reason not to recurse: the binder adds what
  it can until nothing changes anymore, and reports what is missing afterwards, once.
- **The scope of a file** is the public names of its prelude package, plus its imports, plus its own top-level
  declarations, with the later ones shadowing the earlier ones. `Void` and `Never` are a fixed table behind all of
  that, so a script without a prelude can still name them; `std/prelude` declares them as well and wins.
- **Only names in type positions are resolved here.** Names in expressions cannot be (see milestone 4), so the front
  end does not pretend to: it checks that every imported name exists, and every name that stands where a type stands.

### Back Ends

- **C first.** The first native back end prints C: it runs everywhere a C compiler does, it is debuggable, and it is
  the shortest path to the fixpoint. `torb build` needs a C compiler on the machine for now. A back end that emits
  machine code directly can replace it later behind the same typed IR; nothing in the language depends on it.
- **The VM is a TorbScript program.** `torb run`, the sandbox and the REPL interpret bytecode produced from the same
  IR. The VM is part of `compiler/` and gets compiled like everything else, so "interpreted and compiled behave the
  same" is tested by running the conformance suite through both.
- Generics: monomorphized where the type is known, dictionary passing where it is not (generic methods on
  trait-typed values, see Open Questions in the concept). Both back ends support both.

### Runtime and `native`

`native` declarations are what the compiler and its runtime provide instead of TorbScript code: numbers, `String`,
the storage of `ArrayList` and the tries, reference counting, tasks. With the C back end the runtime starts as a small
C file that is linked into every binary. It shrinks over time: what can be written in TorbScript on top of `foreign`
declarations and a few intrinsics (raw memory, atomics) moves to `std/`.

## Memory Model

The language has value semantics; identity is the marked exception (`shared type`). The implementation maps that to:

| Language                                         | Implementation                                                                |
|--------------------------------------------------|-------------------------------------------------------------------------------|
| Small values (`Int`, `Float`, `Bool`, `Char`, small tuples and types, `Array<Item, Size>`) | Stored inline. Copied, never counted              |
| Storage (`ArrayList`, `String`, tries, big types) | Reference counted buffer. A copy shares it. A write goes through "make unique": in place if the count is 1, copy first otherwise |
| `var` parameters, `var self`, `a[i].x = 1`       | A reference into the frame of the caller. Never escapes (references are second-class), so it needs no counting and no lifetime tracking |
| `shared type`                                    | Reference counted object with interior mutability. The only thing that can form cycles |
| Cycles                                           | Trial deletion (Bacon/Rajan) over `shared` objects only. Values are never scanned |

- **Reference counts are not atomic.** Every task owns its heap. Values that cross a task boundary are sent through a
  channel: storage with count 1 moves, shared storage is copied once. (To be measured against "atomic counts only for
  storage that was ever sent".)
- **Last use is a move.** The lowering to IR marks the last use of every binding. A moved value keeps its count at 1,
  so `list = list.added(x)` and the default participles (`var result = self`) change in place instead of copying.
  This is what makes the functional style as fast as the mutating one, and it has to be identical in both back ends.
- No tracing garbage collector, and no destructors: releasing storage never runs user code. What is deterministic in
  the language is the cleanup that is written down (`using`, `Close`), not when a count reaches zero.

(Stage 0 does the same with `Rc::make_mut` and moves values out of their path for the duration of a change. It has no
last-use analysis; the patterns the compiler relies on - building lists and maps in `var` fields - are in place
regardless.)

## Quality

- **Differential tests while both implementations exist.** `cargo test` in `bootstrap/` runs the TorbScript lexer on
  and parser on every `.trb` file of the repository (and on files full of errors) and compares tokens, syntax trees,
  diagnostics and their rendering with the Rust implementation. The tree is compared through the generated `Show` of
  the TorbScript AST, which stage 0 prints for its own tree (`torb ast`). Neither side can drift.
- The tests of the compiler are TorbScript (`compiler/tests/*.test.trb`, `torb test`), so they move to stage 1 and 2
  unchanged.
- `examples/`, `std/` and `compiler/` are the conformance suite: every file parses and resolves today (`torb check`
  over the repository is a test in `bootstrap/`); later every file type checks, and the runnable ones produce the
  same output in every stage and back end.
- Stage 0: `#![forbid(unsafe_code)]`, `clippy -D warnings`, `rustfmt`, no dependencies.

## Milestones

1. **Done:** stage 0 (parser, interpreter, `torb run`, `torb test`), the lexer in TorbScript, verified against stage 0.
2. **Done:** AST and parser in TorbScript. Trees, diagnostics and their rendering agree with stage 0 on every file of
   the repository, including files full of errors. The compiler parses itself.
3. **Done:** modules and declarations. Projects and workspaces (`project.trb`, `members "std/*"`), the module graph
   (`use`, packages, the prelude from `std/`), the symbols of every module, visibility, and every name in a _type
   position_. `torb check <path>...` takes a workspace root, a project or a single script, reports what does not
   resolve, and hands the type checker the tables (modules, symbols, the scope of every module, and the symbol every
   name in a type position resolved to). It looked at `std/` and the examples for the first time beyond their
   syntax and found `Range`, `NumberParseError` and `NumberRangeError` missing from the prelude.
4. Type checker: inference, traits, generics, exhaustiveness, `var` paths, exclusivity, dead changes - and the names
   in _expressions_. They cannot be resolved earlier: what `port` means in `server { port 8080 }` depends on the type
   of the parameter the closure is passed to (design principle 1), so resolving names and checking types is one pass.
   From here on the compiler checks itself, which stage 0 never could. The plan and the state of its ten steps are in
   [docs/TYPECHECKER.md](TYPECHECKER.md); **4.1 and 4.2 are done**: every type position of the repository becomes a
   type, every declaration a signature, and every expression of the monomorphic part of the language a type - about
   half of the expressions of the repository today, with the rest recorded as "not checked yet" and counted by
   `torb check --statistics`. The first thing it found by checking the compiler itself was that
   `compiler/src/ir/mangle.trb` mixed the `UInt8` of a byte with `Int64` arithmetic.
5. Typed IR and the C back end. The tour runs natively, with the same output as under stage 0. The plan and the state
   of its sub-milestones are in [docs/BACKEND.md](BACKEND.md); **5.1 is done**: the IR's data model, the layouts and
   their representation classes, the mangling, a builder, a verifier and the text format.
6. The compiler compiles itself, stage 1 and stage 2 agree. `bootstrap/` is frozen.
7. Bytecode and VM, tasks, channels, the sandbox (`Sandbox.load`, receiver scripts, `project.trb`).
8. Formatter, language server, package manager.
