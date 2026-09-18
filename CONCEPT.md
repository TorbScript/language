# TorbScript Concept

## Key Facts

- Completely functional scripting language.
- C-style syntax with elements taken from languages like Scala, Kotlin, Rust, TypeScript, Swift
- Strongly typed with type inference
- First-class functions and higher-order functions
- Immutable by default with support for mutable state
- Pattern matching and algebraic data types
- Concurrency and asynchronous programming support
- Convention over Configuration
- Single-binary toolchain (Runtime, Package Manager, Test Framework, Linter, Formatter etc. integrated)
- Groovy-style configuration syntax
- Name undecided (TorbeScript is a working name. So are resulting things like extensions (.scr) etc.)

## Language Standard

- Defines the syntax and semantics of the language
- Specifies the core library and built-in types
- Ensures compatibility and consistency across different implementations
- Provides guidelines for language evolution and feature additions

## Runtime

- Provides the execution environment for TorbScript programs
- Manages memory and resource allocation
- Handles concurrency and asynchronous tasks
- Offers debugging and profiling tools
- Provides a standard library for common tasks
- Ensures safe execution and error handling

## Syntax Ideas/Examples

These try to convey the general style of the language that we're trying to achieve. It's not the exact syntax to be implemented,
but just a general design concept.

## Decision Log

- `const` instead of `val` as it is clearer (reading many `val` with `var` in between lets you easily miss some)

### Project Layout

```text
my-project/
├ src/
├─ main.scr      # Default entry point for execution/import
├ tests/
├ project.scr
└ README.md
```

### project.scr

The `project.scr` file contains the configuration and metadata for the TorbScript project. It typically includes information such as the project name, version, dependencies, and build settings.

Full Example of `project.scr`:
```text
name "my-project"
version "0.1.0"
authors [
  "Author Name <author@example.com>",
  "Another Author <another@example.com>",
]
dependencies {
  always "some-library:^1.2.3"
  optional "another-library:^2.3.4"
  dev "dev-library:^3.4.5"
  test "test-library:^4.5.6"
  suggest "suggested-library:^5.6.7" because "it provides additional optional features"
}
build {
  target "dev"
  input "{path}/src/main.scr"
  output "build/{target}/{name}"
}
test {
  input "{path}/tests"
  coverageThreshold 80
}

```

Similar to how Groovy works, the `project.scr` file uses a declarative syntax for defining project settings and build configurations.

```text
// "project" is the context object of `project.scr` files (similar to how "globalThis" works in JavaScript)
// It doesn't actually exist as a variable ("this" can be used to access the global context in a global scope)
project.name("my-project")
project.version("0.1.0")
project.authors([
  "Author Name <author@example.com>",
  "Another Author <another@example.com>",
])
project.dependencies((context) => {
  context.always("some-library:^1.2.3")
  context.optional("another-library:^2.3.4")
  context.dev("dev-library:^3.4.5")
  context.test("test-library:^4.5.6")
  context.suggest("suggested-library:^5.6.7", "it provides additional optional features")
})
project.build((context) => {
  context.target("dev")
  context.input("{path}/src/main.scr")
  context.output("build/{target}/{name}")
})
project.test((context) => {
  context.input("{path}/tests")
  context.coverageThreshold(80)
})
```

## Basic Syntax

### Variables

```text
var x = 20 # This is a mutable variable
const y = 30 # This is an immutable constant value
```

Types are inferred from the assigned value.

Undefined variables are not possible

```text
# Undefined variables are not possible
var z # This will result in a compile error
var z: int # This will result in a compile error, no default value is implied
```

### Data Types

```text
const someInt = 10                         # Int64
const someFloat = 3.14                     # Float64
const someChar = 'A'                       # Char
const someString = "Hello"                 # String
const someBool = true                      # Boolean
const someArray = [1, 2, 3]                # Array<Int64>
const someMap = ["a": 1, "b": 2, "c": 3]   # HashMap<String, Int64>
const someFunction = (x) { x * 2 }         # Function(Int64) => Int64 ("Function" can be omitted)
```

### Public, mutable types (Struct)

