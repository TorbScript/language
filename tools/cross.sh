#!/bin/sh
# Builds the release binary of another machine on this one (docs/design/RELEASE.md section 13, "Cross-compiled
# archives"): the compiler's C for that target, emitted by a `torb` of this machine, compiled together with the runtime
# by the pinned `zig cc` (tools/fetch-zig.sh) - the path the playground takes to WebAssembly with emscripten
# (playground/build.sh), with Zig as the C compiler.
#
#   sh tools/cross.sh <torb> <target> <output>
#   sh tools/cross.sh build/release/torb windows-x64 build/cross/windows-x64/torb
#   sh tools/cross.sh check <target> <binary>      only the checks of step 3, of a binary built before
#
#   windows-x64   x86_64-windows-gnu: MinGW-w64 on the UCRT, as MSYS2's UCRT64 gcc builds it on the native runner, with
#                 the icon and the version of tools/windows/torb.rc compiled by `zig rc`; writes <output>.exe
#   linux-arm64   aarch64-linux-musl, linked statically, as `musl-gcc -static` builds it on the native runner
#   macos-arm64   aarch64-macos, macOS 13 and newer, with the ad hoc signature Zig's linker writes
#
# 1. `<torb> build ./compiler --emit-c --target <target>` writes the C into the directory of <output>. The C a host
#    emits for a target is the C that target's own bootstrap emits - the `agree` job of the gates compares exactly that
#    on every run - so this binary is the compiler a native bootstrap of the same commit builds, compiled by another C
#    compiler.
# 2. tools/build-seed.sh compiles it with its runtime, `$TORB_CC` a wrapper of `zig cc -target <triple>` beside the C.
# 3. The checks: the file is an executable of that machine; windows-x64 imports only DLLs every Windows has (the list
#    of .forgejo/actions/portable) and carries its resources; linux-arm64 is static; macos-arm64 is signed (an arm64
#    Mach-O without a signature is killed at its start, RELEASE.md section 5) and loads nothing but the system's
#    libraries.
#
# It never runs the binary: that needs its machine, or an emulator - `TORB_SMOKE_RUNNER=wine` or `qemu-aarch64` with
# tools/smoke-test.sh. A binary built here is smoke-tested at most, never gated: the conformance suite and the runtime's
# tests run on a native runner of the target, or on the GitHub mirror (.github/workflows/portable.yml).
#
# Needs a `torb` of this machine, and what tools/fetch-zig.sh needs; `file`, and `objdump` for windows-x64. `$TORB_ZIG`
# names a zig to use instead of the pinned one.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "cross.sh: $*"
  exit 1
}

