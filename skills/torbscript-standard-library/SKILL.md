---
name: torbscript-standard-library
description: "Looks up the TorbScript standard library: which `std/` package has a type or function, how it is imported and its exact API - text, numbers, collections, files and paths, JSON and YAML, regex, time, processes, hashing, compression and more. Use it when TorbScript code imports from `std/` or needs a library function, before guessing a name."
license: MIT
compatibility: "Reference only. Verifying code needs the torb command, which the torbscript skill checks and installs."
---

# The TorbScript standard library

The `torbscript` skill has the language; this skill has the packages of `std`, except the ones for testing, concurrency,
networking and project files, which have skills of their own, named below where their package is. Paths are relative
to the directory of this file.

Never guess a name: find the package here, then check the code with `torb check`. A command-line program reaches for
`std/process` (arguments, exit code, running a program), `std/io` (standard input), `std/fs` and `std/path` (files),
and `std/os` (the environment); [reading a file](references/how-to/read-a-file.md) and
[reading and writing JSON](references/how-to/read-and-write-json.md) are recipes with a complete program each.

## The packages

The standard library is a set of packages owned by `std`: `std/core`, `std/text`, `std/collections` and the rest. They
come with the toolchain and have its version, so none of them needs an entry in `dependencies`.

The **prelude** (`std/prelude`) is a package of re-exports whose public names are in scope in every file. It holds the
pure part of the library - values, text, numbers, collections, pipelines, encoding, quotations, tasks, printing,
mathematics, JSON and the time values. What a program can *touch* is deliberately not in it: `std/fs`, `std/io`,
`std/process`, `std/os`, `std/http`, `std/sandbox` and `Clock` stay explicit imports, so that
`use File from "std/fs"` at the top of a file is the statement "this file touches files".

### What belongs here

One page per package: what it is for, how it is imported, and every public declaration with its doc comment. The
`## Declarations` section of each page is written by hand; the reference generated from the sources, with every
signature and doc comment of a package, is what `torb doc std` (skill `torbscript-projects`: `references/tooling/torb-doc.md`) writes. One reference page
stands beside them, for the three closure aliases every package's signatures use.

What does not belong here: the language rules that a type participates in, which are in
the language reference (skill `torbscript-language`: `references/language/index.md`), and the argument for a design decision, which is in
explanation (skill `torbscript-language`: `references/explanation/index.md`). A page here says what a package contains.

### Pages

