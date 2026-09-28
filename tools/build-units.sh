#!/bin/sh
# Compiles the C files of one `torb build` into objects, several at a time, keeps each object in the cache, and links.
#
# `torb build` splits a large program into units (docs/BACKEND.md 4.1) and compiles those and every file of the runtime
# into objects that are linked afterwards. It decides which objects the cache has already - an object is named after
# the hash of everything that decides it - and hands this script the list, one line per entry, fields apart by a tab:
#
#   compile <object> <source> [flag]    compile the source into the object, with one more flag where there is one
#   keep    <object> <source> [flag]    the object is in the cache: touch it, and compile it where it is gone
#   link    <argument>                  one argument of the link, the first one the C compiler, in order
#   link-jobs <argument>                one more argument of the link: <argument> followed by the number of jobs
#   link-output <binary>                the binary the link writes
#   link-cache <file>                   where the binary of these very link arguments is kept
#
# A list without `link` lines only compiles: `torb build` compiles mbedTLS that way, once per checkout.
#
#   sh tools/build-units.sh <list> <cache directory> <compiler> [flag]...
#
# Every source is compiled with `<compiler> [flag]... [its flag] -c -o <temporary> <source>`, and the temporary file is
# moved to the object's name only once the compiler succeeded - so a build that is interrupted never leaves half an
# object behind under a name another build reads. An object is compiled by one build at a time: the build that
# compiles it holds `<object>.lock`, a directory taken with `mkdir` as a build slot is (`tools/build-slot.sh`), and a
# build that finds it taken compiles the rest of its lane first and then waits for the object and uses it - several
# programs that start at once, the conformance suite's, compile the runtime and mbedTLS once and not once each. A lock
# whose build is gone (`kill -0` of the process id in it) or that is older than an hour is taken over. At most
# `$TORB_BUILD_JOBS` compiles run at a time, by default as many as the machine has processors, each lane of them
# taking every n-th line of the list: `torb build` lists the largest files first. Then the objects are linked, with
# `-flto=<jobs>` where `torb build` asks for the program to be optimized across its units - unless the binary of the
# very same link is in the cache, and then it is copied. A link whose arguments are longer than
# `$TORB_RESPONSE_FILE_BYTES` (default 16384) reads them from a response file, `@<list>.link.arguments`.
#
# What a failing compiler or linker said is printed on standard output once every compile has ended, in the order of
# the list, and the exit code is 1; nothing else is printed. `torb build` runs this through `tools/build-slot.sh` where
# what it compiles is large, so the whole batch holds one machine-wide build slot.
#
# Afterwards the cache is trimmed to `$TORB_OBJECT_CACHE_MB` (default 2048) megabytes, the oldest objects first, and never
# one that was used in the last hour - every object of this build was just written or touched.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -u

if [ $# -lt 3 ]; then
  printf '%s\n' "usage: sh tools/build-units.sh <list> <cache directory> <compiler> [flag]..." >&2
  exit 2
fi

list=$1
cache=$2
shift 2

tab=$(printf '\t')

# The length of the link's arguments from which they are handed over in a response file: half of what Windows takes,
# since MSYS may lengthen a path on the way; `$TORB_RESPONSE_FILE_BYTES` sets another, `0` a response file always
response_threshold=${TORB_RESPONSE_FILE_BYTES:-16384}
case "$response_threshold" in
  '' | *[!0-9]*) response_threshold=16384 ;;
esac

processors() {
  case "${TORB_BUILD_JOBS-}" in
    '' | *[!0-9]* | 0) ;;
    *)
      printf '%s\n' "$TORB_BUILD_JOBS"
      return
      ;;
  esac
  count=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || printf '%s\n' "${NUMBER_OF_PROCESSORS:-4}")
  case "$count" in
    '' | *[!0-9]* | 0) count=4 ;;
  esac
  printf '%s\n' "$count"
}

# What is compiled, one `<number> <TAB> <object> <TAB> <source> <TAB> <flag>` per line: every `compile` line, and every
# `keep` line whose object is gone since `torb build` looked - the trim of another build may have taken it. The number
# is the line's place, which orders what failing compilers said.
pending="$list.pending"
deferred="$list.deferred"
: >"$pending"
total=0
while IFS="$tab" read -r kind object source extra; do
  case "$kind" in
    compile)
      printf '%s\t%s\t%s\t%s\n' "$total" "$object" "$source" "$extra" >>"$pending"
      total=$((total + 1))
      ;;
    keep)
      if [ -f "$object" ]; then
        touch "$object"
      else
        printf '%s\t%s\t%s\t%s\n' "$total" "$object" "$source" "$extra" >>"$pending"
        total=$((total + 1))
      fi
      ;;
  esac
done <"$list"

jobs=$(processors)

