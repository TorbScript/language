# The TorbScript documentation

Every page of the reference, with what it answers. Search this file for a word, then open the one page
that answers the question. A page marked (planned) describes a feature that does not compile yet.

## Contents

- the root
- design
- explanation
- guide
- how-to
- language
- language/collections-and-iteration
- language/concurrency-and-streams
- language/configuration
- language/errors
- language/execution
- language/extensibility
- language/functions
- language/generics
- language/modules-and-packages
- language/pattern-matching
- language/reflection
- language/syntax
- language/traits
- language/types
- language/values-and-types
- standard-library
- tooling

## The root

- `glossary.md` - **Glossary** (glossary): Every term this documentation uses, one entry each, at most two sentences. The entry decides which word is correct.

## design

- `design/index.md` - **Design records** (index): The specification documents behind a language or library feature that is still being built, each opening with a status line that says how much of it exists today.

## explanation

- `explanation/coming-from-kotlin.md` - **Coming from Kotlin** (contrast): What carries over from Kotlin - when expressions, extension functions, a nullable-looking ? - and the three places nullability, data classes and DSL receivers work on a different mechanism underneath.
- `explanation/coming-from-rust.md` - **Coming from Rust** (contrast): What carries over from Rust, what looks the same and is not, and what Rust has that TorbScript deliberately does not.
- `explanation/coming-from-swift.md` - **Coming from Swift** (contrast): What carries over from Swift - value types, enums with payloads, Optionals, protocols with default members - and what a Swift habit gets wrong here.
- `explanation/coming-from-typescript.md` - **Coming from TypeScript** (contrast): What carries over from TypeScript - literal unions, structural-looking optional chaining, declarative generics - and the four places TypeScript's type-level programming has no counterpart at all.
- `explanation/index.md` - **Why the language is like this** (index): The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.
- `explanation/mistakes-models-make.md` - **What a model trained on other languages gets wrong** (explanation): The mistakes a language model makes in TorbScript because it has read Rust, Swift, Kotlin and TypeScript, each with the wrong line, the right line and the diagnostic.
- `explanation/where-are-my-overloads.md` - **Where are my overloads** (explanation): A call has exactly one signature, because that signature is what gives every argument its meaning, so overloading by parameter type and uniform function call syntax are both out.
- `explanation/why-bit-operators-bind-like-arithmetic.md` - **Why the bit operators bind like arithmetic** (explanation): The integer types have `&`, `|`, `^`, `~`, `<<` and `>>` on the levels Go and Swift give them, so `x & 1 == 0` is the test it looks like, and they are the members of the Bits trait the way `+` is Add.
- `explanation/why-cases-are-never-bare.md` - **Why a case is never bare** (explanation): Circle alone is a type, a function or a variable, exactly like every other name, so a case is written Shape.Circle, .Circle or imported by its path, and a misspelled case can never fall back to matching everything.
- `explanation/why-commands.md` - **Why a call is written as a command** (explanation): A call is written without parentheses wherever the grammar allows it, so a control structure, a DSL and an ordinary call share one shape and no library gets special syntax the language itself does not have.
- `explanation/why-dead-changes-are-errors.md` - **Why a change that cannot be seen is an error** (explanation): A var that is changed and never read again, or the discarded result of a method that takes self, is a compile error rather than a lint, because with value semantics such a change is always a mistake and never a defensive copy.
- `explanation/why-exhaustive-matches.md` - **Why every match is exhaustive** (explanation): A public ADT is a promise about every case it has today, so a match must cover all of them and a new case is a breaking change, while a library that wants room to grow hides its ADT behind a type instead.
- `explanation/why-no-exceptions.md` - **Why there are no exceptions** (explanation): A function that can fail says so in its result type, Result<Value, Failure>, and the caller handles it with match, ??, or the question mark operator, while panic stays reserved for bugs that cannot be recovered from.
- `explanation/why-no-getters.md` - **Why there are no properties** (explanation): A field is storage and a method computes, so the parentheses tell a reader which one they are looking at, and private(var) replaces the getter-and-setter pair without hiding either fact.
- `explanation/why-no-higher-kinded-types.md` - **Why there are no higher-kinded types** (explanation): Option, Result, Task and Iterate share method names by convention of the standard library rather than by a shared abstraction, because a kind system would cost local inference and readable errors for problems a script rarely has.
- `explanation/why-no-macros.md` - **Why there are no macros** (explanation): Names are resolved with the help of types, which have to exist before resolution runs, so an AST macro generating code before that point cannot go together with the rest of the language - quoted expressions read code instead.
- `explanation/why-no-null.md` - **Why there is no null** (explanation): Absence is Option<Value>, an ordinary case of an ordinary type, so a value is wrapped and unwrapped on purpose and nothing can be dereferenced without checking first.
- `explanation/why-no-reflection.md` - **Why there is no reflection** (explanation): Types never flow as values, so nothing can inspect a type at runtime; what reflection is reached for - serialization, schemas, debug output - is covered by three generated forms of a constructor, Encode, Decode and Describe.
- `explanation/why-one-member-namespace.md` - **Why a method is a constant** (explanation): A method is structurally a constant of the type that holds a receiver closure, so a field and a method live in one namespace and cannot share a name, which is what lets a trailing block configure a record field in place.
- `explanation/why-traits-instead-of-inheritance.md` - **Why traits instead of inheritance** (explanation): A type comes with a trait instead of extending a base class, so composition and delegation replace an inheritance hierarchy, and a value can still be typed by capability without carrying a class it did not ask for.
- `explanation/why-values-instead-of-references.md` - **Why values instead of references** (explanation): Every type is a value and the binding decides about mutation, which removes every mutable-and-immutable type pair at the price of one local, lintable trap.
- `explanation/why-verbs-and-participles.md` - **Why verbs and participles** (explanation): A method that changes in place and the method that returns a changed copy are different words, sort against sorted, because value semantics make one name for both ambiguous at the call site.

