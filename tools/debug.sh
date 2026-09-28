#!/bin/sh
# The sessions of `tests/debug/`, each piped into `build/release/torb debug` over standard input with the framing of the
# Debug Adapter Protocol, and what the adapter writes to standard output compared with `<name>.expected`: one message
# per line, its JSON as the adapter wrote it, after the length of every message was checked against its
# `Content-Length`. `<name>.exit` is the exit code, `0` where it is missing.
#
# A session file has one request per line, JSON as a client would send it, where `{root}` stands for the directory the
# session runs in (forward slashes). Empty lines and lines starting with `#` are skipped. Every session runs in a
# directory of its own below a temporary one; a directory `tests/debug/<name>/` next to `<name>.dap` is copied into it
# first, and the adapter is started there.
#
# All requests of a session are written at once. The debuggee answers them in their order, and one that needs a
# stopped program waits until the program stops (docs/design/DEBUGGER.md section 9), so a session reads the way an
# editor would have sent it. The end of the input tells the debuggee that nothing more comes; the program then runs to
# its end without stopping again.
#
# What the output is compared as: the `seq` of every message is left out, consecutive `output` events of one category
# are joined into one, the directory of the session is written `{root}`, and the output of standard error follows the
# messages under a line `# stderr`, because the adapter reads the program's two streams from two pipes and their order
# against each other is the scheduler's.
#
# `$DEBUG_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/debug.sh
#   sh tools/debug.sh --filter breakpoint
#   sh tools/debug.sh --update        # rewrite .expected/.exit from what the sessions answer now

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

# The directory as a program on this machine names it: `C:/...` in Git Bash, `/tmp/...` elsewhere
native_path() {
  (cd "$1" && (pwd -W 2>/dev/null || pwd))
}

# The requests of a session file, framed, with `{root}` replaced
framed_session() {
  session=$1
  base=$2
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      '' | '#'*) continue ;;
    esac
    message=$(printf '%s' "$line" | sed "s|{root}|$base|g")
    length=$(printf '%s' "$message" | LC_ALL=C wc -c | tr -d ' ')
    printf 'Content-Length: %s\r\n\r\n%s' "$length" "$message"
  done <"$session"
}

# The messages of the adapter's standard output, one per line, each checked against its `Content-Length`
unframed() {
  LC_ALL=C awk '
    BEGIN { RS = "Content-Length: " }
    NR == 1 { if ($0 != "") print "stray output: " $0 ; next }
    {
      if (!match($0, /\r?\n\r?\n/)) { print "a header without its empty line: " $0 ; next }
      length_given = substr($0, 1, RSTART - 1) + 0
      body = substr($0, RSTART + RLENGTH)
      if (length(body) != length_given) print "Content-Length " length_given " for " length(body) " bytes: " body
      else print body
    }
  '
}

# The messages as they are compared: without `seq`, output events of one category in a row joined, standard error last
normalized() {
  LC_ALL=C awk '
    function flush() {
      if (pending != "") {
        print "{\"type\":\"event\",\"event\":\"output\",\"body\":{\"category\":\"stdout\",\"output\":\"" pending "\"}}"
        pending = ""
      }
    }
    {
      line = $0
      sub(/^\{"seq":[0-9]+,/, "{", line)
      if (match(line, /^\{"type":"event","event":"output","body":\{"category":"stderr","output":"/)) {
        rest = substr(line, RLENGTH + 1)
        sub(/"\}\}$/, "", rest)
        errors = errors rest
        next
      }
      if (match(line, /^\{"type":"event","event":"output","body":\{"category":"stdout","output":"/)) {
        rest = substr(line, RLENGTH + 1)
        sub(/"\}\}$/, "", rest)
        pending = pending rest
        next
      }
      flush()
      print line
    }
    END {
      flush()
      if (errors != "") {
        print "# stderr"
        print errors
      }
    }
  '
}

filter=""
update=0
while [ $# -gt 0 ]; do
  case "$1" in
    --filter)
      filter=$2
      shift 2
      ;;
    --update)
      update=1
      shift
      ;;
    *)
      say "usage: sh tools/debug.sh [--filter <part of a name>] [--update]"
      exit 2
      ;;
  esac
done

torb=$(binary_of "${DEBUG_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  say "debug.sh: no build/release/torb - run: sh tools/bootstrap.sh"
  exit 1
}
case "$torb" in
  /* | ?:*) ;;
  *) torb="$root/$torb" ;;
esac

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

passed=0
failed=0
for session in tests/debug/*.dap; do
  name=$(basename "$session" .dap)
  case "$name" in
    *"$filter"*) ;;
    *) continue ;;
  esac
  directory="$scratch/$name"
  mkdir -p "$directory"
  if [ -d "tests/debug/$name" ]; then
    cp -R "tests/debug/$name/." "$directory/"
  fi
  base=$(native_path "$directory")
  status=0
  framed_session "$session" "$base" >"$scratch/$name.in"
  (cd "$directory" && "$torb" debug <"$scratch/$name.in" >"$scratch/$name.raw" 2>"$scratch/$name.err") || status=$?
  # A path in a message is the directory's, however the program spelled it: forward slashes, or escaped backslashes
  escaped=$(printf '%s' "$base" | sed 's|/|\\\\\\\\|g')
  unframed <"$scratch/$name.raw" | sed -e "s|$base|{root}|g" -e "s|$escaped|{root}|g" | normalized >"$scratch/$name.out"
  expected="tests/debug/$name.expected"
  if [ "$update" -eq 1 ]; then
    cp "$scratch/$name.out" "$expected"
    if [ "$status" -ne 0 ]; then printf '%s\n' "$status" >"tests/debug/$name.exit"; else rm -f "tests/debug/$name.exit"; fi
    say "$name: written"
    continue
  fi
  expected_status=0
  if [ -f "tests/debug/$name.exit" ]; then
    expected_status=$(cat "tests/debug/$name.exit")
  fi
  problems=""
  if ! cmp -s "$scratch/$name.out" "$expected"; then
    problems="$problems standard output"
  fi
  if [ "$status" -ne "$expected_status" ]; then
    problems="$problems exit code ($status, expected $expected_status)"
  fi
  if [ -z "$problems" ]; then
    passed=$((passed + 1))
    continue
  fi
  failed=$((failed + 1))
  say "$name: differs in$problems"
  diff "$expected" "$scratch/$name.out" >&2 || true
  cat "$scratch/$name.err" >&2
done

if [ "$update" -eq 1 ]; then
  exit 0
fi
say "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
