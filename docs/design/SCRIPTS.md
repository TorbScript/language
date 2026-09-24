# Receiver Scripts and the Sandbox

**Status: decided on 2026-09-23 as milestones 7.4 and 7.5 (`docs/BACKEND.md` section 5.4); slices 1 to 4 are in
(section 9).** A receiver script is lowered as one function of its module and run by the VM inside a sandbox: the
grant reaches the runtime (`runtime/sandbox.c`), a panic, `Process.exit`, a refused path and every limit stop the
script and never the host, and each stop carries the line of the script it happened on (`compiler/src/vm/sandbox.trb`,
`compiler/tests/sandbox.test.trb`). `torb manifest` evaluates a `project.trb` through it and `torb manifest --check`
compares the result with the static reader on every manifest of the repository, which is a gate of tier A. A program
run with `torb run --vm` loads and applies scripts through `std/sandbox` (`examples/config-dsl`,
`tests/conformance/vm-only/sandbox-load.trb`). The evaluation replacing the static reader where a setting is computed
(slice 5), a path only known at run time (slice 7) and a native binary that loads scripts (slice 8) are not built.

A receiver script is a `.trb` file that is the **body of a receiver closure** instead of a module: `project.trb` against
`Project`, a `config.trb` against the `ServerConfig` of the program that loads it (CONCEPT.md, "Receiver Scripts and the
Sandbox"). The checker has checked such files for a long time (`semantics/checker/receiver.trb`); this record decides
how one is **lowered, run and contained**, and how the value it configured gets back to whoever asked.

```text
   project.trb / config.trb                 the host: torb (project.trb) or a program (Sandbox.load)
        │  checked as the body of                      │
        │  (var self: Receiver) => Void                │  grants: modules, files, environment, limits
        ▼                                              ▼
   one IR function `script` of the module ──► bytecode ──► the VM, inside a sandboxed call
                                                            │   steps and time: counters of the interpreter
                                                            │   memory, files, environment, exit, panics:
                                                            │   runtime/sandbox.c, consulted by the runtime
                                                            ▼
                                        Ok: the receiver, configured in place
                                        Fail: SandboxError(line, message) - the host goes on
```

- **[1. A script runs in the VM, always](#1-a-script-runs-in-the-vm-always)**
- **[2. Checking](#2-checking)** — the receiver, the prelude, and imports against the grant
- **[3. Lowering](#3-lowering)** — one function per script module, emitted only where something loads it
- **[4. The sandbox](#4-the-sandbox)** — capabilities, limits, and what `project.trb` may do
- **[5. How the host passes the value in and reads it out](#5-how-the-host-passes-the-value-in-and-reads-it-out)**
- **[6. Errors and their sites](#6-errors-and-their-sites)**
- **[7. `project.trb` through the VM](#7-projecttrb-through-the-vm)**
- **[8. What this is not](#8-what-this-is-not)**
- **[9. Slices](#9-slices)**
- **[10. Open](#10-open)**

---

## 1. A script runs in the VM, always

**Decision: a receiver script is interpreted, by every host, and never compiled to native code.** The three things
that make a sandbox a sandbox - a step limit, a time limit and a panic that does not end the host - are properties of
an interpreter: a native frame cannot be counted per instruction, and a panic in native code ends the process by design
(CONCEPT.md, "A panic aborts the process"). So the one place where the language promises a recoverable panic is also
the one construct defined to run in the VM.

- **This is the one exception to "the VM is never the more permissive back end"** (VM.md section 1). That rule is
  about *programs*, which both back ends must run alike. A script is not a program: it is data with control flow,
  and the C back end refuses a program that loads one with a finding that says a native binary does not embed the VM
  yet (slice 8), exactly as it refused `Script` before.
- **The hosts that exist are the ones that already have a VM:** `torb` itself (it evaluates `project.trb`) and a
  program run with `torb run --vm` (`Sandbox.load`, slice 4). A native binary that loads scripts embeds the front end
  and the VM (BACKEND 5.4) and is slice 8.

## 2. Checking

The file is checked as the body of `(var self: Receiver) => Void` with the file scope "the prelude and the receiver",
as `semantics/checker/receiver.trb` has done since the checker existed. What changes is imports.

**Decision: a script may `use` a module, and the import is checked against the grant - at `torb check` time where the
grant is fixed, at load time where the host decides it.** "Capabilities are checked at module import, before anything
runs" (BACKEND 5.4): the import list *is* the capability list, because there is no reflection, no `eval` and no dynamic
import.

- **`project.trb`'s grant is the toolchain's and never changes** (section 7), so `torb check` reports an import it does
  not allow, at the `use`, with the list of what is allowed:

  ```text
  error: A project file may import `std/fs`, `std/text` and `std/os/environment`, and not `std/process`
   --> project.trb:1:1
    |
  1 | use Process from "std/process"
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    = A project file reads its own directory and the environment it was started in, nothing else
  ```

- **A script a program loads is granted modules at the call site**, which the checker cannot evaluate, so its imports
  are checked by `Sandbox.load` when the script is loaded, and a module the grant does not name is the `SandboxError` of
  the load with the line of the `use` (section 6).
- **`std/machine` and `std/sandbox` are never importable by a script**, whatever the grant says, and the checker says
  so for every script: the first trusts its operands the way machine code does (VM.md section 6), and a script that
  loads scripts is a nesting the sandbox does not have (section 8).
- **Nothing is transitive.** The allowlist is about what the *script* names. A module of the standard library it may
  import can itself use anything, which is why files, the environment and `Process.exit` have a second lock in the
  runtime (section 4) - the lock that does not depend on who asked.

## 3. Lowering

**Decision: a script module lowers to one IR function, `script`, whose single parameter is the `var` receiver and whose
body is the file.** It is named after the module with the member component `script`, which no declaration can produce
and which never collides with the entry function of the same module.

- **The receiver is parameter zero, a reference.** The checker declares the receiver binding at `Span(0, 0)` and
  records the file's receiver closure there (`checkReceiverScript`); the lowering maps that span to the parameter, which
  is exactly what `bindClosureParameters` does for the implicit receiver of a closure. A property command, a member call
  and a bare `self` of the script are then places through a `var` parameter like any other.
- **The statements are a body, not a top level.** A `const` of a script is a local of the function (the checker already
  checks it with `checkStatement`), a `for` is a loop in it, and a `use` is no code at all.
- **A script is lowered only where something loads it.** `project.trb` was excluded from every program so that
  `torb build` never compiles a manifest (`programModules` in `ir/lower/lower.trb`), and it stays excluded: the
  toolchain's evaluation program asks for the script functions of the manifests it evaluates, and nothing else does.
- **A program carries the table of its scripts.** `scriptBody<Value>(path)` and `scriptImports<Value>(path)` of
  `std/sandbox` are natives whose bodies the lowering generates once per receiver type (`ir/lower/script.trb`): a chain
  of text comparisons of the path, relative to the package, against every file the checker checked against `Value`,
  answering the script function as a closure and the modules its `use` lines name with their lines. It is ordinary IR,
  and it is what `Sandbox.load` - TorbScript now - reads. The one native only the VM has is `runScript`: the lowering
  refuses it for the C back end and hands it to the bytecode emitter as the instruction `run.script` otherwise.
- **A loaded file is a script wherever it lies**, also where a test directory swept it in as a module of the package:
  the load decides (`addReceiverScripts` in `semantics/graph.trb`).

## 4. The sandbox

**Decision: the capabilities are a closed list, granted by the host, enforced twice - once at import, once in the
runtime - and every limit is exact about what it counts.**

| Capability | How it is granted | The first lock (import) | The second lock (runtime) |
|---|---|---|---|
| Other modules of `std` | `modules "std/text"`; inside `std/os` one module at a time, `modules "std/os/system"` | the script's `use` against the list: a package, or the module of `std/os`, and a grant of a package covers its modules | - |
| The file system | `files readOnly: "./config", readWrite: "./out"` | `std/fs` has to be in `modules` | every path a file function of the runtime is handed |
| The environment | `environment "APP_*"`, `"*"` for all | `std/os/environment` in `modules` | `Environment.get` of a name no pattern matches answers `None` |
| Processes | `modules "std/process"` | the import | `Process.exit` inside a script stops the script, always |
| The network | `modules "std/http"` | the import | - (its natives are milestone 8) |
| The clock | `modules "std/time"` | the import | - |
| Output | the prelude's `print` and `printError` | - | - |
| Foreign functions | never | - | - |

- **Files: a root is an absolute, normalized directory, and "inside" is PATH.md section 7's lexical rule plus a link
  check.** A relative path of the script is resolved against the **base directory** of the sandbox (the project
  directory for `project.trb`, the working directory for a program's script), normalized, and must start with a root of
  the right side: a read may use a `readOnly` or a `readWrite` root, a write only a `readWrite` one. Every component
  below the root that exists must not be a symbolic link (a junction or any other reparse point on Windows), because
  the lexical check alone is the gap PATH.md names. `File.exists` and `File.absolutePath` are reads: whether a file
  exists outside the roots is information the grant did not give. A refusal stops the script (section 6); it is never
  an `IoError` the script could catch and ignore.
- **The environment: a pattern is a name, or a prefix followed by one `*`.** A variable no pattern matches reads as
  unset. That is quiet on purpose and is the reason the loud lock is the module: a host that grants no environment
  grants no `std/os/environment`, and the script fails to load with a message that names the module
  (PROJECT.md section 8, "A dependency does not get the environment").
- **`Process.exit` never ends the host.** Inside a sandbox it stops the script with the code in the message.
- **Output is allowed** and goes to the host's standard output and standard error, unchanged. A script cannot read
  anything through it, and a configuration that says what it is doing is a configuration somebody can debug. (Section
  10 lists this as a question of taste.)

**The limits, and what each one counts:**

| Limit | Default | What it counts |
|---|---|---|
| `steps` | 1 000 000 | bytecode instructions the VM dispatches while the script runs, including the receiver's members and `std` code it calls |
| `time` | 2 s | wall-clock time, read from the monotonic clock every 4096 steps, so a script overruns by at most that much work |
| `memory` | 64 MB | bytes the script **allocates** while it runs, counted by `torb_allocate` and `torb_raw_allocate` |

- **Memory is allocation, not residency, for now.** The runtime's `free` does not know a block's size, so what a script
  *holds* cannot be measured without the sandbox's own heap (slice 6). What it allocates can, and it is the conservative
  bound: a script can never hold more than it allocated. A configuration allocates kilobytes; a script that churns
  through 64 MB of garbage is stopped, which for a configuration file is the right answer anyway.
- **Limits are counters, not signals.** Nothing interrupts the host: the interpreter checks the step counter per
  instruction and the clock every 4096 steps, the runtime checks the allocation budget at every allocation.

## 5. How the host passes the value in and reads it out

**Decision: inside one VM the value is passed by reference; across the boundary between a host that is native code and
the VM, only text crosses, and the text is the receiver's own vocabulary.**

- **A program run by the VM** (`torb run --vm`, slice 4) and the script it loads are one bytecode program: the receiver
  has one layout, and `Script.apply(var value)` hands the script the reference the `var` parameter already is. Nothing
  is copied or converted, and the script configures the caller's value in place, as CONCEPT.md says.
- **`torb` is native code, and its `Manifest` is not the VM's `Project`.** A record of the VM has the VM's own inline
  layout (VM.md section 2), so no word of a `Project` means anything to the compiled toolchain. A `String` is the one
  value whose representation both sides share, because it *is* the runtime's `torb_text`. So the evaluation program
  answers the configured `Project` **printed back as its settings** - the literal command calls that would produce it,
  which is the `settings` block of the locked manifest (PROJECT.md section 8) - and the toolchain reads that text with
  the static reader it already has. The kernel operation `TextOut` copies a text of the VM into words the host decodes;
  it is an operation, not a native, so it needs no second commit (VM.md section 6).
- **Values in** are the same the other way round: the host places a text with `Machine.placeText` (the program's
  arguments already come in this way), and a receiver starts from its default value inside the VM.
- **A native binary** (slice 8) will cross with `std/encoding`: the host's value is encoded, decoded into the VM,
  configured, and encoded back. A receiver used from a native host must then be `Encode & Decode`, which a receiver of
  plain settings is by derivation; the checker will say so at the `Sandbox.load` of a native build. That is a decision
  about slice 8 and nothing before it depends on it.

## 6. Errors and their sites

`SandboxError(line, message)` is the only failure a script produces. `line` is a line of the **script**, 0 where the
failure is about no line of it.

| When | `line` | `message` |
|---|---|---|
| a module the grant does not name | the `use` | ``The script may not import `std/fs`: the caller grants it with `modules "std/fs"` `` |
| a path whose script the program was not compiled with | 0 | ``There is no script `./other.trb` in this program: a script is loaded by a path it was compiled with`` |
| the step limit | where it stopped | `The script ran more than 1000000 steps` |
| the time limit | where it stopped | `The script ran longer than 2000 milliseconds` |
| the memory limit | where it stopped | `The script allocated more than 64000000 bytes` |
| a panic | the panic's site, or the script's line that called into it | the panic's message; a site outside the script is appended as `(at std/text/src/lib.trb:12:5)` |
| a path outside the roots | the line that called into `std/fs` | ``The script may not read `../VERSION`: it is not inside `C:/work/shop` `` |
| a link below a root | the same | ``The script may not read `data/x`: `data` is a symbolic link`` |
| `Process.exit` | the same | `The script called Process.exit(3)` |

- **"Where it stopped" is the innermost frame of the VM whose function belongs to the script's file**, and the line is
  the location of the last instruction before its program counter that carries one. A limit that is reached inside the
  receiver's own member names the line of the script that called the member.
- **A stop leaks what the script held.** Nothing runs on the way out, as with every panic (gap 9): the frames of the
  script are abandoned and the destructors the kernel had queued are dropped without running. What leaked is bounded by
  the memory limit, and slice 6's own heap ends it.
- **For `project.trb` a stop is a diagnostic** with the file and the line (PROJECT.md section 8, "Errors"):

  ```text
  error: The script may not read `../../VERSION`: it is not inside `C:/work/shop/member`
   --> member/project.trb:2
  ```

## 7. `project.trb` through the VM

**Decision: the toolchain evaluates a manifest by running `evaluated` of `std/project` in the VM, with the manifest's
script function as its argument, under a fixed grant; the result is text that the static reader reads.**

```trb
public fn evaluated(script: (var self: Project) => Void): String {
  var project = Project()
  script project
  project.settings()
}
```

- **The grant** is PROJECT.md section 8's: modules `std/fs`, `std/text` and `std/os/environment`; files read-only below the
  project's own directory, which is also the base a relative path is resolved against; the environment `"*"`; limits of
  1 000 000 steps, 16 MB and 2 s. A dependency's manifest will get the same without `std/os/environment` once
  dependencies are resolved (PROJECT.md section 7).
- **`settings()` prints what differs from the vocabulary's defaults**, in the order of the vocabulary, every argument a
  literal. A default is not printed because the static reader has defaults of its own - a workspace without a `test`
  section has no test directory, which a printed `test { input "tests" }` would contradict.
- **One front end run evaluates every manifest below a path.** `torb manifest --check .` checks the workspace once,
  lowers every manifest's script function next to `evaluated` into one program, and runs each in its own sandbox. That
  is what makes the comparison with the static reader cheap enough to be a gate of tier A: the two must agree on every
  `Manifest` field of every `project.trb` of the repository before anything reads the evaluated one (BACKEND row 7.5).
- **The static reader stays** for the settings that decide the workspace - `name`, `prelude`, `dependencies`,
  `workspace` - because they are needed before anything can be checked, and "the static read comes first"
  (PROJECT.md section 8). What evaluation adds is every other setting, once slice 5 lets the toolchain read it.

## 8. What this is not

- **Not a build script.** A script configures a value. It does not run before a build, produce files or call the
  compiler (PROJECT.md section 13).
- **Not nestable.** A script cannot load scripts: `std/sandbox` is never importable by one. A host that wants two
  layers loads both itself.
- **Not a security boundary against the operating system.** The locks are the language's: a script cannot name a
  capability it was not granted, and the runtime refuses paths and variables outside the grant. A bug in the runtime,
  a race between the link check and the open (PATH.md section 7), and a hard link inside a root are outside it.
- **Not a cache.** Which files and variables an evaluation read (`build/manifest-inputs.trb`) is PROJECT.md's, and
  slice 5.

## 9. Slices

| # | Scope | State |
|---|---|---|
| 1 | This record | **Done** |
| 2 | The sandbox of the VM: `runtime/sandbox.c` (roots, links, patterns, the allocation budget, the stop), the kernel's recovery point around every operation while a sandbox is open, `TextOut`, the interpreter's step and time counters and the propagation of a stop, the line of a stop; a script module lowered as its `script` function; `compiler/src/vm/sandbox.trb`, the host's API; the checker's import rule for scripts | **Done**: `compiler/tests/sandbox.test.trb` pins every refusal with its exact text |
| 3 | `project.trb` through the VM: `Project.settings()` and `evaluated` in `std/project`, `torb manifest [--check]`, the gate | **Done**: `torb manifest --check` agrees with the static reader on every manifest of the repository |
| 4 | `Sandbox.load` in a program the VM runs: `SandboxCapabilities` and `Script` as TorbScript over the generated table of the program's scripts (`scriptBody`, `scriptImports`) and the one operation only the VM has (`runScript`, the instruction `run.script`); relative roots and the base read against the working directory | **Done**: `examples/config-dsl` under `torb run --vm`, `tests/conformance/vm-only/sandbox-load.trb` in `vm.list` |
| 5 | The toolchain reads the evaluated manifest where a setting it uses is computed, and refuses a computed static setting (PROJECT.md section 12, slice 7); `build/manifest-inputs.trb` | Open, after 4 |
| 6 | The sandbox's own heap: an exact memory limit, a teardown that frees what a stopped script held | Open |
| 7 | A path known only at run time: the front end inside the running `torb` checks and lowers the file into the program | Open |
| 8 | A native binary that loads scripts: the front end and the VM embedded, the value encoded across (section 5) | Open |

## 10. Open

Questions of taste for the owner; everything technical above is decided.

- **Output from a script.** Allowed and unchanged today. The alternatives are to refuse `print` without a grant
  (`output` as a seventh capability) or to prefix what a script prints with its file.
- **The CLI.** `torb manifest` prints the evaluated settings of a project; `torb lock` will write them into
  `project.lock.trb` (PROJECT.md section 8). Whether the first stays a command of its own or becomes `torb lock --print`
  is a question of the command's vocabulary.
- **The environment of `project.trb`.** PROJECT.md section 8 grants every variable to the invoked project and its
  members; the grant here follows it. Narrowing it to nothing, which would make a manifest a pure function of its
  directory, is the one direction PROJECT.md considered and rejected, and it stays the owner's to reopen.
