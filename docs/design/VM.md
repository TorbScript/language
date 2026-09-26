# The Bytecode VM

**Status: implemented** — decided on 2026-09-23 as the start of milestone 7 (`docs/BACKEND.md` section 5 and
rows 7.1-7.2); every slice is in (section 9): the bytecode and `torb ir --bytecode` (`compiler/src/backend/bytecode/`),
the kernel of `std/machine` with `runtime/machine.c` and the generated table of thunks, and the interpreter
(`compiler/src/vm/`). `tools/conformance.sh --vm` runs every program of the suite, `binary-only/` included, and the
programs of `vm-only/`, and compares them byte for byte with their native run - a program with a `.workers` file on
that many workers and on one. `test`/`group`, keys compared by a program's own `equals` and tasks run through the
call back of section 10, tasks run on every worker of the pool (section 4), `Array<Item, Size>` is inline words, the
leak gate holds the VM to "live blocks at exit: 0" as it holds a native binary, `torb test` runs a test suite with
the native binary's report, and `fibonacci(30)` interprets in about 0.2 s (section 8). **`torb run` and `torb test`
run in the VM by default** and `--native` builds a binary instead; `torb build` is always native. The compiler's own
test suite runs natively in the gates (`torb test --native compiler/tests`): it checks and lowers whole programs
thousands of times, which the VM interprets many times slower than the binary does, and everything else runs in both. A program the VM runs
loads a receiver script from a path only known while it runs, which the running `torb` checks and lowers into it as a
continuation (`docs/design/SCRIPTS.md` slice 7), and `torb build --embed-vm` builds a native binary that embeds the VM
and runs the program's bytecode (section 11).

TorbScript has two back ends that read one IR. The C back end turns it into a native binary; the VM turns it into
bytecode and runs that inside `torb`, for `torb run` and `torb test`, the sandbox (7.4), `project.trb` as a script
(7.5) and the REPL (7.6). Design principle 5 is the whole requirement: **nothing observable may differ between the two**. Output,
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
- **[11. A native binary that embeds the VM](#11-a-native-binary-that-embeds-the-vm)** — `torb build --embed-vm`

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
| `FixedArray` (`Array<Item, Size>`) | `Size` times the item's | the items, one after the other; an item step checks the index against `Size` |
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
- **Sub-word values are widened to a word everywhere the VM stores them itself** - a register, a field of a block. A
  value the *runtime* stores keeps the runtime's size (`torb_element_text` is sixteen bytes, two words).
- **An element of a container is stored in the C back end's width - decided, since the network: an integer narrower
  than a word, a `Bool` and a `Char` are a byte, two or four in a list, a map, a set, a task's value or a channel.**
  A function of the runtime reads the bytes of a `List<UInt8>` (`torb_network_send` refuses any other element
  size), so a list the VM makes has to be the list the runtime expects. The element descriptor is a narrow one of the
  kernel's (`ElementKind.Narrow`, equal by its bytes, hashed as the language hashes the number, `torb_hash_u64` of the
  widened word - `element-hashes` prints the hashes of such containers in both back ends); the conversions are where bytes move
  between a register and the storage: a place that ends in such an element loads and stores through the kernel
  (`NarrowLoad`, `NarrowStore`) in the element's width, sign- or zero-extending; a value handed to the runtime by
  address is its word, whose low bytes are the narrow value; an element the runtime writes through a `void *` lands in
  a zeroed word and a signed one is widened (`Widen`); a task's value goes into its slot in its own width; and a `var`
  argument that is such an element is read into a word for the call and written back after it, which exclusive
  access makes the same as a reference into the storage - a reference always points at words. A `Float32` element
  stays a word: no function of the runtime reads one out of a list, and its `equals` is the program's.

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

