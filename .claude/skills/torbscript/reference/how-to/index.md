---
title: Task recipes
summary: "One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works."
kind: index
status: stable
order: 40
---

Recipes for one task each. A page here assumes you can read TorbScript and tells you what to write; it forks where your
situation forks, and it ends with one complete program that type checks.

If you do not know the language yet, [the learning path](../guide/index.md) is the place to start.

## What belongs here

One task per page, titled with the verb (`Read a file`, `Add a dependency`). The reader is competent and in a hurry: the
steps come first, the full program comes at the end, and the pitfalls are named rather than explained.

What does not belong here: a learning order, which is [`guide/`](../guide/index.md); the complete rules of a construct,
which are in [`language/`](../language/index.md); and an argument, which is in
[`explanation/`](../explanation/index.md).

<!-- torb:index:begin -->

## Pages

- **[Set up your editor](set-up-your-editor.md)** - Install the VS Code extension from the Marketplace, Open VSX or a .vsix - it installs a missing toolchain and brings the language server, the Test Explorer, the debugger and the formatter - or point an editor at torb lsp and torb debug.
- **[Read a file](read-a-file.md)** - Read a whole file or its lines, hand the failure to the caller with the question mark operator, and turn an IoError into your own error type.
- **[Write a configuration file](write-a-configuration-file.md)** - Declare a type for the configuration, write the file as TorbScript against it, and load it through the sandbox with the capabilities you grant.
- **[Define an error type](define-an-error-type.md)** - Declare a type with one case per distinct failure, add Show and Error where a layer above needs to hand it further up, and let the generated From do the conversion at every ?.
- **[Parse text into a type](parse-text-into-a-type.md)** - Give the type a private field nothing outside can set directly, and implement TryFrom<String, Failure> so that Type.tryFrom(text) validates the text and answers a Result instead of a bare value.
- **[Write a test](write-a-test.md)** - Put a test in a file called *.test.trb, by convention under tests/, group related ones, and let assert show the source and the values instead of writing a matcher.
- **[Build a native binary](build-a-native-binary.md)** - Run torb build in a package or point it at a file, look at the generated C with --emit-c first if a C compiler is not on the machine yet, and read what the back end does not lower yet before you debug the program instead.
- **[Add a dependency](add-a-dependency.md)** - Declare the package in project.trb before importing from it, tell a runtime dependency from a development one, and read what the imports of everything you depend on say it can reach.
- **[Set up a workspace](set-up-a-workspace.md)** - Name the member directories in the root project.trb, give each one its own project.trb, and depend on a sibling by name alone - the workspace resolves it from source.
- **[Use a type as a map key](use-a-type-as-a-map-key.md)** - An ordinary type is already a legal key once every field is Hash, which the compiler generates for free; a field that cannot be Hash is the one thing that rules a type out.
- **[Convert between types](convert-between-types.md)** - Implement From when the conversion cannot fail and TryFrom when it can - text is a source like any other - and call to<Target>() to collect a pipeline into any type built from one.
- **[Collect a pipeline into what you need](collect-a-pipeline.md)** - Reach for the named terminal operation when there is one - toList, sum, joined, groupBy - and fall back to collect with an Accumulator for anything else, including your own accumulator.
- **[Sort by more than one key](sort-by-more-than-one-key.md)** - Sort by a tuple key instead of a single field - a tuple's Compare is generated lexicographically by position, which a type never gets because an order is a decision, not a structure.
- **[Write a builder](write-a-builder.md)** - Write a function that creates a value, hands it to a receiver closure, and returns it - three lines that make every property command, nested block and method call in the closure statically typed.
- **[Read and write JSON](read-and-write-json.md)** - Json().encode and Json().decode<T> work on any Encode/Decode type for free; an option of the format spells the field names, and the pair is written by hand only where a constructor cannot say what a document may.

<!-- torb:index:end -->

