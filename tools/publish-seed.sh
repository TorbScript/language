#!/bin/sh
# Publishes a packed seed (`tools/pack-seed.sh`) to the release `seeds` of the repository on its forge and puts it at
# the top of the index `seeds.txt` there, which is what `tools/fetch-seed.sh` reads.
#
# The workflows run it (`.forgejo/workflows/seed.yml`, `nightly.yml`, `release.yml`) with the JWT of an Authorized
# Integration; `tools/land.sh publish` runs it with the owner's access token; the owner runs it by hand for the very
# first seed of a forge, because CI cannot bootstrap before one exists:
#
#   sh tools/pack-seed.sh seed                               # the main checkout's seed/, into build/seed-archive/
#   TORB_FORGE_TOKEN=<token> sh tools/publish-seed.sh build/seed-archive/torb-seed-<commit>.tar.gz
#
# The forge is `tools/forge.sh`'s: Forgejo at git.torb.dev unless TORB_FORGE, TORB_FORGE_URL and
# TORB_FORGE_REPOSITORY say otherwise (TORB_FORGE=github publishes to the GitHub mirror's release instead), and
# TORB_FORGE_TOKEN is a token that may write the repository's releases.
#
# It is idempotent: a seed whose commit the index already lists is left as it is. The `seeds` release is a
# prerelease, so it is never the repository's newest stable release, and it is created on first use at the commit
# `main` is at; the commit its tag points at means nothing. Two publishers at the same time would race on `seeds.txt`,
# which is why every workflow that calls this script shares the concurrency group `seeds`.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "publish-seed.sh: $*"
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

[ $# -eq 1 ] || fail "usage: sh tools/publish-seed.sh <torb-seed-<commit>.tar.gz>"
archive=$1
[ -f "$archive" ] || fail "there is no $archive"
[ -f "$archive.sha256" ] || fail "there is no $archive.sha256 beside it (tools/pack-seed.sh writes both)"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
forge() {
  sh "$root/tools/forge.sh" "$@"
}

name=$(basename "$archive")
case "$name" in
  torb-seed-*.tar.gz) ;;
  *) fail "a seed archive is named torb-seed-<commit>.tar.gz, not $name" ;;
esac
commit=${name#torb-seed-}
commit=${commit%.tar.gz}

hash=$(sha256_of "$archive")
recorded=$(cut -d ' ' -f 1 <"$archive.sha256")
[ "$hash" = "$recorded" ] || fail "$name has the SHA-256 $hash, and its .sha256 says $recorded"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# `forge.sh release` answers 2 for a release that does not exist, and 1 when the forge could not be asked
status=0
forge release seeds >"$work/release" 2>"$work/error" || status=$?
if [ "$status" -eq 1 ]; then
  cat "$work/error" >&2
  fail "the release \`seeds\` could not be read"
fi
if [ "$status" -eq 2 ]; then
  say "creating the release \`seeds\`"
  cat >"$work/notes.md" <<'EOF'
The portable seeds of the compiler: its own C and the runtime of the same commit, one archive per commit. seeds.txt
lists them, newest first. `sh tools/fetch-seed.sh` downloads, verifies and builds one; nothing else here is meant to
be downloaded by hand.
EOF
  forge create seeds main "Seeds" "$work/notes.md" --prerelease >/dev/null
  forge release seeds >"$work/release"
fi

# An index that exists is read, and one that cannot be read stops the publish: starting a new one would drop every
# seed it lists
if forge assets seeds | grep -q -x 'seeds.txt'; then
  forge download seeds seeds.txt "$work/seeds.txt" || fail "seeds.txt is on the release and could not be downloaded"
else
  printf '%s\n' "# <commit> <sha256> <archive> - the newest first. Written by tools/publish-seed.sh." >"$work/seeds.txt"
fi
tr -d '\r' <"$work/seeds.txt" >"$work/seeds.read"

if awk -v commit="$commit" '$1 == commit { found = 1 } END { exit !found }' "$work/seeds.read"; then
  say "the index already lists the seed $commit - nothing to do"
  exit 0
fi

forge upload seeds "$archive" "$archive.sha256"

# The new line goes above every seed and below the comment lines at the top
{
  grep '^#' "$work/seeds.read" || true
  printf '%s %s %s\n' "$commit" "$hash" "$name"
  grep -v '^#' "$work/seeds.read" || true
} >"$work/seeds.txt"
forge upload seeds "$work/seeds.txt"

say "published the seed $commit (release seeds): $name, sha256 $hash"
say "  $(forge url seeds seeds.txt)"
