---
title: Add a page
summary: The seven steps from an empty file to a page that passes the gate, with a template for every kind of page to copy.
kind: how-to
status: stable
skill: omit
order: 50
keywords:
  - template
  - new page
  - checklist
---

Adding a page means choosing the folder, copying the template of the kind, filling it, and running two commands. The
index it belongs to updates itself.

## Steps

1. **Decide the kind before the folder.** Ask the two questions of
   [Diátaxis](https://diataxis.fr/compass/): does the page inform *action* or *cognition*, and does it serve the
   *acquisition* of skill or its *application*? Action plus acquisition is a `guide`, action plus application a
   `how-to`, cognition plus application a `reference`, cognition plus acquisition an `explanation`. A page that answers
   both ways is two pages. `contrast`, `tooling`, `package` and `glossary` are the four kinds that exist next to the
   four modes; [structure.md](structure.md) says what each folder is for.

2. **Find the page in the [inventory](inventory.md)** and take its path, title, scope and sources from there. The
   inventory is the work plan of the whole documentation; a page that is not in it needs a line added to it first, so
   that two people cannot write the same page twice.

3. **Create the file** at that path. A file name is lowercase with hyphens, because it is part of every link to the
   page. If the folder is new, create its `index.md` in the same change - a folder with no index is an error, and so is
   a folder with nothing but an index.

4. **Copy the template** of the kind from [the templates below](#full-example) and fill the front matter first. The
   `summary` is the hardest field and the most valuable one: write it as the answer to "should I open this page?", in 40
   to 240 characters. The [front matter reference](front-matter.md) lists every field.

5. **Write the example before the prose.** Take it from a real source - `CONCEPT.md`, `std/`, `examples/tour` - and run
   it through the toolchain before you write a word about it. Never write a snippet from memory: a wrong example is the
   one thing a reader and a model both copy without checking.

6. **Run the two commands**, from the repository root:

   ```console
   torb docs index docs
   torb docs check docs
   ```

   The first one writes the generated part of every index, so the new page appears in its folder's index. The second one
   is the gate: front matter, sections, headings, links, anchors, summaries, forbidden words and every code block.
   [checks.md](checks.md) lists what it decides and what it does not.

7. **Read the page as if it were the only one loaded.** Every term defined or linked, no `see above`, every error
   message quoted exactly, every limit a number. That is the part no tool can decide, and it is the part that matters.

## Pitfalls

- **A template's placeholder text will not pass the gate.** A `summary` under 40 characters, a missing required section
  and an empty `## Related` all fail. Fill the template before running the check, not after.
- **The templates are code blocks, not files.** A template file would itself be a page, and a page has to pass the
  checks, so it could not contain placeholders. Copy the block.
- **Do not edit the generated part of an index.** Everything between `<!-- torb:index:begin -->` and
  `<!-- torb:index:end -->` is overwritten. Write the introduction above the first marker.
- **`docs check` type checks every `trb check` block against the real standard library in one run, and builds every
  `trb run` block into one program.** `--no-snippets` skips both while you are still moving text around, and
  `--no-native` skips the building on a machine without a C compiler.

## Full example

One template per kind. Everything in angle brackets is to be replaced; every section that is listed is required.

### A reference page

````md
---
title: <the construct, as it is named in the language>
summary: <what the construct does, in one or two sentences, 40 to 240 characters, ending with a full stop>
kind: reference
status: stable
order: <a number, or leave the field out>
keywords:
  - <a word somebody would search for that is not in the title>
source:
  - CONCEPT.md#<the section this was derived from>
  - <a file of std/ or examples/ that shows it>
---

<One paragraph: what this construct is and when it is used. No heading. It may repeat the summary in more words.>

## Example

<The smallest complete program that shows the construct. No explanation before it.>

```trb
<code>
```

## Syntax

<The grammar or the forms, as a text block. One form per line.>

## Rules

1. **<The claim, in bold.>** <The rule, in one or two sentences. Testable.>
2. **<The next claim.>** <...>

## What this is not

<The mistake this construct invites, the correct line first and the wrong one after it with its diagnostic.>

```trb
<the right way>
```

```trb error
<the wrong way>
// error: <the exact message the compiler prints>
```

## Related

- [<page>](<path>) - <why somebody here would go there>.
````

### A guide page

````md
---
title: <what the reader will be able to do>
summary: <what this step teaches, in one or two sentences>
kind: guide
status: stable
order: <the position in the learning path>
prerequisites:
  - <the path of the previous step>
---

<One paragraph: where the reader is and what happens now.>

## Goal

<One sentence: what works at the end of this page.>

## <A step, named by what it does>

<Prose and code, one path, no branches and no alternatives.>

## Next

- [<the next step>](<path>) - <what it adds>.
````

### A how-to page

````md
---
title: <a verb phrase: Read a file, Add a dependency>
summary: <the task and the situation it applies to>
kind: how-to
status: stable
---

<One paragraph: the situation, and what the result is.>

## Steps

1. **<The step.>** <What to do, and what to write.>
2. **<The next step.>** <...>

## Pitfalls

- **<What goes wrong.>** <How to tell, and what to do instead.>

## Full example

```trb check
<the whole thing, in one program that type checks>
```

## Related

- [<page>](<path>) - <why>.
````

### An explanation page

````md
---
title: <a noun phrase: Why values instead of references>
summary: <the decision and the consequence, in one or two sentences>
kind: explanation
status: stable
source:
  - CONCEPT.md#decision-log
---

<One paragraph: the question this answers.>

## The decision

<What was decided, in the shortest form. A list where there is more than one part.>

## Why

<The argument. This is the one kind of page where alternatives, counter-examples and history belong.>

## Consequences

<What somebody writing TorbScript has to do differently because of this.>

## Related

- [<page>](<path>) - <why>.
````

### A contrast page

````md
---
title: Coming from <language>
summary: <the three or four differences that make a <language> programmer write TorbScript that does not compile>
kind: contrast
status: stable
keywords:
  - <language>
---

<One paragraph: what carries over, and what does not.>

## At a glance

| <language> | TorbScript | Why |
|------------|------------|-----|
| <construct> | <construct> | <one clause> |

## What changes in your code

### <The difference>

<The <language> habit, then the TorbScript line, then the reason.>

## Habits to unlearn

<What <language> has that TorbScript deliberately does not, and what to use instead. This section is what makes the
page credible: a comparison that only lists advantages is an advertisement.>

## Related

- [<page>](<path>) - <why>.
````

### A tooling page

````md
---
title: <the command or the file>
summary: <what it does and when it is used>
kind: tooling
status: stable
---

<One paragraph.>

## Synopsis

```text
torb <command> [options]
```

## What it does

<The behaviour, and the exit code where there is one.>

## Examples

```console
<a command line, and what it prints>
```

## Related

- [<page>](<path>) - <why>.
````

### A standard-library package page

````md
---
title: std/<name>
summary: <what the package is for, in one or two sentences>
kind: package
status: stable
source:
  - std/<name>/src/lib.trb
---

<One paragraph: what the package contains and whether it is in the prelude.>

## Import

```trb fragment
use <Name> from "std/<name>"
```

## Declarations

### <Name>

<The doc comment of the declaration, and its signature.>

## Related

- [<page>](<path>) - <why>.
````

### A folder index

````md
---
title: <the section>
summary: <what a reader finds in this folder, in one or two sentences>
kind: index
status: stable
order: <the position among its siblings>
---

<One paragraph: what this section is.>

## What belongs here

<What goes in this folder, and what explicitly does not. This is where the non-goals of the section are written down.>

<!-- torb:index:begin -->
<!-- torb:index:end -->
````

## Related

- [How this documentation is structured](structure.md) - the tree, the kinds and the required sections.
- [The front matter](front-matter.md) - every field and its rules.
- [How to write here](writing.md) - the writing rules.
- [The checks](checks.md) - what the gate decides.
- [The inventory](inventory.md) - every page the documentation needs.
