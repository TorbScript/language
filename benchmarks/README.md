# The Performance Suite

One small program per pattern, and beside every one of them the C a careful C programmer would write for the same work.
`run.sh` builds both with the same C compiler and the same flags, runs each of them a few times and prints the ratio.
The numbers behind [docs/PERFORMANCE.md](../docs/PERFORMANCE.md) come from here.

This is a **measurement and not a gate**. Nothing here runs under `tools/gates.sh`: a number that moves with the machine,
the load and the C compiler does not belong in a test. What belongs in a test is a ratio with a budget, and
`docs/PERFORMANCE.md` section 6 says which ones those should be.

## Running it

```sh
cd benchmarks
sh run.sh                    # everything
sh run.sh list-index         # one program
RUNS=9 sh run.sh             # more repetitions; the fastest of them is what is printed
sh run.sh --allocations      # also count how often each side called malloc
TORB=../seed/torb.exe sh run.sh    # build the programs with another `torb` binary
```

It needs a `torb` and a C compiler on the `PATH` (`$TORB_CC`, then `clang`, `gcc`, `cc` - the order `torb build`
uses). `$TORB` is the compiler, `../build/release/torb` by default, and it is called directly, which is how two
builds of the compiler are measured against each other; `$TORB_COMPILER` names a compiler *package* for a `$TORB`
that is a driver rather than a compiler. Everything it writes lands in `out/`, which is not checked in:
`out/<program>/program.c` is the C the back end emitted, `out/<program>/build.log` is what `torb build` said.

## Reading the table

```text
program              torb          c    ratio  checksum
list-index         524282      15271   34.33x  list-index 3839953560
nested-write      1021686          1 >510.84x  nested-write 4800
```

- **torb** and **c** are the fastest of `RUNS` runs, in microseconds, **net of the process floor** that `nothing.trb`
  and its twin measure at the start of every run. On Windows that floor is tens of milliseconds and it is not the same
  for both sides, so each side is corrected with its own.
- **ratio** is torb divided by C. `1.00x` is "the same speed"; there is no scaling of any kind. A `>` in front of it
  means the C side did under two milliseconds of work, which is inside the noise of the floor, so the ratio is a lower
  bound computed against two milliseconds. Read the allocation column for those rows instead.
- **checksum** is the line both programs printed. The two sides print the same line, so `DIFFERENT:` in this column
  means the two programs stopped computing the same thing and the times below it mean nothing.
- **skipped** in the torb column means the native back end refused the program; the reason is printed beside it. The
  suite is written to run today, so a program the back end cannot build is reported rather than left out.

`--allocations` links a second copy of both binaries against `c/allocations.c` through `ld --wrap`, so the count is the
program's own `malloc`/`calloc`/`realloc` and needs no change to `runtime/`. That column is the one that does not move
with the machine, which is why the findings in `docs/PERFORMANCE.md` are argued with it wherever they can be.

## The programs

Each one is small, deterministic and prints a checksum, so a program that is wrong is visible before its time is read.

| Program | What it measures |
|---------|------------------|
| `arithmetic` | A data dependent chain of `*`, `+` and `%` in a counted loop: what the overflow checks cost |
| `call-depth` | Recursive Fibonacci: the calling convention with nothing counted and nothing generic |
| `wrapper` | A one-field `Meters` against the `double` inside it: the zero-cost claim in its smallest form |
| `closure` | A closure that captures one value, against a function pointer plus a context |
| `list-index` | `numbers[index]` in a counted loop against `data[index]` |
| `array-index` | `values[index]` of an `Array<Int, 64>` in a counted loop: whether the range analysis leaves a bounds check |
| `list-iterate` | `for value in numbers` against walking a pointer |
| `pipeline` | `map`, `filter`, `sum` against the fused loop they stand for |
| `record-write` | `points[index].y = ...` against the same store through a pointer |
| `nested-write` | `grid[row][column] = value`, where the row is shared and the write copies it |
| `accumulate` | `numbers = appended(numbers, index)`: whether the last use is a move or a copy |
| `interpolation` | `"row {index}: {index % 97}"` against one `snprintf` into a buffer |
| `map-count` | `get` then `set` on a `Map<String, Int>` against one probe of an open addressing table |
| `nothing` | The process floor that every other row is corrected with. It is not printed as a row of its own |

One program has no twin: `binary-formats` decodes a large DNS message, deflates and inflates a gzip member and hashes
a megabyte with SHA-256 - the standard library measured against itself. `run.sh` does not run it; a copy of it outside
the repository (inside, the workspace's own `std/` wins) is built twice, against two checkouts' `std/`
(`TORB_STD=<checkout>/std torb build binary-formats.trb`), and the two binaries' lines of seconds are the comparison. [docs/design/BINARY.md](../docs/design/BINARY.md) section 7 has what it measured.

Three of the C twins - `list-index`, `list-iterate` and `pipeline` - are sum reductions that gcc vectorizes, and no
program whose elements come back from a call can. Part of those ratios is the vectorization rather than the dispatch,
and `docs/PERFORMANCE.md` section 4 says so where the numbers are read.

## Adding one

1. Write `<name>.trb` next to this file, with a doc comment that says which cost it isolates. It is checked
   (`torb check benchmarks` from the repository root), so it is ordinary TorbScript in the formatter canon.
2. Write `c/<name>.c` that does the **same work** and prints the **same line**. It is what a careful C programmer would
   write, not a transliteration of the TorbScript and not a hand-tuned kernel.
3. Add the name to `programs` in `run.sh`, in the place where it reads best: the cheap patterns first.
4. Size it so the C side runs for at least a few tens of milliseconds where the pattern allows it. Where it does not -
   because the TorbScript side is quadratic and the C side is not - say so in the doc comment.
