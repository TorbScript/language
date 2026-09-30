#!/bin/sh
#
# Six programs of the Computer Language Benchmarks Game (game/), each in TorbScript - built with `torb build` and run
# in the VM with `torb run` - and in the Benchmarks Game's own C, Python and JavaScript. Every program is built and
# run on this machine, every output is checked against the C's, and the times go into one JSON report, which the
# website renders as its benchmarks page (docs/site/benchmarks.md).
#
#   sh game.sh                  every program, at the inputs the website states
#   sh game.sh n-body fasta     some of them
#   sh game.sh --quick          tiny inputs: that everything builds, runs and agrees - not how fast it is
#   RUNS=9 VM_RUNS=5 sh game.sh more runs; the median is what the report says
#
# Each program runs on two inputs: a large one for C, the native binary and Node.js, and a small one for the VM and
# Python, which would take many minutes on the large one, with C and the native binary again as their yardstick. Every language runs once to warm up - its output checked, its peak memory measured with GNU
# `/usr/bin/time` where there is one - and then RUNS times (VM_RUNS for the VM), the languages taking turns so that a
# slow moment of the machine falls on all of them.
#
# $TORB is the toolchain (`../build/release/torb` by default, else `torb` on the PATH), $TORB_CC the C compiler (gcc
# by default), $PYTHON and $NODE the interpreters (python3 or python, and node): a language whose tool is missing is
# reported as missing, and the rest is measured. $OUT is where everything is written (out/game), $BENCHMARKS_JSON the
# report ($OUT/benchmarks.json). $BENCHMARKS_ORIGIN says where the numbers come from - `ci` in the workflow that
# publishes them (.forgejo/workflows/benchmarks.yml), `local` otherwise - and $BENCHMARKS_COMMIT and
# $BENCHMARKS_RELEASE name what was measured (by default the checkout's commit and nothing).
#
# It is POSIX sh with GNU date, and it runs under Git Bash on Windows as well, which is how it is tried out; the numbers
# the website shows come from the forge's Linux runner. Nothing here is a gate.

set -u

here=$(dirname "$0")
cd "$here" || exit 1

runs=${RUNS:-5}
vm_runs=${VM_RUNS:-3}
out=${OUT:-out/game}
report=${BENCHMARKS_JSON:-$out/benchmarks.json}
origin=${BENCHMARKS_ORIGIN:-local}
timeout_seconds=${BENCHMARK_TIMEOUT:-900}

all_programs="binary-trees fannkuch-redux n-body spectral-norm mandelbrot fasta"

quick=no
wanted=""
for argument in "$@"; do
  case $argument in
    --quick) quick=yes ;;
    --help | -h)
      sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    -*)
      echo "game.sh: unknown option $argument" >&2
      exit 2
      ;;
    *)
      case " $all_programs " in
        *" $argument "*) wanted="$wanted $argument" ;;
        *)
          echo "game.sh: there is no program $argument; the programs are: $all_programs" >&2
          exit 2
          ;;
      esac
      ;;
  esac
done
programs=${wanted:-$all_programs}

# The inputs. On the large one the slowest of C, the native binary and Node.js takes some seconds on the forge's
# runner; on the small one the VM takes some seconds - and C some milliseconds, which is why the ratios of the small
# input are the rougher ones. An input that changes makes the nights before it incomparable, so they change rarely.
large_input() {
  if [ "$quick" = yes ]; then
    case $1 in
      binary-trees) echo 10 ;; fannkuch-redux) echo 8 ;; n-body) echo 20000 ;;
      spectral-norm) echo 200 ;; mandelbrot) echo 400 ;; fasta) echo 20000 ;;
    esac
    return
  fi
  case $1 in
    binary-trees) echo 17 ;; fannkuch-redux) echo 10 ;; n-body) echo 1000000 ;;
    spectral-norm) echo 2000 ;; mandelbrot) echo 4000 ;; fasta) echo 250000 ;;
  esac
}

