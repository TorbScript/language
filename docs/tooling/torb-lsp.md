---
title: torb lsp
summary: torb lsp is the language server - the compiler over the Language Server Protocol - with the diagnostics of torb check for the whole workspace, hover from torb doc, references, a checked rename, completion, formatting and lint fixes.
kind: tooling
status: stable
order: 47
keywords:
  - torb lsp
  - language server
  - LSP
  - editor
  - hover
  - completion
  - semantic tokens
  - references
  - rename
  - signature help
  - workspace diagnostics
source:
  - compiler/src/language-server/command.trb
  - compiler/src/language-server/server.trb
  - compiler/src/language-server/analysis.trb
  - compiler/src/language-server/view.trb
  - compiler/src/language-server/workspace.trb
  - compiler/src/language-server/references.trb
  - compiler/src/language-server/test-tree.trb
  - docs/design/LANGUAGE-SERVER.md
---

`torb lsp` is what an editor starts to understand TorbScript. It speaks the Language Server Protocol over standard input
and output, and it is the compiler: the diagnostics are the ones [`torb check`](torb-check.md) prints - for the open
documents as they are typed, and for every other file of the workspace in the background - a hover is what
[`torb doc`](torb-doc.md) says about a declaration, the references and a rename are what the checker resolved, the
formatting is [`torb format`](torb-format.md)'s, and the quick fixes are what [`torb lint --fix`](torb-lint.md) writes.
Nobody runs it by hand; the VS Code extension of this repository starts it, and so can any editor with a client of the
protocol ([Set up your editor](../how-to/set-up-your-editor.md)).

## Synopsis

```text
torb lsp            The language server, until the client sends `exit`
    --stdio         Accepted and ignored: standard input and output are the only transport
torb lsp --help     What it takes
```

## What it does

### The protocol

A message is a header of `Content-Length: <bytes>`, an empty line, then that many bytes of JSON-RPC 2.0. Positions are a
0-based line and a 0-based character that counts UTF-16 code units, which is what an editor written in JavaScript
counts; the server translates them to the byte offsets of the compiler and back. What it logs - one line per run of the
checker, with what each pass cost - goes to standard error, which an editor shows in its output panel.

| Message | What the server does |
|---------|----------------------|
| `initialize` | Reads every folder of `workspaceFolders` - or the one `rootUri` names - the way `torb check` reads it, each a project of its own, takes the settings of its `initializationOptions` (below), and answers what it does |
| `initialized` | Begins the check of the workspace, a while later |
| `textDocument/didOpen`, `didChange`, `didClose` | Holds the text of the document in place of the disk's; a change is a range and its new text, or the whole text |
| `textDocument/publishDiagnostics` | Sent after every run of the checker for every document it checked, and by the check of the workspace for the files nobody opened |
| `textDocument/hover` | The declaration under the cursor as `torb doc` shows it - its signature, its doc comment with links, its parameters and sections - and at a call of a generic function what its type parameters are there; a local with its type, or the type of the expression |
| `textDocument/definition` | Where the name under the cursor is declared, in any file of the program, the standard library included |
| `textDocument/references` | Every place the checker resolved to what the name under the cursor is about, in every file of the project; the files nobody opened are checked for it |
| `textDocument/prepareRename`, `rename` | The name under the cursor and its range; then the edits of every place that names it, once a check before and after them found that every name still means what it meant (below) |
| `textDocument/completion` | Behind `value.` the fields and methods of its type, behind `Type.` its cases and `static` members, behind `module.` its exports; anywhere else the locals, the names of the file, its prelude and the keywords |
| `completionItem/resolve` | The item with the signature and the documentation of its declaration |
| `textDocument/signatureHelp` | Inside the arguments of a call, the signature of what it calls and the parameter at the cursor |
| `textDocument/documentSymbol`, `workspace/symbol` | The declarations of a document, nested; the declarations of every file of the workspace whose name holds the query |
| `textDocument/formatting`, `rangeFormatting` | The edits of `torb format`, one per run of lines that changes; of a range, those that touch its lines |
| `textDocument/semanticTokens/full` | The tokens of `torb highlight`, with every member behind a `.` classified by the member the checker resolved |
| `textDocument/codeAction` | The fix of every finding of `torb lint`'s rules that touches the range, as a quick fix |
| `torbscript/tests` | The tests and groups of a file, from its syntax tree alone (below) |
| `workspace/didChangeWatchedFiles` | Reads the changed files again, unless they are open, forgets a deleted one, and checks them and their importers a while later |
| `workspace/didChangeConfiguration` | Takes the settings below `settings.torbscript.languageServer` |
| `$/cancelRequest` | Drops a request that is still waiting for its steps and answers it with `-32800` |
| `$/setTrace`, `textDocument/didSave` | Nothing |
| `shutdown`, then `exit` | Answers the requests that wait first, then `shutdown`; `exit` ends with exit code 0, and without `shutdown` with 1 |

