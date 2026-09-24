# The Bytecode VM

**Status: partly implemented** — decided on 2026-09-23 as the start of milestone 7 (`docs/BACKEND.md` section 5 and
rows 7.1-7.2). Slices 1 to 3 are in (section 9): the bytecode and `torb ir --bytecode`
(`compiler/src/backend/bytecode/`), the kernel of `std/machine` with `runtime/machine.c` and the generated table of
thunks, and the interpreter with `torb run --vm` (`compiler/src/vm/`). `tools/conformance.sh --vm` runs the programs of
`tests/conformance/vm.list` - 120 of the 142 of the suite - and compares them byte for byte with their native run.
Tasks, `test`/`group`, keys compared by a program's own `equals` and the leak gate of the VM are still open.

TorbScript has two back ends that read one IR. The C back end turns it into a native binary; the VM turns it into
bytecode and runs that inside `torb`, for `torb run --vm`, the sandbox (7.4), `project.trb` as a script (7.5) and the
REPL (7.6). Design principle 5 is the whole requirement: **nothing observable may differ between the two**. Output,
exit codes, panic messages and sites, the order in which `close()` runs and whether a block leaks are observable, so
all of them are decided once, in the IR, and executed by both back ends as they were decided.

```text
  CheckedProgram ─► lowering ─► devirtualize ─► ownership ─┬─► C source ─► cc ─► native binary
                                  (the final IR)            └─► bytecode ─► VM (TorbScript, inside torb)
                                                                              │
                                                  the same runtime/ ◄─────────┘  natives, text, lists, maps,
                                                  (linked into torb)             counts, panics, the leak counter
```

