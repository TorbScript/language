#!/bin/sh
# The conformance suite, run with the native compiler alone.
#
# Every program under `tests/conformance/` (and `binary-only/`) is built with `build/release/torb build`, run,
# and compared against its expectation files - `.expected` (standard output), `.stderr` (standard error, folded so a
# line of `std/` reads `path:_:_` instead of its real position), `.exit` (the exit code) and `.leaks` (why the leak
# gate does not apply); `.environment` (`NAME=value` lines) is set for every run of the binary, and `.input` is its
# standard input (otherwise it reads an empty one). A file that is missing means "nothing to check" for
# `.expected`/`.exit`, and "must be empty" for `.stderr`. `tests/conformance/README.md` is the contract.
#
# Two more things are asserted per program:
#   - the generated C names no absolute path of this machine
#   - `--emit-c` twice gives byte-identical C (a pure function of the program)
# and, unless a `.stderr` or `.leaks` file exempts the program, that the binary frees everything it allocated
# (`TORB_REPORT_LEAKS=1`, "live blocks at exit: 0").
#
# A behaviour the native back end cannot produce yet has no program here at all - it belongs in `compiler/tests/` as
# an IR snapshot until the back end gap is closed (`tests/conformance/README.md`, "Adding a program").
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/conformance.sh
#   sh tools/conformance.sh --jobs 8
#   sh tools/conformance.sh --filter closures
#   sh tools/conformance.sh --update             # rewrite .expected/.stderr/.exit from a native run
#   sh tools/conformance.sh --vm                 # the same programs, run by the VM
#   sh tools/conformance.sh --torb build/x/torb  # another compiler than build/release/torb
#
# `--vm` is the second leg of docs/design/VM.md section 8: every program of the suite, `binary-only/` included, is run
# with `torb run --vm` and compared against the same `.expected`/`.stderr`/`.exit` as its native run, byte for byte,
# and so is every program of `vm-only/` (one with an `.expected` file: the others are the receiver scripts it loads),
# which only the VM runs. The leak gate applies as it does natively - the kernel counts the
# program's blocks apart from torb's own - and so does a `.workers` file: the VM's tasks run on the pool of `torb`,
# with as many workers as it names and again with one. The two checks of the C do not apply to it.
#
# **How many at a time, and in which order.** One program per processor and per 768 MiB of memory (`tools/machine.sh`)
# unless `--jobs` or `$TORB_CONFORMANCE_JOBS` says otherwise, and never more than the machine has processors: building
# a program is the compiler's front end and a C compile, one processor each. The slowest programs start first:
# the wall time of every program's last run is kept per back end in `torb-conformance-timings/` under the system's
# temporary directory (`$TORB_CONFORMANCE_TIMINGS` names another directory), shared by every checkout of the machine,
# and the next run starts them longest first, a program without a time before all of them - so the run does not end
# with one long program on one processor while the others idle. Without the file the order is the names'. The two
# programs that build something once per checkout go first always (below, at `pinned`).

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "conformance.sh: $*"
  exit 1
}

# A C compiler on Windows appends `.exe` to an output name without an extension, so both names are the binary.
binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
}

# Waits while the machine is short of memory, before a program is built and run: less than `$TORB_MINIMUM_FREE_MB`
# (default 2048) available, as `/proc/meminfo` says where there is one (Linux, and Git Bash, which reads it from
# Windows). A run of the suite goes on at its own pace while something else takes the memory, instead of adding to it.
# After five minutes it goes on anyway, so a machine that is always this full still finishes.
wait_for_room() {
  [ -r /proc/meminfo ] || return 0
  minimum=${TORB_MINIMUM_FREE_MB:-2048}
  case "$minimum" in
    '' | *[!0-9]*) minimum=2048 ;;
  esac
  waited=0
  while [ "$waited" -lt 300 ]; do
    available=$(awk '/^MemAvailable:/ { print $2; exit } /^MemFree:/ { free = $2 } END { if (free != "") print free }' \
      /proc/meminfo | head -n 1)
    case "$available" in
      '' | *[!0-9]*) return 0 ;;
    esac
    [ "$available" -lt $((minimum * 1024)) ] || return 0
    sleep 2
    waited=$((waited + 2))
  done
}

