# release-sync

Pulls the releases of the forge onto the machine that serves `https://torb.dev/download/`
([RELEASE.md section 7.11](../../docs/design/RELEASE.md#711-hosting-and-the-server)): the server pulls, CI never pushes.
It listens for the forge's "release published" webhook, looks for anything the webhook missed at its start and every
few minutes, and places every release it can verify under `<root>/download/<version>/`, where the installers, `torb
upgrade` and the site find it. The forge is Forgejo at git.torb.dev; GitHub, which mirrors it, is one variable away.

## What it does

For one release - named by a webhook or found by the periodic check - it:

1. **asks the forge's API for the release** (`GET .../releases/tags/<tag>`, or the list the periodic check read). The
   webhook's own payload names the assets too, but nothing signs it, so it is only ever used for the tag;
2. **downloads every asset** with `curl` into a directory of its own in the scratch directory, which only its owner may
   read while nothing in it is verified;
3. **verifies** every archive against `SHA256SUMS` (`std/digest`), and `SHA256SUMS` itself with `cosign verify-blob`
   against the project's public key - `SHA256SUMS.sig` for a release of the forge, GitHub's keyless
   `SHA256SUMS.sigstore.json` and the identity of the workflow that made it for a release of GitHub. **No `cosign` on
   the machine, no key, or a signature that does not verify: the release is refused and nothing is placed;**
4. **places it**: the verified directory is renamed to `<root>/download/<version>` in one step, so the web server finds
   a version complete or not at all - never empty, never half filled. A directory already under that name (the release
   published again) is set aside first and removed once the new one is in place;
5. **lists it** in `<root>/download/versions.txt`, rewritten whole or not at all, each channel newest first and the
   stable releases before the nightlies - `install.sh` and `install.ps1` take the first line of their channel - and,
   when it is the newest stable release, points the symbolic link `<root>/download/latest` at it: a new link beside it,
   renamed over the old one, so `latest` is never missing. A nightly has no `latest`.

A placement that fails after the rename - `versions.txt` or `latest` cannot be written - is undone: the version's
directory goes back to the scratch directory, the old one and the old `versions.txt` come back, and the periodic check
tries the release again. Whatever an attempt leaves in the scratch directory is removed when it ends, and what an
attempt that never ended left (the program was stopped mid-download) is removed when the program starts.

The log is standard output and standard error, one line per event: `release-sync: synced v0.2.0`, or why a release was
refused. Every line is written the moment it happens, into a pipe as into a terminal, so `docker compose logs
release-sync` shows it at once.

## Configuration

Everything comes from the environment (`src/config.trb`); `tools/deploy/compose.example.yml` sets it, and
`tools/deploy/.env.example` lists what a deployment fills in.

| Variable | Default | What |
|---|---|---|
| `RELEASE_SYNC_FORGE` | `forgejo` | where the releases are published: `forgejo` or `github` |
| `RELEASE_SYNC_URL` | `https://git.torb.dev` (`https://api.github.com` for `github`) | the forge, or GitHub's API |
| `RELEASE_SYNC_REPOSITORY` | `torbscript/language` (`TorbScript/language` for `github`) | `owner/name` on the forge |
| `RELEASE_SYNC_TOKEN` | none | a token that reads the repository's releases, needed only while the repository is private; never one with write access - this program never publishes |
| `RELEASE_SYNC_WEBHOOK_SECRET` | none | the secret the forge's webhook is configured with. **Without it every request to `/webhook` is taken unchecked** - which does no harm beyond a sync that was not due, since a sync verifies everything and trusts nothing the request says, but every deployment sets it |
| `RELEASE_SYNC_COSIGN_KEY` | `/etc/release-sync/cosign.pub` (none for `github`) | the public half of the project's signing key (`tools/deploy/cosign.pub`); without a key a release of GitHub is checked against its keyless signature instead |
| `RELEASE_SYNC_ROOT` | `/srv/torb` | `<root>/download/` is where the releases go |
| `RELEASE_SYNC_LISTEN` | `0.0.0.0:8088` | what the webhook's server binds, `host:port` |
| `RELEASE_SYNC_POLL_SECONDS` | `300` | how often the periodic check runs, in seconds |
| `RELEASE_SYNC_SCRATCH` | `<root>/download/.incoming` | where a release is downloaded and verified before it is placed; see below |

### The scratch directory

A verified release is placed by **renaming its directory**, and no system renames across file systems: the scratch
directory has to be on the file system of `<root>/download/`. The default, `<root>/download/.incoming`, is inside it,
which in the container is the one directory mounted from the host - the container's own `/tmp` is not, and a rename
from there fails with `Invalid cross-device link`. A scratch directory on another file system fails the same way, and
the message names `RELEASE_SYNC_SCRATCH`.

Being inside `download/` is safe because the site never serves a hidden entry there (`tools/deploy/nginx.conf` answers
`404` for every path below `/download/` with a name that starts with a dot, which also covers the `.<version>.replaced`
and `.latest-<version>` names placing uses for a moment), and because a download is readable by its owner alone until
it is verified. The nightly backup leaves the hidden entries out (`tools/deploy/backup.sh`).

## The webhook

`POST /webhook`, with the release event of the forge as its JSON body; every other path is `404`. On the forge:
Settings -> Webhooks -> Forgejo, target `https://torb.dev/webhook`, content type `application/json`, the secret of
`RELEASE_SYNC_WEBHOOK_SECRET`, triggered by release events (`docs/contributing/releasing.md`, "The root server").

The signature is the HMAC-SHA256 of the raw body with the secret, in one of two headers:

| Header | Sent by | Value |
|---|---|---|
| `X-Forgejo-Signature` | Forgejo | the bare hex |
| `X-Hub-Signature-256` | GitHub, and Forgejo as well | `sha256=<hex>` |

Forgejo sends both, and its own is the one read. The digest is computed with `openssl dgst -sha256 -hmac` and compared
without stopping at the first digit that differs.

| Answer | When |
|---|---|
| `202 syncing <tag>` | the signature matches and the action is `published`: the sync runs in a task of its own, because the forge gives a delivery seconds and a download takes longer - what happened is in the log |
| `200 ignored: action "<action>"` | any other action (`created`, `edited`, `deleted`, ...) |
| `401 bad signature` | no signature, or one that does not match |
| `400` | a body that cannot be read or is not a release event |
| `500` | `openssl` could not compute the digest |

The forge does not deliver a failed webhook again by itself, which is what the periodic check is for.

## The periodic check

When the program starts and then every `RELEASE_SYNC_POLL_SECONDS`, it asks the forge for its newest releases
(`GET .../releases?draft=false&limit=50` on Forgejo, `?per_page=50` on GitHub) and syncs, oldest first, every one of
these that is not placed yet:

- the newest stable release - neither a draft nor a prerelease, the highest version;
- every nightly (`nightly-YYYYMMDD`) of the thirty days that end with the newest nightly listed - the thirty days the
  forge keeps a nightly for (RELEASE.md, "Channels"). The window counts from the newest nightly and not from the
  machine's clock.

A release counts as placed once `<root>/download/<version>/SHA256SUMS` is there. That also sees through the empty
directory a failed placement left before placing was one rename, and since a failed placement leaves nothing under the
version's name now, the next check tries it again. A check with nothing to do - also while the repository has no
release at all - says nothing; one that cannot reach the forge says so and tries again at the next.

## Running it locally

The tests need nothing but `torb`:

```console
$ build/release/torb check tools/release-sync
$ build/release/torb test --native tools/release-sync
```

`tests/check.test.trb` runs the periodic check against a fake forge over loopback, `tests/place.test.trb` places
releases into temporary directories - the rename, `versions.txt`, `latest`, and the placements that fail and are undone
- and `tests/forge.test.trb`, `tests/verify.test.trb` and `tests/versions.test.trb` cover the pure decisions.

The program itself needs `curl`, `openssl` and `cosign` on the `PATH`. Against the real forge, into a directory of your
own:

```console
$ mkdir -p /tmp/torb-root/download
$ RELEASE_SYNC_ROOT=/tmp/torb-root RELEASE_SYNC_LISTEN=127.0.0.1:8088 RELEASE_SYNC_WEBHOOK_SECRET=local \
    RELEASE_SYNC_COSIGN_KEY=tools/deploy/cosign.pub build/release/torb run --native tools/release-sync/src/main.trb
```

It places the newest releases at once, through the check at its start. A webhook delivery by hand, signed the way the
forge signs it:

```console
$ body='{"action":"published","release":{"tag_name":"v0.1.0"}}'
$ signature=$(printf '%s' "$body" | openssl dgst -sha256 -hmac local | sed 's/.*= //')
$ curl -X POST http://127.0.0.1:8088/webhook -H "X-Forgejo-Signature: $signature" -d "$body"
```

On Windows a program may create the symbolic link `latest` only in developer mode or with the privilege to; without
either, placing a stable release fails with `Operation not permitted` and is undone.

## Deployment

The image `torbscript/release-sync` of the forge's container registry is built from each release's own assets by
`tools/deploy/Dockerfile.release-sync` (`.forgejo/workflows/images.yml`), with exactly the tools the program calls:
`curl`, `openssl` and a pinned `cosign`. `tools/deploy/compose.example.yml` runs it behind the Traefik of the
machine, as user 1000, with `<TORB_DATA>/download` mounted read-write at `/srv/torb/download` - it is the only writer
there, and the site mounts the same directory read-only - and the public key mounted at
`/etc/release-sync/cosign.pub`, never baked in. Its router claims `torb.dev/webhook` with an explicit priority:
Traefik ranks routers by the length of their rule otherwise, and the site's rule for `torb.dev` is the longer one.
While the container is down - or not healthy yet, where a deployment gives it a health check - Traefik has no route to
it and a delivery of the webhook is lost; the check at the start places what it missed.

How the server is set up once - DNS, `.env`, the webhook, the first sync - is `docs/contributing/releasing.md`, "The
root server"; why it is built this way is
[RELEASE.md section 7.11](../../docs/design/RELEASE.md#711-hosting-and-the-server).
