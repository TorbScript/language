# The language server

**Status: the second round is implemented** (2026-09-28, milestone 8 of [ROADMAP.md](../ROADMAP.md)). `torb lsp` speaks
the Language Server Protocol over standard input and output (`compiler/src/language-server/`): the life cycle,
incremental document sync, the diagnostics of `torb check` with their ranges - for the open documents while they are
typed, and for every other file of the workspace in the background - hover with the documentation `torb doc` builds,
go to definition, the references, a rename that is checked before it is made, completion with the documentation of each
item, signature help, the symbols of a document and of the workspace, the formatting of `torb format`, semantic tokens,
and the fixes of `torb lint` as quick fixes. A keystroke inside of a function checks that function and keeps the check
of the rest of the file (section 7). Every folder of a workspace is a project of its own (section 13). Work that takes
longer than a message - the check of the workspace, the references in files nobody opened, a rename - runs in steps
between the messages and can be cancelled (sections 2 and 3). The VS Code extension in `editors/vscode` starts it
(`lsp-client.js`), published to the Visual Studio Marketplace and Open VSX with every release (section 14).
`compiler/tests/language-server.test.trb` holds sessions fed to the server in memory, `tests/lsp/` whole sessions over
standard input and output (`tools/lsp.sh`, a gate of tier A), and `sh tools/vscode-test.sh language-server` every
feature in a real VS Code. `torbscript/tests`, a request of the server's own (`experimental.tests` among the
capabilities), answers the tests and groups of a file from its syntax tree alone, for the list of tests of an editor
([torb lsp](../tooling/torb-lsp.md)). Section 15 is what is left.

The roadmap says it in one line: *the language server is the compiler* - `check` incremental per body, and the
checker's tables answer hover, go to definition, completion, the references and diagnostics; its semantic tokens
replace `torb highlight`, a lint fix is its quick fix, `torb format` its formatting and `torb doc` its hover. This
record says how, and why each decision went the way it did.

```text
   editor ──stdin──► a task: bytes to message bodies (protocol.trb) ──┐
                                                                      ▼
                     a channel of events: the messages of a chunk, the server's own tick, the clock's timer
                                                                      │
                     LanguageServer.receive (server.trb) ◄────────────┤  documents, changed paths, requests
                     LanguageServer.step                 ◄────────────┘  one step of the work between messages
                            │
                            ▼
                     a view per open document (view.trb): the run that checked all of its bodies, the run that
                            │   checked the bodies changed since, and where the text of the one lies in the other
                            ▼
                     diagnostics, hover, definition, references, rename, completion, signature help, symbols,
                            │   formatting, tokens, quick fixes - and, in steps, the check of the workspace
                            ▼
   editor ◄──stdout── framed JSON (command.trb)
```

