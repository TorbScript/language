---
title: Loops
summary: for walks an Iterable, while repeats while a condition holds, and loop is the endless one - with the type Never until a break gives it a Void. while true is an error, because never ending is a property of the syntax here.
kind: reference
status: stable
order: 5
keywords:
  - loop
  - while
  - break
  - continue
  - endless loop
source:
  - CONCEPT.md#blocks-and-control-flow
  - compiler/src/semantics/checker/statement.trb
---

There are three loops: `for` over anything `Iterable`, `while` over a condition, and `loop` for the one that does not
end by itself. `break` and `continue` work in all three, and none of them is an expression - a loop produces no value.

## Example

```trb check
var attempts = 0
loop {
  attempts = attempts + 1
  if attempts > 3 {
    break
  }
  print "attempt {attempts}"
}

for index in 0..3 {
  print index
}

var remaining = 2
while remaining > 0 {
  remaining = remaining - 1
}
print remaining
```

## Syntax

```text
for <pattern> in <iterable> { ... }        over anything `Iterable`
while <condition> { ... }                   while the condition holds
while const <pattern> = <expression> { }    while the pattern matches
loop { ... }                                until a `break`, or forever
break                                       leaves the innermost loop
continue                                    starts the next round of the innermost loop
```

## Rules

1. **`loop { ... }` is the only way to write an endless loop, and `while true` is an error.** Never ending is a
   property of the syntax here, not of a condition somebody has to recognise as a literal, which is what lets the
   checker and both back ends see it without evaluating anything.

   ```trb error
   fn forever(): Int {
     while true {
       print "on and on"
     }
   }
   // error: A loop that never ends is written `loop`
   ```

   The note says what the two types are: `` `loop { ... }` has the type `Never` without a `break` and `Void` with one ``.

   `while false` is left alone: it is not an endless loop, and a condition that is written out for a reason (a feature
   flag, a generated file) is nobody's business.

2. **A `loop` without a `break` that targets it has the type `Never`.** So nothing after it is reached, and a function
   whose whole body is one needs no other result:

   ```trb check
   fn serve(): Int {
     loop {
       print "waiting"
     }
   }
   ```

3. **A `loop` with a `break` has the type `Void`.** A `break` in a *nested* loop belongs to that one, so it does not
   make the outer `loop` end.

4. **There is no `break value`.** A loop produces nothing; a value that a loop computes is written into a `var` in
   front of it, or the loop is a pipeline instead (`first`, `find`, `fold`). It can be added later without a break.

   ```trb error
   var found = 0
   loop {
     break found
   }
   // error: Expected the end of the statement, found a name
   ```

5. **`break` and `continue` only exist inside a loop, and neither leaves a closure.** A closure is a function, not a
   block, so `items.each { break }` has no loop to leave; `do { ... }` is an ordinary function too and is no loop
   either.

   ```trb error
   fn probe() {
     break
   }
   // error: `break` is only allowed inside of a `for`, a `while` or a `loop`
   ```

6. **`continue` in a `loop` works as in a `while`:** it starts the next round without running the rest of the body.

7. **A loop is a statement, never an expression.** `const x = loop { ... }` is not a thing, the way `const x = if a { 1 }
   else { 2 }` is.

## What this is not

**`loop` is not `while true` with a shorter spelling.** `while true` is a condition, and a compiler can only tell that
such a loop diverges by evaluating it - which makes "this function needs no result" depend on constant folding. `loop`
puts the answer in the syntax: the checker reads one node and knows, and so does every back end.

**And a `loop` is not a `do`/`while`.** There is no form that tests at the end; a loop that has to run once and then
decide is a `loop` with an `if ... { break }` at the point where the decision belongs, which is also where a reader
looks for it.

```trb check
var line = "first"
loop {
  print line
  line = ""
  if line.isEmpty() {
    break
  }
}
```

## Related

- [Iterating](../collections-and-iteration/iterating.md) - what `for` pulls from, and how a type takes part.
- [Patterns in bindings and conditions](../pattern-matching/patterns-in-bindings.md) - `while const Some(x) = next()`.
- [Void and Never](../values-and-types/void-and-never.md) - the two types a loop and a `break` have.
- [Control structures](../extensibility/control-structures.md) - why everything else is a function instead.
