---
title: Making decisions
summary: With if and else a program does one thing or another, depending on a question whose answer is true or false.
kind: lesson
status: stable
order: 4
---

A program often has to choose: take a jacket or not. `if` asks a question, and runs the lines in its curly braces only
when the answer is yes. `else` holds what runs when the answer is no.

The question compares two values: `<` is less than, `>` greater than, `<=` and `>=` include the value itself, `==` is
equal and `!=` is not equal. Note the two equals signs: one `=` gives a name a value, two compare.

```trb run
const temperature = 8
if temperature < 10 {
  print "Take a jacket."
} else {
  print "No jacket needed."
}
// prints Take a jacket.
```

## More than two ways

`else if` asks the next question when the one before was answered no. The computer goes from the top and takes the
first way whose answer is yes.

```trb run
const hour = 14
if hour < 12 {
  print "Good morning"
} else if hour < 18 {
  print "Good afternoon"
} else {
  print "Good evening"
}
// prints Good afternoon
```

The answer to a question is a value too: `true` or `false`. Its type is `Bool`, and a name can hold it like any other
value: `const isCold = temperature < 10`.

## Exercise

Make the program print `Adult` when `age` is 18 or more, and `Child` otherwise. With the age 20, it prints `Adult`.

```trb exercise
const age = 20
print "Child"
```

<details>
<summary>Hint</summary>

Ask `if age >= 18`, print `Adult` in its braces, and move `print "Child"` into the braces of an `else`.

</details>

<details>
<summary>Solution</summary>

```trb run
const age = 20
if age >= 18 {
  print "Adult"
} else {
  print "Child"
}
// prints Adult
```

</details>

## Recap

`if` runs its lines when the answer to its question is `true`, `else` when it is `false`, and `==` compares while `=`
gives a value.
