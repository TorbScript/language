# TorbScript Implementation Architecture

**Status: implemented** — this is how the toolchain is built today, milestones 1 to 6 are done, and 7 and 8 are the
ones ahead.

**TorbScript is written in TorbScript.** The toolchain in [`compiler/`](../compiler) is the whole of it, and it
compiles itself. The one external tool a checkout needs is a C compiler.

The language itself is specified in [CONCEPT.md](../CONCEPT.md).

## Repository

| Directory    | Contains                                                                                   | Language   |
|--------------|--------------------------------------------------------------------------------------------|------------|
| `compiler/`  | The toolchain: front end, type checker, back ends, tools. A normal TorbScript project        | TorbScript |
| `std/`       | The standard library: one package per directory (`std/core`, `std/fs`, `std/io`, ...). `std/prelude` declares nothing: it re-exports | TorbScript |
| `runtime/`   | What every compiled binary links against: counts, `String`, the collections, panics, IO      | C11        |
| `examples/`  | Tour, example projects. With `std/` and `compiler/` the conformance suite of every back end  | TorbScript |
| `tests/`     | The conformance suite (`tests/conformance/`) and the language smoke programs (`tests/language/`), each a workspace of its own | TorbScript |
| `docs/`      | This file                                                                                    |            |

The root `project.trb` makes the repository a workspace of `std/*`, `compiler`, `examples/*` and `benchmarks`. That is
how the compiler finds the standard library: `"std/fs"` is a member of the workspace it runs in. A workspace manifest
has no test directory unless it names one, so `tests/` - which belongs to other workspaces - is no part of the root
package. A file or a project outside of every workspace with a `std/` gets the toolchain's own (`project/toolchain.trb`).

## The Bootstrap

```text
seed          a `torb` that already exists: a release binary, or seed/program.c compiled once
step 1        the seed compiles compiler/ into build/bootstrap/torb
step 2        that binary compiles compiler/ again into build/release/torb
```

`sh tools/bootstrap.sh` runs both steps and compares the two `program.c` byte for byte. They agree exactly when the
compiler is a fixed point of itself, which is what says the compiler is correct about the language it is written in.
`build/release/torb` is the compiler that comes out.

`seed/` is outside git, because a seed is a build artifact of an earlier commit and not a fact about this one. A
release publishes a binary per platform and the compiler's own C as one file, and between releases the seed is
whatever `torb` was built last. A commit that changes the syntax therefore comes in two: one that teaches the compiler
the new form beside the old one, and one that switches the sources over
([docs/RUST-EXIT.md](RUST-EXIT.md) section 4.2).

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
| `syntax/`                | Source text, spans, diagnostics, tokens, lexer, AST, parser           | done     |
| `project/`               | Paths, the source tree, `project.trb`, workspaces and their members   | done     |
| `semantics/`             | Modules, symbols, visibility, names in type positions (`torb check`)  | done     |
| `semantics/checker/`     | The type checker ([docs/TYPECHECKER.md](TYPECHECKER.md))              | started: see the table in section 8 of TYPECHECKER.md for what is done |
| `cli/`                   | Collecting files, rendering diagnostics                               | started  |
| `ir/`                    | Typed IR, lowering, last-use analysis                                 | started: see the table in section 6 of BACKEND.md for what is done |
| `backend/c/`             | Typed IR to C                                                         | started: see the table in section 6 of BACKEND.md for what is done |
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
  standard library before there is a VM. It is nevertheless **type checked** like any other file, against the `Project`
  of [`std/project`](../std/project), and so is every file a `Sandbox.load<Value>("./config.trb")` names with a literal
  path: such a file is a module whose body is a receiver closure, not a module anybody imports.
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
- **Back ends after the VM (JavaScript, PHP) keep the language's semantics, not the host's.** The integer types keep
  their fixed widths and panic on overflow exactly as in C - an answer in `TODO.md` once preferred JavaScript's
  `number` for `Int`, and the fixed widths of CONCEPT's Built-in Types win over it, so a JavaScript `Int` is an exact
  64-bit integer however it is represented. And a type that may contain a `Close` object stays reference counted
  even where the host has a garbage collector, because the moment its `close()` runs is part of the program's meaning
  (`docs/DESTRUCTORS.md` section 2a).

### Runtime and `native`

`native` declarations are what the compiler and its runtime provide instead of TorbScript code: numbers, `String`,
the storage of `ArrayList` and the tries, reference counting, tasks. With the C back end the runtime starts as a small
C file that is linked into every binary. It shrinks over time: what can be written in TorbScript on top of `foreign`
declarations and a few intrinsics (raw memory, atomics) moves to `std/`.

[`runtime/`](../runtime) is that runtime: portable C11 with no dependency beyond libc, one page of ABI in
[runtime/README.md](../runtime/README.md), built and tested on its own with `sh runtime/build.sh`. The bytecode VM
calls the same functions, so there is exactly one `ArrayList`, one hash table and one `String` in a process whichever
back end is running. The contract between the two sides is the **manifest of natives**
(`compiler/src/backend/c/natives.trb`): every `native` declaration of `std/` mapped to an intrinsic, to a runtime
symbol, or to a body the lowering generates - and to the milestone that will bring it where the runtime does not have
it yet, so a missing native is a compile error naming the milestone and never a link error.

## Memory Model

The language has value semantics; identity is the marked exception (`shared type`). The implementation maps that to:

