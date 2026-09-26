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

One run of a pipeline, `Accumulator<Item, Output>`, with `add`, `finish` and `isDone`: a value, so a copy of one is a
fresh run and one accumulator is description and state at once. A collection is not an accumulator; what gathers a
pipeline into one is a type beside it, such as `ListAccumulator`. See
[Collectors](language/collections-and-iteration/collectors.md).

### Backpressure

The shape of a stream's protocol rather than a mechanism of its own: reading pulls, so nothing runs before something
asks, and writing waits, so `add` finishes only once the target has taken the item. See
[Streams](language/concurrency-and-streams/streams.md).

### Binding

A name for a value, introduced by `const` or `var`. Never called a variable: the binding decides whether the value it
holds can be changed. See [Bindings](language/values-and-types/bindings.md).

### Bits

The trait the integer types come with, and the bit operators are its members: `bitwiseAnd` (`&`), `bitwiseOr` (`|`),
`bitwiseExclusiveOr` (`^`), `bitwiseNot` (`~`), `shiftedLeft(by:)` (`<<`), `shiftedRight(by:)` (`>>`). `UInt64` alone adds `addedWrapping` and `multipliedWrapping`,
the only arithmetic that does not panic on overflow. See [Integers](language/values-and-types/integers.md).

### Bound

A restriction on a type parameter to types that implement one or more traits, written inline (`<Item: Hash>`) or
after `where`. See [Bounds](language/generics/bounds.md).

### Builder

