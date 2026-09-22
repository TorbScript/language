#!/bin/sh
# Builds `torb` from the seed, and then builds it again with itself.
#
# This is how a checkout gets a compiler without Rust in it. The seed is a `torb` that already exists - a binary under
# `seed/`, or `seed/program.c`, the compiler's own generated C, which builds anywhere a C compiler does. Neither is in
# git: `seed/` is ignored, and what goes in it is downloaded or produced by the previous commit's `torb`.
#
# Two steps and not one, because one says nothing. The seed compiles the *current* sources, so the binary that comes
# out is built by an older compiler; that binary compiles the sources again, and if the two `program.c` are identical
# the compiler is a fixed point of itself. That is the same comparison the fixpoint gate makes.
#
# POSIX sh. It runs in Git Bash on Windows, where the binaries are called `torb.exe`.
#
#   sh tools/bootstrap.sh              # seed -> build/bootstrap/torb -> build/release/torb
#   TORB_SEED=/path/to/torb sh tools/bootstrap.sh
#   TORB_CC=clang sh tools/bootstrap.sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "bootstrap.sh: $*"
  exit 1
}

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
  # compiler and may be read by another version of the C compiler, and a warning is not this script's business.
  compiler=$(find_compiler) || fail "no C compiler found (tried \$TORB_CC, clang, gcc, cc)"
  say "building the seed from seed/program.c with $compiler - this takes a few minutes"
  # shellcheck disable=SC2086
  "$compiler" -std=c11 -O2 -g0 -I runtime/include -o seed/torb seed/program.c runtime/*.c -lm
  seed=$(binary_of "seed/torb")
  [ -n "$seed" ] || fail "the C compiler wrote no binary"
fi

if [ -z "$seed" ]; then
  say "bootstrap.sh: there is no seed."
  say ""
  say "  A seed is a \`torb\` that already exists, and it is not in git. Put one of the two in place:"
  say "    seed/torb          a binary for this platform, from a release or from \`torb build compiler\`"
  say "    seed/program.c     the compiler's own generated C, which builds anywhere a C compiler does"
  say ""
  say "  A \`torb\` that is already on this machine writes both:"
  say "    torb build ./compiler --output ./seed/torb"
  say "    torb build ./compiler --emit-c --output ./seed/torb"
  exit 1
fi

say "seed: $seed"

# ----------------------------------------------------------------------------- the two steps ----------------------

say "step 1: the seed builds the compiler"
"$seed" build ./compiler --output ./build/bootstrap/torb
first=$(binary_of "build/bootstrap/torb")
[ -n "$first" ] || fail "step 1 wrote no binary"

say "step 2: that compiler builds the compiler again"
"$first" build ./compiler --output ./build/release/torb
second=$(binary_of "build/release/torb")
[ -n "$second" ] || fail "step 2 wrote no binary"

# ----------------------------------------------------------------------------- the fixed point --------------------
#
# Step 1 was emitted by the seed, step 2 by the compiler itself. They agree unless the change being built alters what
# the compiler emits - then the seed writes the old C and the fresh compiler the new, and the honest comparison is a
# third step: the compiler of step 2 building the compiler once more. Only two builds of the SAME compiler have to agree.

if cmp -s build/bootstrap/program.c build/release/program.c; then
  say ""
  say "the fixpoint holds: both steps emitted the same C."
  say "torb: $second"
  exit 0
fi

say ""
say "step 1 and step 2 emitted different C: the seed is older than the code generation it built."
say "step 3: the compiler of step 2 builds the compiler once more"
"$second" build ./compiler --output ./build/fixpoint/torb
third=$(binary_of "build/fixpoint/torb")
[ -n "$third" ] || fail "step 3 wrote no binary"

if cmp -s build/release/program.c build/fixpoint/program.c; then
  say ""
  say "the fixpoint holds: steps 2 and 3 emitted the same C. Refresh the seed from build/release."
  say "torb: $second"
else
  say ""
  say "steps 2 and 3 emitted DIFFERENT C:"
  say "  build/release/program.c   (built by the seed's output)"
  say "  build/fixpoint/program.c  (built by the compiler's own output)"
  say "The compiler is not a fixed point of itself. `cmp` says where they part."
  exit 1
fi
