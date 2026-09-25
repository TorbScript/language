#!/bin/sh
# Tier A and tier B of `compiler/CONTRIBUTING.md`, run with the native binary.
#
#   sh tools/gates.sh a     # every round
#   sh tools/gates.sh b     # a round that touches the IR, a back end or runtime/ - once, not again on main
#
# One line per gate, with its time. The first red gate stops the run and shows its output; nothing after it runs.
#
# Tier A: bootstrap if `build/release/torb` is missing or older than a file it is built from (the compiler's sources,
# `std/`, the runtime), `check .`, `check --statistics .`, `check tests/conformance tests/language`,
# `check --every-target std/os`,
# `manifest --check` over every `project.trb`, `test --native compiler/tests`, `test` of the std/example packages in
# both back ends (one that waits for a back-end gap is named in docs/RUST-EXIT.md section 2.4 and skipped here), the
# programs of `tests/language/` against their `.expected` in both back ends, the sessions of `tests/repl/` piped into
# `torb repl` (`tools/repl.sh`), the three docs gates, and `canon --check` with the five rules. Every binary that is
# only built to be run once is built with `--profile dev`, which `torb test --native` and `torb run --native` do.
#
# Tier B: `tools/conformance.sh` (the conformance suite, and with `--vm` the same suite and `vm-only/` in the bytecode
# VM), `tools/bootstrap.sh` (the fixpoint: seed -> torb -> torb, byte-identical C), and the C runtime's own tests.
#
# A run holds one of the machine-wide gate slots (`tools/gate-slot.sh`, `$TORB_GATE_SLOTS`, default 2) from its first
# gate to its last, and says so while it waits for one: several checkouts running their gates at once once ran the
# machine out of processes. The bootstrap it runs inside takes no second slot.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

if [ "${TORB_GATE_SLOT_HELD-}" != "1" ]; then
  exec sh "$(dirname -- "$0")/gate-slot.sh" sh "$0" "$@"
fi

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

# Whether a file `torb` is built from is newer than the binary: the compiler's sources, the standard library's, the
# runtime it links, and the manifests that make them one workspace. Everything that is *written* during a build or a
# test run lives in a `build` directory and is never looked at.
is_stale() {
  newer=$(find compiler/src compiler/project.trb std runtime project.trb -name build -prune -o -type f \
    \( -name '*.trb' -o -name '*.c' -o -name '*.h' \) -newer "$1" -print 2>/dev/null | head -n 1)
  [ -n "$newer" ]
}

# The programs of `tests/language/`, each built and run with `torb run` and compared with its `.expected`. The ones in
# `$language_broken` do not build natively yet and are skipped, with the reason printed below the gate.
language_programs() {
  torb=$1
  # `--native`, or nothing for the VM
  mode=${2-}
  failures=0
  for program in tests/language/*.trb; do
    case "$program" in
      */project.trb) continue ;;
    esac
    case " $language_broken " in
      *" $program "*) continue ;;
    esac
    expected="${program%.trb}.expected"
    if actual=$("$torb" run $mode "$program" 2>&1); then
      if ! printf '%s\n' "$actual" | cmp -s - "$expected"; then
        printf '%s\n' "$program: the output is not $expected:"
        printf '%s\n' "$actual" | diff "$expected" - || true
        failures=$((failures + 1))
      fi
    else
      printf '%s\n' "$program failed:"
      printf '%s\n' "$actual"
      failures=$((failures + 1))
    fi
  done
  [ "$failures" -eq 0 ]
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
  if [ -z "$torb" ] || is_stale "$torb"; then
    gate "bootstrap (seed -> torb -> torb)" sh tools/bootstrap.sh
  else
    say "bootstrap (seed -> torb -> torb): skipped, build/release/torb is current"
  fi
  torb=$(binary_of "$torb_path")
  [ -n "$torb" ] || fail "sh tools/bootstrap.sh did not write $torb_path"

  gate "check ." "$torb" check .
  gate "check --statistics ." "$torb" check --statistics .
  gate "check tests/conformance tests/language" "$torb" check tests/conformance tests/language
  # docs/design/OS.md section 2: every program and test of std/os lowered once per target, without C, so a native of one
  # system reached from the arm of another is an error on this machine too
  gate "check --every-target std/os" "$torb" check --every-target std/os
  # docs/design/SCRIPTS.md section 7: every project.trb of the repository, evaluated in the sandboxed VM, reads the same
  # as the file itself does to the static reader the other commands use
  gate "manifest --check (every project.trb, evaluated)" "$torb" manifest --check . tests/conformance tests/language \
    tests/project.trb
  # Natively: the compiler's own suite checks whole programs in every test, which the VM runs too slowly for a gate
  gate "test compiler/tests (native)" "$torb" test --native compiler/tests

  # docs/RUST-EXIT.md section 2.4: every candidate package builds natively, and they are combined into one binary. A
  # package named here is skipped, which is for one that waits for a back-end gap RUST-EXIT 2.4 names.
  broken=""
  buildable=""
  for candidate in std/*/tests examples/*/tests; do
    [ -d "$candidate" ] || continue
    case " $broken " in
      *" $candidate "*) continue ;;
    esac
    buildable="$buildable $candidate"
  done
  # shellcheck disable=SC2086
  gate "test (std/example packages, native)" "$torb" test --native $buildable
  # shellcheck disable=SC2086
  gate "test (std/example packages, VM)" "$torb" test $buildable

  # A program that waits for a back-end gap is named here and skipped; none does.
  language_broken=""
  gate "tests/language against .expected (native)" language_programs "$torb" --native
  gate "tests/language against .expected (VM)" language_programs "$torb"

  # docs/design/REPL.md: whole sessions of `torb repl`, each against its exact standard output, standard error and exit code
  gate "tests/repl against .expected (torb repl)" sh tools/repl.sh

  gate "docs check" "$torb" docs check docs
  gate "docs index --check" "$torb" docs index --check docs
  gate "docs skill --check" "$torb" docs skill docs .claude/skills/torbscript --check

  gate "canon --check" "$torb" canon --check --rule calls --rule strings --rule imported-case-patterns \
    --rule unused-bindings --rule loops .

  say "tier A: green"
  exit 0
fi

# ----------------------------------------------------------------------------- tier B --------------------------------

torb=$(binary_of "$torb_path")
[ -n "$torb" ] || fail "no native compiler at $torb_path - run: sh tools/gates.sh a"

gate "conformance suite" sh tools/conformance.sh
gate "conformance suite in the VM" sh tools/conformance.sh --vm
gate "fixpoint (seed -> torb -> torb)" sh tools/bootstrap.sh
gate "runtime tests" sh runtime/build.sh

say "tier B: green"
