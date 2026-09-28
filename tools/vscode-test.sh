#!/bin/sh
# The VS Code extension of editors/vscode in a real VS Code: an Extension Development Host with its own user data and
# extension directories, over a workspace of tests/debug/'s programs, runs editors/vscode/test/debugging.js through
# `--extensionTestsPath` - the debugger to a breakpoint and through its steps, the CodeLens of an entry file, and the
# Test Explorer's Debug profile - and prints what it found. It opens a window of VS Code for as long as it runs, so it
# is no gate; run it after a change of the debugger or of the extension.
#
# `$VSCODE` is the executable of VS Code (`Code.exe` on Windows, not the `code` script, which returns before the run
# ends); without it the script looks where the installers put it. `$VSCODE_TEST_TORB` is the `torb` the extension runs,
# `build/release/torb` by default. POSIX sh; runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/vscode-test.sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

# The `.exe` first: Git Bash finds `torb` where only `torb.exe` exists, and the extension looks for the file it is named
binary_of() {
  if [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  elif [ -f "$1" ]; then
    printf '%s\n' "$1"
  fi
  return 0
}

native_path() {
  (cd "$1" && (pwd -W 2>/dev/null || pwd))
}

vscode=${VSCODE-}
if [ -z "$vscode" ]; then
  for candidate in \
    "${LOCALAPPDATA:-}/Programs/Microsoft VS Code/Code.exe" \
    "/c/Program Files/Microsoft VS Code/Code.exe" \
    "/Applications/Visual Studio Code.app/Contents/MacOS/Electron" \
    "/usr/share/code/code" \
    "/usr/bin/code"; do
    if [ -f "$candidate" ]; then
      vscode=$candidate
      break
    fi
  done
fi
[ -n "$vscode" ] || {
  say "vscode-test.sh: no VS Code found - set \$VSCODE to its executable"
  exit 1
}

torb=$(binary_of "${VSCODE_TEST_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  say "vscode-test.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}
case "$torb" in
  /* | ?:*) ;;
  *) torb="$root/$torb" ;;
esac

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
workspace="$scratch/workspace"
mkdir -p "$workspace/.vscode"
cp tests/debug/breakpoint/main.trb "$workspace/main.trb"
cp tests/debug/tests/math.test.trb "$workspace/math.test.trb"
torb_native=$(cd "$(dirname "$torb")" && (pwd -W 2>/dev/null || pwd))/$(basename "$torb")
printf '{\n  "torbscript.executablePath": "%s",\n  "torbscript.languageServer.enabled": true\n}\n' "$torb_native" \
  >"$workspace/.vscode/settings.json"

result="$(native_path "$scratch")/result.txt"
status=0
# A terminal inside VS Code sets ELECTRON_RUN_AS_NODE, which makes the executable a Node.js that refuses every option
unset ELECTRON_RUN_AS_NODE
TORBSCRIPT_TEST_RESULT="$result" "$vscode" \
  --user-data-dir "$(native_path "$scratch")/user" \
  --extensions-dir "$(native_path "$scratch")/extensions" \
  --disable-workspace-trust \
  --skip-welcome \
  --skip-release-notes \
  --extensionDevelopmentPath="$(native_path "$root/editors/vscode")" \
  --extensionTestsPath="$(native_path "$root/editors/vscode/test")/debugging.js" \
  "$(native_path "$workspace")" >"$scratch/vscode.log" 2>&1 || status=$?
if [ -f "$scratch/result.txt" ]; then
  cat "$scratch/result.txt"
else
  tail -40 "$scratch/vscode.log" >&2
  say "vscode-test.sh: the test wrote no result (exit $status)"
  exit 1
fi
if [ "$status" -ne 0 ] || ! grep -q '^ok$' "$scratch/result.txt"; then
  say "--- the last lines VS Code wrote"
  tail -40 "$scratch/vscode.log" >&2
  exit 1
fi
