#!/bin/sh
# Tier A and tier B of `compiler/CONTRIBUTING.md`, run with the native binary.
#
#   sh tools/gates.sh a     # every round
#   sh tools/gates.sh b     # a round that touches the IR, a back end or runtime/ - once, not again on main
#   sh tools/gates.sh ab    # both tiers side by side on one gate slot: what `tools/land.sh gates` runs
#
# One line per gate, with its time.
#
# Tier A: bootstrap if `build/release/torb` is missing or older than a file it is built from (the compiler's sources,
# `std/`, the runtime), `check .`, `check --statistics .`, `check tests/conformance tests/language`,
# `check --every-target std/os`,
# `manifest --check` over every `project.trb`, `test --native compiler/tests`, `test` of the std/example packages in
# both back ends (one that waits for a back-end gap is named in docs/RUST-EXIT.md section 2.4 and skipped here), the
# programs of `tests/language/` against their `.expected` in both back ends, the sessions of `tests/repl/` piped into
# `torb repl` (`tools/repl.sh`), the sessions of `tests/lsp/` piped into `torb lsp` (`tools/lsp.sh`), the reports of
# `torb test --filter` and `--report json` in both back ends (`tools/test-report.sh`), the unknown flags every
# subcommand refuses (`tools/command-line.sh`), the package
# manager against a `file:` registry (`tools/packages.sh`), `lock --check`
# over every `project.lock.trb`, the four docs
# gates, `doc --check --no-run std` (the links of std's doc comments and its examples, type checked), and
# `format --check` over the repository. Every binary that is only built to be run once is built with `--profile dev`,
# which `torb test --native` and `torb run --native` do.
#
# Tier B: `tools/conformance.sh` (the conformance suite, and with `--vm` the same suite and `vm-only/` in the bytecode
# VM), the programs of `tests/language/` and `examples/config-dsl` in native binaries that embed the VM
# (`torb build --embed-vm`), `tools/bootstrap.sh` (the fixpoint: seed -> torb -> torb, byte-identical C), and the C
# runtime's own tests.
#
# **Side by side.** Once the binary exists every gate only reads it, and none of them depends on another, so the gates
# run in lanes at the same time: tier A in four (the compiler's own suite, one test binary run as up to four shards at
# once; the checks, the packages and the lock files; the REPL sessions and `format --check`; the docs), tier
# B in two (the conformance suite natively and then in the VM, each on every processor the run has; the runtime's tests,
# then the programs that embed the VM). A lane runs its gates one after another. A gate prints its line when it ends;
# a red one says so at once and its output follows once every lane has ended, so a run reports every red gate and not
# only the first. The bootstrap runs before the lanes, and so does tier B's fixpoint when it is not skipped: they are
# the two gates that replace the binary.
#
# The two conformance suites share a lane rather than running beside each other: both keep every processor busy, so
# one after another at full width takes as long as both at once, without a split of the processors that is right for
# one machine's load and wrong for the next (measured: at 13 and 3 at a time the VM's suite, a fifth of the work, ended
# last). The processors are shared out: `$TORB_GATE_JOBS` is how many processes one run keeps busy, by default one per
# processor and per 768 MiB of memory (`tools/machine.sh`). The conformance suites get all of it
# (`TORB_CONFORMANCE_JOBS` set by hand wins), the REPL sessions a quarter (`TORB_REPL_JOBS`), and `ab` keeps four for
# tier A's lanes and gives the conformance suites the rest.
#
# `TORB_GATES_PARALLEL=0` runs every gate one after another in the old order, and there the first red gate stops the
# run; a machine with fewer than four processors or less than 8 GiB of memory runs that way by itself
# (`TORB_GATES_PARALLEL=1` overrides it).
#
# A run holds one of the machine-wide gate slots (`tools/gate-slot.sh`, `$TORB_GATE_SLOTS`, default 2) from its first
# gate to its last, and says so while it waits for one: several checkouts running their gates at once once ran the
# machine out of processes. The bootstrap it runs inside takes no second slot, and neither do the tiers of `ab`.
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