**Decision: one word stack (a `List<Int64>` of the interpreter, reserved at its largest size before the first
instruction, so it never moves), a frame is a window of it, and a call's return record lies in the stack too.** A call
pushes a frame of the callee's word count on top of the caller's, copies each argument's words into the callee's
parameter words, and writes the return record `(code offset, base, chunk, result register)` into the four words right
below the callee's frame - the upper half of the caller's scratch words, which nothing uses while the callee runs;
`return` copies the result words into the caller's result register and goes on where the record says. A loop knows how
many calls it made, so its bottom frame's `return` ends it.

- **A `var` parameter receives one reference word: an address.** The IR passes a place (`Argument.place`), and the VM
  passes where that place is - a register of the word stack, whose address stays valid because the stack never moves,
  or storage inside a counted block (an interior pointer, which `MakeUnique` made safe to write through, exactly as in
  C). A place through a `var` parameter continues from the address it holds. (The kernel still reads an odd word as
  the register `index * 2 + 1`, which is what the interpreter passed before its registers stopped moving.)
- **The loop reads everything by address.** The chunks are laid out once as an image (`Image` of
  `compiler/src/vm/interpret.trb`): every chunk's code in one list with its jump targets, resume points and stop paths
  relocated, a header of nine words per chunk (where its code starts, its frame, its parameters, places and witness
  entries, its location, its task register), four words per parameter, and the witness tables as words. The kernel
  answers the address of each list once (`ListAddress`, `RegisterAddress`), and from then on the loop reads a head
  word, a register or a header with one `Machine.load` and writes with one `Machine.store` - both `static inline` in
  `runtime/include/torb.h`, so neither is a call.
- **A slot that is a `var` parameter is read and written through its reference.** C writes `*p` wherever the IR names
  such a slot; the emitter writes an explicit `load`/`store` through the parameter's place around an instruction that
  uses one as a plain operand, so every other opcode reads and writes plain frame words.
- **Results by pointer do not exist in the VM.** `ResultMode.ByPointer` is a property of the C calling convention; a
  VM result is words copied into the caller's register, whatever their number.
- **Tasks fit because frames are data - decided: the runtime's own scheduler, and a frame saved into the task block.**
  The IR's state machine (`FunctionKind.TaskResume`, `Suspend(state, awaited, observes)`, `Terminator.Stop`) decides
  everything observable, and the VM encodes it as the C back end does: a task's body keeps its task in one more register after
  its slots; `cancelled` is the check at the entry and at every loop back-edge, `suspend` records the state and waits -
  `torb_task_await` for an `await()`, which takes over a cancellation of the awaited task, `torb_task_observe` for a
  `result()`, which only observes it (docs/design/CONCURRENCY.md sections 8 and 16) -
  `resume` - where the body goes on, at once or later - is the check and the outcome read once, `finish` moves the
  value into the task block, and every stop path releases what `ir/suspension.trb` says is live and ends in `stop`.
  `TaskNew` makes the task over one resume function of `runtime/machine.c` with room for the chunk's number and the
  frame's words, moves the arguments into the frame and starts it pinned to the running worker. When the scheduler
  resumes it, the kernel calls the interpreter back (section 10): the frame is copied out of the task block above
  everything in use and the body goes on at the offset of its state; where it suspends again, the frame goes back
  into the block and the body answers `TORB_POLL_SUSPENDED`. Only the bottom frame of a body suspends, because the
  IR makes every `await` one of the body itself. The entry file of a program that waits is the main task
  (`RunMain`), whose `torb_task_end_main` makes the run end with `TORB_EXIT_CANCELLED` where it ended cancelled, one
  that starts tasks and does not wait hands the scheduler the rest once it is done, and `torb_scheduler_finish`
  cancels what still runs - `main`'s order in the C back end. The entry cells of a top-level `const` (`entry.cell`, a
  gap of the constant pool per cell and per flag) are released after all of it by one chunk, which skips a cell its
  flag says was never set; a program that ends in `Process.exit` never gets there, so the run hands the kernel that
  chunk first (`OnExit`) and `torb_process_on_exit` runs it through a call back, as the native `main` hands it
  `entry_cells_release`.
