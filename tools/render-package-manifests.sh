#!/bin/sh
# Fills tools/homebrew/torb.rb.template and tools/scoop/torb.json.template from a release's SHA256SUMS, for the
# release.yml job `publish-packages` (docs/design/RELEASE.md section 5, "winget, Scoop and Homebrew"):
#
#   sh tools/render-package-manifests.sh <version> <dist directory> <output directory>
#
# <dist directory> is where the release's archives and SHA256SUMS were packed (tools/package.sh); this only reads
# hashes out of the sums, never recomputes one. Writes <output directory>/Formula/torb.rb when the release has the
# three archives the formula names (macos-arm64, linux-x64, linux-arm64) and <output directory>/bucket/torb.json when
# it has the windows-x64 one - a release without them, from a forge that has no runner for those targets yet, renders
# nothing for that channel, and the job skips it. winget has no template here: `wingetcreate update` renders its own
# manifest from the package's existing one on winget-pkgs.
#
# Every URL is torb.dev's (`$TORB_DOWNLOAD_URL`, default https://torb.dev/download), where release-sync places each
# release's assets: the manifests name the project's own host, never a forge.
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

[ $# -eq 3 ] || fail "usage: sh tools/render-package-manifests.sh <version> <dist directory> <output directory>"
version=$1
dist=$2
output=$3
download=${TORB_DOWNLOAD_URL:-https://torb.dev/download}
download=${download%/}

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
    -e "s|{{DOWNLOAD}}|$download|g" \
    -e "s|{{SHA256_MACOS_ARM64}}|$(hash_of "torb-$version-macos-arm64.tar.gz")|g" \
    -e "s|{{SHA256_LINUX_X64}}|$(hash_of "torb-$version-linux-x64.tar.gz")|g" \
    -e "s|{{SHA256_LINUX_ARM64}}|$(hash_of "torb-$version-linux-arm64.tar.gz")|g" \
    -e "s|{{SHA256_WINDOWS_X64}}|$(hash_of "torb-$version-windows-x64.zip")|g" \
    "$template" >"$out"
  say "wrote $out"
}

# has <archive file name>...: whether SHA256SUMS lists every one
has() {
  for archive in "$@"; do
    [ -n "$(hash_of "$archive")" ] || return 1
  done
  return 0
}

if has "torb-$version-macos-arm64.tar.gz" "torb-$version-linux-x64.tar.gz" "torb-$version-linux-arm64.tar.gz"; then
  render "$root/tools/homebrew/torb.rb.template" "$output/Formula/torb.rb"
else
  say "no Formula/torb.rb: the release lacks the macos-arm64, linux-x64 or linux-arm64 archive"
fi
if has "torb-$version-windows-x64.zip"; then
  render "$root/tools/scoop/torb.json.template" "$output/bucket/torb.json"
else
  say "no bucket/torb.json: the release has no windows-x64 archive"
fi
