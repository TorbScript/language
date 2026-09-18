# Language Tour

Every file is a standalone script and walks through one part of the syntax: `torb run src/01-bindings-and-values.trb`

| File                          | Covers                                                                             |
|-------------------------------|------------------------------------------------------------------------------------|
| `01-bindings-and-values.trb`  | `const`/`var`, literals, strings, tuples, ranges, options                          |
| `02-functions.trb`            | `fn`, defaults, labels, variadics, lambdas, closures, command calls, generics      |
| `03-types.trb`                | `type`, `var`, `shared type`, constructors, factories, `copy`, `From`/`Into`, visibility   |
| `04-adts-and-matching.trb`    | `case`, `match`, guards, destructuring, `if const`                                 |
| `05-traits.trb`               | traits, defaults, `extend`, bounds, operators, traits as types                     |
| `06-errors.trb`               | `Option`, `Result`, `?`, `??`, `?.`, error conversion, `panic`                     |
| `07-collections.trb`          | persistent and mutable collections, iteration, queries                             |
| `08-control-flow.trb`         | `if`, `for`, `while`, `do`, custom control structures, `lazy`                      |
| `09-dsl.trb`                  | receiver closures, builders, property commands, nested receivers                   |
| `10-async.trb`                | `async`, `Task`, `spawn`, channels                                                 |
| `11-data.trb`                 | `Encode`/`Decode` instead of reflection, JSON, invariants, generic code over data |
| `12-type-system.trb`          | Literal types, const parameters and `Array`, named tuples, `Show + Encode`       |
