#!/bin/sh
# Fills tools/homebrew/torb.rb.template and tools/scoop/torb.json.template from a release's SHA256SUMS, for the
# release.yml job `publish-packages` (docs/design/RELEASE.md section 5, "winget, Scoop and Homebrew"):
#
#   sh tools/render-package-manifests.sh <version> <repository> <dist directory> <output directory>
#
# <dist directory> is where the release's archives and SHA256SUMS were packed (tools/package.sh); this only reads
# hashes out of the sums, never recomputes one. Writes <output directory>/Formula/torb.rb and
# <output directory>/bucket/torb.json. winget has no template here: `wingetcreate update` renders its own manifest
# from the package's existing one on winget-pkgs.
#
# POSIX sh.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "render-package-manifests.sh: $*"
  exit 1
}

[ $# -eq 4 ] || fail "usage: sh tools/render-package-manifests.sh <version> <repository> <dist directory> <output directory>"
version=$1
repository=$2
dist=$3
output=$4

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sums="$dist/SHA256SUMS"
[ -f "$sums" ] || fail "no SHA256SUMS in $dist"

hash_of() {
  # hash_of <archive file name>
  awk -v f="$1" '{ name = $2; sub(/^\*/, "", name); if (name == f) { print $1; exit } }' "$sums"
}

render() {
  # render <template> <output file>
  template=$1
  out=$2
  mkdir -p "$(dirname -- "$out")"
  sed \
    -e "s|{{VERSION}}|$version|g" \
    -e "s|{{REPOSITORY}}|$repository|g" \
    -e "s|{{SHA256_MACOS_ARM64}}|$(hash_of "torb-$version-macos-arm64.tar.gz")|g" \
    -e "s|{{SHA256_LINUX_X64}}|$(hash_of "torb-$version-linux-x64.tar.gz")|g" \
    -e "s|{{SHA256_LINUX_ARM64}}|$(hash_of "torb-$version-linux-arm64.tar.gz")|g" \
    -e "s|{{SHA256_WINDOWS_X64}}|$(hash_of "torb-$version-windows-x64.zip")|g" \
    "$template" >"$out"
  say "wrote $out"
}

render "$root/tools/homebrew/torb.rb.template" "$output/Formula/torb.rb"
render "$root/tools/scoop/torb.json.template" "$output/bucket/torb.json"
