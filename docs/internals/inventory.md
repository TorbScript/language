---
title: The page inventory
summary: The historical work plan the documentation tree was written from, every page listed with its path, kind, scope and sources, grouped by the package a writer took.
kind: explanation
status: stable
skill: omit
order: 10
keywords:
  - inventory
  - work plan
  - phase two
  - packages of work
source:
  - CONCEPT.md
  - std
---

This was the work plan. Every page of the tree is listed here with the path it got, the kind it is, the one line it
had to answer, and where its facts came from - split into thirteen packages so that independent writers could each
take one without touching the same file. Every page listed here exists today, so this document is kept as a record
of how the tree was planned rather than as an open queue; [Adding a page](../contributing/adding-a-page.md) is what a
genuinely new page follows now.

## The decision

The work is split into thirteen packages, each one a folder or a group of subfolders with no overlap. Two writers can
take two packages at the same time without touching the same file. Every package can be finished on its own, because a
page is written to stand alone and the index it belongs to is generated.

| Package | Paths | Pages | Depends on |
|---------|-------|-------|------------|
| 1 | `guide/` | 13 | The reference for the links, so write it late or link forward |
| 2 | `language/syntax/`, `language/values-and-types/` | 22 | Nothing |
| 3 | `language/functions/` | 8 | Nothing |
| 4 | `language/types/` | 13 | Nothing |
| 5 | `language/traits/`, `language/generics/` | 14 | Nothing |
| 6 | `language/pattern-matching/`, `language/errors/` | 13 | Nothing |
| 7 | `language/collections-and-iteration/`, `language/concurrency-and-streams/` | 11 | `docs/design/STREAMS.md` for the streams pages |
| 8 | `language/modules-and-packages/`, `language/reflection/`, `language/configuration/`, `language/execution/`, `language/extensibility/` | 21 | Nothing |
| 9 | `standard-library/` | 22 | The `std/` sources only |
| 10 | `how-to/` | 14 | The reference, for the links |
| 11 | `explanation/` without the contrast pages | 16 | The Decision Log of `CONCEPT.md` |
| 12 | The four contrast pages | 4 | The reference, and knowledge of the other language |
| 13 | `tooling/` | 13 | `compiler/src/cli/`, `compiler/src/main.trb` |

`glossary.md` is not a package: every writer adds the terms their pages use, in alphabetical order, at most two
sentences each.

## Why

**One folder per writer, because a folder is the unit that has no shared files.** The index of a folder is generated, so
two writers never edit the same index; the only shared file in the whole tree is `glossary.md`, and an entry there is one
paragraph that can be added without reading the rest.

**The reference before the guide and the how-to pages.** A guide page links into `language/`, and a link that does not
resolve is an error. Packages 2 to 9 have no dependency on anything but `CONCEPT.md` and `std/`, so they go first; 1, 10
and 12 come after them.

**A page that has no source is not written.** Every row below names where its facts come from. Where `CONCEPT.md` is
silent or contradicts the compiler, the page says so and the contradiction goes into the report rather than being
resolved by the writer.

## The pages

The columns are the path below `docs/`, the `title`, the `kind`, the one line the page answers, and the `source` field
it carries. `done` in the last column means the page exists and passes the gate.

### Package 1: guide

