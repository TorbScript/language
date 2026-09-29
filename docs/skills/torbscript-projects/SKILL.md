---
name: torbscript-projects
description: "Sets up and maintains TorbScript projects: `project.trb` and `project.lock.trb`, dependencies (`torb add`, `remove`, `update`, `install`), workspaces, build profiles and native binaries, `torb doc`, and publishing to a registry. Use it when creating a TorbScript project or changing its manifest, dependencies, build or release."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs. Adding a package needs network access to its registry."
---

<!-- carry: tooling/project-trb.md tooling/project-lock-trb.md tooling/torb-add.md tooling/torb-remove.md -->
<!-- carry: tooling/torb-update.md tooling/torb-install.md tooling/torb-lock.md tooling/torb-publish.md -->
<!-- carry: tooling/torb-doc.md standard-library/project.md -->
<!-- carry: language/modules-and-packages/packages.md language/modules-and-packages/workspaces.md -->
<!-- carry: how-to/add-a-dependency.md how-to/set-up-a-workspace.md how-to/build-a-native-binary.md -->

# TorbScript projects and packages

A project is a directory with a `project.trb`, which is TorbScript run against a `Project` value, not a configuration
language. The names of the files say what a project produces: `src/main.trb` is its program, `src/lib.trb` what a
package exports, and every `*.test.trb` a test. The `torbscript` skill has `torb new` and the everyday commands; this
skill has the manifest, the lock file, dependencies, workspaces, builds and publishing. Paths are relative to the
directory of this file.

The manifest is edited by the commands where a command exists: `torb add` and `torb remove` rewrite `project.trb`,
resolve the workspace and write `project.lock.trb` in one step. Commit `project.lock.trb`.

## Adding a dependency

<!-- inline: how-to/add-a-dependency.md -->

## Pages

<!-- pages -->
