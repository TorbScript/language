#!/bin/sh
# Renames whole words in every text file git tracks: sh tools/rename-word.sh <old>=<new>...
#
# The migration tool of a rename the checker cannot drive by itself - a trait or a type, named in code, in messages
# and tests, in prose and in the documentation alike (`sh tools/rename-word.sh Indexed=Index`). A field is renamed by
# `torb rename` instead, which asks the checker which declaration a name means.
#
# A word ends at anything that is not a letter, a digit or `_`, so `Indexed` is renamed and `IndexedList`,
# `isIndexed` and `indexed` are not. The line endings of a file are kept (GNU sed with `--binary`, which Windows needs
# so that a CRLF file stays one). Binary files are left alone, and so are `seed/`, the generated skill (`torb docs
# skill` writes it from `docs/`) and this script. Every file that changed is printed with how many words it had, and
# the run fails if one of the old words is still there afterwards. It is regenerable: run it again on a newer tree.
set -eu

if [ "$#" -eq 0 ]; then
  echo "usage: sh tools/rename-word.sh <old>=<new>..." >&2
  exit 2
fi

cd "$(git rev-parse --show-toplevel)"

for pair in "$@"; do
  old=${pair%%=*}
  new=${pair#*=}
  for word in "$old" "$new"; do
    case "$word" in
      "" | [0-9]* | *[!A-Za-z0-9_]*)
        echo "rename-word: \`$word\` in \`$pair\` is not a word of letters, digits and _" >&2
        exit 2
        ;;
    esac
  done
done

total=0
for pair in "$@"; do
  old=${pair%%=*}
  new=${pair#*=}
  files=$(git grep -l -I -w -e "$old" -- . ':!seed' ':!skills' ':!tools/rename-word.sh' || true)
  for file in $files; do
    count=$(grep -o -w -e "$old" "$file" | wc -l | tr -d ' ')
    sed --binary -i "s/\\b$old\\b/$new/g" "$file"
    echo "$file: $old -> $new ($count)"
    total=$((total + count))
  done
  if git grep -q -I -w -e "$old" -- . ':!seed' ':!skills' ':!tools/rename-word.sh'; then
    echo "rename-word: \`$old\` is still there after the rename:" >&2
    git grep -n -I -w -e "$old" -- . ':!seed' ':!skills' ':!tools/rename-word.sh' >&2
    exit 1
  fi
done
echo "renamed $total words"
