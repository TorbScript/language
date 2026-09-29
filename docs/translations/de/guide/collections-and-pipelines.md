---
title: Kollektionen und Pipelines
summary: Wie du eine List, Map und Set baust, eine an Ort und Stelle änderst oder eine geänderte Kopie bekommst, und Werte durch eine faule Pipeline ziehst.
kind: guide
status: stable
order: 80
prerequisites:
  - errors.md
translates: 79f6b7c8db50
---

`List`, `Map` und `Set` sind Werte, also gilt die Bindungsregel aus
[Werte und Bindungen](values-and-bindings.md) für sie genauso wie für einen `Point`. Diese Seite baut von jeder eine,
ändert eine an Ort und Stelle, und liest eine Pipeline bis zu einem Ergebnis durch.

## Ziel

Am Ende dieser Seite kannst du eine `List`, eine `Map` und ein `Set` bauen, bei einer Kollektion ein Verb von seinem
Partizip unterscheiden, und eine Pipeline schreiben, die in einem Collector endet.

## Eine Kollektion bauen

```trb
const numbers = [1, 2, 3]
var ages = ["Ada": 36, "Grace": 45]
const primes = Set.of 2, 3, 5, 7

print numbers
print ages
print primes
```

`[1, 2, 3]` ist eine `List<Int>`, `["Ada": 36, "Grace": 45]` ist eine `Map<String, Int>`, und ein `Set` wird über eine
Factory gebaut, weil es kein eigenes Literal hat. Siehe [Lists](../language/collections-and-iteration/lists.md) und
[Maps and sets](../language/collections-and-iteration/maps-and-sets.md).

## Ein Verb ändert sie, sein Partizip nicht

```trb
var buffer = numbers
buffer.append 4
buffer[0] = 10
print numbers
print buffer
```

`buffer` ist eine Kopie von `numbers`, also rührt Wachsen und Schreiben in `buffer` `numbers` nie an – [Zuweisen ist
Kopieren](values-and-bindings.md) gilt für eine `List` genauso wie für einen `Point`. Jede Änderung hat ein Verb und
ein Partizip:

```trb
const more = numbers.appended(4).appended(5).removed(2)
print more
print numbers
```

`appended` und `removed` geben eine geänderte Kopie zurück und lassen `numbers` unberührt, funktionieren also über
eine `const`-Bindung. `append` und `remove` brauchen ein `var`. Jede Art hat die Wörter, die jeder dafür kennt – eine
Liste appended, ein Set inserted, eine Map setzt, ein Stack pusht und poppt, eine Queue enqueued und dequeued. Alle
davon stehen in [The collection traits](../language/collections-and-iteration/collection-traits.md).

## Lesen mit for

```trb
for number in numbers {
  print number
}

for (name, age) in ages {
  print "{name} is {age}"
}
```

Eine `Map` iteriert als `(key, value)`-Tupel. Was dabei einmal ausgewertet wird und was nicht, steht in
[Iterating](../language/collections-and-iteration/iterating.md).

## Eine Pipeline: faule Stufen, eine terminale Operation

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

`filter` und `map` sind faule [Stufen](../glossary.md#stage): Nach der Zuweisung an `seniorEngineers` ist noch nichts
gelaufen, weil eine Pipeline erst läuft, wenn eine terminale Operation die Werte durchzieht – hier `toList()`. Siehe
[Pipelines](../language/collections-and-iteration/pipelines.md).

Ein [Collector](../glossary.md#collector) ist eine wiederverwendbare Beschreibung dessen, was mit den Werten zu tun
ist, statt noch einer von Hand geschriebenen terminalen Operation:

```trb
const headcount = employees.collect counting()
const byDepartment = employees.collect(groupingBy { _.department })

print headcount
print byDepartment
```

Welche Collectors die Standardbibliothek mitbringt und wie du einen eigenen schreibst, steht in
[Collectors](../language/collections-and-iteration/collectors.md).

## Weiter

- [Control flow and your own constructs](control-flow-and-dsls.md) - warum `unless` eine Funktion ist, kein
  Schlüsselwort.
- [Slices](../language/collections-and-iteration/slices.md) - `list[from..to]` als Wert und als `var`-Pfad.
- [The collection traits](../language/collections-and-iteration/collection-traits.md) - `List`, `Map`, `Set`, `Stack`
  und `Queue` vollständig.
