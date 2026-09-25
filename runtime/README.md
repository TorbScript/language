# The TorbScript Runtime

Portable C11, no dependency beyond libc - except TLS, which is mbedTLS vendored under `vendor/` and linked only
into a program that reaches `std/tls`. Every binary `torb build` produces links against this, and the bytecode VM
calls the same functions through the same manifest - so there is exactly one `ArrayList`, one hash table and one
`String` in a process, whichever back end is running.

The plan this implements is [docs/BACKEND.md](../docs/BACKEND.md) sections 2 and 3. The contract with the compiler is
[`compiler/src/backend/c/natives.trb`](../compiler/src/backend/c/natives.trb).

## Running the tests

```sh
sh runtime/build.sh               # compiles the runtime and the tests, runs them, checks the generated header
TORB_CC=clang sh runtime/build.sh # any C11 compiler (default: clang, gcc, cc); $CFLAGS is added to the flags
```

Everything it writes goes to `build/runtime/` of the repository.

It works in Git Bash on Windows and on Linux and macOS, and `make` is not needed. Everything is built with
`-std=c11 -Wall -Wextra -Wpedantic -Werror`; a warning is a bug.

The harness counts live allocations and **every test must end at zero**, which is the leak check. A test that
deliberately panics says `TORB_IGNORE_LEAKS()`, because a panic runs nothing on the way out.

## Layout

