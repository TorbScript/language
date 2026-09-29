# The pages of torbscript-language

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- the root
- language
- language/syntax
- language/values-and-types
- language/types
- language/functions
- language/traits
- language/pattern-matching
- language/generics
- language/errors
- language/collections-and-iteration
- language/modules-and-packages
- language/reflection
- language/configuration
- language/execution
- language/extensibility
- how-to
- explanation

## the root

- `glossary.md` - **Glossary** (glossary): Every term this documentation uses, one entry each, at most two sentences. The entry decides which word is correct.

## language

- `language/index.md` - **The language reference** (index): One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites.

## language/syntax

- `language/syntax/index.md` - **Syntax** (index): How TorbScript is written: where a statement ends, how a call is spelled, and what a literal looks like.
- `language/syntax/lexical-structure.md` - **Lexical structure** (reference): A statement ends at the end of its line, a name is ASCII while text is not, a block comment ends at its first `*/`, three keywords are never reserved, and a member may be named after a reserved one.
- `language/syntax/literals.md` - **Literals** (reference): An integer, a decimal, a character and a string each have exactly one literal form, and a literal adapts to the type it is expected to have.
- `language/syntax/string-interpolation.md` - **String interpolation** (reference): `{expression}` inside a string runs the expression and shows it, a literal brace is written `\{` or `\}`, and the expression inside the braces has to fit on one line.
- `language/syntax/multi-line-strings.md` - **Multi-line strings** (reference): A `\"\"\"` string is dedented by the indentation of its first line with content, so a block of text reads at the indentation of the code around it instead of jammed against the left margin.
- `language/syntax/naming.md` - **Naming** (reference): A name is written in ASCII letters, digits and `_`, a type starts with an uppercase letter and everything else with a lowercase one, and the compiler reports both at the declaration.
- `language/syntax/generics-or-comparison.md` - **Angle brackets or comparison** (reference): A `<` after a name starts a type argument list only if what follows parses as types up to a matching `>` that is itself followed by a token a comparison could not have.
- `language/syntax/doc-comments.md` - **Doc comments** (reference): A `/** */` comment attaches to the declaration written directly after it, and everything that can be declared - including a parameter, a field or a case - can have one.
- `language/syntax/command-calls.md` - **Command calls** (reference): A call is written without parentheses wherever the grammar allows it, and with parentheses everywhere else. This is the formatter canon and it is enforced, not preferred.

## language/values-and-types

