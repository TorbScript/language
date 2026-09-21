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
- **[8. What a project file may read](#8-what-a-project-file-may-read)** — files and the environment, and the locked manifest that keeps installing free of code
- **[9. Resources, as far as the project file is concerned](#9-resources-as-far-as-the-project-file-is-concerned)** — the output layout and the file list; `docs/RESOURCES.md` has the rest
- **[10. The vocabulary](#10-the-vocabulary)** — every setting, its type, its default, and whether it is read statically
- **[11. Workspaces and the lock file](#11-workspaces-and-the-lock-file)** — what a member inherits, what the lock pins
- **[12. Migration](#12-migration)** — nine slices, each green on its own
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
torb build --profile dev            the default, decided
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

**`dev` is the default and `release` is explicit.** That is Cargo's and Zig's default and the opposite of what the
toolchain does today (`buildTarget = "release"` is one hardcoded constant): the first build somebody runs should be
fast and its panics should carry frames, and the one place that pays is the toolchain building itself, which passes
`--release` in a script that already exists.

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

**A `project.trb` may read files below its own directory and the environment it was started in. It may do nothing
else.** No network, no writing, no clock, no processes, no foreign functions.

```trb
name "acme/shop"
version Environment.get("CI_COMMIT_TAG") ?? File.readText("VERSION")?.trim() ?? "0.0.0"
```

The toolchain is the **caller** of this receiver script, and a caller grants capabilities at the call site — which is
CONCEPT's rule for the sandbox and not a new one. Written out, the grant is — with the member name
`docs/RESOURCES.md` section 5 gives it, because the path of a `project.trb` is a directory somebody typed on a command
line and is therefore *not* known when the toolchain is compiled:

```trb
const manifest = Sandbox.read<Project>(projectDirectory.joined("project.trb")) {
  modules "std/fs", "std/text", "std/environment"
  files readOnly: <the project directory>
  environment "*"
  limits steps: 1_000_000, memory: 16.megabytes(), time: 2.seconds()
}?
```

- **Three modules and no more.** `std/fs` is the file reading; `std/text` is what you do with the text you read
  (`trim`, `lines`, `split`); `std/environment` is `Environment.get(name): String?` and nothing else. `std/time`
  would be a clock, `std/process` a shell, `std/http` a network: each of those is a way for a manifest to reach
  something the project neither contains nor was handed, which is the line this grant draws.
- **`files readOnly:` is the project directory, and `docs/PATH.md` section 7 is what "below" means.** The check is
  lexical — the *text* of the path does not leave the root — and the sandbox's own natives additionally refuse a
  component that is a symbolic link at open time, because section 7 names the sandbox as the one caller that must not
  accept the lexical gap. So `File.readText("../../etc/passwd")` is a `SandboxError` naming the path and the root, and
  so is a `VERSION` that is a link out of the tree. What is *not* promised is anything about the file's content: a
  manifest may read a file the project owns, and the project owns whatever is in its directory.
- **`environment "*"` — every variable, and the narrower pattern is rejected.** A pattern has to be written somewhere,
  and there are only two places, both wrong. In the manifest itself it would be the script granting itself a
  capability, which CONCEPT forbids in so many words ("a script that could grant itself capabilities would not be a
  sandbox"). Fixed in the toolchain as, say, `TORB_*`, it would exclude every variable anybody actually has:
  `CI_COMMIT_TAG`, `GITHUB_REF_NAME`, `BUILD_NUMBER` share no prefix and never will. And the sandbox here is not
  protecting the user from the manifest — the user wrote it and is about to compile the code it names. What the
  sandbox protects is that the toolchain *knows what the manifest did*, and that is served by recording the names
  read, not by narrowing the set.
- **The limits are the toolchain's, and they are why `loop { }` in a manifest is a diagnostic and not a hang.**

### Reading the environment, and its two pitfalls

Reading a variable is what a CI job actually wants: a version that comes from a tag, without a commit that changes
`VERSION` before every release. It is granted, and the two things that used to be arguments against it are true
anyway, so they are written down as pitfalls rather than as a prohibition. Both belong in
`docs/tooling/project-trb.md` under `# Pitfalls`.

1. **Whatever flows from the environment into a setting is published with the package.** `version` ends up in the
   locked manifest below, which is the file a registry, a consumer and an editor read. A manifest that writes
   `description Environment.get("NPM_TOKEN") ?? ""` publishes the token, and no mechanism can tell a token from a tag.
   What the design can do is make it visible at the moment it matters: the locked manifest records **which** variables
   were read (by name), and `torb publish` prints that list before it uploads. It asks for no flag and no
   confirmation: reading a variable is the most ordinary thing a configuration file written in TorbScript does - more
   ordinary than reading a file - and a manifest that does it is not doing anything that needs permission.
2. **A build is reproducible only together with the variables it read.** Two checkouts of one commit with two
   environments are two packages. That is not a defect of the grant, it is what the grant is *for* — but it means "the
   commit" stops being the whole input, and a bug report that says "it builds here and not there" has one more place
   to look. The locked manifest and `build/manifest-inputs.trb` both name the variables, so the place to look is
   written down; nothing makes it unnecessary.

**A dependency does not get the environment.** The grant above is for the project `torb` was invoked in and for its
workspace members, and for nothing else. A `git:` or `path:` dependency is evaluated with file access to **its own**
directory and **without `std/environment` in `modules`** — so a dependency's manifest that reads a variable fails to
load, with the sandbox's own "a module the script may not import" naming the module and the dependency. Refusing the
*module* rather than handing back `None` is deliberate: `Environment.get` answers `None` both for a variable that is
unset and for one the sandbox denied (`std/environment` says so under `# Pitfalls`), so a silent denial would let a
dependency fall back to some default and nothing would ever say why. The reason for the refusal is the first pitfall
read backwards: a dependency that could read your environment could carry your secret into its own settings, and from
there into your binary.

### The locked manifest: installing still never runs code

**There are exactly two files.** `project.trb` is the one a person maintains. `project.lock.trb` is the locked
manifest: the evaluated, purely literal settings *and* the pinned dependency graph, in one file, in the same
vocabulary printed back. There is no third file, no frozen copy under another name, and no frozen file that is also
called `project.trb`.

**A consumer, a registry and an editor read the lock of a foreign package and never evaluate its `project.trb`.**
That is CONCEPT's "installing never runs code", kept exactly, and it is now one sentence instead of a rule about which
of two `project.trb` files you are holding.

```trb
// project.lock.trb - written by `torb add`, `torb update`, `torb lock` and `torb publish`. Do not edit.

language "0.3.0"

settings "acme/shop" {
  version "1.4.2"
  description "A shop"
  license "MIT"
  authors "Ada Lovelace"
  prelude "std/prelude"
  program "shop", entry: "src/main.trb"
  program "migrate", entry: "tools/migrate.trb"
  dependencies {
    runtime "acme/http:^1.2.3"
  }
  resource "acme/shop/src/templates/invoice.html"
  from {
    file "VERSION", hash: "sha256:..."
    variable "CI_COMMIT_TAG", hash: "sha256:..."
  }
}

graph {
  package "acme/http", version: "1.2.5", registry: "acme", hash: "sha256:..."
  package "acme/json", version: "2.0.1", registry: "acme", hash: "sha256:..."
  package "acme/local", path: "../local"
}
```

- **`settings` is the evaluated `Project`, printed back as the command calls that would produce it**, in a fixed
  order, every argument a plain string or number literal. This is `docs/ENCODING.md`'s principle one level up: a value
  is its constructor call, and a receiver script is its settings. There is no second format, no `.toml`, no JSON.
- **`settings` names its package, so a workspace root's lock has one block per member.** That is what CONCEPT's "one
  `project.lock.trb`, at the root" already asks for: one resolution for everybody (`graph`, once) and one set of
  settings per package (`settings`, per member). A published archive carries a lock with exactly one `settings` block
  and no `graph`.
- **`settings` carries what a *consumer* needs and nothing else**: `language`, `version`, `prelude`,
  `dependencies`, the `program` lines, the metadata, and the resource list (`docs/RESOURCES.md`, so a consumer sees
  every non-code file a dependency carries without opening the archive). It carries **no `source` and no `registry`**
  — where a package comes from is the *building* project's decision, and a dependency that could republish its own
  `source` lines would be dependency confusion by another route — and **no `workspace`**, which is about a tree the
  consumer does not have, and no `profile` or `test`, which are about a build the consumer is not running.
- **`from` is the provenance of the evaluation**: which files and which variables the script read, each by name and by
  a hash of what it answered. It is what makes pitfall 1 visible and pitfall 2 diagnosable, and it is the published
  half of `build/manifest-inputs.trb`, which has the same two line shapes.
- **`graph` is what CONCEPT's lock file already pins** (section 11), unchanged.
- **A published package carries both files, whole.** `project.trb` travels so that a person can read how the version
  was computed and what the author actually wrote; `project.lock.trb` travels because it is the only thing the
  toolchain reads. npm and Bun ship the pair for the same reason, and it is what makes the third file unnecessary.
  **The `graph` section travels with it and is information, not resolution:** it records what the package was built
  and tested against, which is the first thing a bug report wants, and a consumer resolves its own graph from its own
  root lock and never from a dependency's.
- **`torb publish` verifies the settings section with the *static* reader** and refuses if a setting did not survive
  being printed back — which makes "a published manifest is statically readable" a checked property rather than a
  hope.
- **A package without a `project.lock.trb` is not installable.** There is nothing a consumer may read, and evaluating
  a foreign `project.trb` is the one thing this design does not do. `torb publish` writes the file, so the case only
  arises for an archive somebody assembled by hand, and the message says which file is missing.

**Writing the lock is deterministic: the same inputs produce a byte-identical file.** That is a requirement and not an
aspiration, because the file is checked into a repository, reviewed in a diff and compared by a registry, and a file
that differs between two machines is a file everybody learns to ignore.

- **A fixed order, everywhere.** The sections in the order `language`, `settings`, `graph`; one `settings` block per
  package, sorted by package name; the settings inside a block in the vocabulary's own declaration order (section 10's
  table, top to bottom), never in the order the manifest happened to write them; `from` entries by kind and then by
  name; `graph` packages by name; `program` lines in the order the manifest declares them, which is the one order that
  is itself meaningful (section 4: `torb build` builds them in it).
- **Nothing about the machine, ever.** No timestamp, no tool build identifier, no absolute path, no user name, no
  locale-dependent formatting. A `path:` source is written relative to the lock file. This is the same rule the C back
  end already lives under — "no path of this machine, no time and no build id is in it, so two runs of the compiler
  write the same bytes", which the generated C says in its own header.
- **One spelling per value.** Hashes as `sha256:` plus lowercase hex; numbers in the canonical form `torb canon`
  already defines for a literal; strings with `canon`'s escapes; UTF-8, LF, no BOM, exactly one trailing newline.
- **How it is checked.** `torb lock --check` writes the file into memory and compares it byte for byte with the one on
  disk, failing with a diff — which is exactly the shape of `canon --check` and `docs index --check`, two gates this
  repository already runs on every change, so it costs a gate and no new machinery. Two further checks fall out of it
  for free: writing the lock twice must produce the same bytes (idempotence, asserted in the toolchain's own tests),
  and once the VM exists, stage 0 and the compiled `torb` must write the same bytes for one project — which is the
  conformance suite's contract applied to a file instead of to standard output.

### Which manifest is evaluated, and which is read from a lock

**A package you build from source is evaluated; a package you install is read from its lock.** That is the whole rule.

| The package | What the toolchain does | The environment |
|---|---|---|
| the project itself | its `project.trb` is evaluated, scoped to its own directory | **yes** |
| a workspace member | its `project.trb` is evaluated, scoped to **the member's** directory | **yes**, the same one |
| a `path:` source | the same, scoped to its directory | **no** |
| a `git:` source | the same, scoped to the checkout — it is source you chose to build from | **no** |
| a registry package | **never evaluated.** The `settings` section of its `project.lock.trb` is all the toolchain reads; its `project.trb` ships for a human and the toolchain does not parse it | — |

The alternative for members and `path:`/`git:` sources — require them to be literal-only — was considered and
rejected: it creates two dialects of one file inside one repository, which is the disease section 1 is a list of, and
it buys nothing, because the grant is file access to a directory whose source you are about to compile anyway. The
environment is the one capability where the line *is* worth drawing, and the table draws it.

**A member's grant is its own directory, not the root's.** A member that wants the workspace's version *inherits* it
(section 11), which already works and needs no read; a member that reads `../../VERSION` gets a `SandboxError`. That
keeps the rule one sentence and makes the common case free.

**Your own lock's `settings` section is never read by your own build.** It is written for other people. What is in
your tree is `project.trb`, and a build evaluates it — so an absent or stale `settings` section in your own lock is
not an error, not a warning and not something a build fixes on the way past. The reason is pitfall 2 turned into a
rule: if every build rewrote the section, a `BUILD_NUMBER` would change a checked-in file on every CI run, and the one
file that is supposed to be stable would be the noisiest thing in the repository. So:

- **`settings` is written by `torb publish`, by `torb pack`, and by an explicit `torb lock`.** Nothing else writes it.
- **`graph` is written by `torb add`, `torb remove` and `torb update`**, exactly as CONCEPT already says.
- **`torb run`, `build`, `test` and `check` read `graph`, never write it, and refuse when it does not match
  `project.trb`** — also exactly as CONCEPT already says — and they ignore `settings` completely.

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
- **The inputs of an evaluation are recorded, by name and by hash, never by value.**

  ```trb
  // build/manifest-inputs.trb - written by the build. Do not edit.
  file "project.trb", hash: "sha256:..."
  file "VERSION", hash: "sha256:..."
  variable "CI_COMMIT_TAG", hash: "sha256:..."
  variable "BUILD_NUMBER", unset: true
  ```

  An incremental build re-evaluates the manifest when one of the hashes no longer matches and not otherwise. That is
  sound precisely because the grant has no clock, no network and no writing: given the same files and the same
  variables, the evaluation has the same result.

  **A variable is never recorded by value.** A manifest may legitimately read something that is a secret in a build
  that is not publishing, and a build artifact that held the value would be a place a secret ends up without anybody
  deciding that it should. The hash is taken over the name and the value together, so two variables that happen to
  hold one string do not look alike. The honest limit: a hash of a low-entropy value — a flag, a four-digit number —
  is guessable, so this file is a local build artifact under `build/`, which `.gitignore` already covers and which
  nothing publishes, and it is never an input to anything that leaves the machine.

  **A variable that was *not* set is recorded too.** `Environment.get "BUILD_NUMBER"` answering `None` is a read whose
  answer changes the moment somebody sets it, so `unset: true` is an input like any other. Without it, exporting a
  variable would not invalidate the cache and the first build after it would be wrong.

  The lock file's `from { }` section (above) has the same two line shapes for the same two kinds of input. The
  difference is lifetime and audience: `build/manifest-inputs.trb` is rewritten by every build and read by the next
  one, and `from { }` is written when the package is locked and read by whoever looks at the package.

### Stage 0 has no sandbox

Probe 21: `Sandbox` is ``Unknown name `Sandbox` `` on stage 0, and the native back end refuses `Script`. So none of
this section runs until the VM does (7.x), and stage 0's reading of a `project.trb` stays what it is — a line scan for
`name "..."`, used to build the stable path in a panic message.

**What works before the VM exists is the static subset, which is every setting in the repository's own thirty-five
manifests.** `language`, `name`, `prelude`, `dependencies`, `source`, `registry`, `workspace` and `program` — section
10's static nine rows — are plain literals, read from the syntax tree, and a `version` written as a literal is read by
the same pass. A manifest that computes anything is
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
- **The published package's file list.** The `settings` section of `project.lock.trb` (section 8) carries every
  resource path the package's own code names, as `resource "..."` lines, so a consumer can see the non-code files of a
  dependency without evaluating anything and `torb publish` can refuse a package whose code names a file the archive
  does not contain.
- **The one thing `project.trb` says about resources is a budget for what gets embedded.**

  ```trb
  resources {
    embeddedWarningAbove 4.megabytes()
    embeddedErrorAbove 64.megabytes()
  }
  ```

  **What is measured is the embedded total per program** — the sum of every `EmbeddedBytes` and `EmbeddedText` that
  ends up in that program's binary. Above the first number the build report says so; above the second the build fails
  and names the three largest contributors. **A single file gets no limit of its own**, deliberately: one oversized
  file blows the total as well, so a per-file limit catches nothing extra, while the case it *would* be needed for — a
  hundred files of two hundred kilobytes each — is exactly the case a per-file limit does not catch. One number for
  the thing that actually costs build time is better than two numbers that disagree about which one fired.

  **The defaults are `4.megabytes()` and `64.megabytes()`, argued from measurement** (`docs/RESOURCES.md` probe 3:
  1 MB embeds in a 21 s build, 16 MB in a 196 s build, so a megabyte of payload is about twelve seconds of build
  time). Four megabytes is roughly a minute of build — the point at which somebody notices a rebuild and deserves to
  be told why. Sixty-four is roughly thirteen minutes and about four hundred megabytes of generated C, which is past
  "slow" and into "the CI job is broken and nobody knows it"; that is the accident — an `assets/` directory embedded
  by a loop somebody wrote without thinking — and an accident is worth a stop. Raising either is one line.

  **The "warning" is a line on the build report, not a new diagnostic severity.** The toolchain has errors and notes
  and nothing in between today, and one budget is not a reason to introduce a third severity with the flags that come
  with it. `torb build` already reports what it wrote; this is one more line of that report. If a warning severity
  ever arrives for a reason of its own, this becomes one and the setting does not change.

  **Neither setting is static** (section 10): nothing before evaluation needs them, no editor, registry or `torb add`
  reads them, and they are about *your* build rather than about the package, so they are not in the lock's `settings`
  either — the same place `profile` and `test` sit.

  **`megabytes()` moves.** `kilobytes`, `megabytes` and `gigabytes` on `Int64` live in `std/sandbox` today, where
  `SandboxCapabilities.limits` needs them. They belong in `std/number`, which the prelude already exports, so that a
  `project.trb` can write `4.megabytes()` with no import and there is still exactly one definition for both callers.
- **Nothing else in `project.trb` configures a resource, and a budget is not the exception it looks like.** No
  `assets:` list, no loader table, no per-file setting. The objection this design makes to SwiftPM's `resources:` is
  that it is a second source of truth about *which files exist*, which can disagree with the call sites; a budget
  says nothing about which files exist and cannot disagree with anything — it is a limit on a number the build
  computes from the call sites themselves. A
  manifest that listed assets would be a second source of truth about the same files, which is section 1's disease,
  and a manifest that configured loaders would be a build script, which is CONCEPT's non-goal.

## 10. The vocabulary

Every setting of `project.trb`. **Static** means the toolchain reads it from the syntax tree without evaluating the
file, which is what stage 0 does today, what an editor wants before anything is checked, what a registry wants without
running anything, and what the checker needs before it checks a file.

| Setting | Type | Default | Static | Error |
|---|---|---|---|---|
| `language "0.3.0"` | `String` | none | **yes** | an older toolchain refuses the project, and says so before reading anything else |
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
| `resources { embeddedWarningAbove }` | `Int64` | `4.megabytes()` | no | negative, or above `embeddedErrorAbove` |
| `resources { embeddedErrorAbove }` | `Int64` | `64.megabytes()` | no | negative |

The first nine are **static**, and that is a promise with teeth: they are top-level command calls whose arguments are
plain string literals. Everything below them may be computed, because nothing is read before the file can be
evaluated.

**`version` moved.** The previous round listed it among the static settings. Section 8's whole point is that
`version Environment.get("CI_COMMIT_TAG") ?? File.readText("VERSION")?.trim()` is the motivating example, so `version`
is exactly the setting that must be allowed to compute. What a registry and a consumer need is the version of a
*published* package, and that comes from the `settings` section of its `project.lock.trb`, where it is a literal
again. A tool that wants the version of a project it is not building reads that lock; a tool that wants the version of
a project it *is* building evaluates the manifest. There is no third answer and there does not need to be.

### `language`, the minimum-version setting

```trb
language "0.3.0"
```

**The setting versions one thing, which is why it is one number, and that thing is the language.** The standard
library is part of the language here, not a layer beside it: `std` ships with every toolchain, it is never chosen or
pinned separately, and everything a program does not use is erased from the binary anyway, so there is no cost that
would make a separable `std` worth having. That is the difference from Rust, where `std` is a real layer — `no_std` is
a thing you can be, a target can lack it, and a crate can be written against a language edition without it. Nothing
here is like that. **The language version and the `std` version always coincide, so the number is the language's and
so is its name.**

A `torb` binary is a front end, a back end, `runtime/*.c` and `std/*` built from one commit. There is nothing to
version separately — no "language edition" next to a "tool version", no SDK next to a compiler — and the setting says
one thing: *this project needs the language at least this new*. It is the one setting an **older** toolchain has to
understand, which is why it is read first, before anything else in the file, and why adding it late would mean the
versions that cannot read it already exist.

**The message an older toolchain prints is the one thing to get right**, because it is the only thing that toolchain
will ever say about the project:

```text
error: This project needs language 0.3.0, this is 0.2.1
 --> project.trb:1:10
  |
1 | language "0.3.0"
  |          ^^^^^^^
  = Nothing else in this file was read. Install a newer toolchain
```

Both numbers, in that order, and the note says that nothing else was read — so nobody goes looking for a second
problem that the toolchain never got far enough to find.

| Ecosystem | What it is called | What it versions |
|---|---|---|
| **Rust** | `rust-version = "1.75"` in `[package]` | the toolchain: compiler plus `std` |
| **Go** | `go 1.21` in `go.mod` | the language version *and* the toolchain, in one line |
| **Node** | `"engines": { "node": ">=18" }` | the runtime only; the compiler is somebody else's |
| **Dart** | `environment: sdk: '>=3.0.0'` in `pubspec.yaml` | the SDK: compiler, runtime and core libraries |
| **Swift** | `// swift-tools-version:5.9`, a **comment on the first line** of `Package.swift` | the manifest format itself |

Dart is the closest match, because its SDK is the same bundle ours is. Swift is the instructive one: the version lives
in a *comment*, on the first line, because `Package.swift` is a program and the version has to be readable before the
program can be parsed. A `project.trb` needs no such hack — the setting is a static command call (section 8), read
from the syntax tree before anything is evaluated, so it can be an ordinary line like every other setting.

**What was considered**, with `language` first:

| Candidate | For | Against |
|---|---|---|
| **`language "0.3.0"`** — decided | names what is actually versioned, once `std` is understood as part of the language; a full word that needs no gloss; nothing in the sentence "this project needs language 0.3.0" has to be looked up | it is not the name of the thing you install, so a user who has to *act* on the message installs "torb" — which the note in the diagnostic says |
| `torb "0.3.0"` | the tool's own name is the thing you installed | it versions the tool rather than what the tool implements, and a reader might briefly expect it to *configure* `torb`, with `program "torb"` three lines away in one repository |
| `toolchain "0.3.0"` | what Rust calls it and what this document still calls the program in prose | "toolchain" is a word a user has to learn maps to "the thing you installed", and it names the packaging rather than the contract |
| `torbscript "0.3.0"` | the language's name, spelled out | the language's name in a file of that language is noise, and it reads like a dependency on a package called `torbscript` |
| `minimumVersion "0.3.0"` | precise about the comparison | silent about the minimum version *of what*, which is the only interesting part |
| `requires "0.3.0"` | short | reads like a dependency, and dependencies are two lines below |

The prose in this document and in the toolchain's own messages still says **toolchain** for the program that reads the
file, and that is not a contradiction: the toolchain is what you install, the language is what it implements, and the
setting is a statement about the second.

**Which file the static reader is pointed at depends on whose package it is**, and that is the whole of section 8 in
one table:

| What is being read | The file | How |
|---|---|---|
| the project `torb` was invoked in | its `project.trb` | statically; then evaluated if a setting is not a literal |
| a workspace member | its `project.trb` | the same |
| a `path:` or `git:` dependency | its `project.trb` | the same, without the environment |
| a **registry package** | its `project.lock.trb`, `settings` section | statically, always — its `project.trb` is never parsed by the toolchain |
| the dependency graph of the build | the root's `project.lock.trb`, `graph` section | statically |
| an editor or a registry looking at a foreign package | its `project.lock.trb`, `settings` section | statically |

So a project file may do this —

```trb
name "acme/shop"
version Environment.get("CI_COMMIT_TAG") ?? File.readText("VERSION")?.trim() ?? "0.0.0"

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

**One `project.lock.trb`, at the root, for every member — and it is the workspace's own lock, with one `graph`
section and one `settings` section per member.** Section 8 has the shape of the file; this is what the `graph` section
pins, per package:

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
for the same reason the tree is. A registry package's version is a literal in the `settings` section of its own lock
and needs nothing.

`torb run`, `build`, `test` and `check` read the lock and never write it, and refuse when it does not match
`project.trb`. `torb add`, `torb remove` and `torb update` write it. Both halves are already CONCEPT's position; what
this document adds is that the same static subset (section 10) is what makes the writing half possible on a file that
is code.

The exact fields of `project.lock.trb` stay where CONCEPT left them — open, because there is no registry protocol to
resolve against yet. What is decided here is what has to be pinned, not how it is written.

## 12. Migration

Nine slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations. Slices 1 to 6 need nothing that does not exist; slices 7 and 8
need the VM; slice 9 is `docs/RESOURCES.md`'s own plan.

**All of this lands after the repository has moved to `static` and `var fn`**, because that round touches practically
every `.trb` file and nothing should be rebased across it.

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The names decide.** `isEntryFile` stops reading `buildInput` and reads the file name; `isLibraryModule` and it stop disagreeing; `packageAt` takes the whole package directory instead of `src/` plus `testInput`; `torb build` picks its entry from the programs; `torb test` collects `*.test.trb` below the package. `Manifest` loses `buildInput` and `testInput` | `compiler/src/project/{manifest,workspace}.trb`, `compiler/src/semantics/graph.trb`, `compiler/src/semantics/checker/declaration.trb`, `compiler/src/cli/{build,test}.trb`, `compiler/tests/project.test.trb` | **Medium.** Probe 18 says the thirty-two `build { input "src/lib.trb" }` blocks are currently switching a rule off; removing them turns that rule back on, so every `src/lib.trb` in the repository is checked for top-level code for the first time |
| 2 | **The manifests.** The `build { }` and `test { input }` blocks go out of all thirty-five `project.trb` files; `Build` is deleted from `std/project` and `Test` keeps only `coverageThreshold` | `std/project/src/lib.trb`, every `project.trb`, `docs/standard-library/project.md` | **Low**, and it is the slice that proves slice 1, because nothing may change behaviour |
| 3 | **Programs.** `Program` and `Project.program` in `std/project`; the static reader reads `program` lines; `torb run <name>`, `torb build <name>`, the "name one" diagnostic, the library-only "nothing to build"; the ten diagnostics of section 4; `"owner/name/main"` and an imported `entry` become errors; `compiler/project.trb` writes `program "torb"` | `std/project/src/lib.trb`, `compiler/src/project/manifest.trb`, `compiler/src/cli/{build,run,test}.trb`, `compiler/src/semantics/graph.trb`, `compiler/project.trb` | **Low.** No file moves and no import changes. The one thing to watch is stage 0's hardcoded `src/main.trb` for a directory argument (`bootstrap/crates/torb-cli/src/main.rs`), which has to learn the `program` lines so that `torb run <dir>` of a renamed default still finds it |
| 4 | **Profiles and targets.** `--profile`, `--release`, `--target`, with `dev` as the default; `build/<profile>/<program>`; the `profile` block in the vocabulary and in the static reader; `output` on a `program` taken literally | `compiler/src/cli/build.trb`, `std/project/src/lib.trb`, `compiler/src/project/manifest.trb` | **Low.** `buildTarget = "release"` is one constant today, and the layout already has the shape |
| 5 | **The specifier grammar.** One function that takes a specifier apart, with a message per shape: a dot in a relative component, a `scheme:`, a host-qualified owner, a `..` inside a package path, a climb out of the package | `compiler/src/semantics/graph.trb`, `compiler/src/semantics/scope.trb`, `compiler/tests/check.test.trb` | **Low**, and it is the slice with the most new diagnostics, so it is mostly tests with exact messages |
| 6 | **Sources and the static subset.** `source` in `std/project` and in the static reader; a plain-string rule with a diagnostic for the nine static settings; `language`, `description`, `license`, `repository` | `std/project/src/lib.trb`, `compiler/src/project/manifest.trb`, `compiler/tests/project.test.trb` | **Low** on its own. It does not resolve anything — resolution needs the registry protocol, which is CONCEPT's open question |
| 7 | **The manifest that reads.** The toolchain becomes a `Sandbox` caller: the grant of section 8 (files, the environment for the invoked project and its members only, three modules), the evaluation only when the static read is not enough, `build/manifest-inputs.trb` by name and hash, the diagnostics for a failing script | `compiler/src/project/*`, `compiler/src/cli/*`, `std/sandbox`, `std/environment`, the VM | **Highest, and blocked.** Probe 21: `Sandbox` runs on neither implementation, so this slice cannot start before 7.x. Nothing in the repository's own manifests needs it, which is what makes waiting free |
| 8 | **The locked manifest.** `Lock` in `std/project` with its `settings` and `graph` sections; the deterministic printer that writes an evaluated `Project` back as literals in a fixed order; `torb lock` and `torb lock --check`; `torb publish` writing and verifying `settings` and printing `from`; both files travelling in an archive; the consumer side reading a dependency's `settings` instead of its `project.trb` | `std/project/src/lib.trb`, `compiler/src/project/*`, `compiler/src/cli/*` | **Medium, and it needs slice 7 in front of it.** The printer is the interesting half: "a value is its constructor call" has to hold for the whole vocabulary, `torb lock --check` is the gate that says it is deterministic, and `torb publish`'s static re-read is the one that says it round-trips |
| 9 | **Resources.** `docs/RESOURCES.md`'s slices, which are a plan of their own | see that document | see that document |

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
  towards `Package.swift` and exactly one: the grant is read-only, below one directory, plus the environment, with
  three modules and a step limit, and a published package carries a locked manifest that nobody evaluates at all.
  There is no way to write, to fetch, to spawn or to ask the machine what day it is, and there is no place to hang
  one.
- **Not a place for capabilities.** What a script may do is granted at the call site that loads it — including
  when the caller is the toolchain and the script is `project.trb`. What a package may touch is visible from its
  imports. Neither is a setting.
- **Not a loader system, and not an asset list.** `docs/RESOURCES.md` is written so that nobody has to reconstruct
  the argument, and neither an extension-loader table nor a `resources:` list is part of this design.
- **Not a path type.** Every path in a `project.trb` and every specifier is a `String` the workspace reader
  interprets; `std/path`'s `Path` is for files, and `docs/PATH.md` says why the two stay apart.
- **Not a change to what `public` means.** A package's surface is still its `public` declarations, reached through
  `"owner/name"` and `"owner/name/path"`. Nothing here adds a way to hide a module or to expose one twice.
- **Not a second manifest format, and not a third file.** There are two files a project has: `project.trb`, which a
  person maintains, and `project.lock.trb`, which the toolchain writes and which holds both the locked settings and
  the pinned graph. `build/manifest-inputs.trb` is a build artifact, not a manifest. All of them are receiver scripts
  in the same vocabulary, and there is no `.toml`, no `.json` and no generated file that is not TorbScript.
- **Not a convention for a second program.** There is no `src/<name>/main.trb` and no `programs/<name>.trb`. A second
  program is rare, and a convention that reserves a directory name in every package forever to shorten a rare line is
  a bad trade.

## 14. Open

Everything technical above is decided, and so is the one naming question this document used to carry: the
minimum-version setting is `language` (section 10). Nothing is open here.
