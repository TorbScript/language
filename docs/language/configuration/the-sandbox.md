---
title: The sandbox
summary: A script has no IO, no network, no clock, no environment and no foreign functions by default, and only the caller of Sandbox.load can grant more, in a block that names exactly what is granted.
kind: reference
status: draft
order: 40
keywords:
  - sandbox
  - capability
  - SandboxCapabilities
  - limits
source:
  - CONCEPT.md#receiver-scripts-and-the-sandbox
  - std/sandbox/src/lib.trb
  - docs/ARCHITECTURE.md
---

> **Draft.** It runs in the VM: a program `torb run` runs loads and applies a script, and so does the toolchain for a
> `project.trb`; the native back end refuses `Script.apply` until a binary can embed the VM
> ([Receiver Scripts and the Sandbox](../../design/SCRIPTS.md)).

A [receiver script](receiver-scripts.md) is untrusted code from outside the program. What it can touch is not a
setting inside the script or its own `project.trb`; it is a
[capability](../../glossary.md#capability) the caller of `Sandbox.load` grants at the one place that is not the
script itself.

## Example

```trb check
use Sandbox from "std/sandbox"
use Int64.megabytes from "std/sandbox"

type ServerConfig {
  var host: String = "localhost"
}

fn loadConfig(path: String) {
  Sandbox.load<ServerConfig>(path) {
    modules "std/text", "std/time"
    files readOnly: "./config"
    environment "APP_*"
    limits steps: 1_000_000, memory: 64.megabytes(), time: 2.seconds()
  }
}
```

`megabytes` is a member `std/sandbox` adds to `Int64`, a type it does not own, so a file that writes `64.megabytes()`
names where it comes from: `use Int64.megabytes from "std/sandbox"`. `2.seconds()` needs nothing, because the prelude
re-exports that one.

## Syntax

```text
Sandbox.load<Value>(path, capabilities: (var self: SandboxCapabilities) => Void = {}): Result<Script<Value>, SandboxError>

SandboxCapabilities.modules(...names: String)
SandboxCapabilities.files(readOnly: String = "", readWrite: String = "")
SandboxCapabilities.environment(...patterns: String)
SandboxCapabilities.limits(steps: Int = 1_000_000, memory: Int = 64.megabytes(), time: Duration = 2.seconds())
```

## Rules

1. **Left at the defaults, a script has no IO, no network, no clock, no environment and no foreign functions.** The
   file scope is the prelude, the receiver and what the script imports - and a receiver script's `use` lines are what
   the `modules` capability decides, not the script's own choice: an import the grant does not name fails the load.

2. **Every capability is granted at the call site of `Sandbox.load`, never in the script or its `project.trb`.** A
   script that could grant itself a capability would not be a sandbox; the trailing block on `load` is a receiver
   closure over `SandboxCapabilities`, configured the same way any other builder is.

3. **`modules` names additional packages of the standard library the script may `use`.** A script has no other way
   to reach one, because a receiver script's imports are checked against exactly this list.

4. **`files` names the file system roots a script may reach, one side at a time.** `readOnly` and `readWrite` each
   default to nothing; a side left out stays closed, so a script granted `readOnly` alone cannot write anywhere.

5. **`limits` guards against a script that never stops.** `steps`, `memory` and `time` each have a default (1,000,000
   steps, 64 megabytes, 2 seconds), so a script that does not ask for more still cannot run forever.

6. **`Script.apply` reports a limit or a panic as a value, not as a crash of the host.** The one panic in the
   language that can be recovered from is the one inside a script the host's own VM is interpreting - a sandbox
   whose failures aborted the caller would not be a sandbox.

7. **Foreign functions can never be granted to a script.** There is no capability for them in
   `SandboxCapabilities`, because a `foreign` declaration reaches outside the process the sandbox itself runs in.

8. **The VM enforces all of it, and only the VM runs a script.** A program `torb run` runs loads scripts under
   these capabilities and limits - from a literal path or from one it computes - and so does the toolchain for a
   `project.trb` (`torb manifest`, see [project.trb](../../tooling/project-trb.md)); the native back end refuses
   `Script.apply` until a binary can embed the VM. A relative root is read against the directory
   the program runs in.

## What this is not

**A capability is not a permission the script requests.** There is no line inside a `.trb` script that asks for a
file or an environment variable; every capability is decided once, by the code that calls `Sandbox.load`, before the
script is even read.

```trb check
use Sandbox from "std/sandbox"

type ServerConfig {
  var host: String = "localhost"
}

fn loadConfig(path: String) {
  Sandbox.load<ServerConfig>(path) {
    files readOnly: "./config"
  }
}
```

```trb error
use Sandbox from "std/sandbox"

type ServerConfig {
  var host: String = "localhost"
}

fn loadConfig(path: String) {
  Sandbox.load<ServerConfig>(path) {
    grant "files", "./config"
  }
}
// error: Cannot find `grant` here
```

## Related

- [Receiver scripts](receiver-scripts.md) - what is loaded and checked before a capability ever matters.
- [Foreign functions](../extensibility/foreign-functions.md) - the one capability a sandbox can never grant.
- [Packages](../modules-and-packages/packages.md) - the same "visible from the imports alone" idea, for a whole dependency graph.
