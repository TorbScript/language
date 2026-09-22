#!/bin/sh
# The conformance suite, run with the native compiler alone.
#
# Every program under `tests/conformance/` (and `binary-only/`) is built with `build/release/torb build`, run,
# and compared against its expectation files - `.expected` (standard output), `.stderr` (standard error, folded so a
# line of `std/` reads `path:_:_` instead of its real position), `.exit` (the exit code) and `.leaks` (why the leak
# gate does not apply). A file that is missing means "nothing to check" for `.expected`/`.exit`, and "must be empty"
# for `.stderr`. `tests/conformance/README.md` is the contract.
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

  # The program runs in its own work directory, so a program that writes files (`build/native-*`) writes them there and
  # never into the checkout, and two programs never share one
  mkdir -p "$work/run"
  set +e
  (cd "$work/run" && "$binary" >"$work/stdout" 2>"$work/stderr")
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
    (cd "$work/run" && TORB_REPORT_LEAKS=1 "$binary" >"$work/leak.stdout" 2>"$work/leak.stderr")
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

if [ "${1-}" = "--run-one" ]; then
  run_one "$2"
  exit 0
fi

# ----------------------------------------------------------------------------- the driver ---------------------------

jobs=2
filter=""
update=0

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
    -h | --help)
      say "usage: sh tools/conformance.sh [--jobs N] [--filter substring] [--update]"
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

torb=$(binary_of "build/release/torb")
[ -n "$torb" ] || fail "no native compiler at build/release/torb - run: sh tools/bootstrap.sh"
torb=$(cd "$(dirname "$torb")" && pwd)/$(basename "$torb")

directory="tests/conformance"
[ -d "$directory" ] || fail "no $directory"

list=$(mktemp)
trap 'rm -f "$list"' EXIT

for file in "$directory"/*.trb "$directory"/binary-only/*.trb; do
  [ -f "$file" ] || continue
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

scratch=$(mktemp -d)
trap 'rm -f "$list"; rm -rf "$scratch"' EXIT
mkdir -p "$scratch/work" "$scratch/results"

export CONFORMANCE_SCRATCH="$scratch"
export CONFORMANCE_TORB="$torb"
export CONFORMANCE_UPDATE="$update"

say "building and running $total programs with $torb, $jobs at a time"
started=$(date +%s)
# `-a` is not portable to BSD `xargs` (macOS), so the list is read from standard input instead. A child that crashed
# outright is caught below, from the result file it never wrote - not from `xargs`'s own exit code.
set +e
cat "$list" | xargs -P "$jobs" -I{} sh "$0" --run-one {}
set -e
elapsed=$(($(date +%s) - started))

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
[ "$failed" -eq 0 ]
