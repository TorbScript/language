---
title: The website
summary: The pages torb.dev shows around the documentation - the front page and the install page - which torb docs site writes at the root of the site.
kind: index
status: stable
order: 100
skill: omit
---

These pages are the website around the documentation: [torb docs site](../tooling/torb-docs-site.md) writes each of
them at the root of torb.dev instead of below `/docs/<version>/`, with a layout of its own for the front page.

## What belongs here

Pages of the website that are not documentation: the front page (`home.md`, written as `/`), the install page
(`install.md`, as `/install`), and the imprint and the privacy notice (`imprint.md` and `privacy.md`, as `/imprint` and
`/privacy`), which the site links in its footer only where the page exists. Each has the kind `site`, and none of them
is carried by the skill or by `llms-full.txt`.

What does not belong here: anything about the language or the toolchain, which is in the sections of the documentation,
and the release notes, which will have `docs/releases/`.

<!-- torb:index:begin -->

## Pages

- **[TorbScript](home.md)** - A single-language ecosystem - one functional-first language with value semantics for scripts, configuration, servers and tools, interpreted while you write it and native when it ships.
- **[Install TorbScript](install.md)** - One command installs the toolchain for the current user on Linux, macOS, FreeBSD and Windows, checked against the release's hashes, and torb upgrade keeps it current.

<!-- torb:index:end -->
