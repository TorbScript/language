---
title: Functions
summary: A function gives a few lines a name, so they can run again with a different value each time, and it can hand a result back.
kind: lesson
status: stable
order: 7
---

A function is a few lines with a name. `fn greet(name: String)` makes a function called `greet` that needs one value,
a `String` it calls `name`. Writing `greet "Ada"` runs its lines with `name` standing for `"Ada"`.

Calling a function looks like `print`, and that is no accident: `print` is a function too.

```trb run
fn greet(name: String) {
  print "Hello, {name}!"
}

greet "Ada"
greet "Alan"
// prints Hello, Ada!
// prints Hello, Alan!
```

## A function that answers

A function can hand a value back. `: Int` after the parentheses says what kind of value comes back, and the last line of
the function is that value. A call that is part of something bigger - like `double(21)` inside `print` - keeps its
value in parentheses.

```trb run
fn double(number: Int): Int {
  number * 2
}

print double(21)
print(double(5) + 1)
// prints 42
// prints 11
```

## Exercise

Write a function `square` that takes an `Int` and gives back the number times itself, so the program prints `25`.

```trb exercise incomplete
// Write the function square here

print square(5)
```

<details>
<summary>Hint</summary>

Start like `double` above: `fn square(number: Int): Int { ... }`, with `number * number` as its last line.

</details>

<details>
<summary>Solution</summary>

```trb run
fn square(number: Int): Int {
  number * number
}

print square(5)
// prints 25
```

</details>

## Recap

`fn name(value: Type) { ... }` makes a function, `: Type` after the parentheses says what it gives back, and its last
line is the answer.
