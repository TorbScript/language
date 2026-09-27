#!/bin/sh
# Lands finished branches on `main`: picks their commits onto a landing branch, builds it, runs both tiers, and - with
# `--publish` - moves `main` there, refreshes and publishes the seed, and pushes.
#
# The history is linear (`compiler/CONTRIBUTING.md`, "Repository Operations"): a branch lands by having its commits
# picked, never by a merge. Several branches land together, so the two tiers run once for all of them.
#
#   sh tools/land.sh prepare <name> <branch>...   # a worktree ../torbscript-<name> on main with every branch's commits
#   sh tools/land.sh continue <name>              # after resolving a conflict by hand: the rest of the picks
#   sh tools/land.sh build <name>                 # bootstrap, the generated files, and a commit for what they changed
#   sh tools/land.sh gates <name>                 # tier A and tier B side by side, on the landing's own gate slot
#   sh tools/land.sh publish <name>               # main -> the landing, the seed refreshed and published, pushed
#
# **Where `publish` pushes**: to the forge, the remote `forgejo` (ssh://git@git.torb.dev:2223/torbscript/language.git),
# whose push mirror carries every push on to GitHub. `$TORB_PUBLISH_REMOTES` names other remotes to push to as well,
# separated by spaces (`forgejo origin` pushes to GitHub directly too, for as long as the mirror is not set up). The
# seed goes to the forge's release `seeds` with `$TORB_FORGE_TOKEN`, an access token of the owner with the permission
# "repository: read and write" (tools/publish-seed.sh); `TORB_SEED_PUBLISH=0` skips that, and the nightly publishes
# the seed of main instead.
#
# **Generated files are never merged by hand.** A conflict in `runtime/machine_natives.c`,
# `runtime/include/torb_natives.h`, a generated docs index or the skill takes either side; `build` writes them again from
# the merged sources. `machine_natives.c` is compiled into `torb` itself, so a table that two branches both renumbered
# cannot build the compiler that would regenerate it: `build` then starts from main's table, regenerates it with that
# compiler, and builds once more.
#
# **The landing takes a third gate slot** (`TORB_GATE_SLOTS=3`) for `build` and for `gates`: agents' gates hold at most
# two, and the landing is what every agent waits for. `gates` is `tools/gates.sh ab`, both tiers side by side on that
# one slot; `build` has bootstrapped already, so the binary both tiers run is current before either starts.
#
# **A format needs no second bootstrap.** `build` formats the repository with the landing's own compiler after the
# bootstrap, and a file of `compiler/src` or `std/` it changes makes the binary older than its sources - which made
# tier A bootstrap a second time, ten to seventeen minutes for a change of layout. The formatter changes the layout and
# nothing else (its safety net drops any other edit), and what the compiler does with a program never depends on its
# own source positions: they are in the C it was built from only as the `file:line:column` of its own panic sites. So
# the binary of the bootstrap is the compiler of the formatted sources - every gate answers with it what it would answer
# with a second build, and the fixpoint of the formatted sources follows from the one the bootstrap proved - and `build`
# marks it current instead of building it again. What differs is `program.c`, which `publish` refreshes the seed from:
# it names the positions of before the format. To keep it the C of its commit in the common case, the seed - main's
# compiler, whose formatter is the landing's unless a branch changed it - formats the `.trb` files of `compiler/` and
# `std/` the picked commits touched before the first bootstrap, which is where a merge leaves a layout to change.
#
# `publish` reuses the landing's build for `main` - it is the same commit - so the seed is refreshed without a third
# bootstrap. It runs from the main checkout, the one that has `seed/`.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "land.sh: $*"
  exit 1
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

[ $# -ge 2 ] || fail "usage: sh tools/land.sh prepare|continue|build|gates|publish <name> [branch]..."
command=$1
name=$2
shift 2
landing="$root/../torbscript-$name"
branch="landing-$name"
seed_binary="$root/seed/torb.exe"
[ -f "$seed_binary" ] || seed_binary="$root/seed/torb"

generated='runtime/machine_natives.c runtime/include/torb_natives.h'

binary_in() {
  if [ -f "$1/build/release/torb.exe" ]; then
    printf '%s\n' "$1/build/release/torb.exe"
  elif [ -f "$1/build/release/torb" ]; then
    printf '%s\n' "$1/build/release/torb"
  fi
}

is_generated() {
  case "$1" in
    runtime/machine_natives.c | runtime/include/torb_natives.h | .claude/skills/*) return 0 ;;
    docs/*/index.md | docs/index.md) return 0 ;;
  esac
  return 1
}

# Picks until a conflict needs a person: a generated file takes the side being picked, anything else stops.
pick_until_done() {
  while :; do
    unmerged=$(git -C "$landing" diff --name-only --diff-filter=U)
    [ -n "$unmerged" ] || break
    manual=""
    for file in $unmerged; do
      if is_generated "$file"; then
        git -C "$landing" checkout --theirs -- "$file"
        git -C "$landing" add -- "$file"
      else
        manual="$manual $file"
      fi
    done
    if [ -n "$manual" ]; then
      say "resolve by hand in $landing, then: sh tools/land.sh continue $name"
      for file in $manual; do
        say "  $file"
      done
      exit 1
    fi
    if ! GIT_EDITOR=true git -C "$landing" cherry-pick --continue >/dev/null 2>&1; then
      # An empty pick (the change is on the landing already) is skipped
      if [ -z "$(git -C "$landing" diff --name-only --diff-filter=U)" ]; then
        git -C "$landing" cherry-pick --skip >/dev/null 2>&1 || true
      fi
    fi
  done
}

