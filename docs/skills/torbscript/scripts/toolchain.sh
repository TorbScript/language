#!/bin/sh
# Finds the TorbScript toolchain, and installs it when asked to. Part of the torbscript Agent Skill.
#
# POSIX sh: Linux, macOS, FreeBSD, and Git Bash, MSYS2 or Cygwin on Windows. Windows PowerShell has toolchain.ps1.

set -u

usage() {
  cat <<'EOF'
Usage: sh toolchain.sh [--install]

Finds the TorbScript toolchain and prints one "key: value" line per fact:

  torb        the path of the torb in use, or "missing"
  version     what `torb --version` answers
  on-path     "yes", or how to call it when the PATH of this shell does not have it
  c-compiler  the C compiler `torb build` and `--native` would use, or "none"

  (no argument)  Changes nothing.
  --install      Runs the official installer first when torb is missing or does not run, and does nothing when it
                 runs. Ask the user before using it: it downloads https://torb.dev/install.sh (install.ps1 in a
                 Windows shell) and installs the toolchain for the current user.
  --help         This text.

Exit status: 0 torb runs, 1 it is missing or does not run, 2 a wrong argument, 3 the installer failed.
The installer's own output goes to standard error. Its variables, such as TORB_INSTALL_CHANNEL, pass through;
TORB_INSTALLER_URL names another installer, for a test.
EOF
}

is_windows() {
  case "$(uname -s 2>/dev/null)" in
    MINGW* | MSYS* | CYGWIN*) return 0 ;;
    *) return 1 ;;
  esac
}

# The torb in use: the one on the PATH, else where the installers put it (~/.torb/bin/torb, or on Windows
# %LOCALAPPDATA%\Programs\torb\bin\torb.exe). Sets torb_path (empty when there is none) and on_path.
find_torb() {
  on_path=yes
  torb_path=$(command -v torb 2>/dev/null || true)
  if [ -n "$torb_path" ]; then
    return 0
  fi
  on_path=no
  torb_path="${HOME:-}/.torb/bin/torb"
  if [ -x "$torb_path" ]; then
    return 0
  fi
  if is_windows && [ -n "${LOCALAPPDATA:-}" ]; then
    base=$LOCALAPPDATA
    if command -v cygpath >/dev/null 2>&1; then
      base=$(cygpath -u "$LOCALAPPDATA")
    fi
    torb_path="$base/Programs/torb/bin/torb.exe"
    if [ -x "$torb_path" ]; then
      return 0
    fi
  fi
  torb_path=""
  return 1
}

# What `torb --version` answers without the leading "torb", or nothing when it does not run
find_version() {
  version=""
  if [ -n "$torb_path" ]; then
    answer=$(NO_COLOR=1 "$torb_path" --version 2>/dev/null) || answer=""
    version=${answer#torb }
  fi
}

# The compiler `torb build` looks for: $TORB_CC, else the first of clang, gcc and cc on the PATH
find_c_compiler() {
  if [ -n "${TORB_CC:-}" ]; then
    c_compiler="$TORB_CC (from TORB_CC)"
    return 0
  fi
  for name in clang gcc cc; do
    if command -v "$name" >/dev/null 2>&1; then
      c_compiler=$name
      return 0
    fi
  done
  c_compiler="none - torb run, check, test and format work; torb build and --native need one"
}

report() {
  if [ -z "$torb_path" ]; then
    echo "torb: missing"
    if [ "$mode" = check ]; then
      echo "install: once the user agrees, run this script with --install"
    fi
  else
    echo "torb: $torb_path"
    if [ -n "$version" ]; then
      echo "version: $version"
    else
      echo "version: none - it is there but does not run"
    fi
    if [ "$on_path" = yes ]; then
      echo "on-path: yes"
    else
      echo "on-path: no - call it as $torb_path, or put $(dirname "$torb_path") on the PATH"
    fi
  fi
  echo "c-compiler: $c_compiler"
}

fetch() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -q "$1" -O "$2"
  else
    echo "toolchain.sh: neither curl nor wget is on the PATH" >&2
    return 1
  fi
}

# Downloads the official installer into a directory of its own and runs it, its output on standard error
run_installer() {
  scratch=$(mktemp -d 2>/dev/null || mktemp -d -t torb-install)
  trap 'rm -rf "$scratch"' EXIT
  if is_windows; then
    url=${TORB_INSTALLER_URL:-https://torb.dev/install.ps1}
    echo "toolchain.sh: running $url" >&2
    fetch "$url" "$scratch/install.ps1" || return 1
    script="$scratch/install.ps1"
    if command -v cygpath >/dev/null 2>&1; then
      script=$(cygpath -w "$script")
    fi
    powershell -NoProfile -ExecutionPolicy Bypass -File "$script" >&2 || return 1
  else
    url=${TORB_INSTALLER_URL:-https://torb.dev/install.sh}
    echo "toolchain.sh: running $url" >&2
    fetch "$url" "$scratch/install.sh" || return 1
    sh "$scratch/install.sh" >&2 || return 1
  fi
}

mode=check
if [ $# -gt 1 ]; then
  echo "toolchain.sh: one argument at most - see --help" >&2
  exit 2
fi
case "${1:-}" in
  "") ;;
  --install) mode=install ;;
  --help | -h)
    usage
    exit 0
    ;;
  *)
    echo "toolchain.sh: unknown argument $1 - see --help" >&2
    exit 2
    ;;
esac

find_torb || true
find_version
find_c_compiler

if [ "$mode" = install ]; then
  if [ -n "$version" ]; then
    report
    echo "install: nothing to do, torb runs"
    exit 0
  fi
  if ! run_installer; then
    report
    echo "install: failed - the installer's messages are above"
    exit 3
  fi
  find_torb || true
  find_version
fi

report
if [ -z "$version" ]; then
  exit 1
fi
