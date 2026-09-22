---
title: std/path
summary: Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
kind: package
status: stable
order: 128
keywords:
  - std/path
  - Path
  - Root
  - PathError
  - resolved
  - normalized
source:
  - std/path/src/lib.trb
  - docs/PATH.md
---

`std/path` is `Path`: a root and the components between its separators, and never a string. `joined` cannot be given
a root, `parent` cannot fall off the top, and `..` is only ever resolved on purpose, by `normalized()` or
`resolved(inside:)`. `show()` is the one text form, always with `/` regardless of platform. The package is pure value
arithmetic over text - it reads no disk and touches no file system, so whether a `Path` names a real file is a
question for `std/fs`.

## Import

```trb fragment
use Path, Root, PathError from "std/path"
```

```trb check
use Path from "std/path"

const module = Path.from "std/path/src/lib.trb"
print "{module.name()} {module.parent()}"
print module.joined(Path.from("deeper"))
```

## Declarations

### Path

```trb fragment
public type Path with Show, Equals, Hash, Compare {
  private storedRoot: Root?
  private storedComponents: List<String>

  fn root(): Root?
  fn components(): List<String>
}
```

A file path: where it starts, and the components between the separators. `Path.from(text)` builds one and never
fails - there is no text a file system is guaranteed to reject - and `show()` reads it back with `/` on every
platform. `Path` is `Equals`, `Hash` and `Compare`, and all three are lexical and case sensitive everywhere: two paths
that differ only in case are two different values, because whether they name the same file is a question for a file
system and not for this type.

`Path` is a [capsule](../language/types/data-or-capsule.md): both fields are `private` and neither has a default, so
`Path.from` is the one way to a value and `root()` and `components()` are the two reads. Its conversion pair is
`String` - `Path` has `From<String>` and `String` has `From<Path>`, which answers the same text as `show()` - so
`Encode` and `Decode` are derived through that pair and a path in a document is its text.

Every member that is defined for every path answers a value: `isAbsolute`, `isRelative`, `joined`, `startsWith`,
`normalized`, `show`, `compare`. `name`, `nameWithoutExtension`, `extension`, `parent`, `withName`, `withExtension`
and `relativeTo` answer an `Option`, because a root and the empty path have no name and no parent, and two paths on
different roots have nothing in common. `resolved(inside:)` is the one member that can be refused, and answers a
`Result<Path, PathError>`.

`joined` drops the root of its argument, so `base.joined(other)` always keeps `base`'s root and starts with `base`'s
components, for every `other` - `base.joined(Path.from("/etc/passwd"))` is `base/etc/passwd` and not `/etc/passwd`.
That does not cover `..`: `base.joined(Path.from("../../etc"))` is text that escapes once it is normalized, which is
what `resolved(inside:)` guards against instead.

### Root

```trb fragment
public type Root with Show, Equals, Hash {
  case Unix
  case Drive(letter: Char)
  case Share(server: String, share: String)
}
```

Where a path starts. A path without one is relative. `Unix` is `/` - the root of the file system, and on Windows the
root of the current drive. `Drive` is one drive with an absolute path on it, its letter always upper case. `Share` is
a UNC share, `//server/share`. Windows' drive-relative form (`C:foo`, "wherever the process last was on drive C") is
per-process state and not a value, so `Path.from` reads it as absolute on that drive instead of holding a fourth case
for it.

### PathError

```trb fragment
public type PathError with Show, Error {
  case Outside(path: Path, base: Path)
  case NoBase(base: Path)
}
```

What a path operation refuses. `resolved(inside:)` is the only member of `Path` that answers this: `NoBase` where the
base it was given has no root, so "inside" has nothing to be measured against, and `Outside` where the path, joined
under the base if it is relative or judged on its own if it is absolute, does not start with the base once both are
normalized.

## Pitfalls

- **A POSIX file name that literally contains a backslash cannot be named as a `Path` at all.** Both `/` and `\`
  separate on every platform, so that the same program behaves the same in every back end and on every platform, and
  `Path.from` is the one way in. Reach such a file through `std/fs` with its text.
- **Comparison is lexical and case sensitive, on every platform, always.** `Path.from("A") == Path.from("a")` is
  `false` everywhere, including on a case-insensitive mount - a case-insensitive `==` would make a `Map<Path, _>`
  answer differently on two platforms, and it would still be wrong, because case folding is a property of the mount
  and not of the operating system.
- **`resolved(inside:)` is lexical: it reads no disk and follows no link.** It says the *text* of a path does not
  leave a base, not that the file it opens is under that base. A symlink inside the base that points outside it
  defeats it; closing that gap needs the operating system asked while it opens, which belongs to `std/fs`.

## Related

- [std/fs](fs.md) - where a path meets the operating system, and where the capability to read or write one lives.
- [The standard library](index.md) - the other packages.
