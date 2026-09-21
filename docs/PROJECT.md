# The Project File

**The names of the files say what a package produces, and `project.trb` says only what the names cannot.** A file
called `lib.trb` is the library, a file called `main.trb` is a program, a file called `*.test.trb` is a test — and none
of that needs a line in a manifest. What is left for the manifest is what no file name can carry: who the package is,
what it needs, and where each of those comes from. `build { input }` and `test { input }` say the first half a second
time, in a weaker language, and they go.

```text
  project.trb ──→ name, version, dependencies, where each one comes from, the workspace, the profiles
                  what no file name can say: WHO the package is and WHERE its parts come from

  src/  ──┬── lib.trb ─────────────→ the library, imported as "owner/name"
          ├── <path>.trb ──────────→ a module of it, imported as "owner/name/<path>"
          ├── main.trb ────────────→ the program named after the package
          └── <name>/main.trb ─────→ the program <name>
  tests/ ─── *.test.trb ───────────→ the test entries
                                     what no manifest should say: WHAT the package produces
```

- **[1. What the toolchain does today](#1-what-the-toolchain-does-today)** — eighteen probes, and four places where the docs promise more than the code does
- **[2. What other systems do](#2-what-other-systems-do)** — nine of them, and what we take from each
- **[3. What a package produces](#3-what-a-package-produces)** — one library, any number of programs, decided by name
- **[4. Programs](#4-programs)** — several `main`s, what they are called, what `run` and `build` do with them
- **[5. Profiles and targets](#5-profiles-and-targets)** — two words for two things, and where the binary lands
- **[6. The specifier grammar](#6-the-specifier-grammar)** — what may stand in the quotes, and what is reserved
- **[7. Where a dependency comes from](#7-where-a-dependency-comes-from)** — names in source, locations in the manifest
- **[8. Resources](#8-resources)** — a compiler-known parameter type instead of loaders
- **[9. The vocabulary](#9-the-vocabulary)** — every setting, its type, its default, and whether it is read statically
- **[10. Workspaces and the lock file](#10-workspaces-and-the-lock-file)** — what a member inherits, what the lock pins
- **[11. Migration](#11-migration)** — seven slices, each green on its own
- **[12. What this is not](#12-what-this-is-not)**
- **[13. Open](#13-open)**

This design replaces four things that exist today: `Build { target, input, output }` and `Test { input }` in
`std/project/src/lib.trb`, the `buildInput`/`testInput` fields of `compiler/src/project/manifest.trb` and the two
places that read them (`compiler/src/cli/build.trb`, `compiler/src/semantics/graph.trb`), the thirty-two
`build { input "src/lib.trb" }` blocks that the standard library writes to say "this is a library", and the sentence
in `docs/tooling/project-trb.md` that lists which settings are read. It adds two things the language does not have:
a grammar for the text after `from`, and a parameter type for a file the compiler resolves.

Every claim about what the toolchain does today comes from a probe in section 1, run on a stage 0 built in this
worktree. A claim the probes do not cover is marked as unproven where it stands.

---

## 1. What the toolchain does today

Eighteen probe packages, each a directory with a `project.trb` whose workspace reaches the standard library, checked
and built with the self-hosted compiler on stage 0
(`torb run ../compiler check <package>`, `torb run ../compiler build --emit-c <package>`).

| # | The package | Result |
|---|-------------|--------|
| 1 | `src/main.trb` and `src/other.trb`, both with top-level code | `3 files, no problems` |
| 2 | `src/main.trb` plus `src/server/main.trb`, both with top-level code | `3 files, no problems` |
| 3 | `src/main.trb` and `src/lib.trb` in one package | `3 files, no problems` |
| 4 | `build { input "src/other.trb" }` next to a `src/main.trb` | `3 files, no problems`; the build emits `#line 1 "probe/other-input/src/other.trb"` |
| 5 | `build { input "src/server/main.trb" }` | the build emits `#line 1 "probe/second-program/src/server/main.trb"` |
| 6 | no `build { }` at all, one `src/main.trb` | `2 files, no problems`; the build writes `build/release/program.c` |
| 7 | only `src/lib.trb`, no `build { }` | check passes; build: ``error: `torb build` needs one entry file. Name the file, or a project with a `build` input`` |
| 8 | `use File from "std/fs"` with an empty `dependencies` | `2 files, no problems` |
| 9 | `use greet from "probe/lib"` where `probe/lib` is a workspace member and not a dependency | ``error: The package `probe/lib` is not a dependency of `probe/app` `` |
| 10 | the same with `dependencies { runtime "probe/lib" }` | `5 files, no problems` |
| 11 | `dependencies { runtime "acme/http:^1.2.3" }` for a package that exists nowhere, never imported | `2 files, no problems` |
| 12 | `name "probe-bare"` — no owner | `2 files, no problems` |
| 13 | `use X from "https://example.test/x"` | ``error: There is no package `https:/` `` |
| 14 | `use X from "github.com/project/x"` | ``error: There is no package `github.com/project` `` |
| 15 | `use logo from "./logo.png"` next to a real `logo.png` | ``error: There is no module `./logo.png` `` |
| 16 | `use logo from "./logo.png"` next to a file named `logo.png.trb` | `3 files, no problems` — it resolves |
| 17 | `use marker from "probe/lib/main"` — another package's `src/main.trb`, which has top-level code | `6 files, no problems` — it resolves |
| 18 | `build { input "src/lib.trb" }` on a library whose `src/lib.trb` has top-level code | `5 files, no problems`; without the `build` line the same file is ``error: Top-level code is only allowed in entry files`` |

The two diagnostics in full, because their notes are what a user reads:

```text
error: There is no package `https:/`
 --> src/main.trb:1:1
  |
1 | use X from "https://example.test/x"
  | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  = A package is `owner/name`. The standard library comes with the toolchain; everything else has to be a
    dependency or a member of the workspace
```

```text
error: There is no module `./logo.png`
 --> src/main.trb:1:1
  |
1 | use logo from "./logo.png"
  | ^^^^^^^^^^^^^^^^^^^^^^^^^^
  = A path that starts with `./` or `../` is a file next to this one, without its extension
```

Two further facts, each from a probe rather than from reading the code:

- **A file outside `src/` is not a module of the package.** `use helper from "../tools/helper"` in `src/main.trb`,
  with a real `tools/helper.trb`, answers ``error: There is no module `../tools/helper` ``. The package's modules are
  `src/**` plus whatever `test { input }` names, and nothing else.
- **A workspace member pattern is relative to the root that writes it, and a pattern that reaches far enough out of
  the tree stops matching.** `members "<absolute path>/std/*"` answers ``error: The workspace member pattern ...
  matches no project``, and so does the same path written with eleven `../`. The patterns in the repository
  (`std/*`, `../../../std/*`) work; the general case is not something the probes could make work.

### Four places where the docs promise more than the code does

1. **"Nothing outside a package can name its `src/main.trb`."** `docs/language/modules-and-packages/packages.md`
   rule 2. Probe 17 imports one. Worse, that `src/main.trb` is an entry file by the manifest's own rule, so it may
   hold top-level code *and* be imported — the one combination the language forbids, reachable without a diagnostic.
2. **"Top-level code is only allowed in entry files."** Probe 18: `build { input "src/lib.trb" }` — which is what
   thirty-two of the repository's thirty-five `project.trb` files write — makes `src/lib.trb` an entry file and
   switches the rule off for the one file it exists to protect. The two rules are written in two places that do not
   agree: `isEntryFile` asks the manifest, `isLibraryModule` hardcodes `src/lib.trb`, and `isEntryFile` is asked
   first.
3. **"A name is `owner/name`."** `packages.md` rule 1 and `std/project`'s own doc comment. Probe 12: a bare name is
   accepted everywhere, and the static reader has no validation of any kind.
4. **`build { target, output }` and `test { input }`.** `torb build` hardcodes the profile name `"release"` and the
   path `<project>/build/<profile>/<name>`; `torb test` hardcodes the directory `tests`. `output` is read by nobody —
   `compiler/project.trb` writes `output "build/{target}/torb"` and the binary is not called `torb`. An interpolated
   string is not a literal, so the static reader cannot read it at all, and it ignores it silently.

Every one of the four is a consequence of the same thing: a manifest setting that says what a file name already says,
and then disagrees with it.

## 2. What other systems do

| System | Several programs in one package | One library or many | Where versions and locations live | How a non-code file gets in | What the lock pins |
|---|---|---|---|---|---|
| **Cargo** | `src/main.rs`, `src/bin/*.rs`, `[[bin]]` tables | exactly one `[lib]` | `Cargo.toml`; `git`/`path`/`registry` written on the dependency line | `include_bytes!`/`include_str!`, relative to the file; nothing lists them | name, version, source, checksum, per package |
| **Go modules** | every directory whose package is `main`, by convention `cmd/<name>/` | many — every directory is an importable package | the import path **is** the location, host-qualified; `go.mod` lists modules and versions | `//go:embed` on a variable, patterns relative to the file's directory | `go.sum`: a hash per module zip and per `go.mod` |
| **Deno** | any file with top-level code; named tasks in `deno.json` | not a package model at all | once: the URL in the import. Now: `deno.json`'s `imports` map and `jsr:`/`npm:` bare specifiers | an import attribute (`with { type: "text" }`), or a run-time read under a process permission | `deno.lock`: an integrity hash per specifier |
| **JSR** | — | one package with an `exports` map of several entry points | `deno.json`/`jsr.json`, resolved through the import map | — | — |
| **Node, bundlers** | a `bin` map in `package.json` | one package, an `exports` map | `package.json`, plus `npm:`/`file:`/`git:` protocols inside the version field | a loader chosen by file extension, configured in the bundler | name, version, resolved URL, integrity |
| **SwiftPM** | `products: [.executable(...)]`, each over targets | any number of library products | `Package.swift`, which is a Swift program | `resources:` per target, copied or processed, reached through `Bundle.module` | `Package.resolved`: a revision per package |
| **Zig** | one `b.addExecutable` per program in `build.zig` | `b.addModule` | `build.zig.zon`: a URL and a hash | `@embedFile`, relative to the file | the hashes in `build.zig.zon` |
| **Dart, pub** | `bin/<name>.dart` | one `lib/`, with `lib/src/` private by convention | `pubspec.yaml`; `git:`/`path:` on the dependency | an `assets:` list in `pubspec.yaml` | `pubspec.lock` |
| **Gleam** | `src/<name>.gleam` with a `pub fn main` | one `src/` | `gleam.toml` and a generated `manifest.toml` | files under `priv/`, copied | hashes in `manifest.toml` |

**What we take.**

- From **Cargo**: one library per package, a manifest with a closed vocabulary, and a lock file that pins a content
  hash rather than a version. All three are already CONCEPT's position.
- From **Go**: a program is a *directory with a `main`*, not a table in a manifest — and a host-qualified name that
  the host verifies instead of a registry, which is section 7's `github.com/project/x` as a *name*.
- From **Dart** and **Gleam**: the convention decides which file is a program, and the manifest stays small enough to
  read in one breath. Gleam's `gleam.toml` is the size a `project.trb` should be.
- From **Zig**: `@embedFile`'s shape — a file named by a literal that the compiler resolves. That is section 8's
  `Resource`, minus the part of Zig we must not take.
- From **Deno**: the conclusion, not the mechanism. Deno shipped URL imports in source and then built an import map
  to get the locations back out of the source files. Taking the conclusion without repeating the experiment is the
  whole of section 7.
- From **SwiftPM**: `resources:` states the honest requirement — the build must know every non-code file a program
  needs. We answer it from the call sites instead of from a second list.

**What we leave.**

- **`[[bin]]` tables, `bin` maps, `products:`** — a list in the manifest of things the directory already shows. Every
  one of them can disagree with the tree, and `build { input }` is the same mistake one program small.
- **`Package.swift` and `build.zig`** — a manifest that is a program you run. A `project.trb` is a receiver script
  that is *evaluated*, which is not the same thing: it has no IO, it is deterministic, and the toolchain reads its
  first settings without evaluating it at all (section 9). That line is the one this design must not cross.
- **An `exports` map** (JSR, Node). `"owner/name/path"` already is the surface, and `public` already decides what a
  module exports. A second list would be a second source of truth about the same thing, which is the disease of
  section 1.
- **Loaders chosen by extension** (Node, bundlers). Section 8 says why.
- **Cargo features and optional dependencies.** CONCEPT already rejected them: an optional integration is a package.
- **`git`/`path`/`npm:` written on the dependency line.** We take the information and split it into two statements,
  so that "what I need" and "where it comes from" are two lines a reviewer can read separately (section 7).

## 3. What a package produces

**A package produces at most one library and any number of programs, and which is which is decided by the name of a
file.**

- `src/lib.trb` makes the package a library. It is what `"owner/name"` resolves to, and `"owner/name/path"` is
  `src/path.trb` of it. There is no second library, because the package **name** is the import name: a second library
  would need a second name, a name is a package, and a second package in the same tree is a workspace member. This is
  Cargo's answer and not Go's, and the reason to prefer it here is that TorbScript already has the finer surface —
  `"owner/name/path"` — so the thing many-libraries buys is already bought.
- A file called `main.trb` under `src/` makes a program. `src/main.trb` is the program named after the package;
  `src/<name>/main.trb` is the program `<name>` (section 4 weighs the alternative).
- A file called `*.test.trb` is a test entry, anywhere in the package.
- Every other `.trb` file under `src/` is a module: no top-level code, importable inside the package, importable from
  outside when the package is a dependency.

**A file that may hold top-level code is never importable, and a file that is importable may never hold top-level
code.** That is one rule read in two directions, and with semantic names both directions are answerable from the file
name alone:

| The file | Top-level code | Importable |
|---|---|---|
| `src/lib.trb` | no | yes, as `"owner/name"` |
| `src/<path>.trb` | no | yes, as `"owner/name/<path>"` |
| `src/main.trb`, `src/<name>/main.trb` | yes | **no** |
| `*.test.trb` | yes | no |
| a receiver script (`project.trb`, a `Sandbox.load` target) | yes | no |
| a `.trb` file with no `project.trb` above it | yes | it is a script; there is nothing to import it |

The fifth row is already true. The third row's "no" is new and it closes probe 17: `"owner/name/main"` becomes an
error, and so does any package path whose last component is `main`. That is what makes `main.trb` semantic in both
directions rather than only in one.

**The one rule that is not local stays.** CONCEPT says top-level code is about being *imported*, not about the file
name, and the checker implements the fallback: a file nothing imports may hold top-level code. That fallback is what
makes the thirteen chapters of `examples/tour/src/` and the seventy-three conformance programs of
`bootstrap/tests/native/` legal, and neither of them is a *product* — they are scripts that happen to live in a
package. Keeping the fallback costs a whole-workspace analysis (`isImported` resolves every `use` of every module
before it can answer about one file), which an editor cannot do incrementally and which makes a file become illegal
because of an edit somewhere else. The trade is worth it, because the alternative is seventy-three directories, and
because with the semantic names the fallback is now only ever consulted for files that are not products. What changes
is that it is written down as what it is: **a script is a file, not a product.** `torb build` with no argument never
builds one.

**`build { input }` and `test { input }` disappear.** Nothing is left for them to say. What they do say today is
wrong in three of the four ways section 1 lists, and their one real use — `bootstrap/tests/native/project.trb`'s
`test { input "." }`, which pulls flat `.trb` files into the package — is served better by widening what belongs to a
package:

**A package's files are every `.trb` file below its directory that is not inside a nested package**, skipping hidden
directories, `target`, `node_modules` and `build`. That is one rule instead of two paths from the manifest, it makes
`../tools/helper` resolve (the second probe fact of section 1, which is a surprise today), and it is what a reader
already assumes when they say "the project".

## 4. Programs

`torb run` runs the only program of the package, or asks for a name. `torb run server` runs the program `server`.
`torb build` builds every program; `torb build server` builds one. `torb run <file>` and `torb build <file>` still
name a file directly, which is how a script is run and how a program that is not a product of any package is built.

```text
torb run [name] [arguments]     run the only program, or the one named
torb build [name]               build every program of the package, or the one named
torb build --output <file>      one program, somewhere else
```

The error when a package has several programs and none is named:

```text
error: `probe/shop` has three programs. Name one: `torb run server`
  = server, importer, migrate
```

**Where the extra programs live is the first open question.** Two forms, and the repository is evidence for neither,
because it contains no second program today.

**(a) `src/<name>/main.trb`.** The program `<name>` is a directory that contains a file called `main.trb`, exactly
like the package itself is a directory that contains `src/main.trb`. The two forms are the singular and the plural of
one rule, and the word `main` keeps meaning one thing. A program that grows past one file puts its own modules in
`src/<name>/`, which needs no new rule at all, because they are ordinary modules of the package that happen to sit in
a subdirectory. Against it: `src/server/` is a directory whose *name* is load-bearing only because of one file inside
it, so a reader who opens `src/` sees a program and a grouping directory as the same thing until they look inside.
Against it also: a one-file program costs a directory.

**(b) `programs/<name>.trb`.** A flat list of entry files, one file per program, outside `src/`. This is Dart's
`bin/`, Cargo's `src/bin/` and Gleam's `src/<name>.gleam`. For it: `src/` then means exactly "the modules of this
package" with no exceptions, `programs/` means exactly "the entry files", and both questions are answered by the
directory rather than by the file name — so an editor answers them from the path with no workspace analysis at all.
Against it: `src/main.trb` still exists (it is what CONCEPT promises and what the owner wants to keep), so the
package has *two* forms for a program, and they look nothing like each other. Against it also: a program that needs
files of its own has nowhere to put them except `programs/<name>/main.trb`, which is form (a) under a different
parent, so the second form does not remove the first.

**The recommendation is (a).** It adds no directory whose meaning is new, it degrades to (a)-with-more-files when a
program grows instead of changing form, and — decisively — it is what the owner's own words describe: "several
`main`s in one project". `programs/` is the better design for a package that is *nothing but* a pile of one-file
programs, and the repository has two of those; but both of them are script corpora, not products, and section 3
already classifies them.

**The compiler's binary.** `torbscript/compiler` must produce a binary called `torb`. Under the convention the
program is named after its directory, so the answer is a directory: `compiler/src/torb/main.trb`, and there is no
manifest setting at all. The cost is one file's imports — `compiler/src/main.trb` has nineteen relative imports, all
of which gain one `../`. The alternative, a `program "compiler" { output "torb" }` block, buys one file's worth of
churn and costs the whole argument of section 3, because it is `build { output }` under a new name. **Recommendation:
the directory, and no `program` block in the vocabulary.** If a program ever needs a setting that is genuinely not a
file name, that is when the block is added and not before.

**Scripts and the checker.** A `.trb` file with no `project.trb` above it is checked alone against the default
prelude, may hold top-level code, and `torb run` runs it. Nothing here changes that, and section 7 says why such a
file does not get dependencies.

## 5. Profiles and targets

`build { target "release" }` puts two different things under one word. They separate:

- A **profile** is how the same program is built: optimisation, debug information, whether a panic prints frames,
  whether integer overflow checks are elided (they are not — overflow panics in every profile, that is a rule of the
  language and not a setting). The profiles are `dev` and `release`, there is no third, and a project may set the few
  knobs each one has.
- A **target** is the machine the program is built *for*: an operating system and an architecture, and later `wasm`
  and the VM. It is not a project setting at all. A package that only works on one target says so by failing to
  compile there, which is what `native` and `foreign` already make visible.

```text
torb build --profile dev            the default
torb build --release                the shorthand for --profile release
torb build --target linux-x64       cross-compile; the default is the host
```

```trb
profile "release" {
  optimize 2
  debugInformation false
  panicFrames false
}
```

`profile` is a method on `Project` taking a name and a receiver closure, so the vocabulary does not grow a field per
profile and a fourth profile name is a diagnostic rather than a parse error. A setting a back end does not have yet
is read and ignored — that is the one place in this design where a silently-ignored setting is right, because the
back end is the thing that is incomplete, not the file.

**Where the binary lands.** `build/<profile>/<program>` for the host, and `build/<target>/<profile>/<program>` when
`--target` names something else. That is Cargo's layout exactly, for Cargo's reason: two targets must not overwrite
each other and the common path must stay short. `--output <file>` overrides it for one program.

**`build { output }` is deleted rather than kept.** It is a TorbScript string interpolated eagerly against the
receiver, so `"build/{target}/{binary}"` is evaluated once, when the file runs, with whatever `target` was at that
moment — which means it cannot express "per profile" at all without the file being evaluated once per profile. The
static reader cannot read it either: an interpolated string is not a literal, and today it is ignored without a word.
A path that is a convention plus one flag says everything it said, and says it the same way twice in a row.

## 6. The specifier grammar

Today the text after `from` is a string the resolver takes apart with `startsWith("./")` and `split("/")`, and
everything that is not one of the two shapes falls into "there is no package `<the first two segments>`". That is how
`https://example.test/x` becomes ``There is no package `https:/` ``. A grammar makes the good shapes precise and gives
every other shape a message of its own.

```text
specifier   = relative | package | scheme
relative    = ("./" | "../") component ("/" component)*
package     = owner "/" name ("/" component)*
scheme      = word ":" rest

owner       = segment | segment ("." segment)+          a dot makes it host-qualified: RESERVED
name        = segment
component   = segment
segment     = lowercase (lowercase | digit | "-")*      no dot, no "." and no ".."
word        = lowercase (lowercase | digit)*
```

- **A relative specifier is file-relative and names a module without its extension**, which is what the note already
  says. `component` forbids a dot, so `"./logo.png"` is a grammar error with its own message instead of resolving to
  `src/logo.png.trb` (probe 16) or failing as a missing module (probe 15). No module in the repository has a dot in
  its name, and reserving the dot is what leaves room for an asset import if section 8's option (b) is ever wanted.
- **A relative specifier may leave `src/`**, because section 3 widened a package to its whole directory. It may not
  leave the *package*: a `../` that climbs past the package directory is an error naming the package.
- **A package specifier is `owner/name`, optionally followed by a module path.** The path is resolved under `src/`,
  it may not contain `.` or `..`, and its last component may not be `main` — a program is not importable (section 3).
- **An owner that contains a dot is host-qualified and is reserved.** `github.com/project/x` parses as the name it
  looks like and answers a message that says what it would mean, rather than ``There is no package
  `github.com/project` ``. Section 7 says what it would take to make it resolve.
- **A `scheme:` prefix is reserved, all of it.** `file:`, `http:`, `https:`, `git:`, `npm:`, `jsr:` and anything else
  of the shape `word:`. Nothing resolves one today and the message says why, not that the package is missing:

```text
error: A module specifier cannot be a URL
 --> src/main.trb:1:1
  |
1 | use X from "https://git.acme.test/project/x"
  | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  = Source files name packages; where a package comes from belongs in project.trb:
    `dependencies { runtime "acme/x" }` and `source "acme/x", git: "https://git.acme.test/project/x"`
```

**One grammar, four readers, and each takes a subset.** This is the part that has to be written down, because the
four look alike and are not the same language:

| Where | What it accepts | Relative to |
|---|---|---|
| `use ... from "..."` | `relative` and `package` | the **file** |
| `dependencies { runtime "..." }` | `owner/name`, optionally followed by `":" requirement` — never a module path | nothing |
| `source "...", ...` | the same `owner/name` | nothing |
| `Resource` (section 8), `Sandbox.load` | a **path**, not a module specifier: `./` or `../`, **with** the extension written | the **file** at compile time, recorded as **project**-relative |

The last row is the one that surprises. `use Config from "./config"` names a module and drops the `.trb`;
`Sandbox.load<Config>("./config.trb")` names a file and writes it. They are different questions — one is about the
source tree, the other about a file the program carries — and they stay different. What the resource type does is
answer "relative to what" once, at compile time, the same way `use` does, and record the project-relative path the
run time needs. The mismatch CONCEPT documents (`use` is file-relative, `Sandbox.load` is project-relative) becomes a
difference between what an author writes and what a program carries, which is a translation the compiler performs
rather than a rule a reader has to hold.

## 7. Where a dependency comes from

**Source files name packages. `project.trb` says where each package comes from. `project.lock.trb` pins what was
actually taken.** Three alternatives, and the owner's literal wish is the second one.

**(A) Names in source, locations in the manifest.**

```trb
dependencies {
  runtime "acme/x:^1.2.3"
}
source "acme/x", git: "https://git.acme.test/project/x", revision: "v1.2.3"
```

```trb
use X from "acme/x"
```

**(B) The location in the source file**, which is what the owner wrote:
`use X from "https://git.acme.test/project/x"`.

**(C) A host-qualified name** — Go's model: `use X from "github.com/project/x"`, where `github.com/project` is an
owner whose namespace the *host* verifies instead of a registry, and `dependencies` still lists the requirement
centrally.

| | (A) locations in the manifest | (B) locations in source | (C) host-qualified names |
|---|---|---|---|
| Reproducibility | one requirement per package, in one file | two files can name two versions of one package; nothing makes them agree | one requirement per package, in one file |
| Reviewing an update | one diff in `project.trb` and `project.lock.trb` | a grep across every source file | the same as (A) |
| Offline builds | the lock plus a cache answers it | "what do I have to vendor" is a source-wide query | the same as (A) |
| A fetch at compile time | never: resolution reads the lock | the **checker** resolves a URL, so the type checker needs the network | never |
| Where the location is visible | in one file, next to the requirement | on the import line, where it is used | in the name, everywhere it is used |
| Namespace trust | the registry binding, which is what stops dependency confusion | whoever holds the host name | the host, which is a weaker promise than a registry but a real one |
| Sandbox capabilities | unaffected — they are granted at the call site | unaffected | unaffected |

**The decisive argument against (B) is not reproducibility, it is the fetch.** `use` is resolved by the *checker*.
If a specifier can be a location, then type checking a file can open a socket. That breaks three things CONCEPT says
in three different places: "Installing never runs code", "`project.trb` is evaluated in a sandbox without IO, so
reading the metadata of a package is safe", and "the compiler knows from the imports alone what a package can touch".
The last one is the worst: the promise is that a program's capabilities are readable from its imports, and a
compile-time fetch adds a capability to the *compiler* that no import declares. Deno is the experiment that was
already run — URL imports in source, then an import map in `deno.json` and `jsr:`/`npm:` bare specifiers to get the
locations back out of the files. Taking the result without repeating the experiment costs nothing.

**What (B) buys and (A) loses, honestly:** you can read where a dependency comes from on the line that uses it, and a
one-file program can depend on something without a second file. Both are real. The first is answered by the fact that
a project has one `project.trb` and a reader opens it once; the second is answered below.

**Recommendation: (A), with (C) available as a name.** An owner may be host-qualified, which means "this namespace is
verified by the host rather than by a registry"; the requirement is still listed in `dependencies`, and the lock still
pins a content hash. Then the owner's wish is spelled with two statements instead of one, and the thing that is lost
— seeing the location on the import line — is exactly the thing that made Deno add a file to look it up in.

**A script has no project file, and should not get dependencies through the back door.** The coordinator's position
is that a script may name its requirement where it uses it. Against: that is (B) in miniature, with the same fetch
during `torb run`, and it creates the split Deno had — one rule inside a project, another outside it. **A script gets
the standard library and nothing else.** The moment it needs a dependency it needs a project, which is one `torb init`
away. If that is ever too strict, the spelling should be a *manifest in the file* and not a location in a `use` — a
`project { }` header at the top of the script, in the same vocabulary, which is what Python's inline script metadata
settled on and what keeps one rule for where a requirement is written.

**Sources, spelled out.** One `source` line per package, and a package with none comes from the registry its owner is
bound to:

```trb
registry "acme", url: "https://packages.acme.test"

source "acme/x", git: "https://git.acme.test/project/x", revision: "v1.2.3"
source "acme/y", path: "../y"
source "acme/z", archive: "https://files.acme.test/z-1.0.tar.gz", hash: "..."
```

A `source` overrides where a name resolves and never what it is called, so replacing a registry package with a local
checkout during development touches one line and no source file. `revision` is required for `git:` — a branch name
is not a version — and `hash` is required for `archive:`, because there is no registry to ask.

## 8. Resources

After `from` there is always a module. An asset has no names to pick, so `use logo from "./logo.png"` is a different
construct that happens to share a keyword. Three ways to do it.

### (a) A parameter type the compiler knows

```trb
/**
 * A file of the project, named at a call site by a string literal the compiler resolves. The build knows every one
 * of them, so it can embed them, ship them next to the binary, and watch them.
 */
public native type Resource {
  /** Where the file is, relative to the project directory, always with `/`. */
  path: String
}
```

A parameter declared `resource: Resource` accepts **only a string literal**. The compiler resolves it against the
directory of the file that writes it, reports a missing file at that line, and records it. Everything else is a
compile error:

```text
error: A `Resource` is a file named by a literal, so the compiler can find it
 --> src/main.trb:7:26
  |
7 | const script = Sandbox.load<Config>(chosen)
  |                                     ^^^^^^
  = A path computed at run time is a `Path`, and reading one needs a capability
```

This is `lazy` and `Expression<Value>` one more time: the signature says how the argument is read, there is no second
syntax, and nothing about it is meta-programming. CONCEPT's rule is already the rule — "a call has one signature, and
that signature says how its arguments are read".

```trb
use Sandbox from "std/sandbox"
use embeddedText from "std/resource"

const script = Sandbox.load<ServerConfig>("./config.trb")?
const schema = embeddedText("./schema.sql")
const logo = Image.load("./logo.png")?
```

- **What it buys.** A missing file is a compile error at the line that names it. The build knows every file a program
  needs, which is what watch mode, packaging and a reproducible `torb build` all want and none of them can get today.
  "Relative to what" has one answer — the file, like `use` — and the project-relative path the run time needs is
  recorded rather than written.
- **Embedded or shipped.** A resource is *shipped* next to the binary by default and *embedded* when the function
  that reads it is one of the embedding ones (`embeddedText`, `embeddedBytes`, both `native`, both folded to a
  constant). Which of the two a file gets is decided by the function it is passed to, which is the same principle one
  level down.
- **The on-demand form the owner asked for is a second member, not an overload**, because there is no overloading:
  `Sandbox.load` takes a `Resource` and the file is part of the program; a sibling takes a `Path` and reads it at run
  time under a `files` capability of the host. Naming that sibling is `std/sandbox`'s business; the requirement is
  that both exist and that the compile-time one is the short name.
- **What it needs.** `Resource` as a parameter kind is checker work plus a constant in the back end, and nothing
  else — it can exist before the VM does. `Sandbox.load` needs the VM. `Image.load` needs a graphics package that
  does not exist. So the type can land long before any of its interesting callers.

### (b) An import that binds a value, with loaders

`use logo from "./logo.png"`, with a table of loaders in the project file or in a semantic file
(`trb/loaders.trb`), each loader a pure function run at build time in the sandbox, whose result is embedded — and
because a value is its constructor call, the encoding layer is the embedding format, which is genuinely elegant.

It is also, honestly, three of CONCEPT's non-goals at once:

- **It is a build script.** "Installing never runs code. There are no install scripts and no build scripts." A loader
  is milder than a `build.rs` — it is TorbScript, sandboxed, with one file as its input — but the shape is the same:
  the build runs code the package chose, and the program's output depends on it.
- **It makes an import run something.** "Nothing runs when a module is imported" is what makes cyclic imports free
  and what lets the checker compute exports to a fixpoint. If a loader produces the exports, an import's *names* are
  a function of running user code.
- **It needs the checker to know the type of `logo` before the loader has run**, so either the loader declares types
  — which is meta-programming, and "no AST macros, no annotations" — or the checker evaluates arbitrary user code,
  which is CONCEPT's "one possible future step: compile-time functions that produce types", listed as a future and
  not as a v1 feature.

And it has the failure mode Node and every bundler demonstrate: `import x from "./a.svg"` means a URL, a string, a
React component or a data URI depending on a configuration file nobody reads, and the same source file means
different things in two builds.

### (c) Both, layered

(a) now, (b) never unless compile-time functions arrive for a reason of their own — at which point a loader is a
compile-time function and needs no import syntax at all, because `Image.load("./logo.png")` already is the loader,
chosen by the caller instead of by an extension table.

**Recommendation: (a).** It gives the build the list of files it needs, which is the real requirement behind "there
must be an on-demand syntax", and it costs the language one parameter kind and no new construct. `Resource` is the
name: it is a full word, it is free in `std`, and it says what the value is rather than how it is spelled
(`ModuleUri` says the second and is not a module).

## 9. The vocabulary

Every setting of `project.trb`. **Static** means the toolchain reads it from the syntax tree without evaluating the
file, which is what stage 0 does today, what an editor wants before anything is checked, and what a registry wants
without running anything.

| Setting | Type | Default | Static | Error |
|---|---|---|---|---|
| `toolchain "0.1"` | `String` | none | **yes** | a toolchain older than this refuses the project, and says so before reading anything else |
| `name "owner/name"` | `String` | none | **yes** | missing, not `owner/name`, or not a plain string |
| `version "1.4.0"` | `String` | the workspace root's | **yes** | not a version; required to publish |
| `prelude "std/prelude"` | `String` | `"std/prelude"` | **yes** | not a package specifier; the package must exist |
| `dependencies { runtime "..." }` | variadic | none | **yes** | not `owner/name[:requirement]` |
| `dependencies { development "..." }` | variadic | none | **yes** | the same |
| `source "owner/name", git:/path:/archive:` | method | the owner's registry | **yes** | two `source` lines for one package; `git:` without `revision:`; `archive:` without `hash:` |
| `registry "owner", url: "..."` | method | none | **yes** | two registries for one owner |
| `workspace { members "..." }` | variadic | none | **yes** | a pattern that matches no project; a member without a `project.trb` |
| `authors "..."` | variadic | the workspace root's | yes | — |
| `description "..."` | `String` | `""` | yes | required to publish |
| `license "MIT"` | `String` | `""` | yes | required to publish |
| `repository "https://..."` | `String` | the workspace root's | yes | — |
| `profile "release" { ... }` | method | the built-in profiles | no | an unknown profile name |
| `test { coverageThreshold 80 }` | `Int` | `0` | no | outside `0..100` |

The first nine are **static**, and that is a promise with teeth: they are top-level command calls whose arguments are
plain string literals. Everything below them may be computed, because nothing is read before the file can be
evaluated. So a project file may still do this —

```trb
name "acme/shop"
version "1.4.0"

const threshold = if version.startsWith("0.") { 50 } else { 80 }

test {
  coverageThreshold threshold
}
```

— and may not do this:

```trb
const suffix = "shop"

name "acme/{suffix}"
```

Today that second file is accepted and `name` is silently empty (there is a test asserting it). It becomes an error:

```text
error: `name` has to be a plain string
 --> project.trb:3:6
  |
3 | name "acme/{suffix}"
  |      ^^^^^^^^^^^^^^^
  = The toolchain reads `name` before it can run anything, so it cannot be computed
```

That one rule is what makes `torb add` and `torb update` possible: they edit a `dependencies { }` block whose lines
are plain calls, and a block that is computed is one they refuse to touch with a message instead of rewriting.

**What does not belong in a project file.**

- **Sandbox capabilities.** Granted at the call site of `Sandbox.load`, never here. CONCEPT's decision, unchanged, and
  the reason is that whoever loads a script is the one who knows what it may do.
- **`native` and `foreign`.** They are keywords in source, so they are visible where they are used, and `torb add`
  reads them from the dependency's code. A manifest switch would be a second source of truth and the first thing to
  go stale.
- **Build scripts, install scripts, code generators, loaders.** Section 8.
- **`input`, `output`, `target`.** Sections 3 and 5.
- **An exports list.** `public` and `"owner/name/path"` already are it.
- **Feature flags and optional dependencies.** CONCEPT already decided: an optional integration is a package.
- **Anything per file.** A file's name is the only thing about a file the project says.

## 10. Workspaces and the lock file

**A member inherits `version`, `authors`, `license`, `repository`, the registries, the `source` lines and the
`profile` blocks of the root, unless it sets its own.** It never inherits `name`, `dependencies` or `prelude`: the
first is what it *is*, and the other two are what the rule of section 1 exists to keep honest — a member still has to
name a sibling as a dependency to import it (probe 9), and a workspace is not a way to skip that.

**`source` lines are inherited and never overridden by a member.** Where a package comes from is a property of the
build, not of whoever happens to import it; two members resolving one name to two places is the thing the lock file
exists to make impossible.

**One `project.lock.trb`, at the root, for every member.** What it pins, per package:

| Source | Pinned |
|---|---|
| a registry | the exact version, the content hash, and which registry answered |
| `git:` | the resolved commit, and the content hash of the tree at it |
| `archive:` | the URL and the content hash |
| `path:` | the path, and nothing else — it is source the project owns, and a hash of it would change on every edit |

A `path:` source is listed rather than pinned so that a reviewer sees that one dependency is not reproducible; that
is the honest half of what a lock file can promise about a directory somebody is editing.

`torb run`, `build`, `test` and `check` read the lock and never write it, and refuse when it does not match
`project.trb`. `torb add`, `torb remove` and `torb update` write it. Both halves are already CONCEPT's position; what
this document adds is that the same static subset (section 9) is what makes the writing half possible on a file that
is code.

The exact fields of `project.lock.trb` stay where CONCEPT left them — open, because there is no registry protocol to
resolve against yet. What is decided here is what has to be pinned, not how it is written.

## 11. Migration

Seven slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations.

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The names decide.** `isEntryFile` stops reading `buildInput` and reads the file name; `isLibraryModule` and it stop disagreeing; `packageAt` takes the whole package directory instead of `src/` plus `testInput`; `torb build` picks its entry from the programs; `torb test` collects `*.test.trb` below the package. `Manifest` loses `buildInput` and `testInput` | `compiler/src/project/{manifest,workspace}.trb`, `compiler/src/semantics/graph.trb`, `compiler/src/semantics/checker/declaration.trb`, `compiler/src/cli/{build,test}.trb`, `compiler/tests/project.test.trb` | **Medium.** Probe 18 says the thirty-two `build { input "src/lib.trb" }` blocks are currently switching a rule off; removing them turns that rule back on, so every `src/lib.trb` in the repository is checked for top-level code for the first time |
| 2 | **The manifests.** The `build { }` and `test { input }` blocks go out of all thirty-five `project.trb` files; `Build` is deleted from `std/project` and `Test` keeps only `coverageThreshold` | `std/project/src/lib.trb`, every `project.trb`, `docs/standard-library/project.md` | **Low**, and it is the slice that proves slice 1, because nothing may change behaviour |
| 3 | **Programs.** `src/<name>/main.trb`; `torb run <name>`, `torb build <name>`, the "name one" diagnostic; `compiler/src/main.trb` becomes `compiler/src/torb/main.trb` and its nineteen relative imports gain a `../`; `"owner/name/main"` becomes an error | `compiler/src/cli/*`, `compiler/src/main.trb`, `compiler/src/semantics/graph.trb` | **Medium.** The compiler moves its own entry file while compiling itself; run the fixpoint test for this slice |
| 4 | **Profiles and targets.** `--profile`, `--release`, `--target`; `build/<profile>/<program>`; the `profile` block in the vocabulary and in the static reader | `compiler/src/cli/build.trb`, `std/project/src/lib.trb`, `compiler/src/project/manifest.trb` | **Low.** `buildTarget = "release"` is one constant today, and the layout already has the shape |
| 5 | **The specifier grammar.** One function that takes a specifier apart, with a message per shape: a dot in a relative component, a `scheme:`, a host-qualified owner, a `..` inside a package path, a climb out of the package | `compiler/src/semantics/graph.trb`, `compiler/src/semantics/scope.trb`, `compiler/tests/check.test.trb` | **Low**, and it is the slice with the most new diagnostics, so it is mostly tests with exact messages |
| 6 | **Sources and the static subset.** `source` in `std/project` and in the static reader; a plain-string rule with a diagnostic for the nine static settings; `toolchain`, `description`, `license`, `repository` | `std/project/src/lib.trb`, `compiler/src/project/manifest.trb`, `compiler/tests/project.test.trb` | **Low** on its own. It does not resolve anything — resolution needs the registry protocol, which is CONCEPT's open question |
| 7 | **`Resource`.** The parameter kind in the checker, the resolution against the writing file, the recorded project-relative path, `embeddedText`/`embeddedBytes` folded to constants; `Sandbox.load` takes a `Resource` | `compiler/src/semantics/checker/*`, `compiler/src/ir/*`, `std/sandbox`, a new `std/resource` | **Highest.** It is a new parameter kind, which touches the same machinery as `lazy` and `Expression<Value>`, and it is the one slice that needs both back ends before anything may depend on it |

**Stage 0 needs nothing.** Its only read of a `project.trb` is a line scan for `name "..."`, used to build the stable
path in a panic message, and that line stays exactly where it is. It ignores `build`, `test`, `prelude`,
`dependencies` and `workspace` entirely, resolves no package specifier, and hardcodes `src/main.trb` for a directory
argument — which slice 3 has to change in one place (`bootstrap/crates/torb-cli/src/main.rs`) so that `torb run <dir>`
of a package whose only program is `src/server/main.trb` finds it.

**The prose.** `docs/tooling/project-trb.md` (the settings table is rewritten), `torb-build.md`, `torb-run.md`,
`torb-test.md`, `docs/language/modules-and-packages/{packages,top-level-code,use,workspaces}.md`,
`docs/standard-library/project.md`, `docs/glossary.md`'s "entry file" and "package", `docs/guide/modules-and-packages.md`,
`docs/how-to/{add-a-dependency,build-a-native-binary}.md`, CONCEPT's project layout, its `project.trb` example and
three entries in its decision log. `docs/internals/index.md` lists this document, which lands with slice 1 so that
`docs check` never sees a design document nothing links to.

## 12. What this is not

- **Not a package manager.** There is no registry protocol here, no resolution algorithm and no exact shape for
  `project.lock.trb`. What this document decides is what has to be *pinned* and where a location is *written*; how a
  registry answers is CONCEPT's open question and stays one.
- **Not a build system.** There are no rules, no targets that depend on targets, no code generation and no hooks. A
  `project.trb` describes a package; it does not describe how to make one.
- **Not `Cargo.toml` written in TorbScript.** A project file is code, and section 9's static subset is what keeps that
  from becoming a lie: the nine settings the toolchain needs before it can run anything are plain literals, and
  everything else may compute. A manifest that is a *program you run* — `Package.swift`, `build.zig` — is the line
  this design does not cross.
- **Not a place for capabilities.** What a script may do is granted at the call site of `Sandbox.load`; what a package
  may touch is visible from its imports. Neither is a setting.
- **Not a loader system.** Section 8 (b) is written down so that nobody has to reconstruct the argument, and it is not
  part of this design.
- **Not a path type.** Every path in a `project.trb` and every specifier is a `String` the workspace reader
  interprets; `std/path`'s `Path` is for files, and `docs/PATH.md` says why the two stay apart.
- **Not a change to what `public` means.** A package's surface is still its `public` declarations, reached through
  `"owner/name"` and `"owner/name/path"`. Nothing here adds a way to hide a module or to expose one twice.
- **Not a second manifest format.** `project.lock.trb` is a receiver script like `project.trb`, by the same mechanism,
  and there is no `.toml`, no `.json` and no generated file that is not TorbScript.

## 13. Open

Everything technical above is decided. These are taste or direction, and only the owner answers them.

1. **Where does a second program live — `src/<name>/main.trb` or `programs/<name>.trb`?** Section 4 lays both out.
   The document uses `src/<name>/main.trb`, because it is one rule in singular and plural, because a program that
   grows needs no new form, and because "several `main`s" is what was asked for. The case for `programs/` is that it
   makes `src/` mean exactly "modules" with no exceptions and answers the top-level-code question from the directory
   alone; the case against is that `src/main.trb` still exists, so the package would have two unrelated forms for one
   thing.
2. **Do the locations of dependencies live in `project.trb` or in the `use` line?** Section 7. The document says the
   manifest, and allows a host-qualified *name* as the middle ground, because the deciding cost of a location in
   source is that the type checker gains a network. What is lost is reading the location where it is used. If the
   answer is the `use` line after all, the thing that has to be decided with it is what the checker is allowed to do
   during a check, and that is a bigger decision than the syntax.
3. **Is `Resource` the right answer for assets, and is it the only one?** Section 8. The document says a parameter
   type the compiler resolves, and no loaders, because a loader is a build script and an import that runs something.
   The half worth arguing with is whether the shipped/embedded split should be decided by the reading function at all,
   or whether every resource should simply be embedded until somebody has a file too big for that.
4. **Does `torb build` default to `dev` or to `release`?** Section 5 says `dev`, which is Cargo's and Zig's default
   and the opposite of what the toolchain does today. `release` by default makes the first build somebody runs slow
   and makes a panic less useful; `dev` by default means the compiler builds itself with `--release` in one more
   place.
5. **Is `toolchain` worth a setting before there are two toolchain versions?** It is the only setting an *older*
   toolchain has to understand, so adding it late means the versions that cannot read it already exist. The document
   includes it for that reason alone.
