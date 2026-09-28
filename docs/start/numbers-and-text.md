---
title: Numbers and text
summary: Programs calculate with + - * and /, join text with +, and every value has a type that says what kind of value it is.
kind: lesson
status: stable
order: 3
---

A program can calculate: `+` adds, `-` subtracts, `*` multiplies and `/` divides. When what you print is a
calculation, put it in parentheses, so the computer knows it all belongs to `print`.

Whole numbers stay whole: `7 / 2` is `3`, with the rest dropped. For a result with a decimal point, write the numbers
with one: `7.0 / 2.0` is `3.5`.

```trb run
const price = 4
const count = 3
print(price * count)
print(7 / 2)
print(7.0 / 2.0)
// prints 12
// prints 3
// prints 3.5
```

## Text

Text is joined with `+`, and it has things it can do, which are written after a dot: `full.toUpperCase()` gives the
same text in capital letters.

```trb run
const first = "Ada"
const last = "Lovelace"
const full = first + " " + last
print full
print full.toUpperCase()
// prints Ada Lovelace
// prints ADA LOVELACE
```

## Every value has a type

A whole number like `12` is an `Int`, a number with a decimal point like `3.5` is a `Float`, and text like `"Ada"` is a
`String`. That kind of value is called its [type](../glossary.md#type).

The type decides what works: text and a number cannot be added with `+`, because it is unclear what that should mean.
To put a number into text, use curly braces: `"Age: {age}"`.

<details>
<summary>What happens if you add text and a number?</summary>

The computer finds it before the program runs:

```trb error
print "Age: " + 36
// error: Expected `String`, found `Int64`
```

`Int64` is the full name of `Int`: a whole number that fits in 64 bits.

</details>

## Exercise

A ticket costs 12 and you buy 3. Make the program print `Total: 36` by calculating the total instead of printing 0.

```trb exercise
const pricePerTicket = 12
const tickets = 3
print "Total: 0"
```

<details>
<summary>Hint</summary>

Give the total a name with `const total = ...`, then put `{total}` into the text.

</details>

<details>
<summary>Solution</summary>

```trb run
const pricePerTicket = 12
const tickets = 3
const total = pricePerTicket * tickets
print "Total: {total}"
// prints Total: 36
```

</details>

## Recap

Numbers calculate with `+ - * /`, text joins with `+`, and every value has a type: `Int`, `Float` or `String`.
