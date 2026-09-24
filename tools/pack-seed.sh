#!/bin/sh
# Packs a seed into the archive that is published: `torb-seed-<commit>.tar.gz` and `torb-seed-<commit>.tar.gz.sha256`.
#
# A published seed is what a checkout without `seed/` bootstraps from (`tools/fetch-seed.sh`), so it carries everything
# a C compiler needs and nothing a C compiler does not: the compiler's own C, the runtime that C was emitted against,
# and the script that compiles the two. The runtime has to come from the same commit as the C - a later runtime may
# have renamed a native the older C still calls - so it is taken from git at that commit, never from the working tree.
#
#   sh tools/pack-seed.sh                        # build/release, built from HEAD, into build/seed-archive/
#   sh tools/pack-seed.sh seed                   # the seed of the main checkout, built from the commit in seed/commit
#   sh tools/pack-seed.sh build/release dist     # into dist/
#
# The archive:
#
#   torb-seed-<commit>/
#     program.c       the compiler's C, emitted by the compiler of <commit> from the sources of <commit>
#     program.hash    the hash of program.c, which `torb run` keys its cache on
#     commit          <commit>: twelve hexadecimal digits
#     runtime/        the C runtime of <commit>: include/, *.c, os/*.c - not its tests
#     build.sh        tools/build-seed.sh: `sh build.sh` writes ./torb with the machine's C compiler
#     LICENSE         the license of that commit, where it has one
#     README.md       what this is, in five lines
#
# It is reproducible: entries sorted by name, owner and group 0, fixed modes, every timestamp the commit's time, and
# gzip without a name or a time - the same commit packs the same bytes wherever GNU tar and gzip run.
#
# POSIX sh plus GNU tar (`tar`, or `gtar` on macOS). Runs in Git Bash on Windows and on Linux.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "pack-seed.sh: $*"
  exit 1
}

absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
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

source_directory=$(absolute "${1:-build/release}")
output=$(absolute "${2:-build/seed-archive}")

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

[ -f "$source_directory/program.c" ] || fail "there is no program.c in $source_directory"

tar=""
for candidate in tar gtar; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" --version 2>/dev/null | grep -q 'GNU tar'; then
    tar=$candidate
    break
  fi
done
[ -n "$tar" ] || fail "GNU tar is needed for a reproducible archive (tar or gtar)"

# The commit the C was built from: what the refresh that filled the directory wrote down, or HEAD for build/release -
# and then the tree the C was built from must be HEAD's, or the runtime in the archive would not be the one it expects
if [ -f "$source_directory/commit" ]; then
  given=$(tr -d ' \r\n' <"$source_directory/commit")
else
  given=HEAD
  changed=$(git status --porcelain -- compiler std runtime project.trb)
  if [ -n "$changed" ]; then
    say "$changed"
    fail "the tree has uncommitted changes to what the compiler is built from, so the C in $source_directory cannot be named by a commit"
  fi
fi
full=$(git rev-parse --verify --quiet "$given^{commit}") || fail "the commit \`$given\` is not in this repository"
commit=$(git rev-parse --short=12 "$full")
epoch=$(git show -s --format=%ct "$full")
name="torb-seed-$commit"

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
seed="$staging/$name"
mkdir -p "$seed"

cp "$source_directory/program.c" "$seed/program.c"
if [ -f "$source_directory/program.hash" ]; then
  cp "$source_directory/program.hash" "$seed/program.hash"
fi
printf '%s\n' "$commit" >"$seed/commit"
git archive --format=tar "$full" runtime | "$tar" -x -C "$seed"
rm -rf "$seed/runtime/tests" "$seed/runtime/build.sh"
# The license of the compiler whose C this is, where that commit has one (runtime/ brings its own)
if git cat-file -e "$full:LICENSE" 2>/dev/null; then
  git show "$full:LICENSE" >"$seed/LICENSE"
fi
cp tools/build-seed.sh "$seed/build.sh"
chmod 755 "$seed/build.sh"
cat >"$seed/README.md" <<EOF
# TorbScript seed $commit

The TorbScript compiler of commit $commit as one C file, with the C runtime of the same commit.
\`sh build.sh\` compiles it into \`./torb\` with the machine's C compiler (\`\$TORB_CC\`, clang, gcc or cc).
In a checkout of the repository, \`sh tools/fetch-seed.sh\` downloads, verifies and builds this for you, and
\`sh tools/bootstrap.sh\` then builds the checkout's compiler with it.
EOF

mkdir -p "$output"
archive="$output/$name.tar.gz"
"$tar" --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner --mode='a=rX,u+w' --format=gnu \
  -C "$staging" -cf - "$name" | gzip -9 -n >"$archive"
hash=$(sha256_of "$archive")
printf '%s  %s\n' "$hash" "$name.tar.gz" >"$archive.sha256"

say "wrote $archive ($(wc -c <"$archive" | tr -d ' ') bytes)"
say "sha256 $hash"
printf '%s\n' "$archive"
