---
title: torb format
summary: torb format writes TorbScript sources in the one layout of the language - the rules of the formatter canon, then indentation, spaces, blank lines and a width of 120 columns - and --check fails on every file that is not in it.
kind: tooling
status: stable
order: 120
keywords:
  - torb format
  - formatter
  - layout
  - format --check
  - indentation
  - line width
  - trailing comma
source:
  - CONCEPT.md#formatter-canon
  - compiler/src/format/command.trb
  - compiler/src/format/layout.trb
  - compiler/src/format/width.trb
  - compiler/src/format/typed.trb
---

`format` is the formatter of `torb`, milestone 8's replacement for [`torb canon`](torb-canon.md). It runs every rule
of the canon, then lays out the file: indentation, the spaces between tokens, blank lines, the end of the file, and a
width of 120 columns that it breaks long lines to and joins short ones back into. There is no option: the layout is
decided once, for every program, and a file is either in it or not.

## Synopsis

```text
torb format [--check] [path]...

  A path is a file or a directory (default: the current directory)
  --check   Write nothing, list the files that would change, leave with 1 if there is one
```

## What it does

A path is a file or a directory. Below a directory every `.trb` file is formatted except those in a hidden directory,
in a `build` directory - what a build, a test run or a docs check wrote - and in the two directories of files that are
broken on purpose (`parser-cases`, `lexer-cases`). A file that does not parse is left alone and named.

### First the canon

Every rule of [the formatter canon](torb-canon.md) runs, all five of them: a call is a command wherever the grammar
allows it and has parentheses everywhere else, a multi-line `"""` string is indented one level deeper than the line it
starts on, `.None` becomes `None` for a case a `use` imported, an unread binding of a refutable pattern becomes `_`,
and `while true {` becomes `loop {`. Each edit is applied on its own and parsed again, exactly as `torb canon` did.

### What the type checker adds

The syntax tree says where a call may be a command. It cannot say what the callee is, and one kind of callee looks like
every other although the parentheses mean something for it: a field that holds a function. `debugger.stop(event)` calls
the function in the field `stop`, and `debugger.stop event` is an error, because a command on a field would write it
(see Command calls (skill `torbscript-language`: `references/language/syntax/command-calls.md`), rule 10) - while `buffer.append(3)` of a method becomes
`buffer.append 3`.

So every file the syntax tree alone would change is type checked first, the way `torb check` checks it, and formatted
again with what the checker resolved: the rule `calls` rewrites a call only where the checker resolved its callee to
something that is no field - a method, a function, a case, or a local or a constant that holds a closure. A call of a
field keeps the way it is written wherever it stands: behind a value (`debugger.stop(event)`), in an `extend` of the
type, and inside a receiver closure. So does a call the checker did not resolve at all - a body it did not check, such
as a branch of another target, or a name after an error. A file the syntax tree leaves as it is is not checked, because
what the checker knows only ever leaves a call alone: `torb format --check` of a repository in the layout runs no check
at all.

### Then the layout

**Indentation** is two spaces per level, and it is computed, never kept:

- A line inside `(`, `[` or `{` is one level deeper than the line that opened it, however many brackets that line
  opened; a line that starts with the closing bracket is at the level of the line that opened it.
- A line that continues the one before it is one level deeper than the line its item started on, and every further
  line of the same item is at that level too. A line continues the one before where the lexer says so - it starts with
  an operator or a `.` (except an arm of a `match`), or the line before ends with one - and inside `(...)` and `[...]`
  every line the line before did not end with `,`.
- The block of an `if`, `while`, `for`, `match`, `fn` or `type` whose head runs over several lines is one level deeper
  than the head's first line, not than its last.

```trb
fn describe(items: List<Int>, limit: Int): String {
  const count = items.filter({ _ > limit }).count()
  const label = "{count} of " +
    "{items.length()}"
  if count > 10 && limit > 0
    && !items.isEmpty() {
    return "many: {label}"
  }
  label
}
```

Both breaks in there are the author's, and they stay: neither is in the shape the width breaks into (see below), so
the width leaves them alone.

**Spaces** between the tokens of a line:

| Where | Spaces |
|-------|--------|
| Around an infix operator (`+`, `==`, `&&`, `??`, the bit operators), `=` and `=>` | one on both sides |
| In front of `,` and `:` | none, and one behind them |
| Inside `(...)` and `[...]` | none |
| Inside `{ ... }` | one, and none in an empty `{}` |
| Around `.`, `?.`, `..` and `..=` | none |
| Behind a prefix `-`, `!`, `~`, `...` and the `.` of `.Case` | none |
| In front of a postfix `?` | none |
| Around the `<` and `>` of type arguments | none |
| Between a callee and its `(`, a value and the `[` of an index | none |
| In front of a comment behind code | as many as there were, at least one |