- `language/values-and-types/index.md` - **Values and types** (index): Bindings, the built-in types, and the type forms that are about values rather than about behaviour.
- `language/values-and-types/built-in-types.md` - **Built-in types** (reference): Every type a file has without an import - the sized numbers, Bool, Char, String, tuples, lists, maps, ranges, Option, function types, Void and Never.
- `language/values-and-types/bindings.md` - **Bindings** (reference): A const binding never changes and nothing below it changes; a var binding can be changed in place. That one rule replaces every mutable-and-immutable type pair.
- `language/values-and-types/integers.md` - **Integers** (reference): Eight sized integer types with fixed ranges on every platform, an unannotated literal is always Int64, and overflow is a compile error when it is written and a panic when it happens at runtime.
- `language/values-and-types/floating-point.md` - **Floating-point numbers** (reference): On a Float every operator is IEEE-754 and `compare` is a total order that disagrees with them on `nan` and `-0.0`, and neither Float type is Hash.
- `language/values-and-types/decimal.md` - **Decimal** (reference, planned): Decimal is designed for exact base-ten arithmetic such as money, and a decimal literal adapts to it the way it adapts to Float - but no back end implements it yet.
- `language/values-and-types/strings.md` - **Strings** (reference): A String has no length() and no text[i], because "length" and "the i-th character" each have three different answers and two of them are slow.
- `language/values-and-types/tuples.md` - **Tuples** (reference): A tuple is positional and accessed by .0, .1; a label makes a position easier to read but is not part of the type, so a labelled and an unlabelled tuple of the same shape are the same type.
- `language/values-and-types/ranges.md` - **Ranges** (reference): The ends a range has are its type - Range, RangeFrom or RangeTo - so nothing is optional and nothing panics; what accepts every form takes the Bounds trait.
- `language/values-and-types/option.md` - **Option** (reference): Absence is a value, Some(value) or None, and there is no null: a value wraps itself into Some where an Option is expected, and an Option is only ever unwrapped on purpose.
- `language/values-and-types/void-and-never.md` - **Void and Never** (reference): Void has exactly one value, the keyword literal void, the way true and false are the values of Bool; Never has no value at all and converts to every type, which is why panic fits into any expression.
- `language/values-and-types/literal-types.md` - **Literal types** (reference): `"tcp" | "udp"` is a type made only of specific values of one base type; only literals combine with `|`, because there are no unions of types.
- `language/values-and-types/checked-literals.md` - **Checked literals** (reference): A string literal where a Path, a Uri, a UriTemplate, a Regex or a resource type is expected is read by the compiler where it is written, and one that is not valid is a compile error at that line; a template and a pattern are read verbatim.
- `language/values-and-types/type-aliases.md` - **Type aliases** (reference): `type Name = Other` names an existing type rather than declaring a new one, and the two names are freely interchangeable - there is no separate `alias` keyword.
- `language/values-and-types/distinct-types.md` - **Distinct types** (reference): A distinct type is an ordinary single-field type, and `by` forwards specific traits to that field so the wrapper costs no boilerplate - there is no separate opaque-alias feature.
- `language/values-and-types/arrays.md` - **Arrays and const parameters** (reference): Array<Item, const Size> carries its length in the type, a const parameter is a value rather than a type, and there is no arithmetic over one.

## language/types

- `language/types/index.md` - **Types** (index): Declaring a type, its fields, its methods, what is generated for it, and how one is changed.
- `language/types/declaring-a-type.md` - **Declaring a type** (reference): One keyword declares every data type. Fields are const unless marked var, members are public unless marked private, and Equals, Hash, Show and copy are generated.
- `language/types/case-values.md` - **Cases that stand for numbers** (reference): A type whose cases have no fields may give each of them a fixed number, case Read = 1, and gets rawValue() and fromRawValue generated - the way to a C enum, a protocol code, a column and a Flags mask.
- `language/types/fields.md` - **Fields** (reference): A field is const unless marked var, and private or protected var decide who may read it and who may write it, independently of each other.
- `language/types/construction.md` - **Construction** (reference): Every type has exactly one constructor, generated from its fields in declaration order, and it never contains logic - validation and parsing are static factory functions instead.
- `language/types/data-or-capsule.md` - **Data or capsule** (reference): A type is data, whose constructor is the way in, or a capsule, whose constructor a private field without a default closes - and then a factory, accessors and one conversion pair take its place.
- `language/types/copy-and-equality.md` - **Copy and equality** (reference): Assigning, passing or capturing a value copies it, and Equals, Hash and copy are generated for a type without being written, each only if every field supports it.
- `language/types/generated-show.md` - **The generated Show** (reference): Show is generated for every type without being written, and its text is fixed so that two implementations of the language print the same thing for the same value.
- `language/types/methods.md` - **Methods and `static fn`s** (reference): A member says what it is with two words - static belongs to the type, var may change - and a type has one namespace of members, so a field and a method can never share a name.
- `language/types/verbs-and-participles.md` - **Verbs and participles** (reference): A verb changes its receiver in place and is a var fn, and its participle answers a changed copy instead, so calling the verb through a const path names the participle in its error.
- `language/types/var-paths.md` - **Mutation and var paths** (reference): A change needs an unbroken var path from the binding down to the field being changed, and a var parameter or a var fn receiver is a reference that cannot outlive the call it belongs to.
- `language/types/exclusivity.md` - **Exclusivity** (reference): Two var accesses of the same call may never target the same path, so swap(a, a) and two indices the checker cannot tell apart are both compile errors, and items.swapAt is the one access that is allowed instead.
- `language/types/shared-types.md` - **Shared types** (reference): A shared type has an identity instead of a value, so assigning it never copies, a change of the object needs no var path, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
- `language/types/conversions.md` - **Conversions** (reference): From provides Into for free and TryFrom provides TryInto, text is a source like any other, and the language has exactly five coercions that apply only where a type is expected.
- `language/types/property-commands.md` - **Property commands** (reference): A field is written only with `=`, everywhere; the one command call left is a trailing block on a field whose value is a record, which configures that value in place rather than replacing it - no hand-written setters.

