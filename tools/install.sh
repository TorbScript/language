#!/bin/sh
# Installs TorbScript for the current user (docs/design/RELEASE.md section 5), published at https://torb.dev/install.sh:
#
#   curl -fsSL https://torb.dev/install.sh | sh
#
# Detects the target, downloads its archive and SHA256SUMS, verifies the hash, and unpacks into
# ~/.torb/toolchains/<version>/ with ~/.torb/bin/torb the one in use - the layout `torb upgrade` manages
# (docs/design/RELEASE.md, "torb upgrade") and `compiler/src/project/toolchain.trb` finds `std/` and `runtime/` beside.
# Nothing is written to a shell's profile: the PATH line is printed, once, for the person to add themselves.
#
# Overridable for testing, never needed otherwise:
#   TORB_INSTALL_BASE_URL   default https://torb.dev/download
#   TORB_INSTALL_CHANNEL    default stable; "preview" for the newest release candidate, beta or alpha (or the newest
#                           stable release where that is newer), "nightly" for the nightly channel
#   TORB_INSTALL_VERSION    an exact version, instead of the newest of the channel
#
# POSIX sh, curl or wget, tar, and sha256sum or shasum. Linux, macOS and FreeBSD; Windows uses install.ps1.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "install.sh: $*"
  exit 1
}

fetch() {
  # fetch <url> <output file>
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -q "$1" -O "$2"
  else
    fail "neither curl nor wget is on the PATH"
  fi
}

base_url=${TORB_INSTALL_BASE_URL:-https://torb.dev/download}
channel_requested=${TORB_INSTALL_CHANNEL:-}
channel=${TORB_INSTALL_CHANNEL:-stable}
home=${HOME:?TORB_INSTALL needs \$HOME}
torb_home="$home/.torb"

# ------------------------------------------------------------------------------------------------------- the target

# TORB_INSTALL_TARGET skips detection - for a container whose uname is not the host's, and for testing
target=${TORB_INSTALL_TARGET:-}
if [ -z "$target" ]; then
  case "$(uname -s)" in
    Linux) os=linux ;;
    Darwin) os=macos ;;
    FreeBSD) os=freebsd ;;
    *) fail "$(uname -s) is not a target of this script; Windows uses install.ps1" ;;
  esac
  case "$(uname -m)" in
    x86_64 | amd64) arch=x64 ;;
    arm64 | aarch64) arch=arm64 ;;
    *) fail "$(uname -m) is not a target architecture" ;;
  esac
  target="$os-$arch"
fi

# ---------------------------------------------------------------------------------------------------- the version

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

# The newest release of a channel: its first line in versions.txt, which release-sync writes newest first
first_of() {
  awk -v c="$1" '$2 == c { print $1; exit }' "$scratch/versions.txt"
}

# Whether the numbers of release $1 are above those of release $2 - all SemVer's precedence needs to set a pre-release
# against a stable release, since a release is newer than every pre-release of its numbers: 0.2.0-rc.1 is newer than
# 0.1.0, and 0.1.0 than 0.1.0-rc.1
numbers_above() {
  awk -v a="${1%%-*}" -v b="${2%%-*}" 'BEGIN {
    split(a, x, ".")
    split(b, y, ".")
    for (i = 1; i <= 3; i++) {
      if (x[i] + 0 > y[i] + 0) exit 0
      if (x[i] + 0 < y[i] + 0) exit 1
    }
    exit 1
  }'
}

version=${TORB_INSTALL_VERSION:-}
if [ -z "$version" ]; then
  fetch "$base_url/versions.txt" "$scratch/versions.txt" || fail "could not reach $base_url/versions.txt"
  version=$(first_of "$channel")
  if [ "$channel" = "preview" ]; then
    # The preview channel is never behind stable: once 0.1.0 is out it is 0.1.0, not 0.1.0-rc.1, until 0.2.0-rc.1
    stable_version=$(first_of stable)
    if [ -n "$stable_version" ] && { [ -z "$version" ] || ! numbers_above "$version" "$stable_version"; }; then
      version="$stable_version"
    fi
  fi
  if [ -z "$version" ] && [ -z "$channel_requested" ] && [ "$channel" = "stable" ]; then
    # No stable release yet (docs/design/RELEASE.md section 5): the default channel falls back to the newest
    # preview, and without one to the newest nightly, and the toolchain follows that channel (torb upgrade reads
    # install.trb below). An explicitly requested channel or version never falls back.
    for fallback in preview nightly; do
      fallback_version=$(first_of "$fallback")
      if [ -n "$fallback_version" ]; then
        say "no stable release yet: installing the $fallback $fallback_version"
        channel="$fallback"
        version="$fallback_version"
        break
      fi
    done
  fi
  [ -n "$version" ] || fail "no version of the \"$channel\" channel is listed at $base_url/versions.txt"
fi

archive="torb-$version-$target.tar.gz"
say "installing TorbScript $version ($target)"

# SHA256SUMS is fetched first and is what decides whether $target has an archive of $version at all: a missing
# archive is a clear error naming the version and target, not a failed download that looks like a network problem
fetch "$base_url/$version/SHA256SUMS" "$scratch/SHA256SUMS" || fail "could not download SHA256SUMS for $version"

# GNU sha256sum marks a file read in binary mode with a leading "*" (always, on a system where text and binary
# differ) - stripped here so the same SHA256SUMS verifies on every platform that wrote it
expected=$(awk -v f="$archive" '{ name = $2; sub(/^\*/, "", name); if (name == f) { print $1; exit } }' "$scratch/SHA256SUMS")
[ -n "$expected" ] || fail "$version has no $target archive yet"

fetch "$base_url/$version/$archive" "$scratch/$archive" || fail "could not download $archive"

# ---------------------------------------------------------------------------------------------------------- the hash

if command -v sha256sum >/dev/null 2>&1; then
  actual=$(sha256sum "$scratch/$archive" | awk '{ print $1 }')
elif command -v shasum >/dev/null 2>&1; then
  actual=$(shasum -a 256 "$scratch/$archive" | awk '{ print $1 }')
else
  fail "neither sha256sum nor shasum is on the PATH"
fi
[ "$actual" = "$expected" ] || fail "$archive: expected sha256 $expected, got $actual"
say "sha256 verified"

# ------------------------------------------------------------------------------------------------------- unpacking

toolchain="$torb_home/toolchains/$version"
rm -rf "$toolchain"
mkdir -p "$toolchain" "$torb_home/bin"
tar -xzf "$scratch/$archive" --strip-components=1 -C "$toolchain"
ln -sfn "../toolchains/$version/bin/torb" "$torb_home/bin/torb"
printf '%s\n' "$version" >"$torb_home/active"

# `torb upgrade` reads this to know how the toolchain got here (docs/design/RELEASE.md, "torb upgrade"): fields are
# written with `=`, which is the format of this file and of nothing else in the repository.
cat >"$torb_home/install.trb" <<EOF
channel = "$channel"
method = "script"
EOF

say ""
say "TorbScript $version is installed in $toolchain"
say "add this to your shell's profile (~/.profile, ~/.bashrc, ~/.zshrc):"
say ""
say "    export PATH=\"\$HOME/.torb/bin:\$PATH\""
say ""
say "then start a new shell and run: torb"
