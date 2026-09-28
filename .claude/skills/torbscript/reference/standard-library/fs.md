---
title: std/fs
summary: File and IoError - whole files as text or bytes, a File as both ends of a byte stream, and the tree around them - remove, rename, move, copy, metadata, links, temporary files and atomic replacement.
kind: package
status: stable
order: 130
keywords:
  - std/fs
  - File
  - IoError
  - Metadata
  - FileKind
  - Permissions
  - file system
  - readBytes
  - writeBytesAtomically
  - symbolic link
  - temporary file
source:
  - std/fs/src/lib.trb
  - std/fs/src/metadata.trb
---

`std/fs` is files: whole-file helpers for what fits in memory, streams for everything else, and the operations on the
tree around them. It is deliberately not in the prelude - `use File from "std/fs"` at the top of a file is the
statement "this file touches files".

Every function takes a path as the same text on every system - `/` between the names, a drive letter on Windows - and
the runtime converts it where it calls the system (PATH.md section 6). Nothing in the package
branches on the operating system, so a program that uses it is the same code for every target.

## Import

```trb fragment
use File, IoError from "std/fs"
use Metadata, FileKind, Permissions from "std/fs"
```

```trb check
use File, IoError from "std/fs"

fn wordCount(path: String): Result<Int, IoError> {
  const text = File.readText(path)?
  text.split(" ").length()
}
```

## Declarations

### IoError

```trb fragment
public type IoError with Show, Error {
  path: String
  message: String

  fn isCrossDevice(): Bool
}
```

What went wrong, with which path, in the operating system's own words. Bytes off a disk are not always UTF-8, and a
`String` always is, so decoding is one of the ways reading fails: `extend IoError with From<Utf8Error>` (see
[std/stream](stream.md)) is what lets `?` convert a decoding failure into an `IoError` automatically.

`isCrossDevice` says whether the operation failed because its two paths are on different volumes - what `rename`
answers across file systems (`EXDEV`, `ERROR_NOT_SAME_DEVICE` on Windows), which the runtime words the same on every
system.

### File: whole files

```trb fragment
public native shared type File with Close, Sink<Bytes, IoError> {
  static fn readText(path: String): Result<String, IoError>
  static fn writeText(path: String, text: String): Result<Void, IoError>
  static fn readBytes(path: String): Result<Bytes, IoError>
  static fn writeBytes(path: String, bytes: Bytes): Result<Void, IoError>
  static fn read(path: String): Task<Result<Bytes, IoError>>
  static fn write(path: String, var source: Source<Bytes, IoError>): Task<Result<Void, IoError>>
  static fn writeBytesAtomically(path: String, bytes: Bytes): Result<Void, IoError>
  static fn writeTextAtomically(path: String, text: String): Result<Void, IoError>
}
```

`readText` and `writeText` are the text pair, and `readBytes` and `writeBytes` the same for bytes; all four are
synchronous and need no task, which is what a tool, a test and the top level of a program want - the package manager
reads and writes its archives with them. `readText` fails where the bytes are not UTF-8 rather than inventing a
replacement character. `read` is `readBytes` for a task: it reads chunk by chunk on a thread of the blocking pool, so
the worker goes on meanwhile. `write` creates a file and fills it from a source of any size without holding it in
memory; `Source.from([bytes])` is a source of one chunk.

`writeBytesAtomically` and `writeTextAtomically` replace a file whole or not at all: the content goes into a new file
beside it, is flushed to the disk, and is renamed over the target in one step. A reader - or the program after a crash
- finds the old content or the new one and never a part of either, a file that was there keeps its permissions, and
where the call fails nothing is left beside the target. On Windows a file another program holds open without allowing
its deletion cannot be replaced, and the call fails with the old content in place.

```trb check
use File, IoError from "std/fs"

fn saveIndex(path: String, entries: List<String>): Result<Void, IoError> {
  File.writeTextAtomically path, entries.joined(separator: "\n")
}
```

