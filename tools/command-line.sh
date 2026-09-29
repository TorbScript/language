#!/bin/sh
# The command line of `torb` where it is wrong: every subcommand that takes paths or the name of a program refuses an
# argument that starts with `--` and is none of its flags, before it reads or builds anything, rather than looking for a
# file or a program of that name; `--color` belongs to every one of them, and a value it does not know is refused the
# same way (docs/tooling/the-torb-command.md). Each line of the table below is run once, from the repository root, and
# what it writes to both streams plus a last line `exit <code>` is compared with `tests/command-line/arguments.expected`.
# `torb --help` is run for its exit code alone: the usage it prints changes with every subcommand. `--help` and `-h` of
# every subcommand are run in an empty directory of their own, for whether they print that command's usage.
#
# `$COMMAND_LINE_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/command-line.sh
#   sh tools/command-line.sh --update     # rewrite the expected file from what the lines answer now

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

say() {
  printf '%s\n' "$*" >&2
}

binary_of() {
  if [ -f "$1" ]; then
    printf '%s\n' "$1"
  elif [ -f "$1.exe" ]; then
    printf '%s\n' "$1.exe"
  fi
  return 0
}

update=0
case "${1-}" in
  '') ;;
  --update) update=1 ;;
  *)
    say "usage: sh tools/command-line.sh [--update]"
    exit 2
    ;;
esac

torb=$(binary_of "${COMMAND_LINE_TORB:-build/release/torb}")
[ -n "$torb" ] || {
  say "command-line.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}
# Absolute, because the runs of `--help` below start in a directory of their own
case "$torb" in
  /* | [A-Za-z]:*) ;;
  *) torb="$root/$torb" ;;
esac

expected=tests/command-line/arguments.expected
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

: >"$scratch/arguments.out"
while IFS= read -r line; do
  [ -n "$line" ] || continue
  status=0
  # shellcheck disable=SC2086
  "$torb" $line >"$scratch/answer" 2>&1 || status=$?
  {
    printf '$ torb %s\n' "$line"
    cat "$scratch/answer"
    printf 'exit %s\n' "$status"
  } >>"$scratch/arguments.out"
done <<'EOF'
check --frobnicate
check --statistics tests/test-report/suite --frobnicate
manifest --frobnicate
parse --frobnicate
build --frobnicate
build --emit-c --output build/command-line --frobnicate
ir --frobnicate
run --frobnicate
run --native --frobnicate tests/test-report/suite
test --frobnicate
check --color purple
check --color
--color=never check --frobnicate
test --color always --frobnicate
debug --frobnicate
new --frobnicate
new somewhere --frobnicate
new somewhere --ci bogus
new somewhere --license bogus
new somewhere --template
init --frobnicate
init somewhere
upgrade --frobnicate
repl --frobnicate
EOF
status=0
"$torb" --help >/dev/null 2>&1 || status=$?
printf '$ torb --help\nexit %s\n' "$status" >>"$scratch/arguments.out"

# `--help` and `-h` of every subcommand print that command's usage on standard output and run nothing: each is run in
# an empty directory, which it has to leave empty (`torb new --help` once made a package called `--help`). What is
# recorded is the exit code and whether the output names the command, not the text, which changes with every flag.
mkdir "$scratch/work"
for command in check build run test new init upgrade add remove update install publish lock repl manifest ir natives \
  parse tokens ast highlight format canon lint rename lsp debug doc docs; do
  # `torb canon` is the deprecated name of `torb format`, and answers with its usage
  named=$command
  [ "$command" = canon ] && named=format
  for flag in --help -h; do
    status=0
    (cd "$scratch/work" && "$torb" "$command" "$flag") >"$scratch/answer" 2>"$scratch/errors" || status=$?
    {
      printf '$ torb %s %s\n' "$command" "$flag"
      if grep -q "torb $named" "$scratch/answer" && [ ! -s "$scratch/errors" ]; then
        printf 'the usage of torb %s\n' "$named"
      else
        cat "$scratch/answer" "$scratch/errors"
      fi
      printf 'exit %s\n' "$status"
    } >>"$scratch/arguments.out"
  done
done
if [ -n "$(ls -A "$scratch/work")" ]; then
  printf 'left behind: %s\n' "$(ls -A "$scratch/work" | tr '\n' ' ')" >>"$scratch/arguments.out"
fi

if [ "$update" -eq 1 ]; then
  mkdir -p "$(dirname "$expected")"
  cp "$scratch/arguments.out" "$expected"
  say "$expected: written"
  exit 0
fi
if cmp -s "$scratch/arguments.out" "$expected"; then
  say "the command line: as expected"
  exit 0
fi
say "the command line: differs from $expected"
diff "$expected" "$scratch/arguments.out" >&2 || true
exit 1
