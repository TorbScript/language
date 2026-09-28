---
title: Your first program
summary: A program is a list of instructions that the computer follows from top to bottom, and print is the instruction that shows a line of text.
kind: lesson
status: stable
order: 1
---

A program is a list of instructions. The computer reads them from the top down and does one after the other, exactly
as written.

The instruction `print` shows a line of text. The text goes between double quotes, so the computer knows where it
starts and where it ends.

The lines that start with `//` are comments: notes for people, which the computer skips. In these lessons, a comment
that says `prints` shows what the program prints. Try it: change the text, then run the program again.

```trb run
print "Hello!"
print "I am learning to program."
// prints Hello!
// prints I am learning to program.
```

## Exercise

Change the program so it prints two lines: first `Hello, Ada!`, then `Nice to meet you.`

```trb exercise
print "Hello, World!"
```

<details>
<summary>Hint</summary>

Change the text between the quotes, then add a second line that starts with `print`.

</details>

<details>
<summary>Solution</summary>

```trb run
print "Hello, Ada!"
print "Nice to meet you."
// prints Hello, Ada!
// prints Nice to meet you.
```

</details>

## Recap

A program runs from top to bottom, one line after the other, and `print "..."` shows a line of text.
