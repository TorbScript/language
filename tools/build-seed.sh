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
# `$TORB_OBJECTS` adds object files to the link: the release links the icon and the version of the Windows `torb.exe`
# this way (tools/windows/resource.sh; a `.res` where `zig cc` links, tools/cross.sh), and nothing else passes any.
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

# The worker pool is pthreads everywhere but Windows, where it is the Win32 API that every C compiler links anyway. The
# machine the binary is for decides, and the compiler knows it (`-dumpmachine`: x86_64-w64-mingw32, or
# x86_64-unknown-windows-gnu from the `zig cc` of tools/cross.sh) - a cross compiler's machine is not this one. Without
# an answer it is this machine.
threads="-pthread"
machine=$("$compiler" -dumpmachine 2>/dev/null || true)
case "$machine" in
  *mingw* | *windows* | *cygwin*) threads="" ;;
  "")
    case "$(uname -s 2>/dev/null)" in
      MINGW* | MSYS* | CYGWIN* | Windows*) threads="" ;;
    esac
    if [ "${OS-}" = "Windows_NT" ]; then
      threads=""
    fi
    ;;
esac

# Every `.c` of the runtime and of its `os/` directory, as `torb build` compiles them. A runtime that predates `os/`
# has none there, and the unexpanded pattern is not passed on.
sources=""
for file in "$runtime"/*.c "$runtime"/os/*.c; do
  if [ -f "$file" ]; then
    sources="$sources $file"
  fi
done

# What `torb build ./compiler` adds for the compiler, which hosts the VM, so a `torb` built here - the release binaries
# of CI are - runs programs the way a bootstrapped one does: `TORB_HOSTS_MACHINE`, so that `TORB_MEMORY_LIMIT` limits
# the program `torb run` interprets and not `torb` itself, and the TLS part of the runtime with the mbedTLS of
# `vendor/` (docs/design/NETWORK.md section 5), without which `torb run` of a program that speaks TLS panics. A runtime
# that has no TLS part yet gets the macro alone.
hosting="-DTORB_HOSTS_MACHINE"
if [ -f "$runtime/tls/torb_mbedtls_user.h" ] && [ -d "$runtime/vendor/mbedtls/library" ]; then
  hosting="$hosting -DTORB_WITH_TLS -DMBEDTLS_USER_CONFIG_FILE=<torb_mbedtls_user.h> -I $runtime/tls"
  hosting="$hosting -I $runtime/vendor/mbedtls/include -I $runtime/vendor/mbedtls/library"
  for file in "$runtime"/tls/*.c "$runtime"/vendor/mbedtls/library/*.c; do
    if [ -f "$file" ]; then
      sources="$sources $file"
    fi
  done
fi

mkdir -p "$(dirname "$output")"
log="$(dirname "$output")/c-compiler.log"

say "compiling $seed/program.c with $compiler - this takes a few minutes"
slot="$here/build-slot.sh"
# shellcheck disable=SC2086
if [ -f "$slot" ]; then
  if ! sh "$slot" "$log" "$compiler" -std=c11 -O2 -g0 ${TORB_CFLAGS-} $hosting -I "$runtime/include" -o "$output" \
    "$seed/program.c" $sources ${TORB_OBJECTS-} $threads -lm; then
    cat "$log" >&2
    fail "the C compiler could not build $seed/program.c"
  fi
else
  if ! "$compiler" -std=c11 -O2 -g0 ${TORB_CFLAGS-} $hosting -I "$runtime/include" -o "$output" \
    "$seed/program.c" $sources ${TORB_OBJECTS-} $threads -lm >"$log" 2>&1; then
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