whole_number() {
  case "$1" in
    '' | *[!0-9]*) return 1 ;;
  esac
  return 0
}

tier=${1-}
case "$tier" in
  a | A) tier=a ;;
  b | B) tier=b ;;
  ab | AB | ba | BA) tier=ab ;;
  *)
    say "usage: sh tools/gates.sh a|b|ab"
    exit 2
    ;;
esac

# ----------------------------------------------------------------------------- the machine ---------------------------

. "$root/tools/machine.sh"
processors=$(machine_processors)
memory=$(machine_memory)

parallel=${TORB_GATES_PARALLEL-}
case "$parallel" in
  0 | 1) ;;
  *)
    parallel=1
    if [ "$processors" -lt 4 ]; then
      parallel=0
    elif whole_number "$memory" && [ "$memory" -lt 8192 ]; then
      parallel=0
    fi
    ;;
esac

jobs=${TORB_GATE_JOBS:-$(machine_jobs 768)}
whole_number "$jobs" && [ "$jobs" -gt 0 ] ||
  fail "\$TORB_GATE_JOBS needs a whole number above zero, and it is \`$jobs\`"

# What the programs of the conformance suites and the REPL sessions run with, each a process of a few hundred MiB. One
# gate after another, a suite has the machine to itself and takes its own default. Side by side, the conformance
# suites get the run's share, less four for tier A's lanes with `ab`; the REPL sessions share their lane with
# `format --check`, beside three other lanes. `TORB_CONFORMANCE_JOBS` or `TORB_REPL_JOBS` set by hand wins.
conformance_jobs=""
repl_jobs=""
if [ "$parallel" -eq 1 ]; then
  conformance_jobs=$jobs
  if [ "$tier" = "ab" ]; then
    conformance_jobs=$((jobs - 4))
    [ "$conformance_jobs" -ge 2 ] || conformance_jobs=2
  fi
  repl_jobs=$((jobs / 4))
  [ "$repl_jobs" -ge 1 ] || repl_jobs=1
fi
if [ -n "${TORB_CONFORMANCE_JOBS-}" ]; then
  conformance_jobs=$TORB_CONFORMANCE_JOBS
fi
if [ -n "${TORB_REPL_JOBS-}" ]; then
  repl_jobs=$TORB_REPL_JOBS
fi

# The compiler's suite is one test binary, built once and run as that many shards at the same time (`torb test
# --shards`): a quarter of the run's share side by side, and never more than four, because the shard that holds
# types.test.trb - minutes of the suite on its own - sets the time from there on. One gate after another it is one
# process. `TORB_COMPILER_TEST_SHARDS` set by hand wins.
compiler_shards=1
if [ "$parallel" -eq 1 ]; then
  compiler_shards=$((jobs / 4))
  [ "$compiler_shards" -le 4 ] || compiler_shards=4
  [ "$compiler_shards" -ge 1 ] || compiler_shards=1
fi
if [ -n "${TORB_COMPILER_TEST_SHARDS-}" ]; then
  compiler_shards=$TORB_COMPILER_TEST_SHARDS
fi

# ----------------------------------------------------------------------------- gates and lanes -----------------------

reports=$(mktemp -d)
trap 'rm -rf "$reports"' EXIT

lane=main
count=0
lanes=""
lane_processes=""

# One gate: runs the command and prints "<name>: Ns" when it is green. A red gate prints "<name>: FAILED after Ns";
# run one after another it prints its output and stops the script there, side by side its output is printed at the end
# (`report_failures`) and the lane goes on with its next gate.
gate() {
  name=$1
  shift
  count=$((count + 1))
  report="$reports/$lane-$(printf '%03d' "$count")"
  started=$(date +%s)
  # In a subshell: a function run as a gate keeps its variables to itself
  if ("$@") >"$report.out" 2>&1; then
    status=0
  else
    status=$?
  fi
  elapsed=$(($(date +%s) - started))
  if [ "$status" -eq 0 ]; then
    rm -f "$report.out"
    say "$name: ${elapsed}s"
    return 0
  fi
  if [ "$parallel" -eq 0 ]; then
    say "$name: FAILED after ${elapsed}s"
    cat "$report.out" >&2
    exit 1
  fi
  printf '%s\n' "$name: FAILED after ${elapsed}s" >"$report.failed"
  say "$name: FAILED after ${elapsed}s - its output follows when every lane has ended"
  return 0
}

