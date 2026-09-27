#!/bin/sh
# The sessions of `tests/repl/`, each piped into `build/release/torb repl` and compared with its expectation files
# (docs/design/REPL.md): `.expected` is standard output - what the entries printed and the values they showed -
# `.stderr` is standard error - the messages, the stops and the notes of the session - and `.exit` the exit code. A
# file that is missing means empty for the first two and `0` for the third.
#
# Every session runs in a directory of its own below a temporary one, so it sees the standard library and nothing of
# this repository; a directory `tests/repl/<name>/` next to `<name>.repl` is copied into it first, which is how a
# session gets a file to `:load` or a package to `use`. A file `tests/repl/<name>.flags` holds `torb repl`'s own
# arguments for that one session - `--sandbox`, say - space-separated, one line; a session without one gets none.
#
# The sessions are independent of each other, so several run at once: one per processor and per 768 MiB of memory
# (`tools/machine.sh`), never more than there are sessions (`--jobs N` or `$TORB_REPL_JOBS` says otherwise). What a
# failing session printed is reported in the order of the names, whichever finished first.
#
# `$REPL_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/repl.sh
#   sh tools/repl.sh --filter errors
#   sh tools/repl.sh --jobs 1
#   sh tools/repl.sh --update        # rewrite .expected/.stderr/.exit from what the sessions answer now

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
}

# `$1` against the file `$2`, where a missing file is the empty text
same_as() {
  if [ -f "$2" ]; then
    cmp -s "$1" "$2"
  else
    [ ! -s "$1" ]
  fi
}

# ----------------------------------------------------------------------------- one session -------------------------
#
# `sh tools/repl.sh --run-one <name>`, once per session, so that `xargs -P` runs several at once. `$REPL_SCRATCH`,
# `$REPL_BINARY` and `$REPL_UPDATE` come from the invocation below. The result file starts with `PASS` or `FAIL`; what
# follows a `FAIL` is the report of the session.
run_one() {
  name=$1
  scratch=$REPL_SCRATCH
  result="$scratch/results/$name"
  directory="$scratch/$name"
  mkdir -p "$directory"
  if [ -d "tests/repl/$name" ]; then
    cp -R "tests/repl/$name/." "$directory/"
  fi
  flags=""
  if [ -f "tests/repl/$name.flags" ]; then
    flags=$(cat "tests/repl/$name.flags")
  fi
  status=0
  # shellcheck disable=SC2086
  (cd "$directory" && "$REPL_BINARY" repl $flags <"$root/tests/repl/$name.repl" >"$scratch/$name.out" 2>"$scratch/$name.err") ||
    status=$?
  base="tests/repl/$name"
  if [ "$REPL_UPDATE" = "1" ]; then
    if [ -s "$scratch/$name.out" ]; then cp "$scratch/$name.out" "$base.expected"; else rm -f "$base.expected"; fi
    if [ -s "$scratch/$name.err" ]; then cp "$scratch/$name.err" "$base.stderr"; else rm -f "$base.stderr"; fi
    if [ "$status" -ne 0 ]; then printf '%s\n' "$status" >"$base.exit"; else rm -f "$base.exit"; fi
    echo "PASS" >"$result"
    return
  fi
  expected_status=0
  if [ -f "$base.exit" ]; then
    expected_status=$(cat "$base.exit")
  fi
  problems=""
  if ! same_as "$scratch/$name.out" "$base.expected"; then
    problems="$problems standard output"
  fi
  if ! same_as "$scratch/$name.err" "$base.stderr"; then
    problems="$problems standard error"
  fi
  if [ "$status" -ne "$expected_status" ]; then
    problems="$problems exit code ($status, expected $expected_status)"
  fi
  if [ -z "$problems" ]; then
    echo "PASS" >"$result"
    return
  fi
  {
    echo "FAIL"
    printf '%s\n' "$name: differs in$problems"
    if [ -f "$base.expected" ]; then
      diff "$base.expected" "$scratch/$name.out" || true
    else
      cat "$scratch/$name.out"
    fi
    if [ -f "$base.stderr" ]; then
      diff "$base.stderr" "$scratch/$name.err" || true
    else
      cat "$scratch/$name.err"
    fi
  } >"$result"
}

if [ "${1-}" = "--run-one" ]; then
  run_one "$2"
  exit 0
fi

# ----------------------------------------------------------------------------- the driver ---------------------------

filter=""
update=0
jobs=${TORB_REPL_JOBS-}
while [ $# -gt 0 ]; do
  case "$1" in
    --filter)
      filter=$2
      shift 2
      ;;
    --update)
      update=1
      shift
      ;;
    --jobs)
      jobs=$2
      shift 2
      ;;
    --jobs=*)
      jobs=${1#--jobs=}
      shift
      ;;
    *)
      say "usage: sh tools/repl.sh [--filter <part of a name>] [--jobs N] [--update]"
      exit 2
      ;;
  esac
done

if [ -z "$jobs" ]; then
  # A session is one `torb repl`, a few hundred MiB
  . "$root/tools/machine.sh"
  jobs=$(machine_jobs 768)
fi
case "$jobs" in
  '' | *[!0-9]* | 0) jobs=1 ;;
esac

torb=$(binary_of "${REPL_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  say "repl.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}
case "$torb" in
  /* | ?:*) ;;
  *) torb="$root/$torb" ;;
esac

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/results"

list="$scratch/sessions"
: >"$list"
for session in tests/repl/*.repl; do
  name=$(basename "$session" .repl)
  case "$name" in
    *"$filter"*) printf '%s\n' "$name" >>"$list" ;;
  esac
done
total=$(wc -l <"$list" | tr -d ' ')
if [ "$jobs" -gt "$total" ] && [ "$total" -gt 0 ]; then
  jobs=$total
fi

REPL_SCRATCH=$scratch
REPL_BINARY=$torb
REPL_UPDATE=$update
export REPL_SCRATCH REPL_BINARY REPL_UPDATE

# `-a` is not portable to BSD `xargs`, so the list is read from standard input; a session whose runner crashed is
# caught below from the result file it never wrote
set +e
if [ "$total" -gt 0 ]; then
  cat "$list" | xargs -P "$jobs" -I{} sh "$0" --run-one {}
fi
set -e

passed=0
failed=0
while IFS= read -r name; do
  result="$scratch/results/$name"
  if [ "$update" -eq 1 ]; then
    if [ -f "$result" ]; then
      say "$name: written"
    else
      say "$name: not written (the runner crashed?)"
      failed=$((failed + 1))
    fi
    continue
  fi
  if [ ! -f "$result" ]; then
    say "$name: no result was written (the runner crashed?)"
    failed=$((failed + 1))
    continue
  fi
  if [ "$(head -n 1 "$result")" = "PASS" ]; then
    passed=$((passed + 1))
    continue
  fi
  failed=$((failed + 1))
  tail -n +2 "$result" >&2
done <"$list"

if [ "$update" -eq 1 ]; then
  [ "$failed" -eq 0 ] || exit 1
  exit 0
fi
say "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
