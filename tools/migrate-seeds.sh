#!/bin/sh
# Copies the seeds published on GitHub (the release `seeds` of TorbScript/language, the home of CI before 2026-09-27)
# to the release `seeds` on the forge (git.torb.dev), so `tools/fetch-seed.sh` finds every one of them there and the
# GitHub fallback is no longer needed. Run once by the owner, and again whenever it is unsure whether it finished:
#
#   GH_TOKEN=<GitHub token that reads the repository> TORB_FORGE_TOKEN=<forge token> sh tools/migrate-seeds.sh
#   GH_TOKEN=<...> sh tools/migrate-seeds.sh --dry-run      # what it would copy; writes nothing, needs no forge token
#
# For every seed GitHub's `seeds.txt` lists and the forge's does not, the archive and its `.sha256` are downloaded from
# GitHub, the archive checked against the hash the index records, and both uploaded to the forge's release. The
# forge's `seeds.txt` is then written once: the seeds it listed already (published there after the move, so newer)
# first, then the copied ones in GitHub's order - newest first throughout. A seed both list is left alone, so a second
# run copies nothing.
#
# Variables:
#   GH_TOKEN, GITHUB_TOKEN   read GitHub's release while the repository is private (`contents: read`)
#   TORB_FORGE_TOKEN         writes the forge's release (the permission "repository: read and write")
#   TORB_FORGE_URL, TORB_FORGE_REPOSITORY   the forge (tools/forge.sh; git.torb.dev and torbscript/language)
#   TORB_SEED_GITHUB_REPOSITORY             the GitHub repository (TorbScript/language)
#   TORB_SEED_SOURCE_FORGE, TORB_SEED_SOURCE_URL, TORB_SEED_SOURCE_TOKEN
#                            copy from another forge than GitHub (`forgejo`, its URL and a token that reads it) - the
#                            same script then copies the seeds between any two forges, a fork's or a test's
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "migrate-seeds.sh: $*"
  exit 1
}

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d ' ' -f 1
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  else
    openssl dgst -sha256 "$1" | sed 's/.*= *//'
  fi
}

dry=0
case "${1-}" in
  "") ;;
  --dry-run) dry=1 ;;
  *) fail "usage: sh tools/migrate-seeds.sh [--dry-run]" ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_forge=${TORB_SEED_SOURCE_FORGE:-github}
