# The REPL

**Status: implemented** - decided on 2026-09-23 as milestone 7.6 (`docs/BACKEND.md` section 5 and row 7.6), the owner's
taste questions of section 13 decided 2026-09-24. Every slice is in (section 12): `torb repl` reads entries from
standard input, checks each against the session, runs it in the VM and keeps what it binds and declares
(`compiler/src/repl/`, `compiler/src/cli/repl.trb`); `> ` and `. ` prompt every line on standard error; a value a type
declared again keeps working under a generation of its own, shown as `Point#1` - the derived `show()` of the
generation's own kept declaration opens with that name directly, so nothing already shown is ever rewritten; Ctrl+C
stops a running entry or gives a fresh prompt, without ending the session; and `--sandbox` runs every entry under
SCRIPTS.md's grant of the working directory instead of `unrestricted`. `compiler/tests/repl.test.trb` pins how an
entry is read and written into a module, and `tests/repl/` pins whole sessions against their exact output
(`tools/repl.sh`, a gate of tier A).

`torb repl` is the user's own shell for TorbScript: an entry is typed, checked against everything the session holds,
compiled and run once, and what it binds and declares stays for the entries after it.

```text
   standard input ──► the reader: lines until they are a whole entry (section 2)
                              │
                              ▼
   the entry, taken apart ──► declarations · statements · names it binds · the value it shows (section 3)
                              │
                              ▼
   the entry's module ──────► kept declarations + the entry's + fn replEntry(<bindings>, var <outputs>) { statements }
                              │   checked twice: the probe finds the types, the second check hands them out (section 4)
                              ▼
   lowered, and bytecode ───► appended to the one program the VM has loaded, every index continued (section 5)
   that continues it          │
                              ▼
   replEntry called ─────────► in a sandbox that grants everything: a panic, a failed `?` and Process.exit end the
   with every binding          entry, never the session (section 7); the new bindings land in words below every frame
```