## guide

- `guide/a-small-program.md` - **Put it together** (guide): One small program - a type with cases, a function that can fail, and a pipeline - that uses everything this path taught.
- `guide/cases-and-matching.md` - **Cases and matching** (guide): How to declare a type with more than one shape, and take it apart with a match that has to cover every case.
- `guide/collections-and-pipelines.md` - **Collections and pipelines** (guide): How to build a list, map and set, change one in place or get a changed copy, and pull values through a lazy pipeline.
- `guide/control-flow-and-dsls.md` - **Control flow and your own constructs** (guide): if, for, while and loop as you would expect, and why unless is an ordinary function you could have written yourself.
- `guide/errors.md` - **Errors** (guide): How a function says it can fail with Result, and how a caller handles that with match or the question mark operator.
- `guide/functions-and-closures.md` - **Functions and closures** (guide): How to declare a function, when it must spell out its return type, and the one closure form the language has.
- `guide/idiomatic-torbscript.md` - **Idiomatic TorbScript** (guide): The habits that make TorbScript read like TorbScript - names, mutation, calls, types, errors, closures, resources and tasks - each as one rule, one runnable example and the reason behind it.
- `guide/index.md` - **Learn TorbScript** (index): The learning path from nothing to a working program, in order, one step per page.
- `guide/installing-and-running.md` - **Run your first program** (guide): Build the toolchain, run a single file, and create a project with a manifest, a source file and a test.
- `guide/modules-and-packages.md` - **Modules and packages** (guide): How use brings a name in from another file or the standard library, and what public means for a top-level declaration.
- `guide/tests-and-tooling.md` - **Tests and the toolchain** (guide): How to write a test with test, group and assert, and the two commands that check whether what you wrote is correct.
- `guide/the-language-in-sixty-seconds.md` - **The language in sixty seconds** (guide): The mental model of TorbScript in one screen: values, bindings, no null, no exceptions, traits, and calls written as commands.
- `guide/traits.md` - **Traits** (guide): How to declare a capability, give it to a type, and use the trait itself as a type that hides which concrete type it is.
- `guide/types-and-methods.md` - **Types and methods** (guide): How to declare a type, add methods to it, and tell a verb that changes it from the participle that answers a copy.
- `guide/values-and-bindings.md` - **Values and bindings** (guide): Why const and var are the whole mutation story, what a copy costs, and the one trap that catches everybody coming from a language with references.

## how-to

