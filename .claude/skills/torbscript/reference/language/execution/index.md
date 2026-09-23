---
title: Execution
summary: The parts of running a program that are a rule of the language rather than an implementation detail - evaluation order, copies, tail calls, destructors, and which arm of a branch on a compile-time constant is compiled.
kind: index
status: stable
order: 110
---

The typed IR is one thing shared by the interpreter and the compiled binary; this section is what that sharing
promises a reader: the order code runs in, what a copy costs, what recursion is guaranteed, what cleanup is, and which
arm of a branch on the target is compiled at all.

## What belongs here

What does not belong here: memory as a topic in general, which stays inside `CONCEPT.md`'s own execution model
outside what a program can observe. Every page in this folder is a reference page: an example first, then the
syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Loops](loops.md)** - for walks an Iterate, while repeats while a condition holds, and loop is the endless one - with the type Never until a break gives it a Void. while true is an error, because never ending is a property of the syntax here.
- **[Evaluation order](evaluation-order.md)** - Evaluation order is source order - the receiver first, then the arguments as they are written, then the parameter defaults - so a side effect in an argument is exactly as predictable as reading the line.
- **[What a copy costs](copies.md)** - A copy always behaves the same way, but what it costs depends on the shape of the type - inline for a small fixed-size value, copy-on-write for heap-backed storage, and never for a shared type.
- **[Tail calls and stack overflow](tail-calls.md)** - Direct self-recursion in tail position is guaranteed to run without growing the stack, and every other call uses a frame of its task's stack; a recursion that runs out of stack panics with stack overflow instead of crashing.
- **[Destructors - close() runs at the last release](destructors.md)** - A shared type's close() is its destructor - it runs exactly once at the last release, user code never calls it, and a binding that holds one is released at the end of its block, the last declared first.
- **[Compile-time branches](compile-time-branches.md)** - A match, an if or an if const whose subject is a compile-time constant keeps the one arm its value selects - OperatingSystem.current is one - while every arm is still type checked on every machine and exhaustiveness is judged by the type.

<!-- torb:index:end -->

