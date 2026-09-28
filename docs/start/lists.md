---
title: Lists
summary: A list holds several values in order, a loop goes through them one by one, and giving a list a second name makes a copy.
kind: lesson
status: stable
order: 6
---

A list holds several values in a row, between square brackets. The values are numbered from 0, so `fruits[0]` is the
first one. `fruits.length()` says how many there are, and a `for` loop goes through them one after the other.

```trb run
const fruits = ["apple", "banana", "cherry"]
print fruits[0]
print fruits.length()
for fruit in fruits {
  print "I like {fruit}."
}
// prints apple
// prints 3
// prints I like apple.
// prints I like banana.
// prints I like cherry.
```

## Adding to a list

`append` adds a value at the end. It changes the list, so the list needs a `var`, just like a number you change.

```trb run
var shopping = ["bread"]
shopping.append "milk"
shopping.append "eggs"
print shopping
// prints ["bread", "milk", "eggs"]
```

## Two names, two lists

When you give a list a second name, you get a copy. Changing the copy leaves the first list alone, so nothing changes
behind your back: a value only changes where you change it.

```trb run
const original = ["bread"]
var copy = original
copy.append "milk"
print original
print copy
// prints ["bread"]
// prints ["bread", "milk"]
```

Many languages share one list between both names instead, and a change through one name shows up under the other.
TorbScript does not: every value behaves like a number here. The reference calls this
[value semantics](../glossary.md#value-semantics).

## Exercise

Add up the prices with a `for` loop, so the program prints `Total: 16`.

```trb exercise
const prices = [3, 8, 5]
var total = 0
print "Total: {total}"
```

<details>
<summary>Hint</summary>

Go through the list with `for price in prices { ... }` and add each `price` to `total`.

</details>

<details>
<summary>Solution</summary>

```trb run
const prices = [3, 8, 5]
var total = 0
for price in prices {
  total = total + price
}
print "Total: {total}"
// prints Total: 16
```

</details>

## Recap

A list keeps values in order from position 0, `append` adds to a `var` list, and a second name is a copy of its own.
