---
name: torbscript-standard-library
description: "Looks up the TorbScript standard library: which `std/` package has a type or function, how it is imported and its exact API - text, numbers, collections, files and paths, JSON and YAML, regex, time, processes, hashing, compression and more. Use it when TorbScript code imports from `std/` or needs a library function, before guessing a name."
license: MIT
compatibility: "Reference only. Verifying code needs the torb command, which the torbscript skill checks and installs."
---

<!-- carry: standard-library/ how-to/read-a-file.md how-to/read-and-write-json.md -->

# The TorbScript standard library

The `torbscript` skill has the language; this skill has the packages of `std`, except the ones for testing, concurrency,
networking and project files, which have skills of their own, named below where their package is. Paths are relative
to the directory of this file.

Never guess a name: find the package here, then check the code with `torb check`. A command-line program reaches for
`std/process` (arguments, exit code, running a program), `std/io` (standard input), `std/fs` and `std/path` (files),
and `std/os` (the environment); [reading a file](references/how-to/read-a-file.md) and
[reading and writing JSON](references/how-to/read-and-write-json.md) are recipes with a complete program each.

## The packages

<!-- inline: standard-library/index.md -->
