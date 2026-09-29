# Closed Traits: Every Case Is a Type

**Status: accepted** — phases 1a, 1b and 2 to build; 3 deferred. The owner decided on 2026-09-28 that there is no new
keyword, because a `type` with `case` members *is* the closed trait (section 1.1), and that a case is written bare
wherever the expected type names it, so the leading dot goes (section 6); on 2026-09-29 the owner answered the
questions of section 10, and every one of them is a decision now. Nothing of this record is implemented yet.

The owner asked why a case cannot be bare and why it needs its leading dot, and then: "Why not treat cases like types,
basically, and the type above them like a trait? Like a **closed** trait, of course." This record works that out: what
a case is once it is a type, what the type that holds the cases is once it is a closed trait, how both are laid out
without costing anything, how a case is named and matched, and what it takes to get there from today's `.Case`.

```text
  type Shape {                           the closed trait `Shape`: its implementors are exactly its cases,
    case Circle(radius: Float)     ──►     the type `Shape.Circle`     { radius: Float }
    case Rectangle(width: Float,   ──►     the type `Shape.Rectangle`  { width: Float, height: Float }
                   height: Float)
    case Empty                     ──►     the type `Shape.Empty`      { }
  }

  a `Shape`          one of the three: a tag and a payload, inline - no box and no witness table, as today
  a `Shape.Circle`   a record; where a `Shape` is expected it becomes one, in O(1)
  `Circle 1.0`       where a `Shape` is expected: no `Shape.` and no `.` in front of it (section 6)
```

