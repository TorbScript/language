---
title: Foreign functions
summary: foreign declares functions of a C library with the ABI as the contract, available to any package unlike native, but nothing links or calls one yet and its Pointer and CString types are not declared in std/ either.
kind: reference
status: planned
order: 30
keywords:
  - foreign
  - native
  - C ABI
  - Pointer
  - CString
source:
  - CONCEPT.md#foreign-functions-draft
---

> **Planned.** This feature is designed but not implemented. Nothing on this page runs today.

`native` and `foreign` both declare a function without a TorbScript body, but for opposite reasons: a `native`
function is implemented by the compiler and its runtime, a `foreign` function is implemented by a C library.

## Example

```trb check
foreign "sqlite3" {
  fn sqlite3_open(path: Int64, database: Int64): Int32
  fn sqlite3_close(database: Int64): Int32
}
```

A real binding would spell `path` as `CString` and `database` as `Pointer<Pointer<Void>>`; neither type is declared
in `std/` yet, which is one reason nothing here can be called.

## Syntax

```text
foreign "<library>" {
  fn <name>(<parameters>): <Type>
}

foreign {
  library "<name>", windows: "<file>.dll", linux: "lib<name>.so.<version>", macos: "<name>.dylib"
}
```

## Rules

1. **A `foreign` block declares functions and nothing else, and none of them has a body.** The library is the
   implementation, the same way a required trait member without a body is a requirement on the runtime for `native`.

2. **`foreign` is available to every package, `native` is not.** `native` marks what the compiler and its runtime
   implement instead of TorbScript code, so only `std/` may use it; a `foreign` declaration of a C library is
   ordinary code any package can write.

3. **The C ABI is the contract, so both back ends behave the same.** The interpreter calls through a generic
   trampoline, a compiled binary links directly - once either one exists for this.

4. **Foreign types are `Pointer<Value>`, `CString`, the sized numbers, and a `foreign type` struct with C layout.**
   None of the first two are declared in `std/` today, so a real binding cannot be written yet even though the
   `foreign` block shape itself is checked.

5. **A package that declares `foreign` functions names its library per platform in `project.trb`.** `torb add` shows
   it, the way it shows every other capability, and a `Sandbox` can never grant it (see [The sandbox](../configuration/the-sandbox.md)).

6. **Memory a C library allocated is never freed by the runtime.** It is owned by a `shared type` with `Close`, and
   `using` is what makes the cleanup deterministic - the same mechanism as any other resource
   (see [There are no destructors](../execution/no-destructors.md)).

7. **A closure without captures can be passed where C expects a function pointer; a closure with captures cannot.**
   C has no lifetime for what the closure would need to keep alive, so a callback with state is wrapped in a
   `shared type` by the library instead.

8. **Nothing links or calls a `foreign` function yet.** The shape of a `foreign` block type checks, as the example
   above does; running a program that calls one is not possible on any back end today.

## What this is not

**`foreign` is not `native`, and the two are rejected in opposite places.** `native` is for `std/` alone; writing one
anywhere else is an error, while `foreign` is legal in any package precisely because it names an outside library
instead of the runtime.

```trb check
foreign "sqlite3" {
  fn sqlite3_close(database: Int64): Int32
}
```

```trb error
native fn fastPath(): Int64
// error: `native` is only allowed in a package of the standard library
```

## Related

- [The sandbox](../configuration/the-sandbox.md) - the one capability a sandboxed script can never be granted.
- [There are no destructors](../execution/no-destructors.md) - `Close` and `using`, for memory a C library allocated.
- [Control structures are functions](control-structures.md) - the other half of "extended by functions, not macros".
