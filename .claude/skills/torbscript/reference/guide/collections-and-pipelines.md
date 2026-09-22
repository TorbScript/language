---
title: Collections and pipelines
summary: How to build a list, map and set, change one in place or get a changed copy, and pull values through a lazy pipeline.
kind: guide
status: stable
order: 80
prerequisites:
  - errors.md
keywords:
  - List
  - Map
  - Set
  - pipeline
  - collector
source:
  - CONCEPT.md#collections-and-iteration
  - examples/tour/src/07-collections.trb
---

`List`, `Map` and `Set` are values, so the binding rule from
[Values and bindings](values-and-bindings.md) applies to them exactly as it does to a `Point`. This page builds one of
each, changes one in place, and reads a pipeline through to a result.

## Goal

At the end of this page you can build a `List`, a `Map` and a `Set`, tell a verb from its participle on a collection,
and write a pipeline that ends in a collector.

## Building a collection

```trb
const numbers = [1, 2, 3]
var ages = ["Ada": 36, "Grace": 45]
const primes = Set.of 2, 3, 5, 7

print numbers
print ages
print primes
```

`[1, 2, 3]` is a `List<Int>`, `["Ada": 36, "Grace": 45]` is a `Map<String, Int>`, and a `Set` is built with a factory
because it has no literal of its own. See [Lists](../language/collections-and-iteration/lists.md) and
[Maps and sets](../language/collections-and-iteration/maps-and-sets.md).

## A verb changes it, its participle does not

```trb
var buffer = numbers
buffer.add 4
buffer[0] = 10
print numbers
print buffer
```

`buffer` is a copy of `numbers`, so growing and writing into `buffer` never touches `numbers` -
[Assigning is copying](values-and-bindings.md) holds for a `List` exactly as it does for a `Point`. Every change has a
verb and a participle:

```trb
const more = numbers.added(4).added(5).removed(2)
print more
print numbers
```

`added` and `removed` answer a changed copy and leave `numbers` untouched, so they work through a `const` binding.
`add` and `remove` need a `var`. See
[The collection traits](../language/collections-and-iteration/collection-traits.md) for the full vocabulary, shared by
`List`, `Map`, `Set`, `Stack` and `Queue`.

## Reading with for

```trb
for number in numbers {
  print number
}

for (name, age) in ages {
  print "{name} is {age}"
}
```

A `Map` iterates as `(key, value)` tuples. See [Iterating](../language/collections-and-iteration/iterating.md) for
what evaluates once and what does not.

## A pipeline: lazy stages, one terminal operation

```trb
type Employee {
  name: String
  department: String
  age: Int
}

const employees = [
  Employee("Ada", "Engineering", 36),
  Employee("Grace", "Engineering", 45),
  Employee("Linus", "Operations", 28),
]

const seniorEngineers = employees
  .filter { _.department == "Engineering" && _.age >= 40 }
  .map { _.name }

print seniorEngineers.toList()
```

`filter` and `map` are lazy [stages](../glossary.md#stage): nothing has run yet after the assignment to
`seniorEngineers`, because a pipeline only runs when a terminal operation pulls the values through - here `toList()`.
See [Pipelines](../language/collections-and-iteration/pipelines.md).

A [collector](../glossary.md#collector) is a reusable description of what to do with the values instead of one more
terminal operation written by hand:

```trb
const headcount = employees.collect counting()
const byDepartment = employees.collect(groupingBy { _.department })

print headcount
print byDepartment
```

See [Collectors](../language/collections-and-iteration/collectors.md) for the collectors the standard library ships
and how to write your own.

## Next

- [Control flow and your own constructs](control-flow-and-dsls.md) - why `unless` is a function, not a keyword.
- [Slices](../language/collections-and-iteration/slices.md) - `list[from..to]` as a value and as a `var` path.
- [The collection traits](../language/collections-and-iteration/collection-traits.md) - `List`, `Map`, `Set`, `Stack`
  and `Queue` in full.