source_url=${TORB_SEED_SOURCE_URL:-https://github.com}
github_repository=${TORB_SEED_GITHUB_REPOSITORY:-TorbScript/language}
github_token=${TORB_SEED_SOURCE_TOKEN:-${GH_TOKEN:-${GITHUB_TOKEN-}}}

# The forge the seeds come from: GitHub, unless TORB_SEED_SOURCE_FORGE says otherwise
github() {
  TORB_FORGE=$source_forge TORB_FORGE_URL=$source_url TORB_FORGE_REPOSITORY="$github_repository" \
    TORB_FORGE_TOKEN="$github_token" sh "$root/tools/forge.sh" "$@" </dev/null
}
forge() {
  TORB_FORGE=forgejo sh "$root/tools/forge.sh" "$@" </dev/null
}

[ "$dry" -eq 1 ] || [ -n "${TORB_FORGE_TOKEN-}" ] || fail "TORB_FORGE_TOKEN is needed to write the forge's release (or --dry-run)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ----------------------------------------------------------------------------- the two indexes ---------------------

github download seeds seeds.txt "$work/github.txt" ||
  fail "the seeds.txt of $source_url/$github_repository could not be read (a private repository needs GH_TOKEN)"
grep -v '^[[:space:]]*\(#\|$\)' "$work/github.txt" | tr -d '\r' >"$work/github.entries" || true
[ -s "$work/github.entries" ] || fail "the seeds.txt of $source_url/$github_repository lists no seed"

forge_has_release=1
forge_status=0
forge release seeds >/dev/null 2>"$work/error" || forge_status=$?
if [ "$forge_status" -eq 1 ]; then
  cat "$work/error" >&2
  fail "the forge's release \`seeds\` could not be read"
fi
[ "$forge_status" -eq 0 ] || forge_has_release=0
: >"$work/forge.entries"
printf '%s\n' "# <commit> <sha256> <archive> - the newest first. Written by tools/publish-seed.sh." >"$work/forge.comments"
if [ "$forge_has_release" -eq 1 ] && forge assets seeds | grep -q -x 'seeds.txt'; then
  forge download seeds seeds.txt "$work/forge.txt" || fail "the forge's seeds.txt exists and could not be downloaded"
  tr -d '\r' <"$work/forge.txt" >"$work/forge.read"
  grep '^#' "$work/forge.read" >"$work/forge.comments" || true
  grep -v '^[[:space:]]*\(#\|$\)' "$work/forge.read" >"$work/forge.entries" || true
fi

# The seeds GitHub lists and the forge does not, in GitHub's order (newest first)
awk 'NR == FNR { listed[$1] = 1; next } !($1 in listed)' "$work/forge.entries" "$work/github.entries" >"$work/missing"
total=$(wc -l <"$work/github.entries" | tr -d ' ')
missing=$(wc -l <"$work/missing" | tr -d ' ')
say "$source_url/$github_repository lists $total seeds; the forge lacks $missing of them"
if [ "$missing" -eq 0 ]; then
  say "nothing to copy"
  exit 0
fi

if [ "$dry" -eq 1 ]; then
  while read -r commit hash archive; do
    say "would copy $archive (sha256 $hash)"
  done <"$work/missing"
  [ "$forge_has_release" -eq 1 ] || say "would create the release \`seeds\` on the forge"
  say "would write the forge's seeds.txt with $(($(wc -l <"$work/forge.entries") + missing)) seeds"
  exit 0
fi

# ----------------------------------------------------------------------------- copying -----------------------------

if [ "$forge_has_release" -eq 0 ]; then
  cat >"$work/notes.md" <<'EOF'
The portable seeds of the compiler: its own C and the runtime of the same commit, one archive per commit. seeds.txt
lists them, newest first. `sh tools/fetch-seed.sh` downloads, verifies and builds one; nothing else here is meant to
be downloaded by hand.
EOF
  forge create seeds main "Seeds" "$work/notes.md" --prerelease >/dev/null
  say "created the release \`seeds\` on the forge"
fi

present=$(forge assets seeds)
while read -r commit hash archive; do
  mkdir -p "$work/copy"
  github download seeds "$archive" "$work/copy/$archive"
  actual=$(sha256_of "$work/copy/$archive")
  [ "$actual" = "$hash" ] || fail "$archive from GitHub has the SHA-256 $actual, and its index says $hash"
  # The .sha256 beside it is what `fetch-seed.sh --archive` checks; an old seed without one gets it written now
  if ! github download seeds "$archive.sha256" "$work/copy/$archive.sha256" 2>/dev/null; then
    printf '%s  %s\n' "$hash" "$archive" >"$work/copy/$archive.sha256"
  fi
  if printf '%s\n' "$present" | grep -q -x "$archive"; then
    say "$archive is on the forge already (an earlier run stopped before the index): kept"
  else
    forge upload seeds "$work/copy/$archive" "$work/copy/$archive.sha256"
  fi
  printf '%s %s %s\n' "$commit" "$hash" "$archive" >>"$work/copied"
  rm -rf "$work/copy"
done <"$work/missing"

# One write of the index: the forge's own seeds first, then the copied ones
{
  cat "$work/forge.comments"
  cat "$work/forge.entries"
  cat "$work/copied"
} >"$work/seeds.txt"
forge upload seeds "$work/seeds.txt"
say "copied $missing seeds; the forge's seeds.txt lists $(grep -c -v '^#' "$work/seeds.txt") now"
