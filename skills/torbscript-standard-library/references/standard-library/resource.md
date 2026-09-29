---
title: std/resource
summary: Resource, EmbeddedBytes and EmbeddedText - a file of the package named by a string literal, resolved by the compiler where it is written; reading the bytes comes with the next slices.
kind: package
status: stable
order: 130
keywords:
  - std/resource
  - Resource
  - EmbeddedBytes
  - EmbeddedText
  - asset
  - embed
  - stable name
source:
  - std/resource/src/lib.trb
  - std/resource/src/resource.trb
  - docs/design/RESOURCES.md
---

`std/resource` names the files a program needs. A string literal where a `Resource`, an `EmbeddedBytes` or an
`EmbeddedText` is expected is resolved by the compiler against the file that writes it, and a file that is not there
is an error at that line - the literal rule (skill `torbscript-language`: `references/language/values-and-types/checked-literals.md`). The value holds the
file's stable name: the owner and name of the package, then the path inside it, always with `/`. The design is
docs/design/RESOURCES.md; this is its first slice, and reading the bytes - embedding them in
the binary and shipping them beside it - is the next two.

## Import

```trb fragment
use Resource, EmbeddedBytes, EmbeddedText from "std/resource"
```

## Declarations

```trb fragment
public type Resource with Show, Equals, Hash {
  fn name(): String
}
public type EmbeddedBytes with Show, Equals, Hash {
  fn name(): String
}
public type EmbeddedText with Show, Equals, Hash {
  fn name(): String
}
```

- **A resource literal is relative to the file that writes it**, the way a `use` is: a library's literal names the
  library's file, whoever calls it.
- **It stays inside its package.** `"../../secrets/key.pem"` is an error that names the package: a file outside it is
  not published with it and cannot be shipped.
- **It is compared byte for byte, on every platform.** `"./Hero.png"` next to `hero.png` is an error on Windows and
  macOS too, where opening the file would have worked, and the message names the file that is there.
- **A `String` value is never a resource**, and cannot be interpolated into one: a path chosen while the program runs
  is a `Path`, read through `std/fs`.
- **Each is a capsule over its stable name**, so a literal is the only way to make one.

## Related

- [std/path](path.md) - `Path`, for the files a program is pointed at while it runs.
- [std/fs](fs.md) - reading a `Path`.
- [The standard library](index.md) - the other packages.

