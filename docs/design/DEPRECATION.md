# Deprecation

**Status: decided and built (2026-09-28).** — CONCEPT's open question "`deprecated` (and `since`)" is answered here, and
everything section 3 decides is in the compiler: the clause in the parser, its rules and the warning at every use in the
checker, the rule `deprecated` of `torb lint` with its fix, the notice of `torb doc`, and the modifier, the tag and the
quick fix of the language server. Section 6 lists what is left.

**A 1.0 that cannot deprecate can only freeze.** RELEASE.md section 3 promises that `std` only grows within 1.x: a
member that turns out wrong is not removed, it is marked, and `torb lint --fix` moves its callers to what replaces it.
That needs something the compiler reads - a warning at the use, a rewrite a tool can apply - and not a sentence in a doc
comment that nothing checks.

```trb fragment
public type Point {
  /** The horizontal position, as it was stored. */
  deprecated("it is computed from the polar form now", replacement: "x()", since: "0.4")
  x: Int = 0
  radius: Int
  angle: Float

  /** The horizontal position. */
  fn x(): Int {
    ...
  }
}
```

```text
warning: `Point.x` is deprecated since 0.4: it is computed from the polar form now
 --> src/main.trb:12:13
   |
12 | print point.x
   |             ^
   = Write `x()` instead; `torb lint --fix --rule deprecated` rewrites it
```

