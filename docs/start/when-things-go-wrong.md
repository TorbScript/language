---
title: When something goes wrong
summary: A function that can fail says so and answers Ok with its result or Fail with the reason, so every failure is handled where it happens.
kind: lesson
status: stable
order: 11
---

Some things can fail: text that should be a number is not one, a file is not there. A function that can fail says so,
and answers one of two cases: `Ok` with its result, or `Fail` with the reason.

`Int.tryFrom("42")` turns text into a whole number. It answers `Ok(42)` for `"42"`, and `Fail` for `"forty-two"`. A
`match` handles both.

```trb run
for typed in ["42", "forty-two"] {
  match Int.tryFrom(typed) {
    Ok(number) => print "{typed} is the number {number}."
    Fail(_) => print "{typed} is not a number."
  }
}
// prints 42 is the number 42.
// prints forty-two is not a number.
```

`_` is a name for a value you do not need, here the details of the failure.

## Why no hidden jumps

In many languages, a failure jumps out of the middle of the program to some place far away that catches it - or ends the
program when nothing does. That jump is called an exception, and the code does not show where it can happen.

TorbScript has no exceptions. A failure is an ordinary value, the type of the function says it can fail, and you see
every place where something can go wrong. That type is a [Result](../glossary.md#result).

## Your own function that can fail

`Result<Int, String>` after the parentheses says: the answer is an `Int` when it works, and a `String` with the reason
when it does not. `return Fail "..."` ends the function early with a failure.

```trb run
fn share(total: Int, people: Int): Result<Int, String> {
  if people == 0 {
    return Fail "there is nobody to share with"
  }
  Ok(total / people)
}

match share(12, 0) {
  Ok(each) => print "Each gets {each}."
  Fail(reason) => print "Cannot share: {reason}."
}
// prints Cannot share: there is nobody to share with.
```

## Exercise

A ticket costs 10, but an age below 0 is a mistake. Make `ticketPrice` fail with the reason `an age cannot be negative`
for such an age, so the program prints `Error: an age cannot be negative`.

```trb exercise
fn ticketPrice(age: Int): Result<Int, String> {
  Ok 10
}

match ticketPrice(-3) {
  Ok(price) => print "Price: {price}"
  Fail(reason) => print "Error: {reason}"
}
```

<details>
<summary>Hint</summary>

Before `Ok 10`, ask `if age < 0 { ... }` and `return Fail "an age cannot be negative"` inside it.

</details>

<details>
<summary>Solution</summary>

```trb run
fn ticketPrice(age: Int): Result<Int, String> {
  if age < 0 {
    return Fail "an age cannot be negative"
  }
  Ok 10
}

match ticketPrice(-3) {
  Ok(price) => print "Price: {price}"
  Fail(reason) => print "Error: {reason}"
}
// prints Error: an age cannot be negative
```

</details>

## Recap

What can fail answers `Ok(result)` or `Fail(reason)`, there are no exceptions, and `match` handles both where it happens.
