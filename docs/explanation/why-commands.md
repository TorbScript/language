---
title: Why a call is written as a command
summary: A call is written without parentheses wherever the grammar allows it, so a control structure, a DSL and an ordinary call share one shape and no library gets special syntax the language itself does not have.
kind: explanation
status: stable
order: 60
keywords:
  - command call
  - formatter canon
  - DSL
source:
  - CONCEPT.md#formatter-canon
  - CONCEPT.md#command-calls-calls-without-parentheses
---

`print("hello")` and `print "hello"` both parse. TorbScript picks one of them, in every place the grammar allows it,
and the formatter enforces the choice rather than leaving it to taste. This page is the argument for spending a rule
on something that looks like a style question.

## The decision

**A call is written as a command wherever the grammar allows it, and with parentheses everywhere else.** `torb canon`
decides this on every file and there is no option to turn it around.

- A command call is legal only in command position: the start of a statement, the right side of `=`, after `return`,
  after `=>`.
- It needs a callee that is a name or a member path, at least one argument whose first token is not `(`, `[`, `-`,
  `!` or `.`, no operator at the top level of an argument, and its arguments on one line.
- Everything else - a nested call, a call with no arguments, an argument with a top-level operator, the head of an
  `if`/`for`/`while`/`match` - keeps its parentheses.

```trb check
type Email with TryFrom<String, String> {
  private value: String

  fn tryFrom(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }
}

fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Fail "must not be negative"
  }
  Ok value
}

const email = Email.tryFrom "user@example.test"
print checked(5)
```

## Why

**Because a control structure and an ordinary call would otherwise need two different grammars.** `unless done { ... }`
is a function call - `unless` takes a `Bool` and a closure - and it reads like a keyword only because the command form
lets it. If commands did not exist, every control structure the standard library adds (`retry`, `using`, `do`) would
need its own keyword, which is the opposite of [Control structures are functions](../language/extensibility/control-structures.md).

**Because a configuration file gets its look from the same rule that makes `print "hello"` read naturally, not from a
second syntax.** `port 8080` inside a receiver closure is an ordinary command call on a field - see
[Property commands](../language/types/property-commands.md) - so a `project.trb` or any other DSL is legal TorbScript
end to end, checked by the same compiler that checks a function body, instead of a template language layered on top.

**Because deciding it once removes a decision from every call site.** Leaving the choice to the author, as most
languages that allow both do, means a codebase mixes `print(x)` and `print x` for no reason a reader can find, and a
model trained on other conventions reproduces whichever one it saw more often. The canon removes the question: there
is exactly one way to write a call with one argument and no operator in it, and `torb canon --check` finds the rest.

**Because "commands do not nest" keeps the one hard case decidable.** A command's own arguments are ordinary
expressions, so `print describe(numbers)` is legal and `print describe numbers` is not - if it were, a reader (and the
formatter) would have to work out for every argument whether it was itself a command, which is exactly the ambiguity
whitespace-sensitive parsing produces: nothing about the meaning of a token sequence depends on whitespace beyond
where a [statement ends](../language/syntax/lexical-structure.md).

### What was rejected

- **Leaving the choice to the author**, as Ruby and Kotlin do for calls that already support both. Rejected because it
  buys nothing that a fixed rule does not, and it costs a codebase a consistent shape.
- **Commands that nest** (`a b c` meaning `a(b(c))`). Rejected because it reintroduces exactly the ambiguity the single
  rule "the `{` belongs to the outermost command" was written to remove: `print numbers.map { _ * 2 }` would no longer
  have one unambiguous reading.

## Consequences

**A trailing closure always belongs to the outermost command of the statement**, so a closure argument to a command's
own argument needs parentheses around that inner call.

```trb check
const numbers = [1, 2, 3]
print numbers.map({ _ * 2 }).toList()
```

Writing the trailing closure on `map` itself instead - `print numbers.map { _ * 2 }` - is specified as an error (see
[Command calls](../language/syntax/command-calls.md), rule 9), because the `{ ... }` would otherwise belong to `print`
rather than to `map`. Today the checker does not reject this with that diagnostic; it reports an unrelated type-inference
failure instead. See the report of this package for the minimal reproduction.

**An operator at the top level of an argument forces parentheses, even in command position**, because
`assert sum == 3` would otherwise read as `(assert sum) == 3`.

```trb check
const sum = 1 + 2
assert(sum == 3)
```

**A call with no arguments always has parentheses.** `list.length()` needs them even though it stands in command
position, because a bare name is always a reference and never a call - see
[Command calls](../language/syntax/command-calls.md) for the complete rule and every corner case it covers.

## Related

- [Command calls](../language/syntax/command-calls.md) - the full grammar of a command, rule by rule.
- [Control structures are functions](../language/extensibility/control-structures.md) - `unless`, `retry`, `using`
  built from the same rule.
- [Property commands](../language/types/property-commands.md) - the same command syntax writing a field instead of
  calling a method.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the command-call mistake, first on
  the list.
