# Command Lines

**Status: planned** — there is no `std/cli`. `torb` itself reads its arguments with one `match` over
`Process.arguments()` (`compiler/src/main.trb`). This record is a stub that keeps the direction; the design with
probes comes before the package is built.

**`std/cli` is the standard library's answer to Rust's `clap`: a program with several commands, flags, options and
positional arguments, its help text and its errors, declared once.** Its first user is the compiler: `torb` has a dozen
subcommands, and every one of them parses its own arguments by hand today.

## 1. The direction

The language has no annotations and no reflection, so the declaration is not a type with attributes on its fields. It
is a builder on receiver closures, the same means `project.trb` uses:

```trb fragment
const cli = command "torb" {
  description "The TorbScript toolchain"
  command "check" {
    description "Check projects, files or a whole workspace"
    flag "statistics", help: "Print typed and deferred expressions per module"
    arguments "paths", help: "Projects, directories or files"
    run { input => check(input.arguments("paths"), statistics: input.flag("statistics")) }
  }
}

cli.run Process.arguments()
```

What follows from the direction, and is not open:

- the help text of every command and the error for a wrong argument are generated from the declaration, and a wrong
  argument exits with a message that names the command and the argument;
- nothing is registered by scanning: a subcommand is part of the tree because the builder names it;
- the package is pure except for `run`, which reads the arguments it is given, so a test hands it a list of strings.

## 2. Open

Questions of taste, decided with the first probe:

1. **Typed options or a decoded type.** `option<Int>("jobs", default: 4)` read back as `input.option<Int>("jobs")`,
   or the arguments of a command decoded into a type of the program (`input.decode<CheckOptions>()`), through
   `Decode` as [std/json](../standard-library/json.md) does it. The second keeps one declaration per option, and the
   derived `Describe` ([ENCODING.md](ENCODING.md)) carries the documentation of each field into the help text.
2. **Shell completion**: later, generated from the same declaration.

## 3. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `std/cli`: commands, flags, options, positional arguments, help, errors; tests with argument lists | nothing |
| 2 | `torb` parses its command line with it | 1, and a seed refresh, because the compiler imports it |
| 3 | Shell completion scripts | 1 |