A request before `initialize` is answered with the error `-32002`, a request the server does not know with `-32601`, a
rename that is refused with `-32803` and the reason as its message.

### Settings

The settings of the check of the workspace come in the `initializationOptions` of `initialize`, and again below
`settings.torbscript.languageServer` of `workspace/didChangeConfiguration`:

| Setting | Default | Meaning |
|---------|---------|---------|
| `workspaceDiagnostics` | `true` | Whether the files nobody opened are checked and their problems published |
| `workspaceDiagnosticsDelay` | `1000` | How long after `initialized`, or after the last change on disk, a check of the workspace begins, in milliseconds |

### Diagnostics

Every problem `torb check` reports for a document is an error with the range of its span, and its notes follow the
message on lines of their own. Every finding of the rules of `torb lint` is a hint that names its rule as the code and
`torb lint` as the source; a binding nothing reads is marked as unneeded, which an editor fades. The rules that read the
checker's tables only run on a document the checker found no problem in, as `torb lint` does. The one rule that is
more than a hint is `deprecated`: a use of a deprecated declaration is the warning `torb check` prints, so it is a
warning with `torb` as the source and the tag `Deprecated`, which an editor strikes through, and its fix is a quick fix
([Deprecation](../language/modules-and-packages/deprecation.md)). The same declaration and every use of it carry the
semantic token modifier `deprecated`, a hover says it above the doc comment, and a completion item carries the tag.

The files nobody opened are checked in the background: every module of the project a while after `initialized`, and
after files change on disk the ones that changed and every module that imports one of them. The server answers the
messages that arrive while it checks - it checks for a few dozen milliseconds at a time - and publishes the problems of each file
where they are not what it published before; a file that never had one is not published at all. The lint rules are
left to the open documents. Closing a document publishes what the check of the workspace found about it again.

### The tests of a file

`torbscript/tests` is a request of this server's own, which `initialize` announces as `"experimental":{"tests":true}`
among the capabilities: an editor's list of tests asks it for every `*.test.trb` of the workspace. Its parameters name
a document, `{"textDocument":{"uri":"<file uri>"}}`, and the answer is an array of the tests and groups of the file in
source order, each an object of five keys:

| Key | What it holds |
|-----|---------------|
| `kind` | `group` or `test` |
| `name` | What the string literal of the name reads, escapes decoded: the name the report of `torb test` prints |
| `range` | The whole call, from `test` or `group` to the end of its closure |
| `selectionRange` | The string literal of the name, its quotes included |
| `children` | The tests and groups inside of a group, the same way; `[]` for a test |

A test is a call of the bare name `test` or `group` in any style - `test "name" { ... }`, `test("name") { ... }`,
`test("name", { ... })` - whose first argument is a string literal without interpolation. Every statement and
expression of the file is looked at, closures and the bodies of `if`, `for` and `while` included, except the body of a
`test` and the body of a function the file declares, whose tests belong to whatever group it is called in. A call
whose name is interpolated or no literal is left out with everything inside it: its name is known when it runs, and
`torb test --report json` names it then ([torb test](torb-test.md)).

It costs a parse and no run of the checker, so it answers for a file nobody opened as fast as for an open one: the text
is the open document's where the client opened it, and otherwise what the disk holds now. A URI that is no file, and a
file that cannot be read, answer `[]` rather than an error.

For this file, whose second test is written in parentheses:

