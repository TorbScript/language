#!/bin/sh
# The package manager end to end (docs/design/PROJECT.md section 11, docs/design/RELEASE.md section 7): the packages of
# `tests/packages/sources/` published into a `file:` registry, then `torb add`, a build and a run of
# `tests/packages/app` against what was installed, `torb update` - refused while it gains a capability, taken with
# `--accept-capabilities` - `torb remove`, `torb install` into an empty cache, an archive that is not the one the lock
# pins, a set of requirements with no solution, and `torb publish` refusing what it must refuse.
#
# Every command and its output, standard error included, and its exit code go into a transcript, which is compared
# with `tests/packages/transcript.expected`. The run happens in a temporary directory with a cache of its own
# (`TORB_CACHE`), so it sees neither this repository's workspace nor the machine's cache. The archives are
# deterministic and the fixture is checked out byte for byte (.gitattributes), so the tree hashes in the transcript are
# the same on every machine.
#
# `$PACKAGES_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/packages.sh
#   sh tools/packages.sh --update     # rewrite transcript.expected from what the commands answer now

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
}

update=0
if [ "${1-}" = "--update" ]; then
  update=1
fi

torb=$(binary_of "${PACKAGES_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  printf '%s\n' "packages.sh: no build/release/torb - run: sh tools/bootstrap.sh" >&2
  exit 1
}
case "$torb" in
  /* | ?:*) ;;
  *) torb="$root/$torb" ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R tests/packages/app tests/packages/sources "$work/"
TORB_CACHE="$work/cache"
export TORB_CACHE
# The standard library of this checkout, whatever the temporary directory is next to
TORB_STD="$root/std"
TORB_RUNTIME="$root/runtime"
export TORB_STD TORB_RUNTIME
transcript="$work/transcript"
: > "$transcript"

# One step: the command as it is written in the transcript, then what it printed and how it ended.
step() {
  printf '$ torb %s\n' "$*" >> "$transcript"
  if output=$(cd "$work" && "$torb" "$@" 2>&1); then
    status=0
  else
    status=$?
  fi
  if [ -n "$output" ]; then
    printf '%s\n' "$output" >> "$transcript"
  fi
  printf '[exit %s]\n\n' "$status" >> "$transcript"
}

# A file of the work directory, as it is now.
show() {
  printf '$ cat %s\n' "$1" >> "$transcript"
  cat "$work/$1" >> "$transcript"
  printf '\n' >> "$transcript"
}

step publish --project sources/json-1.0.0
step check app
step add acme/json --project app
show app/project.trb
show app/project.lock.trb
step run ./app
step run --native ./app

step publish --project sources/json-1.1.0
# A package with a dependency is checked against its own lock before it is published
step publish --dry-run --project sources/text-1.0.0
step update --project sources/text-1.0.0
step publish --project sources/text-1.0.0
step update --project app
step run ./app
step add acme/text --project app
show app/project.lock.trb

step add acme/json@=1.0.0 --project app
step add acme/json@^9.0.0 --project app
show app/project.trb

step publish --project sources/json-1.2.0
step update --project app
step update acme/json --accept-capabilities --project app
step run ./app
step remove acme/text --project app
show app/project.lock.trb

rm -rf "$work/cache"
step install --project app
step check app

# An archive that is not the one the lock pins: the tree hash says so before anything is written into the cache
cp "$work/registry/archives/acme/json/1.1.0.tar.gz" "$work/registry/archives/acme/json/1.2.0.tar.gz"
rm -rf "$work/cache"
step install --project app

step publish --dry-run --project app
step publish --project sources/json-1.0.0

expected=tests/packages/transcript.expected
if [ "$update" -eq 1 ]; then
  cp "$transcript" "$expected"
  printf '%s\n' "wrote $expected"
  exit 0
fi
if ! cmp -s "$transcript" "$expected"; then
  printf '%s\n' "packages.sh: the transcript is not $expected:"
  diff "$expected" "$transcript" || true
  exit 1
fi
printf '%s\n' "packages.sh: the transcript is $expected"