- `how-to/add-a-dependency.md` - **Add a dependency** (how-to): Declare the package in project.trb before importing from it, tell a runtime dependency from a development one, and read what the imports of everything you depend on say it can reach.
- `how-to/build-a-native-binary.md` - **Build a native binary** (how-to): Point torb build at the entry file, look at the generated C with --emit-c first if a C compiler is not on the machine yet, and read what the back end does not lower yet before you debug the program instead.
- `how-to/collect-a-pipeline.md` - **Collect a pipeline into what you need** (how-to): Reach for the named terminal operation when there is one - toList, sum, joined, groupBy - and fall back to collect with an Accumulator for anything else, including your own accumulator.
- `how-to/convert-between-types.md` - **Convert between types** (how-to): Implement From when the conversion cannot fail and TryFrom when it can - text is a source like any other - and call to<Target>() to collect a pipeline into any type built from one.
- `how-to/define-an-error-type.md` - **Define an error type** (how-to): Declare a type with one case per distinct failure, add Show and Error where a layer above needs to hand it further up, and let the generated From do the conversion at every ?.
- `how-to/index.md` - **Task recipes** (index): One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works.
- `how-to/parse-text-into-a-type.md` - **Parse text into a type** (how-to): Give the type a private field nothing outside can set directly, and implement TryFrom<String, Failure> so that Type.tryFrom(text) validates the text and answers a Result instead of a bare value.
- `how-to/read-a-file.md` - **Read a file** (how-to): Read a whole file or its lines, hand the failure to the caller with the question mark operator, and turn an IoError into your own error type.
- `how-to/read-and-write-json.md` - **Read and write JSON** (how-to): Json().encode and Json().decode<T> work on any Encode/Decode type for free; an option of the format spells the field names, and the pair is written by hand only where a constructor cannot say what a document may.
- `how-to/set-up-a-workspace.md` - **Set up a workspace** (how-to): Name the member directories in the root project.trb, give each one its own project.trb, and depend on a sibling by name alone - the workspace resolves it from source.
- `how-to/sort-by-more-than-one-key.md` - **Sort by more than one key** (how-to): Sort by a tuple key instead of a single field - a tuple's Compare is generated lexicographically by position, which a type never gets because an order is a decision, not a structure.
- `how-to/use-a-type-as-a-map-key.md` - **Use a type as a map key** (how-to): An ordinary type is already a legal key once every field is Hash, which the compiler generates for free; a field that cannot be Hash is the one thing that rules a type out.
- `how-to/write-a-builder.md` - **Write a builder** (how-to): Write a function that creates a value, hands it to a receiver closure, and returns it - three lines that make every property command, nested block and method call in the closure statically typed.
- `how-to/write-a-configuration-file.md` - **Write a configuration file** (how-to): Declare a type for the configuration, write the file as TorbScript against it, and load it through the sandbox with the capabilities you grant.
- `how-to/write-a-test.md` - **Write a test** (how-to): Put a test in tests/*.test.trb, group related ones, and let assert show the source and the values instead of writing a matcher.

## language

- `language/index.md` - **The language reference** (index): One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites.

## language/collections-and-iteration

- `language/collections-and-iteration/collection-traits.md` - **The collection traits** (reference): Every kind of collection is a trait - List, Set, Map, Stack, Queue - so a signature names what a value can do, and only its construction names the data structure behind it.
- `language/collections-and-iteration/collectors.md` - **Collectors** (reference): An Accumulator describes what to do with the values of a pipeline and is the state of one run at the same time, because a value is a copy; collect fills a copy of the one it is given.
- `language/collections-and-iteration/index.md` - **Collections and iteration** (index): List, Map, Set, Stack and Queue as traits over a shared Iterate, plus slices, pipelines and collectors.
- `language/collections-and-iteration/iterating.md` - **Iterating** (reference): for pulls from Iterator.next() through Iterate.iterate(), and the subject of a for is evaluated once into a temporary, so changing it inside the loop does not affect what is walked.
- `language/collections-and-iteration/lists.md` - **Lists** (reference): List is the ordered, indexable sequence behind the literal [1, 2, 3], with ArrayList as the default implementation and a verb paired with a participle for every change.
- `language/collections-and-iteration/maps-and-sets.md` - **Maps and sets** (reference): The literal ["a": 1] builds a Map, a Set is built from a list literal instead of having one of its own, and both iterate in insertion order while comparing regardless of it.
- `language/collections-and-iteration/pipelines.md` - **Pipelines** (reference): A pipeline is a source, zero or more lazy stages and exactly one terminal operation, and nothing runs until the terminal operation pulls a value through.
- `language/collections-and-iteration/slices.md` - **Slices** (reference): list[from..to] answers a List that shares storage and starts at index 0 again; as a var path the same expression is a window into the original instead.
- `language/collections-and-iteration/stacks-and-queues.md` - **Stacks and queues** (reference): Stack is LIFO with push, pop and peek, Queue is FIFO with enqueue, dequeue and peek - the words everybody knows for each structure.

## language/concurrency-and-streams

- `language/concurrency-and-streams/channels.md` - **Channels** (reference): A Channel is a stream in memory whose one holder has both ends, handed out separately as a Source and a Sink so a producer never sees the reading end and a consumer never sees the writing one.
- `language/concurrency-and-streams/index.md` - **Concurrency and streams** (index): Task, Channel, Source and Sink - asynchrony in the type system instead of a keyword - designed and type-checked today, but not yet run by any back end.
- `language/concurrency-and-streams/streams.md` - **Streams** (reference): Source and Sink are the asynchronous siblings of Iterator and Accumulator, with the same verbs, the same Stage values in between, and a failure that stands in the type on both ends.
- `language/concurrency-and-streams/tasks.md` - **Tasks** (reference): Task<Value> is what an asynchronous function answers; await() waits for it and answers the value, and a cancellation is passed on to the waiter instead of answered.

## language/configuration

- `language/configuration/builders.md` - **Builders and DSLs** (reference): A builder is a function that creates a value, hands it to a receiver closure to configure, and returns it, which is what makes a configuration block a statically typed value instead of a string to parse.
- `language/configuration/index.md` - **Configuration** (index): Receiver closures, the builder function around one, and the receiver script and sandbox that let a whole file play the same role - statically typed configuration without a second language.
- `language/configuration/receiver-closures.md` - **Receiver closures** (reference): A receiver closure is a closure whose first parameter is called self, so names inside it resolve against that receiver first, exactly as inside a method.
- `language/configuration/receiver-scripts.md` - **Receiver scripts** (reference, draft): A .trb file can be loaded as the body of a receiver closure, type checked against a receiver type before it runs, and run by the sandboxed VM.
- `language/configuration/the-sandbox.md` - **The sandbox** (reference, draft): A script has no IO, no network, no clock, no environment and no foreign functions by default, and only the caller of Sandbox.load can grant more, in a block that names exactly what is granted.

## language/errors

- `language/errors/error-types.md` - **Declaring an error type** (reference): An error type is a type with cases like any other; a case that wraps one value of a type no other case wraps gets From generated, which is what makes ? convert on its own.
- `language/errors/index.md` - **Errors** (index): How a function says it can fail, how a caller handles it, and what a panic is for.
- `language/errors/option-chaining.md` - **Optional chaining** (reference): `?.` is Option.map, or Option.flatMap when the member itself answers an Option, so chaining never nests. `??` is the trait OrElse, which gives a lazy fallback for an absent Option, a failed Result or any type that comes with it.
- `language/errors/panic.md` - **panic** (reference): panic prints panic, the message and the site to standard error, exits with 101, and runs nothing else on the way out - it is for bugs, never for an expected failure.
- `language/errors/question-mark.md` - **The question mark operator** (reference): A postfix ? unwraps an Ok or a Some and returns the Fail or None from the surrounding function early, converting the error type through From when they differ.
- `language/errors/result.md` - **Result** (reference): A function that can fail answers Result<Value, Failure>, whose cases are Ok and Fail. The postfix question mark unwraps an Ok or returns the Fail from the surrounding function.
- `language/errors/the-error-trait.md` - **The Error trait** (reference): Error is a trait, not a base type; a failure that implements it fits into Result<Value, Error> for the layers that only need to report it, and cause() gives the chain.
- `language/errors/top-level-errors.md` - **Errors at the top level** (reference): A ? at the top level of an entry file or a script is not a panic; it is specified to print the error and exit with 1, walking cause() one line per link.

## language/execution

- `language/execution/compile-time-branches.md` - **Compile-time branches** (reference): A match, an if or an if const whose subject is a compile-time constant keeps the one arm its value selects - OperatingSystem.current is one - while every arm is still type checked on every machine and exhaustiveness is judged by the type.
- `language/execution/copies.md` - **What a copy costs** (reference): A copy always behaves the same way, but what it costs depends on the shape of the type - inline for a small fixed-size value, copy-on-write for heap-backed storage, and never for a shared type.
- `language/execution/destructors.md` - **Destructors - close() runs at the last release** (reference): A shared type's close() is its destructor - it runs exactly once at the last release, user code never calls it, and a binding that holds one is released at the end of its block, the last declared first.
- `language/execution/evaluation-order.md` - **Evaluation order** (reference): Evaluation order is source order - the receiver first, then the arguments as they are written, then the parameter defaults - so a side effect in an argument is exactly as predictable as reading the line.
- `language/execution/index.md` - **Execution** (index): The parts of running a program that are a rule of the language rather than an implementation detail - evaluation order, copies, tail calls, destructors, and which arm of a branch on a compile-time constant is compiled.
- `language/execution/loops.md` - **Loops** (reference): for walks an Iterate, while repeats while a condition holds, and loop is the endless one - with the type Never until a break gives it a Void. while true is an error, because never ending is a property of the syntax here.
- `language/execution/tail-calls.md` - **Tail calls and stack overflow** (reference): Direct self-recursion in tail position is guaranteed to run without growing the stack, and every other call uses a frame of its task's stack; a recursion that runs out of stack panics with stack overflow instead of crashing.

## language/extensibility

- `language/extensibility/control-structures.md` - **Control structures are functions** (reference): do, unless, retry and test are ordinary functions with a closure or lazy parameter, so writing your own control structure is nothing more than writing a function that takes one and calling it with a trailing closure.
- `language/extensibility/expression-trees.md` - **Reading code instead of running it** (reference): A query provider reads the typed tree of an Expression<Value> instead of running it, translates what it recognizes, and fails at its own runtime for a call it does not - the language cannot know in advance what a library can translate.
- `language/extensibility/foreign-functions.md` - **Foreign functions** (reference, planned): foreign declares functions of a C library with the ABI as the contract, available to any package unlike native, but nothing links or calls one yet and its Pointer and CString types are not declared in std/ either.
- `language/extensibility/index.md` - **Extensibility** (index): The language is extended by writing functions, not macros or annotations - control structures, DSLs and query providers are all ordinary functions, closures and Expression<Value> parameters.

## language/functions

- `language/functions/arguments.md` - **Arguments and labels** (reference): An argument is passed positionally or by label, positional arguments always come first, and a label matches a parameter by name rather than by position.
- `language/functions/closures.md` - **Closures** (reference): A brace in expression position is always a closure with inferred or written parameters; it captures a const binding as a copy and a var binding as itself, which only a closure handed to a parameter that just calls it may do.
- `language/functions/declaring-a-function.md` - **Declaring a function** (reference): fn declares a function with a mandatory parameter type on every parameter; the last expression of the body is the result, and a public function or a trait method must always spell out its return type.
- `language/functions/default-values.md` - **Default values** (reference): A parameter default is an expression that runs at every call which omits the argument, in the scope of the declaration, without self and without the other parameters.
- `language/functions/index.md` - **Functions** (index): Declaring a function, its arguments and defaults, variadic parameters, closures, trailing closures, parameter modes and quoted expressions.
- `language/functions/parameter-modes.md` - **Parameter modes** (reference): A parameter is an ordinary value unless it says otherwise; var hands over a path to mutate, lazy defers evaluation once, a self-named closure resolves names against a receiver, and Expression also hands over the typed tree.
- `language/functions/quoted-expressions.md` - **Quoted expressions** (reference): A parameter or binding typed Expression<Value> gets the ordinary value plus the typed tree of what was written, its source text and the values it captured, which is what assert and a query provider read instead of running the code twice.
- `language/functions/trailing-closures.md` - **Trailing closures** (reference): When the last parameter of a call is a function, the closure argument can follow the call as a brace instead of sitting inside the parentheses.
- `language/functions/variadics.md` - **Variadic parameters** (reference): A parameter written ...name collects every remaining positional argument into a List, and a collection is only unpacked into it when the call spreads it with the same three dots.

## language/generics

- `language/generics/bounds.md` - **Bounds** (reference): A bound restricts a type parameter to types that implement one or more traits, written inline or after where, and a member can carry a bound of its own that is not a requirement on every implementor.
- `language/generics/index.md` - **Generics** (index): Type parameters, where they are declared, how a bound restricts them, what is inferred, and how a trait-typed value satisfies one at runtime.
- `language/generics/inference.md` - **Inference** (reference): A type argument is inferred from a call's arguments or its expected type, a closure's parameter types follow the same rule, and a fn's own parameter types and a public fn's result are always written out.
- `language/generics/no-higher-kinded-types.md` - **No higher-kinded types** (reference): Option, Result, Iterate and Task share method names with the same meaning as a convention of the standard library, not as a shared trait, because the language has no way to be generic over a type constructor.
- `language/generics/type-parameters.md` - **Type parameters** (reference): A type parameter is declared in angle brackets after the name of a fn, type, trait or extend, and its name is written out like a type, never a single letter.
- `language/generics/witnesses.md` - **Witness tables** (reference): A trait-typed value carries a witness table per trait it is known through, so a generic bound is satisfied by any trait the value's own traits require, even without knowing its concrete type.

## language/modules-and-packages

- `language/modules-and-packages/cyclic-imports.md` - **Cyclic imports** (reference): Two modules may import each other, because nothing runs when a module is imported and its exports are computed to a fixpoint, but the same cycle between top-level statements is an error.
- `language/modules-and-packages/index.md` - **Modules and packages** (index): How a file brings in names from elsewhere, what a package is, and the two rules - visibility and top-level code - that decide what a module may contain.
- `language/modules-and-packages/packages.md` - **Packages** (reference): A package is a directory with a project.trb and a src/, named owner/name, and it can only be reached by a project that lists it as a dependency.
- `language/modules-and-packages/the-prelude.md` - **The prelude** (reference): The prelude is the package whose public names are in scope in every file without an import, and it holds only the pure part of the standard library.
- `language/modules-and-packages/top-level-code.md` - **Top-level code** (reference): A statement outside every declaration is only allowed in an entry file, a script or a test file, and a top-level const of a module has to be known at compile time.
- `language/modules-and-packages/use.md` - **use** (reference): use brings names into scope from a package or a file. Everything after from names a module; a path brings in a case of a type or a member another package attaches to it, and a use without names is an error.
- `language/modules-and-packages/visibility.md` - **Visibility** (reference): A top-level declaration is private to its file unless marked public, and a public declaration may not expose a type that is private to its own file.
- `language/modules-and-packages/workspaces.md` - **Workspaces** (reference): A workspace is one root project.trb naming several member projects that check, build and test as a group, and resolve their dependencies into one shared project.lock.trb.

## language/pattern-matching

- `language/pattern-matching/cases-and-match.md` - **Cases and match** (reference): A case is a variant of a type, written Type.Case or .Case and bare only when it is imported. A match is an expression and must cover every case.
- `language/pattern-matching/exhaustiveness.md` - **Exhaustiveness** (reference): A match has to cover every value of its subject, and an arm that no value can reach is a compile error, not a defensive line.
- `language/pattern-matching/if-var.md` - **if var** (reference): if var P = place binds a pattern into the place itself, exactly like a var parameter, instead of copying the value out first.
- `language/pattern-matching/importing-cases.md` - **Importing cases** (reference): A case is imported through the type it belongs to, and only a case can be; once imported it needs nothing in front of it, in an expression and in a pattern.
- `language/pattern-matching/index.md` - **Cases and pattern matching** (index): How a type with cases is declared, and every place a pattern can stand.
- `language/pattern-matching/pattern-forms.md` - **Pattern forms** (reference): Every pattern the language has, from a literal to a list pattern with a rest, one form per line.
- `language/pattern-matching/patterns-in-bindings.md` - **Patterns in bindings and conditions** (reference): A pattern also stands after const and var, in the head of if and while, and in a for loop - the same vocabulary as a match arm, without the braces.

## language/reflection

- `language/reflection/encode-and-decode.md` - **Encode and Decode** (reference): A value is its constructor call, offered in three forms - Encode writes it, Decode reads it back, Describe describes it without a value - so a type works with every format without hand-written serialization code.
- `language/reflection/encoders.md` - **Encoder and Decoder** (reference): Encoder and Decoder each name every scalar the language has - bool, int, unsigned, float, decimal, string, bytes - plus the four shapes a value can take, sequence, map, record and variant, which open and are closed by finish.
- `language/reflection/index.md` - **Reflection** (index): Why there is no runtime reflection, the four syntactic bridges that connect a type to a value instead, and the generated Encode and Decode pair that covers serialization.
- `language/reflection/no-reflection.md` - **There is no reflection** (reference): A type never flows as a value, so there is no Type type, no typeof and no Class.forName - only four syntactic bridges connect a type to a value, all resolved at compile time.

## language/syntax

- `language/syntax/cheat-sheet.md` - **Syntax cheat sheet** (reference): Every form of the language in one place: declarations, expressions, patterns, types and the call rules, with the exact spelling of each.
- `language/syntax/command-calls.md` - **Command calls** (reference): A call is written without parentheses wherever the grammar allows it, and with parentheses everywhere else. This is the formatter canon and it is enforced, not preferred.
- `language/syntax/doc-comments.md` - **Doc comments** (reference): A `/** */` comment attaches to the declaration written directly after it, and everything that can be declared - including a parameter, a field or a case - can have one.
- `language/syntax/generics-or-comparison.md` - **Angle brackets or comparison** (reference): A `<` after a name starts a type argument list only if what follows parses as types up to a matching `>` that is itself followed by a token a comparison could not have.
- `language/syntax/index.md` - **Syntax** (index): How TorbScript is written: where a statement ends, how a call is spelled, and what a literal looks like.
- `language/syntax/lexical-structure.md` - **Lexical structure** (reference): A statement ends at the end of its line, a name is ASCII while text is not, a block comment ends at its first `*/`, and only three keywords are never reserved.
- `language/syntax/literals.md` - **Literals** (reference): An integer, a decimal, a character and a string each have exactly one literal form, and a literal adapts to the type it is expected to have.
- `language/syntax/multi-line-strings.md` - **Multi-line strings** (reference): A `\"\"\"` string is dedented by the indentation of its first line with content, so a block of text reads at the indentation of the code around it instead of jammed against the left margin.
- `language/syntax/naming.md` - **Naming** (reference): A name is written in ASCII letters, digits and `_`, a type starts with an uppercase letter and everything else with a lowercase one, and the compiler reports both at the declaration.
- `language/syntax/string-interpolation.md` - **String interpolation** (reference): `{expression}` inside a string runs the expression and shows it, a literal brace is written `\{` or `\}`, and the expression inside the braces has to fit on one line.

