---
title: Where are my overloads
summary: A call has exactly one signature, because that signature is what gives every argument its meaning, so overloading by parameter type and uniform function call syntax are both out.
kind: explanation
status: stable
order: 170
keywords:
  - overloading
  - UFCS
  - uniform function call syntax
  - extend
  - default parameter
source:
  - CONCEPT.md#decision-log
---

Two habits arrive together from other languages: writing the same function name twice with different parameter types, and
writing `value.f(x)` for a free function `f(value, x)`. TorbScript has neither, and the reason is the same one for both.

## The decision

**A call has one signature, and there is no uniform function call syntax.**

- No overloading by parameter type. One name in one namespace means one declaration. `fn draw(circle: Circle)` and
  `fn draw(square: Square)` in one file is a duplicate name.
- No uniform function call syntax. `value.f(x)` never means `f(value, x)`. A member is a member, and a free function is
  called by its name.
- What the language has instead: a name per **receiver** (a method, or `extend` on a type somebody else declared) and a
  trait with a **parameter** (`From<Source>`, `Multiply<Other, Output>`, or one of your own).

## Why

**Because the signature of the callee decides how its arguments are read.** This is the load-bearing property of the
language, and almost every convenience rests on it:

| What is written | What decides its meaning |
|-----------------|--------------------------|
| `.Circle` | the type the parameter is declared as |
| `None`, `Some(x)` | the parameter is an `Option` |
| `[1, 2, 3]` | a `List`, an `Array<Int, 3>`, or a `From<Iterable<Item>>` target |
| `{ _ + 1 }` | the function type of the parameter, which names its parameters |
| a `lazy` argument | the parameter's mode: it becomes a thunk |
| `assert(x > 1)` | the parameter is an `Expression<Bool>`, so the argument is quoted |
| a `var` argument | the parameter's mode: the argument is a path, not a copy |
| `configure settings` | the parameter is a receiver closure |

With two candidates, none of these can be read: the argument would have to decide which candidate is meant, and the
candidate would have to decide what the argument means. `draw([1, 2])` with a `draw(shape: Polygon)` and a
`draw(rows: Array<Int, 2>)` has no answer that is not a coin toss. One signature is what makes `Ok Some(x)`,
`print [1, 2]` and `assert(sum == 3)` all mean something definite.

**Because uniform function call syntax needs overloading to be useful.** A free function lives in a module's one
namespace, so `first` and `length` would exist once each in the whole program. UFCS without overloading buys nothing, and
UFCS with overloading buys the problem above. And `extend` already *is* "a function named after the type of its first
argument", with a visibility rule and one namespace of members - so UFCS would be a second way to write what `extend`
writes, against [why a method is a constant](why-one-member-namespace.md).

### What was rejected

- **Ad-hoc overloading by parameter type**, as in Java, C#, Swift and C++. Rejected for the reason above. It also makes
  every error message about a call a list of candidates and why each one did not fit, which is the worst diagnostic a
  language produces.
- **Uniform function call syntax**, as in D, Nim and (for extension methods) Kotlin. Rejected because it turns every
  function name in scope into a possible member and duplicates `extend`.
- **Overloading by arity only.** A default parameter covers it with one signature, and a reader sees the default.

## Consequences

Every shape people reach for overloading for has one answer here, and it is shorter than the overload set.

**A name per receiver.** Two types with the same operation is what a method is for, and `extend` adds one to a type you
did not declare.

```trb check
type Circle {
  radius: Float

  fn area(self): Float {
    radius * radius * 3.14159
  }
}

type Square {
  side: Float

  fn area(self): Float {
    side * side
  }
}

print "{Circle(1.0).area()} {Square(2.0).area()}"
```

**A trait with a parameter, where one operation really takes different second arguments.** The parameter carries what an
overload set would have carried, and a caller can add a case for a type the trait's author never saw.

```trb check
type Celsius {
  degrees: Float
}

type Fahrenheit {
  degrees: Float
}

extend Celsius with From<Fahrenheit> {
  fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

const warm = Celsius.from Fahrenheit(212.0)
print warm.degrees
```