small_input() {
  if [ "$quick" = yes ]; then
    case $1 in
      binary-trees) echo 8 ;; fannkuch-redux) echo 7 ;; n-body) echo 2000 ;;
      spectral-norm) echo 100 ;; mandelbrot) echo 200 ;; fasta) echo 2000 ;;
    esac
    return
  fi
  case $1 in
    binary-trees) echo 15 ;; fannkuch-redux) echo 9 ;; n-body) echo 60000 ;;
    spectral-norm) echo 400 ;; mandelbrot) echo 1000 ;; fasta) echo 50000 ;;
  esac
}

say() {
  printf '%s\n' "$*" >&2
}

# ----------------------------------------------------------------------------- the tools -----------------------------

case $(uname -s) in
  MINGW* | MSYS* | CYGWIN*) windows=yes ;;
  *) windows=no ;;
esac

torb=${TORB-}
if [ -z "$torb" ]; then
  for candidate in ../build/release/torb ../build/release/torb.exe; do
    if [ -x "$candidate" ]; then
      torb=$candidate
      break
    fi
  done
fi
if [ -z "$torb" ] && command -v torb >/dev/null 2>&1; then
  torb=torb
fi
if [ -z "$torb" ] || ! "$torb" --version >/dev/null 2>&1; then
  say "game.sh: no torb. Build it (sh tools/bootstrap.sh from the repository root) or name it in \$TORB."
  exit 3
fi

cc=${TORB_CC-}
if [ -z "$cc" ]; then
  for candidate in gcc cc clang; do
    if command -v "$candidate" >/dev/null 2>&1; then
      cc=$candidate
      break
    fi
  done
fi
if [ -z "$cc" ]; then
  say "game.sh: no C compiler. Put gcc on the PATH or name one in \$TORB_CC."
  exit 3
fi

# An interpreter counts only when it runs: Windows answers `python3` with a stub that asks for the Microsoft Store
working() {
  for candidate in "$@"; do
    if [ -n "$candidate" ] && "$candidate" --version >/dev/null 2>&1; then
      echo "$candidate"
      return
    fi
  done
}
python=$(working "${PYTHON-}" python3 python)
node=$(working "${NODE-}" node)

case $(date +%s%N) in
  *N | "") say "game.sh: needs a date that knows %N (GNU date)"; exit 3 ;;
esac

if command -v sha256sum >/dev/null 2>&1; then
  hasher="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
  hasher="shasum -a 256"
else
  hasher="cksum"
fi

gnu_time=""
if [ -x /usr/bin/time ] && /usr/bin/time -f %M -o /dev/null true >/dev/null 2>&1; then
  gnu_time=/usr/bin/time
fi
timeout_command=""
if command -v timeout >/dev/null 2>&1 && timeout --version >/dev/null 2>&1; then
  timeout_command=timeout
fi

# `-O2` and nothing else, which is the optimization `torb build` asks of the same compiler in its release profile.
# Windows has no setlinebuf, which fasta.c calls; setvbuf is the same request, for trying the harness out there.
c_flags="-O2"
exe=""
if [ "$windows" = yes ]; then
  exe=".exe"
fi

mkdir -p "$out" || exit 1
work="$out/work"
mkdir -p "$work" || exit 1

# ----------------------------------------------------------------------------- one program ---------------------------

# The source of a language's version of a program, relative to this directory. `startup` is the program that prints
# one line and ends: what starting each language costs.
source_of() {
  case $1:$2 in
    torbscript:startup | torbscript-vm:startup) echo "nothing.trb" ;;
    c:startup) echo "c/nothing.c" ;;
    python:startup) echo "$out/startup/nothing.py" ;;
    node:startup) echo "$out/startup/nothing.js" ;;
    torbscript:* | torbscript-vm:*) echo "game/$2.trb" ;;
    c:*) echo "game/c/$2.c" ;;
    python:*) echo "game/python/$2.py" ;;
    node:*) echo "game/node/$2.js" ;;
  esac
}

