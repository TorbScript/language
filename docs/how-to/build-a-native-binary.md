---
title: Build a native binary
summary: Point torb build at the entry file, look at the generated C with --emit-c first if a C compiler is not on the machine yet, and read what the back end does not lower yet before you debug the program instead.
kind: how-to
status: stable
order: 60
keywords:
  - torb build
  - emit-c
  - TORB_CC
  - TORB_RUNTIME
  - native back end
source:
  - compiler/src/cli/build.trb
---

`torb build` lowers the typed program to C and hands the C to whatever compiler is on the machine. The C back end does
not lower every declaration `torb check` accepts yet, so building is also how a program finds out whether it is inside
the part of the language the native back end already covers.

## Steps

1. **Point `torb build` at the entry file**, from `bootstrap/` until the compiler compiles itself:

   ```console
   $ cargo run --release -q -- run ../compiler build ../my-project
   ```

   A path to a project with a `build { input "src/main.trb" }` works the same way; `torb build` reads that project's
   entry file.

2. **Look at the generated C first with `--emit-c` if there is no C compiler on the machine yet.** It writes the C
   next to where the binary would go and stops - no C compiler needed for this step at all.

   ```console
   $ cargo run --release -q -- run ../compiler build ../my-project --emit-c
   ```

3. **Choose where the binary goes with `--output <path>`.** The C file is written next to it, so both ends of the
   build are in one place.

   ```console
   $ cargo run --release -q -- run ../compiler build ../my-project --output ../build/my-project
   ```

4. **Make sure a C compiler is reachable.** `torb build` tries, in order, `$TORB_CC`, `clang`, `gcc`, `cc`, then `cl`.
   Set `$TORB_CC` to force a specific one.

5. **Set `$TORB_RUNTIME` if the runtime is not found automatically.** The binary links against the C runtime under
   `runtime/` of the toolchain checkout; `torb build` walks up from the working directory looking for it, and an
   explicit `$TORB_RUNTIME` skips that search.

6. **When it refuses, check what fraction of the program the back end can lower**, with `torb ir --statistics` on the
   same path. It answers a count and, below it, one line per reason, so a program that will not build says why before
   `torb build` itself does. Run it rather than trust a number written down here - the count moves as the back end
   grows.

   ```console
   $ cargo run --release -q -- run ../compiler ir --statistics ../examples/tour
   374 of 600 functions lowered (62%), 226 not supported yet
   ```

## Pitfalls

- **`print`, `printError` and string interpolation build.** Both were the two most common reasons a program refused
  to build; a program that only prints and interpolates strings is no longer a reason to reach for `--emit-c` just to
  read the generated code.
- **A `for` over a range that is not a literal at that line does not lower yet**, because `Range.iterator` is not
  built into the back end: `for value in 0..n { ... }` builds, `for value in someRange { ... }` where `someRange` is
  a `Range<Int>` value does not, with `` not supported by the back end yet: `Range.iterator` `` naming it.
- **A handful of other constructs are the same kind of gap** - a variadic parameter of a function you declare
  (`print`'s own variadic call is a back-end intrinsic and is not affected), map and set iteration, a write through
  `a[key]`, a list pattern, `Array<Item, Size>`, a slice used as a window, a `shared type` object, `using`, a task or
  a stream, and a quoted expression. `torb build` reports each one as "not supported by the back end yet" with the
  construct named, and refuses to build rather than emit something that does not do what the program says.
- **A refusal is not a bug in the program.** The message names what the back end cannot lower, not what is wrong with
  the code; the same program type checks and runs correctly under other backends, so `torb check` staying green is
  what tells the two apart.
- **No C compiler is a different failure from a C compiler that rejects the generated C.** The first prints where it
  looked (`$TORB_CC`, `clang`, `gcc`, `cc`, `cl`) and exits `3`; the second is an internal error of the emitter, keeps
  the C file, and exits `70` - only the second is worth reporting as a compiler bug.

## Full example

A program that prints, interpolates strings, and loops over a literal range builds today.

```trb check
fn greeting(name: String): String {
  "hello {name}"
}

fn sumTo(n: Int): Int {
  var total = 0
  for value in 0..n {
    total = total + value
  }
  total
}

print greeting("world")
print sumTo(5)
```

## Related

- [The torb command](../tooling/the-torb-command.md) - `build`, `ir` and every other subcommand.
- [Verify your work](../tooling/verifying-your-work.md) - `check` first, always, whether or not `build` will follow.
- [Errors at the top level](../language/errors/top-level-errors.md) - what a `?` in `main` prints, for a program that
  does use `Result` instead of an exit code.