- **[1. The server is a subcommand of the compiler](#1-the-server-is-a-subcommand-of-the-compiler)**
- **[2. Standard input is read by a task](#2-standard-input-is-read-by-a-task)**
- **[3. The server is a value without a transport](#3-the-server-is-a-value-without-a-transport)**
- **[4. Positions, URIs and JSON](#4-positions-uris-and-json)**
- **[5. What a run checks](#5-what-a-run-checks)**
- **[6. The front of the last run](#6-the-front-of-the-last-run)**
- **[7. A keystroke checks the bodies it changed](#7-a-keystroke-checks-the-bodies-it-changed)**
- **[8. Diagnostics, of the open documents and of the workspace](#8-diagnostics-of-the-open-documents-and-of-the-workspace)**
- **[9. Hover, definition, completion and signature help](#9-hover-definition-completion-and-signature-help)**
- **[10. References and a checked rename](#10-references-and-a-checked-rename)**
- **[11. Symbols and formatting](#11-symbols-and-formatting)**
- **[12. Semantic tokens](#12-semantic-tokens)**
- **[13. A project per folder](#13-a-project-per-folder)**
- **[14. The client](#14-the-client)**
- **[15. What is left](#15-what-is-left)**

## 1. The server is a subcommand of the compiler

**Decision: `torb lsp`, in `compiler/src/language-server/`, is the same binary as every other command.** CONCEPT's
single-binary toolchain names the language server among its parts, and there is nothing a second binary would do better:
the server needs the lexer, the parser, the checker, the highlighter, the lint rules, the formatter and the model of
`torb doc`, which are the compiler. It adds no native, no flag the build uses and no syntax, so it needs no second commit
and no new seed (CLAUDE.md, "Seed and breaking changes"), and it does not branch on the operating system anywhere, so
the C of the compiler is the same for every target as before.

## 2. Standard input is read by a task

The base protocol frames a message with a byte count and no line break after the body, so it cannot be read with
`readLine()`: the line would end only with the next message's header, and a client that waits for the answer to
`initialize` before it sends anything else would wait forever.

**Decision: a task of its own reads `std/io`'s `standardInput()`, cuts the chunks into messages and hands the messages
of every chunk to a `Channel`; the server takes what the channel holds, one event at a time.** The reads are tasks
already (`standardRead` on a thread of the blocking pool), so nothing new was needed. The top level of `torb` does not
`await()` the server: an `await()` there would make the whole entry file the main task, and the VM that `torb run` and
`torb test` start refuses to run its scheduler from inside one. The entry starts the task and finishes;
`torb_scheduler_run` after the entry runs it, and the server ends the process itself with `Process.exit`
(docs/design/CONCURRENCY.md; the C main of every program that uses tasks has had that tail).

**The work between messages is done in steps, and a message never waits for more than one.** Three things feed the
channel: the reader with the messages of a chunk, the server itself with a *tick*, and a clock with a *timer* every
quarter of a second. After every event the server says whether a step is due (`nextStepAt`); where one is due now, it
lets every other ready task run first (`pause()`) - the reader among them, which puts what standard input brought meanwhile
into the channel - and then hands the channel its tick, behind those messages. So the messages that arrived while a step
ran are handled before the next step, a `$/cancelRequest` among them, and a change of a document is checked before the
check of the workspace takes its next module. A step that is due later - the check of the workspace begins a while after
the last change - waits for the clock's timer.

A step is a slice of a run of the checker - the statements of a module for 40 ms (`run.trb`) - or a part of a request:
the files an index still has to read, the files a round of the workspace parses before it builds its front. The alternative was to run the long work in a task on another worker and
answer the messages in parallel. A task that starts on another worker gets a copy of what it captures, and what the
long work needs is the whole program: its trees, its front, its checker. The steps keep everything in the one heap of
the server, and every task of it on one worker, and cost no copy at all.

Every task keeps its end of the channel for as long as the server runs: an end that is dropped is closed, and a closed
end closes the channel for everybody - a sleeping task that handed in one tick and dropped its end once ended the server.
Standard output is written through `standardOutput()`, which writes the bytes as they are - `\r\n` in a header stays
`\r\n` on every system, and a body is never split.

## 3. The server is a value without a transport

**Decision: `LanguageServer.receive(bodies)` takes the bodies of the messages that arrived together and answers every
message to send, in order; `LanguageServer.step()` does one step of the work between messages and answers what it
sends; `command.trb` is the only file that touches standard input or output.** The tests of
`compiler/tests/language-server.test.trb` build a workspace in memory, feed JSON texts to the server and compare what
comes back, without a process, a pipe or a file, and `settle()` runs every step there is, each as soon as it would be
due; `tests/lsp/` runs the same kind of session through the real transport, and its `await <n>` holds the rest of a
session back until the server has written `n` messages, for a session that tests the work between messages.

**The batch is the unit of work.** A change marks its document. The checker runs once at the end of the batch, or
earlier when a request of the batch is about a document whose text changed since its last run. A client that sends a
change per keystroke while a run is busy has those changes waiting in the channel, and they cost one run.

**A request whose answer takes steps is a job.** `workspace/symbol` while the index of the workspace is not whole,
`textDocument/references` where files nobody opened have the name, and every `textDocument/rename` are answered once
their steps are done; the requests that come meanwhile are answered as they come, so the answers are not in the order of
the requests, which the protocol allows. `$/cancelRequest` drops such a job and answers it with the error the protocol
has for it (`-32800`); every other request was answered before the next message was read, and there is nothing to
cancel. `shutdown` answers the jobs that wait first - their answers come before its own - and a rename whose documents
changed while it was checked answers `-32801`, because its edits would land somewhere else.

## 4. Positions, URIs and JSON

- **Positions are UTF-16, offsets are bytes.** The protocol's character counts UTF-16 code units, and so does VS Code;
  the compiler's spans are byte offsets. `LineMap` (`position.trb`) translates both ways: a character that UTF-8 writes
  in four bytes is two code units, every other one is one. The server announces `positionEncoding: utf-16`.
- **A URI below the root keeps the root's spelling.** VS Code writes `file:///c%3A/Projekte/...`, a lowercase drive and
  an escaped colon, and `File.absolutePath` spells the same path `C:/Projekte/...`. `Locations` (`uri.trb`) turns a URI
  below the root the client named into a path below the root the tree was read from, and a path back into the client's
  own spelling, so a location the server sends names the same document the client knows. With several folders, a path
  takes the spelling of the innermost folder that holds it. Everything else is a `file:` URI of the absolute path, with
  a drive letter recognized by its shape - text alone, the same on every system.
- **JSON is `std/json`.** A message is read with `Json().parse` into a `JsonValue` and written with `Json().encode`; the
  objects the server answers with are built from lists of entries, and a `Map` keeps the order of insertion, so the text
  of an answer is the same every time - which is what the expectations of the tests compare.
- **Changes are incremental** (`change: 2`): a range and its new text, or the whole text where no range is given.

## 5. What a run checks

**Decision: a run checks the documents that changed, every open document that imports one whose *interface* changed
(directly or through other modules, in the module graph of its last run), and a document a request is about that no run
has checked yet.** The interface of a document is what other bodies can see of it: its declarations and their
signatures, its `use`s, its top-level code - and, of every function whose body changed, the result the body infers and
what it does with the functions it is handed (section 7). A change of a body that leaves all of that alone checks that
body, and nothing that imports the document. `check` has the parameter this needs, `onlyNamed`: the paths it is given
are checked and reported, not the files they import through a relative path.

The program a run knows is the program of every open document (`onlyReached`, the packages their imports reach), so a
change of any open document finds its module in the last run's graph. The diagnostics of the files that are not open
come from the check of the workspace (section 8).

## 6. The front of the last run

`check` is two stages (`../semantics/check.trb`): **`frontOf`** - the workspace, the module graph, the names of every
module (`resolveNames`), the names in every type position (`resolveTypes`) - and **`checkFront`**, the type checker over
that. The front depends on the declarations and the imports of the files and not on their bodies.

**Decision: the server keeps the front of its last run, and where a change leaves a document's outline alone, it redoes
only that document's part of it** (`refreshedFront` in `analysis.trb`). The outline (`outlineOf`) is what the rest of
the program sees of a file: every name it declares with its kind and visibility, the cases of its types, every `use`
with its items, every script it loads - in order, without a position. With the same outline the file declares the same
symbols in the same order, and every other module's names still lead to them; only their positions moved. So the server
parses the file, declares its symbols once more on the side to read their new spans off, writes those spans into the
symbols the front already has - same ids, so nothing that points at them changes - and resolves the file's names in type
positions again.

A front is built from scratch when a document's outline changed, when a document was closed (its text goes back to the
disk's), when a watched file changed on disk, and after 200 runs: resolving a file's type positions declares its type
parameters again, so the symbols of the program grow by a few with every run until then.

**Parsing is kept in the tree.** Every run hands its parses to the tree (`SourceTree.withParsed`), and
`SourceTree.withFile` forgets the parse of the one file whose text it replaces, so even a front from scratch parses only
what changed. `SourceTree.withoutFile` forgets a file deleted on disk.

## 7. A keystroke checks the bodies it changed

TYPECHECKER.md 7.3 says a body is a pure function of the signatures, and one edit invalidates one body. What stood in
the way was that the tables of the other bodies are keyed by spans that moved.

**Decision: every open document has a view of two runs (`view.trb`), and a span that moved is translated where it is
asked about, never written into a table.** The **base** is a run that checked every body of the document, over the text
it had then. The **overlay** is a run over the current text that checked the bodies that changed since - and nothing
else of the document. A **mapping** (`mapping.trb`) says where the base's text lies in the current one: two parses are
compared declaration by declaration, in order; a declaration with the same text moved as a whole, a function with the
same text in front of its body and another body has a *changed body* - what stands in front of it moved, the body itself
has no counterpart, and its borders are each other's. The members of a type, a trait and an `extend` are compared the
same way behind a header that has to be the same. Anything else - a declaration more or less, a signature, a field, a
`use`, a statement of a script - and there is no mapping: the document is checked as a whole, and the documents that
import it with it (section 5).

The overlay is `checkBodiesWithin` of the checker (`checker/check.trb`): a checker over the current front, with the
declaration sites and the implementation index of the whole program built as `checkTypes` builds them, because a body
may call anything; then the bodies inside of the changed spans, the spelling of the module's names, the escapes of what
those bodies hand on and the bounds of their type positions. What it reports outside of the changed bodies - a callee's
body checked on demand - is the business of the base, and dropped.

Whatever a view answers is in the coordinates of the current text. The diagnostics are the base's outside of the
changed bodies, moved to where their code is now, and the overlay's inside of them, together with what the current front
says about the parse, the names and the type positions; the findings of the lint rules the same way. A question about a
position goes to the run that knows the code there - the overlay inside of a changed body, the base everywhere else,
through the mapping - and its answer comes back through the mapping. A span of another file in a run's text - a
definition, a link - is translated through a mapping of that file's two parses where its text changed since the run.

**What other bodies see of a body.** A body decides the result of a function that writes none, and whether the function
keeps what it is handed or hands it to a task (`escape.trb`), which decides what a closure at a call of it may capture.
The server compares both between the base and the overlay for every changed function (`bodyFactsOf`); where one differs,
the change is one of the interface after all, and the document is checked as a whole with its importers. Where they
agree, no other body can have changed: the check of every other body stays true.

**Why not re-key the tables.** Rebasing the spans of the base's tables behind an edit would keep one run per document,
and it would have to rebase every span-keyed structure of the checker - some eighty fields, the syntax trees of the
declaration sites, the spans of the checker's own symbols and bindings - and stay right as the checker grows. The views
use the checker as a black box: every run is a run of `check`, and each answers in its own coordinates.

**What a keystroke costs**, from the change to its diagnostics, measured over standard input and output with the
`torb` of the first round and the one of this round, one after the other, on the machine of
[PERFORMANCE.md](../PERFORMANCE.md) while other agents' builds ran beside it; the median of ten keystrokes that type a
word into the name of a local of a function in the middle of the file, the check of the workspace turned off, and the
pass `bodies` of the same runs:

| Document | Lines | First round | Its bodies | Second round | Its bodies |
|---|---:|---:|---:|---:|---:|
| `compiler/src/semantics/checker/expression.trb` | 4014 | 1505 ms | 1310 ms | 172 ms | 6 ms |
| `compiler/src/backend/c/body.trb` | 3252 | 449 ms | 254 ms | 143 ms | 12 ms |
| `compiler/src/semantics/checker/implementation.trb` | 3064 | 533 ms | 294 ms | 142 ms | 19 ms |
| `compiler/src/backend/c/emit.trb` | 2795 | 438 ms | 268 ms | 92 ms | 15 ms |
| `compiler/src/vm/interpret.trb` | 2531 | 428 ms | 248 ms | 242 ms | 109 ms |

On a quieter machine the first of them measured 1132 ms against 94 ms. What is left of a keystroke besides its body is
the part of the front of the one file (20-30 ms), the declaration sites and the implementation index of the whole
program (40-70 ms), the lint rules over the document and the diagnostics. `interpret.trb` is edited inside of `answer`,
a function of a hundred lines whose check - with the bodies it asks for on demand - is the 109 ms.

## 8. Diagnostics, of the open documents and of the workspace

- **A problem of `torb check` is an error** with the range of its span; its notes follow the message, a line each.
- **A finding of `torb lint` is a hint**, with the id of its rule as the code and `torb lint` as the source; a binding
  nothing reads carries the tag *unnecessary*, which an editor fades. A hint and not a warning, because the rules are
  the style the checker leaves alone and `torb lint` is no gate. The rules that read the syntax tree run wherever the
  file parses; the rules that read the checker's tables run where the checker found no problem, exactly as `torb lint`
  decides it.
- **A use of a deprecated declaration is a warning**, the one finding that is more than a hint: it is the warning
  `torb check` prints, with `torb` as the source and the tag *deprecated*, which an editor strikes through, and its
  fix - the replacement - is a quick fix (docs/design/DEPRECATION.md). It is found wherever the checker resolved the
  name, a file with a problem included, and like every other finding from the run that knows the code: the base's uses
  outside of the changed bodies, moved, and the overlay's inside of them (`checkBodiesOfFront` records the uses of the
  bodies it checked).
- **A quick fix is the fix of one finding** (`actions.trb`): the edits `torb lint --fix` writes, as a `quickfix` code
  action on every finding with a fix that touches the range asked about.

**Decision: the files nobody opened are checked in the background, and what `torb check` finds in them is published
too** (`workspace.trb`). A round checks a slice of a module per step on one checker (`ModuleRun` of `run.trb`:
`preparedChecker`, then per module `beginNextModule`, `continueModuleCheck` for 40 ms at a time and `finishModuleCheck`,
and the escapes settled as the last step, as `checkTypes` does it), over a front of every package of the project, whose
files it parses a dozen per step first. The first round begins a while after `initialized` and checks every
module below the root; after files change on disk (`workspace/didChangeWatchedFiles`, a file deleted included) a round
checks the files that changed and every module that imports one of them, directly or through others, and begins a while
after the last change, so a burst of changes - a branch checked out - costs one round. A change while a round runs
starts it again, with what it had still to check. A file is published after its module's step where what it has
differs from what was published, and once more after the escapes are settled; a file without a problem that never had
one is not published at all. An open document is left to its view, and closing it publishes what the workspace found
about it again. The lint rules are left to the open documents: they are a style `torb lint` reports on request.

The check is configurable: `workspaceDiagnostics` (on unless turned off) and `workspaceDiagnosticsDelay` (1000 ms), in
the `initializationOptions` of `initialize` and below `settings.torbscript.languageServer` of
`workspace/didChangeConfiguration`. Turned off, it clears what it published. The whole repository - some 690 files - is
one round of about sixteen seconds while the server answers a hover in 5 to 25 ms.

## 9. Hover, definition, completion and signature help

The tables are keyed by span ([TYPECHECKER.md](../TYPECHECKER.md) 7.3), so **what stands at a position is the smallest
span that holds it** (`lookup.trb`): of `a.b.c` with the cursor on `b`, that is the span of `a.b`, whose resolution is
the member `b`. The candidates are the resolutions of expressions, the names in type positions, the names of
declarations and the names of local bindings where they are bound; an expression's type answers where none of them does.

- **Hover shows the item `torb doc` builds** (`documentation.trb`, `itemAt` of `../reference/model.trb`): the
  declaration as the source writes it up to its body, the doc comment above its first heading, the parameters that carry
  a comment of their own, and its sections - `# Panics`, `# Related` - one model for the site, the doc tests and the
  hover. A link of the comment resolves the way a name at that place resolves, and `[Type.member]` to the member in the
  declaration of the type; it becomes a link to the place of the declaration (`file:///...#L12,5`), which an editor
  opens. At a call of a generic function the hover says what its type parameters are there (`Item` is `Int64` here),
  from the type arguments the checker recorded for the call. A deprecated declaration says so first - the reason, the
  version and what to write instead - from the clause the model carries. A local shows `const`/`var`, its name and
  its type; a constant its type, which its declaration need not write. The one difference to a page: the first
  declaration of a file without imports has the comment at the start of the file as its own, as the parser has it.
- **Go to definition** follows a resolution to the symbol it names and the symbol to its module and span; a local leads
  to where it is bound, a module to its first line.
- **Completion behind a `.`** looks for the smallest expression of the run that ends at the dot. Where the text around
  it does not parse, the server checks the text once more without the dot and what follows it on the line, which is the
  receiver alone - a view of that text, which takes the body path of section 7 and is kept by nobody. The receiver's
  type offers its fields, `copy` and the methods that take a receiver; a type offers its cases, constants and `static`
  members; a module its exports. **Anywhere else** the names in scope: the locals of the bodies around the cursor, read
  from the syntax tree, then the file's own names and imports, its prelude, and the keywords. An item of a declaration
  carries the place of its name (`data`), and `completionItem/resolve` answers it with the signature and the
  documentation of the hover; an item of a deprecated declaration carries the tag *deprecated*.
- **Signature help** (`signature-help.trb`) finds the innermost call of the syntax tree whose arguments hold the cursor,
  in either style - between the parentheses of `f(a, b)` or behind the name of `f a, b` - and shows what the checker
  resolved it to: a function by its signature, the constructor of a type and a case by their fields. The parameter is
  the one the argument's label names, or the one at the argument's position, counted by the commas in front of the
  cursor. Where the checker could not resolve the call yet, the callee's name is looked up in the file, and a member
  through the type of its receiver.

## 10. References and a checked rename

**Decision: a declaration is known by where its name is now** (`references.trb`). Runs over different texts give the
same declaration different ids - the checker's own symbols (fields, methods) are made in the order signatures are asked
for - and the place of the name in the current text is what they agree on. A run answers through two functions that turn
its ids into such places; the places it finds are in its own text, and move to where they are now.

- **The references** of a declaration are every place a run resolved to it: a call, a member behind a `.`, a name in a
  type position, the item of a `use`, the name of the declaration itself, the label of an argument that names a parameter
  of the function, and a link of a doc comment. A field is found by the rename of `torb rename` (`../lint/rename.trb`),
  which knows the labels of constructors, of `copy`, of cases and of patterns, a delegate, and a bare name inside of its
  type. The open documents answer from their views; every file nobody opened whose text has the name as a word is
  checked, a slice of a module per step, and the answer waits for them. A local is found in its document alone.
- **A rename is checked before it is made** (`rename.trb`). The files it could touch - every file whose text has the old
  name *or the new one* as a word - are checked before the edits and after them, and what each name there resolves to
  and what is wrong with each file are compared, the places moved by the edits. The rename is answered only where the
  two agree; where a name would resolve to something else - a local the new name shadows, a declaration it collides
  with, a member it would take over - or a problem would appear, it is refused with the file and the line. A file that
  has the old name and a problem before the rename refuses it too: where the checker could not resolve everything, a use
  might go unseen. A new name has to be a name of the language, no keyword, and start with the letter its kind is
  spelled with; a declaration outside of the folders of the workspace - the toolchain's `std` - is not renamed.
  `prepareRename` answers the name at the position and its range.

The alternative was to rename what the text and the scopes say - rust-analyzer's approach, with a list of conflicts to
look for. The check before and after is slower - two runs over the files that have either name - and it cannot miss a
conflict nobody thought of, because it asks the checker what every name means.

## 11. Symbols and formatting

- **`textDocument/documentSymbol`** reads the declarations of a document from its syntax tree - types with their fields,
  cases and members, traits, functions, constants, `extend`s - with the signature of each as its detail (`symbols.trb`),
  nested where the client says it can show them so, a flat list with each member's container otherwise.
- **`workspace/symbol`** answers from an index of the declarations of every file below the folders of the workspace,
  read once per text of a file and read again when it changes; while the index is not whole the files are read a few
  dozen per step. A name matches where it holds the query's characters in order, whatever their case: a name that starts
  with the query first, then one that holds it, then the rest, the shorter first among equals.
- **`textDocument/formatting`** is `formatted` of `torb format` (`../format/command.trb`) over the text the editor holds,
  answered as the edits between the two texts line by line (`formatting.trb`, Myers's shortest edit script) - never one
  edit of the whole text, which would move the cursor and every mark to its end. **`rangeFormatting`** formats the
  whole text, because the layout is one function of it - the width of a line decides where a call breaks - and keeps
  the edits that touch the lines of the range. A text that does not parse is left as it is.

## 12. Semantic tokens

**Decision: the tokens are `torb highlight`'s, sharpened by the checker, in the legend the extension always declared.**
The syntax resolver of `../highlight` decides every name it can place from the file alone. The member behind a `.` is
classified by the member the checker resolved - a `var` field is `mutable`, a method of a type is a `method`, a case an
`enumMember` - and a plain name the resolver placed nowhere gets the token of what it resolved to: the resolutions of the
base outside of the changed bodies, moved to where they are now, and those of the overlay inside of them. The legend's
one addition is the modifier `deprecated`, on the name of a deprecated declaration and on every use of one.

## 13. A project per folder

**Decision: every folder of the workspace is a project of its own** (`projects.trb`), read the way `torb check <folder>`
reads it, with its own tree, its own front, its own views, index and check of the workspace. A document belongs to the
innermost folder whose root holds it, or to a folder whose tree holds the file - the toolchain's `std` of a project
that has none of its own - and a file of no folder to a project of its own surroundings, which is not checked in the
background. A client that sends `workspaceFolders` names the folders; `rootUri` is the one folder of a client that does
not. The VS Code extension starts the server again when the folders change.

## 14. The client

**Decision: `lsp-client.js` speaks the protocol to `torb lsp` directly, without `vscode-languageclient`.** The extension
is a folder of plain files with no `node_modules` and no build step - `editors/vscode` is packed into a `.vsix` as it
is (`tools/package-extension.sh`). It syncs every open `.trb` file with incremental changes, turns what the server
answers into VS Code's diagnostics, hovers, locations, completion items, signature help, workspace edits, symbols,
semantic tokens and code actions, reports file changes of the workspace and the settings of the check of the workspace,
restarts a crashed server up to three times in three minutes, and has a command to restart it by hand. Format Document
and Format Selection ask the server where it runs; where it does not, `formatting.js` formats a copy of the whole text
with `torb format`. `torbscript.languageServer.enabled` turns the server off; where it does not run, the TextMate grammar
and `torb highlight` color the code as they did before.

**Decision (2026-09-28): the extension is published, from `editors/vscode`, with the toolchain's version.** The
release workflow packs it with every release and publishes a stable release's to the Visual Studio Marketplace and to
Open VSX as `torbscript.torbscript`; a nightly and a release candidate carry a pre-release `.vsix` that no store gets
(RELEASE.md section 13). An extension
from a store may run where no `torb` is, so it looks for one in the places the installers write as well, offers the
official installer in a terminal the person sees where it finds none, and opens a walkthrough on its first start; its
Test Explorer lists the tests of `torbscript/tests` and runs them with `torb test --report json`
(`editors/vscode/CONTRIBUTING.md`). The repository recommends the published extension in `.vscode/extensions.json`,
and `.vscode/launch.json` runs the working copy in an Extension Development Host.

**The second client is the playground (2026-09-29).** Its editor is Monaco with a bridge of its own - Monaco's
providers speaking JSON-RPC (`playground/editor/protocol.mjs`), where CodeMirror with `@codemirror/lsp-client` was
until 2026-09-30 - and the server is this one, `torb lsp` in the playground's WebAssembly, in a worker of the page:
the worker frames the editor's messages into the server's standard input and cuts what it writes into messages again.
Nothing of the server knows it runs in a page - the runtime's browser target makes a read of standard input that finds
nothing wait for the page, and the scheduler return to it, instead of blocking (RELEASE.md section 6, "The editor, the
language server and the gallery, as built" and "Monaco in every runnable block"; [the
playground](../tooling/the-playground.md)).

## 15. What is left

- **The passes over the whole program per keystroke.** The declaration sites and the implementation index of the
  overlay are built for the whole program on every keystroke - 40 to 70 ms of the ones above - though a change of a body
  cannot change them. They depend on the spans of the changed document, which is what keeps them from being reused.
- **The semantic tokens of a large document** are the resolver's over the whole file on every request, about 100 ms in
  the largest file of the compiler.
- **A document's check as a whole is one step**: the first check of a document, and one after a change of its interface,
  run before the next message is read - two to four seconds for the largest file of the compiler when it is opened. The
  check of the workspace is taken in slices of 40 ms (`ModuleRun`, `beginModuleCheck`/`continueModuleCheck`); a check
  of a document could be taken the same way, with the requests about it waiting for its last slice.
- **A rename of a name many files have** checks all of them twice, which takes seconds; the steps keep the server
  answering, and `$/cancelRequest` stops it.
- **The brand of the published extension**: its icon, gallery banner and walkthrough images are placeholders
  (`editors/vscode/CONTRIBUTING.md`, "The brand").