## language/functions

- `language/functions/index.md` - **Functions** (index): Declaring a function, its arguments and defaults, variadic parameters, closures, trailing closures, parameter modes and quoted expressions.
- `language/functions/declaring-a-function.md` - **Declaring a function** (reference): fn declares a function with a mandatory parameter type on every parameter; the last expression of the body is the result, and a public function or a trait method must always spell out its return type.
- `language/functions/arguments.md` - **Arguments and labels** (reference): An argument is passed positionally or by label, positional arguments always come first, and a label matches a parameter by name rather than by position.
- `language/functions/default-values.md` - **Default values** (reference): A parameter default is an expression that runs at every call which omits the argument, in the scope of the declaration, without self and without the other parameters.
- `language/functions/variadics.md` - **Variadic parameters** (reference): A parameter written ...name collects every remaining positional argument into a List, and a collection is only unpacked into it when the call spreads it with the same three dots.
- `language/functions/closures.md` - **Closures** (reference): A brace in expression position is always a closure with inferred or written parameters; it captures a const binding as a copy and a var binding as itself, which only a closure handed to a parameter that just calls it may do.
- `language/functions/trailing-closures.md` - **Trailing closures** (reference): When the last parameter of a call is a function, the closure argument can follow the call as a brace instead of sitting inside the parentheses.
- `language/functions/parameter-modes.md` - **Parameter modes** (reference): A parameter is an ordinary value unless it says otherwise; var hands over a path to mutate, lazy defers evaluation once, a self-named closure resolves names against a receiver, and Expression also hands over the typed tree.
- `language/functions/quoted-expressions.md` - **Quoted expressions** (reference): A parameter or binding typed Expression<Value> gets the ordinary value plus the typed tree of what was written, its source text and the values it captured, which is what assert and a query provider read instead of running the code twice.

## language/traits

- `language/traits/index.md` - **Traits** (index): How a capability is declared, how a type comes with one, and how a trait is used as a type.
- `language/traits/traits.md` - **Traits** (reference): A trait is a capability a type comes with. A single-method trait is named after its method, there is no inheritance, and operators are traits.
- `language/traits/extend.md` - **extend** (reference): extend adds constants and functions to a type after its declaration, with a trait or without one, never adds a field or a case, and is named by the file that uses it when it targets a type of another package.
- `language/traits/supertraits.md` - **Supertraits** (reference): A trait declared with a supertrait requires every implementing type to also implement that supertrait, and a default member can call the supertrait's members directly.
- `language/traits/trait-types.md` - **Traits as types** (reference): A trait can stand wherever a type can, a value coerces to it automatically, and that coercion is the only subtyping the language has, with no variance for the types built from it.
- `language/traits/intersections.md` - **Trait intersections** (reference): The & operator combines two or more traits into one type, in a parameter, a field or a bound, and only traits can be combined this way.
- `language/traits/delegation.md` - **Delegation with by** (reference): by forwards a trait's required members to the one field of a single-field type, binding only to the trait or parenthesised group written directly in front of it.
- `language/traits/coherence.md` - **Coherence and blanket implementations** (reference): A package may implement a trait for a type only if it owns the type, the trait, or a type named as an argument of the trait, and two implementations of one trait may never overlap.
- `language/traits/operators.md` - **Operators are traits** (reference): An operator is a trait exactly when it is a method call, so writing one on your own type means implementing the trait it stands for - and the three that are no method call are the three that are not traits.
- `language/traits/object-safety.md` - **Object safety** (reference): A member that mentions Self in a parameter or its result, or that has no self, cannot be called on a trait-typed value, even though the trait stays a legal type.