## language/traits

- `language/traits/coherence.md` - **Coherence and blanket implementations** (reference): A package may implement a trait for a type only if it owns the type, the trait, or a type named as an argument of the trait, and two implementations of one trait may never overlap.
- `language/traits/delegation.md` - **Delegation with by** (reference): by forwards a trait's required members to the one field of a single-field type, binding only to the trait or parenthesised group written directly in front of it.
- `language/traits/extend.md` - **extend** (reference): extend adds constants and functions to a type after its declaration, with a trait or without one, never adds a field or a case, and is named by the file that uses it when it targets a type of another package.
- `language/traits/index.md` - **Traits** (index): How a capability is declared, how a type comes with one, and how a trait is used as a type.
- `language/traits/intersections.md` - **Trait intersections** (reference): The & operator combines two or more traits into one type, in a parameter, a field or a bound, and only traits can be combined this way.
- `language/traits/object-safety.md` - **Object safety** (reference): A member that mentions Self in a parameter or its result, or that has no self, cannot be called on a trait-typed value, even though the trait stays a legal type.
- `language/traits/operators.md` - **Operators are traits** (reference): An operator is a trait exactly when it is a method call, so writing one on your own type means implementing the trait it stands for - and the three that are no method call are the three that are not traits.
- `language/traits/supertraits.md` - **Supertraits** (reference): A trait declared with a supertrait requires every implementing type to also implement that supertrait, and a default member can call the supertrait's members directly.
- `language/traits/trait-types.md` - **Traits as types** (reference): A trait can stand wherever a type can, a value coerces to it automatically, and that coercion is the only subtyping the language has, with no variance for the types built from it.
- `language/traits/traits.md` - **Traits** (reference): A trait is a capability a type comes with. A single-method trait is named after its method, there is no inheritance, and operators are traits.