case "$command" in
  prepare)
    [ $# -ge 1 ] || fail "prepare needs at least one branch"
    [ ! -e "$landing" ] || fail "$landing exists already"
    git worktree add -q -b "$branch" "$landing" main
    for source in "$@"; do
      base=$(git merge-base main "$source")
      count=$(git rev-list --count "$base..$source")
      say "$source: $count commits"
      git -C "$landing" cherry-pick "$base..$source" >/dev/null 2>&1 || pick_until_done
    done
    say "prepared $landing ($(git -C "$landing" rev-list --count main..HEAD) commits); next: sh tools/land.sh build $name"
    ;;

  continue)
    git -C "$landing" add -A
    GIT_EDITOR=true git -C "$landing" cherry-pick --continue >/dev/null 2>&1 || true
    pick_until_done
    say "all picks applied; if branches are left, prepare picks them in order - pick the rest with git cherry-pick"
    ;;

  build)
    # The whole build on one slot, the third: the bootstraps inside it take none of their own
    if [ "${TORB_GATE_SLOT_HELD-}" != "1" ]; then
      exec env TORB_GATE_SLOTS=3 sh "$root/tools/gate-slot.sh" sh "$root/tools/land.sh" build "$name"
    fi
    cd "$landing"
    export TORB_SEED="${TORB_SEED:-$seed_binary}"
    # The layout of what the picks touched, with the seed's formatter, before anything is built from it (see above);
    # a file the seed cannot read yet (new syntax) is left for the landing's own formatter
    touched=$(git diff --name-only --diff-filter=AMR main...HEAD -- compiler std | grep '\.trb$' || true)
    if [ -n "$touched" ] && [ -f "$TORB_SEED" ]; then
      # shellcheck disable=SC2086
      "$TORB_SEED" format $touched >/dev/null 2>&1 || true
    fi
    if ! sh tools/bootstrap.sh; then
      say "the bootstrap failed; building once more from main's natives table"
      for file in $generated; do
        git show "main:$file" >"$file"
      done
      rm -rf build/staging-*
      sh tools/bootstrap.sh || fail "the bootstrap fails with main's natives table too"
    fi
    torb=$(binary_in "$landing")
    (cd compiler && "$torb" natives --header)
    if [ -n "$(git status --porcelain -- $generated)" ]; then
      say "the natives table changed: building once more with it"
      sh tools/bootstrap.sh
      torb=$(binary_in "$landing")
    fi
    "$torb" docs index docs >/dev/null
    "$torb" docs skill docs .claude/skills/torbscript >/dev/null
    "$torb" format . >/dev/null
    if [ -n "$(git status --porcelain)" ]; then
      git add -A
      git commit -q -m "chore: regenerate the natives table, the docs indexes and the skill, and format, after the landing"
      say "committed what the generated files and the formatter changed"
    fi
    # The format changed a file the compiler is built from, and only its layout: the binary is the compiler of these
    # sources (see above), so it is marked current - `fixpoint` last, newer than the binary, as the bootstrap leaves it
    newer=$(find compiler/src compiler/project.trb std runtime project.trb -name build -prune -o -type f \
      \( -name '*.trb' -o -name '*.c' -o -name '*.h' \) -newer "$torb" -print 2>/dev/null | head -n 1)
    if [ -n "$newer" ]; then
      say "the format changed the layout of $newer and more: the binary is theirs, and is marked current"
      for file in "$torb" build/release/program.c build/release/program.hash build/release/fixpoint; do
        if [ -f "$file" ]; then
          touch "$file"
        fi
      done
    fi
    say "built; next: sh tools/land.sh gates $name"
    ;;

  gates)
    cd "$landing"
    export TORB_SEED="${TORB_SEED:-$seed_binary}" TORB_GATE_SLOTS=3
    sh tools/gates.sh ab
    say "both tiers are green; next: sh tools/land.sh publish $name"
    ;;

  publish)
    [ -z "$(git status --porcelain -- compiler std runtime project.trb)" ] || fail "the main checkout has changes"
    remotes=${TORB_PUBLISH_REMOTES:-forgejo}
    for remote in $remotes; do
      git remote get-url "$remote" >/dev/null 2>&1 ||
        fail "there is no remote \`$remote\`: git remote add forgejo ssh://git@git.torb.dev:2223/torbscript/language.git"
    done
    if [ "${TORB_SEED_PUBLISH-1}" != "0" ] && [ -z "${TORB_FORGE_TOKEN-}" ]; then
      fail "publishing the seed needs TORB_FORGE_TOKEN (a token of git.torb.dev that may write the releases), or TORB_SEED_PUBLISH=0"
    fi
    git merge --ff-only "$branch"
    [ "$(git rev-parse HEAD)" = "$(git -C "$landing" rev-parse HEAD)" ] || fail "main is not the landing's commit"
    mkdir -p build/release
    for file in "$(binary_in "$landing")" "$landing/build/release/program.c" "$landing/build/release/program.hash"; do
      cp "$file" build/release/
    done
    : >build/release/fixpoint
    first=${remotes%% *}
    git fetch -q "$first" main
    sh tools/check-commits.sh FETCH_HEAD
    sh tools/refresh-seed.sh
    if [ "${TORB_SEED_PUBLISH-1}" != "0" ]; then
      archive=$(sh tools/pack-seed.sh seed)
      sh tools/publish-seed.sh "$archive"
    else
      say "TORB_SEED_PUBLISH=0: the seed is not published; the nightly publishes the seed of main"
    fi
    for remote in $remotes; do
      git push "$remote" main
    done
    say "published: main is $(git rev-parse --short HEAD) on $remotes; remove the landing with: git worktree remove $landing"
    ;;

  *)
    fail "unknown command \`$command\`: prepare, continue, build, gates or publish"
    ;;
esac
