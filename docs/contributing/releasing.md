---
title: Cut a release
summary: The steps from a green main to a signed release on the forge at git.torb.dev, how a seed is published on its own, what the owner sets up once on the forge, the GitHub mirror and the root server, and what to do when a release job fails.
kind: how-to
status: stable
skill: omit
order: 80
keywords:
  - release
  - tag
  - nightly
  - seed
  - fetch-seed
  - publish-seed
  - migrate-seeds
  - SHA256SUMS
  - cosign
  - Forgejo
  - forge
  - Authorized Integration
  - push mirror
source:
  - docs/design/RELEASE.md#13-the-release-pipeline-as-built
  - docs/design/RELEASE.md#14-the-forge
  - docs/design/RELEASE.md#7.11-hosting-and-the-server
  - .forgejo/workflows/release.yml
  - .forgejo/workflows/seed.yml
  - .forgejo/workflows/images.yml
  - .forgejo/actions/token/action.yml
  - .github/workflows/portable.yml
  - tools/forge.sh
  - tools/fetch-seed.sh
  - tools/publish-seed.sh
  - tools/migrate-seeds.sh
  - tools/install.sh
  - tools/install.ps1
  - tools/render-package-manifests.sh
  - tools/release-sync
  - tools/registry
  - tools/deploy
---

