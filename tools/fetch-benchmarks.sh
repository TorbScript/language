#!/bin/sh
# The newest report of the website's benchmarks page: `benchmarks.json` of the release `benchmarks` of the forge,
# which .forgejo/workflows/benchmarks.yml replaces after every nightly, fetched to where `torb docs site` reads it
# (docs/tooling/torb-docs-site.md, "The benchmarks page"). Dockerfile.site runs it before it builds the site, for a
# release, a nightly and the site of main alike.
#
#   sh tools/fetch-benchmarks.sh             into build/benchmarks.json
#   sh tools/fetch-benchmarks.sh <file>      into <file>
#
# The release is public, so no token is needed. **Fetching nothing is not a failure**: the page then says that nothing
# is measured yet. So the script says why and exits 0 when the forge cannot be reached, has no report yet or answers
# with something that is not one; it fails only when it cannot write the file. A file that was there before is left as
# it was unless a new report replaces it.
#
# Variables: TORB_FORGE_URL (https://git.torb.dev) and TORB_FORGE_REPOSITORY (torbscript/language), as for
# tools/forge.sh. Needs curl or wget.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

target=${1:-build/benchmarks.json}
forge=${TORB_FORGE_URL:-https://git.torb.dev}
repository=${TORB_FORGE_REPOSITORY:-torbscript/language}
address="${forge%/}/$repository/releases/download/benchmarks/benchmarks.json"

say() {
  printf '%s\n' "$*" >&2
}

directory=$(dirname -- "$target")
mkdir -p "$directory" || { say "fetch-benchmarks.sh: cannot create $directory"; exit 1; }
partial="$target.partial"
trap 'rm -f "$partial"' EXIT

if command -v curl >/dev/null 2>&1; then
  fetched() { curl -fsSL --retry 3 --retry-delay 2 --max-time 60 -o "$partial" "$address"; }
elif command -v wget >/dev/null 2>&1; then
  fetched() { wget -q -T 60 -t 3 -O "$partial" "$address"; }
else
  say "fetch-benchmarks.sh: neither curl nor wget is here, so the benchmarks page says nothing is measured yet"
  exit 0
fi

if ! fetched; then
  say "fetch-benchmarks.sh: no report at $address, so the benchmarks page says nothing is measured yet"
  exit 0
fi
# A report of benchmarks/game.sh starts with its schema; a page of the forge's that answered instead does not
if ! grep -q '"schema": 1' "$partial"; then
  say "fetch-benchmarks.sh: $address is not a report of benchmarks/game.sh, so it is left out"
  exit 0
fi
mv -f "$partial" "$target" || { say "fetch-benchmarks.sh: cannot write $target"; exit 1; }
measured=$(sed -n 's/.*"measured": "\([^"]*\)".*/\1/p' "$target" | head -n 1)
say "fetch-benchmarks.sh: the report measured ${measured:-at an unknown time}, in $target"
