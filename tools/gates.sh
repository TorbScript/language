#!/bin/sh
# Tier A and tier B of `compiler/CONTRIBUTING.md`, run with the native binary.
#
#   sh tools/gates.sh a     # every round
#   sh tools/gates.sh b     # a round that touches the IR, a back end or runtime/ - once, not again on master
#
# One line per gate, with its time. The first red gate stops the run and shows its output; nothing after it runs.
#
# Tier A: bootstrap if `build/release/torb` is missing or older than the compiler's sources, `check .`,
# `check --statistics .`, `test compiler/tests`, `test` of the std/example packages that build natively (the ones
# that do not are named in docs/RUST-EXIT.md section 2.4 and skipped here with the same reason), the two docs gates,
# and `canon --check` with the five rules.
#
# Tier B: `tools/conformance.sh` (the conformance suite), `tools/bootstrap.sh` (the fixpoint: seed -> torb -> torb,
# byte-identical C), and the C runtime's own tests.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "gates.sh: $*"
  exit 1
}

binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
}

# One gate: runs the command, prints "<name>: Ns" on success, and on failure prints the output and stops the whole
# script (a later gate never runs after a red one).
gate() {
  name=$1
  shift
  started=$(date +%s)
  if output=$("$@" 2>&1); then
    status=0
  else
    status=$?
  fi
  elapsed=$(($(date +%s) - started))
  if [ "$status" -ne 0 ]; then
    say "$name: FAILED after ${elapsed}s"
    say "$output"
    exit 1
  fi
  say "$name: ${elapsed}s"
}

tier=${1-}
case "$tier" in
  a | A) tier=a ;;
  b | B) tier=b ;;
  *)
    say "usage: sh tools/gates.sh a|b"
    exit 2
    ;;
esac

torb_path="build/release/torb"

# ----------------------------------------------------------------------------- tier A --------------------------------

if [ "$tier" = "a" ]; then
  torb=$(binary_of "$torb_path")
  stale=1
  if [ -n "$torb" ]; then
    stale=0
    if [ -n "$(find compiler -type f -newer "$torb" 2>/dev/null)" ]; then
      stale=1
    fi
  fi
  if [ "$stale" -eq 1 ]; then
    gate "bootstrap (seed -> torb -> torb)" sh tools/bootstrap.sh
  else
    say "bootstrap (seed -> torb -> torb): skipped, build/release/torb is current"
  fi
  torb=$(binary_of "$torb_path")
  [ -n "$torb" ] || fail "sh tools/bootstrap.sh did not write $torb_path"

  gate "check ." "$torb" check .
  gate "check --statistics ." "$torb" check --statistics .
  gate "test compiler/tests" "$torb" test compiler/tests

  # docs/RUST-EXIT.md section 2.4: four of the six candidate packages do not build natively yet, each blocked by one
  # back-end gap that is tracked on its own and does not block the exit. The other two are combined into one binary.
  broken="std/path/tests std/stream/tests examples/encoding-lab/tests examples/game-engine/tests"
  buildable=""
  for candidate in std/*/tests examples/*/tests; do
    [ -d "$candidate" ] || continue
    case " $broken " in
      *" $candidate "*) continue ;;
    esac
    buildable="$buildable $candidate"
  done
  # shellcheck disable=SC2086
  gate "test (std/example packages, native)" "$torb" test $buildable
  say "  skipped, do not build natively yet (docs/RUST-EXIT.md 2.4): std/path/tests (a Path is not boxed into its"
  say "  witness), std/stream/tests (onto outside a witness table), examples/encoding-lab/tests (describe as a"
  say "  function value), examples/game-engine/tests (a conversion through From)"

  gate "docs check" "$torb" docs check docs
  gate "docs index --check" "$torb" docs index --check docs

  gate "canon --check" "$torb" canon --check --rule calls --rule strings --rule imported-case-patterns \
    --rule unused-bindings --rule loops .

  say "tier A: green"
  exit 0
fi

# ----------------------------------------------------------------------------- tier B --------------------------------

torb=$(binary_of "$torb_path")
[ -n "$torb" ] || fail "no native compiler at $torb_path - run: sh tools/gates.sh a"

gate "conformance suite" sh tools/conformance.sh
gate "fixpoint (seed -> torb -> torb)" sh tools/bootstrap.sh
gate "runtime tests" sh runtime/build.sh

say "tier B: green"