### File: the tree

```trb fragment
public native shared type File with Close, Sink<Bytes, IoError> {
  static fn exists(path: String): Bool
  static fn isDirectory(path: String): Bool
  static fn absolutePath(path: String): Result<String, IoError>
  static fn createDirectory(path: String): Result<Void, IoError>
  static fn removeDirectory(path: String): Result<Void, IoError>
  static fn list(path: String): Result<List<String>, IoError>
  static fn walk(path: String): Result<List<String>, IoError>
  static fn remove(path: String): Result<Void, IoError>
  static fn rename(path: String, to: String): Result<Void, IoError>
  static fn move(path: String, to: String): Result<Void, IoError>
  static fn copy(path: String, to: String): Result<Void, IoError>
}
```

`createDirectory` makes the directory and every one above it, and does nothing where it is already there;
`removeDirectory` is its counterpart - the directory and everything in it, and nothing where nothing is there. A
symbolic link inside is removed and never followed, so nothing outside the directory is touched. `remove` takes one
file, one symbolic link (the link, never what it points at) or one empty directory, and fails where nothing is there; a
read-only file is removed as well, on Windows too.

`list` answers the names of a directory's entries, sorted by bytes. `walk` answers every entry below a directory as a
path relative to it, with `/` between the names: a directory before what is in it, the entries of one directory in the
order `list` answers them, and a symbolic link as an entry that is not followed. An entry whose name is not UTF-8 is an
`IoError` whose message shows its bytes.

`rename` moves a file or a directory in one step and replaces a file that is at the target, which is what makes it the
last step of every replacement that must not be seen half done; both paths have to be on one volume, and across two it
fails with an `IoError` whose `isCrossDevice()` is true. `move` is the move that also crosses volumes: on one volume it
is `rename`, across two it copies a file, a symbolic link or a directory with everything in it beside the target under
a hidden name, renames the copy onto the target in one step once it is whole, and removes the original - so the target
is never seen half written, and a copy that fails leaves both as they were. It is not one step, though: during the copy
the original is still there, and where removing it fails it is in both places. Where that matters, stage on the
target's volume and `rename`. `copy` takes the content and the permissions of a file to a target it creates or
replaces; a symbolic link is followed, a directory is refused. `absolutePath` is text arithmetic against the working directory (`.` and `..` resolved), does not require the
path to exist and does not follow links.

```trb check
use File, IoError from "std/fs"

fn sources(root: String): Result<List<String>, IoError> {
  const entries = File.walk(root)?
  Ok entries.filter({ _.endsWith ".trb" }).toList()
}
```

### File: metadata, links and temporary files

```trb fragment
public native shared type File with Close, Sink<Bytes, IoError> {
  static fn metadata(path: String): Result<Metadata, IoError>
  static fn linkMetadata(path: String): Result<Metadata, IoError>
  static fn setPermissions(path: String, permissions: Permissions): Result<Void, IoError>
  static fn createSymbolicLink(path: String, target: String): Result<Void, IoError>
  static fn symbolicLinkTarget(path: String): Result<String, IoError>
  static fn createTemporaryFile(prefix: String = "", inside: String? = None): Result<String, IoError>
  static fn createTemporaryDirectory(prefix: String = "", inside: String? = None): Result<String, IoError>
}
```

`metadata` answers what a path is, how large, when it was last written and its permissions - of what a symbolic link
points at, the way every other function here follows one. `linkMetadata` describes a link itself and is the one way to
see that a path is a link. `setPermissions` sets the permission bits; on Windows only the owner's write bit counts, and
without it the file is read-only.

`createSymbolicLink` stores its target exactly as written, so a relative target is read from the link's directory.
Windows lets a program create one only in developer mode or with the privilege to, and without either the call fails
with `Operation not permitted`; `symbolicLinkTarget` answers the target as it was stored, with `/` between its names.

