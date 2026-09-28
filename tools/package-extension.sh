#!/bin/sh
# Packs the VS Code extension of editors/vscode into a .vsix with @vscode/vsce, pinned and run through npx - what the
# job `extension` of .forgejo/workflows/gates.yml runs on every push, and what a contributor runs to install the
# working copy (editors/vscode/CONTRIBUTING.md). Nothing here publishes: release.yml does that with the file this
# writes.
#
#   sh tools/package-extension.sh                          build/extension/torbscript-<version>.vsix
#   sh tools/package-extension.sh <version> <directory>    <directory>/torbscript-<version>.vsix
#
# <version> is what a release or a nightly is called (gates.yml's input `version`), or empty:
#   0.4.0              a release: package.json has to say 0.4.0 too, and the .vsix is the one the stores get
#   nightly-20260928   a nightly: the version package.json says, marked as a pre-release (`vsce package
#                      --pre-release`) and named after the nightly - the stores never get it, it is an asset of the
#                      nightly's prerelease on the forge (docs/design/RELEASE.md section 13, "What a release contains")
#   empty              the version package.json says
#
# Before packing it checks what the stores and the release rely on: the `version` of package.json is the one of
# project.trb - one number for the toolchain and its extension (docs/design/RELEASE.md section 3) - and
# editors/vscode/LICENSE is the repository's LICENSE.
#
# Needs Node.js 22 or newer with npx, and the npm registry. POSIX sh; runs in Git Bash on Windows and on Linux/macOS.

set -eu

vsce_version=4.0.0

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "package-extension.sh: $*"
  exit 1
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

version=${1-}
output=${2:-build/extension}
extension=editors/vscode

command -v node >/dev/null 2>&1 || fail "Node.js is needed (22 or newer): https://nodejs.org"
command -v npx >/dev/null 2>&1 || fail "npx is needed; it comes with Node.js"
major=$(node -p 'process.versions.node.split(".")[0]')
[ "$major" -ge 22 ] || fail "@vscode/vsce $vsce_version needs Node.js 22 or newer, and this is $(node --version)"

declared=$(cd "$extension" && node -p 'require("./package.json").version')
toolchain=$(sed -n 's/^version *=\{0,1\} *"\([^"]*\)".*$/\1/p' project.trb | tr -d '\r' | head -n 1)
[ -n "$toolchain" ] || fail "project.trb names no version"
[ "$declared" = "$toolchain" ] ||
  fail "$extension/package.json says version \"$declared\" and project.trb \"$toolchain\": the extension carries the toolchain's number"
cmp -s LICENSE "$extension/LICENSE" || fail "$extension/LICENSE is not the repository's LICENSE: cp LICENSE $extension/LICENSE"

flags=""
case "$version" in
  "")
    name=$declared
    ;;
  nightly-*)
    name=$version
    flags="--pre-release"
    ;;
  *)
    printf '%s\n' "$version" | grep -q -E '^[0-9]+\.[0-9]+\.[0-9]+$' ||
      fail "a version is MAJOR.MINOR.PATCH or nightly-YYYYMMDD, not $version"
    [ "$version" = "$declared" ] || fail "the release is $version, and $extension/package.json says $declared"
    name=$version
    ;;
esac

mkdir -p "$output"
# node is handed a path it can read on Windows as well: C:/... rather than Git Bash's /c/...
directory=$(cd "$output" && (pwd -W 2>/dev/null || pwd))
file="$directory/torbscript-$name.vsix"
rm -f "$file"

say "packing $extension with @vscode/vsce $vsce_version ($(node --version))"
# shellcheck disable=SC2086 # $flags is empty or one word
(cd "$extension" && npx --yes "@vscode/vsce@$vsce_version" package --no-dependencies $flags --out "$file" </dev/null) ||
  fail "vsce could not pack the extension"
[ -f "$file" ] || fail "vsce wrote no $file"
say "wrote $output/torbscript-$name.vsix"
