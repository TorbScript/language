---
title: torb docs site
summary: torb docs site writes the website of TorbScript from docs/ as static files - the front page, every page with its navigation, a search index per version - highlighted by the compiler and styled by brand/.
kind: tooling
status: stable
order: 78
keywords:
  - torb docs site
  - website
  - static site
  - torb.dev
  - search index
  - versions.json
source:
  - compiler/src/documentation/site.trb
  - compiler/src/documentation/site-html.trb
  - compiler/src/documentation/site-navigation.trb
  - compiler/src/documentation/site-search.trb
  - compiler/src/documentation/site-layout.trb
  - compiler/src/documentation/site-style.trb
  - docs/design/RELEASE.md#6-the-website
  - docs/design/BRAND.md#11-applications
---

The website at torb.dev is one more output of the documentation tree, beside the skill and the bundle: `torb docs site`
reads the pages `torb docs check` reads and writes plain files that any static host can serve. No page is written by
hand in HTML, and no JavaScript is needed to read one.

## Synopsis

```text
torb docs site <root> --output <dir> [--version <v>] [--brand <dir>] [--check]

  --output <dir>   Where the site goes
  --version <v>    The version the documentation is of, and its directory below docs/ (default: latest)
  --brand <dir>    Where the fonts, the icons and the logo are (default: <root>/../brand)
  --check          Build the site and check its links, and write nothing
```

## What it does

### What it writes

| Path | What is in it | From |
|---|---|---|
| `index.html` | The front page: the line, the install commands, the tour, where to go next | `site/home.md` |
| `install.html`, `imprint.html`, `privacy.html` | A plain page of the site, each only where its source exists | `site/install.md`, `site/imprint.md`, `site/privacy.md` |
| `docs/<version>/...` | Every page and design document, as `.html` at the path of its `.md` | every other `.md` file |
| `docs/<version>/search-index.json` | The search of this version | every page |
| `404.html` | The page for a path that is not there | |
| `assets/site.css`, `assets/site.js` | The one stylesheet and the one script | the compiler |
| `assets/fonts/` | Chivo and Geist Mono as `.woff2`, with their licence | `brand/fonts/` |
| `favicon.svg`, `favicon.ico`, `apple-touch-icon.png`, `mask-icon.svg`, `icons/`, `manifest.webmanifest`, `social-card.png` | The icons, the web manifest with a maskable icon, and the preview card | `brand/` |

Every link between two pages is relative, so the output can be served from any directory. The page for a missing
path links from the root of the host, because the server answers it at every depth.

### A page

A page of the documentation has the navigation of the index tree beside it - `docs/index.md`, then each folder's
index, ordered by `order` and then by title, as torb docs index orders them - with its own
section open. Below the title stand the summary, the body, a link that edits the page on git.torb.dev, and the previous
and next pages in the order of the navigation; beside it on a wide screen, its headings of level 2 and 3.

- **A heading has the anchor `torb docs check` resolves links against**: the slug of its text, or the `{#anchor}` it
  ends with. A link between pages becomes a link to the `.html` file the page became, with its anchor; a link out of
  `docs/` - `../CONCEPT.md`, a source file - goes to the file on git.torb.dev.
- **A `trb` block is coloured when the site is built**, by the compiler's lexer and the resolver behind `torb
  highlight`, in the syntax colours of [BRAND.md](../design/BRAND.md) section 9. A name that can be changed is
  underlined. The marker of a block (`run`, `check`, `fragment`) is for the tools and is not shown; a `trb error`
  block carries the label that it does not compile.
- **A quote that starts with `**Planned.**` or `**Draft.**`** - the banner a page that is not `stable` opens with - is
  a callout in the colours of information or warning.
- **The written page of a package of `std/`** links to its generated reference below `docs/<version>/reference/`,
  which `torb doc std` writes; the link comes from the package name the page is titled with.

### The search

`search-index.json` holds the title, the summary, the keywords and the headings of every page, and a term index of
their bodies: every word of the prose, the lists and the tables, sorted, with the pages it occurs on, the page that uses
it most first. The script fetches it the first time somebody searches (`/`, or Ctrl+K) and ranks a match in a title
first, then a keyword, a heading, the summary, and the bodies. There is no search service.

### Versions

`--version 0.2.0` writes the documentation below `docs/0.2.0/`. The version switcher of the navigation reads
`docs/versions.json` at the root of the site when one is there, and shows only the version of the page when none is:

```json
{"latest": "0.2.0", "versions": ["nightly", "0.2.0", "0.1.0"]}
```

Choosing another version opens the same page in it, or its overview when the page does not exist there.

### The look

One stylesheet: the tokens of `brand/tokens.css`, which the compiler carries and a test holds equal to the file, and
the rules written for the site, with a light and a dark theme that follow the system and a toggle that overrides it.
The fonts are served by the site itself, never by a font service. Every script only adds to a page that is complete
without it: the copy buttons, the theme toggle, the search and the version switcher.

**What a page asks of a reader's browser**: no cookie, and no request to another host - the fonts, the icons, the
search index and `versions.json` all come from the site. The one thing stored on the device is the theme a reader
picks with the toggle (`localStorage`, key `torb-theme`), and only when the toggle is used. The logo of the front page
assembles when the reader arrives from elsewhere and not again on the way back from another page of the site, which
the page it came from says, so nothing is stored for it.

### The check

Every `href` and `src` of the written pages that is relative has to lead to a file of the site, or the command fails
and names it - except a link into `docs/<version>/reference/`, which `torb doc` writes. `--check` builds the site,
checks it and writes nothing.

## Examples

The site of the version being released, then the reference of the standard library into it:

```console
$ torb docs site docs --output build/site --version 0.2.0
295 pages of 0.2.0: wrote 314 files to build/site
$ torb doc std --output build/site/docs/0.2.0/reference
41 packages, 133 modules: wrote 180 files to build/site/docs/0.2.0/reference
```

The same build as a check:

```console
$ torb docs site docs --check
295 pages of latest, 314 files, no broken link
```

## Related

- [torb doc](torb-doc.md) - the generated reference of a package, which the site links to and which shares its look.
- The docs commands - `docs check`, `index`, `skill` and `bundle`, the gates of the same
  tree.
- The website - the pages the site writes at its root.