- **Tasks run on every worker of the pool - decided: registers and an interpreter per thread, the program shared by
  address.** Every thread that may resume a task - the workers of `torb`'s own pool after the first, and the threads
  of its blocking pool - gets `Registers` of its own before the first instruction (`workersFor` of
  `compiler/src/vm/interpret.trb`): its own words for its frames, a stack of a million words, and an image that has
  the program's addresses and none of its lists, installed as that thread's interpreter (`InstallFor`, then
  `Machine.install`). What the threads share - the constant pool with the cells in its gaps, the code, the headers,
  the places and witness entries, which are words of the image for this reason - they only read by address, so no
  count of the interpreter's is ever touched by two threads, which is the memory model's one rule (docs/design/
  CONCURRENCY.md section 2). The kernel keeps one interpreter and one queue of destructors per thread. A task is
  started portable where the C back end's test says its frame may cross: `TaskNew` carries the runtime's tests of
  the frame's texts, lists, maps and closures, written from the IR types as `crossing.trb` writes them, and a type
  that never crosses pins it. **The copy at the crossing** is the C back end's too: where `privateOf` says every
  value of the frame may be copied soundly, `TaskNew` carries each counted value's register and shape, and while
  the pool has more than one worker the kernel makes them private by walking the shape - a text by
  `torb_text_privatize`, a list, a map or a set by the runtime's with what makes one element private (nothing for a
  plain one, the runtime's for a text, the walk of the slot's shape for a record of the program), a task or a channel
  of plain values and a shared environment as they are - and starts the task portable. **A closure's environment is
  shared** (`Share`, `torb_share`) where every capture may cross without a transfer, as the C back end shares it, so
  a closure in a task's frame no longer pins it; the kernel releases a shared block with the runtime's atomic count. A
  module
  constant is built under the runtime's lock of constants and published with a release, as the C accessor does
  (`ConstantLock`, `ConstantUnlock`). A thread of the pool that runs a task of the program counts what its scheduler
  frees as the program's, so the leak gate stays exact whichever worker ran what. A program that runs scripts keeps
  its tasks on one thread, because the sandbox is the whole process's.
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
  the result is written back. The word an out parameter (`void *out`) points at is zeroed first, because an element
  the runtime describes itself may be narrower than a word - the flag a deadline task answers. `test` and `group` take a closure the runtime calls, and a closure of the program is a
  function of the bytecode: their thunks call substitutes of `runtime/machine.c` instead, which hand `runtime/test.c` a
  C closure that calls back into the interpreter (section 10), so a failing test body lands on the runtime's own
  recovery point. A function no thunk can be written for - `Machine.install` itself - gets one that panics with an
  internal error, so the numbering stays the manifest's. `runtime/machine.c` adds the operations only a VM needs: allocation of a counted block with its
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
  moving of words. A kernel call costs one C call; a word the loop reads or writes costs a load or a store, because
  `Machine.load` and `Machine.store` are the one pair of natives the runtime defines `static inline` in `torb.h`: the
  prototype `torb_natives.h` writes after it takes its internal linkage (C11 6.2.2), so the manifest and the header stay
  what they were and no seed has to learn anything.

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
  compares with an `equals` of its own - a `String` field, a float - gets a slot as well, whose `equals` and `hash`
  call back into the interpreter (section 10) with the element's address: the chunks of the program's `equals` and
  `hash` run on the words of the element, borrowed, exactly as the C back end's element helpers call them.
