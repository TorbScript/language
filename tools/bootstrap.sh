#!/bin/sh
# Builds `torb` from the seed, and then builds it again with itself.
#
# This is how a checkout gets a compiler without Rust in it. The seed is a `torb` that already exists - a binary under
# `seed/`, or `seed/program.c`, the compiler's own generated C, which builds anywhere a C compiler does. Neither is in
# git: `seed/` is ignored, and what goes in it is produced by an earlier `torb` (`sh tools/refresh-seed.sh`).
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
# POSIX sh. It runs in Git Bash on Windows, where the binaries are called `torb.exe`.
#
#   sh tools/bootstrap.sh              # seed -> torb -> torb, into build/release/torb
#   TORB_SEED=/path/to/torb sh tools/bootstrap.sh
#   TORB_CC=clang sh tools/bootstrap.sh

set -eu

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

# The same order `torb build` uses, so the seed and everything it builds are compiled by one compiler.
find_compiler() {
  if [ -n "${TORB_CC-}" ]; then
    printf '%s\n' "$TORB_CC"
    return 0
  fi
  for candidate in clang gcc cc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
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
  # The portable seed: the compiler as one C file. No `-Werror` here - the C was emitted by another version of the
  # compiler and may be read by another version of the C compiler, and a warning is not this script's business. It is
  # compiled inside a machine-wide build slot, like every C file of this size.
  compiler=$(find_compiler) || fail "no C compiler found (tried \$TORB_CC, clang, gcc, cc)"
  say "building the seed from seed/program.c with $compiler - this takes a few minutes"
  # shellcheck disable=SC2086
  if ! sh tools/build-slot.sh seed/c-compiler.log "$compiler" -std=c11 -O2 -g0 -I runtime/include -o seed/torb \
    seed/program.c runtime/*.c -lm; then
    cat seed/c-compiler.log >&2
    fail "the C compiler could not build seed/program.c"
  fi
  seed=$(binary_of "seed/torb")
  [ -n "$seed" ] || fail "the C compiler wrote no binary"
fi

if [ -z "$seed" ]; then
  say "bootstrap.sh: there is no seed."
  say ""
  say "  A seed is a \`torb\` that already exists, and it is not in git. Put one of the two in place:"
  say "    seed/torb          a binary for this platform"
  say "    seed/program.c     the compiler's own generated C, which builds anywhere a C compiler does"
  say ""
  say "  A checkout that has built build/release/torb writes both: sh tools/refresh-seed.sh"
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
"$seed" build ./compiler --output "./$staging/bootstrap/torb"
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
  rm -f "build/release/$name.old-$$" 2>/dev/null || true
  succeeded=1
}

# ----------------------------------------------------------------------------- the fixed point --------------------
#
# Step 1 was emitted by the seed, step 2 by the compiler itself. They agree unless the change being built alters what
# the compiler emits - then the seed writes the old C and the fresh compiler the new, and the honest comparison is a
# third step: the compiler of step 2 building the compiler once more. Only two builds of the SAME compiler have to agree.

refresh_hint="A merge whose tier A is green refreshes the seed from it: sh tools/refresh-seed.sh"

if cmp -s "$staging/bootstrap/program.c" "$staging/release/program.c"; then
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

if cmp -s "$staging/release/program.c" "$staging/fixpoint/program.c"; then
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
