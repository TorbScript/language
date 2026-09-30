---
title: How fast is TorbScript?
summary: Six well-known programs, each in TorbScript, C, Python and JavaScript, measured every night on the same machine - a TorbScript program built with torb build against C, and torb run against Python and Node.js.
kind: site
status: stable
order: 40
source:
  - benchmarks/game.sh
  - benchmarks/game/n-body.trb
  - .forgejo/workflows/benchmarks.yml
  - compiler/src/documentation/site-benchmarks.trb
  - docs/PERFORMANCE.md
---

TorbScript runs a program in two ways. `torb build` turns it into a program file of its own, through C and a C
compiler. `torb run` starts it at once, in the VM - an interpreter inside `torb` that needs no compiler.

This page measures both. The programs are six from the
[Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/), the collection
people use to compare languages, and each runs against the same program in C, Python and JavaScript.

<div data-benchmark="summary"></div>

In every chart, a bar is the time a program took, relative to C on the same input. **1× is as fast as C, and 2× took
twice as long.** TorbScript's binary is the red bar.

## Program by program

Each program says what it measures, and where TorbScript does work that the C does not - which is where it falls
behind. The C is the plain, single-threaded program of the Benchmarks Game, not its fastest one, and the TorbScript
follows it statement for statement. What is found here is work for the compiler: none of it is a property of the
language, and [Performance](../PERFORMANCE.md#43-the-benchmarks-game) has the details.

### `binary-trees`

Builds millions of small trees, walks them and throws them away: allocating and freeing memory. A C node is a `malloc`
and a `free`. A TorbScript node is the same block with a reference count in it, which is raised and lowered as the
tree is built and walked, and the tree is freed when nothing uses it any more.

<div data-benchmark="binary-trees"></div>

### `fannkuch-redux`

Reverses the front of short lists of numbers, again and again, for every order of the numbers: reading and writing a
list by its index. TorbScript checks every index against the length of the list, and every sum for overflow; the C
checks neither. And every `permutation[i] = value` is a call into TorbScript's runtime, which the C compiler cannot
see into, where the C stores into an array.

<div data-benchmark="fannkuch-redux"></div>

### `n-body`

Moves five planets in small steps: arithmetic with decimal numbers, on the fields of records that live in a list.
This is where TorbScript is furthest behind today. A planet is seven numbers, more than the 32 bytes a record may have
to be stored inside the list, so the list holds a pointer to it - and `bodies[i].velocityX = ...` copies the whole
planet before it changes one field of it, where the C changes the field through a pointer.

<div data-benchmark="n-body"></div>

### `spectral-norm`

Multiplies a vector by a matrix whose entries are computed on the fly: one division and a few multiplications per
step, in two loops over lists of decimal numbers. As in fannkuch-redux, every `product[i] = ...` is a call into the
runtime, and every index is checked.

<div data-benchmark="spectral-norm"></div>

### `mandelbrot`

Computes the Mandelbrot set point by point and writes it as a bitmap: arithmetic with decimal numbers in a tight loop,
and nothing else. It is the program where the two languages hand the C compiler the most similar code.

<div data-benchmark="mandelbrot"></div>

### `fasta`

Writes DNA as text, one random letter at a time, sixty to a line. TorbScript's `print` hands every line to the
operating system before it returns; the C program asks for the same with `setlinebuf`, so both make one system call
per line. In between, TorbScript collects a line's letters in a list and makes one `String` of them, where the C puts
each letter into the buffer of its output.

<div data-benchmark="fasta"></div>

## Why the VM is slower

`torb run` is for starting a program at once while you write it. Its VM is young and written in TorbScript itself,
and today it takes several times as long as Python. It is slowest where a program changes a field of a record in a
list, or an element of a list, in its innermost loop - n-body and fannkuch-redux - because it reaches such a place
step by step through a description of the path. [The design of the VM](../design/VM.md#10-open) lists what comes next.
Its time on this page includes checking and compiling the program, which the "starting up" row of the table measures
on its own.

## All the numbers

<div data-benchmark="table"></div>

<div data-benchmark="machine"></div>

## How it is measured

- **Where.** On the forge's Linux runner, after every nightly build, with the toolchain of that nightly: the same
  machine every night, and every language on it in the same run.
- **What.** TorbScript with `torb build` (its release profile, `-O2`), C with `gcc -O2` - the same compiler and the same
  optimization - and the Benchmarks Game's own Python and JavaScript programs with `python3` and `node`. Every program
  uses one thread.
- **Checked.** Every program's output is compared with the C program's, byte for byte, before anything is timed.
- **How often.** Every language runs once to warm up, then five times - the VM three times - taking turns, so that a
  busy moment of the machine falls on all of them. A chart shows the median; the table shows the fastest and the
  slowest run as well.
- **Two inputs.** The VM and Python would need many minutes for the input the binaries finish in seconds, so they run
  a smaller one, and C runs it too: every ratio compares with C on the same input. On the small input C takes only
  milliseconds, so those ratios are the rougher ones.
- **The VM** is the `torb` of the nightly as it is downloaded: a static binary built against musl, whose memory
  allocator is slower than the one of the C library the other programs use - which binary-trees, above all, feels.
- **Memory.** The peak of the memory the process holds, measured by GNU `time`. For the VM, the process is `torb`
  itself, compiler and all.

## What is not measured

These are small programs that do one thing each, not applications, and one C compiler with one set of flags. Nothing
here uses more than one core. How much work TorbScript's abstractions cost, pattern by pattern, and what is not
measured anywhere yet, is in [Performance](../PERFORMANCE.md#7-what-is-not-measured-here).

## Measure it yourself

Every program and the script that measures them are in the repository, in `benchmarks/game/`, with the licence of the
programs that come from the Benchmarks Game. From a checkout with a built `torb`:

```console
$ sh benchmarks/game.sh
$ sh benchmarks/game.sh --quick n-body
```

The first measures everything, as the forge does, and writes `benchmarks/out/game/benchmarks.json`. The second only
checks that one program builds, runs and agrees in every language. [The performance suite](../../benchmarks/README.md)
says more.
