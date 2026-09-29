---
title: Use TorbScript with your AI agent
summary: One command installs the TorbScript Agent Skills into Claude Code, Codex, OpenClaw, Copilot, Cursor or any other agent that reads the open skill format, so that the agent finds or installs torb and writes TorbScript that compiles.
kind: site
status: stable
order: 25
source:
  - docs/skills/torbscript/SKILL.md
  - .claude-plugin/marketplace.json
  - .agents/plugins/marketplace.json
  - apm.yml
---

A model that has never seen TorbScript writes Rust, Swift or Kotlin and gives the file a `.trb` ending. The TorbScript
[Agent Skills](https://agentskills.io) fix that: installed once, they teach an agent the language, check that `torb` is
installed - and install it with the official installer once you agree - and make the agent run every line it writes
through `torb check`, `torb format` and `torb test` before it hands the code over.

## The skills

| Skill | What it covers |
|-------|----------------|
| `torbscript` | The toolchain, the everyday commands, the rules of the language that models get wrong, and how to verify code. Every other skill builds on it |
| `torbscript-language` | The exact rules of every construct, and recipes for common tasks |
| `torbscript-standard-library` | Every package of `std`: what it holds and how it is imported |
| `torbscript-projects` | `project.trb`, dependencies, workspaces, builds and publishing |
| `torbscript-testing` | Test files, `test`, `group`, `assert` and `torb test` |
| `torbscript-concurrency` | Tasks, channels, streams and parallel pipelines |
| `torbscript-networking` | HTTP clients and servers, sockets, TLS, DNS and URIs |

An agent reads the name and the one-sentence description of each skill and loads a skill only when a task needs it:
nothing of them is in its context while it works on something else.

## Install them

Every command below installs all seven skills unless it says otherwise. The repository is
[git.torb.dev/torbscript/language](https://git.torb.dev/torbscript/language); the skills are its `skills/` directory.
Its public mirror [github.com/TorbScript/language](https://github.com/TorbScript/language) has the same skills, so an
agent that takes a GitHub repository by its short name installs them as `TorbScript/language` as well.

### Claude Code

In a session:

```text
/plugin marketplace add https://git.torb.dev/torbscript/language.git
/plugin install torbscript@torbscript
```

Or from a shell, checking out only what the plugin needs:

```console
$ claude plugin marketplace add https://git.torb.dev/torbscript/language.git --sparse .claude-plugin skills
$ claude plugin install torbscript@torbscript
```

`/plugin marketplace add TorbScript/language` (or `claude plugin marketplace add TorbScript/language`) adds the same
marketplace from the GitHub mirror.

The skills are then named after the plugin: `torbscript:torbscript`, `torbscript:torbscript-testing`, and the five
others the same way.
`claude plugin marketplace update torbscript`, then `claude plugin update torbscript@torbscript`, fetches a newer
version.

### Codex

```console
$ codex plugin marketplace add https://git.torb.dev/torbscript/language.git
$ codex plugin add torbscript@torbscript
```

`codex plugin marketplace add TorbScript/language` adds the same marketplace from the GitHub mirror, and `/plugins` in
a session does what the two lines do. Without the plugin system, the command of the next section with `-a codex`
copies the skills into `.agents/skills/` of the project. The skill installer built into Codex also takes a skill from
GitHub by its directory, one skill at a time, and the skill is there from the next turn:

```text
$skill-installer install https://github.com/TorbScript/language/tree/main/skills/torbscript
```

`skills/torbscript-language`, `skills/torbscript-testing` and the other directories install the rest the same way.

### OpenClaw, GitHub Copilot, Cursor, Gemini CLI and others

[`npx skills`](https://github.com/vercel-labs/skills) installs skills for more than seventy agents from any Git host:

```console
$ npx skills add https://git.torb.dev/torbscript/language
$ npx skills add TorbScript/language
```

The second line takes the same skills from the GitHub mirror. It asks which agents to install for; `-a openclaw`,
`-a github-copilot` or `-a cursor` names one, `-g` installs for your user instead of the project, and `--skill '*' -y`
takes every skill without asking.

### APM

```console
$ apm install https://git.torb.dev/torbscript/language.git
```

`apm install TorbScript/language` installs the same package from the GitHub mirror. The
[Agent Package Manager](https://microsoft.github.io/apm/) reads `apm.yml` at the root of the repository and deploys the
skills for the agent it finds in the project; where it finds several, `--target claude` (or `codex`, `copilot`,
`cursor`, `opencode`) says which.

### By hand

Copy the directories below `skills/` into the directory your agent reads skills from - `~/.claude/skills/` for Claude
Code, `~/.agents/skills/` for Codex, `skills/` of an OpenClaw workspace:

```console
$ git clone --depth 1 https://git.torb.dev/torbscript/language.git
$ cp -r language/skills/torbscript* ~/.claude/skills/
```

## What the agent does with them

Before its first `torb` command, the agent runs a small script of the `torbscript` skill that reports where `torb` is,
its version, and whether a C compiler is there for native builds. The script changes nothing. When `torb` is missing,
the agent asks you before it runs the same script with `--install`, which downloads the installer of
[Install TorbScript](install.md) and runs it. On Linux, macOS and FreeBSD the installer leaves your shell's profile
alone, and the agent tells you the line that puts `torb` on your `PATH`.