absolute() {
  case "$1" in
    /* | [A-Za-z]:*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$(pwd)/$1" ;;
  esac
}

usage() {
  say "usage: sh tools/cross.sh <torb> <target> <output>"
  say "       sh tools/cross.sh check <target> <binary>"
  exit 2
}

# The load commands of a 64-bit little-endian Mach-O file: "<command> <size>" per line, and "dylib <path>" after each
# command that loads a library. od reads 32-bit words in the byte order of this machine, which is little-endian on
# every host this runs on (x64 and arm64).
macho_commands() {
  # shellcheck disable=SC2046
  set -- $(od -An -tu4 -N 32 "$1") "$1"
  [ $# -eq 9 ] || fail "$9 is too short for a Mach-O header"
  [ "$1" = "4277009103" ] || fail "$9 is not a 64-bit Mach-O file (magic $1)"
  [ "$2" = "16777228" ] || fail "$9 is not for arm64 (cputype $2)"
  od -An -tu4 -v -j 32 -N "$6" "$9" | awk -v count="$5" '
    { for (i = 1; i <= NF; i++) word[n++] = $i }
    # The byte at `offset` of the words from `start` on
    function byte(start, offset) {
      return int(word[start + int(offset / 4)] / (256 ^ (offset % 4))) % 256
    }
    END {
      at = 0
      for (c = 0; c < count; c++) {
        command = word[at]
        size = word[at + 1]
        printf "%d %d\n", command, size
        # LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB: the name at the offset in the third word
        if (command == 12 || command == 2147483672 || command == 2147483679) {
          name = ""
          for (offset = word[at + 2]; offset < size; offset++) {
            value = byte(at, offset)
            if (value == 0) break
            name = name sprintf("%c", value)
          }
          printf "dylib %s\n", name
        }
        # LC_BUILD_VERSION: platform, then the oldest macOS it runs on as xxxx.yy.zz
        if (command == 50) {
          printf "minimum %d.%d\n", int(word[at + 3] / 65536), int(word[at + 3] / 256) % 256
        }
        if (size < 8) exit 1
        at += size / 4
      }
    }'
}

check() {
  target=$1
  binary=$2
  [ -f "$binary" ] || fail "there is no $binary"
  kind=$(file -b "$binary")
  say "$binary: $kind"
  case "$target" in
    windows-x64)
      case "$kind" in
        PE32+*x86-64*) ;;
        *) fail "$binary is not an x86-64 Windows executable" ;;
      esac
      imports=$(objdump -p "$binary" | sed -n 's/^[[:space:]]*DLL Name: //p')
      [ -n "$imports" ] || fail "objdump lists no DLL that $binary imports"
      say "imports: $(printf '%s' "$imports" | tr '\n' ' ')"
      # The list of .forgejo/actions/portable: what every Windows 10 and 11 has
      foreign=$(printf '%s\n' "$imports" |
        grep -v -i -E '^(kernel32|user32|shell32|advapi32|ws2_32|bcrypt|ntdll|ucrtbase|msvcrt|api-ms-win-[a-z0-9-]+)\.dll$' ||
        true)
      [ -z "$foreign" ] || fail "$binary needs DLLs a Windows machine does not have: $foreign"
      if ! objdump -h "$binary" | grep -q '\.rsrc'; then
        fail "$binary has no resource section: no icon and no version"
      fi
      ;;
    linux-arm64)
      case "$kind" in
        ELF\ 64-bit*ARM\ aarch64*) ;;
        *) fail "$binary is not an arm64 Linux executable" ;;
      esac
      case "$kind" in
        *"statically linked"* | *"static-pie linked"*) ;;
        *) fail "$binary is linked dynamically" ;;
      esac
      ;;
    macos-arm64)
      case "$kind" in
        Mach-O\ 64-bit*arm64*) ;;
        *) fail "$binary is not an arm64 Mach-O executable" ;;
      esac
      commands=$(macho_commands "$binary") || fail "the load commands of $binary could not be read"
      # LC_CODE_SIGNATURE, 0x1d
      printf '%s\n' "$commands" | grep -q '^29 ' || fail "$binary carries no code signature"
      libraries=$(printf '%s\n' "$commands" | sed -n 's/^dylib //p')
      say "loads: $(printf '%s' "$libraries" | tr '\n' ' ')"
      say "runs on: macOS $(printf '%s\n' "$commands" | sed -n 's/^minimum //p') and newer"
      foreign=$(printf '%s\n' "$libraries" | grep -v -E '^(/usr/lib/|/System/Library/)' || true)
      [ -z "$foreign" ] || fail "$binary loads libraries that are not the system's: $foreign"
      ;;
    *) fail "no checks for $target" ;;
  esac
  say "checked: $binary is a $target executable"
}

[ $# -eq 3 ] || usage
if [ "$1" = "check" ]; then
  check "$2" "$(absolute "$3")"
  exit 0
fi

torb=$(absolute "$1")
target=$2
output=$(absolute "$3")
[ -f "$torb" ] || [ -f "$torb.exe" ] || fail "there is no torb at $1"
[ -f "$torb" ] || torb="$torb.exe"

case "$target" in
  windows-x64) triple=x86_64-windows-gnu ;;
  linux-arm64) triple=aarch64-linux-musl ;;
  macos-arm64) triple=aarch64-macos.13.0 ;;
  *) fail "no cross build for $target (windows-x64, linux-arm64, macos-arm64)" ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

zig=${TORB_ZIG-}
if [ -z "$zig" ]; then
  zig=$(sh tools/fetch-zig.sh) || fail "no zig: tools/fetch-zig.sh failed"
fi
say "zig $("$zig" version): $zig"

directory=$(dirname "$output")
mkdir -p "$directory"
rm -f "$directory"/program.c "$directory"/program.h "$directory"/program-*.c "$directory"/program.hash

say "emitting the compiler's C for $target"
"$torb" build ./compiler --emit-c --target "$target" --output "$output" >&2 ||
  fail "$torb could not emit the compiler's C for $target"
[ -f "$directory/program.c" ] || fail "the emission wrote no $directory/program.c"
say "the compiler's C for $target: program.hash $(cat "$directory/program.hash" 2>/dev/null || echo none)"

# tools/build-seed.sh names its compiler as one word
compiler="$directory/cc-$target"
cat >"$compiler" <<EOF
#!/bin/sh
exec "$zig" cc -target $triple "\$@"
EOF
chmod 755 "$compiler"

objects=""
if [ "$target" = "windows-x64" ]; then
  TORB_ZIG=$zig sh tools/windows/resource.sh "$directory/torb-resource.res" ||
    fail "zig rc could not compile tools/windows/torb.rc"
  objects="$directory/torb-resource.res"
fi

rm -f "$output" "$output.exe"
TORB_CC=$compiler TORB_OBJECTS=$objects sh tools/build-seed.sh "$directory" "$output"

binary=$output
if [ "$target" = "windows-x64" ]; then
  # zig names the file as it is told, where MinGW's gcc appends .exe
  if [ -f "$output" ] && [ ! -f "$output.exe" ]; then
    mv "$output" "$output.exe"
  fi
  binary="$output.exe"
fi
check "$target" "$binary"
say "wrote $binary"