The learning path, in order, mirroring `examples/tour/src/*.trb`. Every page has `order` and `prerequisites`.

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `guide/the-language-in-sixty-seconds.md` | The language in sixty seconds | guide | What kind of language this is, in one screen | done |
| `guide/installing-and-running.md` | Run your first program | guide | How to get from nothing to output | done |
| `guide/values-and-bindings.md` | Values and bindings | guide | Why `const` and `var` are the whole mutation story | done |
| `guide/functions-and-closures.md` | Functions and closures | guide | How to declare one, and the one closure form | `CONCEPT.md#functions`, tour 02 |
| `guide/types-and-methods.md` | Types and methods | guide | How to declare a type and give it behaviour | `CONCEPT.md#types`, tour 03 |
| `guide/cases-and-matching.md` | Cases and matching | guide | How a type with cases is declared and taken apart | `CONCEPT.md#algebraic-data-types-and-pattern-matching`, tour 04 |
| `guide/traits.md` | Traits | guide | How a capability is declared and given to a type | `CONCEPT.md#traits`, tour 05 |
| `guide/errors.md` | Errors | guide | How a function says it can fail, and how a caller handles it | `CONCEPT.md#error-handling`, tour 06 |
| `guide/collections-and-pipelines.md` | Collections and pipelines | guide | How to build, read and transform a collection | `CONCEPT.md#collections-and-iteration`, tour 07 |
| `guide/control-flow-and-dsls.md` | Control flow and your own constructs | guide | Why `unless` is a function and how to write one | `CONCEPT.md#blocks-and-control-flow`, tour 08, tour 09 |
| `guide/modules-and-packages.md` | Modules and packages | guide | How a program is split into files and a project | `CONCEPT.md#modules-and-packages` |
| `guide/tests-and-tooling.md` | Tests and the toolchain | guide | How to write a test and run the checks | `CONCEPT.md#toolchain`, `std/test` |
| `guide/a-small-program.md` | Put it together | guide | One small program that uses everything learned | The examples |

### Package 2: syntax, values and types

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/syntax/command-calls.md` | Command calls | reference | When a call is written without parentheses | done |
| `language/syntax/cheat-sheet.md` | Syntax cheat sheet | reference | Every form of the language on one page | done |
| `language/syntax/lexical-structure.md` | Lexical structure | reference | Where a statement ends and what a comment is | `CONCEPT.md#lexical-structure` |
| `language/syntax/literals.md` | Literals | reference | How to write a number, a string, a character | `CONCEPT.md#lexical-structure` |
| `language/syntax/string-interpolation.md` | String interpolation | reference | What `{...}` in a string does and how to escape it | `CONCEPT.md#strings` |
| `language/syntax/multi-line-strings.md` | Multi-line strings | reference | How `"""` is dedented, and the four rules of it | `CONCEPT.md#strings` |
| `language/syntax/naming.md` | Naming | reference | Which characters a name may contain, which case it starts with, and what is only a convention | `CONCEPT.md#lexical-structure` |
| `language/syntax/generics-or-comparison.md` | Angle brackets or comparison | reference | How `f<T>(x)` is told from `a < b` | `CONCEPT.md#lexical-structure` |
| `language/syntax/doc-comments.md` | Doc comments | reference | What `/** */` attaches to and which headings it uses | `CONCEPT.md#doc-comments` |
| `language/values-and-types/bindings.md` | Bindings | reference | What `const` and `var` decide | done |
| `language/values-and-types/built-in-types.md` | Built-in types | reference | The types every file has without an import | `CONCEPT.md#built-in-types` |
| `language/values-and-types/integers.md` | Integers | reference | The widths, the defaults, and what overflow does | `CONCEPT.md#built-in-types` |
| `language/values-and-types/floating-point.md` | Floating-point numbers | reference | Why `==` and `compare` disagree, and why floats are not `Hash` | `CONCEPT.md#built-in-types` |
| `language/values-and-types/decimal.md` | Decimal | reference | Exact base-ten arithmetic, and that it is planned | `CONCEPT.md#built-in-types` |
| `language/values-and-types/strings.md` | Strings | reference | Why a `String` has no `length()` and no `text[i]` | `CONCEPT.md#strings` |
| `language/values-and-types/tuples.md` | Tuples | reference | Positional and labelled tuples, and what a label is not | `CONCEPT.md#built-in-types` |
| `language/values-and-types/ranges.md` | Ranges | reference | The three range types, and the `Bounds` trait | `CONCEPT.md#built-in-types` |
| `language/values-and-types/option.md` | Option | reference | How absence is modelled without `null` | `std/core/src/option.trb` |
| `language/values-and-types/void-and-never.md` | Void and Never | reference | The one value `void`, and the type with none | `CONCEPT.md#built-in-types` |
| `language/values-and-types/literal-types.md` | Literal types | reference | `"tcp" \| "udp"` and why only literals combine with `\|` | `CONCEPT.md#literal-types` |
| `language/values-and-types/type-aliases.md` | Type aliases | reference | `type X = Y`, and why there is no `alias` keyword | `CONCEPT.md#type-aliases` |
| `language/values-and-types/distinct-types.md` | Distinct types | reference | A single-field type plus `by`, instead of an opaque alias | `CONCEPT.md#distinct-types-opaque-aliases` |
| `language/values-and-types/arrays.md` | Arrays and const parameters | reference | A size in the type, and what may stand in one | `CONCEPT.md#const-parameters-and-array` |

