#!/bin/sh
# Downloads a published seed, verifies it, and compiles it into `build/seed/torb` - the seed `tools/bootstrap.sh` uses
# when this machine has none of its own (no `$TORB_SEED`, no `seed/`, no archive in `../torbscript-seeds/`).
#
# Seeds are published as assets of one GitHub release, tagged `seeds`, of the repository (`tools/publish-seed.sh`,
# run by the nightly, the release and the seed workflows). Beside the archives it holds `seeds.txt`, the index, one
# line per seed and the newest first:
#
#   <commit> <sha256 of the archive> <file name of the archive>
#   1ae963fa1234 5f3a...e9 torb-seed-1ae963fa1234.tar.gz
#
# An archive is found in the same directory as the index, so a mirror, a fork or a local directory is an index URL
# away. The download is checked against the hash in the index, unpacked into `build/seed/`, compiled with the local C
# compiler (`tools/build-seed.sh`: `$TORB_CC`, clang, gcc, cc) and run once on a one-line program before it counts.
#
#   sh tools/fetch-seed.sh                  # the newest seed of the index
#   sh tools/fetch-seed.sh 1ae963fa         # the seed built from that commit (any unique prefix of it)
#   sh tools/fetch-seed.sh --newest         # print the commit of the newest seed, download nothing
#   sh tools/fetch-seed.sh --list           # print the index
#   sh tools/fetch-seed.sh --archive <path or URL of a torb-seed-*.tar.gz>
#                                           # that archive, checked against the .sha256 beside it
#
# Variables:
#   TORB_SEED_REPOSITORY  owner/name on GitHub (default TorbScript/language)
#   TORB_SEED_INDEX       the index: an https:// or file:// URL or a path (default: the `seeds` release's seeds.txt)
#   TORB_SEED_SHA256      the expected hash for --archive, instead of the .sha256 beside it
#   GH_TOKEN, GITHUB_TOKEN  for a private repository: the downloads then go through the REST API with this token
#   TORB_CC, TORB_CFLAGS  the C compiler and extra flags (tools/build-seed.sh)
#
# Needs curl or wget, tar, gzip, and sha256sum, shasum or openssl - no `gh`, no `torb`. A seed that is already in
# `build/seed/` for the requested commit is not downloaded or compiled again.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "fetch-seed.sh: $*"
  exit 1
}

absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
}

binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
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

mode=fetch
wanted=""
archive_source=""
while [ $# -gt 0 ]; do
  case "$1" in
    --newest) mode=newest ;;
    --list) mode=list ;;
    --archive)
      [ $# -ge 2 ] || fail "--archive needs a path or a URL"
      archive_source=$2
      shift
      ;;
    -h | --help)
      sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    -*) fail "unknown option $1" ;;
    *) wanted=$1 ;;
  esac
  shift
done

case "$archive_source" in
  "" | *://*) ;;
  *) archive_source=$(absolute "$archive_source") ;;
esac
case "${TORB_SEED_INDEX-}" in
  "" | *://*) ;;
  *) TORB_SEED_INDEX=$(absolute "$TORB_SEED_INDEX") ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

repository=${TORB_SEED_REPOSITORY:-TorbScript/language}
index_url=${TORB_SEED_INDEX:-https://github.com/$repository/releases/download/seeds/seeds.txt}
token=${GH_TOKEN:-${GITHUB_TOKEN-}}
destination="$root/build/seed"

# ----------------------------------------------------------------------------- downloading --------------------------

# One URL into one file. A path or a file:// URL is copied. A release asset of GitHub is fetched through the REST API
# when there is a token - the plain download URL of a private repository answers with a login page - and with a plain
# GET otherwise. curl drops the Authorization header on the redirect to the storage host, which is what GitHub expects.
fetch() {
  url=$1
  file=$2
  case "$url" in
    file://*)
      cp "${url#file://}" "$file"
      return
      ;;
    *://*) ;;
    *)
      cp "$url" "$file"
      return
      ;;
  esac
  case "$url" in
    https://github.com/*/releases/download/*)
      if [ -n "$token" ] || [ "${TORB_SEED_USE_API-}" = "1" ]; then
        fetch_asset "$url" "$file"
        return
      fi
      ;;
  esac
  get "$url" "$file"
}

