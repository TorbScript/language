---
title: Why the language is like this
summary: The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript.
kind: index
status: stable
order: 50
---

Why TorbScript is the way it is. Each page takes one decision, gives the argument, names what was rejected, and says what
somebody writing TorbScript has to do differently because of it. This is the one part of the documentation where
alternatives, counter-examples and history belong.

The **contrast** pages are the most valuable ones here, and not only for people. A model that has read a great deal of
Rust, Swift, Kotlin and TypeScript will write TorbScript that looks like those languages and does not compile. Each
contrast page names the habits that break and what replaces them.

## What belongs here

An argument per page: the decision, why it was made, what was considered instead, and the consequence for code. Titles are
noun phrases (`Why values instead of references`), except the contrast pages, which are titled `Coming from <language>`.

What does not belong here: the syntax or the rules of a construct, which are in
[the language reference](../language/index.md), and a task, which is a [how-to](../how-to/index.md). A page here links the
reference instead of restating it.

<!-- torb:index:begin -->

## Pages

- **[Why values instead of references](why-values-instead-of-references.md)** - Every type is a value and the binding decides about mutation, which removes every mutable-and-immutable type pair at the price of one local, lintable trap.
- **[What a model trained on other languages gets wrong](mistakes-models-make.md)** - The mistakes a language model makes in TorbScript because it has read Rust, Swift, Kotlin and TypeScript, each with the wrong line, the right line and the diagnostic.
- **[Coming from Rust](coming-from-rust.md)** - What carries over from Rust, what looks the same and is not, and what Rust has that TorbScript deliberately does not.
- **[Coming from Swift](coming-from-swift.md)** - What carries over from Swift - value types, enums with payloads, Optionals, protocols with default members - and what a Swift habit gets wrong here.
- **[Why there is no null](why-no-null.md)** - Absence is Option<Value>, an ordinary case of an ordinary type, so a value is wrapped and unwrapped on purpose and nothing can be dereferenced without checking first.
- **[Coming from Kotlin](coming-from-kotlin.md)** - What carries over from Kotlin - when expressions, extension functions, a nullable-looking ? - and the three places nullability, data classes and DSL receivers work on a different mechanism underneath.
- **[Why there are no exceptions](why-no-exceptions.md)** - A function that can fail says so in its result type, Result<Value, Failure>, and the caller handles it with match, ??, or the question mark operator, while panic stays reserved for bugs that cannot be recovered from.
- **[Coming from TypeScript](coming-from-typescript.md)** - What carries over from TypeScript - literal unions, structural-looking optional chaining, declarative generics - and the four places TypeScript's type-level programming has no counterpart at all.
- **[Why a case is never bare](why-cases-are-never-bare.md)** - Circle alone is a type, a function or a variable, exactly like every other name, so a case is written Shape.Circle, .Circle or imported by its path, and a misspelled case can never fall back to matching everything.
- **[Why a call is written as a command](why-commands.md)** - A call is written without parentheses wherever the grammar allows it, so a control structure, a DSL and an ordinary call share one shape and no library gets special syntax the language itself does not have.
- **[Why verbs and participles](why-verbs-and-participles.md)** - A method that changes in place and the method that returns a changed copy are different words, sort against sorted, because value semantics make one name for both ambiguous at the call site.
- **[Why there are no properties](why-no-getters.md)** - A field is storage and a method computes, so the parentheses tell a reader which one they are looking at, and private(var) replaces the getter-and-setter pair without hiding either fact.
- **[Why a change that cannot be seen is an error](why-dead-changes-are-errors.md)** - A var that is changed and never read again, or the discarded result of a method that takes self, is a compile error rather than a lint, because with value semantics such a change is always a mistake and never a defensive copy.
- **[Why there are no higher-kinded types](why-no-higher-kinded-types.md)** - Option, Result, Task and Iterable share method names by convention of the standard library rather than by a shared abstraction, because a kind system would cost local inference and readable errors for problems a script rarely has.
- **[Why there are no macros](why-no-macros.md)** - Names are resolved with the help of types, which have to exist before resolution runs, so an AST macro generating code before that point cannot go together with the rest of the language - quoted expressions read code instead.
- **[Why there is no reflection](why-no-reflection.md)** - Types never flow as values, so nothing can inspect a type at runtime, and what reflection is reached for - serialization, config mapping, debug output - is covered by one generated trait pair, Encode and Decode, instead.
- **[Why there are no bit operators](why-no-bit-operators.md)** - Both `&` and `|` already mean something else, so bitwise work is a method of the Bits trait instead of a symbol, and only UInt64 gets the wrapping arithmetic a hash function needs.
- **[Why traits instead of inheritance](why-traits-instead-of-inheritance.md)** - A type comes with a trait instead of extending a base class, so composition and delegation replace an inheritance hierarchy, and a value can still be typed by capability without carrying a class it did not ask for.
- **[Why a method is a constant](why-one-member-namespace.md)** - A method is structurally a constant of the type that holds a receiver closure, so a field and a method live in one namespace and cannot share a name, which is what lets a command call on a field write it instead of needing a second rule.
- **[Why every match is exhaustive](why-exhaustive-matches.md)** - A public ADT is a promise about every case it has today, so a match must cover all of them and a new case is a breaking change, while a library that wants room to grow hides its ADT behind a type instead.
- **[Where are my overloads](where-are-my-overloads.md)** - A call has exactly one signature, because that signature is what gives every argument its meaning, so overloading by parameter type and uniform function call syntax are both out.

<!-- torb:index:end -->