## language/pattern-matching

- `language/pattern-matching/index.md` - **Cases and pattern matching** (index): How a type with cases is declared, and every place a pattern can stand.
- `language/pattern-matching/cases-and-match.md` - **Cases and match** (reference): A case is a variant of a type, written Type.Case or .Case and bare only when it is imported. A match is an expression and must cover every case.
- `language/pattern-matching/pattern-forms.md` - **Pattern forms** (reference): Every pattern the language has, from a literal to a list pattern with a rest, one form per line.
- `language/pattern-matching/exhaustiveness.md` - **Exhaustiveness** (reference): A match has to cover every value of its subject, and an arm that no value can reach is a compile error, not a defensive line.
- `language/pattern-matching/importing-cases.md` - **Importing cases** (reference): A case is imported through the type it belongs to, and only a case can be; once imported it needs nothing in front of it, in an expression and in a pattern.
- `language/pattern-matching/patterns-in-bindings.md` - **Patterns in bindings and conditions** (reference): A pattern also stands after const and var, in the head of if and while, and in a for loop - the same vocabulary as a match arm, without the braces.
- `language/pattern-matching/if-var.md` - **if var** (reference): if var P = place binds a pattern into the place itself, exactly like a var parameter, instead of copying the value out first.

## language/generics

- `language/generics/index.md` - **Generics** (index): Type parameters, where they are declared, how a bound restricts them, what is inferred, and how a trait-typed value satisfies one at runtime.
- `language/generics/type-parameters.md` - **Type parameters** (reference): A type parameter is declared in angle brackets after the name of a fn, type, trait or extend, and its name is written out like a type, never a single letter.
- `language/generics/bounds.md` - **Bounds** (reference): A bound restricts a type parameter to types that implement one or more traits, written inline or after where, and a member can carry a bound of its own that is not a requirement on every implementor.
- `language/generics/inference.md` - **Inference** (reference): A type argument is inferred from a call's arguments or its expected type, a closure's parameter types follow the same rule, and a fn's own parameter types and a public fn's result are always written out.
- `language/generics/no-higher-kinded-types.md` - **No higher-kinded types** (reference): Option, Result, Iterate and Task share method names with the same meaning as a convention of the standard library, not as a shared trait, because the language has no way to be generic over a type constructor.
- `language/generics/witnesses.md` - **Witness tables** (reference): A trait-typed value carries a witness table per trait it is known through, so a generic bound is satisfied by any trait the value's own traits require, even without knowing its concrete type.

## language/errors

- `language/errors/index.md` - **Errors** (index): How a function says it can fail, how a caller handles it, and what a panic is for.
- `language/errors/result.md` - **Result** (reference): A function that can fail answers Result<Value, Failure>, whose cases are Ok and Fail. The postfix question mark unwraps an Ok or returns the Fail from the surrounding function.
- `language/errors/option-chaining.md` - **Optional chaining** (reference): `?.` is Option.map, or Option.flatMap when the member itself answers an Option, so chaining never nests. `??` is the trait OrElse, which gives a lazy fallback for an absent Option, a failed Result or any type that comes with it.
- `language/errors/question-mark.md` - **The question mark operator** (reference): A postfix ? unwraps an Ok or a Some and returns the Fail or None from the surrounding function early, converting the error type through From when they differ.
- `language/errors/the-error-trait.md` - **The Error trait** (reference): Error is a trait, not a base type; a failure that implements it fits into Result<Value, Error> for the layers that only need to report it, and cause() gives the chain.
- `language/errors/error-types.md` - **Declaring an error type** (reference): An error type is a type with cases like any other; a case that wraps one value of a type no other case wraps gets From generated, which is what makes ? convert on its own.
- `language/errors/panic.md` - **panic** (reference): panic prints panic, the message and the site to standard error, exits with 101, and runs nothing else on the way out - it is for bugs, never for an expected failure.
- `language/errors/top-level-errors.md` - **Errors at the top level** (reference): A ? at the top level of an entry file or a script is not a panic; it prints the error and exits with 1, walking cause() one line per link, and in the dev profile names every ? the failure went through.

