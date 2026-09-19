# TorbScript

A functional-first, multi-paradigm scripting language with value semantics. It runs interpreted and compiles to
native executables, and it is its own configuration format.

- [CONCEPT.md](CONCEPT.md) - the language
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - how it is implemented, and the state of it
- [examples/tour](examples/tour) - the language in twelve files

| Directory    | Contains                                                                      |
|--------------|-------------------------------------------------------------------------------|
| `compiler/`  | The toolchain, written in TorbScript                                          |
| `std/`       | The standard library, one package per directory (`std/prelude`, `std/fs`, ...) |
| `runtime/`   | The C runtime every compiled binary links against ([runtime/README.md](runtime/README.md)) |
| `examples/`  | Tour and example projects                                                     |
| `bootstrap/` | Temporary: a Rust interpreter that runs `compiler/` until it compiles itself  |

```text
cd bootstrap
cargo test                                                     # Everything, including the TorbScript tests
cargo run --release -- run ../compiler tokens ../compiler/src/main.trb
cd .. && sh runtime/build.sh                                   # The C runtime and its own tests
```
