---
title: Build the toolchain from source
summary: A checkout builds torb with one script - it takes a seed, the torb of an earlier commit, builds the compiler with it, builds it again with the result, and compares the two.
kind: how-to
status: stable
skill: omit
order: 70
keywords:
  - build from source
  - bootstrap
  - seed
  - fixpoint
  - fetch-seed
  - C compiler
source:
  - README.md#building
  - tools/bootstrap.sh
  - tools/fetch-seed.sh
---

Somebody who only writes TorbScript installs it with the [installer](../site/install.md) and never builds it. This page
is for working on the toolchain itself. The compiler is written in TorbScript and compiles itself, so building it takes
a `torb` that already exists: the **seed**.

## Steps

1. **Have a C compiler, and `curl` or `wget`.** The compiler is `$TORB_CC`, or the first of `clang`, `gcc` and `cc` on
   the `PATH`. On Windows the scripts run in Git Bash.
2. **Clone the repository** from [git.torb.dev](https://git.torb.dev/torbscript/language).
3. **Run the bootstrap** from the root of the checkout:

   ```console
   $ sh tools/bootstrap.sh
   ```

   It takes the first seed it finds: `$TORB_SEED`, then `seed/`, then the newest archive in `../torbscript-seeds/`,
   then `build/seed/`. A fresh clone has none of them, so the script runs `sh tools/fetch-seed.sh`, which downloads the
   newest published seed, checks its SHA-256 and compiles it into `build/seed/torb`. `TORB_SEED_FETCH=0` forbids the
   download.
4. **Use `build/release/torb`.** That is the compiler that comes out. It finds the `std/` and `runtime/` of its
   checkout by itself.
5. **Run the gates before a commit:** `sh tools/gates.sh a` on every change, and `sh tools/gates.sh b` as well when
   the change touches the IR, a back end or `runtime/` ([compiler/CONTRIBUTING.md](../../compiler/CONTRIBUTING.md)).

The build takes two steps, not one. The seed builds the current sources; the binary that comes out builds them again;
and the two generated C programs have to be identical. That comparison is the **fixpoint**: it proves that the compiler
builds itself the same way the seed built it. Only then does `build/release/` get the new binary.

## Full example

A fresh clone on a machine with no seed:

```console
$ git clone https://git.torb.dev/torbscript/language.git torbscript
$ cd torbscript
$ sh tools/bootstrap.sh
no seed on this machine: fetching the newest published seed (sh tools/fetch-seed.sh)
seed: build/seed/torb
step 1: the seed builds the compiler
step 2: that compiler builds the compiler again

the fixpoint holds: both steps emitted the same C.
torb: build/release/torb
$ build/release/torb check examples/tour
14 files, no problems
```

A checkout that already has a `torb` elsewhere on the machine names it instead of downloading one:

```console
$ TORB_SEED=/path/to/torb sh tools/bootstrap.sh
```

## Related

- [Install TorbScript](../site/install.md) - the installer, for everybody who does not work on the toolchain.
- [Cut a release](releasing.md) - how seeds and releases are published.
- [README.md](../../README.md) - the layout of the repository and the two kinds of seed.
