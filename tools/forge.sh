#!/bin/sh
# The releases of the repository on its forge, over the forge's REST API with curl - what `tools/publish-seed.sh`,
# `tools/migrate-seeds.sh` and the workflows of `.forgejo/workflows/` call to create, fill, publish and delete a
# release. The forge is Forgejo at git.torb.dev (docs/design/RELEASE.md, "The forge"); GitHub, which mirrors it,
# speaks nearly the same API, and the differences between the two are here and nowhere else.
#
#   sh tools/forge.sh release <tag>                 the release of the tag, flattened (below); exit 2 when there is none
#   sh tools/forge.sh create <tag> <commit> <title> <notes file> [--draft] [--prerelease]
#                                                   a release, and the tag at <commit> (Forgejo: once it is published)
#   sh tools/forge.sh upload <tag> <file>...        each file as an asset of the release; one of the same name is replaced
#   sh tools/forge.sh assets <tag>                  the names of the release's assets, one per line
#   sh tools/forge.sh download <tag> <asset> <file> one asset into a file
#   sh tools/forge.sh publish <tag>                 a draft made public: from here on the webhook "published" is sent
#   sh tools/forge.sh delete <tag>                  the release and its tag
#   sh tools/forge.sh releases                      "<tag> <draft|prerelease|release>" for every release, newest first
#   sh tools/forge.sh tags [prefix]                 "<tag> <commit>" for every tag that starts with the prefix
#   sh tools/forge.sh url <tag> <asset>             the public download URL of an asset
#   sh tools/forge.sh flatten                       the JSON on standard input as "<path><TAB><value>" lines
#
# A flattened document is one line per value: `tag_name<TAB>seeds`, `assets.0.name<TAB>seeds.txt`, `assets.0.id<TAB>7`.
# A string's `\"`, `\\` and `\/` are read; every other escape stays as it was written, so a value is always one line.
#
# Variables:
#   TORB_FORGE              forgejo (the default) or github
#   TORB_FORGE_URL          the forge: https://git.torb.dev for Forgejo, https://github.com for GitHub
#   TORB_FORGE_REPOSITORY   owner/name: torbscript/language on Forgejo, TorbScript/language on GitHub
#   TORB_FORGE_TOKEN        what writes, and reads a private repository. On Forgejo an access token with the
#                           permission "repository: read and write", or the JWT a workflow gets from an Authorized
#                           Integration (.forgejo/workflows/, "the token"); on GitHub a token with `contents: write`,
#                           and GH_TOKEN or GITHUB_TOKEN are read when it is not set.
#
# The token never appears on a command line: curl reads the header from a file only this user can read. Needs curl,
# awk and sed - no `gh`, no `jq`, no `torb`.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

say() {
  printf '%s\n' "$*" >&2
}

fail() {
  say "forge.sh: $*"
  exit 1
}

forge=${TORB_FORGE:-forgejo}
case "$forge" in
  forgejo)
    url=${TORB_FORGE_URL:-https://git.torb.dev}
    repository=${TORB_FORGE_REPOSITORY:-torbscript/language}
    token=${TORB_FORGE_TOKEN-}
    api="${url%/}/api/v1/repos/$repository"
    ;;
  github)
    url=${TORB_FORGE_URL:-https://github.com}
    repository=${TORB_FORGE_REPOSITORY:-TorbScript/language}
    token=${TORB_FORGE_TOKEN:-${GH_TOKEN:-${GITHUB_TOKEN-}}}
    api="https://api.github.com/repos/$repository"
    ;;
  *) fail "TORB_FORGE is forgejo or github, not $forge" ;;
esac
url=${url%/}

# A token is only ever sent over HTTPS, or to this machine for a test
case "$url" in
  https://* | http://127.0.0.1* | http://localhost*) ;;
  *) fail "the forge is reached over https://, and $url is not" ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
umask 077

# ----------------------------------------------------------------------------- JSON --------------------------------

