---
title: Why a method is a constant
summary: A method is structurally a constant of the type that holds a receiver closure, so a field and a method live in one namespace and cannot share a name, which is what lets a command call on a field write it instead of needing a second rule.
kind: explanation
status: stable
order: 150
keywords:
  - member
  - namespace
  - receiver closure
  - property command
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure
---

Java gives a type two namespaces, one for fields and one for methods, so `name` and `name()` can both exist on the
same class and mean different things. TorbScript gives a type one, and this page argues for why collapsing the two
namespaces into one is what makes the rest of the member syntax work at all.

## The decision

**A type has one namespace of members, and a member is either a field or a constant that holds a closure.** A method
is not a separate kind of thing: it is a constant of the type (declared with the `fn` sugar) whose value is a
*receiver closure* - a closure whose first parameter is the receiver, which the declaration does not have to write.

```trb check
type Point {
  x: Int
  y: Int

  fn area(): Int {
    x * y
  }
}

const p = Point x: 10, y: 20
print p.area()
print Point.area(p)
```

- `p.area()` is `Type.member(value, args)` when the member reads its receiver - not a separate call form, just what happens
  when the constant behind `area` is called with `p` as its first argument.
- Because there is one namespace, a field and a method of the same type can never share a name.
- An instance field can hold a function too (`onClick: () => Void`), and it is called exactly the same way a method
  is: `button.onClick()`. Whether a call reaches a method or a function stored in a field is not visible at the call
  site, and it does not need to be.

## Why

**Because two namespaces need a rule for what happens when they collide, and one namespace needs none.** In a
language where fields and methods live apart, nothing stops a class from declaring both `name` and `name()`, and a
reader has to know which one a bare `.name` reaches by memorizing the type instead of by a rule the language
enforces. Collapsing the two into one namespace turns "can these share a name?" from a question a reviewer answers by
convention into "no" - a compile error, checked mechanically, for every type without exception.

**Because a property command needs there to be exactly one meaning per line, and two namespaces would give it two.**
`port 8080` on a field writes it; `configure 8080` on a method calls it - see
[Property commands](../language/types/property-commands.md). With two namespaces, `port 8080` could mean "call the
method `port`" or "write the field `port`" depending on which namespace the reader has in mind, and a single type
could even declare both a field and a method under one name for the parser to choose between. With one namespace,
there is exactly one `port` to resolve, and what it is - field or method - is a fixed fact of the declaration, not a
guess made at the call site.

**Because methods costing no memory per instance falls out for free.** A method is a constant of the *type*, not a
per-instance field, so it never occupies storage in a value the way a field does. This is also what keeps a `type`
plain data: `Equals`, `Hash`, `Show` and `copy` are generated from the fields alone, because the fields are all the
storage a value has - a method cannot be swapped per instance, and there is nothing "extra" hanging off a value at
runtime that reflection would need to account for.

**Because it removes a rule Java-style separate namespaces need and this language does not have room for anyway.**
[Uniform function call syntax](why-traits-instead-of-inheritance.md#what-was-rejected) - `value.f(x)` meaning
`f(value, x)` for any free function `f` - was rejected partly because it would turn every function name into a
possible member, on top of an existing namespace for fields and methods; one namespace, populated only by what the
type itself declares or `extend`s, keeps "is `x.f` a member of `x`'s type?" answerable by reading the type once.

### What was rejected

- **Separate namespaces for fields and methods**, as in Java, C# and most class-based languages. Rejected because it
  needs its own collision rule, and because it leaves property commands with two possible readings of the same line
  instead of one.
- **A per-instance field holding a bound method**, which some prototype-based languages use to let methods be
  swapped at runtime. Rejected because it would cost every value memory for behavior that a `type`'s value semantics
  say is fixed at compile time, and because a swappable method contradicts a value being plain, structurally comparable
  data.

## Consequences

**A field and a method are specified to never collide, because the compiler is meant to check one namespace for
both.** CONCEPT.md states this as a consequence of the one-namespace rule: a `type Broken` with the field
`name: String` and the method `fn name(): String` should be rejected as an error, because `name` would be claimed
twice in the same namespace. Today's checker accepts this declaration without reporting a problem - a gap between the design and the
checker rather than a second namespace appearing through the back door.

**A property command's meaning is fixed by what the name refers to and by whether the call has parentheses.** `tls
true` writes the field, `tls(true)` calls it and is refused because a `Bool` has nothing to call; a command on a method
always calls it, with or without an argument list that reads like data.

```trb check
type Server {
  var port: Int = 8080

  fn describe(): String {
    "listening on {port}"
  }
}

var server = Server()
server.port 9090
print server.describe()
```

**A member reference works the same for a method as for a value in a field**, because both are constants of the same
namespace: `Point.area` is the receiver closure itself, unapplied, and `points.map(Point.area)` passes it to a
pipeline stage exactly as it would pass a free function.

## Related

- [Methods and `static fn`s](../language/types/methods.md) - `static` and `var`, the two words a member says, in
  full.
- [Property commands](../language/types/property-commands.md) - the one rule this namespace makes possible: a
  command on a field writes it.
- [Declaring a type](../language/types/declaring-a-type.md) - fields and methods declared together, in one body.
- [Why there are no properties](why-no-getters.md) - the other consequence of fields and methods sharing one rule for
  visibility.

