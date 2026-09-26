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
  - tools/fetch-seed.sh
  - tools/install.sh
  - tools/install.ps1
  - tools/render-package-manifests.sh
  - tools/release-sync
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
7.11](../design/RELEASE.md#public-downloads-while-the-repository-is-private-decided-here)), which makes
`torb.dev/download/...` answer while the repository is private:

1. A machine with Docker and Docker Compose, and DNS for `torb.dev` and `packages.torb.dev` pointed at it (or at a CDN
   in front of it).
2. Copy `tools/deploy/docker-compose.example.yml` to `docker-compose.yml` next to a `.env` (never committed) with
   `ACME_EMAIL`, `RELEASE_SYNC_TOKEN` (a fine-grained token scoped to `contents: read` of this repository - read-only,
   since this program never publishes anything back to GitHub) and `RELEASE_SYNC_WEBHOOK_SECRET` (any random string).
   Then `docker compose up -d --build`.
3. On the repository, Settings -> Webhooks -> Add webhook: payload URL `https://torb.dev/webhook`, content type
   `application/json`, secret the same `RELEASE_SYNC_WEBHOOK_SECRET`, event "Releases" only.
4. Verify it: publish a release (or wait for `release-sync`'s periodic check, `RELEASE_SYNC_POLL_SECONDS`, default 300)
   and look for `<root>/download/<version>/` on the server; `docker compose logs release-sync` says why one is
   missing - most often `cosign` refusing a signature it could not verify, which is by design (section 7.11, "a
   release this program cannot check is not placed as if it had been").

## Related

- [The release record](../design/RELEASE.md) - versions, channels, what a download contains, and section 13, the
  pipeline as built.
- [Working on the compiler](../../compiler/CONTRIBUTING.md) - the gates, the seed and the two-commit rule.
- [The docs commands](checks.md) - the gates this page itself passes.
