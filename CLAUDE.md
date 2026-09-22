# TorbScript repository

Self-hosted: `compiler/` is TorbScript, emits C and links `runtime/` (C11). No Rust, no cargo, no interpreter yet
(the VM is milestone 7) - `torb run` builds natively. Load the `torbscript` skill before writing any `.trb`.
Code rules and gates: `compiler/CONTRIBUTING.md`.

## Build and gates (repository root, Git Bash on Windows)

- `sh tools/bootstrap.sh` - seed -> `build/bootstrap/torb` -> `build/release/torb(.exe)`, fixpoint compared.
- `sh tools/gates.sh a` - every round. `sh tools/gates.sh b` - additionally when the round touches `compiler/src/ir`,
  `compiler/src/backend`, or `runtime/`.
- A false positive of the checker is a checker bug. "0 files, no problems" means nothing was checked.

## Checking a scratch program

- std resolves only through a workspace member: put the file in a package that is a member (for example
  `tests/language/`, or `examples/<name>/` with `project.trb` + `src/main.trb`), never loose or in a temp dir.
- `run` outside the repository needs `TORB_RUNTIME=<repo>/runtime`.

## Seed and breaking changes

- `seed/` is not in git and exists only on this machine. Never delete it; refresh it from `build/release` only after a
  merge whose tier A is green.
- A syntax change, a new native, or renaming a std name the compiler looks up by string (`semantics/checker/wellknown.trb`,
  operator traits in `checker/expression.trb`, member lookups in `ir/lower/collection.trb`) takes two commits: teach
  both forms and refresh the seed, then migrate (`docs/COLLECTIONS.md` 6a, `docs/RUST-EXIT.md` 4.2).

## Traps

- Parallel builds of the compiler (about 60 MB of C each) run out of memory (`cc1: out of memory`): retry alone.
- Some files are CRLF: keep each file's line endings. Bulk edits only through a script that asserts exact matches;
  never PowerShell arrays.
- TorbScript in docs and answers: no semicolons, no squeezed one-liners, command calls where the canon says so,
  members as `fn area(): Int` (no `self` in the list) and `var fn` for mutation.
- `docs/` is generated into `.claude/skills/torbscript`: after a docs change run
  `build/release/torb.exe docs skill docs .claude/skills/torbscript` and commit both.
- `TODO.md` is the owner's channel (German, very large): read `git diff TODO.md`, never the whole file; never delete
  from it.

## Working as a subagent

- You work in your own git worktree; commit there, never on master, never push. Do not touch `seed/`, `TODO.md` or
  `.claude/` unless the task says so.
- Report what changed, the gate output, and anything left undone with the reason.
