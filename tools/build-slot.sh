#!/bin/sh
# Runs one C compile while holding one of a few machine-wide build slots.
#
# The C of the compiler is about 60 MB, and one `cc1` over it needs gigabytes. Several checkouts of this repository
# building at once - agents in their own worktrees, a gate in the background - run the machine out of memory
# (`cc1: out of memory`), and nothing of that is visible until one of them fails. A slot is a directory under the
# system's temporary directory, taken with `mkdir`, which either creates it or fails and is therefore atomic: at most
# `$TORB_BUILD_SLOTS` (default 3) compiles of that size run at the same time on the machine, whichever checkout they
# come from.
#
# `torb build` calls this for a C file of at least 8 MB and compiles smaller programs directly, so a small program
# never waits. It is found beside the runtime (`<runtime>/../tools/build-slot.sh`) and run with `sh`; where either is
# missing, the compile runs without a slot.
#
#   sh tools/build-slot.sh <log> <compiler> [argument]...
#
# The compiler's standard output and standard error go to `<log>`, and the exit code is the compiler's. The only line
# this script writes itself is the one on standard error when every slot is taken.
#
# A slot whose holder is gone is taken over: the directory holds the process id of the shell that took it, and a
# process that no longer exists (`kill -0`, or `tasklist` by Windows process id and image name under Git Bash - see
# `holder_alive` below) or a slot older than an hour frees it. The shell removes its slot on the way out, on an
# interrupt as well.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -u

if [ $# -lt 2 ]; then
  printf '%s\n' "usage: sh tools/build-slot.sh <log> <compiler> [argument]..." >&2
  exit 2
fi

log=$1
shift

slots=${TORB_BUILD_SLOTS:-3}
case "$slots" in
  '' | *[!0-9]* | 0) slots=3 ;;
esac

temporary=${TMPDIR:-${TEMP:-${TMP:-/tmp}}}
# Git Bash hands a `TEMP` of Windows over as `C:\Users\...`; every caller has to arrive at the same directory
if command -v cygpath >/dev/null 2>&1; then
  temporary=$(cygpath -u "$temporary")
fi
directory="$temporary/torb-build-slots"
mkdir -p "$directory" 2>/dev/null

held=""

release() {
  if [ -n "$held" ]; then
    rm -rf "$held"
    held=""
  fi
}

# On MSYS/Cygwin (Git Bash) a slot also records the Windows process id and image name of the process that holds it
# (`take_slot` below): `kill -0` of an MSYS process id whose process already exited can fall back to interpreting the
# number as a raw Windows process id and find a live, unrelated process there, so a dead holder looks alive for up to
# an hour - which is the bug this works around. `/proc/<pid>/winpid` gives the real Windows process id, and
# `tasklist` asks Windows about it directly; the image name recorded alongside it rules out a Windows process id
# that has since been reused by a different program. Elsewhere (Linux, macOS, FreeBSD) there is no such file and
# `kill -0` is exact, so nothing changes there.
windows_pid=""
windows_image=""
if [ -r "/proc/$$/winpid" ] && command -v tasklist >/dev/null 2>&1; then
  read -r windows_pid <"/proc/$$/winpid" 2>/dev/null
  if [ -n "$windows_pid" ]; then
    seen=$(tasklist //FI "PID eq $windows_pid" //FO CSV //NH 2>/dev/null)
    case "$seen" in
      '"'*)
        rest=${seen#\"}
        windows_image=${rest%%\"*}
        ;;
    esac
  fi
  [ -n "$windows_image" ] || windows_pid=""
fi

# Whether the process recorded for a slot is still the one that holds it. Where the slot also recorded a Windows
# process id and image name (see above), this asks Windows with `tasklist` instead of `kill -0`, and checks the image
# name too, so a Windows process id since reused by a different program is not mistaken for the same holder.
holder_alive() {
  slot=$1
  holder=$2
  if [ -f "$slot/winholder" ]; then
    wpid=""
    wimage=""
    { read -r wpid; read -r wimage; } <"$slot/winholder" 2>/dev/null
    seen=$(tasklist //FI "PID eq $wpid" //FO CSV //NH 2>/dev/null)
    case "$seen" in
      '"'*)
        rest=${seen#\"}
        [ "${rest%%\"*}" = "$wimage" ]
        return $?
        ;;
      *)
        return 1
        ;;
    esac
  fi
  kill -0 "$holder" 2>/dev/null
}

# A slot is free again when the shell that holds it is gone, or when it has been held for longer than any compile takes.
# A slot without a process id is one whose holder was stopped between `mkdir` and writing it, and it gets a minute.
is_stale() {
  slot=$1
  if [ -f "$slot/pid" ]; then
    holder=$(cat "$slot/pid" 2>/dev/null)
    if [ -n "$holder" ] && ! holder_alive "$slot" "$holder"; then
      return 0
    fi
    [ -n "$(find "$slot" -maxdepth 0 -mmin +60 2>/dev/null)" ]
    return $?
  fi
  [ -n "$(find "$slot" -maxdepth 0 -mmin +1 2>/dev/null)" ]
}

# One pass over every slot: takes the first free one, and frees a stale one on the way. Two shells that free the same
# stale slot at the same moment can both end up compiling, which costs one extra compile and nothing else.
take_slot() {
  number=1
  while [ "$number" -le "$slots" ]; do
    slot="$directory/slot-$number"
    if mkdir "$slot" 2>/dev/null; then
      held=$slot
      printf '%s\n' "$$" >"$slot/pid"
      if [ -n "$windows_pid" ]; then
        printf '%s\n%s\n' "$windows_pid" "$windows_image" >"$slot/winholder"
      fi
      return 0
    fi
    if is_stale "$slot" && rm -rf "$slot" 2>/dev/null; then
      continue
    fi
    number=$((number + 1))
  done
  return 1
}

trap 'release' EXIT
trap 'release; exit 130' INT
trap 'release; exit 143' TERM
trap 'release; exit 129' HUP

if ! take_slot; then
  printf '%s\n' "waiting for one of $slots machine-wide build slots (\$TORB_BUILD_SLOTS) - other C compiles of this size are running" >&2
  while ! take_slot; do
    sleep 2
  done
fi

"$@" >"$log" 2>&1
status=$?
release
exit "$status"