- **`close()` runs where C runs it.** A release that drops the last count of a block whose shape has a destructor
  queues the block instead of freeing it. After every kernel call that answered a queued count, the interpreter takes
  the batch out, and for each block lends it a count (`torb_closing_begin`), runs its `close()` as one more `execute`
  on top of the words in use, takes the count back (`torb_closing_end`), and releases its fields - and what that
  release queues runs before the next block of the batch. That is the depth-first order of nested C drop functions,
  and nothing but a destructor can tell that it ran after the kernel call instead of inside it, because the runtime
  writes no output of its own. **A release outside every operation of the loop closes at once:** the scheduler of a
  worker that frees the value of a task nobody waits for, or the scheduler an operation runs (`RunMain`,
  `SchedulerRun`, the wait of a test), releases where no loop will look at the queue before the program goes on, so
  the kernel counts per thread how many operations it is inside and, at zero, runs the queue there through a call back
  (`TORB_REQUEST_CLOSE`) - where the C back end's drop function would run (`destructor-unawaited-result`).
- A panic runs nothing on the way out, in the VM as in C (gap 9): the kernel calls the runtime's panic function, which
  prints and leaves with 101.

## 8. The gate: C and the VM agree

**Decision: `tools/conformance.sh --vm` runs every program of the suite with `torb run --vm` and compares standard
output, standard error (folded as for the native run) and the exit code with the same `.expected`/`.stderr`/`.exit`
files the native run is compared with, byte for byte.** 7.2's gate - C and the VM agree on every program - is the whole
suite run twice, and the list of what the VM runs that grew with every slice is gone.

- **The two that answered differently, and how they agree.** `Process.executablePath()` inside the VM answers the path
  of the entry file, which is what executes, as an interpreter answers the path of its script (`SetExecutable`, the
  substitute `torb_machine_process_executable_path`); `process-executable-path` holds both answers to the same
  checks. **`TORB_MEMORY_LIMIT` is the program's**: a binary that hosts the VM (`TORB_HOSTS_MACHINE`, which the driver
  defines for every program that calls the kernel - `torb`, the compiler's tests) counts the blocks the interpreted
  program allocates inside the kernel against the variable, or against the dev default where it is not set, because
  `torb run` means the dev profile, and ends the program with the native binary's message and exit code 102; the host
  itself keeps the default of its own profile and compiles unlimited by the variable (`memory-limit`). Every
  program of the network is in, since the elements of a list are stored narrow (section 2), and so are `tls-loopback`
  and `https-exchange`: the driver links the TLS part of the runtime (`runtime/tls/`, mbedTLS) into every program
  that calls the VM's kernel - `torb` itself - because the table of thunks reaches every function of the runtime.
  `narrow-elements` holds every narrow width to it, the negative ones and a `var` argument that is an element too.
- **Workers.** A program with a `.workers` file runs on that many workers of `torb`'s pool (`TORB_WORKERS`) and again
  on one, and has to print the same bytes both times, as its native binary does; every other program runs on one.
  `task-workers-callbacks` holds the per-thread parts to it: a map whose key is compared by the program's `equals`
  and objects with a destructor, in tasks that idle workers take. Eight tasks of a million steps each take 7.9 s of
  interpretation on one worker, 2.4 s on four and 1.6 s on eight. `target-branch` is in: the table of thunks has a thunk for every native of `runtime/os/`, which
  calls it where its file is compiled and panics elsewhere, and the lowering keeps only the branch of the machine it
  runs on. So is `destructor-slice-concrete`: a slice of a list whose items may hold a `Close` object copies them,
  through the kernel's `ListSliceCopied`, where the C back end calls `torb_list_slice_copied`. `stack-overflow` is in:
  its recursion never ends, so the VM's limit (section 4) is reached as surely as the native stack's, at the same site.
- **The leak gate for the VM - decided: the kernel counts the program's blocks apart.** The VM runs inside `torb`, so
  the heap's own "live blocks at exit" counts the compiler's blocks as well, and a count after the program against one
  before it would count the interpreter's own allocations of the run too. Every block of the program is made and freed
  inside a call of the kernel, though, and nothing of the interpreter is: `torb_machine_operate` sets the thread's heap
  counting (`torb_count_in_machine`), a call back clears it for as long as the interpreter runs, and the heap counts
  what it allocates and frees while it is set once more, apart. Once a VM counted a program, `torb_report_leaks`
  reports those two counts - at the end of `torb run --vm` and at a `Process.exit` inside the program alike - so
  `TORB_REPORT_LEAKS=1` says of a run of the VM what it says of the native binary, and `tools/conformance.sh --vm`
  holds every program to "live blocks at exit: 0" with the same exemptions (`.stderr`, `.leaks`, `binary-only/`). The
  registers themselves (`Reserve`) are the interpreter's and are counted as `torb`'s.
