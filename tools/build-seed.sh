#!/bin/sh
# Compiles a seed - the compiler as one C file, `program.c`, and the runtime it was emitted against - into a `torb`.
#
# A seed is what a checkout without a `torb` starts from (`tools/bootstrap.sh`). Its C builds with any C11 compiler
# that takes gcc's options - gcc, clang, musl-gcc, `zig cc` - and needs nothing else: no `make`, no `torb`, no network.
# This script is the one place that knows the command line, so `tools/bootstrap.sh`, `tools/fetch-seed.sh`, the release
# binaries of CI and the `build.sh` inside a published seed archive (`tools/pack-seed.sh` copies this file there) all
# compile the same way.
#
#   sh tools/build-seed.sh <seed directory> [output]
#   TORB_CC=musl-gcc TORB_CFLAGS=-static sh tools/build-seed.sh build/release build/torb-static
#
# `<seed directory>` holds `program.c`. The runtime is `<seed directory>/runtime` when the seed brings its own (a
# fetched or unpacked seed does), and otherwise the `runtime/` beside this script's directory - the checkout's, for
# `seed/` and `build/release/`, whose C was emitted against it. Without an argument the seed directory is the one this
# script is in, which is how `sh build.sh` inside an unpacked seed archive works. The output is `<seed directory>/torb`
# unless it is named; a C compiler on Windows appends `.exe`.
#
# `$TORB_CC` picks the compiler (default: clang, gcc, cc - the order `torb build` uses), `$TORB_CFLAGS` adds flags. The
# compile runs inside one of the machine-wide build slots of `tools/build-slot.sh` where that script is found beside
# this one, because the C is about 100 MB and one `cc1` over it takes 1.5 to 2 GB.
#
# No `-Werror`: the C was emitted by one version of the compiler and may be read by another version of the C compiler,
# and a warning is not this script's business.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "build-seed.sh: $*"
  exit 1
}

absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
}

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ $# -ge 1 ]; then
  seed=$(absolute "$1")
else
  seed=$here
fi
output=$(absolute "${2:-$seed/torb}")

[ -f "$seed/program.c" ] || fail "there is no program.c in $seed"

if [ -f "$seed/runtime/include/torb.h" ]; then
  runtime=$seed/runtime
elif [ -f "$here/../runtime/include/torb.h" ]; then
  runtime=$(CDPATH= cd -- "$here/../runtime" && pwd)
else
  fail "no runtime: neither $seed/runtime nor $here/../runtime has include/torb.h"
fi

if [ -n "${TORB_CC-}" ]; then
  compiler=$TORB_CC
else
  compiler=""
  for candidate in clang gcc cc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      compiler=$candidate
      break
    fi
  done
  [ -n "$compiler" ] || fail "no C compiler found (tried \$TORB_CC, clang, gcc, cc)"
fi

# The worker pool is pthreads everywhere but Windows, where it is the Win32 API that every C compiler links anyway
threads="-pthread"
case "$(uname -s 2>/dev/null)" in
  MINGW* | MSYS* | CYGWIN* | Windows*) threads="" ;;
esac
if [ "${OS-}" = "Windows_NT" ]; then
  threads=""
fi

# Every `.c` of the runtime and of its `os/` directory, as `torb build` compiles them. A runtime that predates `os/`
# has none there, and the unexpanded pattern is not passed on.
sources=""
for file in "$runtime"/*.c "$runtime"/os/*.c; do
  if [ -f "$file" ]; then
    sources="$sources $file"
  fi
done

mkdir -p "$(dirname "$output")"
log="$(dirname "$output")/c-compiler.log"

say "compiling $seed/program.c with $compiler - this takes a few minutes"
slot="$here/build-slot.sh"
# shellcheck disable=SC2086
if [ -f "$slot" ]; then
  if ! sh "$slot" "$log" "$compiler" -std=c11 -O2 -g0 ${TORB_CFLAGS-} -I "$runtime/include" -o "$output" \
    "$seed/program.c" $sources $threads -lm; then
    cat "$log" >&2
    fail "the C compiler could not build $seed/program.c"
  fi
else
  if ! "$compiler" -std=c11 -O2 -g0 ${TORB_CFLAGS-} -I "$runtime/include" -o "$output" \
    "$seed/program.c" $sources $threads -lm >"$log" 2>&1; then
    cat "$log" >&2
    fail "the C compiler could not build $seed/program.c"
  fi
fi

if [ -f "$output" ]; then
  say "wrote $output"
elif [ -f "$output.exe" ]; then
  say "wrote $output.exe"
else
  fail "the C compiler wrote no binary"
fi