```text
// type = class, struct, enum, adt, data etc.
type Point {
  # Explicit public fields
  # `var` is implicit, but can be included for clarity
  public x: int # same as `public var x: int`
  public y: int

  # Constructors (no first "self" parameter)
  # They are just static methods, but `.new` can be omitted
  # i.e. `Point(0, 1)` and `Point.new(0, 1)` are equivalent
  new(x: int, y: int): Self {
    Self { x, y }
  }

  # Methods (first parameter is always "self")
  # They are `public` by default and have to made `private` explicitly.
  area(self): int {
    self.x * self.y
  }
}

const p = Point.new(10, 20)
p.x = 20
print "The area is {p.area()}"   # Prints "The area is 200"
```

### Immutable Classes

```text
const type Point {
  public x: int
  public y: int

  area(): int {
    x * y
  }

  # One example of handling immutable data
  with(x: int = x, y: int = y) {
    Self { x, y }
  }
}

# Here we don't use a constructor: Types without undefined private fields can be instantiated directly with their public fields
var p = Point { x: 10, y: 20 }
p.x = 20 # This will result in an error because `x` is `const`
p = p.with(y = 30)
print p.area()
```

### Traits

```text
trait Clone {
  clone(): Self
}

const type Point with Clone {
  public x: int
  public y: int

  clone(): Self {
    Self { x, y }
  }
}
```

### UFCS (Uniform Function Call Syntax)

```text
# Instead of calling a method on an object, you can call it as a free function
const p = Point.new(10, 20)
print Point.area(p)   # Equivalent to `p.area()`
```

In many situations, functions can be called without parenthesis

```text
var p = Point 10, 20
p = p with x = 30, y = y * 2
```

Chained calls without parenthesis collapse to nested calls

```text
a b c d e f

# same as

a(b(c(d(e(f)))))
```

### Blocks

Blocks contain multiple expressions/statements and return their last expression as their value.
To avoid this, return `void` or `never` depending on the situation.

```text
const aOrB = if something {
  a
} else {
  b
}

const initializedThing = {
  const nums = list [1, 2, 3]
  nums.add 4
  nums.add 5
  nums.remove 2
  nums
}
```

### Functions

Functions are _first-class citizens_ in TorbScript. They are **values** and consume the scope around them.

```text
const sum = (a: int, b: int): int => {
  a + b
}
```

### Higher-order functions

```text
const type MyNums {
  nums: list<int>

  new(nums: list<int>) {
    Self { nums }
  }

  map(self, fn: (int) => int): Self {
    const newNums = List.withCapacity self.nums.length()
    for num in self.nums {
      newNums.add fn(num)
    }
    Self { nums: newNums }
  }
}

const nums = MyNums.new list [1, 2, 3]
// Style 1: Lambda function (inline function object/closure)
const mappedNums = nums.map (num) => { num * 2 }

// Style 2: If the conditions fit, you can use the trailing closure syntax
// - Parameter has to be last argument of the call
const mappedNums2 = nums.map { _ * 2 }

// The name of the `_` parameter can be controlled by the _type of the function_
// By default they are _, _2, _3, _4, _5 etc.

[...]
map(self, fn: (value: int) => int): Self {
[...]

[...]
const mappedNums3 = nums.map { value * 2 }
```

## Builder Pattern

Callers can override `self` when calling functions by explicitly passing a different value for the `self` parameter.

As `self.` is implicit when calling methods, this allows a builder pattern similar to that of Groovy

> TODO: Research how it works in Groovy and check if our approach is valid, good and consistent.

```text
const type ConfigBuilder {
  config: map<string, string> = Map()

  databaseUrl(url: string) {
    config.set("databaseUrl", url)
  }

  username(name: string) {
    config.set("username", name)
  }

  password(pwd: string) {
    config.set("password", pwd)
  }

  build() {
    config
  }
}

const type App {
  configBuilder = ConfigBuilder()

  configure(fn: (self: ConfigBuilder) => void) {
    fn(configBuilder)
  }

  start() {
    const config = configBuilder.build()
    // ...
  }
}

const app = App()
app.configure {
  databaseUrl "postgres://localhost:5432/mydb"
  username "myuser"
  password "mypassword"
}
app.start()
```

When combined with the sandbox, this allows you to use TorbScript directly as a proper configuration format for your applications.

```text
const sandbox = Sandbox()
const executeConfig = sandbox.compile "./config.trb"
app.configure executeConfig
```

```text
// config.trb
databaseUrl "postgres://localhost:5432/mydb"
username "myuser"
password "mypassword"
```