# Builds the C and the native binary of a program into $out/<program>/. Sets c_built and torb_built to yes or no, and
# says why in the build logs next to them.
build_program() {
  program_name=$1
  directory="$out/$program_name"
  mkdir -p "$directory"
  set -- "$cc" $c_flags -o "$directory/c$exe" "$(source_of c "$program_name")" -lm
  if [ "$windows" = yes ]; then
    set -- "$@" "-Dsetlinebuf(stream)=setvbuf((stream),NULL,_IOLBF,BUFSIZ)"
  fi
  if "$@" >"$directory/c.log" 2>&1; then c_built=yes; else c_built=no; fi
  if "$torb" build "$(source_of torbscript "$program_name")" --output "$directory/torbscript" \
      >"$directory/torbscript.log" 2>&1; then
    torb_built=yes
  else
    torb_built=no
  fi
  if [ ! -x "$directory/torbscript$exe" ]; then
    torb_built=no
  fi
}

# Runs one language of one program once. $1 language, $2 program, $3 input (empty for none), $4 where its standard
# output goes, $5 `warm` for the warm-up - with the timeout and the memory - or `timed`. Sets `elapsed` (nanoseconds)
# and `peak` (KiB, 0 where it was not measured), and answers the program's exit status.
run_once() {
  run_language=$1
  run_program=$2
  run_input=$3
  run_output=$4
  run_kind=$5
  set --
  : >"$work/memory"
  if [ "$run_kind" = warm ]; then
    if [ -n "$gnu_time" ]; then
      set -- "$gnu_time" -f %M -o "$work/memory"
    fi
    if [ -n "$timeout_command" ]; then
      set -- "$@" "$timeout_command" "$timeout_seconds"
    fi
  fi
  case $run_language in
    c) set -- "$@" "$out/$run_program/c$exe" ;;
    torbscript) set -- "$@" "$out/$run_program/torbscript$exe" ;;
    torbscript-vm) set -- "$@" "$torb" run "$(source_of torbscript-vm "$run_program")" ;;
    python) set -- "$@" "$python" "$(source_of python "$run_program")" ;;
    node) set -- "$@" "$node" "$(source_of node "$run_program")" ;;
  esac
  if [ -n "$run_input" ]; then
    set -- "$@" "$run_input"
  fi
  run_started=$(date +%s%N)
  "$@" >"$run_output" 2>"$work/stderr"
  run_status=$?
  run_ended=$(date +%s%N)
  elapsed=$((run_ended - run_started))
  peak=$(tail -n 1 "$work/memory" 2>/dev/null | tr -dc '0-9')
  if [ -z "$peak" ]; then
    peak=0
  fi
  return "$run_status"
}

# The SHA-256 of an output. Windows writes a C program's `\n` as `\r\n`, so there every carriage return is dropped
# from every output alike before the comparison.
digest() {
  if [ "$windows" = yes ]; then
    tr -d '\r' <"$1" | $hasher | cut -d' ' -f1
  else
    $hasher <"$1" | cut -d' ' -f1
  fi
}

# The median, the fastest and the slowest of a file of nanoseconds, one a line, in seconds.
statistics() {
  sort -n "$1" | awk '
    { value[NR] = $1 }
    END {
      if (NR == 0) { print "0 0 0"; exit }
      if (NR % 2 == 1) { median = value[(NR + 1) / 2] } else { median = (value[NR / 2] + value[NR / 2 + 1]) / 2 }
      printf "%.6f %.6f %.6f\n", median / 1e9, value[1] / 1e9, value[NR] / 1e9
    }'
}

json_text() {
  printf '%s' "$1" | tr -d '\r' | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n\t' '  '
}

runs_of() {
  if [ "$1" = torbscript-vm ]; then echo "$vm_runs"; else echo "$runs"; fi
}

# Whether the tool a language needs is there at all.
available() {
  case $1 in
    python) [ -n "$python" ] ;;
    node) [ -n "$node" ] ;;
    *) true ;;
  esac
}

