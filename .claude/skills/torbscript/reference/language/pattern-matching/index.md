---
title: Cases and pattern matching
summary: How a type with cases is declared, and every place a pattern can stand.
kind: index
status: stable
order: 50
---

Cases, `match`, every pattern form, exhaustiveness, importing cases, and patterns in bindings and conditions.

## What belongs here

What does not belong here: the fields and methods of a type, which are in `types/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Cases and match](cases-and-match.md)** - A case is a variant of a type, written Type.Case or .Case and bare only when it is imported. A match is an expression and must cover every case.
- **[Pattern forms](pattern-forms.md)** - Every pattern the language has, from a literal to a list pattern with a rest, one form per line.
- **[Exhaustiveness](exhaustiveness.md)** - A match has to cover every value of its subject, and an arm that no value can reach is a compile error, not a defensive line.
- **[Importing cases](importing-cases.md)** - A case is imported through the type it belongs to, and only a case can be; once imported it needs nothing in front of it, in an expression and in a pattern.
- **[Patterns in bindings and conditions](patterns-in-bindings.md)** - A pattern also stands after const and var, in the head of if and while, and in a for loop - the same vocabulary as a match arm, without the braces.
- **[if var](if-var.md)** - if var P = place binds a pattern into the place itself, exactly like a var parameter, instead of copying the value out first.

<!-- torb:index:end -->