## language/collections-and-iteration

- `language/collections-and-iteration/index.md` - **Collections and iteration** (index): List, Map, Set, Stack and Queue as traits over a shared Iterate, plus slices, pipelines and collectors.
- `language/collections-and-iteration/collection-traits.md` - **The collection traits** (reference): Every kind of collection is a trait - List, Set, Map, Stack, Queue - so a signature names what a value can do, and only its construction names the data structure behind it.
- `language/collections-and-iteration/lists.md` - **Lists** (reference): List is the ordered, indexable sequence behind the literal [1, 2, 3], with ArrayList as the default implementation and a verb paired with a participle for every change.
- `language/collections-and-iteration/maps-and-sets.md` - **Maps and sets** (reference): The literal ["a": 1] builds a Map, a Set is built from a list literal instead of having one of its own, and both iterate in insertion order while comparing regardless of it.
- `language/collections-and-iteration/stacks-and-queues.md` - **Stacks and queues** (reference): Stack is LIFO with push, pop and peek, Queue is FIFO with enqueue, dequeue and peek - the words everybody knows for each structure.
- `language/collections-and-iteration/slices.md` - **Slices** (reference): list[from..to] answers a List that shares storage and starts at index 0 again; as a var path the same expression is a window into the original instead.
- `language/collections-and-iteration/iterating.md` - **Iterating** (reference): for pulls from Iterator.next() through Iterate.iterate(), and the subject of a for is evaluated once into a temporary, so changing it inside the loop does not affect what is walked.
- `language/collections-and-iteration/changing-in-place.md` - **Changing elements in place** (reference): for var item in items binds each slot of a container in turn, so a change of item changes the container itself - over a List, an Array, a window, the values of a Map, and any type with MutableIndex.
- `language/collections-and-iteration/pipelines.md` - **Pipelines** (reference): A pipeline is a source, zero or more lazy stages and exactly one terminal operation, and nothing runs until the terminal operation pulls a value through.
- `language/collections-and-iteration/collectors.md` - **Collectors** (reference): An Accumulator describes what to do with the values of a pipeline and is the state of one run at the same time, because a value is a copy; collect fills a copy of the one it is given.

## language/modules-and-packages

- `language/modules-and-packages/index.md` - **Modules and packages** (index): How a file brings in names from elsewhere, what a package is, and the two rules - visibility and top-level code - that decide what a module may contain.
- `language/modules-and-packages/use.md` - **use** (reference): use brings names into scope from a package or a file. Everything after from names a module; a path brings in a case of a type or a member another package attaches to it, and a use without names is an error.
- `language/modules-and-packages/visibility.md` - **Visibility** (reference): A top-level declaration is private to its file unless marked public, and a public declaration may not expose a type that is private to its own file.
- `language/modules-and-packages/deprecation.md` - **Deprecation** (reference): A deprecated clause above a declaration or a member keeps it working for its callers, warns at every use outside its file, and names the replacement that torb lint --fix writes.
- `language/modules-and-packages/the-prelude.md` - **The prelude** (reference): The prelude is the package whose public names are in scope in every file without an import, and it holds only the pure part of the standard library.
- `language/modules-and-packages/top-level-code.md` - **Top-level code** (reference): A statement outside every declaration is only allowed in an entry file, a script or a test file, and a top-level const of a module has to be known at compile time.
- `language/modules-and-packages/cyclic-imports.md` - **Cyclic imports** (reference): Two modules may import each other, because nothing runs when a module is imported and its exports are computed to a fixpoint, but the same cycle between top-level statements is an error.

