---
title: When a value might be missing
summary: When a value might not be there - the fourth item of a list with three - the answer says so, and ?? or match decide what happens then.
kind: lesson
status: stable
order: 10
---

A list of three names has no fourth one. `winners[3]` would stop the program, because there is nothing to give back.
`winners.get(3)` asks politely instead: it answers either the value or nothing, and you decide what to do.

`??` is the simplest way to decide: `winners.get(3) ?? "nobody"` is the value when there is one, and `"nobody"` when
there is not.

```trb run
const winners = ["Ada", "Alan", "Grace"]
const third = winners.get(2) ?? "nobody"
const fourth = winners.get(3) ?? "nobody"
print "Third place: {third}"
print "Fourth place: {fourth}"
// prints Third place: Grace
// prints Fourth place: nobody
```

## Something or nothing

The answer of `get` is one of two cases, like the weather of the last lesson: `Some(name)` when there is a value, and
`None` when there is not. `match` can take it apart, and has to handle both.

```trb run
const winners = ["Ada", "Alan", "Grace"]
match winners.get(0) {
  Some(name) => print "The winner is {name}!"
  None => print "Nobody won."
}
// prints The winner is Ada!
```

Many languages have a special value for "nothing" that fits everywhere, and a program crashes when it forgets to check
for it. In TorbScript "maybe nothing" is a type of its own, an [Option](../glossary.md#option), so the computer notices
when a check is missing.

## Exercise

Print the third guest, or `nobody` when there is no third one. With two guests, the program prints `nobody`.

```trb exercise
const guests = ["Ada", "Alan"]
print guests[0]
```

<details>
<summary>Hint</summary>

Ask with `guests.get(2)`, and use `?? "nobody"` for the case that it answers nothing. Give the result a name before you
print it.

</details>

<details>
<summary>Solution</summary>

```trb run
const guests = ["Ada", "Alan"]
const third = guests.get(2) ?? "nobody"
print third
// prints nobody
```

</details>

## Recap

A value that might be missing is `Some(value)` or `None`, and `??` or `match` decide what happens when it is `None`.
