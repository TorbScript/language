# TorbScript

**One language, every layer.** A single-language ecosystem.

A functional-first, multi-paradigm scripting language with value semantics. It runs interpreted and compiles to
native executables, and it is its own configuration format.

- [CONCEPT.md](CONCEPT.md) - the language
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - how it is implemented, and the state of it
- [examples/tour](examples/tour) - the language in thirteen files

| Directory    | Contains                                                                      |
|--------------|-------------------------------------------------------------------------------|
| `compiler/`  | The toolchain, written in TorbScript                                          |
| `std/`       | The standard library, one package per directory (`std/core`, `std/fs`, ...; `std/prelude` re-exports) |
| `runtime/`   | The C runtime every compiled binary links against ([runtime/README.md](runtime/README.md)) |
| `examples/`  | Tour and example projects                                                     |
| `tests/`     | The conformance suite and the language smoke programs ([tests/conformance/README.md](tests/conformance/README.md)) |
| `brand/`     | The logo, the icons and the design tokens ([brand/README.md](brand/README.md), [docs/design/BRAND.md](docs/design/BRAND.md)) |

## Building

The compiler is written in TorbScript and compiles itself, so a checkout needs two things: a **C compiler**
(`$TORB_CC`, or `clang`, `gcc`, `cc` in that order) and a **seed** - a `torb` that already exists. The seed is not in
git, because it is a build artifact of an earlier commit rather than a fact about this one.

```text
seed/torb[.exe]     a binary for this platform: a release download, or one built by a `torb` you already have
seed/program.c      the portable seed: the compiler as one C file, which builds anywhere a C compiler does
```

Put one of the two in place and build:

```text
sh tools/bootstrap.sh          # seed -> build/bootstrap/torb -> build/release/torb
```

Two steps and not one: the seed compiles the current sources, that binary compiles them again, and the two `program.c`
are compared byte for byte. `build/release/torb` is the compiler that comes out.

A release publishes both artifacts, so a checkout downloads the binary for its platform, or `program.c` for a platform
that has none. Between releases the seed is whatever `torb` was built last:

```text
torb build ./compiler --output ./seed/torb           # a binary seed for this platform
torb build ./compiler --emit-c --output ./seed/torb  # seed/program.c, the portable one
```

Once `build/release/torb` is there:

```text
sh tools/gates.sh a            # the gates of every round (compiler/CONTRIBUTING.md)
sh runtime/build.sh            # the C runtime and its own tests
```
