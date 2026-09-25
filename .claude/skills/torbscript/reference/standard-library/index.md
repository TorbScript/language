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
`std/process`, `std/os`, `std/http`, `std/sandbox` and `Clock` stay explicit imports, so that
`use File from "std/fs"` at the top of a file is the statement "this file touches files".

## What belongs here

One page per package: what it is for, how it is imported, and every public declaration with its doc comment. The
`## Declarations` section of each page stands between generator markers, so that milestone 8's `torb doc` can fill it from
the sources. One reference page stands beside them, for the three closure aliases every package's signatures use.

What does not belong here: the language rules that a type participates in, which are in
[the language reference](../language/index.md), and the argument for a design decision, which is in
[explanation](../explanation/index.md). A page here says what a package contains.

<!-- torb:index:begin -->

## Pages

- **[std/core](core.md)** - The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.
- **[Predicate, Action and Transform](function-types.md)** - Three aliases in the prelude for the closure shapes signatures take most - a question about one value, an effect on one value, and a conversion of one value into another.
- **[std/text](text.md)** - Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
- **[std/number](number.md)** - Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
- **[std/collections](collections.md)** - The collection traits every signature talks about, and the implementations that only show up where one is built.
- **[std/iteration](iteration.md)** - Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
- **[std/encoding](encoding.md)** - Encode, Decode and Describe, the Encoder, Decoder and Describer a format implements, EncodedValue and Structure for a value or a structure without its type, and Format for the streaming side.
- **[std/expression](expression.md)** - Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
- **[std/task](task.md)** - Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
- **[std/parallel](parallel.md)** - parallel() and Parallel, a pipeline whose stages run on the workers of the pool with the results in input order, in scope through the prelude.
- **[std/console](console.md)** - print and printError, the two functions that write to the standard streams.
- **[std/linear](linear.md)** - Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
- **[std/geometry](geometry.md)** - The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
- **[std/json](json.md)** - Json, a value with the options of the format, for encoding and decoding any Encode/Decode type, and JsonValue for the rare document whose shape is not known ahead of time.
- **[std/yaml](yaml.md)** - Yaml reads and writes any Encode/Decode type as YAML 1.2 or 1.1, with the target type resolving every scalar, and YamlNode is the tree of a document with its anchors, tags, styles and comments.
- **[std/regex](regex.md)** - Regex, a compiled pattern with the syntax and the linear-time semantics of RE2, with whole and partial matches, named groups that decode into a type, replace and split.
- **[std/time](time.md)** - Instant and Duration, the two time values, plus Clock and sleep, which read and wait on the wall clock.
- **[std/path](path.md)** - Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
- **[std/uri](uri.md)** - Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
- **[std/fs](fs.md)** - File and IoError - whole-file helpers for what fits in memory, and a File as both ends of a byte stream.
- **[std/io](io.md)** - Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
- **[std/process](process.md)** - Process for arguments, exiting and running a program to its end as a task, Child for a running program's pipes, and ProcessOutput for what it left behind.
- **[std/os](os.md)** _(draft)_ - Environment, System and Directories - the environment a program was started with, which system and version it runs on, and where the user's files belong - with OsError and the three target constants.
- **[std/test](test.md)** - test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
- **[std/ip](ip.md)** - IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
- **[std/network](network.md)** - Name resolution and TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - over the address values of std/ip, which it re-exports.
- **[std/tls](tls.md)** - TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, and a stream like the TCP one.
- **[std/http](http.md)** - HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
- **[std/sandbox](sandbox.md)** - Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
- **[std/project](project.md)** - The receiver type of project.trb - Project, Dependencies, Build, Test and Workspace.
- **[std/prelude](prelude.md)** - The package of re-exports that is in scope in every file of a project, unless project.trb names another one.
- **[std/stream](stream.md)** - Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.

<!-- torb:index:end -->

