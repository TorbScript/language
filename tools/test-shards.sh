#!/bin/sh
# Runs every shard of one test binary at the same time, and reports them one after the other.
#
# `torb test --native --shards <n>` builds a suite's binary once and hands it here: each shard, `--shard <k>/<n>`, runs
# in a process of its own, all of them at once, and writes its report into `<directory>/shard-<k>.log`. Once every one
# has ended the reports are printed in the order of the shards, and then one line with the counts of all of them,
# `N passed, M failed (K files, n shards)`. The exit code is the highest one a shard left with: 0 where nothing failed.
#
#   sh tools/test-shards.sh <binary> <count> <directory>
#
# One build and several processes of it, rather than one `torb test --shard k/n` per shard: every one of those checks
# and lowers the whole suite before its C is known to be the one the others build, which costs the front end once per
# shard - more than the shards save while the slowest file of the suite holds one of them.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -u
# A summary is split into words below, and a word of a panic's message is no pattern of file names
set -f

if [ $# -ne 3 ]; then
  printf '%s\n' "usage: sh tools/test-shards.sh <binary> <count> <directory>" >&2
  exit 2
fi

binary=$1
count=$2
directory=$3
case "$count" in
  '' | *[!0-9]* | 0)
    printf '%s\n' "tools/test-shards.sh: the count is a whole number above zero, and it is \`$count\`" >&2
    exit 2
    ;;
esac
mkdir -p "$directory"

pids=""
index=1
while [ "$index" -le "$count" ]; do
  "$binary" --shard "$index/$count" >"$directory/shard-$index.log" 2>&1 &
  pids="$pids $!"
  index=$((index + 1))
done
status=0
for pid in $pids; do
  wait "$pid"
  code=$?
  if [ "$code" -gt "$status" ]; then
    status=$code
  fi
done

# The last line of a shard's report is its summary, `N passed, M failed (K files, shard k of n)`; a shard that ended
# without one - a panic outside of every test - counts nothing, and its exit code says what happened
passed=0
failed=0
files=0
index=1
while [ "$index" -le "$count" ]; do
  log="$directory/shard-$index.log"
  cat "$log"
  summary=$(tail -n 1 "$log")
  # shellcheck disable=SC2086
  set -- $summary
  if [ $# -ge 5 ] && [ "$2" = "passed," ] && [ "$4" = "failed" ]; then
    passed=$((passed + $1))
    failed=$((failed + $3))
    shard_files=${5#"("}
    files=$((files + shard_files))
  fi
  index=$((index + 1))
done
printf '\n%s passed, %s failed (%s files, %s shards)\n' "$passed" "$failed" "$files" "$count"
exit "$status"
