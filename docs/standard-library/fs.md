---
title: std/fs
summary: File and IoError - whole-file helpers for what fits in memory, and a File as both ends of a byte stream.
kind: package
status: stable
order: 130
keywords:
  - std/fs
  - File
  - IoError
  - file system
source:
  - std/fs/src/lib.trb
---

`std/fs` is files: whole-file helpers for what fits in memory, and streams for everything else. It is deliberately not
in the prelude - `use File from "std/fs"` at the top of a file is the statement "this file touches files".

## Import

```trb fragment
use File, IoError from "std/fs"
```

```trb check
use File, IoError from "std/fs"

fn wordCount(path: String): Result<Int, IoError> {
  const text = File.readText(path)?
  Ok text.split(" ").length()
}
```

## Declarations

<!-- torb:declarations:begin -->

### IoError

```trb fragment
public type IoError with Show, Error {
  path: String
  message: String
}
```

What went wrong, with which path. Bytes off a disk are not always UTF-8, and a `String` always is, so decoding is one
of the ways reading fails: `extend IoError with From<Utf8Error>` (see [std/stream](stream.md)) is what lets `?` convert
a decoding failure into an `IoError` automatically.

### File

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

  static fn readText(path: String): Result<String, IoError>
  static fn writeText(path: String, text: String): Result<Void, IoError>
  static fn write(path: String, var source: Source<Bytes, IoError>): Task<Result<Void, IoError>>
  static fn absolutePath(path: String): Result<String, IoError>
  static fn exists(path: String): Bool
  static fn isDirectory(path: String): Bool
  static fn createDirectory(path: String): Result<Void, IoError>
  static fn list(path: String): Result<List<String>, IoError>
}
```

A file that is open has an identity - the handle of the operating system - so it is a `shared type`:
`const text = using File.open(path)? { file => file.readAll() }`. An open file is both ends of a stream: `chunks()` and
`lines()` read it, and a `File` *is* a `Sink<Bytes, IoError>` for writing, so everything that writes into a sink writes
into a file (see [std/stream](stream.md)). Most code needs neither: `readText`, `writeText`, `exists`, `isDirectory`,
`createDirectory` and `list` work on a path directly, without opening a handle. `absolutePath` is text arithmetic
against the working directory (`.` and `..` resolved) and does not require the path to exist, so it does not follow
links either. `chunks`, `lines`, `add`, `end` and `write` answer a `Task` and need `.await()`; see
[std/task](task.md), which is `status: planned` because no back end gives a `Task` a value yet. `open`, `readAll`,
`close`, `readText`, `writeText`, `exists`, `isDirectory`, `createDirectory` and `list` do not touch `Task` at all.

<!-- torb:declarations:end -->

## Related

- [std/stream](stream.md) - `Source` and `Sink`, which `File.chunks` and the writing side of `File` answer.
- [Read a file](../how-to/read-a-file.md) - the task recipe this package is for.
- [The standard library](index.md) - the other packages.
