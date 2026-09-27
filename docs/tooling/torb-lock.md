---
title: torb lock
summary: torb lock writes the settings block of every member of a workspace into project.lock.trb from its evaluated manifest and keeps the graph as it is; with --check it writes nothing and fails where the file is not what it would write.
kind: tooling
status: stable
order: 107
keywords:
  - torb lock
  - lock --check
  - project.lock.trb
  - settings
  - deterministic
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/lock.trb
  - tools/gates.sh
  - docs/design/PROJECT.md#the-locked-manifest-installing-still-never-runs-code
---

`lock` writes the half of [project.lock.trb](project-lock-trb.md) that a consumer of a package reads: one `settings`
block per member of the workspace, printed from the member's evaluated `project.trb` as literals. It resolves nothing -
the `graph` belongs to [`torb add`](torb-add.md), [`torb remove`](torb-remove.md) and
[`torb update`](torb-update.md) - and it writes the same bytes for the same inputs, which `--check` is there to prove.

## Synopsis

```text
torb lock                    Write the settings of every member into the lock of the workspace
  --check                    Write nothing: fail where the file is not what torb lock writes
  --project <directory>      The project whose workspace to lock, instead of the one around the working directory
```

## What it does

### What it writes

The lock of the workspace root, with three parts:

- **The top-level `language`** is the root project's, where it names one.
- **A `settings "<name>" { }` block for every member that has a name**, sorted by name: its `language`, `prelude`,
  `dependencies`, `version`, `authors`, `description`, `license` and `repository`, as the evaluation of its
  `project.trb` answers them - a computed `version` as the value it computed - each written only where it differs
  from the default. `program` lines and `profile` blocks are not part of it: a consumer has no use for either yet.
- **The `graph`, as it is.** A graph that does not match `project.trb` - a dependency it does not pin, a pin the
  requirement does not allow - is refused with the same problems `torb check` reports, each with the `torb update`
  that fixes it, and nothing is written.

`lock` prints `wrote project.lock.trb`, or `project.lock.trb is current` where the file already holds exactly that and
nothing had to be written. Nothing else writes a `settings` block except [`torb publish`](torb-publish.md), for the
package it publishes; `torb check`, `build`, `run` and `test` never do, so a build does not change a checked-in file.

### `--check`

`--check` writes nothing. It builds the same text in memory and compares it with the file on disk byte for byte, and
where they differ it leaves with `1`, names the first ten lines that differ - each as it is on disk and as it would be
written - and says which command writes it:

```console
$ torb lock --check
error: project.lock.trb is not what `torb lock` writes
  line 4: on disk `  version = "0.1.0"`, written `  version = "0.2.0"`
  line 5: on disk `}`, written `  description = "A shop"`
  line 6: on disk ``, written `}`
  line 7: on disk nothing, written ``
  `torb lock --project .` writes it
```

It is a gate of tier A: `tools/gates.sh a` runs `lock --check` for the project of every `project.lock.trb` git knows.

## Pitfalls

**A `settings` block holds what the evaluation answered.** A `version` computed from the environment
(`Environment.get("CI_COMMIT_TAG")`) is written as the value it had when `lock` ran, and `lock --check` fails in an
environment where it computes another one. That is the lock doing its job: the published settings are a literal.

**The first line of the file names the commands that change the graph and `torb publish`.** `lock` writes it as it
is.

## Examples

The one lock of this repository, checked the way the gate checks it:

```console
$ torb lock --check --project tools/registry
tools/registry/project.lock.trb is current
```

What it holds - the service's settings from its `project.trb`, and a graph of one `path:` package:

```trb fragment
// project.lock.trb - written by `torb add`, `torb remove`, `torb update` and `torb publish`. Do not edit.

settings "torbscript/registry" {
  dependencies {
    runtime "torbscript/compiler"
  }
  version = "0.1.0"
  description = "The write service of packages.torb.dev: publish, yank, owners, tokens and trusted publishing"
  license = "MIT"
}

graph {
  package "torbscript/compiler", path: "../../compiler"
}
```

## Related

- [project.lock.trb](project-lock-trb.md) - the file, its parts and the order they are written in.
- [torb publish](torb-publish.md) - the other command that writes a `settings` block.
- [torb update](torb-update.md) - what writes the `graph` that `lock` keeps.
- [project.trb](project-trb.md) - the manifest a `settings` block is printed from.
