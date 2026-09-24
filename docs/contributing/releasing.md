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
  - .github/workflows/release.yml
  - .github/workflows/seed.yml
  - tools/fetch-seed.sh
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

## Related

- [The release record](../design/RELEASE.md) - versions, channels, what a download contains, and section 13, the
  pipeline as built.
- [Working on the compiler](../../compiler/CONTRIBUTING.md) - the gates, the seed and the two-commit rule.
- [The docs commands](checks.md) - the gates this page itself passes.
