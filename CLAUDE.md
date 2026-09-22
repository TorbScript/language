# TorbScript repository

Self-hosted: `compiler/` is TorbScript, emits C and links `runtime/` (C11). No Rust, no cargo, no interpreter yet
(the VM is milestone 7) - `torb run` builds natively. Load the `torbscript` skill before writing any `.trb`.
Code rules, gates and repository operations: `compiler/CONTRIBUTING.md`.

## Build and gates (repository root, Git Bash on Windows)

- `sh tools/bootstrap.sh` - seed -> torb -> torb in `build/staging-<pid>/`, fixpoint compared, then moved into
  `build/release/torb(.exe)`. A worktree has no `seed/`: `TORB_SEED=<main checkout>/seed/torb.exe sh tools/bootstrap.sh`.
- `sh tools/gates.sh a` - every round; it bootstraps only when `compiler/src`, `std/` or `runtime/` changed.
  `sh tools/gates.sh b` - additionally when the round touches `compiler/src/ir`, `compiler/src/backend`, or `runtime/`.
- `torb test` and `torb run` build the `dev` profile (`-O1`), `torb build` builds `release` (`-O2`); `--profile` or
  `--release` says otherwise.
- A false positive of the checker is a checker bug. `torb check` of a path that reaches no file is an error.

## Checking a scratch program

- Any file can be named: `torb check scratch.trb` / `torb run scratch.trb`, at the root, in a temp dir, in a package
  that is nobody's member. Without a `std/` of its own it gets the toolchain's, found above the file, the working
  directory and `torb` itself (`build/release/torb` finds its checkout's `std/` and `runtime/`); no variables needed.

## Seed and breaking changes

- `seed/` is not in git and exists only in the main checkout. Never delete it; refresh it with `sh tools/refresh-seed.sh`
  (archives the old one to `../torbscript-seeds/`, the five newest kept) only after a merge whose tier A is green -
  never by copying files into `seed/`, which skips the archive. Never delete an archive. A missing or broken seed makes
  `tools/bootstrap.sh` fall back to the newest archived seed that still builds the compiler.
- A syntax change, a new native, a new driver flag the build uses, or renaming a std name the compiler looks up by
  string (`semantics/checker/wellknown.trb`, operator traits in `checker/expression.trb`, member lookups in
  `ir/lower/collection.trb`) takes two commits: teach both forms and refresh the seed, then migrate
  (`docs/design/COLLECTIONS.md` 6a, `docs/RUST-EXIT.md` 4.2).

## Traps

- C files of 8 MB or more (the compiler, its test suite) compile through machine-wide build slots
  (`TORB_BUILD_SLOTS`, default 3; `torb` says when it waits). If `cc1: out of memory` still happens, retry alone.
- Some files are CRLF: keep each file's line endings. Bulk edits only through a script that asserts exact matches;
  never PowerShell arrays.
- TorbScript in docs and answers: no semicolons, no squeezed one-liners, command calls where the canon says so,
  members as `fn area(): Int` (no `self` in the list) and `var fn` for mutation.
- `docs/` is generated into `.claude/skills/torbscript`: after a docs change run
  `build/release/torb.exe docs skill docs .claude/skills/torbscript` and commit both (`--check` is a tier A gate).
- `TODO.md` is the owner's channel (German, very large): read `git diff TODO.md`, never the whole file; never delete
  from it.

## Working as a subagent

- You work in your own git worktree; commit there, never on master, never push. Do not touch `seed/`, `TODO.md` or
  `.claude/` unless the task says so.
- Report what changed, the gate output, and anything left undone with the reason.
