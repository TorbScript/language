---
title: Pipelines
summary: A pipeline is a source, zero or more lazy stages and exactly one terminal operation, and nothing runs until the terminal operation pulls a value through.
kind: reference
status: stable
order: 70
keywords:
  - pipeline
  - lazy
  - terminal operation
  - through
  - Stage
source:
  - CONCEPT.md#pipelines-and-collectors
---

Working with an `Iterate` has three parts: a source (anything `Iterate`), zero or more lazy stages (`map`, `filter`,
`sorted`, `take`, ...) that describe a transformation without running it, and exactly one terminal operation that pulls
the values through and decides where they end up.

## Example

```trb check
type Employee {
  name: String
  department: String
  salary: Int
  age: Int
}

const employees = [
  Employee("Ada", "Engineering", 120, 36),
  Employee("Grace", "Engineering", 130, 45),
  Employee("Linus", "Operations", 90, 28),
]

const seniorEngineers = employees
  .filter { _.department == "Engineering" && _.age >= 40 }
  .sorted { _.name }
  .map { "{_.name} ({_.age})" }

print seniorEngineers.toList()
```

## Syntax

```text
source.filter { ... }.map { ... }.take(n)      lazy stages: each answers an Iterate again
source.through(stage)                          puts a Stage in front of the values instead of a method
.toList()  .to<Target>()  .fold(...)  .find(...)  .collect(...)      terminal operations: pull the values through
```

## Rules

1. **A lazy stage answers an `Iterate` and runs nothing when it is called.** `map`, `filter`, `filterMap`,
   `mapWhile`, `flatMap`, `take`, `skip`, `takeWhile`, `zip`, `indexed` and `sorted` all return a value that describes
   the next step; none of them touch the source until something pulls.

2. **A pipeline is a value: it can be stored, passed around, extended, and iterated more than once.** `sorted`
   composes with `map` before either one runs, and the result of that composition is itself something further stages
   can be added to.

   ```trb check
   const positive = [1, -2, 3, -4].filter { _ > 0 }
   print positive.toList()
   print(positive.map { _ * 10 }.toList())
   ```

3. **The catch of laziness: a stage with side effects does not run them until it is pulled.** Building `.map { ... }`
   runs the closure zero times; only a terminal operation runs it once per value.

   A stage keeps its closure, so the closure changes no `var` binding - it counts on an object, which every holder
   shares:

   ```trb check
   shared type Tally {
     var count: Int = 0
   }

   const calls = Tally()
   const doubled = [1, 2, 3].map { value =>
     calls.count = calls.count + 1
     value * 2
   }
   print calls.count
   print doubled.toList()
   print calls.count
   ```

4. **A terminal operation decides where the values end up, and there is exactly one per pipeline.**
   `toList()`, `to<Target>()` (any `From<Iterate<Item>>`), `fold`, `find`, `first`, `any`, `all`, `count`, `sum`,
   `joined(separator:)`, `forEach`, `for ... in`, and the general one, `collect` - see
   [Collectors](collectors.md).

5. **`sorted` is lazy as a stage, but it has to buffer every value the moment it is pulled.** Every other stage reads
   one value and answers one value (or none, or several, for `filter` and `flatMap`); `sorted` cannot answer its first
   value before it has seen the last one.

6. **`through(stage)` puts a reusable `Stage` value in front of the pipeline instead of a named method**, and the
   very same `Stage` also drives a `Source` once [Streams](../concurrency-and-streams/streams.md) run - a stage is
   written once and used from both directions.

## What this is not

**An unfinished pipeline is not printable, because it is not `Show`.** Only a collection a terminal operation actually
built - a `List`, a `Set`, a `Map` - implements `Show`; the lazy stage in between does not.

```trb check
const employees = [1, 2, 3]
print employees.filter({ _ > 1 }).toList()
```

```trb error
const employees = [1, 2, 3]
print employees.filter({ _ > 1 })
// error: `Iterate<Int64>` does not implement `Show`
```

## Related

- [Iterating](iterating.md) - `Iterate` and `Iterator`, which every stage is built from.
- [Collectors](collectors.md) - `collect`, the general terminal operation, and how to write one.
- [The collection traits](collection-traits.md) - the traits a pipeline's terminal operation can build back into.
