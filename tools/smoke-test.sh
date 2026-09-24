#!/bin/sh
# Tests a release binary the way a download is used: laid out with its `std/` and `runtime/` (`tools/package.sh
# layout`), started from a directory that is not inside any checkout, and with no `$TORB_STD` or `$TORB_RUNTIME` - so
# the binary has to find its toolchain beside itself (`Process.executablePath`, docs/design/RELEASE.md section 1,
# fact 5). It checks one program, builds and runs it, and compares what it prints.
#
#   sh tools/smoke-test.sh build/release/torb
#   sh tools/smoke-test.sh build/torb-static
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "smoke-test.sh: $*"
  exit 1
}

[ $# -eq 1 ] || fail "usage: sh tools/smoke-test.sh <binary>"
case "$1" in
  /* | [A-Za-z]:*) binary=$1 ;;
  *) binary="$(pwd)/$1" ;;
esac
# .exe first: Git Bash answers true for `torb` when only `torb.exe` is there
if [ -f "$binary.exe" ]; then
  binary="$binary.exe"
fi
[ -f "$binary" ] || fail "there is no binary at $1"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
sh tools/package.sh layout "$binary" "$scratch/toolchain"
if [ -f "$scratch/toolchain/bin/torb.exe" ]; then
  torb="$scratch/toolchain/bin/torb.exe"
else
  torb="$scratch/toolchain/bin/torb"
fi

mkdir -p "$scratch/somewhere"
cat >"$scratch/somewhere/hello.trb" <<'EOF'
const names = ["seed", "fixpoint", "release"]
for name in names {
  print "hello, {name}"
}
EOF
expected='hello, seed
hello, fixpoint
hello, release'

cd "$scratch/somewhere"
unset TORB_STD TORB_RUNTIME 2>/dev/null || true

say "smoke test of $binary, laid out in $scratch/toolchain"
if ! output=$("$torb" check hello.trb 2>&1); then
  say "$output"
  fail "torb check failed outside of a checkout"
fi
say "check: $output"
if ! output=$("$torb" run hello.trb 2>&1); then
  say "$output"
  fail "torb run failed outside of a checkout"
fi
if [ "$output" != "$expected" ]; then
  say "expected:"
  say "$expected"
  say "got:"
  say "$output"
  fail "torb run printed something else"
fi
say "run: the program printed what it should"
say "smoke test: green"
