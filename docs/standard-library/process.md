---
title: std/process
summary: Process for arguments, exiting and running a program to its end as a task, Child for a running program's pipes, and ProcessOutput for what it left behind.
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
use IoError from "std/fs"

fn firstArgument(): String? {
  Process.arguments().first()
}

fn sorted(): Task<Result<String, IoError>> {
  const output = Process.run("sort", [], input: "banana\napple\n").await()?
  output.standardOutput
}
```

## Declarations

### Process

```trb fragment
public native type Process {
  static fn arguments(): List<String>
  static fn exit(code: Int): Never
  static fn executablePath(): String?
  static fn run(program: String, arguments: List<String>, input: String = ""): Task<Result<ProcessOutput, IoError>>
  static fn runBlocking(program: String, arguments: List<String>): Result<ProcessOutput, IoError>
  static fn runPassingThrough(program: String, arguments: List<String>): Result<Int, IoError>
  static fn start(program: String, arguments: List<String>): Result<Child, IoError>
}
```

`arguments()` is the command line arguments, without the program itself. `exit(code)` ends the program without closing
open resources, which is why returning from the entry file is preferred. `executablePath()` is the absolute path of
the running program's own executable, with `/` between its parts, or `None` where the operating system does not say
(macOS, a BSD without `/proc`) rather than a guess from `arguments()`; it is how a program finds files it was
installed beside - `torb` finds its `std/` and `runtime/` this way. `run` runs a program to its end as a task: it
feeds `input` as the whole of the child's standard input, which then ends - an empty one at once, so the child never
waits for a terminal - and collects the exit code and both streams into a `ProcessOutput`, so a task writes
`Process.run(program, arguments).await()?`. The wait runs on the blocking pool (`offload` of [std/task](task.md)) where
the arguments may move there. The arguments are passed as they are, there is no shell, so nothing is interpreted and
nothing has to be quoted. A program that could not be started at all is an `IoError`; a program that ran and failed is
an exit code, which is why `torb build` can tell "there is no C compiler" from "the C compiler said no". `runBlocking`
is the same run for code that is not a task, such as the driver itself: it blocks the thread that asks, and the child
reads this program's standard input. `runPassingThrough`
is the same run with this program's own three streams instead of collecting them: nothing is buffered, so the
child's output appears while it writes it and a program reading standard input reads what the user is typing - a
driver that calls another program, such as `torb build` calling the C compiler, uses this and not `run`. `start` is
for output too big to collect, or that has to be read while it arrives; `run` and `runPassingThrough` are the short
forms for everything else.

### ProcessOutput

```trb fragment
public type ProcessOutput {
  exitCode: Int
  standardOutput: String
  standardError: String

  fn isSuccess(): Bool
}
```

`Process.run` and `Process.runBlocking` collect the two streams of the child apart: `standardOutput` is what it wrote to standard output and
`standardError` what it wrote to standard error. The order in which it interleaved the two is lost; a caller that
needs the output while it arrives starts the child with `Process.start` and reads `output()` and `errors()`.

What a program run with `Process.run` or `Process.runBlocking` left behind. `isSuccess()` is the usual question about a child process: did it do
what it was asked (`exitCode == 0`).

### Child

```trb fragment
public shared type Child with Close {
  fn input(): Sink<Bytes, IoError>
  fn output(): Source<Bytes, IoError>
  fn errors(): Source<Bytes, IoError>
  fn wait(): Task<Result<Int, IoError>>
  var fn close()
}
```

A child process that is still running, started with `Process.start`. `input().end()` closes its standard input,
which is how most filters learn that they are done. `close()` releases the pipes and stops waiting for the child; it
does not kill it, because ending somebody else's program is a decision and not a cleanup. `wait()` and every use of a
`Child`'s pipes need `.await()` (see [std/task](task.md)): each read, write and wait runs on a thread of the blocking
pool, so the worker goes on meanwhile, and a pipe that is still being read when `close()` comes is closed once the read
is back. Every line over a pipe is `.await()?` - the `?` is the `IoError`, and a cancellation stops the task at the
`await()` it meets, closing what its `using`s hold. `Process.start` answers a failure, not a child, for a program that
cannot be started at all.

```trb check
use Process from "std/process"
use IoError from "std/fs"

fn sorted(words: List<String>): Task<Result<Int, IoError>> {
  using child = Process.start("sort", [])?
  var input = child.input()
  for word in words {
    input.add("{word}\n".bytes()).await()?
  }
  input.end().await()?
  var output = child.output()
  var received = 0
  while const Some(chunk) = output.next().await()? {
    received = received + chunk.length()
  }
  const code = child.wait().await()?
  print "sort left with {code}"
  received
}
```

## Related

- [std/fs](fs.md) - `IoError`, which every method here that touches the operating system answers.
- [std/stream](stream.md) - `Source` and `Sink`, which `Child`'s three pipes are.
- [The standard library](index.md) - the other packages.
