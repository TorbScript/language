# The TorbScript Toolchain

TorbScript, written in TorbScript, compiled by itself. There is no Rust and no cargo: `sh tools/bootstrap.sh` carries
a seed binary through itself to a fixpoint and writes `build/release/torb(.exe)`, and every command below is run from
the repository root:

```text
torb check .
torb build tests/conformance/arithmetic.trb
torb run examples/tour
torb test compiler/tests
torb ir examples/tour/src/01-bindings-and-values.trb
torb parse compiler std examples
torb ast examples/tour/src/01-bindings-and-values.trb
torb tokens examples/tour/src/01-bindings-and-values.trb
torb docs check docs
torb natives --header
torb highlight examples/tour/src/01-bindings-and-values.trb
torb canon --check .
```

`torb check <path>...` takes a workspace root (then all of its members), a single project, or one script file. Every
path around it is loaded too - that is where the imports and the prelude are - but only what was asked for is
reported. `--statistics` adds how many expressions of every module have a type and how many wait for a construct the
back end does not lower yet.

`torb build [path] [--profile dev|release] [--release] [--emit-c] [--output <file>]` compiles the entry file it was
given to a native binary: it checks, lowers it to the typed IR, writes one C11 translation unit and hands it plus
`runtime/*.c` to the first C compiler it finds (`$TORB_CC`, `clang`, `gcc`, `cc`, `cl`). The C goes next to the binary,
which is `<project>/build/<profile>/<entry file>` unless `--output` says otherwise, and `--emit-c` stops after
writing it - which needs no C compiler at all. A construct the back end does not compile yet is reported by name, in
one sentence built in `ir/unsupported.trb`, and nothing is written; a C compiler error is an internal error of the
compiler and keeps the `.c` file. `torb run <path> [...]` builds a file or a project into a cache under `build/run/`
and executes it, everything after the path being the program's own `Process.arguments()`; `torb test <path>...`
builds every `*.test.trb` below the paths into one binary and runs it. Both default to the `dev` profile (`-O1`);
`build` defaults to `release` (`-O2`).

