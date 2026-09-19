# The TorbScript Toolchain

TorbScript, written in TorbScript. Until it compiles itself it is run by the bootstrap interpreter:

```text
cd ../bootstrap
cargo run --release -- run ../compiler check ..
cargo run --release -- run ../compiler parse ../compiler ../std ../examples
cargo run --release -- run ../compiler ast ../examples/tour/src/01-bindings-and-values.trb
cargo run --release -- run ../compiler ir ../examples/tour/src/01-bindings-and-values.trb
cargo run --release -- run ../compiler build ../bootstrap/tests/native/arithmetic.trb
cargo run --release -- test ../compiler/tests
```

`torb check <path>...` takes a workspace root (then all of its members), a single project, or one script file. Every
path around it is loaded too - that is where the imports and the prelude are - but only what was asked for is
reported. `--statistics` adds how many expressions of every module have a type and how many wait for a later
sub-milestone of the type checker (see [docs/TYPECHECKER.md](../docs/TYPECHECKER.md)).

`torb build [path] [--emit-c] [--output <file>]` compiles the entry file it was given to a native binary: it checks,
lowers it to the typed IR, writes one C11 translation unit and hands it plus `runtime/*.c` to the first C compiler it
finds (`$TORB_CC`, `clang`, `gcc`, `cc`, `cl`). The C goes next to the binary, which is `<project>/build/release/<entry
file>` unless `--output` says otherwise, and `--emit-c` stops after writing it - which needs no C compiler at all. A
construct the back end does not compile yet is reported with the milestone that brings it, and nothing is written; a C
compiler error is an internal error of the compiler and keeps the `.c` file. The profiles, the content-hash cache,
`torb run` and `torb test` as native builds are milestone 5.13's (see [docs/BACKEND.md](../docs/BACKEND.md) section 4).

```text
src/
├ main.trb              Command line: check, build, ir, parse, ast, tokens
├ cli/
│ ├ files.trb           Collecting source files
│ ├ build.trb           `torb build`: check, lower, emit C, find a C compiler, compile
│ └ render.trb          Diagnostics as text
├ ir/                   The typed IR and the lowering (milestone 5.1, 5.2)
├ backend/c/
│ ├ natives.trb         The manifest: every `native` declaration of `std/` and what it becomes
│ ├ writer.trb          C as values: expressions, statements, members, declarations, and the one renderer
│ ├ type.trb            Every IR type as C, and the struct declaration of every layout
│ ├ emission.trb        What the emitter writes into: declarations, findings, slot names, locations
│ ├ prototype.trb       A manifest prototype, and whether a call of the native is the one the lowering wrote
│ ├ body.trb            One function body: slots, labels, `goto`, instructions, places
│ └ emit.trb            One C11 translation unit: types, static data, functions, `main`
├ project/
│ ├ path.trb            Paths as text: normalize, join, relative, "is inside of"
│ ├ source-tree.trb     SourceTree: every file with its text, read once
│ ├ read.trb            The only IO of the front end: disk to SourceTree
│ ├ manifest.trb        project.trb, read statically from its syntax tree
│ └ workspace.trb       Finding the workspace of a path, its members and their files
├ semantics/
│ ├ module.trb          ModuleId, Module: one parsed file of one package
│ ├ symbols.trb         SymbolId, Symbol: what a name can mean, and the table of all of them
│ ├ graph.trb           Parsing every module, and what a `use "..."` path points at
│ ├ scope.trb           Exports, imports and the prelude, to a fixpoint. Program: the tables
│ ├ resolve-types.trb   Every name in a type position
│ ├ check.trb           `check`: source tree in, diagnostics and tables out
│ └ checker/            The type checker (milestone 4)
│   ├ type.trb          TypeForm, TypeId, the interning table, signatures, a type in a message
│   ├ wellknown.trb     The declarations of `std/prelude` the language itself refers to
│   ├ context.trb       Checker: every table of the pass, the scopes, and what it resolved
│   ├ unify.trb         Equality of types and substitution of generic parameters
│   ├ lowering.trb      A `TypeReference` to a `TypeId`: names, tuples, functions, literals, const arguments
│   ├ signature.trb     Declarations to signatures and aliases, on demand, with cycle detection
│   ├ member.trb        What `a.b` and `Type.b` mean on a concrete type, and what a declaration comes with
│   ├ name.trb          Names in expressions: the lookup order and the receiver
│   ├ call.trb          Arguments, labels, defaults, variadics, spread, trailing closures
│   ├ expression.trb    The type of every expression, and the operators
│   ├ pattern.trb       Patterns and the names they bind
│   ├ statement.trb     Statements, blocks, bodies, definite return
│   └ check.trb         The walk over the declarations of a module
└ syntax/
  ├ source.trb          Span, SourceText (characters and byte offsets), LineIndex
  ├ diagnostic.trb      Diagnostic
  ├ token.trb           Token, TokenKind, Keyword
  ├ lexer.trb           Source text to tokens
  ├ dump.trb            Tokens as text, the format of the differential tests
  ├ ast.trb             The syntax tree
  └ parser/
    ├ parser.trb        `parse`, and `Parser`: the cursor over the tokens
    ├ statements.trb    Statements, blocks, bindings, conditions
    ├ declarations.trb  fn, type, trait, extend, use, foreign
    ├ types.trb         Types
    ├ patterns.trb      Patterns
    └ expressions.trb   Expressions: command calls, operators, closures, if, match
tests/
├ lexer.test.trb
├ parser.test.trb
├ project.test.trb      Paths, manifests, workspaces
├ check.test.trb        Modules, symbols, visibility, type positions
├ types.test.trb        Types, signatures, equality, substitution, the messages about them
├ harness.trb           The in-memory workspace of the type checker's tests, and what they ask about it
├ c-writer.test.trb     The structured C writer: every node kind, the brackets, the indentation, the paragraphs
├ emit-c.test.trb       The generated C: types, static data, bodies, `main`, and what it cannot emit yet
├ expressions.test.trb  Literals and their adaptation, names, members, operators, `if` and `match`
├ statements.test.trb   Bindings, scopes, assignment, loops, definite return, discarded values
└ calls.test.trb        Arguments, labels, defaults, variadics, constructors, `copy`, the side tables
```

Rules for this code:

- It is ordinary, idiomatic TorbScript. Nothing is written in a special way for the bootstrap; what the bootstrap
  cannot do is simply not used yet (the list is in [bootstrap/README.md](../bootstrap/README.md)).
- **IO happens at the edge.** `project/read.trb` reads every file that could matter into a `SourceTree`, and
  everything after that - workspaces, the module graph, symbols, name resolution - is a pure function of that value.
  So the tests build whole projects in memory, and a language server can hand in text that is newer than the disk.
- **Program-wide things are ids, not references.** Values have no identity in TorbScript, so modules and symbols live
  in lists and are referred to by `ModuleId` and `SymbolId`.
- Every part that is a port of `bootstrap/crates/torb-syntax` is tested against it on all `.trb` files of the
  repository (`cargo test` in `bootstrap/`), until the original is retired.
- The plan and the state are in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).
