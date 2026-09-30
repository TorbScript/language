---
title: The website
summary: The pages torb.dev shows around the documentation - the front page, the install page and the page for AI agents - which torb docs site writes at the root of the site.
kind: index
status: stable
order: 100
skill: omit
---

These pages are the website around the documentation: [torb docs site](../tooling/torb-docs-site.md) writes each of
them at the root of torb.dev instead of below `/docs/<version>/`, with a layout of its own for the front page.

## What belongs here

Pages of the website that are not documentation: the front page (`home.md`, written as `/`), the install page
(`install.md`, as `/install`), the page about the Agent Skills (`agents.md`, as `/agents`), the playground (`play.md`,
as `/play`, which the header links where it exists), the benchmarks (`benchmarks.md`, as `/benchmarks`, which the
footer links and whose numbers the nightly measures), and the imprint and the privacy notice (`imprint.md` and
`privacy.md`, as `/imprint` and `/privacy`), which the site links in its footer only where the page exists. Each has the
kind `site`, and none of them is carried by the skills or by `llms-full.txt`. Their translations are below
`translations/<language>/site/`.

What does not belong here: anything about the language or the toolchain, which is in the sections of the documentation,
and the release notes, which will have `docs/releases/`.

<!-- torb:index:begin -->

## Pages

- **[TorbScript](home.md)** - A programming language for scripts, tools and servers. It finds mistakes before your program runs, starts at once while you write, and builds a fast program when you ship.
- **[Install TorbScript](install.md)** - One command installs the toolchain for the current user on Linux, macOS, FreeBSD and Windows, checked against the release's hashes, and torb upgrade keeps it current.
- **[Use TorbScript with your AI agent](agents.md)** - One command installs the TorbScript Agent Skills into Claude Code, Codex, OpenClaw, Copilot, Cursor or any other agent that reads the open skill format, so that the agent finds or installs torb and writes TorbScript that compiles.
- **[Playground](play.md)** - Write TorbScript and run it in your browser - the toolchain as WebAssembly checks, compiles and runs the program on your machine and completes as you type, with no server and no account, and the address is the link to share it.
- **[How fast is TorbScript?](benchmarks.md)** - Six well-known programs, each in TorbScript, C, Python and JavaScript, measured every night on the same machine - a TorbScript program built with torb build against C, and torb run against Python and Node.js.

<!-- torb:index:end -->