## language/types

- `language/types/construction.md` - **Construction** (reference): Every type has exactly one constructor, generated from its fields in declaration order, and it never contains logic - validation and parsing are static factory functions instead.
- `language/types/conversions.md` - **Conversions** (reference): From provides Into for free and TryFrom provides TryInto, text is a source like any other, and the language has exactly four coercions that apply only where a type is expected.
- `language/types/copy-and-equality.md` - **Copy and equality** (reference): Assigning, passing or capturing a value copies it, and Equals, Hash and copy are generated for a type without being written, each only if every field supports it.
- `language/types/data-or-capsule.md` - **Data or capsule** (reference): A type is data, whose constructor is the way in, or a capsule, whose constructor a private field without a default closes - and then a factory, accessors and one conversion pair take its place.
- `language/types/declaring-a-type.md` - **Declaring a type** (reference): One keyword declares every data type. Fields are const unless marked var, members are public unless marked private, and Equals, Hash, Show and copy are generated.
- `language/types/exclusivity.md` - **Exclusivity** (reference): Two var accesses of the same call may never target the same path, so swap(a, a) and two indices the checker cannot tell apart are both compile errors, and items.swapAt is the one access that is allowed instead.
- `language/types/fields.md` - **Fields** (reference): A field is const unless marked var, and private or private(var) decide who may read it and who may write it, independently of each other.
- `language/types/generated-show.md` - **The generated Show** (reference): Show is generated for every type without being written, and its text is fixed so that two implementations of the language print the same thing for the same value.
- `language/types/index.md` - **Types** (index): Declaring a type, its fields, its methods, what is generated for it, and how one is changed.
- `language/types/methods.md` - **Methods and `static fn`s** (reference): A member says what it is with two words - static belongs to the type, var may change - and a type has one namespace of members, so a field and a method can never share a name.
- `language/types/property-commands.md` - **Property commands** (reference): A field is written only with `=`, everywhere; the one command call left is a trailing block on a field whose value is a record, which configures that value in place rather than replacing it - no hand-written setters.
- `language/types/shared-types.md` - **Shared types** (reference): A shared type has an identity instead of a value, so assigning it never copies, a change of the object needs no var path, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
- `language/types/var-paths.md` - **Mutation and var paths** (reference): A change needs an unbroken var path from the binding down to the field being changed, and a var parameter or a var fn receiver is a reference that cannot outlive the call it belongs to.
- `language/types/verbs-and-participles.md` - **Verbs and participles** (reference): A verb changes its receiver in place and is a var fn, and its participle answers a changed copy instead, so calling the verb through a const path names the participle in its error.

