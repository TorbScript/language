# Roadmap

**Status: planned** — the milestones that are still ahead, each with its goal and the design records that specify it.
Milestones 1 to 6 are done ([ARCHITECTURE.md](ARCHITECTURE.md), "Milestones"): the compiler is written in TorbScript,
compiles itself, and builds native programs through C. Nothing here has a date; a milestone is finished when its gates
are green.

The order is the order of dependence. Milestones 7 and 8 are the core, and the public release of
[design/RELEASE.md](design/RELEASE.md) is gated by them. Milestones 9 and 10 are additions a minor release may bring,
so neither blocks 1.0. Beside the milestones, a few packages and language features are designed and wait for a slot;
they are listed after the milestones.

- **[Milestone 7: the VM, tasks and the sandbox](#milestone-7-the-vm-tasks-and-the-sandbox)**
- **[Milestone 8: formatter, linter, language server, package manager](#milestone-8-formatter-linter-language-server-package-manager)**
- **[The kernel of natives](#the-kernel-of-natives)**
- **[Milestone 9: JavaScript and PHP](#milestone-9-javascript-and-php)**
- **[Milestone 10: the breadth of the standard library](#milestone-10-the-breadth-of-the-standard-library)**
- **[Packages designed outside the milestones](#packages-designed-outside-the-milestones)**
- **[Language features designed and not built](#language-features-designed-and-not-built)**
- **[Smaller open points](#smaller-open-points)**

## Milestone 7: the VM, tasks and the sandbox

**Goal: the same program runs interpreted and compiled, with the same output, and `torb run` needs no C compiler.**
The bytecode VM interprets the same IR the C back end compiles, and the conformance suite holds both to one behaviour.
**The core of the milestone is done:** every conformance program runs in both back ends with the same output, exit
code, panics and leak count, `torb run` and `torb test` run in the VM by default and `torb build` stays native. Tasks,
channels and the worker pool run in both; the streams of `File`, of the three standard streams and of `Process.start`
are built on the blocking pool beside the IO poller of sockets; a program the VM runs loads receiver scripts from
paths only known while it runs; `torb build --embed-vm` builds a native binary that embeds the VM; and the REPL has
every slice of REPL.md. **What is left around it:** compiled code that loads a script with the value encoded across
(SCRIPTS.md slice 8), the toolchain reading an evaluated manifest (SCRIPTS.md slice 5), the pool's manifest setting
and the copy of a closure's environment and of a variant at a crossing (CONCURRENCY.md), and the poller for the pipes
of a POSIX child.

- [design/VM.md](design/VM.md) section 9 - slices 4 to 8
- [BACKEND.md](BACKEND.md) section 5 - rows 7.1 to 7.7
- [design/CONCURRENCY.md](design/CONCURRENCY.md), [design/STREAMS.md](design/STREAMS.md) section 14,
  [design/SCRIPTS.md](design/SCRIPTS.md), [design/REPL.md](design/REPL.md)

## Milestone 8: formatter, linter, language server, package manager

**Goal: the tools a user expects around a language that is meant to be used.**

- **`torb format`** replaces `torb canon`, which enforces the canon today and says it is temporary
  ([tooling/torb-format.md](tooling/torb-format.md)).
- **`torb lint`**, with `--fix` as the migration tool for every breaking change before 1.0 and for every deprecation
  after it. The rules named so far are on [tooling/torb-lint.md](tooling/torb-lint.md).
- **The language server is the compiler**: `check` incremental per file, and the checker's tables answer hover,
  go to definition, completion and diagnostics. Its semantic tokens replace `torb highlight`, and a lint fix is its
  quick fix. The VS Code extension in `.vscode/extensions/torbscript` gets the client.
- **The package manager**: the manifest that is evaluated, the lock file, `add`, `remove`, `update`, `publish`, and
  the registry at packages.torb.dev ([design/PROJECT.md](design/PROJECT.md) section 12,
  [design/RELEASE.md](design/RELEASE.md) section 7). It needs an HTTP client with TLS, and TLS comes from the platform:
  SChannel on Windows, the system's stack on macOS, OpenSSL where Linux has it.
- **`torb doc`**, which renders doc comments ([tooling/torb-doc.md](tooling/torb-doc.md)).
- **The natives that are declared and planned**: `Decimal`, `Float32` arithmetic, the full Unicode tables, and
  `get`, `post` and `request` of `std/http`.

`std/yaml` and `std/markdown` come before it, because `torb doc` and the hover of the language server render
Markdown ([design/TEXT-FORMATS.md](design/TEXT-FORMATS.md)).

## The kernel of natives

**Goal: a new back end starts from a short, fixed list of IR intrinsics, and everything else in `std` is TorbScript.**
Every `native` of `std/` is work for every back end, so before milestone 9 the manifest of natives
(`compiler/src/backend/c/natives.trb`) is audited and reduced to a kernel: integer and float arithmetic, comparisons,
raw storage, bytes to numbers. The body of a `native fn` becomes IR, and `native { ... }` a block of IR inside an
ordinary function (CONCEPT, "Foreign Functions"), so `std/` itself says which operation `Int64.add` is. `Buffer<Item>`
joins `Array` in `std/core` as the heap storage primitive, and `ArrayList`, the hash tables and the tries are rebuilt
on it in TorbScript ([design/COLLECTIONS.md](design/COLLECTIONS.md), gap 6). A back end may still replace an ordinary
function by something faster; that is its private business.

The performance work that waits for a second back end belongs here too: steps P9 to P12, an allocation budget per
conformance program as a gate, and `torb build --explain-copies` ([PERFORMANCE.md](PERFORMANCE.md)).

## Milestone 9: JavaScript and PHP

**Goal: TorbScript compiles to JavaScript, so the front end of an interactive application is written in the same
language as its server, and to PHP, so a program runs on a classic shared web host.** Both keep the language's
semantics rather than the host's: fixed integer widths, and reference counting where `close()` has to run at a known
line. Tasks become promises in JavaScript and run on an event loop in PHP that uses what the host has. A capability
table per target says which package is real, simulated or a compile error at its import, and packages for npm and
Composer are generated where the public interface allows it.

- [design/JAVASCRIPT-AND-PHP.md](design/JAVASCRIPT-AND-PHP.md)
- [ARCHITECTURE.md](ARCHITECTURE.md), "Back Ends"; [design/DESTRUCTORS.md](design/DESTRUCTORS.md) section 2a

## Milestone 10: the breadth of the standard library

**Goal: a standard library on the level of a platform, as separate packages of `std`, each of them pure TorbScript on
the natives that exist.** In this order, because each part stands on the one before it:

1. **File systems.** `std/fs` stays the native driver on `Path`. Above it, a storage layer chooses a driver by the
   scheme of a URI - local files, memory for tests, then S3 and WebDAV as packages of their own - and `std/fs` gains
   `remove` and `rename` ([design/URI.md](design/URI.md) sections 11 and 14, [design/PATH.md](design/PATH.md)).
   Connections to databases and caches are URIs whose scheme chooses the driver in the same way.
2. **The network.** `std/network` (sockets), then `std/http` (client and server, TLS through the platform), then gRPC
   and OpenAPI on top of it. `std/uri` is finished before `std/http` grows further (the owner, 2026-09-25): the URI
   types, IRIs, URNs, templates and the pure address package `std/ip` come first
   ([design/URI.md](design/URI.md) section 14). The web layer above HTTP - handlers, routes, HTML and a live UI - is
   [design/WEB.md](design/WEB.md).
3. **Formats**, as packages on `Encode` and `Decode` ([design/ENCODING.md](design/ENCODING.md) section 9): `json`
   exists, then `toml`, `xml` and `html` (the document formats, each with its tree), JSON Schema and JSON Patch, in the
   versions of the table below. `yaml`, `regex` and `markdown` come earlier
   ([design/TEXT-FORMATS.md](design/TEXT-FORMATS.md)).
4. **Computation and machine learning** on `std/linear` and `std/geometry`, which exist
   ([design/LINEAR.md](design/LINEAR.md)): `std/tensor`, `std/gradient`, `std/gpu` and `std/learning`
   ([design/COMPUTE.md](design/COMPUTE.md)).
5. **The application framework**: the scope of Spring Boot or Symfony, wired at compile time through traits,
   constructors and a module DSL instead of annotations and reflection ([design/FRAMEWORK.md](design/FRAMEWORK.md)).
6. **Language models and agents**: `std/language-model` with clients for the important providers over one streamed
   protocol of messages and threads, and `std/agent`, an agent framework after the model of pi
   ([design/LANGUAGE-MODELS.md](design/LANGUAGE-MODELS.md)).
7. **The engine packages**, last: `std/ecs`, `std/scene`, `std/transform`, `std/collision`, `std/animation`,
   `std/render` and `std/input`, independent of any one graphics interface ([design/ECS.md](design/ECS.md)). They
   share `std/linear`, `std/geometry` and `std/gpu` with the computation of part 4.

### Which versions a format supports

**A format package reads its current version and every earlier version that a large share of the documents in
circulation still uses, and writes the current version unless the caller asks for an earlier one** (the consumer of a
document is often older than its producer). The versions of one format share one model: an earlier version is read
into the model of the current one, not into a second set of types. A version that is no longer written is not
supported, and neither is a dialect with its own name.

| Format | Supported | Not supported |
|---|---|---|
| YAML | 1.2, 1.1 ([design/TEXT-FORMATS.md](design/TEXT-FORMATS.md)) | 1.0 |
| JSON | RFC 8259, and JSON Lines as a stream of values | JSON5, HJSON |
| TOML | 1.0 and every later 1.x | the 0.x drafts |
| XML | 1.0 (fifth edition) with namespaces; a DTD's entities are read, an external entity is never fetched | 1.1, validation against a DTD |
| HTML | the parsing algorithm of the WHATWG standard, which also reads older documents | a separate HTML 4 or XHTML parser (XHTML is XML) |
| JSON Schema | 2020-12, 2019-09, draft 07 (draft 06 is read as 07), draft 04 (the dialect of OpenAPI 3.0 and Swagger 2.0) | draft 03 and older |
| OpenAPI | 3.2, 3.1, 3.0; Swagger 2.0 is read into the 3.x model | writing Swagger 2.0 |
| JSON Patch | RFC 6902, and JSON Merge Patch (RFC 7396) | |
| Protocol Buffers | proto3, proto2 and the editions | |
| Markdown | CommonMark with the tables of GitHub | other flavours |

## Packages designed outside the milestones

| Package | What it is | Record | Waits for |
|---|---|---|---|
| `std/yaml` | built: YAML 1.2 and 1.1 in full, anchors, aliases, tags and comments in the tree, the target type resolves scalars, a schema is an option; all 402 cases of the YAML test suite pass | [design/TEXT-FORMATS.md](design/TEXT-FORMATS.md) section 1a | - |
| `std/regex` | built: a `Regex` with RE2's syntax and linear time, groups by name that decode into a type, replace and split; RE2's search tests pass; next: the literal the compiler checks | [design/TEXT-FORMATS.md](design/TEXT-FORMATS.md) section 2a | the literal waits for the checker's parameter kind |
| `std/markdown` | built: CommonMark 0.31.2 with GitHub's tables and front matter, the document tree as a value with the lines of its blocks and links, HTML and a writer that round trips; all 652 examples of the specification pass | [design/TEXT-FORMATS.md](design/TEXT-FORMATS.md) section 3a, [design/RELEASE.md](design/RELEASE.md) slice 1 | - |
| `std/random` | a seedable, splittable generator that is a value | [design/RANDOM.md](design/RANDOM.md) | nothing |
| `std/identifier` | `Uuid`, `Ulid`, the trait `Identifier` | [design/URI.md](design/URI.md) section 12 | `std/random` |
| `std/uri` | built: `Uri` and `UriReference` after RFC 3986, IRIs (RFC 3987), `Urn` (RFC 8141), `UriTemplate` (RFC 6570), the `file:` bridge, `std/ip`, `std/http` on `Uri`; next: IDNA, `data:` and the literal rule | [design/URI.md](design/URI.md) section 14 | nothing; the literal rule waits for the checker's parameter kind |
| `std/cli` | commands, flags and options declared once, help and errors generated | [design/CLI.md](design/CLI.md) | nothing |
| `std/os`, `std/path` | the rest of their slices | [design/OS.md](design/OS.md), [design/PATH.md](design/PATH.md) | - |
| resources | files a program ships or embeds, named by a literal | [design/RESOURCES.md](design/RESOURCES.md) | `std/uri` |

## Language features designed and not built

| Feature | Record |
|---|---|
| Cases with a fixed value, and `Flags<Case>` over a bit mask | [design/FLAGS.md](design/FLAGS.md) |
| Loops as expressions | [design/LOOPS.md](design/LOOPS.md) |
| One principle for where a program may panic | [design/PANICS.md](design/PANICS.md) |
| `for var`, the words per kind of collection | [design/COLLECTIONS.md](design/COLLECTIONS.md) |
| `?` return traces in the debug profile | [BACKEND.md](BACKEND.md) row 5.13, CONCEPT "Error Handling" |
| `deprecated`, a public enum that may grow, the prelude rule | [design/RELEASE.md](design/RELEASE.md) section 2, the 1.0 list |
| The candidates without a decision: let-else, variadic type parameters, `_` as a type argument, fields on one line, and others | CONCEPT, "Open Questions" |

## Smaller open points

Each of these is small and has no record of its own.

- **`Show` of a function value** prints `<function>`. The spelling chosen for it is the source form of its type,
  `(Int64) => Int64`, and it is not built.
- **A type alias in a diagnostic** is shown by what it stands for; showing the name that was written, with what it
  stands for beside it (`EntityId (Int64)`), is not built.
- **No signature can say that a closure runs as a task**, so every closure may call `await()`
  ([TYPECHECKER.md](TYPECHECKER.md), gap 58). The spelling belongs to the design of tasks in the VM.
- **The spelling of a NaN**: the runtime prints `nan`; whether `Show` of a `Float64` says `nan` or `NaN` is not
  decided.
- **`Fixed.arcSine` panics outside `[-1, 1]`**, where `Float64.arcSine` answers `nan`: the two implementations of
  `Real` disagree there.
- **Index-free helpers on lists**: `zip` and `indexed` exist, `windows`, `chunks` and `splitAt` do not.
