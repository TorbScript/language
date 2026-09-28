---
title: TorbScript
summary: A programming language for scripts, tools and servers. It finds mistakes before your program runs, starts at once while you write, and builds a fast program when you ship.
kind: site
status: stable
order: 10
source:
  - docs/start/index.md
  - docs/start/lists.md
  - docs/start/when-things-go-wrong.md
  - docs/tooling/torb-run.md
  - docs/tooling/torb-build.md
---

## Where do you want to start?

- [New to programming?](../start/index.md) Learn from zero in thirteen short lessons, with code you run right in the page.
- [Already code?](../guide/index.md) A quick tour for people who know another language, and what is different here.
- [Go deep](../index.md) Every rule of the language, every package of the standard library, every command of the tools.

## Change only what you mean to change

Giving a list a second name makes a copy. Changing the copy leaves the original alone, so a value never changes behind
your back - only where you change it. See [value semantics](../glossary.md#value-semantics).

```trb run
const original = [1, 2]
var copy = original
copy.append 3
print "{original} {copy}"
// prints [1, 2] [1, 2, 3]
```

## Mistakes show up before the program runs

When something can be missing or can fail, its type says so, and the [compiler](../glossary.md#compiler) makes you
handle it before the program runs. A failure is an answer like any other: there is no [null](../glossary.md#null) and
no [exception](../glossary.md#exception) to forget.

```trb run
match Int.tryFrom("forty-two") {
  Ok(number) => print number
  Fail(_) => print "That is not a number."
}
// prints That is not a number.
```

## Instant while you write, fast when you ship

`torb run` starts a program at once. `torb build` turns the same program into a program file of its own: fast, and
runnable without TorbScript. The same one tool also tests, formats and publishes. See
[the torb command](../tooling/the-torb-command.md).

```console
$ torb run hello.trb
Hello, World!
$ torb build hello.trb --output hello
wrote hello
$ ./hello
Hello, World!
```