| File                  | Contains                                                                                  |
|-----------------------|-------------------------------------------------------------------------------------------|
| `include/torb.h`      | The public ABI: the header, `torb_text`, `torb_list`, `torb_map`, the closure and object shapes, panics, allocation, element descriptors, console, process, files, `torb_file`, `Instant`/`Duration` |
| `include/torb_number.h` | The checked arithmetic of all eight integer widths as `static inline`, plus the conversions |
| `include/torb_natives.h` | Generated from the manifest by `torb natives --header`. Do not edit                      |
| `include/torb_task.h` | The task ABI of 7.3: the task block, the resume function a `Task` function is lowered to, the suspension primitives, `sleep`/`pause`/`cancel`/`within`, channels, the scheduler and the worker pool, the tests of what may cross a worker (`docs/design/CONCURRENCY.md` section 16) |
| `include/torb_network.h` | The natives of `std/network`: listen, accept, connect, receive, send, shutdown, close, addresses, name resolution, and the words of a failure - over handles, numbers and tasks of the runtime (docs/design/NETWORK.md section 2) |
| `include/torb_tls.h` | The natives of `std/tls`: a client or server session, an identity, feeding received bytes, the handshake, taking the bytes to send, reading and writing, `close_notify`, and the words of a failure (docs/design/NETWORK.md section 5) |
| `include/torb_io.h`   | What `io.c` and the pollers of `os/` share: the socket record, the operation, the packed failures, and the interface every poller implements. Never included by generated C |
| `include/torb_posix_io.h` | What `os/posix_io.c` shares with the two readiness pollers, `os/epoll.c` and `os/kqueue.c` |
| `include/torb_pool.h` | What the runtime's own files share about a worker: its heap counters, its scheduler, the threads, locks and conditions of `platform.c`, the atomics, the one-load `torb_worker_current`. Never included by generated C |
| `memory.c`            | Block header, non-atomic counts (atomic only for a `TORB_SHARED_COUNT` block while there are threads), retain/release/is-unique/make-unique, immortal values and the lock around building a constant, the live-block counters of every worker summed, the memory limit (`TORB_MEMORY_LIMIT`, the dev default, the count where the system takes no limit) |
| `task.c`              | The worker pool: a FIFO run queue per worker that is also its inbox, the stealing of unstarted tasks, the timer heaps, waking and cancelling across workers, the tasks the runtime writes itself (`sleep`, `pause`, `within`), channels, the end-of-program drain, the tasks of a test (waited for, a panic in one on any worker the test's failure), the blocking pool and its turn (`offload`), `Workers.count`, `Workers.blocking` |
| `io.c`                | The IO core (docs/design/NETWORK.md section 2): the handle table, operations with a count of their own, the tasks of the runtime a socket operation is, the resolver threads, and the stop of the core at the end of the program. The poller of each system is its file in `os/` |
| `panic.c`             | `torb_panic` and friends, the per-thread stack check, exit code 101 (102 for out of memory), the test hook, one panic at a time |
| `text.c`              | UTF-8, slices, concatenation, comparison, hashing, `Show`, float formatting, parsing       |
| `element.c`           | The callbacks every contextual element descriptor shares: the ones the VM makes at run time, which are told which descriptor they serve |
| `list.c`              | The one contiguous list: growth, shared slices, copy on write, a stable merge sort         |
| `map.c`               | The one insertion-ordered hash table, and the set on top of it                             |
| `number.c`            | The float routines, the total order of gap 5, the wrapping pair, the checked narrowings, `**`, and the `Real` members of a `Float64` (thin `<math.h>` wrappers) |
| `console.c`           | `print`, `printError`, `readLine`, `isTerminal`, `printErrorRaw` (a prompt, no line break), the Ctrl+C flag `torb repl` installs and polls |
| `process.c`           | `Process.arguments`, `Process.exit`, `Process.executablePath`, running a child process      |
| `file.c`              | `readText`, `writeText`, `exists`, `isDirectory`, `list` (sorted), `absolutePath`, and the open handle (`File.open`/`readAll`/`close`) |
| `clock.c`             | `std/time`: `Clock.now` and the arithmetic of `Instant` and `Duration`                     |
| `environment.c`       | `Environment` of `std/os`: `Environment.get` and `Environment.entries`                     |
| `platform.c`          | **The runtime's own portability layer, and the only file with an `#ifdef _WIN32` between its functions**: path kind, working directory, directory listing, opening and removing a file, whole-file read and write, running a child process, a monotonic clock reading, sleeping until a timer is due, the program's own arguments, reading an environment variable and setting one (for `runtime/tests` only), the memory limit of the process (a job object, `RLIMIT_DATA`, `RLIMIT_AS`), the physical memory and the size of a `malloc`ed block. Everything crosses it as UTF-8; the Windows half converts to UTF-16 and calls the wide API, because the narrow one is the code page of the machine |
| `include/torb_os.h`   | The prototypes of every native of `runtime/os/`, declared on every machine so a signature is compared with the manifest's everywhere |
| `os/<family>.c`       | The natives of `std/os` one family of systems has: `windows.c`, `linux.c`, `macos.c`, `freebsd.c`, `posix.c` for what Linux, macOS and FreeBSD share, and `bsd.c` for the `sysctl` interface of macOS and FreeBSD. **Each file is one `#if` from its first line after the includes to its last**, so every file is compiled on every machine and is empty where it does not belong, and no function has an `#ifdef` inside it. A row of the manifest names the systems its native exists on (`availableOn`), which is what keeps a call of one out of another system's build (docs/design/OS.md section 7) |
| `os/<poller>.c`       | The pollers of the IO core, one `#if` each like the family files: `iocp.c` (IOCP and Winsock, loaded on first use), `epoll.c`, `kqueue.c` (macOS and FreeBSD), and `posix_io.c` for the socket calls and the readiness logic the last two share |
| `tls/`                | The glue of `std/tls` over mbedTLS, compiled only into a program that calls a `torb_tls_` function (`-DTORB_WITH_TLS`): `tls.c` (sessions over two memory buffers, one lock around every mbedTLS call, randomness as mbedTLS's hardware source), `platform_windows.c` (the chain handed to `CertGetCertificateChain` and the SSL policy of crypt32), `platform_posix.c` (the system's bundle of roots), and `torb_mbedtls_user.h`, every change to mbedTLS's default configuration |
| `vendor/mbedtls/`     | mbedTLS 3.6 (`VERSION`), vendored whole and unchanged under its Apache-2.0 `LICENSE`; compiled once per checkout and C compiler into `build/vendor/` by the driver, into `build/runtime/mbedtls/` by `build.sh`, without `-Werror` |
| `tests/`              | `harness.h`/`harness.c` plus one `*_test.c` per area, one executable                       |

Not here yet, by design: the streams over files and pipes (`standardInput`, `Process.start`, `File.chunks`), which
`io.c` serves for sockets only so far (docs/design/NETWORK.md; the blocking pool `offload` runs on is in `task.c`). There is no `collect.c` and there will be none: the language has no
cycle collector (`docs/design/DESTRUCTORS.md` section 9). `File.lines` is also still
`.Planned`, for 5.7 rather than 5.12: it answers `Result<Iterable<String>, IoError>`, and `Iterable` is a trait-typed
value over an iterator type - the ABI 5.7 defines. Reading the whole file to fake a streaming iterator now would mean
inventing that ABI early and probably wrong, so it waits. The manifest records every one of these as `.Planned` with
its milestone, so using one is a compile error naming the milestone and never a link error.

## The ABI in one page

**Every symbol is `torb_`.** Every function of the public ABI carries a comment stating the ownership of each
parameter and of the result, in the words of BACKEND section 2:

- **borrowed** - the function reads the value, the count does not change, and the caller keeps it alive for the call.
  Parameters are borrowed by default, which is what removes a retain/release pair from every call.
- **owned** - the result carries a count the caller now owns and must release exactly once.
- **consumed** - the function takes over the count of that argument; the caller must not release it afterwards.
  The manifest's `ownedParameters` says which parameters of a native these are.

### The header every counted block starts with

```c
typedef struct torb_header { uint32_t count; uint16_t kind; uint16_t color; } torb_header;
```

Counts are plain integers: nothing a worker can reach is reachable from another one, so nothing has to be atomic - except
the blocks with `TORB_SHARED_COUNT` (a task, a channel, a closure environment that may cross), whose counts change
atomically while the pool runs threads. `count == TORB_IMMORTAL_COUNT`
marks static data, which is never retained, released or freed - so a write through a static value always copies.
`color` is `TORB_COLOR_NONE` for every block: it was reserved for a cycle collector, and there will be none.

### The values

```c
typedef struct torb_bytes { torb_header header; uint32_t capacity; uint8_t data[]; } torb_bytes;
typedef struct torb_text  { torb_bytes *storage; uint32_t offset; uint32_t length; } torb_text;
typedef struct torb_list  { torb_list_storage *storage; uint32_t offset; uint32_t length; } torb_list;
typedef struct torb_map   { torb_map_storage *storage; } torb_map;
typedef torb_map torb_set;
typedef uint32_t torb_char;
```

A `String` is a slice of one counted `torb_bytes`, so `text[3..]` is O(1) and shares the storage and `byteLength()` is
a field read. A literal is an immortal `torb_bytes` in read-only data (`TORB_LITERAL_BYTES`). A list is the same shape
over a contiguous buffer. A map is one pointer, because an insertion-ordered table needs two side buffers and a value
has no room for them.

### The generic containers

There are no templates. One list and one hash table, parameterized by a static descriptor the compiler emits per
element type:

```c
typedef struct torb_element {
  uint32_t size, align;
  void (*retain)(void *);          /* NULL when the element is trivial */
  void (*release)(void *);
  bool (*equals)(const void *, const void *);
  uint64_t (*hash)(const void *);
} torb_element;
```

One indirect call per element retain, skipped entirely when the pointer is `NULL`. The VM uses the same descriptors.
The runtime brings three of its own: `torb_element_text`, `torb_element_int64` and `torb_element_unit` (size zero, the
value side of a set).

### The twenty calls the C emitter makes most

```c
void      torb_retain(void *block);
void      torb_release(void *block, torb_drop_function drop);
void     *torb_make_unique(void *block, size_t size, torb_retain_children_function retain, torb_drop_function drop);
void     *torb_allocate(size_t size, torb_block_kind kind);
_Noreturn void torb_panic(torb_text message, torb_location at);
int64_t   torb_add_i64(int64_t a, int64_t b, torb_location at);        /* and the other seven widths, inline */
torb_text torb_text_from_storage(const void *storage, uint32_t offset, uint32_t length);
torb_text torb_text_concat(const torb_text *parts, size_t count);     /* Intrinsic.TextConcat */
torb_text torb_text_slice(torb_text text, int64_t from, int64_t to, torb_location at);
bool      torb_text_equal(torb_text first, torb_text second);
uint64_t  torb_text_hash(torb_text text);
torb_text torb_show_i64(int64_t value);                               /* and _f64, _bool, _char, _u64, _void */
torb_list torb_list_new(const torb_element *element);
int64_t   torb_list_length(torb_list list);
const void *torb_list_at(torb_list list, int64_t index, torb_location at);
void      torb_list_add(torb_list *list, const void *value);          /* value consumed */
void     *torb_list_element_reference(torb_list *list, int64_t index, torb_location at);  /* the Element path step */
torb_map  torb_map_new(const torb_element *key, const torb_element *value);
void      torb_map_set(torb_map *map, const void *key, const void *value);   /* both consumed */
bool      torb_map_next(torb_map map, uint32_t *cursor, const void **key, const void **value);
void      torb_print_parts(const torb_text *parts, size_t count);
```

Two conventions follow from "a runtime function may not build a type of the program":

- A native whose result is an `Option` or a `Result` is a function that answers `bool` and writes the payload through
  an out parameter (`torb_list_get`, `torb_text_index_of`, `torb_file_read_text`). The lowering builds the `Option`,
  the `Result` and the `IoError` around it.
- A native whose result is `Ordering` is `.Derived` in the manifest: the lowering generates the comparisons and the
  `Construct`.

## Decisions worth knowing

**Panics.** `panic: <message>` and then `  at path/file.trb:12:5` to stderr, exit code 101, and nothing else runs:
no release, no `Close`, no destructor (decided gap 9). The message is rendered into a fixed buffer, never allocated,
so a panic still works when the heap is exhausted. Running out of memory prints the same way and leaves with 102, past
every recovery point (below). `_exit` is used where it exists; where it does not, `exit` is the
same thing because the runtime registers no `atexit` handler.

**Every process has a memory limit, and the operating system holds it.** A test binary once committed 88 GB and Windows
paged the machine to death, so `torb_process_start` sets a limit before the program runs (`torb_memory_limit_start`,
memory.c): `TORB_MEMORY_LIMIT` where it is set (bytes, or `512M`, `8G`; `0` or `none` for none; anything else is exit
code 2), otherwise the smaller of 8 GiB and half the physical memory for a binary of the dev profile - `torb build`
compiles what `torb test` and `torb run` build with `-DTORB_PROFILE_DEV` - and none for a release binary, the compiler
among them. The system enforces it (`torb_platform_limit_memory`):

- **Windows**: a job object of the process's own with `JOB_OBJECT_LIMIT_PROCESS_MEMORY`, nested inside whatever job the
  process is already in (Windows 8 and later), with `JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK` so that a child - a C
  compiler - is not held to it. What counts is committed memory: a commit over the limit fails and `malloc` answers
  `NULL`.
- **Linux**: `RLIMIT_DATA`, which since 4.7 counts every writable private mapping (the heap, `mmap`ed blocks, thread
  stacks) and not the address space glibc only reserves - `RLIMIT_AS` would count 64 MiB per malloc arena and fail a
  program with many threads that holds little.
- **FreeBSD**: `RLIMIT_AS`, because its `RLIMIT_DATA` counts `brk` alone and its allocator maps.
- **macOS**: no system limit fits (`RLIMIT_DATA` is not held against `mmap`, `RLIMIT_AS` against an address space that
  already holds the shared cache), so the runtime counts.

On POSIX a lowered limit is inherited, so a child started with `fork` gets the old one back before `exec`, and one
started through the shell gets it back from a `ulimit` in front of its command line. **Where the system does not take
the limit**, the runtime counts instead: every block of `torb_allocate` and `torb_raw_allocate` at the size the C
allocator gave it (`_msize`, `malloc_usable_size`, `malloc_size`), atomically, and only while counting is on - one test
of a flag per allocation and free otherwise. That sees the runtime's blocks and not the C library's, and it says so in
one line on stderr where the limit was asked for explicitly. Either way the allocation over the limit ends the program
with `panic: out of memory: the limit of 64 MiB was reached (TORB_MEMORY_LIMIT)` and exit code 102
(`tests/conformance/memory-limit.trb`). No recovery point catches it: a recovered panic frees nothing, so every test
after it would start at the limit. A program the VM runs is inside `torb`, and has the limit `torb` has.

**Console output on Windows goes through `WriteConsoleW`, only where the target is a live console.** `print`,
`printError` and `readLine` write and read raw UTF-8 bytes everywhere else, exactly as before; where the standard
handle is a real console (`GetConsoleMode`, asked once per stream and cached), the text crosses to UTF-16 through
`torb_platform_wide`/`torb_platform_utf8` and is written or read with `WriteConsoleW`/`ReadConsoleW` instead, because
the console's own code page - not `SetConsoleOutputCP`, which is the console's own setting and would outlive this
process - is what a byte-for-byte write would otherwise be at the mercy of (`docs/BACKEND.md`, "What the boundary to
the operating system decided"). A pipe or a file is never a console, so the conformance suite's byte comparison never
takes this path.

**Overflow.** Checked in every profile, because it is semantics and not a diagnostic. The 64 bit operations use
`__builtin_*_overflow` where it exists and a portable bit test otherwise, selected by `TORB_HAS_OVERFLOW_BUILTINS`,
so MSVC works. The narrow widths compute in 64 bits and check the range, which is exact and needs no builtin.
Division truncates toward zero, the remainder takes the sign of the dividend, and `x / 0` and `smallest / -1` panic
(decided gap 2). A shift by a negative amount or by the width or more panics; bits that leave the type are dropped
(decided gap 3).

**Float formatting is correctness first.** `torb_show_f64` asks `printf("%.*e")` for 1 up to 17 significant digits and
takes the first answer `strtod` turns back into the identical bit pattern. That is by construction the shortest
round-tripping decimal (decided gap 4), and 17 digits always round-trip, so the loop always ends. A Ryu or Grisu
routine would be faster and has to produce exactly the same strings; this one is the reference it is tested against,
by a table of tricky values and a round-trip property over 20 000 pseudo-random bit patterns with a fixed seed.
`%e` is asked rather than `%g` because `%g` picks its notation from the precision it was given, which would make
`100.0` come out as `1e2`. The notation is decided here instead, from the decimal exponent: the exponent form below
-6 and at 21 and above, the plain form in between, and `.0` appended when the result carries neither `.` nor `e`.

**Determinism.** The hash seed is the FNV-1a-64 offset basis and never varies. Map and set iteration is insertion
order, for every implementation (decided gap 6). Directory listings are sorted by bytes. `torb_list_sort` is a stable
merge sort, so the order does not depend on what the comparison does with equal keys. The fixpoint test of the
compiler rests on all four.

**Copy on write, exactly once.** A write to a list or a map prepares the storage first: in place when this value owns
the whole storage alone, one copy otherwise. A slice never owns the whole storage, so writing through a slice copies -
which is what keeps `samples[0..4]` from reaching its parent. `runtime/tests/list_test.c` and `map_test.c` pin the
number of allocations, because from TorbScript the difference is invisible.

**Panics in the tests are caught in process.** `TORB_EXPECT_PANIC(expression)` installs a panic hook that `longjmp`s
out of the panicking call, so the message can be compared and the suite goes on. Child processes would be the other
way; this one is portable (no `fork`, no `CreateProcess`), identical on Windows and POSIX, and keeps everything in one
binary. `torb build` never installs a hook, so a panic in a real program is a panic.

**An empty list still allocates.** `torb_list` has no room for the element descriptor, so an empty list carries a
storage of capacity zero. Shrinking that away would mean a fourth word in every list value.

**A text and a list are at most 4 GiB and 2^32 elements.** `offset` and `length` are `uint32_t`, which keeps
`torb_text` at 16 bytes. Both limits panic rather than wrap.

**`Instant` and `Duration` are nanoseconds in an `Int64`, and records of the program.** `std/time` declares them as
ordinary types over one private `Int64` field, so comparing, subtracting and showing them is TorbScript and the runtime
only reads the clock: `torb_clock_now` answers one monotonic reading in nanoseconds from an unspecified per-process
origin (only a difference of two is ever meaningful). About 292 years fit before an `int64_t` nanosecond count
overflows, which a monotonic clock within one process never approaches, and the checked `Int64` arithmetic panics
rather than wraps in that never-reached case. `torb_instant` and `torb_duration` stay the runtime's names for the
deadlines and limits of `task.c`, and a `Duration` crosses into it as its number of nanoseconds.

**`Real` of a `Float64` is thin wrappers over `<math.h>`, nothing more.** A domain error (`naturalLogarithm()` of `-1.0`,
`arcSine()` of `2.0`)
answers `nan` the way libm already does, never a panic - the same rule `Float64.squareRoot` follows. Bit-identical
results across platforms hold only where libm itself guarantees them; the runtime does not add a portable
implementation on top to fix that, which is a real (if narrow) gap in the conformance suite's cross-platform promise.

**`File` is a `shared type` the runtime owns end to end.** `torb_file` (torb.h) is a counted block like any other -
header first, so the generic `torb_retain`/`torb_release` already work on it - holding the path (for error messages)
and the OS handle. The language has no destructors, so `torb_file_drop`, the block's `torb_drop_function`, closes the
handle if the program never called `close`: a `File` a program drops on the floor still cannot leak the OS handle.
`close` is idempotent and reading a closed file is the ordinary `Result`/`IoError` path, never a panic.
`runtime/tests/file_test.c` proves the handle is not leaked by releasing an unclosed `File` and then deleting its
file - which an OS that locks open files (Windows) would refuse if the handle were still open.

## What is deliberately approximate

- **Case mapping and character classification are ASCII plus the letters of Latin-1.** `toUpperCase` and `toLowerCase`
  are the *simple* mapping of one code point to one code point over ASCII, over the letters of Latin-1 (`ä` to `Ä`,
  without `×` and `÷`, which are symbols) and over the one pair that reaches out of Latin-1 (`ÿ` to `Ÿ`); every other
  code point is answered unchanged, `ß` included, because its uppercase is `SS` and a `Char` holds one code point.
  `torb_text_to_upper_case` is that mapping per character, so a mapped text is exactly as many bytes as it was.
  `isDigit` is ASCII `0`-`9`; above ASCII `isWhitespace` knows the common `White_Space` code points and `isLetter`
  answers true for everything that is not Latin-1 punctuation or a symbol. Full Unicode tables are milestone 8, and
  with them a `String.toUpperCase` that may make a text longer. **`tests/conformance/character-case.trb`
  pins all five from the language side**, so a change to the table here is a change to that program. The compiler's own
  identifiers and keywords are ASCII.
- **The heap is libc's `malloc`.** The bump allocator with size-class free lists of BACKEND 2.5 is a replacement
  behind `torb_allocate` and changes nothing above it. A worker's heap is its counters: a block moves to another worker
  by being re-homed; a value somebody else holds too is first copied by the thread that starts the task
  (`torb_text_privatize` and its siblings, `task.c`, "What crosses a worker").
- **No small-string optimization**, on purpose: it doubles the code path of every string operation for a win the
  compiler does not need, whose strings are slices of source files.
- **A child process goes through a shell on POSIX and through none on Windows.** `Process.run` is `CreateProcess` plus one
  pipe on Windows - `_popen` could not work, because `cmd` re-parses the quotes of the command line by a rule that
  depends on where the first quote stands, and `"gcc" "--version"` arrives as one command *named* `gcc" "--version`. On
  POSIX it is still `popen`, which is `/bin/sh -c`, with every argument in single quotes so that nothing inside one is
  interpreted. Two consequences of the difference, both on 5.14's list: a program that cannot be started at all is a
  **failure** on Windows (what `std/process` promises) and the shell's own exit code on POSIX, and `fork` plus `execvp`
  is what would make POSIX shell free as well - which is `Process.start`'s job at 7.3.
- **A `String` printed to a live Windows console truncates at an embedded NUL byte.** `torb_platform_wide` is NUL
  terminated, because every other caller of it hands it a path, which never holds one; `print` reuses it rather than
  duplicating the conversion, so the rare `String` built with a NUL inside it (`U+0000` is otherwise ordinary UTF-8)
  stops there on that one target. A pipe or a file gets the whole `String` regardless, because that path never
  converts at all - the gap is only ever visible on the console a byte-for-byte write was already wrong on.