| Language                                         | Implementation                                                                |
|--------------------------------------------------|-------------------------------------------------------------------------------|
| Small values (`Int`, `Float`, `Bool`, `Char`, small tuples and types, `Array<Item, Size>`) | Stored inline. Copied, never counted              |
| Storage (`ArrayList`, `String`, tries, big types) | Reference counted buffer. A copy shares it. A write goes through "make unique": in place if the count is 1, copy first otherwise |
| `var` parameters, a `var fn` receiver, `a[i].x = 1`       | A reference into the frame of the caller. Never escapes (references are second-class), so it needs no counting and no lifetime tracking |
| `shared type`                                    | Reference counted object with interior mutability. The only thing that can form cycles |
| Cycles                                           | Not collected, ever. Handles instead of references, receiver closures for stored callbacks, a leak report that names the types - `docs/DESTRUCTORS.md` section 9 |

- **Reference counts are not atomic.** Every task owns its heap. Values that cross a task boundary are sent through a
  channel: storage with count 1 moves, shared storage is copied once. (To be measured against "atomic counts only for
  storage that was ever sent".)
- **Last use is a move.** The lowering to IR marks the last use of every binding. A moved value keeps its count at 1,
  so `list = list.appended(x)` and the default participles (`var result = self`) change in place instead of copying.
  This is what makes the functional style as fast as the mutating one, and it has to be identical in both back ends.
- No tracing garbage collector and no cycle collector. **`close()` is the one destructor** (planned,
  [DESTRUCTORS.md](DESTRUCTORS.md)): only a `shared type` may have one, the last release runs it, and a slot whose
  type may contain one is released at the end of its scope in reverse declaration order rather than at its last use,
  so the moment it runs is a line in the source. Every other release runs no user code and stays where the last-use
  analysis puts it.

## Quality

- **The conformance suite is the behaviour of the language, written down.** `tests/conformance/` holds one small
  program per behaviour, built and run with `torb build`, compared against its `.expected`, `.stderr` and `.exit` byte
  for byte, and asked to free everything it allocated. `tools/conformance.sh` is the runner and
  [tests/conformance/README.md](../tests/conformance/README.md) is the contract.
- The tests of the compiler are TorbScript (`compiler/tests/*.test.trb`, `torb test`), so a back end that comes later
  inherits them unchanged.
- `examples/`, `std/` and `compiler/` are checked as one: every file parses, resolves and type checks
  (`torb check .` over the repository), and the runnable ones produce the same output in every back end.
- **The fixpoint is the gate on the compiler itself**: the seed builds it, it builds itself, and the two `program.c`
  are identical.

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
   From here on the compiler checks itself. The plan and the state of its ten steps are in
   [docs/TYPECHECKER.md](TYPECHECKER.md); **4.1 to 4.4 are done**: every type position of the repository becomes a
   type, every declaration a signature, and every expression a type - traits and their implementations included, so the
   operators, `for`, indexing, interpolation, `?`, `??`, `?.` and `into()` all resolve through the trait they mean, and
   generics, closures and inference included, so every call records the type arguments it was instantiated with, one
   witness per bound, and what every closure captures. **4.5 is done**: every `match` is checked for the cases it does
   not cover and for the arms nothing can reach, and every pattern position carries the plan milestone 5 lowers it
   from. **4.6 is done as well**: every change goes through a path the checker resolved and recorded, no two overlapping
   accesses of one call may run where one of them is a `var`, and a change nobody reads afterwards is an error - which
   found a dozen reads of a place that a `var` argument had already taken out, in the compiler's own sources. **4.7 is
   done** too: receiver closures, the innermost-receiver rule, property commands, and receiver scripts - `torb check`
   checks every `project.trb` of the repository against the `Project` of `std/project`, and
   `examples/config-dsl/config.trb` against the type its `Sandbox.load` names. **4.8 is done**: an `Expression<Value>`
   parameter or binding quotes what arrives at it, so every one of the repository's 1600 `assert`s records the tree
   milestone 5 has to build, the captures it hands on and their `Encode` witness, and `examples/query-provider`
   translates `filter { _.age >= minAge }` to SQL. **4.9 is done** too: `private` and `private(var)`, what a `public`
   declaration promises, which files may contain top-level code, top-level constants that are evaluated at compile time,
   and the rules of `native`, `shared type`, `isSame`, `extend` and `foreign`. **Milestone 4 is done**: 4.10 closed the
   last unchecked expressions (the generated members of a literal type, a `.Case` whose type only a following `?` would
   decide, the callee of `into`), made exclusivity follow the model of Swift, and added `torb check --timings` - the
   whole repository checks with "no problems" and **every** expression of it has a type, which `torb check --statistics`
   now reports as 0 deferred.
   `std/` and `compiler/` are at 100%. The first thing it found by checking the compiler itself was that
   `compiler/src/ir/mangle.trb` mixed the `UInt8` of a byte with `Int64` arithmetic; checking the generic code found
   `Set.new()`, which the standard library never had.
5. Typed IR and the C back end. The tour runs natively. The plan and the state
   of its sub-milestones are in [docs/BACKEND.md](BACKEND.md); **5.1 is done**: the IR's data model, the layouts and
   their representation classes, the mangling, a builder, a verifier and the text format. **5.R1 is done**: the C
   runtime ([`runtime/`](../runtime)) with counts, `String`, one list, one ordered hash table, panics and the minimum
   of file IO, and the manifest of natives that is the contract between it and the lowering.
6. **Done:** the compiler compiles itself. The seed builds it, it builds itself again, and the two emit the same C.
7. Bytecode and VM, tasks, channels, the sandbox (`Sandbox.load`, receiver scripts, `project.trb`). The runtime half
   of streams comes with it: the specification and the declarations are in [docs/STREAMS.md](STREAMS.md) and
   `std/stream`, and section 14 there lists what 7 and 10 have to build.
8. Formatter, language server, package manager.