## language/values-and-types

- `language/values-and-types/arrays.md` - **Arrays and const parameters** (reference): Array<Item, const Size> carries its length in the type, a const parameter is a value rather than a type, and there is no arithmetic over one.
- `language/values-and-types/bindings.md` - **Bindings** (reference): A const binding never changes and nothing below it changes; a var binding can be changed in place. That one rule replaces every mutable-and-immutable type pair.
- `language/values-and-types/built-in-types.md` - **Built-in types** (reference): Every type a file has without an import - the sized numbers, Bool, Char, String, tuples, lists, maps, ranges, Option, function types, Void and Never.
- `language/values-and-types/checked-literals.md` - **Checked literals** (reference): A string literal where a Path, a Uri, a UriTemplate, a Regex or a resource type is expected is read by the compiler where it is written, and one that is not valid is a compile error at that line; a template and a pattern are read verbatim.
- `language/values-and-types/decimal.md` - **Decimal** (reference, planned): Decimal is designed for exact base-ten arithmetic such as money, and a decimal literal adapts to it the way it adapts to Float - but no back end implements it yet.
- `language/values-and-types/distinct-types.md` - **Distinct types** (reference): A distinct type is an ordinary single-field type, and `by` forwards specific traits to that field so the wrapper costs no boilerplate - there is no separate opaque-alias feature.
- `language/values-and-types/floating-point.md` - **Floating-point numbers** (reference): On a Float every operator is IEEE-754 and `compare` is a total order that disagrees with them on `nan` and `-0.0`, and neither Float type is Hash.
- `language/values-and-types/index.md` - **Values and types** (index): Bindings, the built-in types, and the type forms that are about values rather than about behaviour.
- `language/values-and-types/integers.md` - **Integers** (reference): Eight sized integer types with fixed ranges on every platform, an unannotated literal is always Int64, and overflow is a compile error when it is written and a panic when it happens at runtime.
- `language/values-and-types/literal-types.md` - **Literal types** (reference): `"tcp" | "udp"` is a type made only of specific values of one base type; only literals combine with `|`, because there are no unions of types.
- `language/values-and-types/option.md` - **Option** (reference): Absence is a value, Some(value) or None, and there is no null and no implicit Some - a value has to be wrapped and unwrapped on purpose.
- `language/values-and-types/ranges.md` - **Ranges** (reference): The ends a range has are its type - Range, RangeFrom or RangeTo - so nothing is optional and nothing panics; what accepts every form takes the Bounds trait.
- `language/values-and-types/strings.md` - **Strings** (reference): A String has no length() and no text[i], because "length" and "the i-th character" each have three different answers and two of them are slow.
- `language/values-and-types/tuples.md` - **Tuples** (reference): A tuple is positional and accessed by .0, .1; a label makes a position easier to read but is not part of the type, so a labelled and an unlabelled tuple of the same shape are the same type.
- `language/values-and-types/type-aliases.md` - **Type aliases** (reference): `type Name = Other` names an existing type rather than declaring a new one, and the two names are freely interchangeable - there is no separate `alias` keyword.
- `language/values-and-types/void-and-never.md` - **Void and Never** (reference): Void has exactly one value, the keyword literal void, the way true and false are the values of Bool; Never has no value at all and converts to every type, which is why panic fits into any expression.