# A plain HTTPS GET, with the headers given after the file, retried a few times.
get() {
  get_url=$1
  get_file=$2
  shift 2
  if command -v curl >/dev/null 2>&1; then
    set -- --proto '=https' --tlsv1.2 -fsSL --retry 3 --retry-delay 2 -o "$get_file" "$@" "$get_url"
    curl "$@" || fail "could not download $get_url"
  elif command -v wget >/dev/null 2>&1; then
    # `-H <header>` pairs become `--header=<header>`: the list is rebuilt in place, which keeps the spaces in a header
    for header in "$@"; do
      shift
      [ "$header" = "-H" ] && continue
      set -- "$@" "--header=$header"
    done
    wget -q --https-only --tries=3 -O "$get_file" "$@" "$get_url" || fail "could not download $get_url"
  else
    fail "neither curl nor wget is installed"
  fi
}

# https://github.com/<owner>/<name>/releases/download/<tag>/<asset> through the API: the release of the tag names the
# id of every asset, and the asset's URL with `Accept: application/octet-stream` redirects to its bytes.
fetch_asset() {
  asset_file=$2
  path=${1#https://github.com/}
  owner=${path%%/*}
  path=${path#*/}
  name=${path%%/*}
  path=${path#*/releases/download/}
  tag=${path%%/*}
  asset=${path#*/}
  listing=$(mktemp)
  if [ -n "$token" ]; then
    get "https://api.github.com/repos/$owner/$name/releases/tags/$tag" "$listing" \
      -H "Authorization: Bearer $token" -H "Accept: application/vnd.github+json"
  else
    get "https://api.github.com/repos/$owner/$name/releases/tags/$tag" "$listing" -H "Accept: application/vnd.github+json"
  fi
  # The JSON on one line, `"key" : value` as `"key":value`, then cut before every `"url":"`: an asset's own URL is
  # followed by its `name` before the next URL (its uploader's) starts
  id=$(tr -d '\r\n' <"$listing" | sed 's/"[[:space:]]*:[[:space:]]*/":/g' | awk -v wanted="$asset" '
    {
      count = split($0, parts, /"url":"/)
      for (part = 2; part <= count; part++) {
        if (match(parts[part], /^https:\/\/api\.github\.com\/repos\/[^"]*\/releases\/assets\/[0-9]+"/)) {
          url = substr(parts[part], 1, RLENGTH - 1)
          if (index(parts[part], "\"name\":\"" wanted "\"") > 0) {
            sub(/.*\//, "", url)
            print url
            exit
          }
        }
      }
    }')
  rm -f "$listing"
  [ -n "$id" ] || fail "the release \`$tag\` of $owner/$name has no asset \`$asset\`"
  if [ -n "$token" ]; then
    get "https://api.github.com/repos/$owner/$name/releases/assets/$id" "$asset_file" \
      -H "Authorization: Bearer $token" -H "Accept: application/octet-stream"
  else
    get "https://api.github.com/repos/$owner/$name/releases/assets/$id" "$asset_file" -H "Accept: application/octet-stream"
  fi
}

# ----------------------------------------------------------------------------- which seed ---------------------------

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -n "$archive_source" ]; then
  archive_name=$(basename "$archive_source")
  case "$archive_name" in
    torb-seed-*.tar.gz) ;;
    *) fail "an archive is named torb-seed-<commit>.tar.gz, not $archive_name" ;;
  esac
  commit=${archive_name#torb-seed-}
  commit=${commit%.tar.gz}
  if [ -n "${TORB_SEED_SHA256-}" ]; then
    expected=$TORB_SEED_SHA256
  else
    fetch "$archive_source.sha256" "$work/sums" || fail "there is no $archive_source.sha256 and no \$TORB_SEED_SHA256"
    expected=$(cut -d ' ' -f 1 <"$work/sums")
  fi
else
  fetch "$index_url" "$work/seeds.txt"
  # Comments and blank lines are not seeds; a line is `<commit> <sha256> <file>`
  grep -v '^[[:space:]]*\(#\|$\)' "$work/seeds.txt" | tr -d '\r' >"$work/entries" || true
  [ -s "$work/entries" ] || fail "the index $index_url lists no seed"
  if [ "$mode" = "list" ]; then
    cat "$work/entries"
    exit 0
  fi
  if [ -z "$wanted" ]; then
    entry=$(head -n 1 "$work/entries")
  else
    matches=$(awk -v prefix="$wanted" 'index($1, prefix) == 1' "$work/entries")
    [ -n "$matches" ] || fail "the index lists no seed built from a commit that starts with $wanted"
    [ "$(printf '%s\n' "$matches" | wc -l | tr -d ' ')" -eq 1 ] || fail "$wanted names more than one seed of the index"
    entry=$matches
  fi
  commit=$(printf '%s\n' "$entry" | awk '{ print $1 }')
  expected=$(printf '%s\n' "$entry" | awk '{ print $2 }')
  archive_name=$(printf '%s\n' "$entry" | awk '{ print $3 }')
  [ -n "$archive_name" ] || fail "the index line \`$entry\` is not \`<commit> <sha256> <file>\`"
  archive_source="${index_url%/*}/$archive_name"
fi

if [ "$mode" = "newest" ]; then
  printf '%s\n' "$commit"
  exit 0
fi

# ----------------------------------------------------------------------------- already here? ------------------------

present=$(binary_of "$destination/torb")
if [ -n "$present" ] && [ "$(cat "$destination/commit" 2>/dev/null)" = "$commit" ]; then
  say "the seed $commit is already built: $present"
  printf '%s\n' "$present"
  exit 0
fi

# ----------------------------------------------------------------------------- download, verify, unpack ------------

say "downloading the seed $commit: $archive_source"
fetch "$archive_source" "$work/$archive_name"
actual=$(sha256_of "$work/$archive_name")
if [ "$actual" != "$expected" ]; then
  fail "$archive_name has the SHA-256 $actual, and $expected was expected - nothing was unpacked"
fi
say "sha256 verified: $actual"

mkdir -p "$work/unpacked"
gzip -dc "$work/$archive_name" | tar -x -C "$work/unpacked" -f -
unpacked="$work/unpacked/torb-seed-$commit"
[ -f "$unpacked/program.c" ] || fail "$archive_name has no torb-seed-$commit/program.c"
[ -f "$unpacked/runtime/include/torb.h" ] || fail "$archive_name has no torb-seed-$commit/runtime/include/torb.h"

# ----------------------------------------------------------------------------- compile and probe -------------------

sh "$root/tools/build-seed.sh" "$unpacked" "$unpacked/torb"
binary=$(binary_of "$unpacked/torb")
[ -n "$binary" ] || fail "the C compiler wrote no binary"

# The probe of tools/refresh-seed.sh: a seed that cannot check a one-line program is not a seed. The program lies in a
# directory of its own - a file under a `build/` directory is never a source - and is checked from the repository
# root, so the seed finds this checkout's std/.
mkdir -p "$work/probe"
printf '%s\n' 'print "the seed runs"' >"$work/probe/probe.trb"
if ! output=$("$binary" check "$work/probe/probe.trb" 2>&1); then
  say "$output"
  fail "the compiled seed cannot check a one-line program"
fi
case "$output" in
  "1 file, no problems"* | "1 files, no problems"*) ;;
  *)
    say "$output"
    fail "the compiled seed answered something else than \"1 file, no problems\" for a one-line program"
    ;;
esac

# Only a seed that runs replaces the one in build/seed/. A running seed.exe cannot be deleted on Windows, but the
# directory holding it can be renamed out of the way.
if [ -d "$destination" ]; then
  mv "$destination" "$destination.old-$$"
fi
mkdir -p "$(dirname "$destination")"
mv "$unpacked" "$destination"
rm -rf "$destination.old-$$" 2>/dev/null || true
installed=$(binary_of "$destination/torb")
say "seed $commit: $installed"
printf '%s\n' "$installed"