Which `<` compares and which one opens type arguments, and which `-` subtracts and which one negates, is read from the
syntax tree, not guessed from the spaces around it.

**Blank lines**: at most one in a row, none at the start of the file, none behind a line that ends with an opening
bracket and none in front of a line that starts with a closing one. The file ends with exactly one line break, and no
line ends with a space.

### Then the width

A line holds at most **120 columns**. A column is a character - a Unicode scalar value, what `String.chars()` counts -
whatever its width on a screen: `a`, `ä` and the wide `漢` are one column each, and so is a combining mark. The line
break is not counted, and neither is the carriage return of a CRLF. Characters and not the width on a screen, because
the lines that hold wide characters are strings and comments, which are never broken anyway; a count of characters is
the column every editor shows and every diagnostic of `torb` names, and it needs no table of East Asian widths that
changes with each version of Unicode.

**What breaks.** Five kinds of construct, each into one shape:

| Construct | Broken into |
|-----------|-------------|
| A bracketed list: the arguments of a call, the parameters of a function or a case, a list, map or tuple, a pattern, the type arguments and type parameters of a type | One item per line, one level deeper than the line the construct starts on, the closing bracket on a line of its own at the level of that line, a comma behind the last item |
| The arguments of a command call | Parentheses first, then the shape of a call |
| A chain of at least two method calls | A line break in front of the `.` of every call, one level deeper |
| A run of infix operators of one precedence | A line break in front of every operator, one level deeper |
| A `with` list | The `with` and every trait behind it start a line, one level deeper |

```trb
fn register(
  owner: String,
  kind: RegistrationKind,
  capacity: Int,
  description: String,
  notify: (Registration) => Void,
): Registration {
  const accepted = registrations
    .filter({ _.owner == owner && _.kind == kind })
    .sorted({ _.capacity })
    .take(capacity)
    .toList()
  if accepted.isEmpty() && description.isEmpty() && kind != RegistrationKind.Temporary
    || capacity > maximumCapacityOfTheRegistry {
    report(
      "no registration for {owner} accepted",
      severity: Severity.Warning,
      context: [owner, description, kind.show(), capacity.show()],
    )
  }
  Registration owner, kind, capacity, description
}
```

`report` was written `report "no registration...", severity: ...`: a command call that breaks gets its parentheses,
which the canon writes for a call over several lines anyway.

**Where a long line breaks.** At the outermost construct whose break makes every line that comes out of it fit -
breaking what is inside of it again where one of those lines is still too long. Constructs inside of each other are
tried from the outside in, and of two side by side the wider one first: a function's parameters before the type
arguments of its result, the run of `||` above before the `&&` inside of it, the arguments of `combine` below before
those of `transform`.

```trb
const outer = combine(
  firstArgumentOfTheOuterCall,
  transform(
    innerFirstArgumentOfTransform,
    innerSecondArgumentOfTransform,
    innerThirdArgumentOfTransform,
    innerFourthArgument,
    innerFifthArgument,
  ),
)
```

**A call hugs its only argument.** Where the only argument of a call is another call - a constructor or a case too -
or a list or map literal, the two brackets stay together on the line and only the inner list breaks, one level deeper
than that line; both closing brackets share the last line. The call has no item of its own to put on a line, so the
hug saves the two lines the outer brackets would take, as Prettier and rustfmt do:

```trb
fn check(path: String, number: Int, line: String) {
  var problems: List<DocumentationProblem> = []
  if line.startsWith(" ") {
    problems.append(DocumentationProblem(
      path,
      number,
      "An indented line in the front matter belongs to a list item and has to start with `- `",
    ))
  }
}
```

That line was `problems.append DocumentationProblem(path, number, "...")`: a command that hugs gets its parentheses.
The same holds for `Fail(Problem(` ... `))`, `Ok(`, `Some([` ... `])`, and for calls of one argument inside of each
other, which hug down to the innermost list (`Ok(Some(Pair(` ... `)))`). Where the line up to the inner bracket does not
fit, the call breaks on its own instead, with the inner call on a line of its own. A hugged call joins again like any
other broken list once it fits, and a call broken one argument per line whose argument could be hugged is hugged.

**A line no break makes fit stays exactly as it is** - not half broken:

- **A string is never broken**, and a string wider than the room it has keeps its line long. A list of nothing but one
  token - `printError "..."`, `[name]` - is never broken either: the break would only move the string one line down.
- **A comment is never moved**: a comment behind the code stays behind the last token of the line, and a line with a
  comment between its tokens is not broken at all.
- **A block is never broken and never joined.** `{ ... }` on one line stays on one line - a closure, a one-line `if` or
  `match` expression, a trailing closure - and so does everything inside of it and in the head in front of it.
