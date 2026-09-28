# The language server

**Status: the first round is implemented** (2026-09-28, milestone 8 of [ROADMAP.md](../ROADMAP.md)). `torb lsp` speaks
the Language Server Protocol over standard input and output (`compiler/src/language-server/`): the life cycle,
incremental document sync, the diagnostics of `torb check` with their ranges, hover, go to definition, completion of
members and of the names in scope, semantic tokens, and the fixes of `torb lint` as quick fixes. The VS Code extension
in `editors/vscode` starts it (`lsp-client.js`), published to the Visual Studio Marketplace and Open VSX with every
release (section 10). A keystroke redoes the part of the one file that changed
(section 6). `compiler/tests/language-server.test.trb` holds sessions fed to the server in memory, `tests/lsp/` whole
sessions over standard input and output (`tools/lsp.sh`, a gate of tier A). `torbscript/tests`, a request of the
server's own (`experimental.tests` among the capabilities), answers the tests and groups of a file from its syntax tree
alone, without the checker, for the list of tests of an editor; `torb test --report json` reports how they ran
([torb lsp](../tooling/torb-lsp.md)). Section 11 is what is left.

The roadmap says it in one line: *the language server is the compiler* - `check` incremental per file, and the checker's
tables answer hover, go to definition, completion and diagnostics; its semantic tokens replace `torb highlight`, and a
lint fix is its quick fix. This record says how, and why each decision went the way it did.

```text
   editor ──stdin──► FrameReader: bytes to message bodies (protocol.trb)
                            │   the bodies that arrived together are one batch
                            ▼
                     LanguageServer.receive (server.trb) ──► documents, changed paths
                            │   once per batch, or before a request that needs it
                            ▼
                     a run (analysis.trb): the front of the last run with the changed files redone,
                            │   or a front from scratch; the type checker over it, for the bodies of the
                            │   changed documents and the open documents that import them
                            ▼
                     diagnostics, hover, definition, completion, tokens, quick fixes (one file each)
                            │
   editor ◄──stdout── framed JSON (command.trb)
```

