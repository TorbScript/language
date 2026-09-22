---
title: Why verbs and participles
summary: A method that changes in place and the method that returns a changed copy are different words, sort against sorted, because value semantics make one name for both ambiguous at the call site.
kind: explanation
status: stable
order: 70
keywords:
  - verb
  - participle
  - sort
  - sorted
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#collections-and-iteration
---

`sort` changes a list in place; `sorted` returns a new one. Two words for what looks like one idea cost every reader a
second name to learn, so this page argues for why the two words earn their keep.

## The decision

**A verb changes its receiver in place and is a `var fn`; its participle reads its receiver and returns a changed
copy, under a different name.** `add`/`added`, `sort`/`sorted`, `remove`/`removed`, `insert`/`inserted`.

- The participles are default members of the collection traits, written once in terms of the verb: copy, change the
  copy, return it. An implementation only has to write the verb.
- Calling the verb through a `const` path is a compile error that names the participle, so the fix is one word away.

```trb check
var numbers = [3, 1, 2]
numbers.sort { _ }
print numbers

const numbers2 = [3, 1, 2]
const ascending = numbers2.sorted({ _ }).toList()
print "{numbers2} {ascending}"
```

## Why

**Because value semantics make one name for both readings ambiguous at the call site, not just in documentation.**
Under [mutable value semantics](why-values-instead-of-references.md) `list.sort()` and `const sorted = list.sort()`
would both be legal syntax with one shared name: the first line changes `list`, the second one only looks like it
does. A single overloaded `sort` cannot tell a reader which happened without them checking whether the result was
used - exactly the ambiguity `Sort.sort` in languages with reference semantics never has, because there a mutating
method returning `Void` already signals "this is the only effect."

**Because the wrong one is now a compile error instead of a quiet copy.** Calling the verb on a `const` binding fails
immediately and names the participle in the message, so the fix is mechanical:

```trb error
const numbers = [3, 1, 2]
numbers.sort { _ }
// error: `sort` needs a `var`
```

**Because a name says what happens without the reader tracing the return value.** `list.sorted()` reads as "the sorted
list," which is what it returns; `list.sort()` reads as an instruction, which is what it does. Neither name needs the
call site's context (was the result assigned? was it discarded?) to be understood, unlike a single `sort` that means
either depending on how it is used.

**Because it costs nothing per implementation.** The participles are default members of `Collection`, `List`, `Set`
and the rest, each written once against the verb they pair with. A type that implements `add` gets `added` for free;
nobody writes the copy-and-call-through logic twice.

### What was rejected

- **One name plus a `copy { ... }` block** to opt into a copy where needed. Rejected because it moves the ambiguity
  from the method name to a block that is easy to forget, and the forgetting is silent - the call still compiles and
  changes the original.
- **Ruby's `!` suffix** for the mutating form. Rejected because a single punctuation mark is easy to miss in review and
  reads as an intensity marker rather than a different operation; `sort`/`sorted` needs no footnote to explain what the
  mark means.
- **Scala's symbolic operators** for the copy-returning side. Rejected for the same reason bit operators were
  (see [Why there are no bit operators](why-no-bit-operators.md)): a symbol needs a legend, a word does not.

## Consequences

**Every collection member that could mutate has two names to choose between, and the table is fixed rather than
invented per type.** `add`/`added`, `remove`/`removed`, `sort`/`sorted`, `insert`/`inserted` - one pair per meaning,
spelled the same on every collection, and a participle answers `Self` so it can be chained. See
[Verbs and participles](../language/types/verbs-and-participles.md) for the complete table.

**A type built on the collection traits writes the verb once and gets the participle for free**, which is what keeps
the standard library from doubling in size:

```trb check
type Point {
  var x: Int
  var y: Int

  var fn translate(deltaX: Int, deltaY: Int) {
    x = x + deltaX
    y = y + deltaY
  }

  fn translated(deltaX: Int, deltaY: Int): Point {
    copy(x: x + deltaX, y: y + deltaY)
  }
}

var point = Point x: 0, y: 0
point.translate deltaX: 1, deltaY: 1
const moved = Point(x: 0, y: 0).translated(deltaX: 1, deltaY: 1)
print "{point} {moved}"
```

**A new verb/participle pair follows the same rule when a type introduces one of its own**: the verb is a `var fn`
and changes the receiver, the participle only reads it, is named for what it returns, and is typically written in terms
of the verb through `copy`.

## Related

- [Verbs and participles](../language/types/verbs-and-participles.md) - the complete table and the generated
  diagnostic.
- [Why values instead of references](why-values-instead-of-references.md) - why a change needs a `var` path at all.
- [Why a change that cannot be seen is an error](why-dead-changes-are-errors.md) - the other half of the same rule,
  for a `var` that is never read.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the copy trap this naming makes
  visible.
