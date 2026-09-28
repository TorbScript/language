#!/bin/sh
# `torb test --filter` and `torb test --report json` over the suite of `tests/test-report/suite/`, in the VM and in a
# native binary. Every case of the table below runs in both back ends, and what it writes to standard output - the
# durations of the JSON report masked as `<ms>` - plus a last line `exit <code>` is compared with
# `tests/test-report/<case>.expected`: one file for both back ends, because the runtime writes the report of both
# (`runtime/test.c`). The one thing a back end adds is the frames of a failure, which a binary of the `dev` profile
# knows and the VM does not: where the native report differs for that, it is compared with `<case>.native.expected`
# instead. Standard error is not compared: it says when a build waits for a slot.
#
# The mistakes of the command line are one more case, `arguments`: each is run once - `torb test` refuses it before it
# builds or runs anything - and its standard error and exit code are compared with `tests/test-report/arguments.expected`.
#
# `$TEST_REPORT_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/test-report.sh
#   sh tools/test-report.sh --update     # rewrite the .expected files from what the cases answer now

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

update=0
case "${1-}" in
  '') ;;
  --update) update=1 ;;
  *)
    say "usage: sh tools/test-report.sh [--update]"
    exit 2
    ;;
esac

torb=$(binary_of "${TEST_REPORT_TORB:-build/release/torb}")
[ -n "$torb" ] || {
  say "test-report.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}

suite=tests/test-report/suite
expectations=tests/test-report
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

passed=0
failed=0

# The duration of a test differs from run to run; that it is a number is what stays
masked() {
  sed 's/"duration":[0-9][0-9]*\.[0-9][0-9]*/"duration":<ms>/'
}

# `compare <actual> <expected> <label> [<standard error>]`: one more passed or failed, and the difference where it failed
compare() {
  if cmp -s "$1" "$2"; then
    passed=$((passed + 1))
    return 0
  fi
  failed=$((failed + 1))
  say "$3: differs from $2"
  diff "$2" "$1" >&2 || true
  if [ -n "${4-}" ]; then
    cat "$4" >&2
  fi
}

# One case: `torb test <arguments> <suite>`, in the VM and natively
case_of() {
  name=$1
  shift
  for mode in vm native; do
    flag=""
    [ "$mode" = native ] && flag=--native
    status=0
    # shellcheck disable=SC2086
    "$torb" test $flag "$@" "$suite" >"$scratch/$name.$mode.raw" 2>"$scratch/$name.$mode.err" || status=$?
    {
      masked <"$scratch/$name.$mode.raw"
      printf 'exit %s\n' "$status"
    } >"$scratch/$name.$mode.out"
  done
  if [ "$update" -eq 1 ]; then
    cp "$scratch/$name.vm.out" "$expectations/$name.expected"
    if cmp -s "$scratch/$name.vm.out" "$scratch/$name.native.out"; then
      rm -f "$expectations/$name.native.expected"
    else
      cp "$scratch/$name.native.out" "$expectations/$name.native.expected"
    fi
    return 0
  fi
  compare "$scratch/$name.vm.out" "$expectations/$name.expected" "$name (VM)" "$scratch/$name.vm.err"
  native_expected="$expectations/$name.native.expected"
  [ -f "$native_expected" ] || native_expected="$expectations/$name.expected"
  compare "$scratch/$name.native.out" "$native_expected" "$name (native)" "$scratch/$name.native.err"
}

# The human report, which the options leave as it was
case_of plain
# Every event of the JSON report, a failure with its message, site and frames among them
case_of json --report json
# One test by its full name, below two groups
case_of filter-test --filter "Arithmetic > Division > divides"
# A group by its name, and a name outside ASCII: the group Other is passed over, and its body does not print
case_of filter-group --report json --filter "Arithmetic" --filter "Größe > zählt Äpfel 日本"
# The shard in the summary of the JSON report
case_of shard --report json --shard 2/2

# The mistakes of the command line: what each writes and its exit code, refused before anything is built. The path
# comes first, so that an option at the end has nothing behind it to take for its value. `--help` lists the flags
: >"$scratch/arguments.out"
while IFS= read -r line; do
  [ -n "$line" ] || continue
  status=0
  # shellcheck disable=SC2086
  "$torb" test "$suite" $line >"$scratch/arguments.err" 2>&1 || status=$?
  {
    printf '$ torb test %s %s\n' "$suite" "$line"
    cat "$scratch/arguments.err"
    printf 'exit %s\n' "$status"
  } >>"$scratch/arguments.out"
done <<'EOF'
--filter=Arithmetic
--report=json
--report xml
--report json --native --shards 2
--filter
--report
--frobnicate
EOF
status=0
"$torb" test --help >"$scratch/arguments.err" 2>&1 || status=$?
{
  printf '$ torb test --help\n'
  cat "$scratch/arguments.err"
  printf 'exit %s\n' "$status"
} >>"$scratch/arguments.out"
if [ "$update" -eq 1 ]; then
  cp "$scratch/arguments.out" "$expectations/arguments.expected"
  say "tests/test-report: written"
  exit 0
fi
compare "$scratch/arguments.out" "$expectations/arguments.expected" "arguments"

say "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
