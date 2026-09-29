---
title: torb new
summary: torb new scaffolds a package or app from a template - git.torb.dev's package or app by default - or, offline, the built-in project.trb, src/main.trb and tests/main.test.trb; torb init does the same into the current directory.
kind: tooling
status: stable
order: 25
keywords:
  - torb new
  - torb init
  - template
  - template.trb
  - scaffold
  - project.trb
  - getting started
source:
  - compiler/src/cli/new.trb
  - compiler/src/cli/init.trb
  - compiler/src/cli/template.trb
---

`new` is where a package or an app starts: one command instead of forking a repository by hand and renaming it
everywhere it appears. `new <path>` creates the directory; [`torb init`](torb-init.md) fills the current one instead,
the way Cargo's `cargo new` and `cargo init` divide the same work. Both take the same flags, ask the same six
questions where nothing on the command line answers them, and fall back to the same built-in scaffold - a
`project.trb` with a name and a version, a `src/main.trb` that prints a greeting, and a `tests/main.test.trb` with one
passing test - where a template cannot be reached.

## Synopsis

```text
torb new <path> [flags]        A new directory, from a template or the built-in scaffold; refuses if <path> exists
torb init [flags]               The same, into the current, empty-enough directory; refuses to overwrite a file

  --template <name|url>         package (default), app, owner/repo, github:owner/repo, or a URL/file://; a
                                 local path (a directory, or an archive) works too - see "Where a template comes from"
  --name <name>                 The project's name, over the one derived from the directory
  --owner <owner>                The registry owner, over the one derived from `git config user.name`
  --ci github|forgejo|both|none  Which CI workflow(s) a template keeps (default: github)
  --license mit|apache-2.0|none  Which LICENSE a template keeps (default: mit)
  --git / --no-git               Whether to run `git init` (default: yes, where git is on the PATH)
  --yes                          Every question a flag does not answer takes its default; no prompts
  --offline                      Never reach the network: the built-in scaffold, even for the default template
```

## What it does

### The six questions

Without `--template` and on a terminal, `new` and `init` ask, in order, with a default shown and Enter accepting it:

1. **What are you building?** Package (a library to publish) or App (a command-line program) - only asked where
   `--template` does not already name one of the two, a URL, or `owner/repo`.
2. **Name?** Defaults to the directory's own name (`new`) or the working directory's (`init`).
3. **Registry owner?** Defaults to `git config user.name`, lowercased and turned into a valid owner - `your-name`
   where there is none, or `git` is not on the `PATH`.
4. **CI?** GitHub, Forgejo, both, or none - default GitHub.
5. **License?** MIT, Apache-2.0 or none - default MIT.
6. **Initialise a git repository?** Default yes, where `git` is on the `PATH`.

Then a short summary and "Create? [Y/n]" before anything is written. Off a terminal (a pipe, CI) or with `--yes`,
every question not answered by a flag takes its default and nothing is asked - the summary still prints, and the
answer to "Create?" is always yes.

### Where a template comes from

