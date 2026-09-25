---
title: How to write here
summary: The writing rules for pages that both a person and a language model have to be able to trust, with the reason behind each one.
kind: reference
status: stable
skill: omit
order: 40
keywords:
  - style
  - terminology
  - examples
  - snippets
  - formatter canon
source:
  - https://developers.google.com/style
  - CONCEPT.md#formatter-canon
---

A page here is read by a person who wants an answer and by a model that will copy what it reads. Both need the same
thing: one claim per sentence, the exact spelling of everything in code font, a number wherever there is a limit, and an
example before the prose. These rules are the mechanically enforced ones plus the ones a reviewer checks.

## Example

The shape of a rule, in the form every reference page uses.

````md
3. **A `const` binding is deep.** Through a `const` binding nothing changes: no reassignment, no field assignment and no
   `var fn` method. `const` on a `List` therefore means the list never changes, not that the binding cannot be
   reassigned.

   ```trb
   const fixed = [1, 2]
   ```

   ```trb error
   const fixed = [1, 2]
   fixed.append 3
   // error: `append` needs a `var`. Did you mean `appended`?
   ```
````

Bold claim, then the rule in one or two sentences, then the example that shows it, then the mistake it prevents with the
diagnostic quoted exactly.

## Syntax

Six things about the mechanics of a page.

- **No `#` heading in the body.** The `title` of the front matter is the heading of the page. The body starts at `##`.
- **Sentence case headings, no full stop and no colon at the end.** `## What this is not`, not
  `## What This Is Not:`.
- **Heading levels are never skipped.** A `##` is followed by a `###` at the deepest.
- **A heading that another page links to carries an explicit anchor**: `## A long heading {#short-anchor}`. Then
  rewording the heading does not break the link.
- **A code block is fenced with backticks and names its language**: `trb`, `text`, `console`, `json`, `md`, `yaml` or
  `diff`. Three backticks are the normal fence; a block that contains a fence itself opens with four. An indented code
  block and a tilde fence are not part of the format, because neither names a language and a block whose language is
  unknown is a block nothing verifies.
- **Links are inline**: `[text](target)` or `[text](target#anchor)`. A reference link (`[text][label]`) splits the
  target away from the place it is used, which is what a chunked reader loses first.

## Rules

### Terminology

1. **One term per thing, and the [glossary](../glossary.md) decides which.** Write *binding* for a `const` or a `var`,
   never *variable*. Write *case* for a variant of a `type`, never *enum case* or *variant*. Write *command call* for a
   call without parentheses. A second word for one thing costs a reader a lookup and costs a retrieval system the
   ability to find the chunk at all: a term that is not in a chunk cannot retrieve it.
2. **Define or link every term at its first use on the page.** A page is read alone. There is no `see above`, no `as we
   saw` and no `the type from the previous section`, because the previous section may not have been loaded.
3. **Repeat the noun instead of writing a pronoun across a paragraph boundary.** `The binding decides` beats `It
   decides`.

### Precision

4. **Every rule is testable.** A sentence that cannot be turned into a program that passes or fails is not a rule, it is
   a feeling. Number the rules of a reference page so that a reader, a reviewer and an error message can name one.
5. **State defaults and limits as numbers.** `The stack check keeps 128 KiB of the stack in reserve`, not `the stack
   check keeps some room`.
6. **No hedges.** The words `usually`, `simply`, `obviously`, `basically`, `easily`, `of course`, `as mentioned`,
   `see above`, `and so on`, `please note` and `etc.` are rejected by the checker. If something holds under a condition,
   name the condition. If it holds always, say always.
7. **Quote an error message exactly, character for character, in code font.** Somebody will search for it. Run the code
   and copy what the compiler prints.
8. **Mark what does not exist yet.** A feature that is designed but not implemented lives on a page with
   `status: planned` and the planned banner, or it is not written about. A `stable` page that mentions a planned feature
   says so in the sentence and links the planned page.
9. **Name the source.** A `reference` or `package` page lists in `source` which section of `CONCEPT.md` or which file of
   `std/` it was derived from. That is how a page is found again when the language moves.

### Examples

10. **The example comes before the prose.** A reference page opens with `## Example`: the smallest complete program that
    shows the construct, before a word of explanation. Examples convey style and level of detail better than
    descriptions do.
