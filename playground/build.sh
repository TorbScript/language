#!/bin/sh
# Builds `torb` for WebAssembly, the toolchain of the playground (docs/design/RELEASE.md section 6, "The playground").
#
# The compiler is TorbScript that emits C, so the `torb` of the browser is the compiler's own C, emitted for the target
# `browser-wasm64`, together with `runtime/`, compiled by emscripten. Nothing of the fixpoint changes: the C of that
# target differs from the host's only where `OperatingSystem.current` is asked, and nothing but this script compiles it.
#
#   sh playground/build.sh              # build/release/torb emits the C, emcc or the pinned image compiles it
#   sh playground/build.sh --c <dir>    # compiles C emitted before into <dir> (program.h and program-*.c)
#   sh playground/build.sh --compiled <dir>   # takes torb.js and torb.wasm compiled before from <dir>, and writes
#                                             # the rest: the site image of main, whose workflow keeps them in the
#                                             # forge's cache while compiler/src, std/ and runtime/ stay the same
#   TORB=/opt/torb/bin/torb sh playground/build.sh    # another torb emits the C: a release's, in the site image
#   TORB_EMSDK_IMAGE=emscripten/emsdk:6.0.10 sh playground/build.sh
#   TORB_EMCC=/path/to/emcc sh playground/build.sh
#
# It writes into `build/playground/`, which `torb docs site --playground build/playground` copies into `assets/`:
#
#   torb.wasm, torb.wasm.gz  the toolchain with `std/` inside it, and the same gzipped for a server's gzip_static
#   torb.js                  emscripten's glue: a script that defines `createTorb`, for the worker and for node
#   playground.js            the page's half: `TorbPlayground.mount`, the light editor, Run, the output, the sharing,
#                            the gallery of /play
#   playground-worker.js     the worker that runs `torb` off the page's thread: a run, or `torb lsp` for the editor
#   playground-editor.js     CodeMirror and its language server client, loaded when an editor is about to be used;
#                            built by playground/editor/build.sh and committed, so nothing here needs npm
#   playground.css           the look of it, on the tokens of brand/tokens.css
#   examples/                the gallery's examples and their index.json, where playground/examples/ has them
#
# The C compiler is emscripten, pinned: `$TORB_EMCC`, or an `emcc` on the PATH of the pinned version, and otherwise
# the image `$TORB_EMSDK_IMAGE` through docker, which is how CI and the site image build it. Emscripten is chosen over
# wasi-sdk (RELEASE.md, "The playground, as built") because it has, in one toolchain, setjmp over WebAssembly exception
# handling, the pthread stubs a pool of one worker needs, the clock, randomness, an in-memory file system with `std/`
# embedded into the wasm, and 64-bit pointers on engines without memory64:
#
# The pointers are 64 bits wide (`-sMEMORY64=2`: wasm64 for clang, lowered to wasm32 by binaryen, so it runs on every
# engine): the VM keeps a value in 64-bit words laid out as the runtime's own structs (docs/design/VM.md section 2),
# which is true only where a pointer is a word.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS; the compile runs where emcc is, the rest anywhere.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "playground/build.sh: $*"
  exit 1
}

emsdk_version=6.0.10
image=${TORB_EMSDK_IMAGE:-emscripten/emsdk:$emsdk_version}

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/.." && pwd)
cd "$root"

output=build/playground

# ------------------------------------------------------------------------------------ the compile, where emcc is --

# `-sSUPPORT_LONGJMP=wasm` makes the setjmp of runtime/machine.c, task.c and panic.c WebAssembly exception handling
# rather than a JavaScript trampoline around every call in the functions that call it
common_flags="-sMEMORY64=2 -fwasm-exceptions -sSUPPORT_LONGJMP=wasm"
compile_flags="$common_flags -std=c11 -O2 -g0 -DTORB_HOSTS_MACHINE"

# What the link makes of the objects: a factory `createTorb` that instantiates a fresh toolchain each time it is called
# (the worker runs every program in a new one), stopped at the end of `main`, with a memory that grows to the 4 GiB of
# wasm32, the stack the checker's recursion needs, and `std/` as files below `/torb/std` - where `torb` finds it above
# the working directory `/torb/work` without a variable
link_flags="-sMODULARIZE=1 -sEXPORT_NAME=createTorb -sENVIRONMENT=web,worker,node -sINVOKE_RUN=0 -sEXIT_RUNTIME=1
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=16MB
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,ENV -sFORCE_FILESYSTEM=1 --embed-file build/playground-std/std@/torb/std"

