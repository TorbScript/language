# File Paths

**Status: partly implemented** — slice 1 of section 8, `std/path` itself, is in; the boundary to `std/fs` and the
compiler's own paths (slices 2 to 5) are not, and `std/path/tests` does not build natively yet (`docs/RUST-EXIT.md` 2.4).

**A path is a root and a list of components, and never a string.** That is the whole design of `std/path`. Everything
else follows from it: `joined` cannot be given a root, `parent` cannot fall off the top, `..` cannot be resolved by
accident, and one text form is what a path shows on every platform while the operating system gets its own form at the
one place that talks to it.

```text
                    ┌── root()        ─────  None | Unix | Drive('C') | Share(server, share)
      Path ─────────┤   (both private, both read through an accessor: Path is a capsule)
                    └── components()   ─────  the components between the separators

  a String ──→ From<String>, infallible ──→ Path ──→ show(), always with `/`  ──→ a message, a diagnostic, a Map key
                                              │           = String.from(path), the other half of the pair
                                              └──→ systemPath(), in `std/fs` ──→ the bytes the operating system gets
```

- **[1. The type](#1-the-type)** — the capsule, the roots, and what `==` means
- **[2. Parsing](#2-parsing)** — the separators, the empty string, `.` and `..`
- **[3. The members](#3-the-members)** — every signature, and `PathError`
- **[4. `joined` with an absolute argument](#4-joined-with-an-absolute-argument)** — three options weighed, one chosen
- **[5. `Into<Path>` as a parameter type](#5-intopath-as-a-parameter-type)** — what a compiled probe proved
- **[6. The boundary to the operating system](#6-the-boundary-to-the-operating-system)**
- **[7. The sandbox](#7-the-sandbox)** — what `inside` promises and what it does not
- **[8. Migration](#8-migration)** — six slices, each green on its own
- **[9. What this is not](#9-what-this-is-not)**
- **[10. Open](#10-open)**

`std/path` replaces three path layers that exist today: `compiler/src/project/path.trb` (seven functions over strings,
which does not know a drive root), `compiler/src/documentation/tree.trb` (`fileName`, `folderOf`, `lastSlash`,
`joinDocumentationPath`, written again by hand), and about twenty places that build a path with string interpolation
(`"{root}/{path}"`). It is a package of its own and not part of `std/fs`, because nothing in it touches a disk.

---

## 1. The type

```trb
/** Where a path starts. A path without one is relative. */
public type Root with Show, Equals, Hash {
  /** `/`: the root of the file system, and on Windows the root of the current drive. */
  case Unix
  /** `C:/`: one drive, with an absolute path on it. The letter is upper case. */
  case Drive(letter: Char)
  /** `//server/share`: a UNC share. */
  case Share(server: String, share: String)
}

/** A file path: where it starts, and the components between the separators. */
public type Path with Show, Equals, Hash, Compare {
  /** Where the path starts, and `None` for a relative path. [Path.root] reads it. */
  private storedRoot: Root?
  /** The components between the separators, outermost first. [Path.components] reads them. */
  private storedComponents: List<String>

  fn root(): Root?
  fn components(): List<String>
}
```

**Absolute or relative is a question a member answers**, not a case of the type: `isAbsolute()` is `root.isSome()`.
There is no third answer, because the one thing that would need one — Windows' drive-relative `C:foo`, which means
"wherever I last was on drive C" — is per-drive process state and not a value. So it is not representable, and section 2
says what parsing does with it.

**`Path` is `Equals`, `Hash` and `Compare`, and all three are lexical and case sensitive on every platform.** A `Path`
is a value, `==` on a value compares content, and the content is the root and the components. A case-insensitive `==`
on Windows would make `Map<Path, Module>` answer differently on two platforms, which is the one thing the language does
not allow (CONCEPT, design principle 5) — and it would still be wrong, because case folding on a real machine is a
property of the mount and not of the operating system. **Whether two paths name the same file is a question for the
file system**, it is asked separately, and `std/path` does not answer it.

`compare` orders by the root first (`None` before `Unix` before `Drive` before `Share`, and within a case by its
fields), then component by component, then by the number of components. It is hand written, because `List<Item>` is
`Equals` and `Hash` but not `Compare`, and because comparing `show()` would order `a.b` before `a/b` — a text order is
not a path order.

**`Path` is a capsule, so `From<String>` is the one way in.** Both fields are `private` and neither has a default, so
nothing outside `std/path` can call the constructor and no caller can hand in a component list of its own — which is
what used to make `Path(None, ["", "src"])` a value whose `show()` had a doubled separator. The two fields are read
through [`root()`](#3-the-members) and `components()`, and the accessor is what every caller writes.

**The capsule costs nothing here, because `Path` has a conversion pair.** `Path` has `From<String>` and `String` has
`From<Path>` — the same text `show()` answers — so `Encode` and `Decode` are derived through that pair
(`docs/design/ENCODING.md` section 3a): a path in a configuration file or in a JSON document is its text, which is what a
reader of that document expects and what a hand-written `encode` would have had to write anyway. `copy` and a `Path`
pattern are closed with the constructor, and neither was part of this type's vocabulary: every member that changes a
path answers a new one.

## 2. Parsing

```trb
/** A path is what the text says it is. Whether it names a file is what the file system says when it is opened. */
extend Path with From<String> {
  static fn from(value: String): Path
}
```

**`From<String>` is infallible**, as the owner's outline says: there is no text a file system is guaranteed to reject,
so a parse that answered a `Result` would answer `Ok` for everything and cost a `?` at every call site.

**Both `/` and `\` separate, on every platform.** Not `\` on Windows only. The reason is determinism: the same program
has to behave the same on stage 0 and in a compiled binary, and ideally on every platform, so `Path.from("a\\b")` must
be one value and not two. The cost is stated as a pitfall, and the capsule sharpens it: a POSIX file whose name
literally contains a backslash cannot be named as a `Path` at all, because `From<String>` is the one way in, and the
file is reached through `std/fs` with its text. The opposite cost — a program that reads a configuration file on Linux
and splits it differently on Windows — is the one the language refuses to pay.

| Input | `root()`, `components()` | Shows as |
|-------|-------|----------|
| `""` | no root, no components | `.` |
| `"."` | no root, no components | `.` |
| `"src/"` | `None`, `["src"]` | `src` |
| `"src//main.trb"` | `None`, `["src", "main.trb"]` | `src/main.trb` |
| `"./src/./x"` | `None`, `["src", "x"]` | `src/x` |
| `"../src"` | `None`, `["..", "src"]` | `../src` |
| `"/"` | `Some(.Unix)`, `[]` | `/` |
| `"/usr/bin"` | `Some(.Unix)`, `["usr", "bin"]` | `/usr/bin` |
| `"c:\\x"` | `Some(.Drive('C'))`, `["x"]` | `C:/x` |
| `"C:"` | `Some(.Drive('C'))`, `[]` | `C:/` |
| `"C:foo"` | `Some(.Drive('C'))`, `["foo"]` | `C:/foo` |
| `"//server/share/x"` | `Some(.Share("server", "share"))`, `["x"]` | `//server/share/x` |

**Empty components and `.` are dropped at construction, `..` is kept.** The split is not a matter of taste: `a/./b` and
`a/b` name the same file on every file system, whatever links are on the way, because `.` always resolves to the
directory itself. `a/../b` and `b` name the same file only when `a` is not a symlink. So dropping `.` is a fact and
dropping `..` would be a guess, and a construction never guesses.

**A trailing separator is dropped**, so `src/` and `src` are one value. A trailing separator says "this is a
directory", and a `Path` never claims to know that — the file system does.

**`C:foo` is read as `C:/foo`.** Drive-relative is the one form this type cannot hold, and rejecting it is not
available to an infallible parse. Reading it as absolute on that drive loses the only thing a drive-relative path could
mean, which is a per-process, per-drive current directory that no other platform has and that the language's
determinism rule already forbids. The value says what it became: `show()` answers `C:/foo` and not the input.

**`normalized()` is lexical and resolves `..`:**

- against the root it vanishes — `Path.from("/../a").normalized()` is `/a`, because no file system has a parent of its
  root (POSIX says `/..` is `/`), and the same for a `Drive` and a `Share`;
- at the front of a relative path it stays — `../a` normalized is `../a`, and `a/../../b` is `../b`, because there is no
  way to resolve it without knowing what the path will be resolved against;
- empty and `.` components are removed, which only a hand-written `Path` can still have.

`normalized()` is text arithmetic and says so in its first sentence: it does not read a disk and it does not follow a
link, so `a/../b` normalized to `b` is a different file whenever `a` is a symlink. That is why it is a member a caller
asks for and never something a construction does.

## 3. The members

```trb
public type Path with Show, Equals, Hash, Compare {
  private storedRoot: Root?
  private storedComponents: List<String>

  /** Where the path starts, and `None` for a relative path. */
  fn root(): Root?

  /** The components between the separators, outermost first. */
  fn components(): List<String>

  /** Whether the path starts at a root. Its opposite is [Path.isRelative]. */
  fn isAbsolute(): Bool

  /** Whether the path has no root, so that it means something only against a base. */
  fn isRelative(): Bool

  /** The last component: the name of the file or of the directory. `None` for a root and for the empty path. */
  fn name(): String?

  /** The name without its extension and without the dot. `main` for `src/main.trb`. */
  fn nameWithoutExtension(): String?

  /** The extension of the name, without the dot. `None` where the name has none, and for `.gitignore`. */
  fn extension(): String?

  /** The path one component up. `None` at a root and for the empty path: there is nothing above them. */
  fn parent(): Path?

  /** `relative` below this path. The root of `relative` is dropped, so the result never leaves `self`. */
  fn joined(relative: Into<Path>): Path

  /** The same path with its last component replaced. `None` where there is no name to replace. */
  fn withName(name: String): Path?

  /** The same path with a different extension, added where there was none. An empty `extension` removes it. */
  fn withExtension(extension: String): Path?

  /** Whether this path starts at the same root and with the same components as `prefix`. */
  fn startsWith(prefix: Into<Path>): Bool

  /** This path as seen from `base`, with a `..` per component of `base` that is not shared. */
  fn relativeTo(base: Into<Path>): Path?

  /** `.` and empty components dropped and `..` resolved as text. It reads no disk and follows no link. */
  fn normalized(): Path

  /** This path resolved below `base`, refusing to leave it. */
  fn resolved(inside: Into<Path>): Result<Path, PathError>

  /** The path with `/` as the separator, on every platform. `.` for the empty path. */
  fn show(): String

  /** Root first, then component by component, then by length. Case sensitive everywhere. */
  fn compare(other: Path): Ordering
}

/** What a path operation refuses. */
public type PathError with Show, Error {
  /** `resolved(inside:)` would have left `base`. */
  case Outside(path: Path, base: Path)
  /** `resolved(inside:)` was given a base that has no root, so "inside" has nothing to be measured against. */
  case NoBase(base: Path)
}
```

**`components()` is a method and the field behind it is `storedComponents`**, which is the naming a capsule takes: a
field and a method never share a name, so the field says what it stores and the method what it answers. `path.root()`
and `path.components()` are the two reads, and both are total.

**No member panics**, as the std rule requires: what can be absent answers an `Option`, what can be refused answers a
`Result<_, PathError>`, and everything else is total.

| Answers | Members | Why |
|---------|---------|-----|
| a value | `isAbsolute`, `isRelative`, `joined`, `startsWith`, `normalized`, `show`, `compare` | they are defined for every path |
| `Option` | `name`, `nameWithoutExtension`, `extension`, `parent`, `withName`, `withExtension`, `relativeTo` | a root and the empty path have no name and no parent; two different roots have nothing in common |
| `Result<_, PathError>` | `resolved(inside:)` | it is the one member that refuses something, and the refusal is the point |

`withName` and `withExtension` answer `None` exactly when `name()` does, which is the symmetry a reader can rely on
without reading a table. `relativeTo` answers `None` when the two roots differ — today's `relativePath` answers the
absolute path there, and answering `None` lets the caller decide instead of guessing for it.

`PathError` has two cases and not more, because those are the two things this package can refuse. Everything else a
path could go wrong about — it does not exist, it is a directory, it is not readable — is an `IoError` and belongs to
whoever opened it.

## 4. `joined` with an absolute argument

`base.joined(Path.from("/etc/passwd"))` is the trap this section closes. With one type the signature cannot forbid it,
so one of three things has to happen. The `joined` call sites decide it: there are about twenty-five in
`compiler/src/`, and in every one of them the argument is a literal (`"src"`, `"project.trb"`, `"include/torb.h"`).

| | What happens | Cost |
|---|---|---|
| **(a)** `joined` answers `Result` | the caller writes `?` or `expect` | twenty-five `?`s for a condition only a literal could violate. A `Result` nobody can produce is exactly the `expect` noise the owner refuses |
| **(b)** a second type `RelativePath` | the type says it | `joined`, `parent`, `name`, `extension`, `components`, `startsWith`, `relativeTo`, `normalized`, `Show` and `From<String>` almost all again, or a shared trait to carry them; every signature in `std/fs` has to pick one of the two; and `Path.from(userInput)` cannot answer a `RelativePath` infallibly. The owner's outline decided one type |
| **(c)** `joined` drops the argument's root | `base.joined("/etc/passwd")` is `base/etc/passwd` | one sentence of documentation |

**(c) is chosen**, and its doc comment says it in the first sentence. Two reasons beyond the call sites. First, it is
the only one of the three that is both total and safe: (a) reports the problem and still has to be handled, and the
behaviour every other language has — an absolute argument *replaces* the base, as in Rust's `join`, Python's `/` and
Java's `resolve` — is not on the list at all, because that is the bug. Second, (c) makes the safety property a theorem
of the signature rather than a promise in prose:

> **`base.joined(other)` has `base`'s root and starts with `base`'s components, for every `other`.**

That is strictly stronger than a `Result` a caller could ignore. What it does **not** cover is `..` in the argument:
`base.joined("../../etc")` is text that escapes once it is normalized. Containment is `resolved(inside:)`'s job and
section 7 says exactly what it promises; `joined` is honest text arithmetic and nothing more.

The Range split in `TODO.md` is the case where a second type won, and the difference is worth naming: there the two
types have *different members* (a range without a start has no `iterate`), so the split removed two `expect`s and
added nothing. Here the two types would have the same members, and the split would double a surface to express a
condition that one sentence expresses.

### Ten call sites

The left column is `compiler/src/` today, the right is the same line with `Path`. Note that the shape of a command
call is unchanged wherever the argument is a literal, which is what makes this a rewrite and not a redesign of the
callers.

```trb
// project/read.trb:53
const manifest = joinPath current, "project.trb"
const manifest = current.joined "project.trb"

// project/read.trb:87 - `entry` is a name out of `File.list`
const child = joinPath path, entry
const child = path.joined entry

// project/read.trb:63,66 - the walk up to the root. `pathDepth` exists only for this guard
const parent = parentPath current
if parent == current || pathDepth(parent) < 1 {
  return Ok None
}
// ...becomes the loop's own condition, and `pathDepth` is deleted
while const Some(parent) = current.parent() {
  current = parent
}

// project/workspace.trb:123
var files = tree.below joinPath(directory, "src")
var files = tree.below directory.joined("src")

// project/workspace.trb:139
const text = tree.text(joinPath(directory, "project.trb")) ?? ""
const text = tree.text(directory.joined("project.trb")) ?? ""

// project/workspace.trb:62 - "every directory that has a project.trb"
var directories = tree.paths().filter({ pathName(_) == "project.trb" }).map(parentPath).toList()
var directories = tree.paths().filter({ _.name() == Some("project.trb") }).filterMap({ _.parent() }).toList()

// project/workspace.trb:103 - the `std/*` member pattern, with the byte slicing gone
const parent = joinPath root, pattern[0..(pattern.byteLength() - 2)]
const parent = root.joined(Path.from(pattern).parent() ?? Path.from(""))

// project/workspace.trb:52 - "which manifests own this directory"
const owners = manifests.filter({ isInside directory, _ }).toList().reversed()
const owners = manifests.filter({ directory.startsWith(_) }).toList().reversed()

// ir/instantiate.trb:526 - the same question, in the IR
if isInside(path, source) {
  relative = relativePath source, path
}
if const Some(relative) = path.relativeTo(source) {
  return relative
}

// cli/build.trb:199 - an interpolated path becomes three components
joinPath directory, "build/{buildTarget}/{name}"
directory.joined("build").joined(buildTarget).joined(name)
```

All ten run, on stage 0 and as a compiled binary, in probe 0 of section 5. Two things they showed that a design
document would otherwise have got wrong: `filterMap(Path.parent)` — a member as a function value — is *"not supported
by the back end yet: `parent` used as a function value, which would have to bind its receiver (milestone 5.11)"*, so
the closure is the form to write; and `pathDepth` exists for exactly one guard in the whole repository, which `parent()`
answering an `Option` removes, so the function does not survive the migration at all.

`joined` taking `Into<Path>` is what keeps the first, second, fourth and fifth of those reading as they read today. The
next section says whether that is available.

## 5. `Into<Path>` as a parameter type

`fn open(path: Into<Path>): Result<File, IoError>` — a trait as a type, no generics — is the signature the outline
asks for. **Five probes, run on stage 0, type checked by stage 1 and built natively. The type of section 1 and a trait
as a parameter type both work on both back ends; `Into` specifically does not, on either.** Everything below is copied
from the runs.

### Probe 0 — the type itself

`Root`, `PathError` and `Path` exactly as section 1 and section 3 declare them, with every member written out, plus
thirty-four lines of output covering every row of section 2's table, `resolved(inside:)` on five inputs, `==`, a
`Map<Path, Int>` key and a `sort`:

```text
===== CHECK =====
2 files, no problems
===== NATIVE BUILD =====
wrote ../scratch/shape-probe/probe.exe
===== NATIVE RUN vs STAGE 0 =====
IDENTICAL apart from line endings
```

The line endings are the compiled binary's `printf` on Windows and nothing to do with paths; the conformance runner
normalizes them itself (the conformance runner). So **every table in this document is measured
rather than asserted**, and the type needs nothing the back ends do not already have. The one thing `std/text` is
missing for it: there is no `lastIndexOf`, so `extension` splits the name on `.` instead of finding the last one.

### Probe 1 — a single-method trait as a parameter type, with a coercion from `String`

`trait PathValue { fn path(): Path }`, `extend String with PathValue`, `extend Path with PathValue`,
`fn openByTrait(path: PathValue): String`, called with a literal and with a `Path`.

```text
===== CHECK =====
2 files, no problems
===== STAGE 0 RUN =====
opened src/main.trb
opened /etc/hosts
===== NATIVE BUILD =====
wrote ../scratch/path-probe/probe.exe
===== NATIVE RUN =====
opened src/main.trb
opened /etc/hosts
```

### Probe 2 — a *generic* trait as a parameter type, the shape of `Into`

The same, with `trait Convert<Target> { fn convert(): Target }` and `fn open(path: Convert<Path>): String`:

```text
===== CHECK =====
2 files, no problems
===== STAGE 0 RUN =====
opened src/main.trb
opened etc/hosts
===== NATIVE BUILD =====
wrote ../scratch/into-probe/probe.exe
===== NATIVE RUN =====
opened src/main.trb
opened etc/hosts
```

So neither "a trait as a type" nor "a generic trait as a type" nor "a coercion from a foreign `extend`" is what is
missing. A witness for a generic trait is built, passed and dispatched through, in the interpreter and in the compiled
binary, with identical output.

### Probe 3 — `Into<Path>` itself

`extend Path with From<String>`, `extend Path with From<Path>`, `fn openByInto(path: Into<Path>): String`, three
spellings of the body:

```text
error: `Into<Path>` does not convert into `Path`
  --> ../scratch/into-probe/src/main.trb:30:24
   |
30 |   const target: Path = path.into()
   |                        ^^^^^^^^^^^
   = `into()` is the `From` of the target: `extend Path with From<Into<Path>>` provides it

error: `Into<Path>` has no member `to`
  --> ../scratch/into-probe/src/main.trb:42:23
   |
42 |   const target = path.to<Path>()
   |                       ^^
   = Did you mean `into`?

2 problems in 1 of 2 files
```

`Into.into(path)` — the trait's member reached through the trait instead of through the receiver — is the one spelling
that type checks. It gets no further:

```text
===== CHECK =====
2 files, no problems
===== STAGE 0 RUN =====
error: Unknown name `Into`
  at probe/into/src/main.trb:30:24
  in open
===== NATIVE BUILD =====
error: not supported by the back end yet: `into`, a member the back end cannot build an instance of (at probe/into/src/main.trb:34:12)
1 problems the back end cannot compile yet, nothing was built
```

### Probe 4 — the bound instead of the trait type

`fn openByBound<Source: Into<Path>>(path: Source): String` with `path.into()` type checks, and then:

```text
===== STAGE 0 RUN =====
error: the String "src\main.trb" has no method `into`
  at probe/path/src/main.trb:67:24
  in openByBound
===== NATIVE BUILD =====
error: not supported by the back end yet: a call the checker did not resolve (at probe/path/src/main.trb:67:24)
```

And `fn openByFrom<Source>(path: Source): String where Path: From<Source>` does not even type check:

```text
error: `from` takes none of these from a `Source`
  --> ../scratch/path-probe/src/main.trb:73:18
   |
73 |   const target = Path.from path
   |                  ^^^^^^^^^^^^^^
   = `from` exists for String, Path
```

### What is missing, exactly

**One condition in the checker.** `isConversionCall` in `compiler/src/semantics/checker/expression.trb:1959`
intercepts *every* argument-less call named `into` that has an expected type, and rewrites it into "the `From` of the
target". It never asks whether the receiver's own static type is `Into<Target>`, in which case `into` is that type's
one required member and needs no `From` at all. The fix is to let the receiver's own trait member win: when the
receiver's type is the trait `Into<Target>` itself, resolve `into` as an ordinary trait call through the witness the
value carries. Probe 2 shows the machinery behind that call already works in both back ends, so the fix is a checker
change and not a back-end one.

**The second gap is `Into` on a concrete type behind a bound**, which probe 4 hit: the blanket
`extend<Source, Target> Source with Into<Target> where Target: From<Source>` is not instantiated for a type parameter,
so `path.into()` under `<Source: Into<Path>>` reaches neither back end. That is the same family as item 2 of
`docs/design/ENCODING.md` section 13 ("a static trait member reached through a bound") and it is not on this design's path,
because the trait-typed signature does not need it.

**Until the first fix lands**, the interim signature is the concrete one and the conversion moves one character to the
call site:

```trb
public fn open(path: Path): Result<File, IoError>

const file = File.open(Path.from(text))?
const other = File.open(text.into())?              // `into()` on a concrete `String` works today
const third = File.open(directory.joined("x"))?
```

That costs one `.into()` where a `String` meets the boundary and nothing at all where a `Path` already exists. It is
not an `expect`, it works on both back ends today, and every signature that carries it changes to `Into<Path>` without
touching a single caller once the condition is in.

## 6. The boundary to the operating system

**The `/` form crosses the boundary, and each back end converts it on the way in.** The natives of `std/fs` keep taking
one text — what `show()` answers — and the platform's rules are applied in the two places that already exist for it:
`runtime/file.c`. There is no TorbScript-visible "native form",
because a member of `Path` whose text differs per platform is the one thing this type must not have.

The C runtime is most of the way there already. `torb_file_absolute_path` (`runtime/file.c:161`) rewrites every `\` to
`/`, upper-cases the drive letter, emits `C:/` as the root and resolves `.` and `..` against a stack — and its comment
says why: *"Separators come out as forward slashes, on every platform, so no path of a machine ever differs between
them beyond the drive letter."* `torb_platform_create_directory` already treats both separators. That is exactly the
contract this design wants, promoted from one function to the whole boundary.

**What has to be fixed, and it is a real bug today, not a new requirement.** Everything under `runtime/platform.c`
goes to the operating system as narrow `char *` through the CRT: `fopen`, `opendir`, `_findfirst`, `mkdir`, `_stat`.
There is no wide-character call and no `\\?\` anywhere in `runtime/`. So in a compiled binary on Windows:

- a path with a non-ASCII component is handed to the ANSI code page and fails or opens the wrong file, while stage 0
  (Rust, which encodes to UTF-16 itself) opens it — an observable divergence between the two implementations;
- a path over `MAX_PATH` cannot be opened at all.

The Windows half of `platform.c` therefore moves to `CreateFileW`, `FindFirstFileW`, `_wmkdir` and `_wstat` over
UTF-16, with **one** function that converts a shown path into what a call gets:

- every `/` becomes `\`;
- the text is encoded as UTF-16 (a `String` is always valid UTF-8, so this cannot fail);
- the prefix `\\?\` is added when the path is absolute, has no `.` or `..` component and is longer than 247 bytes —
  and a normalized absolute `Path` satisfies the middle condition by construction, which is why the prefix is safe to
  add at all. A `Share` root becomes `\\?\UNC\server\share`.

A rule that lives in more than one place drifts, so **one conformance program per rule in `tests/conformance/`** is
what holds them together — that suite builds and runs a program and compares its standard output, its standard error
and its exit code byte for byte with what is written down beside it, and it is the repository's answer to "one
behaviour, whichever back end runs it". The programs to add: a shown path on every root; an absolute path built from a
relative one; a long path written and read back; a non-ASCII name written, listed and read back; a directory entry
whose name is not UTF-8.

`\\?\` must never appear in output. Stage 0 stripped it (`display_path` in its `program.rs`, deleted with stage 0)
because `canonicalize()` produced it. The C runtime adds the prefix only inside its conversion to the platform's form
(`runtime/platform.c`), so it never reaches a value, and with `Path` that stays true by construction rather than by a
strip.

**`std/fs` answers `Path` values.** The signatures the package gains:

```trb
public native shared type File with Close, Sink<Bytes, IoError> {
  native static fn open(path: Into<Path>): Result<File, IoError>
  native static fn create(path: Into<Path>): Result<File, IoError>
  native static fn readText(path: Into<Path>): Result<String, IoError>
  native static fn writeText(path: Into<Path>, text: String): Result<Void, IoError>
  native static fn exists(path: Into<Path>): Bool
  native static fn isDirectory(path: Into<Path>): Bool
  native static fn createDirectory(path: Into<Path>): Result<Void, IoError>
  native static fn absolutePath(path: Into<Path>): Result<Path, IoError>
  /** The entries of a directory as paths below it, sorted by name. */
  native static fn list(path: Into<Path>): Result<List<Path>, IoError>
}

/** What went wrong with which path. */
public type IoError with Show, Error {
  path: Path
  message: String
}

/** The path as the operating system of this machine reads it: for a foreign function and for a child process. */
public fn systemPath(path: Path): String
```

`list` answering `List<Path>` instead of `List<String>` is the single biggest reason the bare `"{a}/{b}"`
interpolations exist today — every directory walker in the tree re-joins the names it got by hand. The entries come
back as `path.joined(name)`, sorted by the byte order of their names, which is what both implementations already do.

**A directory entry whose name is not valid UTF-8 is an `IoError`**, and this is a `# Pitfalls` entry on `list`:
a `String` is always valid UTF-8 and a `Path` is made of `String`s, so there is no value for such a name and no
replacement character is invented. Stage 0 silently mangled it (`entry.file_name().to_string_lossy()` in its
`natives.rs`, deleted with stage 0). The C runtime does not check either: the POSIX `torb_platform_list_directory`
in `runtime/platform.c` hands the bytes on through `torb_text_from_cstring` without validating them, so a `String`
that is not UTF-8 can reach a program today, and closing that is part of the same slice. What a program does about
such an entry is its own decision: the error names the directory and the bytes.

`Process` gains the parameter the outline names, which does not exist today:

```trb
fn run(command: String, arguments: List<String>, workingDirectory: Path? = None): Result<ProcessOutput, IoError>
native fn start(command: String, arguments: List<String>, workingDirectory: Path? = None): Result<Child, IoError>
```

## 7. The sandbox

```trb
/** This path resolved below `base`, refusing to leave it. */
fn resolved(inside: Into<Path>): Result<Path, PathError>
```

**"Inside" is defined lexically, in four steps, and nothing else happens.** Let `base` be `inside.normalized()`:

1. `base` must be absolute, or the answer is `Fail PathError.NoBase(base)` — "inside" has nothing to be measured
   against otherwise.
2. If `self` is relative, the candidate is `base.joined().normalized()`. If `self` is absolute, the candidate is
   `self.normalized()`: an absolute path is not joined to anything, it is judged.
3. If the candidate `startsWith(base)`, the answer is `Ok candidate`.
4. Otherwise it is `Fail PathError.Outside(candidate, base)`.

So `Path.from("/etc/passwd").resolved(inside: Path.from("/srv/app"))` fails, `Path.from("../../etc").resolved(inside:
...)` fails, `Path.from("a/../b")` succeeds as `/srv/app/b`, and a candidate on another root fails at step 3 because
the roots differ.

**What is not promised, stated plainly.** `resolved(inside:)` says *the text of this path does not leave `base`*. It
does not say *the file this path opens is under `base`*, and the difference is everything a real attacker uses:

- a **symlink** inside `base` that points outside it. `/srv/app/data → /etc`, and `data/passwd` resolves inside and
  opens outside. `resolved(inside:)` cannot see it, because it reads no disk;
- a **hard link**, a **bind mount** or a **junction** with the same effect, and a `base` that is itself a link;
- a **race**: a component that is a directory when it is checked and a link when it is opened. No answer a pure
  function gives can survive that, because the promise is about a moment.

There is exactly one thing that closes the gap, and it is not in this package: the operating system has to be asked
while it opens. That is `openat` with `O_NOFOLLOW` per component on POSIX and
`CreateFileW` with `FILE_FLAG_OPEN_REPARSE_POINT` on Windows, or a check of what was actually opened afterwards. It
belongs to `std/fs` and to the VM's own natives, because it is a file-system operation and not path arithmetic.
`resolved(inside:)`'s doc comment names it under `# Pitfalls` so that nobody reads a lexical check as a security
boundary on its own.

**How `std/sandbox` uses it.** The one permission over paths today is
`SandboxCapabilities.files(readOnly: String = "", readWrite: String = "")` — two bare strings, one root
each, no containment check, and all six sandbox natives are still `.Planned` for 7.4. It becomes:

```trb
public native type SandboxCapabilities {
  /** File system roots the script may reach. A side that is left out stays closed. */
  native var fn files(readOnly: List<Path> = [], readWrite: List<Path> = [])
}
```

Each granted root is stored as `File.absolutePath(root)?.normalized()`, so the roots are absolute before the script
runs and no working directory can move them afterwards. Every path a script hands to a `std/fs` native is checked with
`resolved(inside: root)` against each granted root of the matching side, and the first `Ok` is the path the native
gets; a script whose every root answers `Fail` gets a `SandboxError` naming the path and the roots. Because the check
is lexical, the sandbox's natives additionally refuse a component that is a link at open time — that is the half
`resolved(inside:)` does not promise, and the sandbox is the one caller that must not accept the gap.

`SandboxError` carries no path today, so a file that could not be read loses which file it was. It gains one with this
change, and that is a one-field addition with no other consequence.

## 8. Migration

Six slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations.

**`std/path` is part of the fixpoint from slice 1.** The compiler is written in TorbScript and compiles itself, so
`std/path` is code that stage 1 type checks, that the C back end emits, and that the resulting binary runs while it
compiles the next one. It therefore has to be supported by *both* back ends before anything depends on it. It is —
probe 0 of section 5 is this package, written out and built, and the two implementations answer the same thirty-four
lines. **The only new back-end requirement in the whole design is the `Into` witness of section 5, and the interim
signature removes even that from the critical path.**

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The package.** `Path`, `Root`, `PathError`, every member, `From<String>`, `Show`, `Equals`, `Hash`, `Compare`. The checker condition of section 5, or the interim signature | `std/path/{project.trb,src/lib.trb,tests/}`, the root `project.trb` is unchanged (`std/*` is a member pattern), `compiler/src/semantics/checker/expression.trb` | **Low.** No native, nothing depends on it yet. The risk is the checker condition, and its blast radius is every `.into()` in the repository, so it needs checker tests with exact messages |
| 2 | **The boundary.** `std/fs` takes `Into<Path>`, `list` answers `List<Path>`, `IoError.path` becomes a `Path`, `systemPath`; the Windows half of `runtime/platform.c` goes wide-character; the conformance programs of section 6 | `std/fs/src/lib.trb`, `std/io`, `runtime/{file.c,platform.c,include/torb.h}`, `compiler/src/backend/c/natives.trb`, `tests/conformance/` | **Highest of the six.** It changes the type of a field that six places construct positionally (`IoError(path, message)`), and it touches the runtime and the manifest of natives. It is also the slice that fixes two live bugs (non-ASCII paths, `to_string_lossy`) |
| 3 | **The sandbox and processes.** `SandboxCapabilities.files` takes `List<Path>`, `SandboxError` gains a path, `Process.run`/`start` gain `workingDirectory` | `std/sandbox/src/lib.trb`, `std/process/src/lib.trb`, `compiler/src/backend/c/natives.trb`, `runtime/platform.c` | **Low.** Every sandbox native is `.Planned` for 7.4, so this slice writes signatures the runtime does not implement yet. `workingDirectory` is new behaviour in `torb_process_run` |
| 4 | **`compiler/src/project/path.trb` is deleted.** `SourceTree`, `Workspace`, `Package`, `Graph`, `Module.path`, `Checked`, the IR's stable path and every CLI subcommand carry a `Path`. `pathDepth` disappears into `parent()`, `isInside` into `startsWith`, `relativePath` into `relativeTo` | `compiler/src/project/*`, `compiler/src/semantics/{graph,check,scope}.trb`, `compiler/src/semantics/checker/{implementation,declaration,receiver,wellknown}.trb`, `compiler/src/ir/{instantiate.trb,lower/lower.trb}`, `compiler/src/cli/*`, `compiler/src/main.trb`, `compiler/tests/project.test.trb` and the four test files that walk up to the repository root | **Highest for the fixpoint.** `stablePathOf` (`ir/lower/lower.trb:197`) and Rust's `stable_path` (`program.rs:341`) have to stay byte-identical, because that text is in every panic site of every compiled program and in every `.expected` file. Run the fixpoint test for this slice |
| 5 | **The second path vocabulary.** `documentation/tree.trb`'s `fileName`, `folderOf`, `lastSlash` and `joinDocumentationPath` go; the bare `"{a}/{b}"` interpolations in `cli/files.trb`, `documentation/{snippets,index,skill,bundle,command}.trb` and `main.trb` become `joined` | `compiler/src/documentation/*`, `compiler/src/cli/files.trb`, `compiler/src/main.trb` | **Medium.** The documentation gates compare generated text, so a changed join changes output. `command.trb:75` builds `"{root}/../std"` by interpolation and never normalizes it — that one changes meaning for the better and needs a look |
| 6 | **The prose.** A new `docs/standard-library/path.md`; `fs.md`, `sandbox.md`, `process.md` and the how-to pages that show a path; `CONCEPT.md`'s decision log gains one entry; `docs/internals/index.md` lists `PATH.md` | `docs/**` | **Low**, and `torb run ../compiler docs check ../docs` plus `docs index --check` is the gate |

Slices 1 and 6's index entry land together with this document, so that `docs check` never sees a design document
nothing links to.

## 9. What this is not

- **Not a file system.** Nothing in `std/path` reads a disk, so it has no `exists`, no `isDirectory`, no
  `absolutePath` and no way to follow a link. Those are `std/fs`, which is where the capability lives and where an
  import says "this file touches files".
- **Not a same-file question.** `==` compares text. Two paths that name one file (a link, a case-insensitive mount, a
  bind mount) are not equal, and two equal paths on two machines are not one file. `sameFile` would be a member of
  `std/fs` and is not part of this design.
- **Not case insensitive, on any platform, ever.** Section 1 says why.
- **Not a glob and not a matcher.** `std/*` in a `project.trb` is a pattern over paths and stays a `String` the
  workspace reader interprets. A pattern language is a package of its own if it is ever wanted.
- **Not a URL, and not a module import path.** A URL is `docs/design/URI.md`'s `Uri`, which is its own type because a path
  is platform-dependent — separators, drive letters, UNC shares — and a URI is not; the two are joined by two
  fallible conversions (`Uri.tryFrom(path)`, `Path.tryFrom(uri)`) and never merged. `use Path from "std/path"` is a
  third thing again: it names a package and a module inside it, it is resolved by the workspace, and it is neither a
  `Path` nor a `Uri`, which is why `Graph.resolveImport` keeps its own `String`.
- **Not a byte path.** There is no `OsString`, there are no non-UTF-8 components, and a directory entry whose name is
  not UTF-8 is an error rather than a value. The cost is that such a file cannot be named; the benefit is that
  `Path`, `String` and every back end need no rule for broken text, which is the same trade `String` already made.
- **Not `Path`/`PathBuf`.** There is one type, because a value that is never aliased needs no borrowed twin.
- **Not a place where a literal adapts yet.** A `String` *literal* adapting to an expected `Path`, the way `1` adapts
  to a `Float`, would remove the last `.into()` from every call site in section 4. It is a candidate for later
  (literal traits), it is mentioned here so nobody designs around its absence, and it is not part of this design.

## 10. Open

Everything technical above is decided. These are taste or direction, and only the owner answers them.

1. **`nameWithoutExtension`, or `stem`?** The rule says full words and no abbreviations, and `stem` is a full English
   word that happens to be jargon (`file_stem` in Rust, `stem` in `pathlib`). `nameWithoutExtension` needs no
   documentation to be understood and reads as the truth next to `name` and `extension`; it is also nineteen
   characters. The document uses `nameWithoutExtension`.
2. **Does the prelude export `Path`?** `std/fs` deliberately is not in the prelude, so that `use File from "std/fs"`
   is the statement "this file touches files". A `Path` is not a capability — it reads nothing — and it appears in
   signatures all over the compiler and in every `project.trb`-shaped configuration. The document assumes
   `use Path from "std/path"`, which is the conservative half.
3. **Is slice 2's wide-character Windows runtime this design's work, or a round of its own?** It fixes two bugs that exist
   today and are not about paths as such (a non-ASCII path fails in a compiled binary, a long path cannot be opened),
   and it is the largest single piece of C in the plan. Doing it here is what makes `Path` honest on Windows; deferring
   it means `std/path` ships with a documented hole on one platform.
4. **`Root.Share` now or later?** A UNC share is one case and about thirty lines of parsing, and nothing in the
   repository has ever needed one. Leaving it out would make `//server/share/x` parse as a `Unix` root with `server`
   as its first component, which is wrong rather than unsupported — so the document includes it.
