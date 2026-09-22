#!/bin/sh
# Refreshes the seed from `build/release`: the binary and its C become the `torb` the next bootstrap starts from.
#
# The seed is not in git and exists only on this machine, so the one it replaces is archived first - outside of the
# repository, in `../torbscript-seeds/<short commit>-<date>/` beside the checkout (`$TORB_SEED_ARCHIVE` elsewhere) -
# and the five newest archives are kept. An archive is what a seed that turns out to be broken is rolled back to: copy
# its files into `seed/`. The commit in the name is the one the seed was built from, which `seed/commit` records.
#
# Run it after a merge whose tier A is green, from the checkout that has `seed/` (the main checkout, not a worktree).
# Nothing is copied unless the new binary runs: `torb --version` does not exist, so it has to check a one-line program.
#
#   sh tools/refresh-seed.sh                    # build/release -> seed/
#   sh tools/refresh-seed.sh /some/other/seed   # the same into another seed directory (a test of this script)
#   TORB_SEED_DIR=/some/other/seed sh tools/refresh-seed.sh
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "refresh-seed.sh: $*"
  exit 1
}

# A relative directory means relative to where the script was started, so it is made absolute before the `cd`.
absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
}

given=${1-${TORB_SEED_DIR-}}
if [ -n "$given" ]; then
  given=$(absolute "$given")
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

seed=${given:-$root/seed}
# Beside the directory that holds the seed: `<checkout>/seed` archives to `<checkout>/../torbscript-seeds`
archives=${TORB_SEED_ARCHIVE:-$(dirname "$(dirname "$seed")")/torbscript-seeds}
keep=5

binary=""
for candidate in build/release/torb build/release/torb.exe; do
  if [ -f "$candidate" ]; then
    binary=$candidate
  fi
done
[ -n "$binary" ] || fail "there is no build/release/torb - run: sh tools/bootstrap.sh"
[ -f build/release/program.c ] || fail "there is no build/release/program.c beside the binary - run: sh tools/bootstrap.sh"

# ----------------------------------------------------------------------------- does the new one run ---------------

probe=$(mktemp -d)
trap 'rm -rf "$probe"' EXIT
printf '%s\n' 'print "the seed runs"' >"$probe/probe.trb"
if ! output=$("$binary" check "$probe/probe.trb" 2>&1); then
  say "$output"
  fail "$binary cannot check a one-line program, so it does not become the seed"
fi
case "$output" in
  "1 files, no problems"*) ;;
  *)
    say "$output"
    fail "$binary answered something else than \"1 files, no problems\" for a one-line program"
    ;;
esac

# ----------------------------------------------------------------------------- archive the old one ----------------

name=$(basename "$binary")
if [ -f "$seed/$name" ] || [ -f "$seed/program.c" ]; then
  # The commit the old seed was built from, which the refresh that put it there wrote down
  commit=$(cat "$seed/commit" 2>/dev/null || echo unknown)
  archive="$archives/$commit-$(date +%Y-%m-%d-%H%M%S)"
  mkdir -p "$archive"
  for file in "$seed/$name" "$seed/program.c" "$seed/commit"; do
    if [ -f "$file" ]; then
      cp -p "$file" "$archive/"
    fi
  done
  say "archived the previous seed to $archive"
  # The names start with the commit, so the newest are the ones with the newest modification time
  # shellcheck disable=SC2012
  ls -1t "$archives" | tail -n +$((keep + 1)) | while IFS= read -r old; do
    rm -rf "${archives:?}/$old"
    say "removed the archive $archives/$old (only the $keep newest are kept)"
  done
fi

# ----------------------------------------------------------------------------- the new one ------------------------

mkdir -p "$seed"
# A running seed cannot be overwritten on Windows, but it can be renamed out of the way
if [ -f "$seed/$name" ]; then
  mv "$seed/$name" "$seed/$name.old-$$"
fi
cp -p "$binary" "$seed/$name"
cp -p build/release/program.c "$seed/program.c"
current=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
printf '%s\n' "$current" >"$seed/commit"
rm -f "$seed/$name.old-$$" 2>/dev/null || true
say "seed: $seed/$name and $seed/program.c, built from $current"
