---
title: torb lsp
summary: torb lsp is the language server - the compiler over the Language Server Protocol on standard input and output - with the diagnostics of torb check, hover, go to definition, completion, semantic tokens and lint fixes as quick fixes.
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
source:
  - compiler/src/language-server/command.trb
  - compiler/src/language-server/server.trb
  - compiler/src/language-server/analysis.trb
  - compiler/src/language-server/test-tree.trb
  - docs/design/LANGUAGE-SERVER.md
---

`torb lsp` is what an editor starts to understand TorbScript. It speaks the Language Server Protocol over standard input
and output, and it is the compiler: the diagnostics are the ones [`torb check`](torb-check.md) prints, the types a hover
shows are the checker's, and the quick fixes are what [`torb lint --fix`](torb-lint.md) writes. Nobody runs it by hand;
the VS Code extension of this repository starts it, and so can any editor with a client of the protocol ([Set up your
editor](../how-to/set-up-your-editor.md)).

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
| `initialize` | Reads the workspace below `rootUri` (or the first of `workspaceFolders`) the way `torb check` reads it, and answers what it does |
| `initialized`, `$/cancelRequest`, `$/setTrace` | Nothing |
| `textDocument/didOpen`, `didChange`, `didClose` | Holds the text of the document in place of the disk's; a change is a range and its new text, or the whole text |
| `textDocument/publishDiagnostics` | Sent after every run of the checker, for every document it checked |
| `textDocument/hover` | The declaration under the cursor with its signature and doc comment, a local with its type, or the type of the expression |
| `textDocument/definition` | Where the name under the cursor is declared, in any file of the program, the standard library included |
| `textDocument/completion` | Behind `value.` the fields and methods of its type, behind `Type.` its cases and `static` members, behind `module.` its exports; anywhere else the locals, the names of the file, its prelude and the keywords |
| `textDocument/semanticTokens/full` | The tokens of `torb highlight`, with every member behind a `.` classified by the member the checker resolved |
| `textDocument/codeAction` | The fix of every finding of `torb lint`'s rules that touches the range, as a quick fix |
| `torbscript/tests` | The tests and groups of a file, from its syntax tree alone (below) |
| `workspace/didChangeWatchedFiles` | Reads the changed files again, unless they are open |
| `shutdown`, then `exit` | Ends with exit code 0; `exit` without `shutdown` ends with 1 |

A request before `initialize` is answered with the error `-32002`, a request the server does not know with `-32601`.

### Diagnostics

Every problem `torb check` reports for a document is an error with the range of its span, and its notes follow the
message on lines of their own. Every finding of the rules of `torb lint` is a hint that names its rule as the code and
`torb lint` as the source; a binding nothing reads is marked as unneeded, which an editor fades. The rules that read the
checker's tables only run on a document the checker found no problem in, as `torb lint` does.

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

### What a keystroke costs

A change marks its document; the checker runs once the messages that arrived together are handled, or earlier where a
request among them needs the answer, so a client that sends a change per keystroke while the checker is busy has them
checked together. A run checks the documents that changed and every open document that imports one of them - their
bodies, and the bodies of nothing else.

Where a change leaves the declarations and the imports of a document alone - the usual keystroke - the server keeps
everything of the last run that does not depend on bodies and redoes the part of that one document. A change that adds,
renames or removes a declaration or a `use` builds that part from scratch, and parses only what changed even then.
Measured on the machine of PERFORMANCE.md, from the change to its diagnostics: about 100 ms in a
file of `std`, 170 to 230 ms in a typical file of the compiler, and 1.5 s in the largest one, whose bodies alone take
1.35 s to check.

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

`tests/lsp/` holds whole sessions like this one, which `sh tools/lsp.sh` pipes into `torb lsp` and compares with what it
writes; `compiler/tests/language-server.test.trb` holds more of them, fed to the server without the transport.

## Related

- [Set up your editor](../how-to/set-up-your-editor.md) - the VS Code extension, and the settings of another editor.
- The language server - the design record: what a run keeps, and what is left.
- [torb check](torb-check.md) - the diagnostics, on the command line.
- [torb lint](torb-lint.md) - the rules whose fixes are the quick fixes.

