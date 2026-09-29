---
title: The playground
summary: The playground at torb.dev/play and in the course runs TorbScript in the browser - torb itself as WebAssembly checks and runs the program in its VM, with no server - and what a program can do there.
kind: tooling
status: stable
order: 48
keywords:
  - playground
  - /play
  - WebAssembly
  - browser-wasm64
  - OperatingSystem.Browser
  - share a program
  - exercise
source:
  - playground/playground.js
  - playground/playground-worker.js
  - playground/build.sh
  - playground/smoke-test.mjs
  - compiler/src/project/capability.trb
  - compiler/src/vm/run.trb
  - std/os/src/browser/system.trb
  - runtime/os/browser.c
  - docs/design/RELEASE.md#the-playground
---

The playground runs a program in your browser the way [`torb run`](torb-run.md) runs it on your machine: the toolchain
checks it, lowers it and runs it in the VM. It is the same `torb`, compiled to WebAssembly, so a program prints in the
playground what it prints anywhere else. Nothing is sent to a server; the page loads the toolchain once and works
offline from then on.

## Synopsis

```text
torb.dev/play                 A page with an editor, Run and the output
torb.dev/play#code=<source>   The same page with a program in it: the address is the link to share
Ctrl+Enter, Cmd+Enter         Run
Tab, Shift+Tab                Indent and outdent by two spaces; Escape, then Tab, leaves the editor

sh playground/build.sh              Build it: the compiler's C for browser-wasm64, compiled by emscripten
node playground/smoke-test.mjs      Run hello world in the result under node
```

## What it does

### Where a program runs

Press **Run** and the page hands the source to a worker thread, which starts a fresh copy of the toolchain, writes the
source as `main.trb` into a file system in memory, and runs `torb run main.trb`. What the program prints appears under
the editor as it prints it; what the checker reports appears there too, and the line it names is marked in the editor
with the message beside it. A place such as `main.trb:2:1` in the output moves the cursor there.

While a program runs, **Run** is **Stop**: a program that never ends is stopped there, and one that prints more than a
megabyte is stopped by the playground. Every run starts from nothing - an empty file system, a fresh memory - so one
run never sees what the last one left.

### What a program can do there

The target of the playground is `browser-wasm64`: `OperatingSystem.current` is `.Browser` and `Architecture.current`
is `.Wasm64`. What each package that reaches outside of the program does there follows one rule: real, simulated, or
an error at the import.

| Package | In the playground |
|---|---|
| printing, `std/time` | real: the output appears under the editor, the clock is the machine's |
| `std/fs` | simulated: a file system in memory, empty when the program starts, gone when it ends |
| `std/os` | an empty environment; the system questions answer `Unsupported`, `Directories.temporary()` is `/tmp`, and `Entropy` is real: `crypto.getRandomValues` of the page |
| standard input | at its end: `readLine()` answers `Ok(None)` |
| `std/task` | tasks take turns on one thread, in the order `TORB_WORKERS=1` gives everywhere |
| `std/process` | an error at the import: a program in the browser starts no processes |
| `std/network`, `std/http`, `std/tls`, `std/dns` | an error at the import: a program in the browser has no network |

### Sharing

On the playground's page, the address changes as you type: behind `#code=` stands the source,
compressed. Copying the address shares the program, and opening it opens the program. The part behind the `#` is never
sent to a server.

### Exercises

In the course, an exercise is a playground with an expected output. Beside **Run** it has **Reset**, which brings the
starting code back, and **Show solution**, which puts the solution into the editor; after a run, a line says whether
the output is the expected one.

### What the browser needs

WebAssembly with exception handling - Chrome and Edge 95, Firefox 100, Safari 15.2 or newer - and JavaScript. The
toolchain is about 13 MB, 4 MB as the server sends it compressed, fetched once when you first focus an editor or press
Run and then kept by the browser. The page makes no request to another host and stores nothing on the device.

### How it is built

The compiler is TorbScript that emits C, so the toolchain of the browser is the compiler's own C for the target
`browser-wasm64` (`torb build ./compiler --emit-c --target browser-wasm64`) together with `runtime/`, compiled by
emscripten, pinned by version in `playground/build.sh`. `std/` is embedded into the WebAssembly. [torb docs
site](torb-docs-site.md) copies what the script wrote into `assets/` of the site, and the site image builds it from the
release's own source.

## Examples

An import the browser does not have is an error at the import. The program below runs on every machine; in the
playground the run stops before its first instruction:

```trb
use Process from "std/process"

print Process.arguments()
```

```console
error: `std/process` does not exist in the browser: a program in the browser starts no processes
 --> main.trb:1:1
  |
1 | use Process from "std/process"
  | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  = The playground runs a program in the browser: printing and the clock are real, `std/fs` is in memory
```

A file written, read back and removed, in the file system in memory - and on your machine, where the same program
runs in the working directory:

```trb run
use File from "std/fs"

File.writeText("notes.txt", "kept in memory").expect("written")
print File.readText("notes.txt").expect("read")
File.remove("notes.txt").expect("removed")
// prints kept in memory
```

Building the playground and running its smoke test, from the root of a checkout with a bootstrapped `torb`:

```console
$ sh playground/build.sh
emitting the compiler's C for browser-wasm64
compiling in emscripten/emsdk:6.0.10
linking build/playground/torb.js and build/playground/torb.wasm
wrote build/playground/: torb.wasm 13212198 bytes, 4020117 gzipped
$ node playground/smoke-test.mjs
ok    hello world (860 ms)
```

## Related

- [torb run](torb-run.md) - the command the playground runs, and the VM it runs in.
- [Compile-time branches](../language/execution/compile-time-branches.md) - `match OperatingSystem.current`, which has
  a `.Browser` arm.
- [torb docs site](torb-docs-site.md) - the site that serves the playground and turns runnable blocks into it.
- [std/os](../standard-library/os.md) - what the system questions answer in the browser.

