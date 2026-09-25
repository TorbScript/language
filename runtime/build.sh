#!/bin/sh
# Compiles the runtime and its tests and runs them. Works in Git Bash on Windows and on Linux and macOS; make is not
# needed. The compiler is the one `torb build` picks, in the same order: $TORB_CC, then clang, gcc, cc. $CFLAGS adds to
# the flags. Everything it writes goes to `build/runtime/` of the repository, never into `runtime/` itself.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
if [ -n "${TORB_CC-}" ]; then
  cc=$TORB_CC
else
  cc=""
  for candidate in clang gcc cc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      cc=$candidate
      break
    fi
  done
  if [ -z "$cc" ]; then
    echo "runtime/build.sh: no C compiler found (tried \$TORB_CC, clang, gcc, cc)" >&2
    exit 1
  fi
fi
out="$here/../build/runtime"
mkdir -p "$out" "$out/os"
warnings="-Wall -Wextra -Wpedantic -Werror"
# The worker pool is pthreads everywhere but Windows, where it is the Win32 API that every C compiler links anyway
threads=""
case "$(uname -s 2>/dev/null)" in
  MINGW* | MSYS* | CYGWIN* | Windows*) ;;
  *) threads="-pthread" ;;
esac
sources="memory.c panic.c test.c text.c element.c list.c map.c number.c console.c process.c platform.c file.c clock.c environment.c sandbox.c task.c io.c os/windows.c os/linux.c os/macos.c os/freebsd.c os/posix.c os/bsd.c os/iocp.c os/posix_io.c os/epoll.c os/kqueue.c"
tests="tests/harness.c tests/memory_test.c tests/number_test.c tests/text_test.c tests/list_test.c tests/map_test.c tests/file_test.c tests/clock_test.c tests/environment_test.c tests/process_test.c tests/platform_test.c tests/console_test.c tests/task_test.c tests/pool_test.c tests/sandbox_test.c tests/io_test.c tests/tls_test.c"
# The TLS part (docs/design/NETWORK.md section 5): the glue of tls/ with the runtime's warnings, and the mbedTLS of
# vendor/ with its own - compiled once into build/runtime/mbedtls and kept there, because it is a library of somebody
# else's that only changes with its version, and it takes longer than all of the rest together
tls="-DTORB_WITH_TLS -DMBEDTLS_USER_CONFIG_FILE=<torb_mbedtls_user.h> -Itls -Ivendor/mbedtls/include"
vendor_out="$out/mbedtls"
mkdir -p "$out/tls" "$vendor_out"
version=$(grep -o 'MBEDTLS_VERSION_STRING  *"[^"]*"' vendor/mbedtls/include/mbedtls/build_info.h | sed 's/.*"\(.*\)"/\1/')
if [ "$(cat "$vendor_out/compiled-version" 2>/dev/null)" != "$version $cc" ]; then
  echo "compiling mbedTLS $version once, with $cc"
  rm -f "$vendor_out"/*.o "$vendor_out/compiled-version"
  for source in vendor/mbedtls/library/*.c; do
    name=$(basename "$source" .c)
    # shellcheck disable=SC2086
    "$cc" -std=c11 -O2 -w $threads ${CFLAGS-} $tls -Ivendor/mbedtls/library -c "$source" -o "$vendor_out/$name.o"
  done
  printf '%s\n' "$version $cc" > "$vendor_out/compiled-version"
fi
echo "compiling the runtime with $cc"
objects=""
for source in $sources tls/tls.c tls/platform_windows.c tls/platform_posix.c; do
  object="$out/${source%.c}.o"
  # shellcheck disable=SC2086
  "$cc" -std=c11 $warnings $threads ${CFLAGS-} $tls -Iinclude -c "$source" -o "$object"
  objects="$objects $object"
done
echo "compiling and linking the tests"
# shellcheck disable=SC2086
"$cc" -std=c11 $warnings $threads ${CFLAGS-} $tls -Iinclude -Itests -o "$out"/runtime-tests $tests $objects "$vendor_out"/*.o -lm
echo "running the tests"
"$out"/runtime-tests
# The natives manifest and the header it generates must agree, which `torb natives --header` writes and
# compiler/tests/natives.test.trb checks. Here the C compiler enforces the signatures.
echo "checking torb_natives.h against the headers"
# shellcheck disable=SC2086
"$cc" -std=c11 $warnings ${CFLAGS-} -Iinclude -c tests/natives_header_test.c -o "$out"/natives_header_test.o
echo "ok"
