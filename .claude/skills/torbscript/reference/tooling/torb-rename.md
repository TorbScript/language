---
title: torb rename
summary: torb rename renames fields at their declaration and at every use the type checker resolves to them - members, bare names, labels, patterns and doc links - or refuses and writes nothing.
kind: tooling
status: stable
order: 135
keywords:
  - torb rename
  - rename a field
  - checked rename
  - keyword-named field
  - question-field
source:
  - compiler/src/lint/rename-command.trb
  - compiler/src/lint/rename.trb
---

`rename` changes the name of a field everywhere it is used, and only there. It asks the type checker which declaration
each name means, so a parameter, a local, the label of a function's call or a field of another type that happens to
have the same name keeps it. That is the difference to a replacement of text, and the reason a rename of a field that
a dozen types share is one command.

## Synopsis

```text
torb rename [<file>:]<Type>.<field>=<name>... [path]...
torb rename [<file>:]<Type>.<Case>.<field>=<name>... [path]...

  <file>        The end of the path of the file the type is declared in, where two types share its name
  [path]...     What is checked and changed (default: the current directory); name every project that uses the fields
```

## What it does

The paths are checked the way [`torb check`](torb-check.md) checks them. Each rename names one field: of a type
(`Door.isOpen=open`) or of a case (`Shape.Circle.radius=size`), with the end of a file's path in front where more than
one type of that name is declared (`syntax/ast.trb:Parameter.annotation=type`). Every rename of the command line is made
in one go, and every place the checker resolved to the field is edited:

- the declaration;
- a member read or written through `.`: `door.isOpen`, `self.isOpen = true`;
- a bare name inside the type, in a method or a receiver closure;
- the label of the type's constructor, of its `copy` and of a case: `Door(isOpen: true)`, `door.copy(isOpen: false)`;
- the label of a pattern: `Door(isOpen: true, ...)`, `.Circle(radius: size)`;
- the field a `with ... by` delegates to;
- a link of a doc comment that names the field through its type: `[Door.isOpen]`.

A new name may be a keyword, because a field may be named after one
([Lexical structure](../language/syntax/lexical-structure.md), rule 6). A bare keyword always begins its own construct,
so a bare use of the field inside its type becomes `self.type`. A bare use in a body that binds the new name itself - a
parameter, a local - becomes `self.name` too, so that it does not mean the local afterwards.

**A rename is refused rather than half made.** Nothing is written, every problem is printed, and the command leaves
with 1 when one rename cannot be made:

- no field of that name, or more than one (put the end of a file's path in front);
- the new name is not the name of a field, or the type already has a field or a member of that name - a field and a
  member never share a name;
- a bare name inside the type already means something else by the new name - a function or a constant of the file -
  which the renamed field would take over;
- a file whose text has the old name as a word was not checked without a problem: a file the paths do not reach, or one
  with an error, might hold a use nobody resolved;
- a file no longer parses with the new name - every edited file is parsed again before the first one is written.

What `rename` wrote still has to pass `torb check`, and [`torb format`](torb-format.md) writes the layout again where
a shorter name lets a broken call fit on one line.

### Exit codes

`0` when the renames were made, `1` when one of them could not be and nothing was written, `2` for arguments `rename`
does not recognize.

## Examples

Two fields of the syntax tree named after a keyword, and the `else` of an `if`, over the whole repository:

```console
$ torb rename syntax/ast.trb:Parameter.annotation=type syntax/ast.trb:ExpressionKind.If.otherwise=else .
Parameter.annotation -> type: 17 places
ExpressionKind.If.otherwise -> else: 3 places
renamed 2 fields at 20 places in 14 files
```

A new name the type already has:

```console
$ torb rename Door.isOpen=open .
torb rename: `Door.isOpen`: `Door` has a member `open`, and a field and a member never share a name
nothing was written
```

## Related

- [torb lint](torb-lint.md) - `question-field` renames a `Bool` field whose adjective it is certain of the same way,
  and names this command for the others.
- [Fields](../language/types/fields.md) - what a field is, and how one named after a keyword is reached.
- [torb check](torb-check.md) - what has to pass afterwards.
- [The torb command](the-torb-command.md) - every subcommand.

