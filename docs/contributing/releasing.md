---
title: Cut a release
summary: From a green main to a signed release on git.torb.dev and the VS Code extension in its stores, a seed published on its own, what the owner sets up once on the forge, the stores, the mirror and the server, and what to do when a job fails.
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
  - cross-compile
  - zig cc
  - SHA256SUMS
  - cosign
  - Forgejo
  - forge
  - Authorized Integration
  - push mirror
  - VS Code Marketplace
  - Open VSX
source:
  - docs/design/RELEASE.md#13-the-release-pipeline-as-built
  - docs/design/RELEASE.md#14-the-forge
  - docs/design/RELEASE.md#7.11-hosting-and-the-server
  - .forgejo/workflows/release.yml
  - .forgejo/workflows/nightly.yml
  - .forgejo/workflows/seed.yml
  - .forgejo/workflows/images.yml
  - .forgejo/actions/token/action.yml
  - .forgejo/actions/cross/action.yml
  - .github/workflows/portable.yml
  - tools/forge.sh
  - tools/fetch-seed.sh
  - tools/publish-seed.sh
  - tools/migrate-seeds.sh
  - tools/cross.sh
  - tools/fetch-zig.sh
  - tools/install.sh
  - tools/install.ps1
  - tools/render-package-manifests.sh
  - tools/package-extension.sh
  - editors/vscode/package.json
  - tools/release-sync
  - tools/registry
  - tools/deploy
---

