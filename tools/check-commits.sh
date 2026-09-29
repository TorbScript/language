#!/bin/sh
# Checks that a range of commits is linear and follows Conventional Commits (compiler/CONTRIBUTING.md, "Repository
# Operations").
#
#   sh tools/check-commits.sh <base> [<head>]
#
# A merge commit is refused: main is linear, and a branch lands rebased or squashed.
set -eu

[ $# -ge 1 ] || { echo "usage: sh tools/check-commits.sh <base> [<head>]" >&2; exit 2; }
base=$1
head=${2:-HEAD}

types='feat|fix|perf|refactor|docs|test|build|ci|chore|revert'
pattern="^($types)(\([a-z0-9][a-z0-9/._-]*(,[a-z0-9][a-z0-9/._-]*)*\))?!?: [^ ].*"
limit=100

bad=0
for commit in $(git rev-list "$base..$head"); do
  subject=$(git log -1 --format=%s "$commit")
  short=$(git rev-parse --short "$commit")
  if [ "$(git rev-list --no-walk --count --merges "$commit")" -ne 0 ]; then
    echo "$short: a merge commit - main is linear, rebase or squash the branch: $subject" >&2
    bad=1
  elif ! printf '%s\n' "$subject" | grep -qE "$pattern"; then
    echo "$short: not a Conventional Commit: $subject" >&2
    bad=1
  elif [ "${#subject}" -gt "$limit" ]; then
    echo "$short: the subject has ${#subject} characters, at most $limit: $subject" >&2
    bad=1
  fi
done

if [ "$bad" -ne 0 ]; then
  echo "A subject is \`<type>(<scope>): <summary>\` with the type one of $(echo "$types" | tr '|' ' ')." >&2
  exit 1
fi
echo "every commit of $base..$head follows Conventional Commits"
