---
title: Functions
summary: Declaring a function, its arguments and defaults, variadic parameters, closures, trailing closures, parameter modes and quoted expressions.
kind: index
status: stable
order: 40
---

Everything about a `fn`: how one is declared, how its arguments are passed, the one closure form, and the parameter
modes a signature can ask for.

## What belongs here

What does not belong here: the members of a `type`, which are in `types/`. Every page in this folder is a reference
page: an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Declaring a function](declaring-a-function.md)** - fn declares a function with a mandatory parameter type on every parameter; the last expression of the body is the result, and a public function or a trait method must always spell out its return type.
- **[Arguments and labels](arguments.md)** - An argument is passed positionally or by label, positional arguments always come first, and a label matches a parameter by name rather than by position.
- **[Default values](default-values.md)** - A parameter default is an expression that runs at every call which omits the argument, in the scope of the declaration, without self and without the other parameters.
- **[Variadic parameters](variadics.md)** - A parameter written ...name collects every remaining positional argument into a List, and a collection is only unpacked into it when the call spreads it with the same three dots.
- **[Closures](closures.md)** - A brace in expression position is always a closure with inferred or written parameters; it captures a const binding as a copy and a var binding as itself, which only a closure handed to a parameter that just calls it may do.
- **[Trailing closures](trailing-closures.md)** - When the last parameter of a call is a function, the closure argument can follow the call as a brace instead of sitting inside the parentheses.
- **[Parameter modes](parameter-modes.md)** - A parameter is an ordinary value unless it says otherwise; var hands over a path to mutate, lazy defers evaluation once, a self-named closure resolves names against a receiver, and Expression also hands over the typed tree.
- **[Quoted expressions](quoted-expressions.md)** - A parameter or binding typed Expression<Value> gets the ordinary value plus the typed tree of what was written, its source text and the values it captured, which is what assert and a query provider read instead of running the code twice.

<!-- torb:index:end -->
