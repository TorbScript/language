#!/bin/sh
# Builds `torb` from the seed, and then builds it again with itself.
#
# This is how a checkout gets a compiler without Rust in it. The seed is a `torb` that already exists, taken from the
# first of these that has one:
#
#   1. `$TORB_SEED`: the binary it names, and nothing else - an explicit seed never falls back
#   2. `seed/torb`, or `seed/program.c` compiled into it (the main checkout of the maintainer's machine)
#   3. the newest archive of `tools/refresh-seed.sh` in `../torbscript-seeds/` (`$TORB_SEED_ARCHIVE`)
#   4. `build/seed/torb`: a published seed, downloaded, verified and compiled by `tools/fetch-seed.sh`. When there is
#      none, this script runs `tools/fetch-seed.sh` itself - a fresh clone needs nothing but a C compiler and the
#      network. `TORB_SEED_FETCH=0` forbids the download.
#
# None of them is in git: `seed/` and `build/` are ignored, and what goes in them is produced by an earlier `torb`.
#
# Two steps and not one, because one says nothing. The seed compiles the *current* sources, so the binary that comes
# out is built by an older compiler; that binary compiles the sources again, and if the two `program.c` are identical
# the compiler is a fixed point of itself. That is the same comparison the fixpoint gate makes.
#
# Every step writes into a staging directory of its own (`build/staging-<pid>/`), and `build/release/` receives the
# binary and its C only once the fixpoint holds - so a step that fails, or a second bootstrap running at the same
# time, never leaves `build/release/` half written, and a `torb` that is still running there is renamed out of the way
# instead of overwritten.
#
# A bootstrap is as heavy as a gate run, so it holds one of the machine-wide gate slots (`tools/gate-slot.sh`,
# `$TORB_GATE_SLOTS`, default 2) - unless it runs inside one already, as the bootstrap of `tools/gates.sh` does.
#
# POSIX sh. It runs in Git Bash on Windows, where the binaries are called `torb.exe`.
#
#   sh tools/bootstrap.sh              # seed -> torb -> torb, into build/release/torb
#   TORB_SEED=/path/to/torb sh tools/bootstrap.sh
#   TORB_CC=clang sh tools/bootstrap.sh
#   TORB_SEED_FETCH=0 sh tools/bootstrap.sh    # never download a seed

set -eu

if [ "${TORB_GATE_SLOT_HELD-}" != "1" ]; then
  exec sh "$(dirname -- "$0")/gate-slot.sh" sh "$0" "$@"
fi

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "bootstrap.sh: $*"
  exit 1
}