### Package 3: functions

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/functions/declaring-a-function.md` | Declaring a function | reference | The form, hoisting, and when a result type is required | `CONCEPT.md#functions` |
| `language/functions/arguments.md` | Arguments and labels | reference | Positional, labelled, and the order rule | `CONCEPT.md#arguments` |
| `language/functions/default-values.md` | Default values | reference | When a default is evaluated, and in which scope | `CONCEPT.md#arguments` |
| `language/functions/variadics.md` | Variadic parameters | reference | `...rest`, and why a collection never unpacks by itself | `CONCEPT.md#arguments` |
| `language/functions/closures.md` | Closures | reference | The one closure form and what it captures | `CONCEPT.md#lambdas-and-closures` |
| `language/functions/trailing-closures.md` | Trailing closures | reference | When a brace can follow a call, and the implicit name | `CONCEPT.md#trailing-closures` |
| `language/functions/parameter-modes.md` | Parameter modes | reference | `var`, `lazy`, receiver closures, `Expression` | `CONCEPT.md#parameter-modes` |
| `language/functions/quoted-expressions.md` | Quoted expressions | reference | What `Expression<Value>` hands a function | `CONCEPT.md#quoted-expressions-expressionvalue` |

### Package 4: types

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/types/declaring-a-type.md` | Declaring a type | reference | Fields, methods, and what is generated | done |
| `language/types/fields.md` | Fields | reference | `var`, `private`, `private(var)`, and the table of four | `CONCEPT.md#visibility-and-encapsulation` |
| `language/types/construction.md` | Construction | reference | The one generated constructor, and where logic goes | `CONCEPT.md#construction` |
| `language/types/data-or-capsule.md` | Data or capsule | reference | What a `private` field without a default closes, and the factory, accessors and conversion pair that take its place | `CONCEPT.md#construction` |
| `language/types/copy-and-equality.md` | Copy and equality | reference | What `copy`, `==` and `hash` do without being written | `CONCEPT.md#values` |
| `language/types/generated-show.md` | The generated Show | reference | The exact text a value prints as | `CONCEPT.md#values` |
| `language/types/methods.md` | Methods and `static fn`s | reference | `static` and `var fn`, and the one member namespace | `CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure` |
| `language/types/verbs-and-participles.md` | Verbs and participles | reference | `sort` against `sorted`, and how to name a new pair | `CONCEPT.md#lexical-structure` |
| `language/types/var-paths.md` | Mutation and var paths | reference | What has to be `var` from the binding down | `CONCEPT.md#var-paths-and-var-parameters` |
| `language/types/exclusivity.md` | Exclusivity | reference | Which two accesses may not overlap | `CONCEPT.md#var-paths-and-var-parameters` |
| `language/types/shared-types.md` | Shared types | reference | When a value has an identity instead | `CONCEPT.md#identity-shared-type` |
| `language/types/conversions.md` | Conversions | reference | `From`, `Into`, `TryFrom`, `TryInto`, and the four coercions | `CONCEPT.md#conversions` |
| `language/types/property-commands.md` | Property commands | reference | Why a field is written only with `=`, and what a trailing block on a record field still does | `CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure` |

