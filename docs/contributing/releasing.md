---
title: Cut a release
summary: The steps from a green main to a signed release on GitHub, how a seed is published on its own, and what to do when a release job fails.
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
  - SHA256SUMS
  - cosign
  - GitHub Actions
source:
  - docs/design/RELEASE.md#13-the-release-pipeline-as-built
  - docs/design/RELEASE.md#7.11-hosting-and-the-server
  - .github/workflows/release.yml
  - .github/workflows/seed.yml
  - .github/workflows/images.yml
  - tools/fetch-seed.sh
  - tools/install.sh
  - tools/install.ps1
  - tools/render-package-manifests.sh
  - tools/release-sync
  - tools/registry
  - tools/deploy
---

A release of TorbScript is made by pushing a tag `v0.MINOR.PATCH` to a commit of `main`. The workflow `release` checks
the tag, runs every gate on every tier 1 target, and only when all of them are green publishes the archives, the
source, the portable seed and a signed `SHA256SUMS` as a GitHub release. The design behind it is
[section 13 of the release record](../design/RELEASE.md#13-the-release-pipeline-as-built).

## Steps

1. **Choose the number.** Before 1.0 a minor release may break and a patch may not
   ([RELEASE.md section 3](../design/RELEASE.md#3-versions-and-the-stability-promise)): a release with a breaking change
   raises `MINOR` and sets `PATCH` to 0, a release of fixes only raises `PATCH`.
2. **Write the number into both manifests.** `version "0.2.0"` in `project.trb` and in `compiler/project.trb`. The
   workflow refuses a tag whose number differs from either.
3. **Write the release notes** in `docs/releases/0.2.0.md`, when there are notes to write: every breaking change, and
   the command that migrates it. Without the file the release says one line and how to verify a download.
4. **Merge to `main` and wait for `ci` to be green.** The release runs the same gates again, but a red `ci` is the
   cheaper place to find out.
5. **Tag the commit and push the tag.** Only the tag starts a release; the workflow checks that its commit is on
   `main`.
6. **Approve it**, when the `release` environment has required reviewers: the run waits after the gates until one
   of them does.
7. **Check the release page**: one `.tar.gz` per target and a `.zip` for Windows, the source, the seed with its
   `.sha256`, `SHA256SUMS` and `SHA256SUMS.sigstore.json`. The seed is also added to `seeds.txt` of the release `seeds`.
   The job `images` pushes `ghcr.io/torbscript/release-sync`, `site` and `registry` tagged with the version and
   `latest`, signed; the root server takes them with `docker compose pull && docker compose up -d`.

**Publishing a seed without a release** is Actions -> `seed` -> Run workflow on `main`. It bootstraps on linux-x64, runs
tier A and tier B, and publishes the seed of `main`. That is what the second commit of a breaking change waits for:
the published seed has to understand the new form before a commit that uses it can bootstrap in CI or in a fresh
clone. The nightly publishes one every night that `main` changed.

**The very first seed** comes from the main checkout, because CI cannot bootstrap before one is published. With `gh`
logged in:

```console
sh tools/pack-seed.sh seed
sh tools/publish-seed.sh build/seed-archive/torb-seed-653af8bb6699.tar.gz
```

## Pitfalls

- **A tag that fails the check is still a tag.** The workflow publishes nothing, but the tag stays: delete it
  (`git push --delete origin v0.2.0`) before pushing the corrected one, and never reuse the number of a published
  release.
- **A release job that fails after the gates** - the signature or the upload - left a release half made or none at all.
  Delete the release on GitHub if it exists, then re-run the failed jobs of the same run; the gates are not run again.
- **A public repository needs a `LICENSE`.** The check refuses a release without one; a private repository may cut
  test releases without it.
- **`fetch-seed.sh` answers "lists no seed"**: no seed was published yet, or the index URL is wrong. A private
  repository needs `GH_TOKEN` (or `GITHUB_TOKEN`) for the download.
- **A breaking change pushed in one go** fails CI at the bootstrap: the published seed does not know the new form. Push
  the teaching commit, run `seed`, then push the migration.

## Full example

The release of 0.2.0, from a green `main`:

```console
$ git switch main
$ git pull
$ grep '^version' project.trb compiler/project.trb
project.trb:version "0.2.0"
compiler/project.trb:version "0.2.0"
$ git tag v0.2.0
$ git push origin v0.2.0
```

Verifying the download afterwards, from a directory with the archive, `SHA256SUMS` and the bundle:

```console
$ cosign verify-blob --bundle SHA256SUMS.sigstore.json \
    --certificate-identity "https://github.com/TorbScript/language/.github/workflows/release.yml@refs/tags/v0.2.0" \
    --certificate-oidc-issuer https://token.actions.githubusercontent.com SHA256SUMS
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

Everything below is done once, by the owner, outside this repository - nothing here is a gate, and none of it blocks a
release: a channel whose secret is missing is skipped by the workflow that would publish to it.

**The package manager channels** (`publish-packages` of `release.yml`, [RELEASE.md section
5](../design/RELEASE.md#5-installing-upgrading-channels-and-signing)):

1. Create `TorbScript/homebrew-tap` and `TorbScript/scoop-bucket` on GitHub, each with an empty `Formula/` or
   `bucket/` directory so the job's first push is a normal commit and not an empty repository's first one.
2. Submit the first winget manifest by hand, from a built `torb-<version>-windows-x64.zip`:
   `wingetcreate new https://github.com/TorbScript/language/releases/download/v<version>/torb-<version>-windows-x64.zip`
   answers a few questions (publisher `TorbScript`, package `Torb`, portable, the nested `bin/torb.exe`) and opens the
   pull request against `microsoft/winget-pkgs` that creates `TorbScript.Torb`; `wingetcreate update` (what the
   workflow calls) only ever edits a manifest that already exists.
3. Add three repository secrets (Settings -> Secrets and variables -> Actions): `HOMEBREW_TAP_TOKEN` and
   `SCOOP_BUCKET_TOKEN`, each a fine-grained personal access token with `contents: write` of the one repository it
   pushes to and nothing else; `WINGET_TOKEN`, a token with permission to open a pull request against
   `microsoft/winget-pkgs` under the owner's account (a classic token with `public_repo` is what `wingetcreate` itself
   documents needing).

**The root server** ([RELEASE.md section
7.11](../design/RELEASE.md#the-root-server-and-the-write-service-as-built-2026-09-27)): torb.dev with the downloads,
packages.torb.dev with the registry, from the images every release pushes to GHCR, behind the Traefik that already
runs on the machine. Nothing is built on the server.

1. **DNS**: `torb.dev`, `www.torb.dev` and `packages.torb.dev` pointed at the machine (or at the CDN in front of it).
2. **The files**: a directory such as `/opt/torb` with `tools/deploy/compose.example.yml` copied as `compose.yml`,
   `.env.example` as `.env` (never committed), `litestream.yml` and `backup.sh` beside them. The data directories
   below `TORB_DATA` (default `/srv/torb`) belong to the containers' user:
   `mkdir -p /srv/torb/download /srv/torb/registry /srv/torb/database /srv/torb/index-mirror && chown -R 1000:1000 /srv/torb`.
3. **The `.env`**:
   - `TRAEFIK_NETWORK`, `TRAEFIK_ENTRYPOINT` and `TRAEFIK_CERTRESOLVER`: the Docker network the existing Traefik watches,
     its HTTPS entrypoint and its Let's Encrypt resolver, as its own configuration names them (`docker network ls`
     shows the network).
   - `RELEASE_SYNC_TOKEN`: a fine-grained token with `contents: read` of this repository and nothing else;
     `RELEASE_SYNC_WEBHOOK_SECRET`: any random string (`openssl rand -hex 32`), entered in the webhook below too.
   - `REGISTRY_ADMINISTRATOR_TOKEN`: another random string, for step 7; empty again afterwards.
   - `BACKUP_ENDPOINT`, `BACKUP_REGION`, `BACKUP_BUCKET`, `BACKUP_ACCESS_KEY_ID`, `BACKUP_SECRET_ACCESS_KEY`: a bucket of
     S3-compatible object storage at another provider than the server's, and a key limited to it; `RESTIC_PASSWORD`:
     a random string kept somewhere else too - without it the file backups cannot be read.
4. **GHCR, while the repository is private**: its packages are private as well, so the machine logs in once with a
   classic personal access token that has only `read:packages`:
   `echo "$TOKEN" | docker login ghcr.io -u <github user> --password-stdin`. Once the repository and its packages are
   public, no login is needed.
5. **Start**: `docker compose pull && docker compose up -d`. **An update** is the same command; `TORB_VERSION` pins a
   version, `latest` follows stable releases, `nightly` the nightlies. `docker compose logs <service>` says what each
   one does.
6. **The webhook**: on the repository, Settings -> Webhooks -> Add webhook: payload URL `https://torb.dev/webhook`,
   content type `application/json`, the secret of `RELEASE_SYNC_WEBHOOK_SECRET`, event "Releases" only. Verify it by
   publishing a release (or waiting for the periodic check, `RELEASE_SYNC_POLL_SECONDS`, default 300) and looking for
   `/srv/torb/download/<version>/`; `docker compose logs release-sync` says why one is missing - most often `cosign`
   refusing a signature it could not verify, which is by design.
7. **The first accounts**: with `REGISTRY_ADMINISTRATOR_TOKEN` set,
   `curl -X POST https://packages.torb.dev/api/1/accounts -H "Authorization: Bearer $ADMIN" -d '{"name":"torben"}'`
   answers the account's first token (shown once - keep it); an organisation is
   `curl -X POST .../api/1/owners -H "Authorization: Bearer $TOKEN" -d '{"name":"acme"}'`, and the reserved owner
   `torbscript` only with the operator's token and `"account":"torben"`. Then empty `REGISTRY_ADMINISTRATOR_TOKEN` and
   `docker compose up -d` again. `tools/registry/README.md` lists every call.
8. **Trusted publishing** for a package, so its CI publishes without a stored token: an owner of it sends
   `curl -X PUT https://packages.torb.dev/api/1/packages/acme/http/trusted-publishers -H "Authorization: Bearer $TOKEN" -d '{"publishers":[{"repository":"acme/http","workflow":"release.yml","environment":"release"}],"required":true}'`,
   and the workflow's job gets `permissions: id-token: write` and runs `torb publish` - `required` switches tokens off
   for the package.
9. **The index mirror** (optional): an empty public repository, e.g. `TorbScript/index`, cloned into
   `/srv/torb/index-mirror` with a remote whose URL carries a fine-grained token with `contents: write` of that
   repository alone (`git clone https://x-access-token:<token>@github.com/TorbScript/index.git /srv/torb/index-mirror`,
   then `chown -R 1000:1000` it), and in `.env` `REGISTRY_MIRROR=/srv/torb/index-mirror` and
   `REGISTRY_MIRROR_PUSH=1`.
10. **Backups**: Litestream runs as a service and replicates the database at once. The files are backed up by the
    host's cron: `17 3 * * * cd /opt/torb && docker compose --profile backup run --rm backup >> /var/log/torb-backup.log 2>&1`.
    Restoring the database is
    `docker compose run --rm litestream restore -config /etc/litestream.yml /srv/torb/database/registry.db` before the
    registry starts; the files come back with `restic restore latest --target /` from
    `docker compose --profile backup run --rm --entrypoint sh backup`.
11. **The CDN** (optional, later): in front of both host names, honouring the `Cache-Control` the site sends - a year
    for an archive, a minute for an index file.

Verifying an image by hand:

```console
$ cosign verify ghcr.io/torbscript/registry:0.2.0 \
    --certificate-identity-regexp '^https://github.com/TorbScript/language/.github/workflows/images.yml@' \
    --certificate-oidc-issuer https://token.actions.githubusercontent.com
```

## Related

- [The release record](../design/RELEASE.md) - versions, channels, what a download contains, and section 13, the
  pipeline as built.
- [Working on the compiler](../../compiler/CONTRIBUTING.md) - the gates, the seed and the two-commit rule.
- [The docs commands](checks.md) - the gates this page itself passes.
