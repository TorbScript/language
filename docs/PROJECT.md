# The Project File

**The names of the files say what a package produces, `project.trb` says what the names cannot, and a program is the
one thing it says by hand.** A file called `lib.trb` is the library, a file called `main.trb` is the program, a file
called `*.test.trb` is a test — and none of that needs a line in a manifest. What is left for the manifest is what no
file name can carry: who the package is, what it needs, where each of those comes from, and the rare second program,
which is configured rather than found by a path convention. `build { input }` and `test { input }` say the first half a
second time, in a weaker language, and they go.

```text
  project.trb ──→ name, version, dependencies, where each one comes from, the workspace, the profiles,
                  and the second program:   program "migrate", entry: "tools/migrate.trb"

  src/  ──┬── lib.trb ─────────────→ the library, imported as "owner/name"
          ├── <path>.trb ──────────→ a module of it, imported as "owner/name/<path>"
          └── main.trb ────────────→ the one program that needs no line in the manifest
  tests/ ─── *.test.trb ───────────→ the test entries
                                     what no manifest should say: WHAT the package produces
```

- **[1. What the toolchain does today](#1-what-the-toolchain-does-today)** — twenty-one probes, and four places where the docs promise more than the code does
- **[2. What other systems do](#2-what-other-systems-do)** — nine of them, and what we take from each
- **[3. What a package produces](#3-what-a-package-produces)** — one library, one program for free, the rest configured
- **[4. Programs](#4-programs)** — `program`, its three settings, and what `run` and `build` do with them
- **[5. Profiles and targets](#5-profiles-and-targets)** — two words for two things, and where the binary lands
- **[6. The specifier grammar](#6-the-specifier-grammar)** — what may stand in the quotes, and what is reserved
- **[7. Where a dependency comes from](#7-where-a-dependency-comes-from)** — names in source, locations in the manifest
- **[8. What a project file may read](#8-what-a-project-file-may-read)** — one capability, and the frozen manifest that keeps installing free of code
- **[9. Resources, as far as the project file is concerned](#9-resources-as-far-as-the-project-file-is-concerned)** — the output layout and the file list; `docs/RESOURCES.md` has the rest
- **[10. The vocabulary](#10-the-vocabulary)** — every setting, its type, its default, and whether it is read statically
- **[11. Workspaces and the lock file](#11-workspaces-and-the-lock-file)** — what a member inherits, what the lock pins
- **[12. Migration](#12-migration)** — eight slices, each green on its own
- **[13. What this is not](#13-what-this-is-not)**
- **[14. Open](#14-open)**

This design replaces four things that exist today: `Build { target, input, output }` and `Test { input }` in
`std/project/src/lib.trb`, the `buildInput`/`testInput` fields of `compiler/src/project/manifest.trb` and the two
places that read them (`compiler/src/cli/build.trb`, `compiler/src/semantics/graph.trb`), the thirty-two
`build { input "src/lib.trb" }` blocks that the standard library writes to say "this is a library", and the sentence
in `docs/tooling/project-trb.md` that lists which settings are read. It adds three things the language does not have:
a grammar for the text after `from`, a `program` setting, and a toolchain that is the *caller* of the receiver script
it reads.

**The repository is migrating to `static` and `var fn`**, so a member that belongs to the type is written
`static origin = Point(0, 0)` and `static fn from(source: Source): Self`, a method lists no `self`
(`fn area(): Int`), a mutating method is `var fn translate(deltaX: Int)`, and `const` is optional on a field. Every
snippet below is in that form; function *types* keep their receiver (`(var self: SandboxCapabilities) => Void`).

Every claim about what the toolchain does today comes from a probe in section 1, run on a stage 0 built in the
worktree that wrote this. A claim the probes do not cover is marked as unproven where it stands.

---

## 1. What the toolchain does today

Twenty-one probe packages, each a directory with a `project.trb` whose workspace reaches the standard library, checked
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
| 19 | a parameter of a user type, called with a string literal | ``error: Expected `Resource`, found `String` `` — a literal adapts to nothing today |
| 20 | `Sandbox.load<ServerConfig>("./nowhere/at/all.trb")`, and the same with a `String` variable | `2 files, no problems` — the checker reads the path not at all |
| 21 | the same program on stage 0 and through the native back end | stage 0: ``error: Unknown name `Sandbox` ``; native: ``error: the type `Script` is not supported by the native back end yet`` |

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

**Probes 19 to 21 are the ones this round added**, and together they say what the two new mechanisms cost. Probe 19 is
the baseline for section 9 and for `docs/RESOURCES.md`: a string literal adapts to nothing, so a parameter type the
compiler resolves is checker work that does not exist in any form yet. Probes 20 and 21 are the baseline for section
8: `Sandbox.load` type checks perfectly — the generic argument, the trailing capability block, a path that is a
computed variable, a path that names no file — and then runs on neither implementation. **The sandbox is a signature
today and nothing else**, which is why section 8's slice is a 7.x slice.

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
and then disagrees with it. The fourth is also the shape of the one setting this design *adds*: `program` says what no
file name says, and section 4 is careful that it can never disagree with the tree, because the tree has nothing to say
about it.

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
  hash rather than a version. All three are already CONCEPT's position. And, against the previous round of this
  document, the `[[bin]]` *table* — for the second program only, and stripped to three settings that are all literals.
- From **Go**: a host-qualified name that the host verifies instead of a registry, which is section 7's
  `github.com/project/x` as a *name*. Not its "a program is a directory with a `main`", which is what section 4
  weighed and dropped.
- From **Dart** and **Gleam**: the convention decides which file is *the* program, and the manifest stays small enough
  to read in one breath. Gleam's `gleam.toml` is the size a `project.trb` should be.
- From **Zig**: `@embedFile`'s shape — a file named by a literal that the compiler resolves. That is
  `docs/RESOURCES.md`, minus the part of Zig we must not take.
- From **Deno**: the conclusion, not the mechanism. Deno shipped URL imports in source and then built an import map
  to get the locations back out of the source files. Taking the conclusion without repeating the experiment is the
  whole of section 7.
- From **SwiftPM**: `resources:` states the honest requirement — the build must know every non-code file a program
  needs. We answer it from the call sites instead of from a second list, and section 9 says what the build does with
  the answer.

**What we leave.**

- **`bin` maps, `products:`, and `[[bin]]` for the *first* program.** A list in the manifest of things the directory
  already shows is a second source of truth, and `build { input }` is that mistake one program small. `src/main.trb`
  needs no line, and that is the case that is not rare.
- **`Package.swift` and `build.zig`** — a manifest that is a program you *run*, with the machine's whole surface under
  it. A `project.trb` is a receiver script that the toolchain *evaluates* under one capability (section 8), which is a
  different thing: it can read a file below its own directory and it can do nothing else at all. That line is the one
  this design must not cross, and section 8 draws it exactly.
- **An `exports` map** (JSR, Node). `"owner/name/path"` already is the surface, and `public` already decides what a
  module exports. A second list would be a second source of truth about the same thing, which is the disease of
  section 1.
- **Loaders chosen by extension** (Node, bundlers). `docs/RESOURCES.md` says why.
- **Cargo features and optional dependencies.** CONCEPT already rejected them: an optional integration is a package.
- **`git`/`path`/`npm:` written on the dependency line.** We take the information and split it into two statements,
  so that "what I need" and "where it comes from" are two lines a reviewer can read separately (section 7).

## 3. What a package produces

**A package produces at most one library and any number of programs. The library and the first program are decided by
the name of a file; every program after the first is a line in `project.trb`.**

- `src/lib.trb` makes the package a library. It is what `"owner/name"` resolves to, and `"owner/name/path"` is
  `src/path.trb` of it. There is no second library, because the package **name** is the import name: a second library
  would need a second name, a name is a package, and a second package in the same tree is a workspace member. This is
  Cargo's answer and not Go's, and the reason to prefer it here is that TorbScript already has the finer surface —
  `"owner/name/path"` — so the thing many-libraries buys is already bought.
- `src/main.trb` is **the** program, named after the package, and it needs no line in the manifest.
- Every further program is a `program` line (section 4). There is no path convention for a second program: a
  convention would reserve a directory name or a file name in every package forever, to buy a shorthand for something
  that is rare.
- A file called `*.test.trb` is a test entry, anywhere in the package.
- Every other `.trb` file under `src/` is a module: no top-level code, importable inside the package, importable from
  outside when the package is a dependency.

**A file that may hold top-level code is never importable, and a file that is importable may never hold top-level
code.** That is one rule read in two directions:

| The file | Top-level code | Importable |
|---|---|---|
| `src/lib.trb` | no | yes, as `"owner/name"` |
| `src/<path>.trb` | no | yes, as `"owner/name/<path>"` |
| `src/main.trb` | yes | **no** |
| the `entry` of a `program` line | yes | **no** |
| `*.test.trb` | yes | no |
| a receiver script (`project.trb`, anything the sandbox loads) | yes | no |
| a `.trb` file with no `project.trb` above it | yes | it is a script; there is nothing to import it |

The sixth row is already true. The third row's "no" is new and it closes probe 17: `"owner/name/main"` becomes an
error, and so does any package path whose last component is `main`. The fourth row is what makes `entry` a **plain
string literal** and not a computed one: the checker has to know which files are entry files before it checks any of
them, and the static reader (section 10) is what tells it. Section 4 says what happens when the two collide anyway.

**The one rule that is not local stays.** CONCEPT says top-level code is about being *imported*, not about the file
name, and the checker implements the fallback: a file nothing imports may hold top-level code. That fallback is what
makes the thirteen chapters of `examples/tour/src/` and the seventy-three conformance programs of
`bootstrap/tests/native/` legal, and neither of them is a *product* — they are scripts that happen to live in a
package. Keeping the fallback costs a whole-workspace analysis (`isImported` resolves every `use` of every module
before it can answer about one file), which an editor cannot do incrementally and which makes a file become illegal
because of an edit somewhere else. The trade is worth it, because the alternative is seventy-three directories, and
because with the semantic names the fallback is now only ever consulted for files that are not products. What changes
is that it is written down as what it is: **a script is a file, not a product.** `torb build` with no argument never
builds one, and no `program` line names one.

**`build { input }` and `test { input }` disappear.** Nothing is left for them to say. What they do say today is
wrong in three of the four ways section 1 lists, and their one real use — `bootstrap/tests/native/project.trb`'s
`test { input "." }`, which pulls flat `.trb` files into the package — is served better by widening what belongs to a
package:

**A package's files are every `.trb` file below its directory that is not inside a nested package**, skipping hidden
directories, `target`, `node_modules` and `build`. That is one rule instead of two paths from the manifest, it makes
`../tools/helper` resolve (the second probe fact of section 1, which is a surprise today), and it is what a reader
already assumes when they say "the project". It is also what lets a `program` name an `entry` outside `src/`:
`tools/migrate.trb` is a file of the package, so it may import the package's modules like any other file of it.

## 4. Programs

**A program is `src/main.trb`, or a `program` line.** One setting, three arguments, all of them plain string literals.

```trb
program "migrate", entry: "tools/migrate.trb"
program "importer", entry: "tools/importer.trb", output: "dist/importer"
```

and, for the one case that needs no `entry` at all:

```trb
program "torb"
```

which **renames the default program**: `src/main.trb` still is the entry, and the binary is called `torb` instead of
being named after the package. That is `torbscript/compiler`'s whole manifest line, and it is why
`compiler/src/main.trb` does not move anywhere.

### The signature, and why there is no trailing block

```trb
/** A program of this package, beyond the `src/main.trb` that needs no line. */
public type Program {
  /** The name of the program, of the binary, and of the argument to `torb run` and `torb build`. */
  name: String
  /** The file whose top-level code the program is. Empty renames the default program `src/main.trb`. */
  entry: String = ""
  /** Where the binary goes, relative to the project and taken literally. Empty means `build/<profile>/<name>`. */
  output: String = ""
}
```

```trb
extend Project {
  /** Declares a program: `program "migrate", entry: "tools/migrate.trb"`. */
  var fn program(name: String, entry: String = "", output: String = "") {
    programs.add Program(name, entry, output)
  }
}
```

**A method with labels, not a method with a receiver closure.** The two forms already exist side by side in the
vocabulary and the line between them is not style: `registry "acme", url: "..."` is labels, `profile "release" { ... }`
is a block, and the difference is whether the toolchain has to read the setting *before it can run anything*. A
receiver closure is evaluated — its body is statements, its arguments may be computed, and the static reader of
section 10 cannot see into it at all. All three of a program's settings are in the static nine: `entry` decides which
files may hold top-level code, so the *checker* needs it; `name` is what `torb run <name>` matches; `output` is a path
the build writes and `torb publish` has to be able to list. None of the three may be computed, so none of the three
may live in a block.

**The block is what the design reserves for the setting that is genuinely not static.** If a program ever needs a
per-program profile override, or a target it refuses to build for, that is a `program "migrate" { ... }` block *next
to* the three labels, and the three labels stay on the line where the static reader can see them. Adding it later
costs one optional parameter of the function type `(var self: Program) => Void` with the default `{}`, exactly the way
`Sandbox.load` carries its capabilities, and it breaks nothing written before it. Adding it now would cost a block
that has nothing to put in it and an invitation to put the static three inside it.

### The rules, and the diagnostic for each

| The mistake | Where it is caught | The message |
|---|---|---|
| two `program` lines with one name | the static reader, before anything is checked | ``error: There are two programs called `migrate` `` with both lines |
| a `program` whose name is the package's own short name and which is not the default | the static reader | allowed — `program "shop", entry: "tools/shop.trb"` in `acme/shop` is only a collision if `src/main.trb` also exists, and then it is the "two programs" error above |
| two `program` lines with no `entry` | the static reader | ``error: `torb` and `cli` both rename the default program`` |
| `program "torb"` with no `src/main.trb` | the workspace reader, once it has the tree | ``error: `torb` renames the default program, and there is no `src/main.trb` `` with the note that a program with an entry writes `entry:` |
| an `entry` that does not exist | the workspace reader | ``error: The entry `tools/migrate.trb` of the program `migrate` is not a file`` |
| an `entry` outside the package directory | the workspace reader | ``error: The entry `../tools/migrate.trb` of the program `migrate` is outside `acme/shop` `` |
| `entry: "src/lib.trb"` | the workspace reader | ``error: `src/lib.trb` is the library of `acme/shop`, so it cannot be the entry of a program`` |
| an `entry` that some module imports | the **module graph**, at the `use` | ``error: `tools/migrate.trb` is the entry of the program `migrate`, so nothing can import it`` |
| two programs that resolve to one output path | the build, per profile | ``error: `migrate` and `importer` both build to `dist/tool` `` |
| an `output` that is interpolated | the static reader | the plain-string rule of section 10, with its own message |

**The entry-that-is-imported rule needs two diagnostics for one rule, and that is not an accident.** The manifest can
see `entry: "src/lib.trb"` — `src/lib.trb` is a file whose role the *name* decides, so the manifest line is wrong on
its own and the message points at the manifest. The manifest cannot see that somebody, three files away, wrote
`use helper from "../tools/migrate"`; only the module graph can, and the message that helps is the one at the import.
So: a collision with a name-decided role is a manifest error, and a collision with an actual import is an import
error. Both quote the `program` line.

**A program name is a `segment`** of section 6's grammar: lowercase, digits and `-`, no dot, no slash. That makes
`torb run <argument>` decidable without touching the disk: **an argument that contains `/` or `\`, or ends in `.trb`,
is a path; everything else is a program name.** `torb run migrate` is a name, `torb run tools/migrate.trb` is a file,
`torb run .` is a directory, and there is no case where the two readings are both possible.

### What `run` and `build` do

```text
torb run [name] [arguments]     run the only program, or the one named
torb run <file.trb>             run a script or an entry file directly
torb build [name]               build every program of the package, or the one named
torb build --output <file>      one program, somewhere else
```

- **`torb run` with no name** runs the package's only program. "Only" counts `src/main.trb` plus every `program` line
  that has an `entry`; a `program` line without an `entry` is the same program under another name and does not add to
  the count. Several programs and no name is an error that lists them:

  ```text
  error: `acme/shop` has three programs. Name one: `torb run server`
    = server, importer, migrate
  ```

- **`torb run <name>`** runs that program. A name nothing declares lists the ones that exist.
- **`torb build`** builds every program of the package, in the order the manifest declares them with `src/main.trb`
  first. In a workspace at the root it builds every program of every member.
- **`torb build <name>`** builds one.
- **A library-only package builds nothing, and that is not an error.** Today it is (probe 7). There is no artifact a
  library produces on its own — the C back end emits one translation unit per *program* — so `torb build` on
  `std/text` checks it and says `acme/lib is a library: checked, nothing to build`. That matters most in a workspace,
  where `torb build` at the root walks thirty members of which twenty-six are libraries; twenty-six failures would
  make the command useless at the one place it is worth having.
- **`torb run <file.trb>` and `torb build <file.trb>`** name a file directly, which is how a script is run and how a
  program that is not a product of any package is built. Nothing here changes that, and it is what keeps
  `examples/tour` and `bootstrap/tests/native/` legal: they declare no `program` at all, so they have no products,
  and `torb run ../compiler check ..` still checks every one of their files as the script it is (section 3's
  fallback).

### What the thirty-two manifests become

Every one of the thirty-two libraries that writes

```trb
build {
  input "src/lib.trb"
}
```

deletes the block and writes nothing in its place. `src/lib.trb` is what makes it a library, and probe 18 says the
block was never doing what its author thought: it made `src/lib.trb` an *entry* file, which switched the top-level
code rule off for the one file it exists to protect.

`compiler/project.trb` becomes three lines and one of them is new:

```trb
name "torbscript/compiler"
version "0.1.0"

program "torb"
```

`bootstrap/tests/native/project.trb` and `examples/tour/project.trb` lose `test { input "." }` and `build { }`
respectively, and gain nothing: section 3's "a package is its whole directory" covers what `test { input "." }` was
for.

**This is the decision that removes a whole slice from the migration.** The previous round's recommendation — a
program is `src/<name>/main.trb`, so the compiler's binary is `compiler/src/torb/main.trb` — moved the compiler's own
entry file while the compiler was compiling itself, and gave nineteen relative imports one more `../` each. A
configured program costs one line in one manifest and moves no file.

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
each other and the common path must stay short. A `program` line's `output` replaces the whole path for that one
program and is taken **literally**, relative to the project directory: `output: "dist/migrate"` is `dist/migrate`
under every profile and every target. `--output <file>` overrides even that, for one program and one invocation.

**`output` on a program is literal, and `build { output }` is deleted.** Today's `output "build/{target}/torb"` is a
TorbScript string interpolated eagerly against the receiver, so it is evaluated once, when the file runs, with
whatever `target` was at that moment — which means it cannot express "per profile" at all without the file being
evaluated once per profile. The static reader cannot read it either: an interpolated string is not a literal, and
today it is ignored without a word. The replacement says the two useful things separately: the *default* path carries
the profile and the target because it is a convention, and an `output` that overrides it is a fixed place somebody
wants a file, which is a literal by definition. A project that wants one program under two profiles in two places
wants `--output`, which is an argument of the invocation that knows which profile it is.

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
  its name, and reserving the dot is what keeps `use logo from "./logo.png"` unavailable — which `docs/RESOURCES.md`
  lists as a non-goal and not as a gap.
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

**One grammar, five readers, and each takes a subset.** This is the part that has to be written down, because the
five look alike and are not the same language:

| Where | What it accepts | Relative to |
|---|---|---|
| `use ... from "..."` | `relative` and `package` | the **file** |
| `dependencies { runtime "..." }` | `owner/name`, optionally followed by `":" requirement` — never a module path | nothing |
| `source "...", ...` | the same `owner/name` | nothing |
| `program "...", entry: "...", output: "..."` | a `segment` for the name; a **path** for `entry` and `output`, with the extension written and no `..` | the **project** |
| a resource literal (`docs/RESOURCES.md`) | a **path**, not a module specifier: `./` or `../`, **with** the extension written | the **file** at compile time, recorded as **project**-relative |

The last two rows are the ones that surprise, and they differ from each other on purpose. `program`'s `entry` is
written in `project.trb`, which *is* the project directory, so it is project-relative and has nothing else it could
be. A resource literal is written in a source file that may belong to a library somebody else wrote, so it is
file-relative, and the project-relative name the run time needs is what the compiler *records* rather than what the
author writes. `use Config from "./config"` names a module and drops the `.trb`; a resource path names a file and
writes it. They are different questions — one is about the source tree, the other about a file the program carries —
and they stay different.

## 7. Where a dependency comes from

**Decided: source files name packages, `project.trb` says where each package comes from, and `project.lock.trb` pins
what was actually taken.** A host-qualified owner is available as a *name*. The alternative — a location in the `use`
line — is written out here with its cost, because it is what was asked for first and because nobody should have to
reconstruct the argument.

**(A) Names in source, locations in the manifest — this is the decision.**

```trb
dependencies {
  runtime "acme/x:^1.2.3"
}
source "acme/x", git: "https://git.acme.test/project/x", revision: "v1.2.3"
```

```trb
use X from "acme/x"
```

**(B) The location in the source file**: `use X from "https://git.acme.test/project/x"`.

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

**A script has no project file, and does not get dependencies through the back door.** A script gets the standard
library and nothing else. The moment it needs a dependency it needs a project, which is one `torb init` away. Naming
the requirement in the `use` line would be (B) in miniature, with the same fetch during `torb run`, and it would
create the split Deno had — one rule inside a project, another outside it. If that is ever too strict, the spelling
should be a *manifest in the file* and not a location in a `use` — a `project { }` header at the top of the script, in
the same vocabulary, which is what Python's inline script metadata settled on and what keeps one rule for where a
requirement is written.

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

## 8. What a project file may read

**A `project.trb` may read files below its own directory. It may do nothing else.** No network, no writing, no clock,
no environment, no processes, no foreign functions.

```trb
name "acme/shop"
version File.readText("VERSION")?.trim()
```

The toolchain is the **caller** of this receiver script, and a caller grants capabilities at the call site — which is
CONCEPT's rule for the sandbox and not a new one. Written out, the grant is — with the member name
`docs/RESOURCES.md` section 5 gives it, because the path of a `project.trb` is a directory somebody typed on a command
line and is therefore *not* known when the toolchain is compiled:

```trb
const manifest = Sandbox.read<Project>(projectDirectory.joined("project.trb")) {
  modules "std/fs", "std/text"
  files readOnly: <the project directory>
  limits steps: 1_000_000, memory: 16.megabytes(), time: 2.seconds()
}?
```

- **Two modules and no more.** `std/fs` is the reading; `std/text` is what you do with the text you read
  (`trim`, `lines`, `split`). `std/time` would be a clock, `std/environment` a machine, `std/process` a shell,
  `std/http` a network: each of those is a way for two evaluations of one commit to disagree, and the manifest is the
  one file where they must not.
- **`files readOnly:` is the project directory, and `docs/PATH.md` section 7 is what "below" means.** The check is
  lexical — the *text* of the path does not leave the root — and the sandbox's own natives additionally refuse a
  component that is a symbolic link at open time, because section 7 names the sandbox as the one caller that must not
  accept the lexical gap. So `File.readText("../../etc/passwd")` is a `SandboxError` naming the path and the root, and
  so is a `VERSION` that is a link out of the tree. What is *not* promised is anything about the file's content: a
  manifest may read a file the project owns, and the project owns whatever is in its directory.
- **The limits are the toolchain's, and they are why `loop { }` in a manifest is a diagnostic and not a hang.**

### No environment, and what to write instead

The one real argument for an exception is CI: a version number that comes from a tag, through `CI_TAG` or
`GITHUB_REF_NAME`. The answer is still no, for three reasons that do not depend on each other.

1. **It makes the manifest a function of the machine.** Two checkouts of one commit would evaluate to two different
   packages, and the frozen manifest below would be a photograph of whichever CI run happened to publish. Every other
   thing in this design is reproducible from the tree; `version` is the worst possible place to give that up.
2. **The file is already the answer.** A CI job that wants the tag in the version writes the tag into `VERSION`
   before it builds — one line of the job, inside the one capability, and the file is then *in the tree* the package
   is built from, which is exactly what makes the result reproducible afterwards.
3. **Every grant is something four other readers have to emulate.** `torb add`, `torb update`, an editor and a
   registry all read a manifest. An environment read means each of them has to decide what the environment *is* when
   they read it, and there is no answer that is right for all four.

### The frozen manifest: installing still never runs code

**When a package is published, its manifest is evaluated once and the result is written back as a purely literal
manifest, and that is the `project.trb` the package carries.** The file that was evaluated ships beside it as
`project.source.trb`, which nothing reads and a human may.

- **One name, one meaning.** A consumer's toolchain, an editor and a registry all look for `project.trb`. If the
  frozen file had a name of its own, every one of them would need a rule for which to prefer, and there is a version
  of each that has the wrong rule. Instead: **the `project.trb` of a published package is literal**, so reading it is
  the static read of section 10 and nothing else runs. That is CONCEPT's "installing never runs code", kept exactly.
- **The format is the vocabulary, printed back.** The frozen manifest is the evaluated `Project` value written as the
  command calls that would produce it, in a fixed order, every argument a plain string or number literal. This is
  `docs/ENCODING.md`'s principle one level up: a value is its constructor call, and a receiver script is its settings.
  There is no second format, no `.toml` and no generated JSON.
- **`torb publish` verifies it.** It re-reads the frozen manifest with the *static* reader and refuses if any setting
  did not survive, which makes "a published manifest is statically readable" a checked property rather than a hope.
- **The file list is part of it.** The frozen manifest lists the resources the package's own code names
  (`docs/RESOURCES.md`), so a consumer knows every non-code file the package carries without evaluating anything.

### Which manifests are evaluated, and which are frozen

**A package you build from source is evaluated; a package you install is frozen.** That is the whole rule, and every
case falls out of it.

| The package | What the toolchain does |
|---|---|
| the project itself | evaluated, under the grant above, scoped to its own directory |
| a workspace member | evaluated, under the same grant, scoped to **the member's** directory |
| a `path:` source | the same |
| a `git:` source | the same, scoped to the checkout — it is source you chose to build from |
| a registry package | **never evaluated.** Its `project.trb` is the frozen one, and the static reader is all that touches it |

The alternative for members and `path:`/`git:` sources — require them to be literal-only — was considered and
rejected: it creates two dialects of one file inside one repository, which is the disease section 1 is a list of, and
it buys no security at all, because the grant is identical and the source is already code you are about to compile.
The honest note is the one the lock file already makes (section 11): a `path:` source is not reproducible, and a
reviewer sees that on the line that declares it.

**A member's grant is its own directory, not the root's.** A member that wants the workspace's version *inherits* it
(section 11), which already works and needs no read; a member that reads `../../VERSION` gets a `SandboxError`. That
keeps the rule one sentence and makes the common case free.

### Errors, determinism and caching

- **A failure of the script is a diagnostic against `project.trb`**, with the line the `SandboxError` carries, never a
  toolchain crash — which is the reason for a sandbox rather than an `include`. A capability refusal names the path
  and the granted root; a panic inside the script is reported as one; a step, memory or time limit is reported as
  itself.
- **The static read comes first.** It is what finds the workspace, the dependencies and the entry files, and it
  cannot depend on evaluation. A manifest whose static settings are not plain literals (section 10) is reported by
  the static reader and **never evaluated**.
- **A manifest that says nothing computed is never evaluated at all.** If the static read covers every setting the
  file writes — which is true of every manifest in this repository today — the toolchain has the whole `Project`
  already and starting a VM would be work with no result. So the cost of section 8 is paid only by the projects that
  use it.
- **The inputs of an evaluation are recorded.** `project.trb` plus every file the script read, each with a content
  hash, written to `build/manifest-inputs.trb` as `input "VERSION", hash: "..."` lines. An incremental build
  re-evaluates the manifest when one of them changed and not otherwise. That is sound precisely because the grant is
  read-only and there is no clock and no environment: given the same inputs, the evaluation has the same result.

### Stage 0 has no sandbox

Probe 21: `Sandbox` is ``Unknown name `Sandbox` `` on stage 0, and the native back end refuses `Script`. So none of
this section runs until the VM does (7.x), and stage 0's reading of a `project.trb` stays what it is — a line scan for
`name "..."`, used to build the stable path in a panic message.

**What works before the VM exists is the static subset, which is every setting in the repository's own thirty-five
manifests.** `name`, `version`, `prelude`, `dependencies`, `source`, `registry`, `workspace`, `program` and the rest
of section 10's static nine are plain literals, read from the syntax tree. A manifest that computes anything is
refused by a toolchain without a VM, with a message that says so — rather than the current behaviour, which is to
read an empty value and say nothing (section 1, findings 3 and 4). **The repository's own manifests therefore stay
literal until the VM exists**, and the slice that switches evaluation on (section 12, slice 7) changes no manifest in
the tree.

## 9. Resources, as far as the project file is concerned

**`docs/RESOURCES.md` is the design.** What belongs here is only what the project file, the build output and the
specifier grammar have to say about it.

- **A resource is named by a string literal at the call site and resolved by the compiler**, against the directory of
  the file that writes it. There is no list of assets in the manifest: SwiftPM's `resources:` states the honest
  requirement — the build must know every non-code file a program needs — and the call sites already answer it.
- **A resource path is not a module specifier** (section 6, the last row of the table): it carries its extension, it
  is file-relative where it is written, and what is recorded is the project-relative name.
- **The build output layout.** A program's shipped resources go beside the binary, under a directory named after the
  program, each at its **stable name** — `<owner>/<name>/<path inside the package>`, which is the text the compiler
  already builds for a panic site. The run time finds them relative to **the program** and never to the working
  directory:

  ```text
  build/<profile>/<program>                                              the binary
  build/<profile>/<program>.resources/acme/game/src/sheets/hero.spr      one shipped file
  ```

  A `program` line's `output` moves both: `output: "dist/migrate"` is `dist/migrate` and `dist/migrate.resources/`.
- **The published package's file list.** The frozen manifest (section 8) carries every resource path the package's own
  code names, so a consumer can see the non-code files of a dependency without evaluating anything and `torb publish`
  can refuse a package whose code names a file the archive does not contain.
- **Nothing in `project.trb` configures a resource.** No `assets:` list, no loader table, no per-file setting. A
  manifest that listed assets would be a second source of truth about the same files, which is section 1's disease,
  and a manifest that configured loaders would be a build script, which is CONCEPT's non-goal.

## 10. The vocabulary

Every setting of `project.trb`. **Static** means the toolchain reads it from the syntax tree without evaluating the
file, which is what stage 0 does today, what an editor wants before anything is checked, what a registry wants without
running anything, and what the checker needs before it checks a file.

| Setting | Type | Default | Static | Error |
|---|---|---|---|---|
| `toolchain "0.1"` | `String` | none | **yes** | a toolchain older than this refuses the project, and says so before reading anything else |
| `name "owner/name"` | `String` | none | **yes** | missing, not `owner/name`, or not a plain string |
| `prelude "std/prelude"` | `String` | `"std/prelude"` | **yes** | not a package specifier; the package must exist |
| `dependencies { runtime "..." }` | variadic | none | **yes** | not `owner/name[:requirement]` |
| `dependencies { development "..." }` | variadic | none | **yes** | the same |
| `source "owner/name", git:/path:/archive:` | method | the owner's registry | **yes** | two `source` lines for one package; `git:` without `revision:`; `archive:` without `hash:` |
| `registry "owner", url: "..."` | method | none | **yes** | two registries for one owner |
| `workspace { members "..." }` | variadic | none | **yes** | a pattern that matches no project; a member without a `project.trb` |
| `program "name", entry:, output:` | method | `src/main.trb` is the one program | **yes** | section 4's table |
| `version "1.4.0"` | `String` | the workspace root's | no | not a version; required to publish |
| `authors "..."` | variadic | the workspace root's | no | — |
| `description "..."` | `String` | `""` | no | required to publish |
| `license "MIT"` | `String` | `""` | no | required to publish |
| `repository "https://..."` | `String` | the workspace root's | no | — |
| `profile "release" { ... }` | method | the built-in profiles | no | an unknown profile name |
| `test { coverageThreshold 80 }` | `Int` | `0` | no | outside `0..100` |

The first nine are **static**, and that is a promise with teeth: they are top-level command calls whose arguments are
plain string literals. Everything below them may be computed, because nothing is read before the file can be
evaluated.

**`version` moved.** The previous round listed it among the static settings. Section 8's whole point is that
`version File.readText("VERSION")?.trim()` is the motivating example, so `version` is exactly the setting that must be
allowed to compute. What a registry and a lock file need is the version of a *published* package, and that comes from
the frozen manifest, where it is a literal again. A tool that wants the version of a project it is not building
evaluates the manifest or reads the lock; there is no third answer and there does not need to be.

So a project file may do this —

```trb
name "acme/shop"
version File.readText("VERSION")?.trim()

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

- **Sandbox capabilities.** Granted at the call site that loads the script, never here. CONCEPT's decision, unchanged, and
  section 8 is the same rule applied to the toolchain itself: the toolchain is a caller, so it grants; the manifest is
  a script, so it receives and cannot widen.
- **`native` and `foreign`.** They are keywords in source, so they are visible where they are used, and `torb add`
  reads them from the dependency's code. A manifest switch would be a second source of truth and the first thing to
  go stale.
- **Build scripts, install scripts, code generators, loaders, asset lists.** Sections 8 and 9, and
  `docs/RESOURCES.md`.
- **`input`, `output` as a whole-project setting, `target`.** Sections 3, 4 and 5.
- **An exports list.** `public` and `"owner/name/path"` already are it.
- **Feature flags and optional dependencies.** CONCEPT already decided: an optional integration is a package.
- **Anything per file except a program's entry.** A file's *name* is what the project says about it; `entry` is the
  one exception and section 4 is the argument for it.

## 11. Workspaces and the lock file

**A member inherits `version`, `authors`, `license`, `repository`, the registries, the `source` lines and the
`profile` blocks of the root, unless it sets its own.** It never inherits `name`, `dependencies`, `prelude` or
`program`: the first is what it *is*, the next two are what the rule of section 1 exists to keep honest — a member
still has to name a sibling as a dependency to import it (probe 9), and a workspace is not a way to skip that — and
the last is about files that are inside the member.

**Inheritance happens after evaluation, not before it.** A member whose `version` is computed keeps its own; a member
that says nothing about `version` takes the root's *evaluated* value. That is what makes section 8's "a member's grant
is its own directory" cheap: the root already read the `VERSION` file, and the member gets the answer rather than the
file.

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

**The lock pins the version a package was resolved *at*, which for a source-built package is the version its evaluated
manifest answered.** That is the one place section 8 reaches the lock file: a `git:` dependency whose manifest computes
its version from a file in its own tree is pinned by commit and content hash, so the computed version is reproducible
for the same reason the tree is. A registry package's version is a literal in its frozen manifest and needs nothing.

`torb run`, `build`, `test` and `check` read the lock and never write it, and refuse when it does not match
`project.trb`. `torb add`, `torb remove` and `torb update` write it. Both halves are already CONCEPT's position; what
this document adds is that the same static subset (section 10) is what makes the writing half possible on a file that
is code.

The exact fields of `project.lock.trb` stay where CONCEPT left them — open, because there is no registry protocol to
resolve against yet. What is decided here is what has to be pinned, not how it is written.

## 12. Migration

Eight slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations. Slices 1 to 6 need nothing that does not exist; slice 7 needs the
VM; slice 8 is `docs/RESOURCES.md`'s own plan.

**All of this lands after the repository has moved to `static` and `var fn`**, because that round touches practically
every `.trb` file and nothing should be rebased across it.

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The names decide.** `isEntryFile` stops reading `buildInput` and reads the file name; `isLibraryModule` and it stop disagreeing; `packageAt` takes the whole package directory instead of `src/` plus `testInput`; `torb build` picks its entry from the programs; `torb test` collects `*.test.trb` below the package. `Manifest` loses `buildInput` and `testInput` | `compiler/src/project/{manifest,workspace}.trb`, `compiler/src/semantics/graph.trb`, `compiler/src/semantics/checker/declaration.trb`, `compiler/src/cli/{build,test}.trb`, `compiler/tests/project.test.trb` | **Medium.** Probe 18 says the thirty-two `build { input "src/lib.trb" }` blocks are currently switching a rule off; removing them turns that rule back on, so every `src/lib.trb` in the repository is checked for top-level code for the first time |
| 2 | **The manifests.** The `build { }` and `test { input }` blocks go out of all thirty-five `project.trb` files; `Build` is deleted from `std/project` and `Test` keeps only `coverageThreshold` | `std/project/src/lib.trb`, every `project.trb`, `docs/standard-library/project.md` | **Low**, and it is the slice that proves slice 1, because nothing may change behaviour |
| 3 | **Programs.** `Program` and `Project.program` in `std/project`; the static reader reads `program` lines; `torb run <name>`, `torb build <name>`, the "name one" diagnostic, the library-only "nothing to build"; the ten diagnostics of section 4; `"owner/name/main"` and an imported `entry` become errors; `compiler/project.trb` writes `program "torb"` | `std/project/src/lib.trb`, `compiler/src/project/manifest.trb`, `compiler/src/cli/{build,run,test}.trb`, `compiler/src/semantics/graph.trb`, `compiler/project.trb` | **Low.** No file moves and no import changes. The one thing to watch is stage 0's hardcoded `src/main.trb` for a directory argument (`bootstrap/crates/torb-cli/src/main.rs`), which has to learn the `program` lines so that `torb run <dir>` of a renamed default still finds it |
| 4 | **Profiles and targets.** `--profile`, `--release`, `--target`; `build/<profile>/<program>`; the `profile` block in the vocabulary and in the static reader; `output` on a `program` taken literally | `compiler/src/cli/build.trb`, `std/project/src/lib.trb`, `compiler/src/project/manifest.trb` | **Low.** `buildTarget = "release"` is one constant today, and the layout already has the shape |
| 5 | **The specifier grammar.** One function that takes a specifier apart, with a message per shape: a dot in a relative component, a `scheme:`, a host-qualified owner, a `..` inside a package path, a climb out of the package | `compiler/src/semantics/graph.trb`, `compiler/src/semantics/scope.trb`, `compiler/tests/check.test.trb` | **Low**, and it is the slice with the most new diagnostics, so it is mostly tests with exact messages |
| 6 | **Sources and the static subset.** `source` in `std/project` and in the static reader; a plain-string rule with a diagnostic for the nine static settings; `toolchain`, `description`, `license`, `repository` | `std/project/src/lib.trb`, `compiler/src/project/manifest.trb`, `compiler/tests/project.test.trb` | **Low** on its own. It does not resolve anything — resolution needs the registry protocol, which is CONCEPT's open question |
| 7 | **The manifest that reads.** The toolchain becomes a `Sandbox` caller: the grant of section 8, the evaluation only when the static read is not enough, `build/manifest-inputs.trb`, the diagnostics for a failing script, the frozen manifest and `torb publish`'s verification | `compiler/src/project/*`, `compiler/src/cli/*`, `std/sandbox`, the VM | **Highest, and blocked.** Probe 21: `Sandbox` runs on neither implementation, so this slice cannot start before 7.x. Nothing in the repository's own manifests needs it, which is what makes waiting free |
| 8 | **Resources.** `docs/RESOURCES.md`'s slices, which are a plan of their own | see that document | see that document |

**The prose.** `docs/tooling/project-trb.md` (the settings table is rewritten), `torb-build.md`, `torb-run.md`,
`torb-test.md`, `docs/language/modules-and-packages/{packages,top-level-code,use,workspaces}.md`,
`docs/standard-library/project.md`, `docs/glossary.md`'s "entry file" and "package", `docs/guide/modules-and-packages.md`,
`docs/how-to/{add-a-dependency,build-a-native-binary}.md`, CONCEPT's project layout, its `project.trb` example and
four entries in its decision log. `docs/internals/index.md` lists this document and `docs/RESOURCES.md`, which is
already done, so that `docs check` never sees a design document nothing links to.

## 13. What this is not

- **Not a package manager.** There is no registry protocol here, no resolution algorithm and no exact shape for
  `project.lock.trb`. What this document decides is what has to be *pinned* and where a location is *written*; how a
  registry answers is CONCEPT's open question and stays one.
- **Not a build system.** There are no rules, no targets that depend on targets, no code generation and no hooks. A
  `project.trb` describes a package; it does not describe how to make one.
- **Not a manifest that is a program you run.** A project file is code and section 8 lets it read, which is one step
  towards `Package.swift` and exactly one: the grant is read-only, below one directory, with two modules and a step
  limit, and a published package carries a frozen manifest that nobody evaluates at all. There is no way to write, to
  fetch, to spawn or to ask the machine what day it is, and there is no place to hang one.
- **Not a place for capabilities.** What a script may do is granted at the call site that loads it — including
  when the caller is the toolchain and the script is `project.trb`. What a package may touch is visible from its
  imports. Neither is a setting.
- **Not a loader system, and not an asset list.** `docs/RESOURCES.md` is written so that nobody has to reconstruct
  the argument, and neither an extension-loader table nor a `resources:` list is part of this design.
- **Not a path type.** Every path in a `project.trb` and every specifier is a `String` the workspace reader
  interprets; `std/path`'s `Path` is for files, and `docs/PATH.md` says why the two stay apart.
- **Not a change to what `public` means.** A package's surface is still its `public` declarations, reached through
  `"owner/name"` and `"owner/name/path"`. Nothing here adds a way to hide a module or to expose one twice.
- **Not a second manifest format.** `project.lock.trb`, the frozen `project.trb` and `build/manifest-inputs.trb` are
  all receiver scripts in the same vocabulary, and there is no `.toml`, no `.json` and no generated file that is not
  TorbScript.
- **Not a convention for a second program.** There is no `src/<name>/main.trb` and no `programs/<name>.trb`. A second
  program is rare, and a convention that reserves a directory name in every package forever to shorten a rare line is
  a bad trade.

## 14. Open

Everything technical above is decided. These are taste or direction, and only the owner answers them.

1. **Does `torb build` default to `dev` or to `release`?** Section 5 says `dev`, which is Cargo's and Zig's default
   and the opposite of what the toolchain does today. `release` by default makes the first build somebody runs slow
   and makes a panic less useful; `dev` by default means the compiler builds itself with `--release` in one more
   place.
2. **Is `toolchain` worth a setting before there are two toolchain versions?** It is the only setting an *older*
   toolchain has to understand, so adding it late means the versions that cannot read it already exist. The document
   includes it for that reason alone.
3. **Is `project.source.trb` the right name for the file that was evaluated, and should it ship at all?** Section 8
   ships it because a reader of a published package should be able to see how the version was computed, and it costs
   one file nothing reads. The alternative is to ship only the frozen `project.trb`, which is smaller and loses the
   answer to "where did this version come from".