A release of TorbScript is made by pushing a tag `v0.MINOR.PATCH` of a commit of `main` to the forge, git.torb.dev. The
workflow `release` checks the tag, runs every gate, and only when all of them are green publishes the archives, the
source, the portable seed, the VS Code extension and `SHA256SUMS` with its cosign signature as a release of
`torbscript/language`; release-sync then places them at `https://torb.dev/download/<version>/`, where the installers
read them, and the extension goes to the Visual Studio Marketplace and to Open VSX. Every nightly publishes its own
extension to both stores too, as a pre-release with a store version of its own - VS Code's "Switch to Pre-Release
Version" always has `main`, without waiting for a release. The design behind it is
[section 13 of the release record](../design/RELEASE.md#13-the-release-pipeline-as-built), and the forge, its runners,
the GitHub mirror and the tokens are [section 14](../design/RELEASE.md#14-the-forge).

## Steps

1. **Choose the number.** Before 1.0 a minor release may break and a patch may not
   ([RELEASE.md section 3](../design/RELEASE.md#3-versions-and-the-stability-promise)): a release with a breaking change
   raises `MINOR` and sets `PATCH` to 0, a release of fixes only raises `PATCH`.
2. **Write the number into the three manifests.** `version = "0.2.0"` in `project.trb` and in `compiler/project.trb`,
   and `"version": "0.2.0"` in `editors/vscode/package.json` - the VS Code extension carries the toolchain's number.
   The workflow refuses a tag whose number differs from any of them, and CI refuses a `package.json` whose number is not
   `project.trb`'s.
3. **Write the release notes** in `docs/releases/0.2.0.md`, when there are notes to write: every breaking change, and
   the command that migrates it. Without the file the release says one line, its targets and how to verify a download.
   What changed in the extension goes into `editors/vscode/CHANGELOG.md` as a section `## 0.2.0`, which the stores
   show as its changelog.
4. **Land on `main` and wait for `ci` to be green** on the forge (Actions of `torbscript/language`). The release runs
   the same gates again, but a red `ci` is the cheaper place to find out.
5. **Tag the commit and push the tag to the forge.** Only the tag starts a release, and pushing it is the approval: the
   forge holds no job for a reviewer. The workflow checks that the commit is on `main`.
6. **Check the release page** on git.torb.dev: one `.tar.gz` per tier 1 target - linux-x64, windows-x64, linux-arm64
   and macos-arm64 - and a `.zip` for Windows, the source, the seed with its `.sha256`, the VS Code extension
   `torbscript-0.2.0.vsix`, `SHA256SUMS` and
   `SHA256SUMS.sig`. The release is a draft until its last asset is uploaded, so the webhook reaches release-sync once,
   with everything there. The seed is added to `seeds.txt` of the release `seeds`, the job `images` pushes
   `cr.torb.dev/torbscript/release-sync`, `site` and `registry` tagged with the version and `latest`, signed, and
   `publish-packages` pushes the Homebrew formula and the Scoop manifest where the release has their archives. The root
   server takes the images with `docker compose pull && docker compose up -d`.
7. **Check the extension in the stores.** The job `publish-extension` publishes the `.vsix` of the release to the Visual
   Studio Marketplace and to Open VSX; its log says "not published" for a store whose secret is not set. The
   Marketplace verifies an upload for a few minutes before
   [its page](https://marketplace.visualstudio.com/items?itemName=torbscript.torbscript) shows the version; Open VSX
   shows it at once ([open-vsx.org/extension/torbscript/torbscript](https://open-vsx.org/extension/torbscript/torbscript)).

**Which targets a release carries**: all four tier 1 targets. linux-x64 is built on the forge's runner, and so is each
of windows-x64, linux-arm64 and macos-arm64 whose runner the repository variable `TORB_RUNNERS` names; the others are
cross-compiled on the linux-x64 runner by a pinned `zig cc` (the jobs `cross-compiled on linux-x64 (<target>)`,
[RELEASE.md section 13, "Cross-compiled archives"](../design/RELEASE.md#cross-compiled-archives-2026-09-29)). A
cross-compiled binary is checked and started under wine (windows-x64) or qemu-user (linux-arm64) before anything is
published, macos-arm64's is checked and not started, and the release notes name every target that was cross-compiled.
None of them has passed the conformance suite on its machine: that is what the GitHub mirror does for each target's own
build after every push, and every morning its job `download` installs the newest nightly on real windows-x64,
linux-arm64 and macos-arm64 machines and runs it - look there before a release.

**The nightly's extension** is published the same way, automatically, by `nightly.yml`'s own `publish-extension`: once
the nightly's release is out, its `.vsix` goes to both stores as a pre-release - `vsce verify-pat torbscript` and
`ovsx verify-pat torbscript` run first, so a wrong publisher or namespace fails with a clear message rather than an
obscure one from `publish`, and `--skip-duplicate` passes over a second nightly of the same day. Nothing to check by
hand unless a store's log says otherwise; the store version comes from `tools/package-extension.sh`
(`MAJOR.(MINOR+1).YYYYMMDD`), not from `package.json`.

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
- **A job `cross-compiled on linux-x64 (<target>)` is red**, and nothing was published. "no source had
  zig-x86_64-linux-...": neither a mirror of Zig nor ziglang.org answered with the pinned archive - re-run the job. A
  red check of `tools/cross.sh` (a DLL, a dynamic binary, no signature) is a real finding about the build; a red smoke
  test under wine or qemu-user is one too until it is shown to be the emulator's - then the same smoke test of that
  target's native build on the GitHub mirror decides. A new Zig is a new pin in `tools/fetch-zig.sh`: the version and
  the four SHA-256 of `https://ziglang.org/download/index.json`, with each archive's minisign signature checked against
  the key on ziglang.org/download.
- **Forgejo ignores a job that has a `permissions:` key** ("not supported, the job is ignored"). The workflows of
  `.forgejo/` have none; a write goes through an Authorized Integration (`.forgejo/actions/token`).
- **A release needs a `LICENSE`.** The check refuses a release without one.
- **`fetch-seed.sh` answers "no index had the seed"**: neither the forge nor GitHub lists it. The forge's index is
  public; GitHub's needs `GH_TOKEN` while that repository is private.
- **A breaking change pushed in one go** fails CI at the bootstrap: the published seed does not know the new form. Push
  the teaching commit, run `seed`, then push the migration.
- **`publish-extension` failed** after the release or the nightly is out: the release stays as it is. A store that
  refused a token (401, "Access Denied", or an expired token - an Azure DevOps token lives a year at most) needs a new
  token in its secret (step 9 of "One-time setup"); then re-run that job alone, which passes over the store that has
  the version already. Both stores keep a published version for good, so the fix of a release's broken extension is
  the next patch release - a nightly's fixes itself the next night, with a store version of its own.
- **The job `extension` of CI is red** with "package.json says version ... and project.trb ...": step 2 above wrote the
  number into the manifests but not into `editors/vscode/package.json`.

## Full example

The release of 0.2.0, from a green `main`:

```console
$ git switch main
$ git pull forgejo main
$ grep -E '^version|^  "version"' project.trb compiler/project.trb editors/vscode/package.json
project.trb:version = "0.2.0"
compiler/project.trb:version = "0.2.0"
editors/vscode/package.json:  "version": "0.2.0",
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
   would wait, which is why the workflows run those jobs only for the targets the variable names. A target the variable
   names is no longer cross-compiled: its release binary is built and gated on its runner.
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
9. **The stores of the VS Code extension**, so that `publish-extension` of a release, and of every nightly, publishes
   `torbscript.torbscript`. Until both secrets exist the job skips the store without one and says so; nothing else
   waits for them.

   **The Visual Studio Marketplace**, with one Microsoft account for all three steps:

   1. The token: sign in at `https://dev.azure.com` (create an Azure DevOps organization when asked - any name, it only
      holds the token), then User settings (the icon beside the avatar, top right) -> Personal access tokens -> New
      Token. Name `torbscript-vsce`; Organization **All accessible organizations** (a token of one organization is
      refused by the Marketplace); Expiration: the longest offered, one year - note the date, a release after it fails
      until the token is renewed; Scopes: Custom defined -> Show all scopes -> **Marketplace: Manage**. Create, and copy
      the token: it is shown once.
   2. The publisher: `https://marketplace.visualstudio.com/manage` -> Create publisher: ID `torbscript` (it cannot be
      changed, and it is the first half of `torbscript.torbscript`), Name `TorbScript`. Optionally, the publisher's
      "Verified domain" with `torb.dev` (a TXT record) gives the store page its verified mark.
   3. The check, on any machine with Node.js 22: `VSCE_PAT=<token> npx @vscode/vsce@4.0.0 verify-pat torbscript`
      answers that the token may publish as `torbscript`.

   **Open VSX** (the store of VSCodium, Cursor, Gitpod and the other editors built on VS Code's open source):

   1. Sign in at `https://open-vsx.org` with a GitHub account, and sign the Eclipse Foundation's Open VSX Publisher
      Agreement under Profile (it asks for an Eclipse account, which can be created there).
   2. The token: Profile -> Access Tokens -> Generate New Token, description `torbscript release`; copy it, it is
      shown once. It does not expire.
   3. The namespace: `OVSX_PAT=<token> npx ovsx@1.2.0 create-namespace torbscript` creates `torbscript`, owned by that
      account. To have it verified (the store's mark that the namespace belongs to the project), open an issue at
      `https://github.com/EclipseFdn/open-vsx.org/issues` asking for ownership of the namespace `torbscript`.

   **The secrets**: `torbscript/language` -> Settings -> Actions -> Secrets -> Add secret: `VSCE_PAT` = the Azure DevOps
   token, `OVSX_PAT` = the Open VSX token. Only `publish-extension` of `release` and of `nightly` reads them. A renewed
   token replaces
   the value of its secret; nothing else changes.

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
   - `REGISTRY_SIGNING_KEY`: the seed of the key every record of the index is signed with, which
     `docker compose run --rm registry signing-key` prints together with its public key (after step 4's pull). Keep a
     copy of the seed off the machine: a lost key cannot endorse its successor, and every client that trusts it refuses
     an index signed by another. The public key goes into `shippedKeys` of `compiler/src/package/registry.trb` in the
     release that opens the registry ([RELEASE.md section 7.14](../design/RELEASE.md#714-the-signed-index-as-built-2026-09-27)).
     Without the variable the registry signs with a development key it makes itself, and says so in its log.
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
   the same machine behind a hairpin - add `torb.dev` to that list. Verify it with a release, or with the periodic
   check, which runs when the container starts and every `RELEASE_SYNC_POLL_SECONDS` (default 300) and places the
   newest stable release and the nightlies of the last thirty days that are missing - the forge never delivers a failed
   webhook again - and look for `/srv/torb/download/<version>/`; `docker compose logs release-sync` says why one is
   missing - most often a signature that does not verify against `cosign.pub`, which is by design.
   `tools/release-sync/README.md` has every variable and the answers of the webhook.
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
11. **Changing the signing key**: `registry signing-key` makes the next one; its public key goes into
    `REGISTRY_SIGNING_KEY_SUCCESSOR` and `docker compose up -d` announces it in `index/config.trb`, endorsed by the
    current key, and into `shippedKeys` of the next release. Once that release is out, the new seed goes into
    `REGISTRY_SIGNING_KEY` and the successor variable is emptied. Replacing the key without announcing it makes every
    client that trusted the old one refuse the index.

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