### Package 5: traits and generics

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/traits/traits.md` | Traits | reference | What a trait is and how a type comes with one | done |
| `language/traits/extend.md` | extend | reference | Adding members afterwards, with and without a trait | `CONCEPT.md#traits` |
| `language/traits/supertraits.md` | Supertraits | reference | `trait Compare with Equals`, and what it requires | `CONCEPT.md#traits` |
| `language/traits/trait-types.md` | Traits as types | reference | `fn draw(shape: Shape)`, and what it costs | `CONCEPT.md#traits` |
| `language/traits/intersections.md` | Trait intersections | reference | `Show & Encode` as a type and as a bound | `CONCEPT.md#traits` |
| `language/traits/delegation.md` | Delegation with by | reference | `with Add & Subtract by value`, and the one-field rule | `CONCEPT.md#distinct-types-opaque-aliases` |
| `language/traits/coherence.md` | Coherence and blanket implementations | reference | Which package may implement what | `CONCEPT.md#traits` |
| `language/traits/operators.md` | Operators are traits | reference | Which operator is which method | `CONCEPT.md#traits` |
| `language/traits/object-safety.md` | Object safety | reference | Which member cannot be called on a trait-typed value | `CONCEPT.md#traits` |
| `language/generics/type-parameters.md` | Type parameters | reference | Where they are declared, and their names | `CONCEPT.md#traits` |
| `language/generics/bounds.md` | Bounds | reference | `where`, inline bounds, and a member's own `where` | `CONCEPT.md#traits` |
| `language/generics/inference.md` | Inference | reference | What is inferred and what has to be written | `CONCEPT.md#key-facts` |
| `language/generics/no-higher-kinded-types.md` | No higher-kinded types | reference | The shared vocabulary that replaces them | `CONCEPT.md#one-vocabulary-instead-of-higher-kinded-types` |
| `language/generics/witnesses.md` | Witness tables | reference | How a generic member works on a trait-typed value | `CONCEPT.md#traits` |

### Package 6: pattern matching and errors

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/pattern-matching/cases-and-match.md` | Cases and match | reference | How a case is written, and the bare imported case | done |
| `language/pattern-matching/pattern-forms.md` | Pattern forms | reference | Every pattern the language has, one per line | `CONCEPT.md#algebraic-data-types-and-pattern-matching` |
| `language/pattern-matching/exhaustiveness.md` | Exhaustiveness | reference | Why every `match` covers everything, and unreachable arms | `CONCEPT.md#algebraic-data-types-and-pattern-matching` |
| `language/pattern-matching/importing-cases.md` | Importing cases | reference | `use Option.Some from "std/core"` and what changes | `CONCEPT.md#modules-and-packages` |
| `language/pattern-matching/patterns-in-bindings.md` | Patterns in bindings and conditions | reference | `const Point(x, y) =`, `if const`, `while const` | `CONCEPT.md#algebraic-data-types-and-pattern-matching` |
| `language/execution/loops.md` | Loops | reference | `for`, `while`, `loop`, `break`, `continue`, and why `while true` is an error | `CONCEPT.md#blocks-and-control-flow` |
| `language/pattern-matching/if-var.md` | if var | reference | Binding into a place instead of into a copy | `CONCEPT.md#algebraic-data-types-and-pattern-matching` |
| `language/errors/result.md` | Result | reference | `Ok`, `Fail`, and the vocabulary on them | done |
| `language/errors/option-chaining.md` | Optional chaining | reference | What `?.` and `??` are, and where they do not apply | `CONCEPT.md#error-handling` |
| `language/errors/question-mark.md` | The question mark operator | reference | Early return, and the conversion it does | `CONCEPT.md#error-handling` |
| `language/errors/the-error-trait.md` | The Error trait | reference | `Result<Value, Error>`, and `cause()` | `std/core/src/error.trb` |
| `language/errors/error-types.md` | Declaring an error type | reference | A type with cases, and the generated `From` | `CONCEPT.md#algebraic-data-types-and-pattern-matching` |
| `language/errors/panic.md` | panic | reference | What is printed, the exit code, and what does not run | `CONCEPT.md#error-handling` |
| `language/errors/top-level-errors.md` | Errors at the top level | reference | What a `?` in `main` prints, and the exit code | `CONCEPT.md#error-handling` |