# Runs the gates of one lane: side by side in a process of its own, otherwise here and now. `$1` names the lane (the
# names sort in the order of the gates' output at the end), `$2` is the function that runs its gates.
start_lane() {
  lanes="$lanes $1"
  if [ "$parallel" -eq 0 ]; then
    lane=$1
    count=0
    "$2"
    : >"$reports/$1.done"
    return 0
  fi
  (
    lane=$1
    count=0
    "$2"
    : >"$reports/$1.done"
  ) &
  lane_processes="$lane_processes $!"
}

# A terminal's interrupt reaches every process of the run, but a lane is a background process of a script, and `sh`
# lets those ignore it - so the run ends every process of its group on the way out: its lanes, what they started, and
# the gate slot's shell above it, which frees the slot. Twice, a second apart: a signal reaches the processes of a group
# one after another, and a lane that has not got it yet may start one more command.
stop_everything() {
  trap '' INT TERM
  kill -TERM 0 2>/dev/null || true
  sleep 1
  kill -TERM 0 2>/dev/null || true
}

wait_for_lanes() {
  for process in $lane_processes; do
    wait "$process" || true
  done
  lane_processes=""
}

# The output of every red gate, in the order of the lanes, and whether any gate was red; a lane that ended before its
# last gate (it never does unless the script itself is broken) is one as well
report_failures() {
  red=0
  for name in $lanes; do
    if [ ! -f "$reports/$name.done" ]; then
      say ""
      say "lane $name ended before its last gate"
      red=1
    fi
  done
  for failed in "$reports"/*.failed; do
    [ -f "$failed" ] || continue
    red=1
    say ""
    say "=== $(cat "$failed") ==="
    cat "${failed%.failed}.out" >&2
  done
  [ "$red" -eq 0 ]
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

# The same programs in a native binary that embeds the VM (`torb build --embed-vm`, docs/design/VM.md section 11): each
# built into `build/embed-vm/`, run, and compared with the same `.expected` - and `examples/config-dsl`, whose receiver
# script runs in the sandbox of the embedded VM, compared with what `torb run` prints for it.
embedded_programs() {
  torb=$1
  failures=0
  mkdir -p build/embed-vm
  for program in tests/language/*.trb; do
    case "$program" in
      */project.trb) continue ;;
    esac
    name=$(basename "$program" .trb)
    expected="${program%.trb}.expected"
    if ! built=$("$torb" build --embed-vm "$program" --output "build/embed-vm/$name" 2>&1); then
      printf '%s\n' "$program did not build with --embed-vm:"
      printf '%s\n' "$built"
      failures=$((failures + 1))
      continue
    fi
    binary=$(binary_of "build/embed-vm/$name")
    if actual=$("$binary" 2>&1); then
      if ! printf '%s\n' "$actual" | cmp -s - "$expected"; then
        printf '%s\n' "$program, embedded: the output is not $expected:"
        printf '%s\n' "$actual" | diff "$expected" - || true
        failures=$((failures + 1))
      fi
    else
      printf '%s\n' "$program, embedded, failed:"
      printf '%s\n' "$actual"
      failures=$((failures + 1))
    fi
  done
  # A program that loads a receiver script and applies it in its sandbox, from its own directory: as `torb run` runs it
  if built=$("$torb" build --embed-vm examples/config-dsl --output build/embed-vm/config-dsl 2>&1); then
    binary=$(pwd)/$(binary_of build/embed-vm/config-dsl)
    case "$torb" in
      /* | ?:*) runner=$torb ;;
      *) runner=$(pwd)/$torb ;;
    esac
    wanted=$(cd examples/config-dsl && "$runner" run . 2>&1)
    actual=$(cd examples/config-dsl && "$binary" 2>&1)
    if [ "$actual" != "$wanted" ]; then
      printf '%s\n' "examples/config-dsl, embedded, is not what torb run prints:"
      printf '%s\n' "$actual"
      failures=$((failures + 1))
    fi
  else
    printf '%s\n' "examples/config-dsl did not build with --embed-vm:"
    printf '%s\n' "$built"
    failures=$((failures + 1))
  fi
  [ "$failures" -eq 0 ]
}

# `torb lock --check` for the project of every `project.lock.trb` git knows: the file is what the toolchain writes.
lock_files() {
  torb=$1
  failures=0
  for lock in $(git ls-files '*project.lock.trb'); do
    if ! "$torb" lock --check --project "$(dirname "$lock")"; then
      failures=$((failures + 1))
    fi
  done
  [ "$failures" -eq 0 ]
}

torb_path="build/release/torb"

# ----------------------------------------------------------------------------- tier A --------------------------------

bootstrap_if_stale() {
  torb=$(binary_of "$torb_path")
  if [ -z "$torb" ] || is_stale "$torb"; then
    gate "bootstrap (seed -> torb -> torb)" sh tools/bootstrap.sh
  else
    say "bootstrap (seed -> torb -> torb): skipped, build/release/torb is current"
  fi
  torb=$(binary_of "$torb_path")
  [ -n "$torb" ] || fail "sh tools/bootstrap.sh did not write $torb_path"
}

lane_checks() {
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

  # docs/design/RELEASE.md section 7.13: publish, add, build, update, remove and install against a `file:` registry, the
  # whole transcript against tests/packages/transcript.expected
  gate "tests/packages against transcript.expected (the package manager)" sh tools/packages.sh

  # docs/design/PROJECT.md section 8: every project.lock.trb of the repository is byte for byte what `torb lock` writes
  gate "lock --check (every project.lock.trb)" lock_files "$torb"
}

# Natively: the compiler's own suite checks whole programs in every test, which the VM runs too slowly for a gate
lane_compiler_tests() {
  gate "test compiler/tests (native)" "$torb" test --native compiler/tests --shards "$compiler_shards"
}

lane_sessions() {
  # docs/design/REPL.md: whole sessions of `torb repl`, each against its exact standard output, standard error and exit code
  gate "tests/repl against .expected (torb repl)" env TORB_REPL_JOBS="$repl_jobs" sh tools/repl.sh
  # docs/tooling/torb-lsp.md: sessions of the language server over standard input and output, framed as an editor does
  gate "tests/lsp against .expected (torb lsp)" sh tools/lsp.sh
  # docs/tooling/torb-test.md: `--filter` and `--report json` over one suite, in the VM and natively, against .expected
  gate "tests/test-report against .expected (torb test --filter, --report json)" sh tools/test-report.sh
  # An unknown flag of every subcommand is refused before anything runs, and never looked for as a file
  gate "tests/command-line against .expected (unknown flags)" sh tools/command-line.sh
  gate "format --check" "$torb" format --check .
}

lane_docs() {
  gate "docs check" "$torb" docs check docs
  gate "docs index --check" "$torb" docs index --check docs
  gate "docs skill --check" "$torb" docs skill docs .claude/skills/torbscript --check
  # docs/design/PANICS.md recommendation 10: every public function of std/ that can panic says when (`# Panics`)
  gate "docs source --panics" "$torb" docs source --panics std
  # docs/tooling/torb-doc.md: every link of a doc comment of std's public API resolves, and every example type checks.
  # Running the examples too (`torb doc --check std`) takes over a minute, which is why the gate stops at the type check
  gate "doc --check --no-run std" "$torb" doc --check --no-run std
}

# The four lanes of tier A; run one after another, the checks come first, as they always did
start_tier_a() {
  start_lane a1-checks lane_checks
  start_lane a2-compiler-tests lane_compiler_tests
  start_lane a3-sessions lane_sessions
  start_lane a4-docs lane_docs
}

# ----------------------------------------------------------------------------- tier B --------------------------------

# The bootstrap of tier A proves the fixpoint of the sources it built, and marks it with `build/release/fixpoint`. The
# same comparison of the same sources again is twenty minutes of nothing; a source changed since, or a binary built by
# hand over the bootstrap's, runs it.
fixpoint_is_held() {
  [ -f build/release/fixpoint ] && ! is_stale build/release/fixpoint && ! [ "$torb" -nt build/release/fixpoint ]
}

fixpoint() {
  if fixpoint_is_held; then
    say "fixpoint (seed -> torb -> torb): held at the bootstrap of these sources, not repeated"
  else
    gate "fixpoint (seed -> torb -> torb)" sh tools/bootstrap.sh
  fi
}

lane_conformance() {
  gate "conformance suite" env TORB_CONFORMANCE_JOBS="$conformance_jobs" sh tools/conformance.sh
  gate "conformance suite in the VM" env TORB_CONFORMANCE_JOBS="$conformance_jobs" sh tools/conformance.sh --vm
}

# The embedded programs after the runtime's tests: `examples/config-dsl` links the script host, which the native suite
# builds first (`sandbox-crossing.trb`), and two builds of it at the same time would both compile the compiler's size
# of C
lane_runtime_and_embedded() {
  gate "runtime tests" sh runtime/build.sh
  gate "tests/language and examples/config-dsl in native binaries (torb build --embed-vm)" embedded_programs "$torb"
}

start_tier_b() {
  start_lane b1-conformance lane_conformance
  start_lane b2-runtime-and-embedded lane_runtime_and_embedded
}

# ----------------------------------------------------------------------------- the run -------------------------------

run_started=$(date +%s)

if [ "$parallel" -eq 1 ]; then
  trap 'stop_everything; exit 130' INT
  trap 'stop_everything; exit 143' TERM
fi

if [ "$tier" = "b" ]; then
  torb=$(binary_of "$torb_path")
  [ -n "$torb" ] || fail "no native compiler at $torb_path - run: sh tools/gates.sh a"
else
  lane=a0-bootstrap
  bootstrap_if_stale
  if [ -f "$reports/a0-bootstrap-001.failed" ]; then
    report_failures || true
    exit 1
  fi
fi

if [ "$parallel" -eq 0 ]; then
  # The old order: tier A's checks, the compiler's suite, the sessions, the docs; tier B's conformance suites, the
  # embedded programs, the fixpoint, the runtime's tests - and the first red gate stops the run
  if [ "$tier" != "b" ]; then
    start_tier_a
    say "tier A: green"
  fi
  if [ "$tier" != "a" ]; then
    lane=b0
    count=0
    lane_conformance
    gate "tests/language and examples/config-dsl in native binaries (torb build --embed-vm)" embedded_programs "$torb"
    fixpoint
    gate "runtime tests" sh runtime/build.sh
    say "tier B: green"
  fi
  exit 0
fi

# Side by side. The fixpoint, when it runs, replaces build/release/torb, which every lane runs: it goes first.
if [ "$tier" != "a" ]; then
  lane=b0-fixpoint
  count=0
  fixpoint
fi
sequential="TORB_GATES_PARALLEL=0 runs them one after another"
case "$tier" in
  a) say "tier A in four lanes side by side ($sequential)" ;;
  b) say "tier B in two lanes side by side, $conformance_jobs conformance programs at a time ($sequential)" ;;
  ab) say "tiers A and B in six lanes side by side, $conformance_jobs conformance programs at a time ($sequential)" ;;
esac
if [ "$tier" != "b" ]; then
  start_tier_a
fi
if [ "$tier" != "a" ]; then
  start_tier_b
fi
wait_for_lanes

elapsed=$(($(date +%s) - run_started))
if report_failures; then
  case "$tier" in
    a) say "tier A: green (${elapsed}s)" ;;
    b) say "tier B: green (${elapsed}s)" ;;
    ab) say "tiers A and B: green (${elapsed}s)" ;;
  esac
  exit 0
fi
say ""
red=$(ls "$reports" | grep -c '\.failed$' || true)
say "gates.sh: $red red gate(s) after ${elapsed}s - their output is above"
exit 1
