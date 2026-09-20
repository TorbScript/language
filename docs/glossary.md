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

### Backpressure

The shape of a stream's protocol rather than a mechanism of its own: reading pulls, so nothing runs before something
asks, and writing waits, so `add` finishes only once the target has taken the item. See
[Streams](language/concurrency-and-streams/streams.md).

### Binding

A name for a value, introduced by `const` or `var`. Never called a variable: the binding decides whether the value it
holds can be changed. See [Bindings](language/values-and-types/bindings.md).

### Bound

A restriction on a type parameter to types that implement one or more traits, written inline (`<Item: Hash>`) or
after `where`. See [Bounds](language/generics/bounds.md).

### Canon

The one formatting a program is written in - most visibly that a call is a [command call](#command-call) wherever the
grammar allows it. `torb canon` writes it and `torb canon --check` reports what is not in it. See
[Command calls](language/syntax/command-calls.md).

### Capability

What a package or a sandboxed script may reach beyond the pure part of the language: the file system, the network,
the clock, the environment, processes or foreign functions. Visible from the imports alone, because there is no
reflection to grant one silently. See [The sandbox](language/configuration/the-sandbox.md).

### Case

A variant of a `type`, declared with `case`. Never called a variant or an enum case. See
[Cases and match](language/pattern-matching/cases-and-match.md).

### Cause chain

What `Error.cause()` walks: an error that wraps another one hands it out, so a report can print each link as
`  caused by: <...>`. See [The Error trait](language/errors/the-error-trait.md).

### Closure

The one closure form, `{ ... }` in expression position, which captures a `const` binding as a copy and a `var`
binding as a shared box. See [Closures](language/functions/closures.md).

### Coercion

One of the four implicit conversions the language has, all of which apply only where a type is expected: a value to a
trait it implements, a trait value to fewer bounds, `Never` to anything, and a literal to a [literal type](#literal-type).

### Coherence

The rule that a package may write `extend X with Trait` only if it owns `X` or `Trait`, and that two implementations
of one trait may never overlap. See [Coherence and blanket implementations](language/traits/coherence.md).

### Collector

A reusable description of what to do with the values of a pipeline, which starts an [accumulator](#accumulator) per run.
`counting()`, `summing { ... }` and `groupingBy { ... }` are collectors.

### Command call

A call written without parentheses, in command position: at the start of a statement, on the right of `=`, after `return`,
after `=>`, or as the default of a field. See [Command calls](language/syntax/command-calls.md).

### Command position

The five places a [command call](#command-call) is allowed. Nothing inside parentheses, brackets, an operator or an
argument list is command position.

### Const parameter

A parameter of a generic type or function whose argument is a value rather than a type - `Int`, `Bool`, `Char` or
`String`. There is no arithmetic over one; the checker only compares const arguments for equality. See
[Arrays and const parameters](language/values-and-types/arrays.md).

### Constructor

The one function that builds a value of a `type`, generated from its fields in declaration order and never
containing logic. See [Construction](language/types/construction.md).

### Contextual keyword

A word that reads as an ordinary name after a `.` or as an argument label, and as a keyword everywhere else - today
only `from`, `as` and `by`, because nothing can be declared with any other keyword's spelling. See
[Lexical structure](language/syntax/lexical-structure.md).

### Copy trap

Taking a value out of a collection or a field produces a copy, so changing it changes nothing. It is a compile error
rather than a silent bug. See
[Why values instead of references](explanation/why-values-instead-of-references.md).

### Cyclic import

Two modules or packages that import each other. Harmless between modules, because nothing runs when a module is
imported and their exports are computed to a fixpoint; still an error between the members of a workspace. See
[Cyclic imports](language/modules-and-packages/cyclic-imports.md).

### Dead change

A change that cannot have an effect: a `var` that is changed and never read afterwards, or the discarded result of a
method that takes `self`. Both are compile errors, because with value semantics they are always mistakes.

### Delegation

Forwarding a trait's required members to the one field of a single-field type with `by`, such as
`Add & Subtract by value`. See [Delegation with by](language/traits/delegation.md).

### Distinct type

A `type` with a single field, used instead of an opaque alias; `by` forwards specific traits of that field one at a
time. See [Distinct types](language/values-and-types/distinct-types.md).

### Doc comment

A `/** ... */` comment that belongs to the declaration after it. Everything that is declared can have one, parameters,
fields and cases included, and its text is Markdown with the conventional headings `# Errors`, `# Panics` and
`# Examples`.

### Error trait

The trait every failure can carry (`with Show`) so it fits into `Result<Value, Error>` for the layers that only need
to report it, not match on it. See [The Error trait](language/errors/the-error-trait.md).

### Exclusivity

The rule that while a `var` access to a path is running, the same path cannot be accessed another way. The access of a
call begins once all of its arguments have been evaluated. See [Exclusivity](language/types/exclusivity.md).

### Exhaustive

Said of a `match`, a binding, a `for` loop or a closure parameter whose pattern or arms cover every value of the
subject's type, which the checker proves before the program runs. See
[Exhaustiveness](language/pattern-matching/exhaustiveness.md).

### Field

A named piece of storage declared inside a `type`, `const` unless marked `var` and public unless marked `private` or
`private(var)`. See [Fields](language/types/fields.md).

### Foreign function

A function of a C library, declared in a `foreign "library" { ... }` block with no body and the C ABI as its
contract. Available to any package, unlike [native](#native); a sandboxed script can never be granted it. See
[Foreign functions](language/extensibility/foreign-functions.md).

### Format

An implementation of `Encode`/`Decode`'s `Encoder` and `Decoder` for a whole document shape, such as `Json`, which
never sees a type of its own. See [std/encoding](standard-library/encoding.md).

### Front matter

The block between the two `---` lines at the top of a documentation page, in a written subset of YAML. See
[The front matter](contributing/front-matter.md).

### Guard

A condition after `if` in a pattern (`n if n < 0`), which also has to hold for the arm to match. A guarded arm never
counts towards exhaustiveness, because the guard could always be false. See
[Pattern forms](language/pattern-matching/pattern-forms.md).

### Hoisting

A `fn` declaration is visible everywhere in its scope, including above the line it is written on, so two functions
can call each other without a forward declaration. See
[Declaring a function](language/functions/declaring-a-function.md).

### Intersection

`&` combining two or more traits into one type, such as `Show & Encode`; only traits can be combined this way. See
[Trait intersections](language/traits/intersections.md).

### Kind

What a documentation page is: `index`, `guide`, `reference`, `how-to`, `explanation`, `contrast`, `tooling`, `package` or
`glossary`. The kind decides the required sections.

### Label

The name of a parameter, written before `:` in a call to fill it out of position. Every positional argument of a call
comes before every labelled one. See [Arguments and labels](language/functions/arguments.md).

### Lazy parameter

A parameter written `lazy Type`, whose argument is evaluated at most once, the first time the parameter is read, and
not at all if the parameter is never read. See [Parameter modes](language/functions/parameter-modes.md).

### Literal type

A type that is a union of literals of one base type, such as `"tcp" | "udp"`. Only literals can be combined this way;
there are no unions of types.

### Method

A member of a type that declares `self`. Structurally it is a constant of the type that holds a receiver closure, which is
why a field and a method cannot share a name. See [Methods and static functions](language/types/methods.md).

### Module

One file, or the `src/lib.trb` of a package: a set of declarations another file reaches with
[`use`](language/modules-and-packages/use.md), never something that runs on its own. See
[Top-level code](language/modules-and-packages/top-level-code.md) for what a module may not hold.

### Native

A declaration implemented by the compiler and its runtime instead of by TorbScript code, such as `Array`, `String`
or a collection's storage. Only `std/` may declare one; see [Foreign functions](language/extensibility/foreign-functions.md)
for the same idea applied to a C library instead of the runtime.

### Object safety

Whether a trait's member can be called on a value known only through the trait: a member that mentions `Self` in a
parameter or its result, or that has no `self`, cannot be. See [Object safety](language/traits/object-safety.md).

### Package

A directory with a `project.trb` and a `src/`, named `owner/name`. `src/lib.trb` is what other packages import and
`src/main.trb` is what `torb run` executes. See [Packages](language/modules-and-packages/packages.md).

### Page

A `.md` file of this documentation that starts with [front matter](#front-matter). A `.md` file without front matter is a
design document and is linked by an index rather than being a page.

### Panic

An unrecoverable stop for a bug rather than an expected failure: `panic: <message>` to standard error, then the site,
exit code 101, and nothing else runs. See [panic](language/errors/panic.md).

### Participle

The method that returns a changed copy, next to the [verb](#verb) that changes in place: `added` next to `add`, `sorted`
next to `sort`. See [Verbs and participles](language/types/verbs-and-participles.md).

### Pipeline

A source, zero or more lazy stages (`map`, `filter`, `take`, `sorted`), and one terminal operation (`toList()`, `fold`,
`count`) that pulls the values through. Nothing runs until the terminal operation does.

### Prelude

The package whose public names are in scope in every file, `std/prelude` by default. It holds the pure part of the
standard library; what a program can touch stays an explicit import. See
[The prelude](language/modules-and-packages/the-prelude.md).

### Property command

A [command call](#command-call) on a field, which writes the field rather than calling it. `port 8080` is `port = 8080`,
and `database { ... }` configures the field's value in place.

### Question mark operator

The postfix `?`, which unwraps an `Ok` or a `Some` and returns the `Fail` or `None` from the surrounding function or
script, converting the error type through `From` where they differ. See
[The question mark operator](language/errors/question-mark.md).

### Quotation

The typed tree, source text and captured values an `Expression<Value>` parameter or binding hands over alongside the
ordinary value. See [Quoted expressions](language/functions/quoted-expressions.md).

### Read-only view

A `const` binding, field or parameter that names a [shared type](#shared-type): the object can still change through
somebody else's `var` path, but never through this one, and it cannot be widened back into a `var`.

### Receiver closure

A closure whose first parameter is called `self`, so names inside it resolve against that receiver. It is what a builder,
a configuration block and a method all are. See
[Receiver closures](language/configuration/receiver-closures.md).

### Receiver script

A `.trb` file loaded as the body of a [receiver closure](#receiver-closure), type checked against the receiver type.
`project.trb` is one, with the receiver `Project`. See [Receiver scripts](language/configuration/receiver-scripts.md).

### Rest pattern

The `...name` part of a list pattern, such as `[first, ...rest]`, which binds the items it does not name individually
as a `List`. See [Pattern forms](language/pattern-matching/pattern-forms.md).

### Sandbox

Where a [receiver script](#receiver-script) runs: no IO, no network, no clock, no environment and no foreign
functions by default, and every [capability](#capability) beyond that granted at the call site of
`Sandbox.load`, never by the script itself. See [The sandbox](language/configuration/the-sandbox.md).

### Shared type

A type declared `shared type`, which has an identity: assigning it does not copy, and everybody who holds it sees the same
object. Handles to the outside world are shared types. See [Shared types](language/types/shared-types.md).

### Sink

The writing end of a stream, `Sink<Item, Failure>`, with `add` and `finish` in place of an `Accumulator`'s members of
the same name, each answering a `Task`. See [Streams](language/concurrency-and-streams/streams.md).

### Skill

The Agent Skill derived from this documentation by `torb docs skill`: a folder whose `SKILL.md` has a `name` and a
`description` and whose `reference/` holds the pages. See [The Agent Skill](contributing/the-skill.md).

### Snippet

A fenced code block of this documentation. A `trb` snippet is verified by the compiler's own front end; the marker in the
fence decides how hard. See [The docs commands](contributing/checks.md).

### Source

The reading end of a stream, `Source<Item, Failure>`, with `next` in place of an `Iterator`'s `next`, answering a
`Task` instead of the value directly. See [Streams](language/concurrency-and-streams/streams.md).

### Stage

The synchronous middle of a pipeline, `Stage<Input, Output>`, written once and driven by both an `Iterable` and a
`Source`. See [Pipelines](language/collections-and-iteration/pipelines.md).

### Stage 0

The Rust interpreter in `bootstrap/` that runs the self-hosted toolchain until it can compile itself. It has no type
checker and is thrown away afterwards.

### Static function

A function declared on a `type` that does not take `self`, called as `Type.member(...)` rather than through an
instance. See [Methods and static functions](language/types/methods.md).

### Stream

One flow in one direction, named by whichever end a signature holds - `Source` to read or `Sink` to write - never
"the stream" itself, because a program holds one end at a time. See [std/stream](standard-library/stream.md).

### Supertrait

A trait that another trait requires with `with` at its own declaration, such as `trait Compare with Equals`; every
implementor of the smaller trait implements it too. See [Supertraits](language/traits/supertraits.md).

### Tail call

A call in tail position of a function that calls itself directly. Guaranteed not to grow the stack; every other call
uses a stack frame and counts against the per-task frame limit. See
[Tail calls and the frame limit](language/execution/tail-calls.md).

### Task

A computation that finishes later, `Task<Value>`; a function that answers one may call `await()`, the way a function
that answers a `Result` may use `?`. See [Tasks](language/concurrency-and-streams/tasks.md).

### Trailing closure

A closure argument for a call's last parameter, written as a `{ ... }` after the call instead of inside its
parentheses; it always belongs to the outermost command call of the statement. See
[Trailing closures](language/functions/trailing-closures.md).

### Trait

A list of members a type provides, given with `with` at the declaration or with `extend` afterwards. A trait with one
required method is named after that method. See [Traits](language/traits/traits.md).

### Type parameter

A name standing for a type, filled in at each use of a `fn`, `type`, `trait` or `extend`, written out like a type
(`Item`, `Value`) rather than a single letter. See [Type parameters](language/generics/type-parameters.md).

### Var path

A path from a binding down to a value through which a change is legal: a `var` binding, `var` parameter or `var self`, then
`var` fields, indices and ranges all the way. Without one, nothing changes. See
[Mutation and var paths](language/types/var-paths.md).

### Variadic parameter

A parameter written `...name: Type`, which collects every remaining positional argument into a `List<Type>`; a caller
spreads a collection into it with `...`. See [Variadic parameters](language/functions/variadics.md).

### Verb

A method that changes its receiver in place and declares `var self`, next to its [participle](#participle), which returns
a changed copy. See [Verbs and participles](language/types/verbs-and-participles.md).

### Wildcard

The pattern `_`, which matches anything and binds nothing. The same underscore in an expression is the implicit
closure parameter instead; the two positions never overlap. See
[Pattern forms](language/pattern-matching/pattern-forms.md).

### Witness table

What a trait-typed value carries to call a generic bound's members without knowing its concrete type - one function
pointer per member of the trait. See [Witness tables](language/generics/witnesses.md).

### Workspace

A root `project.trb` naming several member projects, sharing one `project.lock.trb` so they resolve their
dependencies together. See [Workspaces](language/modules-and-packages/workspaces.md).
