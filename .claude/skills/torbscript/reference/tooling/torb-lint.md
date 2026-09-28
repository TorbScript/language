---
title: torb lint
summary: torb lint reports the style rules the type checker leaves alone - Self, a Bool field named as a question, an unread binding, an unlabeled literal, a redundant Some or Ok, private(var) - each with its id and, where certain, a fix.
kind: tooling
status: stable
order: 130
keywords:
  - torb lint
  - lint --fix
  - self-name
  - question-field
  - unread-binding
  - labeled-literal
  - redundant-wrap
  - protected-field
source:
  - compiler/src/lint/command.trb
  - compiler/src/lint/finding.trb
  - compiler/src/lint/question-field.trb
  - compiler/CONTRIBUTING.md
---

`lint` is the linter of `torb`: the rules a program can break and still type check, because they are about how it is
written and not about what it means. Every finding names its rule, and a rule that knows the fix for certain carries it,
so `torb lint --fix` is also how a change of a convention migrates the code that follows the old one.

## Synopsis

```text
torb lint [--fix] [--rule <id>]... [--skip <id>]... [path]...

Rules (every one runs unless --rule picks some):
  self-name        Inside a `type` or an `extend`, its own name is written `Self` (fix)
  question-field   A `Bool` field is an adjective or a participle, not a question that starts with `is` (fix; runs the checker)
  unread-binding   A name that `const`, `for` or a closure binds and nothing reads is written `_` (fix; runs the checker)
  labeled-literal  `true`, `false` or `None` for a `Bool` or an optional carries the label (fix; runs the checker)
  redundant-wrap   No `Some(value)` or `Ok(value)` where the value wraps itself (fix; runs the checker)
  protected-field  A field only its own file writes is `protected var`, not `private(var)` (fix)

  --fix         Write every fix a rule is certain of, then lint again and report what is left
  --rule <id>   Run this rule (repeatable); without it every rule runs
  --skip <id>   Leave this rule out (repeatable)
```

## What it does

A path is a file or a directory (default: the current directory), and a directory is read the way
[`torb format`](torb-format.md) reads one: every `.trb` file below it, without hidden directories, `build`
directories and the files that are broken on purpose. A file that does not parse is named and skipped.

A finding is printed the way [`torb check`](torb-check.md) prints a diagnostic, with `warning` in front, and with the
id of its rule under it:

```console
$ torb lint --rule self-name std/geometry
warning: `Box<Scalar>` is the type this is declared in: write `Self`
  --> std/geometry/src/box.trb:37:71
   |
37 |   static fn between(first: Vector3<Scalar>, second: Vector3<Scalar>): Box<Scalar> {
   |                                                                       ^^^^^^^^^^^
   = rule `self-name`
   = `torb lint --fix` writes the fix

...
49 findings in 11 of 15 files (self-name 49)
```

### The rules

**`self-name`**: inside a `type` or an `extend`, the type's own name is written `Self` (the owner, 2026-09-26). It finds
the name where `Self` means exactly the same type and nowhere else: in a written type whose type arguments are the
type's own parameters in their order (`Box<Other>` inside `type Box<Item>` is another type and stays), in an expression
of a type without parameters (`Point(0, 0)`, `Point.origin`, `Shape.Circle`), and `Box<Item>(...)` with exactly its own
parameters - a bare `Box(...)` inside a generic type may build another instance, so it stays. A pattern has no `Self`,
so `Shape.Circle(radius)` becomes `.Circle(radius)` there. A method with a type parameter named like one of the type's
own is left alone, and a `trait` is not looked into.

**`question-field`**: a `Bool` field is an adjective or a participle (`inclusive`, `shared`, `exported`), and a
question that is computed is a method (`isEmpty()`) - see [Naming](../language/syntax/naming.md). A field of a type or
of a case whose type is `Bool` and whose name is `is` followed by a capital letter is found.

Its fix is the rename [`torb rename`](torb-rename.md) makes: the field gets the word after `is` (`isShared` becomes
`shared`) at its declaration and at every use the checker resolves to it, so the rule runs the checker. It offers the
fix only where it is certain of that word - the last word after `is` ends like a participle or an adjective (`-ed`,
`-able`, `-ible`, `-ive`, `-ous`, `-ful`, `-less`, but not `-eed`) - and where nothing stands in its way: the word is
not a keyword, the type has no field or member of that name, and every file that mentions the field was checked
without a problem. A noun (`isCall`), a preposition (`isOn`) and a keyword (`isVar`) need a person's word, and the
finding names the `torb rename` that takes it. `--fix` writes the edit of every use as well; a plain run reports the
declaration alone.

**`unread-binding`**: a name that an irrefutable pattern binds and that nothing reads is written `_` - a `const` inside
a body, what `for` binds, and the parameters of a closure. The refutable positions are the checker's, where the same
thing is an error. Without name resolution the rule stays on the safe side: a name counts as read when it occurs as a
word anywhere in the text it could be read in, so a comment, a string and a name bound again all count. A name that
starts with `_`, a `var`, a `using`, a binding at the top level of a file and the parameters of a function (which are
the labels of its calls) are left alone.