# A relative `$TORB_SEED` means relative to where the script was started, so it is made absolute before the `cd`.
if [ -n "${TORB_SEED-}" ]; then
  case "$TORB_SEED" in
    /* | [A-Za-z]:*) ;;
    *) TORB_SEED="$(pwd)/$TORB_SEED" ;;
  esac
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

# A C compiler on Windows appends `.exe` to an output name without an extension, so both names are the binary.
binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
}

# ----------------------------------------------------------------------------- the seed ---------------------------

seed=""
if [ -n "${TORB_SEED-}" ]; then
  seed=$(binary_of "$TORB_SEED")
  [ -n "$seed" ] || fail "\$TORB_SEED is set to \`$TORB_SEED\`, and there is no binary there"
else
  seed=$(binary_of "seed/torb")
fi

if [ -z "$seed" ] && [ -f "seed/program.c" ]; then
  # The portable seed: the compiler as one C file, compiled the way every seed is (tools/build-seed.sh)
  sh tools/build-seed.sh seed seed/torb || fail "the C compiler could not build seed/program.c"
  seed=$(binary_of "seed/torb")
  [ -n "$seed" ] || fail "the C compiler wrote no binary"
fi

# The archives `tools/refresh-seed.sh` keeps, newest first: what a missing or broken seed falls back to. An explicit
# $TORB_SEED never falls back - it names the one seed to use.
archives=${TORB_SEED_ARCHIVE:-$(dirname "$root")/torbscript-seeds}
archived_seeds() {
  [ -z "${TORB_SEED-}" ] || return 0
  [ -d "$archives" ] || return 0
  ls -1t "$archives" | while IFS= read -r archive; do
    binary_of "$archives/$archive/torb"
  done
}

if [ -z "$seed" ]; then
  seed=$(archived_seeds | head -n 1)
  [ -z "$seed" ] || say "seed/ has no seed: falling back to the newest archive, $seed"
fi

# A published seed: the one tools/fetch-seed.sh built before, or the newest one, fetched now
fetched=0
may_fetch() {
  [ -z "${TORB_SEED-}" ] && [ "${TORB_SEED_FETCH-1}" != "0" ]
}
if [ -z "$seed" ]; then
  seed=$(binary_of "build/seed/torb")
  if [ -n "$seed" ]; then
    fetched=1
    say "no seed on this machine: using the published seed $(cat build/seed/commit 2>/dev/null || echo '?') in build/seed/"
  elif may_fetch; then
    say "no seed on this machine: fetching the newest published seed (sh tools/fetch-seed.sh)"
    sh tools/fetch-seed.sh >/dev/null || fail "tools/fetch-seed.sh could not provide a seed"
    seed=$(binary_of "build/seed/torb")
    fetched=1
  fi
fi

if [ -z "$seed" ]; then
  say "bootstrap.sh: there is no seed."
  say ""
  say "  A seed is a \`torb\` that already exists, and it is not in git. One of these provides it:"
  say "    sh tools/fetch-seed.sh    downloads the newest published seed and compiles it into build/seed/"
  say "    seed/torb                 a binary for this platform"
  say "    seed/program.c            the compiler's own generated C, which builds anywhere a C compiler does"
  say ""
  say "  A checkout that has built build/release/torb writes seed/: sh tools/refresh-seed.sh"
  say "  A \`torb\` somewhere else on this machine: TORB_SEED=/path/to/torb sh tools/bootstrap.sh"
  exit 1
fi

say "seed: $seed"

# ----------------------------------------------------------------------------- the two steps ----------------------

staging="build/staging-$$"
rm -rf "$staging"
# A failed run keeps its staging directory for a look at what it wrote; a green one leaves nothing behind
succeeded=0
trap 'if [ "$succeeded" -eq 1 ]; then rm -rf "$staging"; fi' EXIT

# No `--profile`: `torb build` builds `release` unless told otherwise, and a seed may be older than the flag
say "step 1: the seed builds the compiler"
if ! "$seed" build ./compiler --output "./$staging/bootstrap/torb"; then
  # A seed that cannot build the sources is broken or too old; an archived one may still do it. The seed in seed/ is
  # left as it is - `tools/refresh-seed.sh` replaces it once a bootstrap is green.
  built=0
  # A published seed that was fetched some time ago may be older than a change the sources already rely on (the first
  # commit of a breaking change teaches the new form, and its seed is published before the second uses it)
  if [ "$fetched" -eq 1 ] && may_fetch; then
    before=$(cat build/seed/commit 2>/dev/null || echo "")
    say "step 1 failed with the published seed $before: fetching the newest one"
    if sh tools/fetch-seed.sh >/dev/null && [ "$(cat build/seed/commit 2>/dev/null)" != "$before" ]; then
      candidate=$(binary_of "build/seed/torb")
      if "$candidate" build ./compiler --output "./$staging/bootstrap/torb"; then
        seed=$candidate
        built=1
      fi
    fi
  fi
  for candidate in $(archived_seeds); do
    [ "$built" -eq 0 ] || break
    [ "$candidate" != "$seed" ] || continue
    say "step 1 failed with $seed: trying the archived seed $candidate"
    if "$candidate" build ./compiler --output "./$staging/bootstrap/torb"; then
      seed=$candidate
      built=1
      break
    fi
  done
  [ "$built" -eq 1 ] || fail "step 1 failed with the seed and with every archived seed in $archives"
  say "seed: $seed (an archive - refresh seed/ with sh tools/refresh-seed.sh once this is green)"
fi
first=$(binary_of "$staging/bootstrap/torb")
[ -n "$first" ] || fail "step 1 wrote no binary"

say "step 2: that compiler builds the compiler again"
"$first" build ./compiler --output "./$staging/release/torb"
second=$(binary_of "$staging/release/torb")
[ -n "$second" ] || fail "step 2 wrote no binary"

# Moves the binary of step 2 and its C into `build/release/`. A running `torb.exe` cannot be overwritten or deleted on
# Windows, but it can be renamed, so the old one steps aside first and is removed where that is possible - now, or by
# the next bootstrap once nothing runs it any more.
install() {
  mkdir -p build/release
  rm -f build/release/*.old-* 2>/dev/null || true
  name=$(basename "$second")
  if [ -f "build/release/$name" ]; then
    mv "build/release/$name" "build/release/$name.old-$$"
  fi
  mv "$second" "build/release/$name"
  mv "$staging/release/program.c" build/release/program.c
  # The hash of that C is the identity `torb run` keys its cache on; an old one must never stay beside a new program.c
  if [ -f "$staging/release/program.hash" ]; then
    mv "$staging/release/program.hash" build/release/program.hash
  else
    rm -f build/release/program.hash
  fi
  rm -f "build/release/$name.old-$" 2>/dev/null || true
  # Written last, so it is newer than the binary: tier B's fixpoint gate skips a second bootstrap of the same sources
  : >build/release/fixpoint
  succeeded=1
}

# ----------------------------------------------------------------------------- the fixed point --------------------
#
# Step 1 was emitted by the seed, step 2 by the compiler itself. They agree unless the change being built alters what
# the compiler emits - then the seed writes the old C and the fresh compiler the new, and the honest comparison is a
# third step: the compiler of step 2 building the compiler once more. Only two builds of the SAME compiler have to agree.

refresh_hint="A merge whose tier A is green refreshes the seed from it: sh tools/refresh-seed.sh"

# Whether two steps emitted the same C: `program.c`, the whole program, and - where both steps split it into units
# (docs/BACKEND.md 4.1; a seed older than the split writes none) - the header and every unit as well
same_c() {
  cmp -s "$1/program.c" "$2/program.c" || return 1
  [ -f "$1/program.h" ] && [ -f "$2/program.h" ] || return 0
  for file in "$1"/program.h "$1"/program-*.c "$2"/program-*.c; do
    name=$(basename "$file")
    cmp -s "$1/$name" "$2/$name" || return 1
  done
}

if same_c "$staging/bootstrap" "$staging/release"; then
  install
  say ""
  say "the fixpoint holds: both steps emitted the same C."
  say "torb: build/release/$(basename "$second")"
  say "$refresh_hint"
  exit 0
fi

say ""
say "step 1 and step 2 emitted different C: the seed is older than the code generation it built."
say "step 3: the compiler of step 2 builds the compiler once more"
"$second" build ./compiler --output "./$staging/fixpoint/torb"
third=$(binary_of "$staging/fixpoint/torb")
[ -n "$third" ] || fail "step 3 wrote no binary"

if same_c "$staging/release" "$staging/fixpoint"; then
  install
  say ""
  say "the fixpoint holds: steps 2 and 3 emitted the same C. The seed is older than the code generation."
  say "torb: build/release/$(basename "$second")"
  say "$refresh_hint"
else
  say ""
  say "steps 2 and 3 emitted DIFFERENT C, and build/release/ was left as it was:"
  say "  $staging/release/program.c   (built by the seed's output)"
  say "  $staging/fixpoint/program.c  (built by the compiler's own output)"
  say "The compiler is not a fixed point of itself. \`cmp\` says where they part."
  exit 1
fi
