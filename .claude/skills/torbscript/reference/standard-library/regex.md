---
title: std/regex
summary: Regex, a compiled pattern with the syntax and the linear-time semantics of RE2, with whole and partial matches, named groups that decode into a type, replace and split.
kind: package
status: stable
order: 112
keywords:
  - std/regex
  - Regex
  - RegexMatch
  - RegexError
  - regular expression
  - RE2
  - named groups
  - replace
  - split
source:
  - std/regex/src/lib.trb
  - std/regex/src/regex.trb
  - std/regex/src/syntax.trb
  - std/regex/src/machine.trb
  - docs/design/TEXT-FORMATS.md
---

`std/regex` is regular expressions with the syntax and the semantics of RE2, written in TorbScript. A `Regex` is
compiled once, and matching runs in time linear in the length of the text: the engine is a Pike VM that advances every
thread one character at a time, so there is no backtracking, no backreference and no lookaround, and no pattern can
take a program down with a text it was handed. It works on characters, not bytes, and every position it answers is a
byte offset into the text, as every position of a `String` is. It passes RE2's own search tests, which are its tests.
The design is TEXT-FORMATS.md section 2.

## Import

```trb fragment
use Regex, RegexMatch, RegexError, GroupRange from "std/regex"
```

```trb check
use Regex from "std/regex"

type Date {
  year: Int
  month: Int
  day: Int
}

const pattern: Regex = "(?P<year>\d{4})-(?P<month>\d{2})-(?P<day>\d{2})"
print pattern.matches("2026-09-25")
print pattern.replace("due 2026-09-25", "$day.$month.$year")
if const Some(found) = pattern.find("released 2026-10-01") {
  print found.decoded<Date>()
}

match Regex.tryFrom("(?P<year>") {
  Ok(_) => print "compiled"
  Fail(problem) => print "{problem}"
}
```

A literal is compiled by the compiler; a pattern that is only known while the program runs is compiled by
`Regex.tryFrom(text)`, which answers what is wrong with it.

## Declarations

### Regex

```trb fragment
public type Regex with Show, Equals, TryFrom<String, RegexError> {
  static fn tryFrom(text: String): Result<Regex, RegexError>
  fn text(): String
  fn groupCount(): Int
  fn groupNames(): List<String?>
  fn matches(text: String): Bool
  fn isFound(text: String): Bool
  fn wholeMatch(text: String): RegexMatch?
  fn find(text: String): RegexMatch?
  fn findFrom(text: String, from: Int): RegexMatch?
  fn findAll(text: String): List<RegexMatch>
  fn replace(text: String, replacement: String): String
  fn replaceFirst(text: String, replacement: String): String
  fn replaceWith(text: String, transform: (found: RegexMatch) => String): String
  fn split(text: String): List<String>
}
extend String with From<Regex>
```

`Regex.tryFrom(text)` compiles a pattern, and refuses one with a `RegexError` that names the byte offset of what is
wrong: a syntax error, a backreference (`\1`, `(?P=name)`) or a lookaround (`(?=`, `(?!`, `(?<=`, `(?<!`), which RE2's
semantics do not have, a repetition above 1000, or a pattern that compiles to more than 100000 instructions. A
`Regex` is a capsule whose conversion pair is its text, so it is written and read as its pattern in every format, and
a pattern in a document that does not compile is a decoding error.

**A literal where a `Regex` is expected is compiled by the compiler**, where it is written (the
[literal rule](../language/values-and-types/checked-literals.md)): a pattern that does not compile is an error at that
line, pointing at the character it fails at, and the value is built once per program, so a literal inside a loop is
compiled once. The literal is read verbatim - no escape sequences, no interpolation - so its backslashes and braces are
the pattern's and `raw` is not needed:

```trb check
use Regex from "std/regex"

const date: Regex = "(?P<year>\d{4})-(?P<month>\d{2})"
print date.find("due 2026-09")?.named("month")
```

`matches` asks whether the whole text matches, `isFound` whether the pattern matches anywhere. `wholeMatch` and `find`
answer the match with its groups; `find` takes the leftmost match, and of the matches that start there the one the
pattern prefers - the first alternative, and a greedy or a lazy repetition as it is written (Perl's and RE2's
"leftmost first"). `findFrom` starts at a byte offset and still sees the text before it for `^` and `\b`. `findAll`
answers every match, left to right and none overlapping; an empty match right after the previous match does not count.

`replace` replaces every match by `replacement`, in which `$1` and `${1}` stand for a group by its number, `$name` and
`${name}` for a group by its name, and `$$` for a `$`; a group that took no part is empty. `replaceWith` hands every
match to a function. `split` cuts the text at every match: an empty match at the start makes no empty first piece, and
the text after the last match is the last piece - as RE2's and Go's split do it.

**The syntax is RE2's**: literals and escapes (`\n`, `\t`, `\x41`, `\x{263a}`, `\101`, `\Q...\E`), `.`, classes
(`[a-z]`, `[^...]`, `[[:alpha:]]`, `\d`, `\w`, `\s` and their negations, which are ASCII as in RE2), the Unicode
classes `\p{...}` and `\P{...}`, groups (`(...)`, `(?P<name>...)`, `(?<name>...)`, `(?:...)`), the flags `i`, `m`, `s`
and `U` as `(?i)` and `(?i:...)`, repetitions (`*`, `+`, `?`, `{n}`, `{n,}`, `{n,m}`, each lazy with `?`), alternation,
and the assertions `^`, `$`, `\A`, `\z`, `\b` and `\B`. `\C`, which matches one byte, is refused: a `Regex` matches
characters.

**Unicode**: `.` and a class match a character, not a byte. `(?i)` folds case over ASCII, Latin-1, Latin Extended-A,
Greek and Cyrillic, with the Kelvin sign, the long s, the Ångström sign, the micro sign and the final sigma. The Unicode
classes are `Any`, `L`, `Lu`, `Ll`, `N`, `Nd`, `Z`, `Zs`, `Latin`, `Greek` and `Cyrillic`; another name is refused until
the full tables of Unicode, a native of milestone 8, are there.

### RegexMatch

```trb fragment
public type RegexMatch {
  fn start(): Int
  fn end(): Int
  fn text(): String
  fn group(index: Int): String?
  fn named(name: String): String?
  fn groupRange(index: Int): GroupRange?
  fn groups(): List<String?>
  fn namedGroups(): Map<String, String>
  fn decoded<Value: Decode>(): Result<Value, DecodeError>
  fn expanded(replacement: String): String
}

public type GroupRange {
  start: Int
  end: Int
}
```

One match: where it is, as byte offsets, and what each group found - `group(0)` is the whole match, and a group that
took no part is `None`. `decoded<Value>()` reads the named groups as a value whose fields have their names, through
`Decode`: each text is read as what its field asks for, so `(?P<year>\d{4})` fills an `Int` field, and a group that
took no part is an absent field, whose default applies. `expanded` is the replacement template of `replace`, for one
match.

### RegexError

```trb fragment
public type RegexError with Show, Error {
  pattern: String
  offset: Int
  message: String
}
```

Why a pattern is refused, and the byte offset in it where that was found.

## Related

- [std/text](text.md) - `String`, whose byte offsets every position of a match is.
- [std/encoding](encoding.md) - `Decode`, which named groups decode through.
- [The standard library](index.md) - the other packages.

