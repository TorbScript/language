#!/bin/sh
#
# The performance suite: every program here against the C that does the same work.
#
#   sh run.sh                 build everything and print the table
#   sh run.sh list-index      one program
#   RUNS=9 sh run.sh          more repetitions; the fastest of them is what is printed
#   sh run.sh --allocations   also link a counted copy of each binary and print how often it allocated
#
# $TORB is the compiler that builds the programs, `../build/release/torb` by default, and it is called directly.
# $TORB_COMPILER names a compiler *package* for $TORB to run instead, for a $TORB that is a driver rather than a
# compiler; an empty value, which is the default, means $TORB is a `torb` binary already. Two builds of the compiler
# are compared against each other this way:
#
#   TORB=../seed/torb.exe sh run.sh
#
# It is POSIX sh and works under Git Bash on Windows. Nothing here is a gate: these are measurements, and a number
# that moves with the machine does not belong in a test.

set -u

here=$(dirname "$0")
cd "$here" || exit 1

torb=${TORB:-../build/release/torb.exe}
compiler=${TORB_COMPILER-}
runs=${RUNS:-5}
out=${OUT:-out}

programs="arithmetic call-depth wrapper closure list-index list-iterate pipeline record-write nested-write accumulate interpolation map-count"

allocations=no
wanted=""
for argument in "$@"; do
  case $argument in
    --allocations) allocations=yes ;;
    --help|-h)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    -*)
      echo "run.sh: unknown option $argument" >&2
      exit 2
      ;;
    *) wanted="$wanted $argument" ;;
  esac
done
if [ -n "$wanted" ]; then
  programs=$wanted
fi

compiler_command=${TORB_CC:-}
if [ -z "$compiler_command" ]; then
  for candidate in clang gcc cc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      compiler_command=$candidate
      break
    fi
  done
fi
if [ -z "$compiler_command" ]; then
  echo "run.sh: no C compiler found. Set \$TORB_CC, or put clang, gcc or cc on the PATH." >&2
  exit 3
fi
if [ ! -x "$torb" ]; then
  echo "run.sh: $torb is not there. Build the compiler first: sh tools/bootstrap.sh from the repository root." >&2
  exit 3
fi

mkdir -p "$out" || exit 1

# The flags of docs/BACKEND.md section 4, which is what `torb build` itself passes.
flags="-std=c11 -O2 -g0 -Wall -Wextra"

microseconds() {
  date +%s%N | cut -c1-16
}

# The fastest of `runs` runs, in microseconds, plus the line the program printed. A run that fails answers -1.
measure() {
  measure_binary=$1
  measure_best=-1
  measure_output=""
  measure_turn=0
  while [ "$measure_turn" -lt "$runs" ]; do
    measure_start=$(microseconds)
    measure_output=$("$measure_binary" 2>/dev/null)
    measure_status=$?
    measure_end=$(microseconds)
    if [ "$measure_status" -ne 0 ]; then
      measure_best=-1
      return 1
    fi
    measure_elapsed=$((measure_end - measure_start))
    if [ "$measure_best" -lt 0 ] || [ "$measure_elapsed" -lt "$measure_best" ]; then
      measure_best=$measure_elapsed
    fi
    measure_turn=$((measure_turn + 1))
  done
  return 0
}

# A ratio with two decimals, out of two integers, without a shell that has floating point.
ratio() {
  if [ "$2" -le 0 ]; then
    echo "-"
  else
    echo "$(( $1 * 100 / $2 ))" | sed 's/\(.*\)\(..\)$/\1.\2/;s/^\./0./;s/^-\./-0./'
  fi
}

