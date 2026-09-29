# The pages of torbscript-standard-library

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- standard-library
- how-to

## standard-library

- `standard-library/index.md` - **The standard library** (index): One page per package of std, what each contains, and which of them are in scope everywhere without an import.
- `standard-library/core.md` - **std/core** (package): The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.
- `standard-library/function-types.md` - **Predicate, Action and Transform** (reference): Three aliases in the prelude for the closure shapes signatures take most - a question about one value, an effect on one value, and a conversion of one value into another.
- `standard-library/text.md` - **std/text** (package): Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
- `standard-library/number.md` - **std/number** (package): Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
- `standard-library/collections.md` - **std/collections** (package): The collection traits every signature talks about, and the implementations that only show up where one is built.
- `standard-library/iteration.md` - **std/iteration** (package): Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
- `standard-library/encoding.md` - **std/encoding** (package): Encode, Decode and Describe, the Encoder, Decoder and Describer a format implements, EncodedValue and Structure for a value without its type, Format for streaming, and RFC 4648's Base64, Base32 and hexadecimal for bytes as text.
- `standard-library/expression.md` - **std/expression** (package): Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
- `standard-library/console.md` - **std/console** (package): print and printError, the two functions that write to the standard streams.
- `standard-library/linear.md` - **std/linear** (package): Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
- `standard-library/geometry.md` - **std/geometry** (package): The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
- `standard-library/json.md` - **std/json** (package): Json, a value with the options of the format, for encoding and decoding any Encode/Decode type, and JsonValue for the rare document whose shape is not known ahead of time.
- `standard-library/yaml.md` - **std/yaml** (package): Yaml reads and writes any Encode/Decode type as YAML 1.2 or 1.1, with the target type resolving every scalar, and YamlNode is the tree of a document with its anchors, tags, styles and comments.
- `standard-library/regex.md` - **std/regex** (package): Regex, a compiled pattern with the syntax and the linear-time semantics of RE2, with whole and partial matches, named groups that decode into a type, replace and split.
- `standard-library/markdown.md` - **std/markdown** (package): Markdown reads CommonMark with GitHub's tables and front matter into a document tree that is a value, with the lines of every block and link; HTML and Markdown are written from the tree.
- `standard-library/time.md` - **std/time** (package): Instant, Duration and Timestamp, the three time values, plus Clock and sleep, which read the two clocks and wait on them.
- `standard-library/path.md` - **std/path** (package): Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
- `standard-library/fs.md` - **std/fs** (package): File and IoError - whole files as text or bytes, a File as both ends of a byte stream, and the tree around them - remove, rename, move, copy, metadata, links, temporary files and atomic replacement.
- `standard-library/resource.md` - **std/resource** (package): Resource, EmbeddedBytes and EmbeddedText - a file of the package named by a string literal, resolved by the compiler where it is written; reading the bytes comes with the next slices.
- `standard-library/storage.md` - **std/storage** (package): Storage, a capability over a Uri whose scheme chooses the driver - FileStorage for files, MemoryStorage for tests - and Storage.registry, one storage over the drivers a program names.
- `standard-library/io.md` - **std/io** (package): Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
- `standard-library/process.md` - **std/process** (package): Process for arguments, exiting and running a program to its end as a task, Child for a running program's pipes, and ProcessOutput for what it left behind.
- `standard-library/os.md` - **std/os** (package, draft): Environment, System, Directories and Entropy - the environment a program was started with, which system and version it runs on, where the user's files belong, and the system's randomness - with OsError and the target constants.
- `standard-library/binary.md` - **std/binary** (package): ByteReader, ByteWriter, BitReader and BitWriter - numbers of every width in either byte order, IEEE 754 floats, LEB128, runs of bytes and text, read with a ReadError instead of a panic and written into a growable buffer.
- `standard-library/digest.md` - **std/digest** (package): Sha256, Sha512, Sha1 and Md5 and the Digest they answer, Hmac (RFC 2104) and PBKDF2 (RFC 8018) over any of them - fed at once or in pieces, shown as lowercase hexadecimal.
- `standard-library/compression.md` - **std/compression** (package): DEFLATE and gzip in TorbScript - inflated and gunzipped read every stream the formats allow, deflated and gzipped write deterministic output, and crc32 is the checksum gzip uses.
- `standard-library/archive.md` - **std/archive** (package): tar archives - untarred reads what GNU tar, bsdtar and pax write, with modes and links; tarred writes ustar and pax; extract of std/archive/extract unpacks safely.
- `standard-library/signature.md` - **std/signature** (package): Ed25519 of RFC 8032 in TorbScript - a private key from a 32-byte seed signs, a public key verifies, and keys and signatures are capsules that read and write themselves as bytes and hexadecimal.
- `standard-library/sandbox.md` - **std/sandbox** (package): Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
- `standard-library/prelude.md` - **std/prelude** (package): The package of re-exports that is in scope in every file of a project, unless project.trb names another one.

## how-to

- `how-to/read-a-file.md` - **Read a file** (how-to): Read a whole file or its lines, hand the failure to the caller with the question mark operator, and turn an IoError into your own error type.
- `how-to/read-and-write-json.md` - **Read and write JSON** (how-to): Json().encode and Json().decode<T> work on any Encode/Decode type for free; an option of the format spells the field names, and the pair is written by hand only where a constructor cannot say what a document may.
