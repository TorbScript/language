# registry

The write service of packages.torb.dev ([RELEASE.md section 7](../../docs/design/RELEASE.md#7-the-registry)): the one
program that writes the registry's files. Reading - the sparse index, the archives, the documentation - is static files
behind a CDN, served by `cr.torb.dev/torbscript/site`; a client needs this service only to publish. It is the first real
server program written in TorbScript: `std/http`'s server, a SQLite database, and the index and archives written
through a storage driver.

## What it does

| Call | Who | What |
|---|---|---|
| `GET /api/1/health` | anyone | `{"message":"ok"}` |
| `POST /api/1/accounts {name}` | the operator | an account, the person owner of its name, and a first token (every action, 90 days) |
| `POST /api/1/owners {name, account?}` | an account with `owners` | an organisation, the caller its first owner; the operator names the founder and may take a reserved name |
| `GET /api/1/owners/<owner>` | anyone | the owner and its members |
| `PUT /api/1/owners/<owner>/members/<account> {role}` | an owner of it | a member added as `owner` or `publisher`, or its role changed |
| `DELETE /api/1/owners/<owner>/members/<account>` | an owner of it | a member removed, never the last owner |
| `POST /api/1/tokens {name, owners, packages, actions, days}` | an account with `tokens` | a new token, shown once; never an action its maker lacks |
| `GET /api/1/tokens` | an account | its tokens, without secrets |
| `DELETE /api/1/tokens/<id>` | an account | a token revoked |
| `PUT /api/1/packages/<owner>/<name>/<version>` | a member, or a trusted publisher | publish: the archive as the body, its tree hash in `Torb-Tree-Hash` |
| `POST /api/1/packages/<owner>/<name>/<version>/yank {reason}` | a member with `yank` | the version withdrawn for new resolutions |
| `DELETE /api/1/packages/<owner>/<name>/<version>/yank` | a member with `yank` | the yank undone |
| `GET /api/1/packages/<owner>/<name>/trusted-publishers` | anyone | who publishes it from CI, and whether tokens are off for it |
| `PUT /api/1/packages/<owner>/<name>/trusted-publishers {publishers, required}` | an owner | the trusted publishers replaced; `required` switches tokens off for the package |
| `POST /api/1/trusted-publishing {token, package}` | a CI job | its OIDC token of GitHub Actions or of a trusted Forgejo's Actions traded for a token that publishes `package` for fifteen minutes; `.../trusted-publishing/github` is the same call |

Every answer is JSON; a refusal is `{"error": ..., "problems": [...]}` with its status. `torb publish`
(`compiler/src/package/upload.trb`) is the client: `TORB_TOKEN`, or trusted publishing from a GitHub Actions or Forgejo Actions job.

**A publish is checked as `torb publish --dry-run` checks it, with the same code.** The registry depends on the
compiler as a `path:` package and calls its `unpacked`, `readLock`, `publishProblems`, `capabilitiesOf` and index
writer, so the tree hash, the limits (10 MiB, 64 MiB unpacked, 10 000 files), the refusals and the index format cannot
drift apart from the client's. It trusts nothing the client computed and never evaluates a `project.trb`; it does not
check the package, because its toolchain may be another patch release.

**Every record of the index is signed** with the registry's Ed25519 key (`std/signature`): a release, a yank and an
unyank carry the signature of the record at its place in the file, and `index/config.trb` lists the keys, oldest
first, each later one endorsed by the one before it. `torb` checks every record it reads against the keys it trusts
- the ones it ships with, or the ones it pinned at its first contact - with the same code
(`compiler/src/package/signing.trb`, [RELEASE.md section 7.14](../../docs/design/RELEASE.md#714-the-signed-index-as-built-2026-09-27)).
`registry signing-key` prints a new seed for `REGISTRY_SIGNING_KEY` and its public key, and does nothing else.

**Trusted publishing** verifies the job's token itself. Its `iss` picks the issuer: GitHub Actions
(`https://token.actions.githubusercontent.com`, key set at `/.well-known/jwks`) or a Forgejo of
`REGISTRY_FORGEJO_ISSUERS` (`https://git.torb.dev/api/actions` by default; its key set is the `jwks_uri` of its
`/.well-known/openid-configuration`). The key set is fetched for every exchange, then come `RS256`, the issuer, the
audience (`packages.torb.dev`) and the lifetime give or take a minute, and then the claims against the package's
trusted publishers. A publisher on GitHub is `owner/name`: the repository, the workflow file below `.github/workflows/`
(`job_workflow_ref`, so a reusable workflow is named by the called file) and the environment where one is configured.
A publisher on a Forgejo is `<host>/owner/name` - `git.torb.dev/torbscript/language` - with the workflow file
(`workflow`); Forgejo's tokens name no environment, so such a publisher names none. Either may name a `ref`,
`refs/tags/v1.2.5` or `refs/tags/v*` for every ref it starts.

## Configuration

Everything comes from the environment (`tools/deploy/compose.example.yml` sets it):

| Variable | Default | What |
|---|---|---|
| `REGISTRY_LISTEN` | `0.0.0.0:8089` | what the server binds |
| `REGISTRY_STORAGE` | `file:///srv/torb/registry` | where the index and the archives go: `<storage>/index/<owner>/<name>.trb`, `<storage>/archives/<owner>/<name>/<version>.tar.gz` |
| `REGISTRY_DATABASE` | `/srv/torb/database/registry.db` | the SQLite file |
| `REGISTRY_SQLITE` | `sqlite3` | the SQLite shell |
| `REGISTRY_ADMINISTRATOR_TOKEN` | none | the operator's token: creates accounts and reserved owners. Set it while creating them, then remove it |
| `REGISTRY_AUDIENCE` | `packages.torb.dev` | the audience a trusted publisher's token has to name |
| `REGISTRY_OIDC_KEYS` | GitHub's key set | where GitHub Actions' keys are fetched |
| `REGISTRY_FORGEJO_ISSUERS` | `https://git.torb.dev/api/actions` | the Forgejo instances whose Actions may publish, by issuer, separated by spaces or commas; empty for none |
| `REGISTRY_TRUSTED_TOKEN_SECONDS` | `900` | how long an exchanged token lives |
| `REGISTRY_MIRROR` | none | a git working copy the index is mirrored into after every change |
| `REGISTRY_MIRROR_PUSH` | `0` | `1` pushes the mirror after each commit |
| `REGISTRY_SIGNING_KEY` | a development key | the seed of the key the index is signed with, 64 hexadecimal digits; without it the registry makes a seed in `<database>.development-key` on its first start, keeps it, and says in its log that a development key signs |
| `REGISTRY_SIGNING_KEY_SUCCESSOR` | none | the public key that is to follow, 64 hexadecimal digits: announced in `index/config.trb`, endorsed by the current key, before `REGISTRY_SIGNING_KEY` switches to it |

## Decisions

- **SQLite through the `sqlite3` shell, not a binding.** `std` has no database driver. A native binding to a vendored
  SQLite is a new native - two commits and a seed refresh - and a nine-megabyte C file in every build; an append-only
  file store of our own would lose what Litestream gives a real SQLite file. The shell is one Debian package, the
  file is an ordinary SQLite database in WAL mode, and Litestream replicates it unchanged. Every value goes in as an
  SQL literal whose only escape is the doubled `'`. When `std` has a driver, `src/database.trb` is the one module
  that changes.
- **Programs run through a `Child`'s pipes** (`src/program.trb`), not `Process.run`: that offloads to the blocking pool
  only where its closure captures nothing counted, so with a command line built at run time it would stall the one
  worker every connection is served on - and deadlock where the program is a client of this server.
- **The storage layer is `std/storage`** (URI.md section 11): `Storage` over a `Uri`, a driver per scheme, and
  `Storage.registry` choosing by the scheme and naming what it knows. It started here as `src/storage.trb` and moved to
  `std` once `std` shipped two drivers for it (`file:` and `memory:`), by that section's own rule. A write replaces the
  file whole (`File.writeBytesAtomically`) rather than through `mv`. `s3:` is one more driver.
- **One writer of the storage at a time**: a lock on the one worker, released by a `using`, which a cancellation closes
  too. Run one replica of this service.
- **RSA in TorbScript** (`src/rsa.trb`): the public operation only, with 16-bit limbs and Montgomery's multiplication,
  checked against a key and tokens Node made. No new native, and both back ends compute the same bits.
- **Tokens** are `torb_` and 256 bits of the system's randomness, stored as their SHA-256.
- **Names that would confuse** share a skeleton - hyphens removed, `1` as `l`, `0` as `o`, `rn` as `m` - which the
  database keeps under `UNIQUE`, for owners and for packages.

## Not built yet

The production signing key does not exist yet, so no `torb` ships a key for packages.torb.dev and every client trusts
the registry's keys on first use; accounts are created by the operator, not by signing in through GitHub, GitLab, Codeberg or a passkey; there is no second factor, no
documentation worker, no search, no `deprecate`, no verified domains and no "elsewhere" owners; the similar-name rule
does not yet compare against packages with many dependents; and `torb yank`, `torb owner` and `torb login` are not
commands yet - the API is there for them.

## Building and testing

```console
$ torb check tools/registry
$ torb build tools/registry/src/main.trb --release --output build/registry
$ torb test tools/registry/tests
```

The tests need the SQLite shell on the `PATH` (or `REGISTRY_SQLITE`): `tests/server.test.trb` runs the registry and a
stand-in for GitHub's key set and a Forgejo's discovery document over loopback, with a database and a `file:` storage in a temporary directory, and
makes every call once the way it works and once the way it is refused; `tests/client.test.trb` drives `torb publish`
against it (`REGISTRY_TEST_TORB` names the `torb`, which `torb test` in the VM is itself). The container image is
`tools/deploy/Dockerfile.registry`, built by `.forgejo/workflows/images.yml` from each release and pushed to
`cr.torb.dev/torbscript/registry`.