# The JSON on standard input as "<path><TAB><value>", one line per string, number, true, false and null
flatten() {
  awk '
    function path(    p, d) {
      p = ""
      for (d = 1; d <= depth; d++) {
        p = (p == "" ? key[d] : p "." key[d])
      }
      return p
    }
    { text = text $0 "\n" }
    END {
      n = length(text)
      depth = 0
      awaitingKey = 0
      i = 1
      while (i <= n) {
        c = substr(text, i, 1)
        if (c == "{" || c == "[") {
          depth++
          kind[depth] = c
          position[depth] = 0
          key[depth] = (c == "[" ? 0 : "")
          awaitingKey = (c == "{")
          i++
        } else if (c == "}" || c == "]") {
          depth--
          awaitingKey = 0
          i++
        } else if (c == ",") {
          if (kind[depth] == "[") {
            position[depth]++
            key[depth] = position[depth]
          } else {
            awaitingKey = 1
          }
          i++
        } else if (c == ":") {
          awaitingKey = 0
          i++
        } else if (c == "\"") {
          value = ""
          j = i + 1
          while (j <= n) {
            d = substr(text, j, 1)
            if (d == "\\") {
              e = substr(text, j + 1, 1)
              if (e == "\"" || e == "\\" || e == "/") {
                value = value e
              } else {
                value = value "\\" e
              }
              j += 2
            } else if (d == "\"") {
              break
            } else {
              value = value d
              j++
            }
          }
          i = j + 1
          if (depth > 0 && kind[depth] == "{" && awaitingKey) {
            key[depth] = value
          } else {
            print path() "\t" value
          }
        } else if (c ~ /[ \t\r\n]/) {
          i++
        } else {
          value = ""
          while (i <= n) {
            c = substr(text, i, 1)
            if (c ~ /[,\]} \t\r\n]/) {
              break
            }
            value = value c
            i++
          }
          print path() "\t" value
        }
      }
    }
  '
}

# The value at one path of a flattened document on standard input
value_of() {
  awk -F '\t' -v wanted="$1" '$1 == wanted { sub(/^[^\t]*\t/, ""); print; exit }'
}

# A text as a JSON string, quotes included: the backslash, the quote and the control characters escaped
json_string() {
  printf '%s' "$1" | awk '
    BEGIN { ORS = "" }
    {
      line = $0
      gsub(/\\/, "\\\\", line)
      gsub(/"/, "\\\"", line)
      gsub(/\t/, "\\t", line)
      gsub(/\r/, "\\r", line)
      text = (NR == 1 ? line : text "\\n" line)
    }
    END { print "\"" text "\"" }
  '
}

# ----------------------------------------------------------------------------- HTTP --------------------------------

command -v curl >/dev/null 2>&1 || fail "curl is needed to talk to the forge"

# The headers of every request: the token, from a file and never from the command line
headers="$work/headers"
: >"$headers"
if [ -n "$token" ]; then
  printf 'Authorization: Bearer %s\n' "$token" >>"$headers"
fi
printf '%s\n' "User-Agent: torbscript-forge" >>"$headers"
if [ "$forge" = "github" ]; then
  printf '%s\n' "Accept: application/vnd.github+json" "X-GitHub-Api-Version: 2022-11-28" >>"$headers"
fi

# request <method> <url> <answer file> [curl arguments...]: prints the HTTP status, the answer goes to the file
request() {
  method=$1
  target=$2
  answer=$3
  shift 3
  curl -sS --retry 3 --retry-delay 2 -X "$method" -H @"$headers" -o "$answer" -w '%{http_code}' "$@" "$target" ||
    fail "$method $target could not be sent"
}

# expect <status> <answer file> <wanted>...: fails with the answer when the status is none of the wanted ones
expect() {
  got=$1
  answered=$2
  shift 2
  for wanted in "$@"; do
    [ "$got" = "$wanted" ] && return 0
  done
  say "forge.sh: the forge answered $got:"
  head -c 2000 "$answered" >&2 || true
  say ""
  exit 1
}

needs_token() {
  [ -n "$token" ] || fail "$1 needs TORB_FORGE_TOKEN: a token that may write the releases of $repository"
}

# ----------------------------------------------------------------------------- releases ----------------------------

# The release of a tag, flattened, into $work/release; answers 1 when there is none
find_release() {
  status=$(request GET "$api/releases/tags/$1" "$work/answer")
  if [ "$status" = "200" ]; then
    flatten <"$work/answer" >"$work/release"
    return 0
  fi
  [ "$status" = "404" ] || expect "$status" "$work/answer" 200
  # A draft of GitHub has no tag yet and is found only in the list
  if [ "$forge" = "github" ]; then
    status=$(request GET "$api/releases?per_page=100" "$work/answer")
    expect "$status" "$work/answer" 200
    flatten <"$work/answer" | awk -F '\t' -v tag="$1" '
      { split($1, parts, "."); index_ = parts[1]; rest = substr($1, length(index_) + 2) }
      rest == "tag_name" && $2 == tag { found = index_ }
      { lines[NR] = $0; owner[NR] = index_; field[NR] = rest }
      END {
        if (found == "") { exit 1 }
        for (line = 1; line <= NR; line++) {
          if (owner[line] == found) { sub(/^[^\t]*\t/, "", lines[line]); print field[line] "\t" lines[line] }
        }
      }' >"$work/release" && return 0
  fi
  return 1
}

