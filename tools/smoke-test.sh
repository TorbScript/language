#!/bin/sh
# Tests a release binary the way a download is used: laid out with its `std/` and `runtime/` (`tools/package.sh
# layout`), started from a directory that is not inside any checkout, and with no `$TORB_STD` or `$TORB_RUNTIME` - so
# the binary has to find its toolchain beside itself (`Process.executablePath`, docs/design/RELEASE.md section 1,
# fact 5). It checks one program, builds and runs it, and compares what it prints; then it runs a second one that
# compares values of a type with `Compare` and prints floats, and writes the C of `tools/registry`, which has the
# whole compiler inside it.
#
#   sh tools/smoke-test.sh build/release/torb
#   sh tools/smoke-test.sh build/torb-static
#   TORB_SMOKE_RUNNER=wine sh tools/smoke-test.sh build/cross/windows-x64/torb.exe
#
# `$TORB_SMOKE_RUNNER` is the command a binary of another machine runs under - `wine` for a windows-x64 `torb.exe`,
# `qemu-aarch64` for a linux-arm64 `torb` - which is how the binaries tools/cross.sh builds are started on the runner
# that built them. Everything else is the same.
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

# An order comparison of a type with `Compare`, then floats. gcc 13 once compiled the compiler's lowering of exactly
# that comparison with an MMX instruction, which leaves the x87 registers unusable, and the static musl `torb` crashed
# on the first float it formatted afterwards (runtime/include/torb.h, docs/design/RELEASE.md section 14).
cat >"$scratch/somewhere/order.trb" <<'EOF'
type Version with Compare {
  major: Int
  minor: Int

  fn compare(other: Self): Ordering {
    if major != other.major {
      return major.compare other.major
    }
    minor.compare other.minor
  }
}

const older = Version 1, 2
const newer = Version 1, 3
print "{older < newer} {newer >= older} {0.01 * 3.0} {7.5 % 2.0}"
EOF
ordered='true true 0.03 1.5'

runner=${TORB_SMOKE_RUNNER-}
run_torb() {
  # shellcheck disable=SC2086
  $runner "$torb" "$@"
}

cd "$scratch/somewhere"
unset TORB_STD TORB_RUNTIME 2>/dev/null || true

say "smoke test of $binary${runner:+ under $runner}, laid out in $scratch/toolchain"
if ! output=$(run_torb check hello.trb 2>&1); then
  say "$output"
  fail "torb check failed outside of a checkout"
fi
say "check: $output"
if ! output=$(run_torb run hello.trb 2>&1); then
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
if ! output=$(run_torb run order.trb 2>&1); then
  say "$output"
  fail "torb run of an order comparison and floats failed"
fi
if [ "$output" != "$ordered" ]; then
  say "expected: $ordered"
  say "got:      $output"
  fail "torb run of an order comparison and floats printed something else"
fi
say "run: an order comparison and floats printed what they should"

# The build stage of the registry's image (tools/deploy/Dockerfile.registry) up to its C: a program with the whole
# compiler inside it, the largest one a release binary meets, from a tree with `std/` beside it as the source archive is
cd "$root"
if ! output=$(run_torb build tools/registry/src/main.trb --emit-c --output "$scratch/registry/registry" 2>&1); then
  say "$output"
  fail "torb build --emit-c of tools/registry failed"
fi
[ -f "$scratch/registry/program.c" ] || fail "torb build --emit-c of tools/registry wrote no program.c"
say "build: tools/registry emitted its C"
say "smoke test: green"
