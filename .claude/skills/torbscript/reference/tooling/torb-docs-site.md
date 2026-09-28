---
title: torb docs site
summary: torb docs site writes the website of TorbScript from docs/ as static files - three levels from the first lesson to the reference, in every language of translations/, with a search per language.
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
  - lessons
  - translations
  - playground
source:
  - compiler/src/documentation/site.trb
  - compiler/src/documentation/site-strings.trb
  - compiler/src/documentation/translations.trb
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
hand in HTML, and no JavaScript is needed to read one. It leads a reader in at one of three levels - Start, Guide,
Reference - and is written in English and in every language below `docs/translations/`.

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
| `index.html` | The front page: the line, the install commands, the three doors, a few examples | `site/home.md` |
| `install.html`, `imprint.html`, `privacy.html`, `play.html` | A plain page of the site, each only where its source exists | `site/install.md`, `site/imprint.md`, `site/privacy.md`, `site/play.md` |
| `docs/<version>/...` | Every page and design document, as `.html` at the path of its `.md` | every other `.md` file |
| `docs/<version>/search-index.json` | The search of this version | every page |
| `<language>/...` | All of the above but the assets, in another language: `de/index.html`, `de/docs/<version>/...` | `translations/<language>/` |
| `404.html` | The page for a path that is not there | |
| `assets/site.css`, `assets/site.js` | The one stylesheet and the one script | the compiler |
| `assets/fonts/` | Chivo and Geist Mono as `.woff2`, with their licence | `brand/fonts/` |
| `favicon.svg`, `favicon.ico`, `apple-touch-icon.png`, `mask-icon.svg`, `icons/`, `manifest.webmanifest`, `social-card.png` | The icons, the web manifest with a maskable icon, and the preview card | `brand/` |

Every link between two pages is relative, so the output can be served from any directory. The page for a missing
path links from the root of the host, because the server answers it at every depth.

### Three levels

Every page belongs to one level, by the folder it is in, and every level has one door in the header - on a phone, in a
row below it:

| Level | For | Folder | Its door leads to |
|---|---|---|---|
| **Start** | somebody who has never programmed | `start/` | `start/index.md`, the list of lessons |
| **Guide** | somebody who programs already | `guide/` | `guide/index.md` |
| **Reference** | the full depth: the language, the standard library, the tools, the reasons, the records | everything else | `index.md` |

The level shows on the page: a label above the title, in the colour of its level. A page's sidebar lists only its own
level, and its previous and next pages stay inside it; the last page of Start leads on to the Guide and the last page
of the Guide to the Reference, each link saying which step it is. The writing rules that go with the levels are in
Three levels, plain words.

**A lesson of Start** (`kind: lesson`) has no sidebar. Above its title stand its number and the number of lessons, a
line that fills as the course goes on, a menu of every lesson and the arrows to its neighbours; its body is cut into
rows at its `##` headings, each row's prose on the left and its code on the right, so the editor is beside the text it
belongs to. What is folded - a hint, a solution - stands below the prose, and on a phone below the code. A large link
to the next lesson ends it. **The list of lessons**, `start/index.md`, shows its introduction and every lesson with its
number and summary, in place of the list `docs index` writes into it. Lessons are not carried by the skill or the
bundle, which state the rules the lessons teach in small steps.

### A page

A page of the Guide or the Reference has the navigation of its level beside it - the index tree, ordered by `order`
and then by title, as torb docs index orders them - with its own section open. Above the
title stands its level, and for a page of the Reference the way from the overview to its section; below the title the
summary, the body, a link that edits the page on git.torb.dev, and the previous and next pages; beside it on a wide
screen, its headings of level 2 and 3.

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

### Runnable blocks and exercises

A `trb run` block can be edited and run in the page, and a `trb exercise` block is a small task with an expected
output. Both stay ordinary Markdown that `torb docs check` verifies; the site turns them into mounts that the
playground (`docs/design/RELEASE.md`, "The playground") enhances, and without the playground they are highlighted code
with a copy button.

