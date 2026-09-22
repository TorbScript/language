---
title: Mutation and var paths
summary: A change needs an unbroken var path from the binding down to the field being changed, and a var parameter or a var fn receiver is a reference that cannot outlive the call it belongs to.
kind: reference
status: stable
order: 80
keywords:
  - var path
  - var parameter
  - reference
  - temporary
  - copy trap
source:
  - CONCEPT.md#var-paths-and-var-parameters
  - examples/tour/src/03-types.trb
---

Nothing in the language is changed through an alias. A change always goes through a **var path**: a `var` binding,
`var` parameter or a `var fn` receiver, then `var` fields, indices and ranges all the way down to the value that changes.

## Example

```trb
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

fn incrementTwice(var target: Counter) {
  target.increment()
  target.increment()
}

var counter = Counter()
incrementTwice counter
print counter.count
```

## Syntax

```text
<var-path> ::= (<var-binding> | <var-parameter> | "self" in a var-self member) <step>*
<step>     ::= "." <var-field> | "[" <index-or-range> "]"
```

## Rules

1. **A `var` parameter works on the caller's value, and its argument has to be a `var` path.** `incrementTwice`
   changes `counter` itself; there is no marker at the call site, the parameter's own `var` is what promises it.

   ```trb error
   type Counter {
     var count: Int = 0

     var fn increment() {
       count = count + 1
     }
   }

   fn incrementTwice(var target: Counter) {
     target.increment()
     target.increment()
   }

   const frozen = Counter()
   incrementTwice frozen
   // error: `incrementTwice` changes `target`, and `frozen` is a `const`
   ```

2. **A path through `a[key]` or `a[from..to]` changes the collection in place, without a copy.** `samples[1..4].sort {
   value => value }` sorts that slice of `samples` itself, the same way a field step would.

3. **A reference is second-class: it exists only as a `var` parameter or a `var fn` receiver, for the duration of one call.**
   It cannot be stored in a field, returned, or captured by a closure that outlives the call - which is exactly what
   keeps the language free of lifetimes and dangling references.

   ```trb error
   type Counter {
     var count: Int = 0

     var fn watcher(): () => Void {
       { count = count + 1 }
     }
   }
   // error: This closure captures the receiver `self` and may outlive the call
   ```

4. **A closure that captures a `var` binding may not escape its scope** - the same rule, and the same conservative
   check, as for a `var` parameter: the closure has to be written directly as the argument of a call that does not
   store it. The binding is shared between the closure and its scope for that call, and the call counts as an access
   to it. *Decided, and not yet enforced by the checker*: until it is, a closure that captures a `var` binding can
   still be bound to a name, stored or returned, and the binding then lives in a counted box - the aliasing, the race
   through `spawn` and the cycles that come from that are the reason for the rule.

   ```trb skip decided, not yet enforced by the checker
   fn makeCounter(): () => Int {
     var count = 0
     {
       count = count + 1
       count
     }
   }
   // error (planned): the closure captures `count`, a `var` binding, and escapes its scope
   ```

   State that has to outlive its scope is a [shared type](shared-types.md), which says it has an identity; recursion is
   a local `fn`, which captures nothing.

5. **The base of a var path can never be a temporary.** Changing the result of a call directly is always rejected,
   because the change would be thrown away the moment the call returns - the value has to be bound to a name first.

   ```trb error
   type Counter {
     var count: Int = 0

     var fn increment() {
       count = count + 1
     }
   }

   fn makeCounter(): Counter {
     Counter()
   }

   makeCounter().increment()
   // error: This is a temporary, and `increment` changes its receiver: the change would be thrown away
   ```

   As the *argument* of a `var` parameter a temporary is fine: the callee becomes its only owner, so nothing is ever
   written back anywhere. `tick(makeCounter())` needs nothing bound first.

6. **The variable of a `for` loop is a `const`.** Changing an element needs the path (`items[index].x = 1`) or a new
   collection built with `map`; the loop variable itself never becomes a `var` path. *(Decided, not implemented:
   `for var element in items` binds a `var` reference to each slot instead, over `MutableIndexed` - see the collections
   design record, section 3.11. Until it lands the path is the only way.)*

## What this is not

**Taking a value out of a collection is not a way to reach its path.** It is a copy, so changing it changes nothing
the collection holds - this is the copy trap, and reading `counters[0]` again is the only way to see whether a change
went where it was meant to.

```trb
var counters = [Counter(), Counter()]
counters[0].increment()          // Through the path: changes the element in place
print counters[0].count
```

```trb
var counters = [Counter(), Counter()]
var first = counters[0]          // A copy
first.increment()                // Changes the copy. `counters[0]` is still at 0.
print counters[0].count
```

## Related

- [Exclusivity](exclusivity.md) - the rule for when two `var` accesses to the same path may not overlap.
- [Fields](fields.md) - which field modifier a path is allowed to end on.
- [Declaring a type](declaring-a-type.md) - `var fn` on a verb, and its participle that needs no path at all.
- [Bindings](../values-and-types/bindings.md) - `const` and `var` on the binding a path starts from.

