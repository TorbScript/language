---
title: std/storage
summary: Storage, a capability over a Uri whose scheme chooses the driver - FileStorage for files, MemoryStorage for tests - and Storage.registry, one storage over the drivers a program names.
kind: package
status: stable
order: 131
keywords:
  - std/storage
  - Storage
  - StorageFailure
  - FileStorage
  - MemoryStorage
  - Storage.registry
  - scheme
  - driver
source:
  - std/storage/src/lib.trb
  - std/storage/src/storage.trb
  - std/storage/src/file.trb
  - std/storage/src/memory.trb
---

`std/storage` is bytes at a reference, wherever the scheme of the reference reaches. `Storage` is the capability -
read, exists, list, write, delete - and a driver is a `shared type` that has it for the schemes it names: `FileStorage`
for `file:` over [std/fs](fs.md), and `MemoryStorage` for `memory:`, the double every test of a program that stores
things needs. S3, WebDAV and the other stores are packages of their own, whose drivers have the same trait
(URI.md section 11).

**The program names its drivers, and nothing registers itself.** `Storage.registry` is one storage over several
drivers, chosen by the scheme of each reference, and it is itself a `Storage`. A scheme it was not given is a failure
that lists the ones it was.

## Import

```trb fragment
use Storage, StorageFailure, FileStorage, MemoryStorage from "std/storage"
```

```trb check
use Storage, StorageFailure, FileStorage, MemoryStorage from "std/storage"

fn stored(): Task<Result<String, StorageFailure>> {
  var storage = Storage.registry([FileStorage(), MemoryStorage()])
  storage.writeText("memory:/notes/first", "hello").await()?
  storage.readText("memory:/notes/first").await()
}
```

## Declarations

### Storage

```trb fragment
public shared trait Storage with Schemes {
  fn read(uri: Uri): Task<Result<Bytes, StorageFailure>>
  fn exists(uri: Uri): Task<Result<Bool, StorageFailure>>
  fn list(uri: Uri): Task<Result<List<Uri>, StorageFailure>>
  var fn write(uri: Uri, content: Bytes): Task<Result<Void, StorageFailure>>
  var fn delete(uri: Uri): Task<Result<Void, StorageFailure>>

  fn readText(uri: Uri): Task<Result<String, StorageFailure>>
  var fn writeText(uri: Uri, text: String): Task<Result<Void, StorageFailure>>

  static fn registry(drivers: List<Storage>): Storage
}
```

Five members every driver writes, and `readText` and `writeText` over `read` and `write`. A driver that cannot do one
of them - a store that cannot list - says so in the `Result` of that member rather than by missing it. Every member
reaches the outside world and answers a `Task`, and a writing member changes the store while a task waits, so the trait
is a `shared trait` and every driver a `shared type`. `write` replaces whatever was stored, whole or not at all where
the store can promise it; `delete` removes what is stored at the reference and everything below it; `list` answers the
references directly below one, sorted. `Schemes` comes from std/uri (skill `torbscript-networking`: `references/standard-library/uri.md`): the schemes a driver answers to, in
lower case.

A reference is a `Uri`, so a relative one has no scheme to choose a driver by: resolve it against a base first
(`reference.resolved(against: base)`).

### StorageFailure

```trb fragment
public type StorageFailure with Show, Equals, Error {
  case UnknownScheme(uri: String, known: List<String>)
  case NotAddressable(uri: String, reason: String)
  case NotFound(uri: String)
  case NotText(uri: String, reason: String)
  case Refused(uri: String, reason: String)
}
```

Every case carries the reference as `Uri.show()` wrote it, so a password in the user information of a reference is
`***` in every failure that reaches a log or a user. `UnknownScheme` is the registry's own; `NotAddressable` is a
reference a driver cannot reach (another scheme, a `file:` URI with a query); `Refused` is the store's own words.

### FileStorage

```trb fragment
public shared type FileStorage with Storage
```

`file:` over [std/fs](fs.md): a reference is a `file:` URI of RFC 8089 (`file:///srv/data/index`,
`file:///C:/data/index`), read as a `Path`. A write creates the directories above the file and replaces it with
`File.writeBytesAtomically`, so a reader never finds half of what was written; a read runs on the blocking pool
(`File.read`); a directory is listed with the references of its entries, and deleting one deletes everything in it
without following a symbolic link.

### MemoryStorage

```trb fragment
public shared type MemoryStorage with Storage
```

`memory:`: bytes kept in a table for as long as the driver lives. A reference is its whole text, and a directory is only
a prefix of other references - `memory:/notes` exists and lists `memory:/notes/first` because something is stored below
it, and deleting it deletes everything below it.

```trb check
use MemoryStorage, StorageFailure from "std/storage"

fn remembered(): Task<Result<List<Uri>, StorageFailure>> {
  var storage = MemoryStorage()
  storage.writeText("memory:/notes/first", "one").await()?
  storage.writeText("memory:/notes/second", "two").await()?
  storage.list("memory:/notes").await()
}
```

## What is not here

- **No driver for a network store.** S3, WebDAV, Google Cloud Storage and the rest are packages of their own with a
  `type S3Storage with Storage`; `std` carries the trait because it ships two drivers for it, one of which reaches the
  outside world, which is URI.md section 11's rule for a capability trait.
- **No `Connection` and no `Cache`.** A database or a cache is opened once from a URI and then queried by key, which is
  another shape; both traits wait for a second driver each (URI.md section 16, question 8).
- **No discovery.** Nothing adds itself to a registry: the program names every driver it can reach, in one expression.

## Related

- [std/fs](fs.md) - the files `FileStorage` reaches.
- std/uri (skill `torbscript-networking`: `references/standard-library/uri.md`) - `Uri`, the reference, and `Schemes`, what a driver answers to.
- URI.md section 11 - the design, and why a registry is a value.
- [The standard library](index.md) - the other packages.

