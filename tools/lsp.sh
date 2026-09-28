#!/bin/sh
# The sessions of `tests/lsp/`, each piped into `build/release/torb lsp` over standard input with the framing of the
# Language Server Protocol, and what the server writes to standard output compared with `<name>.expected`: one
# message per line, its JSON as the server wrote it, after the length of every message was checked against its
# `Content-Length`. `<name>.exit` is the exit code, `0` where it is missing. Standard error is not compared: it says
# how long every run of the checker took.
#
# A session file has one message per line, JSON as a client would send it, where `{root}` stands for the URI of the
# directory the session runs in. `open <file>` is a line of its own that sends `textDocument/didOpen` for that file of
# the session's directory, with its text. Empty lines and lines starting with `#` are skipped. Every session runs in a
# directory of its own below a temporary one; a directory `tests/lsp/<name>/` next to `<name>.lsp` is copied into it
# first, and that directory is the root the session's `initialize` names.
#
# All messages of a session are written at once, so the server may read several of them as one batch. A batch checks
# a changed document once at its end, or before a request that needs it - so a session follows every change with a
# request, and what it expects does not depend on where the reads of the pipe end.
#
# `await <n>` is a line of its own that holds the rest of the session back until the server has written `<n>` messages
# in all: what a session writes that tests the work the server does between messages - the check of the files nobody
# opened - which runs only while no message waits. Such a session is written through a pipe, one part per `await`, and
# a part that waits longer than two minutes for its messages is sent anyway.
#
# `$LSP_TORB` names another `torb` to drive. POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.
#
#   sh tools/lsp.sh
#   sh tools/lsp.sh --filter hover
#   sh tools/lsp.sh --update        # rewrite .expected/.exit from what the sessions answer now

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

# The directory as the `file:` URI a client would name it by: `file:///C:/...` in Git Bash, `file:///tmp/...` elsewhere
uri_of() {
  directory=$(cd "$1" && (pwd -W 2>/dev/null || pwd))
  case "$directory" in
    /*) printf 'file://%s\n' "$directory" ;;
    *) printf 'file:///%s\n' "$directory" ;;
  esac
}

# A file's text as a JSON string
json_text() {
  LC_ALL=C awk '
    BEGIN { ORS = "" ; print "\"" }
    {
      gsub(/\\/, "\\\\")
      gsub(/"/, "\\\"")
      gsub(/\t/, "\\t")
      gsub(/\r/, "\\r")
      if (NR > 1) print "\\n"
      print
    }
    END { print "\\n\"" }
  ' "$1"
}

# The messages of a session file, framed, with `{root}` replaced and every `open` expanded. With a fourth argument, only
# the part of the session after that many `await` lines, up to the next one
framed_session() {
  session=$1
  directory=$2
  base=$3
  part=${4:-}
  awaited=0
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      'await '*)
        awaited=$((awaited + 1))
        continue
        ;;
    esac
    if [ -n "$part" ] && [ "$awaited" -ne "$part" ]; then
      continue
    fi
    case "$line" in
      '' | '#'*) continue ;;
      'open '*)
        file=${line#open }
        text=$(json_text "$directory/$file")
        line="{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"{root}/$file\",\"languageId\":\"trb\",\"version\":1,\"text\":$text}}}"
        ;;
    esac
    message=$(printf '%s' "$line" | sed "s|{root}|$base|g")
    length=$(printf '%s' "$message" | LC_ALL=C wc -c | tr -d ' ')
    printf 'Content-Length: %s\r\n\r\n%s' "$length" "$message"
  done <"$session"
}

# The counts of messages the `await` lines of a session wait for, one per line
awaited_counts() {
  sed -n 's/^await \([0-9][0-9]*\)$/\1/p' "$1"
}

# How many messages the server wrote to a file so far
messages_in() {
  if [ -f "$1" ]; then
    LC_ALL=C grep -o 'Content-Length:' "$1" | wc -l | tr -d ' '
  else
    printf '0\n'
  fi
}

# The parts of a session, each after the server has written the messages the `await` in front of it waits for
session_parts() {
  session=$1
  directory=$2
  base=$3
  raw=$4
  framed_session "$session" "$directory" "$base" 0
  part=0
  for count in $(awaited_counts "$session"); do
    part=$((part + 1))
    waited=0
    while [ "$(messages_in "$raw")" -lt "$count" ] && [ "$waited" -lt 1200 ]; do
      sleep 0.1
      waited=$((waited + 1))
    done
    framed_session "$session" "$directory" "$base" "$part"
  done
}

# The messages of the server's standard output, one per line, each checked against its `Content-Length`. The header
# ends in `\r\n\r\n`; an `awk` that reads in text mode (Git Bash) hands the `\r`s over as nothing, which the pattern
# allows, and a body never holds a line break of its own.
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
      say "usage: sh tools/lsp.sh [--filter <part of a name>] [--update]"
      exit 2
      ;;
  esac
done

torb=$(binary_of "${LSP_TORB:-$root/build/release/torb}")
[ -n "$torb" ] || {
  say "lsp.sh: no build/release/torb - run: sh tools/bootstrap.sh"
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
for session in tests/lsp/*.lsp; do
  name=$(basename "$session" .lsp)
  case "$name" in
    *"$filter"*) ;;
    *) continue ;;
  esac
  directory="$scratch/$name"
  mkdir -p "$directory"
  if [ -d "tests/lsp/$name" ]; then
    cp -R "tests/lsp/$name/." "$directory/"
  fi
  base=$(uri_of "$directory")
  status=0
  if [ -z "$(awaited_counts "$session")" ]; then
    framed_session "$session" "$directory" "$base" >"$scratch/$name.in"
    (cd "$directory" && "$torb" lsp <"$scratch/$name.in" >"$scratch/$name.raw" 2>"$scratch/$name.err") || status=$?
  else
    : >"$scratch/$name.raw"
    session_parts "$session" "$directory" "$base" "$scratch/$name.raw" |
      (cd "$directory" && "$torb" lsp >"$scratch/$name.raw" 2>"$scratch/$name.err") || status=$?
  fi
  unframed <"$scratch/$name.raw" | sed "s|$base|{root}|g" >"$scratch/$name.out"
  expected="tests/lsp/$name.expected"
  if [ "$update" -eq 1 ]; then
    cp "$scratch/$name.out" "$expected"
    if [ "$status" -ne 0 ]; then printf '%s\n' "$status" >"tests/lsp/$name.exit"; else rm -f "tests/lsp/$name.exit"; fi
    say "$name: written"
    continue
  fi
  expected_status=0
  if [ -f "tests/lsp/$name.exit" ]; then
    expected_status=$(cat "tests/lsp/$name.exit")
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