# ----------------------------------------------------------------------------- one program -------------------------
#
# Invoked as `sh tools/conformance.sh --run-one <program>`, once per program, so that `xargs -P` can run several at
# once without this script needing anything beyond POSIX `sh` to do it. `$CONFORMANCE_SCRATCH`, `$CONFORMANCE_TORB`
# and `$CONFORMANCE_UPDATE` are environment variables set by the parent invocation below - inherited by any shell,
# unlike a shell function passed through `xargs`.
run_one() {
  program=$1
  name=$(printf '%s' "$program" | sed 's#[/\\]#_#g; s/\.trb$//')
  work="$CONFORMANCE_SCRATCH/work/$name"
  rm -rf "$work"
  mkdir -p "$work/again"
  result="$CONFORMANCE_SCRATCH/results/$name"
  wait_for_room
  began=$(date +%s)

  target="$work/prog"
  if ! "$CONFORMANCE_TORB" build "$program" --output "$target" >"$work/build.log" 2>&1; then
    {
      echo "FAIL"
      echo "torb build $program failed:"
      cat "$work/build.log"
    } >"$result"
    return
  fi

  # A drive path is a letter, a colon and a slash with no letter in front of it: `C:/x` and `"D:\x"` are, the `p:/` of
  # an `http://` in a string literal is not
  cfile="$work/program.c"
  bad_path=0
  if [ -f "$cfile" ] && { grep -qF "$root" "$cfile" || grep -qE '(^|[^A-Za-z])[A-Za-z]:[\\/]' "$cfile"; }; then
    bad_path=1
  fi

  binary=$(binary_of "$target")
  if [ -z "$binary" ]; then
    { echo "FAIL"; echo "torb build $program reported success but wrote no binary"; } >"$result"
    return
  fi

  # One worker, the order the suite pins, unless a `.workers` file names how many (`all`: one per processor) - and then
  # the program runs a second time with one worker below and has to print the same bytes (tests/conformance/README.md)
  workers_file="${program%.trb}.workers"
  workers=1
  if [ -f "$workers_file" ]; then
    workers=$(tr -d ' \t\r\n' <"$workers_file")
  fi
  if [ "$workers" = "all" ]; then
    unset TORB_WORKERS
  else
    TORB_WORKERS=$workers
    export TORB_WORKERS
  fi

  # The variables of a `.environment` file, one `NAME=value` per line, are set for every run of the binary and never
  # for its build: `TORB_MEMORY_LIMIT=64M` limits the program and not the compiler that builds it
  environment_file="${program%.trb}.environment"
  settings=""
  if [ -f "$environment_file" ]; then
    settings=$(sed 's/\r$//; s/#.*//; /^[[:space:]]*$/d' "$environment_file" | tr '\n' ' ')
  fi
  input=$(input_of "$program")

  # The program runs in its own work directory, so a program that writes files (`build/native-*`) writes them there and
  # never into the checkout, and two programs never share one
  mkdir -p "$work/run"
  set +e
  # shellcheck disable=SC2086
  (cd "$work/run" && env $settings "$binary" <"$input" >"$work/stdout" 2>"$work/stderr")
  code=$?
  set -e
  fold_library_positions <"$work/stderr" >"$work/stderr.folded"

  expected_file="${program%.trb}.expected"
  exit_file="${program%.trb}.exit"
  stderr_file="${program%.trb}.stderr"
  leaks_file="${program%.trb}.leaks"

  if [ "$CONFORMANCE_UPDATE" = "1" ]; then
    if [ -s "$work/stdout" ] || [ -f "$expected_file" ]; then
      cp "$work/stdout" "$expected_file"
    fi
    printf '%s' "$code" >"$exit_file"
    if [ -s "$work/stderr.folded" ]; then
      cp "$work/stderr.folded" "$stderr_file"
    elif [ -f "$stderr_file" ]; then
      rm -f "$stderr_file"
    fi
    echo "PASS" >"$result"
    return
  fi

  problems=""

  if [ $bad_path -eq 1 ]; then
    problems="$problems
the generated C of $program contains an absolute path of this machine"
  fi

  if [ -f "$expected_file" ]; then
    fold_crlf <"$expected_file" >"$work/expected.norm"
    if ! cmp -s "$work/expected.norm" "$work/stdout"; then
      problems="$problems
unexpected standard output:
$(diff -u "$work/expected.norm" "$work/stdout" 2>&1 || true)"
    fi
  fi

  if [ -f "$workers_file" ] && [ "$workers" != "1" ] && [ -f "$expected_file" ]; then
    set +e
    # shellcheck disable=SC2086
    (cd "$work/run" && env $settings TORB_WORKERS=1 "$binary" <"$input" >"$work/stdout.one" 2>"$work/stderr.one")
    set -e
    if ! cmp -s "$work/expected.norm" "$work/stdout.one"; then
      problems="$problems
with one worker the standard output is not the same:
$(diff -u "$work/expected.norm" "$work/stdout.one" 2>&1 || true)"
    fi
  fi

  if [ -f "$exit_file" ]; then
    want=$(tr -d ' \t\r\n' <"$exit_file")
    if [ "$code" != "$want" ]; then
      problems="$problems
left with exit code $code, expected $want"
    fi
  fi

  if [ -f "$stderr_file" ]; then
    fold_crlf <"$stderr_file" >"$work/stderr.expected.norm"
    if ! cmp -s "$work/stderr.expected.norm" "$work/stderr.folded"; then
      problems="$problems
unexpected standard error:
$(diff -u "$work/stderr.expected.norm" "$work/stderr.folded" 2>&1 || true)"
    fi
  elif [ -s "$work/stderr.folded" ]; then
    problems="$problems
wrote to stderr and has no .stderr file:
$(cat "$work/stderr.folded")"
  fi

  # The leak gate: a program that has a `.stderr` or a `.leaks` file already promises that it panics or recovers one,
  # and a panic frees nothing on the way out - the file is the exemption, `tests/conformance/README.md` says why.
  # Every program of `binary-only/` ends in a recovered panic as well (a failing test), reported on standard output
  # rather than standard error, so the README exempts the whole directory instead of one file at a time.
  case "$program" in
    */binary-only/*) is_binary_only=1 ;;
    *) is_binary_only=0 ;;
  esac
  if [ "$is_binary_only" -eq 0 ] && [ ! -f "$stderr_file" ] && [ ! -f "$leaks_file" ]; then
    set +e
    # shellcheck disable=SC2086
    (cd "$work/run" && env $settings TORB_REPORT_LEAKS=1 "$binary" <"$input" >"$work/leak.stdout" 2>"$work/leak.stderr")
    set -e
    if ! grep -q 'live blocks at exit: 0$' "$work/leak.stderr"; then
      problems="$problems
does not report zero live blocks:
$(cat "$work/leak.stderr")"
    fi
    if ! grep -q 'immortal blocks at exit: ' "$work/leak.stderr"; then
      problems="$problems
does not report its immortal blocks:
$(cat "$work/leak.stderr")"
    fi
  fi

  # `--emit-c` is a pure function of the program: building it a second time gives the same bytes.
  if "$CONFORMANCE_TORB" build "$program" --emit-c --output "$work/again/prog" >"$work/build-again.log" 2>&1; then
    if ! cmp -s "$cfile" "$work/again/program.c"; then
      problems="$problems
the C of $program is not the same the second time"
    fi
  else
    problems="$problems
torb build --emit-c $program failed the second time:
$(cat "$work/build-again.log")"
  fi

  if [ -z "$problems" ]; then
    echo "PASS" >"$result"
  else
    { echo "FAIL"; printf '%s\n' "$problems"; } >"$result"
  fi
}

# `\r\n` folded to `\n`: git may check an expectation file out with either line ending, and what it holds is the text
# of the expectation and not its bytes on this machine. Nothing a program *wrote* is folded this way.
fold_crlf() {
  sed 's/\r$//'
}

# `  at std/core/src/option.trb:12:3` becomes `  at std/core/src/option.trb:_:_`, because a line of the standard
# library moves whenever a comment above it is edited and what a program promises is which file panicked. A frame of
# the program itself keeps its exact position.
fold_library_positions() {
  awk '{
    line = $0
    sub(/\r$/, "", line)
    trimmed = line
    sub(/^[ \t]*/, "", trimmed)
    if (trimmed ~ /^at std\//) {
      base = line
      if (match(base, /:[0-9]+:[0-9]+$/)) {
        base = substr(base, 1, RSTART - 1)
      }
      print base ":_:_"
    } else {
      print line
    }
  }'
}