# Measures one input of one program in the given languages and appends its workload to $workloads_file. $1 program,
# $2 the name of the input (large, small, startup), $3 the input, then the languages.
measure_workload() {
  program=$1
  size=$2
  input=$3
  shift 3
  languages="$*"
  reference=""
  for language in $languages; do
    status=ok
    note=""
    memory=0
    : >"$work/$language.times"
    if ! available "$language"; then
      status=missing
      note="not installed"
    elif [ "$language" = c ] && [ "$c_built" = no ]; then
      status=build-failed
      note=$(head -n 3 "$out/$program/c.log")
    elif [ "$language" = torbscript ] && [ "$torb_built" = no ]; then
      status=build-failed
      note=$(grep -m 1 '^error' "$out/$program/torbscript.log" || head -n 3 "$out/$program/torbscript.log")
    else
      run_once "$language" "$program" "$input" "$work/$language.out" warm
      code=$?
      memory=$peak
      if [ "$code" -eq 124 ] && [ -n "$timeout_command" ]; then
        status=timeout
        note="more than $timeout_seconds s"
      elif [ "$code" -ne 0 ]; then
        status=failed
        note="exit code $code: $(head -n 3 "$work/stderr")"
      else
        hash=$(digest "$work/$language.out")
        if [ -z "$reference" ]; then
          reference=$hash
        elif [ "$hash" != "$reference" ]; then
          status=wrong-output
          note="the output differs from the C's"
          cp "$work/$language.out" "$out/$program/$language.$size.wrong-output"
        fi
      fi
    fi
    eval "status_$(echo "$language" | tr '-' '_')=\$status"
    eval "note_$(echo "$language" | tr '-' '_')=\$note"
    eval "memory_$(echo "$language" | tr '-' '_')=\$memory"
  done

  # The timed runs: in every round each language once, in the same order
  round=1
  most=$runs
  if [ "$vm_runs" -gt "$most" ]; then
    most=$vm_runs
  fi
  while [ "$round" -le "$most" ]; do
    for language in $languages; do
      eval "status=\$status_$(echo "$language" | tr '-' '_')"
      if [ "$status" != ok ] || [ "$round" -gt "$(runs_of "$language")" ]; then
        continue
      fi
      if run_once "$language" "$program" "$input" "$work/$language.out" timed; then
        echo "$elapsed" >>"$work/$language.times"
      else
        eval "status_$(echo "$language" | tr '-' '_')=failed"
        eval "note_$(echo "$language" | tr '-' '_')=\"a timed run failed\""
      fi
    done
    round=$((round + 1))
  done

  entries=""
  for language in $languages; do
    key=$(echo "$language" | tr '-' '_')
    eval "status=\$status_$key"
    eval "note=\$note_$key"
    eval "memory=\$memory_$key"
    count=0
    median=0
    fastest=0
    slowest=0
    if [ "$status" = ok ]; then
      count=$(wc -l <"$work/$language.times" | tr -d ' ')
      set -- $(statistics "$work/$language.times")
      median=$1
      fastest=$2
      slowest=$3
      printf '%-15s %-8s %-10s %-14s %10s s  (%s - %s)  %s KiB\n' \
        "$program" "$size" "$input" "$language" "$median" "$fastest" "$slowest" "$memory"
    else
      printf '%-15s %-8s %-10s %-14s %s: %s\n' "$program" "$size" "$input" "$language" "$status" "$note"
    fi
    entry=$(printf '{"language": "%s", "status": "%s", "runs": %s, "median": %s, "minimum": %s, "maximum": %s, "memory": %s, "note": "%s"}' \
      "$language" "$status" "$count" "$median" "$fastest" "$slowest" "$memory" "$(json_text "$note")")
    if [ -z "$entries" ]; then
      entries="        $entry"
    else
      entries="$entries,
        $entry"
    fi
  done
  workload=$(printf '    {"size": "%s", "input": "%s", "results": [\n%s\n    ]}' "$size" "$input" "$entries")
  if [ -s "$workloads_file" ]; then
    printf ',\n' >>"$workloads_file"
  fi
  printf '%s' "$workload" >>"$workloads_file"
}