## standard-library

- `standard-library/archive.md` - **std/archive** (package): POSIX ustar archives - tarred writes entries as bytes that depend on nothing but the entries, untarred reads the regular files back and refuses links.
- `standard-library/collections.md` - **std/collections** (package): The collection traits every signature talks about, and the implementations that only show up where one is built.
- `standard-library/compression.md` - **std/compression** (package): DEFLATE and gzip in TorbScript - inflated and gunzipped read every stream the formats allow, deflated and gzipped write deterministic output, and crc32 is the checksum gzip uses.
- `standard-library/console.md` - **std/console** (package): print and printError, the two functions that write to the standard streams.
- `standard-library/core.md` - **std/core** (package): The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions.
- `standard-library/digest.md` - **std/digest** (package): Sha256 and the Digest it answers - SHA-256 of FIPS 180-4 in TorbScript, fed at once or in pieces, shown as lowercase hexadecimal.
- `standard-library/dns.md` - **std/dns** (package): Domain names with IDNA and the comparison of RFC 4343, DNS records as typed cases, and the RFC 1035 wire format with compression and EDNS0 - values with no natives and no capability, for the transports of std/network and std/http.
- `standard-library/encoding.md` - **std/encoding** (package): Encode, Decode and Describe, the Encoder, Decoder and Describer a format implements, EncodedValue and Structure for a value or a structure without its type, and Format for the streaming side.
- `standard-library/expression.md` - **std/expression** (package): Expression and ExpressionNode, the typed tree a quoted parameter hands over, plus assert and nameOf.
- `standard-library/fs.md` - **std/fs** (package): File and IoError - whole-file helpers for what fits in memory, and a File as both ends of a byte stream.
- `standard-library/function-types.md` - **Predicate, Action and Transform** (reference): Three aliases in the prelude for the closure shapes signatures take most - a question about one value, an effect on one value, and a conversion of one value into another.
- `standard-library/geometry.md` - **std/geometry** (package): The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
- `standard-library/http.md` - **std/http** (package): HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
- `standard-library/index.md` - **The standard library** (index): One page per package of std, what each contains, and which of them are in scope everywhere without an import.
- `standard-library/io.md` - **std/io** (package): Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
- `standard-library/ip.md` - **std/ip** (package): IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
- `standard-library/iteration.md` - **std/iteration** (package): Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
- `standard-library/json.md` - **std/json** (package): Json, a value with the options of the format, for encoding and decoding any Encode/Decode type, and JsonValue for the rare document whose shape is not known ahead of time.
- `standard-library/linear.md` - **std/linear** (package): Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
- `standard-library/markdown.md` - **std/markdown** (package): Markdown reads CommonMark with GitHub's tables and front matter into a document tree that is a value, with the lines of every block and link; HTML and Markdown are written from the tree.
- `standard-library/network.md` - **std/network** (package): Name resolution and DNS lookups, TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - and UDP datagrams, over the address values of std/ip, which it re-exports.
- `standard-library/number.md` - **std/number** (package): Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
- `standard-library/os.md` - **std/os** (package, draft): Environment, System and Directories - the environment a program was started with, which system and version it runs on, and where the user's files belong - with OsError and the three target constants.
- `standard-library/parallel.md` - **std/parallel** (package): parallel() on anything that can be iterated, Parallel, a pipeline whose fused stages run on the workers of the pool with the results in input order, and Cut, the collections that cut themselves.
- `standard-library/path.md` - **std/path** (package): Path, a root and a list of components, never a string, plus Root and PathError - the type behind Path.resolved.
- `standard-library/prelude.md` - **std/prelude** (package): The package of re-exports that is in scope in every file of a project, unless project.trb names another one.
- `standard-library/process.md` - **std/process** (package): Process for arguments, exiting and running a program to its end as a task, Child for a running program's pipes, and ProcessOutput for what it left behind.
- `standard-library/project.md` - **std/project** (package): The receiver type of project.trb - Project, Dependencies, Build, Test, Tasks and Workspace.
- `standard-library/regex.md` - **std/regex** (package): Regex, a compiled pattern with the syntax and the linear-time semantics of RE2, with whole and partial matches, named groups that decode into a type, replace and split.
- `standard-library/resource.md` - **std/resource** (package): Resource, EmbeddedBytes and EmbeddedText - a file of the package named by a string literal, resolved by the compiler where it is written; reading the bytes comes with the next slices.
- `standard-library/sandbox.md` - **std/sandbox** (package): Sandbox and Script, which load a .trb file as a type-checked, capability-limited receiver closure.
- `standard-library/stream.md` - **std/stream** (package): Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.
- `standard-library/task.md` - **std/task** (package): Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
- `standard-library/test.md` - **std/test** (package): test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
- `standard-library/text.md` - **std/text** (package): Char, a Unicode scalar value, and String, always-valid UTF-8 text with no length() and no indexing by character.
- `standard-library/time.md` - **std/time** (package): Instant and Duration, the two time values, plus Clock and sleep, which read and wait on the wall clock.
- `standard-library/tls.md` - **std/tls** (package): TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, a stream like the TCP one, and DNS over TLS.
- `standard-library/uri.md` - **std/uri** (package): Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
- `standard-library/yaml.md` - **std/yaml** (package): Yaml reads and writes any Encode/Decode type as YAML 1.2 or 1.1, with the target type resolving every scalar, and YamlNode is the tree of a document with its anchors, tags, styles and comments.

