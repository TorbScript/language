---
title: Your own types
summary: A type of your own bundles values that belong together, like the name and the age of a pet, and can have functions of its own.
kind: lesson
status: stable
order: 8
---

Some values belong together: a pet has a name and an age. `type Pet { ... }` makes a new type that holds both. Each
value inside it is a [field](../glossary.md#field), with a name and a type.

`Pet "Rex", 3` makes a pet, with the values in the order the fields are written. A dot reads a field: `pet.name`.

```trb run
type Pet {
  name: String
  age: Int
}

const pet = Pet "Rex", 3
print "{pet.name} is {pet.age} years old."
// prints Rex is 3 years old.
```

## Functions that belong to a type

A function written inside the type belongs to it, and can use its fields by their names. It is called with a dot, like
`pet.greeting()`. Such a function is a [method](../glossary.md#method); you have met one already, `toUpperCase()`.

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

## Exercise

Give `Pet` a method `humanYears` that answers the age times 7, and print it for Rex, so the program prints `21`.

```trb exercise
type Pet {
  name: String
  age: Int
}

const pet = Pet "Rex", 3
print pet.age
```

<details>
<summary>Hint</summary>

Inside the type, below the fields, write `fn humanYears(): Int { ... }` with `age * 7` as its last line. Then print
`pet.humanYears()`.

</details>

<details>
<summary>Solution</summary>

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

## Recap

`type Name { ... }` bundles fields into a value of its own, and a function inside it is a method you call with a dot.