**In Markdown.** An exercise is its starting code, then a folded hint, then a folded solution. The solution is the first
`trb run` block after the exercise and before the next one, and the lines its `// prints` comments name are the
exercise's expected output, so the output a reader is asked for is the output `docs check` saw the solution print.

````md
```trb exercise
const name = "World"
print "Hello, {name}!"
```

<details>
<summary>Hint</summary>

The text between the quotes is what the name holds.

</details>

<details>
<summary>Solution</summary>

```trb run
const name = "Ada"
print "Hello, {name}!"
// prints Hello, Ada!
```

</details>
````

| Fence | What `docs check` asks of it |
|---|---|
| `trb run` | Type checks, is built natively and run, and prints what its `// prints` comments say |
| `trb exercise` | Parses, is in the canon and type checks: a whole program that does not do the task yet. Its output is not compared |
| `trb exercise incomplete` | Lexes: starting code with a gap on purpose, which the reader fills in |

A `trb exercise` block without a `trb run` block after it, before the next exercise, is a problem. The blank lines
around the `<details>` and `<summary>` lines are what keep the Markdown between them Markdown.

**In HTML.** Every `trb run` block but a solution, and every `trb exercise` block, is written as a mount. The attribute
names are stable; the playground reads nothing else:

```text
<div class="playground" data-playground="run" data-file="first-program.trb" data-source="...">
  <div class="code-block"><pre class="code language-trb"><code>...highlighted...</code></pre></div>
  <p class="playground-local">...torb run first-program.trb...</p>
</div>

<div class="playground playground-exercise" data-playground="exercise" data-file="first-program.trb"
     data-source="..." data-expected-output="Hello, Ada!" data-solution="...">
  ...the same fallback, and the expected output as text...
</div>
```

| Attribute | What it holds |
|---|---|
| `data-playground` | `run` or `exercise` |
| `data-source` | The code of the block, exactly as written in the Markdown, HTML-escaped |
| `data-file` | The name a reader saves it as to run it locally: the page's file name with `.trb` |
| `data-expected-output` | Exercise only: the expected standard output, its lines joined by a line feed, no final one |
| `data-solution` | Exercise only: the code of the solution, for a button that shows it |

A solution is written as a plain code block inside its `<details>`, not as a mount. The line "run it locally" stands
only under the exercises of Start; everywhere else a mount looks like any other code block until the playground takes
it.

**Loading.** No page names `playground.js` statically, so the link check does not require it. When a page has a mount,
`assets/site.js` inserts `<script src="<root>assets/playground.js">`; when that loads and defines
`window.TorbPlayground`, it calls, for every mount in document order:

```text
TorbPlayground.mount(element, {
  source,           // data-source
  file,             // data-file
  expectedOutput,   // data-expected-output, or undefined
  solution,         // data-solution, or undefined
  labels,           // { run, reset, solution, output, expected, solved, running }, in the page's language
  onResult          // function ({ output, exitCode, passed }) - passed: the output equals expectedOutput
})
```

From then on `mount` owns the element's children. `playground.js` does not mount anything by itself, and whatever it
loads besides - the toolchain as WebAssembly - it loads relative to its own URL. When the script is missing or fails,
nothing happens and the fallback stays. A page `site/play.md` is written as `play.html`, and the header links it as
Playground where it exists.

### The front page

`site/home.md` below the hero, cut at its `##` headings. The hero is fixed: the mark, the line, the page's
`summary` in plain words, the install commands and the one red button. A section whose body is one list, every item a
link and one sentence, is the row of doors, each labelled with the level its link leads to; every other section is a
panel, its prose beside its code. The page stays short: the hero and the doors fill about one screen, and at most three
panels follow, each a plain title, two sentences and a few lines of code.

### Languages

English is the source, in `docs/`. A translation mirrors the path of its original below
`docs/translations/<language>/` - `translations/de/start/lists.md` is the German `start/lists.md` - and
`translations/<language>/strings.json` holds the words of the interface in that language: the navigation, the buttons,
the notices, the search and the labels of the playground, as a flat JSON object whose keys are the ones of
`englishStrings` in `site-strings.trb`. A key it leaves out is said in English. A language exists when its
`strings.json` does.