- **[1. What was asked](#1-what-was-asked)**
- **[2. Where this exists already](#2-where-this-exists-already)**
- **[3. The decision](#3-the-decision)** — the clause, the rules, the warning, the fix
- **[4. The field that becomes a method](#4-the-field-that-becomes-a-method)** — the use case that settles the shape
- **[5. What each tool does with it](#5-what-each-tool-does-with-it)**
- **[6. Open](#6-open)**

---

## 1. What was asked

CONCEPT, "Open Questions", before this record:

> `deprecated` (and `since`): not documentation but something the compiler has to read. A modifier? Decide when the
> first API needs it. The use case that settles its shape: a field that becomes a method. The field stays for one
> version next to the new method, marked `deprecated` with its replacement, and `torb lint --fix` rewrites the callers
> (`.x` to `.x()`); the language server offers the same as a quick fix.

Three constraints come with the language:

- **There are no annotations.** CONCEPT's Decision Log refuses them ("No AST macros, no annotations"), and a tag
  inside a doc comment is refused as "annotations through the back door" (CONCEPT, "Doc Comments").
- **A modifier is a word in front of a declaration**: `public`, `private`, `protected`, `native`, `shared`, `static`,
  `lazy`. The one modifier that ever took an argument was `private(var)`, which `protected var` replaced.
- **The compiler reads it, so nothing of it is evaluated.** A reason, a replacement and a version are constants of the
  source, like the path of a `use`.

## 2. Where this exists already

| Language | Spelling | Reason | Replacement a tool applies | Version | Where it warns |
|---|---|---|---|---|---|
| Rust | `#[deprecated(since = "1.2", note = "...")]` | `note` | none in the attribute; clippy has its own table | `since` | every use, the crate itself included |
| Swift | `@available(*, deprecated, renamed: "x()", message: "...")` | `message` | `renamed`, which Xcode's fix-it applies | `deprecated: 5.5` per platform | every use |
| Kotlin | `@Deprecated("...", ReplaceWith("x()"), level = WARNING)` | required | `ReplaceWith`, an expression the IDE substitutes | `@SinceKotlin` beside it | every use; `level` makes it an error or hides it |
| Java | `@Deprecated(since = "9", forRemoval = true)` plus the Javadoc tag | the Javadoc | none | `since` | outside the outermost class that declares it |
| C# | `[Obsolete("...", error: false)]` | the message | none | none | every use |
| Go | a `// Deprecated: ...` paragraph in the doc comment | the paragraph | none; `gopls` and `staticcheck` read the paragraph | none | only in the tools that read it |
| Dart | `@Deprecated("...")` | required | `dart fix` reads a separate data file | none | every use |
| Zig | none: the declaration becomes a `@compileError` | the error | none | none | — |

**What they agree on:** a reason is part of the mark; a use is a warning and not an error, because the point is that the
old code keeps building; and the tools that migrate need the replacement as data. Swift and Kotlin are the two that carry
the replacement in the mark itself, and they are the two whose editors rewrite a use in one step.

## 3. The decision

**`deprecated(...)` is a clause on the line above a declaration or a member, below its doc comment: a reason, then
`replacement:` and `since:`, every one a string literal the compiler reads.**

```text
/** <doc comment> */
deprecated("<why>"[, replacement: "<name>" | "<name>()"][, since: "<major>.<minor>[.<patch>]"])
<declaration or member>
```

| Option | Example | For | Against |
|---|---|---|---|
| **A clause of its own line, above the declaration** — the decision | `deprecated("why", replacement: "x()", since: "0.4")` then `x: Int` | reads like the call it resembles; one line whatever the declaration; the declaration's own line stays what it was, so a diff that deprecates adds a line and changes none | a modifier with arguments, which the language had once (`private(var)`) and gave up; a word that is not reserved |
| A modifier in the line of the declaration | `deprecated("why") fn size(): Int` | where every other modifier stands | a long line on every deprecated member, broken by the formatter at the clause's parenthesis; the signature `torb doc` shows would carry the clause |
| A keyword without arguments, the rest in the doc comment | `deprecated fn size(): Int` and a `# Deprecated` heading | the smallest syntax | the replacement is prose a tool has to guess from; "no second syntax inside comments" (CONCEPT); a seventh heading |
| A doc comment paragraph (Go) | `Deprecated: use [length] instead.` | no syntax at all | exactly "annotations through the back door"; the compiler reads comments for nothing else |
| An annotation (Rust, Swift, Kotlin, Java) | `@deprecated(...)` | familiar | the language has no annotations and does not get them for one use |

**The rules**, each held by the parser or the checker with a message:

1. **The reason is required and comes first.** A mark that says neither why nor what instead helps nobody; a warning
   says the reason after "is deprecated", so it reads as the rest of a sentence (`the name says too little`).
2. **`replacement:` is a name of the declaration's own scope**: another member of the same type for a member, a name
   the module declares or imports otherwise. `()` behind it says the use becomes a call without arguments - a field
   that turned into a method - and is allowed only behind a function. A name that does not resolve is an error at the
   clause, so the fix `torb lint` writes cannot name nothing.
3. **`since:` is a version of two or three numbers**, the number of the release the declaration became deprecated in -
   the one number RELEASE.md section 3 gives the toolchain, the language and `std` together.
4. **The arguments are string literals without interpolation.** Nothing runs; a checked program's clause is data.
5. **It stands on a line of its own**, below the doc comment and above the declaration. A doc comment below it is an
   error that still keeps the comment.
6. **It marks what somebody names**: a `fn`, a `type`, a `trait`, a type alias, a `public const` of a module, and a
   field, a method, a constant or a case of a type, a trait, an `extend` or a `foreign` block. A `use`, an `extend` and a
   `foreign` block are no declarations anybody names, and a declaration inside a body has no caller outside it: the
   clause on one of them is an error.
7. **`deprecated` is a word only here.** It is not reserved: a field, a parameter, a label and a function may still be
   named `deprecated`, and a call `deprecated(...)` stays a call unless the next line starts a declaration.

**A use is a warning, and a warning is no problem.** `torb check` prints it after the problems and counts it in its
last line (`3 files, no problems, 2 warnings`); `torb build` prints it and builds. `torb run` and `torb test` print
nothing of it, because their output is the program's: a program that uses a deprecated declaration writes what it
writes.

**What a use is.** A name the checker resolved to the declaration: a member behind a `.`, a bare name, a case, the label
of a constructor, a `copy` or a case, a name in a type position. Three kinds of place are no use:

- **the file that declares it**, which keeps its implementation, the members that stand in for it and its examples;
- **a declaration that is deprecated itself**, whose code stays for old callers and may lean on what stays with it;
- **a pattern**, because a `match` has to name every case for as long as the case exists - warning there would warn
  about the one thing a program cannot leave out.

## 4. The field that becomes a method

A type has one namespace of members (CONCEPT, "Members"): a field and a method never share a name, because `x` would
mean two things. The use case CONCEPT names is exactly that pair, for one version: the field `x` stays for its callers
and the method `x()` replaces it. **So the one pair of members that may share a name is a deprecated field whose
replacement is `"x()"` and a method `x` without parameters.** Reading `point.x` reads the field, with the warning;
calling `point.x()` calls the method; `torb lint --fix` turns the first into the second. Every other pair is the error it
always was, including this one without the clause, and with a method that takes an argument.

The price is one sentence in the member rule, and it is paid only while the field is deprecated: removing the field in
the next major removes the exception with it.

## 5. What each tool does with it

- **`torb check`**: the errors of section 3, and a warning per use with a note that names the replacement.
- **`torb lint --rule deprecated`**: the same uses as findings, each with its fix where the replacement can stand in
  its place - the name, `x()` for a read of a field that became a method - and the `use` that imports a deprecated
  declaration renamed with it where the file does not see the replacement already. No fix where it cannot stand: a
  method as the target of an assignment or the label of a constructor, a name imported under another one.
- **`torb doc`**: the item carries `deprecation` in its JSON (`reason`, `replacement`, `since`), and its page says
  `Deprecated since 0.4: why. Write x() instead.` above the documentation.
- **`torb lsp`**: a use is a diagnostic of severity warning with the tag `Deprecated` (which an editor strikes through)
  and the fix as a quick fix; the declaration and every use carry the semantic token modifier `deprecated`; a hover
  says it above the doc comment; a completion item carries the tag.

## 6. Open

- **`since` for an addition.** Kotlin's `@SinceKotlin` and Swift's `introduced:` say since when a declaration *exists*,
  so that a package whose `language` line names an older version is told it uses something too new. The same clause
  could take it (`available(since: "1.2")`), and the checker would compare it with the `language` line of
  docs/design/PROJECT.md section 10. It is an addition and waits for the first minor release after 1.0.
- **A level.** Kotlin's `level = ERROR` makes a use an error for the last version before the removal. A `torb check
  --deny deprecated` that turns every warning into a problem is the smaller step and is not built.
- **A deprecated parameter or label.** A parameter cannot carry the clause; a renamed label is a new overload today.