A release of TorbScript is made by pushing a tag `v0.MINOR.PATCH` of a commit of `main` to the forge, git.torb.dev. The
workflow `release` checks the tag, runs every gate, and only when all of them are green publishes the archives, the
source, the portable seed and `SHA256SUMS` with its cosign signature as a release of `torbscript/language`; release-sync
then places them at `https://torb.dev/download/<version>/`, where the installers read them. The design behind it is
[section 13 of the release record](../design/RELEASE.md#13-the-release-pipeline-as-built), and the forge, its runners,
the GitHub mirror and the tokens are [section 14](../design/RELEASE.md#14-the-forge).

## Steps

1. **Choose the number.** Before 1.0 a minor release may break and a patch may not
   ([RELEASE.md section 3](../design/RELEASE.md#3-versions-and-the-stability-promise)): a release with a breaking change
   raises `MINOR` and sets `PATCH` to 0, a release of fixes only raises `PATCH`.
2. **Write the number into both manifests.** `version "0.2.0"` in `project.trb` and in `compiler/project.trb`. The
   workflow refuses a tag whose number differs from either.
3. **Write the release notes** in `docs/releases/0.2.0.md`, when there are notes to write: every breaking change, and
   the command that migrates it. Without the file the release says one line, its targets and how to verify a download.
4. **Land on `main` and wait for `ci` to be green** on the forge (Actions of `torbscript/language`). The release runs
   the same gates again, but a red `ci` is the cheaper place to find out.
5. **Tag the commit and push the tag to the forge.** Only the tag starts a release, and pushing it is the approval: the
   forge holds no job for a reviewer. The workflow checks that the commit is on `main`.
6. **Check the release page** on git.torb.dev: one `.tar.gz` per target that has a runner and a `.zip` for Windows, the
   source, the seed with its `.sha256`, `SHA256SUMS` and `SHA256SUMS.sig`. The release is a draft until its last asset
   is uploaded, so the webhook reaches release-sync once, with everything there. The seed is added to `seeds.txt` of the
   release `seeds`, the job `images` pushes `cr.torb.dev/torbscript/release-sync`, `site` and `registry` tagged with the
   version and `latest`, signed, and `publish-packages` pushes the Homebrew formula and the Scoop manifest where the
   release has their archives. The root server takes the images with `docker compose pull && docker compose up -d`.

**Which targets a release carries**: linux-x64 always, and windows-x64, linux-arm64 and macos-arm64 once the forge has
a runner of that label and the repository variable `TORB_RUNNERS` names it. Until then a release is a linux-x64 release,
its notes say so, and the Homebrew and Scoop channels skip themselves.

**Publishing a seed without a release** is the forge's Actions -> `seed` -> Run workflow on `main`. It bootstraps on
linux-x64, runs tier A and tier B, and publishes the seed of `main`. That is what the second commit of a breaking change
waits for: the published seed has to understand the new form before a commit that uses it can bootstrap in CI or in a
fresh clone. The nightly publishes one every night that `main` changed, and `tools/land.sh publish` publishes the seed of
every landing.

**The first seeds on the forge** come from GitHub, where CI published them before the move, or from the main checkout:

```console
$ GH_TOKEN=<GitHub token> TORB_FORGE_TOKEN=<forge token> sh tools/migrate-seeds.sh
$ sh tools/pack-seed.sh seed
$ TORB_FORGE_TOKEN=<forge token> sh tools/publish-seed.sh build/seed-archive/torb-seed-653af8bb6699.tar.gz
```

## Pitfalls

- **A tag that fails the check is still a tag.** The workflow publishes nothing, but the tag stays: delete it
  (`git push --delete forgejo v0.2.0`) before pushing the corrected one, and never reuse the number of a published
  release.
- **A release job that fails after the gates** - the signature, the token, the upload - left a draft release or none
  at all. A draft has no tag yet and no webhook was sent: delete the draft on the release page, then re-run the failed
  jobs of the same run; the gates are not run again.
- **"the repository variable TORB_PUBLISH_AUDIENCE is not set"** or **"the secret COSIGN_PRIVATE_KEY is not set"**: a
  step of "One-time setup" below is missing. A token the forge refuses (401) means the Authorized Integration's rules do
  not admit this run - another workflow file, ref or event than the ones it names.
- **A job that waits for ever** has a `runs-on` label no online runner has: `TORB_RUNNERS` names a target whose runner
  is gone. Remove the target from the variable, or bring the runner back.
- **Forgejo ignores a job that has a `permissions:` key** ("not supported, the job is ignored"). The workflows of
  `.forgejo/` have none; a write goes through an Authorized Integration (`.forgejo/actions/token`).
- **A release needs a `LICENSE`.** The check refuses a release without one.
- **`fetch-seed.sh` answers "no index had the seed"**: neither the forge nor GitHub lists it. The forge's index is
  public; GitHub's needs `GH_TOKEN` while that repository is private.
- **A breaking change pushed in one go** fails CI at the bootstrap: the published seed does not know the new form. Push
  the teaching commit, run `seed`, then push the migration.

## Full example

The release of 0.2.0, from a green `main`:

```console
$ git switch main
$ git pull forgejo main
$ grep '^version' project.trb compiler/project.trb
project.trb:version "0.2.0"
compiler/project.trb:version "0.2.0"
$ git tag v0.2.0
$ git push forgejo v0.2.0
```

Verifying the download afterwards, from a directory with the archive, `SHA256SUMS` and `SHA256SUMS.sig`:

```console
$ cosign verify-blob --key https://git.torb.dev/torbscript/language/raw/branch/main/tools/deploy/cosign.pub \
    --signature SHA256SUMS.sig --insecure-ignore-tlog=true SHA256SUMS
Verified OK
$ sha256sum --check --ignore-missing SHA256SUMS
torb-0.2.0-linux-x64.tar.gz: OK
```

A fresh clone bootstraps from the newest published seed without any setup beyond a C compiler:

```console
$ sh tools/bootstrap.sh
no seed on this machine: fetching the newest published seed (sh tools/fetch-seed.sh)
```

## One-time setup

Everything below is done once, by the owner, outside this repository. Where a value is written here, it is the exact
value to enter.

**The forge** ([RELEASE.md section 14](../design/RELEASE.md#14-the-forge)): git.torb.dev, repository
`torbscript/language`, one runner `torb-runner-1` with the labels `ubuntu-latest` and `ubuntu-24.04`.

1. **Two Authorized Integrations**, created by a user who is an owner of the organization `torbscript` - a token of an
   integration acts as that user, limited to the integration's permissions. User settings -> Authorized Integrations
   -> Add authorized integration -> Forgejo Actions (Local):

   | Field | `torbscript-publish` | `torbscript-images` |
   |---|---|---|
   | Select repository | `torbscript/language` | `torbscript/language` |
   | Workflow file | `{release,nightly,seed}.yml` | `{release,nightly,images}.yml` |
   | Git reference | `{refs/heads/main,refs/tags/v*}` | `{refs/heads/main,refs/tags/v*}` |
   | Event | push, schedule, workflow_dispatch | push, schedule, workflow_dispatch |
   | Repository and organization access | Specific repositories: `torbscript/language`, `torbscript/homebrew-tap`, `torbscript/scoop-bucket` | Public only |
   | Permissions | repository: Read and write; everything else: No access | package: Read and write; everything else: No access |

   The forge shows each integration's audience once it is saved (`u:<id>:<uuid>`); it is not a secret. An integration
   limited to specific repositories can only be given the repository and issue permissions, which is why the images
   need a second one.
2. **Repository variables** (`torbscript/language` -> Settings -> Actions -> Variables): `TORB_PUBLISH_AUDIENCE` =
   the audience of `torbscript-publish`, `TORB_IMAGES_AUDIENCE` = the audience of `torbscript-images`, and - once a
   runner of another target exists - `TORB_RUNNERS` = the targets that have one, separated by spaces:
   `windows-x64 linux-arm64 macos-arm64` or any of them.
3. **The signing key**, on a machine the owner trusts, with cosign 2.4.1:
   `cosign generate-key-pair` asks for a password and writes `cosign.key` and `cosign.pub`. Repository secrets
   (Settings -> Actions -> Secrets): `COSIGN_PRIVATE_KEY` = the whole content of `cosign.key`, `COSIGN_PASSWORD` = the
   password. Commit `cosign.pub` as `tools/deploy/cosign.pub`, copy it beside `compose.yml` on the root server, and keep
   `cosign.key` and its password offline as well - without them no release can be signed with the same key.
4. **Protection**: Settings -> Branches: protect `main` against force pushes and deletion; Settings -> Tags: protect
   `v*` and `seeds` so that only the owner - and the integration, which acts as the owner - creates them.
5. **More runners**, when there are machines for them: a runner registered with the label of its target
   (`windows-x64`, `linux-arm64`, `macos-arm64`), and the target added to `TORB_RUNNERS`. A job for a label no runner has
   would wait, which is why the workflows run those jobs only for the targets the variable names.
6. **The package manager channels**: create `torbscript/homebrew-tap` and `torbscript/scoop-bucket` on the forge, each
   with a first commit that holds an empty `Formula/` or `bucket/` directory (a `.gitkeep`), and give each a push mirror
   to `github.com/TorbScript/homebrew-tap` and `github.com/TorbScript/scoop-bucket` (step 7): `brew tap torbscript/tap`
   finds a tap on GitHub without a URL. winget's first manifest is submitted by hand from Windows,
   `wingetcreate new https://torb.dev/download/<version>/torb-<version>-windows-x64.zip` (publisher `TorbScript`, package
   `Torb`, portable, the nested `bin/torb.exe`); every later version with the command the release job prints.
7. **GitHub as the mirror**: on GitHub, a fine-grained personal access token limited to `TorbScript/language` with
   Contents: Read and write and Workflows: Read and write (without the second, GitHub refuses a push that changes
   `.github/workflows/`). On the forge, `torbscript/language` -> Settings -> Repository -> Mirror settings -> Push
   mirror: Git remote repository URL `https://github.com/TorbScript/language.git`, Username the GitHub account, Password
   the token, Sync when commits are pushed on, interval `8h0m0s`. The same for the tap and the bucket with tokens of
   their repositories. Making `TorbScript/language` public turns `.github/workflows/portable.yml` on; while it is
   private that workflow does nothing.
8. **Once the forge has every seed** (`sh tools/migrate-seeds.sh` answers "nothing to copy"): the release `seeds` on
   GitHub may stay as an archive; nothing reads it any more.

**The root server** ([RELEASE.md section
7.11](../design/RELEASE.md#the-root-server-and-the-write-service-as-built-2026-09-27)): torb.dev with the downloads,
packages.torb.dev with the registry, from the images every release pushes to cr.torb.dev, behind the Traefik that already
runs on the machine - beside the forge's own stack or in it. Nothing is built on the server.

1. **DNS**: `torb.dev`, `www.torb.dev` and `packages.torb.dev` pointed at the machine (or at the CDN in front of it).
2. **The files**: a directory such as `/opt/torb` with `tools/deploy/compose.example.yml` copied as `compose.yml`,
   `.env.example` as `.env` (never committed), `cosign.pub`, `litestream.yml` and `backup.sh` beside them. The data
   directories below `TORB_DATA` (default `/srv/torb`) belong to the containers' user:
   `mkdir -p /srv/torb/download /srv/torb/registry /srv/torb/database /srv/torb/index-mirror && chown -R 1000:1000 /srv/torb`.
   No runner of the forge ever mounts any of them.
3. **The `.env`**:
   - `TRAEFIK_NETWORK`, `TRAEFIK_ENTRYPOINT` and `TRAEFIK_CERTRESOLVER`: the Docker network the existing Traefik watches,
     its HTTPS entrypoint and its Let's Encrypt resolver, as its own configuration names them.
   - `RELEASE_SYNC_FORGE=forgejo`, `RELEASE_SYNC_URL=https://git.torb.dev`, `RELEASE_SYNC_REPOSITORY=torbscript/language`
     (the defaults), `RELEASE_SYNC_TOKEN` empty - the repository is public - and `RELEASE_SYNC_WEBHOOK_SECRET` any
     random string (`openssl rand -hex 32`), entered in the webhook too.
   - `REGISTRY_ADMINISTRATOR_TOKEN`: another random string, for step 6; empty again afterwards.
     `REGISTRY_FORGEJO_ISSUERS=https://git.torb.dev/api/actions`: whose Forgejo Actions may publish packages.
   - `BACKUP_ENDPOINT`, `BACKUP_REGION`, `BACKUP_BUCKET`, `BACKUP_ACCESS_KEY_ID`, `BACKUP_SECRET_ACCESS_KEY`: a bucket of
     S3-compatible object storage at another provider than the server's, and a key limited to it; `RESTIC_PASSWORD`:
     a random string kept somewhere else too - without it the file backups cannot be read.
4. **Start**: `docker compose pull && docker compose up -d`; the images of cr.torb.dev are public, no `docker login`.
   **An update** is the same command; `TORB_VERSION` pins a version, `latest` follows stable releases, `nightly` the
   nightlies. `docker compose logs <service>` says what each one does.
5. **The webhook**: on the forge, `torbscript/language` -> Settings -> Webhooks -> Add webhook -> Forgejo: Target URL
   `https://torb.dev/webhook`, HTTP method `POST`, POST content type `application/json`, Secret the value of
   `RELEASE_SYNC_WEBHOOK_SECRET`, Trigger on: Custom events -> Releases, Active. The forge refuses to deliver to a
   private address (`[webhook] ALLOWED_HOST_LIST`, `external` by default): where the forge resolves `torb.dev` to one -
   the same machine behind a hairpin - add `torb.dev` to that list. Verify it with a release, or wait for the periodic
   check (`RELEASE_SYNC_POLL_SECONDS`, default 300), and look for `/srv/torb/download/<version>/`;
   `docker compose logs release-sync` says why one is missing - most often a signature that does not verify against
   `cosign.pub`, which is by design.
6. **The first accounts**: with `REGISTRY_ADMINISTRATOR_TOKEN` set,
   `curl -X POST https://packages.torb.dev/api/1/accounts -H "Authorization: Bearer $ADMIN" -d '{"name":"torben"}'`
   answers the account's first token (shown once - keep it); an organisation is
   `curl -X POST .../api/1/owners -H "Authorization: Bearer $TOKEN" -d '{"name":"acme"}'`, and the reserved owner
   `torbscript` only with the operator's token and `"account":"torben"`. Then empty `REGISTRY_ADMINISTRATOR_TOKEN` and
   `docker compose up -d` again. `tools/registry/README.md` lists every call.
7. **Trusted publishing** for a package, so its CI publishes without a stored token: an owner of it sends
   `curl -X PUT https://packages.torb.dev/api/1/packages/acme/http/trusted-publishers -H "Authorization: Bearer $TOKEN" -d '{"publishers":[{"repository":"git.torb.dev/acme/http","workflow":"release.yml","ref":"refs/tags/v*"}],"required":true}'`
   for a repository on the forge - its job has `enable-openid-connect: true` and runs `torb publish` - or
   `{"repository":"acme/http","workflow":"release.yml","environment":"release"}` for one on GitHub, whose job has
   `permissions: id-token: write`. `required` switches tokens off for the package.
8. **The index mirror** (optional): an empty public repository on the forge, e.g. `torbscript/index`, cloned into
   `/srv/torb/index-mirror` with a remote whose URL carries an access token of the forge with repository write access
   to that repository alone (`git clone https://<user>:<token>@git.torb.dev/torbscript/index.git /srv/torb/index-mirror`,
   then `chown -R 1000:1000` it), and in `.env` `REGISTRY_MIRROR=/srv/torb/index-mirror` and `REGISTRY_MIRROR_PUSH=1`.
9. **Backups**: Litestream runs as a service and replicates the database at once. The files are backed up by the
   host's cron: `17 3 * * * cd /opt/torb && docker compose --profile backup run --rm backup >> /var/log/torb-backup.log 2>&1`.
   Restoring the database is
   `docker compose run --rm litestream restore -config /etc/litestream.yml /srv/torb/database/registry.db` before the
   registry starts; the files come back with `restic restore latest --target /` from
   `docker compose --profile backup run --rm --entrypoint sh backup`.
10. **The CDN** (optional, later): in front of both host names, honouring the `Cache-Control` the site sends - a year
    for an archive, a minute for an index file.

Verifying an image by hand:

```console
$ cosign verify --key https://git.torb.dev/torbscript/language/raw/branch/main/tools/deploy/cosign.pub \
    --insecure-ignore-tlog=true cr.torb.dev/torbscript/registry:0.2.0
```

## Related

- [The release record](../design/RELEASE.md) - versions, channels, what a download contains, section 13, the pipeline
  as built, and section 14, the forge.
- [Working on the compiler](../../compiler/CONTRIBUTING.md) - the gates, the seed and the two-commit rule.
- [The docs commands](checks.md) - the gates this page itself passes.
