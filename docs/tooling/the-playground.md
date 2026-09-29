---
title: The playground
summary: The playground at torb.dev/play and in the course runs TorbScript in the browser - torb as WebAssembly runs the program in its VM and its language server completes and checks as you type, with no server - and what a program can do there.
kind: tooling
status: stable
order: 48
skill: omit
keywords:
  - playground
  - /play
  - WebAssembly
  - browser-wasm64
  - OperatingSystem.Browser
  - share a program
  - exercise
  - examples
  - completion
  - CodeMirror
  - language server in the browser
source:
  - playground/playground.js
  - playground/playground-worker.js
  - playground/editor/editor.mjs
  - playground/editor/build.sh
  - playground/build.sh
  - playground/smoke-test.mjs
  - playground/examples/index.json
  - compiler/src/project/capability.trb
  - compiler/src/vm/run.trb
  - compiler/src/language-server/command.trb
  - std/os/src/browser/system.trb
  - runtime/os/browser.c
  - tools/gates.sh
  - docs/design/RELEASE.md#the-playground
---

The playground runs a program in your browser the way [`torb run`](torb-run.md) runs it on your machine: the toolchain
checks it, lowers it and runs it in the VM. It is the same `torb`, compiled to WebAssembly, so a program prints in the
playground what it prints anywhere else, and the editor has the same [language server](torb-lsp.md) an editor on your
machine has: completion, the checker's errors as you type, hover and formatting. Nothing is sent to a server; the page
loads the toolchain once and works offline from then on.

## Synopsis

```text
torb.dev/play                 A page with the examples, an editor, Run and the output
torb.dev/play#code=<source>   The same page with a program in it: the address is the link to share
torb.dev/play#example=<id>    The same page with one of the examples
Ctrl+Enter, Cmd+Enter         Run
Ctrl+Space                    Completion; it also opens by itself as you type a name or a `.`
F12, Ctrl or Cmd and a click  Go to the definition of what is under the cursor
F2                            Rename it
Shift+Alt+F                   Format, as `torb format` does
Tab, Shift+Tab                Indent and outdent by two spaces; Escape, then Tab, leaves the editor

sh playground/build.sh              Build it: the compiler's C for browser-wasm64, compiled by emscripten
node playground/smoke-test.mjs      Run hello world in the result under node, and the language server
sh playground/editor/build.sh       Build the editor bundle from its source and lock file
```

## What it does

### Where a program runs

Press **Run** and the page hands the source to a worker thread, which starts a fresh copy of the toolchain, writes the
source as `main.trb` into a file system in memory, and runs `torb run main.trb`. What the program prints appears under
the editor as it prints it; what the checker reports appears there too, and the place it names is marked in the
editor. A place such as `main.trb:2:1` in the output moves the cursor there.

While a program runs, **Run** is **Stop**: a program that never ends is stopped there, and one that prints more than a
megabyte is stopped by the playground. Every run starts from nothing - an empty file system, a fresh memory - so one
run never sees what the last one left.

### The editor

