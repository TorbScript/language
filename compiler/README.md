# The TorbScript Toolchain

TorbScript, written in TorbScript. Until it compiles itself it is run by the bootstrap interpreter:

```text
cd ../bootstrap
cargo run --release -- run ../compiler parse ../compiler ../std ../examples
cargo run --release -- run ../compiler ast ../examples/tour/src/01-bindings-and-values.trb
cargo run --release -- test ../compiler/tests
```

```text
src/
├ main.trb              Command line: parse, ast, tokens
├ cli/
│ ├ files.trb           Collecting source files
│ └ render.trb          Diagnostics as text
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
└ parser.test.trb
```

Rules for this code:

- It is ordinary, idiomatic TorbScript. Nothing is written in a special way for the bootstrap; what the bootstrap
  cannot do is simply not used yet (the list is in [bootstrap/README.md](../bootstrap/README.md)).
- Every part that is a port of `bootstrap/crates/torb-syntax` is tested against it on all `.trb` files of the
  repository (`cargo test` in `bootstrap/`), until the original is retired.
- The plan and the state are in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).