- **What it costs, measured.** `fibonacci(30)` (about 2.7 million calls), the wall time of `torb run --vm` minus that of
  a program that prints one line (the front end, the same for both) in a `torb` built with the `release` profile, on
  one machine, the best of five runs:

  | | `fibonacci(30)` | `fibonacci(32)` |
  |---|---|---|
  | native binary | 0.03 s | - |
  | VM, registers through `Indexed.at`, a `List` of return records, a `Chunk` copied per call | 1.18 s | 3.31 s |
  | VM, registers and code by address, return records in the stack, the image (section 4) | 0.19 s | 0.46 s |

  The first number was 2.0 s in a `dev` build. Six times faster is what the reads alone buy; the rest - about seventy
  nanoseconds a call - is the loop's own checked arithmetic on addresses, the header words a call reads, and one C call
  per count, which section 10 lists.
- **The start, measured** (`torb run --timings`). Once the VM was the default, the front end was the whole wait for a
  short program: 1.7 s for one that prints one line, of which 0.7 s parsed every file of the workspace, 0.7 s checked
  every body of every package of the program, and 0.6 s parsed everything a second time for a script the program might
  load. Three changes took it to about 0.25 s (0.23 s until the first instruction, 0.26 s wall time):
  - **Only what the program reaches is read and parsed.** `readSourceTree` lists the files and reads a file the first
    time its text is asked for (`SourceTree.onDisk`), and the graph of a run holds the packages of the program alone -
    the files below the entry and of its package, of every package one of them imports, depends on or names as its
    prelude (`reachedFilesOf` of `semantics/graph.trb`), which is the closed world `programPackagesOf` names anyway.
  - **Bodies are checked on demand.** The checker checks the bodies of the entry; the lowering checks the bodies of a
    module the first time it enters it (`enterModule`, `checkBodiesOf`, [Checker.checksOnDemand]). A program that
    prints one line checks the bodies of one module of `std`. Where the lowering refuses the program, every body of
    the program is checked and it is lowered again, so the refusal is the one a native build gives.
  - **The loader of scripts parses the program's files at the first script it loads**, and not before the run.

  What is left is parsing the packages the prelude reaches (about 0.15 s) and starting the process. `torb build` and
  `torb check` keep reading, parsing and checking the whole program as before.

## 9. Slices