# ----------------------------------------------------------------------------- one program in the VM ----------------
#
# The standard input of a program's runs: its `.input` file, as an absolute path because the runs go into a work
# directory, or an empty input - never the terminal or the pipe the suite itself was started with.
input_of() {
  if [ -f "${1%.trb}.input" ]; then
    printf '%s\n' "$CONFORMANCE_ROOT/${1%.trb}.input"
  else
    printf '%s\n' /dev/null
  fi
}

# The same comparison for `--vm`: the program is run by `torb run --vm` in a work directory of its own, and its three
# outputs are compared with the expectation files of its native run. Nothing is built, so there is no C to check.
run_one_vm() {
  program=$1
  name=$(printf '%s' "$program" | sed 's#[/\\]#_#g; s/\.trb$//')
  work="$CONFORMANCE_SCRATCH/work/$name"
  rm -rf "$work"
  mkdir -p "$work/run"
  result="$CONFORMANCE_SCRATCH/results/$name"
  absolute="$CONFORMANCE_ROOT/$program"
  wait_for_room
  began=$(date +%s)

  # The workers of the pool `torb` runs the program's tasks on: one, unless a `.workers` file names how many - and then
  # once more with one, which has to print the same bytes, exactly as the native run does
  workers_file="${program%.trb}.workers"
  workers=1
  if [ -f "$workers_file" ]; then
    workers=$(tr -d ' \t\r\n' <"$workers_file")
  fi
  if [ "$workers" = "all" ]; then
    unset TORB_WORKERS
  else
    TORB_WORKERS=$workers
    export TORB_WORKERS
  fi

  # The variables of a `.environment` file reach the program as they reach its native binary: `torb` hands a
  # `TORB_MEMORY_LIMIT` to the program it interprets and does not keep it for itself (runtime/memory.c)
  environment_file="${program%.trb}.environment"
  settings=""
  if [ -f "$environment_file" ]; then
    settings=$(sed 's/\r$//; s/#.*//; /^[[:space:]]*$/d' "$environment_file" | tr '\n' ' ')
  fi
  input=$(input_of "$program")

  set +e
  # shellcheck disable=SC2086
  (cd "$work/run" && env $settings "$CONFORMANCE_TORB" run --vm "$absolute" <"$input" >"$work/stdout" 2>"$work/stderr")
  code=$?
  set -e
  fold_library_positions <"$work/stderr" >"$work/stderr.folded"

  expected_file="${program%.trb}.expected"
  exit_file="${program%.trb}.exit"
  stderr_file="${program%.trb}.stderr"
  problems=""

  if [ -f "$expected_file" ]; then
    fold_crlf <"$expected_file" >"$work/expected.norm"
    if ! cmp -s "$work/expected.norm" "$work/stdout"; then
      problems="$problems
unexpected standard output in the VM:
$(diff -u "$work/expected.norm" "$work/stdout" 2>&1 || true)"
    fi
  fi

  if [ -f "$workers_file" ] && [ "$workers" != "1" ] && [ -f "$expected_file" ]; then
    set +e
    # shellcheck disable=SC2086
    (cd "$work/run" && env $settings TORB_WORKERS=1 "$CONFORMANCE_TORB" run --vm "$absolute" <"$input" >"$work/stdout.one" 2>"$work/stderr.one")
    set -e
    if ! cmp -s "$work/expected.norm" "$work/stdout.one"; then
      problems="$problems
with one worker the standard output in the VM is not the same:
$(diff -u "$work/expected.norm" "$work/stdout.one" 2>&1 || true)"
    fi
  fi
  if [ -f "$exit_file" ]; then
    want=$(tr -d ' \t\r\n' <"$exit_file")
    if [ "$code" != "$want" ]; then
      problems="$problems
left the VM with exit code $code, expected $want"
    fi
  fi
  if [ -f "$stderr_file" ]; then
    fold_crlf <"$stderr_file" >"$work/stderr.expected.norm"
    if ! cmp -s "$work/stderr.expected.norm" "$work/stderr.folded"; then
      problems="$problems
unexpected standard error in the VM:
$(diff -u "$work/stderr.expected.norm" "$work/stderr.folded" 2>&1 || true)"
    fi
  elif [ -s "$work/stderr.folded" ]; then
    problems="$problems
wrote to stderr in the VM and has no .stderr file:
$(cat "$work/stderr.folded")"
  fi

  # The leak gate in the VM: the kernel counts the program's blocks apart from those of the `torb` it runs in, so the
  # report is the program's alone (docs/design/VM.md section 8), and a `.stderr` or a `.leaks` file exempts it here as
  # it does natively.
  leaks_file="${program%.trb}.leaks"
  case "$program" in
    */binary-only/*) is_binary_only=1 ;;
    *) is_binary_only=0 ;;
  esac
  if [ "$is_binary_only" -eq 0 ] && [ ! -f "$stderr_file" ] && [ ! -f "$leaks_file" ]; then
    set +e
    # shellcheck disable=SC2086
    (cd "$work/run" && env $settings TORB_REPORT_LEAKS=1 "$CONFORMANCE_TORB" run --vm "$absolute" <"$input" >"$work/leak.stdout" 2>"$work/leak.stderr")
    set -e
    if ! grep -q 'live blocks at exit: 0$' "$work/leak.stderr"; then
      problems="$problems
does not report zero live blocks in the VM:
$(cat "$work/leak.stderr")"
    fi
    if ! grep -q 'immortal blocks at exit: ' "$work/leak.stderr"; then
      problems="$problems
does not report its immortal blocks in the VM:
$(cat "$work/leak.stderr")"
    fi
  fi

  if [ -z "$problems" ]; then
    echo "PASS" >"$result"
  else
    { echo "FAIL"; printf '%s\n' "$problems"; } >"$result"
  fi
}

# The wall time of one program, from after `wait_for_room` to its result, for the order of the next run
record_time() {
  name=$(printf '%s' "$1" | sed 's#[/\\]#_#g; s/\.trb$//')
  printf '%s %s\n' "$(($(date +%s) - began))" "$1" >"$CONFORMANCE_SCRATCH/times/$name"
}

if [ "${1-}" = "--run-one" ]; then
  began=$(date +%s)
  run_one "$2"
  record_time "$2"
  exit 0
fi

if [ "${1-}" = "--run-one-vm" ]; then
  began=$(date +%s)
  run_one_vm "$2"
  record_time "$2"
  exit 0
fi

# ----------------------------------------------------------------------------- the driver ---------------------------

. "$root/tools/machine.sh"
processors=$(machine_processors)

# One program per processor, and no more than one per 768 MiB of memory: a program is a front end and a C compile of a
# few hundred MiB each, and `wait_for_room` still holds one back while memory is short. `TORB_CONFORMANCE_JOBS` or
# `--jobs` says otherwise.
jobs=${TORB_CONFORMANCE_JOBS:-$(machine_jobs 768)}
filter=""
update=0
machine=0
chosen_torb="build/release/torb"

while [ $# -gt 0 ]; do
  case "$1" in
    --jobs)
      jobs=$2
      shift 2
      ;;
    --jobs=*)
      jobs=${1#--jobs=}
      shift
      ;;
    --filter)
      filter=$2
      shift 2
      ;;
    --filter=*)
      filter=${1#--filter=}
      shift
      ;;
    --update)
      update=1
      shift
      ;;
    --vm)
      machine=1
      shift
      ;;
    --torb)
      chosen_torb=$2
      shift 2
      ;;
    -h | --help)
      say "usage: sh tools/conformance.sh [--jobs N] [--filter substring] [--update] [--vm] [--torb path]"
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

# Never more programs at a time than the machine has processors, whatever `--jobs` says: each one is a C compile
case "$jobs" in
  '' | *[!0-9]* | 0) fail "--jobs needs a whole number above zero, and it is \`$jobs\`" ;;
esac
if [ "$jobs" -gt "$processors" ]; then
  jobs=$processors
fi

torb=$(binary_of "$chosen_torb")
[ -n "$torb" ] || fail "no native compiler at $chosen_torb - run: sh tools/bootstrap.sh"
torb=$(cd "$(dirname "$torb")" && pwd)/$(basename "$torb")

directory="tests/conformance"
[ -d "$directory" ] || fail "no $directory"

list=$(mktemp)
trap 'rm -f "$list"' EXIT

if [ "$machine" = "1" ]; then
  [ "$update" = "0" ] || fail "--update rewrites the expectations from a native run, never from the VM"
  candidates=$(ls "$directory"/*.trb "$directory"/binary-only/*.trb 2>/dev/null)
  for script in "$directory"/vm-only/*.trb; do
    if [ -f "${script%.trb}.expected" ]; then
      candidates="$candidates $script"
    fi
  done
else
  candidates=$(ls "$directory"/*.trb "$directory"/binary-only/*.trb 2>/dev/null)
fi

for file in $candidates; do
  [ -f "$file" ] || fail "$file is named but does not exist"
  case "$file" in
    */project.trb) continue ;;
  esac
  if [ -n "$filter" ]; then
    case "$file" in
      *"$filter"*) ;;
      *) continue ;;
    esac
  fi
  printf '%s\n' "$file" >>"$list"