# Build both sides of one program. Answers 0 and sets `binary` and `twin`, or answers non-zero and says why in `reason`.
build_both() {
  build_program=$1
  build_directory="$out/$build_program"
  reason=""
  mkdir -p "$build_directory" || exit 1
  if [ -n "$compiler" ]; then
    set -- run "$compiler" build
  else
    set -- build
  fi
  if ! "$torb" "$@" "$build_program.trb" --output "$build_directory/$build_program" \
      > "$build_directory/build.log" 2>&1; then
    reason=$(grep -m1 '^error:' "$build_directory/build.log" || echo "see $build_directory/build.log")
    return 1
  fi
  binary="$build_directory/$build_program"
  if [ ! -x "$binary" ] && [ -x "$binary.exe" ]; then
    binary="$binary.exe"
  fi
  # The C side, with the same compiler and the same flags.
  if ! $compiler_command $flags -o "$build_directory/twin" "c/$build_program.c" -lm \
      > "$build_directory/twin.log" 2>&1; then
    reason="the twin did not compile, see $build_directory/twin.log"
    return 1
  fi
  twin="$build_directory/twin"
  if [ ! -x "$twin" ] && [ -x "$twin.exe" ]; then
    twin="$twin.exe"
  fi
  return 0
}

# The floor: what starting either kind of process costs, which is subtracted from every row below.
torb_floor=0
twin_floor=0
if build_both nothing && measure "$binary"; then
  torb_floor=$measure_best
  if measure "$twin"; then
    twin_floor=$measure_best
  fi
fi

printf '%-14s %10s %10s %8s  %s\n' program torb c ratio checksum
printf '%-14s %10s %10s %8s  %s\n' -------------- ---------- ---------- -------- --------
skipped=""
counted=""

for program in $programs; do
  directory="$out/$program"

  if ! build_both "$program"; then
    printf '%-14s %10s %10s %8s  %s\n' "$program" skipped - - "$reason"
    skipped="$skipped $program"
    continue
  fi

  if ! measure "$binary"; then
    printf '%-14s %10s %10s %8s  %s\n' "$program" "did not run" - - "see $directory"
    continue
  fi
  torb_time=$((measure_best - torb_floor))
  torb_line=$measure_output
  if [ "$torb_time" -lt 1 ]; then
    torb_time=1
  fi

  if ! measure "$twin"; then
    printf '%-14s %10s %10s %8s  %s\n' "$program" "$torb_time" "did not run" - "$torb_line"
    continue
  fi
  twin_time=$((measure_best - twin_floor))
  twin_line=$measure_output
  if [ "$twin_time" -lt 1 ]; then
    twin_time=1
  fi

  if [ "$torb_line" = "$twin_line" ]; then
    checksum="$torb_line"
  else
    checksum="DIFFERENT: torb [$torb_line] c [$twin_line]"
  fi
  # Under two milliseconds the C side is inside the noise of the floor, so the ratio is only a lower bound.
  if [ "$twin_time" -lt 2000 ]; then
    shown=">$(ratio "$torb_time" 2000)x"
  else
    shown="$(ratio "$torb_time" "$twin_time")x"
  fi
  printf '%-14s %10s %10s %8s  %s\n' "$program" "$torb_time" "$twin_time" "$shown" "$checksum"

  if [ "$allocations" = yes ]; then
    wraps="-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc"
    if $compiler_command $flags -I../runtime/include -o "$directory/counted" \
        "$directory/program.c" ../runtime/*.c c/allocations.c -lm $wraps > "$directory/counted.log" 2>&1 \
      && $compiler_command $flags -o "$directory/twin-counted" "c/$program.c" c/allocations.c -lm $wraps \
        > "$directory/twin-counted.log" 2>&1; then
      torb_count=$("$directory/counted" 2>&1 >/dev/null | grep '^allocations')
      twin_count=$("$directory/twin-counted" 2>&1 >/dev/null | grep '^allocations')
      counted="$counted
$(printf '%-14s torb %-34s c %s' "$program" "$torb_count" "$twin_count")"
    else
      counted="$counted
$(printf '%-14s the counted link failed, see %s' "$program" "$directory/counted.log")"
    fi
  fi
done

if [ "$allocations" = yes ]; then
  echo
  echo "allocations (ld --wrap over malloc, calloc and realloc)"
  echo "$counted" | sed '/^$/d'
fi

if [ -n "$skipped" ]; then
  echo
  echo "skipped:$skipped - the native back end refused these, the reason is in the table"
fi

echo
echo "time is the fastest of $runs runs, in microseconds, net of the floor that nothing.trb measured"
echo "(torb $torb_floor, c $twin_floor). ratio is torb divided by c."
echo "compiler: $($compiler_command --version 2>&1 | head -1), flags: $flags"
