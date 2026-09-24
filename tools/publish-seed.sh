#!/bin/sh
# Publishes a packed seed (`tools/pack-seed.sh`) to the `seeds` release of the repository and puts it at the top of the
# index `seeds.txt` there, which is what `tools/fetch-seed.sh` reads.
#
# The workflows run it (`.github/workflows/seed.yml`, `nightly.yml`, `release.yml`), with the job's own token; the owner
# runs it once by hand for the very first seed, because CI cannot bootstrap before one exists:
#
#   sh tools/pack-seed.sh seed                               # the main checkout's seed/, into build/seed-archive/
#   sh tools/publish-seed.sh build/seed-archive/torb-seed-<commit>.tar.gz
#
# Needs `gh`, logged in (`gh auth login`) or given `$GH_TOKEN` with `contents: write` on the repository.
# `$TORB_SEED_REPOSITORY` names the repository (default: TorbScript/language).
#
# It is idempotent: a seed whose commit the index already lists is left as it is. The `seeds` release is a
# prerelease, so it is never the repository's "latest" release, and it is created on first use; the commit its tag
# points at means nothing. Two publishers at the same time would race on `seeds.txt`, which is why every workflow that
# calls this script shares the concurrency group `seeds`.
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
command -v gh >/dev/null 2>&1 || fail "gh is needed to publish (https://cli.github.com)"

repository=${TORB_SEED_REPOSITORY:-TorbScript/language}
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

if ! gh release view seeds --repo "$repository" >/dev/null 2>&1; then
  say "creating the release \`seeds\` of $repository"
  gh release create seeds --repo "$repository" --prerelease --title "Seeds" --notes \
    "The portable seeds of the compiler: its own C and the runtime of the same commit, one archive per commit. \
seeds.txt lists them, newest first. sh tools/fetch-seed.sh downloads, verifies and builds one; nothing else here is \
meant to be downloaded by hand."
fi

if gh release download seeds --repo "$repository" --pattern seeds.txt --dir "$work" >/dev/null 2>&1; then
  :
else
  printf '%s\n' "# <commit> <sha256> <archive> - the newest first. Written by tools/publish-seed.sh." >"$work/seeds.txt"
fi

if awk -v commit="$commit" '$1 == commit { found = 1 } END { exit !found }' "$work/seeds.txt"; then
  say "the index already lists the seed $commit - nothing to do"
  exit 0
fi

gh release upload seeds --repo "$repository" --clobber "$archive" "$archive.sha256"

# The new line goes above every seed and below the comment lines at the top
{
  grep '^#' "$work/seeds.txt" || true
  printf '%s %s %s\n' "$commit" "$hash" "$name"
  grep -v '^#' "$work/seeds.txt" || true
} >"$work/seeds.next"
mv "$work/seeds.next" "$work/seeds.txt"
gh release upload seeds --repo "$repository" --clobber "$work/seeds.txt"

say "published the seed $commit to $repository (release seeds): $name, sha256 $hash"
