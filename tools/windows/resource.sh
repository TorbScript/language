#!/bin/sh
# Compiles the icon and the version of the released torb.exe (tools/windows/torb.rc) into an object file, which the
# release links into the binary like any other object (docs/design/RELEASE.md section 4):
#
#   sh tools/windows/resource.sh <object>
#   TORB_OBJECTS=<object> sh tools/build-seed.sh build/release build/dist-binary/torb
#
# The icon is brand/icons/torb.ico; the file and product version are the `version` of project.trb, `0.4.0` as the
# numbers 0, 4, 0, 0 and as the text. Nothing else links the object: the bootstrap and its fixpoint compare the C, and
# a program `torb build` writes carries no TorbScript icon.
#
# The resource compiler is `$TORB_WINDRES`, or else the first of `windres` (GNU binutils: MSYS2's UCRT64 gcc, which the
# Windows runners and the maintainer's machine use, brings it), `x86_64-w64-mingw32-windres` (a MinGW cross toolchain
# on Linux) and `llvm-windres` (LLVM's). Without one it says so and exits with 3, and the caller ships torb.exe without
# its icon rather than not at all.
#
# An `<object>` whose name ends in `.res` is a resource file instead, compiled by `zig rc` (`$TORB_ZIG`, or `zig` on
# the PATH), which brings its own preprocessor and <winver.h>; the linker of `zig cc` takes it like an object. That is
# how the cross-compiled torb.exe gets its icon (tools/cross.sh), on a machine with no MinGW at all.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "resource.sh: $*"
  exit 1
}

[ $# -eq 1 ] || {
  say "usage: sh tools/windows/resource.sh <object>"
  exit 2
}

case "$1" in
  /* | [A-Za-z]:*) output=$1 ;;
  *) output=$(pwd)/$1 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

windres=""
zig=""
case "$output" in
  *.res)
    zig=${TORB_ZIG:-zig}
    if ! command -v "$zig" >/dev/null 2>&1; then
      say "resource.sh: no zig for a .res (\$TORB_ZIG or zig on the PATH)"
      exit 3
    fi
    ;;
  *)
    windres=${TORB_WINDRES-}
    if [ -z "$windres" ]; then
      for candidate in windres x86_64-w64-mingw32-windres llvm-windres; do
        if command -v "$candidate" >/dev/null 2>&1; then
          windres=$candidate
          break
        fi
      done
    fi
    if [ -z "$windres" ]; then
      say "resource.sh: no resource compiler (tried \$TORB_WINDRES, windres, x86_64-w64-mingw32-windres, llvm-windres)"
      exit 3
    fi
    ;;
esac

version=$(tr -d '\r' <project.trb | sed -n 's/^version *=\{0,1\} *"\([^"]*\)".*$/\1/p' | head -n 1)
printf '%s\n' "$version" | grep -q -E '^[0-9]+\.[0-9]+\.[0-9]+$' ||
  fail "project.trb says version \"$version\", which is not MAJOR.MINOR.PATCH"
major=${version%%.*}
rest=${version#*.}
minor=${rest%%.*}
patch=${rest#*.}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat >"$scratch/torb-version.h" <<EOF
#define TORB_VERSION_MAJOR $major
#define TORB_VERSION_MINOR $minor
#define TORB_VERSION_PATCH $patch
#define TORB_VERSION_TEXT "$version"
EOF

mkdir -p "$(dirname "$output")"
if [ -n "$zig" ]; then
  "$zig" rc -i "$scratch" -fo "$output" -- tools/windows/torb.rc || fail "$zig rc could not compile tools/windows/torb.rc"
else
  "$windres" --input-format=rc --output-format=coff -I "$scratch" -i tools/windows/torb.rc -o "$output" ||
    fail "$windres could not compile tools/windows/torb.rc"
fi
say "wrote $output: the icon of brand/icons/torb.ico and version $version"