compile_inside() {
  c_directory=$1
  emcc=${TORB_EMCC:-emcc}
  objects=build/playground-objects
  mkdir -p "$objects" "$output"
  # Objects of other flags or of another emcc are of no use
  identity="$compile_flags $("$emcc" --version | head -n 1)"
  if [ "$(cat "$objects/identity" 2>/dev/null)" != "$identity" ]; then
    rm -f "$objects"/*.o
    printf '%s\n' "$identity" >"$objects/identity"
  fi
  # One compile of a unit of the compiler's C takes up to 2 GiB: as many at a time as the memory holds
  jobs=${TORB_BUILD_JOBS:-}
  if [ -z "$jobs" ]; then
    processors=$(nproc 2>/dev/null || echo 4)
    memory=$(awk '/^MemTotal:/ { print int($2 / 2097152) }' /proc/meminfo 2>/dev/null || echo 4)
    jobs=$processors
    if [ "$memory" -lt "$jobs" ]; then
      jobs=$memory
    fi
    if [ "$jobs" -lt 1 ]; then
      jobs=1
    fi
  fi
  list="$objects/sources"
  : >"$list"
  for file in runtime/*.c runtime/os/*.c "$c_directory"/program-*.c; do
    printf '%s\n' "$file" >>"$list"
  done
  say "compiling $(wc -l <"$list") C files with $("$emcc" --version | head -n 1), $jobs at a time"
  # An object newer than its source and every header is kept: the C of the compiler changes with every commit, the
  # runtime seldom
  # shellcheck disable=SC2086
  if ! xargs -P "$jobs" -I '{}' sh -c '
    file=$1
    objects=$2
    headers=$3
    shift 3
    object="$objects/$(printf "%s" "$file" | tr "/" "_").o"
    if [ -f "$object" ] && [ -z "$(find "$file" $headers -newer "$object" 2>/dev/null)" ]; then
      exit 0
    fi
    "$@" -c "$file" -o "$object.part" && mv "$object.part" "$object"
  ' sh '{}' "$objects" "$c_directory/program.h runtime/include" "$emcc" $compile_flags \
    -I runtime/include -I "$c_directory" <"$list"; then
    fail "a C file did not compile"
  fi
  # The objects of this build only: a unit of an older, longer C must not be linked in
  set --
  while IFS= read -r file; do
    set -- "$@" "$objects/$(printf '%s' "$file" | tr '/' '_').o"
  done <"$list"
  # std/ as torb reads it, and nothing a build left inside it: a test of std/archive builds into its own build/
  rm -rf build/playground-std
  mkdir -p build/playground-std
  tar -cf - --exclude=build std | tar -xf - -C build/playground-std
  say "linking $output/torb.js and $output/torb.wasm"
  # shellcheck disable=SC2086
  "$emcc" $common_flags -O2 -g0 -o "$output/torb.js" "$@" $link_flags ${TORB_LINK_FLAGS-}
}

# ---------------------------------------------------------------------------------------------- the host's half --

if [ "${1-}" = "--inside" ]; then
  shift
  compile_inside "$1"
  exit 0
fi

c_directory=""
compiled=""
case "${1-}" in
  --c)
    [ $# -ge 2 ] || fail "--c needs the directory of the C"
    c_directory=$2
    ;;
  --compiled)
    [ $# -ge 2 ] || fail "--compiled needs the directory of torb.js and torb.wasm"
    compiled=$2
    ;;
esac

if [ -n "$compiled" ]; then
  # Compiled before from the same compiler/src, std/ and runtime/ - the one thing that makes this script slow - so
  # nothing is emitted or compiled, and the page's files below are written beside them as always
  for file in torb.js torb.wasm; do
    [ -f "$compiled/$file" ] || fail "there is no $file in $compiled"
  done
  mkdir -p "$output"
  if [ "$(CDPATH= cd -- "$compiled" && pwd)" != "$(CDPATH= cd -- "$output" && pwd)" ]; then
    cp "$compiled/torb.js" "$compiled/torb.wasm" "$output/"
  fi
  say "took torb.js and torb.wasm from $compiled"
else
  if [ -z "$c_directory" ]; then
    torb=${TORB-}
    for candidate in build/release/torb build/release/torb.exe; do
      if [ -z "${TORB-}" ] && [ -f "$candidate" ]; then
        torb=$candidate
      fi
    done
    [ -n "$torb" ] || fail "there is no build/release/torb: sh tools/bootstrap.sh builds it, or \$TORB names a torb"
    c_directory=$output/c
    rm -rf "$c_directory"
    mkdir -p "$c_directory"
    say "emitting the compiler's C for browser-wasm64"
    "$torb" build ./compiler --emit-c --target browser-wasm64 --output "$c_directory/torb" >&2
  fi

  [ -f "$c_directory/program.h" ] || fail "there is no program.h in $c_directory"

  emcc_found=""
  if [ -n "${TORB_EMCC-}" ]; then
    emcc_found=$TORB_EMCC
  elif command -v emcc >/dev/null 2>&1 && emcc --version 2>/dev/null | head -n 1 | grep -q " $emsdk_version "; then
    emcc_found=emcc
  fi

  if [ -n "$emcc_found" ]; then
    TORB_EMCC=$emcc_found compile_inside "$c_directory"
  else
    command -v docker >/dev/null 2>&1 || fail "neither emcc $emsdk_version nor docker is there to compile the C"
    say "compiling in $image"
    # The files belong to whoever runs this, and emscripten's cache of system libraries (the wasm64 ones are built on
    # first use) lives in build/, where that user can write it and the next build finds it
    user=""
    case "$(uname -s 2>/dev/null)" in
      Linux | Darwin | FreeBSD) user="--user $(id -u):$(id -g)" ;;
    esac
    # Git Bash rewrites an argument that starts with a slash into a Windows path; the container wants it as it is
    # shellcheck disable=SC2086
    MSYS_NO_PATHCONV=1 docker run --rm $user -v "$root:/src" -w /src -e EM_CACHE=/src/build/playground-cache \
      -e "TORB_BUILD_JOBS=${TORB_BUILD_JOBS-}" -e "TORB_LINK_FLAGS=${TORB_LINK_FLAGS-}" \
      "$image" sh playground/build.sh --inside "$c_directory"
  fi
fi

for file in playground.js playground-worker.js playground-editor.js playground.css; do
  cp "$here/$file" "$output/$file"
done

# The gallery's programs: index.json and the .trb files it names, not the .expected beside them (tools/gates.sh
# compares those, the site never reads them)
rm -rf "$output/examples"
mkdir -p "$output/examples"
cp "$here/examples"/*.trb "$output/examples/"
cp "$here/examples/index.json" "$output/examples/index.json"

gzip -9 -n -c "$output/torb.wasm" >"$output/torb.wasm.gz"

say "wrote $output/: torb.wasm $(wc -c <"$output/torb.wasm") bytes, $(wc -c <"$output/torb.wasm.gz") gzipped"
