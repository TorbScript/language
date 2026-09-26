---
title: std/sandbox
summary: Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
kind: package
status: stable
order: 190
keywords:
  - std/sandbox
  - Sandbox
  - Script
  - capability
  - receiver script
source:
  - std/sandbox/src/lib.trb
---

> **Only the VM runs a script.** A script is always interpreted
> ([Receiver Scripts and the Sandbox](../design/SCRIPTS.md)): `torb run` runs a program that loads one in the VM; a
> native binary `torb build` compiled links the script host - the front end and the VM, built once per toolchain - and
> hands it the value as text, through the derived `Encode` and `Decode` of the receiver, which is therefore
> `Encode & Decode` there; `torb build --embed-vm` builds a native binary that runs the whole program in the VM.

`std/sandbox` loads `.trb` files as sandboxed receiver closures - the mechanism behind `project.trb` and every
configuration script. What a script may do is granted at the call site of `Sandbox.load`, never in the script or its
own project file, because a script that could grant itself capabilities would not be a sandbox. Left at the defaults, a
script has no IO, no network, no clock, no environment and no foreign functions.

## Import

```trb fragment
use Sandbox, Script, SandboxCapabilities, SandboxError from "std/sandbox"
```

```trb check
use Sandbox from "std/sandbox"

type ServerConfig {
  var port: Int = 8080
}

const script = Sandbox.load<ServerConfig>("./config.trb") {
  limits steps: 1_000_000
}
print script.isOk()
```

## Declarations

### Sandbox

```trb fragment
public native type Sandbox {
  static fn load<Value>(
    path: String,
    capabilities: (var self: SandboxCapabilities) => Void = {},
  ): Result<Script<Value>, SandboxError>
}
```

Type checks `path` against `Value` and answers a `Script`, ready to run against a `Value` of the caller's own. What is
wrong with the file (syntax, types, a module the script may not import) is reported here; what goes wrong while it runs
is reported by `Script.apply` instead. The trailing block grants capabilities beyond the defaults.

The path is relative to the directory the program runs in. A literal path names a file the checker checked against the
type argument with the program, which carries that script. Any other path - one the program computes - is read, checked
and lowered when the program asks for it, by the `torb` that runs it, and costs a check of the program the first time;
`Value` is then a type without type arguments. A file that is nowhere or does not check is the `SandboxError` of
`load`, with the line of the first error, and so is an import of the script that the grant does not name, with the line
of the `use`.

### Script

```trb fragment
public type Script<Value> {
  fn apply(var value: Value): Result<Void, SandboxError>
}
```

A loaded script, ready to run against a `Value` of the caller's own. It is not a closure, because its body runs inside
the sandbox and can still fail there - a step, memory or time limit, or a panic. This is the one panic in the language
that is recoverable, because the sandbox interprets the script and the script has a heap of its own.

### SandboxCapabilities

```trb fragment
public type SandboxCapabilities {
  var fn modules(...names: String)
  var fn files(readOnly: String = "", readWrite: String = "")
  var fn environment(...patterns: String)
  var fn limits(steps: Int = 1_000_000, memory: Int = 64.megabytes(), time: Duration = 2.seconds(), workers: Int = 1)
}
```

`modules` names additional parts of the standard library the script may `use`. `files` names the file system roots the
script may reach; a side left out stays closed. `environment` names the environment variable patterns the script may
read. `limits` guards against a runaway script (`loop {}`) - `workers` is the most workers its tasks may run on at once,
and the VM runs a script's tasks on the thread of its sandbox, so it never uses more than one - and `Int64.megabytes()` (`64.megabytes()`, after
`use Int64.megabytes from "std/sandbox"`) is the
byte unit these limits are written in.

### SandboxError

```trb fragment
public type SandboxError with Show, Error {
  line: Int
  message: String
}
```

What went wrong loading a script: a syntax or type error against the receiver type, or a capability it does not have.
`line` is `0` when the problem is not about one place in the script, such as the file not being readable at all.

## Related

- [std/project](project.md) - `Project`, the receiver type `project.trb` is loaded against.
- [std/os](os.md) - `Environment`, gated by `SandboxCapabilities.environment`.
- [The standard library](index.md) - the other packages.