## language/reflection

- `language/reflection/index.md` - **Reflection** (index): Why there is no runtime reflection, the four syntactic bridges that connect a type to a value instead, and the generated Encode and Decode pair that covers serialization.
- `language/reflection/no-reflection.md` - **There is no reflection** (reference): A type never flows as a value, so there is no Type type, no typeof and no Class.forName - only four syntactic bridges connect a type to a value, all resolved at compile time.
- `language/reflection/encode-and-decode.md` - **Encode and Decode** (reference): A value is its constructor call, offered in three forms - Encode writes it, Decode reads it back, Describe describes it without a value - so a type works with every format without hand-written serialization code.
- `language/reflection/encoders.md` - **Encoder and Decoder** (reference): Encoder and Decoder each name every scalar the language has - bool, int, unsigned, float, decimal, string, bytes - plus the four shapes a value can take, sequence, map, record and variant, which open and are closed by finish.

## language/configuration

- `language/configuration/index.md` - **Configuration** (index): Receiver closures, the builder function around one, and the receiver script and sandbox that let a whole file play the same role - statically typed configuration without a second language.
- `language/configuration/receiver-closures.md` - **Receiver closures** (reference): A receiver closure is a closure whose first parameter is called self, so names inside it resolve against that receiver first, exactly as inside a method.
- `language/configuration/builders.md` - **Builders and DSLs** (reference): A builder is a function that creates a value, hands it to a receiver closure to configure, and returns it, which is what makes a configuration block a statically typed value instead of a string to parse.
- `language/configuration/receiver-scripts.md` - **Receiver scripts** (reference, draft): A .trb file can be loaded as the body of a receiver closure, type checked against a receiver type before it runs, and run by the sandboxed VM.
- `language/configuration/the-sandbox.md` - **The sandbox** (reference, draft): A script has no IO, no network, no clock, no environment and no foreign functions by default, and only the caller of Sandbox.load can grant more, in a block that names exactly what is granted.

## language/execution

- `language/execution/index.md` - **Execution** (index): The parts of running a program that are a rule of the language rather than an implementation detail - evaluation order, copies, tail calls, destructors, and which arm of a branch on a compile-time constant is compiled.
- `language/execution/loops.md` - **Loops** (reference): for walks an Iterate, while repeats while a condition holds, and loop is the endless one - with the type Never until a break gives it a Void. while true is an error, because never ending is a property of the syntax here.
- `language/execution/evaluation-order.md` - **Evaluation order** (reference): Evaluation order is source order - the receiver first, then the arguments as they are written, then the parameter defaults - so a side effect in an argument is exactly as predictable as reading the line.
- `language/execution/copies.md` - **What a copy costs** (reference): A copy always behaves the same way, but what it costs depends on the shape of the type - inline for a small fixed-size value, copy-on-write for heap-backed storage, and never for a shared type.
- `language/execution/tail-calls.md` - **Tail calls and stack overflow** (reference): Direct self-recursion in tail position is guaranteed to run without growing the stack, and every other call uses a frame of its task's stack; a recursion that runs out of stack panics with stack overflow instead of crashing.
- `language/execution/destructors.md` - **Destructors - close() runs at the last release** (reference): A shared type's close() is its destructor - it runs exactly once at the last release, user code never calls it, and a binding that holds one is released at the end of its block, the last declared first.
- `language/execution/compile-time-branches.md` - **Compile-time branches** (reference): A match, an if or an if const whose subject is a compile-time constant keeps the one arm its value selects - OperatingSystem.current is one - while every arm is still type checked on every machine and exhaustiveness is judged by the type.

