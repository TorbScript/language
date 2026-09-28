---
title: torb debug
summary: torb debug is the debugger - a debug adapter over the Debug Adapter Protocol on standard input and output - with breakpoints, stepping, the call stack, locals and evaluate for a program or a test run in the VM.
kind: tooling
status: stable
order: 48
keywords:
  - torb debug
  - debugger
  - debug adapter
  - DAP
  - breakpoint
  - step
  - locals
  - watch
source:
  - compiler/src/debugger/adapter.trb
  - compiler/src/debugger/debuggee.trb
  - compiler/src/debugger/values.trb
  - compiler/src/debugger/evaluate.trb
  - compiler/src/vm/debug.trb
  - docs/design/DEBUGGER.md
---

`torb debug` is what an editor starts to debug TorbScript. It speaks the Debug Adapter Protocol over standard input and
output, runs the program or the tests a `launch` names in the VM, and stops where a breakpoint, a step, a pause or a
panic says so; while it stands, the editor reads the call stack, the locals of every frame with their values, and the
answers of expressions over them. Nobody runs it by hand: the VS Code extension of this repository starts it for every
Debug button, and so can any editor with a client of the protocol ([Set up your editor](../how-to/set-up-your-editor.md)).

## Synopsis

```text
torb debug                   The debug adapter, until the editor ends the session
torb debug --help            What it takes and what a launch names
torb debug --debuggee        Internal: the process the program runs in, which the adapter starts
```

## What it does

### Two processes

The adapter answers `initialize` itself and starts a second `torb` on `launch`, `torb debug --debuggee`, whose standard
output and standard error are the program's: what the program prints arrives in the editor as `output` events, and a
panic or `Process.exit` ends the program's process and not the session. The adapter hands every request to it, and it
answers them in their order; when it ends, the editor hears `exited` with its exit code and `terminated`. The program's
standard input carries the adapter's requests, so a debugged program cannot read standard input of its own.

### A launch

| Argument | Meaning |
|----------|---------|
| `program` | A file or a directory, as [`torb run`](torb-run.md) takes it |
| `args` | The program's arguments, what `Process.arguments()` answers |
| `test` | A test file or a directory of tests instead, as [`torb test`](torb-test.md) takes it |
| `filter` | With `test`: the full name of a test or a group (`Group > test`), or a list of them, as `torb test --filter` takes them |
| `cwd` | The program's working directory; a relative `program` is found from it |
| `env` | Environment variables of the program, an object of texts |
| `stopOnEntry` | Stop at the first statement |
| `justMyCode` | `true` unless it says `false`: a step does not stop in a file of the standard library, and its frames are shown faded |
| `noDebug` | Run without stopping: VS Code's Run Without Debugging |

The program is checked and lowered as `torb run` lowers it, with a marker in front of every statement and every binding
kept alive to the end of its block - what a debugger needs to stop at a line and to show a value there
(the design record, sections 5 and 6). A program that does not check is not run: what `torb
check` would say arrives as output, and `launch` fails.

### Requests

| Request | What the debugger does |
|---------|------------------------|
| `setBreakpoints` | Binds each line to the first statement of it in every function that has one there, or to the next line that has one, and answers the line it bound to |
| `setExceptionBreakpoints` | The filter `panic`, "Panics", on by default: the program stops where a panic begins - a failing `assert` too - before it ends the program or fails its test |
| `configurationDone` | The program starts |
| `threads` | One thread, `main`: a debugged program runs its tasks on one worker, so one stop stops everything |
| `stackTrace` | The frames, innermost first: a closure's body above the function that called it, a test body above the test file's top level, a task above the frames that resumed it |
| `scopes`, `variables` | One scope per frame, **Locals**: its parameters, its captures and every binding in sight at its line, with their values; a record, a case, a list, a map or a set opens into its fields, items or entries, a long list in pages |
| `evaluate` | A name of the frame with fields, tuple fields, items and entries below it - `point.x`, `pair.0`, `names[2]`, `ages["Ada"]` - for a hover, a watch and the debug console alike |
| `next`, `stepIn`, `stepOut` | Over the rest of the line, into the next call, out to the caller's next line |
| `continue`, `pause` | On until the next stop; a stop at the next statement the program runs |
| `exceptionInfo` | The message of the panic it stopped at |
| `disconnect`, `terminate` | The program's process ends at once |

A request that needs a stopped program and arrives while it runs waits until it stops; `pause`, `disconnect` and
`terminate` are acted on at once. A running program reads its requests every thousand statements, so a program that
waits in a native call - reading a file, sleeping, waiting for a task - pauses when it runs its next statement.

### Values

A value is shown the way the source writes it: `42`, `2.5`, `true`, `'a'`, `"text"`, `Point(x: 1, y: 2)`, `Some(3)`,
`Shape.Circle(radius: 2.0)`, `[10, 20, 30]`, `["a": 1]`, and `List<Int>(1200)` for a list too long for one line. A
binding in sight always holds its value: nothing a debugger shows is ever read out of freed memory.

### Exit codes

The adapter ends with 0. The debuggee ends with its program's exit code - 101 after a panic - which the `exited` event
carries, and with 1 where the program could not be launched. `2` for an argument either does not take.

## Examples

A session as it goes over the wire, without the headers, for a program whose line 18 calls a function:

```text
--> {"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"torbscript"}}
<-- {"seq":1,"type":"response","request_seq":1,"success":true,"command":"initialize","body":{"supportsConfigurationDoneRequest":true, ...}}
--> {"seq":2,"type":"request","command":"launch","arguments":{"program":"/work/app/main.trb"}}
<-- {"seq":2,"type":"response","request_seq":2,"success":true,"command":"launch","body":{}}
<-- {"seq":3,"type":"event","event":"initialized","body":{}}
--> {"seq":3,"type":"request","command":"setBreakpoints","arguments":{"source":{"path":"/work/app/main.trb"},"breakpoints":[{"line":18}]}}
<-- {"seq":4,"type":"response","request_seq":3,"success":true,"command":"setBreakpoints","body":{"breakpoints":[{"verified":true,"line":18}]}}
--> {"seq":4,"type":"request","command":"configurationDone"}
<-- {"seq":5,"type":"response","request_seq":4,"success":true,"command":"configurationDone","body":{}}
<-- {"seq":6,"type":"event","event":"stopped","body":{"reason":"breakpoint","threadId":1,"allThreadsStopped":true}}
--> {"seq":5,"type":"request","command":"evaluate","arguments":{"expression":"origin.x","frameId":1}}
<-- {"seq":7,"type":"response","request_seq":5,"success":true,"command":"evaluate","body":{"result":"1","type":"Int","variablesReference":0}}
```

`tests/debug/` holds whole sessions like this one - a breakpoint and the steps, the tests of a file with a filter, a
panic - which `sh tools/debug.sh` pipes into `torb debug` and compares with what it writes.

## Related

- [Set up your editor](../how-to/set-up-your-editor.md) - the Debug buttons of VS Code, and another editor's settings.
- The debugger - the design record: why the VM first, what a debugged program carries, and what is left.
- [torb run](torb-run.md) - what runs, without a debugger.
- [torb test](torb-test.md) - the tests and the filter a debug session of tests takes.

