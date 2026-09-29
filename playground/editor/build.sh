#!/bin/sh
# Builds the editor of the playground, `playground/playground-editor.js`, from `editor.mjs` and the packages of
# `package.json` at the exact versions of `package-lock.json` (docs/tooling/the-playground.md, "How it is built").
#
#   sh playground/editor/build.sh            # npm ci, then esbuild: writes playground/playground-editor.js
#   sh playground/editor/build.sh --check    # fails where the committed file is not what it would write
#
# The bundle is committed, so neither `playground/build.sh` nor the site image needs npm or the network: this script
# runs only where the editor changes - a new version in package.json (`npm install` rewrites the lock file), or an edit
# of editor.mjs - and its result is committed with the change. It needs node and npm, and reaches the npm registry.
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"

# `--ignore-scripts`: nothing of a package runs at its installation; esbuild finds its binary in the optional package
# of the machine's platform, which npm installs as the lock file says
npm ci --ignore-scripts --no-audit --no-fund >&2
node build.mjs "$@"