- **[1. What the VM interprets](#1-what-the-vm-interprets)** — the final IR, nothing earlier
- **[2. Values: typed words in the runtime's own representation](#2-values-typed-words-in-the-runtimes-own-representation)**
- **[3. The bytecode](#3-the-bytecode)** — chunks, instructions, places, the constant pool
- **[4. Frames and calls](#4-frames-and-calls)** — one word stack per task, references as words
- **[5. Generics, witness tables, closures](#5-generics-witness-tables-closures)**
- **[6. How the VM reaches the runtime](#6-how-the-vm-reaches-the-runtime)** — the kernel of natives and the thunk table
- **[7. Releases and destructors](#7-releases-and-destructors)** — identical to the C back end, by construction
- **[8. The gate: C and the VM agree](#8-the-gate-c-and-the-vm-agree)**
- **[9. Slices](#9-slices)**
- **[10. Open](#10-open)**

---

## 1. What the VM interprets

**Decision: bytecode is emitted from the final IR** - the `IrProgram` that `emitProgram` of the C back end reads:
after `lowerWorkspace` (`finishProgram`, `devirtualizeProgram`, `rewriteElementPlaces`, `dropUncountedMakeUnique`,
`dropProvableChecks`) and after `insertOwnership`. The emitter takes the same set of functions the C back end emits
(`emittedFunctionsOf`: the kept functions plus the destructors their releases need).

- **Because every decision is then shared.** Monomorphization, layouts and representation classes, which calls are
  direct after the devirtualization, which overflow checks the range analysis dropped, and every `Retain`, `Release`,
  `Move` and `MakeUnique`: the VM re-derives none of it. A leak or a copy is the same in both back ends because it is
  the same instruction.
- **Because the IR was built for it** (`docs/BACKEND.md` 1.1): blocks over numbered slots are a register machine
  already, so the bytecode is a near 1:1 encoding and not a second lowering.
- **What the C back end refuses, the VM refuses.** A construct the C back end reports as unsupported (a call that
  passes a witness table, a `var` path through a map, a `Contents` step) is reported by the bytecode emitter too, with
  the same kind of finding, and the program does not run. The VM never becomes the more permissive back end. The one
  exception is a receiver script, which is not a program but the one construct defined to be interpreted: its limits
  and its recoverable panic are properties of an interpreter (docs/design/SCRIPTS.md section 1).

The bytecode lives in memory; a file format (`.torbc`, a version and a hash of its inputs) is milestone 8, together
with the IR cache.

## 2. Values: typed words in the runtime's own representation

**Decision: a register is a 64-bit word, and a value is the C runtime's own bytes spread over as many words as it
needs.** There are no tags: the IR gives every slot a type, so the emitter chooses a typed opcode (`add.i64`,
`move` and a `kernel retain` with the shape of the value) and the interpreter never inspects a value to learn what it is.

| IR type | Words | What the words hold |
|---|---|---|
| `Integer`, `Boolean`, `Character`, `VoidType` | 1 | the value, sign- or zero-extended to 64 bits |
| `Floating` | 1 | the bits of the `double` |
| `Text` | 2 | the `torb_text` struct: the storage pointer, then offset and length |
| `Runtime(ListStorage)` | 2 | the `torb_list` struct |
| `Runtime(MapStorage)`, `Runtime(SetStorage)` | 1 | the `torb_map` / `torb_set` struct (one storage pointer) |
| `Runtime(TaskObject/ChannelObject/FileHandle)` | 1 | the pointer to the runtime's counted block |
| `Record`/`Tuple`/`Variant`, `Boxed` | 1 | the pointer to a counted block (below) |
| `Record`/`Tuple`, `Inline` | sum of the fields | the fields, flattened in declaration order |
| `Variant`, `Inline` | common fields + 1 + largest group | the common fields, the tag word, then the group of the variant |
| `Variant`, `Niche` | the payload's words | exactly the payload; `None` is a zero first word, as in C |
| `Closure` | 2 | the function (its index plus one, so zero is the niche) and the environment pointer |
| `Object(bounds)` | 1 + bounds | the payload block, then one witness table (index plus one) per bound |
| `SharedObject` | 1 | the pointer to the object's counted block |

- **Because a value must cross between the VM and the runtime without marshalling.** A `String` the VM holds *is* a
  `torb_text`, a list *is* a `torb_list`: `String.trim`, `ArrayList.append` and `print` are the functions of
  `runtime/` called on the very words in the register file (section 6). There is one list implementation and one text
  implementation in the process, and both back ends use it.
- **Because the counts are then the counts.** A retain is `torb_retain` on the same pointer the C back end retains, a
  release of a string is `torb_text_release`, and the live-block counter of `runtime/memory.c` counts the VM's blocks
  exactly as it counts a native binary's.
- **Not tagged values.** A tagged `Value` of the host language would put a second representation of every type next to
  the runtime's, a conversion at every native call, and counts that the host language manages instead of the IR - the
  moment of a release, and with it the moment of a `close()`, would no longer be the IR's.
- **Inline layouts are the VM's own.** Field offsets and slot sizes are the back end's business (BACKEND section 0):
  there is no `sizeof`, no offset and no reflection in the language, so a record of two `Int32` fields is two words in
  the VM and eight bytes in C, and nobody can tell. The *representation class* - inline, boxed or niche - is the IR's,
  because it decides where a count is.
- **Sub-word values are widened to a word everywhere the VM stores them itself** - a register, a field of a block, and
  the element descriptors the VM creates for its lists (a `List<Bool>` of the VM has elements of eight bytes). A value
  the *runtime* stores keeps the runtime's size (`torb_element_text` is sixteen bytes, two words).

**A counted block of the VM** is `torb_allocate(8 + 8 + 8 * words, kind)`: the `torb_header` of `runtime/torb.h`, one
word naming the block's **shape** (which of its words are counted, and how), then the words of the value. The shape
word is what lets code that did not create the block release it: an element of a list the runtime frees, the payload
of a trait-typed value, a closure's environment. C keeps the same knowledge in a drop function pointer (in the
environment, in the witness table); the VM keeps it in the block because it has no C function per layout.

## 3. The bytecode

**Register based, one `Chunk` per function, one `List<Int64>` of code per chunk.** Each IR instruction becomes one
bytecode instruction (a few become two: a read through a `var` parameter that is also an operand, section 4), and the
blocks disappear into code offsets.

```text
  instruction = head word, then its tail words

  head:   bits  0-7   opcode
          bits  8-23  operand A    a register offset, a count, or a small index
          bits 24-39  operand B
          bits 40-55  operand C
  tail:   one full word per operand that does not fit a head field: a function index, a jump target, an
          argument register, a place index, a location index
```

- **Operands are word offsets into the frame**, not slot numbers: the emitter lays the frame out once (every slot at
  an offset, with its width) and the interpreter adds the frame base. A frame of more than 65535 words, and anything
  else that does not fit sixteen bits, is a finding of the emitter; a `wide` form of the head is the answer when the
  first real program needs it, and nothing has yet.
- **Jump targets are absolute code offsets in a tail word**, because a chunk of more than 65535 words is plausible
  (the compiler's largest functions) and a jump is rare next to the arithmetic around it.
- **A place is compiled once, into the chunk's place table**: the base register, whether the base is a `var`
  parameter (whose word is a reference, section 4), and one step per `PathStep` - `offset k` for a field, a tuple
  field or a variant field (a field of a `Boxed` value is a dereference first, exactly as `->` is in C), and
  `element index missing at` for an element of a contiguous list. `Read`, `Write`, `MakeUnique` and every argument of a
  `var` parameter name a place by its index.
- **The constant pool is per program**: every static the code names, as the words it places into a register - an
  integer, a float's bits, an immortal `torb_text`, the flattened words of an inline aggregate. A constant that is text
  or a boxed aggregate is immortal, as the C back end's static data is, so a retain or a release of it is a no-op in
  both back ends.
- **Witness sources, switch tables, argument lists and captures** are tail words or entries of per-chunk tables, never
  values built at run time.
- **Locations** are an index into the program's location table, which the kernel turns into `torb_location` values once
  when the program is loaded, so a panic prints `at path:line:column` from the same table the C back end's `#line`
  directives come from.

`torb ir --bytecode <path>` prints the disassembly: per chunk its frame (slot, offset, width, type), its places, and one
line per instruction at its code offset. The format is deterministic, which is what the snapshot tests compare.

## 4. Frames and calls

**Decision: one word stack per task (a `List<Int64>` of the interpreter), a frame is a window of it, and a separate
list of return records.** A call pushes a frame of the callee's word count on top of the caller's, copies each argument's
words into the callee's parameter words, and pushes a return record `(chunk, code offset, base, result register)`;
`return` copies the result words into the caller's result register and pops both.

- **A `var` parameter receives one reference word.** The IR passes a place (`Argument.place`), and the VM passes where
  that place is: an **odd** word is a register of the word stack (`index * 2 + 1`, an absolute index, because the place
  belongs to a frame further down), an **even** word is the address of the storage inside a counted block (an interior
  pointer, which `MakeUnique` made safe to write through, exactly as in C). A place through a `var` parameter continues
  from the reference it holds. The word stack may grow and move while a callee runs; an index stays valid, which is why
  a register is referred to by index and never by address.
- **A slot that is a `var` parameter is read and written through its reference.** C writes `*p` wherever the IR names
  such a slot; the emitter writes an explicit `load`/`store` through the parameter's place around an instruction that
  uses one as a plain operand, so every other opcode reads and writes plain frame words.
- **Results by pointer do not exist in the VM.** `ResultMode.ByPointer` is a property of the C calling convention; a
  VM result is words copied into the caller's register, whatever their number.
- **Tasks fit because frames are data.** A suspended task is its word stack plus its return records plus the code
  offset after its `Suspend`, so the VM can stop a task in the middle of a call chain and resume it - no C stack is
  captured. The IR's own state machine (`FunctionKind.TaskResume`, `Suspend(state, awaited)`, `Terminator.Stop`) is
  still what decides the observable part: the cancellation checks at every resume point and loop back-edge, and the
  stop paths that release what `ir/suspension.trb` says is live. Section 9 slice 7 is where this is built.
- **The recursion limit is the VM's own** (a word stack of bounded size), and exceeding it is the same
  `panic: stack overflow` with the site of the function entry that C reports. How deep a program may recurse before it
  panics differs between the two back ends - as it already differs between two machines of the C back end, whose limit
  is the native stack - and nothing else about it does.

## 5. Generics, witness tables, closures

- **Monomorphic by construction.** The IR holds one function per instance, so a chunk is a function instance and a
  call names a chunk by the function's index. Mangled names appear only in the disassembly.
- **A witness table is a list of chunk indices plus the ids of its nested tables.** `CallWitness` resolves its source
  at run time - a static table, or the table word of a trait-typed value, then one `nested` step per supertrait - and
  calls the member. It does what the C back end's witness thunk does: the receiver is the payload out of the value's
  block (its words for a `self` by value, its address for a `var self`), and a parameter the member takes **owned** is
  retained first, because the erased convention borrows every operand.
- **A closure value is the function index and the environment pointer.** A function whose first parameter is an
  `Environment` record receives the environment there; a named function used as a value has none and ignores it - the
  job the C back end's `F_` thunks do. The VM always allocates the environment; the C back end's frame environment of a
  closure that does not escape is an optimization whose only trace is the live-block counter's peak, never its end.
- **Element descriptors are made at load time** from the IR's `ElementDescriptor`: a trivial element gets a descriptor
  with no callbacks and a size of words, a `String` element is `torb_element_text`. A counted element of the program's
  own types needs callbacks the runtime can call into the VM with (section 7).

## 6. How the VM reaches the runtime

The VM is TorbScript compiled into `torb` by the C back end, so it is ordinary machine code linked against `runtime/`.
What it needs beyond TorbScript is a way to put the words of a register in front of a C function.

**Decision: a kernel of six natives, `Machine` of `std/machine`, and one table of thunks generated from the manifest.**

```text
  operate(var words: ArrayList<Int64>, base: Int64, code: ArrayList<Int64>, at: Int64): Int64
      code[at] names an operation of the kernel's table, code[at + 1 ...] are its operands (register offsets from
      base, location indices, sizes). The operation reads and writes the words directly and answers one word.
  load(address: Int64): Int64, store(address: Int64, value: Int64)
      one word of a counted block: a field the interpreter reads or writes through a place
  placeText(var words: ArrayList<Int64>, at: Int64, text: String)
      an immortal copy of the text, as two words at the register at: how the constant pool gets its strings
  placeFloat(var words: ArrayList<Int64>, at: Int64, value: Float64)
      the bits of the float at the register at: how the constant pool gets its floats
  install(interpreter: (Int64) => Int64)
      the closure the kernel calls back into the interpreter with, where the runtime has to run code of the program:
      a test body behind its recovery point, an equals or a hash a map asks for, a task the scheduler resumes
```

- **The table** (`runtime/machine_natives.c`, written by `torb natives --header` next to `torb_natives.h`) has one
  thunk per `.Ready` `.Runtime` row of the manifest, in the manifest's sorted order, generated from the row's C
  prototype: each argument is read from the register the operand names (a struct by `memcpy`, an integer by a cast, a
  pointer parameter from a reference word or as the address of the register), the runtime function is called, and
  the result is written back. A function the thunk cannot be written for - `test` and `group`, which take a
  `torb_closure` the runtime would call back - gets a thunk that panics with an internal error, so the numbering stays
  the manifest's. `runtime/machine.c` adds the operations only a VM needs: allocation of a counted block with its
  shape word, the shapes and the counts of section 7, float arithmetic and conversions, checked arithmetic of every
  width with the runtime's own panic functions, the tables of locations and element descriptors, the program's
  arguments (`Process.arguments()` answers them, not `torb`'s), the immortal region a module constant is built in, and
  the sandbox of a receiver script: opening and closing it, the stop it recorded, and a text copied out as code points
  (docs/design/SCRIPTS.md sections 4 to 6).
- **One native, many operations, is deliberate.** A new native is two commits and a refreshed seed (CONTRIBUTING, "Two
  commits for a breaking change"). A new *operation* is a new row of a C table and a new constant of the emitter, in
  one commit, because the manifest does not change. The kernel natives are the only natives the VM will ever need.
- **Why the generated table and not a VM-side adapter per native.** "Every `native` declaration has to be rebuilt by
  every back end" (CONTRIBUTING) is exactly what a table generated from the manifest's prototypes avoids: a native
  added to `runtime/` reaches the VM when the header is regenerated, and `compiler/tests` fails when the file on disk is
  not what the manifest generates.
- **What stays TorbScript.** The dispatch loop, the frames, `Int64` arithmetic with its overflow checks (a check that
  fails calls the kernel for the panic, so the message and the site are the runtime's), comparisons, jumps and the
  moving of words. A kernel call costs one C call; a list read in the loop costs a bounds check. Both are the C back
  end's to make cheaper later (an intrinsic that loads a word inline), and neither changes a result.

`std/machine` is not for programs. Its operations trust their operands the way machine code does - a wrong register
offset is a wrong write - and a sandboxed script cannot import it, because a sandbox's capabilities are its import list
(BACKEND 5.4).

## 7. Releases and destructors

**Decision: the kernel retains and releases, in C, over a mirror of the shapes; the interpreter runs the destructors.**
Revised from the first plan (a walk in TorbScript), because the runtime itself has to release the VM's values too - an
element of a list, a map value it replaces - and two walkers would be two orders.

- **The shapes are defined once, when a program is loaded** (`DefineShape`), and `retain`/`release` are one kernel call
  each: the counted words of the value in the order the C back end's helpers visit them - the common fields, then the
  group of the variant the tag names, or all of it reversed where `emitDropHelper` reverses it. A counted block of the
  VM carries its shape word, so a block is released the same way whoever releases it.
- **A counted element of the program's own types** gets a descriptor whose callbacks walk its shape. The runtime hands a
  callback the element and nothing else, so the callbacks come from a pool of 64 slots in `runtime/machine.c`, one per
  counted element type; a trivial key of several words is compared and hashed by its words in the same way, which is
  exact for integers, `Bool`s and `Char`s (the bytecode says which keys qualify, `comparesByWords`). A key the program
  compares with an `equals` of its own - a `String` field, a float - needs a callback into the interpreter and is
  refused until then.
- **`close()` runs where C runs it.** A release that drops the last count of a block whose shape has a destructor
  queues the block instead of freeing it. After every kernel call that answered a queued count, the interpreter takes
  the batch out, and for each block lends it a count (`torb_closing_begin`), runs its `close()` as one more `execute`
  on top of the words in use, takes the count back (`torb_closing_end`), and releases its fields - and what that
  release queues runs before the next block of the batch. That is the depth-first order of nested C drop functions,
  and nothing but a destructor can tell that it ran after the kernel call instead of inside it, because the runtime
  writes no output of its own.
- A panic runs nothing on the way out, in the VM as in C (gap 9): the kernel calls the runtime's panic function, which
  prints and leaves with 101.

## 8. The gate: C and the VM agree

**Decision: `tools/conformance.sh --vm` runs every program of `tests/conformance/vm.list` with `torb run --vm` and
compares standard output, standard error (folded as for the native run) and the exit code with the same
`.expected`/`.stderr`/`.exit` files the native run is compared with, byte for byte.**

- The list grows with every slice until it is every program of the suite; then the list goes away and 7.2's gate - C
  and the VM agree on every script - is the whole suite run twice.
- **Not in the list, and why.** The sixteen programs with tasks (slice 7), the three that run `test`/`group` (a
  closure the runtime calls back), the two whose map keys a program's own `equals` compares (`capsule`, `paths`), and
  `process-executable-path`, whose executable is `torb` in the VM - a difference that belongs to running inside the
  toolchain and that the sandbox of 7.4 will answer for itself. `stack-overflow` is in: its recursion never ends, so
  the VM's limit (section 4) is reached as surely as the native stack's, at the same site.
- **The leak gate for the VM** is still open: the VM runs inside `torb`, so "live blocks at exit" counts the compiler's
  own blocks as well. The kernel already answers the live count (`LiveBlocks`); the gate is the count after the program
  against the count before it, which needs a `Process.exit` inside the VM to report the difference instead of the
  process's.
- **What it costs, measured.** `fibonacci(30)` (about 2.7 million calls): 2.0 s of interpretation in a `torb` built
  with the `dev` profile, against 0.035 s natively. The front end before it is the same for both commands and takes
  most of a small program's run. Register reads through `Indexed.at`, a kernel call per count and a list of return
  records are where the time goes; section 10 has the next steps.

## 9. Slices

| # | Scope | State |
|---|---|---|
| 1 | The bytecode format, the frame layout, the emitter from the final IR, the disassembler, `torb ir --bytecode`, snapshots | **Done**: `compiler/src/backend/bytecode/`, `compiler/tests/bytecode.test.trb` |
| 2 | The kernel: `std/machine` (five natives, a sixth for the call back of section 10), the manifest rows, `runtime/machine.c`, the thunk table generated by `torb natives --header` | **Done**, as the first of two commits: the seed is refreshed from it before the interpreter can call the natives |
| 3 | The interpreter loop: calls, closures, witness calls, records, variants, text, lists, maps and sets, module constants, destructors; `torb run --vm`; `tools/conformance.sh --vm` with `vm.list` | **Done**: `compiler/src/vm/`, 120 programs of the suite |
| 4 | Keys compared by a program's own `equals`: a callback from the runtime into the interpreter | Open |
| 5 | The leak gate of the VM: the live count after the program against the count before it | Open |
| 6 | `test` and `group`: the runtime's recovery point around a closure of the interpreter | Open, needs the callback of 4 |
| 7 | Tasks: `TaskNew`, `Suspend`, `Stop`, channels, the FIFO order of `runtime/task.c` (docs/BACKEND.md 7.3's VM half) | Open |
| 8 | The gate of 7.2: every conformance program in both back ends; `vm.list` deleted | After 7 |

## 10. Open

- **A call from the runtime into the interpreter.** Slices 4, 6 and 7 need the same thing: a C function the runtime calls
  (an element's `equals`, a test body, a task's resume) that runs a chunk. The interpreter is TorbScript compiled into
  `torb`, so the entry has to be a function of the program the runtime can hold - a closure handed to the kernel once,
  or a queue the interpreter drains as it drains the destructors. The queue works for tasks, whose scheduler decides
  when a task runs anyway; an `equals` needs its answer at once and so needs the closure.
- **Where the tasks' scheduler runs.** Either the runtime's own FIFO scheduler drives a VM task through one generic
  resume function of `runtime/machine.c` that hands control back to the interpreter, or the VM carries the same
  algorithm in TorbScript (BACKEND 5.3 names both). The first keeps one scheduler in the process and is preferred.
- **Calling compiled code from bytecode.** BACKEND 5.2 promised that a compiled function can be called from bytecode
  and back. With the VM's own inline layouts that is a marshalling step at the boundary for any record of the program.
  docs/design/SCRIPTS.md section 5 decided it for the sandbox: only text crosses between `torb` and the VM (the kernel
  operation `TextOut`), and a native binary that loads scripts will cross with `std/encoding`.
- **Speed.** A register read is `Indexed.at` (a runtime call and an `Option`); a direct read of the list's storage, an
  intrinsic for a word of a block, and return records kept in the word stack instead of a list of records are the first
  three steps, each measured against `fibonacci(30)` above. The first two are new natives, so two commits each.

