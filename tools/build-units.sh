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
#   sh tools/build-units.sh <list> <cache directory> <compiler> [flag]...
#
# Every source is compiled with `<compiler> [flag]... [its flag] -c -o <temporary> <source>`, and the temporary file is
# moved to the object's name only once the compiler succeeded - so a build that is interrupted, or two builds that
# compile the same object at once, never leave half an object behind under a name another build reads. At most
# `$TORB_BUILD_JOBS` compiles run at a time, by default as many as the machine has processors, each lane of them
# taking every n-th line of the list: `torb build` lists the largest files first. Then the objects are linked, with
# `-flto=<jobs>` where `torb build` asks for the program to be optimized across its units - unless the binary of the
# very same link is in the cache, and then it is copied.
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

# What is compiled, one `<object> <TAB> <source> <TAB> <flag>` per line: every `compile` line, and every `keep` line
# whose object is gone since `torb build` looked - the trim of another build may have taken it
pending="$list.pending"
: >"$pending"
while IFS="$tab" read -r kind object source extra; do
  case "$kind" in
    compile)
      printf '%s\t%s\t%s\n' "$object" "$source" "$extra" >>"$pending"
      ;;
    keep)
      if [ -f "$object" ]; then
        touch "$object"
      else
        printf '%s\t%s\t%s\n' "$object" "$source" "$extra" >>"$pending"
      fi
      ;;
  esac
done <"$list"

total=$(wc -l <"$pending" | tr -d ' ')
jobs=$(processors)
lanes=$jobs
if [ "$total" -lt "$lanes" ]; then
  lanes=$total
fi

# One lane compiles lines lane, lane + lanes, lane + 2 lanes, ... one after the other, and leaves what a failing
# compiler said in `<list>.failed-<line>`
compile_lane() {
  lane=$1
  shift
  status=0
  number=0
  while IFS="$tab" read -r object source extra; do
    if [ $((number % lanes)) -eq "$lane" ]; then
      temporary="$object.$$-$lane.tmp"
      # shellcheck disable=SC2086
      if "$@" $extra -c -o "$temporary" "$source" >"$temporary.log" 2>&1; then
        # A build that compiled the same object at the same moment may have moved its own there already, and on Windows
        # a file that a linker holds open cannot be replaced: either way the object is there and complete
        if ! mv -f "$temporary" "$object" 2>/dev/null; then
          rm -f "$temporary"
          if [ ! -f "$object" ]; then
            printf '%s\n' "$source: the object could not be moved to $object" >"$list.failed-$number"
            status=1
          fi
        fi
      else
        {
          printf '%s\n' "$source:"
          cat "$temporary.log"
        } >"$list.failed-$number"
        rm -f "$temporary"
        status=1
      fi
      rm -f "$temporary.log"
    fi
    number=$((number + 1))
  done <"$pending"
  return "$status"
}

# The link, with the arguments of the `link` lines in their order - or a copy of the binary an earlier link of the very
# same arguments left in the cache. A C compiler on Windows appends `.exe` to an output name without an extension, so
# the kept binary carries the name's ending with it.
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
  if [ -n "$kept" ] && [ -n "$output" ]; then
    for ending in "" ".exe"; do
      if [ -f "$kept$ending" ] && cp -f "$kept$ending" "$output$ending" 2>/dev/null; then
        touch "$kept$ending"
        return 0
      fi
    done
  fi
  "$@" >"$list.link.log" 2>&1
  status=$?
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

failed=0
pids=""
lane=0
while [ "$lane" -lt "$lanes" ]; do
  compile_lane "$lane" "$@" &
  pids="$pids $!"
  lane=$((lane + 1))
done
for pid in $pids; do
  if ! wait "$pid"; then
    failed=1
  fi
done

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