done
sort -o "$list" "$list"

total=$(wc -l <"$list" | tr -d ' ')
[ "$total" -gt 0 ] || fail "no programs matched"
if [ "$jobs" -gt "$total" ]; then
  jobs=$total
fi

# The times of the last run, per back end, for every checkout of this machine: `xargs` starts the programs in the order
# of the list, so the longest go first, and one that has no time yet (a new program) before them
timings_directory=${TORB_CONFORMANCE_TIMINGS-}
if [ -z "$timings_directory" ]; then
  temporary=${TMPDIR:-${TEMP:-${TMP:-/tmp}}}
  # Git Bash hands a `TEMP` of Windows over as `C:\Users\...`; every checkout has to arrive at the same directory
  if command -v cygpath >/dev/null 2>&1; then
    temporary=$(cygpath -u "$temporary")
  fi
  timings_directory="$temporary/torb-conformance-timings"
fi
if [ "$machine" = "1" ]; then
  timings="$timings_directory/vm.txt"
else
  timings="$timings_directory/native.txt"
fi
known=/dev/null
if [ -s "$timings" ]; then
  known=$timings
fi
# Two programs start first whatever the times say, because what they cost depends on the checkout and not on the
# program: `sandbox-crossing.trb` builds the script host of the checkout (`build/script-host/`, the compiler's size of
# C, once per change of the compiler's sources - ten minutes on a loaded machine, and last in the order of the names),
# and `dns-over-tls.trb` compiles mbedTLS into `build/vendor/` once per checkout (minutes, on one processor).
pinned=" tests/conformance/sandbox-crossing.trb tests/conformance/dns-over-tls.trb "
awk -v pinned="$pinned" '
  FILENAME == ARGV[1] { seconds[$2] = $1; next }
  {
    if (index(pinned, " " $0 " ") > 0) {
      key = 2000000
    } else if ($0 in seconds) {
      key = seconds[$0]
    } else {
      key = 1000000
    }
    print key, $0
  }' "$known" "$list" | sort -k1,1nr -k2,2 | cut -d' ' -f2- >"$list.ordered"
