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
#   TORB_INSTALL_CHANNEL    default stable ("nightly" for the nightly channel)
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

version=${TORB_INSTALL_VERSION:-}
if [ -z "$version" ]; then
  fetch "$base_url/versions.txt" "$scratch/versions.txt" || fail "could not reach $base_url/versions.txt"
  version=$(awk -v c="$channel" '$2 == c { print $1; exit }' "$scratch/versions.txt")
  if [ -z "$version" ] && [ -z "$channel_requested" ] && [ "$channel" = "stable" ]; then
    # No stable release yet (docs/design/RELEASE.md section 3): the default channel falls back to the newest
    # nightly, and the toolchain follows the nightly channel (torb upgrade reads install.trb below) until a
    # stable release exists. An explicitly requested channel or version never falls back.
    fallback_version=$(awk -v c="nightly" '$2 == c { print $1; exit }' "$scratch/versions.txt")
    if [ -n "$fallback_version" ]; then
      say "no stable release yet: installing the nightly $fallback_version"
      channel="nightly"
      version="$fallback_version"
    fi
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