The standard library's own conversions and operators are this shape: `From<Source>`, `TryFrom<Source, Failure>`,
`Add<Other, Output>`, `Multiply<Other, Output>`. A matrix that multiplies by a vector and by a number is two
implementations of one trait, not two functions called `multiply`.

Your own operation works the same way. `Draw<Shape>` is a capability of a renderer, and every shape it can draw is one
implementation:

```trb check
type Circle {
  radius: Float
}

type Square {
  side: Float
}

trait Draw<Shape> {
  fn draw(self, shape: Shape): String
}

type Canvas {
  scale: Float
}

extend Canvas with Draw<Circle> {
  fn draw(self, shape: Circle): String {
    "circle of {shape.radius * scale}"
  }
}

extend Canvas with Draw<Square> {
  fn draw(self, shape: Square): String {
    "square of {shape.side * scale}"
  }
}

const canvas = Canvas 2.0
print "{canvas.draw(Circle(1.0))} {canvas.draw(Square(3.0))}"
```

**A default parameter instead of a second arity.** One signature, and the default is written where a reader looks for it.

```trb check
fn greet(name: String, greeting: String = "Hello"): String {
  "{greeting} {name}"
}

print greet("Ada")
print greet("Ada", greeting: "Welcome")
```

**A named static function instead of a second constructor.** A `type` has exactly one constructor, generated from its
fields, and every other way of building one is a function with a name that says what it takes.

```trb check
type Color {
  red: Int
  green: Int
  blue: Int

  fn gray(level: Int): Color {
    Color level, level, level
  }
}

const mid = Color.gray 128
print "{mid.red} {mid.green} {mid.blue}"
```

**One implementation per argument type, in an `extend` of its own.** The two `draw` members above cannot stand in one
`type` body: a type has one namespace of members, so two members called `draw` are a duplicate name however they differ.
One `extend` per instantiation of the trait is the form, and it is what `std/core` does with `From`. Writing them in the
body says so:

```trb error
trait Draw<Shape> {
  fn draw(self, shape: Shape): String
}

type Circle {
  radius: Float
}

type Square {
  side: Float
}

type Canvas with Draw<Circle> & Draw<Square> {
  scale: Float

  fn draw(self, shape: Circle): String {
    "circle"
  }

  fn draw(self, shape: Square): String {
    "square"
  }
}
// error: `draw` is already declared in `Canvas`
```

### What decides which implementation a call means

Everything the call says: **every argument, and the type the call is expected to produce.** The trait's argument does
not have to be the first parameter, and it does not have to be a parameter at all.

```trb check
trait Store<Component> {
  fn valueOf(self): Component?
  fn attachAt(self, slot: Bool, value: Component): Int
}

type Game {
  number: Int
  text: String
}

extend Game with Store<Int> {
  fn valueOf(self): Int? {
    Some number
  }

  fn attachAt(self, slot: Bool, value: Int): Int {
    value
  }
}

extend Game with Store<String> {
  fn valueOf(self): String? {
    Some text
  }

  fn attachAt(self, slot: Bool, value: String): Int {
    value.byteLength()
  }
}

const game = Game 1, "one"
print game.attachAt(true, "eight")          // the later parameter decides
const named: String? = game.valueOf()       // the expected type decides
print named
```

Where nothing tells them apart, the checker says so instead of taking the first one: `` `valueOf` fits more than one
implementation here ``, with the list of what exists. Write the type that is expected of the call.

## Related

- [Why a method is a constant](why-one-member-namespace.md) - one namespace of members, which is what `extend` writes into.
- [extend](../language/traits/extend.md) - the form that names an operation after its receiver.
- [Conversions](../language/types/conversions.md) - `From` and `Into`, the trait-with-a-parameter shape in the standard library.
- [Default values](../language/functions/default-values.md) - the form that replaces overloading by arity.
- [Traits](../language/traits/traits.md) - declaring a trait with a parameter of your own.
