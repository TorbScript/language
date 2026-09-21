# Resources

**A file a program needs is named by a string literal, and the signature of the call says what kind of file it is.**
`SpriteSheet.load("./hero.png")` ships the file next to the binary and reads it when the program runs;
`SpriteSheet.embedded("./hero.png")` puts its bytes in the binary. Neither is an import, neither is a loader chosen by
an extension, and neither runs anything at build time that the author did not write as an ordinary function call. A
string *literal* whose expected type is a resource type **is** a resource, the way `1` is a `Float` where a `Float` is
expected. A `String` variable is never one.

```text
  what the signature says                what the build does              what the caller gets

  embedded(text: EmbeddedText)   ──→  the bytes are static data    ──→  String, synchronous, infallible
  embedded(bytes: EmbeddedBytes) ──→  the bytes are static data    ──→  Bytes,  synchronous, infallible
  load(file: Resource)           ──→  the file ships beside the    ──→  Task<Result<Bytes, ResourceError>>
                                      binary, found relative to
                                      the PROGRAM
  read(path: Path)               ──→  nothing: the compiler has    ──→  Task<Result<..., IoError>>, and the
                                      never seen this path              import of std/fs says it reads a disk
```

- **[1. What the toolchain does today](#1-what-the-toolchain-does-today)** — four probes, run in the worktree that wrote this
- **[2. What other systems do](#2-what-other-systems-do)** — seven of them, and the one shape worth taking
- **[3. A literal adapts; a `String` never converts](#3-a-literal-adapts-a-string-never-converts)** — exactly which expressions count
- **[4. Three types, one parameter kind](#4-three-types-one-parameter-kind)** — and why one type whose member decides is rejected
- **[5. Three verbs across `std`](#5-three-verbs-across-std)** — `embedded`, `load`, `read`, and what happens to `Sandbox.load`
- **[6. Relative to what, and what a resource is called](#6-relative-to-what-and-what-a-resource-is-called)** — the writing file, the stable name, case, directories
- **[7. L2: a `.trb` resource is type checked at build time](#7-l2-a-trb-resource-is-type-checked-at-build-time)**
- **[8. L3: the loader is an ordinary function](#8-l3-the-loader-is-an-ordinary-function)** — and what the compiler may fold
- **[9. What the build knows because of this](#9-what-the-build-knows-because-of-this)** — packaging, watching, the C back end, the web
- **[10. Migration](#10-migration)** — six slices, four of which need no VM
- **[11. What this is not](#11-what-this-is-not)**
- **[12. Open](#12-open)**

This design adds one parameter kind to the checker and three types to a new `std/resource`, and it changes one
signature that runs on neither implementation today (`Sandbox.load`). It adds no syntax, no keyword, no annotation and
no build step. `docs/PROJECT.md` section 9 says what the project file and the build output layout have to say about
it; everything else is here.

**The repository is migrating to `static` and `var fn`**, so a member that belongs to the type is written
`static origin = Point(0, 0)` and `static fn from(source: Source): Self`, a method lists no `self`
(`fn area(): Int`), a mutating method is `var fn translate(deltaX: Int)`, and `const` is optional on a field. Every
snippet below is in that form; function *types* keep their receiver (`(var self: SandboxCapabilities) => Void`).

---

## 1. What the toolchain does today

Four probes, each a package in a workspace of its own, checked and built with the self-hosted compiler on a stage 0
built in this worktree. A claim below that no probe covers is marked as unproven where it stands.

### Probe 1 — a parameter of a type the compiler does not know specially

```trb
/** A stand-in for the `Resource` of this design. */
public type Resource {
  /** Where the file is. */
  path: String
}

/** Takes the type the way `SpriteSheet.load` would. */
fn load(resource: Resource): String {
  resource.path
}

/** A generic parameter, the shape of `Sandbox.load<Value>`. */
fn loadAs<Value>(path: Resource): String {
  path.path
}

print load("./hero.png")
print loadAs<Int>("./level.scene.trb")
print load(Resource("./ok.png"))
```

```text
error: Expected `Resource`, found `String`
  --> ../probe/baseline/src/main.trb:19:12
   |
19 | print load("./hero.png")
   |            ^^^^^^^^^^^^

error: Expected `Resource`, found `String`
  --> ../probe/baseline/src/main.trb:20:19
   |
20 | print loadAs<Int>("./level.scene.trb")
   |                   ^^^^^^^^^^^^^^^^^^^

2 problems in 1 of 2 files
```

**A string literal adapts to nothing.** There is no mechanism to build on and no partial one to finish: section 3 is
entirely new checker work. The generic case behaves like the plain one, so the parameter kind does not have to fight
inference. One further fact from the same probe: `public native type Resource` in a package that is not part of the
standard library is ``error: `native` is only allowed in a package of the standard library``, so the three types of
section 4 must live in `std`, which they do.

### Probe 2 — how a string literal that is a module constant is emitted natively

```trb
/** A plain string literal as a module constant - the shape an embedded text would have. */
public const schema = "create table users (id integer primary key)"

/** The name of the table, so the next one is interpolated rather than plain. */
public const table = "users"

/** An interpolated one, for comparison. */
public const computed = "create table {table}"
```

Both constants come out as **static data** and neither is a `FunctionKind.ConstantCell`:

```c
/* The storage of every string literal: immortal, so a write through one always copies. */
static struct { torb_header header; uint32_t capacity; uint8_t data[43]; }
  s_probe_x2f_literal_data_schema_storage = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 43, { 0x63, 0x72, 0x65, ... } };

/* The compile-time constants of the program. */
static const torb_text s_probe_x2f_literal_data_schema = { (torb_bytes *)&s_probe_x2f_literal_data_schema_storage, 0, 43 };
```

**The interpolated one is folded too** — `"create table {table}"` becomes an eighteen-byte static block, not a call of
`Add.add` at first read. So an embedded text needs no new emission machinery at all: `EmbeddedText.text()` is exactly
this, and section 8's folding has a working precedent in the emitter that exists.

**And the purity rule of a module constant is narrower than section 8 needs.** The same probe, with
`public const computed = "create table " + "users"`:

```text
5 | public const computed = "create table " + "users"
  |                         ^^^^^^^^^^^^^^^^^^^^^^^^^
  = A module has no initialization order: literals, the operators of the number types and of `Bool`, interpolation,
    tuple, list and map literals, constructor calls of such, and other compile-time constants. `Point(0, 0)` is one,
    `Point.origin()` is a call and is not
```

So `static hero = SpriteSheet.embedded("./hero.png")` — L3's motivating line — **does not check today**, because
`embedded(...)` is a call. Section 8 is where that is dealt with, and the answer is not to widen the rule now.

### Probe 3 — how large a literal the C back end emits

A generated module with one string literal, built end to end. gcc 13.2, 16 cores, release, Windows.

| Payload | The `.trb` file | `check` | `build --emit-c` | full `build` | the C | the binary | it runs |
|---|---|---|---|---|---|---|---|
| 1 MB of ASCII | 1 048 624 B | 9.1 s | 16.4 s | 20.9 s | 6 295 281 B | 1 208 253 B | yes |
| 16 MB of ASCII | 16 777 264 B | 66.0 s | 143.0 s | 195.7 s | 100 667 124 B | 16 936 893 B | yes, in 0.03 s |

**Nothing breaks at either size, and the cost is linear and steep.** The emitter writes one `0x..,` per byte, so the C
is about **6.0 times** the payload; the C compiler is about a quarter of the wall time at 1 MB and about a quarter at
16 MB, so no single stage dominates. `docs/BACKEND.md` states no size limit for a literal, and the C standard's
4095-character limit on a *string literal* does not apply, because the emitter never writes one — it writes a byte
array initializer, whose only standard minimum is the 65 535-byte object limit that every real C compiler is far past.
The practical guidance section 9 draws from this: under a megabyte, embedding is free; at sixteen, it is three minutes
of build time somebody will notice.

### Probe 4 — is `Sandbox.load` usable at all today?

```trb
use Sandbox from "std/sandbox"

/** The receiver a loaded script configures. */
public type ServerConfig {
  /** The port the server listens on. */
  var port: Int = 8080
}

const chosen = "./does-not-exist.trb"

const missing = Sandbox.load<ServerConfig>("./nowhere/at/all.trb")
const computed = Sandbox.load<ServerConfig>(chosen)

print missing.isOk()
print computed.isOk()
```

```text
2 files, no problems
```

The checker accepts all of it — the generic argument, a trailing capability block, a path that names no file, a path
that is a computed variable. Then:

```text
$ torb run ../probe/sandbox
error: Unknown name `Sandbox`
  at probe/sandbox/src/main.trb:11:16

$ torb run ../compiler build ../probe/sandbox
error: a value of type `Script` is not supported by the native back end yet (at probe/sandbox/src/main.trb:11:16)
error: the type `Script` is not supported by the native back end yet
2 problems the native back end cannot compile yet, nothing was built
```

**`Sandbox` is a signature and nothing else**: it runs on neither implementation. Two things follow. Changing its
signature (section 5) costs nothing, because no program can be relying on the current one. And L2 (section 7) cannot
be built before the VM, which is what puts it in slice 5.

## 2. What other systems do

| System | Compile-time embedding | Shipped file at run time | How the file is named | Who decides which |
|---|---|---|---|---|
| **Rust** | `include_bytes!`/`include_str!`, a macro | nothing; you write your own path arithmetic | a literal, relative to the **file** | the macro you call |
| **Zig** | `@embedFile`, a builtin | nothing | a literal, relative to the **file** | the builtin you call |
| **Go** | `//go:embed` on a variable, with `embed.FS` for a tree | nothing | a pattern, relative to the **directory** | a comment above a declaration |
| **Deno** | an import attribute (`with { type: "text" }`) | `Deno.readFile` under a permission | a specifier, relative to the file | the attribute |
| **Node, bundlers** | a loader chosen by extension, configured in the bundler | `fs.readFile` | a specifier | a configuration file, far from the call |
| **SwiftPM** | nothing; everything is a bundle resource | `Bundle.module.url(forResource:)`, a run-time lookup by name | a `resources:` list in the manifest, then a **string at run time** | the manifest |
| **Dart, Flutter** | nothing | `rootBundle.load(key)` | an `assets:` list in `pubspec.yaml`, then a string at run time | the manifest |

**What we take.** Rust's and Zig's shape — a file named by a literal that the compiler resolves, relative to the file
that writes it — and nothing of how they spell it. `include_bytes!` and `@embedFile` are a macro and a builtin because
those languages have no way for a *parameter* to say "I take a literal". TorbScript does: that is what `lazy` and
`Expression<Value>` already are, and CONCEPT's rule is exactly the one this needs — **"a call has one signature, and
that signature says how its arguments are read"**. So the mechanism is an ordinary function with an unusual parameter
type, and the resource types can be used by any function anybody writes, which is more than a builtin gives.

Go's `embed.FS` is the one thing worth watching: a whole directory as one value, which section 6 deliberately leaves
out for now and says why.

**What we leave.**

- **A bundler's loader table.** `import x from "./a.svg"` means a URL, a string, a React component or a data URI
  depending on a configuration file nobody reads, and the same source file means different things in two builds.
- **A manifest list plus a run-time string key** (SwiftPM, Flutter). It is two sources of truth that can disagree, and
  the disagreement is a run-time failure with a string in it. The call site already knows; asking the manifest as well
  only adds a way to be wrong.
- **A comment that declares something** (`//go:embed`). A declaration that lives in a comment is an annotation, and
  CONCEPT has no annotations.
- **A macro or a builtin.** There are none, and this design needs none.

## 3. A literal adapts; a `String` never converts

**A string literal whose expected type is a resource type is a resource.** This is the rule that already exists for
numbers, applied to one more expected type: the literal takes its type from where it is written, and an expression
that is not a literal takes no type from anywhere. There is no `Into<Resource>`, no `From<String>` and no coercion:
`docs/PATH.md` records the same wish for `Path` and calls it a candidate for later, which is the same mechanism and
would be the same decision.

**Exactly these expressions are a resource literal:**

| The expression | Is it | Why |
|---|---|---|
| `load("./hero.png")` | **yes** | a string literal whose expected type is a resource type |
| `load(sheet: "./hero.png")` | **yes** | a label changes nothing about where the expected type comes from |
| `["./a.spr", "./b.spr"]` for `List<Resource>` | **yes**, each element | the element's expected type is the resource type; this is how a set of files is written until section 6's `ResourceDirectory` exists |
| `fn load(sheet: Resource = "./default.spr")` | **yes** | a default is written at the declaration, so it resolves against the *declaring* file, which is section 6's rule unchanged |
| `load("./sheets/{name}.spr")` | **no** | interpolated |
| `load("./a" + "/b.spr")` | **no** | a call of `Add.add`, and probe 2 shows the module-constant rule already draws that line here |
| `const path = "./hero.png"` then `load(path)` | **no** | the binding is a place with no expected type, so the literal is a `String` there and a `String` never converts |
| `static path = "./hero.png"` then `load(path)` | **no** | the same, and see below |
| `load(chosen)` for any variable | **no** | the value is known when the program runs |
| `load(other)` where `other: Resource` | **yes**, trivially | it is already a resource; a resource is an ordinary value once it exists |

**A constant initialised by a literal is deliberately not a resource literal**, and this is the one place the rule
gives something up. `const one = 1` does not make `one` usable where a `Float` is expected either, and the value of
having *one* rule for literals is larger than the value of the exception. The wish behind the exception — naming the
assets of a package in one place — has a better answer that costs nothing:

```trb
extend SpriteSheet {
  /** The hero's sheet, embedded once. */
  static hero = SpriteSheet.embedded("./hero.png")
}
```

The literal is at the call site where its expected type is right there, and the constant holds the *loaded sheet*
rather than a path — typed, built once, and the compiler still sees the file. A constant that holds a path holds the
one thing about a resource that is worth nothing to a reader. Section 8 is what makes this form legal.

### The messages

```text
error: A `Resource` is a file the compiler resolves, so it has to be written as a literal here
 --> src/sprites.trb:7:32
  |
7 | const sheet = SpriteSheet.load(chosen)
  |                                ^^^^^^
  = `chosen` is a `String`, and its value is only known when the program runs. A path chosen at run
    time is a `Path`: `File.readText(path)` reads one, and `use File from "std/fs"` is what says so
```

```text
error: A `Resource` is a file the compiler resolves, so it cannot be interpolated
 --> src/sprites.trb:7:32
  |
7 | const sheet = SpriteSheet.load("./sheets/{name}.spr")
  |                                ^^^^^^^^^^^^^^^^^^^^^
  = Write one literal per file, or read a path at run time with `File.readText(path)`
```

```text
error: There is no file `sheets/hero.spr`
  --> src/sprites.trb:12:33
   |
12 | static hero = SpriteSheet.embedded("./sheets/hero.spr")
   |                                    ^^^^^^^^^^^^^^^^^^^
   = A resource path is relative to this file, and `acme/game/src/sheets/hero.spr` is not there
```

A missing file is a **build error at the line that names it**, which is the single largest thing this design buys and
the one that has nothing to do with embedding.

## 4. Three types, one parameter kind

```trb
/**
 * A file that ships with the program and is read while it runs. The build puts it beside the binary; the run time
 * finds it relative to the *program*, never to the working directory.
 */
public native type Resource {
  /** What the file is called, project-relative and always with `/` - what a diagnostic and a log print. */
  name: String

  /** The bytes. Fails if the file was not shipped or cannot be read, and waits, because on the web it is a fetch. */
  native fn bytes(): Task<Result<Bytes, ResourceError>>

  /** The bytes as text. Bytes that are not UTF-8 are a `ResourceError` and never a replacement character. */
  fn text(): Task<Result<String, ResourceError>>
}

/** A file whose bytes are in the binary. Synchronous and infallible: the compiler has already read them. */
public native type EmbeddedBytes {
  /** What the file is called, project-relative. */
  name: String

  /** The bytes, as static data. */
  native fn bytes(): Bytes
}

/** A file whose text is in the binary. The compiler checked that it is UTF-8, so reading it cannot fail. */
public native type EmbeddedText {
  /** What the file is called, project-relative. */
  name: String

  /** The text, as static data. */
  native fn text(): String
}
```

**Three types and one rule.** For the checker they are one parameter kind: a parameter whose type is one of the three
takes a literal, the literal is resolved against the writing file, and the resolved file is recorded with the call.
Everything after that is a property of the type and not a new mechanism.

### Why the embedding type is two types and `Resource` is one

The asymmetry is not taste, it follows from where failure lives.

- **An `Embedded*` read cannot fail**, because the bytes are in the binary. So if there were one embedding type with
  both `bytes()` and `text()`, `text()` would have to be fallible — for a file the *build* had in its hands and could
  have checked. The compiler knows at the literal whether the bytes are UTF-8; throwing that away and handing the
  program an `Option` is giving up a build error to gain nothing. Two types keep `text()` infallible and put the UTF-8
  check where it belongs: at the literal, as a build error naming the byte offset.
- **A `Resource` read can fail anyway** — the file may be missing, truncated, or on a network that answered 404. So
  folding "these bytes are not UTF-8" into the same `ResourceError` costs nothing and adds no type. One `Resource`
  with both `bytes()` and `text()` is right for exactly the reason two embedding types are.

### Why not one type whose member decides

The alternative is one `Resource` type where `resource.bytes()` reads the shipped file and
`resource.embeddedBytes()` means "put this in the binary instead". **Rejected, for two independent reasons.**

1. **It needs whole-program flow analysis, and it does not terminate at analysis.** To decide whether to ship or embed,
   the compiler must know at the *literal* which member will eventually be called on the value. A `Resource` handed
   through three functions makes that an interprocedural question; a `Resource` stored in a field, put in a list,
   returned from a trait member or captured by a closure makes it undecidable. And the failure when the analysis
   cannot answer is not a diagnostic — it is a binary sixteen megabytes bigger than intended, or a file that was not
   shipped and is missing on a customer's machine. A rule whose failure mode is silent is worse than a rule that asks
   for one more word.
2. **The two have different signatures and always will.** `Resource.bytes()` is `Task<Result<Bytes, ResourceError>>`;
   `EmbeddedBytes.bytes()` is `Bytes`. A single type would have to answer the fallible, waiting one in both cases,
   which throws away precisely what embedding buys. The point of embedding is not that the bytes are in the file, it
   is that reading them is a load instruction.

This is the same argument CONCEPT makes for `lazy` and `Expression<Value>`: **the signature decides at the call site,
and nothing about a call is a function of what happens to its result.**

### Why `Resource` waits

`Resource.bytes()` returns a `Task`, on every target, including the ones where the file is on a local disk and the
read is instantaneous. That is a colouring decision and it is made on purpose.

- **The web forces it.** In a page, a shipped file is a `fetch` and there is no way to block. If the native signature
  were synchronous, the same source would need a different signature on the web target, and "one program, both back
  ends" would stop being true for every program that reads a resource.
- **CONCEPT already has the rule.** "Asynchrony lives in the type system, not in a keyword. A function that returns
  `Task<Value>` may call `await()`." A `Task` whose value is already there is exactly what `File.add` returns today
  on native, so the shape and its runtime exist.
- **The escape is the point of the split.** Code that must not be a `Task` — a decoder, a `Describe` implementation, a
  `static` initializer — uses `EmbeddedText`/`EmbeddedBytes` and is synchronous and infallible. The two types are not
  an optimization with the same interface; they are the two answers to "may this wait", and choosing one is the whole
  decision a caller makes.

The honest cost: a library that reads one shipped configuration file becomes a library whose function returns a
`Task`, and every caller of it does too. CONCEPT worries about exactly this colouring for decoders and refuses it
there. The difference is that a decoder is handed bytes somebody else read, so it has a choice; a resource read *is*
the IO, so it has none.

## 5. Three verbs across `std`

A naming convention, so that a reader of any signature in the standard library knows what the build will do without
looking anything up:

| Verb | The parameter | When it happens | Fallible | Waits | What the import says |
|---|---|---|---|---|---|
| `embedded(...)` | `EmbeddedBytes` / `EmbeddedText` | build time | no | no | nothing: it is data in the binary |
| `load(...)` | `Resource` | run time, from beside the program | yes | yes | nothing: the *build* shipped the file |
| `read(...)` / `open(...)` | `Path` | run time, from wherever the program was pointed | yes | yes | `use File from "std/fs"` — this touches the user's disk |

**The third row is the one that carries a capability, and the first two are the ones that do not.** That is worth
being explicit about, because it looks like a security claim and is not one: an `EmbeddedBytes` is bytes the build put
in the binary, and a `Resource` is a file the build put beside it — in both cases the *author of the program* chose
the file, at compile time, and a reader can see every one of them in the frozen manifest. Reading them grants nothing
that compiling the program did not already grant. `std/fs` stays the import that means "this program reads files the
user names", and nothing in this design weakens it.

### `Sandbox`, which needs all three

`Sandbox.load` today takes `path: String` and resolves it against the **project directory**, which for a deployed
program is the working directory. Under the three verbs that is the wrong name for the wrong thing: a configuration
file of a deployed program is usually a *user's* file, not a shipped one, and the two cases are different enough that
one member cannot serve both.

```trb
public native type Sandbox {
  /** Checks an embedded `.trb` script against `Value`. The bytes are in the binary, so only the checking can fail. */
  native fn embedded<Value>(
    script: EmbeddedText,
    capabilities: (var self: SandboxCapabilities) => Void = {},
  ): Result<Script<Value>, SandboxError>

  /** Checks a shipped `.trb` script against `Value`. The compiler resolves the literal and checks the script (section 7). */
  native fn load<Value>(
    script: Resource,
    capabilities: (var self: SandboxCapabilities) => Void = {},
  ): Task<Result<Script<Value>, SandboxError>>

  /** Checks a script the program was pointed at while it ran: `--config`, a user's scene, a plug-in directory. */
  native fn read<Value>(
    path: Path,
    capabilities: (var self: SandboxCapabilities) => Void = {},
  ): Task<Result<Script<Value>, SandboxError>>
}
```

- **Today's `Sandbox.load` becomes `Sandbox.read`.** What it does today — take a path that is known when the program
  runs and resolve it against the working directory — is exactly what `read` means, so the behaviour keeps its
  meaning and only loses the name. The name goes to the shipped case, which is the one that deserves the short word.
- **Changing it costs nothing.** Probe 4: `Sandbox` runs on neither implementation, so there is no program to break.
- **`project.trb` is a `read`, not a `load`.** The toolchain is handed a directory on a command line and finds a
  `project.trb` in it; that path is not known when the toolchain is compiled. `docs/PROJECT.md` section 8's grant is
  therefore written against `read`, and the frozen manifest changes nothing about that.
- **A scene file is a `load`.** `docs/ECS.md` writes `Sandbox.load<Scene<World>>("./scenes/level.trb")`, and under
  this design that literal is a `Resource`: the scene ships beside the game, the compiler type checks it at build time
  (section 7), and `instance "scenes/asteroid.trb"` stays what ECS says it is — a method of the receiver, resolved by
  the host against the scene root, and not a resource literal at all, because it is written in a *script* and a
  script has no compiler to resolve it.

## 6. Relative to what, and what a resource is called

**A resource literal is relative to the file that writes it.** That is `use`'s rule, and it is the only rule that
works: a library's `SpriteSheet.embedded("./hero.png")` must find the library's own `hero.png` whoever calls it, and
an application's `SpriteSheet.load("./hero.png")` must find the application's.

- **A library's assets travel in its package.** They are files below the package directory, so they are part of what
  is published (`docs/PROJECT.md` section 3), and the frozen manifest lists every one the package's own code names.
- **An application's literal is the application's file**, even when it is passed to a library's function. The literal
  is written in the application, so it resolves there. Nothing about the library's location enters into it.
- **The resolution happens once, in the checker, and what is recorded is a name.**

### The stable name

A resource is identified at run time by `<owner>/<name>/<path inside the package>` — the same text the compiler
already builds for a panic site (`stablePathOf` in `ir/lower/lower.trb`, `stable_path` in stage 0's `program.rs`),
with `/` on every platform and no directory of the build machine in it. `acme/game/src/sheets/hero.spr` is a stable
name. The layout beside the binary is that name under the program's resource directory:

```text
build/<profile>/<program>                                      the binary
build/<profile>/<program>.resources/acme/game/src/sheets/hero.spr
```

Using the same text the panic paths use is not a saving of code, it is a saving of *one* thing a reader has to know:
a file's name in an error message is a file's name in an error message, whether the error is a panic or a missing
resource.

- **Two literals that resolve to one file are one resource.** The identity is the stable name after normalization, so
  `"./hero.png"` and `"./a/../hero.png"` in one file, and a literal in two packages that reaches the same file, all
  name the same resource; it ships once and is embedded once. A `..` is allowed as long as it does not leave the
  package.
- **A resource outside the package directory is an error.** `"../../secrets/key.pem"` names a file that is not part of
  the package, so it cannot be published and the build cannot ship it. The message names the package and its
  directory, exactly like section 6 of `docs/PROJECT.md` does for a `use` that climbs out.
- **Case is compared byte for byte, on every platform.** The compiler resolves a literal against the *listing* of the
  directory, not against the file system's own matching, so `"./Hero.png"` next to a file called `hero.png` is an
  error on Windows and macOS too, where the open would have succeeded. `docs/PATH.md` already decided this for paths
  ("not case insensitive, on any platform, ever"), and the reason is sharper here: the alternative is a build that
  works on the author's machine and fails on the deployment's, which is the single most expensive bug shape a resource
  system can have. It costs one directory listing per directory that holds a resource, cached for the build.

### Directories and globs — not yet

A `ResourceDirectory` type, or a glob in the literal, is the obvious next thing and is deliberately **not** part of
this design. It has questions this design does not have to answer: is the listing fixed at build time or read at run
time; is a file added to the directory after the build part of it; what is a directory on the web, where there is no
directory to list. Until somebody has a real case, the answer that costs nothing is a list literal, which section 3
already allows:

```trb
extend Level {
  /** Every level of the game, in order. */
  static all = [
    Scene.embedded("./levels/1.scene.trb"),
    Scene.embedded("./levels/2.scene.trb"),
    Scene.embedded("./levels/3.scene.trb"),
  ]
}
```

Explicit, diffable, checked file by file, and it fails at build time when a level is renamed. When it is a hundred
levels, that is when `ResourceDirectory` earns its questions, and Go's `embed.FS` is the shape to start from.

## 7. L2: a `.trb` resource is type checked at build time

`Sandbox.load<Scene>("./levels/1.scene.trb")` names a file the compiler can open and a type the compiler knows, so
**a type error in a scene or in a shipped configuration is a build error.** Execution stays exactly where it is: at
run time, in the sandbox, with its own heap and its own limits.

### What the checker needs

CONCEPT already says how a script is checked: as the body of `(var self: Value) => Void`, with the file scope "the
prelude, and nothing else", plus whatever the **module allowlist** adds. Two of the three are static at the call site
— the path is a literal, the receiver type is the type argument. The third is the question.

**The allowlist is granted in the trailing block, and the block is a closure.** `modules "acme/game/components",
"std/linear"` looks like a list of literals and usually is one, but it is a receiver closure body, so it may hold an
`if`, a loop or a computed name. So the rule has to say what happens then, and "check it when we can" is the one
answer that must not be chosen: a checker rule that is sometimes on is worse than one that is off, because a project
loses build-time checking by an edit somewhere unrelated and nothing says so.

**Decided: `Sandbox.load` and `Sandbox.embedded` require a statically readable capability block, and that is part of
their signature's promise.**

- A capability block is **statically readable** when every statement in it is a command call of a
  `SandboxCapabilities` member whose arguments are plain literals. That is the same rule `docs/PROJECT.md` section 10
  applies to the manifest's static nine, one level down, and it covers every capability block in the repository's
  documents and examples.
- A block that is not statically readable is an **error at the call site**, naming the statement and pointing at
  `Sandbox.read` — which takes a `Path`, checks nothing at build time, and is the member for a script whose shape is
  decided while the program runs.
- So the three members differ in exactly the property that makes L2 possible: `embedded` and `load` know the file and
  the allowlist at build time and check the script; `read` knows neither and checks it when it loads.

### What it means for incremental checking and the module graph

A `.trb` resource is a node of the module graph with an unusual shape: it has no name, nothing can import it, and its
scope is the prelude plus the allowlist rather than the package.

- **Its edges are three**, and the graph already has the last two kinds: the file itself, the receiver type, and every
  module in the allowlist. A change to any of them re-checks the script, and a change to the script re-checks nothing
  else, because it exports nothing.
- **It is checked once per call site, not once per file.** The receiver type and the allowlist come from the call, so
  two calls that load one file against two receiver types are two checks. That is also the cache key:
  `(stable name, receiver type, allowlist)`.
- **It does not take part in the export fixpoint.** A module's exports are computed to a fixpoint because modules can
  see each other; a script exports nothing and nothing can see it, so it is a leaf. Adding leaves is the cheapest
  thing that can happen to that algorithm.
- **A cycle is impossible.** A script cannot be imported, so nothing can point back at one. Scene-to-scene nesting is
  a run-time relation through the receiver (`instance` is a method, `docs/ECS.md`), not a graph edge — which is why
  ECS gives it a depth limit and a `SceneError` rather than a compile-time cycle check.
- **The cost is that `torb check` now opens files that are not `.trb` modules**, and that a scene with a type error
  makes `torb check` red. Both are the feature.

## 8. L3: the loader is an ordinary function

**There is no loader mechanism. `SpriteSheet.embedded(...)` is a function somebody wrote, and its return type is the
type of the thing.** Nothing in the language knows what a sprite sheet is, no extension maps to anything, and adding a
format is adding a function.

```trb
extend SpriteSheet {
  /** Parses a sheet whose bytes are in the binary. */
  static fn embedded(file: EmbeddedBytes): SpriteSheet {
    SpriteSheet.parse(file.bytes())
  }

  /** Reads a sheet that ships beside the program. */
  static fn load(file: Resource): Task<Result<SpriteSheet, ResourceError>> {
    Ok SpriteSheet.parse(file.bytes().await()?)
  }
}
```

```trb
extend SpriteSheet {
  /** The hero's sheet. */
  static hero = SpriteSheet.embedded("./hero.png")
}
```

That last line is a `static` whose initializer is a call, so by `docs/BACKEND.md` it is a `FunctionKind.ConstantCell`:
built once, at its first read, into a block that is never freed, with no module initialization order anywhere.

**It does not check today** — probe 2: the purity rule for a module or type constant allows literals, the number and
`Bool` operators, interpolation, collection literals, constructor calls of those and other constants, and **never a
function call.** That rule stays exactly as it is until the VM exists. With the VM it gains exactly one case: *a call
of a function the compiler can evaluate at build time*, which is what the rest of this section defines. Before then, a
loader is a `fn` and not a `static`, and the only thing lost is that the sheet is parsed at every call.

### What the compiler may do, and what "pure" has to mean

**With the VM, the compiler MAY evaluate such an initializer at build time and embed the result.** The embedding
format is `docs/ENCODING.md`'s principle: a value is its constructor call, so the built `SpriteSheet` is written back
as the constructor call that produces it, and that call is what the binary holds. This is constant folding — the
program behaves identically either way — and it turns a corrupt asset into a build error.

**"Pure", exactly, and all six conditions have to hold:**

1. **It terminates.** Enforced by the VM's step and time limits. Exceeding them is a build error naming the constant,
   never a hang.
2. **It reads nothing but its arguments and other compile-time constants.** No IO of any kind. An `EmbeddedBytes` is
   not IO — its bytes are an argument the compiler already holds. A **`Resource` is IO and can never be folded**: the
   file is not shipped yet when the build runs, and there is nothing to read. That is the sharp line the two types
   draw a second time: `embedded` folds, `load` never does.
3. **It is deterministic.** No clock, no environment, no process, no randomness, and no iteration order that depends
   on the address of anything — a map whose order came from pointer identity would make two builds of one commit
   differ, which is the one property the whole toolchain is built on.
4. **It does not panic.** A panic while folding is a **build error at the line of the constant**, carrying the panic's
   own message. That is the feature and not the cost: a truncated PNG stops the build instead of crashing on a
   customer's machine.
5. **Nothing it touches is `shared`.** A `shared type` has an identity — a handle, a socket, a mutable cell somebody
   else can see — and an identity is not a value, so there is no constructor call that reproduces it.
6. **It produces no function value.** A closure's code is in the binary but its captures are not its constructor, so
   there is no call that writes it back. A value with a function-typed field is not embeddable, and the message says
   so at the field.

**What the value's type must satisfy**, so that "a value is its constructor call" can actually write it:

- **It round-trips through its own constructor**: what `Encode` writes, `Decode` reads back as the same value. For a
  type with the derived implementations (`docs/ENCODING.md` section 3) that is free. A type that implements `Encode`
  by hand is embeddable only if its hand-written form is still the constructor call `Decode` reads — and if it is not,
  the compiler cannot tell, so **a hand-written `Encode` disqualifies a type from folding** and the message names it.
- **Every field's type satisfies the same, transitively**, including the items of a collection and the payload of a
  case.
- **No `private` field is in the way.** A `private` field takes the constructor with it (the constructor-probe round
  found this), so a type whose constructor is not reachable from where the embedding is written cannot be written
  back. The embedding is emitted *inside the defining package*, which is where the constructor is reachable, so this
  holds by construction — but it is why the embedding cannot simply be a blob the linker drops in from elsewhere.

### What is NOT promised

- **Folding is never guaranteed.** The compiler *may* fold. A program must behave identically whether it did or not,
  which the six conditions ensure, and a build is free to skip folding for a value whose embedding would be larger
  than the work it saves.
- **There is no way to demand it.** No keyword, no attribute, no `constexpr`. Such a keyword is the door to
  compile-time programming, which CONCEPT lists as a possible future and not as a v1 feature, and the observable
  difference between a folded and an unfolded constant is build time and binary size — which are not semantics. The
  one thing somebody legitimately wants, "tell me this was not folded", belongs in `torb build --statistics`.
- **It is not compile-time evaluation anywhere else.** `static` and module constants are the only place, because they
  are the only place with no observable timing. A folded expression in a function body would be an optimization the
  back ends may do and this document says nothing about.
- **It does not promise a smaller binary.** A decoded `SpriteSheet` may be larger than the PNG it came from. The
  reasons to fold are that the work disappears and that a corrupt asset becomes a build error; bytes are not one of
  them.

## 9. What the build knows because of this

**After checking, the build has the complete set of files the program needs, with a type on each.** That set is what
every item below is.

- **Packaging.** Each `Resource` is copied to `<program>.resources/<stable name>`; each `EmbeddedBytes` and
  `EmbeddedText` is emitted as static data. Nothing is guessed and nothing is listed twice.
- **The published package's file list.** Every literal in the package's *own* files goes into the frozen manifest
  (`docs/PROJECT.md` section 8), so a consumer sees the non-code files of a dependency without evaluating anything,
  and `torb publish` refuses a package whose code names a file the archive does not contain.
- **Watch mode.** The resource set is a file set, so `torb build --watch` watches it. Today it can only watch `.trb`
  files, because they are the only files it knows exist.
- **Stage 0.** `torb run` interprets from the source tree, so **the file is simply there**: a `Resource` resolves to
  the file at its stable name inside the workspace, and nothing is copied. An `EmbeddedText` is read from the tree at
  the moment its constant is first read. That is observationally identical except for one case — a program that
  rewrites its own asset while it runs would see the new bytes on stage 0 and the old ones in a binary — which belongs
  in the stage-0-versus-binary list, not in the language.
- **The C back end.** Static data, measured in probe 3: about six bytes of C per byte of payload, nothing breaking at
  1 MB or at 16 MB, 21 s and 196 s of build time respectively. So `torb build` reports the embedded total, and a
  program whose embedded bytes pass a threshold gets a note that names `Resource` as the other option. A note and not
  an error: it is the author's trade to make, and the numbers are the argument.
- **The conformance suite.** One program per behaviour, byte-identical on both implementations: an embedded text, an
  embedded binary whose bytes are not UTF-8, a shipped resource read successfully, a shipped resource that is missing
  at run time, and a program that prints a resource's `name`. The last one matters most, because the stable name is in
  every error message, so it is in every `.expected` file, so the two implementations are held to one spelling of it.
- **The web target, later.** `Resource.bytes()` is a `fetch` of `<program>.resources/<stable name>` relative to the
  module's URL; an `Embedded*` is a byte range in the wasm data segment. Both signatures are already right for that,
  which is the whole reason `Resource` waits (section 4).

## 10. Migration

Six slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations. Slices 1 to 4 need nothing that does not exist; slices 5 and 6
need the VM (7.x).

**All of this lands after the repository has moved to `static` and `var fn`, and after `docs/PROJECT.md`'s slices 1
to 4**, because a resource's stable name is a package-relative path and section 3 of that document is what settles
which files belong to a package.

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The parameter kind.** `std/resource` with `Resource`, `EmbeddedBytes`, `EmbeddedText`, `ResourceError`; the checker's literal rule and its four diagnostics; resolution against the writing file with a directory listing for case; the recorded stable name in the IR | `std/resource/*`, `compiler/src/semantics/checker/{expression,call}.trb`, `compiler/src/ir/*`, `compiler/tests/check.test.trb` | **Highest of the six.** It is a new parameter kind, which touches the machinery `lazy` and `Expression<Value>` use, and probe 1 says there is nothing there to build on |
| 2 | **Embedding.** `EmbeddedBytes.bytes()` and `EmbeddedText.text()` as static data in the C back end and in stage 0; the UTF-8 check at the literal; the first conformance programs | `compiler/src/backend/c/*`, `bootstrap/crates/torb-interpreter/src/natives.rs`, `bootstrap/tests/native/` | **Low.** Probe 2 says the emitter already writes exactly this shape for a string literal |
| 3 | **Shipping.** `<program>.resources/`, the run time finding the program's own directory, `Resource.bytes()` as a ready `Task`, `ResourceError`; stage 0 reads from the tree | `runtime/*`, `compiler/src/cli/build.trb`, `bootstrap/crates/torb-cli/*` | **Medium.** "Where is my own binary" is a platform call on each platform, and the two implementations must agree on the error text |
| 4 | **The build's file set.** Packaging, `torb publish`'s list in the frozen manifest, `--watch`, the embedded total in `--statistics` | `compiler/src/cli/*`, `compiler/src/project/*` | **Low**, and it is the slice that pays for slice 1 |
| 5 | **L2.** `Sandbox.embedded`/`load`/`read`; the literal capability block and its diagnostic; the script as a leaf of the module graph with its three edges and its cache key | `std/sandbox/src/lib.trb`, `compiler/src/semantics/{graph,check}.trb`, the VM | **Blocked.** Probe 4: `Sandbox` runs on neither implementation |
| 6 | **L3.** The one new case in the module-constant purity rule; folding a pure initializer in the VM; the embedding through `Encode`; the six conditions as diagnostics | `compiler/src/semantics/checker/declaration.trb`, `compiler/src/ir/*`, the VM | **Blocked, and the largest.** It is compile-time evaluation with a fence around it, and every one of the six conditions is a message somebody will read |

**The prose.** A new `docs/standard-library/resource.md`; `docs/standard-library/sandbox.md` (three members instead of
one); `docs/tooling/torb-build.md` (the output layout and the embedded total); `docs/ECS.md`'s loader snippet;
CONCEPT's decision log gains one entry; `docs/internals/index.md` lists this document, which is already done.

## 11. What this is not

- **Not a loader chosen by an extension.** `.png` means nothing to the compiler, and there is no table anywhere that
  maps a suffix to a function. The function is chosen by the caller, which is the thing bundlers give up and then
  spend a configuration language getting back.
- **Not `use logo from "./logo.png"`.** An import binds names, and an asset has no names to bind. Making it an import
  would be three of CONCEPT's non-goals at once: it is a **build script** ("installing never runs code, there are no
  build scripts"), it makes an **import run something** ("nothing runs when a module is imported", which is what makes
  cyclic imports free and lets the checker compute exports to a fixpoint), and it needs the **checker to know a type
  before the code that produces it has run** — so either the loader declares types, which is meta-programming, or the
  checker evaluates arbitrary user code, which CONCEPT lists as a possible future and not a v1 feature.
  `docs/PROJECT.md` section 6 reserves the dot in a module component so that this stays unavailable rather than
  half-available.
- **Not an asset pipeline.** Nothing converts, compresses, resizes, atlases or transcodes. A loader is a function, and
  a function is not a pipeline. A project that wants a texture atlas builds one and checks it in, or writes a program
  that builds one and runs it — and that program is an ordinary `program` line (`docs/PROJECT.md` section 4), run by a
  person, not a hook the build fires.
- **Not a list in the manifest.** SwiftPM's `resources:` and Flutter's `assets:` are a second source of truth that can
  disagree with the call sites, and the disagreement is a run-time failure with a string in it.
- **Not a capability.** An `Embedded*` is data in the binary and a `Resource` is a file the build shipped; in both
  cases the author chose the file at compile time and every one is listed in the frozen manifest. `use File from
  "std/fs"` stays the import that means "this program reads files the user names", and nothing here weakens it.
- **Not a virtual file system.** There is no mounted tree, no `open("res://…")`, no run-time lookup by string. A
  resource is reached through the value the literal produced, and a program cannot ask for a resource it did not name.
- **Not a directory or a glob, yet.** Section 6 says what a `ResourceDirectory` would have to answer first, and what
  to write until then.
- **Not a `Path`.** A resource has a *name*, not a path: it is project-relative, always `/`, and it is not a thing
  `std/fs` will open for you. `docs/PATH.md`'s `Path` is for files the program is pointed at.
- **Not a change to how a script runs.** L2 checks a `.trb` resource at build time and changes nothing about
  execution: it still runs in the sandbox, at run time, with a heap and limits of its own, and `Script.apply` still
  reports what went wrong while it ran.

## 12. Open

Everything technical above is decided. These are taste or direction, and only the owner answers them.

1. **`EmbeddedBytes` and `EmbeddedText`, or one `Embedded` with both members?** The document splits them, because one
   type would make `text()` fallible for a fact the build had in its hands (section 4). The cost is two names in `std`
   instead of one — though a caller writes neither, since `SpriteSheet.embedded("./hero.png")` names no type at all;
   only the author of a signature ever types them. If one name is wanted anyway, the price is `text(): String?`, and
   a build error becomes an `Option` somebody unwraps.
2. **Is `Resource.bytes()` returning a `Task` the right trade?** Section 4 argues it is forced by the web and is what
   the two types exist to let a caller escape. It is still a colouring decision, and CONCEPT refuses the same
   colouring for decoders in so many words. The alternative is a synchronous `Resource` on native with a different
   signature on the web, which gives up "one program, both back ends" for the programs that read a file.
3. **Should `torb build` refuse an embedded total above some size, or only say so?** Probe 3 measured the cost: 1 MB
   embeds in 21 s, 16 MB in 196 s. The document reports it and does not refuse, because the trade is the author's.
   A threshold that errors would catch the accident — a whole `assets/` directory embedded by a loop somebody wrote
   without thinking — at the price of a flag to turn it off.