release_id() {
  find_release "$1" || fail "$repository has no release \`$1\`"
  value_of id <"$work/release"
}

# The id of the asset named $2 in the flattened release $1
asset_id() {
  awk -F '\t' -v wanted="$2" '
    $1 ~ /^assets\.[0-9]+\.id$/ { split($1, parts, "."); id[parts[2]] = $2 }
    $1 ~ /^assets\.[0-9]+\.name$/ { split($1, parts, "."); name[parts[2]] = $2 }
    END { for (k in name) { if (name[k] == wanted) { print id[k]; exit } } }
  ' "$1"
}

asset_names() {
  awk -F '\t' '$1 ~ /^assets\.[0-9]+\.name$/ { sub(/^[^\t]*\t/, ""); print }' "$1"
}

public_url() {
  printf '%s/%s/releases/download/%s/%s\n' "$url" "$repository" "$1" "$2"
}

[ $# -ge 1 ] || fail "usage: sh tools/forge.sh release|create|upload|assets|download|publish|delete|releases|tags|url|flatten ..."
command=$1
shift

case "$command" in
  flatten)
    flatten
    ;;

  url)
    [ $# -eq 2 ] || fail "usage: sh tools/forge.sh url <tag> <asset>"
    public_url "$1" "$2"
    ;;

  release)
    [ $# -eq 1 ] || fail "usage: sh tools/forge.sh release <tag>"
    if find_release "$1"; then
      cat "$work/release"
    else
      exit 2
    fi
    ;;

  assets)
    [ $# -eq 1 ] || fail "usage: sh tools/forge.sh assets <tag>"
    find_release "$1" || fail "$repository has no release \`$1\`"
    asset_names "$work/release"
    ;;

  create)
    [ $# -ge 4 ] || fail "usage: sh tools/forge.sh create <tag> <commit> <title> <notes file> [--draft] [--prerelease]"
    needs_token "creating a release"
    tag=$1
    commit=$2
    title=$3
    notes=$4
    shift 4
    draft=false
    prerelease=false
    for option in "$@"; do
      case "$option" in
        --draft) draft=true ;;
        --prerelease) prerelease=true ;;
        *) fail "create takes --draft and --prerelease, not $option" ;;
      esac
    done
    [ -f "$notes" ] || fail "there is no notes file $notes"
    body=$(json_string "$(cat "$notes")")
    {
      printf '{"tag_name":%s,"target_commitish":%s,"name":%s,"body":%s,"draft":%s,"prerelease":%s' \
        "$(json_string "$tag")" "$(json_string "$commit")" "$(json_string "$title")" "$body" "$draft" "$prerelease"
      # A prerelease is never GitHub's "latest" release; Forgejo's latest is the newest one that is not a prerelease
      if [ "$forge" = "github" ] && [ "$prerelease" = "true" ]; then
        printf ',"make_latest":"false"'
      fi
      printf '}'
    } >"$work/request.json"
    status=$(request POST "$api/releases" "$work/answer" -H "Content-Type: application/json" --data-binary @"$work/request.json")
    expect "$status" "$work/answer" 201
    flatten <"$work/answer" | value_of id
    ;;

  upload)
    [ $# -ge 2 ] || fail "usage: sh tools/forge.sh upload <tag> <file>..."
    needs_token "uploading to a release"
    tag=$1
    shift
    id=$(release_id "$tag")
    for file in "$@"; do
      [ -f "$file" ] || fail "there is no file $file"
      name=$(basename "$file")
      # Replacing is deleting first: neither forge overwrites an asset of the same name
      find_release "$tag" || fail "the release \`$tag\` disappeared"
      existing=$(asset_id "$work/release" "$name")
      if [ -n "$existing" ]; then
        if [ "$forge" = "github" ]; then
          status=$(request DELETE "$api/releases/assets/$existing" "$work/answer")
        else
          status=$(request DELETE "$api/releases/$id/assets/$existing" "$work/answer")
        fi
        expect "$status" "$work/answer" 204
      fi
      if [ "$forge" = "github" ]; then
        status=$(request POST "https://uploads.github.com/repos/$repository/releases/$id/assets?name=$name" \
          "$work/answer" -H "Content-Type: application/octet-stream" --data-binary @"$file")
      else
        status=$(request POST "$api/releases/$id/assets?name=$name" "$work/answer" -F "attachment=@$file")
      fi
      expect "$status" "$work/answer" 201
      say "uploaded $name to the release \`$tag\` of $repository"
    done
    ;;

  download)
    [ $# -eq 3 ] || fail "usage: sh tools/forge.sh download <tag> <asset> <file>"
    tag=$1
    asset=$2
    file=$3
    if [ -z "$token" ]; then
      # A public repository: the download URL, the same one a browser and tools/fetch-seed.sh use
      status=$(request GET "$(public_url "$tag" "$asset")" "$file" -L)
      expect "$status" "$file" 200
    elif [ "$forge" = "github" ]; then
      # A private repository of GitHub answers its download URL with a login page: the asset through the API
      find_release "$tag" || fail "$repository has no release \`$tag\`"
      existing=$(asset_id "$work/release" "$asset")
      [ -n "$existing" ] || fail "the release \`$tag\` of $repository has no asset \`$asset\`"
      status=$(request GET "$api/releases/assets/$existing" "$file" -L -H "Accept: application/octet-stream")
      expect "$status" "$file" 200
    else
      status=$(request GET "$(public_url "$tag" "$asset")" "$file" -L)
      expect "$status" "$file" 200
    fi
    ;;

  publish)
    [ $# -eq 1 ] || fail "usage: sh tools/forge.sh publish <tag>"
    needs_token "publishing a release"
    id=$(release_id "$1")
    printf '%s' '{"draft":false}' >"$work/request.json"
    status=$(request PATCH "$api/releases/$id" "$work/answer" -H "Content-Type: application/json" --data-binary @"$work/request.json")
    expect "$status" "$work/answer" 200
    say "published the release \`$1\` of $repository"
    ;;

  delete)
    [ $# -eq 1 ] || fail "usage: sh tools/forge.sh delete <tag>"
    needs_token "deleting a release"
    if find_release "$1"; then
      id=$(value_of id <"$work/release")
      status=$(request DELETE "$api/releases/$id" "$work/answer")
      expect "$status" "$work/answer" 204
    fi
    if [ "$forge" = "github" ]; then
      status=$(request DELETE "$api/git/refs/tags/$1" "$work/answer")
      expect "$status" "$work/answer" 204 404 422
    else
      status=$(request DELETE "$api/tags/$1" "$work/answer")
      expect "$status" "$work/answer" 204 404
    fi
    say "deleted the release and the tag \`$1\` of $repository"
    ;;

  releases)
    page=1
    : >"$work/all"
    while :; do
      if [ "$forge" = "github" ]; then
        status=$(request GET "$api/releases?per_page=100&page=$page" "$work/answer")
      else
        status=$(request GET "$api/releases?limit=50&page=$page" "$work/answer")
      fi
      expect "$status" "$work/answer" 200
      flatten <"$work/answer" >"$work/page"
      [ -s "$work/page" ] || break
      awk -F '\t' '
        { split($1, parts, "."); index_ = parts[1]; field = substr($1, length(index_) + 2) }
        field == "tag_name" { tag[index_] = $2; if (!(index_ in seen)) { seen[index_] = 1; order[++count] = index_ } }
        field == "draft" { draft[index_] = $2 }
        field == "prerelease" { prerelease[index_] = $2 }
        END {
          for (k = 1; k <= count; k++) {
            i = order[k]
            kind = (draft[i] == "true" ? "draft" : (prerelease[i] == "true" ? "prerelease" : "release"))
            print tag[i] " " kind
          }
        }' "$work/page" >>"$work/all"
      page=$((page + 1))
    done
    cat "$work/all"
    ;;

  tags)
    prefix=${1-}
    page=1
    while :; do
      if [ "$forge" = "github" ]; then
        status=$(request GET "$api/tags?per_page=100&page=$page" "$work/answer")
      else
        status=$(request GET "$api/tags?limit=50&page=$page" "$work/answer")
      fi
      expect "$status" "$work/answer" 200
      flatten <"$work/answer" >"$work/page"
      [ -s "$work/page" ] || break
      awk -F '\t' -v prefix="$prefix" '
        { split($1, parts, "."); index_ = parts[1]; field = substr($1, length(index_) + 2) }
        field == "name" { name[index_] = $2; order[++count] = index_ }
        field == "commit.sha" { commit[index_] = $2 }
        END {
          for (k = 1; k <= count; k++) {
            i = order[k]
            if (index(name[i], prefix) == 1) { print name[i] " " commit[i] }
          }
        }' "$work/page"
      page=$((page + 1))
    done
    ;;

  *)
    fail "unknown command \`$command\`: release, create, upload, assets, download, publish, delete, releases, tags, url or flatten"
    ;;
esac
