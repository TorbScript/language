---
title: std/archive
summary: tar archives - untarred reads what GNU tar, bsdtar and pax write, with modes and links; tarred writes ustar and pax; extract of std/archive/extract unpacks safely.
kind: package
status: stable
order: 187
keywords:
  - std/archive
  - tar
  - ustar
  - pax
  - GNU tar
  - TarEntry
  - TarKind
  - extract
  - path traversal
source:
  - std/archive/src/lib.trb
  - std/archive/src/extract.trb
---

`std/archive` reads and writes `tar` archives. `untarred` reads what the `tar` programs in use write - POSIX ustar,
pax (POSIX.1-2001) and GNU tar's own format - and `tarred` writes ustar, with a pax header where a path does not fit.
A package of the registry is a `tar` inside gzip ([std/compression](compression.md)), and so is a release of the
toolchain, which `torb upgrade` unpacks with this package.

`tarred` puts nothing of the machine into an archive: no owner and no group, and every mode and time the entry's own -
`0644` and 0 unless an entry says otherwise - so the same entries are the same bytes on every machine.

Reading and writing are pure. Writing entries out below a directory is the module `std/archive/extract`, which
refuses every entry that would land outside of it; it is a module of its own because it reaches the file system, and
a package of the registry that imports it has the capability `files`.

## Import

```trb fragment
use TarEntry, TarKind, ArchiveError, tarred, untarred from "std/archive"
use extract, ExtractError from "std/archive/extract"
```

```trb check
use TarEntry, TarKind, tarred, untarred from "std/archive"

const entries = [
  TarEntry.directory("tool"),
  TarEntry("tool/run", "#!/bin/sh\n".bytes(), TarKind.File, 0b111_101_101),
  TarEntry.symbolicLink("tool/latest", "run"),
]
match tarred(entries) {
  Ok(archive) => print untarred(archive).map({ found => found.map({ _.path }).toList() })
  Fail(problem) => print problem
}
```

## Declarations

### `TarEntry`

```trb fragment
public type TarEntry {
  path: String
  bytes: Bytes
  kind: TarKind = TarKind.File
  mode: Int = 0b110_100_100
  modified: Int = 0

  static fn directory(path: String, mode: Int = 0b111_101_101): Self
  static fn symbolicLink(path: String, target: String): Self
}
```

One entry of an archive: its path, with `/` between the directories and none at the end, its bytes, what it is, its
permission bits the way POSIX writes them (`0b111_101_101` is `0755`, a program; the set-id and sticky bits above
them), and the time it was last written in seconds since 1970. `TarEntry(path, bytes)` is a file of `0644` and time 0.
A directory and a link have no bytes.

### `TarKind`

```trb fragment
public type TarKind with Show, Equals, Hash {
  case File
  case Directory
  case SymbolicLink(target: String)
  case HardLink(target: String)
}
```

What an entry is. The target of a symbolic link is read from the directory of the link; the target of a hard link is
another path of the same archive.

### `tarred`

```trb fragment
public fn tarred(entries: List<TarEntry>): Result<Bytes, ArchiveError>
```

The entries as an archive, sorted by path - the hard links after the rest, so that what each names is unpacked before
it - and closed by two blocks of zeros. A path that fits ustar's fields (100 bytes, or a prefix of 155 and a name of
100 split at a `/`) is written in them, so such an archive is plain ustar. A longer path, a link target of more than
100 bytes, a size of 8 GiB or more and a time before 1970 or after 2242 go into a pax header (`x`) in front of the
entry instead - pax rather than GNU's long names, because pax is POSIX, every `tar` of the last twenty years reads it
and it has no limit of length. An empty path, two entries of one path and a directory or a link with bytes are an
`ArchiveError`.

### `untarred`

```trb fragment
public fn untarred(bytes: Bytes): Result<List<TarEntry>, ArchiveError>
```

The entries of an archive, in the order it holds them: files, directories, symbolic links and hard links, each with its
mode and time. What the formats put around an entry is read into it and is no entry of its own: GNU's long names (`L`
for a path, `K` for a link target), ustar's prefix, and pax headers - `x` for the next entry, `g` for every one after
it, with `path`, `linkpath`, `size` and `mtime` read and every other key ignored. GNU's base-256 numbers, the old
format's directory (a file whose name ends in `/`) and a checksum of signed bytes are read too. A device, a pipe, a
sparse file and a type the reader does not know are refused, and so are a header whose checksum does not match, a pax
record that breaks its format and an archive that ends inside an entry.

### `ArchiveError`

```trb fragment
public type ArchiveError with Show, Error {
  reason: String
  offset: Int
}
```

What is wrong, and the byte of the archive where it was found.

### `extract`

```trb fragment
public fn extract(entries: List<TarEntry>, into: String, stripComponents: Int = 0): Result<Void, ExtractError>
```

Of `std/archive/extract`. Writes the entries out below the directory `into`, creating it: every directory, every file
with its bytes and its mode, every hard link as a copy of the file it names, and the symbolic links last.
`stripComponents` drops that many leading directories from every path, as `tar --strip-components` does; an entry left
with no path is skipped, and a later entry of a path replaces an earlier one.

**It is safe by default.** Every entry is checked before the first byte is written, and one that fails stops the
whole extraction:

- a path that is absolute, names a drive (any `:`), or holds `..` - with `\` counted as a separator, as Windows reads it;
- a path that leads through a symbolic link, of the archive or already below `into`;
- a symbolic link whose target is absolute, climbs higher than the link's own directory, or climbs with a `..` after a
  name - that `..` follows whatever the name is, and the name may be another link (`a -> ..` beside `b -> a/d/a/..`),
  under another spelling too on a system that ignores case, so only a target that climbs first and then descends is
  sure to stay inside;
- a hard link that does not name a file of the archive written before it.

A symbolic link already where an entry goes is replaced, not followed. The mode loses the set-id and sticky bits and the
write bits of group and others, as the usual umask `022` does, and a directory gets its mode last, so a read-only one
is filled first. On Windows the mode sets only the read-only attribute, where the owner's write bit is clear.

### `ExtractError`

```trb fragment
public type ExtractError with Show, Error {
  case Refused(path: String, reason: String)
  case Failed(problem: IoError)
}
```

An entry `extract` refused, with its path in the archive and why, or a failure of the file system, with what it wrote
before it left in place.

## Pitfalls

**`untarred` keeps a path as the archive says it.** An entry named `../../etc/passwd` or `/tmp/x` comes back with
exactly that path, and a link with exactly its target. Only `extract` decides where a file may go; whoever writes
entries out another way checks every path first, as the package manager does.

**Windows allows a symbolic link only in developer mode or with the privilege to.** Without either, `extract` of an
archive that has one fails with `Operation not permitted` after everything else is written.

**The whole archive is in memory.** `untarred` reads bytes and answers a list; an archive of a few hundred megabytes
needs that much memory twice.

## Related

- [std/compression](compression.md) - the gzip around the archive.
- [std/fs](fs.md) - what `extract` writes with, and `Permissions`, the mode as a type.
- [std/digest](digest.md) - SHA-256, which the tree hash of a package is made of.
- [The standard library](index.md) - the other packages.
