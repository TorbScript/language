# TorbScript repository

Self-hosted: `compiler/` is TorbScript, emits C and links `runtime/` (C11). No Rust, no cargo. `torb run` and
`torb test` interpret bytecode of the same IR in the VM (milestone 7, `docs/design/VM.md`); `--native` builds and runs a
binary, and `torb build` is always native. Tier B runs the whole conformance suite in both. Load the `torbscript` skill
before writing any `.trb`.
Code rules, gates and repository operations: `compiler/CONTRIBUTING.md`.

## Build and gates (repository root, Git Bash on Windows)

- `sh tools/bootstrap.sh` - seed -> torb -> torb in `build/staging-<pid>/`, fixpoint compared, then moved into
  `build/release/torb(.exe)`. A worktree has no `seed/`: `TORB_SEED=<main checkout>/seed/torb.exe sh tools/bootstrap.sh`.
- `sh tools/gates.sh a` - every round; it bootstraps only when `compiler/src`, `std/` or `runtime/` changed.
  `sh tools/gates.sh b` - additionally when the round touches `compiler/src/ir`, `compiler/src/backend`, or `runtime/`.
  `sh tools/gates.sh ab` - both tiers side by side on one slot. Gates run in lanes and every red one is reported;
  `TORB_GATE_JOBS` caps the processes (default: per processor and 768 MiB), `TORB_GATES_PARALLEL=0` is the old order.
- `torb test --native compiler/tests` runs the compiler's suite (the VM is too slow for it). `torb test --native` and
  `torb run --native` build the `dev` profile (`-O1`), `torb build` builds `release` (`-O2`); `--profile` or
  `--release` says otherwise.
- A false positive of the checker is a checker bug. `torb check` of a path that reaches no file is an error.
- `torb format --check .` is a tier A gate; `torb format <path>` writes the layout (the canon plus indentation, spaces
  and blank lines). `torb canon` is a deprecated alias of it. `torb lint <path>` reports the style rules, not a gate.
  `torb format` holds lines to 120 columns: a longer line breaks at the outermost bracket, what fits joins again.

## Checking a scratch program

- Any file can be named: `torb check scratch.trb` / `torb run scratch.trb`, at the root, in a temp dir, in a package
  that is nobody's member. Without a `std/` of its own it gets the toolchain's, found above the file, the working
  directory and `torb` itself (`build/release/torb` finds its checkout's `std/` and `runtime/`); no variables needed.

## Seed and breaking changes

- `seed/` is not in git and exists only in the main checkout. Never delete it; refresh it with `sh tools/refresh-seed.sh`
  (archives the old one to `../torbscript-seeds/`, the five newest kept) only after a merge whose tier A is green -
  never by copying files into `seed/`, which skips the archive. Never delete an archive. A missing or broken seed makes
  `tools/bootstrap.sh` fall back to the newest archived seed that still builds the compiler.
- The repository lives on the project's Forgejo, **git.torb.dev** (`torbscript/language`, remote `forgejo`, SSH on port
  2223); its CI is `.forgejo/workflows/`, and GitHub (`origin`) is a push mirror whose one workflow,
  `.github/workflows/portable.yml`, tests the targets the forge has no runner for (`docs/design/RELEASE.md` section 14).
- Seeds are also **published**: `program.c` plus the runtime of its commit, as `torb-seed-<commit>.tar.gz` on the release
  `seeds` of git.torb.dev, listed newest first in its `seeds.txt`. `sh tools/fetch-seed.sh [commit]` downloads one over
  HTTPS (curl or wget, no `gh`) - from the forge, and from GitHub's old `seeds` release only for a seed the forge lacks -
  checks its SHA-256, compiles it with the local C compiler into `build/seed/torb`. Bootstrap order: `$TORB_SEED`,
  `seed/`, `../torbscript-seeds/`, `build/seed/` - and with none of them it runs `tools/fetch-seed.sh` itself
  (`TORB_SEED_FETCH=0` forbids that). CI and fresh clones bootstrap this way. The nightly and every release publish the
  seed of main; the forge's Actions -> `seed` -> Run workflow publishes one at once, and `sh tools/publish-seed.sh`
  with `TORB_FORGE_TOKEN` by hand (`docs/contributing/releasing.md`).
- A syntax change, a new native, a new driver flag the build uses, or renaming a std name the compiler looks up by
  string (`semantics/checker/wellknown.trb`, operator traits in `checker/expression.trb`, member lookups in
  `ir/lower/collection.trb`) takes two commits: teach both forms and refresh the seed, then migrate
  (`docs/design/COLLECTIONS.md` 6a, `docs/RUST-EXIT.md` 4.2). The published seed has to know the new form too before
  the migration is pushed, or CI cannot bootstrap it: run the forge's `seed` workflow after the first commit is on main.

## Traps

- C files of 8 MB or more (the compiler, its test suite) compile through machine-wide build slots
  (`TORB_BUILD_SLOTS`, default 3; `torb` says when it waits). If `cc1: out of memory` still happens, retry alone.
- `tools/gates.sh` and `tools/bootstrap.sh` each hold one of `TORB_GATE_SLOTS` (default 2) machine-wide gate slots
  (`tools/gate-slot.sh`; a bootstrap inside a gate run takes none). A run that waits says so - it is not hung. Six
  parallel gate runs once exhausted the machine's processes.
- Every `dev` binary (`torb test --native`, `torb run --native`) and every program the VM runs stops at min(8 GiB, half
  the RAM): `panic: out of memory: the limit of
  ... was reached`, exit code 102, enforced by the OS (job object / `RLIMIT_DATA`). `TORB_MEMORY_LIMIT` (`16G`, `512M`,
  `0`/`none`) overrides it for every TorbScript process that sees it - except a binary that hosts the VM (`torb`, the
  compiler's tests): there it limits the programs the VM interprets, counted by the kernel, and the host keeps its own
  profile's default. Release binaries have no default limit. A runaway test once took the machine down.
- Some files are CRLF: keep each file's line endings. Bulk edits only through a script that asserts exact matches;
  never PowerShell arrays.
- TorbScript in docs and answers: no semicolons, no squeezed one-liners, command calls where the canon says so,
  members as `fn area(): Int` (no `self` in the list) and `var fn` for mutation.
- `docs/` is generated into `.claude/skills/torbscript`: after a docs change run
  `build/release/torb.exe docs skill docs .claude/skills/torbscript` and commit both (`--check` is a tier A gate).
- Commit messages are Conventional Commits (`feat(vm): ...`, `fix(checker): ...`), at most 100 characters, and the
  history is linear: no merge commits - a branch lands rebased or squashed (`compiler/CONTRIBUTING.md`, "Repository
  Operations"). CI checks both. A worktree takes a newer `main` with `git rebase main`, not `git merge`.
- Questions and requests go through the chat. There is no to-do file in the repository: what is planned is in
  `docs/ROADMAP.md` and the design records under `docs/design/`, what is undecided in CONCEPT's "Open Questions".

## Working as a subagent

- You work in your own git worktree; commit there, never on `main`, never push. Do not touch `seed/` or `.claude/`
  unless the task says so.
- Report what changed, the gate output, and anything left undone with the reason.