# The lock of one object: `mkdir` creates it or fails, so exactly one build takes it, and it holds the process id of
# this script - a lane is a subshell, and `$$` in it is the script's, which lives until every lane has ended.
take_lock() {
  lock="$1.lock"
  if mkdir "$lock" 2>/dev/null; then
    printf '%s\n' "$$" >"$lock/pid"
    return 0
  fi
  if lock_is_stale "$lock" && rm -rf "$lock" 2>/dev/null && mkdir "$lock" 2>/dev/null; then
    printf '%s\n' "$$" >"$lock/pid"
    return 0
  fi
  return 1
}

# A lock is stale when the build that holds it is gone, or when it is older than any compile takes. One without a
# process id is one whose holder stopped between `mkdir` and writing it, and it gets a minute.
lock_is_stale() {
  if [ -f "$1/pid" ]; then
    holder=$(cat "$1/pid" 2>/dev/null)
    if [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; then
      return 0
    fi
    [ -n "$(find "$1" -maxdepth 0 -mmin +60 2>/dev/null)" ]
    return $?
  fi
  [ -n "$(find "$1" -maxdepth 0 -mmin +1 2>/dev/null)" ]
}

# Compiles one line of the list into its object, and leaves what a failing compiler said in `<list>.failed-<line>`
compile_one() {
  number=$1
  object=$2
  source=$3
  extra=$4
  shift 4
  temporary="$object.$$-$lane.tmp"
  result=0
  # shellcheck disable=SC2086
  if "$@" $extra -c -o "$temporary" "$source" >"$temporary.log" 2>&1; then
    # On Windows a file that a linker holds open cannot be replaced, and then the object is there and complete
    if ! mv -f "$temporary" "$object" 2>/dev/null; then
      rm -f "$temporary"
      if [ ! -f "$object" ]; then
        printf '%s\n' "$source: the object could not be moved to $object" >"$list.failed-$number"
        result=1
      fi
    fi
  else
    {
      printf '%s\n' "$source:"
      cat "$temporary.log"
    } >"$list.failed-$number"
    rm -f "$temporary"
    result=1
  fi
  rm -f "$temporary.log"
  return "$result"
}

# One lane compiles lines lane, lane + lanes, lane + 2 lanes, ... of the pending ones, one after the other. An object
# another build is compiling is put aside in `<list>.deferred-<lane>`, for the round after the lanes have ended.
compile_lane() {
  lane=$1
  shift
  status=0
  position=0
  while IFS="$tab" read -r number object source extra; do
    if [ $((position % lanes)) -eq "$lane" ] && [ ! -f "$object" ]; then
      if take_lock "$object"; then
        # The build that held the lock before may have finished the object between the test and the lock
        if [ ! -f "$object" ] && ! compile_one "$number" "$object" "$source" "$extra" "$@"; then
          status=1
        fi
        rm -rf "$object.lock"
      else
        printf '%s\t%s\t%s\t%s\n' "$number" "$object" "$source" "$extra" >>"$list.deferred-$lane"
      fi
    fi
    position=$((position + 1))
  done <"$pending"
  return "$status"
}

# One round: the pending lines over as many lanes as there are jobs, and what they put aside gathered in `<deferred>`
run_lanes() {
  count=$(wc -l <"$pending" | tr -d ' ')
  lanes=$jobs
  if [ "$count" -lt "$lanes" ]; then
    lanes=$count
  fi
  pids=""
  lane=0
  while [ "$lane" -lt "$lanes" ]; do
    rm -f "$list.deferred-$lane"
    compile_lane "$lane" "$@" &
    pids="$pids $!"
    lane=$((lane + 1))
  done
  for pid in $pids; do
    if ! wait "$pid"; then
      failed=1
    fi
  done
  : >"$deferred"
  lane=0
  while [ "$lane" -lt "$lanes" ]; do
    if [ -f "$list.deferred-$lane" ]; then
      cat "$list.deferred-$lane" >>"$deferred"
      rm -f "$list.deferred-$lane"
    fi
    lane=$((lane + 1))
  done
}

# Waits until every object put aside is there, or one of them has nobody compiling it any more: its lock is gone, or
# the build that holds it is (asked every ten seconds). The test of a file and of a directory is the shell's own, so a
# build that waits starts one process a second and takes nothing from the one that compiles.
wait_for_deferred() {
  seconds=0
  while :; do
    held=""
    while IFS="$tab" read -r number object source extra; do
      if [ ! -f "$object" ]; then
        if [ ! -d "$object.lock" ]; then
          return 0
        fi
        if [ -z "$held" ]; then
          held="$object.lock"
        fi
      fi
    done <"$deferred"
    if [ -z "$held" ]; then
      return 0
    fi
    seconds=$((seconds + 1))
    if [ $((seconds % 10)) -eq 0 ] && lock_is_stale "$held"; then
      return 0
    fi
    sleep 1
  done
}

# The link, with the arguments of the `link` lines in their order - or a copy of the binary an earlier link of the very
# same arguments left in the cache, or one a build linking them at the same time leaves there.
link_program() {
  output=""
  kept=""
  set --
  while IFS="$tab" read -r kind argument; do
    case "$kind" in
      link) set -- "$@" "$argument" ;;
      link-jobs) set -- "$@" "$argument$jobs" ;;
      link-output) output=$argument ;;
      link-cache) kept=$argument ;;
    esac
  done <"$list"
  [ $# -gt 0 ] || return 0
  if [ -z "$kept" ] || [ -z "$output" ]; then
    link_arguments "$@"
    return $?
  fi
  # Another build that links the very same arguments - a shard of the same test suite - holds the lock of the kept
  # binary: it is waited for, and what it keeps is copied
  while :; do
    if copy_kept; then
      return 0
    fi
    if take_lock "$kept"; then
      break
    fi
    sleep 1
  done
  if copy_kept; then
    status=0
  else
    link_arguments "$@"
    status=$?
  fi
  rm -rf "$kept.lock"
  return "$status"
}