| # | Scope | State |
|---|---|---|
| 1 | The bytecode format, the frame layout, the emitter from the final IR, the disassembler, `torb ir --bytecode`, snapshots | **Done**: `compiler/src/backend/bytecode/`, `compiler/tests/bytecode.test.trb` |
| 2 | The kernel: `std/machine` (five natives, a sixth for the call back of section 10), the manifest rows, `runtime/machine.c`, the thunk table generated by `torb natives --header` | **Done**, as the first of two commits: the seed is refreshed from it before the interpreter can call the natives |
| 3 | The interpreter loop: calls, closures, witness calls, records, variants, text, lists, maps and sets, module constants, destructors; `torb run --vm`; `tools/conformance.sh --vm` | **Done**: `compiler/src/vm/`, 120 programs of the suite then |
| 4 | Keys compared by a program's own `equals`: a callback from the runtime into the interpreter | **Done**: the slots of the element pool call the program's `equals` and `hash` back; `capsule`, `paths` |
| 5 | The leak gate of the VM: the program's blocks counted apart from `torb`'s | **Done**: section 8; `tools/conformance.sh --vm` checks every program |
| 6 | `test` and `group`: the runtime's recovery point around a closure of the interpreter | **Done**: `Machine.install` and the substitutes `torb_machine_test_case`/`_group`; `tests`, `test-failure`, `assert-values` |
| 7 | Tasks: `TaskNew`, `Suspend`, `Stop`, channels, the FIFO order of `runtime/task.c` (docs/BACKEND.md 7.3's VM half) | **Done**, on every worker of the pool: section 4; the entry cells of a top-level `const` too; every program with tasks |
| 8 | The gate of 7.2: every conformance program in both back ends; `vm.list` deleted | **Done**: every program, `binary-only/` and `vm-only/` too (section 8); `vm.list` is deleted |
| 11 | `torb test` in the VM: every test file an entry of one program, the report of `runtime/test.c` | **Done**: `TestFile`, `TestFinish`; the default of `torb test`, and tier A runs the test packages of `std/` and `examples/` both ways |
| 9 | `Array<Item, Size>`, added to the language after the interpreter: inline items, a checked item step | **Done**: `ArrayNew`, the static `Items`, `PlaceStep.Item`, the counted words of every item; the eleven programs with an array |
| 10 | Speed: register reads without `Indexed.at`, return records in the word stack (section 10) | **Done**: the image, addresses and inline `load`/`store` (section 4); six times faster (section 8) |
| 12 | A native binary that embeds the VM: `torb build --embed-vm` (section 11) | **Done**: `cli/embed.trb`, `vm/embed.trb`; tier B runs `tests/language/` in such binaries |

## 10. Open

- **A call from the runtime into the interpreter - decided: one closure, `Machine.install`.** Slices 4, 6 and 7 need the
  same thing: a C function the runtime calls (an element's `equals`, a test body, a task's resume) that runs a chunk.
  The interpreter hands the kernel one closure when a run starts, `(Int64) => Int64`, and the kernel calls it with the
  address of a request (`torb_machine_request` of `runtime/machine.c`: a kind, a chunk, two operands, a width) and gets
  the request's one word back. The interpreter lays the frame of a request out above everything the interrupted loop
  uses - the loop writes that height into `Registers.top` in front of every kernel call - and runs it as one more
  `execute`. Two rules make this safe: the registers are reserved at their largest size before the first instruction
  (`Reserve`), so a call back never moves the words a kernel call holds a pointer into, and a call back uses a kernel
  scratch of its own. A queue the interpreter drains would have done for tasks, but an `equals` needs its answer at
  once, and one mechanism serves all three. A call back runs with an unlimited budget of its own, and a sandboxed
  script that stops inside one (a panic in an `equals`) is passed on: the interpreter says so (`StopInCallBack`), and
  `torb_machine_call_back` panics again with the stop the inner operation recorded, which the recovery point of the
  operation that made the call takes as it is - so the loop that made that call unwinds the script as for any other
  stop. **A stop inside a task the scheduler resumed, or inside a test body, jumps through nothing** - a jump would
  leave the scheduler in the middle of a run: the task answers `TORB_POLL_STOPPED`, the test body returns, and the
  stop waits until the operation that ran the scheduler returns, which then stops the script with it. **A script's
  tasks belong to it**: each opening of a sandbox is a generation, a task remembers the one it was started in, and it
  runs only while that sandbox is open - one the script left behind, or one of a script that stopped, stops where it
  is without running again, so a script's code never runs outside its grant, in the program or in another script's
  test (`vm-only/sandbox-tasks.trb`). What such a frame holds is not given back, as nothing is when a script stops. A host that loads a program once (`LoadedProgram`: a session of
  `torb repl`, the scripts of a manifest) keeps its registers and its image in the one `Registers` the call back
  reads, and lays the image out again when a continuation adds chunks.
- **Where the tasks' scheduler runs - decided: in the runtime.** The runtime's own FIFO scheduler drives a VM task
  through one resume function of `runtime/machine.c` that calls the interpreter back (section 4), so the process has
  one scheduler and the order of `runtime/task.c` is the VM's by construction, on every worker of the pool (section 4).
  A task of the program that the scheduler resumes while a script's sandbox is open - the script's test waits for its
  own tasks and runs whatever is queued - runs with the sandbox set aside and no recovery point of the script's, so it
  reads what the program may and a panic of it is the program's (`vm-only/sandbox-tasks.trb`).
- **Calling compiled code from bytecode.** BACKEND 5.2 promised that a compiled function can be called from bytecode
  and back. With the VM's own inline layouts that is a marshalling step at the boundary for any record of the program.
  docs/design/SCRIPTS.md section 5 decided it for the sandbox: only text crosses between `torb` and the VM (the kernel
  operation `TextOut`), and a native binary that loads scripts will cross with `std/encoding`.
- **Speed, the next steps.** The three first steps are in (section 4): registers and code read by address through an
  inline `Machine.load`, return records in the word stack, and the image instead of a `Chunk` copied per call - no new
  native was needed, because a native the runtime defines `static inline` is inlined wherever `torb.h` is included.
  What is left, each measured against `fibonacci(30)` in section 8: the loop's addresses are computed with checked
  arithmetic, which the range analysis could prove safe once a frame's size bounds its registers; a call reads five
  header words where a per-chunk record in the image could be one; a retain or a release is a C call of the kernel even
  for a value whose shape says it is one pointer, which could be one instruction of its own; and a `Place` is a record
  of `List<PlaceStep>` read per `load`, where the steps could be words of the image as well.

## 11. A native binary that embeds the VM

**Decision: `torb build --embed-vm` builds the interpreter natively with the program's bytecode inside it.** The
program is checked, lowered for the VM and encoded as bytecode exactly as `torb run` does; the bytecode is written as
text (`vm/embed.trb`, `programText`) into a module that exists only in memory, beside the interpreter in the
toolchain's own sources (`compiler/src/vm/embedded-program.trb`), and that module - whose top-level code decodes the
program and runs it (`runEmbedded`) - is what `torb build` compiles through C with the interpreter and the runtime.

- **What it is for.** A program that loads receiver scripts ships as one executable: the scripts it was compiled with
  run in their sandbox, with the steps, the time, the memory and the recoverable panic of section 1 of
  `docs/design/SCRIPTS.md`, and the binary needs neither `torb` nor a C compiler where it runs. Every other program
  runs as it runs under `torb run` - the same output, exit code, panics, leak report and memory limit
  (`TORB_MEMORY_LIMIT` limits the program's own blocks, as in `torb`) - which tier B checks with the programs of
  `tests/language/`.
- **What it is not.** The whole program is interpreted: this is no native code that calls into a VM for its scripts,
  and nothing crosses between a native and an interpreted value. That host - compiled code that loads a script, the
  value encoded across with `std/encoding` - is `docs/design/SCRIPTS.md`'s slice 8, built beside this: a program
  `torb build` compiles links the script host instead. The front end is not in an `--embed-vm` binary, so a script is
  loaded there by a path the program was compiled with; any other path is the `SandboxError` of `Sandbox.load`.
- **Why text, and why a module of TorbScript.** The bytecode is plain data, and a string literal is the one piece of
  data the C back end already places in a binary without a native of its own; the text is one token per word, and its
  first token is a version the binary checks. The derived `Encode` and `Decode` reach every field of a `Chunk` since
  an `ArrayList` decodes; the hand-written reader of a few hundred lines stays, because it reads the tokens straight
  into the chunks, where a derived `decode` would build every word of the code as a value first. Building the interpreter from the toolchain's own
  sources, as the compiler itself is built, needs no library of the VM kept beside the runtime; it costs a C compile of
  the interpreter per binary, about half a minute.
- **The sources.** `--embed-vm` finds `compiler/` the way the toolchain finds `std/` and `runtime/` (above a path of
  the command line, the working directory or `torb` itself), or where `TORB_COMPILER` says.

