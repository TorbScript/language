---
title: The Agent Skills
summary: How torb docs skill turns this documentation into the TorbScript Agent Skills below skills/, which templates declare them, which page goes into which skill, and how the result is installed and checked.
kind: tooling
status: stable
skill: omit
order: 70
keywords:
  - agent skill
  - SKILL.md
  - progressive disclosure
  - install
  - plugin marketplace
source:
  - https://agentskills.io/specification
  - https://platform.claude.com/docs/en/agents-and-tools/agent-skills/best-practices
  - compiler/src/documentation/skill.trb
---

An [Agent Skill](https://agentskills.io/specification) is a directory whose `SKILL.md` carries a `name` and a
`description` in YAML front matter, a body that is loaded when the skill triggers, and further files that are read only
when they are needed. `torb docs skill` builds the TorbScript skills from this documentation, so that a model with no
training on TorbScript can write it correctly, and writes them to `skills/` at the root of the repository, where every
agent's installer finds them.

## Synopsis

```text
torb docs skill <root> <out>           Write every skill <root>/skills/ declares, one directory each below <out>
torb docs skill <root> <out> --check   Write nothing: report every file below <out> that is not what it would write
```

From the repository root, as the gate of tier A runs it:

```console
torb docs skill docs skills --check
torb docs skill docs skills
```

## What it does

### The set

```text
skills/
├ torbscript/                    The entry: toolchain, everyday commands, the mental model, verification, the map
│ ├ SKILL.md
│ ├ scripts/toolchain.sh         Finds torb, installs it with --install; toolchain.ps1 is the same for PowerShell
│ └ references/                  The guide, the toolchain, the cheat sheet, the mistakes models make
├ torbscript-language/           Every construct, the explanations, the language recipes
├ torbscript-standard-library/   Every package of std but the ones below
├ torbscript-projects/           project.trb, dependencies, workspaces, builds, publishing
├ torbscript-testing/            Test files and torb test
├ torbscript-concurrency/        Tasks, channels, streams, parallel pipelines
└ torbscript-networking/         HTTP, sockets, TLS, DNS, IP addresses, URIs
```

One entry skill with the basics that every task needs, and one skill per area an agent needs only for some tasks. The
split follows the sections of this documentation where it can and the kind of program where it cannot: a skill scoped
too narrowly makes one task load several, one scoped too broadly triggers where it should not.

### The templates

`docs/skills/<name>/` declares one skill. Its `SKILL.md` is the frame: the front matter as it is published, the few
sentences only an agent needs, and directives, each on a line of its own, that the builder expands. Every other file of
the directory - the scripts of the entry skill - is copied as it is. Nothing below `docs/skills/` is a page: `docs
check`, `docs index`, `docs site` and `docs bundle` never see it.

| Directive | What it becomes |
|-----------|-----------------|
| `<!-- carry: <path>... -->` | Nothing in the body: the pages it names are copied into `references/`. A path that ends in `/` is a folder and everything below it |
| `<!-- inline: <path> -->` | The body of that page, one heading level deeper, its links seen from `SKILL.md` |
| `<!-- pages -->` | One line per page of the skill, with its summary |
| `<!-- sections -->` | One line per folder index the skill carries, with its summary |
| `<!-- skills -->` | One line per other skill of the set, with the first sentence of its description |

The front matter uses only the fields of the format - `name`, `description`, `license`, `compatibility`, `metadata` and
`allowed-tools` - because a field one agent does not know fails another agent's validator. `name` is the name of the
directory; the `description` says what the skill does and when to use it, names the words that should trigger it and
stays below 1024 characters.

### Which page goes where

Every page belongs to at most one skill. The most specific `carry` rule decides: a page named on its own wins over its
folder, and a deeper folder over a shallower one, so `torbscript-language` can carry `language/` while
`torbscript-concurrency` carries `language/concurrency-and-streams/` and the entry skill the cheat sheet. Two skills
naming the same page or folder is a problem. A page that no rule reaches is a problem too, unless it says
`skill: omit` or is a page of the website or a lesson of the course, which only a rule naming the page itself carries.
So a new page cannot fall out of every skill without somebody deciding it.

### How a page is copied

- **A page is copied into `references/` at its path in the documentation, front matter included**, so a link between
  two pages of one skill resolves unchanged.
- **A link to a page of another skill keeps its text and names that skill**, and where that skill has the page. A
  skill can be installed on its own, so a path into another one is not a link it can promise:

  ```text
  See [Result](../errors/result.md).     becomes     See Result (skill `torbscript-language`: `references/...`).
  ```

- **A link to anything no skill carries keeps only its text**: a page marked `skill: omit`, a design record, a file of
  the repository outside `docs/`.
- **Every link of every Markdown file of a skill has to resolve inside that skill.** The builder checks it after writing
  the files in memory and fails on a dangling link instead of writing it.
- **`references/index.md` is generated**: every page of the skill with its path, its kind and its summary, grouped by
  folder, with a table of contents at the top.
- **Nothing is rewritten.** A body is made short by choosing what to inline, never by summarizing a page, because two
  versions of one rule is how a documentation starts to contradict itself.

### Progressive disclosure and the size limit

The three levels of the format are what the shape follows. The `name` and the `description` of every installed skill
are in the context of every request, so a description is a sentence or two. A `SKILL.md` is read when the skill
triggers and stays under 500 lines, which `docs skill` enforces. `references/` costs nothing until a file is opened, and
a body says which file answers which question rather than asking for anything to be read up front. When a body grows
past the limit, the fix is to carry a page instead of inlining it, not to compress the prose.

### Public-facing

The skills are for people who use TorbScript, not for the people who build it: nothing in them is about the compiler's
own build, the seed, the gates or the operations of this repository. Those are in `compiler/CONTRIBUTING.md` and
`CLAUDE.md`, and a page that is only about them says `skill: omit`.

## Examples

Rebuilding the skills after a change of `docs/`, which also removes a file a page no longer produces:

```console
$ torb docs skill docs skills
torbscript: SKILL.md has 374 lines, 41 pages
torbscript-concurrency: SKILL.md has 34 lines, 7 pages
...
269 files in skills, 35 pages in no skill on purpose
```

### How people install them

`docs/site/agents.md` has the line for every agent. The repository is a Claude Code plugin marketplace
(`.claude-plugin/marketplace.json`, one plugin whose source is `./skills`), a Codex plugin marketplace
(`.agents/plugins/marketplace.json` and `skills/.codex-plugin/plugin.json`) and an APM package (`apm.yml`), and
`npx skills` finds `skills/` on its own. A hidden entry directly below `skills/` belongs to such a manifest, and the
check leaves it alone.

### How this repository's agents load them

`.claude/skills/<name>` is a symbolic link to `../../skills/<name>` for each skill, so a Claude Code session in this
repository, and every agent it starts in a worktree, loads the skills of its own checkout under their plain names. A
new skill needs its link as well: `ln -s ../../skills/<name> .claude/skills/<name>`, committed with the skill.

## Related

- [The docs commands](checks.md) - the other commands and the gate.
- [How this documentation is structured](structure.md) - the tree the skills copy.
- [What the research decided](research.md) - the published guidance the shape follows, with its sources.
- [Verify your work](../tooling/verifying-your-work.md) - the page the entry skill inlines as its verification.