11. **Every `trb` block is verified by the compiler.** By default it has to parse and be in the formatter canon. Ask for
    more or less in the info string: `trb check` type checks it against the real `std/`, `trb run` also builds and runs
    it and compares the output with its `// prints` comments, `trb fragment` is a signature rather than a program,
    `trb error` is wrong on purpose and names every diagnostic it produces, `trb skip <reason>` is verified by nothing
    and the reason is printed by the gate. A claim about what a program prints belongs in a `trb run` block, not in the
    prose around it. See [the checks](checks.md).
12. **A wrong example is always paired with the right one, and the right one comes first.** A bare prohibition loses
    against what a model already believes; a correct line next to the incorrect one does not. This is why
    `## What this is not` is a required section of every reference page.
13. **Contrast with the language a reader is coming from where it prevents a wrong guess.** One sentence in place
    (`In Rust this would be a borrow; here it is a copy`) plus a link to the [contrast page](../explanation/index.md).

### TorbScript in a snippet

14. **Follow the formatter canon.** A call is a command wherever the grammar allows it (`Ok value`,
    `return Fail problem`, `const role = Role name`, `names.map Role`) and has parentheses everywhere else: nested
    (`Ok Some(x)`), without arguments (`list.length()`), with an operator at the top level of an argument
    (`assert(sum == 3)`), over several lines, and in the head of an `if`, `for`, `while` or `match`. The checker enforces
    both directions.
15. **No semicolons, and never two statements on one line.** A statement ends at the end of its line.
16. **A multi-line `"""` string is indented two spaces deeper than the line its statement starts on**, with a closing
    `"""` that stands alone aligned with the content.
17. **An imported case is bare, everything else keeps its dot.** `None =>` and `Some(value) =>` in a pattern, because
    the prelude imports them; `.Circle(radius) =>` for a case that is not imported. In an expression the same rule
    holds, and `Shape.Circle(1.0)` is written out where nothing says which type is meant.
18. **A snippet is complete enough to be true.** A `trb` block is a whole file, so a snippet that needs an import writes
    the import. Where that would drown the point, use `trb fragment` and say in the prose what surrounds it.

### Tone

19. **Second person, present tense, active voice.** `You write` and `the compiler reports`, not `it will be reported`.
20. **Say why, do not raise the volume.** A rule with its reason is followed; a rule in capital letters is argued with.
    If a rule keeps being broken, the fix is a better example, not a louder sentence.
21. **No dates and no `currently`.** A page says what is true; `status` says how far it can be trusted. A superseded
    approach belongs in `explanation/` as a decision, not in a reference page as history.

## What this is not

**A page is not a narrative.** This is a page that has to be read from the top:

```md
Now that we have seen how bindings work, let us look at what happens when we try to change one. As you might expect,
the compiler will usually complain. Let us try it and see.
```

This is the same content as a page:

```md
## Rules

1. **A `const` binding cannot be reassigned, and nothing below it can be changed.** `const` is deep: assignment to a
   field and a call of a `var fn` method are both errors through a `const` path.
```

The first one costs a reader three sentences to reach the rule and gives a retrieval system nothing to match. It also
contains `usually`, which the checker rejects.

**A `summary` is not the first paragraph.** The summary lives in the front matter and is read in a list. The first
paragraph of the body is written for somebody who has already opened the page, and it may repeat the summary's claim in
more words.

**A reference page is not a place to argue.** One sentence of reason is welcome next to a rule; the argument belongs in
`explanation/`. The reverse holds too: an explanation page does not restate the syntax, it links it.

**A "how to" page is not a tutorial.** A tutorial has one path and no branches, and it is in `guide/`. A how-to assumes
competence, forks where the reader's situation forks, and is titled with the verb (`Read a file`, not
`How to read a file`).

## Related

- [How this documentation is structured](structure.md) - the tree, the kinds and the required sections.
- [The front matter](front-matter.md) - the fields and their rules.
- [Adding a page](adding-a-page.md) - the templates.
- [The checks](checks.md) - which of these rules a command decides.
- [The glossary](../glossary.md) - the normative terms.
- [The formatter canon](../language/syntax/command-calls.md) - the call style every snippet follows.
