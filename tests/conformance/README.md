# The Conformance Suite

**One program, one native binary, one observable behaviour.** Every `.trb` file here is built with the native
compiler (`torb build <program>`), run, and compared against what is written down beside it:

| What is compared | Where it is written down |
|------------------|--------------------------|
| standard output  | `<program>.expected`     |
| standard error   | `<program>.stderr`, and no file means "nothing at all" |
| the exit code    | `<program>.exit`         |
| whether the leak gate applies | `<program>.leaks`, which holds the reason it does not |

**`\n` is `\n`.** Nothing about what a program wrote is normalised - the runtime puts standard output and standard
error into binary mode on Windows (`torb_process_start`), so the bytes a binary writes into a pipe are exactly what
this compares against. Only the expectation *files* are read with `\r\n` folded, because git may check one out with
either ending.

Plus two things that are checked on the built program alone, because there is nothing to compare them with:

- **Zero live blocks.** The binary is run a second time with `TORB_REPORT_LEAKS=1`, and the runtime's block counter has
  to be zero where the program ends. A program that panics is exempt, because a panic runs nothing (CONCEPT, "A panic
  is output") and what it leaves behind is not a leak. The report has a second line, `immortal blocks at exit: N`: the
  value of a module constant is built once into a block that is never freed by construction, and counting those apart is
  what keeps the first number exact. Both lines are asserted.

  A program that **recovers** a panic is exempt for the same reason and says so in a `<program>.leaks` file beside it,
  which holds the sentence why. There is one: a test whose body panics is reported and the next test runs, and the
  frames the jump left behind released nothing. The file is the whole exemption, so one that nobody can justify cannot
  be added without a reader seeing it.
- **The emitted C is a pure function of the program.** `--emit-c` twice gives the same bytes, and no absolute path of
  any machine is in it.

`tools/conformance.sh` is the runner (`sh tools/conformance.sh`, `sh tools/gates.sh b`), and a C compiler is what it
needs (`$TORB_CC`, `clang`, `gcc`, `cc` - the order `torb build` uses).

## What is *not* compared

**Nothing about what a program does beyond what is written down.** The expectation files were written from a native
run, so a program that answers correctly *as the language defines it* is not distinguished here from one that
answers consistently but wrongly - that is what `compiler/tests/` and the pages under `docs/language/` are for, and
this suite alone never catches it.

The one thing that is read loosely is the **position inside a frame of `std/`**: a `.stderr` file writes
`  at std/core/src/option.trb:_:_`, because a line of the standard library moves whenever a comment above it is
edited, and what a program promises is *which file* panicked, not which line - a comment added above a panic site
would otherwise invalidate every `.stderr` file that panics through it. A frame of the program itself keeps its exact
line and column.

Beyond that folding, there is no allowance anywhere: a program that panics with the wrong message, or leaves with the
wrong exit code, fails exactly as one that prints the wrong thing does.

## Adding a program

1. **One behaviour per program, and a small program.** Breadth is what this suite is for: a reader who asks "what pins
   integer overflow" should find `overflow.trb` and read it in ten seconds. A program that pins six things pins none of
   them clearly.
2. Write the `.trb` next to this file. It needs a **doc comment that says what it pins and why that is the language's
   answer** - not what the back end does today. The file is checked (`torb check tests/conformance` from the
   repository root), so it is ordinary, valid TorbScript, in the canon of the formatter.
3. End it in `Process.exit <code>` where the exit code is part of what is pinned, and compute the code from what the
   program measured where that is possible: a program whose answer is wrong then fails on the code as well as on the
   output.
4. `sh tools/conformance.sh --filter <name> --update` builds it, runs it and writes the three expectation files from
   that run, with a `std/` position already folded to `_:_`. Nothing else is folded: the runtime puts standard output
   and standard error into binary mode on Windows, so what the binary writes into a pipe is `\n` exactly as the file
   then holds it. Then read them: an expectation nobody read is a bug that was written down.
5. `sh tools/conformance.sh --filter <name>`. A program that is wrong about the language still passes, because the
   files it is compared against were written from this same run - that is the one case this suite cannot catch by
   itself, and `compiler/tests/` and the pages under `docs/language/` are what catches it instead.

A behaviour the native back end cannot run at all does not belong here; it belongs in `compiler/tests/` as an IR
snapshot. A long program that exercises many things at once belongs in `tests/language/`, which the native
`torb run` runs on its own.

## `binary-only/`

Programs whose run ends in a **recovered** panic, which is why the leak gate does not apply to them: the frames the
jump left behind released nothing. The directory is the exemption, instead of a `.leaks` file per program.
`tools/conformance.sh` builds and runs them exactly like every other program and reads the same expectation files.

| Program | What it pins |
|---------|--------------|
| `assert-compound-capture.trb` | A failing `assert` shows a capture that is not a scalar by its **name and its type** (`found: Point`) rather than by its value. Showing the value needs `Encode` for its type and for every type under it - one implementation in the binary per captured type, and one unshowable type anywhere refuses the whole build, which is what the compiler's own 2801 assertions over its own types would mean. It closes when a capture is an `EncodedValue` |
| `blanket-into.trb` | `into()` through the blanket implementation of `Into`: the instance of `Target.from self` is chosen by the type the value arrives at, which is the one place a member's target type stands nowhere near the call |

## The programs

**Values and copies** - a value has no identity, so a second name is a second value.

| Program | What it pins |
|---------|--------------|
| `copies.trb` | A record, a list, a map and a record inside a list, each copied by being named twice, handed to a function, or read after a write |
| `boxed-copy.trb` | A copy of a value whose layout is `Boxed` may not see a change made through the other one |
| `records.trb` | Records, fields, defaults and nesting |
| `reassignment.trb` | Assignment to a counted local releases what the slot held before the new value lands |
| `participle.trb` | `x = f(x)`: the old value is handed to the call, and every shape of an overwrite releases exactly once |
| `places.trb` | `var` parameters and `var fn` receivers: the callee works on the caller's value |
| `place-counted.trb` | `var` paths through counted storage, and copy on write on every one of them |
| `counted.trb` | Counted values through every position the ownership pass decides about |

**Control flow and patterns**

| Program | What it pins |
|---------|--------------|
| `matching.trb` | Everything a decision tree decides besides a case: literals, ranges, tuples, guards, bindings |
| `match-order.trb` | The first arm that matches wins, a false guard falls through, and a guard runs only for its own arm |
| `list-patterns.trb` | `[]`, `[only]`, `[first, second]`, `[first, ...rest]`, `[first, ..., last]`, a literal item |
| `rest-patterns.trb` | `Config(host, ...)`: the `...` that stands for the fields a pattern does not name, and a labeled sub-pattern that names its own field |
| `nested-list-patterns.trb` | A list pattern **inside** another one: in a case field, in a record field, in a tuple, inside another list, with a rest, under a guard, and in an instance of a generic |
| `states.trb` | A state machine over unit cases, `if const` and `while const` |
| `adts.trb` | A recursive algebraic data type and the decision trees over it |
| `optional-chain.trb` | `?.`: the member on the payload, or `None` without touching the member |

**Closures**

| Program | What it pins |
|---------|--------------|
| `closures.trb` | A closure that captures nothing, one that captures a value, one that is stored and called later |
| `closure-captures.trb` | A captured `var` binding is one **shared box** and everything else is a copy: a write on either side is seen by the other, a closure a function returns keeps the box, the binding of a `for` is fresh every turn |
| `closure-counts.trb` | What a closure does with a count it was only lent |
| `closure-frame.trb` | A closure a callee only calls, made in a loop, beside one the callee keeps and calls later |
| `curried-call.trb` | `adder(4)(1)`: the callee of a call is a call, a field and a conditional |
| `counted-closures.trb` | The leak gate of everything a closure can hold that carries a count |
| `dsl.trb` | Receiver closures and the three property commands |

**Numbers**

| Program | What it pins |
|---------|--------------|
| `arithmetic.trb` | Monomorphic functions, records, tuples, control flow and text |
| `integer-division.trb` | Division truncates towards zero, the remainder takes the sign of the dividend, `(a / b) * b + a % b == a` |
| `floats.trb` | `Show` of a `Float64`: the shortest decimal that parses back, in the notation of decided gap 4 |
| `float-order.trb` | `==` and `<` on a float are IEEE-754 and `compare` is a total order with `nan` on top - and `sorted` uses `compare` |

**Text**

| Program | What it pins |
|---------|--------------|
| `text.trb` | Strings, slices, bytes and characters |
| `interpolation.trb` | Every shown form string interpolation and `print` produce |
| `characters.trb` | `chars()`, `bytes()`, `charAt` and a cursor over a slice, at every UTF-8 width |
| `character-case.trb` | Case mapping is the simple one-to-one mapping over ASCII and the letters of Latin-1; `ß` is unchanged |
| `joined.trb` | Putting many texts together is one merge tree, not one accumulator |

**Collections**

| Program | What it pins |
|---------|--------------|
| `collections.trb` | The list: the literal, its element types, `for`, `a[key]`, growth |
| `collection-equality.trb` | A collection is `Equals` when its items are, and compares structurally |
| `collection-natives.trb` | A native whose result is a collection the runtime owns, behind a trait type |
| `collection-places.trb` | A `var` path through `a[key]`: assignment, a field of an element, a method on one |
| `maps-and-sets.trb` | The literals, insertion order, and the cursor that walks them |
| `ranges.trb` | A `Range` as a value: all four spellings, iterated, measured and shown |
| `sort-stability.trb` | `sort` and `sorted` are stable, and sorting twice by the same key changes nothing |
| `variadics.trb` | `...items: Item` is one parameter that every positional argument lands in |

**Types and traits**

| Program | What it pins |
|---------|--------------|
| `traits.trb` | Traits, trait-typed values, default members, generics with bounds |
| `trait-values.trb` | A `var fn` member reached through a witness table |
| `generics.trb` | A generic type with methods, a generic function with a bound, a generic `extend` |
| `generic-operators.trb` | Every operator on a generic type is the member of its trait, for `Int`, `Float` and `Fixed`, and inside a generic body as well |
| `derived.trb` | The generated `Show`, `Equals` and `Hash` of a record, a case and a tuple |
| `capsule.trb` | The capsule standard: private fields with no default, a `static fn` factory, accessors, and the one conversion pair in both directions |
| `case-defaults.trb` | A case constructor whose field takes its default |
| `constructor-values.trb` | The generated constructor as a function value: the labels and the defaults survive |
| `module-destructuring.trb` | A top-level binding that destructures is one constant per name, each one its own part of the value |
| `literal-union-show.trb` | A literal type is its base at run time: its `Show`, `Equals` and `Hash` are the base's |
| `constants.trb` | Top-level `const`s of every shape, from a function and from the top level, and the mutated copy of one |
| `generic-constants.trb` | A `const` of a generic type is one value per type argument, named after the arguments the read decided |
| `show-compound.trb` | `Show` of everything compound, and of a type whose field carries the name of a member |
| `tuple-compare.trb` | The generated `compare` of a tuple: lexicographic, each field through its own `Compare` |

**Paths** - `std/path`'s `Path`: a capsule over a root and a list of components, never a string, and nothing about a disk.

| Program | What it pins |
|---------|--------------|
| `paths.trb` | Every row of the parse table, every member including the two accessors of the capsule, `resolved(inside:)` in each of its outcomes, and the ten call-site rewrites of `docs/PATH.md` section 4 |

**Tests** - `test` and `group` of `std/test`, whose report both implementations write from the same place.

| Program | What it pins |
|---------|--------------|
| `tests.trb` | One line per test, the group names in front of it with ` > ` between them, and groups that nest |
| `test-failure.trb` | A test fails when its body panics: the name, the message indented under it, the site, and the next test still runs. `assert` names the source of the condition and every scalar it read |
| `assert-values.trb` | What a failed `assert` shows for each kind of scalar: `the Int 1`, `the String "hi"`, `the Char 'a'`, `the Float 1.5`, `the Bool true` |

**Errors**

| Program | What it pins |
|---------|--------------|
| `errors.trb` | `Option` and `Result` through `?`, `??` and the patterns over them |
| `top-level-error.trb` | A top-level `?` that fails: `error: <the error through Show>` and exit code **1** - not a panic |
| `error-chain.trb` | A top-level `?` whose error carries `Error` prints one `  caused by:` line per link of `cause()`, until `cause()` answers `None` |

**Panics** - `panic: <message>`, the site, exit code **101**, and nothing else runs.

| Program | What it pins |
|---------|--------------|
| `panic.trb` | `panic "..."`: the message, the site in the program, and that nothing after it runs |
| `panic-after-output.trb` | Everything the program printed is written before the panic is |
| `overflow.trb` | ``arithmetic overflow in `*` `` |
| `negate-overflow.trb` | ``arithmetic overflow in `-` ``: the smallest `Int64` has no positive counterpart |
| `division-by-zero.trb` | ``division by zero in `/` `` |
| `remainder-by-zero.trb` | ``division by zero in `%` ``: two instructions, two messages |
| `collection-index.trb` | `a[key]` past the end is `Indexed.at`, so it is `Key does not exist` at `std/core/src/option.trb` |
| `slice-out-of-range.trb` | `index 9 is out of bounds for a length of 3`, with the **exclusive** end in the message |
| `slice-reversed.trb` | `the range 2..1 starts after it ends` |
| `text-slice-past-end.trb` | A text is sliced in **bytes**: `the offset 9 is past the end of a text of 6 bytes` |
| `expect-none.trb` | `Option.expect` panics with its message alone |
| `expect-failure.trb` | `Result.expect` panics with its message **and** the error |

**The outside world**

| Program | What it pins |
|---------|--------------|
| `files.trb` | Make a directory, write a file, read it back, run a child process, ask the environment |
| `natives.trb` | The two calling conventions of `runtime/`: a `bool` with an out parameter, and a value |
| `non-ascii-paths.trb` | A path whose components are not ASCII (`grüße/日本.txt`), created, written, asked about, listed and read back - the boundary to the operating system converts, so both implementations name the same file |
| `long-paths.trb` | The same steps through a path of 271 bytes, over the 260 of `MAX_PATH`, which one platform's plain path form cannot hold at all |
| `absolute-path-form.trb` | `File.absolutePath` answers **one** form: forward slashes, an upper-cased drive letter, and no `.`, `..` or `\\?\` left in it |
| `process-non-ascii-argument.trb` | An argument of a child process that is not ASCII arrives as the text that was passed, which the child answers in its exit code |
