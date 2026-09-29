---
title: Collections and pipelines
summary: Build a list, a map and a set, change one in place or get a changed copy, and run values through a pipeline of steps.
kind: guide
status: stable
order: 90
prerequisites:
  - errors.md
keywords:
  - List
  - Map
  - Set
  - pipeline
  - map
  - filter
source:
  - CONCEPT.md#collections-and-iteration
  - examples/tour/src/07-collections.trb
---

`List`, `Map` and `Set` are values like any other, so a second name is a copy and a change needs a `var`.

## Goal

At the end of this page you can build and change a collection, loop over it, and turn it into another one with a
pipeline.

## Build and change

```trb run
var numbers = [1, 2, 3]
var ages = ["Ada": 36, "Grace": 45]
var seen: Set<String> = []

numbers.append 4
numbers[0] = 10
ages["Alan"] = 41
seen.insert "Ada"

print numbers
print ages
print seen
// prints [10, 2, 3, 4]
// prints ["Ada": 36, "Grace": 45, "Alan": 41]
// prints {"Ada"}
```

`[1, 2, 3]` is a `List<Int>` and `["Ada": 36]` is a `Map<String, Int>`. An empty collection is `[]`, or `[:]` for a
map, with the type on the binding. Each collection has its own word for adding: a list appends, a set inserts, a map
sets. All of them remove.

Every change has a twin that returns a changed copy and works on a `const`:

```trb run
const numbers = [1, 2, 3]
const more = numbers.appended(4).removed(1)
print "{numbers} {more}"
// prints [1, 2, 3] [2, 3, 4]
```

## Loop over it

```trb run
const ages = ["Ada": 36, "Grace": 45]
for (name, age) in ages {
  print "{name} is {age}"
}
// prints Ada is 36
// prints Grace is 45
```

`for` goes through a list value by value, and through a map as `(key, value)` pairs.

## Pipelines

```trb run
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

const seniors = employees.filter({ _.age >= 40 }).map({ _.name })
print seniors.toList()
print employees.filter({ _.department == "Engineering" }).count()
// prints ["Grace"]
// prints 2
```

`filter` and `map` are steps of a pipeline. They are lazy: nothing runs until a last step such as `toList()` or
`count()` pulls the values through, and no list is built in between. `sorted`, `take` and `flatMap` are steps as well;
`fold`, `find` and `count` end a pipeline.

## Next

- [Control flow and your own constructs](control-flow-and-dsls.md) - `if`, loops, and control structures you write
  yourself.
- Pipelines (skill `torbscript-language`: `references/language/collections-and-iteration/pipelines.md`) - every step, and how to end a pipeline.
- The collection traits (skill `torbscript-language`: `references/language/collections-and-iteration/collection-traits.md`) - `List`, `Map`, `Set`, `Stack`
  and `Queue` in full.

