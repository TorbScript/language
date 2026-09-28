---
title: TorbScript
summary: A single-language ecosystem - one functional-first language with value semantics for scripts, configuration, servers and tools, interpreted while you write it and native when it ships.
kind: site
status: stable
order: 10
source:
  - docs/guide/the-language-in-sixty-seconds.md
  - docs/tooling/torb-run.md
  - docs/tooling/project-trb.md
  - docs/tooling/the-torb-command.md
---

## Values, not references

Every type is a value, and the binding decides whether it changes. Assigning, passing and capturing a value is a copy,
so two names never share one thing, and a change happens exactly where it is written.

A list or a string shares its storage until one side writes to it, so the copy costs nothing until then. See
[why values instead of references](../explanation/why-values-instead-of-references.md).

```trb run
const fixed = [1, 2]
var buffer = fixed
buffer.append 3
print "{fixed} {buffer}"
// prints [1, 2] [1, 2, 3]
```

## No null and no exceptions

Absence is an `Option` and failure is a `Result`. The postfix `?` returns a failure to the caller, and a `match` covers
every case or the program does not compile.

```trb
use File from "std/fs"

fn firstLine(path: String): Result<String, Error> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => line
    None => ""
  }
}
```

## Interpreted while you work, native when you ship

`torb run` checks a program and runs it at once in the VM inside `torb`, with no C compiler. `torb build` compiles the
same program through C into a native binary. The conformance suite holds every program to the same output, exit code
and panics both ways.

```trb run
const name = "World"
print "Hello, {name}!"
// prints Hello, World!
```

```console
$ torb run hello.trb
Hello, World!
$ torb run --native hello.trb
Hello, World!
```

## One language for the code and its configuration

The manifest of a package is TorbScript as well: `project.trb` is checked against the type `Project` of the standard
library, so a setting is a field and a section is a block, with the checker and the editor of every other file. See
[project.trb](../tooling/project-trb.md).

```trb fragment
name = "acme/shop"
version = "1.0.0"

dependencies {
  runtime "acme/http:^1.2.3"
  development "acme/mock-server:^3.4.5"
}

program "migrate", entry: "tools/migrate.trb"
```

## One toolchain

`torb` is a single binary: the compiler, the VM, the test runner, the formatter, the language server and the package
manager. One installation and one manifest are the whole setup of a project. See
[the torb command](../tooling/the-torb-command.md).

```text
torb run       Run a program in the VM, or natively
torb test      Run the tests below a path
torb build     Compile a program into a native binary
torb format    Write the layout of the language
torb lsp       The language server, for an editor
torb publish   Check a package and publish it
```
