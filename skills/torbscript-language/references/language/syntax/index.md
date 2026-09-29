---
title: Syntax
summary: "How TorbScript is written: where a statement ends, how a call is spelled, and what a literal looks like."
kind: index
status: stable
order: 10
---

The surface of the language: lexical structure, literals, string interpolation, doc comments, the call forms and the
formatter canon.

## What belongs here

What does not belong here: the meaning of a construct, which belongs to the area it is about. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Lexical structure](lexical-structure.md)** - A statement ends at the end of its line, a name is ASCII while text is not, a block comment ends at its first `*/`, three keywords are never reserved, and a member may be named after a reserved one.
- **[Literals](literals.md)** - An integer, a decimal, a character and a string each have exactly one literal form, and a literal adapts to the type it is expected to have.
- **[String interpolation](string-interpolation.md)** - `{expression}` inside a string runs the expression and shows it, a literal brace is written `\{` or `\}`, and the expression inside the braces has to fit on one line.
- **[Multi-line strings](multi-line-strings.md)** - A `\"\"\"` string is dedented by the indentation of its first line with content, so a block of text reads at the indentation of the code around it instead of jammed against the left margin.
- **[Naming](naming.md)** - A name is written in ASCII letters, digits and `_`, a type starts with an uppercase letter and everything else with a lowercase one, and the compiler reports both at the declaration.
- **[Angle brackets or comparison](generics-or-comparison.md)** - A `<` after a name starts a type argument list only if what follows parses as types up to a matching `>` that is itself followed by a token a comparison could not have.
- **[Doc comments](doc-comments.md)** - A `/** */` comment attaches to the declaration written directly after it, and everything that can be declared - including a parameter, a field or a case - can have one.
- **Syntax cheat sheet (skill `torbscript`: `references/language/syntax/cheat-sheet.md`)** - Every form of the language in one place: declarations, expressions, patterns, types and the call rules, with the exact spelling of each.
- **[Command calls](command-calls.md)** - A call is written without parentheses wherever the grammar allows it, and with parentheses everywhere else. This is the formatter canon and it is enforced, not preferred.

<!-- torb:index:end -->

