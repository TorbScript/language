#!/bin/sh
#
# The worker pool against itself: `parallel-map.trb` built once and run with `TORB_WORKERS` of 1, 2, 4, 8 and every
# logical processor, each the fastest of $RUNS runs, with the speed-up over one worker.
#
#   sh parallel.sh
#   RUNS=9 sh parallel.sh
#
# Like run.sh this is a measurement and not a gate. The checksum must be the same on every line: the chunks of
# `parallel()` come back in input order whatever the number of workers.

set -u

here=$(dirname "$0")
cd "$here" || exit 1

torb=${TORB:-../build/release/torb.exe}
runs=${RUNS:-5}
out=${OUT:-out}/parallel-map

mkdir -p "$out"
if ! "$torb" build parallel-map.trb --output "$out/parallel-map" >"$out/build.log" 2>&1; then
  echo "parallel.sh: torb build failed, see $out/build.log" >&2
  exit 1
fi
binary="$out/parallel-map"
[ -f "$binary" ] || binary="$binary.exe"

# Microseconds of the fastest of $runs runs of the binary with the given number of workers.
fastest() {
  best=""
  run=0
  while [ "$run" -lt "$runs" ]; do
    run=$((run + 1))
    started=$(date +%s%N)
    TORB_WORKERS=$1 "$binary" >"$out/stdout" 2>&1
    ended=$(date +%s%N)
    took=$(((ended - started) / 1000))
    if [ -z "$best" ] || [ "$took" -lt "$best" ]; then
      best=$took
    fi
  done
  printf '%s\n' "$best"
}

processors=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
printf '%-8s %12s %9s  %s\n' workers microseconds speed-up checksum
single=""
for workers in 1 2 4 8 "$processors"; do
  took=$(fastest "$workers")
  [ -z "$single" ] && single=$took
  speedup=$(awk "BEGIN { printf \"%.2fx\", $single / $took }")
  printf '%-8s %12s %9s  %s\n' "$workers" "$took" "$speedup" "$(cat "$out/stdout")"
done
