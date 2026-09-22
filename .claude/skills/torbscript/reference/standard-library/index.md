---
title: The standard library
summary: One page per package of std, what each contains, and which of them are in scope everywhere without an import.
kind: index
status: stable
order: 30
---

The standard library is a set of packages owned by `std`: `std/core`, `std/text`, `std/collections` and the rest. They
come with the toolchain and have its version, so none of them needs an entry in `dependencies`.

The **prelude** (`std/prelude`) is a package of re-exports whose public names are in scope in every file. It holds the
pure part of the library - values, text, numbers, collections, pipelines, encoding, quotations, tasks, printing,
mathematics, JSON and the time values. What a program can *touch* is deliberately not in it: `std/fs`, `std/io`,
`std/process`, `std/environment`, `std/http`, `std/sandbox` and `Clock` stay explicit imports, so that
`use File from "std/fs"` at the top of a file is the statement "this file touches files".

## What belongs here

One page per package: what it is for, how it is imported, and every public declaration with its doc comment. The
`## Declarations` section of each page stands between generator markers, so that milestone 8's `torb doc` can fill it from
the sources.

What does not belong here: the language rules that a type participates in, which are in
[the language reference](../language/index.md), and the argument for a design decision, which is in
[explanation](../explanation/index.md). A page here says what a package contains.

<!-- torb:index:begin -->

## Pages

- **[std/core](core.md)** - The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.
- **[std/text](text.md)** - Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
- **[std/number](number.md)** - Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
- **[std/collections](collections.md)** - The collection traits every signature talks about, and the implementations that only show up where one is built.
- **[std/iteration](iteration.md)** - Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
- **[std/encoding](encoding.md)** - Encode and Decode, the Encoder and Decoder a format implements, and Format for the streaming side.
- **[std/expression](expression.md)** - Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
- **[std/task](task.md)** _(planned)_ - Task and Channel, the two shared types that connect concurrent work, and spawn - designed, but not run by any back end yet.
- **[std/console](console.md)** - print and printError, the two functions that write to the standard streams.
- **[std/math](math.md)** - The functions on Float that read as an operation rather than a method, under the math namespace import.
- **[std/linear](linear.md)** - Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
- **[std/geometry](geometry.md)** - The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
- **[std/json](json.md)** - Json for encoding and decoding, and JsonValue for the rare document whose shape is not known ahead of time.
- **[std/time](time.md)** - Instant and Duration, the two time values, plus Clock and sleep, which read and wait on the wall clock.
- **[std/path](path.md)** - Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
- **[std/fs](fs.md)** - File and IoError - whole-file helpers for what fits in memory, and a File as both ends of a byte stream.
- **[std/io](io.md)** - Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
- **[std/process](process.md)** - Process for arguments and exiting, Child for a running program's pipes, and ProcessOutput for what it left behind.
- **[std/environment](environment.md)** - Environment, the one type that reads a process environment variable.
- **[std/test](test.md)** - test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
- **[std/http](http.md)** - A minimal HTTP client - get, post and request answer a Task, and a response body is a stream of any size.
- **[std/sandbox](sandbox.md)** - Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
- **[std/project](project.md)** - The receiver type of project.trb - Project, Dependencies, Build, Test and Workspace.
- **[std/prelude](prelude.md)** - The package of re-exports that is in scope in every file of a project, unless project.trb names another one.
- **[std/stream](stream.md)** - Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.

<!-- torb:index:end -->

