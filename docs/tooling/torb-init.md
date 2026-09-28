---
title: torb init
summary: torb init fills the current, empty-enough directory with a template - or the built-in scaffold - the way torb new fills a new one, and refuses to overwrite a file that is already there.
kind: tooling
status: stable
order: 26
keywords:
  - torb init
  - template
  - scaffold
  - project.trb
  - getting started
source:
  - compiler/src/cli/init.trb
  - compiler/src/cli/template.trb
---

`init` is [`torb new`](torb-new.md) for a directory that already exists: the same template, the same six questions,
the same built-in scaffold where a template cannot be reached - into `.` instead of a new directory. Cargo's
`cargo init` beside its `cargo new` is the model.

## Synopsis

```text
torb init [flags]   Fills the current directory; refuses to overwrite a file that is already there

  --template <name|url>          package (default), app, owner/repo, github:owner/repo, or a URL/file:// - see torb new
  --name <name>                   The project's name, over the one derived from the working directory
  --owner <owner>                  The registry owner, over the one derived from `git config user.name`
  --ci github|forgejo|both|none    Which CI workflow(s) a template keeps (default: github)
  --license mit|apache-2.0|none    Which LICENSE a template keeps (default: mit)
  --git / --no-git                 Whether to run `git init` (default: yes, where git is on the PATH)
  --yes                            Every question a flag does not answer takes its default; no prompts
  --offline                        Never reach the network: the built-in scaffold, even for the default template
```

Every flag, every question and where a template comes from is [`torb new`](torb-new.md#where-a-template-comes-from);
this page is only what differs.

## What it does

### The one difference from `torb new`

`torb new <path>` creates `<path>` and refuses if it exists; `torb init` takes no path - it always means the working
directory - and does not require it to be empty. What it does require: none of the files the chosen template (or the
built-in scaffold) would write may already be there. Where one is, `init` refuses and lists every one that conflicts,
writing nothing - the same all-or-nothing rule `torb new` gets for free from a directory that cannot exist yet.

The project's name defaults to the working directory's own name, the way `torb new`'s defaults to the directory it is
about to create.

## Examples

```console
$ mkdir hello && cd hello
$ torb init --yes
wrote the "package" template into the current directory

torb test
torb run
```

A directory that already has a `project.trb` refuses rather than merge into it:

```console
$ torb init --yes
error: already exists: project.trb
```

## Related

- [torb new](torb-new.md) - the same, into a new directory; templates, `template.trb`, and every flag in full.
- [project.trb](project-trb.md) - the manifest `init` writes, and what the toolchain reads out of it.
