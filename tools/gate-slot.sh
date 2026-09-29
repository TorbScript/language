#!/bin/sh
# Runs a gate while holding one of a few machine-wide gate slots.
#
# A gate run is not one big compile but hundreds of small ones, a bootstrap and many test binaries. Several checkouts
# running `tools/gates.sh` at once - agents in their own worktrees, a gate in the background - exhaust the machine's
# processes and memory (`fork: Resource temporarily unavailable`, and once a crash of the whole machine). The build
# slots of `tools/build-slot.sh` only bound the large compiles; this bounds whole gate runs: at most
# `$TORB_GATE_SLOTS` (default 2) run at the same time on the machine, whichever checkout they come from.
#
#   sh tools/gate-slot.sh <command> [argument]...
#
# `tools/gates.sh` and `tools/bootstrap.sh` run themselves through this script, so every gate run and every bootstrap
# takes a slot without its caller doing anything. The command's output is passed through, and the exit code is the
# command's. A slot is a directory taken with `mkdir` (atomic); a slot whose holder is gone (`kill -0`, or `tasklist`
# by Windows process id and image name under Git Bash - see `holder_alive` below) or that is older than six hours is
# taken over.
#
# **One slot per run, however deep it nests.** The command runs with `TORB_GATE_SLOT_HELD=1` in its environment, and a
# call of this script that finds it set runs its command at once without taking a second slot: `gates.sh` calls
# `bootstrap.sh`, and two slots for one run would let two runs wait for each other.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -u

if [ $# -lt 1 ]; then
  printf '%s\n' "usage: sh tools/gate-slot.sh <command> [argument]..." >&2
  exit 2
fi

if [ "${TORB_GATE_SLOT_HELD-}" = "1" ]; then
  exec "$@"
fi

slots=${TORB_GATE_SLOTS:-2}
case "$slots" in
  '' | *[!0-9]* | 0) slots=2 ;;
esac

temporary=${TMPDIR:-${TEMP:-${TMP:-/tmp}}}
# Git Bash hands a `TEMP` of Windows over as `C:\Users\...`; every caller has to arrive at the same directory
if command -v cygpath >/dev/null 2>&1; then
  temporary=$(cygpath -u "$temporary")
fi
directory="$temporary/torb-gate-slots"
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
# six hours - which is the bug this works around. `/proc/<pid>/winpid` gives the real Windows process id, and
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

is_stale() {
  slot=$1
  if [ -f "$slot/pid" ]; then
    holder=$(cat "$slot/pid" 2>/dev/null)
    if [ -n "$holder" ] && ! holder_alive "$slot" "$holder"; then
      return 0
    fi
    [ -n "$(find "$slot" -maxdepth 0 -mmin +360 2>/dev/null)" ]
    return $?
  fi
  [ -n "$(find "$slot" -maxdepth 0 -mmin +1 2>/dev/null)" ]
}

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
  printf '%s\n' "waiting for one of $slots machine-wide gate slots (\$TORB_GATE_SLOTS) - other gate runs are in progress" >&2
  while ! take_slot; do
    sleep 5
  done
fi

TORB_GATE_SLOT_HELD=1
export TORB_GATE_SLOT_HELD
"$@"
status=$?
release
exit "$status"