- **[std/core](references/standard-library/core.md)** - The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.
- **[Predicate, Action and Transform](references/standard-library/function-types.md)** - Three aliases in the prelude for the closure shapes signatures take most - a question about one value, an effect on one value, and a conversion of one value into another.
- **[std/text](references/standard-library/text.md)** - Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
- **[std/number](references/standard-library/number.md)** - Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
- **[std/collections](references/standard-library/collections.md)** - The collection traits every signature talks about, and the implementations that only show up where one is built.
- **[std/iteration](references/standard-library/iteration.md)** - Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
- **[std/encoding](references/standard-library/encoding.md)** - Encode, Decode and Describe, the Encoder, Decoder and Describer a format implements, EncodedValue and Structure for a value without its type, Format for streaming, and RFC 4648's Base64, Base32 and hexadecimal for bytes as text.
- **[std/expression](references/standard-library/expression.md)** - Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
- **std/task (skill `torbscript-concurrency`: `references/standard-library/task.md`)** - Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
- **std/parallel (skill `torbscript-concurrency`: `references/standard-library/parallel.md`)** - parallel() on anything that can be iterated, Parallel, a pipeline whose fused stages run on the workers of the pool with the results in input order, and Cut, the collections that cut themselves.
- **[std/console](references/standard-library/console.md)** - print and printError, the two functions that write to the standard streams.
- **[std/linear](references/standard-library/linear.md)** - Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
- **[std/geometry](references/standard-library/geometry.md)** - The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
- **[std/json](references/standard-library/json.md)** - Json, a value with the options of the format, for encoding and decoding any Encode/Decode type, and JsonValue for the rare document whose shape is not known ahead of time.
- **[std/yaml](references/standard-library/yaml.md)** - Yaml reads and writes any Encode/Decode type as YAML 1.2 or 1.1, with the target type resolving every scalar, and YamlNode is the tree of a document with its anchors, tags, styles and comments.
- **[std/regex](references/standard-library/regex.md)** - Regex, a compiled pattern with the syntax and the linear-time semantics of RE2, with whole and partial matches, named groups that decode into a type, replace and split.
- **[std/markdown](references/standard-library/markdown.md)** - Markdown reads CommonMark with GitHub's tables and front matter into a document tree that is a value, with the lines of every block and link; HTML and Markdown are written from the tree.
- **[std/time](references/standard-library/time.md)** - Instant, Duration and Timestamp, the three time values, plus Clock and sleep, which read the two clocks and wait on them.
- **[std/path](references/standard-library/path.md)** - Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
- **std/uri (skill `torbscript-networking`: `references/standard-library/uri.md`)** - Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
- **[std/fs](references/standard-library/fs.md)** - File and IoError - whole files as text or bytes, a File as both ends of a byte stream, and the tree around them - remove, rename, move, copy, metadata, links, temporary files and atomic replacement.
- **[std/resource](references/standard-library/resource.md)** - Resource, EmbeddedBytes and EmbeddedText - a file of the package named by a string literal, resolved by the compiler where it is written; reading the bytes comes with the next slices.
- **[std/storage](references/standard-library/storage.md)** - Storage, a capability over a Uri whose scheme chooses the driver - FileStorage for files, MemoryStorage for tests - and Storage.registry, one storage over the drivers a program names.
- **[std/io](references/standard-library/io.md)** - Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
- **[std/process](references/standard-library/process.md)** - Process for arguments, exiting and running a program to its end as a task, Child for a running program's pipes, and ProcessOutput for what it left behind.
- **[std/os](references/standard-library/os.md)** _(draft)_ - Environment, System, Directories and Entropy - the environment a program was started with, which system and version it runs on, where the user's files belong, and the system's randomness - with OsError and the target constants.
- **std/test (skill `torbscript-testing`: `references/standard-library/test.md`)** - test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
- **std/ip (skill `torbscript-networking`: `references/standard-library/ip.md`)** - IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
- **std/dns (skill `torbscript-networking`: `references/standard-library/dns.md`)** - Domain names with IDNA and the comparison of RFC 4343, DNS records as typed cases, and the RFC 1035 wire format with compression and EDNS0 - values with no natives and no capability, for the transports of std/network and std/http.
- **std/network (skill `torbscript-networking`: `references/standard-library/network.md`)** - Name resolution and DNS lookups, TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - and UDP datagrams, over the address values of std/ip, which it re-exports.
- **std/tls (skill `torbscript-networking`: `references/standard-library/tls.md`)** - TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, a stream like the TCP one, and DNS over TLS.
- **std/http (skill `torbscript-networking`: `references/standard-library/http.md`)** - HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
- **[std/binary](references/standard-library/binary.md)** - ByteReader, ByteWriter, BitReader and BitWriter - numbers of every width in either byte order, IEEE 754 floats, LEB128, runs of bytes and text, read with a ReadError instead of a panic and written into a growable buffer.
- **[std/digest](references/standard-library/digest.md)** - Sha256, Sha512, Sha1 and Md5 and the Digest they answer, Hmac (RFC 2104) and PBKDF2 (RFC 8018) over any of them - fed at once or in pieces, shown as lowercase hexadecimal.
- **[std/compression](references/standard-library/compression.md)** - DEFLATE and gzip in TorbScript - inflated and gunzipped read every stream the formats allow, deflated and gzipped write deterministic output, and crc32 is the checksum gzip uses.
- **[std/archive](references/standard-library/archive.md)** - tar archives - untarred reads what GNU tar, bsdtar and pax write, with modes and links; tarred writes ustar and pax; extract of std/archive/extract unpacks safely.
- **[std/signature](references/standard-library/signature.md)** - Ed25519 of RFC 8032 in TorbScript - a private key from a 32-byte seed signs, a public key verifies, and keys and signatures are capsules that read and write themselves as bytes and hexadecimal.
- **[std/sandbox](references/standard-library/sandbox.md)** - Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
- **std/project (skill `torbscript-projects`: `references/standard-library/project.md`)** - The receiver type of project.trb - Project, Dependencies, Program, Profile, Test, Tasks, Workspace, Registry and Source.
- **[std/prelude](references/standard-library/prelude.md)** - The package of re-exports that is in scope in every file of a project, unless project.trb names another one.
- **std/stream (skill `torbscript-concurrency`: `references/standard-library/stream.md`)** - Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.
