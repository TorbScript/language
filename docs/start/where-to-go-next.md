---
title: Where to go next
summary: Install TorbScript to run programs on your own computer, and pick what to read next - the guide, the reference, or a project of your own.
kind: lesson
status: stable
order: 13
---

You have written programs with names, decisions, loops, lists, functions, types, cases, and answers that can be missing
or fail. That is the core of programming, in any language.

To run programs on your own computer, install TorbScript. The command is typed into a terminal: the window where you
give a computer commands as text. On Linux and macOS it is the one below; on Windows, in PowerShell, it is
`irm https://torb.dev/install.ps1 | iex`.

```console
$ curl -fsSL https://torb.dev/install.sh | sh
```

## Run a program on your computer

Save a program in a file whose name ends in `.trb`, and run it with `torb run` and the name of the file. What the
program prints appears below the command.

```console
$ torb run hello.trb
Hello from my computer!
```

## What to read next

- **The [guide](../guide/index.md)** explains the language in more depth, for people who can program - which now
  includes you.
- **The [reference](../index.md)** has every rule of the language and every part of its standard library, for when you
  want to know exactly how something works.
- **A project of your own** teaches more than any page: a list of your books, a quiz, a small game.

## Exercise

Install TorbScript, save this program as `hello.trb`, change it to print `Hello from my computer!`, and run it with
`torb run hello.trb`. You can try the change here first.

```trb exercise
print "Hello from the browser!"
```

<details>
<summary>Hint</summary>

Only the text between the quotes changes. If `torb` is not found after installing, open a new terminal window.

</details>

<details>
<summary>Solution</summary>

```trb run
print "Hello from my computer!"
// prints Hello from my computer!
```

</details>

## Recap

`torb run file.trb` runs a program on your computer, and the guide is the next step.
