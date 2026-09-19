---
title: Glossary
summary: Every term this documentation uses, one entry each, at most two sentences. The entry decides which word is correct.
kind: glossary
status: stable
order: 90
keywords:
  - glossary
  - terminology
source:
  - CONCEPT.md
---

One term, one entry, at most two sentences and one link. This page is normative for terminology: where two words could
mean the same thing, the word with an entry here is the one to use. A term whose explanation wants to become an article
gets a page instead, and keeps a one-line entry here that links it.

## Terms

### Accumulator

The state of one run of a [collector](#collector), held in a `var`, with `add` and `finish`. Every `Collection` is an
accumulator out of the box.

### Binding

A name for a value, introduced by `const` or `var`. Never called a variable: the binding decides whether the value it
holds can be changed. See [Bindings](language/values-and-types/bindings.md).

### Canon

The one formatting a program is written in - most visibly that a call is a [command call](#command-call) wherever the
grammar allows it. `torb canon` writes it and `torb canon --check` reports what is not in it. See
[Command calls](language/syntax/command-calls.md).

### Case

A variant of a `type`, declared with `case`. Never called a variant or an enum case. See
[Cases and match](language/pattern-matching/cases-and-match.md).

### Coercion

One of the four implicit conversions the language has, all of which apply only where a type is expected: a value to a
trait it implements, a trait value to fewer bounds, `Never` to anything, and a literal to a [literal type](#literal-type).

### Collector

A reusable description of what to do with the values of a pipeline, which starts an [accumulator](#accumulator) per run.
`counting()`, `summing { ... }` and `groupingBy { ... }` are collectors.

### Command call

A call written without parentheses, in command position: at the start of a statement, on the right of `=`, after `return`,
after `=>`, or as the default of a field. See [Command calls](language/syntax/command-calls.md).

### Command position

The five places a [command call](#command-call) is allowed. Nothing inside parentheses, brackets, an operator or an
argument list is command position.

### Copy trap

Taking a value out of a collection or a field produces a copy, so changing it changes nothing. It is a compile error
rather than a silent bug. See
[Why values instead of references](explanation/why-values-instead-of-references.md).

### Dead change

A change that cannot have an effect: a `var` that is changed and never read afterwards, or the discarded result of a
method that takes `self`. Both are compile errors, because with value semantics they are always mistakes.

### Doc comment

A `/** ... */` comment that belongs to the declaration after it. Everything that is declared can have one, parameters,
fields and cases included, and its text is Markdown with the conventional headings `# Errors`, `# Panics` and
`# Examples`.

### Exclusivity

The rule that while a `var` access to a path is running, the same path cannot be accessed another way. The access of a
call begins once all of its arguments have been evaluated.

### Front matter

The block between the two `---` lines at the top of a documentation page, in a written subset of YAML. See
[The front matter](contributing/front-matter.md).

### Kind

What a documentation page is: `index`, `guide`, `reference`, `how-to`, `explanation`, `contrast`, `tooling`, `package` or
`glossary`. The kind decides the required sections.

### Literal type

A type that is a union of literals of one base type, such as `"tcp" | "udp"`. Only literals can be combined this way;
there are no unions of types.

### Method

A member of a type that declares `self`. Structurally it is a constant of the type that holds a receiver closure, which is
why a field and a method cannot share a name.

### Package

A directory with a `project.trb` and a `src/`, named `owner/name`. `src/lib.trb` is what other packages import and
`src/main.trb` is what `torb run` executes.

### Page

A `.md` file of this documentation that starts with [front matter](#front-matter). A `.md` file without front matter is a
design document and is linked by an index rather than being a page.

### Participle

The method that returns a changed copy, next to the [verb](#verb) that changes in place: `added` next to `add`, `sorted`
next to `sort`.

### Pipeline

A source, zero or more lazy stages (`map`, `filter`, `take`, `sorted`), and one terminal operation (`toList()`, `fold`,
`count`) that pulls the values through. Nothing runs until the terminal operation does.

### Prelude

The package whose public names are in scope in every file, `std/prelude` by default. It holds the pure part of the
standard library; what a program can touch stays an explicit import.

### Property command

A [command call](#command-call) on a field, which writes the field rather than calling it. `port 8080` is `port = 8080`,
and `database { ... }` configures the field's value in place.

### Receiver closure

A closure whose first parameter is called `self`, so names inside it resolve against that receiver. It is what a builder,
a configuration block and a method all are. See
[Write a configuration file](how-to/write-a-configuration-file.md).

### Receiver script

A `.trb` file loaded as the body of a [receiver closure](#receiver-closure), type checked against the receiver type.
`project.trb` is one, with the receiver `Project`.

### Shared type

A type declared `shared type`, which has an identity: assigning it does not copy, and everybody who holds it sees the same
object. Handles to the outside world are shared types.

### Skill

The Agent Skill derived from this documentation by `torb docs skill`: a folder whose `SKILL.md` has a `name` and a
`description` and whose `reference/` holds the pages. See [The Agent Skill](contributing/the-skill.md).

### Snippet

A fenced code block of this documentation. A `trb` snippet is verified by the compiler's own front end; the marker in the
fence decides how hard. See [The docs commands](contributing/checks.md).

### Stage 0

The Rust interpreter in `bootstrap/` that runs the self-hosted toolchain until it can compile itself. It has no type
checker and is thrown away afterwards.

### Trait

A list of members a type provides, given with `with` at the declaration or with `extend` afterwards. A trait with one
required method is named after that method. See [Traits](language/traits/traits.md).

### Var path

A path from a binding down to a value through which a change is legal: a `var` binding, `var` parameter or `var self`, then
`var` fields, indices and ranges all the way. Without one, nothing changes.

### Verb

A method that changes its receiver in place and declares `var self`, next to its [participle](#participle), which returns
a changed copy.
