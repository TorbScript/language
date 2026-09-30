# The Performance Suite

One small program per pattern, and beside every one of them the C a careful C programmer would write for the same work.
`run.sh` builds both with the same C compiler and the same flags, runs each of them a few times and prints the ratio.
The numbers behind [docs/PERFORMANCE.md](../docs/PERFORMANCE.md) come from here.

This is a **measurement and not a gate**. Nothing here runs under `tools/gates.sh`: a number that moves with the machine,
the load and the C compiler does not belong in a test. What belongs in a test is a ratio with a budget, and
`docs/PERFORMANCE.md` section 6 says which ones those should be.

Beside the patterns there are six whole programs everybody knows, in `game/`: the programs of the website's benchmarks
page ([docs/site/benchmarks.md](../docs/site/benchmarks.md)), which `game.sh` measures - see
[The Benchmarks Game](#the-benchmarks-game) at the end.

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

## The Benchmarks Game

`game/` holds six programs of the [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/),
the collection people compare languages with: `binary-trees`, `fannkuch-redux`, `n-body`, `spectral-norm`, `mandelbrot`
and `fasta`. Each is there four times:

| File | What it is |
|------|------------|
| `game/<name>.trb` | TorbScript, written for this suite: the C program statement for statement, in the TorbScript the guide teaches. Its doc comment says where it differs and why |
| `game/c/<name>.c` | The Benchmarks Game's plain, single-threaded C program - #8, its "naive transliteration" family, or #1 for binary-trees, which has no #8 |
| `game/python/<name>.py` | The Benchmarks Game's Python 3 program of the same family (#8; #2 for binary-trees) |
| `game/node/<name>.js` | The Benchmarks Game's JavaScript program for Node.js of the same family (#8; #7 for binary-trees) |

The C, Python and JavaScript are copied unchanged from the Benchmarks Game, each with a comment that names its page and
its file in `benchmarksgame-sourcecode.zip`, and they are under the Benchmarks Game's Revised BSD licence, which is
`game/LICENSE`. The TorbScript programs are under the licence of this repository.

`game.sh` builds and runs all of them and writes one JSON report:

```sh
cd benchmarks
sh game.sh                    # everything, at the inputs of the website
sh game.sh n-body fasta       # some programs
sh game.sh --quick            # tiny inputs: that everything builds, runs and agrees - not how fast it is
RUNS=9 VM_RUNS=5 sh game.sh   # more runs; the median is what the report says
```

- **Five languages.** TorbScript built with `torb build` (the release profile, `-O2`) and run in the VM with `torb run`,
  C built with `$TORB_CC` (gcc) and `-O2`, and the Python and JavaScript with `python3` and `node` where they are
  installed - a missing one is reported as missing, the rest is measured.
- **Two inputs per program.** A large one for C, the native binary and Node.js, and a small one for the VM and Python,
  which would take many minutes on the large one; C and the native binary run the small one too, so every ratio is
  against C on the same input.
- **Checked first.** Every language runs once to warm up, and its output is compared byte for byte with the C
  program's; a program whose output differs is reported as `wrong-output` and not timed.
- **Timed in turns.** Then every language runs `RUNS` times (`VM_RUNS` in the VM), one run each per round, so that a
  slow moment of the machine falls on all of them. The report has the median, the fastest and the slowest run.
- **Memory** is the maximum resident set size that GNU `/usr/bin/time` reports for the warm-up run, where it is
  installed; Windows has none, and the report says so.
- **`startup`** is a program that prints one line, in every language: what starting each one costs, which for the VM
  includes checking and compiling the program.

The report is `out/game/benchmarks.json` (`$BENCHMARKS_JSON`). Its fields:

| Field | What it is |
|-------|------------|
| `schema` | `1` |
| `origin` | `ci` for the forge's runner, `local` for anything else (`$BENCHMARKS_ORIGIN`) - the website warns about a report that is not `ci` |
| `measured`, `commit`, `release` | When (UTC), the commit of the programs, and the nightly or release the toolchain came from |
| `quick`, `runs`, `vmRuns` | The options of the run |
| `machine` | `system` (`uname -srm`), `distribution`, `processor`, `cores` (logical processors) and `memoryKibibytes` |
| `tools` | The version line of `torb`, the C compiler with `cFlags`, Python and Node.js, and how the memory was measured |
| `programs[].workloads[]` | Per program and input (`size`: `large`, `small` or `startup`; `input`), one result per language |
| `results[]` | `language`, `status` (`ok`, `missing`, `build-failed`, `failed`, `timeout`, `wrong-output`), `runs`, `median`, `minimum` and `maximum` in seconds, `memory` in KiB (0 when not measured) and a `note` |

**Where the website's numbers come from.** `.forgejo/workflows/benchmarks.yml` runs `game.sh` on the forge's Linux
runner after every nightly, with that nightly's toolchain, and publishes the report to the release `benchmarks` of the
forge: `benchmarks.json`, the newest, and `benchmarks-<nightly>.json` for every night. The site image fetches the newest
one when it is built (`tools/fetch-benchmarks.sh`), and `torb docs site` renders it
(`compiler/src/documentation/site-benchmarks.trb`). A report measured anywhere else - this machine, a laptop - can be
rendered locally the same way, and the page then says that it was not measured by the forge:

```sh
cp benchmarks/out/game/benchmarks.json build/benchmarks.json
build/release/torb docs site docs --output build/site
```
