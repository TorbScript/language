---
title: Repeating things
summary: A for loop runs the same lines once for every number of a range, and a while loop runs them as long as a question is answered yes.
kind: lesson
status: stable
order: 5
---

Computers are good at doing the same thing many times. A `for` loop runs the lines in its curly braces once for every
number from a start to an end, and gives the number a name each time round.

`1..=3` means the numbers from 1 up to and including 3. `1..3` stops before the 3.

```trb run
for round in 1..=3 {
  print "Round {round}"
}
print "Done!"
// prints Round 1
// prints Round 2
// prints Round 3
// prints Done!
```

## As long as

A `while` loop asks a question before every round, and stops as soon as the answer is no. Something inside the loop has
to change the answer, or the loop never ends.

```trb run
var countdown = 3
while countdown > 0 {
  print countdown
  countdown = countdown - 1
}
print "Liftoff!"
// prints 3
// prints 2
// prints 1
// prints Liftoff!
```

## Exercise

Add up all numbers from 1 to 10 with a `for` loop, so the program prints `55`.

```trb exercise
var sum = 0
print sum
```

<details>
<summary>Hint</summary>

Put a loop `for number in 1..=10 { ... }` between the two lines, and in it write `sum = sum + number`.

</details>

<details>
<summary>Solution</summary>

```trb run
var sum = 0
for number in 1..=10 {
  sum = sum + number
}
print sum
// prints 55
```

</details>

## Recap

`for number in 1..=10` repeats for every number of a range, and `while` repeats as long as its question is answered
yes.