The editor is [CodeMirror](https://codemirror.net/) with its language server client, connected to `torb lsp`, which
runs in a worker of its own in the page. What the language server does on your machine it does here:

- **Completion** as you type a name, and the members of a value after a `.`, with the signature of each and its
  documentation beside it.
- **The checker's errors while you type**: half a second after the last keystroke the text is checked, and every error
  is a wavy underline with a mark in the gutter; the pointer on it shows the message.
- **Hover** shows the type of a name and its documentation, and **signature help** the parameters of the call you are
  in.
- **Go to definition** and **rename** within the program, and **Format** - the button on `/play`, or Shift+Alt+F - is
  `torb format`.
- **The colours** are the site's: the lexer's at once, and the checker's as soon as it has seen the text - a parameter
  in its own colour, and a binding that can change underlined.

The language server is one per page and holds every editor of it, and it is independent of the runs: a run never waits
for a check, and a check never waits for a run. Until you are about to write - at once on `/play`, at the first click
into a runnable block elsewhere - an editor is a light one that colours and runs without the language server, so a page
with many runnable blocks loads nothing it does not use.

### The examples

`/play` opens with an example, and **Examples** above the editor lists the others by category, each with its title,
its level and one line about it; on the German site they are in German. Choosing one replaces the code - after a
question, if you changed what was there - and the address names the example, `#example=<id>`, until you change the
code; from then on it carries the code itself.

### What a program can do there

The target of the playground is `browser-wasm64`: `OperatingSystem.current` is `.Browser` and `Architecture.current`
is `.Wasm64`. What each package that reaches outside of the program does there follows one rule: real, simulated, or
an error at the import.

| Package | In the playground |
|---|---|
| printing, `std/time` | real: the output appears under the editor, the clock is the machine's |
| `std/fs` | simulated: a file system in memory, empty when the program starts, gone when it ends |
| `std/os` | an empty environment; the system questions answer `Unsupported`, `Directories.temporary()` is `/tmp`, and `Entropy` is real: `crypto.getRandomValues` of the page |
| standard input | at its end: `readLine()` answers `Ok(None)`, and so does the first read of `standardInput()` |
| `std/task` | tasks take turns on one thread, in the order `TORB_WORKERS=1` gives everywhere |
| `std/process` | an error at the import: a program in the browser starts no processes |
| `std/network`, `std/http`, `std/tls`, `std/dns` | an error at the import: a program in the browser has no network |

### Sharing

On [the playground's page](../site/play.md), the address changes as you type: behind `#code=` stands the source,
compressed. Copying the address shares the program, and opening it opens the program. The part behind the `#` is never
sent to a server.

### Exercises

In the course, an exercise is a playground with an expected output. Beside **Run** it has **Reset**, which brings the
starting code back, and **Show solution**, which puts the solution into the editor; after a run, a line says whether
the output is the expected one.

### What the browser needs

WebAssembly with exception handling - Chrome and Edge 95, Firefox 100, Safari 15.2 or newer - and JavaScript. The
toolchain is about 13 MB, 4 MB as the server sends it compressed, fetched once when you first click into an editor or
press Run - at once on `/play` - and then kept by the browser; the editor is 450 KB more, 140 KB compressed. The page
makes no request to another host and stores nothing on the device.

### How the language server runs in the page

A browser cannot wait inside a read: a message reaches the worker only once its thread is back in its event loop. So
the worker runs the real `torb lsp` - the same server, over the same standard input and output, as an editor on your
machine starts - and the runtime's browser target makes its standard input a thing to wait for without blocking
(`runtime/os/browser.c`). A read that finds nothing yet waits as an operation of the runtime, and where the server's
scheduler would sleep it returns to the worker instead. The worker frames each message of the editor with its
`Content-Length` header into standard input and lets the server go on until it waits again; what the server writes it
cuts into messages and hands to the editor. The server asks for its timers the same way, and the worker calls it
again when they are due.

### How it is built

The compiler is TorbScript that emits C, so the toolchain of the browser is the compiler's own C for the target
`browser-wasm64` (`torb build ./compiler --emit-c --target browser-wasm64`) together with `runtime/`, compiled by
emscripten, pinned by version in `playground/build.sh`. `std/` is embedded into the WebAssembly. [torb docs
site](torb-docs-site.md) copies what the script wrote into `assets/` of the site, and the site image builds it from the
release's own source.

The editor is `playground/playground-editor.js`, a bundle of CodeMirror 6 and `@codemirror/lsp-client` with the
playground's own part, `playground/editor/editor.mjs`. It is committed, so building the playground needs neither npm nor
the network: `sh playground/editor/build.sh` rebuilds it with `npm ci` and esbuild at the exact versions of
`playground/editor/package-lock.json`, where the editor changes, and the result is committed with the change; CI checks
that the committed bundle is what its source builds.

### The example gallery

`playground/examples/` holds the programs the gallery on `torb.dev/play` offers beside the empty editor, one file each
and `index.json` naming them: `id`, `file`, `title`, a one-sentence `description`, a `category`
(`Basics`, `Types`, `Errors`, `Collections`, `DSLs`, `Fun`), a `level` (`beginner` or `intermediate`), `"default": true`
on the one example the page opens with, and the same `title`/`description` in German under `de`. `playground/build.sh`
copies `index.json` and every `.trb` file it names into `build/playground/examples/`, beside `torb.wasm`, so the page
serves them the way it serves the toolchain itself - no server call.

Every example is also a program `sh tools/gates.sh a` runs: each `<name>.trb` has a `<name>.expected` beside it, and
the gate runs it with `torb run` (the playground never builds natively) and compares. Adding one is the same shape as
a program of `tests/language/`:

1. Write `playground/examples/<name>.trb` - it has to run in the browser (no `std/process` or network, `std/fs` is in
   memory, a program stops well under a second) and print something worth seeing.
2. Run it and save what it prints: `build/release/torb run playground/examples/<name>.trb > playground/examples/<name>.expected`.
3. Add its entry to `index.json`, English and German.
4. `sh tools/gates.sh a` - the `check playground/examples` and `playground/examples against .expected` gates catch a
   mistake in either the program or the manifest.

## Examples

An import the browser does not have is an error at the import. The program below runs on every machine; in the
playground the run stops before its first instruction, and the editor underlines the import as soon as it is typed:

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
wrote build/playground/: torb.wasm 13510389 bytes, 4093927 gzipped
$ node playground/smoke-test.mjs
ok    hello world (570 ms)
...
ok    the language server answers initialize (90 ms)
ok    a document with an error is published with its diagnostic (187 ms): Cannot find `prnt` here
ok    a change while typing is checked again (33 ms)
ok    completion at a member access answers its members (105 ms): 69 items
ok    hover and semantic tokens answer (7 ms)
ok    shutdown and exit end the server with 0 (14 ms)
```

## Related

- [torb run](torb-run.md) - the command the playground runs, and the VM it runs in.
- [torb lsp](torb-lsp.md) - the language server the editor talks to.
- [Compile-time branches](../language/execution/compile-time-branches.md) - `match OperatingSystem.current`, which has
  a `.Browser` arm.
- [torb docs site](torb-docs-site.md) - the site that serves the playground and turns runnable blocks into it.
- [std/os](../standard-library/os.md) - what the system questions answer in the browser.
- [Verify your work](verifying-your-work.md) - `sh tools/gates.sh a`, which runs every example of the gallery.
