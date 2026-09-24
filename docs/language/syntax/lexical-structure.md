---
title: Lexical structure
summary: A statement ends at the end of its line, a name is ASCII while text is not, a block comment ends at its first `*/`, and only three keywords are never reserved.
kind: reference
status: stable
order: 1
keywords:
  - comments
  - statement continuation
  - contextual keywords
  - reserved words
  - ASCII
source:
  - CONCEPT.md#lexical-structure
---

Every `.trb` file is built from the same small set of tokens: two comment forms and a doc comment, a statement that ends
at the end of its line, and a fixed list of reserved keywords. This page is the mechanics a reader needs before any
construct, because it decides where one statement stops and the next begins.

## Example

```trb check
/** Moves a point along one axis. */
fn move(from: Int, to: Int): Int {
  to - from
}

// The total distance moved so far.
var total = 0

/* Block comment: it ends at the first star-slash. */
total = total +
  1

print "moved {move(from: 3, to: 10)}, total {total}"
```

## Syntax

```text
// text                      a line comment, to the end of the line
/* text */                   a block comment; ends at the first `*/`, does not nest
/** text */                  a doc comment; belongs to the declaration written directly after it
```

## Rules

1. **A statement ends at the end of its line.** There are no semicolons.

   ```trb error
   const total = 1;
   // error: There are no semicolons. A statement ends at the end of its line
   ```

2. **A statement continues onto the next line under two conditions.** The current line ends with a binary operator, a
   comma or an open bracket, or the next line starts with `.`, `?.`, a binary operator, `with` or `where`. The
   `total = total +` example above continues onto its next line for the first reason.

3. **A block comment ends at its first `*/` and does not nest.** Text that looks like a nested comment ends the outer
   one, and whatever follows is read as code.

   ```trb error
   /* outer /* inner */ still here */
   // error: Cannot find `still` here
   // error: Expected an expression, found `/`
   // error: Expected an expression, found the end of the file
   ```

4. **A doc comment belongs to the declaration written directly after it.** Everything that can be declared can have
   one - a function, a field, a parameter, a case. See [Doc comments](doc-comments.md).

5. **A fixed list of words is reserved, and three words never are.** `const`, `var`, `fn`, `type`, `trait`, `extend`,
   `foreign`, `case`, `use`, `public`, `private`, `native`, `shared`, `lazy`, `if`, `else`, `match`, `for`, `in`,
   `while`, `break`, `continue`, `return`, `true`, `false`, `void`, `self`, `Self`, `with` and `where` are reserved.
   `from`, `as` and `by` are contextual: the lexer reads them as ordinary names everywhere, which is what
   `use Name from "./module"` and `type Seconds with Compare by value` rely on.

   `raw` is contextual too, but at the lexer rather than the parser: it starts a raw string (`raw"text"`,
   `raw"""text"""`) only directly in front of `"`, and is an ordinary name everywhere else - `const raw = 1` and
   `fn raw(): Int` both declare a `raw`. See [Literals](literals.md), rule 9.

6. **After a `.` and as an argument label, the parser accepts a reserved word as a name too** - but a *declaration's*
   name always has to be a plain identifier, so nothing can be declared with a keyword's spelling. The rule therefore
   has an effect only for `from`, `as` and `by`, which are not reserved to begin with.

   ```trb check
   fn move(from: Int, to: Int): Int {
     to - from
   }

   print move(from: 3, to: 10)
   ```

   ```trb error
   fn accept(value: Int): Int {
     value
   }

   print accept(in: 3)
   // error: `accept` has no parameter `in`
   ```

7. **A parameter, a field, a function or a type cannot be named after a keyword.** The message names the participle a
   reader is looking for.

   ```trb error
   fn move(where: Int) {
     print "moving"
   }
   // error: `where` is a keyword and cannot be used as a name here
   ```

8. **A name is `[A-Za-z_][A-Za-z0-9_]*`.** The lexer reads the whole word and reports one problem for it, whatever the
   letters in it are, so a name written in another script is one diagnostic and not one per character.

   ```trb error
   const größe = 1
   // error: A name is written in ASCII letters, digits and `_`
   ```

9. **Text is not.** A string, a character literal, a comment and a doc comment may contain anything Unicode has - only
   names are limited, so an identifier is the same text in every editor, every terminal and every back end.

   ```trb check
   /** Grüßt zurück. 👋 */
   const greeting = "Grüße 👋"

   print greeting
   ```

## What this is not

**A block comment is not nestable, even though CONCEPT.md's own prose reads as if a keyword could label a real
member after a dot (`query.where { ... }`).** Rule 7 is why that never happens today: every declaration goes through
the same name parser, so no package - not even `std/` - has ever given a method a keyword's spelling. Only the three
contextual words reach a real name.

```trb check
const first = "a"
const second = "b"
print "{first}{second}"
```

```trb error
/* This mentions /* a comment */ that ends early. */
// error: Cannot find `that` here
// error: Expected the end of the statement, found a name
```

The second block parses as two comments and leftover code, not as one long comment - the lexer does not count nesting
depth for `/*`.

## Related

- [Doc comments](doc-comments.md) - what the text of a `/** */` comment may contain and which headings it uses.
- [Command calls](command-calls.md) - argument labels, and where a call needs no parentheses.
- [Naming](naming.md) - the other rule about a name: which case its first letter is, and what is only a convention.
- [Bindings](../values-and-types/bindings.md) - `const` and `var`, the two keywords a statement most often starts with.