mv -f "$list.ordered" "$list"
# Every other program that links mbedTLS (it uses std/tls or std/http) reuses those objects, but one that starts while
# they are still being compiled compiles a copy of its own - measured: three copies at once, five minutes each. So
# natively they wait until six programs per job have started, which is about as long as the compile takes.
if [ "$machine" = "0" ]; then
  # shellcheck disable=SC2046
  held=" $(grep -lE 'from "std/(tls|http)"' $(cat "$list") 2>/dev/null | tr '\n' ' ') "
  awk -v held="$held" -v pinned="$pinned" -v from=$((jobs * 6)) '
    index(held, " " $0 " ") > 0 && index(pinned, " " $0 " ") == 0 && started < from {
      waiting[++count] = $0
      next
    }
    {
      print
      started += 1
      if (started == from) {
        for (position = 1; position <= count; position += 1) print waiting[position]
        count = 0
      }
    }
    END { for (position = 1; position <= count; position += 1) print waiting[position] }' "$list" >"$list.ordered"
  mv -f "$list.ordered" "$list"
fi

scratch=$(mktemp -d)
trap 'rm -f "$list"; rm -rf "$scratch"' EXIT
mkdir -p "$scratch/work" "$scratch/results" "$scratch/times"

export CONFORMANCE_SCRATCH="$scratch"
export CONFORMANCE_TORB="$torb"
export CONFORMANCE_UPDATE="$update"
export CONFORMANCE_ROOT="$root"