```trb check
use test, group from "std/test"

group "Größe" {
  test "zählt 😀" {
    assert(1 + 1 == 2)
  }

  test("in parentheses", { assert true })
}

test "top level" {
  assert true
}
```

the request and its answer are these (`tests/lsp/tests.lsp` is this session):

```text
--> {"jsonrpc":"2.0","id":2,"method":"torbscript/tests","params":{"textDocument":{"uri":"file:///work/app/math.test.trb"}}}
<-- {"jsonrpc":"2.0","id":2,"result":[{"kind":"group","name":"Größe","range":{"start":{"line":2,"character":0},"end":{"line":8,"character":1}},"selectionRange":{"start":{"line":2,"character":6},"end":{"line":2,"character":13}},"children":[{"kind":"test","name":"zählt 😀","range":{"start":{"line":3,"character":2},"end":{"line":5,"character":3}},"selectionRange":{"start":{"line":3,"character":7},"end":{"line":3,"character":17}},"children":[]},{"kind":"test","name":"in parentheses","range":{"start":{"line":7,"character":2},"end":{"line":7,"character":41}},"selectionRange":{"start":{"line":7,"character":7},"end":{"line":7,"character":23}},"children":[]}]},{"kind":"test","name":"top level","range":{"start":{"line":10,"character":0},"end":{"line":12,"character":1}},"selectionRange":{"start":{"line":10,"character":5},"end":{"line":10,"character":16}},"children":[]}]}
```

### A rename is checked

A rename edits every place that names the declaration - the places the references are - and only after a check of every
file whose text has the old name or the new one as a word, before the edits and after them, found that every name there
resolves to what it resolved to before and no file has a problem it did not have. Where that is not so, the rename is
refused and the reason names the file and the line: a local the new name shadows, a declaration it collides with, a
problem it brings. A file that has the old name and a problem before the rename refuses it as well, because a use in it
might go unseen. A new name has to be a name of the language, no keyword, and start with the letter its kind is spelled
with - an uppercase one for a type, a trait, an alias and a case, a lowercase one for everything else; a declaration
outside of the folders of the workspace is not renamed.

### What a keystroke costs

A change marks its document; the checker runs once the messages that arrived together are handled, or earlier where a
request among them needs the answer, so a client that sends a change per keystroke while the checker is busy has them
checked together.

A keystroke inside of a function - the usual one - checks that function again and keeps what the last run found about
every other body of the file; the documents that import it are not checked again. A change of a declaration, a
signature or a `use`, or of a body that now infers another result, checks the document as a whole and every open
document that imports it, and parses only what changed even then. Measured on the machine of
[PERFORMANCE.md](../PERFORMANCE.md), from the change to its diagnostics: 90 to 250 ms in the largest files of the
compiler, where a check of the whole file took up to 1.5 s ([the design record](../design/LANGUAGE-SERVER.md), section
7).

### Exit codes

`0` after `shutdown` and `exit`. `1` after an `exit` that came without `shutdown`, or where standard input ended before
`exit`. `2` for an argument it does not take.

## Examples

A session as it goes over the wire, without the headers:

```text
--> {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///work/app","capabilities":{}}}
<-- {"jsonrpc":"2.0","id":1,"result":{"capabilities":{"positionEncoding":"utf-16", ...}}}
--> {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///work/app/main.trb", ...}}}
<-- {"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":"file:///work/app/main.trb","diagnostics":[]}}
--> {"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///work/app/main.trb"},"position":{"line":5,"character":7}}}
<-- {"jsonrpc":"2.0","id":2,"result":{"contents":{"kind":"markdown","value":"```trb\nfn double(value: Int): Int\n```\n\nTwice the value."}, ...}}
--> {"jsonrpc":"2.0","id":3,"method":"shutdown","params":null}
<-- {"jsonrpc":"2.0","id":3,"result":null}
--> {"jsonrpc":"2.0","method":"exit","params":null}
```

## Related

- [Set up your editor](../how-to/set-up-your-editor.md) - the VS Code extension, and the settings of another editor.
- [The language server](../design/LANGUAGE-SERVER.md) - the design record: what a run keeps, and what is left.
- [torb check](torb-check.md) - the diagnostics, on the command line.
- [torb lint](torb-lint.md) - the rules whose fixes are the quick fixes.
