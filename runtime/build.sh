#!/bin/sh
# Compiles the runtime and its tests and runs them. Works in Git Bash on Windows and on Linux and macOS; make is not
# needed. $CC overrides the compiler, $CFLAGS adds to the flags.
set -e
here=$(dirname "$0")
cd "$here"
cc=${CC:-cc}
command -v "$cc" >/dev/null 2>&1 || cc=gcc
command -v "$cc" >/dev/null 2>&1 || cc=clang
out=build
mkdir -p "$out"
warnings="-Wall -Wextra -Wpedantic -Werror"
sources="memory.c panic.c text.c list.c map.c number.c console.c process.c platform.c file.c clock.c environment.c"
tests="tests/harness.c tests/memory_test.c tests/number_test.c tests/text_test.c tests/list_test.c tests/map_test.c tests/file_test.c tests/clock_test.c tests/environment_test.c tests/process_test.c"
echo "compiling the runtime with $cc"
# shellcheck disable=SC2086
"$cc" -std=c11 $warnings $CFLAGS -Iinclude -c $sources
mv ./*.o "$out"/
echo "compiling and linking the tests"
# shellcheck disable=SC2086
"$cc" -std=c11 $warnings $CFLAGS -Iinclude -Itests -o "$out"/runtime-tests $tests "$out"/*.o -lm
echo "running the tests"
"$out"/runtime-tests
# The natives manifest and the header it generates must agree, which `torb natives --header` writes and
# compiler/tests/natives.test.trb checks. Here the C compiler enforces the signatures.
echo "checking torb_natives.h against the headers"
# shellcheck disable=SC2086
"$cc" -std=c11 $warnings $CFLAGS -Iinclude -c tests/natives_header_test.c -o "$out"/natives_header_test.o
echo "ok"