### Package 7: collections, iteration, concurrency

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/collections-and-iteration/collection-traits.md` | The collection traits | reference | Why the type is a trait and the implementation a name | `CONCEPT.md#collections-and-iteration` |
| `language/collections-and-iteration/lists.md` | Lists | reference | The literal, the implementations, the verb pairs | `std/collections/src/list.trb` |
| `language/collections-and-iteration/maps-and-sets.md` | Maps and sets | reference | The literals, insertion order, and equality | `std/collections/src/map.trb` |
| `language/collections-and-iteration/stacks-and-queues.md` | Stacks and queues | reference | `push`/`pop`, `enqueue`/`dequeue` and their participles | `std/collections/src/queue.trb` |
| `language/collections-and-iteration/slices.md` | Slices | reference | `list[from..to]` as a value and as a path | `CONCEPT.md#collections-and-iteration` |
| `language/collections-and-iteration/iterating.md` | Iterating | reference | `for`, `Iterate`, `Iterator`, and what is evaluated once | `std/iteration/src/iteration.trb` |
| `language/collections-and-iteration/pipelines.md` | Pipelines | reference | Source, lazy stage, terminal operation | `CONCEPT.md#pipelines-and-collectors` |
| `language/collections-and-iteration/collectors.md` | Collectors | reference | `collect`, and how to write one | `std/iteration/src/collectors.trb` |
| `language/concurrency-and-streams/tasks.md` | Tasks | reference | `Task<Value>`, `await()`, and where it is allowed | `CONCEPT.md#concurrency-draft` |
| `language/concurrency-and-streams/channels.md` | Channels | reference | What crosses a task boundary | `CONCEPT.md#concurrency-draft` |
| `language/concurrency-and-streams/streams.md` | Streams | reference | Sources, stages and accumulators over time | `docs/design/STREAMS.md` |

The three pages of `concurrency-and-streams/` are `status: planned` until the runtime has them. A planned page carries
the planned banner and is left out of the generated skill.

### Package 8: modules, reflection, configuration, execution, extensibility

| Path | Title | Kind | Answers | Source |
|------|-------|------|---------|--------|
| `language/modules-and-packages/use.md` | use | reference | Every import form, and what follows `from` | `CONCEPT.md#modules-and-packages` |
| `language/modules-and-packages/visibility.md` | Visibility | reference | What `public` means at the top level | `CONCEPT.md#visibility-and-encapsulation` |
| `language/modules-and-packages/the-prelude.md` | The prelude | reference | What is in scope everywhere, and what deliberately is not | `std/prelude/src/lib.trb` |
| `language/modules-and-packages/packages.md` | Packages | reference | `owner/name`, `src/lib.trb`, and the dependency rules | `CONCEPT.md#packages-and-the-supply-chain` |
| `language/modules-and-packages/workspaces.md` | Workspaces | reference | One root, many projects, one lock file | `CONCEPT.md#workspaces` |
| `language/modules-and-packages/top-level-code.md` | Top-level code | reference | Which files may have it, and the compile-time constants | `CONCEPT.md#modules-and-packages` |
| `language/modules-and-packages/cyclic-imports.md` | Cyclic imports | reference | Why a cycle is allowed and what it cannot do | `CONCEPT.md#modules-and-packages` |
| `language/reflection/no-reflection.md` | There is no reflection | reference | Which bridges between type and value exist | `CONCEPT.md#types-values-and-reflection` |
| `language/reflection/encode-and-decode.md` | Encode and Decode | reference | The generated pair that replaces reflection | `CONCEPT.md#types-values-and-reflection` |
| `language/reflection/encoders.md` | Encoder and Decoder | reference | The four shapes and the six scalars | `std/encoding` |
| `language/configuration/receiver-closures.md` | Receiver closures | reference | A closure whose names resolve against a receiver | `CONCEPT.md#configuration-dsl` |
| `language/configuration/builders.md` | Builders and DSLs | reference | How a configuration block is typed | `CONCEPT.md#configuration-dsl` |
| `language/configuration/receiver-scripts.md` | Receiver scripts | reference | A file as the body of a receiver closure | `CONCEPT.md#receiver-scripts-and-the-sandbox` |
| `language/configuration/the-sandbox.md` | The sandbox | reference | What a script may reach, and how it is granted | `CONCEPT.md#receiver-scripts-and-the-sandbox` |
| `language/execution/evaluation-order.md` | Evaluation order | reference | The order a reader sees, spelled out | `CONCEPT.md#execution-model` |
| `language/execution/copies.md` | What a copy costs | reference | Which shape is copied and which shares storage | `CONCEPT.md#execution-model` |
| `language/execution/tail-calls.md` | Tail calls and stack overflow | reference | What is guaranteed, and when the stack overflows | `CONCEPT.md#execution-model` |
| `language/execution/destructors.md` | Destructors - close() runs at the last release, a binding that holds one ends with its block | reference | `close()` as the destructor, `using` as a binding, the release order | `docs/design/DESTRUCTORS.md` |
| `language/extensibility/control-structures.md` | Control structures are functions | reference | `do`, `unless`, `retry`, `using`, and how to add one | `CONCEPT.md#extensibility` |
| `language/extensibility/expression-trees.md` | Reading code instead of running it | reference | What a query provider gets, and what it cannot get | `CONCEPT.md#quoted-expressions-expressionvalue` |
| `language/extensibility/foreign-functions.md` | Foreign functions | reference | `foreign`, the C ABI, and what a sandbox never grants | `CONCEPT.md#foreign-functions-draft` |

