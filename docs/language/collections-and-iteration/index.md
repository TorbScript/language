---
title: Collections and iteration
summary: List, Map, Set, Stack and Queue as traits over a shared Iterable, plus slices, pipelines and collectors.
kind: index
status: stable
order: 70
---

The collection types, one trait per kind; how `for` and a pipeline pull values out of anything `Iterable`; and how a
pipeline's terminal operation decides where they end up.

## What belongs here

Every collection trait and its default implementations, indexing and slicing, `for`, lazy pipeline stages and their
terminal operations, and collectors. What does not belong here: the two shared types that carry values between tasks
over time, `Source` and `Sink`, which are in
[`concurrency-and-streams/`](../concurrency-and-streams/index.md); the `String` type itself, which is in
[`values-and-types/`](../values-and-types/index.md) even though a `String` shares `Slice` with a `List`.

<!-- torb:index:begin -->

## Pages

- **[The collection traits](collection-traits.md)** - Every kind of collection is a trait - List, Set, Map, Stack, Queue - so a signature names what a value can do, and only its construction names the data structure behind it.
- **[Lists](lists.md)** - List is the ordered, indexable sequence behind the literal [1, 2, 3], with ArrayList as the default implementation and a verb paired with a participle for every change.
- **[Maps and sets](maps-and-sets.md)** - The literal ["a": 1] builds a Map, a Set is built from a list literal instead of having one of its own, and both iterate in insertion order while comparing regardless of it.
- **[Stacks and queues](stacks-and-queues.md)** - Stack is LIFO and Queue is FIFO, both spend the two words add and remove, and the type name says which end they reach.
- **[Slices](slices.md)** - list[from..to] answers a List that shares storage and starts at index 0 again; as a var path the same expression is a window into the original instead.
- **[Iterating](iterating.md)** - for pulls from Iterator.next() through Iterable.iterator(), and the subject of a for is evaluated once into a temporary, so changing it inside the loop does not affect what is walked.
- **[Pipelines](pipelines.md)** - A pipeline is a source, zero or more lazy stages and exactly one terminal operation, and nothing runs until the terminal operation pulls a value through.
- **[Collectors](collectors.md)** - A Collector describes what to do with the values of a pipeline; start() makes a fresh, push-based Accumulator for one run, and every Collection is one without extra work.

<!-- torb:index:end -->