- **A `use` is never broken**: its names have no brackets. Split a `use` that is too long into two of the same file.
- **A chain directly in the body of a `match`** is not broken: there a line that starts with `.` is an arm.
- **A run of `-`, `<`, `>`, `|` or `>>` breaks only inside of `(...)` and `[...]`**, because the lexer only continues a
  line that starts with an operator that needs a left side: a line that starts with `-` is a statement of its own.

**The head of a chain** is the value it starts with and the members it reads before its first call:
`self.items.filter(...)` breaks in front of `.filter`. Behind a name that starts with a capital letter - a type, a
case - the first call belongs to the head too: `File.readText(path)`, `CExpression.Call(...)`.

**What joins again.** A construct in exactly the shape the width breaks it into - with nothing inside of it but
constructs in that shape, no comment and no multi-line string - is joined into one line once that line fits, and a call
the canon then writes as a command is measured as the command, one column shorter:

```trb
const x = compute(
  first,
  second,
)
```

becomes `const x = compute first, second`. Every other line break of the author's is kept, as `gofmt` keeps them and
unlike `prettier`, which joins what fits: a call that hangs (`compute(first,` and `second)` below it), a run of
operators broken in front of some of them, a chain broken behind its first call. A line break the author chose is only
touched once its line is too long, which keeps the diffs small: a change that makes a list one item longer changes one
line, not the shape of the call around it.

**Trailing commas.** A bracketed list has a comma behind its last item exactly when its closing bracket starts a line:
the width writes one where it breaks a list, takes it away where it joins one, and does both for a list the author
broke. Every list the width breaks takes one - the parser reads every bracketed list with a trailing comma allowed, and
`(a,)` is `a`, not a tuple of one. What would not take one is never broken: the type arguments of an expression
(`decode<Config>(text)`), an index, and the arguments of a command call, which get parentheses first.

**A command call** that breaks gets its parentheses (`print(` ... `)`) rather than going on over a continuation line: a
command runs to the end of its line, so a continuation would have to be read from the commas, and the canon writes a
call over several lines with parentheses anyway. A command that writes a field of the type it stands in (`port 8080`,
"property commands") is never touched, because there the parentheses change what the call means.

### What it keeps

- **Every line break between two tokens that the width does not own.** The width breaks a line only where it is too
  long and joins only what stands in its own shape; every other line break stays where the author put it.
- **Comments**, where they are. A comment on a line of its own takes the level of the line after it; a block comment
  that runs over several lines moves with its first line, and so does its margin of `*`.
- **The inside of every string**, interpolations included: a string is one token, and only the rule `strings` of the
  canon moves the lines of a multi-line one.
- **Each file's line endings.** Every line break the layout writes is the file's first one, so a file with CRLF stays
  CRLF; the line breaks inside a string or a comment are never touched.

### The safety net

The layout of every file is checked the way the canon checks an edit: the file is parsed again, and the syntax tree that
comes out - with every span and every call style erased - has to be the tree that went in. If it is not, the layout of
that file is dropped and reported as `dropped`, because a formatter that would change what a program means is a bug.

Every file that changes is type checked once more, as it comes out. A file that type checked before and would not any
more keeps its text, and the first message of the checker is reported as `dropped` with it: the formatter never turns
code that type checks into code that does not. A file that had a problem of its own before is not held to this, since a
message may quote what the layout changed.

The whole of it - canon, layout, width, and the rule `strings` again where the layout moved a line - runs until the
text stays as it is, which is what makes it idempotent: a second run over its output changes nothing.

### Exit codes

`0` when nothing needs to change, or after writing. `--check` exits `1` when a file would change, so it composes with a
shell's `&&` and with continuous integration the same way [`check`](torb-check.md) does. Either way `1` when a safety
net dropped something. An argument `format` does not recognize prints its usage and exits `2`.

## Examples

A clean repository:

```console
$ torb format --check .
0 of 761 files would change
```

A file that is not in the layout:

```console
$ torb format --check examples/tour/src/scratch.trb
examples/tour/src/scratch.trb
1 of 1 file would change
$ torb format examples/tour/src/scratch.trb
examples/tour/src/scratch.trb
1 of 1 file changed
$ torb format --check examples/tour/src/scratch.trb
0 of 1 file would change
```

That is the tier A gate of compiler/CONTRIBUTING.md, and `sh tools/gates.sh a` runs
it over the whole repository.

## Related

- [torb canon](torb-canon.md) - the rules of the canon `format` runs first, and the command that is an alias of it now.
- [torb lint](torb-lint.md) - the other milestone 8 tool, for the rules of style that are not layout.
- Command calls (skill `torbscript-language`: `references/language/syntax/command-calls.md`) - the rule `calls`.
- Multi-line strings (skill `torbscript-language`: `references/language/syntax/multi-line-strings.md`) - the rule `strings`, and the dedent it relies on.
- [Verify your work](verifying-your-work.md) - where `format --check` sits among `check` and `test`.