### Package 9: standard library

One page per package of `std/`, each `kind: package`, each with the `Declarations` section between the generator
markers so that milestone 8's `torb doc` can fill it.

| Path | Title | Answers | Source |
|------|-------|---------|--------|
| `standard-library/core.md` | std/core | The bottom of the library | done |
| `standard-library/text.md` | std/text | `Char` and `String` | `std/text/src/lib.trb` |
| `standard-library/number.md` | std/number | Every numeric type and `Bits` | `std/number` |
| `standard-library/collections.md` | std/collections | The collection traits and implementations | `std/collections` |
| `standard-library/iteration.md` | std/iteration | `Iterate`, the stages, the collectors | `std/iteration` |
| `standard-library/encoding.md` | std/encoding | `Encode`, `Decode`, the encoders | `std/encoding` |
| `standard-library/expression.md` | std/expression | Quotations, `assert`, `nameOf` | `std/expression` |
| `standard-library/task.md` | std/task | Tasks and channels | `std/task` |
| `standard-library/console.md` | std/console | `print` and `printError` | `std/console` |
| `standard-library/json.md` | std/json | `Json`, `JsonValue` | `std/json` |
| `standard-library/time.md` | std/time | `Duration`, `Instant`, `Clock` | `std/time` |
| `standard-library/fs.md` | std/fs | `File` and `IoError` | `std/fs` |
| `standard-library/io.md` | std/io | Standard input and the streams | `std/io` |
| `standard-library/process.md` | std/process | Arguments, exit, running a program | `std/process` |
| `standard-library/os.md` | std/os | The environment, the system, the well-known directories | `std/os` |
| `standard-library/test.md` | std/test | `test`, `group`, `assert` | `std/test` |
| `standard-library/http.md` | std/http | Requests and responses | `std/http` |
| `standard-library/sandbox.md` | std/sandbox | `Sandbox`, `Script`, the capabilities | `std/sandbox` |
| `standard-library/project.md` | std/project | The receiver type of `project.trb` | `std/project` |
| `standard-library/prelude.md` | std/prelude | What is in scope everywhere | `std/prelude` |
| `standard-library/stream.md` | std/stream | Streams over time | `std/stream` |

### Package 10: how-to