# The binary an earlier link of these arguments kept, copied to the output. A C compiler on Windows appends `.exe` to an
# output name without an extension, so the kept binary carries the name's ending with it.
copy_kept() {
  for ending in "" ".exe"; do
    if [ -f "$kept$ending" ] && cp -f "$kept$ending" "$output$ending" 2>/dev/null; then
      touch "$kept$ending"
      return 0
    fi
  done
  return 1
}

# The link itself, and the binary kept in the cache where it succeeded
link_arguments() {
  linker=$1
  shift
  length=0
  for argument in "$@"; do
    length=$((length + ${#argument} + 3))
  done
  # Windows refuses a command line of more than 32767 characters, and a checkout with a long path - an agent's worktree
  # - comes close with the objects of the compiler's test suite, mbedTLS's among them. Such a link reads its arguments
  # from a response file, which gcc, clang and cl all read, and gcc hands on to its linker in one of its own: each one
  # in double quotes on a line of its own, a backslash turned into the slash every one of them reads in a path, because
  # gcc's quoting takes a backslash for an escape and that of Windows for a character
  if [ "$length" -gt "$response_threshold" ]; then
    for argument in "$@"; do
      printf '%s\n' "$argument"
    done | sed -e 's|\\|/|g' -e 's|"|\\"|g' -e 's|.*|"&"|' >"$list.link.arguments"
    set -- "$linker" "@$list.link.arguments"
  else
    set -- "$linker" "$@"
  fi
  "$@" >"$list.link.log" 2>&1
  status=$?
  rm -f "$list.link.arguments"
  if [ "$status" -ne 0 ]; then
    printf '%s\n' "the link:"
    cat "$list.link.log"
  elif [ -n "$kept" ] && [ -n "$output" ]; then
    ending=""
    if [ ! -f "$output" ] && [ -f "$output.exe" ]; then
      ending=".exe"
    fi
    if cp -f "$output$ending" "$kept$ending.$$.tmp" 2>/dev/null; then
      mv -f "$kept$ending.$$.tmp" "$kept$ending" 2>/dev/null || rm -f "$kept$ending.$$.tmp"
    fi
  fi
  rm -f "$list.link.log"
  return "$status"
}

# Rounds of lanes: the first compiles everything no other build is compiling, and each one after it takes what is still
# missing once another build is done with it - there, or gone without it
failed=0
while [ -s "$pending" ]; do
  run_lanes "$@"
  [ -s "$deferred" ] || break
  wait_for_deferred
  : >"$pending"
  while IFS="$tab" read -r number object source extra; do
    if [ ! -f "$object" ]; then
      printf '%s\t%s\t%s\t%s\n' "$number" "$object" "$source" "$extra" >>"$pending"
    fi
  done <"$deferred"
done
rm -f "$deferred"

if [ "$failed" -ne 0 ]; then
  number=0
  while [ "$number" -lt "$total" ]; do
    if [ -f "$list.failed-$number" ]; then
      cat "$list.failed-$number"
      rm -f "$list.failed-$number"
    fi
    number=$((number + 1))
  done
fi
rm -f "$pending"

if [ "$failed" -eq 0 ] && ! link_program; then
  failed=1
fi

# The trim: the oldest files first, while the cache is larger than its limit, and only what nobody used for an hour
limit=${TORB_OBJECT_CACHE_MB:-2048}
case "$limit" in
  '' | *[!0-9]*) limit=2048 ;;
esac
used=$(du -sk "$cache" 2>/dev/null | cut -f 1)
case "$used" in
  '' | *[!0-9]*) used=0 ;;
esac
if [ "$used" -gt $((limit * 1024)) ]; then
  # shellcheck disable=SC2012
  ls -tr "$cache" 2>/dev/null | while IFS= read -r name; do
    [ "$used" -gt $((limit * 1024)) ] || break
    file="$cache/$name"
    [ -f "$file" ] || continue
    [ -n "$(find "$file" -mmin +60 2>/dev/null)" ] || break
    size=$(($(wc -c <"$file" | tr -d ' ') / 1024))
    rm -f "$file"
    used=$((used - size))
  done
fi

exit "$failed"
