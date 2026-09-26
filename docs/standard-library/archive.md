---
title: std/archive
summary: POSIX ustar archives - tarred writes entries as bytes that depend on nothing but the entries, untarred reads the regular files back and refuses links.
kind: package
status: stable
order: 187
keywords:
  - std/archive
  - tar
  - ustar
  - TarEntry
source:
  - std/archive/src/lib.trb
---

`std/archive` reads and writes `tar` archives in the POSIX ustar format. A package of the registry is a `tar` inside
gzip ([std/compression](compression.md)), and the archive has to be the same bytes on every machine that builds it -
so everything about the machine is left out: every mode `0644`, every time 0, no owner and no group, the entries sorted
by path.

## Import

```trb fragment
use TarEntry, ArchiveError, tarred, untarred from "std/archive"
```

```trb check
use TarEntry, tarred, untarred from "std/archive"

const entries = [TarEntry("src/lib.trb", "public fn one(): Int \{\n  1\n\}\n".bytes().toList())]
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
}
```

One file of an archive: its path, with `/` between the directories, and its bytes.

### `tarred`

```trb fragment
public fn tarred(entries: List<TarEntry>): Result<Bytes, ArchiveError>
```

The entries as a ustar archive, sorted by path, closed by the two blocks of zeros. A path longer than 100 bytes is
split into ustar's prefix and name at a `/`; a path of more than 255 bytes, an empty one and two entries of one path
are an `ArchiveError`.

### `untarred`

```trb fragment
public fn untarred(bytes: Bytes): Result<List<TarEntry>, ArchiveError>
```

The regular files of an archive, in the order it holds them. A directory entry is skipped. **A link, a device and an
extended header are refused**: a package has no use for one, and a link is how an archive writes outside of the
directory it is unpacked into. A header whose checksum does not match and an archive that ends inside an entry are
refused too.

### `ArchiveError`

```trb fragment
public type ArchiveError with Show, Error {
  reason: String
  offset: Int
}
```

What is wrong, and the byte of the archive where it was found.

## Pitfalls

**A path is kept as the archive says it.** `untarred` does not decide where a file may go: an entry named
`../../etc/passwd` or `/tmp/x` comes back with exactly that path. Whoever writes the files out checks every path first,
as the package manager does (no `..`, nothing absolute, no `\` and no `:`).

## Related

- [std/compression](compression.md) - the gzip around the archive.
- [std/digest](digest.md) - SHA-256, which the tree hash of a package is made of.
- [The standard library](index.md) - the other packages.
