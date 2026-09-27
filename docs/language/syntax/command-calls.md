---
title: Command calls
summary: A call is written without parentheses wherever the grammar allows it, and with parentheses everywhere else. This is the formatter canon and it is enforced, not preferred.
kind: reference
status: stable
order: 20
keywords:
  - command call
  - parentheses
  - formatter canon
  - property command
  - trailing closure
source:
  - CONCEPT.md#command-calls-calls-without-parentheses
  - CONCEPT.md#formatter-canon
  - compiler/src/canon/calls.trb
---

Both `print("hello")` and `print "hello"` parse. Which one to write is decided rather than left open: a call is a command
wherever the grammar allows it. `torb format --check` reports every file that disagrees, so this is the shape of every
snippet in this documentation and of every line in the repository.

## Example

```trb
fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Fail "negative"
  }
  value
}

const role = "admin"
const names = ["ada", "alan"]
print "checked: {checked(1)}"
print "role: {role}, names: {names.length()}"
```

## Syntax

```text
<callee> <argument>, <argument>              a command call
<callee>(<argument>, <argument>)             a parenthesized call
<callee> <argument> { ... }                  a command call with a trailing closure
<callee> { ... }                             a call whose only argument is a trailing closure
```

A callee is a name or a member path: `print`, `Ok`, `Email.tryFrom`, `server.route`, `self.builder.add`.

## Rules

A call is written as a command when **all five** of these hold. Otherwise it has parentheses.

1. **It stands in command position**: at the start of a statement, on the right of `=` in a binding or an assignment,
   after `return`, after `=>`, or as the default of a field of a type.
2. **The callee is a name or a member path.** An index in the middle of one is part of it, because a member path with
   an index in it is still a member path: `mounted[index].process elapsed` is the command form of
   `mounted[index].process(elapsed)`, and so is `grid[row][column].fill value`. `a?.b` is not a member path, and
   neither is `pair.0` or `load<Config>`.

   ```trb
   type Cell {
     var value: Int

     var fn fill(amount: Int) {
       value = amount
     }
   }

   var grid = [[Cell(0)]]
   grid[0][0].fill 5
   print grid
   ```

   An index the path **ends** in is not a callee: `mounted[index] elapsed` is the indexing of rule 3 and not a
   command, exactly as `f [1]` is.
3. **It has at least one argument, and the first one does not start with `(`, `[`, `-`, `!` or `.`.** `f [1]` is always
   indexing, `f -1` is always subtraction, and `f .Case` is always the member `f.Case`. Where `f` is a function, which
   is never subtracted from, indexed or asked for a member, the message is the call to write:

   ```trb error
   var history: List<Int> = []
   const amount = 3
   history.append -amount
   print history
   // error: `history.append` is a function, and `history.append -amount` subtracts from it
   ```
4. **No argument has an operator at its top level.** `assert sum == 3` would read as `(assert sum) == 3`, so it is written
   `assert(sum == 3)`.
5. **The arguments are on one line.** A trailing closure may go over several.

Then the rest of the rules:

6. **A call without arguments always has parentheses.** A bare name is a reference to the function, not a call of it.

   ```trb
   const list = [1, 2, 3]
   print list.length()
   ```

7. **Commands do not nest.** The arguments of a command are ordinary expressions, so a nested call keeps its parentheses:
   `Ok Some(x)`, never `Ok Some x`. `print describe(numbers)`, never `print describe numbers`.

8. **The head of an `if`, `for`, `while` or `match` is not command position.** There the `{` is the body, so a call in the
   head has parentheses: `if ready(now) { }`.

9. **A trailing closure belongs to the outermost command call of the statement.** `unless list.isEmpty() { ... }` passes
   the closure to `unless`. Consequently the arguments of a command call cannot contain a trailing closure themselves:
   `print numbers.map { _ * 2 }` is an error, and the fix is `print numbers.map({ _ * 2 })`.

   ```trb
   const numbers = [1, 2, 3]
   print numbers.map({ _ * 2 }).toList()
   ```

10. **A field is written only with `=`, never with a command.** `port 8080` is a compile error, and the fix is
    `port = 8080` - this holds everywhere, including inside a receiver closure. The one property command left is a
    trailing block on a field whose value is a *record* type, never a function: `database { ... }` applies the block
    to the value the field already holds, configuring it in place rather than replacing it. Calling a function held in
    a field always needs parentheses: `onStart()` calls it, `onStart = { ... }` assigns it.

11. **Parentheses always call, and never write a field.** `tls(true)` is an error, because a `Bool` field has nothing
    to call - the fix is `tls = true`.

    ```trb error
    type Options {
      var tls: Bool = false
    }

    var options = Options()
    options.tls(true)
    print options.tls
    // error: `tls` is a field, and parentheses call a function
    ```

12. **A multi-line `"""` string is indented two spaces deeper than the line its statement starts on**, with a closing
    `"""` that stands alone aligned with the content. The value does not depend on it: the lexer subtracts the indentation
    of the first content line from every line.

    ```trb
    fn generated(): String {
      const header = """
        #include <stdint.h>

        static int64_t counted(void)
        """
      header
    }

    print generated()
    ```

13. **Statements end at the end of the line, and there are no semicolons.** A statement continues on the next line when
    the line ends with an operator, a comma or an open bracket, or when the next line starts with `.`, `?.`, a binary
    operator, `with` or `where`.

## What this is not

**It is not a style option.** `torb format` writes both rules over the syntax tree and `torb format --check` reports the
files that are not in the canon. The repository is in it, and every `trb` block of this documentation is checked against
it.

```trb
const role = "admin"
print "hello, {role}"
```

```trb skip a parenthesized call in command position parses, so the canon can only be shown as prose here
print("hello")
```

The second form parses and means the same thing. It is still wrong, because `torb format` rewrites it and the check fails
until it is rewritten.

**A command is not a new call syntax with different semantics.** `f a, b` and `f(a, b)` produce the same syntax tree apart
from a style flag. The only place where the parentheses change the meaning is a trailing block on a record field, which
is rule 10.

**Whitespace never decides anything.** The rules above are about tokens, not about spaces. `f -1` is subtraction because
`-` cannot start a command argument, not because of where the space is.

**A command call is not allowed inside parentheses, brackets, an operator or an argument list.** Only the five positions
of rule 1 are command position, which is what keeps `a b { }` from being ambiguous.

```trb error
const value = (print "hello")
// error: Expected `)`, found a string
```

## Related

- [Bindings](../values-and-types/bindings.md) - the right side of `=` is command position.
- [Declaring a type](../types/declaring-a-type.md) - property commands and the one member namespace.
- [Result](../errors/result.md) - why `Ok value` and `return Fail problem` are the canonical forms.
- [Verify your work](../../tooling/verifying-your-work.md) - the command that checks the canon.
- [What a model trained on other languages gets wrong](../../explanation/mistakes-models-make.md) - the canon is the
  mistake a model makes most often.
