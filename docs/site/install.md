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

**There is no stable release yet.** Both installers fall back to the newest nightly when the `stable` channel is
still empty, print one line saying so, and record it: the toolchain then follows the nightly channel (see
"Upgrading" below) until a stable release exists, without anything more to type.

## Linux, macOS and FreeBSD

```console
$ curl -fsSL https://torb.dev/install.sh | sh
```

The script detects the target, downloads the archive of the newest release of the channel (`stable`, or the newest
nightly while there is none) and its `SHA256SUMS`, checks the hash, and unpacks the toolchain into
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
$ torb upgrade --channel nightly
```

`torb upgrade` installs the newest release of the channel beside the versions already installed and makes it the one in
use; it never runs by itself. `--list` shows what is installed and what the channel offers, and `--channel nightly`
switches to the nightly channel: the last commit of `main` on which every gate is green. Left on `stable` while there
is no stable release, it falls back to the newest nightly exactly as the installers do, and stays on the nightly
channel from then on.

## From source

The toolchain compiles itself from a seed: the README of [the repository](https://git.torb.dev/torbscript/language)
says how to build it from a checkout. [Run your first program](../guide/installing-and-running.md) goes on from an
installed `torb`.