- **[1. A session is one growing program](#1-a-session-is-one-growing-program)**
- **[2. Reading an entry](#2-reading-an-entry)**
- **[3. What an entry is made of](#3-what-an-entry-is-made-of)**
- **[4. The module of an entry](#4-the-module-of-an-entry)**
- **[5. Where the bindings live](#5-where-the-bindings-live)**
- **[6. Rebinding and declaring again](#6-rebinding-and-declaring-again)**
- **[7. Full access, and the sandbox as a recovery point](#7-full-access-and-the-sandbox-as-a-recovery-point)**
- **[8. What the session prints](#8-what-the-session-prints)**
- **[9. Commands and history](#9-commands-and-history)**
- **[10. `use`: the standard library and the workspace](#10-use-the-standard-library-and-the-workspace)**
- **[11. What an entry cannot do yet](#11-what-an-entry-cannot-do-yet)**
- **[12. Slices](#12-slices)**
- **[13. Open](#13-open)**

---

## 1. A session is one growing program

**Decision: an entry is checked as a module the session writes for it, compiled into a continuation of the one program
the VM has loaded, and run once; its bindings are words of that program that outlive the entry.** Nothing runs twice
and nothing is copied out of the VM.

- **Not a replay.** Checking the whole session as one file and running it again for every entry would print what the
  earlier entries printed again, read a file again and draw another random number: an entry has effects, and they
  happen once.
- **Not a persistent checker.** BACKEND.md 5.4 sketched a `Checker` that keeps a growing chain of scopes. The checker is
  a whole-program pass over a `SourceTree` - symbols, then signatures, then bodies - with no incremental entry, and
  giving it one is a rewrite of the front end's order. Checking a module again costs what its *new* text costs instead:
  every other file is parsed once when the session starts (`SourceTree.parsedEverything`), and `check` limits the bodies
  it checks for a lowering to the program's packages (`programPackagesOf`, which the lowering already closed its world
  over) - which makes `torb run` and `torb build` of a scratch file at the root of a large workspace cheaper too.
- **A binding is typed by text.** The next entry's module declares an earlier binding as a parameter with its type
  written out - `var names: List<String>` - as the checker describes it. What an entry sees of the session is exactly
  what a module can declare, and the checker needs no second way into a scope.

## 2. Reading an entry

**Decision: lines are read from standard input and joined until they are a whole entry; a whole entry is one the parser
has nothing to say about at its very end** (`isComplete`, `compiler/src/repl/entry.trb`).

- An open bracket, an operator or `=` at the end of a line, a `"""` string and a `/* */` comment that are not closed
  each make the parser report the end of the input, and the reader reads another line. Anything else that does not
  parse is complete, and the parser's message is what the entry gets - a stray `}` is reported, not waited on.
- **Two empty lines in a row drop an entry that never became whole** (`note: the unfinished entry is dropped`). The end
  of the input evaluates what is still open, and the parser says what is missing.
- A line that starts with `:` while no entry is open is a command (section 9).
- **Plain standard input, no line editor.** A piped file is a session exactly like a typed one, which is what makes
  `tests/repl/` possible and `torb repl < setup.trb` useful.
- **`> ` before a whole new entry, `. ` while one is still open**, written to standard error without a line break
  right before every line is read (`printErrorRaw`, a native that writes without one - `runtime/console.c`,
  `compiler/src/cli/repl.trb`). Standard error, not standard output, so that what the entries printed and showed stays
  exactly that; a piped session carries the same prompts a typed one would, interleaved with the rest of standard
  error exactly as they were written - which is what `tests/repl/*.stderr` pins.
- **Ctrl+C**: on an empty prompt or while an entry is still being typed, it drops what is pending (there is nothing to
  drop on an empty one) and gives a fresh prompt; a second one right on an empty prompt, or Ctrl+D, ends the session
  like the end of the input. While an entry runs, the same Ctrl+C stops it instead - section 7 - and returns to the
  prompt with the session intact. `installInterruptHandler`/`interrupted` (`std/console`) are the native pair behind
  it: the first is called once, when the session starts, and the second is asked after a blocked read answers nothing
  and by the VM's budget (`refill`, `compiler/src/vm/interpret.trb`) on every check.

## 3. What an entry is made of

The entry is parsed alone (`planEntry`):

| Part | What the session does with it |
|---|---|
| A declaration: `fn`, `type`, `trait`, `extend`, `use`, a `public const` | kept for every later entry (section 6) |
| A statement: a binding, an assignment, a loop, a call | run once, in order |
| A name the top level binds with `const` or `var` | a binding of the session once the entry ran to its end |
| A `using` binding | closed at the end of its entry, as at the end of any block, and not kept |
| The last statement, where it is an expression with a value | shown (section 8) |
| The last statement, where it is a `const`/`var` binding instead (not `using`) | shown by name (section 8) |
| A `return` at the top level | refused: an entry is not a function |

An `if` without `else` has no value and shows nothing. A statement that produces a value and is not the last one is the
ordinary error `This value is not used`, as in a program.

## 4. The module of an entry

**Decision: an entry becomes a module of the session's declarations, its own declarations at the top level, and one
function `replEntry` whose parameters are the session's bindings and whose body is the entry's statements** (`synthesize`,
`compiler/src/repl/synthesis.trb`):

```text
use IoError from "std/fs"                      what a binding's type names and the session never imported
<every declaration the session keeps, copied from the entry it came from>
<the entry, its statements blanked>
fn replEntry(count: Int64, var names: List<String>, var replOut0: Int64): Void {
if true {
<the entry, its declarations blanked>
const replShown =
<the last statement>
replOut0 = total
print "{replShown}: Int64"
}
}
```

- **A `const` binding is a parameter by value, a `var` binding a `var` parameter.** A `const` stays unassignable, and a
  `var` is changed in place through the reference its parameter is (VM.md section 4): `names.append "Alan"` changes the
  session's list without a copy.
- **The statements stand in a scope of their own** (`if true { ... }`), because the parameters and the top level of a
  body are one scope and nothing there may shadow a parameter. Inside it `const count = count + 1` is ordinary shadowing.
- **Every piece of an entry is copied verbatim, and what stands where something else stood is blanked byte for byte**,
  so an offset inside a piece is the offset in its entry. A diagnostic is traced back through the pieces
  (`Synthesis.locate`) and rendered against the entry's own text: the line and the column of a message are the entry's.
- **Two checks.** The probe checks the module without outputs and reads the types the checker gave the names the entry
  binds and the value it shows. The second check writes those types into `var` parameters `replOut0`, `replOut1`, ...,
  assigns each new binding to its output at the end, and checks every body the program may reach, which the lowering
  needs.
- **A `?` at the top level.** A top-level `?` of an entry file ends the program with the failure; an entry must not end
  the session. Where the probe's only complaints are "`?` needs a function that returns an `Option` or a `Result`", the
  failure type is read off what each `?` unwraps, and the statements move into `fn replBody(...): Result<Void, E>` with
  `Ok void` at its end; `replEntry` answers whether it ran and prints `error: <the failure>` where not. A `?` on an
  `Option`, or two `?`s that fail with different types, keep the checker's own message.
- **A type is written with what it names.** `const text = File.readText(path)` binds a `Result<String, IoError>` in a
  session that imported only `File`, so the module imports `IoError` from the package whose `src/lib.trb` exports it,
  in front of everything else - never a name the session declares itself. A binding whose type still cannot be written
  (its type is private to its file) is not kept, with a note, and lives as long as its entry, as a `using` does.
- **A declaration of an entry cannot see the session's bindings**, as a top-level `fn` of a program cannot see the
  locals of the code that calls it; the message says so (`pass rate in as a parameter`). A closure can:
  `const scaled = { value: Int => value * rate }` captures it.

## 5. Where the bindings live

**Decision: the program of a session grows by one continuation per entry, and a binding's value is words below every
frame that no continuation moves.**

- **A continuation continues every index.** `emitBytecode(program, entries, after)` numbers the new chunks, constants,
  shapes, witness tables, element descriptors and locations after the ones `after` has, and `appendProgram` loads only
  what is new into the words and the kernel's tables. A closure an earlier entry made names its chunk by index, a boxed
  record its shape, a list its element descriptor: each means the same thing in every later part. Parts never unload.
- **The words of a binding are a gap of the constant pool.** A module constant's cell already was one; its address is
  now absolute and inside the pool (`ConstantPart.Gap`) instead of an offset after it, which is what lets a
  continuation's constants follow an earlier part's cells. Before the call the host reserves a zero gap per output
  (`reserveWords`) - zero is a value of every type, because "the zero value owns no count" - the output parameter is a
  reference to it, and assigning the new binding releases the zero and stores the value. The first frame begins after
  the last gap, so no call ever overwrites a binding.
- **A value is laid out the same in every continuation.** Layouts, representations and word widths are functions of the
  type's declaration, which the session writes the same way every time. What is **not** a function of the declaration
  is what the whole-program passes decide, so the entry function is a *hosted* function (`IrProgram.hosted`): the
  devirtualization may not move its signature - a `List<String>` stays the trait-typed value an earlier entry built,
  not the `ArrayList` one program could prove it is - and the ownership summary may not promote its parameters to
  owned, because the host lends them and keeps them.
- **Element descriptors are shared.** The kernel has 64 callback slots for counted element types, and a continuation
  that holds a `List<Point>` would take one each time. The interpreter names the first of equal shapes for a
  descriptor and the runtime gives equal words and shape one slot (`runtime/machine.c`), so a session uses a slot per
  element type, not per entry.
- **Releasing is the host's.** A binding that is replaced, displaced by a declaration or left at the end of the session
  is released by its shape (the entry chunk carries every parameter's value shape and width) inside the same sandbox,
  and the destructors it queues run as in a program; at the end the newest binding goes first.

## 6. Rebinding and declaring again

| The entry says | The session afterwards |
|---|---|
| `const count = count + 1` where `count` is a binding | the new `count`; the old value is released once the entry ran to its end |
| `var tally = 0`, later `tally = tally + 1` | one binding, changed in place |
| `fn describe(...)` again | the new declaration, for every later entry and every kept declaration |
| `type Point { ... }` again | the new type; a binding whose type names `Point` keeps working, shown with the old type as `Point#1` (then `#2`, ...) |
| `use IoError from "std/fs"` where an earlier `use File, IoError from "std/fs"` imported it | the new `use` of `IoError`; `File` stays, because a kept `use` is one per name |
| a `fn` or `type` with the name of a binding | the binding is gone, with a note |

- **Declarations are kept once the entry checks; bindings once it ran to its end.** A declaration does not run, so an
  entry whose statements stop keeps its declarations and loses its new bindings; what it changed in a `var` binding
  before the stop stays changed, as in a program.
- **Why a binding of a type declared again keeps working.** Its value has the old layout, and the next entry would
  otherwise write its type as the new `Point`, which does not describe it. The session keeps the old type's own
  declaration once more, under a generation of its own - internally `Point__1`, a name nothing a person writes can
  collide with - so the binding's parameter is still written as a real, checkable type in every later entry's module,
  with the old layout, and nothing about the value is reinterpreted. A second redeclaration of `Point` bumps whichever
  generation is still bare (`Point` itself, not an already-numbered one) to the next number; a binding already at
  `Point#1` stays there. Functions and other kept declarations that name `Point` by its bare name are unaffected and
  keep meaning whichever declaration is current - only a *binding*'s own type is rewritten this way.
- **`Point__1` is read as `Point#1` at both places it is shown, and neither one rewrites a finished string.** A type
  annotation - the `: Type` of section 8, `:type`, a diagnostic - goes through `displayedType`
  (`compiler/src/repl/session.trb`), which rewrites the *text the checker itself produced* from the type's structure.
  A value's own `show()` opens with `Point#1` directly, because the generated `show` of a generation's kept
  declaration builds its display name into its own literal text (`generationDisplayName`,
  `compiler/src/ir/lower/derive.trb`) rather than opening with `Point__1` and leaving a later pass to fix it - a
  field could legitimately hold the mangled text as data (a `String` of `"Point__1"`), and a pass over the finished
  string could not tell that apart from the type's own name.
- **A kept declaration is checked again with every entry**, because it is part of the module. An entry that breaks
  one - `twice` declared again with another parameter type that `quad` of an earlier entry calls - is refused, with the
  message rendered against the earlier entry and a note that says so, and the old `twice` stays.

## 7. Full access, and the sandbox as a recovery point

**Decision: an entry has every capability the user has - every file, every variable, `std` as far as it reaches - and
runs in a sandbox all the same, because the sandbox is also the recovery point.**

- A REPL is the user's own shell, and a grant would be a question the user answers by typing what the grant says. The
  limits of a receiver script are a defense against a file somebody else wrote.
- The grant is `unrestricted` (`runtime/sandbox.c`) by default: no path is refused, every variable is readable,
  nothing is counted against a memory or a step limit. What stays is the kernel's recovery point: **a panic stops the
  entry and not the session**, and so does a destructor that panics while a binding is released.
- **Ctrl+C stops a running entry the same way.** The VM's budget - unlimited for an entry otherwise, `unlimitedBudget`
  - still checks the interrupt flag at its usual interval (`refill`, `compiler/src/vm/interpret.trb`), and stops with
  the reason `Interrupted` exactly as a step limit would, reported as `error: Interrupted` and back to the prompt with
  the session intact: whatever the entry changed in a `var` binding before the stop stays changed, and it keeps none
  of the bindings it would have made, exactly as any other stop of an entry does (section 6).
- **`Process.exit(code)` ends the session with that code**, as it ends a program; nothing is released on the way out,
  as nothing is on a program's exit - `--sandbox` does not change this either, because it is a decision about what an
  entry *is*, not about what it may reach.
- **`torb repl --sandbox` (slice 5, built): the grant becomes SCRIPTS.md's own, not `unrestricted`.** The working
  directory read and write (`Grant.readWrite`, `compiler/src/vm/sandbox.trb` - a root to write is one to read as
  well), no variable of the environment, and `Grant`'s own default limits - SCRIPTS.md's general table, 1,000,000
  steps, 64 MB, 2000 ms - so the line this repository draws once needs no line of its own here, a fresh budget per
  entry the same way `runScript` gives a fresh one per script: a runaway entry stops itself exactly as a limited
  script does, without waiting for Ctrl+C. Neither the limits nor the environment pattern are configurable yet (a
  later slice); `:reset` keeps `--sandbox` for the new session it makes.
  What `--sandbox` does *not* narrow: which packages of `std` an entry may `use` (section 10) - that lock is for a
  script a *program* loads at a call site it cannot see ahead of time (SCRIPTS.md section 2), and a person typing at
  their own prompt already sees every line before it runs, so there is nothing here for that lock to check.

## 8. What the session prints

- **What an entry prints goes where a program's would**: `print` to standard output, `printError` to standard error,
  as it happens.
- **The value an entry shows is Scala/Kotlin style, value first: `1 + 2` shows `3: Int64`**, on standard output,
  through `Show` - a line the program printed and a value the session shows are told apart by the trailing `: Type`,
  and `print "hello"` shows nothing of its own because it answers `Void`. The type is dimmed (ANSI faint,
  `\u{1b}[2m`...`\u{1b}[0m`) when standard output is a terminal (`isTerminal`, decided once when the session starts)
  and plain otherwise, which is what every piped session of `tests/repl/` compares. A value without a `show` - a
  function - shows its type alone, in angle brackets: `<(value: Int64) => Int64>`.
- **A `let`/`const`/`var` entry shows the same way, with its name in front**: `const total = 6 * 7` shows
  `total: Int64 = 42`; a tuple pattern shows every name it binds, one line each, in the order they are written. A
  binding of a type with no `Show` shows its name and type alone, without `= value`. An assignment - `total = 43` - is
  not a binding and shows nothing, as it did before.
- **Messages go to standard error**, each a diagnostic exactly as `torb check` renders it, with the entry as the file:
  `<entry 3>` is the third entry of the session, a `:load`ed file is named by its path, `:type` is `<type>`, and the
  line and the column are the entry's. A declaration of an earlier entry that an entry broke is rendered against that
  entry, with a note.
- **A stop is what a native program prints**: `panic: <message>` and `  at <entry 7>:2:3`. Where the site is in code
  the session did not write - `std` - that site follows `at`, and `  in <entry 6>:3:19` names the innermost line of an
  entry the call came from (the entry alone where the call carries no site of its own). A `?` that fails prints
  `error: <the failure through Show>`, as an entry file's top level does.
- **The notes of the session itself** - a binding that is gone, a binding that is not kept, an unfinished entry dropped
  - are `note: ...` lines on standard error.

## 9. Commands and history

| Command | What it does |
|---|---|
| `:type <expression>` | the type of the expression, checked against the session and not run, on standard output |
| `:load <file>` | the file as one entry: its declarations, its code and its bindings join the session; messages name the file |
| `:reset` | a new session: every binding released, every declaration forgotten, the files around read again |
| `:help` | the list of commands |
| `:quit` | the end of the session; so is the end of the input |

- **History is the terminal's.** The Windows console keeps a line history for every program that reads standard input
  in its cooked mode, and on a POSIX terminal `rlwrap torb repl` adds one. A history file of the REPL's own needs a
  line editor to be worth anything - the prompt of slice 2 is plain, printed ahead of an ordinary line read, and no
  line editor came with it - so a history file is still a later slice, past 5.

## 10. `use`: the standard library and the workspace

**Decision: the session's module is a file of the working directory that exists only in memory**
(`<directory>/repl-session.trb`). It may `use` whatever a scratch file in that directory may: every package of `std`,
and in a workspace the packages its package may import - a session started in `shop/` of a workspace is a file of
`acme/shop`, and one started at the root imports what the root depends on, with the checker's own message where it does
not. The files around it are read and parsed once when the session starts; `:reset` reads them again, which is how an
edit to a package reaches the session.

## 11. What an entry cannot do yet

What the VM does not run is refused before anything of the entry runs, with the reason: a task (`Task`, `await`,
channels - VM.md slice 7), `test` and `group` (slice 6), a map key compared by its own `equals` (slice 4). An entry
that never ends is stopped with Ctrl+C (section 7), or by ending `torb`.

## 12. Slices

| # | Scope | State |
|---|---|---|
| 1 | Reading, planning and writing an entry; the continuation of the bytecode (`emitBytecode` with `after`, `appendProgram`, gaps); hosted functions; the session's bindings, rebinding and declaring again; `?` at the top level; the imports a type needs; the `unrestricted` grant; `:type`, `:load`, `:reset`, `:help`, `:quit`; `tests/repl/` and `tools/repl.sh` | **Done** |
| 2 | A prompt: `> ` and `. ` on standard error before a line is read (`printErrorRaw`, a native that writes without a line break) | **Done** |
| 3 | Values of a type declared again kept as `Point#1` (section 6) | **Done** |
| 4 | An entry that can be interrupted: Ctrl+C stops the entry through the budget of the interpreter instead of ending `torb` | **Done** |
| 5 | `torb repl --sandbox`: SCRIPTS.md's grants for a session | **Done** |

## 13. Open

Everything of taste the owner had a question about (2026-09-24) is decided and built: a value of a type declared
again is kept and shown as `Point#1` (section 6); the shown value is Scala/Kotlin style, `3: Int64` (section 8); the
prompt is `> ` and `. ` on standard error (section 2); `--sandbox` is SCRIPTS.md's grant, unconfigured (section 7). No
slice is open. What is not verified by anything but reasoning is one implementation gap: Ctrl+C's native handler
(`installInterruptHandler`, `runtime/console.c`) was written and reasoned through for both platforms - `sigaction`
without `SA_RESTART` on POSIX, `SetConsoleCtrlHandler` on Windows - but interrupting a *live, interactive* session was
not something this environment could drive to verify; the reader's own decision of what to do once an interrupt is
noticed (`actionFor`, `compiler/src/cli/repl.trb`) is unit tested instead (`compiler/tests/repl.test.trb`), and the VM
stopping a running entry is exercised by `refill` unconditionally asking `interrupted()`
(`compiler/src/vm/interpret.trb`) - both without needing a real signal. Worth a person trying it by hand once. Also
left for later, by design and not by gap: `--sandbox`'s limits and environment pattern are `Grant`'s own defaults and
not yet configurable (section 7), and it does not narrow which packages of `std` an entry may `use` (section 7).
