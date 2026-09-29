---
title: Eigene Typen
summary: Ein eigener Typ bündelt Werte, die zusammengehören, etwa den Namen und das Alter eines Haustiers, und kann eigene Funktionen haben.
kind: lesson
status: stable
order: 8
translates: 5c342730eddb
---

Manche Werte gehören zusammen: Ein Haustier hat einen Namen und ein Alter. `type Pet { ... }` macht einen neuen Typ,
der beides hält. Jeder Wert darin ist ein [Feld](../glossary.md#field), mit einem Namen und einem Typ.

`Pet "Rex", 3` macht ein Haustier, mit den Werten in der Reihenfolge, in der die Felder geschrieben sind. Ein Punkt
liest ein Feld: `pet.name`.

```trb run
type Pet {
  name: String
  age: Int
}

const pet = Pet "Rex", 3
print "{pet.name} is {pet.age} years old."
// prints Rex is 3 years old.
```

## Funktionen, die zu einem Typ gehören

Eine Funktion, die innerhalb des Typs geschrieben wird, gehört zu ihm und kann seine Felder bei ihrem Namen benutzen.
Sie wird mit einem Punkt aufgerufen, wie `pet.greeting()`. So eine Funktion ist eine [Methode](../glossary.md#method);
eine hast du schon kennengelernt, `toUpperCase()`.

```trb run
type Pet {
  name: String
  age: Int

  fn greeting(): String {
    "Woof! I am {name}."
  }
}

const pet = Pet "Rex", 3
print pet.greeting()
// prints Woof! I am Rex.
```

## Übung

Gib `Pet` eine Methode `humanYears`, die das Alter mal 7 zurückgibt, und gib sie für Rex aus, sodass das Programm
`21` ausgibt.

```trb exercise
type Pet {
  name: String
  age: Int
}

const pet = Pet "Rex", 3
print pet.age
```

<details>
<summary>Tipp</summary>

Schreib innerhalb des Typs, unter den Feldern, `fn humanYears(): Int { ... }` mit `age * 7` als letzter Zeile. Gib
dann `pet.humanYears()` aus.

</details>

<details>
<summary>Lösung</summary>

```trb run
type Pet {
  name: String
  age: Int

  fn humanYears(): Int {
    age * 7
  }
}

const pet = Pet "Rex", 3
print pet.humanYears()
// prints 21
```

</details>

## Zusammenfassung

`type Name { ... }` bündelt Felder zu einem eigenen Wert, und eine Funktion darin ist eine Methode, die du mit einem
Punkt aufrufst.
