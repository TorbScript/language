#!/bin/sh
# Every host emits the same C: the compiler's own C for every target, as the `torb` of one machine emits it, has to be
# byte for byte what the `torb` of every other machine emits for that target - the C is a pure function of the source
# and the target, never of the machine that compiles (docs/design/RELEASE.md section 13, "every target emits the same C").
#
#   sh tools/agree.sh emit <torb> <file>      the compiler's C for every target: "<target> <program.hash>" per line
#   sh tools/agree.sh compare <file>...       whether every file names the same hash for every target it lists
#
# `emit` runs `torb build ./compiler --emit-c --target <target>` for the nine targets `torb build --target` knows,
# which needs no C compiler, and writes one line per target into <file>, the host's own name on the first line as a
# comment. It also checks that the C `torb` emits for its own machine is the C of the bootstrap
# (`build/release/program.hash`), where that is there: `--target` for this machine must change nothing.
#
# `compare` reads any number of such files - one per host: the Linux runner of the forge, the runners of the other
# targets once they exist, the machines of the GitHub mirror, the maintainer's own - and fails naming each target
# whose hash differs between two of them. A target one file lacks is not a difference.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "agree.sh: $*"
  exit 1
}

targets="windows-x64 windows-arm64 linux-x64 linux-arm64 macos-x64 macos-arm64 freebsd-x64 freebsd-arm64 browser-wasm64"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

[ $# -ge 2 ] || fail "usage: sh tools/agree.sh emit <torb> <file> | compare <file>..."
command=$1
shift

case "$command" in
  emit)
    [ $# -eq 2 ] || fail "usage: sh tools/agree.sh emit <torb> <file>"
    torb=$1
    file=$2
    case "$torb" in
      /* | [A-Za-z]:*) ;;
      *) torb="$(pwd)/$torb" ;;
    esac
    case "$file" in
      /* | [A-Za-z]:*) ;;
      *) file="$(pwd)/$file" ;;
    esac
    [ -f "$torb" ] || fail "there is no $torb"
    cd "$root"
    host=$(uname -s)-$(uname -m)
    work="$root/build/agree"
    rm -rf "$work"
    mkdir -p "$work" "$(dirname "$file")"
    printf '# emitted on %s by %s\n' "$host" "$torb" >"$file.next"
    for target in $targets; do
      started=$(date +%s)
      "$torb" build ./compiler --emit-c --target "$target" --output "$work/$target/torb" >"$work/$target.log" 2>&1 || {
        cat "$work/$target.log" >&2
        fail "the compiler could not be emitted for $target"
      }
      hash=$(cat "$work/$target/program.hash")
      printf '%s %s\n' "$target" "$hash" >>"$file.next"
      say "$target: $hash ($(($(date +%s) - started))s)"
      rm -f "$work/$target/"*.c "$work/$target/"*.h
    done
    mv "$file.next" "$file"
    # --target of this machine changes nothing: the bootstrap's C is the line of this machine's target
    case "$(uname -s)" in
      Linux) system=linux ;;
      Darwin) system=macos ;;
      FreeBSD) system=freebsd ;;
      MINGW* | MSYS* | CYGWIN*) system=windows ;;
      *) system="" ;;
    esac
    case "$(uname -m)" in
      x86_64 | amd64) architecture=x64 ;;
      arm64 | aarch64) architecture=arm64 ;;
      *) architecture="" ;;
    esac
    if [ -f build/release/program.hash ] && [ -n "$system" ] && [ -n "$architecture" ]; then
      native=$(cat build/release/program.hash)
      emitted=$(awk -v target="$system-$architecture" '$1 == target { print $2 }' "$file")
      if [ "$native" != "$emitted" ]; then
        fail "the bootstrap's C ($native) is not the C of --target $system-$architecture ($emitted)"
      fi
      say "--target $system-$architecture emits the bootstrap's C: $native"
    fi
    ;;

  compare)
    status=0
    awk '
      /^#/ { next }
      NF >= 2 {
        if (($1 in hash) && hash[$1] != $2) {
          printf "%s: %s (%s) and %s (%s)\n", $1, hash[$1], from[$1], $2, FILENAME
          differs = 1
        } else if (!($1 in hash)) {
          hash[$1] = $2
          from[$1] = FILENAME
        }
      }
      END { exit differs }
    ' "$@" || status=1
    if [ "$status" -ne 0 ]; then
      fail "two hosts emit other C for the same target (above): the C depends on the machine that compiles"
    fi
    say "$# hosts agree on the C of every target they emitted"
    ;;

  *)
    fail "unknown command \`$command\`: emit or compare"
    ;;
esac
