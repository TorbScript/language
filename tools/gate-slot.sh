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
# command's. A slot is a directory taken with `mkdir` (atomic); a slot whose holder is gone (`kill -0`) or that is older
# than six hours is taken over.
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

is_stale() {
  slot=$1
  if [ -f "$slot/pid" ]; then
    holder=$(cat "$slot/pid" 2>/dev/null)
    if [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; then
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
