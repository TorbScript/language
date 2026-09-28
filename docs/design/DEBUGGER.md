# The Debugger

**Status: decided on 2026-09-28; the VM half is being built** (section 18). The goal is the owner's: in VS Code every
`test` block and every entry file gets a **Debug** button next to its Run button, and debugging works as in any
mainstream language - breakpoints, stepping, the call stack, locals, watch and evaluate. This record decides how, one
table of options per decision, and says what was chosen and why.

```text
   VS Code ──DAP over stdio──► torb debug                     the adapter (compiler/src/debugger/adapter.trb)
                                 │   answers initialize and threads itself, forwards every other request,
                                 │   turns the program's output into `output` events
                                 │ starts, with the program's own standard output and error as pipes
                                 ▼
                               torb debug --debuggee          the debuggee (compiler/src/debugger/debuggee.trb)
                                 │   reads the requests from standard input, answers on standard output
                                 │   (each answer one line behind U+001E, between what the program prints)
                                 ▼
                               the VM with a Debugger         compiler/src/vm/debug.trb, the loop of interpret.trb
                                 │   a `statement` instruction per statement - only in a debugged program -
                                 │   asks the Debugger whether to stop; a panic calls it back before it ends
                                 ▼
                               the program's frames, read by address and rendered through the IR's types
```