A function that creates a value, hands it to a [receiver closure](#receiver-closure) to configure, and returns it -
the one mechanism behind every configuration block. See [Builders and DSLs](language/configuration/builders.md).

### Canon

The one formatting a program is written in - most visibly that a call is a [command call](#command-call) wherever the
grammar allows it. `torb format` writes it and `torb format --check` reports what is not in it. See
[Command calls](language/syntax/command-calls.md).

### Capability

What a package or a sandboxed script may reach beyond the pure part of the language: the file system, the network,
the clock, the environment, processes or foreign functions. Visible from the imports alone, because there is no
reflection to grant one silently. See [The sandbox](language/configuration/the-sandbox.md).

### Capsule

A value type whose fields are `private` and whose values come from factories and go out through accessors, so its
invariants hold - still a value, copied on assignment, never an identity like a [shared type](#shared-type). See
[Data or capsule](language/types/data-or-capsule.md).

### Case

A variant of a `type`, declared with `case`. Never called a variant or an enum case. See
[Cases and match](language/pattern-matching/cases-and-match.md).

### Cause chain

What `Error.cause()` walks: an error that wraps another one hands it out, so a report can print each link as
`  caused by: <...>`. See [The Error trait](language/errors/the-error-trait.md).

### Closure

The one closure form, `{ ... }` in expression position, which captures a `const` binding as a copy and a `var`
binding as itself - which only a closure handed to a parameter that just calls it may do. See
[Closures](language/functions/closures.md).

### Coercion

One of the four implicit conversions the language has, all of which apply only where a type is expected: a value to a
trait it implements, a trait value to fewer bounds, `Never` to anything, and a literal to a [literal type](#literal-type).

### Coherence

The rule that a package may write `extend X with Trait` only if it owns `X` or `Trait`, and that two implementations
of one trait may never overlap. See [Coherence and blanket implementations](language/traits/coherence.md).

### Collector

What `counting()`, `summing { ... }` and `groupingBy { ... }` answer: an [accumulator](#accumulator) that says what to
do with the values of a pipeline. The `Collector` trait and its `start()` were merged into `Accumulator`, so the word
names these functions and not a type of its own.

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
method that only reads its receiver. Both are compile errors, because with value semantics they are always mistakes.

### Decode

The half of the generated `Decode`/`Encode` pair that reads a value back from a format's `Decoder`, generated for
every type whose constructor is reachable from outside. See [Encode and Decode](language/reflection/encode-and-decode.md).

### Delegation

Forwarding a trait's required members to the one field of a single-field type with `by`, such as
`Add & Subtract by value`. See [Delegation with by](language/traits/delegation.md).

### Destructor

`close()` of a `shared type` that implements `Close`, run exactly once by the release of its last reference and never
called by user code. The checker enforces who implements `Close` and that nothing calls it; the release that runs it
is not built yet. See
[Destructors](language/execution/destructors.md).

### Distinct type

A `type` with a single field, used instead of an opaque alias; `by` forwards specific traits of that field one at a
time. See [Distinct types](language/values-and-types/distinct-types.md).

### Doc comment

A `/** ... */` comment that belongs to the declaration after it. Everything that is declared can have one, parameters,
fields and cases included, and its text is Markdown with the conventional headings `# Errors`, `# Panics` and
`# Examples`.

### Encode

The half of the generated `Decode`/`Encode` pair that describes a value to a format's `Encoder`, generated for every
type whose fields are all `Encode`. See [Encode and Decode](language/reflection/encode-and-decode.md).

### Endless loop

`loop { ... }`, the one loop without a condition: its type is `Never` while no `break` targets it and `Void` once one
does, and a `break` never carries a value. `while true` is an error that names it. See
[Loops](language/execution/loops.md).

### Entry file

The one file `torb run` interprets or `torb build` compiles - named directly, or reached as `src/main.trb` of a
project directory, or as the project's own `build { input = "..." }`. See [torb build](tooling/torb-build.md).

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

### Extension member

A constant or function an `extend` adds to a type instead of its declaration. It belongs to the type everywhere when the
type's own package attached it, and otherwise the file that uses it names it. See
[extend](language/traits/extend.md).

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
The front matter.

### Guard

A condition after `if` in a pattern (`n if n < 0`), which also has to hold for the arm to match. A guarded arm never
counts towards exhaustiveness, because the guard could always be false. See
[Pattern forms](language/pattern-matching/pattern-forms.md).

### Hoisting

A `fn` declaration is visible everywhere in its scope, including above the line it is written on, so two functions
can call each other without a forward declaration. See
[Declaring a function](language/functions/declaring-a-function.md).

### Interpreter

There is none today: `torb run` builds a native binary and runs it. The bytecode VM of milestone 7 is the planned
interpreter, reading the same typed IR as the C back end; stage 0, the Rust interpreter, was deleted on 2026-09-22.

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

### Manifest

The settings of a [`project.trb`](tooling/project-trb.md) the toolchain reads today - `name`, `dependencies`,
`workspace { members }`, and the `input` of `build` and `test` - read from its syntax tree rather than by running the
file. A setting outside that list type checks but is not consulted by any command yet.

### Member import

A `use` whose path names a member of a type - `use String.shout from "acme/text"` - which is how a file names an
[extension member](#extension-member) another package attached, `as` renaming it where two of one name meet. A case
import has the same shape.

### Method

A member of a type that works on a value of it. It does not list its receiver - `fn area(): Int` - and a `var fn`
says it changes that receiver. Structurally it is a constant of the type that holds a receiver closure, which is why a
field and a method cannot share a name. See [Methods and `static fn`s](language/types/methods.md).

### Module

One file, or the `src/lib.trb` of a package: a set of declarations another file reaches with
[`use`](language/modules-and-packages/use.md), never something that runs on its own. See
[Top-level code](language/modules-and-packages/top-level-code.md) for what a module may not hold.

### Name

An identifier: `[A-Za-z_][A-Za-z0-9_]*`, ASCII where text is not, and its first letter is a rule the checker reports at
the declaration - `A` to `Z` for a type, a trait, a case, a type parameter and a type alias, lowercase or `_` for
everything else. See [Naming](language/syntax/naming.md).

### Native

A declaration implemented by the compiler and its runtime instead of by TorbScript code, such as `Array`, `String`
or a collection's storage. Only `std/` may declare one; see [Foreign functions](language/extensibility/foreign-functions.md)
for the same idea applied to a C library instead of the runtime.

### Object safety

Whether a trait's member can be called on a value known only through the trait: a member that mentions `Self` in a
parameter or its result, or that is `static`, cannot be. See [Object safety](language/traits/object-safety.md).

### Option

A value that may be absent, `Some(value)` or `None`; `Value?` is sugar for `Option<Value>`. There is no `null` and no
implicit `Some`: a value never wraps itself into one. See [Option](language/values-and-types/option.md).

### OrElse

The trait behind `a ?? b`, `fn orElse(fallback: lazy Value): Value`, in the prelude and implemented by `Option` and
`Result`. A type that does not come with it hears so at the operator. See
[Optional chaining](language/errors/option-chaining.md).

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

The method that returns a changed copy, next to the [verb](#verb) that changes in place: `appended` next to `append`, `sorted`
next to `sort`. See [Verbs and participles](language/types/verbs-and-participles.md).

### Pattern binding

The name a pattern gives a part of the value it matched. A lowercase first letter makes it a binding and an uppercase
one a [case](#case), and in a refutable position - a `match` arm, `if const`/`if var`, `while const` - the guard or the
body has to read it. See [Pattern forms](language/pattern-matching/pattern-forms.md).

### Pipeline

A source, zero or more lazy stages (`map`, `filter`, `take`, `sorted`), and one terminal operation (`toList()`, `fold`,
`count`) that pulls the values through. Nothing runs until the terminal operation does.

### Prelude

The package whose public names - the members it re-exports by path included - are in scope in every file, `std/prelude`
by default. It holds the pure part of the standard library; what a program can touch stays an explicit import. See
[The prelude](language/modules-and-packages/the-prelude.md).

### Property command

A trailing block on a field whose value is a record type, which configures that value in place rather than replacing
it: `database { url = "..." }`. A field itself is written only with `=` - `port 8080` is a compile error, write
`port = 8080`. See [Property commands](language/types/property-commands.md).

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

A closure whose function type names its first parameter `self`, so names inside it resolve against that receiver. It is what a builder,
a configuration block and a method all are. See
[Receiver closures](language/configuration/receiver-closures.md).

### Receiver script

A `.trb` file loaded as the body of a [receiver closure](#receiver-closure), type checked against the receiver type.
`project.trb` is one, with the receiver `Project`. See [Receiver scripts](language/configuration/receiver-scripts.md).

### Registry

Where a published package's versions live. A project binds an owner to one registry
(`registry "acme", url: "https://packages.acme.test"`), so a public package can never take the place of a private one
under the same bare name. See [Packages](language/modules-and-packages/packages.md).

### Rest pattern

The `...` of a pattern, which stands for the parts the pattern does not name. In a list pattern it may take a name
(`[first, ...rest]`) and then binds those items as a `List`; in a constructor pattern it takes none
(`Config(host, ...)`), because the fields behind it are named and heterogeneous. See
[Pattern forms](language/pattern-matching/pattern-forms.md).

### Result

The result of an operation that can fail, `Ok(value)` or `Fail(error)`. The postfix `?` unwraps an `Ok` or returns the
`Fail` from the surrounding function, converting the error type through `From` where they differ. See
[Result](language/errors/result.md).

### Safety net

What keeps [`torb format`](tooling/torb-format.md) from changing what a program means: an edit of the canon is applied
on its own and the file is parsed again, and it is dropped and reported unless the tree that comes back is the one from
before with every span and call style erased - and the layout of a whole file is checked the same way.

### Sandbox

Where a [receiver script](#receiver-script) runs: no IO, no network, no clock, no environment and no foreign
functions by default, and every [capability](#capability) beyond that granted at the call site of
`Sandbox.load`, never by the script itself. See [The sandbox](language/configuration/the-sandbox.md).

### Shared type

A type declared `shared type`, which has an identity: assigning it does not copy, and everybody who holds it sees the same
object. Handles to the outside world are shared types. See [Shared types](language/types/shared-types.md).

### Sink

The writing end of a stream, `Sink<Item, Failure>`, with `add` in place of an `Accumulator`'s `add` and `end()` as its
graceful end, each answering a `Task`; `close()` is the abrupt end. See
[Streams](language/concurrency-and-streams/streams.md).

### Skill

The Agent Skill derived from this documentation by `torb docs skill`: a folder whose `SKILL.md` has a `name` and a
`description` and whose `reference/` holds the pages. See The Agent Skill.

### Seed

A `torb` binary that already exists, which `sh tools/bootstrap.sh` compiles the current compiler sources with. The
compiler is written in TorbScript, so something that already compiles TorbScript has to build it once. See
the architecture.

### Snippet

A fenced code block of this documentation. A `trb` snippet is verified by the compiler's own front end; the marker in the
fence decides how hard. See The docs commands.

### Source

The reading end of a stream, `Source<Item, Failure>`, with `next` in place of an `Iterator`'s `next`, answering a
`Task` instead of the value directly. See [Streams](language/concurrency-and-streams/streams.md).

### Stage

The synchronous middle of a pipeline, `Stage<Input, Output>`, written once and driven by both an `Iterate` and a
`Source`. See [Pipelines](language/collections-and-iteration/pipelines.md).

### Static member

A member declared `static`: it belongs to the type and not to a value, so it is reached as `Type.member(...)` and
never through an instance. `static fn` for a function, `static name = value` for a constant; `static var` does not
exist. See [Methods and `static fn`s](language/types/methods.md).

### Stream

One flow in one direction, named by whichever end a signature holds - `Source` to read or `Sink` to write - never
"the stream" itself, because a program holds one end at a time. See [std/stream](standard-library/stream.md).

### Supertrait

A trait that another trait requires with `with` at its own declaration, such as `trait Compare with Equals`; every
implementor of the smaller trait implements it too. See [Supertraits](language/traits/supertraits.md).

### Tail call

A call in tail position of a function that calls itself directly. Guaranteed not to grow the stack; every other call
uses a frame of its task's stack, and running out of stack panics with `stack overflow`. See
[Tail calls and stack overflow](language/execution/tail-calls.md).

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

### Trait-typed value

A value whose static type is a trait, such as a `Shape` or an `Iterate<Int>`: it holds some implementation and is
dispatched through a [witness table](#witness-table). Never called a trait object or an existential. See
[Trait types](language/traits/trait-types.md).

### Type parameter

A name standing for a type, filled in at each use of a `fn`, `type`, `trait` or `extend`, written out like a type
(`Item`, `Value`) rather than a single letter. See [Type parameters](language/generics/type-parameters.md).

### Var path

A path from a binding down to a value through which a change is legal: a `var` binding, `var` parameter or a `var fn` receiver, then
`var` fields, indices and ranges all the way. Without one, nothing changes. See
[Mutation and var paths](language/types/var-paths.md).

### Variadic parameter

A parameter written `...name: Type`, which collects every remaining positional argument into a `List<Type>`; a caller
spreads a collection into it with `...`. See [Variadic parameters](language/functions/variadics.md).

### Verb

A method that changes its receiver in place and is a `var fn`, next to its [participle](#participle), which returns
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

