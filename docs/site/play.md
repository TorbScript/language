---
title: Playground
summary: Write TorbScript and run it in your browser - the whole toolchain as WebAssembly checks, compiles and runs the program on your machine, with no server and no account, and the address is the link to share it.
kind: site
status: stable
order: 30
source:
  - playground/playground.js
  - playground/build.sh
  - docs/tooling/the-playground.md
  - docs/design/RELEASE.md#the-playground
---

<div class="playground-page-host" data-playground-page>

```trb run
const name = "TorbScript"
print "Hello from {name}!"
// prints Hello from TorbScript!
```

</div>

Press **Run**, or Ctrl+Enter (Cmd+Enter on a Mac). Your program is checked, compiled and run by `torb` itself, built as
WebAssembly and running in this tab: nothing you write is sent anywhere. The first run loads the toolchain, about 4 MB;
after that it starts at once.

The address of this page holds your code, compressed behind the `#`, and changes as you type. To share a program, copy
the address: the part behind the `#` never reaches a server.

What a program can do here: print, read the clock, and read and write files in a file system that lives in memory and
is empty when the program starts. It cannot start other programs or reach the network, so an import of `std/process`,
`std/network`, `std/http`, `std/tls` or `std/dns` is an error. See [the playground](../tooling/the-playground.md) for
the details, and [install TorbScript](install.md) to run the rest on your own machine.
