---
title: Receiver scripts
summary: A .trb file can be loaded as the body of a receiver closure and type checked against a receiver type before it runs, but nothing runs one yet - the sandboxed VM that would is still planned.
kind: reference
status: planned
order: 30
keywords:
  - receiver script
  - Sandbox.load
  - Script
source:
  - CONCEPT.md#receiver-scripts-and-the-sandbox
  - std/sandbox/src/lib.trb
  - docs/ARCHITECTURE.md
---

> **Planned.** This feature is designed but not implemented. Nothing on this page runs today.

A [receiver closure](receiver-closures.md) is a value passed at a call site. A
[receiver script](../../glossary.md#receiver-script) is the same idea with a whole file standing in for that value:
`project.trb` is one already, and any `.trb` file can become one through `Sandbox.load`.

## Example

```trb check
use Sandbox, SandboxError from "std/sandbox"

type ServerConfig {
  var host: String = "localhost"
  var port: Int = 8080
}

fn loadConfig(path: String): Result<ServerConfig, SandboxError> {
  const script = Sandbox.load<ServerConfig>(path)?
  var config = ServerConfig()
  script.apply(config)?
  Ok config
}
```

## Syntax

```text
Sandbox.load<Value>(path: String): Result<Script<Value>, SandboxError>
Script.apply(var value: Value): Result<Void, SandboxError>
```

## Rules

1. **Loading and running are two steps, and each has its own failure.** `Sandbox.load<Value>(path)` reports what is
   wrong with the file itself: a syntax error, a type error against `Value`, or a module the script may not import.
   `script.apply(value)` reports what went wrong while the body ran, against a `value` of the caller's own.

2. **The type argument of `load` is the receiver, and it is checked as the body of `(var self: Value) => Void`.**
   `Value` doubles as the whitelist of what the file can do: its members are what the script can name beyond the
   prelude.

3. **The path is relative to the directory of the project, not to the file that calls `load`.** That is the
   opposite of [`use`](../modules-and-packages/use.md), whose relative path is relative to the importing file - a
   loaded script is a question about where the program runs, not about the source tree.

4. **A receiver script may hold top-level code**, unlike an ordinary module: it is one of the four kinds of file
   [top-level code](../modules-and-packages/top-level-code.md) allows, alongside an entry file, an unimported script
   and a `*.test.trb` file.

5. **Nothing loads or runs a receiver script yet.** The bytecode VM a sandbox needs to interpret one is milestone 7
   and does not exist; even `project.trb`, the one built-in use of this mechanism, is read today by parsing its
   literal top-level command calls directly rather than through `Sandbox.load`. The example above type checks
   against the real `Sandbox` and `Script` declarations; running it panics with an unimplemented native, on every
   back end.

## What this is not

**A receiver script is not imported.** `use` names a module and its exports; `Sandbox.load` names a file and checks
it as a closure body, and the file itself declares nothing another file could import from it.

```trb check
use Sandbox from "std/sandbox"

type ServerConfig {
  var host: String = "localhost"
}

fn describe(path: String) {
  print "loading {path} as ServerConfig"
}

describe "./config"
```

```trb skip a receiver script cannot be produced inside one snippet of this documentation, and nothing runs Sandbox.load yet regardless; see rule 5 above
use Host from "./config"
```

## Related

- [The sandbox](the-sandbox.md) - the capabilities granted at the call site of `Sandbox.load`.
- [Receiver closures](receiver-closures.md) - the closure form a receiver script's body is checked as.
- [Top-level code](../modules-and-packages/top-level-code.md) - why a receiver script may hold statements a module cannot.