`--template` (or the first question's answer) resolves to a place to fetch from:

| Written as | Resolves to |
|---|---|
| `package` (default), `app` | `torb/package-template`, `torb/app-template` of `git.torb.dev` |
| `owner/repo` | `owner/repo` of `git.torb.dev` (`$TORB_TEMPLATE_BASE_URL` instead, where set) |
| `github:owner/repo` | `owner/repo` of `github.com`, fetched from `codeload.github.com` |
| a `http://` or `https://` URL | that URL, downloaded as a `.tar.gz` |
| `file://...`, or anything else | a local path: a directory, copied as it is, or a file, read as a `.tar.gz` |

A repository is fetched as the tarball of its default branch (`main`, then `master`) - Forgejo's
`<repo>/archive/<ref>.tar.gz`, GitHub's `tar.gz/refs/heads/<ref>` of `codeload.github.com` - with `curl` or `wget`
(`torb upgrade` makes the same choice), bounded to a few seconds of connecting so an offline machine fails fast. The
archive's own top directory is stripped, the way `tar --strip-components 1` would, and `.git` is dropped if the
archive (or a local directory named this way) has one.

**`--offline`** skips the network outright and writes the built-in scaffold instead, even for the default template -
useful for a machine with none, or a script that must not depend on one. A local path or `file://` still reads: none
of it is the network, so it behaves the same with or without `--offline`.

**Failure falls back, and says so.** A template that cannot be reached or read - no network, a 404, a broken archive -
does not stop the command: `new` and `init` print why, then write the built-in scaffold.

### What a template does with the six answers

A template's own `template.trb`, read at its root the way `project.trb` (skill `torbscript-projects`: `references/tooling/project-trb.md`) itself is read - nothing in
it is evaluated - says which files to drop and which exact strings in which files become the name, the owner, or (with
nothing to give it yet) the description. See "Writing a template" below. A template without a `template.trb` is
copied as it is: nothing is dropped, nothing is substituted.

### Refusing to overwrite

`new` checks whether `<path>` already exists - as a file or as a directory - before writing anything, and refuses with
an error rather than merge into it or overwrite a part of it. `init` checks every file the template (or the built-in
scaffold) would write, and refuses the same way, listing every one that is already there, if any is.

### Where it finds `std` and the runtime

The built-in scaffold names no dependency and belongs to no workspace, so it is a standalone project the way any loose
file is: [`torb run`](torb-run.md) and `torb test` (skill `torbscript-testing`: `references/tooling/torb-test.md`) find the toolchain's own `std/` and `runtime/`
searched for above the new directory, the working directory and `torb` itself. A template found over the network may
declare dependencies of its own in its `project.trb`, which `torb check` and `torb run` then resolve normally.

## Writing a template

Any repository can be named by `--template` - a URL, `owner/repo`, `github:owner/repo` - and take part the way
`package` and `app` do, with a `template.trb` of its own at its root. It is read the static way `project.trb` is: a
few top-level fields, and two blocks of literal lines, nothing evaluated.

```trb fragment
title = "Package"
description = "a library to publish"
name = "hello"

replace {
  file "project.trb" {
    text "torb/hello", becomes: "owner/name"
  }
  file "README.md" {
    text "torb/hello", becomes: "owner/name"
  }
}

drop {
  path ".github", unless: "ci", isAnyOf: "github,both"
  path ".forgejo", unless: "ci", isAnyOf: "forgejo,both"
  path "LICENSE", unless: "license", isAnyOf: "mit"
}
```

- **`title`, `description`** - shown where the template is offered in a list; today only the built-ins are.
- **`name`** - the template's own default project name (`hello`, `hello-app`), where `--name` and the second question
  do not say otherwise.
- **`replace { file "<path>" { text "<exact string>", becomes: "<name|owner|owner/name>" } }`** - every exact
  occurrence of the string in that one file, and no other, becomes the answer named: `name` and `owner` alone, or
  `owner/name` together (`torb/hello` becoming `acme/widget` in one line). Small and explicit on purpose - a template
  is a handful of files, not a codebase to run a regular expression over. A file that does not decode as UTF-8 (an
  image) is copied unchanged, substitutions and all skipped.
- **`drop { path "<path>", unless: "<ci|license>", isAnyOf: "<comma-separated values>" }`** - the path (a file, or a
  directory and everything below it) is dropped unless the named question's answer is one of the values listed;
  `.git` is always dropped, whether `template.trb` says so or not.

A template without a `template.trb` is copied as it is - no drop, no substitution - which is how a plain "starter
repository" that predates this still works with `--template`.

## Examples

```console
$ torb new hello
What are you building?
  1. Package - a library to publish
  2. App - a command-line program
Choice [1]:
Name? [hello]:
Registry owner? [your-name]:
CI? GitHub, Forgejo, both, or none [GitHub]:
License? MIT, Apache-2.0 or none [MIT]:
Initialise a git repository? [Y/n]:
About to create `hello`:
  template: package
  name: hello
  owner: your-name
  ci: github
  license: mit
  git: yes
Create? [Y/n]:
wrote hello/ from the "package" template

cd hello
torb test
torb run
```

Off a terminal, or with `--yes`, nothing is asked and there is nothing to confirm:

```console
$ torb new hello --yes --offline
note: `--offline`: no network for a template of a repository - writing the built-in scaffold instead
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb

torb test
torb run
$ torb new hello --yes --offline
error: `hello` already exists
```

Pointing straight at the app template, without a question asked about which one:

```console
$ torb new greet --template app --yes
```

## Related

- [torb init](torb-init.md) - the same, into the current directory.
- [torb run](torb-run.md) - builds and executes the `src/main.trb` a new package starts with.
- torb test (skill `torbscript-testing`: `references/tooling/torb-test.md`) - builds and runs the `tests/main.test.trb` a new package starts with.
- project.trb (skill `torbscript-projects`: `references/tooling/project-trb.md`) - the manifest `new` writes, and what the toolchain reads out of it.
- RELEASE.md, "torb upgrade" - the other command that downloads with `curl` or
  `wget` and unpacks with `std/archive`.
- std/test (skill `torbscript-testing`: `references/standard-library/test.md`) - `test` and `group`, the two calls a `.test.trb` file makes.
- [Installing and running TorbScript](../guide/installing-and-running.md) - where `torb new` fits in the first steps.