- **Paths.** English is at the root of the site; every other language has the same paths below its code: `/de/`,
  `/de/docs/0.2.0/start/lists.html`. The assets are shared.
- **A page without a translation** is written anyway, in English inside the other language's interface, with a notice
  and a link to the English page. It names the English page as its canonical one and asks search engines not to index
  it; the generated reference of `std` stays English.
- **Other versions.** Every page names each language it really exists in with `hreflang`, English as the default.
- **The switcher** in the header is a menu of links to the same page in every language, each in its own name
  ("English", "Deutsch"), which works without a script. Picking one is remembered.
- **The browser's language.** A reader who arrives at an English page from elsewhere, has picked no language yet, and
  whose browser prefers an available language over English is sent to the same page in that language. Nobody is sent
  from another language, or when coming from a page of the site.
- **An outdated translation.** A translation says which version of its original it was made from, `translates:`
  and the twelve hexadecimal digits of the original's SHA-256 that `docs check` names. When the original changes,
  the site shows the translation with a notice that it may be outdated, and `docs check` lists it on a line of its own
  that starts with `outdated:` - a note, never a failure, so an English change does not wait for every language.
- **Checks.** A translation is checked like any page: its front matter, its headings, its links, which are the links
  of its original and resolve from the folder of the original, and its `trb` blocks. Its sections are not held to
  the English names a kind requires. It is not part of the index, the skill or the bundle.
- **The search** has one index per language, at `<language>/docs/<version>/search-index.json`.

A separate repository for the website was considered and not taken: the generator, the pages and the versioned
reference change together, and one commit keeps them in step. It stays an option if the site ever needs a life of its
own.

### Where the patterns come from

The three levels and their pages follow what works on the sites of other languages, kept small:

- **One door per level on the front page**: the Vue docs' "pick your learning path", react.dev's Learn and Reference.
- **One idea per lesson, the editor beside the text, "N of M" with arrows and a menu**: the Go Tour, the Gleam tour,
  the Svelte and Vue tutorials.
- **An exercise with a folded hint and a folded solution, checked by its output**: react.dev's challenges, Rust by
  Example's activities, Exercism's hints; the solution a playground can show is Svelte's "solve" and Vue's "show me".
- **A recap at the end, and plain titles with a term linked where it is first shown**: react.dev, the Rust Book.
- **A label on code that does not compile**: the Rust Book's marks.
- **Languages as path prefixes, a switcher that keeps the page, English with a notice where no translation exists, and
  one table of words per language**: Astro's Starlight.
- **Not taken**: forks per language on subdomains, accounts, unlock trees and quizzes, toggles that double every page,
  and several learning products side by side.

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
search index and `versions.json` all come from the site. Three things can be stored on the device, each only in
`localStorage`, each only after the reader does something, and each read and written inside a `try` so that a browser
that refuses storage shows the same pages: the theme a reader picks with the toggle (key `torb-theme`), the language
picked with the switcher or with the link of a notice (key `torb-language`), and which exercises of Start were solved
in the playground (key `torb-progress`), so the lists of lessons can mark them. Opening a lesson stores nothing, and
none of it leaves the device. The logo of the front page
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
311 pages of 0.2.0 in 2 languages: wrote 644 files to build/site
$ torb doc std --output build/site/docs/0.2.0/reference
41 packages, 133 modules: wrote 180 files to build/site/docs/0.2.0/reference
```

The same build as a check:

```console
$ torb docs site docs --check
311 pages of latest in 2 languages, 644 files, no broken link
```

## Related

- [torb doc](torb-doc.md) - the generated reference of a package, which the site links to and which shares its look.
- The docs commands - `docs check`, `index`, `skill` and `bundle`, the gates of the same
  tree.
- The website - the pages the site writes at its root.
- Learn to program - the course of Start.
- Three levels, plain words - how a page of each level is written.

