---
title: Kollektionen und Pipelines
summary: Eine Liste, eine Map und eine Menge bauen, eine an Ort und Stelle ändern oder eine geänderte Kopie bekommen, und Werte durch eine Pipeline aus Schritten schicken.
kind: guide
status: stable
order: 90
prerequisites:
  - errors.md
translates: 45ddde66f5cc
---

`List`, `Map` und `Set` sind Werte wie alle anderen, also ist ein zweiter Name eine Kopie, und eine Änderung braucht
ein `var`.

## Ziel

Am Ende dieser Seite kannst du eine Kollektion bauen und ändern, über sie laufen und sie mit einer Pipeline in eine
andere verwandeln.

## Bauen und ändern

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

`[1, 2, 3]` ist eine `List<Int>` und `["Ada": 36]` eine `Map<String, Int>`. Eine leere Kollektion ist `[]`, oder `[:]`
für eine Map, mit dem Typ an der Bindung. Jede Kollektion hat ihr eigenes Wort fürs Hinzufügen: Eine Liste hängt an
(`append`), eine Menge fügt ein (`insert`), eine Map setzt (`set`). Alle entfernen (`remove`).

Jede Änderung hat einen Zwilling, der eine geänderte Kopie zurückgibt und auf einem `const` funktioniert:

```trb run
const numbers = [1, 2, 3]
const more = numbers.appended(4).removed(1)
print "{numbers} {more}"
// prints [1, 2, 3] [2, 3, 4]
```

## Darüber laufen

```trb run
const ages = ["Ada": 36, "Grace": 45]
for (name, age) in ages {
  print "{name} is {age}"
}
// prints Ada is 36
// prints Grace is 45
```

`for` geht eine Liste Wert für Wert durch, und eine Map als `(key, value)`-Paare.

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

`filter` und `map` sind Schritte einer Pipeline. Sie sind faul: Nichts läuft, bis ein letzter Schritt wie `toList()`
oder `count()` die Werte durchzieht, und dazwischen wird keine Liste gebaut. `sorted`, `take` und `flatMap` sind
ebenfalls Schritte; `fold`, `find` und `count` beenden eine Pipeline.

## Weiter

- [Kontrollfluss und eigene Konstrukte](control-flow-and-dsls.md) - `if`, Schleifen und Kontrollstrukturen, die du
  selbst schreibst.
- [Pipelines](../language/collections-and-iteration/pipelines.md) - jeder Schritt, und wie eine Pipeline endet.
- [The collection traits](../language/collections-and-iteration/collection-traits.md) - `List`, `Map`, `Set`, `Stack`
  und `Queue` vollständig.
