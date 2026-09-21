---
title: std/process
summary: Process for arguments and exiting, Child for a running program's pipes, and ProcessOutput for what it left behind.
kind: package
status: stable
order: 150
keywords:
  - std/process
  - Process
  - Child
  - exit
  - command line
source:
  - std/process/src/lib.trb
---

`std/process` is the running program itself, and the child processes it starts. A child's three pipes are the same
`Source` and `Sink` a file or a socket has, so a pipeline between two programs is
`first.output().into(second.input())`.

## Import

```trb fragment
use Process, Child, ProcessOutput from "std/process"
```

```trb check
use Process from "std/process"

fn firstArgument(): String? {
  Process.arguments().first()
}
```

## Declarations

<!-- torb:declarations:begin -->

### Process

```trb fragment
public native type Process {
  static fn arguments(): List<String>
  static fn exit(code: Int): Never
  static fn run(command: String, arguments: List<String>): Result<ProcessOutput, IoError>
  static fn start(command: String, arguments: List<String>): Result<Child, IoError>
}
```

`arguments()` is the command line arguments, without the program itself. `exit(code)` ends the program without closing
open resources, which is why returning from the entry file is preferred. `run` runs a program to its end and collects
everything about it - the arguments are passed as they are, there is no shell, so nothing is interpreted and nothing
has to be quoted. A program that could not be started at all is an `IoError`; a program that ran and failed is an exit
code, which is why `torb build` can tell "there is no C compiler" from "the C compiler said no". `start` is for output
too big to collect, or that has to be read while it arrives; `run` is the short form for everything else.

### ProcessOutput

```trb fragment
public type ProcessOutput {
  exitCode: Int
  standardOutput: String
  standardError: String

  fn isSuccess(): Bool
}
```

What a program run with `Process.run` left behind. `isSuccess()` is the usual question about a child process: did it do
what it was asked (`exitCode == 0`).

### Child

```trb fragment
public native shared type Child with Close {
  fn input(): Sink<Bytes, IoError>
  fn output(): Source<Bytes, IoError>
  fn errors(): Source<Bytes, IoError>
  fn wait(): Task<Result<Int, IoError>>
  var fn close()
}
```

A child process that is still running, started with `Process.start`. `input().finish()` closes its standard input,
which is how most filters learn that they are done. `close()` releases the pipes and stops waiting for the child; it
does not kill it, because ending somebody else's program is a decision and not a cleanup. `wait()` and every use of a
`Child`'s pipes need `.await()`, so `Process.start` and `Child` wait on the same milestone as [std/task](task.md),
which is `status: planned`; `Process.run`, `Process.arguments` and `Process.exit` do not touch `Task` at all.

<!-- torb:declarations:end -->

## Related

- [std/fs](fs.md) - `IoError`, which every method here that touches the operating system answers.
- [std/stream](stream.md) - `Source` and `Sink`, which `Child`'s three pipes are.
- [The standard library](index.md) - the other packages.
