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
# `$REPL_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/repl.sh
#   sh tools/repl.sh --filter errors
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

filter=""
update=0
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
    *)
      say "usage: sh tools/repl.sh [--filter <part of a name>] [--update]"
      exit 2
      ;;
  esac
done

torb=$(binary_of "${REPL_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  say "repl.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# `$1` against the file `$2`, where a missing file is the empty text
same_as() {
  if [ -f "$2" ]; then
    cmp -s "$1" "$2"
  else
    [ ! -s "$1" ]
  fi
}

passed=0
failed=0
for session in tests/repl/*.repl; do
  name=$(basename "$session" .repl)
  case "$name" in
    *"$filter"*) ;;
    *) continue ;;
  esac
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
  (cd "$directory" && "$torb" repl $flags < "$root/$session" > "$scratch/$name.out" 2> "$scratch/$name.err") || status=$?
  base="tests/repl/$name"
  if [ "$update" -eq 1 ]; then
    if [ -s "$scratch/$name.out" ]; then cp "$scratch/$name.out" "$base.expected"; else rm -f "$base.expected"; fi
    if [ -s "$scratch/$name.err" ]; then cp "$scratch/$name.err" "$base.stderr"; else rm -f "$base.stderr"; fi
    if [ "$status" -ne 0 ]; then printf '%s\n' "$status" > "$base.exit"; else rm -f "$base.exit"; fi
    say "$name: written"
    continue
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
    passed=$((passed + 1))
    continue
  fi
  failed=$((failed + 1))
  say "$name: differs in$problems"
  if [ -f "$base.expected" ]; then
    diff "$base.expected" "$scratch/$name.out" >&2 || true
  else
    cat "$scratch/$name.out" >&2
  fi
  if [ -f "$base.stderr" ]; then
    diff "$base.stderr" "$scratch/$name.err" >&2 || true
  else
    cat "$scratch/$name.err" >&2
  fi
done

if [ "$update" -eq 1 ]; then
  exit 0
fi
say "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