| Path | Title | Answers | Source |
|------|-------|---------|--------|
| `how-to/read-a-file.md` | Read a file | done | done |
| `how-to/write-a-configuration-file.md` | Write a configuration file | done | done |
| `how-to/define-an-error-type.md` | Define an error type | One type with cases that every layer can hand up | `CONCEPT.md#error-handling` |
| `how-to/parse-text-into-a-type.md` | Parse text into a type | A private field plus `TryFrom<String, _>` | `CONCEPT.md#construction` |
| `how-to/write-a-test.md` | Write a test | `test`, `group`, `assert` and where the file goes | `std/test` |
| `how-to/build-a-native-binary.md` | Build a native binary | `torb build`, the C back end, the output path | `compiler/src/cli/build.trb` |
| `how-to/add-a-dependency.md` | Add a dependency | `project.trb`, the lock file, the capabilities | `CONCEPT.md#packages-and-the-supply-chain` |
| `how-to/use-a-type-as-a-map-key.md` | Use a type as a map key | Which traits have to hold, and which cannot | `CONCEPT.md#values` |
| `how-to/convert-between-types.md` | Convert between types | `From`, `Into`, `TryFrom`, `to<Target>()` | `CONCEPT.md#conversions` |
| `how-to/collect-a-pipeline.md` | Collect a pipeline into what you need | The terminal operations and the collectors | `std/iteration` |
| `how-to/sort-by-more-than-one-key.md` | Sort by more than one key | A tuple key, and why a type has no generated order | `CONCEPT.md#values` |
| `how-to/write-a-builder.md` | Write a builder | A receiver closure and property commands | `CONCEPT.md#configuration-dsl` |
| `how-to/read-and-write-json.md` | Read and write JSON | `Json().encode`, `Json().decode<T>`, the naming option, and hand-written pairs | `std/json` |
| `how-to/set-up-a-workspace.md` | Set up a workspace | The root manifest and the members | `CONCEPT.md#workspaces` |

### Package 11: explanation

Distilled from the Decision Log of `CONCEPT.md`. Each page takes one decision, gives the argument, and says what a
reader has to do differently. These pages matter most for a model: a wrong mental model produces code that compiles and
means something else.

| Path | Title | Answers | Source |
|------|-------|---------|--------|
| `explanation/why-values-instead-of-references.md` | Why values instead of references | done | done |
| `explanation/mistakes-models-make.md` | What a model trained on other languages gets wrong | done | done |
| `explanation/why-no-null.md` | Why there is no null | `Option`, and the absence of an implicit `Some` | Decision Log |
| `explanation/why-no-exceptions.md` | Why there are no exceptions | `Result`, `?`, and what a panic is for | Decision Log |
| `explanation/why-cases-are-never-bare.md` | Why a case is never bare | The import rule, and the trap it closes | Decision Log |
| `explanation/why-commands.md` | Why a call is written as a command | The canon, and what it buys | `CONCEPT.md#formatter-canon` |
| `explanation/why-verbs-and-participles.md` | Why verbs and participles | `sort` against `sorted`, and the rejected alternatives | Decision Log |
| `explanation/why-no-getters.md` | Why there are no properties | Fields, methods, and what `()` tells a reader | Decision Log |
| `explanation/why-dead-changes-are-errors.md` | Why a change that cannot be seen is an error | The copy trap, and the one rule behind it | Decision Log |
| `explanation/why-no-higher-kinded-types.md` | Why there are no higher-kinded types | One vocabulary, and what it costs | Decision Log |
| `explanation/why-no-macros.md` | Why there are no macros | Name resolution needs types, and quotations instead | `CONCEPT.md#extensibility` |
| `explanation/why-no-reflection.md` | Why there is no reflection | Two back ends, and the generated pair instead | Decision Log |
| `explanation/why-bit-operators-bind-like-arithmetic.md` | Why the bit operators bind like arithmetic | `Bits`, the precedence of Go and Swift, and wrapping arithmetic | Decision Log |
| `explanation/why-traits-instead-of-inheritance.md` | Why traits instead of inheritance | Composition, delegation, and no base classes | Decision Log |
| `explanation/why-one-member-namespace.md` | Why a method is a constant | One namespace, and what property commands need | Decision Log |
| `explanation/why-exhaustive-matches.md` | Why every match is exhaustive | A public ADT as a promise, and how to stay free to add | Decision Log |

