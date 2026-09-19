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

<!-- torb:index:end -->