Its fix needs the checker. A value that may hold something with a `close()` is released at the end of its block, and
`_` would release it at once, so `close()` would run earlier (see
DESTRUCTORS.md, 2a). The fix is only offered where the checker says the value is plain:
a number, a `Bool`, a `Char`, a `String`, `Void`, a literal type, and a tuple, an `Option`, a `Result`, a range or a
collection of the prelude made of them. Anywhere else the finding comes without a fix and names `using` as the other
way to write a binding that is only there to be closed at the end of its block.

**`labeled-literal`**: `true`, `false` or `None` passed to a parameter that is declared as `Bool` or as an optional
carries the parameter's label (`hasCapacity: false`), except in a call with a single argument and where the parameter's
type is a type parameter - there the literal is the data, not an option. It needs the checker, so a run with it checks
the projects of the paths first, and a file the checker has a problem with gets no finding of it. The fix writes the label only where every argument behind the literal is labeled already or is a trailing closure;
anywhere else the call has to be reordered, which is a person's choice.

```trb check
fn listEntries(entries: Int, kind: String, hasCapacity: Bool = true): Int {
  entries
}

print listEntries(3, "ArrayList", hasCapacity: false)
```

**`redundant-wrap`**: a written `Some(value)` or `Ok(value)` where the value would wrap itself - a value of exactly
`Value` where an `Option<Value>` or a `Result<Value, Failure>` is expected becomes `Some(value)` or `Ok(value)` on its
own ([Conversions](../language/types/conversions.md), rule 8), so the written case is a second spelling. Which calls
those are is the checker's answer, because only it knows the expected type; the rule runs it like `labeled-literal`.
A case the value needs is left alone: the inner `Some` of a nested `Option`, a value whose type the case's expectation
decided (`None`, `Fail(...)`, a call of a generic function, `into()`), `Ok(void)`, and a case where nothing is
expected. The fix replaces the call with its argument, in parentheses where the call is an operand and the argument an
operation.

```trb check
fn half(value: Int): Result<Int, String> {
  if value % 2 != 0 {
    return Fail "{value} is odd"
  }
  value / 2
}

print half(8)
```

**`protected-field`**: a field that everybody reads and only the file of its type writes is spelled
`protected var connections: Int`, and `private(var) connections: Int`, the spelling it replaces, is found and rewritten
to it ([Fields](../language/types/fields.md), rule 4). The checker refuses `private(var)` with an error that names
this rule, which is its fix: the two mean the same, so the fix is certain, and `torb lint --fix --rule protected-field`
is the whole migration of a project. A `private(var)` field that writes `var` as well gets no fix, because the one it
would get says `var` twice.

```trb check
type Server {
  protected var connections: Int = 0

  var fn accepted() {
    connections = connections + 1
  }
}

var server = Server()
server.accepted()
print server.connections
```

### `--fix`

Every fix of every finding is applied, a fix that overlaps one taken before it is left out, and a file whose fixed text
does not parse is fixed one fix at a time, keeping only the ones that parse. Then the whole lint runs again and reports
what is left: the findings without a fix, and whatever a fix could not reach. What `--fix` wrote still has to pass
`torb check` - a fix is certain about the rule, and the checker is the one that is certain about the program.

### Exit codes

`0` when nothing was found, `1` when something was (after `--fix`: when something is left), `2` for an argument `lint`
does not recognize.

### What is not decided

- **Where a project chooses its rules.** PROJECT.md has no setting for them, so `--rule` and
  `--skip` on the command line are the one way to choose today.
- **The rules named and not built.** A closure that only passes its parameter on (`items.map { stripMargin(_) }` for
  `items.map(stripMargin)`) is only certain where the callee takes exactly that one parameter, which needs the checker's
  answer about defaults, labels and `var` parameters. A case whose type the expected type already names
  (`.SwitchCase(path)` for `DecisionNode.SwitchCase(path)`) needs the expected type of every argument. A field marked
  `deprecated` in favour of a method needs the marker, which the language does not have yet.

## Examples

One rule over one package, with its fix:

```console
$ torb lint --fix --rule self-name std/geometry
fixed 11 files; what is left:
15 files, no findings
```

The fix of `question-field` reaches every use, so it runs over every project that uses the fields - for the
repository that is all of it:

```console
$ torb lint --fix --rule question-field .
```

Every fix of the repository applied at once still type checks: `torb lint --fix` over a copy of the whole checkout,
then `torb check .`, answers `no problems`.

## Related

- [Naming](../language/syntax/naming.md) - which rules of a name the parser and the checker enforce, and which are
  conventions `torb lint` reports.
- [torb format](torb-format.md) - the other milestone 8 tool, for layout instead of naming.
- [torb rename](torb-rename.md) - the rename `question-field` makes, with the word a person chose.
- [torb check](torb-check.md) - the errors, in the same format.
- [The torb command](the-torb-command.md) - every subcommand.