## language/extensibility

- `language/extensibility/index.md` - **Extensibility** (index): The language is extended by writing functions, not macros or annotations - control structures, DSLs and query providers are all ordinary functions, closures and Expression<Value> parameters.
- `language/extensibility/control-structures.md` - **Control structures are functions** (reference): do, unless, retry and test are ordinary functions with a closure or lazy parameter, so writing your own control structure is nothing more than writing a function that takes one and calling it with a trailing closure.
- `language/extensibility/expression-trees.md` - **Reading code instead of running it** (reference): A query provider reads the typed tree of an Expression<Value> instead of running it, translates what it recognizes, and fails at its own runtime for a call it does not - the language cannot know in advance what a library can translate.
- `language/extensibility/foreign-functions.md` - **Foreign functions** (reference, planned): foreign declares functions of a C library with the ABI as the contract, available to any package unlike native, but nothing links or calls one yet and its Pointer and CString types are not declared in std/ either.

## how-to

- `how-to/write-a-configuration-file.md` - **Write a configuration file** (how-to): Declare a type for the configuration, write the file as TorbScript against it, and load it through the sandbox with the capabilities you grant.
- `how-to/define-an-error-type.md` - **Define an error type** (how-to): Declare a type with one case per distinct failure, add Show and Error where a layer above needs to hand it further up, and let the generated From do the conversion at every ?.
- `how-to/parse-text-into-a-type.md` - **Parse text into a type** (how-to): Give the type a private field nothing outside can set directly, and implement TryFrom<String, Failure> so that Type.tryFrom(text) validates the text and answers a Result instead of a bare value.
- `how-to/use-a-type-as-a-map-key.md` - **Use a type as a map key** (how-to): An ordinary type is already a legal key once every field is Hash, which the compiler generates for free; a field that cannot be Hash is the one thing that rules a type out.
- `how-to/convert-between-types.md` - **Convert between types** (how-to): Implement From when the conversion cannot fail and TryFrom when it can - text is a source like any other - and call to<Target>() to collect a pipeline into any type built from one.
- `how-to/collect-a-pipeline.md` - **Collect a pipeline into what you need** (how-to): Reach for the named terminal operation when there is one - toList, sum, joined, groupBy - and fall back to collect with an Accumulator for anything else, including your own accumulator.
- `how-to/sort-by-more-than-one-key.md` - **Sort by more than one key** (how-to): Sort by a tuple key instead of a single field - a tuple's Compare is generated lexicographically by position, which a type never gets because an order is a decision, not a structure.
- `how-to/write-a-builder.md` - **Write a builder** (how-to): Write a function that creates a value, hands it to a receiver closure, and returns it - three lines that make every property command, nested block and method call in the closure statically typed.

## explanation

