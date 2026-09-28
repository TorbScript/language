---
title: Names for values
summary: Give a value a name with const and use the name instead of the value; write var instead when the value has to change later.
kind: lesson
status: stable
order: 2
---

A value is a piece of information: a word, a number. When a program needs the same value more than once, it gives the
value a name and uses the name from then on.

`const name = "Ada"` means: from now on, `name` stands for `"Ada"`. Inside text, a name in curly braces is replaced by
its value, so `"Hello, {name}!"` becomes `Hello, Ada!`.

```trb run
const name = "Ada"
print "Hello, {name}!"
print "Goodbye, {name}!"
// prints Hello, Ada!
// prints Goodbye, Ada!
```

## A name whose value changes

`const` means the value stays the same for good. When the value has to change - a score, a counter - write `var`
instead, and give the name a new value with `=`.

`score = score + 10` reads: take the value of `score`, add 10, and make that the new value of `score`.

```trb run
var score = 0
score = score + 10
score = score + 5
print "Score: {score}"
// prints Score: 15
```

<details>
<summary>What happens if you change a const?</summary>

The program does not run. The computer checks the whole program before it runs a single line, and tells you where the
mistake is:

```trb error
const name = "Ada"
name = "Alan"
print name
// error: `name` is a `const`. Only a `var` binding can be changed
```

That check is a good thing: a value you said stays the same really does. A name for a value is called a
[binding](../glossary.md#binding), which is the word the error uses.

</details>

## Exercise

You have 3 coins and find 4 more. Add one line so the program prints `Coins: 7`.

```trb exercise
var coins = 3
print "Coins: {coins}"
```

<details>
<summary>Hint</summary>

Before the `print` line, give `coins` a new value: its old value plus 4.

</details>

<details>
<summary>Solution</summary>

```trb run
var coins = 3
coins = coins + 4
print "Coins: {coins}"
// prints Coins: 7
```

</details>

## Recap

`const` gives a value a name that never changes, `var` one that can, and `{name}` puts a value into text.