## tooling

- `tooling/index.md` - **The toolchain** (index): The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.
- `tooling/project-lock-trb.md` - **project.lock.trb** (tooling): The locked manifest - the exact version, tree hash and registry of every package a workspace depends on, and the evaluated settings of a published package - written deterministically and read without running anything.
- `tooling/project-trb.md` - **project.trb** (tooling): The manifest of a project - name, dependencies, the workspace it belongs to, and what torb build and torb test read out of it today.
- `tooling/the-torb-command.md` - **The torb command** (tooling): Every subcommand of the toolchain, what it does today, and which of them are still planned.
- `tooling/torb-add.md` - **torb add** (tooling): torb add writes a dependency into project.trb, resolves the workspace with it, writes project.lock.trb and installs what is new - or changes nothing and explains why there is no solution.
- `tooling/torb-build.md` - **torb build** (tooling): torb build type checks a program, lowers it to C, and hands the C to whatever compiler it finds - one file in, one native binary out, nothing to configure.
- `tooling/torb-canon.md` - **torb canon** (tooling): torb canon is deprecated - it runs torb format now, with a warning - and the formatter canon it enforced, every rule of it, is what torb format runs first, over the syntax tree and with a safety net.
- `tooling/torb-check.md` - **torb check** (tooling): torb check resolves every module, import and name in a type position, types every expression, and reports one block per diagnostic - the gate every other command trusts.
- `tooling/torb-doc.md` - **torb doc** (tooling): torb doc turns the public API of a package and its doc comments into a reference - a static site, or one JSON document for an editor and the registry - and runs the examples of the doc comments as doc tests.
- `tooling/torb-docs-source.md` - **torb docs source** (tooling): torb docs source checks the doc comments of the code itself - a module comment on every file, a comment on every construct that needs one, six headings, links that resolve, and examples that compile.
- `tooling/torb-format.md` - **torb format** (tooling): torb format writes TorbScript sources in the one layout of the language - the rules of the formatter canon, then indentation, spaces and blank lines - and --check fails on every file that is not in it.
- `tooling/torb-install.md` - **torb install** (tooling): torb install fetches every package project.lock.trb pins that is not in the cache yet, checks its tree hash before a file is written, and never changes the lock.
- `tooling/torb-lint.md` - **torb lint** (tooling): torb lint reports the rules of style the type checker leaves alone - Self, a Bool field named as a question, an unread binding, an unlabeled literal - each with its id and, where it is certain, a fix.
- `tooling/torb-new.md` - **torb new** (tooling): torb new scaffolds a package - project.trb, a src/main.trb that prints a greeting, and a tests/main.test.trb with one passing test - refusing where the name already exists.
- `tooling/torb-publish.md` - **torb publish** (tooling): torb publish builds and checks a package's archive as a registry receives it, prints its tree hash and capabilities, and writes it into a directory registry or uploads it with a token or through trusted publishing.
- `tooling/torb-remove.md` - **torb remove** (tooling): torb remove takes a dependency out of project.trb, resolves the workspace again, and drops from project.lock.trb every package nothing needs any more.
- `tooling/torb-repl.md` - **torb repl** (tooling): torb repl reads entries from standard input, checks each against the session, runs it in the bytecode VM and keeps what it binds and declares for the next entry - a typed session and a piped file behave the same.
- `tooling/torb-run.md` - **torb run** (tooling): torb run runs a file or a project in the bytecode VM, or builds and runs it natively with --native, passing the rest of the command line, the three streams and the exit code through.
- `tooling/torb-test.md` - **torb test** (tooling): torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees.
- `tooling/torb-update.md` - **torb update** (tooling): torb update resolves every package of the workspace, or only the named ones, to the highest version project.trb allows, writes project.lock.trb, and refuses an update that gains a capability unless --accept-capabilities says it is wanted.
- `tooling/verifying-your-work.md` - **Verify your work** (tooling): The commands that decide whether TorbScript you wrote is correct and in the layout of the formatter, in the order to run them.