- `explanation/index.md` - **Why the language is like this** (index): The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.
- `explanation/why-values-instead-of-references.md` - **Why values instead of references** (explanation): Every type is a value and the binding decides about mutation, which removes every mutable-and-immutable type pair at the price of one local, lintable trap.
- `explanation/coming-from-rust.md` - **Coming from Rust** (contrast): What carries over from Rust, what looks the same and is not, and what Rust has that TorbScript deliberately does not.
- `explanation/coming-from-swift.md` - **Coming from Swift** (contrast): What carries over from Swift - value types, enums with payloads, Optionals, protocols with default members - and what a Swift habit gets wrong here.
- `explanation/why-no-null.md` - **Why there is no null** (explanation): Absence is Option<Value>, an ordinary case of an ordinary type, so a value is wrapped and unwrapped on purpose and nothing can be dereferenced without checking first.
- `explanation/coming-from-kotlin.md` - **Coming from Kotlin** (contrast): What carries over from Kotlin - when expressions, extension functions, a nullable-looking ? - and the three places nullability, data classes and DSL receivers work on a different mechanism underneath.
- `explanation/why-no-exceptions.md` - **Why there are no exceptions** (explanation): A function that can fail says so in its result type, Result<Value, Failure>, and the caller handles it with match, ??, or the question mark operator, while panic stays reserved for bugs that cannot be recovered from.
- `explanation/coming-from-typescript.md` - **Coming from TypeScript** (contrast): What carries over from TypeScript - literal unions, structural-looking optional chaining, declarative generics - and the four places TypeScript's type-level programming has no counterpart at all.
- `explanation/why-cases-are-never-bare.md` - **Why a case is never bare** (explanation): Circle alone is a type, a function or a variable, exactly like every other name, so a case is written Shape.Circle, .Circle or imported by its path, and a misspelled case can never fall back to matching everything.
- `explanation/why-commands.md` - **Why a call is written as a command** (explanation): A call is written without parentheses wherever the grammar allows it, so a control structure, a DSL and an ordinary call share one shape and no library gets special syntax the language itself does not have.
- `explanation/why-verbs-and-participles.md` - **Why verbs and participles** (explanation): A method that changes in place and the method that returns a changed copy are different words, sort against sorted, because value semantics make one name for both ambiguous at the call site.
- `explanation/why-no-getters.md` - **Why there are no properties** (explanation): A field is storage and a method computes, so the parentheses tell a reader which one they are looking at, and protected var replaces the getter-and-setter pair without hiding either fact.
- `explanation/why-dead-changes-are-errors.md` - **Why a change that cannot be seen is an error** (explanation): A var that is changed and never read again, or the discarded result of a method that takes self, is a compile error rather than a lint, because with value semantics such a change is always a mistake and never a defensive copy.
- `explanation/why-no-higher-kinded-types.md` - **Why there are no higher-kinded types** (explanation): Option, Result, Task and Iterate share method names by convention of the standard library rather than by a shared abstraction, because a kind system would cost local inference and readable errors for problems a script rarely has.
- `explanation/why-no-macros.md` - **Why there are no macros** (explanation): Names are resolved with the help of types, which have to exist before resolution runs, so an AST macro generating code before that point cannot go together with the rest of the language - quoted expressions read code instead.
- `explanation/why-no-reflection.md` - **Why there is no reflection** (explanation): Types never flow as values, so nothing can inspect a type at runtime; what reflection is reached for - serialization, schemas, debug output - is covered by three generated forms of a constructor, Encode, Decode and Describe.
- `explanation/why-bit-operators-bind-like-arithmetic.md` - **Why the bit operators bind like arithmetic** (explanation): The integer types have `&`, `|`, `^`, `~`, `<<` and `>>` on the levels Go and Swift give them, so `x & 1 == 0` is the test it looks like, and they are the members of the Bits trait the way `+` is Add.
- `explanation/why-traits-instead-of-inheritance.md` - **Why traits instead of inheritance** (explanation): A type comes with a trait instead of extending a base class, so composition and delegation replace an inheritance hierarchy, and a value can still be typed by capability without carrying a class it did not ask for.
- `explanation/why-one-member-namespace.md` - **Why a method is a constant** (explanation): A method is structurally a constant of the type that holds a receiver closure, so a field and a method live in one namespace and cannot share a name, which is what lets a trailing block configure a record field in place.
- `explanation/why-exhaustive-matches.md` - **Why every match is exhaustive** (explanation): A public ADT is a promise about every case it has today, so a match must cover all of them and a new case is a breaking change, while a library that wants room to grow hides its ADT behind a type instead.
- `explanation/where-are-my-overloads.md` - **Where are my overloads** (explanation): A call has exactly one signature, because that signature is what gives every argument its meaning, so overloading by parameter type and uniform function call syntax are both out.
