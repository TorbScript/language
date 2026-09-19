# Bootstrap (Stage 0)

A parser and a tree-walking interpreter for TorbScript, written in Rust. It exists to run the real toolchain in
[`../compiler`](../compiler) until that toolchain can compile itself, and is thrown away afterwards
(see [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)).

```text
cargo run --release -- run ../compiler tokens some-file.trb   # Run the compiler (src/main.trb of the project)
cargo run --release -- run ../compiler build some-file.trb    # A native binary, through C (needs a C compiler)
cargo run --release -- run script.trb [arguments]             # Run a single file
cargo run --release -- test ../compiler/tests                 # Run *.test.trb files
cargo run --release -- parse ..                               # Check the syntax of every .trb file
cargo run --release -- tokens file.trb                        # Tokens, in the format of compiler/src/syntax/dump.trb
cargo run --release -- ast file.trb                           # Syntax tree, as the generated Show of compiler/src/syntax/ast.trb
cargo test                                                    # Includes the differential tests against compiler/
```

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
| `.Case` where the type is expected                    | In patterns always. In expressions where the value arrives at an annotation, a parameter, a field, a result, an assignment or `==`. Inside a list, a tuple or `Some(...)` on either side of `==`/`!=`, resolved element-wise against the value at the same position on the other side; an implicit case with no counterpart to resolve against is a runtime error. Elsewhere: `TokenKind.Dot` |
| `value.into()`, `to<Target>()`, `Json.decode<T>()`    | Not available (they are chosen by the expected type). `Target.from(value)` works |
| `?` converts errors through `From`                    | By the declared return type of the function: a `from` for the type of the error, or a case that wraps it |
| Implicit closure parameters named by the function type | For declared functions with an inline function type. Native methods know `_`, `_2` only |
| Lazy pipelines                                        | Stages are eager and return lists. No infinite sources except `for x in 0..` |
| User-defined `equals`/`hash`                          | `==` and map keys are always structural                          |
| Exclusivity, dead changes, exhaustiveness, visibility | Not checked. `const`, fields without `var` and temporaries are checked when a change reaches them |
| All integer types                                     | One 64 bit integer. Overflow panics                              |
| `shared type`, tasks, sandbox, `foreign`, `Expression<Value>` trees | Not available. `assert` shows the source of its condition and the values of both sides |
| `std/`, packages, workspaces                          | Not loaded. `natives.rs` and `prelude.trb` implement what the compiler uses, imports of packages are ignored |
| Visibility of `extend`                                | Every `extend` of every loaded module applies everywhere |

The compiler sources stay inside of what stage 0 and the language agree on. If the compiler needs something that is
missing here, it is added here - and nothing else is. Three natives arrived that way with `torb build` (milestone 5.3),
all three declared in `std/` and in the manifest of natives: `Process.run` (the driver runs a C compiler with it and has
to tell "there is no such program" from "it said no"), `File.createDirectory` (`mkdir -p` for the directory the C and
the binary go into) and `Environment.get` (`$TORB_CC`, `$TORB_RUNTIME`). `ProcessOutput` is in `prelude.trb`, with the
field order `std/process` declares - the natives build it positionally.

`bootstrap/tests/native/` holds the programs of the end-to-end test of the C back end
(`crates/torb-cli/tests/native.rs`): every one of them is compiled to a native binary, run, and compared with itself on
stage 0. It is a workspace of its own whose only member is `std/`, so `torb check ..` over the repository does not look
at it, and it is the seed of the conformance runner of milestone 5.14.
