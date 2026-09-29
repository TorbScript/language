---
title: Install TorbScript
summary: One command installs the toolchain for the current user on Linux, macOS, FreeBSD and Windows, checked against the release's hashes, and torb upgrade keeps it current.
kind: site
status: stable
order: 20
source:
  - tools/install.sh
  - tools/install.ps1
  - compiler/src/cli/upgrade.trb
---

The toolchain is one binary, `torb`, with the standard library and the C runtime beside it. Both install scripts are
short and readable, and they are served from torb.dev: read one before you run it.

**The first stable release, 0.1.0, comes on 2026-10-06**, and a new minor on the first Tuesday of every month after it,
each a week earlier as a release candidate. Until 0.1.0 is out both installers fall back to the newest release
candidate - and without one to the newest nightly - print one line saying which, and record that channel: the
toolchain then follows it (see "Channels" below), without anything more to type.

## Linux, macOS and FreeBSD

```console
$ curl -fsSL https://torb.dev/install.sh | sh
```

The script detects the target, downloads the archive of the newest release of the channel (`stable`, or while there is
none the newest release candidate, else the newest nightly) and its `SHA256SUMS`, checks the hash, and unpacks the
toolchain into
`~/.torb/toolchains/<version>/`, with `~/.torb/bin/torb` the one in use. It writes nothing into the profile of a
shell: it prints the line that puts `~/.torb/bin` on the `PATH`, for you to add yourself. It needs `curl` or `wget`,
`tar`, and `sha256sum` or `shasum`.

To read the script before running it rather than piping it into `sh`, download it first:

```console
$ curl -fsSLO https://torb.dev/install.sh
$ sh install.sh
```

## Windows

In PowerShell 5.1 or later:

```console
> irm https://torb.dev/install.ps1 | iex
```

The script does the same for Windows: it unpacks the toolchain into `%LOCALAPPDATA%\torb\toolchains\<version>\`, copies
the one in use to `%LOCALAPPDATA%\Programs\torb`, and adds that directory to the `PATH` of the user.

To read the script before running it rather than piping it into `iex`, download it first:

```console
> irm https://torb.dev/install.ps1 -OutFile install.ps1
> powershell -ExecutionPolicy Bypass -File install.ps1
```

## Channels

| Channel | What it installs |
|---|---|
| `stable` | the newest release, `0.1.0` - the default |
| `preview` | the newest release candidate, `0.2.0-rc.1`, out a week before its release - or the newest stable release where that is newer |
| `nightly` | the last commit of `main` on which every gate is green, `nightly-20261015` |

Both installers take another channel from a variable:

```console
$ curl -fsSL https://torb.dev/install.sh | TORB_INSTALL_CHANNEL=preview sh
```

```console
> $env:TORB_INSTALL_CHANNEL = "preview"; irm https://torb.dev/install.ps1 | iex
```

A release candidate becomes the release unchanged unless something blocks it, so the preview channel is where a
release can be tried a week early. In the Marketplace and Open VSX, the extension's stable version follows the stable
releases and its pre-release version follows the nightly; neither store carries a release candidate, since its
version number would collide with the stable release that follows it.

## With an AI agent

[Use TorbScript with your AI agent](agents.md) installs the TorbScript Agent Skills. An agent that has them checks for
`torb` itself and runs this installer once you agree.

## A C compiler for native builds

`torb run` runs a program in the VM inside `torb` and needs nothing else. `torb build` and `torb run --native` hand C to
a C compiler: `$TORB_CC`, or the first of `clang`, `gcc` and `cc` on the `PATH`.

## Upgrading

```console
$ torb upgrade
$ torb upgrade --list
$ torb upgrade --channel preview
```

`torb upgrade` installs the newest release of the channel beside the versions already installed and makes it the one in
use; it never runs by itself. `--list` shows what is installed and what the channel offers, and `--channel` switches
to `stable`, `preview` or `nightly` and stays there. Versions are ordered as semantic versions, so `0.1.0-rc.1` comes
before `0.1.0`. Left on `stable` while there is no stable release, it falls back to the newest release candidate,
else to the newest nightly, exactly as the installers do, and stays on that channel from then on; a toolchain on the
preview channel moves on to `0.1.0` once it is out.

## From source

The toolchain compiles itself from a seed. [Build the toolchain from source](../contributing/building-from-source.md)
builds it from a checkout of [the repository](https://git.torb.dev/torbscript/language) with `sh tools/bootstrap.sh`.
