---
title: Reading code instead of running it
summary: A query provider reads the typed tree of an Expression<Value> instead of running it, translates what it recognizes, and fails at its own runtime for a call it does not - the language cannot know in advance what a library can translate.
kind: reference
status: stable
order: 20
keywords:
  - query provider
  - ExpressionNode
  - translate
source:
  - CONCEPT.md#quoted-expressions-expressionvalue
  - std/expression/src/lib.trb
---

An `Expression<Value>` hands a function two things: the ordinary value, and the typed tree of what was written (see
[Quoted expressions](../functions/quoted-expressions.md)). A query provider is code that reads the second one
instead of the first, to turn a condition into something that is not TorbScript at all - a SQL `WHERE` clause, a
database index lookup, a validation rule stored as data.

## Example

```trb check
type Unsupported with Show, Error {
  description: String

  fn show(): String {
    "cannot translate: {description}"
  }
}

fn translate(condition: Expression<Bool>): Result<String, Unsupported> {
  match condition.tree {
    .Binary(.Greater, left, right, _) => Ok "{nameOfNode(left)} > {nameOfNode(right)}"
    _ => Fail Unsupported(condition.source)
  }
}

fn nameOfNode(node: ExpressionNode): String {
  match node {
    .Captured(_, name, _) => name
    .Parameter(_, name, _) => name
    _ => "?"
  }
}

const limit = 5
print translate(limit > 3)
print translate(limit == 3)
```

## Syntax

```text
fn <name>(condition: Expression<Bool>): Result<Translated, Unsupported> {
  match condition.tree {
    .Binary(operator, left, right, _) => ...   // what the provider can translate
    _ => Fail Unsupported(condition.source)     // everything else, at the provider's own runtime
  }
}
```

## Rules

1. **A provider matches on `condition.tree`, an ordinary `ExpressionNode`.** Every node already carries the type of
   the expression it stands for, because quoting happens after name resolution and type checking - there is nothing
   left to resolve inside the tree itself.

2. **What a provider does not recognize is its own failure, at its own runtime.** The language cannot know ahead of
   time what a library can translate, so an unmatched shape is a `Fail`, not a compile error - the checker only
   guarantees that `condition` was a valid `Bool` expression to begin with.

3. **A provider's `match` ends with a catch-all arm, on purpose.** `ExpressionNode` is an ordinary ADT that gains
   cases as the language grows; a provider whose last arm is `_ => Fail(Unsupported(...))` keeps compiling against a
   newer language instead of becoming non-exhaustive.

4. **The tree cannot be executed, only the value can.** A provider that gives up on translating a shape can still
   fall back to `condition.value()` and run the original code in memory - the tree does not have to be the only way
   to get an answer.

5. **A provider gets exactly the one tree of the one expression, and nothing else.** There is no way from a
   `TypeReference` on a node back to the type it names, so a provider cannot look up a related field, a supertype or
   anything that was not written at the call site.

## What this is not

**Reading a tree is not compiling it.** A provider inspects data; it never turns `ExpressionNode` back into
something callable, because there is no such step in the language.

```trb check
fn translate(condition: Expression<Bool>): String {
  condition.tree.capturedNodes().map { node =>
    match node {
      .Captured(_, name, _) => name
      _ => "?"
    }
  }.joined(separator: ", ")
}

const limit = 5
print translate(limit > 3)
```

```trb error
fn wrong(condition: Expression<Bool>): Bool {
  condition.tree()
}
// error: `tree` is a field, and parentheses call a function
```

## Related

- [Quoted expressions](../functions/quoted-expressions.md) - what `Expression<Value>` hands over, and its restrictions.
- [There is no reflection](../reflection/no-reflection.md) - why `TypeReference` cannot be turned back into a type.
- [Why there are no macros](control-structures.md) - the other half of "extended by functions, not by macros".

