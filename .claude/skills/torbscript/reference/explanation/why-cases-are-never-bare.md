---
title: Why a case is never bare
summary: Circle alone is a type, a function or a variable, exactly like every other name, so a case is written Shape.Circle, .Circle or imported by its path, and a misspelled case can never fall back to matching everything.
kind: explanation
status: stable
order: 50
keywords:
  - bare case
  - import
  - Some
  - None
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

Swift lets a bare `Circle` mean a case when one is in scope; Rust lets a bare name in a pattern mean a case if one is
in scope and a fresh binding otherwise. Both rules answer the same question - "what does this name mean here?" - by
looking at what else happens to be around. TorbScript answers it from one place: the `use` at the top of the file.

## The decision

**A case is written with its type, with a leading dot, or is imported - and never bare.** `Circle` alone is a type, a
function or a binding of that name, exactly like every other name in the language.

- `Shape.Circle(2.0)` names the type; `.Circle(2.0)` uses one wherever a type is already expected - an annotation, a
  parameter, a field, the other side of `==`, an arm of a `match` whose result is expected.
- `use Shape.Circle from "./shape"` imports a case by its path, the same way any other name is imported. From then on
  it needs nothing in front of it, in an expression and in a pattern.
- `Some`, `None`, `Ok` and `Fail` are bare for exactly this reason: the prelude imports them from `Option` and
  `Result`. There is nothing else special about them.

```trb check
type Shape {
  case Circle(radius: Float)
  case Empty
}

use Shape.Circle

const shape = Circle 2.0
const other: Shape = .Empty
print "{shape} {other}"
```

## Why

**Because "is this name a case?" must not depend on what else is in scope.** Under Swift's rule, adding an unrelated
case to some other type in scope can turn a name that used to be a binding into a case, or the reverse - the meaning
of a line changes because of a declaration far away from it. Requiring an import makes the file's own `use` list the
one place that decides, and a name's meaning stops depending on the rest of the program.

**Because a misspelled case must fail loudly instead of matching everything.** Where a bare name falls back to being a
binding when no case matches - Rust's rule for an unqualified pattern - a typo turns an arm into a catch-all: `Nome =>`
compiles, binds the whole value to `Nome`, and silently swallows every input the arm was supposed to reject. Requiring
the type or an import closes that hole: an unimported `Circle` is "`Circle` is not a case in scope," an error at the
line it happens on, never a pattern that quietly matches.

**Because it makes the general rule "an uppercase name is a case or a type, a lowercase name is a binding" hold without
an exception for cases still fresh in scope.** [Why every match is exhaustive](why-exhaustive-matches.md) already
needs that rule to reject an uppercase binding as a compile error; a bare unimported case would be the one uppercase
name that could mean either, and the rule would need a special case just for it.

### What was rejected

- **A bare case whenever one is in scope**, as in Swift. Rejected because "in scope" depends on every other type
  declared nearby, so the same line can mean different things as the file around it changes.
- **A bare name that is a case if one exists and a binding otherwise**, as in Rust's patterns before an explicit `use`.
  Rejected because a misspelled case degrades into a silently matching binding instead of an error.
- **`use Option.*`**, which would make a file's cases change because a dependency gained one. A case is imported by
  its own path instead, one name at a time.

## Consequences

**Every case a file uses without a type in front of it has a `use` line for it.** `use Shape.Circle from "./shape"`
reads exactly like the name written after it in code, so a reader can find where a bare case comes from without
guessing.

**Writing the type out is always available, and is the default where nothing imports the case.** A file that matches
on a `Shape` it does not otherwise use writes `.Circle(radius) =>` inside the `match` (the type is known from the
subject) and `Shape.Circle(2.0)` everywhere else.

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Circle(2.0)
// error: Cannot find `Circle` here
```

**In a pattern the first letter still decides, so `Some(found)` and `None` read the same as a case that keeps its
dot.** An uppercase name in a pattern is a case in scope or a type; a lowercase one is a binding; an uppercase binding
is a compile error. What a pattern name means never depends on the type that is expected of the match, only on what
the file imports - see [Importing cases](../language/pattern-matching/importing-cases.md).

## Related

- [Cases and match](../language/pattern-matching/cases-and-match.md) - the full syntax of a case and a `match`.
- [Importing cases](../language/pattern-matching/importing-cases.md) - `use Shape.Circle from "./shape"` in full.
- [Why every match is exhaustive](why-exhaustive-matches.md) - the rule that a name's meaning never depends on the
  expected type, applied to exhaustiveness.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the bare-case mistake, with the
  diagnostic.
