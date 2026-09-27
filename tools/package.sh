#!/bin/sh
# Lays out and packs the toolchain a release publishes per target (docs/design/RELEASE.md section 4 and section 13).
#
#   sh tools/package.sh layout <binary> <directory>
#   sh tools/package.sh archive <version> <target> <binary> <output directory>
#
# `layout` writes the toolchain into `<directory>`:
#
#   bin/torb(.exe)          the binary
#   std/                    every std package, as source
#   runtime/                include/, *.c, os/*.c and README.md - not its tests and not its test runner
#   tools/build-slot.sh     the machine-wide build slots `torb build` looks for beside the runtime
#   tools/build-units.sh    the parallel compile of the units and the object cache (docs/BACKEND.md 4.1)
#   LICENSE*, NOTICE*       when the repository has them
#   README.md               three lines
#
# which is the layout `torb` finds its `std/` and `runtime/` in: it walks up from its own directory
# (`compiler/src/project/toolchain.trb`). Every file comes from git at HEAD, never from the working tree, so nothing
# untracked - a `build/` directory, a scratch file - can end up in a download.
#
# `archive` lays out `torb-<version>-<target>/` and packs it as `torb-<version>-<target>.tar.gz`, and for a Windows
# target as `.zip` as well. Both are reproducible: sorted entries, fixed owner and modes, every timestamp the time of the
# commit, gzip without a name or a time.
#
# POSIX sh plus GNU tar (`tar` or `gtar`), and `zip` for a Windows target. CI runs it on Linux.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "package.sh: $*"
  exit 1
}

absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
}

usage() {
  say "usage: sh tools/package.sh layout <binary> <directory>"
  say "       sh tools/package.sh archive <version> <target> <binary> <output directory>"
  exit 2
}

command=${1-}
[ -n "$command" ] || usage
shift

# Arguments are paths relative to where the script was started; they are made absolute before the `cd`
case "$command" in
  layout)
    [ $# -eq 2 ] || usage
    binary=$(absolute "$1")
    directory=$(absolute "$2")
    ;;
  archive)
    [ $# -eq 4 ] || usage
    version=$1
    target=$2
    binary=$(absolute "$3")
    output=$(absolute "$4")
    ;;
  *) usage ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

tar=""
for candidate in tar gtar; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" --version 2>/dev/null | grep -q 'GNU tar'; then
    tar=$candidate
    break
  fi
done

# The toolchain of HEAD in `$1`, with the binary as bin/torb or bin/torb.exe
lay_out() {
  into=$1
  [ -f "$binary" ] || fail "there is no binary at $binary"
  mkdir -p "$into/bin"
  extras=$(git ls-files -- 'LICENSE*' 'NOTICE*')
  # shellcheck disable=SC2086
  git archive --format=tar HEAD -- std runtime tools/build-slot.sh tools/build-units.sh $extras | (cd "$into" && tar -xf -)
  rm -rf "$into/runtime/tests" "$into/runtime/build.sh"
  case "$binary" in
    *.exe) cp "$binary" "$into/bin/torb.exe" ;;
    *) cp "$binary" "$into/bin/torb" ;;
  esac
  chmod 755 "$into/bin/"torb*
  cat >"$into/README.md" <<'EOF'
# TorbScript

`bin/torb` is the toolchain; `std/` and `runtime/` beside it are what it compiles every program with.
`torb build` needs a C compiler on the machine (`$TORB_CC`, clang, gcc, cc, or cl).
The language, the guide and the reference: https://torb.dev
EOF
}

if [ "$command" = "layout" ]; then
  [ ! -e "$directory" ] || fail "$directory exists already"
  lay_out "$directory"
  say "laid out the toolchain in $directory"
  exit 0
fi

# ----------------------------------------------------------------------------- archive ------------------------------

[ -n "$tar" ] || fail "GNU tar is needed for a reproducible archive (tar or gtar)"
case "$version" in
  *[!A-Za-z0-9.-]* | "") fail "a version is letters, digits, dots and hyphens: $version" ;;
esac
name="torb-$version-$target"
epoch=$(git show -s --format=%ct HEAD)
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
lay_out "$staging/$name"

mkdir -p "$output"
"$tar" --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner --mode='a=rX,u+w' --format=gnu \
  -C "$staging" -cf - "$name" | gzip -9 -n >"$output/$name.tar.gz"
say "wrote $output/$name.tar.gz"

case "$target" in
  windows-*)
    command -v zip >/dev/null 2>&1 || fail "zip is needed for the archive of a Windows target"
    # zip stores the local time of every file: the commit's time in UTC, the entries sorted, no extra attributes
    find "$staging/$name" -exec touch -d "@$epoch" {} +
    rm -f "$output/$name.zip"
    (cd "$staging" && find "$name" | LC_ALL=C sort | TZ=UTC zip -q -X -9 "$output/$name.zip" -@)
    say "wrote $output/$name.zip"
    ;;
esac
