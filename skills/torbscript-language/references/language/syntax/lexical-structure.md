---
title: Lexical structure
summary: A statement ends at the end of its line, a name is ASCII while text is not, a block comment ends at its first `*/`, three keywords are never reserved, and a member may be named after a reserved one.
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

5. **A fixed list of words is reserved, and four words never are.** `const`, `var`, `fn`, `type`, `trait`, `extend`,
   `foreign`, `case`, `use`, `public`, `private`, `native`, `shared`, `lazy`, `if`, `else`, `match`, `for`, `in`,
   `while`, `break`, `continue`, `return`, `true`, `false`, `void`, `self`, `Self`, `with` and `where` are reserved.
   `from`, `as`, `by` and `protected` are contextual: the lexer reads them as ordinary names everywhere, which is what
   `use Name from "./module"`, `type Seconds with Compare by value` and `protected var count: Int` rely on.
   `protected` is a word only in front of a member of a type, so a field named `protected` is reached bare as well as
   after a `.` ([Fields](../types/fields.md), rule 3).

   `raw` is contextual too, but at the lexer rather than the parser: it starts a raw string (`raw"text"`,
   `raw"""text"""`) only directly in front of `"`, and is an ordinary name everywhere else - `const raw = 1` and
   `fn raw(): Int` both declare a `raw`. See [Literals](literals.md), rule 9.

6. **A field, a method, a case field and a label may be named after a reserved word.** Where a name can only be a
   name, a keyword is one: declared inside a type body (`type: String`, `var in: Int`, `fn match(...)`, a case field
   `case Click(type: String)`), after a `.` (`event.type`, `event.match("click")`) and as a label of an argument or a
   pattern (`Event(type: "click")`, `.Click(type: kind)`). A bare keyword is always the keyword, so inside the type the
   member is reached as `self.type` - in a method and in a receiver closure alike. Derived `Encode` and `Decode` use the
   field's name as the key, so a JSON `"type"` needs no rename.

   ```trb check
   type Event {
     type: String
     var in: Int = 0

     fn match(pattern: String): Bool {
       self.type == pattern
     }
   }

   const event = Event type: "click"
   print "{event.type} {event.in} {event.match("click")}"
   ```

   ```trb error
   type Event {
     var type: String
   }

   fn configure(block: (var self: Event) => Void): Event {
     var event = Event(type: "")
     block event
     event
   }

   print configure({ type = "click" }).type
   // error: `type` is a keyword and cannot be assigned bare
   ```

   A receiver closure writes `self.type = "click"`: its bare names are the receiver's fields, but a bare `type` at the
   start of a line begins a declaration there as everywhere else.

7. **A parameter, a binding, a function and a type cannot be named after a keyword.** A parameter has no `self.` to
   reach it through, so a keyword there would be a name the body can never read. The message names the participle a
   reader is looking for. A label only reaches a parameter that exists, so `accept(in: 3)` is a message about the
   parameter, not about the keyword.

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

**A block comment is not nestable.** Text that looks like a nested comment ends the outer one at its first `*/`.

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

