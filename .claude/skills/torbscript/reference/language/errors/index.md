---
title: Errors
summary: How a function says it can fail, how a caller handles it, and what a panic is for.
kind: index
status: stable
order: 60
---

`Option`, `Result`, the `?` operator, `??` and `?.`, the `Error` trait, declaring an error type, and `panic`.

## What belongs here

What does not belong here: the control flow constructs themselves, which are in `syntax/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Result](result.md)** - A function that can fail answers Result<Value, Failure>, whose cases are Ok and Fail. The postfix question mark unwraps an Ok or returns the Fail from the surrounding function.
- **[Optional chaining](option-chaining.md)** - `?.` is Option.map, or Option.flatMap when the member itself answers an Option, so chaining never nests. `??` is the trait OrElse, which gives a lazy fallback for an absent Option, a failed Result or any type that comes with it.
- **[The question mark operator](question-mark.md)** - A postfix ? unwraps an Ok or a Some and returns the Fail or None from the surrounding function early, converting the error type through From when they differ.
- **[The Error trait](the-error-trait.md)** - Error is a trait, not a base type; a failure that implements it fits into Result<Value, Error> for the layers that only need to report it, and cause() gives the chain.
- **[Declaring an error type](error-types.md)** - An error type is a type with cases like any other; a case that wraps one value of a type no other case wraps gets From generated, which is what makes ? convert on its own.
- **[panic](panic.md)** - panic prints panic, the message and the site to standard error, exits with 101, and runs nothing else on the way out - it is for bugs, never for an expected failure.
- **[Errors at the top level](top-level-errors.md)** - A ? at the top level of an entry file or a script is not a panic; it is specified to print the error and exit with 1, walking cause() one line per link.

<!-- torb:index:end -->