- **[1. The server is a subcommand of the compiler](#1-the-server-is-a-subcommand-of-the-compiler)**
- **[2. Standard input is read by a task](#2-standard-input-is-read-by-a-task)**
- **[3. The server is a value without a transport](#3-the-server-is-a-value-without-a-transport)**
- **[4. Positions, URIs and JSON](#4-positions-uris-and-json)**
- **[5. What a run checks](#5-what-a-run-checks)**
- **[6. The front of the last run](#6-the-front-of-the-last-run)**
- **[7. Diagnostics and quick fixes](#7-diagnostics-and-quick-fixes)**
- **[8. Hover, definition and completion read the checker's
  tables](#8-hover-definition-and-completion-read-the-checkers-tables)**
- **[9. Semantic tokens](#9-semantic-tokens)**
- **[10. The client](#10-the-client)**
- **[11. What is left](#11-what-is-left)**

## 1. The server is a subcommand of the compiler

**Decision: `torb lsp`, in `compiler/src/language-server/`, is the same binary as every other command.** CONCEPT's
single-binary toolchain names the language server among its parts, and there is nothing a second binary would do better:
the server needs the lexer, the parser, the checker, the highlighter and the lint rules, which are the compiler. It adds
no native, no flag the build uses and no syntax, so it needs no second commit and no new seed (CLAUDE.md, "Seed and
breaking changes"), and it does not branch on the operating system anywhere, so the C of the compiler is the same for
every target as before.

## 2. Standard input is read by a task

The base protocol frames a message with a byte count and no line break after the body, so it cannot be read with
`readLine()`: the line would end only with the next message's header, and a client that waits for the answer to
`initialize` before it sends anything else would wait forever.

**Decision: the server reads `std/io`'s `standardInput()`, the byte source every program has, and the loop that reads it
is a function that answers a `Task`.** Its reads are tasks already (`standardRead` on a thread of the blocking pool), so
nothing new was needed. The top level of `torb` does not `await()` that task: an `await()` there would make the whole
entry file the main task, and the VM that `torb run` and `torb test` start refuses to run its scheduler from inside one.
The entry starts the task and finishes; `torb_scheduler_run` after the entry runs it, and the loop ends the process
itself with `Process.exit` (docs/design/CONCURRENCY.md; the C main of every program that uses tasks has had that tail).

The alternative was a synchronous native that reads a given number of bytes. It is simpler to read, and it costs the
two-commit dance of a new native and a seed that knows it before the language server could land. Standard output is
written the same way, through `standardOutput()`, which writes the bytes as they are - `\r\n` in a header stays `\r\n`
on every system, and a body is never split.

## 3. The server is a value without a transport

**Decision: `LanguageServer.receive(bodies)` takes the bodies of the messages that arrived together and answers every
message to send, in order; `command.trb` is the only file that touches standard input or output.** The tests of
`compiler/tests/language-server.test.trb` build a workspace in memory, feed JSON texts to the server and compare what
comes back, without a process, a pipe or a file; `tests/lsp/` runs the same kind of session through the real transport.

**The batch is the unit of work.** A change marks its document. The checker runs once at the end of the batch, or
earlier when a request of the batch is about a document whose text changed since its last run. A client that sends a
change per keystroke while a run is busy has those changes waiting in the pipe, and the next read hands them over
together: they cost one run. Requests are answered in the order they came and each one before the next message is read,
so `$/cancelRequest` has nothing to cancel and is ignored.

## 4. Positions, URIs and JSON

- **Positions are UTF-16, offsets are bytes.** The protocol's character counts UTF-16 code units, and so does VS Code;
  the compiler's spans are byte offsets. `LineMap` (`position.trb`) translates both ways: a character that UTF-8 writes
  in four bytes is two code units, every other one is one. The server announces `positionEncoding: utf-16`.
- **A URI below the root keeps the root's spelling.** VS Code writes `file:///c%3A/Projekte/...`, a lowercase drive and
  an escaped colon, and `File.absolutePath` spells the same path `C:/Projekte/...`. `Locations` (`uri.trb`) turns a URI
  below the root the client named into a path below the root the tree was read from, and a path back into the client's
  own spelling, so a location the server sends names the same document the client knows. Everything else is a `file:`
  URI of the absolute path, with a drive letter recognized by its shape - text alone, the same on every system.
- **JSON is `std/json`.** A message is read with `Json().parse` into a `JsonValue` and written with `Json().encode`; the
  objects the server answers with are built from lists of entries, and a `Map` keeps the order of insertion, so the text
  of an answer is the same every time - which is what the expectations of the tests compare.
- **Changes are incremental** (`change: 2`): a range and its new text, or the whole text where no range is given.

## 5. What a run checks

**Decision: a run checks the documents that changed, every open document that imports one of them (directly or through
other modules, in the module graph of its last run), and a document a request is about that no run has checked yet -
their bodies, and the bodies of nothing else.** The documents that nothing changed keep what their last run found.
`check` got the parameter this needed, `onlyNamed`: the paths it is given are checked and reported, not the files they
import through a relative path, which `torb check main.trb` reports as files of the program named. A file of the
compiler imports sixty others that way, and checking their bodies was more than half of a keystroke.

The program a run knows is the program of every open document (`onlyReached`, the packages their imports reach), so a
change of any open document finds its module in the last run's graph. The diagnostics go to open documents only; a file
that is not open has no diagnostics in the editor, which is what `torb check` in a terminal is for.

## 6. The front of the last run

`check` is two stages now (`../semantics/check.trb`): **`frontOf`** - the workspace, the module graph, the names of
every module (`resolveNames`), the names in every type position (`resolveTypes`) - and **`checkFront`**, the type
checker over that. `check` is the two in a row, and every command still calls `check`. The front depends on the
declarations and the imports of the files and not on their bodies, and with parsing kept (below) it was four fifths of a
keystroke in a file of the compiler.

**Decision: the server keeps the front of its last run, and where a change leaves a document's outline alone, it redoes
only that document's part of it** (`refreshedFront` in `analysis.trb`). The outline (`outlineOf`) is what the rest of
the program sees of a file: every name it declares with its kind and visibility, the cases of its types, every `use`
with its items, every script it loads - in order, without a position. With the same outline the file declares the same
symbols in the same order, and every other module's names still lead to them; only their positions moved. So the server
parses the file, declares its symbols once more on the side to read their new spans off, writes those spans into the
symbols the front already has - same ids, so nothing that points at them changes - and resolves the file's names in type
positions again. The type checker then starts from that front as it always does: it builds its signatures, the
implementation index and the coherence of the program from the syntax trees (a few dozen milliseconds), and checks the
bodies of the documents of the run.

A front is built from scratch when a document's outline changed, when a document was closed (its text goes back to the
disk's), when a watched file changed on disk, and after 200 runs: resolving a file's type positions declares its type
parameters again, so the symbols of the program grow by a few with every run until then.

**Parsing is kept in the tree.** Every run hands its parses to the tree (`SourceTree.withParsed`), and
`SourceTree.withFile` forgets the parse of the one file whose text it replaces, so even a front from scratch parses only
what changed. `torb repl` kept its tree parsed the same way, with `parsedEverything`; the server fills it with what a
run reached instead, which is less than the whole workspace.

What a keystroke costs, from the change to its diagnostics, on the machine of [PERFORMANCE.md](../PERFORMANCE.md) while
other agents' builds ran beside it (the server logs each run with the cost of every pass on standard error):

| Document | A run from scratch (the first) | Before the front was kept | With the front kept |
|---|---:|---:|---:|
| `std/json/src/lib.trb`, 954 lines | 370 ms | 180 ms | 85-105 ms |
| `compiler/src/lint/finding.trb`, 92 lines | 2.3 s | 850 ms | 160-230 ms |
| `compiler/src/semantics/checker/expression.trb`, 3981 lines | 4.8 s | 2.8 s | 1.5 s, of which its bodies are 1.35 s |

## 7. Diagnostics and quick fixes

- **A problem of `torb check` is an error** with the range of its span; its notes follow the message, a line each.
- **A finding of `torb lint` is a hint**, with the id of its rule as the code and `torb lint` as the source; a binding
  nothing reads carries the tag *unnecessary*, which an editor fades. A hint and not a warning, because the rules are
  the style the checker leaves alone and `torb lint` is no gate - and the repository has sweeps of `self-name` and
  `question-field` still to make (ROADMAP, milestone 8), so a warning would paint most of the compiler. The rules that
  read the syntax tree run wherever the file parses; the rules that read the checker's tables run where the checker
  found no problem, exactly as `torb lint` decides it.
- **A quick fix is the fix of one finding** (`actions.trb`): the edits `torb lint --fix` writes, as a `quickfix` code
  action on every finding with a fix that touches the range asked about. One action per finding, so each can be taken on
  its own; `--fix`'s own safety net - keep only fixes that still parse - is the editor's undo here.

## 8. Hover, definition and completion read the checker's tables

The tables are keyed by span ([TYPECHECKER.md](../TYPECHECKER.md) 7.3), so **what stands at a position is the smallest
span that holds it** (`lookup.trb`): of `a.b.c` with the cursor on `b`, that is the span of `a.b`, whose resolution is
the member `b`. The candidates are the resolutions of expressions, the names in type positions, the names of
declarations (a symbol of the module whose name is still written at its span) and the names of local bindings where they
are bound; an expression's type answers where none of them does.

- **Hover** shows a declaration as the source writes it up to its body - the tokens between, one space where the source
  had any, which `torb doc` does for its signatures (`signatureOf` of `../reference/model.trb`) - with its doc comment
  under it. A local shows `const`/`var`, its name and its type; a constant its type, which its declaration need not
  write; anything else the type the checker gave it.
- **Go to definition** follows a resolution to the symbol it names and the symbol to its module and span; a local leads
  to where it is bound, a module to its first line.
- **Completion behind a `.`** looks for the smallest expression of the run that ends at the dot. Where the text around
  it does not parse - `point.` alone on a line - no expression ends there, and the server checks the text once more
  without the dot and what follows it on the line, which is the receiver alone, over the same front. The receiver's type
  offers its fields, `copy` and the methods that take a receiver (`memberNames` and `lookupMember` of the checker, its
  extensions and the traits it implements included); a type offers its cases, constants and `static` members; a module
  its exports. **Anywhere else** the names in scope: the locals of the bodies around the cursor, read from the syntax
  tree so that they are there in code the checker has not reached, then the file's own names and imports, its prelude,
  and the keywords.

## 9. Semantic tokens

**Decision: the tokens are `torb highlight`'s, sharpened by the checker, in the legend the extension always declared.**
The syntax resolver of `../highlight` decides every name it can place from the file alone.
[RUST-EXIT.md](../RUST-EXIT.md) section 5.1 named the seam a checker would enter at, `memberTargetOf`, which answers "an
unknown receiver" for every `.name` whose receiver is not a type or a namespace of the file; the server answers that
case: the member behind a `.` is classified by the member the checker resolved - a `var` field is `mutable`, a method of
a type is a `method`, a case an `enumMember` - and a plain name the resolver placed nowhere gets the token of what it
resolved to. `torb highlight` stays as it is, for the extension where the server does not run and for anybody who reads
its JSON.

## 10. The client

**Decision: `lsp-client.js` speaks the protocol to `torb lsp` directly, without `vscode-languageclient`.** The extension
is a folder of plain files with no `node_modules` and no build step - `editors/vscode` is packed into a `.vsix` as it
is (`tools/package-extension.sh`) - and a client of the handful of messages this server answers is four hundred lines.
It syncs every open `.trb` file with incremental changes, turns what the server answers into VS Code's diagnostics,
hovers, locations, completion items, semantic tokens and code actions, reports file changes of the workspace, restarts a
crashed server up to three times in three minutes, and has a command to restart it by hand.
`torbscript.languageServer.enabled` turns it off; where it does not run, the TextMate grammar and `torb highlight` color
the code as they did before.

**Decision (2026-09-28): the extension is published, from `editors/vscode`, with the toolchain's version.** The
release workflow packs it with every release and publishes it to the Visual Studio Marketplace and to Open VSX as
`torbscript.torbscript`; a nightly carries a pre-release `.vsix` that no store gets (RELEASE.md section 13). An extension
from a store may run where no `torb` is, so it looks for one in the places the installers write as well, offers the
official installer in a terminal the person sees where it finds none, and opens a walkthrough on its first start; its
Test Explorer lists the tests of `torbscript/tests` and runs them with `torb test --report json`
(`editors/vscode/CONTRIBUTING.md`). The repository recommends the published extension in `.vscode/extensions.json`,
and `.vscode/launch.json` runs the working copy in an Extension Development Host.

## 11. What is left

- **A body instead of a file.** A keystroke still checks every body of its file, which is all of the 1.5 s of the
  largest file of the compiler. TYPECHECKER.md 7.3 says a body is a pure function of the signatures and one edit
  invalidates one body; what stands in the way is that the tables of the other bodies are keyed by spans that moved.
  Rebasing the spans behind the edit, or keying the tables by a body and an offset inside it, is the next lever.
- **The program shape and the implementation index per run.** About 100 ms of a keystroke in a file of the compiler are
  passes over the whole program that a change of one body cannot change.
- **Hover from the model of `torb doc`**, with its links resolved and rendered, and with the instantiated type of a
  generic call beside the declaration.
- **Requests not answered yet**: `completionItem/resolve` (a signature and a doc comment for every item),
  `textDocument/signatureHelp`, `references`, `rename`, `documentSymbol`, `workspace/symbol`, and
  `textDocument/formatting` from `torb format` (the VS Code extension formats through `torb format` over a temporary
  copy until then).
- **The workspace**: only the first workspace folder is read; a file deleted on disk stays in the tree until the server
  starts again; the diagnostics of files that are not open are not published.
- **The brand of the published extension**: its icon, gallery banner and walkthrough images are placeholders
  (`editors/vscode/CONTRIBUTING.md`, "The brand").
