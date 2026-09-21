# The Conformance Suite

**One program, two implementations, one observable behaviour.** Every `.trb` file here is run twice - by the
interpreter of stage 0 (`torb run <program>`) and as a native binary built through the C back end
(`torb run ../compiler build <program>`) - and the two runs have to agree, byte for byte, about everything a program
can be observed doing:

| What is compared | Where it is written down |
|------------------|--------------------------|
| standard output  | `<program>.expected`     |
| standard error   | `<program>.stderr`, and no file means "nothing at all" |
| the exit code    | `<program>.exit`         |

Plus two things that are checked on the compiled side alone, because there is nothing to compare them with:

- **Zero live blocks.** The binary is run a second time with `TORB_REPORT_LEAKS=1`, and the runtime's block counter has
  to be zero where the program ends. A program that panics is exempt, because a panic runs nothing (CONCEPT, "A panic
  is output") and what it leaves behind is not a leak. The report has a second line, `immortal blocks at exit: N`: the
  value of a module constant is built once into a block that is never freed by construction, and counting those apart is
  what keeps the first number exact. Both lines are asserted.
- **The emitted C is a pure function of the program.** `--emit-c` twice gives the same bytes, and no absolute path of
  any machine is in it.

`bootstrap/crates/torb-cli/tests/native.rs` is the runner, `cargo test --release --test native` runs it, and a C
compiler is what it needs (`$TORB_CC`, `clang`, `gcc`, `cc` - the order `torb build` uses).

## What is *not* compared

**Nothing about what a program does.** The one thing that is read loosely is the **position inside a frame of `std/`**:
a `.stderr` file writes `  at std/core/src/option.trb:_:_`, because a line of the standard library moves whenever a
comment above it is edited, and what a program promises is *which file* panicked. Stage 0 loads no `std/` at all - it
answers those bodies with a native - so it writes the file with no position, which reads the same way. A frame of the
program itself keeps its exact line and column.

There is no allowance for a message, an exit code or an output. Until milestone 5.14 there was one - a program that
panicked only had to *fail* on stage 0 - and closing it is what that milestone was.

## Adding a program

1. **One behaviour per program, and a small program.** Breadth is what this suite is for: a reader who asks "what pins
   integer overflow" should find `overflow.trb` and read it in ten seconds. A program that pins six things pins none of
   them clearly.
2. Write the `.trb` next to this file. It needs a **doc comment that says what it pins and why that is the language's
   answer** - not what the back end does today. The file is checked (`torb run ../compiler check tests/native` from
   `bootstrap/`), so it is ordinary, valid TorbScript, in the canon of the formatter.
3. End it in `Process.exit <code>` where the exit code is part of what is pinned, and compute the code from what the
   program measured where that is possible: a program whose answer is wrong then fails on the code as well as on the
   output.
4. Run it both ways and write the three expectation files from the **compiled** run, with `\r\n` folded to `\n` and a
   `std/` position replaced by `_:_`. Then read them: an expectation nobody read is a bug that was written down.
5. `cargo test --release --test native`. A program that is wrong about the language fails on both sides at once, which
   is the one case this suite cannot catch - that is what `compiler/tests/` and the pages under `docs/language/` are
   for.

A behaviour that stage 0 cannot run at all does not belong here; it belongs in `compiler/tests/` as an IR snapshot. A
long program that exercises many things at once belongs in `bootstrap/tests/scripts/`, which runs on stage 0 alone.

## `stage-0-only/`

One subdirectory, and it is not an exception to the contract - it is a waiting room. A program lands there when **the C
back end cannot produce the behaviour yet and stage 0 already answers what the language says**: there is nothing to
compare, and the program still pins the answer instead of waiting for the back end. The runner runs those programs on
stage 0 and reads the same three expectation files. Each one says in its doc comment why it is there and what has to
exist for it to move up one directory, and moving it is the whole change.

| Program | Why it waits |
|---------|--------------|
| `error-chain.trb` | A top-level `?` whose error carries `Error` prints one `  caused by:` line per link of `cause()`. The back end's `reportFailure` writes the first line and exits; the loop over `cause()` on top of it is not lowered yet |

## The programs

**Values and copies** - a value has no identity, so a second name is a second value.

| Program | What it pins |
|---------|--------------|
| `copies.trb` | A record, a list, a map and a record inside a list, each copied by being named twice, handed to a function, or read after a write |
| `boxed-copy.trb` | A copy of a value whose layout is `Boxed` may not see a change made through the other one |
| `records.trb` | Records, fields, defaults and nesting |
| `reassignment.trb` | Assignment to a counted local releases what the slot held before the new value lands |
| `places.trb` | `var` parameters and `var self` receivers: the callee works on the caller's value |
| `place-counted.trb` | `var` paths through counted storage, and copy on write on every one of them |
| `counted.trb` | Counted values through every position the ownership pass decides about |

**Control flow and patterns**

| Program | What it pins |
|---------|--------------|
| `matching.trb` | Everything a decision tree decides besides a case: literals, ranges, tuples, guards, bindings |
| `match-order.trb` | The first arm that matches wins, a false guard falls through, and a guard runs only for its own arm |
| `list-patterns.trb` | `[]`, `[only]`, `[first, second]`, `[first, ...rest]`, `[first, ..., last]`, a literal item |
| `states.trb` | A state machine over unit cases, `if const` and `while const` |
| `adts.trb` | A recursive algebraic data type and the decision trees over it |
| `optional-chain.trb` | `?.`: the member on the payload, or `None` without touching the member |

**Closures**

| Program | What it pins |
|---------|--------------|
| `closures.trb` | A closure that captures nothing, one that captures a value, one that is stored and called later |
| `closure-captures.trb` | A captured `var` binding is one **shared box** and everything else is a copy: a write on either side is seen by the other, a closure a function returns keeps the box, the binding of a `for` is fresh every turn |
| `closure-counts.trb` | What a closure does with a count it was only lent |
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
| `trait-values.trb` | A `var self` member reached through a witness table |
| `generics.trb` | A generic type with methods, a generic function with a bound, a generic `extend` |
| `derived.trb` | The generated `Show`, `Equals` and `Hash` of a record, a case and a tuple |
| `case-defaults.trb` | A case constructor whose field takes its default |
| `constants.trb` | Top-level `const`s of every shape, from a function and from the top level, and the mutated copy of one |
| `show-compound.trb` | `Show` of everything compound, and of a type whose field carries the name of a member |
| `tuple-compare.trb` | The generated `compare` of a tuple: lexicographic, each field through its own `Compare` |

**Errors**

| Program | What it pins |
|---------|--------------|
| `errors.trb` | `Option` and `Result` through `?`, `??` and the patterns over them |
| `top-level-error.trb` | A top-level `?` that fails: `error: <the error through Show>` and exit code **1** - not a panic |

**Panics** - `panic: <message>`, the site, exit code **101**, and nothing else runs.

| Program | What it pins |
|---------|--------------|
| `panic.trb` | `panic "..."`: the message, the site in the program, and that nothing after it runs |
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