`createTemporaryFile` and `createTemporaryDirectory` create a new, empty entry with a name nobody else has - the prefix
and twelve letters and digits - where nothing was, so no other program can have it too, and answer its path. It is in
`inside`, or in the system's directory for temporary files (`GetTempPath2W` on Windows, `$TMPDIR` or `/tmp` elsewhere);
on POSIX only its owner may read it, and removing it is the caller's.

```trb check
use File, IoError from "std/fs"

fn isNewer(first: String, second: String): Result<Bool, IoError> {
  const one = File.metadata(first)?
  const other = File.metadata(second)?
  Ok(one.modified > other.modified)
}
```

### Metadata, FileKind and Permissions

```trb fragment
public type Metadata with Show, Equals {
  kind: FileKind
  size: Int
  modified: Timestamp
  permissions: Permissions
}

public type FileKind with Show, Equals, Hash {
  case File
  case Directory
  case SymbolicLink
  case Other
}

public type Permissions with Show, Equals, Hash {
  mode: Int

  fn readable(): Bool
  fn writable(): Bool
  fn executable(): Bool
  fn withWritable(writable: Bool): Permissions
}
```

`modified` is a `Timestamp` of [std/time](time.md), a point on the wall clock. `size` is in bytes and means nothing a
program can rely on for a directory. `Permissions` are the bits of POSIX - read, write and execute for the owner, the
group and everybody else, `0b111_101_101` for `rwxr-xr-x` - and show as the nine letters `ls -l` prints; the three
questions are about the owner. Windows has one bit of its own, read-only, and the bits are made up from it the way its C
library does: everybody may read, everybody may write unless the file is read-only, and a directory or a program
(`.exe`, `.com`, `.bat`, `.cmd`) may be executed.

### File: an open file

```trb fragment
public native shared type File with Close, Sink<Bytes, IoError> {
  static fn open(path: String): Result<File, IoError>
  static fn create(path: String): Result<File, IoError>
  var fn readAll(): Result<String, IoError>
  var fn close()
  var fn chunks(size: Int = 65536): Source<Bytes, IoError>
  var fn lines(): Source<String, IoError>
  var fn add(item: Bytes): Task<Result<Void, IoError>>
  var fn end(): Task<Result<Void, IoError>>
}
```

A file that is open has an identity - the handle of the operating system - so it is a `shared type` with `Close`:
`using file = File.open(path)?` binds it for the rest of a block, and nothing calls `close()` by hand. An open file is
both ends of a stream: `chunks()` and `lines()` read it, and a `File` *is* a `Sink<Bytes, IoError>` for writing, so
everything that writes into a sink writes into a file (see [std/stream](stream.md)) - a `var` file handed to a `var`
parameter of type `Sink<Bytes, IoError>` is the file itself. `chunks`, `lines`, `add` and `end` answer a `Task` and
need `.await()` (see [std/task](task.md)): every read and every write runs on a thread of the blocking pool. `add`
writes through the file's buffer and `end` flushes it; `close` flushes what `end` did not.

```trb check
use File, IoError from "std/fs"

fn copyLines(from: String, into: String): Task<Result<Int, IoError>> {
  using source = File.open(from)?
  using target = File.create(into)?
  var count = 0
  var lines = source.lines()
  while const Some(line) = lines.next().await()? {
    target.add("{line}\n".bytes()).await()?
    count = count + 1
  }
  target.end().await()?
  count
}
```

## Related

- [std/stream](stream.md) - `Source` and `Sink`, which `File.chunks` and the writing side of `File` answer.
- [std/storage](storage.md) - the same operations over a `Uri`, with a driver chosen by its scheme; `file:` is this
  package.
- [std/path](path.md) - `Path`, the value a path is before it reaches a function of this package.
- [Read a file](../how-to/read-a-file.md) - the task recipe this package is for.
- [The standard library](index.md) - the other packages.

