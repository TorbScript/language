# JavaScript and PHP Back Ends

**Status: planned** — milestone 9 ([ROADMAP.md](../ROADMAP.md)); nothing of it exists. This record is a stub that keeps
what was decided on 2026-09-18 and 2026-09-19; the research and the design come at the start of the milestone.

**TorbScript compiles to JavaScript and to PHP as well as to C and to the bytecode of the VM.** JavaScript, so that
the front end of an interactive application is written in the same language as its server. PHP, so that a program
runs on a classic shared web host where nothing but PHP is available. Packages that JavaScript and PHP code can consume
are a goal where they come cheaply, not a condition.

## 1. What the architecture already gives

- **Everything up to the typed IR is shared**, and a back end turns the IR into text ([ARCHITECTURE.md](../ARCHITECTURE.md),
  "Back Ends"). C is the first back end and the VM the second; these are the third and the fourth.
- **The IR stays free of C's assumptions**: layouts and niches are hints a back end may ignore.
- **Natives stay few**, because every back end has to provide every one of them. The kernel of natives
  ([ROADMAP.md](../ROADMAP.md), "The kernel of natives") is done before this milestone, so a new back end starts from a
  short list of IR intrinsics and the rest of `std` is TorbScript it compiles like any other code.

## 2. What is decided

- **The language's semantics, not the host's.** The integer types keep their fixed widths and panic on overflow
  exactly as in C, so a JavaScript `Int` is an exact 64-bit integer however it is represented. A type that may contain
  a `Close` object stays reference counted even where the host has a garbage collector, because the moment its
  `close()` runs is part of the program's meaning ([DESTRUCTORS.md](DESTRUCTORS.md) section 2a). Everything else can
  leave memory to the host's collector and needs only copy on write for `var` paths.
- **Tasks in JavaScript** are promises on the event loop of the host.
- **Tasks in PHP** run on an event loop that is chosen when the program starts: Fibers where PHP has them (8.1 and
  later), `ext-uv` or `ext-ev` where one is installed, plain `stream_select` otherwise. The goal is "copy the files onto
  the web host and it runs", with no extension required. Without Fibers a task is compiled into a state machine, which
  the C back end does already, so older versions of PHP are possible; how far back is decided by the research.
  Composer libraries may be required, but compatibility with a classic web host comes first.
- **A capability table per target.** Each capability package is, per target, a real implementation, a simulation, or a
  compile error at the import: `std/fs` in the browser over the File System Access API or the origin private file
  system, `std/os`'s environment empty in the browser, `std/process` a compile error there. [RELEASE.md](RELEASE.md)
  section 6 applies the same rule to the playground's WebAssembly target.
- **Packages for the host's ecosystem** - npm with `.d.ts` declarations, Composer with PSR-4 autoloading - are
  generated for `public` interfaces that do not depend on generic tricks. Value semantics across the boundary mean a
  copy on the way in and on the way out.

## 3. Open

1. **How an exact 64-bit `Int` is represented in JavaScript**, and what crosses the boundary to JavaScript code. A
   `number` is what integration with existing code wants (an earlier answer preferred it for that reason), so the
   likely answer is exact arithmetic inside and a `number` checked to fit 53 bits at the boundary; a `BigInt` is exact
   everywhere and slow.
2. **The oldest version of PHP** that is supported, from the research into Fibers and the event-loop extensions.
3. **Which capabilities can be simulated** in the browser, and which are a compile error.
