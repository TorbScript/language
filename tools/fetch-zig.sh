#!/bin/sh
# Downloads the pinned Zig, verifies it, unpacks it into `build/zig/`, and prints the path of its `zig` - the C compiler
# `tools/cross.sh` builds the release binaries of other machines with (`zig cc`, docs/design/RELEASE.md section 4:
# "zig cc, managed"). The toolchain `torb toolchain add c` is to fetch one day is this one; until then this script is
# the pipeline's.
#
#   sh tools/fetch-zig.sh          # prints the path of zig (zig.exe on Windows), fetching it first when needed
#
# One version, and the SHA-256 of its archive for each machine this runs on, both written below. The hashes are the
# ones https://ziglang.org/download/index.json lists, and each archive's minisign signature was checked against the
# Zig Software Foundation's key when it was pinned (RWSGOq2NVecA2UPNdBUZykf1CCb147pkmdtYxgb3Ti+JO/wCYvhbAb/U). A new
# version is a new pin: the version, the four hashes, and the same check of the signatures.
#
# The archive is downloaded from the community mirrors of https://ziglang.org/download/community-mirrors.txt in a random
# order, and from ziglang.org itself only when none of them has it: ziglang.org asks automated downloads to use the
# mirrors, and says it is one machine with no promise of uptime. A mirror is not trusted - every archive is checked
# against the pinned hash before it is kept, which is stronger than its signature: it names these very bytes.
#
# Variables:
#   TORB_ZIG_HOME     where the archive is kept and unpacked (default: build/zig of this checkout)
#   TORB_ZIG_MIRROR   one base URL to download from instead of the mirrors and ziglang.org - an https:// or file://
#                     URL of a directory that holds the archive
#
# An archive already in TORB_ZIG_HOME with the pinned hash is not downloaded again (CI keeps it in a cache), and an
# unpacked Zig already there is used as it is. Needs curl or wget, sha256sum, shasum or openssl, and tar with xz
# (unzip on Windows).
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

version=0.16.0

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "fetch-zig.sh: $*"
  exit 1
}

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d ' ' -f 1
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$1" | sed 's/.*= *//'
  else
    fail "no SHA-256 tool (sha256sum, shasum or openssl)"
  fi
}

# One URL into one file; 1 when it could not be downloaded
fetch() {
  case "$1" in
    file://*)
      cp "${1#file://}" "$2" 2>/dev/null
      return
      ;;
  esac
  # A mirror that stalls is left after a minute below 10 kB/s, for the next one
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 2 --connect-timeout 20 --speed-limit 10000 --speed-time 60 -o "$2" "$1"
  elif command -v wget >/dev/null 2>&1; then
    wget -q --timeout=60 --tries=2 -O "$2" "$1"
  else
    fail "neither curl nor wget is there to download with"
  fi
}

case "$(uname -s)" in
  Linux) system=linux ;;
  Darwin) system=macos ;;
  MINGW* | MSYS* | CYGWIN*) system=windows ;;
  *) fail "no Zig is pinned for $(uname -s)" ;;
esac
case "$(uname -m)" in
  x86_64 | amd64) architecture=x86_64 ;;
  arm64 | aarch64) architecture=aarch64 ;;
  *) fail "no Zig is pinned for $(uname -m)" ;;
esac
host=$architecture-$system

case "$host" in
  x86_64-linux) sum=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00 ;;
  aarch64-linux) sum=ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17 ;;
  aarch64-macos) sum=b23d70deaa879b5c2d486ed3316f7eaa53e84acf6fc9cc747de152450d401489 ;;
  x86_64-windows) sum=68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e ;;
  *) fail "no Zig is pinned for $host" ;;
esac

name=zig-$host-$version
if [ "$system" = "windows" ]; then
  archive=$name.zip
  executable=zig.exe
else
  archive=$name.tar.xz
  executable=zig
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
home=${TORB_ZIG_HOME:-$root/build/zig}
mkdir -p "$home"
home=$(CDPATH= cd -- "$home" && pwd)

if [ -x "$home/$name/$executable" ] && [ -f "$home/$name/.verified" ]; then
  printf '%s\n' "$home/$name/$executable"
  exit 0
fi

if [ -f "$home/$archive" ] && [ "$(sha256_of "$home/$archive")" != "$sum" ]; then
  say "fetch-zig.sh: $home/$archive does not have the pinned hash - downloading it again"
  rm -f "$home/$archive"
fi

if [ ! -f "$home/$archive" ]; then
  if [ -n "${TORB_ZIG_MIRROR-}" ]; then
    sources=${TORB_ZIG_MIRROR%/}
  else
    sources=""
    if fetch "https://ziglang.org/download/community-mirrors.txt" "$home/mirrors.txt"; then
      # A random order, so that no one mirror carries every download
      sources=$(awk 'BEGIN { srand() } /^https:\/\// { printf "%.8f %s\n", rand(), $0 }' "$home/mirrors.txt" |
        sort | cut -d ' ' -f 2)
    else
      say "fetch-zig.sh: the list of mirrors could not be read - asking ziglang.org alone"
    fi
    sources="$sources https://ziglang.org/download/$version"
  fi
  for source in $sources; do
    source=${source%/}
    case "$source" in
      https://ziglang.org/*) url="$source/$archive" ;;
      https://*) url="$source/$archive?source=torbscript" ;;
      *) url="$source/$archive" ;;
    esac
    say "downloading $archive from $source"
    if ! fetch "$url" "$home/$archive.part"; then
      say "  could not be downloaded"
      continue
    fi
    actual=$(sha256_of "$home/$archive.part")
    if [ "$actual" != "$sum" ]; then
      say "  sha256 $actual is not the pinned $sum - not taken"
      continue
    fi
    mv "$home/$archive.part" "$home/$archive"
    break
  done
  rm -f "$home/$archive.part" "$home/mirrors.txt"
  [ -f "$home/$archive" ] || fail "no source had $archive with sha256 $sum"
fi

rm -rf "${home:?}/$name"
if [ "$system" = "windows" ]; then
  (cd "$home" && unzip -q "$archive") || fail "could not unpack $archive"
else
  (cd "$home" && tar -xJf "$archive") || fail "could not unpack $archive (tar needs xz)"
fi
[ -x "$home/$name/$executable" ] || fail "$archive holds no $name/$executable"
: >"$home/$name/.verified"
say "zig $("$home/$name/$executable" version) in $home/$name"
printf '%s\n' "$home/$name/$executable"