runner="--run-one"
if [ "$machine" = "1" ]; then
  runner="--run-one-vm"
fi

if [ "$machine" = "1" ]; then
  say "running $total programs in the VM of $torb, $jobs at a time"
else
  say "building and running $total programs with $torb, $jobs at a time"
fi
started=$(date +%s)
# `-a` is not portable to BSD `xargs` (macOS), so the list is read from standard input instead. A child that crashed
# outright is caught below, from the result file it never wrote - not from `xargs`'s own exit code.
set +e
cat "$list" | xargs -P "$jobs" -I{} sh "$0" "$runner" {}
set -e
elapsed=$(($(date +%s) - started))

# This run's times replace the old ones of the same programs; the others keep theirs. Written beside the file and moved
# over it, so two runs that finish together each leave a whole file. A directory that cannot be written costs the order
# of the next run, never this one.
fresh="$scratch/timings"
for file in "$scratch"/times/*; do
  if [ -f "$file" ]; then
    cat "$file"
  fi
done >"$fresh"
if [ -s "$fresh" ] && mkdir -p "$timings_directory" 2>/dev/null; then
  if {
    cat "$fresh"
    if [ -f "$timings" ]; then
      awk 'NR == FNR { seen[$2] = 1; next } !($2 in seen)' "$fresh" "$timings"
    fi
  } | sort -k2,2 >"$timings.$$" 2>/dev/null; then
    mv -f "$timings.$$" "$timings" 2>/dev/null || true
  fi
  rm -f "$timings.$$"
fi
slowest=$(sort -k1,1nr "$fresh" | head -n 5 | awk '{ printf "%s%s %ss", (NR > 1 ? ", " : ""), $2, $1 }')

passed=0
failed=0
failures=""
while IFS= read -r file; do
  name=$(printf '%s' "$file" | sed 's#[/\\]#_#g; s/\.trb$//')
  result="$scratch/results/$name"
  if [ ! -f "$result" ]; then
    failed=$((failed + 1))
    failures="$failures
$file: no result was written (the runner crashed?)"
    continue
  fi
  status=$(head -n 1 "$result")
  if [ "$status" = "PASS" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
    body=$(tail -n +2 "$result")
    failures="$failures

=== $file ===$body"
  fi
done <"$list"

# Every program under `tests/conformance/` builds natively, so nothing is skipped. The count stays in the summary
# line below so a program that waits for a back-end gap has somewhere to report itself.
skipped=0

if [ -n "$failures" ]; then
  say "$failures"
  say ""
fi

if [ "$update" = "1" ]; then
  say "$passed updated (${elapsed}s, $total programs)"
  exit 0
fi

say "$passed passed, $failed failed (${elapsed}s, $total programs, $skipped skipped)"
if [ -n "$slowest" ]; then
  say "slowest: $slowest"
fi
[ "$failed" -eq 0 ]