```text
src/
├ main.trb              The command line: one match over Process.arguments(), one function per subcommand above it
├ cli/
│ ├ files.trb           Collecting source files
│ ├ build.trb           `torb build`: check, lower, emit C, find a C compiler, compile
│ ├ run.trb             `torb run`: a program compiled into a cache and executed, with its three streams passed through
│ ├ test.trb            `torb test`: every `*.test.trb` below the given paths, as one native binary, built and run
│ └ render.trb          A Diagnostic as the text the command line prints, with the source line and a caret under it
├ canon/                `torb canon`: the formatter canon, over the syntax tree, with a safety net that drops any
│ │                      edit that would change what the program means
│ ├ command.trb         The command itself, and the safety net
│ ├ walk.trb            One walk that collects what every rule below needs
│ ├ calls.trb           Rule `calls`: a call is a command wherever the grammar allows it
│ ├ strings.trb         Rule `strings`: a multi-line string is written in the indented form
│ ├ patterns.trb        Rule `imported-case-patterns` (off by default)
│ ├ bindings.trb        Rule `unused-bindings` (off by default)
│ ├ loops.trb           Rule `loops`: `while true {` becomes `loop {` (off by default)
│ └ edit.trb            Text replacements in the coordinates of the original source
├ documentation/        `torb docs check|index|skill|bundle|source`: the commands that keep `docs/` true
│ ├ command.trb         The `docs` subcommand family
│ ├ tree.trb            The documentation as one value: Page, Folder, Documentation
│ ├ markdown.trb        The reader: front matter, headings, fenced code blocks, links
│ ├ schema.trb          The front matter schema, the section order of every kind of page
│ ├ links.trb           Where a link may lead, and the shape the tree has to have
│ ├ index.trb           `torb docs index`: the generated body of every index.md
│ ├ check.trb           `torb docs check`: every rule of `docs/contributing`, asked of the whole tree at once
│ ├ snippets.trb        Every fenced code block, verified with the compiler's own front end
│ ├ canon.trb           The part of the formatter canon a documentation snippet is checked against
│ ├ native.trb          What the native back end says about the blocks of the documentation
│ ├ skill.trb           `torb docs skill`: the documentation as an Agent Skill
│ ├ bundle.trb          `torb docs bundle`: llms.txt and llms-full.txt
│ ├ source.trb          `torb docs source`: the gate of the documentation that lives in the code
│ ├ comments.trb        The comments of a .trb file, read the way that gate judges them
│ ├ source-scope.trb    Where a link of a doc comment may point
│ └ source-examples.trb The examples of doc comments, checked with the compiler's own front end
├ highlight/            `torb highlight`: the semantic tokens of one file, as the JSON an editor extension reads
│ ├ command.trb         The command itself
│ ├ scope.trb           The global names of one file, collected before a single token is produced
│ ├ resolve.trb         One walk that decides, for every name, what it is
│ └ token.trb           One token of the result, and its modifiers
├ ir/                   The typed IR - a control flow graph of basic blocks over numbered slots - and the lowering
│ │                      from the checked syntax tree to it
│ ├ ir.trb              The typed IR itself
│ ├ build.trb           FunctionBuilder: the function under construction
│ ├ instances.trb       The monomorphization worklist: which declarations become functions, under which name
│ ├ instantiate.trb     A checker type to an IR type, under a substitution
│ ├ witness.trb         Witness tables: what a trait-typed value and a dynamic call go through
│ ├ devirtualize.trb    A trait-typed value whose payload the whole program agrees on
│ ├ capture.trb         The three layouts a closure needs
│ ├ constant.trb        The compile-time constant evaluator
│ ├ decision.trb        A match plan as a decision tree
│ ├ mangle.trb          The names a back end sees: deterministic, ASCII, collision free
│ ├ operand.trb         Every operand occurrence of an instruction
│ ├ ownership.trb       Which parameter takes the count, where the last use is, where a Move goes
│ ├ ownership-verify.trb The ownership invariants, as a safety net for every later pass
│ ├ kept.trb            Which parameters a function may keep beyond the call
│ ├ liveness.trb        Liveness over the slots a frame owns a count of
│ ├ suspension.trb      The state machine of a task: its frame, and what every stop releases
│ ├ layout.trb          Layouts and the representation classes
│ ├ element.trb         The element descriptors the one C list and the one C hash table share
│ ├ elements.trb        The element step: a write through `a[key]`
│ ├ ranges.trb          An integer operation that cannot leave the width of its type
│ ├ print.trb           The text format `torb ir`, `--emit-ir` and the snapshot tests show
│ ├ verify.trb          The IR verifier
│ ├ unsupported.trb     How both back-end halves phrase "not supported yet" - never with a plan number
│ └ lower/
│   ├ lower.trb         From a checked program to the IR, in the order the worklist finds it
│   ├ context.trb       What the lowering threads through itself
│   ├ function.trb      One function body, from the first block to the last terminator
│   ├ statement.trb     Statements, blocks and the two loops
│   ├ expression.trb    Every expression of the monomorphic part of the language
│   ├ call.trb          Calls, constructors and operators
│   ├ closure.trb       A closure becomes a function plus an environment
│   ├ task.trb          A task function becomes a resume function and a constructor; spawn, await
│   ├ generic.trb       Which instance a call reaches, and when it goes through a witness table
│   ├ match.trb         match, if const, while const, a destructuring binding, ?
│   ├ place.trb         var paths: a checker Place to an IR Reference
│   ├ collection.trb    `[1, 2, 3]` and `["a": 1]`
│   ├ derive.trb        The members no source declares, generated from the layout
│   ├ native.trb        The two conventions of `runtime/`, and the wrapper around them
│   ├ text.trb          String interpolation, print, printError
│   └ quote.trb         `assert(condition)`, the one quotation the back end builds itself
├ backend/c/            The typed IR to one portable C11 translation unit
│ ├ emit.trb            One translation unit for a whole program: types, static data, functions, main
│ ├ type.trb            Every IR type as C, and the struct declaration of every layout
│ ├ body.trb            One function body as C: slots as locals, blocks as labels and goto
│ ├ writer.trb          A small structured writer for C, and the one renderer
│ ├ emission.trb        What the emitter writes into, and the vocabulary every part shares
│ ├ prototype.trb       A runtime native's C prototype, taken apart
│ └ natives.trb         The manifest of natives: the contract between the lowering and `runtime/`
├ project/
│ ├ path.trb            Paths as text: normalize, join, relative, "is inside of"
│ ├ source-tree.trb     SourceTree: every file with its text, read once
│ ├ read.trb            The only IO of the front end: disk to SourceTree
│ ├ manifest.trb        project.trb, read statically from its syntax tree
│ ├ workspace.trb       Finding the workspace of a path, its members and their files
│ └ toolchain.trb       Finding the toolchain's own std/ and runtime/, for a program that does not carry them
├ semantics/
│ ├ module.trb          ModuleId, Module: one parsed file of one package
│ ├ symbols.trb         SymbolId, Symbol: what a name can mean, and the table of all of them
│ ├ graph.trb           Parsing every module, and what a `use "..."` path points at
│ ├ scope.trb           Exports, imports and the prelude, to a fixpoint. Program: the tables
│ ├ resolve-types.trb   Every name in a type position
│ ├ scripts.trb         Which files a project loads as receiver scripts
│ ├ check.trb           `check`: source tree in, diagnostics and tables out
│ └ checker/            The type checker
│   ├ type.trb          TypeForm, TypeId, the interning table, signatures, a type in a message
│   ├ wellknown.trb     The declarations the language itself refers to, through the exports of `std/prelude`
│   ├ context.trb       Checker: every table of the pass, the scopes, and what it resolved
│   ├ unify.trb         Equality of types and substitution of generic parameters
│   ├ lowering.trb      A TypeReference to a TypeId: names, tuples, functions, literals, const arguments
│   ├ signature.trb     Declarations to signatures and aliases, on demand, with cycle detection
│   ├ member.trb        What `a.b` and `Type.b` mean on a concrete type, and what a declaration comes with
│   ├ name.trb          Names in expressions: the lookup order and the receiver
│   ├ call.trb          Arguments, labels, defaults, variadics, spread, trailing closures
│   ├ command.trb       Property commands
│   ├ expression.trb    The type of every expression, and the operators
│   ├ closure.trb       A closure read from the type expected of it
│   ├ pattern.trb       Patterns and the names they bind
│   ├ statement.trb     Statements, blocks, bodies, definite return
│   ├ declaration.trb   Visibility and the shape of a program
│   ├ implementation.trb Traits and implementations: which type implements which trait, and through which one
│   ├ derive.trb        The implementations the language generates: Equals, Hash, Show, Encode, Decode, ...
│   ├ exhaustive.trb    Whether the arms of a match cover every value, and what is missing where they do not
│   ├ usefulness.trb    The pattern matrix and Maranget's usefulness algorithm
│   ├ escape.trb        Where a closure or a function value may go
│   ├ mutation.trb      A change nobody reads
│   ├ place.trb         What a valid place is, and whether a change may go through it
│   ├ quote.trb         Expression<Value>: a parameter that takes the expression, not its value
│   ├ receiver.trb      Receiver scripts: checking a .trb file that is the body of a receiver closure
│   ├ constant.trb      What "compile-time evaluable" means for a top-level const
│   ├ spelling.trb      How a name is spelled is a rule of the language, not a convention
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
One `*.test.trb` file per area of the front end and back end - the lexer, the parser, the project layer, the type
checker (types, calls, generics, traits, exhaustiveness, ...), the IR and its lowering (one file per `ir/lower/*.trb`
roughly), ownership, the C back end, the natives manifest, `torb canon`, `torb highlight`, and the documentation
commands. 62 files today, collected and run as one binary by `torb test compiler/tests`.
```

Rules for this code:

- It is ordinary, idiomatic TorbScript, in the style [CONTRIBUTING.md](CONTRIBUTING.md) sets out. Nothing is written
  in a special way for the compiler that compiles it.
- **IO happens at the edge.** `project/read.trb` reads every file that could matter into a `SourceTree`, and
  everything after that - workspaces, the module graph, symbols, name resolution - is a pure function of that value.
  So the tests build whole projects in memory, and a language server can hand in text that is newer than the disk.
- **Program-wide things are ids, not references.** Values have no identity in TorbScript, so modules and symbols live
  in lists and are referred to by `ModuleId` and `SymbolId`.
- **A message a user sees carries no number of this repository's plan.** Which round or gap will build a construct is
  a fact about the plan, not about the program in front of the reader, and a plan that moves leaves the number wrong -
  see `ir/unsupported.trb`.
- The front end is held to the whole repository: `torb parse .` accepts every `.trb` file of it, and the tests of
  `compiler/tests` pin the tokens, the tree and every diagnostic.
- The plan and the state are in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).