### Package 12: the contrast pages

Four pages, each `kind: contrast`, each titled `Coming from <language>`. They are the highest-value pages for a model,
because the failure is not ignorance but the habits of a language it does know.

| Path | Title | The three or four differences that bite |
|------|-------|------------------------------------------|
| `explanation/coming-from-rust.md` | Coming from Rust | done |
| `explanation/coming-from-swift.md` | Coming from Swift | `let` against `const`, no `mutating`, cases are imported not inferred, no protocols with associated types |
| `explanation/coming-from-kotlin.md` | Coming from Kotlin | No nullable types with `?:` semantics, `val`/`var` against `const`/`var`, no data classes, no extension receivers without a type |
| `explanation/coming-from-typescript.md` | Coming from TypeScript | No structural typing, no unions of types, no `any`, no `null`/`undefined`, generics are declarative |

### Package 13: tooling

| Path | Title | Answers | Source |
|------|-------|---------|--------|
| `tooling/the-torb-command.md` | The torb command | done | done |
| `tooling/verifying-your-work.md` | Verify your work | done | done |
| `tooling/torb-check.md` | torb check | What it checks, what the flags do | `compiler/src/main.trb` |
| `tooling/torb-run.md` | torb run | Running a project and a single file | `compiler/src/main.trb` |
| `tooling/torb-build.md` | torb build | The C back end, the flags, the output | `compiler/src/cli/build.trb` |
| `tooling/torb-test.md` | torb test | Where tests live and how they run | `std/test` |
| `tooling/torb-canon.md` | torb canon | The canon, the rules it can write, and every rule of the canon in one place | `compiler/src/canon/command.trb` |
| `tooling/project-trb.md` | project.trb | Every field of the manifest | `std/project` |
| `tooling/project-lock-trb.md` | project.lock.trb | What is pinned, and who may write it | `CONCEPT.md#packages-and-the-supply-chain` |
| `tooling/torb-doc.md` | torb doc | Documentation from doc comments | `CONCEPT.md#toolchain` |
| `tooling/torb-format.md` | torb format | The formatter that takes over from `canon` | `CONCEPT.md#formatter-canon` |
| `tooling/torb-lint.md` | torb lint | The naming and style rules | `CONCEPT.md#toolchain` |

`torb doc`, `torb format`, `torb lint` and `torb repl` are `status: planned`.

## Consequences

For a writer taking a package:

1. **Read [How to write here](../contributing/writing.md) and [the front matter](../contributing/front-matter.md)
   first.** They are short and they are the whole contract.
2. **Copy a template from [Add a page](../contributing/adding-a-page.md)**, and copy the tone from the page marked
   `done` in your
   package. A page written like page 4 is the goal; a page written in a new style is a defect even when it is correct.
3. **Take every fact from the `source` column.** Read the section of `CONCEPT.md` and the `std/` file, and run the
   example through the toolchain. Never write a snippet from memory.
4. **Create the folder's `index.md` together with your first page**, with a `## What belongs here` section that says
   what does not belong in it.
5. **Add every term your pages use to [the glossary](../glossary.md)**, in alphabetical order, at most two sentences.
6. **Run `docs index` and then `docs check`**, both green, before you are done.
7. **Report what you could not decide.** Where `CONCEPT.md` is silent, ambiguous, or contradicted by the compiler, say
   so in your report instead of choosing. That list is more valuable than a page that guesses.

## Related

- [Add a page](../contributing/adding-a-page.md) - the steps and the templates.
- [How to write here](../contributing/writing.md) - the writing rules.
- [The front matter](../contributing/front-matter.md) - the nine fields.
- [The docs commands](../contributing/checks.md) - the gate.
- [How this documentation is structured](../contributing/structure.md) - the tree and the kinds.