- **[0. The question, and where the others are](#0-the-question-and-where-the-others-are)** — ten languages, one table per group of dimensions
- **[1. Syntax](#1-syntax)** — no keyword, cases nested only, case bodies, how a case type is named
- **[2. Representation](#2-representation)** — the tagged union stays, a case type erases to it, dispatch by tag
- **[3. Conversions and subtyping](#3-conversions-and-subtyping)** — one conversion, no variance, construction widens
- **[4. Matching](#4-matching)** — `as` binds the whole case, exhaustiveness over the cases, narrowing later
- **[5. Methods](#5-methods)** — one answer whichever type a value is seen through
- **[6. The leading dot](#6-the-leading-dot)** — contextual cases, and the rule that keeps a meaning from changing silently
- **[7. The prelude and std](#7-the-prelude-and-std)** — `Option`, `Result`, the error types, encoding, and why a
  public type with cases never grows
- **[8. Tooling](#8-tooling)** — checker, IR, both back ends, the VM, the language server, `torb doc`, formatter, linter
- **[9. Migration and phases](#9-migration-and-phases)** — what changes meaning, the two commits, the sizes
- **[10. Risks and the answers of the owner](#10-risks-and-the-answers-of-the-owner)**

The examples below already use the bare form of section 6 (`Circle 1.0`, `Empty =>`), because that is what the
language will read like. What today's compiler does was probed with the `build/release/torb` of 2026-09-28; where the
record says "today", it was run.

---

## 0. The question, and where the others are

### 0.1 What changes, and what it supersedes

Today a case is a constructor of its type and nothing else: `Shape.Circle(1.0)` has the type `Shape`, `Shape.Circle`
is not a type, and a function that only takes circles cannot say so. The Decision Log of CONCEPT refused exactly this
record's direction once:

> A `closed trait` (implementable only in its own package, so a `match` over its types is exhaustive) is the honest
> form of "cases with an identity" and is not planned either. Cases are not types: `Circle(1.0)` having the type
> `Circle` would need a join of `Circle` and `Rectangle` in inference, which is the subtyping the language does not
> have.

The objection is real, and section 3.4 answers it the way Scala 3 does: **a construction has the type of the closed
trait unless the expected type asks for the case**, so `[Shape.Circle(1.0), Shape.Rectangle(1.0, 2.0)]` is a
`List<Shape>` without any join, and every expression that type checks today keeps its type. Accepting this record
replaces these entries of CONCEPT and the pages that repeat them:

| Where | What it says today | What replaces it |
|---|---|---|
| Decision Log, "A type has one form, the block" | cases are not types, a closed trait is not planned | sections 1 and 3 |
| Decision Log, "Cases are `Type.Case` or `.Case`, never bare (Swift)" | a case needs its type or a dot | section 6 |
| Decision Log, "In a pattern the first letter decides" | what a pattern name means "hangs on the `use` at the top of the file, never on the expected type" | the first letter still decides; the expected type now counts too (section 6.3) |
| "Conversions" and "Traits" (the five coercions) | a value becomes a trait value where one is expected | the same coercion, now also into a closed trait (section 3.1) |
| `docs/language/pattern-matching/cases-and-match.md` rules 1, 2, 5 and 10, "A case with fields is not a class" | the dot, the arm rule, no methods per case | sections 4, 5 and 6 |
| TYPECHECKER 2.1 and 2.7, the rows of `.Case` | the dot needs an expected type | the bare name does (section 6.2) |
| RELEASE.md section 2, "A way for a public enum to grow" | open for 1.0 | resolved: a policy that such a type never grows, the newtype pattern where growth is needed (decision 7.3); RELEASE.md says so since this record was accepted |

### 0.2 Where the others are

Ten designs, one table per group of dimensions. Every cell comes from the language's own documentation or its design
discussion; the sources are listed after the tables.

**What a case is.**

| Language | Is a case a type? | Where the cases live | Representation |
|---|---|---|---|
| Kotlin (sealed classes and interfaces) | yes: a class or an `object` | the same module and package, nested or top-level; no `permits` list | heap objects; a `value class` is boxed when used as the sealed type |
| Scala 3 (`enum`, `sealed trait`) | class cases yes (`Option.Some[Int]`); singleton cases are values | an `enum`'s cases only in its body; a `sealed` trait's direct subclasses in the same file | JVM objects |
| Java 21 (sealed interfaces, records) | yes | a `permits` list, or the same file; the same module (or package) | heap objects |
| Rust (`enum`) | no: "that type cannot be used as a type specifier"; RFC 2593 proposed it | the `enum` item | inline tagged union; the null-pointer optimization for `Option<&T>` and the like |
| Swift (`enum`) | no: a case behaves like a `static` member answering `Self` | the enum declaration; an extension cannot add a case | inline value with spare bits and extra inhabitants; `indirect` boxes |
| TypeScript (discriminated unions) | yes: each member is its own type | the union alias; the members are structural and live anywhere | plain objects with a discriminant property |
| OCaml (variants, polymorphic variants) | ordinary: no; a polymorphic variant tag: in effect yes | one `type` declaration; extensible variants are open | a constant constructor is an immediate integer, the others heap blocks tagged with the constructor number |
| Gleam (custom types) | no: "Variants are not types, they are values" | the `type` definition | tagged tuples on Erlang, classes on JavaScript |
| Roc (tag unions) | a tag no; a one-tag structural union is a type | structural unions need no declaration; a nominal `Color := [...]` is closed | payloads "optimize to the same thing as tuples", with a discriminant |
| Dart 3 (sealed classes) | yes: subclasses | the same library | objects |

**Conversion, variance and a mixed literal.**

| Language | A case value where the parent is expected | `List<Circle>` as `List<Shape>` | `[Circle(1.0), Square(2.0)]` with nothing expected |
|---|---|---|---|
| Kotlin | subtyping | yes for the read-only `List`, no for `MutableList` | the least upper bound: `List<Shape>` |
| Scala 3 | subtyping | yes, `List` is covariant | an `enum` case construction "will be widened to the underlying enum type, unless a more specific type is expected"; case classes of a `sealed trait` join to `List[Shape]` |
| Java | subtyping | no: "T <: S does not imply that C<T> <: C<S>" | an intersection: `List<Record & Shape>` |
| Rust | the constructor produces the enum | - | `Vec<Shape>`; RFC 2593: "variant types are never inferred for values" |
| Swift | the constructor produces the enum | the standard collections are covariant, a user's generics are not | `[Shape]` for enum cases; two structs of one protocol: "heterogeneous collection literal could only be inferred to '[Any]'" |
| TypeScript | assignable | yes, arrays are covariant, knowingly unsound | the best common type, else the union `(Circle \| Square)[]` |
| OCaml | the constructor produces the type; a polymorphic variant needs `:>` | - | `shape list` |
| Gleam | the constructor produces the type | - | `List(Shape)` |
| Roc | the rows unify | - | the union of the tags |
| Dart | subtyping | yes, covariant and checked at run time | `UP`: `List<Shape>` where `Shape` is the one candidate at its depth, `Object` otherwise |

**Matching and members.**

| Language | The whole case, bound as its type | Exhaustiveness | Narrowing | Members per case |
|---|---|---|---|---|
| Kotlin | `is Circle`, then a smart cast | over the direct subtypes, recursing into nested sealed ones | smart casts on stable values; not on a `var` property | abstract and default members on the parent; each subclass its own interfaces |
| Scala 3 | `case c: Circle =>`, `c @ Circle(r)` | a warning, "match may not be exhaustive" | none on type tests | the enum body's members for all cases; a case may extend further traits |
| Java | `case Circle c ->` | over `permits`, through nested record patterns | `instanceof` binds a new name; the variable keeps its type | interface defaults; each record its own interfaces |
| Rust | `x @ Shape::Circle { .. }`, typed as `Shape`; RFC 2593 typed it as `Sum::A` | exhaustive `match` | none | only on the enum |
| Swift | none: the payload only | exhaustive `switch` | none | on the enum; a conformance per enum, not per case |
| TypeScript | the narrowed variable | the `never` idiom | control-flow narrowing | on the member types |
| OCaml | `as`, typed as the whole type; a GADT refines inside a branch | warning 8, on by default | GADTs, inside a branch | functions only |
| Gleam | `as`; since 1.6 the compiler tracks the variant | an error | variant inference since 1.6 | functions only |
| Roc | `as` | an open union needs `_` | not documented | methods on nominal types |
| Dart | `Circle c`, an object pattern | nested sealed types are expanded recursively | promotion after `is` | class members; interfaces per subclass |

**Naming and growth.**

| Language | Qualified | Bare | By the expected type | Growth |
|---|---|---|---|---|
| Kotlin | `Shape.Circle` | through an import | 2.2: bare, at the **lowest** priority, below even the default imports; objects and properties only, no calls; 2.3 warns where it and ordinary resolution disagree; stable planned for 2.6 | none: a new subclass breaks every `when` |
| Scala 3 | `Color.Red` | `import Color.*`, and inside the enum's own body | none; a `.Red` pull request was closed in 2026, and Odersky proposed a bare lookup "if all other schemes have failed" | none: client matches warn |
| Java | `case Coin.HEADS` in general | enum constants in `switch` labels, since Java 5 | only those labels | a new permitted class: old switches throw `MatchException` |
| Rust | `Shape::Circle` | `use Shape::*`, where a misspelled variant becomes a binding (a deny-by-default lint) | RFC 3444 proposes `.Variant`, open | `#[non_exhaustive]`, outside the crate only |
| Swift | `Shape.circle` | removed inside enums by SE-0036, "to clearly disambiguate static cases from instance members" | the leading dot, looking through `Optional`; where both have a `.none`, `Optional.none` wins, with a warning since 5.1 | `@unknown default`; SE-0487 `@nonexhaustive` |
| TypeScript | - | string discriminants | contextual typing of literals | a new member breaks the `never` checks |
| OCaml | `M.C` | when in scope | type-directed disambiguation; warnings 40, 41 and 42 report where it chose; without a type the "last defined type" wins | extensible variants need a default case |
| Gleam | `shape.Circle`, through the module | an unqualified import | none: "Types are not namespaces, modules are" | a new variant breaks |
| Roc | `Color.Red` | structural tags | a structural tag stands for the nominal one where that is expected | open structural unions grow |
| Dart | `Shape.circle` | - | 3.10: the dot shorthand `.circle`, one namespace, a sealed class's subtypes excluded; a bare name was weighed and called "a little more dangerous because it might happen by accident" | a new subtype breaks; `final` instead of `sealed` stays open |

**What the tables teach:**

1. **Case types that erase to the enum, carry all of its type parameters, convert but do not subtype, are never
   inferred for a construction and are what an `@`-binding gets are Rust's RFC 2593, almost to the word.** It stalled
   on Rust's roadmap, on bandwidth and on Rust's general question of type fallback - "there's a need for it", the
   closing comment conceded - and not on a flaw of the conservative design. Scala 3 ships the same inference rule for
   its enums. Sections 2 and 3 take both.
2. **Where cases are types by way of subtyping, variance and joins come with them**: covariant lists in Kotlin, Scala
   and Dart, Java's `List<Record & Shape>`, TypeScript's knowingly unsound arrays. A language without subtyping takes
   the case type and leaves the rest.
3. **Bare contextual names are where the field is going.** Kotlin 2.2 chose the unprefixed form over Swift's dot -
   `foo` on one line and `.bar` on the next is already a member access, and a dot makes resolution harder - Odersky
   proposed the same for Scala, Roc and OCaml have it, and Java has had it in `switch` labels for twenty years. Dart and
   Swift kept the dot; Swift took bare names out of enum bodies to tell cases from instance members, which the
   first-letter rule already does here.
4. **Where the scope and the expected type can both answer, a silent change has followed.** A star import of an
   unrelated `Child2` changed what Kotlin's `is Child2` meant, and 2.3 added a warning for that ambiguity; OCaml's
   manual calls its scope-based fallback "a quite unstable position that may change surreptitiously after adding or
   moving around a type definition, or after opening a module". Section 6.2 makes that ambiguity an error from the
   start.
5. **Growth always needs a mechanism of its own** - `#[non_exhaustive]`, `@unknown default`, `final` instead of
   `sealed` - and nobody gets it from the closed set itself (section 7.3).

**Sources.** Kotlin: [sealed classes](https://kotlinlang.org/docs/sealed-classes.html),
[smart casts](https://kotlinlang.org/docs/typecasts.html),
[KEEP-379](https://github.com/Kotlin/KEEP/blob/main/proposals/KEEP-0379-context-sensitive-resolution.md) and its
discussion ([priority and compatibility](https://github.com/Kotlin/KEEP/issues/379#issuecomment-2175797622),
[no leading dot](https://github.com/Kotlin/KEEP/issues/379#issuecomment-2199600884)),
[KT-16768](https://youtrack.jetbrains.com/issue/KT-16768), [KT-77821](https://youtrack.jetbrains.com/issue/KT-77821),
[what is new in 2.3](https://kotlinlang.org/docs/whatsnew23.html). Scala 3:
[enum desugaring](https://docs.scala-lang.org/scala3/reference/enums/desugarEnums.html),
[ADTs](https://docs.scala-lang.org/scala3/reference/enums/adts.html),
[the `.Red` pull request](https://github.com/scala/scala3/pull/23801). Java: [JEP 409](https://openjdk.org/jeps/409),
[JEP 440](https://openjdk.org/jeps/440), [JEP 441](https://openjdk.org/jeps/441),
[JLS 4.10](https://docs.oracle.com/javase/specs/jls/se21/html/jls-4.html#jls-4.10.4). Rust:
[enumerations](https://doc.rust-lang.org/reference/items/enumerations.html),
[RFC 2593](https://github.com/rust-lang/rfcs/pull/2593), [RFC 1450](https://github.com/rust-lang/rfcs/pull/1450),
[RFC 3444](https://github.com/rust-lang/rfcs/pull/3444),
[`non_exhaustive`](https://doc.rust-lang.org/reference/attributes/type_system.html). Swift:
[implicit member expressions](https://github.com/swiftlang/swift-book/blob/main/TSPL.docc/ReferenceManual/Expressions.md#implicit-member-expression),
[SE-0036](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0036-enum-dot.md),
[SE-0192](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0192-non-exhaustive-enums.md),
[SE-0287](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0287-implicit-member-chains.md),
[SE-0487](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0487-extensible-enums.md),
[type layout](https://github.com/swiftlang/swift/blob/main/docs/ABI/TypeLayout.rst). TypeScript:
[narrowing](https://www.typescriptlang.org/docs/handbook/2/narrowing.html),
[type inference](https://www.typescriptlang.org/docs/handbook/type-inference.html),
[FAQ](https://github.com/microsoft/TypeScript/wiki/FAQ). OCaml:
[polymorphic variants](https://ocaml.org/manual/5.5/polyvariant.html),
[the C interface](https://ocaml.org/manual/5.5/intfc.html),
[disambiguation](https://ocaml.org/manual/5.5/coreexamples.html),
[warnings](https://ocaml.org/manual/5.5/comp.html). Gleam:
[custom types](https://tour.gleam.run/data-types/custom-types/),
[discussion 4098](https://github.com/gleam-lang/gleam/discussions/4098),
[discussion 3512](https://github.com/gleam-lang/gleam/discussions/3512),
[context-aware compilation](https://gleam.run/news/context-aware-compilation/). Roc:
[tag unions](https://github.com/roc-lang/roc/blob/main/docs/langref/tag-unions.md). Dart:
[class modifiers](https://dart.dev/language/class-modifiers),
[dot shorthands](https://github.com/dart-lang/language/blob/main/accepted/3.10/dot-shorthands/feature-specification.md),
[issue 357](https://github.com/dart-lang/language/issues/357),
[issue 4149](https://github.com/dart-lang/language/issues/4149),
[upper and lower bounds](https://github.com/dart-lang/language/blob/main/resources/type-system/upper-lower-bounds.md).

---

## 1. Syntax

### Decision 1.1 — no keyword: a type with cases is the closed trait (the owner's)

| Option | How `Shape` is declared | For | Against |
|---|---|---|---|
| `closed trait Shape { ... }`, implementors `type Circle with Shape` | Kotlin's `sealed interface`, Java's `sealed interface ... permits` | the kind is written | a second declaration form for what `type` already does; a new keyword; every existing `type` with cases would have to be rewritten or kept as a second spelling |
| `sealed trait` | Scala 2 and 3 | familiar to Scala and Kotlin readers | the same, and "sealed" is jargon for what the language calls closed |
| `enum Shape { ... }` | Swift, Rust, Scala 3's `enum` | the word everybody knows for short enums | a second keyword for data, against "one keyword declares every data type" (CONCEPT, Key Facts) |
| **none: a `type` whose body has `case` members is the closed trait** | what the language writes today | no new word, no second spelling, every existing declaration keeps its meaning | the kind of a type follows from its members - which it already does today |

**Chosen: none (owner, 2026-09-28).** `type Shape { case Circle(radius: Float) }` declares the closed trait `Shape`
and the case type `Shape.Circle`. A reader tells a closed trait from a record the way it is told today: by the `case`
lines. **The reference and every diagnostic keep the words they have - "a type with cases"** ("`Shape` is a type with
cases: take a `Shape`"), and "closed trait" is the word for the model, used where it is explained: here, in
`explanation/` and on the page about case types (the owner, 2026-09-29). A `trait` declaration is open by definition,
so a message that called `Shape` a trait would send a reader looking for a `trait Shape` that does not exist.

### Decision 1.2 — a case declares a nested type

A `case` line declares three things at once, and all three have the one name:

1. **the type `Shape.Circle`**, whose fields are the ones in the parentheses, behind the common fields of `Shape` if it
   has any (a type may carry fields of its own next to its cases: the checker accepts that today, and the native back
   end refuses to build it yet - "... of a type that carries fields of its own next to its cases is not supported by
   the native back end yet");
2. **its constructor**, `Shape.Circle(radius: 1.0)`, exactly the one the case has today;
3. **that `Shape.Circle` implements `Shape`**, and that nothing else ever does.

The case type has the visibility of `Shape`, as a case has today. Its fields are `const`, as the fields of a case are
today; a `var` field in a case stays an error.

### Decision 1.3 — cases are nested, and only nested

Where may an implementor of a closed trait live?

| Option | Who does it | For | Against |
|---|---|---|---|
| Nested in the declaration only | Scala 3 `enum`, Swift, Rust, OCaml, Gleam, Roc | the whole set is one declaration: exhaustiveness, the layout and the tag space are decided from it alone | a type cannot be a case of two closed traits |
| The same file | Scala's `sealed trait`, Java without modules | one place to read | a closed trait made of free-standing types needs a marker the owner ruled out |
| The same package | Kotlin (same module and package), Java with modules, Dart (same library) | large cases in files of their own | the kind and the implementor set of `Shape` follow from declarations elsewhere in the package |
| A `permits` list on the closed trait | Java | explicit | a list that repeats names, and a keyword |

**Chosen: nested only. A free-standing implementor (`type Circle with Shape { radius: Float }`) is not added, and the
phase that would have added it (phase 2 of the owner's sketch) is replaced by case bodies and nested cases (decisions
1.4 and 1.5).** Why:

- **Without a keyword, the parent could not be recognised.** A closed trait whose implementors were all free-standing
  would be written `type Shape {}` - which is a record with no fields today. What `Shape` is would then follow from a
  `type ... with Shape` somewhere else in the package, which is the one thing a keyword-free design cannot afford.
- **It is the rule CONCEPT already has for cases.** "A field or a `case` in an `extend` is an error ('fields and cases
  belong to the declaration of the type'), because exhaustiveness and the generated constructor have to be decidable
  from the declaration alone." A free-standing implementor is a case added from outside the declaration.
- **What free-standing implementors are usually for, case types already give.** The reason code writes
  `case Binary(node: BinaryExpression)` with a separate `BinaryExpression` is to pass one case to a function on its
  own. Once `Expression.Binary` is a type, `fn checkBinary(node: Expression.Binary)` is written directly.
- **What is lost is one thing: a type that is a case of two closed traits** (a `Literal` that is an `Expression` and a
  `Pattern`). The way to write it stays what it is today, a case that wraps the shared type in each of the two
  (`case Literal(literal: Literal)`), inline and with the generated `From`, so `Expression.from(literal)` converts at
  no cost. Should that turn out to be common, a free-standing form can be added later without breaking anything; the
  reverse would not be true.

### Decision 1.4 — a case may have a body (phase 2; accepted by the owner)

A case's fields stay in its parentheses; what it has besides them goes into a body, which holds what the body of a
`type` holds except fields:

```trb
type Shape {
  case Circle(radius: Float) with Hash {
    fn diameter(): Float {
      radius * 2.0
    }
  }
  case Rectangle(width: Float, height: Float)
  case Empty
}
```

- **The fields of a case are in its head, and only there.** A field in a case body is an error that names the head:
  "The fields of a case are written in its parentheses: `case Circle(radius: Float, label: String)`". One form, as
  "a type has one form" asks.
- **The body may hold** methods, `var fn`s, `static` members, the implementations of the closed trait's requirements
  (section 5.2), and a `with` list with the traits the case implements on its own (section 5.4).
- **Until phase 2, `extend Shape.Circle { ... }` does the same** (phase 1b), in the package that declares `Shape`. The
  body is the declaration form of what that `extend` can already do, not a new capability.

### Decision 1.5 — a case may hold cases (phase 2; accepted by the owner)

```trb
type Shape {
  case Round {
    case Circle(radius: Float)
    case Ellipse(width: Float, height: Float)
  }
  case Polygon(corners: List<Point>)
}
```

`Shape.Round` is a case of `Shape` and itself a closed trait, with the case types `Shape.Round.Circle` and
`Shape.Round.Ellipse`. A `match` over a `Shape` may list the leaves (`Circle`, `Ellipse`, `Polygon`) or take the group
whole (`Round(...) as round`, section 4.2). **A group has no fields of its own** in phase 2, and **two cases of one name
anywhere in one tree are an error at the second declaration**, so a case name is unique in the whole closed trait and
section 6 can resolve a leaf through its groups.

### Decision 1.6 — how a case type is named

| Where | Written |
|---|---|
| A type position | `Shape.Circle`; or `Circle` where the file wrote `use Shape.Circle` |
| A generic closed trait | `Result.Fail<Int, IoError>`, `Option.Some<String>`: **the case type takes exactly the type parameters of its closed trait**, in their order, whether its fields use them or not |
| A nested case | `Shape.Round.Circle` |
| An import | `use Shape.Circle` imports the case, its constructor and its type: they are one declaration |

**Why the arguments go behind the whole path** (`Result.Fail<Int, IoError>` and not `Result<Int, IoError>.Fail`): a
written type is a path plus one argument list (`TypeKind.Named(path, arguments)`), and that grammar already reads it.
**Why every parameter of the parent** and not only the ones the case uses (Scala 3 gives `case Fail[E](e: E) extends
Result[Nothing, E]` its own): the other way needs variance and a bottom type in arguments, which the language does not
have ("There is no variance"). A `Result.Fail<Int, IoError>` and a `Result.Fail<String, IoError>` are different types,
as the `Result`s they belong to are.

---

## 2. Representation

The performance goal is low-level speed with zero-cost abstractions (docs/PERFORMANCE.md). A closed trait has the
property an open trait lacks - the compiler knows every implementor when it computes the layout - and the
representation takes all of it.

### 2.1 A value of a closed trait: the tagged union, unchanged

A `Shape` is laid out as a type with cases is laid out today, decided once in the IR (BACKEND 1.3) and executed by both
back ends:

| Representation class (the IR's) | C | VM (VM.md section 2) |
|---|---|---|
| `Inline`: at most 32 bytes estimated, not recursive | `struct { uint32_t tag; union { struct {...} v0; ... } payload; }`, by value | the common fields, the tag word, then the largest group |
| `Boxed`: bigger, or on a cycle of the field graph (a tree) | a pointer to a counted block `{ header; tag; payload }` | one word, the pointer |
| `Niche`: `Option<P>` whose `Some` holds one pointer-like field | the pointer, `NULL` for `None` | the payload's words, a zero first word for `None` |

Nothing about a closed trait makes it bigger or adds an indirection: **it is the same bytes a `Shape` is today.**

### Decision 2.2 — a value of a case type erases to its closed trait

| Option | A `Shape.Circle` is | Widening to `Shape` | Narrowing in a `match` | `List<Shape.Circle>` element |
|---|---|---|---|---|
| A record of its own group (common fields plus its fields) | `struct { double radius; }` | build the union: store the tag, copy the group (a few moves) | copy the group out | the size of a circle |
| **The layout of `Shape`, with a tag the checker knows** | the union's bytes, tag `Circle` | nothing: the value already is one | nothing | the size of a `Shape` |
| Boxed on its own | a pointer | allocate and copy | allocate and copy | a pointer |

**Chosen: erased, for every representation class, in phase 1b; the record of the group as a later, measured
optimization of the IR for `Inline` layouts only.** Why:

- **Both conversions become free.** Widening a `Shape.Circle` into a `Shape` and binding the circle out of a `Shape` are
  a `Copy` in the IR - a move of the same words, or no instruction at all after the ownership pass. A field read on a
  case-typed value is the `Read` with a `PathStep.Variant` step a `match` binding already emits.
- **For a `Boxed` closed trait it is also the right final answer.** Constructing `Tree.Leaf` allocates a `Tree` block
  today; a `Tree.Leaf` that points at that same block costs exactly as much, and widening it back is a pointer copy
  instead of a second allocation.
- **The IR and the back ends change almost nothing.** No new `IrType`, no new instruction: the lowering maps the
  checker's case type to the layout of its closed trait, and everything downstream already handles that layout. That
  is what keeps phase 1b a checker round (section 9).
- **What it costs is space where a case is smaller than the largest one**, and only where values are stored as the case
  type - a `List<Shape.Circle>` of `Inline` shapes carries a tag and the room of the widest case per element. That is
  the one thing the record representation buys back, at the price of a `Construct` per widening; it is an IR decision
  that is not observable (there is no `sizeof`, no layout and no reflection), so it can be made per layout once a
  benchmark shows it matters.
- **A case without fields** (`Shape.Empty`) is a `Shape` whose tag is known, which the lowering can hold as a constant.
- **It is what Rust's RFC 2593 chose** for its variant types: they "share the enum's representation" and carry all of
  its type parameters, so that the conversion costs nothing (section 0.2, lesson 1).

### 2.3 Dispatch: by tag, statically, without a witness table

| Member of the closed trait | What a call compiles to |
|---|---|
| A member with a body (section 5.1) | a direct call of that one function, on a case value as well (the value already is a `Shape`) |
| A required member every case writes (section 5.2, phase 2) | a direct call of a generated function whose body is a `Switch` on the tag with one direct call per case - the `match self` a programmer would have written |
| A derived member (`Show`, `Equals`, `Hash`, `Encode`, ...) | the same generated `Switch`, over the members of each case |
| A required member, on a value whose static type is the case type | a direct call of the case's own function: no tag test at all |

**No closed-trait value ever carries a witness table.** Where a `Shape` meets a generic bound (`fn describe<Item: Show>`
called with a `Shape`), the witness is `Shape`'s implementation, statically known, and the call is monomorphized like
every call without a trait-typed value in it (TYPECHECKER 4.4). Where a `Shape` is coerced to an open trait (a
`Show` parameter), it becomes a trait value exactly as any other value does.

### 2.4 Why this differs from an open trait, and why that is sound

An open trait value is an existential: `IrType.Object(bounds)`, in C `struct { torb_object *data; const w_A *a; ... }`,
the payload boxed "because a witness member takes `void *self`" (BACKEND 3.1), one witness table per bound, and calls
through the table - unless `ir/devirtualize.trb` proves that the whole program agrees on one payload. It has to be that
way: any package may write `type Square with Shape` later, so neither the size of a `Shape` value nor the set of its
implementations is known where the value is built.

A closed trait is the opposite on both counts, and that is the whole soundness argument:

1. **The implementor set is fixed by one declaration** (decision 1.3). No package, not even the declaring one outside
   that declaration, can add an implementor, so the tag space and the widest case are known when the layout is
   computed.
2. **Every implementor is known to every caller**, so a member call can always be a `Switch` over all of them; there
   is no implementation a table would have to reach.
3. **Exhaustiveness is decidable** from the same declaration, so a `Switch` needs no fallback arm.
4. **Nothing is separately compiled.** A program is lowered as a whole and monomorphized (BACKEND 1.4), so an inline
   union of a type from another package is as known as one of this package.

### 2.5 The VM and the native back end

Both read one IR, and the IR is where every decision above lives - the layout, the representation class, the
dispatch function, the erasure of the case type. The native back end emits the union it emits today; the VM holds the
same words in its registers (VM.md section 2, "Variant, Inline: the common fields, the tag word, then the group of
the variant"). The generated dispatch function of a required member is an ordinary `IrFunction` with a `Switch`, which
the bytecode encodes as it encodes a `match`. Phase 2's nested groups give each group a contiguous range of the root's
tags, so "is a `Round`" is a range test (two comparisons, an `Intrinsic` both back ends have) and a group widens to its
parent with no instruction. `tools/conformance.sh --vm` holds the two to the same output, as for everything else.

---

## 3. Conversions and subtyping

### Decision 3.1 — one conversion: a case value where its closed trait is expected

A `Shape.Circle` where a `Shape` is expected becomes a `Shape`: in an argument, a binding with an annotation, a field,
a result, a collection element whose type is known, either side of a comparison that is checked against a `Shape`. It
is O(1) - with the erased representation, free (decision 2.2).

**This is not a sixth coercion.** It is the first of the five CONCEPT lists, "a value where a trait type is expected",
now for a closed trait as well as an open one; only its representation differs (a tag and a payload instead of a box
and a table). In the checker it is one more line in the table of TYPECHECKER 2.5, recorded as its own adaptation
(`Adaptation.ToClosedTrait`) because the lowering does something else for it than for `ToTraitValue`. It applies only
in a check position and never solves an inference variable, like the other four. It composes with the wrap: a
`Shape.Circle` where a `Shape?` is expected widens and then wraps into `Some`, the order TYPECHECKER 2.5 already gives
("exactly `Value` (after a trait coercion)").

### Decision 3.2 — no general subtyping

| Option | What it would mean |
|---|---|
| Case types are subtypes of their closed trait everywhere | `(Shape) => Int` usable as `(Shape.Circle) => Int`, `List<Shape.Circle>` as `List<Shape>`, subtype constraints in inference |
| **Only the conversion of decision 3.1, at a value** | a case value converts where the closed trait is expected, and nothing else converts |

**Chosen: only the conversion.** "Coercion to a trait type is the only subtyping in the language" stays true word for
word. What does **not** convert:

- **the closed trait into a case** - that is what a `match` is for (section 4);
- **one case into another**, or a group of phase 2 into a sibling;
- **a function type**: a `(Shape.Circle) => Float` is not a `(Shape) => Float`, and neither is the other way round;
- **a container**: decision 3.3.

`==` checks its right side against the type of its left side (TYPECHECKER 2.1), so `shape == circle` widens the circle
and compares, while `circle == shape` is an error that names the fix (`Shape.from(circle) == shape`, or the operands
swapped). The same asymmetry holds for trait values today.

### Decision 3.3 — containers stay invariant

`List<Shape.Circle>` is not a `List<Shape>`, for the reason CONCEPT gives for `List<Square>` and `List<Shape>`: there is
no variance. The representation would even allow it today (decision 2.2 makes the element layouts equal), but a rule of
the language that depended on that would forbid the record representation later. A list of circles becomes a list of
shapes with the conversion that is generated for every case type (section 5.5):

```trb
const circles: List<Shape.Circle> = [Circle(1.0), Circle(2.0)]
const shapes: List<Shape> = circles.map(Shape.from).toList()
```

`Shape.from` has one implementation per case type, and the element type picks the one meant - as
`readings.map(Celsius.from)` picks `From<Fahrenheit>` today where `Celsius` has a second `From` (probed).

### Decision 3.4 — a construction widens, and there is no join

What is the type of `Shape.Circle(1.0)`, and of `[Shape.Circle(1.0), Shape.Rectangle(1.0, 2.0)]`?

| Option | `const c = Shape.Circle 1.0` | the mixed list | What it costs |
|---|---|---|---|
| A construction has the case type; a mixed literal is an error | `c: Shape.Circle` | error: annotate | breaks today's code: `var c = Shape.Circle 1.0` followed by `c = Shape.Empty` stops compiling, and so does every unannotated list of mixed cases |
| A least upper bound | `c: Shape.Circle` | `List<Shape>` | subtyping inside inference, which TYPECHECKER 2.2 rules out ("a variable is solved by an exact type only"); with nested groups the bound depends on which cases appear; the `var` example breaks the same way |
| The closed trait where it is the only common type | `c: Shape.Circle` | `List<Shape>` | a join restricted to two levels, and still the `var` example |
| **Widening: a construction has the type of its closed trait, unless the expected type is the case type** | `c: Shape` | `List<Shape>` | nothing; the case type appears where a program asks for it |

**Chosen: widening, Scala 3's rule for enum cases; and no join anywhere.** A construction of a case -
`Shape.Circle(...)`, a bare `Circle(...)` resolved by context, an imported `Circle(...)`, a case without fields - has
the type `Shape` unless the expected type is `Shape.Circle` itself (or, in phase 2, a group that contains it). Why:

- **Every expression that type checks today keeps its type**, so phase 1b changes the meaning of no program.
- **The objection of 0.1 dissolves**: a list of mixed cases never needs a join, because its elements all are `Shape`s.
  Written bare with nothing expected, `[Circle(1.0), Square(2.0)]` does not get that far: there is no expected type to
  find `Circle` in, and the message says to write `Shape.Circle` or `use Shape.Circle`. With the two imports, or
  qualified, it is a `List<Shape>`.
- **A case type shows up exactly where it was asked for**: an annotation (`const c: Shape.Circle = Circle 1.0`), a
  parameter, a field or a result declared with it, an `as` binding (section 4.2), a member that answers it.
- **Where two values of different static types still meet** - a `Shape.Circle` binding and a `Shape` in one list
  literal, or in the arms of one unannotated `match` - it is the error it is today for any two types ("The arms of this
  `match` do not agree on a type: the first is `Shape.Circle`, this one `Shape`"), and the note offers the annotation. A
  join could be added later without breaking a program; taking one back could not.

---

## 4. Matching

### 4.1 Destructuring is unchanged, without the dot

```trb
fn area(shape: Shape): Float {
  match shape {
    Circle(radius) => Float.pi * radius ** 2
    Rectangle(width, height) => width * height
    Empty => 0.0
  }
}
```

A case pattern mirrors the constructor as it does today: positional sub-patterns fill fields from the left, labeled
ones name theirs, and a pattern that names fewer fields ends in `...`. The name is resolved against the type of the
matched value (section 6.2).

### Decision 4.2 — `as` binds the whole case, typed as the case

| Option | Spelling | Against |
|---|---|---|
| A typed binding | `circle: Shape.Circle =>` (Scala, Java's `case Circle c`) | `label: pattern` already means a labeled sub-pattern inside a constructor pattern (`Config(port: 443, ...)`), so `Pair(first: Circle)` would read two ways |
| An at-binding | `circle @ Circle(...)` (Rust, Haskell, Scala) | an operator character spent on one construct |
| A type test without fields | `Circle as circle`, a bare name meaning "any `Circle`" | today a bare name of a case with fields is an error on purpose ("`Circle` has 1 field and this pattern names 0"), so a pattern that omits fields says so |
| **`as` behind any pattern** | `Circle(...) as circle =>` | four more characters than the type test |

**Chosen: `<pattern> as <name>`.** It binds the whole value the pattern matched, with **the most precise type the
pattern proves**: the case type after a case pattern (`Shape.Circle` after `Circle(...)`), the group after a group
pattern of phase 2, and the type of the matched value otherwise - the type RFC 2593 gave `a @ Sum::A(_)`. `as` is
already the word for naming something that has another name (`use Option.None as Nothing`), and it is contextual - an
ordinary identifier to the lexer, like `from` and `by` - so it breaks no name.

```trb
fn grown(shape: Shape): Shape {
  match shape {
    Circle(...) as circle => circle.copy(radius: circle.radius * 2.0)
    Rectangle(width, ...) as rectangle if width < 1.0 => rectangle.copy(width: 1.0)
    other => other
  }
}
```

- **`as` binds loosest**: `Circle(...) | Empty as shape` names the whole alternative, and a guard follows the `as`.
- **It composes**: `Pair(Circle(...) as left, _)`, `[first, ...rest] as all`, `Some(Circle(...) as circle)`.
- **It is a binding of a refutable pattern**, so the rule that a binding somebody never reads is an error applies to it.
- **`if const` is narrowing without flow typing**: `if const Circle(...) as circle = shape { print circle.radius }`.
- **`if var` binds into the place**, as it does today: in `if var Circle(...) as circle = shape`, `circle` is the
  circle inside `shape`, and `circle.radius = 2.0` changes `shape` - the `var` path through a variant step every
  `if var` binding already uses. (Case fields stay `const`, so this matters once a case type has `var fn`s in phase 2.)
- **`Circle as circle` is not accepted** as a short form of `Circle(...) as circle` (the owner, 2026-09-29).
  `Circle(...)` says that fields are left out, which is why a bare case name with fields is an error today; the short
  form can be added later without breaking anything, and taking it back could not.

### 4.3 Exhaustiveness

- **A closed trait's constructor set is its cases**, exactly as for a type with cases today (TYPECHECKER 5.5, Maranget's
  usefulness over a pattern matrix); nothing changes for a `match` over a `Shape`.
- **A case type has one constructor**, like a record: `match circle { Circle(radius) => ... }` is exhaustive with one
  arm, and an `Empty =>` arm against a `Shape.Circle` is the error "This arm never matches: the value is a
  `Shape.Circle`".
- **Nested groups (phase 2)**: the constructor set of `Shape` is its leaves; a group pattern `Round(...)` stands for the
  alternatives of the group's leaves. So `Round(...) as round` together with `Polygon(...)` is exhaustive, `Circle(...)`
  after `Round(...)` is unreachable, and a missing case is reported by its leaf ("`match` does not handle
  `Ellipse(...)`").
- **An `as` binding changes nothing about coverage.**

### Decision 4.4 — narrowing by flow typing is not in phase 1

| Option | Example | Cost |
|---|---|---|
| None beyond patterns | `if const Circle(...) as circle = shape { circle.radius }` | nothing |
| `is` as a `Bool` test only | `shapes.filter { _ is Circle }` | a reserved word (`is` is an identifier today), a tag test; the value keeps its type |
| `is` plus flow typing (Kotlin's smart casts) | `if shape is Circle { shape.radius }` | the checker learns flow typing, which it has nowhere else; a `var` or a place narrowed across a change is the classic source of unsoundness |

**Chosen: none, and phase 3 is deferred (the owner, 2026-09-29).** `as` in `if const`, `while const` and `match` gives
every narrowing a name, statically and without a rule about flow. Whether `is` comes at all is decided after phase 2,
from evidence: the places in the compiler and in `std` where `if const ... as` reads badly, collected while phase 2 is
used. If something comes then, the first candidate is `is` as a `Bool` test without flow typing, because flow typing
would be the only flow-sensitive typing rule of the checker.

---

## 5. Methods

One principle decides every rule of this section:

> **A case value answers every member the same, whether it is seen as its case type or as its closed trait.**

It is the rule coherence already holds for open traits - "what a call does would depend on the static type it is made
through" is why `extend Shape with Describe` may not overlap `Square`'s own `describe` (coherence, rule 4). A closed
trait keeps it by construction.

### Decision 5.1 — a member the closed trait writes with a body is final

| Option | A member with a body on `Shape` | Against |
|---|---|---|
| A default, replaced per case | a case may write `area` itself; `Shape.area` dispatches by tag to the case's or the default | the default's `match self` gets arms that never run for the cases that replace it; which body runs becomes a question |
| **Final: one body for every case** | a case may not write `area` | per-case variation is the `match self` inside that one body - which is how every type with cases is written today |

**Chosen: final.** A member written with a body in a type with cases is what it is today, the one implementation, and
**it is a member of every case type too**: `circle.area()` runs `Shape.area` on the circle, which already is a `Shape`
(decision 2.2). A case that writes a member of the same name is an error at the case: "`area` is written by `Shape`
for every case". An open trait's defaults are replaceable because the trait cannot know its implementors; a closed
trait knows them, so an exception for one case is an arm of the body.

- **A `var fn` of the closed trait is not a member of the case types.** `var fn` on `Shape` may turn a circle into an
  `Empty`, and a `Shape.Circle` binding could not hold the result. It is called on a `Shape` binding.
- **`Self` in the body of a type with cases is the closed trait**, as it is today (`fn scaled(factor: Float): Self`
  answers a `Shape`), and in a case body (phase 2) it is the case type.
- **`static` members belong to whoever declares them.** `Shape.unit` is `Shape`'s; a case type's statics are its own.

### Decision 5.2 — a member without a body is a requirement every case writes (phase 2)

```trb
type Shape {
  case Circle(radius: Float) {
    fn area(): Float {
      Float.pi * radius ** 2
    }
  }
  case Empty {
    fn area(): Float {
      0.0
    }
  }

  fn area(): Float
}
```

Today a `fn` without a body in a type is an error ("`area` has no body. Only a requirement of a trait and a `native`
declaration have none"). In a type with cases it becomes a requirement, like the member of a trait: every case writes
it, in its body or in an `extend Shape.Circle` of the declaring package, **with exactly the signature of the
requirement** (`Self` in it meaning `Shape`). A call on a `Shape` is the generated dispatch of section 2.3; a call on a
`Shape.Circle` is the case's function. A missing one is reported at the case: "`Empty` does not write `area`, which
`Shape` requires of every case".

This reverses one clause of the Decision Log ("methods per case are a second spelling of `match self`"), and the
owner accepted it for phase 2 (2026-09-29): it is what "the type above them like a trait" means, a case that is a
type has members anyway (`extend Shape.Circle`), and the final rule of decision 5.1 keeps it from becoming
overriding.

### Decision 5.3 — members of a case type, and the order they are looked up in

A case type is a `Nominal` type and follows the order of TYPECHECKER 4.1, with one step added:

1. its fields (the common fields of the closed trait first);
2. the members of its own body (phase 2) and of `extend Shape.Circle` in the package of `Shape` or in this one;
3. extension members of `Shape.Circle` from other packages that this file names (`use Shape.Circle.helper from "..."`);
4. the members of the traits it implements (section 5.4);
5. **the members of its closed trait**: the final members of `Shape` and of trait-less `extend Shape` blocks visible
   here - except `var fn`s (decision 5.1);
6. generated members: `copy` (new for a case: `circle.copy(radius: 2.0)` answers a `Shape.Circle`) and the derived
   ones.

Two candidates in one step stay an ambiguity error. A case-only member (`diameter`) and a final member of `Shape` of
the same name cannot both exist - the second declaration is the error of decision 5.1.

### Decision 5.4 — a case implements traits on its own

A case type implements a trait `T` in exactly one of four ways, tried in this order:

1. **It writes `T` itself** - `case Circle(radius: Float) with Hash { ... }` (phase 2) or
   `extend Shape.Circle with Hash { ... }` (phase 1b). Allowed unless `Shape` writes `T` by hand (then `T`'s members are
   final, decision 5.1).
2. **`Shape` writes `T` by hand, and `T` is reachable through the receiver** - every member takes `self`, none answers
   `Self`, none is `static` - **then the case implements `T` through `Shape`'s implementation.** That is CONCEPT's rule
   for "an `extend` implements the trait for the trait-typed value, and reaches a concrete implementer only through the
   receiver", and it is what makes `"{circle}"` print what `"{shape}"` prints when `Shape` writes `show` itself.
3. **`T` is derivable** (`Equals`, `Hash`, `Show`, `Encode`, `Decode`, `Describe`) **and `Shape`'s `T` is derived or
   absent: the case type derives it over its own fields.** The texts agree with today's: the derived `Show` of a
   `Shape.Circle` is `Circle(radius: 1.0)`, which is what the derived `Show` of a `Shape` prints for it; the derived
   `Encode` writes the same `variant` (section 7.4). **A derived member of the closed trait is the dispatch to its
   cases' members**, so a case that writes its own `show` (way 1) changes what the `Shape` prints for it - a
   `Secret(value: String)` can print `Secret(***)` without the closed trait writing every other case by hand.
4. Otherwise it does not implement `T`.

Coherence is the ordinary rule: the package of `Shape` owns `Shape.Circle`, so `extend Shape.Circle with Encode` is
written there, and a package that owns a trait may implement it for a case type of somebody else's closed trait, as it
may for any type. A trait the case type implements is not thereby implemented by `Shape`.

### Decision 5.5 — generated conversions, and `?`

- **Every closed trait gets `From<Case>` for each of its case types**, whose body is the conversion of decision 3.1.
  It is what `.map(Shape.from)` names and what `?` needs (below).
- **The generated `From` of a single-payload case stays** as it is: a case that wraps exactly one value of a type no
  other case wraps generates `From<Payload>` (`AppError.from(configError)` is `AppError.Config(configError)`). One
  refinement: a payload whose type is itself a case type of the same closed trait generates none, because
  `From<Case>` already exists for it.
- **`?` keeps converting through `From`**, unchanged (TYPECHECKER 4.6), and now reaches from a case type to its closed
  trait. So a function can be precise about the one case it fails with, and its callers need no conversion written:

```trb
type ReadError {
  case Truncated(at: Int)
  case BadMagic(found: UInt32)
  case Io(cause: IoError)
}

fn header(bytes: List<UInt8>): Result<Header, ReadError.Truncated> {
  if bytes.length() < 16 {
    return Fail Truncated(bytes.length())
  }
  Header.from(bytes[0..16])
}

fn frame(bytes: List<UInt8>): Result<Frame, ReadError> {
  const found = header(bytes)?
  Frame(found)
}
```

`header(bytes)?` converts a `ReadError.Truncated` into a `ReadError` through the generated `From`; a caller of `header`
that matches its failure has one case to handle and no `_` arm. This is the precise error type without a type per
error, which today takes a `type Truncated` beside the `case Truncated(truncated: Truncated)`.

### Decision 5.6 — a closed trait is not a bound

`fn describe<Item: Shape>(item: Item)` is an error ("`Shape` is a type with cases: take a `Shape`"). A bound over a
closed trait could only stand for `Shape` or one of its case types, and a `Shape` parameter already accepts all of
them through the conversion. What a bound would add - answering the same case type back - needs members that answer
`Self` per case, which decision 5.1 does not give. It can be opened later without a break.

---

## 6. The leading dot

**Decided by the owner (2026-09-28):** "Where the type is known, you can always write them without `.` or `Xyz.`;
otherwise via `use`." The `.Case` form is removed. This section designs the rule that does it, so that **a meaning can
never change silently**.

### 6.1 Where a case is written bare

Exactly where `.Case` works today (cases-and-match rule 2, TYPECHECKER 2.1), so that the migration is the removal of a
character everywhere it does not collide (section 9):

| Position | Example | The expected type |
|---|---|---|
| A binding with an annotation | `const unit: Shape = Circle 1.0` | the annotation |
| An argument | `shapes.append Empty`, `Json(naming: SnakeCase)` | the parameter |
| A field default, a default argument | `var shape: Shape = Empty` | the field or the parameter |
| A result, `return` | `return Fail Timeout(seconds)` | the declared result |
| Either side of `==` and `!=` | `if shape == Empty { ... }`, `if Empty == shape { ... }` | the other side (below) |
| An arm of a `match` or a branch of an `if` whose result is expected | `const s: Shape = if big { Circle 9.0 } else { Empty }` | the expectation of the whole |
| A collection element of a known type | `var access: Flags<Permission> = [Read, Write]` | the element type |
| A pattern | `Circle(radius) =>`, `Some(Circle(radius)) =>` | the type of the value the pattern stands against |
| A case type expected | `const c: Shape.Circle = Circle 1.0` | the case type: its own name resolves, a sibling does not |

A bare name is contextual only **as a whole expression, as the callee of a call** (parenthesised or command), **or as a
pattern**. The head of a path is not: in `Keyword.from(k)`, `Keyword` is resolved in the scope, whatever is expected of
the whole.

**`==` gets both sides.** The right operand is checked against the type of the left one (TYPECHECKER 2.1), so
`shape == Empty` needs nothing new. For `Empty == shape` the rule is one sentence: where the left operand of `==` or
`!=` is a bare uppercase name, or a call of one, that no name of the file resolves, the right operand is inferred first
and the left one is checked against it. Both sides of `==` have one type (`Equals.equals` takes `Self`), so reading the
type off the right is exact. Cases-and-match rule 2 promises "either side of `==`" for `.Case` today, but the checker
only does the right one: `print(.Empty == shape)` is "`.Empty` needs a type, and nothing here says which one"
(probed). Phase 1a closes that gap. Kotlin and Dart resolve the right side only, Kotlin to keep `when (x) { A -> }`
equal to `when { x == A -> }` and Dart because its `==` is not symmetric.

### Decision 6.2 — the resolution rule

When a name that starts with an uppercase letter stands in one of those positions and an expected type `E` is known
(for a pattern, `E` is the type of the matched value):

1. **The file's own names** - the local scope, the receiver, and the file scope: its top-level declarations and its
   imports under the names they were imported as (TYPECHECKER 3.1, steps 1 to 4). Call the answer *own*.
2. **The cases of `E`** - the case of `E` declared under that name; where `E` is an `Option<X>` or a `Result<X, F>`
   with no case of that name, the case of `X` (the fallback `.Case` has today, `implicitMemberTarget`); in phase 2,
   also a case nested in a group of `E` (unique by decision 1.5). Call it *contextual*.
3. **Decide:**

| *own* | *contextual* | Answer |
|---|---|---|
| found | found, and the same declaration (the file imported that case) | that case |
| found | found, a different declaration | **error**: "`Keyword` is the case `Token.Keyword` here and also the type `Keyword` this file imports. Write `Token.Keyword`, or rename the import with `as`" |
| - | found | the case |
| found | - | *own*, as today |
| - | - | **the prelude**, as today; then "Cannot find" with the best note |

Without an expected type, step 2 is skipped: the file's names, then the prelude, as today - and a bare case of no
imported name is "`Circle` needs a type here. Write `Shape.Circle`, or `use Shape.Circle`", with `Shape` named in the
note when exactly one type visible in the file has such a case.

**The prelude comes after the cases, not beside them.** The prelude is a name of every file that the file never wrote,
and the repository declares at least 30 cases named like a prelude name (`case String` and `case Bool` in `JsonValue`,
`case None` and `case Fail` in types of the compiler, `case Error`, `case Set`, `case Range`, `case Ok` ...). Were the
prelude part of *own*, each of them would need its type written in every position, and **adding a name to the prelude
would break programs** - the open 1.0 question of RELEASE.md section 2 ("whether a new prelude name can break a
program") would get a new way to be answered no.

**The comparison with the alternatives:**

| Option | Who | The file adds an import of a case's name | The prelude gains a case's name | Cost |
|---|---|---|---|---|
| Scope first, the cases last, the prelude above them | Kotlin 2.2; Odersky's proposal for Scala | the name becomes the import: a type error at the use, far from its cause (with subtyping, Kotlin met silent changes here) | every bare use of that case breaks | the 30 cases named like a prelude name are written qualified for good |
| The cases first, the scope last | the dotted forms of Swift and Dart | nothing changes | nothing changes | an import that renames a case (`use Shape.Circle as Round`) silently becomes the new case `Shape.Round` once one is added; the file's own `use` lines lose to a declaration elsewhere |
| Every collision an error, the prelude included | - | an error naming both | every bare use of that case breaks | as the first row |
| **The file's names against the cases: an error; the prelude below both** | this record | an error naming both | nothing changes | a site that collides with a name the file wrote is qualified |

**Chosen: the last row.** Resolution looks at names only - which names the file wrote and which cases `E` has - and
never at whether a candidate would type check. That is deliberate: the language has "no return-type-driven overload
resolution", and a name whose meaning depended on which candidate fits would be one. A reader, the highlighter and the
language server explain every bare case by the scope and the expected type alone.

### 6.3 Why a meaning cannot change silently

One observation carries the argument: **outside the cases of `E`, no name can stand where an `E` is expected and type
check** - a closed trait has no implementors but its cases (decision 1.3), and a type with cases has no constructor of
its own. (For an `Option<X>` or a `Result<X, F>`, read "the cases of `E` and of `X`": a name outside them that fits,
such as the constructor of a record `X`, is looked through only when `X` has no cases, so it never meets a contextual
case.) So wherever *own* and *contextual* differ, at most one of them could ever have compiled there, with one
exception: an import that renames a case of `E` itself (`use Shape.Circle as Round`). Every event, one by one:

| What happens | Before | After |
|---|---|---|
| The file adds `use Foo.Keyword` or declares `type Keyword` | `Keyword(k)` was the case `Token.Keyword` | an error at every such `Keyword`, naming both |
| The file removes an import | the name was the imported declaration, and `E` had no case of that name (or it was that case) | the same case, or "Cannot find" - never a different declaration |
| `E` gains a case whose name the file already imports | the name was the imported declaration | an error, naming both - this is the `as`-rename exception, caught |
| `E` gains a case named like a prelude name | the prelude name could not have fit `E`, so no working code used it there | the case - nothing that compiled changes |
| The prelude gains a name that is a case of `E` | the case | the case: the prelude ranks below it |
| `E` loses a case | the case | the file's name or the prelude, which cannot fit `E`: a type error |
| The expected type changes from `Shape` to `Figure`, both with a `Circle` | `Shape.Circle` | `Figure.Circle` - as with `.Circle` today, and as with every expression checked against a changed type |
| The expected type changes from `Answer` to `Answer?`, and `Answer` has a case `None` | `Answer.None` | `Option.None`, because an `Option`'s own cases come first - as `.None` does today; section 8's lint names such cases |

**The first letter still decides in a pattern.** An uppercase name never binds, so `Circel(radius) =>` is "`Circel` is
not a case of `Shape`. Did you mean `Circle`?" and never a catch-all; a lowercase name always binds. The trap the
Decision Log closed stays closed; what changes is only that the matched type is now one of the two places an
uppercase pattern name is looked up in.

### 6.4 Where Kotlin 2.2 and Dart stand

**Kotlin 2.2's context-sensitive resolution** (KEEP-379, behind `-Xcontext-sensitive-resolution`, a preview in 2.2,
experimental in 2.3, stable planned for 2.6) is the closest relative, and it made three choices this record makes
differently, each for a reason Kotlin has and TorbScript does not:

| Kotlin 2.2 | Why Kotlin | This record | Why here |
|---|---|---|---|
| The contextual scope has "the lowest priority (even lower than that of default and star imports)", so in `when (x) { Any -> ... }` `Any` stays `kotlin.Any` although `Test.Any` exists | "every program which currently compiles should keep compiling *with the same behavior*": a bare name meant a scope name before the feature existed | the cases rank above the prelude, and a collision with the file's own names is an error | the migration comes from `.Case`, which already meant the case; and with no `Any`, no subtyping and no type tests, a prelude name can never fit a closed trait where a case of it could (6.3), so the case losing to it would only ever produce an error |
| Only classifiers, objects and properties; no calls - `Leaf` resolves, `Node(...)` does not - because of "resolution explosion" in overload resolution | an argument's expected type is known only after the overload is chosen, and bare names multiply the candidates | cases with and without fields, as `.Circle(1.0)` resolves today | the expected type names exactly one namespace and resolution never filters candidates by type (6.2), so a call adds no candidates |
| A warning, added in 2.3, where contextual and ordinary resolution disagree - after an unrelated star import silently changed what `is Child2` meant (KT-77821) | the priority above made the silent case possible, and compatibility forbade an error | an error for that disagreement, from the first day | nothing compiles today that the error would reject: the bare form is new |

On the dot, Kotlin's reasoning is the owner's: "`foo` newline `.bar`" is already a member access, so a leading dot
would need a second reading of the same text - the arm rule of 6.6 is that second reading, and it goes.

**Dart 3.10's dot shorthands** kept the dot. In the language issue that led to them, a bare name with "magical constant
lookup" was weighed and called "a little more dangerous because it might happen by accident", and the objection that
decided it was `var foo = 5` beside `enum Bar { foo }`, where `foo == 5` becomes ambiguous. Here that collision cannot
happen: a case starts with an uppercase letter and a variable with a lowercase one, and the checker holds that at every
declaration. Dart also left a sealed class's subtypes out of the lookup (issue 4149) and searches only the context
type's own namespace; here the cases are nested in their closed trait, so they *are* its namespace.

**Swift** kept the dot as well, and shows the one trap the bare form inherits unchanged: where an `Optional` and the
enum inside it both have a `.none`, `Optional.none` wins, with a warning since Swift 5.1. Section 6.3's last row is the
same rule here, and section 8 gives it the same kind of warning.

### Decision 6.5 — only cases; the static-member shorthand goes too

Today the dot reaches any static member of the expected type, not only cases: `const p: Point = .origin` and
`const q: Point = .square(3)` compile and run (verified), though nothing in the repository writes either. Kotlin's
context-sensitive resolution and Dart's dot shorthands reach static members and constructors as well.

**Chosen: contextual resolution is for uppercase names, which are types and cases; the dotted static member is removed
with the dotted case and not replaced.** A bare lowercase name is a local, a parameter, a field of the receiver or a
function, and letting the expected type add candidates there would put every local name in reach of every type's
statics. `Point.origin` stays the spelling, as it is everywhere in the repository.

### Decision 6.6 — a line that starts with `.` always continues

Today, directly inside the braces of a `match`, a line that starts with `.` starts an arm, and everywhere else it
continues the expression above - which is why a call chain in an arm needs a block (cases-and-match rule 10). With arms
starting with a name, that exception has nothing left to do:

- **After the migration, a leading `.` continues the line above everywhere, a `match` body included**, so an arm's value
  may be a call chain without a block. The formatter indents such a continuation one level deeper than its arm.
- **A line in a `match` body that starts with `.` and an uppercase name is the removed form**, and the parser keeps
  recognising it for exactly one purpose: the error "A case is written without the dot: `Circle(radius) =>`", with a
  fix that removes the dot. A method name is lowercase, so `.Circle` at the start of a line can mean nothing else.
- **Everywhere else, `.Circle` in expression position** gets the same error and fix, qualified where the bare name would
  collide (section 9).

### 6.7 What stays

- **The qualified path `Shape.Circle`** is valid everywhere, and is the one form where no type is expected.
- **`use Shape.Circle`** makes the name the file's own, in every position, with or without an expected type.
- **The prelude's `Some`, `None`, `Ok` and `Fail`** stay imported, because `const found = Some(3)` and an arm whose
  result nothing expects have no expected type to resolve them by.
- **`Shape.Circle` where the bare name would do** is not an error. `torb lint` reports it as the rule
  `qualified-case`, with a fix, and `torb lint` is not a gate (the owner, 2026-09-29). Only the checker knows whether
  the bare name would resolve to the same case, so the formatter, which works on the syntax tree and cannot change
  what a program means, cannot enforce it; and an error would make every change to an import or an expected type
  ripple into unrelated lines.

### 6.8 The messages

Each names the line to write, as the language's diagnostics do:

| Situation | Message | Note or fix |
|---|---|---|
| A bare case with nothing expected | "`Circle` needs a type here. Write `Shape.Circle`, or `use Shape.Circle`" | `Shape` is named when exactly one type the file can see has a case `Circle` |
| The file's name and a case of the expected type collide | "`Keyword` is the case `Token.Keyword` here and also the type `Keyword` this file imports" | "Write `Token.Keyword`, or rename the import with `as`"; the fix writes the qualified form |
| A misspelled case in a pattern | "`Circel` is not a case of `Shape`" | "Did you mean `Circle`?" at two edits or fewer; the first-letter note where no case is close |
| A misspelled case in an expression | "`Shape` has no case `Circel`" | the same nearest-case note |
| The removed form, anywhere | "A case is written without the dot: `Circle(radius)`" | the fix removes the dot, or qualifies where the bare name would collide |
| The removed static shorthand | "`.origin` is written `Point.origin`" | the fix writes the type the expectation named |
| A case pattern that cannot match the matched type | "This arm never matches: the value is a `Shape.Circle`" | - |
| A `var fn` of the closed trait on a case type | "`scale` changes the `Shape` and may change its case: call it on a `Shape`" | - |

---

## 7. The prelude and std

### 7.1 `Option` and `Result`

Both are types with cases, so both are closed traits with case types: `Option.Some<Value>`, `Option.None<Value>`,
`Result.Ok<Value, Failure>`, `Result.Fail<Value, Failure>`. Their layouts do not change (the niche of `Option` stays,
and a case type erases to it), `?`, `?.` and `??` do not change, and the prelude keeps importing the four cases
(section 6.7). What is new is only that `Option.Some<Value>` and the other three can be named as types, which nothing
in `std` needs yet.

### 7.2 The error types of std

A public error type of `std` is one of two kinds after this record, and decision 7.3 says which is which. **A type with
cases that is complete** gains what decision 5.5 gives: a function may promise the one case it fails with (a
`Result<Path, PathError.Outside>`), and a caller that wants the whole error converts with `?`. **A newtype** exposes no
cases, so its callers ask it questions instead. Whether and where `std` narrows its signatures to one case is a decision
of each package's own round, not of this record.

### Decision 7.3 — a public type with cases never grows; what must grow is a newtype (the owner's)

RELEASE.md section 2 listed for 1.0: "a new case of `OperatingSystem` breaks every `match` on it; the same holds for
`IoError`, `HttpError` and every error enum of `std`", with "a marker that makes `_` mandatory outside the declaring
package, or a policy that such an enum never grows" as the two ways. Closed traits do not change the premise: a case
is a promise, and the decision "No `open type`" stands.

| Option | How | For | Against |
|---|---|---|---|
| A marker (`#[non_exhaustive]`, `@unknown default`, SE-0487's `@nonexhaustive`) | a modifier on the type; `_` mandatory outside the package | explicit; `match` stays for callers | a modifier the language would otherwise not need, and a `match` whose `_` arm hides every case added later |
| A `private case` | a case that code outside cannot name, so every `match` there needs `_` | no new word | growth as a side effect of visibility, which a reader has to know; the files of the declaring package need the `_` too |
| **A policy, and the newtype pattern where growth is needed** | a public type with cases never gains a case in 1.x; a type that must grow keeps its kinds in a private type behind a private field and answers questions | no mechanism at all; the pattern exists in `std` already (`HttpError`) and is what CONCEPT recommends | callers of a growing type ask questions instead of matching |

**Chosen: the policy, and the newtype pattern (the owner, 2026-09-29: "rather the newtype pattern, so not at all").**
The language gets no growth mechanism. The rules:

1. **A public type with cases never gains a case within 1.x.** A new case is a 2.0 change, like a removed member.
2. **A type that must be able to grow is a newtype**: a public type whose kinds are a private type with cases held in a
   private field, and which offers questions (`isNotFound()`, `isTimeout()`), accessors for the data every value has
   (`path`, `statusCode()`), and `static` factories to build one. Nobody outside can match its kinds, so a new kind is a
   new private case and perhaps a new question: an addition, which 1.x allows. CONCEPT shows the shape already
   ("A library that wants to stay free to add cases does not expose the ADT. It wraps it").
3. **An accessor never hands out the kinds.** A `kind()` that answered a public type with cases would hand out the
   growth problem with it. The questions are the interface; `show()` and `cause()` carry the rest.
4. **The newtype keeps everything else a type with cases had**: `Show`, `Error`, the `From` conversions that make `?`
   work (`HttpError` has them from `Utf8Error`, `JsonError` and `NetworkError`). Its private kinds get case types like
   every type with cases (section 1.2); they are as private as the type that declares them.
5. **`private case` is refused.** It parses and checks today and means nothing, and a modifier that means nothing is an
   error in this language, as `protected case` already is ("`protected` is only a modifier of a field"). The message:
   "`private` on a case means nothing: a case has the visibility of its type. A type that keeps its kinds to itself
   wraps a private type with cases". Nothing in the repository writes it; the check lands with phase 1b.

`IoError` in the newtype form - `path` and `message` stay fields, and the private kind has a default, so
`IoError(path, message)` still builds one from outside (a private field with a default is no parameter from outside).
Written with today's `.Other` and `.NotFound`, it type checks and runs:

```trb
public type IoError with Show, Error {
  path: String
  message: String
  private kind: IoErrorKind = Other

  fn isNotFound(): Bool {
    kind == NotFound
  }

  fn isCrossDevice(): Bool {
    kind == CrossDevice
  }

  fn show(): String {
    "{path}: {message}"
  }
}

type IoErrorKind {
  case NotFound
  case PermissionDenied
  case AlreadyExists
  case NotEmpty
  case CrossDevice
  case InvalidText
  case Other
}
```

**What it means for `std`**, type by type - the classification each package's round confirms before 1.0:

| Type | Today | In 1.x | Questions and accessors |
|---|---|---|---|
| `HttpError` (`std/http`) | a newtype: `private kind: HttpErrorKind`, `static` factories | unchanged: the model | `isTimeout()`, `isRetryable()`, `statusCode()`, `answerStatus()`, and a question for each new kind a caller has to tell apart |
| `JsonError`, `NetworkError`, `YamlError` | newtypes | unchanged | as they are |
| `IoError` (`std/fs`) | a record of `path` and `message`; `isCrossDevice()` reads the message text | a newtype, as above | `isNotFound()`, `isPermissionDenied()`, `isAlreadyExists()`, `isNotEmpty()`, `isCrossDevice()`, `isInvalidText()`; `path` and `message` stay fields. The kind comes from the error number the runtime already turns into the message (`fileFailureText`), classified once for `errno` and `GetLastError` |
| `OsError` (`std/os`) | a public type with five cases | a newtype: the questions of `std/os` grow with every topic | `isUnsupported()`, `isDenied()`, `isMissing()`, `isMalformed()`, and `question(): String?` for the question that failed |
| `DnsError` (`std/dns`), `UriError` (`std/uri`), `ReadError` (`std/binary`) | public types with 8, 11 and 7 cases | newtypes: a protocol, the URI features and the binary formats all grow | a question per kind a caller branches on (`isTruncated()`, `isInvalidName()`), `offset(): Int?` where the failures have one |
| `PathError`, `SignatureError`, `ExtractError` | public types with 2, 4 and 2 cases | stay types with cases: the cases are complete for what the package does | - |
| `OperatingSystem`, `Architecture` (`std/core`) | public types with cases, the subject of every compile-time branch (OS.md) | **stay types with cases and never grow within 1.x**: the targets 1.0 ships are the targets of every 1.y, the WebAssembly target of the playground included (RELEASE.md section 6), and a new operating system or architecture is a 2.0 change | `isPosix()`, and `_`, for code that must not care |
| `Ordering`, `ByteOrder`, `Option`, `Result` | complete by nature | stay | - |

**Why `OperatingSystem` stays a type with cases.** OS.md chose "a `match` on a compile-time constant" because
exhaustiveness is the point: every arm is checked on every machine, and the compile errors a new case causes are the
work list of adding a system. A newtype with questions would lose that - a new system would silently take the `else`
of every `if OperatingSystem.current.isWindows()` - and the constant evaluator would have to fold method calls, which
OS.md section 2 does not do. Turning it into a newtype later would itself break every `match` on it, so the choice is
final at 1.0.

**What it resolves.** The 1.0 item of RELEASE.md section 2 is answered as "a policy that such an enum never grows; the
newtype pattern where growth is needed", and RELEASE.md says so. What is left of it for 1.0 is work, not a question:
the conversions of `IoError`, `OsError`, `DnsError`, `UriError` and the `ReadError` of `std/binary`, each a breaking
change of its package and therefore before 1.0, with the `match`es on them in the repository rewritten to questions in
the same round.

### 7.4 Reflection, encoding and JSON

There is no runtime reflection, and closed traits add none. The three generated forms of "a value is its constructor
call" (CONCEPT, "Types, Values and Reflection") extend to case types without a new shape:

- **`Encode` of a case type writes what its closed trait writes for it**: `variant("app/Shape", "Circle")`, its fields,
  `finish` - so `json.encode(circle)` and `json.encode(Shape.from(circle))` are one text, `{"Circle":{"radius":1.0}}`
  in `std/json`, and `"Empty"` for a case without fields.
- **`Decode` of a case type** opens the variant and accepts only its own case: anything else is a `DecodeError` that
  names both ("expected the case `Circle` of `app/Shape`, found `Empty`").
- **`Describe` of a case type** is a variant with that one case, so a schema format sees a closed set of one.
- **A format keys its mappings by the closed trait's qualified name**, as today; a case type announces the same name
  plus its case name, which `variant(typeName, name)` already carries.

### 7.5 `Show`, `Equals`, `Hash`, `copy`

Derived as section 5.4 says: the closed trait's derived members dispatch to the cases' members, a case type's derived
members work over its own fields, and the texts are the fixed format of today - which matters, because the two back ends
are compared through it. `Equals` of a `Shape` is "same tag, then the case's `equals`", so it agrees with the case
types' own. A case type gets `copy` with its fields as parameters; the closed trait still has none.

### 7.6 Cases with fixed values and `Flags`

A type whose cases carry fixed values (FLAGS.md) is a closed trait like any other: `Permission.Read` is a type, a
`Flags<Permission>` holds `Permission` values, and `[Read, Write]` resolves by the element type. Nothing in FLAGS.md
changes.

---

## 8. Tooling

| Component | Phase 1a (bare cases) | Phase 1b (case types) | Phase 2 (case bodies, groups) |
|---|---|---|---|
| **Checker** | `name.trb` and `pattern.trb`: the rule of 6.2 for a bare uppercase name with an expected type, reusing `implicitMemberTarget`; the left side of `==` (6.1); the messages of 6.8 | case symbols as types in type positions; the widening of 3.4; `Adaptation.ToClosedTrait`; member lookup step 5; `as` patterns; `From<Case>`; derivation per 5.4; `extend Shape.Circle`; `private case` refused (7.3) | bodyless members as requirements; final members; nested groups, their tag ranges and their exhaustiveness |
| **Parser** | nothing new; the `.Case` error and fix in commit 2 | `as` behind a pattern | case bodies; `case` lines inside a case |
| **IR and lowering** | nothing: a bare case records the resolution `.Case` records today | case types lower to their closed trait's layout; field reads through the variant step; no new instruction | the generated dispatch functions; contiguous tag ranges for groups |
| **C back end** | nothing | nothing | nothing beyond what the IR emits |
| **VM** | nothing | nothing | nothing; `tools/conformance.sh --vm` gains the programs |
| **Language server** | completion at a name offers the cases of the expected type first; go to definition and hover on a bare case | hover shows `Shape.Circle` as the type of an `as` binding; completion after `Shape.` offers cases as types in type positions | completion inside a case body; the requirement a case still lacks as a quick fix |
| **`torb highlight`, semantic tokens** | a bare case is an `enumMember` by the checker's resolution, as `.Circle` is today | a case type in a type position is a type | - |
| **`torb doc`** | examples in doc comments migrated | a case is documented as a type under its closed trait: fields, members, traits | case bodies and groups nest in the page |
| **Formatter** | commit 2 drops the spacing mark of an implicit member (`format/layout.trb`) and indents a leading-`.` continuation inside an arm | `as` spacing | case bodies laid out like type bodies |
| **Linter** | the rule `bare-case` with `--fix`, checker-backed like `redundant-wrap` (the migration tool, deleted afterwards); `qualified-case` (6.7); a note on a case named `Some`, `None`, `Ok` or `Fail` in a type used inside an `Option` or a `Result` (6.3) | - | - |
| **Canon** | the rule `imported-case-patterns` (`canon/patterns.trb`) is obsolete and goes | - | - |
| **Docs** | every `.Case` in `docs/`, CONCEPT and the generated skill; `cases-and-match.md`, `importing-cases.md`, `pattern-forms.md`, the cheat sheet, `mistakes-models-make.md` and the "coming from" pages rewritten | a new page `pattern-matching/case-types.md` | case bodies in `types/` |

The debugger and the REPL show values through their names and the generated `Show`, which do not change. The policy
of decision 7.3 needs no tool: rule 2 of `docs/language/pattern-matching/exhaustiveness.md`, which already sends a
library that wants to add cases to the wrapper, states it for 1.x, and the page on case types shows `IoError` as the
example of a newtype.

---

## 9. Migration and phases

### 9.1 Is it breaking?

| Phase | Breaking? | What changes meaning |
|---|---|---|
| 1a, bare cases | **yes, syntax**: `.Case` and `.member` are refused once commit 2 lands | nothing silently. A program that compiled before can only turn into an error, never into another program: a bare uppercase name that resolved before still does, unless the file also imports a name that the expected type has as a case - the renamed-case exception of 6.3 - which is now reported. A leading `.` line in a `match` body was the start of an arm or an error; a lowercase one now continues the arm, which no program that compiled before contained. |
| 1b, case types | no | nothing: a construction keeps the type it has (decision 3.4), `Shape.Circle` in a type position and `extend Shape.Circle` were errors, `as` is new syntax, `From<Case>` and a case type's `copy` are new members nobody could have written. `private case` becomes an error, and nothing in the repository writes it |
| 2, bodies and groups | no | a bodyless `fn` in a type with cases and a `case` inside a case were errors |
| 3, narrowing: **deferred** | yes, if it ever reserves `is` | a name `is` somewhere; the checker lists them |

### 9.2 Phase 1a in two commits, with the seed between

The compiler's own source writes `.Case` at about 6,000 of the roughly 8,000 sites of the repository, and the seed that
builds it has to read whatever it is written in. This is the recipe of COLLECTIONS.md 6a and CLAUDE.md for a syntax
change:

1. **Commit 1 — teach both forms.** The checker resolves bare contextual cases by the rule of 6.2 beside `.Case`,
   which keeps working; the parser keeps "a leading `.` in a `match` starts an arm", because the arms still start with
   one. The lint rule `bare-case` lands with its fix. Tier A and tier B (the lowering reads the resolution).
2. **Refresh and publish the seed.** `sh tools/refresh-seed.sh` after the commit is on `main` with tier A green, then
   the forge's `seed` workflow, so that CI and fresh clones bootstrap a compiler that knows the bare form.
3. **Commit 2 — migrate and refuse.** `torb lint --fix --rule bare-case` over `compiler/`, `std/`, `examples/`,
   `tests/`, `tools/` and `editors/`: it removes the dot where the bare name resolves to the same case, and writes
   `Owner.Case` where it would collide (the owner's name as the file can write it; a site where it cannot is listed
   for a hand fix). The code blocks of CONCEPT and of the pages under `docs/` that `docs check` type checks are
   rewritten by a script that takes the sites the fix reports for them and asserts the exact text it replaces - never
   by PowerShell; the historical records keep their text. Then the parser refuses `.Case` and `.member` with the
   fix of 6.6, `ImplicitMember` and `ImplicitVariant` leave the syntax tree except as the error, the canon rule
   `imported-case-patterns` and the `bare-case` rule are deleted, the language pages of section 8 are rewritten, the
   skill is regenerated, and the Decision Log gets the entries of 0.1.

Between the two commits no source may rely on the new continuation rule of 6.6, because the seed of commit 1 still reads
a leading `.` in a `match` body as an arm. The first use of it waits for the next seed.

### 9.3 Phases 1b, 2 and 3

- **Phase 1b** has no syntax change the compiler must read before it can build itself: `Shape.Circle` in a type
  position is a path the parser already accepts, and `as` is new syntax nobody uses yet. It lands in one commit; the
  seed is refreshed before the compiler's own source uses a case type or an `as` binding (a refactoring round of its
  own, for example `fn lowerBinary(node: Expression.Binary)`).
- **Phase 2** adds syntax (case bodies, nested cases) and lands the same way: one commit, then a seed before `std` or
  the compiler adopts it.
- **Phase 3 is deferred** (the owner, 2026-09-29). The evidence for it is collected while phase 2 is used: every place
  in the compiler and in `std` where `if const ... as` reads badly. Should it come, it reserves `is`, which is two
  commits as a new reserved word always is.
- **Beside the phases, before 1.0:** the newtype conversions of decision 7.3 (`IoError`, `OsError`, `DnsError`,
  `UriError`, the `ReadError` of `std/binary`), one package round each. They need no language feature of this record
  and may land before it.

### 9.4 Sizes

Estimated against the modules they touch (`checker/pattern.trb` 614 lines, `checker/exhaustive.trb` 864,
`checker/derive.trb` 682, `checker/member.trb` 947, `ir/lower/match.trb` 2069, `ir/lower/derive.trb` 1586):

| Phase | Compiler, written by hand | Tests | Mechanical | Rounds |
|---|---|---|---|---|
| 1a commit 1 | 600-900 lines: resolution, messages, completion, highlighting, the `bare-case` rule | ~400 lines | - | 1 |
| 1a commit 2 | ~300 lines removed (implicit members, the canon rule, the format mark) | the expectations of existing tests | ~8,000 sites in ~340 `.trb` files, ~530 in ~120 Markdown files (the generated skill included), ~15 language pages rewritten | 1, after the seed |
| 1b | 1,800-2,500 lines: checker ~1,500 (types, widening, conversion, lookup, `as`, `From<Case>`, derivation), lowering ~400 | ~800 lines, ~10 conformance programs | a new language page, six updated | 2-3 |
| 2 | 1,800-2,800 lines: parser ~300, checker ~1,200 (requirements, final members, groups), lowering ~500 | ~800 lines | case bodies documented | 2-3 |
| 3, deferred | `is` alone ~300 lines; with flow typing 1,000-1,500 | ~400 | - | 1-2, if ever |
| The newtypes of 7.3 | per type 100-300 lines in its package, plus the runtime's classification of an error number for `IoError` | the package's tests | the `match`es on the five types, rewritten to questions | one per package |

The record representation of decision 2.2 is not a phase: it is a measured change of `ir/layout.trb` and the lowering
of the two conversions, taken when a benchmark asks for it.

---

## 10. Risks and the answers of the owner

### Risks

- **The collision error of 6.2 lands where a file imports a type named like a case of the expected type** - the
  compiler's own style `case Reserved(keyword: Keyword)` next to a type `Keyword` is the typical site. The migration
  writes the qualified form there automatically, and the message names both declarations; the cost is that such a
  site reads `Token.Keyword(k)` and not `Keyword(k)`.
- **`None` of a type that is also used inside an `Option`** means the `Option`'s `None` where an `Option` is expected
  (6.3, last row). This is today's behaviour of `.None`, made more tempting by the bare form; the lint of section 8
  names such cases.
- **The erased representation spends space** for case types smaller than their widest sibling when they are stored in
  bulk (decision 2.2). The record representation is the answer when a benchmark shows it.
- **Case bodies bring a second place for per-case behaviour** beside `match self` (decision 5.2). The linter can point
  a type whose every case writes the same member as a single `match` in the closed trait, and the other way round.
- **Phase 1a is the largest mechanical change the repository has made since the `Iterable` rename**, and it touches
  every open branch; it should land when few are open.
- **The targets of 1.x are fixed at 1.0.** `OperatingSystem` and `Architecture` stay types with cases (decision 7.3),
  so a new operating system or architecture - RISC-V is the likely one - waits for 2.0. Should a target have to come
  within 1.x after all, the two would have to become newtypes before 1.0, because that change breaks every `match`
  on them.
- **The newtype conversions are breaking changes of five `std` packages** and belong before 1.0, each with the
  `match`es on its type rewritten to questions. A caller who matched a kind now asks a question, and loses the
  exhaustiveness check the `match` gave - which is the point of a type that may grow.

### The answers of the owner

The keyword and the leading dot were decided on 2026-09-28 (sections 1.1 and 6). The six questions this section held
were answered on 2026-09-29 and are decisions of the record now; nothing is open.

| Question | Answer | Recorded in |
|---|---|---|
| What the documentation calls it | "a type with cases" in the reference and in every message; "closed trait" where the model is explained | decision 1.1 |
| Requirements and case bodies | yes, in phase 2 | decisions 1.4, 1.5 and 5.2 |
| `Circle as circle` beside `Circle(...) as circle` | not now | decision 4.2 |
| Narrowing and `is` | not now: phase 3 is deferred, and the evidence is collected after phase 2 | decision 4.4, section 9.3 |
| How a public type with cases grows | not at all: it never grows within 1.x, and a type that must grow is a newtype; `private case` is refused | decision 7.3 |
| One spelling per case | `qualified-case` is a lint, not an error | section 6.7 |