# ----------------------------------------------------------------------------- the run -------------------------------

say "torb:   $("$torb" --version 2>&1 | head -n 1) ($torb)"
say "C:      $("$cc" --version 2>&1 | head -n 1), $c_flags"
version_or_missing() {
  if [ -n "$1" ]; then "$1" --version 2>&1 | head -n 1; else echo missing; fi
}
say "Python: $(version_or_missing "$python")"
say "Node:   $(version_or_missing "$node")"
say "memory: ${gnu_time:-not measured (no GNU /usr/bin/time)}"
say ""

programs_file="$work/programs.json"
: >"$programs_file"

# Starting a program that prints one line: the floor under every time below, which the VM's includes the front end of
mkdir -p "$out/startup"
printf 'print("nothing 0")\n' >"$out/startup/nothing.py"
printf 'console.log("nothing 0")\n' >"$out/startup/nothing.js"
build_program startup
workloads_file="$work/workloads.json"
: >"$workloads_file"
measure_workload startup startup "" c torbscript torbscript-vm python node
printf '  {"name": "startup", "workloads": [\n%s\n  ]}' "$(cat "$workloads_file")" >>"$programs_file"

for program in $programs; do
  build_program "$program"
  : >"$workloads_file"
  measure_workload "$program" large "$(large_input "$program")" c torbscript node
  measure_workload "$program" small "$(small_input "$program")" c torbscript torbscript-vm python
  printf ',\n  {"name": "%s", "workloads": [\n%s\n  ]}' "$program" "$(cat "$workloads_file")" >>"$programs_file"
done

# ----------------------------------------------------------------------------- the report ----------------------------

processor=$(grep -m 1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/^[^:]*: *//')
cores=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)
memory_total=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo 2>/dev/null)
distribution=""
if [ -r /etc/os-release ]; then
  distribution=$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-}")
elif [ "$windows" = yes ]; then
  distribution="Windows"
fi
commit=${BENCHMARKS_COMMIT:-$(git rev-parse HEAD 2>/dev/null || echo "")}

{
  printf '{\n'
  printf '  "schema": 1,\n'
  printf '  "origin": "%s",\n' "$(json_text "$origin")"
  printf '  "measured": "%s",\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '  "commit": "%s",\n' "$(json_text "$commit")"
  printf '  "release": "%s",\n' "$(json_text "${BENCHMARKS_RELEASE-}")"
  printf '  "quick": %s,\n' "$([ "$quick" = yes ] && echo true || echo false)"
  printf '  "runs": %s,\n' "$runs"
  printf '  "vmRuns": %s,\n' "$vm_runs"
  printf '  "machine": {"system": "%s", "distribution": "%s", "processor": "%s", "cores": %s, "memoryKibibytes": %s},\n' \
    "$(json_text "$(uname -srm)")" "$(json_text "$distribution")" "$(json_text "$processor")" "${cores:-0}" \
    "${memory_total:-0}"
  printf '  "tools": {"torbscript": "%s", "c": "%s", "cFlags": "%s", "python": "%s", "node": "%s", "memory": "%s"},\n' \
    "$(json_text "$("$torb" --version 2>&1 | head -n 1)")" \
    "$(json_text "$("$cc" --version 2>&1 | head -n 1)")" \
    "$c_flags" \
    "$(json_text "${python:+$("$python" --version 2>&1 | head -n 1)}")" \
    "$(json_text "${node:+$("$node" --version 2>&1 | head -n 1)}")" \
    "$([ -n "$gnu_time" ] && echo "GNU time, maximum resident set size" || echo "")"
  printf '  "programs": [\n'
  cat "$programs_file"
  printf '\n  ]\n'
  printf '}\n'
} >"$report"

say ""
say "wrote $report"
