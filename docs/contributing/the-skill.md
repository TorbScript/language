---
title: The Agent Skill
summary: How torb docs skill turns this documentation into an Agent Skill, what it copies, what it leaves out, and how to install the result.
kind: tooling
status: stable
skill: omit
order: 70
keywords:
  - agent skill
  - SKILL.md
  - progressive disclosure
  - install
source:
  - https://agentskills.io/specification
  - compiler/src/documentation/skill.trb
---

An [Agent Skill](https://agentskills.io/specification) is a folder whose `SKILL.md` carries a `name` and a
`description` in YAML front matter, a body that is loaded when the skill triggers, and further files that are read only
when they are needed. `torb docs skill` builds one from this documentation, so that a model with no training on
TorbScript can write it correctly.

## Synopsis

```text
torb docs skill <root> <out>
```

From the repository root, writing into the build directory, which is not in version control:

```console
torb docs skill docs build/skill/torbscript
```

The output directory has to be named `torbscript`, because the format requires the directory name and the `name` field
to be equal.

## What it does

### The shape

```text
torbscript/
├ SKILL.md                     name, description, and the body
└ reference/
  ├ index.md                   Every page with its path, its kind and its summary: one file to navigate by
  └ <the documentation tree>   Every page, copied unchanged
```

The three levels of the format are what the shape follows:

1. **`name` and `description`** are in the model's context at all times, at roughly 100 tokens. They decide whether the
   skill is used at all.
2. **`SKILL.md`** is read when the skill triggers. It stays under 500 lines, which is the published limit for the body,
   and the command fails when it does not.
3. **`reference/`** costs nothing until a file is read. `reference/index.md` is the one file worth reading first,
   because it holds every path and every summary.

### What goes into the body

Nothing in the body is written by the builder. Every section is the body of a page that carries the matching `skill`
role in its front matter, one heading level deeper so that it nests, and the builder names the page it came from. So the
skill cannot drift away from the documentation, and a change to the documentation changes the skill.

| Part of `SKILL.md` | Comes from the page with | How |
|--------------------|--------------------------|-----|
| The language in sixty seconds | `skill: model` | the body, inlined one heading level deeper |
| Read these two files first | `skill: cheat-sheet`, `skill: mistakes` | a path and one line each |
| How to verify your work | `skill: verify` | the body, inlined one heading level deeper |
| Where to look things up | the folder indexes | generated |

Two of the four roles are **inlined** and two are **pointed at**, and the split is not arbitrary. The mental model and
the verification loop are needed on every task and are short, so they belong in the body. The cheat sheet and the list of
mistakes are long lookup material: together they are more than 500 lines, which is the whole budget of the body. They sit
one `Read` away instead - one level deep from `SKILL.md`, which is what the format asks for - and `SKILL.md` says in one
sentence why to read them.

Each of the four roles belongs to exactly one page, and the checker enforces that. A missing role is reported by
`docs skill`, so the skill cannot quietly lose a part.

### What is copied and what is condensed

- **A page is copied unchanged, front matter included.** A condensed copy would be a second version of the same rules,
  and two versions of one rule is how a documentation starts to contradict itself. The front matter stays because
  `summary` and `status` are useful to a reader that arrives at a single file.
- **`reference/index.md` is generated, not copied.** It is one flat list of every page with its path, its kind and its
  summary, grouped by folder, with a table of contents at the top. A file longer than 100 lines needs one, so that a
  partial read still shows the whole scope. It takes the place of the documentation root index, whose body would say the
  same thing less completely.
- **Nothing is rewritten.** The body is condensed by choosing which pages to inline, never by summarizing them.

### What is left out

- **Every page with `status: planned`.** A model that reads a designed feature as an available one writes code that
  cannot compile, so a planned page is not in the skill at all - not in `reference/`, not in the index, not counted.
- **Every page with `skill: omit`.** That is how a page that is about the documentation itself, rather than about the
  language, stays out. All of `contributing/` is marked this way.
- **A `status: draft` page stays in** and keeps its draft banner, so the model reads the warning in the same file as the
  content.

### How the size limit is respected

`SKILL.md` has to stay under 500 lines. The command counts the lines it wrote and reports a problem when it does not,
which means the limit is a gate and not advice - the first build of this skill came to 863 lines and failed, which is how
the split between inlined and pointed-at parts was decided. When the body grows past the limit again, the fix is
hierarchy rather than denser prose: move a part out of the body and into the list of files to read. Compressing the body
instead is what produces a skill that is short and wrong.

The description is capped at 1024 characters by the format. The one here stays far below that, because it is in the
context of every request and not only of the ones that use it.

## Examples

Build the skill and install it for yourself:

```console
$ torb docs skill docs build/skill/torbscript
torbscript: 33 files, SKILL.md has 323 lines
32 pages, 10 left out (planned or omitted)
Copy `build/skill/torbscript` to `~/.claude/skills/torbscript` for yourself, or to `.claude/skills/torbscript` of a project to share it
```

The two places a skill is installed:

| Where | Path | Who sees it |
|-------|------|-------------|
| Personal | `~/.claude/skills/torbscript/` | Every session of this machine |
| A project | `<project>/.claude/skills/torbscript/` | Everybody who checks the project out |

The generated skill is an **output**. It goes under `build/`, which `.gitignore` excludes, and it is never edited by
hand: an edit there is lost on the next build. What is edited is the page the section came from.

## Related

- [The docs commands](checks.md) - the other three commands and the gate.
- [How this documentation is structured](structure.md) - the tree the skill copies.
- [What the research decided](research.md) - the published guidance the shape follows, with its sources.
- [Verify your work](../tooling/verifying-your-work.md) - the page that becomes the verification section.