- **[1. The protocol: the Debug Adapter Protocol, and `torb debug`](#1-the-protocol-the-debug-adapter-protocol-and-torb-debug)**
- **[2. Two processes: the adapter and the debuggee](#2-two-processes-the-adapter-and-the-debuggee)**
- **[3. The channel between them](#3-the-channel-between-them)**
- **[4. The VM first; native programs later](#4-the-vm-first-native-programs-later)**
- **[5. What a debugged program carries: statements and the bindings in sight](#5-what-a-debugged-program-carries-statements-and-the-bindings-in-sight)**
- **[6. A binding lives to the end of its scope](#6-a-binding-lives-to-the-end-of-its-scope)**
- **[7. Breakpoints, and zero cost without a debugger](#7-breakpoints-and-zero-cost-without-a-debugger)**
- **[8. Stepping across frames, closures and tasks](#8-stepping-across-frames-closures-and-tasks)**
- **[9. Pause](#9-pause)**
- **[10. Frames and locals](#10-frames-and-locals)**
- **[11. How values are shown](#11-how-values-are-shown)**
- **[12. Evaluate and watch](#12-evaluate-and-watch)**
- **[13. Panics, and `?`](#13-panics-and-)**
- **[14. Tasks and workers](#14-tasks-and-workers)**
- **[15. The REPL](#15-the-repl)**
- **[16. Tests and entry files](#16-tests-and-entry-files)**
- **[17. VS Code](#17-vs-code)**
- **[18. Slices](#18-slices)**
- **[19. What it costs](#19-what-it-costs)**
- **[20. Open](#20-open)**

---

## 1. The protocol: the Debug Adapter Protocol, and `torb debug`

| Option | For | Against |
|---|---|---|
| **The Debug Adapter Protocol (DAP) over standard input and output, `torb debug` as the adapter executable** | What VS Code, Neovim (nvim-dap), Emacs (dap-mode), Helix, Zed and the JetBrains IDEs speak; framed like the Language Server Protocol, so `torb lsp`'s framing and JSON are reused | A second protocol besides the language server's |
| The debugger inside `torb lsp` | One process per editor | DAP is its own session with its own life cycle, and an editor starts a debug adapter per session; no client expects a language server to debug |
| A protocol of our own, with an extension-side adapter in JavaScript | Nothing to learn from DAP | Every editor but VS Code would need its own adapter; the logic would live outside the compiler that knows the program |

**Decision: DAP over stdio, with `torb debug` as the adapter**, the way `torb lsp` is the language server: one binary,
one subcommand, nothing to install besides `torb`. The framing (`Content-Length`, then the JSON body) is the one
`compiler/src/language-server/protocol.trb` reads and writes already, and `language-server/json.trb` builds and reads
the bodies. `torb debug` adds no native, no flag the build uses and no syntax: it needs no second commit and no seed
that knows it (CLAUDE.md, "Seed and breaking changes").

## 2. Two processes: the adapter and the debuggee

A debugged program writes to standard output and standard error, may read standard input, may panic and may call
`Process.exit`. A DAP adapter on stdio owns standard input and output, and must outlive the program to report its exit.

| Option | For | Against |
|---|---|---|
| One process: `torb debug` speaks DAP and runs the VM | No second process, no second protocol | The program's `print` lands in the DAP stream; a panic or `Process.exit` ends the adapter with the program; the VM refuses to run its scheduler inside a task, and a DAP reader that waits for a pause while the program runs has to be one |
| **Two processes: the adapter speaks DAP with the editor, a debuggee process runs the program** | The program's streams stay the program's, its exit is the debuggee's; the adapter is a relay that survives everything | A channel between the two (section 3) |
| The adapter in the extension, in JavaScript | No second TorbScript process | Only VS Code would have it (section 1) |

**Decision: two processes, both `torb`.** `torb debug` is the adapter: it answers `initialize`, starts
`torb debug --debuggee` on `launch`, forwards every request to it, and turns what the program writes into `output`
events. The debuggee does everything that needs the program - it compiles it, resolves breakpoints against its line
table, runs it in the VM and answers `stackTrace`, `scopes`, `variables` and `evaluate` - so the adapter stays a relay
of about two hundred lines, and the logic lives next to the compiler that knows the program. When the debuggee ends,
the adapter sends `exited` with its exit code and `terminated`.

The adapter answers `threads` itself (section 14: one thread) and renumbers the `seq` of every message it sends, so
the debuggee's answers need no numbering of their own.

## 3. The channel between them

The debuggee runs the VM at the top level of `torb` - not in a task (section 2) - so it can only use blocking reads,
and while the program runs it has to notice a `pause` without waiting for one.

| Option | For | Against |
|---|---|---|
| A loopback socket the adapter listens on | The program keeps all three standard streams | No blocking socket in `std/network` (every read is a task); raw sockets in the kernel would duplicate the runtime's Winsock loading of `runtime/os/iocp.c` |
| Files the two poll | Portable, nothing new in C | Latency, and a file two processes write is a race on Windows |
| **The debuggee's standard input for requests; its standard output for answers, each one line behind U+001E between what the program prints** | Three kernel operations in C: a buffered line reader of standard input, whether a line is waiting without blocking, a flush; the answers and the program's output are one ordered stream, so an answer never overtakes the output before it | A debugged program has no standard input of its own |

**Decision: standard input and output, framed by U+001E (the ASCII record separator).** The adapter writes each
request as one line of JSON to the debuggee's standard input. The debuggee writes each answer and event as U+001E,
the JSON, and a line feed to its standard output, with the host's own `print` - through the same buffer as the
program's `print` - and flushes it (kernel operation `DebugFlush`). JSON escapes every control character, so neither
U+001E nor a line feed can occur inside a message; the adapter takes everything between two messages as output of the
program, and a record separator the program itself printed that is not followed by a message it can parse is output
too. Standard error stays the program's alone.

The debuggee reads its requests through kernel operations of its own (`DebugRead`, `DebugTake`, `DebugPending`,
`runtime/debug.c`): a reader with its own buffer, because a `FILE *` buffer would hide a waiting `pause` from the
check that looks at the pipe (`PeekNamedPipe` on Windows, `poll` elsewhere). An operation of the kernel is a row of a C
table and a constant of the emitter, not a native (docs/design/VM.md section 6), so none of it needs a second commit.

What the program gives up is its standard input: a program that reads it under the debugger reads the adapter's
requests. `runInTerminal` - the editor's terminal as the program's streams, the control channel a loopback socket - is
section 20's.

## 4. The VM first; native programs later

| Option | For | Against |
|---|---|---|
| **The VM first, native programs as a later phase** | `torb run` and `torb test` are the VM already; the interpreter is TorbScript, so a hook is a line of the loop, and frames, registers and values are data it reads by address | A native binary is not debugged yet |
| Native first: DWARF from the C back end, gdb or lldb behind DAP | The debuggers exist | Their DAP adapters (`lldb-dap`, `cppdbg`) speak C: a local is `s12_count`, a `String` is a `torb_text` struct, a list is a pointer, a case is a tag and a union; pretty printers per debugger and per platform before a value reads right |
| Both at once | | Twice the work before the first breakpoint |

**Decision: the VM first.** The native half is assessed and planned here, and built later:

- **What the C back end gives already.** `#line` directives point every block, every panic and every intrinsic at the
  TorbScript line (`backend/c/emission.trb`, `lineDirectiveOf`), and a slot is a C variable named after its binding
  (`s12_count`, `slotNameOf`). With `debugInformation = true` in a profile of `project.trb` (`-g`, `/Zi`), gdb and lldb
  already set a breakpoint at `src/main.trb:12` and step by TorbScript lines - block by block, because a block and not
  a statement carries the directive.
- **What the later phase adds.** The statement markers of section 5 become a `#line` per statement in the C back end
  (it writes one already - only a debugged lowering has them); `torb build --debug` compiles with `-O0 -g` and those
  markers; pretty printers for gdb (Python) and lldb (formatters) show `torb_text`, `torb_list`, `torb_map` and the
  program's layouts from a table the emitter writes; and `torb debug` launches `lldb-dap` or `gdb --interpreter=dap`
  for a native program, translating names (`s12_count` to `count`) in `variables`.

## 5. What a debugged program carries: statements and the bindings in sight

A breakpoint names a line, a step goes to the next line, and a frame shows the bindings of its line. The final IR knows
blocks, and a block's location is the construct it came from - an `if`, a loop, a function's entry - not every
statement. The bytecode's `sites` hold the locations of blocks and of the instructions that carry one (a call, a
panic), which is what a stopped script's line comes from (docs/design/SCRIPTS.md section 6) and too coarse for a line
of a debugger: `const point = Point(1, 2)` has no instruction with a location.

| Option | For | Against |
|---|---|---|
| Locations on every instruction | Exact | Every instruction of the IR grows a field; the passes that make instructions have to invent one |
| A side table (block, instruction index) to line | No change to the instructions | The ownership pass and the devirtualization insert and rewrite instructions, and every index moves |
| **An instruction `Statement(at, visible)` at the start of every statement, written only when a program is lowered for debugging** | Passes carry it through as they carry any instruction without operands; the emitter turns it into the line table and a trap point; the C back end can turn it into a `#line` | One more case in every exhaustive `match` over instructions |

**Decision: `Instruction.Statement(at, visible)`, written by a lowering that is asked to (`lowerWorkspace(debugging:
true)`).** `at` is the statement's location. `visible` is every binding of the source that is declared and still in
scope where the statement begins: the lowering knows it exactly, because it keeps the stack of open scopes anyway
(`ir/lower/scope.trb`), and a list per statement answers "which locals does this line show" without a live range or
a liveness question. Parameters and a closure's captures are in sight for the whole body and are not listed. A marker
is written before every statement of a block and before the body of every arm of a `match`; a declaration inside a
block writes none.

The bytecode emitter writes a `statement` instruction for it (`Opcode.Statement`, its tail the index of the statement
in the chunk's table) and one entry in the chunk's statement table: the code offset, the location, and the register,
width and IR slot of every binding in sight. That table is **the line table and the variables table at once**. The
names and types of the slots are the IR's, which the debuggee keeps beside the bytecode: it compiled the program
itself (section 2), so there is no debug format to write and to read back.

## 6. A binding lives to the end of its scope

The ownership pass releases a value after its last use (docs/BACKEND.md section 2), and a move empties the slot it came
from. A debugger that shows a binding for the rest of its block would show a slot whose block was freed - a crash
inside the debugger, not only a wrong value.

| Option | For | Against |
|---|---|---|
| Show a binding only while it is live | Nothing changes in the program | Bindings vanish in the middle of their block; a moved slot still holds a pointer, and a borrowed alias of a released value cannot be told apart |
| Zero a slot where it is released or moved from | The slot says whether it holds something | A borrowed alias of another binding is released by nobody and dangles all the same |
| The ends of scope of `Close` values (`EndOfScope`) for every binding | The machinery exists | It keeps a move a move by design (docs/design/DESTRUCTORS.md 2a), so a binding handed to `append` is still empty afterwards |
| **`KeepAlive(slot)` at the end of every scope, for every binding in it, in a debugged program only** | An ordinary borrowed use: the ownership pass puts the release after it and turns an earlier move into a copy, so a binding holds its value to the end of its block, as in the `-O0` build of any compiled language | A debugged program holds values longer, which only a leak report's peak can tell |

**Decision: `Instruction.KeepAlive(slot)`**, written by a debugged lowering where a scope closes, for every binding of
that scope, the last declared first. Nothing else about the program changes: a `Close` value is released at the end of
its scope already, so no destructor runs at another line; the back ends write nothing for it. Where a scope is left by
`return`, `break` or `?`, nothing is written: the bindings go out of sight there. Together with the `visible` list of
section 5 this is the one guarantee the debugger reads values by: **a binding in sight holds a value it owns, or
borrows from a binding or temporary that outlives it.**

## 7. Breakpoints, and zero cost without a debugger

| Option | For | Against |
|---|---|---|
| A check in front of every instruction | Any instruction can be a breakpoint | A load and a branch per instruction of every program, debugged or not |
| Patched opcodes: a breakpoint overwrites an instruction's head with `break`, which runs the original | Zero cost until a breakpoint is set; the classic design of native debuggers | Stepping still needs a check per line; the image's code is shared by the workers of the pool |
| **A `statement` instruction that only a debugged program has, which asks the debugger** | A program that is not debugged has no such instruction, so it runs exactly the code it ran before; a debugged one pays one dispatch and one call per statement, which steps, breakpoints and pause all share | A breakpoint is a line, not an instruction; a program is compiled for debugging (as `-g` compiles for gdb) |

**Decision: the `statement` instruction, only in a debugged program.** `torb run`, `torb test` and every other host of
the VM lower without markers, and the loop's only change is one more arm of its `match` and a test of
`Registers.debugger` where an `execute` begins (per call back, never per instruction) - section 19 has the measurement.
In a debugged program the arm calls `reachStatement` (`compiler/src/vm/debug.trb`), which notes where the loop is,
counts down to the next look at the channel (section 9), and stops for a pause, for a breakpoint - a set of code
offsets of the image - or for the step in progress (section 8).

A breakpoint at a line binds to the first statement of that line in each chunk that has one; a line without a
statement moves to the next line that has one, in the same file, and the answer to `setBreakpoints` says the line it
bound to (`verified` and `line`). A closure written on the line of its caller is a chunk of its own, so a breakpoint
there stops in both.

## 8. Stepping across frames, closures and tasks

A frame's **height** is how many frames lie below it, counted over every `execute` in progress - the loop of the
program, and the loops a call back started for a test body, an `equals` a map asks for, a task the scheduler resumes,
a destructor. Each `execute` of a debugged run keeps a record (its chunk, code offset, base and depth, written at its
every statement), so the height is known at every statement without walking anything.

- **Step over** (`next`) stops at the next statement of a lower frame, or of the same frame on another line - or on
  the same line again where the loop jumped back.
- **Step in** stops at the next statement anywhere but the rest of the current line: a call's first line, a closure's
  body however it was called (`map`, `filter`, a witness table, the runtime), a test body, a destructor.
- **Step out** stops at the next statement of a lower frame: the caller's next line.
- **The same frame** is the same height, base and return address, so a function that returns and a sibling the caller
  calls next at the same height are told apart.
- **Just my code** (the launch option `justMyCode`, on by default): a step does not stop in a file of the standard
  library - a step into `list.map` stops in the closure the program gave it. A breakpoint stops everywhere, and a
  frame of the standard library is shown `deemphasize`d.
- **Tasks.** A task's body is a chunk like any other; where it suspends, its frame goes into the task block and the
  loop returns to the scheduler (docs/design/VM.md section 4), and where the scheduler resumes it, a call back
  continues it. A step over an `await()` stops at the next statement that runs - of the task itself once it resumes,
  or of whatever else the scheduler runs first.

## 9. Pause

| Option | For | Against |
|---|---|---|
| A signal (`SIGINT`, a console control event) the adapter sends, seen by the `interrupted()` the loop reads every 4096 steps | The check exists (`torb repl`'s Ctrl+C) | No native sends one; on Windows a console control event needs a shared console the adapter does not have |
| **A look at the channel every 1000 statements of a debugged program** | The requests are on the channel anyway (section 3); `DebugPending` is one `PeekNamedPipe` or `poll` | A program blocked in a native call - `readLine`, a sleep, a wait for a task - pauses when it runs its next statement |

**Decision: the look at the channel.** Every thousandth statement the debuggee reads what arrived: `pause` stops at
the next statement with the reason `pause`; `setBreakpoints` and `setExceptionBreakpoints` take effect at once;
`disconnect` and `terminate` end the process. A request that needs a stopped program - `stackTrace`, `variables` - is
answered with an error while it runs, as the protocol allows.

## 10. Frames and locals

The frames of a stop are read out of the registers: the innermost frame of every `execute` at its record, and every
caller of it through the return records below the frames (docs/design/VM.md section 4), innermost first. A caller's
line is the last statement before its return address.

A frame's one scope, **Locals**, lists its parameters, its captures and the bindings of its statement's `visible`
list, in declaration order: the value of each read from its registers - through the reference for a `var` parameter,
whose register holds an address - and shown by its IR type (section 11). A binding the program shadowed shows once,
the innermost. The compiler's own slots - temporaries, the environment of a closure - never show.

## 11. How values are shown

A value in the VM is the runtime's own bytes (docs/design/VM.md section 2), so the debuggee reads words and the IR
type says what they are; `backend/bytecode/words.trb` says where every field lies.

| Type | Shown as | Children |
|---|---|---|
| integers, `Bool`, `Char`, floats | `42`, `true`, `'a'`, `2.5` | - |
| `String` | `"text"`, cut after 1000 characters | - |
| a record, a tuple | `Point(x: 1, y: 2)`, fields shown flat to a depth of one | one per field |
| a case | `Some(3)`, `.Circle(radius: 2.0)`, `None` - the tag word, or the niche's empty first word | the fields of the case |
| `List`, `ArrayList`, `Array` | `[1, 2, 3]` up to ten items, `List(1200)` beyond | one per item, in pages of 100 |
| `Map`, `Set` | `[a: 1, b: 2]`, `Set(3)` | one per entry, the key as the name |
| a `shared type` object | `Door(open: true)` | one per field |
| a trait-typed value | the value it holds, by the target type of its witness table | the value's |
| a closure, a task, a channel, a file | `closure name`, `Task`, `Channel`, `File` | - |

A value with children gets a `variablesReference`, numbered per stop, whose children are read when the editor expands
it (lazily: a list of a million items reads the hundred of the page that is open). An item of a list is found through
the runtime (`DebugContainer`, `torb_list_at`, `torb_map_next`), a narrow item (`List<UInt8>`) through the kernel's
`NarrowLoad`, a text through `TextOut`. The debuggee makes these calls on words of its own, never on the program's
registers, and a null pointer is shown as `<none>` instead of being followed.

## 12. Evaluate and watch

| Option | For | Against |
|---|---|---|
| **A restricted evaluator over the stopped frame: names, fields, tuple fields, indices** | Reads what `variables` reads, so it cannot change the program or run its code; answers hover and watch at once | Not every expression |
| A snippet compiled as a continuation and run by the VM, as an entry of `torb repl` is (docs/design/REPL.md section 5) | Any expression, method calls included | Running code in a stopped program can change it, allocate and panic; a frame's bindings would have to become the snippet's parameters |

**Decision: the restricted evaluator first** (`compiler/src/debugger/evaluate.trb`): `name`, `name.field.field`,
`pair.0`, `list[3]`, `map["key"]`, for hover (`context: hover`), watch and the debug console alike. A name is looked
up in the frame's Locals. The snippet evaluator is section 20's.

## 13. Panics, and `?`

| Option | For | Against |
|---|---|---|
| **Stop before every panic, as the exception filter `Panics` (on by default)** | A panic ends the program, and the moment before it is the one to look at; a failing `assert` is a panic, so a failing test stops at its assertion | A panic a test expects stops too, until the filter is turned off |
| Stop only where no recovery point catches it | Tests that expect a panic run through | A failing test would not stop |
| Stop at a `?` that returns a failure | Where an error starts its way up | Failures are values: a `?` that returns one is ordinary control flow, and stopping at every one would stop in every `tryFrom` of a parser |

**Decision: the exception filter `panic` ("Panics"), on by default.** `runtime/panic.c` calls a debugger's hook
(`torb_set_debug_panic_hook`) first thing in `torb_end_with_panic`, before a recovery point catches the panic and
before it prints; the kernel's hook (`DebugPanics`) calls the interpreter back (`TORB_REQUEST_PANIC`), and the
debuggee stops with the reason `exception` and the message, which `exceptionInfo` answers too. When the person goes
on, the panic goes on - it fails the test, or prints and ends the program with 101. A `?` does not stop; a breakpoint
on its line does.

## 14. Tasks and workers

| Option | For | Against |
|---|---|---|
| Every task a DAP thread | The editor lists what runs concurrently | A suspended task is a frame in a task block of the scheduler; listing them needs a walk of the scheduler's queues the runtime does not offer; stepping one thread while others run needs workers the debugger can hold |
| **One thread; the program runs on one worker while it is debugged** | Deterministic: one statement at a time, a stop stops everything; a task's frames show above the frames of the loop that resumed it | Tasks do not show as threads yet |

**Decision: one DAP thread, `main`; a debugged program runs its tasks on one worker** - `workersFor` gives no other
thread an interpreter, as for a program that runs scripts (docs/design/VM.md section 4). What a task does is
observable exactly as on one worker of a native binary. The list of suspended tasks as threads is section 20's.

## 15. The REPL

`torb repl` and the debugger share the VM and nothing else yet. The REPL's continuations - a program that grows by an
entry that is checked against everything the session holds - are what section 20's snippet evaluator will reuse: the
bindings in sight of a frame as the parameters of a `replEntry`, run in the REPL's sandbox so a panic ends the snippet
and not the program. A `:debug` command of the REPL is not planned.

## 16. Tests and entry files

The launch arguments (DAP `launch`) name what runs, as the command line of `torb run` and `torb test` does:

| Argument | Meaning |
|---|---|
| `program` | A file or a directory, as `torb run <path>` takes it |
| `args` | The program's arguments (`Process.arguments()`) |
| `test` | A test file or a directory of tests, as `torb test <path>` takes it |
| `filter` | One name or a list of full test names (`Group > test`), as `torb test --filter` takes them |
| `cwd` | The working directory of the program; the debuggee changes to it before it compiles (`DebugDirectory`) |
| `env` | Environment variables for the program (`DebugVariable`) |
| `stopOnEntry` | Stop at the first statement |
| `justMyCode` | Steps skip the standard library (default `true`) |
| `noDebug` | Run without markers and without stopping - VS Code's Run Without Debugging |
| `report` | `"json"`: the report of `torb test --report json`, each event as a `torbscript/testReport` event instead of output - what the Test Explorer's Debug profile reads |

`runtime/test.c` reads `--filter` and `--report` off the process's own command line (`torb_process_arguments`), so the
adapter puts them on the debuggee's: `torb debug --debuggee --filter "Group > test"`. `tests/debug/` pins whole
sessions of both kinds (section 18).

## 17. VS Code

- **`contributes.debuggers`**: the type `torbscript`, the launch attributes of section 16 with their schema, initial
  configurations and snippets (a file, the current file, the tests of the current file); `breakpoints` for the
  language `trb`.
- **`debugging.js`**: a `DebugAdapterDescriptorFactory` that starts `torb debug` with the `torb` that `toolchain.js`
  found, and a `DebugConfigurationProvider` that debugs the current file where there is no `launch.json`.
- **The Test Explorer** (`testing.js`) gets a third profile, **Debug** (`TestRunProfileKind.Debug`): it starts a
  session with `test` and `filter` for what was chosen and `report: "json"`, and a `DebugAdapterTracker` maps the
  `torbscript/testReport` events onto the test run as the Run profile maps the report's lines. The Test Explorer and
  the gutter of every `test` and `group` then show Debug beside Run.
- **Entry files**: "Debug File" next to "Run File" in the editor's run menu, and a CodeLens "Run | Debug" above the
  first line of an entry file (`src/main.trb`, a file with top-level code that is no test file).

## 18. Slices

| # | Scope | State |
|---|---|---|
| 1 | `Instruction.Statement` and `Instruction.KeepAlive` in the IR, a debugged lowering (`debugging: true`), the chunk's statement table, `Opcode.Statement` | In progress |
| 2 | The VM's hooks: `Registers.debugger`, the loop records, `reachStatement`, stepping, breakpoints, pause; the panic hook; one worker; measured | In progress |
| 3 | `torb debug`: the adapter and the debuggee, the kernel operations of `runtime/debug.c`, frames, locals, values, evaluate | In progress |
| 4 | VS Code: the debugger contribution, launch snippets, the Debug test profile, Debug File and the CodeLens | In progress |
| 5 | Tests: `tests/debug/` sessions (`tools/debug.sh`, a gate of tier A), `compiler/tests/debugger.test.trb` | In progress |
| 6 | Docs: `docs/tooling/torb-debug.md`, `docs/how-to/set-up-your-editor.md` | In progress |

## 19. What it costs

Measured when slice 2 is in (the numbers follow).

## 20. Open

- **The program's standard input**, through `runInTerminal` and a loopback channel (section 3).
- **Evaluate as a snippet** compiled like a REPL entry, method calls included (section 12).
- **Tasks as threads**, and a worker pool the debugger can hold (section 14).
- **Conditional breakpoints, hit counts and logpoints**: a condition is an expression of section 12.
- **`setVariable`**: a write through the same places `variables` reads.
- **Native programs** (section 4).
