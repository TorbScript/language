# The Type Checker (Milestone 4)

**Status: implemented** — milestone 4 is done and this is its plan and log; the decided gaps of section 7 are still the
rules the checker follows, except where a later note marks one superseded.

One pass over the syntax trees of a workspace that decides everything the back ends must not decide: the type of
every expression, the target of every call, and every rule of the language that is not syntax. It is written in
TorbScript, in `compiler/src/semantics/checker/`.

Its input is the syntax tree ([`compiler/src/syntax/ast.trb`](../compiler/src/syntax/ast.trb)) plus what milestone 3
produced: packages, modules with ids, symbols, file scopes, exports, and a side table that resolves every name in a
_type position_. Its output is a set of side tables that milestone 5 lowers to the typed IR.

**Names in expressions are part of this pass, not of milestone 3.** What `port` means in `server { port 8080 }`
depends on the type of the parameter the closure is passed to, so resolving and checking cannot be separated
(design principle 1). Everything the checker resolves it also records.

## 0. What the pass has to guarantee

| The back ends never decide                           | Because the checker records                                              |
|------------------------------------------------------|--------------------------------------------------------------------------|
| Which `add` is meant                                 | `Resolution` per call span: symbol, generic arguments, dispatch           |
| Whether to monomorphize or pass a dictionary         | `Dispatch` and a `Witness` tree per call                                 |
| What `1` is                                          | `Adaptation.Literal` with the adapted type                               |
| What `?`, `??`, `?.`, `into()`, interpolation mean    | The resolved `From`/`OrElse`/`map`/`show` implementation                  |
| Implicit `self`, `_`, named closure parameters        | `Adaptation.ImplicitSelf`, `.ImplicitParameter`                           |
| Default arguments, argument order, spread, `lazy`     | `Adaptation.DefaultArgument`, `.Reorder`, `.Spread`, `.Lazy`              |
| `port 8080`, `database { ... }`                      | `Resolution.PropertyWrite` with the kind of write                        |
| `with Show` on a type that has no `show`             | `DerivedImplementation` with the members to generate                     |
| Which arm of a `match` can match                     | `MatchPlan`: the decision order and the reachable arms                   |
| Where a copy is a move, where a path is a reference  | `Place` per assignment and per `var` argument                            |

Constraints that shape every decision below:

- **A value has no identity.** Program-wide data lives in lists addressed by integer ids, and one `var checker: Checker`
  value is threaded through the pass, exactly as `var parser: Parser` is threaded through the grammar.
- **Near-linear.** Checking `compiler/` (~6000 lines) plus `std/` must take well under a minute on a tree walker.
  Every lookup goes through a map or an index; trait resolution memoizes; nothing is quadratic in the program size.
- **Both back ends.** Nothing may be left to the interpreter that the C back end cannot see, and the other way round.

---

## 1. Representation

### 1.1 Forms of a type

```trb
public type TypeForm {
  /** `Point`, `List<Int>`, `Option<User>`. Aliases are expanded, so `symbol` is never an Alias. */
  case Nominal(symbol: SymbolId, arguments: List<TypeId>)
  /** A value of one or more traits: `Shape`, `Show & Encode`, `Iterate<Item>`. Bounds are `Nominal` of a trait. */
  case Traits(bounds: List<TypeId>)
  /** `Item`, `Key`, `Self` in a trait, `const Size`. The parameter table says which. */
  case Parameter(parameter: ParameterId)
  /** A unification variable. Lives inside one inference context and is gone when it closes. */
  case Variable(variable: VariableId)
  /** `(Int, String)`, `(lowest: Int, highest: Int)` */
  case Tuple(fields: List<TupleField>)
  case Function(form: FunctionForm)
  /** `"online" | "offline"`, `0 | 1 | 3`. `base` is `String`, `Int64` or `Char`, values are sorted and unique. */
  case Literals(base: TypeId, values: List<LiteralValue>)
  /** A const argument: the `16` of `Array<Float, 16>`. `Matrix<Rows, Other>` holds `Parameter`s instead. */
  case Constant(value: LiteralValue, of: TypeId)
  /** The type with one value. */
  case Void
  /** `panic`, `return`, `break`, `continue`, `Process.exit`. Coerces to everything. */
  case Never
  /** Something was already reported here. Absorbs every further message. */
  case Invalid
}

public type TupleField {
  label: String?
  type: TypeId
}

public type FunctionForm {
  parameters: List<ParameterForm>
  result: TypeId
}

public type ParameterForm {
  /** The label at a call site, and the name a closure may use implicitly. `None` in a bare function type. */
  name: String?
  mode: ParameterMode
  type: TypeId
  /** A default makes the parameter optional. Only in signatures, never in a function type. */
  hasDefault: Bool = false
}

public type ParameterMode {
  case Value
  /** `var name: Counter`: the argument is a `var` path. */
  case Var
  /** `lazy Value`: the argument is an expression, evaluated at most once on first use. */
  case Lazy
  /** `...rest: Int`: the parameter is a `List<Int>`, the argument list is not. */
  case Variadic
  /** `(self: Receiver) => Value`, `(var self: Receiver) => Void`: names inside resolve against the receiver. */
  case Receiver(isVar: Bool)
}

public type LiteralValue {
  case Integer(value: Int64)
  case Text(value: String)
  case Character(value: Char)
  case Boolean(value: Bool)
}
```

Notes on the forms:

- **`Option<T>` is nominal.** `Item?` is sugar the checker expands while building a type; there is no optional form,
  no nullability, and `?.`/`??`/`?` are ordinary calls on `Option` (section 4.7).
- **Trait-typed values and intersections are one form.** `Shape` is `Traits([Shape])`, `Show & Encode` is
  `Traits([Show, Encode])`. Bounds are sorted by symbol id so that `Show & Encode` and `Encode & Show` intern to the
  same id. A `Nominal` whose symbol is a trait appears only inside `Traits` and inside `Bound.traits`.
- **`Self` is an ordinary parameter.** Inside a `trait` body, `Self` is a `Parameter` whose single bound is the trait
  itself. Inside a `type` body or an `extend`, `Self` is bound to the concrete target type, so
  `fn square(size: Int): Self` in `Point` has result `Point` without any special case.
- **Const generics are types.** A const argument is `Constant` (a literal or a named `const` that reduces to one) or a
  `Parameter` whose declaration is `isConst`. `Matrix<2, 3>` and `Matrix<2, 3>` intern to one id; there is no
  arithmetic, so comparison is equality of `LiteralValue`.
- **Literal types are not subtypes.** `"online"` has type `String` unless a literal type is expected. Two literal
  types are the same type when their value sets are equal; a subset is never assignable (section 2.5).
- **`Expression<Value>` stays nominal.** The checker recognizes it by symbol (`wellKnown.expression`) at parameter and
  binding positions and quotes the argument there (section 8, row 4.8).
- **`lazy` is a mode, not a type.** `lazy Value` is only the type of a parameter, so it never has to be interned.
- **`Invalid` is the error type.** Every operation on `Invalid` yields `Invalid` and reports nothing (section 6).

### 1.2 Interning and ids

```trb
public type TypeId { value: Int }

public type TypeTable {
  private var forms: List<TypeForm> = []
  private var identifiers: Map<String, TypeId> = [:]

  /** Interning: `describe(form)` is a full structural key, because the children are already ids. */
  var fn intern(form: TypeForm): TypeId {
    const key = describe(form)
    if const Some(existing) = identifiers.get(key) {
      return existing
    }
    const identifier = TypeId(forms.length())
    forms.add(form)
    identifiers[key] = identifier
    identifier
  }

  fn form(type: TypeId): TypeForm {
    forms[type.value]
  }
}
```

- Interning gives `==` on `TypeId` the meaning "the same type", which is what the hot paths need. `describe` is one
  level deep, so interning is O(size of the form), not O(size of the type).
- Every other table follows the same shape: `ParameterId`, `SignatureId`, `ImplementationId`, `VariableId`,
  `BindingId` are single-field types over an index into a `List` in the `Checker`. This is the only way to have
  program-wide graphs in a language whose values have no identity.
- Ids are append-only and never renumbered. That is what keeps incremental use possible (section 7.3).

### 1.3 From declarations to signatures

A signature is built on demand and cached, because building it needs the types of other declarations:

```trb
public type Signature {
  /** `Self` first (for members), then the declared generic parameters, in order. */
  generics: List<ParameterId>
  form: FunctionForm
  /** Inline bounds and `where` clauses, resolved. */
  bounds: List<Bound>
  /** The type whose member this is, `None` for a top-level function. */
  owner: TypeId?
  /** `fn area()` vs. `fn square(size: Int)`. */
  takesSelf: Bool
  isVarSelf: Bool
}

public type Bound {
  subject: TypeId
  traits: List<TypeId>
}

public type SignatureState {
  case Missing
  case Running
  case Ready(signature: SignatureId)
  /** A cycle or a broken annotation. The signature is `Invalid` everywhere. */
  case Failed
}

public fn signatureOf(var checker: Checker, symbol: SymbolId): SignatureId
```

- `signatureOf` sets `Running`, builds, sets `Ready`. Hitting `Running` is a cycle: report it once at the declaration
  and return a signature whose result is `Invalid`.
- Annotated parts never cycle: milestone 3 already resolved every name in a type position, so building the parameter
  types is a walk over `TypeReference` with a substitution of the generic parameters in scope.
- Only three things can cycle: an **inferred return type** (`fn f() { g() }`, `fn g() { f() }`), an **inferred
  constant** (`const a = B.b`, `const b = A.a`) and an **alias** (`type A = B`, `type B = A`). Aliases are expanded
  while building types, with the same state map. A `public` function and every trait method must annotate its return
  type, so the cycle is always inside one file and the message can name the fix.
- Field types are always annotated (`Field.annotation` is not optional), so a type's layout never cycles. A type that
  contains itself by value (`type Node { next: Node }`) is a different error and belongs to milestone 5; the checker
  reports it here because it is cheap: a walk over the field types with a visited set.

### 1.4 The checker value and the side tables

```trb
public type Checker {
  workspace: Workspace                                  // milestone 3: packages, modules, symbols, scopes
  var types: TypeTable
  var parameters: List<ParameterDeclaration>
  var signatures: List<Signature>
  var signatureStates: Map<SymbolId, SignatureState>
  var implementations: ImplementationTable
  var variables: List<VariableState>
  var scopes: List<Scope>
  /** The file that is being checked: file scope, visible extensions, entry file or module. */
  var file: FileContext
  var tables: Tables
  var diagnostics: List<Diagnostic>
}

/** Everything milestone 5 reads. One of these per module. */
public type Tables {
  var expressionTypes: Map<Span, TypeId> = [:]
  var patternTypes: Map<Span, TypeId> = [:]
  /** What a name, a member, a call or an operator resolved to. */
  var resolutions: Map<Span, Resolution> = [:]
  /** Everything the source does not spell out, in the order in which it applies. */
  var adaptations: Map<Span, List<Adaptation>> = [:]
  var places: Map<Span, Place> = [:]
  var matches: Map<Span, MatchPlan> = [:]
  var quotations: Map<Span, Quotation> = [:]
  /** What a collection literal became: the default collection, inline array slots, or `Target.from` (gap 51). */
  var collectionLiterals: Map<Span, CollectionLiteral> = [:]
  var derived: List<DerivedImplementation> = []
}
```

Side tables are keyed by the `Span` of the node they belong to, as milestone 3 keys its type positions. This rests on
one invariant of the parser: **no two nodes of the same kind in a file have the same span.** Parentheses are the only
place where a node could inherit another one's span, and the parser already drops the inner node and keeps only its
kind. `checkModule` asserts the invariant while it fills the tables, so a future change to the parser fails a test
instead of silently losing a type.

```trb
public type Resolution {
  case Local(binding: BindingId)
  /** A `fn`, a method, a trait method, a `static fn` of a type or trait. */
  case Callable(symbol: SymbolId, arguments: List<TypeId>, dispatch: Dispatch)
  case Field(owner: TypeId, field: SymbolId)
  case Constant(symbol: SymbolId, owner: TypeId?)
  case Construct(owner: TypeId)
  case CopyOf(owner: TypeId)
  case CaseOf(owner: TypeId, variant: SymbolId)
  /** `port 8080`, `database { ... }`, `onStart { ... }`. The kind is `.Assign`, `.AssignClosure` or `.Configure`. */
  case PropertyWrite(owner: TypeId, field: SymbolId, kind: PropertyWriteKind)
  case Namespace(module: ModuleId)
}

public type Dispatch {
  /** Not a trait member: a free function, a method of a concrete type, a field that holds a closure. */
  case Direct
  /** The implementation is known: monomorphize, or call it directly. */
  case Implementation(implementation: ImplementationId, witnesses: List<Witness>)
  /** The receiver is a trait-typed value: take the member from its witness table. */
  case Object(bound: TypeId, witnesses: List<Witness>)
  /** The receiver's type is a generic parameter: the caller passed the witness. */
  case Forwarded(parameter: ParameterId, bound: TypeId, witnesses: List<Witness>)
}

/** How one bound of one generic parameter is satisfied at a call site. */
public type Witness {
  case Implementation(implementation: ImplementationId, arguments: List<TypeId>, witnesses: List<Witness>)
  case Forwarded(parameter: ParameterId, bound: TypeId)
  case Object(type: TypeId, bound: TypeId)
  case Derived(derived: DerivedId)
}

/** Everything the source does not spell out. `Convert` covers `?` and `into()`, `Show` one interpolated part. */
public type Adaptation {
  case Literal(type: TypeId)
  case Convert(witness: Witness, method: SymbolId)
  case ToTraitValue(bounds: List<TypeId>, witnesses: List<Witness>)
  case ImplicitSelf(receiver: BindingId)
  case ImplicitParameter(index: Int)
  case DefaultArgument(parameter: Int)
  /** Written order to declaration order. */
  case Reorder(order: List<Int>)
  case Variadic(element: TypeId, from: Int)
  case Spread(witness: Witness)
  case Lazy
  case Quote(quotation: Span)
  case Show(witness: Witness)
  /** `with Add by value`: unwrap the arguments, wrap the result. */
  case Delegate(field: SymbolId)
}
```

---

## 2. Inference

Bidirectional, local, with unification variables that never leave their statement. There is **no global inference, no
Hindley-Milner generalization, no subtype inference and no return-type-driven overload resolution** (there are no
overloads: one member namespace per type, one name per scope).

```trb
public type Expectation {
  /** No expected type: infer. */
  case None
  case Type(type: TypeId)
  /** The arms of a `match` used as a statement, the body of a `for`: a value is not wanted. */
  case Statement
}

public fn checkExpression(var checker: Checker, expression: Expression, expected: Expectation): TypeId
public fn inferExpression(var checker: Checker, expression: Expression): TypeId
```

### 2.1 Where an expected type flows

| Position                          | Expected type                                                            |
|-----------------------------------|--------------------------------------------------------------------------|
| `const x: T = e`                  | `T`; without the annotation, `None`                                      |
| Argument                          | The parameter type, after substituting the generic arguments known so far |
| Last expression of a body         | The declared return type; without one, `None` (it becomes the result)     |
| `return e`                        | The declared return type; `None` in a closure (it becomes the result)     |
| Field default                     | The field type                                                           |
| Assignment `place = e`            | The type of the place                                                    |
| `if`/`match` arms                 | The expectation of the whole expression; if there is none, the type of the first arm that is not `Never` |
| Closure body                      | The result of the expected function type                                  |
| List/map literal items            | `Item` / `Key`, `Value` of the expected type                              |
| `[a, b]` without an expectation   | The type of `a`; `b` is checked against it                                |
| Interpolation `"{e}"`             | `None`, then `e` must be `Show`                                           |
| `a == b`, `a < b`                 | The left side infers, the right side is checked against it                |
| `a + b`, `-a`                     | An expected *numeric* type, into an operand that is made of numeric literals; a name keeps the type it has |
| `a ?? b`                          | `b` is checked against the `Value` the type's `OrElse<Value>` names        |
| `.Case`                           | Required. The expected type names the type the case belongs to            |
| Empty `[]`, `[:]`                 | Required                                                                  |
| Numeric literal                   | Adapts, see 2.3                                                           |

### 2.2 Unification variables

- A variable is created for each generic parameter of a call whose argument was not given explicitly, for an empty
  collection literal without an expectation (immediately an error, but the variable keeps the pass going), and for a
  closure parameter that the expected type does not pin down.
- Variables are local to an **inference context**, which is opened for one statement and closed at its end. Closing
  reports every variable that is still unsolved. There is no propagation across statements, so
  `var items = []` on one line and `items.add(1)` on the next is an error (annotate), by design.
- Solving is first-order unification over `TypeForm`, with occurs check. The only directed step is coercion
  (2.5), and it is never used to _solve_ a variable: a variable is solved by an exact type only. `[Square(2.0),
  Circle(1.0)]` therefore does not infer `List<Shape>`; with the annotation it checks.
- A variable that is only constrained by literals falls back to `Int64` / `Float64` when the context closes. This
  literal default is the only defaulting in the system (`fold(0) { total, _ => total + 1 }` gets `State = Int64`).

### 2.3 Literal adaptation

An integer literal checks against `Int8 … UInt64`, `Float32`, `Float64`, `Decimal`, a literal type that contains its
value, and a `Variable`. A decimal literal checks against `Float32`, `Float64`, `Decimal`. `String` and `Char`
literals only adapt to a literal type of the matching base. Out of range, or not a member of the literal type, is an
error that names the type. Without an adaptation the literal is `Int64` / `Float64` / `String` / `Char` / `Bool` -
fixed, and it does not follow a shadowed `Int` alias. Every literal records `Adaptation.Literal`.

The adaptation reaches through the arithmetic operators: where a numeric type is expected of `a + b` or of `-a` and the
operand is built from numeric literals and those operators alone, it is what the literals adapt to. So
`const size: Int8 = 3 + 4` is an `Int8` sum, and only a literal that does not *fit* is an error here - whether the sum
itself leaves the range is the constant evaluator's question (BACKEND gap 11). An operand that is not a literal keeps
the type it has, so `Meters + Float` stays the mistake it is.

### 2.4 Closures

A closure is read from the expected function type:

| Expected                              | `{ _ * 3 }`                    | `{ x => ... }`         | `{ x: Int => ... }` |
|---------------------------------------|--------------------------------|------------------------|---------------------|
| `(Int) => Int`                        | `_` is the first parameter     | `x` is the parameter   | annotation must match |
| `(value: Item) => Output`             | `_` is `value`; `value` also works | `x` names it       | ditto               |
| `(var self: Element) => Void`         | receiver closure, `self` implicit | `x` is the receiver | ditto               |
| `None`                                | error: "Cannot infer …"        | error                  | fine                |

- Parameter count must match, except that implicit parameters may use fewer (`{ _ * 2 }` for `(Int, Int) => Int` uses
  only `_`; unused parameters are allowed).
- Patterns work as parameters (`{ (a, b) => (b, a) }`); the pattern is checked against the parameter type.
- The result type is always inferred from the body; the expected result flows into the body.
- A name from the function type that would shadow a name visible at the closure is an error ("no silent shadowing"),
  and so is mixing `_` with named parameters in one closure.
- `return` inside a closure returns from the closure, and is checked against the closure's result.

### 2.5 Coercion, and what is not subtyping

The checker has exactly four directed conversions, applied only in a check position (never to solve a variable):

| From                              | To                          | Records                        |
|-----------------------------------|-----------------------------|--------------------------------|
| A type that implements the bounds | `Traits([...])`             | `Adaptation.ToTraitValue`      |
| `Traits(a)`                       | `Traits(b)` with `b ⊆ a` through supertraits | `ToTraitValue` |
| `Never`                           | anything                    | nothing                        |
| A literal                         | a literal type that contains it | `Adaptation.Literal`       |

Everything else is exact type equality. In particular there is **no variance**: `List<Square>` is not a `List<Shape>`,
`(Int) => Int` is not a `(Int) => Never`, and a literal type is not assignable to another literal type or to its base
(use interpolation or `into()`). Aliases are transparent, so `EntityId` and `Int64` are the same type; messages print
`EntityId (Int64)`.

### 2.6 Generic calls

1. Substitute: fresh variables for the generic parameters of the callee that were not given explicitly. Explicit
   arguments are taken from the left (`into<Set<Employee>>()`), the rest are variables.
2. Match arguments to parameters: positional first, then labels, then defaults (section 3.4). Report arity and label
   errors here and stop.
3. **Two passes over the arguments.** First every argument that is not a closure and not a command-position
   expression, in source order; then the closures. This is what makes `fold(0) { sum, number => sum + number }` and
   `collector(0, finish: { _ }) { count, _ => count + 1 }` work: `State` is solved by `0` before the closure is read.
4. Check the bounds of every generic parameter against its solution and record a `Witness` per bound (section 4.5).
   A bound that cannot be satisfied names the parameter, the bound and the solution.
5. Unsolved variable at the end: "Cannot infer `Output` of `map`. Annotate the closure or the result."
6. `Self` of a method is solved by the receiver, before the arguments.

Const arguments take part: `Array.filled(0.0)` with the expected type `Array<Int, 8>` solves `Size = 8`.
Const unification is equality of `Constant` values; a `Constant` and a `Parameter` unify only if the parameter is the
same one. `Matrix<3, 4>.multiplied(Matrix<3, 4>)` fails with "expected `Matrix<4, _>`, found `Matrix<3, 4>`".

### 2.7 Where annotations are required

| Situation                                   | Message                                                                 |
|---------------------------------------------|-------------------------------------------------------------------------|
| `const items = []`                          | Cannot infer the type of `items`. Annotate it: `const items: List<Int> = []` |
| Closure without an expected type            | The parameters of this closure need types: `{ value: Int => ... }`        |
| `public fn` without a return type           | A `public` function has to declare its return type                        |
| Trait method without a return type          | A trait method has to declare its return type                            |
| Recursive function without a return type    | The return type of `parseExpression` cannot be inferred: it calls itself  |
| `.Case` without an expectation              | `.Circle` needs a type. Write `Shape.Circle(1.0)`                        |
| A variable left over at the end of a statement | Cannot infer `Key` of `groupingBy`. Give it: `groupingBy<Employee, String> { ... }` |

---

## 3. Names in expressions

### 3.1 A bare name

In order, first hit wins:

1. **Local scope chain**, innermost first: bindings, pattern bindings, closure parameters, function parameters, local
   `fn` declarations (hoisted within their scope), generic parameters (as a namespace, section 4.3).
2. **Implicit closure parameters**: `_`, `_2`, `_3`, … and the parameter _names_ of the expected function type
   (`numbers.map { value * 2 }`). Only in a closure that declared no parameters.
3. **The innermost receiver**: members of `self` - fields, constants, methods, extension members visible in this file,
   trait members (section 4.1). A method body and a receiver closure are the same thing here.
4. **File scope**: own top-level declarations of the file, imported names, namespace aliases.
5. **Prelude**.
6. Error.

**Only one receiver is implicit.** Outer receivers and the `self` of an enclosing method are _not_ searched; to reach
one, name the parameter (`server { s => s.database { url "{s.host}/db" } }`). This is the rule the concept's own
`project.trb` example relies on, and it is what makes `@DslMarker` unnecessary. (CONCEPT.md said two things here;
gap 4 decided it.)

Shadowing: a nested scope may shadow, the same scope may not redeclare ("`x` is declared twice in this scope").
Shadowing a prelude name is allowed. An implicit parameter name from a function type that would shadow a visible name
is an error. Two visible extensions that bring the same member for the same type are an error _at the use_, with the
two modules named and `as` offered as the fix.

A name that is not found gets the best available note: a member of an outer receiver ("`count` is a field of
`Report`; only the innermost receiver is implicit"), a case written bare ("A case is written `.Circle` or
`Shape.Circle`"), an unimported extension, or the nearest name by edit distance.

### 3.2 Member paths

`a.b` is, in order: a field of the type of `a`, a constant or method of it, a visible extension member, a trait
member, `.0`/`.1`/a tuple label, `copy`. `Type.b` is a constant, static function, case or nested namespace of `Type`.
`namespace.b` (from `use * as text`) is a top-level declaration of that module. Section 4.1 has the full order.

`Point.area` without a call is the member itself, of type `(self: Point) => Int`; `p.area` without a call is the
member bound to `p`, of type `() => Int`. Both record `Resolution.Callable` with the dispatch, so the lowering knows
whether it has to build a closure.

### 3.3 Command calls and property commands

The parser already decided what is a command call (`CallStyle.Command`). The checker decides what it means:

| The callee resolves to                       | Meaning                                     | Needs               |
|----------------------------------------------|---------------------------------------------|---------------------|
| a function, method or a field holding a function, **called with `()`** | An ordinary call     | -                   |
| a function or method, command style          | An ordinary call                            | -                   |
| a field whose type is a function type        | `field = argument` (`onStart { ... }`)       | a `var` path        |
| a field, one argument                        | `field = argument` (`port 8080`)             | a `var` path        |
| a field, one trailing closure, field type is a `type` | the receiver closure is applied to the field in place (`database { ... }`) | a `var` path |
| a field, anything else                       | error: "`port` is a field. `port 8080` writes it" | -             |

`PropertyWriteKind` is `.Assign`, `.AssignClosure` or `.Configure`. A property command is only possible through the
innermost receiver or an explicit target (`build.target "dev"`); a local binding is written with `=`.
"Calling a function in a field always needs parentheses" stays: `onStart()` calls, `onStart { ... }` assigns.

### 3.4 Arguments, labels, trailing closures

- Positional arguments fill parameters from the left; labeled arguments follow and match by parameter name. A label
  that does not exist gets a "did you mean" note. A parameter that is filled twice, a missing argument for a parameter
  without a default, and a surplus argument each get their own message.
- A **variadic** parameter is last, has no default, is never labeled, and collects the rest into a `List<Item>`
  (`Adaptation.Variadic`). `...expression` spreads any `Iterate<Item>` (`Adaptation.Spread`, with the witness).
  A collection is never spread implicitly.
- A **trailing closure** fills the last parameter. Error if that parameter already has an argument, or is variadic, or
  is not of function type. Labeled arguments for earlier parameters are fine
  (`collector(0, finish: { _ }) { ... }`).
- The evaluation order the IR records is declaration order, and `Adaptation.Reorder` maps the written order to it.
  `Adaptation.DefaultArgument` records the defaults that were not written; a default expression is checked once, in
  the scope of the declaration, and never sees `self` or other parameters.
- `lazy` parameters record `Adaptation.Lazy`; the argument is checked as an ordinary value of the inner type.

### 3.5 Labels, loops and blocks

There are no loop labels in the language. `break` and `continue` are checked against the innermost enclosing `for` or
`while` **of the same function or closure**; crossing a closure boundary is an error ("`break` cannot leave a
closure", note: "a closure is a function, not a block - `do { }` is not a loop either").

---

## 4. Members and traits

### 4.1 Member lookup

For a receiver of type `T` and a name `n`:

| `T`                | Order                                                                                                  |
|--------------------|--------------------------------------------------------------------------------------------------------|
| `Nominal`          | 1. fields 2. constants and methods of the body 3. members from `extend T` in the package of `T` or in this one 4. members from `extend T` in another package that this file named (`use T.n from "..."`) 5. members of the traits `T` implements (required, then default, then supertraits, then delegated through `by`), as far as the trait is a name of this file 6. generated members (`copy`, the members of derived implementations) |
| `Parameter`        | Members of the traits in its bounds only (plus supertraits)                                             |
| `Traits`           | Members of the bounds and their supertraits, plus `extend Trait` members visible here                  |
| `Tuple`            | `.0`, `.1`, …, labels. No extensions (a tuple has no nominal head)                                      |
| `Function`         | Nothing                                                                                                |
| `Literals`         | The members of the base type, plus the generated `tryFrom`/`show`/`into`                                |
| `Void`, `Never`    | Nothing; `Never` absorbs the access without a message                                                   |

Two candidates in the same step are an ambiguity error, and the fix is to rename one with `as` where it is imported.

Steps 3, 4 and 5 are the visibility rule of an extension member: **a member is named where it is used.**

- An `extend` of a type of its own package is part of the type everywhere, and so is one this package wrote itself -
  a file sees what its own package declares.
- Everything else a **trait-less** `extend` adds is reached only where the file wrote `use String.shout from "acme/text"`
  (`scope.trb`'s `MemberImport`, keyed by owner symbol plus local name; `as` maps the local name to the declared one).
  The `from` names the package the member has to come from, and the type in front of the dot is resolved in the scope of
  the *importing* file.
- The members a **trait** puts on a type it does not own - the implementation's package is neither the type's nor this
  one - are reached only where the trait itself is a name of the file (`isNamedHere`), which a bound `Item: Slug` and a
  trait-typed receiver satisfy by themselves.

The two messages carry the rule: ``  `std/time` adds `seconds` to `Int64`, and this file does not name it `` with the note
``  Write `use Int64.seconds from "std/time"` at the top of the file ``, and ``  `slug` comes from `Slug`, which is not a
name in this file ``. An import that names a member the package does not add is reported at the `use` line itself
(``  `acme/text` adds no member `whisper` to `String` ``).

None of this touches the operators, `for`, interpolation, `?`/`??` or `into()`: those resolve through traits of the
prelude, which is a name of every file.

`isVarSelf` on the resolved member turns the receiver into a `var` access (section 5). A member that needs a `var`
and does not get one reports the participle if one exists by name ("`add` needs a `var`. Did you mean `added`?").

### 4.2 Implementations

```trb
public type Implementation {
  /** `None` for `extend X { }` without a trait. */
  trait: TypeId?
  target: TypeId
  generics: List<ParameterId>
  bounds: List<Bound>
  module: ModuleId
  package: PackageId
  members: Map<String, SymbolId>
  /** `with Add & Compare by value` */
  delegate: SymbolId?
  origin: ImplementationOrigin      // .Declared, .Derived, .Delegated, .Native
}

public type ImplementationTable {
  private var implementations: List<Implementation> = []
  /** trait symbol and the head symbol of the target ("any" for a blanket implementation) to candidates. */
  private var index: Map<(SymbolId, SymbolId?), List<ImplementationId>> = [:]
  private var answers: Map<(TypeId, TypeId), Resolved> = [:]
}

public fn resolveBound(var checker: Checker, type: TypeId, bound: TypeId): Resolved
```

- **Resolution** takes the candidates for `(trait, head of type)` plus the blanket candidates, unifies the target with
  the type (the implementation's generics as fresh variables), and then checks the implementation's bounds
  recursively. Exactly one candidate must remain. Results are memoized in `answers`, which is what keeps the whole
  pass near-linear; recursion is limited by depth (64) and the limit is an error, not a hang.
- **Coherence (orphan rule):** `extend X with Trait` requires the current package to declare `X` or `Trait`. An
  implementation whose target is a bare generic parameter (`extend<Source, Target> Source with Into<Target>`) is a
  blanket implementation and needs to own the trait.
- **Overlap:** two implementations of one trait may not overlap. Two candidates overlap if their targets unify and
  neither's bounds are provably disjoint; "provably disjoint" means the same subject is bounded by traits that no
  single type implements. Conservative on purpose: two blanket implementations of one trait always overlap. The error
  names both declarations.
- **Supertraits** are requirements, not inheritance: `type Square with Compare` must also satisfy `Equals`
  (by declaring it, by delegation or by derivation), and the error says so.
- **`with ... by field`** binds to one element of the `with` list (which may be an `&` group) and creates a
  `.Delegated` implementation for each trait it names: every _required_ member is forwarded to the field, with
  `Self` in parameters unwrapped and `Self` in the result wrapped again (only possible for a type with exactly one
  field, named by `by` itself; a trait without `Self` in parameters or result can be delegated by any type). Default
  members come from the trait as usual, so `Seconds.max` works through the delegated `compare`.
  `Adaptation.Delegate` records the field.
- **Derived implementations** are recorded as `.Derived` and generated where they are used:

| Trait                   | Derived for                                                             | Not derived for                              |
|-------------------------|-------------------------------------------------------------------------|----------------------------------------------|
| `Equals`, `Hash`        | every `type` all of whose fields implement it                            | `shared type`, a type with a function field   |
| `Show`                  | every `type` and `shared type` all of whose fields implement it           | -                                            |
| `copy`                  | every `type` (a member, not a trait)                                     | `shared type`                                |
| `Encode`                | every `type` all of whose fields are `Encode`                            | `shared type`                                |
| `Decode`                | the same, and only if the constructor is usable from outside              | a type with a private field without a default |
| `From<Field>`           | a case that wraps exactly one value of a type no other case wraps         | -                                            |
| `TryFrom<String, _>`, `Show`, `Equals`, `Hash`, `Encode`, `Decode`, `Into<base>` | a literal type       | -                                            |

A hand-written member always wins over the derived one, per member (`CaseInsensitive` writes `equals` and `hash`).
The generated `Show` format is fixed, because the differential tests of the AST compare it:
`Name(text: "a", span: Span(start: 0, end: 1))`, cases as `Circle(radius: 2.0)`, a case without fields as its name,
collections as `[a, b]` / `["k": v]`, tuples as `(1, "one")` (labels included when there are any), `String` quoted
with escapes, `Option` as `Some(x)` / `None`.

### 4.3 Bounds, `where`, and type parameters as namespaces

- `<Item: Hash & Equals>` and `where Item: Hash` are the same thing and both land in `Signature.bounds`.
- A bound's subject may be any type mentioning the generic parameters (`where Target: From<Iterate<Item>>`,
  `where Item: Compare`), which is what the prelude needs.
- A **member's** `where` clause makes that member conditionally available (`fn contains(value: Item) where Item:
  Equals`, `fn toSet() where Item: Hash`). An unsatisfied condition is a member-not-available error naming the
  unmet bound; such a member is not a requirement for implementors.
- A generic parameter is a namespace for the static members of its bounds: `Item.from(0)`, `Target.from()`,
  `Value.decode(decoder)`. Dispatch is `.Forwarded`.
- Default type arguments (`trait Add<Other = Self, Output = Self>`) are filled in when the argument list is shorter
  than the parameter list. They are defaulted, never inferred (gap 6).

### 4.4 Trait-typed values, object safety and dictionaries

This resolves the open question in CONCEPT.md ("generic methods on a trait-typed value need dictionary passing").

- A trait-typed value is a pair of a value and a **witness table** per bound. The table holds one entry per required
  member of the trait and its supertraits, plus the witnesses of the trait's own generic parameters.
- A generic call passes one witness per bound of each type argument. A witness is `.Implementation` (known -
  monomorphizable), `.Forwarded` (the caller's witness for one of its own parameters) or `.Object` (taken from a
  trait-typed value). A back end may monomorphize any call whose witness tree contains no `.Object` and no
  `.Forwarded` that reaches an `.Object`; everything else passes the table. Both back ends must support both, and the
  checker's output says which is which - the choice is never made in a back end.
- **Object safety is checked per call, not per type.** A trait may always be used as a type. What cannot be called on
  a trait-typed value is a member that mentions `Self` in a parameter or in the result, and a static member (no
  `self`): the concrete type is not known, so `equals`, `compare`, `added` and `Type.from` have no meaning there. The
  message names the member and the value's type. This keeps `List<Show & Hash>` and `fn audit(entry: Show & Encode)`
  legal - those only hash and render the individual values - while rejecting `a == b` on two of them.
  `var fn field<Value: Decode>(name: String)` on a `RecordDecoder` is fine: a generic method is object-safe
  because its witnesses are passed.
- A `shared type` may only implement a `shared trait`, so a trait-typed value of a non-shared trait is always a value
  and may cross a task boundary. The checker enforces both directions.

### 4.5 The traits the language itself uses

| Syntax                     | Trait and member                             | Notes                                                |
|----------------------------|----------------------------------------------|------------------------------------------------------|
| `a + b`, `-`, `*`, `/`, `%`| `Add.add`, … on the left operand              | `Other` defaults to `Self`, so `Meters + Float` fails |
| `-a`                       | `Negate.negate`                               |                                                      |
| `a == b`, `a != b`         | `Equals.equals`                               | Both sides the same type                             |
| `a < b`, `<=`, `>`, `>=`   | `Compare.compare`                             | Non-associative, no chaining (the parser says so)     |
| `!a`, `a && b`, `a \|\| b`  | built in on `Bool`                            | Not traits: they short-circuit (gap 21)              |
| `a[k]`                     | `Indexed.at`, `MutableIndexed.set` for a write | `a[k]` as a `var` path needs `MutableIndexed`         |
| `a[from..to]`              | `Slice.slice`, `MutableSlice.replace`          | As a `var` path it is a window                        |
| `"{e}"`                    | `Show.show` per part                           | Records `Adaptation.Show`                             |
| `for x in xs`              | `Iterate.iterate`                            | The subject is evaluated once into a temporary        |
| `...e`                     | `Iterate`                                     |                                                      |
| `e?`                       | `Result`/`Option`, plus `From` for the error   | Section 4.6                                           |
| `a ?? b`                   | `OrElse.orElse`, `b` is lazy                   | A trait, because it is a method call                  |
| `a?.m`                     | `Option.map`, or `flatMap` if `m` returns an `Option` | Never a nested `Option` (gap 12)               |
| `e.into()`, `e.to<T>()`    | `Into`/`From`                                  | The target comes from the expectation                 |
| `using r { }`, `Close`     | Ordinary functions and traits                  | No special rule                                       |

### 4.6 `?` and conversions

`e?` requires the enclosing function's return type:

| `e`                    | Return type            | Result   | Conversion                                      |
|------------------------|------------------------|----------|-------------------------------------------------|
| `Result<V, E>`         | `Result<_, E>`         | `V`      | none                                            |
| `Result<V, E>`         | `Result<_, F>`         | `V`      | `F: From<E>`, recorded as `Adaptation.Convert`   |
| `Option<V>`            | `Option<_>`            | `V`      | none                                            |
| `Option<V>`            | `Result<_, _>`         | error    | "`?` on an `Option` in a function that returns a `Result`" |
| anything               | entry file, top level  | `V`      | the program ends with the error                  |

`From` for the error is resolved through the ordinary implementation lookup, which is what makes the generated
`From<ConfigError>` of a wrapper case work without anybody writing it.

---

## 5. Flow-sensitive rules

### 5.1 Places

```trb
public type Place {
  case Local(binding: BindingId)
  /** `self` in a method or receiver closure. */
  case Receiver(binding: BindingId)
  case Field(base: Place, owner: TypeId, field: SymbolId)
  /** `a[key]`: `MutableIndexed` for a write. */
  case Index(base: Place, key: Span)
  /** `a[from..to]`: `MutableSlice` for a write. */
  case Range(base: Place, range: Span)
  case Tuple(base: Place, index: Int)
  /** Everything else: a call result, a literal, a field of a temporary. */
  case Temporary
}

public fn placeOf(var checker: Checker, expression: Expression): Place
public fn isMutable(var checker: Checker, place: Place): Bool
```

`isMutable` walks from the root: the root must be a `var` binding, a `var` parameter or a `var fn` receiver; every `Field`
step must be a `var` field _and_ writable from here (`private(var)` outside the declaring type yields a const path,
and const is deep: `config.routes.add(...)` is an error while `config.routes` reads and iterates); `Index` needs
`MutableIndexed`, `Range` needs `MutableSlice`. `Temporary` is not mutable.

What needs a mutable place: assignment, a `var fn` method, a property command, a `var` argument, and `if var` /
`while var` (gap 3). A `const` root gets "`q` is a `const`. Only a `var` binding can be changed"; a non-`var` field
gets "`id` never changes after construction"; a `private(var)` field gets "`balance` can only be written by
`Account`".

**Temporaries.** A `var` _path_ through a temporary is an error, because the change is lost:
`iterate().next()`, `f().field = 1`, `samples.toList().sort { _ }`. A temporary as the _argument_ of a `var`
parameter is allowed: the callee is its only owner, "copy in, copy out" is exact, and this is what
`using File.open(path)? { ... }` needs (gap 2).

### 5.2 Exclusivity

A stack of active accesses in the `Checker`. Opening a `var` access to place `P` pushes `P`; checking any place `Q`
against the stack fails when `Q` and some `P` are equal, one is a prefix of the other, or both pass through an
`Index`/`Range` of the same base (two indices cannot be compared statically).

**When the access begins is the model of Swift** (decided in 4.10, replacing the stricter rule of 4.6): the `var` access
of a call starts once **every argument has been evaluated**. So everything the arguments read has ended before it, and
`items.removeAt(items.length() - 1)` and `f(checker, checker.count)` are ordinary code. Three conflicts are left:

1. two `var` accesses of the **same** call (`swap(a, a)`, `move(list[0], list)`, `a.merge(a)` with two `var`s) - they
   really do run at the same time,
2. a **closure argument** of the call reaching the place that call is changing: the closure runs *inside* the access,
   which is exactly what makes the DSL rule work,
3. a write from inside such a closure, for the same reason.

An `Access` therefore carries a per-call serial and the closure depth it was opened at, and the two questions are two
functions: `checkNewAccess` (a new `var` access) and `checkAccess` (a read or a write). The DSL rule:

```trb
html { root =>
  root.div {
    text "inside the div"
    // root.attribute "lang", "en"        // `root` is being changed by `div` right now
  }
}
```

`swap(a, a)` and `swap(items[i], items[j])` are errors (the second with the note "use `items.swapAt(i, j)`").
Different fields are fine (`project.build { output "{project.name}" }`), because `Field` steps with different symbols
never conflict. A closure that captures a `var` binding counts as an access to that binding for every call that may
run it. A quotation is not a closure here: it is never run at the call site. The check is conservative and static;
correct programs it rejects go into the Open Questions of CONCEPT.md.

The subject of a `for` loop is evaluated once into a temporary, so `for x in xs { xs.add(y) }` is allowed - the loop
iterates over a copy, as the concept promises.

### 5.3 Dead changes

Two rules, both errors, both with a fix in the message:

1. **A change that is never read.** For every local `var` binding the checker keeps the source-ordered positions of
   its reads and changes plus the loop depth of each. A change is dead when there is no read after it in source order
   _and_ no read inside any loop that encloses it. Bindings that are captured by a closure, are `var` parameters or
   are `self` are exempt. The message points at the change and names the path that was probably meant:
   "This change has no effect: `first` is never read again. Did you mean `counters[0].increment()`?"
2. **A discarded value.** An expression statement must have type `Void` or `Never`, unless the call it consists of has
   a `var` receiver or a `var` argument - then the mutation is the effect and the result may be dropped
   (`parser.bump()`, `cursor.next()`, `list.removeAt(0)`). So `list.added(4)` as a statement is an error
   ("The result of `added` is not used. Did you mean `add`?") and so is `Email.tryFrom(text)`. `const _ = ...` discards
   on purpose. A `match` or `if` used as a statement checks each arm as a statement.

### 5.4 Definite return, `Never`, and the value of a block

`checkBlock` returns `(type: TypeId, diverges: Bool)`. A block's type is the type of its last statement when that is
an expression statement, `Void` otherwise. It diverges when its last statement diverges, or when any statement does
(`return`, `panic`, `break`, `continue`, an `if` whose branches all diverge, a `match` whose arms all diverge). A
function whose return type is not `Void` and whose body neither produces that type nor diverges gets "This function
has to return a `Float`", pointing at the closing brace of the body. `while` and `for` are statements and have no
value; `if` and `match` are expressions, and an `if` without an `else` has type `Void`. Where the result is inferred, a
`return` **without** a value contributes `Void` to it (gap 44), so a body that never ends but leaves through a bare
`return` produces nothing instead of `Never`.

### 5.5 Patterns, exhaustiveness and redundancy

Patterns are checked against a type and bind names (a name that starts with a lowercase letter binds, never
matches a constant; an uppercase one - `A` to `Z`, and a name is ASCII - is resolved through the scope and is a case or
a type, never a binding). Then
exhaustiveness and reachability are decided with Maranget's usefulness algorithm over a pattern matrix, which gives
both answers and a witness for free:

- `useful(matrix, row)` with the standard specialization `S(constructor, matrix)` and default matrix `D(matrix)`.
- **Constructor sets** per type: the cases of a `type` (finite); one constructor of arity _n_ for a tuple or a type
  with fields; `true`/`false` for `Bool`; the members of a literal type (finite); for `Int`, `Char` and `String`
  literals and ranges the intervals are split into a disjoint cover plus a `Missing` constructor that stands for
  "everything not written" (so `0`, `1 | 2 | 3`, `4..=9`, `_` behave correctly and `4..=9` versus `5..=7` is
  detected); for lists, one constructor per fixed length plus one "at least _n_" constructor when a rest pattern is
  present; for `Option`/`Result` the ordinary cases.
- A type parameter, a trait-typed value, a function type and `String` without a literal type have no constructors:
  only `_` and a binding are exhaustive.
- An arm with a guard binds and is reachable, but contributes nothing to exhaustiveness.
- Not exhaustive: "`match` does not handle `.Rectangle(_, _)`" (up to three witnesses, then "and 4 more").
- An arm that is not useful: "This arm is never reached", with the arm that covers it as a note. An error, like every
  other dead code in the language (gap 25).
- `MatchPlan` records the constructor test order and the bindings per arm, so the lowering does not repeat the work.
- **A binding of a refutable pattern that nobody reads is an error**: "`limit` is never read: write `_`, or `_limit` to
  keep the name". The refutable positions are an arm of a `match`, an `if const`/`if var` and a `while const` - the ones
  where a name is a choice instead of the only way to name a part. A lowercase name always binds, so `limit =>` matches
  every value and shadows the constant instead of comparing with it, and the read is the only thing that tells that
  mistake from a binding somebody meant. "Read" is a name in an expression, which is one place in the checker
  (`localTarget` in `name.trb`), so an assignment to the binding counts too. `Checker.bindingsHere()` gives the caller
  the bindings the pattern just declared - for alternatives the one set the body sees - and `requireReadBindings` runs
  after the guard and the body. A name that starts with `_` is exempt. The irrefutable positions (`const`,
  destructuring, `for`, closure and function parameters) are `torb lint`'s, because nothing there can be mistaken for a
  comparison.

`if const Some(user) = e` and `while const Some(x) = e` check the pattern against the type of `e`, must be
refutable-but-possible (an irrefutable pattern gets "This pattern always matches. Use `const`"), and bind for the
body only. `if var` binds `var` paths _into_ the subject place, so `if var Some(inner) = current { inner.next() }`
advances `current` (gap 3); the subject must then be a mutable place and the exclusivity rule covers the body.
`const (a, b) = pair` and `const Point(x, y) = p` must be irrefutable.

### 5.6 Top level, modules and constants

- Top-level code is allowed in an entry file (`src/main.trb`), in a script, in a receiver script and in a
  `tests/*.test.trb` file (which is one long call of `group` and `test`). In every other module only declarations are
  allowed: "Top-level code is only allowed in entry files. `./syntax/lexer.trb` is imported."
- A top-level `const` of a module must be **compile-time evaluable**: a literal, a tuple/list/map literal of such, a
  constructor or case constructor call whose arguments are such, a reference to another compile-time constant, string
  interpolation of such, a unary minus on a literal, and the arithmetic, comparison and logical operators of the
  built-in number types and of `Bool` (`const frameTime = 1.0 / 60.0`). No function calls and no `native` calls -
  `+` on `String` is a call, interpolation is not. That is what removes module initialization order and makes cyclic
  imports harmless. In entry files and scripts a top-level `const` is ordinary code.
- `public const` is allowed at top level, `public var` is not: a module exports constants, it has no mutable state.
- A **field default** is evaluated at every construction, in a scope without `self` and without the other fields, so
  field order is not observable.
- A **const generic argument** must be a literal, a const parameter, or a path to a `const` whose initializer reduces
  to a literal. `Array<Item, Size + 1>` is "there is no arithmetic in types".
- Top-level `?` and `await()` are allowed in entry files and scripts. `await()` elsewhere requires a return type of
  `Task<...>` or a closure passed to `spawn` (milestone 7 activates the rule; the checker has the hook).
- `private(var)` is only valid on a field. `native` is only valid in a `std` package. `shared` on a trait means only
  `shared type`s may implement it and a value of it counts as shared.
- **How a name is spelled is a rule of the language** (`spelling.trb`), reported once per declaration at the name's
  span. `A` to `Z` is uppercase and `_`/`a` to `z` is lowercase; a name is ASCII, so there is no third answer. Uppercase:
  `type`, `shared type`, `trait`, `case`, a type parameter (a size parameter too), a type alias, and an `as` alias of an
  imported type, trait or case. Lowercase: `fn`, fields, parameters, tuple labels (in a type and in a literal),
  `const`/`var`, module constants (**no MACRO_CASE**: "A constant starts with a lowercase letter: TorbScript has no
  `MAX_SIZE` spelling, write `maxSize`"), closure parameters, the name of a `...rest` pattern, a module alias, and an
  `as` alias of a function or a constant. It is **one syntactic walk** over the module's file, asks nothing of the types,
  and therefore never sees a name the compiler generated itself. Two things follow from the parser and not from this
  pass: a pattern binding cannot be uppercase at all (an uppercase name there is a case), and `const Limit = 10` parses
  `Limit` as a case pattern - which used to declare nothing and report nothing, so a pattern that is a bare uppercase
  name is read here as the constant it was meant to be.

---

## 6. Diagnostics

Principles:

- **One root cause, one message.** Every failed operation yields `Invalid`, and `Invalid` never produces a message
  again. A call with a broken argument is still resolved if it can be; a call that cannot be resolved returns
  `Invalid` for its result so the surrounding statement stays quiet.
- **One message per span** (as in the parser: `Parser.error` drops a second message at the same position).
- **Say what to write instead.** Every message that has a canonical fix carries it, in the tone of the parser's
  messages: full sentences, no codes, backticks around code, the fix as code.
- Notes carry the second location (the other declaration, the arm that covers this one, the line of the shadowed
  name). Rendering stays in `cli/render.trb`.
- Order: diagnostics are sorted by span per file, as `parse` already does.

The catalogue (the ~40 that matter):

| Situation | Message (notes in italics) |
|---|---|
| Unknown name | ``Cannot find `count` here`` - _`count` is a field of `Report`. Only the innermost receiver is implicit: name the parameter (`report { r => r.count }`)_ |
| Bare case | ``` `Circle` alone is a type, a function or a variable. A case is written `.Circle` or `Shape.Circle` ``` |
| `.Case` without an expectation | ``` `.Circle` needs a type. Nothing here says which one: write `Shape.Circle(1.0)` ``` |
| `.Case` before a `?` | ``` `.Missing` needs its type here: nothing expects one before the `?` ``` - _Write it out: `ConfigError.Missing(...)`. The `?` only decides the failure type once the argument has been read_ |
| An expression the checker could not type | ``` The checker did not work out the type of this expression ``` - _This is a bug of the compiler, not of this file. Please report it with the line it points at_ |
| Unknown member | ``` `Point` has no member `aera`. Did you mean `area`? ``` |
| Ambiguous extension | ``` `shout` comes from `acme/one` and from `acme/two` ``` - _A member is renamed where it is imported: `use ... as another from "acme/two"`_ |
| Extension not named | ``` `std/time` adds `seconds` to `Int64`, and this file does not name it ``` - _Write `use Int64.seconds from "std/time"` at the top of the file_ |
| Trait not named | ``` `slug` comes from `Slug`, which is not a name in this file ``` - _Write `use Slug from "acme/slug"`: the members a trait puts on a foreign type need the trait_ |
| Member import that is not there | ``` `./text` adds no member `whisper` to `String` ``` |
| `use` without names | ``` A `use` names what it imports ``` |
| `while true` | ``` A loop that never ends is written `loop` ``` |
| `??` without the trait | ``` `Int64` does not implement `OrElse`, so `a ?? b` has no meaning for it ``` |
| Type mismatch | ``Expected `Int64`, found `String` `` |
| Numeric mismatch | ``Expected `Int64`, found `Float64`. There are no implicit conversions: `Int.tryFrom(value)?` `` |
| Literal type | ``` `2` is not one of `0 \| 1 \| 3` ``` |
| Literal subset | ``` `Status` and `"online" \| "offline"` are different types: a subset is not assignable ``` |
| `const` written | ``` `q` is a `const`. Only a `var` binding can be changed ``` |
| Verb on a const path | ``` `add` needs a `var`. Did you mean `added`? ``` |
| Non-`var` field / `private(var)` / `private` | ``` `id` never changes after construction ``` / ``` `balance` can only be written by `Account` ``` / ``` `history` is private to `Account` ``` |
| `var` path through a temporary | ``` `iterate()` is a temporary, and `next` changes its receiver: bind it first (`var cursor = iterate()`) ``` |
| Exclusivity | ``` `root` is being changed by `div` right now ``` / ``` `items[i]` and `items[j]` cannot be told apart. Use `items.swapAt(i, j)` ``` - _While a `var` access runs, the same path cannot be reached a second time_ |
| Dead change | ``` This change has no effect: `first` is never read again. Did you mean `counters[0].increment()`? ``` |
| Discarded value | ``` The result of `added` is not used. Did you mean `add`? Discard it with `const _ = ...` ``` |
| Not exhaustive | ``` `match` does not handle `.Rectangle(_, _)` ``` |
| Unreachable arm | `This arm is never reached` - _`_` above already matches everything_ |
| Missing return | ``This function has to return a `Float64` `` |
| `while` as a value | ``` `while` is a statement and has no value. `do { }` evaluates a block ``` |
| Cannot infer a binding | ``` Cannot infer the type of `items`. Annotate it: `const items: List<Int> = []` ``` |
| Cannot infer a parameter | ``` Cannot infer `Output` of `map`. Annotate the closure parameter or the result ``` |
| Closure without an expectation | ``` The parameters of this closure need types: `{ value: Int => ... }` ``` |
| Implicit parameter shadows | ``` The implicit parameter `value` would shadow `value` from line 12. Name the parameter: `{ item => ... }` ``` |
| Recursive return type | ``` The return type of `parseExpression` cannot be inferred: it calls itself. Annotate it ``` |
| Alias cycle | ``` `type Handler = Handler` is a cycle ``` |
| Unmet bound | ``` `Square` does not implement `Hash` ``` / ``` `Item` is not `Hash`. Add the bound: `where Item: Hash` ``` |
| Missing trait member | ``` `Square` implements `Shape` but has no `name`. `Shape` requires `fn name(): String` ``` |
| Supertrait | ``` `Compare` requires `Equals`. `Square` has neither an `equals` nor a derived one ``` |
| Orphan | ``` `extend String with Show`: neither `String` nor `Show` belongs to this package ``` |
| Overlap | ``` `String` already implements `Show` (in `std/prelude/src/convert`) ``` |
| Not delegated | ``` `Meters` has no `multiply`: `Multiply` was not forwarded by `with Add & Subtract & Compare by value` ``` |
| Not object-safe here | ``` `equals` cannot be called on a `Show & Hash` value: it needs two values of the same type ``` |
| List `+` | ``` `List<Encode>` has no `+`: lists have no `Add`. Use `addedAll` ``` |
| `?` conversion | ``` `IoError` does not convert into `AppError`. Add a case that wraps it, or `extend AppError with From<IoError>` ``` |
| `?` in the wrong function | ``` `?` needs a function that returns an `Option` or a `Result` ``` |
| Arguments | ``` `connect` takes 2 arguments, 3 were given ``` / ``` `connect` has no parameter `timout`. Did you mean `timeout`? ``` / ``` `host` already has an argument ``` |
| Trailing closure | ``` The trailing closure fills `transform`, which was already given by name ``` |
| Property command | ``` `port` is a field: `port 8080` writes it. Only a function can be called ``` / ``` `onStart` holds a function: `onStart { ... }` assigns it, `onStart()` calls it ``` |
| Top-level code | ``` Top-level code is only allowed in entry files. `./syntax/lexer.trb` is imported ``` |
| Constant not evaluable | ``` The initializer of a top-level `const` has to be known at compile time ``` |
| Const arithmetic | ``` There is no arithmetic in types. `Array<Float, Size + 1>` needs its own const parameter ``` |
| Redeclared / `break` | ``` `x` is already declared in this scope ``` / ``` `break` cannot leave a closure ``` |
| Quotation | ``` A `const` cannot appear in a quotation: `Expression<Bool>` holds a single expression ``` / ``` `transform` is captured here and is not `Encode`. `assert` shows the values of what a condition captures ``` |

---

## 7. Module layout

Milestone 3 owns the files directly under `compiler/src/semantics/`. The checker lives one directory below, so the two
milestones cannot collide:

```text
compiler/src/semantics/checker/
├ type.trb            TypeForm, TypeId, TypeTable, formatting a type for a message
├ wellknown.trb       The prelude symbols the checker knows by name (Int64, Option, Iterate, Show, From, ...)
├ lowering.trb        A `TypeReference` to a `TypeId`: names, tuples, functions, literals, const arguments
├ signature.trb       Declarations to signatures, lazily, with cycle detection
├ context.trb         Checker, Tables, Resolution, Adaptation, scopes, diagnostics, inference contexts
├ unify.trb           Unification, coercion, literal adaptation, substitution
├ implementation.trb  The implementation index, bound resolution, witnesses, coherence, overlap, delegation
├ derive.trb          Derived implementations: Equals, Hash, Show, copy, Encode, Decode, From, literal types
├ member.trb          Member lookup on every form of type
├ name.trb            Names in expressions: the lookup order, receivers, the messages about a name
├ command.trb         Property commands: `port 8080`, `onStart { ... }`, `database { ... }`
├ receiver.trb        Receiver scripts: `project.trb` and what `Sandbox.load` names
├ call.trb            Arguments, labels, defaults, variadics, spread, trailing closures, lazy
├ expression.trb      check/infer for every ExpressionKind, operators, interpolation, `?`, `??`, `?.`
├ closure.trb         Closures from an expected function type, implicit parameters, receiver closures
├ pattern.trb         Patterns and the names they bind, and that a refutable binding has to be read
├ usefulness.trb      The pattern matrix: usefulness, specialization by constructor, the default matrix, witnesses
├ exhaustive.trb      Patterns to the matrix and back: refutability, the messages, the MatchPlan
├ place.trb           Places, mutability, exclusivity, `shared` paths
├ mutation.trb        Dead changes, and whether a closure may outlive its call
├ statement.trb       Statements, blocks, definite return, loops, assignment
├ declaration.trb     Types, traits, extends, functions: requirements, visibility, top-level rules
├ spelling.trb        How every name of a file is spelled: uppercase for a type, lowercase for a value
├ quote.trb           Expression<Value>: what is quotable, the tree, the captures
└ check.trb           The entry point
```

The public surface:

```trb
public fn newChecker(workspace: Workspace): Checker                                              // check.trb
public fn checkWorkspace(workspace: Workspace): CheckedProgram
public fn checkModule(var checker: Checker, module: ModuleId): Tables

public fn intern(var types: TypeTable, form: TypeForm): TypeId                                   // type.trb
public fn describeType(checker: Checker, type: TypeId): String
public fn typeFromReference(var checker: Checker, reference: TypeReference, scope: TypeScope): TypeId

public fn signatureOf(var checker: Checker, symbol: SymbolId): SignatureId                       // signature.trb
public fn instantiate(var checker: Checker, signature: SignatureId, arguments: List<TypeId>): FunctionForm

public fn checkExpression(var checker: Checker, expression: Expression, expected: Expectation): TypeId
public fn inferExpression(var checker: Checker, expression: Expression): TypeId                  // expression.trb

public fn checkBlock(var checker: Checker, block: Block, expected: Expectation): BlockResult     // statement.trb
public fn checkStatement(var checker: Checker, statement: Statement)

public fn resolveName(var checker: Checker, name: String, at: Span, expected: Expectation): Resolution
public fn resolveCommand(var checker: Checker, call: Expression, expected: Expectation): TypeId  // name.trb
public fn lookupMember(var checker: Checker, receiver: TypeId, name: String, at: Span): List<Candidate>

public fn resolveBound(var checker: Checker, type: TypeId, bound: TypeId): Resolved              // implementation.trb
public fn implementsTrait(var checker: Checker, type: TypeId, trait: TypeId): Bool
public fn checkCoherence(var checker: Checker)

public fn placeOf(var checker: Checker, expression: Expression): Place                           // place.trb
public fn requireMutable(var checker: Checker, place: Place, at: Span, what: String)
public fn openAccess(var checker: Checker, place: Place, at: Span)
public fn closeAccess(var checker: Checker)

public fn checkExhaustive(var checker: Checker, subject: TypeId, arms: List<MatchArm>,      // exhaustive.trb
                          plans: List<MatchArmPlan>, at: Span): MatchPlan
public fn armPlanOf(var checker: Checker, arm: MatchArm, subject: TypeId): MatchArmPlan
public fn checkConditionPattern(var checker: Checker, pattern: Pattern, subject: TypeId, isLoop: Bool)
public fn requireIrrefutable(var checker: Checker, pattern: Pattern, subject: TypeId, what: String, kind: String)
public fn requireReadBindings(var checker: Checker, bound: List<BindingId>)                       // pattern.trb

public fn checkSpelling(var checker: Checker, module: ModuleId)                                   // spelling.trb
```

### 7.1 What the IR lowering reads

`CheckedProgram` is `Tables` per module plus the global tables (`TypeTable`, signatures, implementations, parameters,
derived implementations). For every expression span the lowering finds its type, its resolution, its adaptations and,
where it is assigned to or passed as a `var` argument, its place. That is enough to emit the IR without looking at a
name again: no scope, no expectation, no trait resolution in milestone 5.

### 7.2 Order of work in `checkWorkspace`

1. Packages and modules in dependency order (milestone 3 provides the order; cycles between packages are an error).
2. Collect implementations of all modules, then `checkCoherence` (orphan and overlap). This is the only step that
   needs the whole program, and it reads declarations only.
3. Per module, per file: check declarations (requirements, visibility, top-level rules), then bodies.
4. Signatures are built on demand throughout, and cached.

### 7.3 Staying incremental

The language server is milestone 8, but nothing here may make it impossible:

- **Signatures do not depend on bodies.** Step 3 can be redone for one file without touching any other.
- **Checking a body is a pure function** of the signature environment, the file's scope, the visible extensions and
  the implementation index. Caching per body is therefore sound, and one edit invalidates one body.
- **Tables are per module** and keyed by span; a re-check replaces one module's tables.
- **Ids are append-only.** A re-check may add types and signatures, never renumber them.
- **No global inference**, so no dependency between two bodies exists that the signature does not already carry.
- Diagnostics carry their module, so partial results are renderable.

---

## 8. Sub-milestones

Each one is one agent session: roughly 600-1500 lines of TorbScript plus tests in `compiler/tests/*.test.trb` with
in-memory programs (`checkSource(text)` returns the diagnostics, as `messages(text)` does for the parser), plus real
files that have to check cleanly afterwards.

| # | Scope | Files | Depends on |
|---|---|---|---|
| **4.1** | **Done.** Type representation and signatures. `TypeForm`, interning, `TypeReference` to `TypeId`, alias expansion, `Self`, generic parameters, const arguments, literal types, tuples, function types, intersections. Signatures on demand with cycle detection. `describeType` for messages. No expression checking. | `type.trb`, `wellknown.trb`, `signature.trb`, `context.trb`, `unify.trb` (equality and substitution only), `check.trb` (skeleton) | M3 |
| **4.2** | **Done.** Statements, blocks and monomorphic expressions. Literals with adaptation, locals, bindings with irrefutable patterns, assignment, `if`/`match` types without exhaustiveness, calls of top-level functions and of methods on concrete types, arguments/labels/defaults/variadics, field and static member access, definite return, `Never`. Everything else becomes `TypeForm.Deferred` and is counted (`torb check --statistics`). | `expression.trb`, `statement.trb`, `call.trb`, `member.trb`, `name.trb`, `pattern.trb` | 4.1 |
| **4.3** | **Done.** Traits and implementations. The index, bound resolution with memoization, coherence, overlap, supertraits, member lookup through traits and extensions (the `extend` visibility rule), delegation `by`, derived implementations, operators, interpolation through `Show`, `Iterate` in `for`, `?`/`??`/`?.`, `into()`. | `implementation.trb`, `derive.trb`, `member.trb`, `expression.trb` | 4.2 |
| **4.4** | **Done.** Generics and inference. Unification variables, inference contexts, two-pass argument checking, closures from expected function types, implicit `_`/named parameters, `.Case`, empty literals, bounds at call sites, witnesses, trait-typed values and per-call object safety. **Gate: `torb check compiler/src/syntax` is clean** - and so is all of `std/` and `compiler/`, at 100% of their expressions. | `unify.trb`, `closure.trb`, `call.trb`, `implementation.trb` | 4.3 |
| **4.5** | **Done.** Exhaustiveness and redundancy. The pattern matrix over ADTs, literals, ranges, tuples, lists with rest, literal unions, `Option`/`Result`; witnesses; `MatchPlan`; `if const`/`if var`/`while const`, and every pattern that has to match. | `exhaustive.trb`, `usefulness.trb`, `pattern.trb` | 4.4 |
| **4.6** | **Done.** Places and the mutation rules. `const`/`var`, valid paths, `var` parameters and temporaries, `private(var)`, exclusivity, dead changes, `break`/`continue`. | `place.trb`, `mutation.trb`, `statement.trb` | 4.4 |
| **4.7** | **Done.** Receivers and the DSL. Receiver closures, the innermost-receiver rule, command calls, property commands (`.Assign`, `.AssignClosure`, `.Configure`), receiver scripts (`project.trb` against the `Project` of the new `std/project`, and every file a `Sandbox.load<Value>` names with a literal path). | `closure.trb`, `command.trb`, `receiver.trb`, `scripts.trb`, `name.trb` | 4.4 |
| **4.8** | **Done.** `Expression<Value>` and `lazy`. What is quotable, building the tree after resolution, captures and their `Encode` bound, `Quotation` in the tables, `lazy` parameters. **Gate: `compiler/tests/*.test.trb` check cleanly** (they are full of `assert`): 1606 quotation sites, every one with a recorded `Quotation`. | `quote.trb`, `call.trb` | 4.4, 4.7 |
| **4.9** | **Done.** Visibility and program shape. `private`/`private(var)`/`public`, top-level code, compile-time constants with folding, field and parameter defaults, `native`, `shared type`/`shared trait`, `isSame`, `foreign`, entry files vs. modules. | `declaration.trb`, `constant.trb` | 4.2 |
| **4.10** | **Done.** The conformance suite. `torb check std`, `torb check examples`, `torb check compiler` clean, and **every expression of the repository has a type** (`Deferred` is unreachable from valid code); the exclusivity rule after the model of Swift; the generated members of a literal type; `torb check --timings`. | all | 4.1 - 4.9 |

Parallelism:

```text
4.1 ─► 4.2 ─► 4.3 ─► 4.4 ─┬─► 4.5 ─┐
                          ├─► 4.6 ─┤
                          ├─► 4.7 ─┴─► 4.8 ─► 4.10
       4.2 ──────────────────► 4.9 ───────────►
```

- 4.5, 4.6 and 4.7 are independent of each other and can run at the same time (they touch different files; all three
  extend `statement.trb`/`name.trb` at separate points).
- 4.9 only needs 4.2 and can run beside 4.3 and 4.4.
- 4.8 needs 4.7 (implicit parameters and receivers must already be resolved before a tree is quoted).

What each one is tested with, beyond its own unit tests:

| # | Real files that must check cleanly |
|---|---|
| 4.1 | -  (every type position of `compiler/`, `std/` and `examples/` builds a type without a crash; a test asserts that) |
| 4.2 | `compiler/src/syntax/source.trb`, `diagnostic.trb` (clean; 134 of their 198 expressions typed - what is left needs the trait members of `List`, `String` and `Option`, which is 4.3) |
| 4.3 | `compiler/src/syntax/token.trb`, `std/prelude/src/compare.trb`, `convert.trb`, `operators.trb` |
| 4.4 | `compiler/src/syntax/**` complete, `std/prelude/src/option.trb`, `result.trb`, `iteration.trb` |
| 4.5 | `compiler/src/syntax/**` stays clean, `std/prelude/src/collections/**` |
| 4.6 | `compiler/src/**`, `std/prelude/src/stages.trb`, `collectors.trb` |
| 4.7 | `examples/config-dsl/**` (`config.trb` included), `examples/tour/src/09-dsl.trb`, `examples/game-engine/src/**`, and every `project.trb` of the repository |
| 4.8 | `compiler/tests/**`, `examples/query-provider/**`, `std/prelude/src/expression.trb` |
| 4.9 | `std/**` (visibility and `native`), `compiler/src/main.trb` (an entry file) |
| 4.10 | everything, plus `examples/tour/**` and `examples/game-engine/**` |

### What 4.1 does differently from sections 1 to 7

The design above is the plan; where it did not fit what milestone 3 actually built, the code won and this is the
list. Everything else is as written.

- **`Void` and `Never` are `TypeForm.VoidType` and `.NeverType`.** A case may not shadow a type name of the prelude,
  which is the same rule that makes the syntax tree say `TupleType` and `FunctionType`. For the same reason the field
  that holds a type is `annotation`, not `type`: `type` is a keyword.
- **`typeFromReference` lives in a file of its own, `lowering.trb`, not in `type.trb`.** Building a type expands
  aliases and asks a declaration how many parameters it has, so it needs the signature tables; and building a
  signature needs the types. The two files use each other, which is what cyclic imports are for. `type.trb` stays the
  representation: forms, interning, the signature *types*, and `describeType`.
- **`describeType(types, symbols, id)`** takes the two tables instead of the whole `Checker`, which is what keeps
  `type.trb` free of a dependency on `context.trb`. `Checker.describe(id)` is the short form the pass uses.
- **The generic parameters of a declaration are their own question** (`genericsOf`), separate from its signature. An
  arity check must not build the signature of a type, or naming `List<Int>` would drag in the whole standard library.
- **`Tables` does not exist yet.** 4.1's only per-module output is the type of every type position, and it is held as
  `List<Map<Span, TypeId>>` in the `Checker` - the shape milestone 3 hands over its name resolutions in. `Resolution`,
  `Adaptation`, `Dispatch` and `Witness` arrive with the passes that fill them.
- **`checkTypes` gives the `Checker` back**, and that value *is* the result of the pass. `semantics/check.trb` reads
  the diagnostics, the symbols and the type positions out of it, and `Checked.checker` hands it to the next milestone.
- **`resolveTypes` now runs for every module**, not only for the requested ones: the checker looks a name of an
  imported module up in the same table, so that table has to exist for every module. Only what was asked for is
  reported.
- **Members get ids.** Fields, cases, constants and methods are not names of a scope, so milestone 3 gave them none.
  The checker declares them while it builds the shape of their type, which is what lets `Resolution.Field` name one
  and a message point at it. `SymbolKind.Field(owner)` is the one case this added to `symbols.trb`.
- **A variadic parameter holds the type of one item**, not the `List<Item>` the parameter is. The list is one
  `Nominal` away, the item type is what checking an argument needs, and `describeType` prints `...rest: Int64`.
- **The name of a parameter of a function type is not part of type identity**, exactly like the label of a tuple
  (gap 17). `sameType` ignores both, the forms keep both.
- **A signature cycle can only be an alias in 4.1.** Nothing else lowers an inferred type yet, so `signatureOf` and
  `typeSignatureOf` carry the same state machine but only `aliasTargetOf` can reach it.
- **An inferred result is `Invalid` for now.** A declaration without a body and without a return type returns `Void`;
  one *with* a body and without a return type has an `Invalid` result until 4.2 infers it. The same for a `const`
  without an annotation.
- **A default type argument is filled in where it is written.** In a bound (`with Add` on `Meters`) the `Self` of the
  default is the subject of the bound, so it means `Add<Meters, Meters>`. In a plain type position (`field: Add`)
  nothing says what implements it, so `Self` stays the trait's own parameter and it means `Add<Self, Self>`.
- **An explicit type argument in an *expression* (`Matrix<Rows, Other>()`) may be a type or a value.** Which parameter
  it fills is only known once the callee is resolved, which is 4.2, so `TypePosition.Either` allows both there.
- **`describeType` prints the expanded type, not the alias.** `EntityId (Int64)` needs the alias to survive in the
  form; showing it is part of polishing the messages in 4.10.

### What 4.2 does differently from sections 1 to 7

- **`TypeForm.Deferred` is the "not checked yet" type.** It absorbs every message exactly as `Invalid` does, but it
  means "a later sub-milestone decides" instead of "this is wrong", and that is what makes progress measurable:
  `Checker.statisticsOf(module)` counts the expressions of a module that have a type against the ones that do not, and
  `torb check --statistics` prints it. Every expression the checker looks at is recorded, `Deferred` included, so the
  number is a fact and not an estimate. `describeType` prints it as `unchecked`; it never appears in a message.
- **Nothing that is not resolved yet is reported as missing.** A member that is not in the body of a type is only an
  error when the type comes with no trait *and* no `extend` anywhere in the program adds to it (`isFullyKnown`); a bare
  name that is not found is only an error when no receiver with unknown members is in scope. That is what keeps `std/`
  and the examples free of messages that belong to 4.3, and it is why `checkTypes` collects the targets of every
  `extend` of the program up front (one walk over the top-level declarations, no types built).
- **Operators and `a[k]` go through the `with` list of a declaration, transitively** (`declaredTraitArguments`), not
  through implementations. `Int64 with Signed`, `Signed with Numeric`, `Numeric with Add<Self, Self>` answers
  `1 + 2` with `Int64` and makes `Meters + Float` a mistake, and it needs nothing from 4.3. The same walk answers
  `Indexed` for `a[key]`, `Slice` for `a[from..to]` and `Iterate<Item>` for `for x in xs`, so `for` is *not* deferred.
  4.3 replaces the walk with the real lookup; the memoization it needs is already there.
- **A comparison is a `Bool` and an interpolated text is a `String`, whatever implements them.** Only the witness is
  4.3's, and deferring the *type* of every `==` and of every `"{x}"` would have left almost nothing typed.
- **The result of a `fn` without a declared one is inferred by checking its body, exactly once.** `signatureOf`
  registers the signature with the result `Void` *before* it checks the body, so a recursive call still sees the
  parameters it takes; if the body turns out to produce something else, the `Void` the recursion assumed was wrong and
  the message of section 2.7 is given then. That is what lets every recursive `fn walk(...)` of the compiler stay
  un-annotated while `fn count(value: Int) { ... return 0 ... }` is reported. A body is checked once, whether the walk
  over the module reached it or a caller asked for its result first (`Checker.checkedBodies`).
- **The arms of an `if` or a `match` that nothing is expected of are not checked against each other.** Section 2.1
  makes the first arm the expectation of the others, which would force every body without a declared result whose last
  statement is a `match` used for effect to agree on `Void` with every arm. The type of the whole is the first arm that
  produces a value; an `if` whose branches disagree is what the inference contexts of 4.4 report.
- **Closure bodies are walked but not checked.** A closure records `Deferred` for its body and everything in it, and
  the type positions inside of it (the annotations of its parameters and of the bindings of its blocks, the signatures
  of the `fn`s it declares) are still built - so what 4.1 promises stays true inside a construct 4.4 will check.
- **`checkPattern` takes `isVar`**, because `var (a, b) = pair` binds mutable names and `match` arms do not.
- **`BlockResult` says whether the block ends in an expression.** A body that does gets the message about that
  expression (`Expected `Float64`, found `Void``), one that does not gets "This function has to return a `Float64`" at
  its closing brace. One root cause, one message.
- **`loop { ... }` without a `break` diverges**, so a function whose body is one needs no other result. Every other loop
  may run zero times, so nothing follows from one. `while true` is the error "A loop that never ends is written `loop`":
  divergence is a property of the syntax, not of a condition the checker has to recognise as a literal.
- **`break` outside a loop reads "`break` is only allowed inside of a `for`, a `while` or a `loop`"**, with the note about
  closures the catalogue asks for. The catalogue's "`break` cannot leave a closure" needs a checked closure body, which
  is 4.4.
- **The body of a function is a scope of its own**, so a `const` of it may shadow a parameter. Only two declarations of
  the *same* block are a redeclaration.
- **`Expectation` and `Tables` live in `context.trb`**, with `Resolution`, `Adaptation`, `Dispatch`, `BindingId`,
  `LocalBinding` and `BodyState`. `Dispatch` has only `.Direct` and `Adaptation` only the cases 4.2 fills; 4.3 and 4.4
  add the rest. `Tables` is one per module, in `Checker.tables`, in the shape the type positions already use.
- **Two traps of stage 0 shaped the code.** `f(checker, checker.x)` and `f(checker, g(checker, x))` read a place that
  the `var` parameter has already taken out, and a `var fn` method whose argument reads another field of `self` does
  the same - so every such read is hoisted into a local first, and the short forms on `Checker` (`typed`, `resolved`,
  `adapted`, `reportHere`) write through their own path instead of calling the long form.

### What 4.3 does differently from sections 1 to 7

- **A supertrait is reachable.** Section 4.2 makes a supertrait a *requirement*, and it stays one: that a type with
  `with Compare` also provides `equals` is what `checkImplementations` verifies. But a type that meets the requirement
  also **implements** the supertrait, so `resolveTrait` searches the supertraits of everything a type declares. That is
  the only reading under which `1 + 2` is an `Int64` (`Int64 with Signed`, `Signed with Numeric`,
  `Numeric with Add<Self, Self>`), and it is what 4.2's walk over the `with` list did already. The `Resolved.Found` of
  such an answer names the implementation the type *wrote down* (`Int64 with Signed`), not one for the supertrait -
  there is none to name.
- **There are no inference variables in trait resolution.** An implementation's target is a pattern, the type is
  concrete, and one-directional matching binds the implementation's own parameters (`matchPattern`). Every parameter has
  to be bound, so the blanket `extend<Source, Target> Source with Into<Target>` answers `resolveBound(X, Into<Meters>)`
  and not `resolveTrait(X, Into)`. This is why trait resolution works a whole sub-milestone before inference exists.
- **An implementation whose target is a trait applies to every type that implements it.** `extend<Item: Show> List<Item>
  with Show` makes `ArrayList<Int>` showable. Such a target is a third shape next to "one head" and "every type"
  (`TargetShape.Bounded`), and it is what `List.from`, `Map.from` and the `Show` of every collection rest on.
- **`Implementation.capability` is the field the design calls `trait`**, because `trait` is a keyword. The data model of
  section 4.2 (`Implementation`, `ImplementationTable`, `Resolved`, `Witness`, `DerivedImplementation`) lives in
  `type.trb` next to `Signature`, and only the algorithms are in `implementation.trb`.
- **`Candidate` moved to `context.trb`.** Member lookup is the hottest question of the pass, so the `Checker` keeps the
  answers (`memberAnswers` by module, receiver and name) - and what a table of the `Checker` holds cannot live in the
  file that asks the question.
- **Three memoizations carry the performance**, and all of them were needed: the trait answers (interned type id plus
  trait symbol), the member answers, and the visible extensions of one module. The one that mattered most was *not*
  adding a cache but removing a scan: looking at every blanket and every trait-targeted implementation for every type
  cost 4 seconds of a 14 second run, so `declaredFor` only ever returns what a type writes down and everything
  program-wide goes through the per-trait index.
- **`Resolved.Ambiguous` says nothing at the use.** Two implementations that both answer are an overlap, and
  `checkCoherence` reports that at the two declarations - one root cause, one message.
- **A derived implementation is an ordinary one.** `Show`, `Equals`, `Hash`, `Encode`, `Decode` and the `From` of a
  wrapper case are generated on the first question and then indexed like any other implementation, so everything after
  that needs no special case. The list of what has to be emitted is `Checker.derived`, which is global and not per
  module - a type is derived once for the whole program, not once per file that uses it. `Compare` is **not** derived:
  the concept generates `Equals`, `Hash`, `Show` and `copy`, and an order is a decision, not a structure.
- **Whether a derivation is possible is assumed while it is being answered.** `Expression` contains a
  `List<Statement>` that contains an `Expression` again, and the generated `Show` of it is well defined - it recurses
  exactly as the value does. So the question is memoized *before* it is answered, under a key that always carries the
  applied bound, and a cycle finds the assumption instead of the depth limit.
- **`Int.from` is one name with five signatures**, because `Int64` implements `From<Int32>`, `From<UInt8>`,
  `From<UInt16>`, `From<UInt32>` and `From<Char>`. That is the one overload set the language has, and which one is meant
  follows from the argument - so such a member records `Candidate.needsArguments` and stays `Deferred` until 4.4.
- **A comparison, an interpolation and `==` on a collection are not reported.** The prelude has `Show` for `List`,
  `Set`, `Map`, `Option` and `Result` (gap 35) but no `Equals` and no `Hash`, so a message about "`List<Token>` does not
  implement `Equals`" would be about the standard library and not about the source. `==`, `<` and `"{x}"` therefore
  record the witness where they find one and say nothing where they do not; 4.10 closes the gap in the prelude and turns
  the messages on. The same holds for an unmet `where` clause on a member (`contains` needs `Item: Equals`).
- **A default type argument that mentions `Self` needs a bound.** 4.1 filled `Self` in with the trait's own parameter in
  a plain type position; `field: Add<Int>` now reads "`Add` needs its second type argument here". The `Self` of an
  enclosing type is a *different* `Self`: in `field: Add` the default means "whatever is stored here", which is an
  existential the language cannot write down.
- **`?` says nothing where the enclosing result is neither an `Option` nor a `Result`.** `fetchUser(id).await()?` stands
  in a function that returns a `Task<Result<...>>`, and that rule belongs to milestone 7. The design's one error - an
  `Option` in a function that returns a `Result` - is reported.
- **A literal type is not reported about.** `Status.tryFrom(text)` needs the generated `TryFrom<String, _>` of a literal type, and
  nothing writes down what its failure type is, so `isFullyKnown` says no for a `Literals` form and the generated
  members of a literal type wait for 4.10.
- **`checkExtension` reads its members back out of the index.** Building the index gives every member of an `extend` its
  id; building them a second time would hand the same source two sets of symbols, so `check.trb` looks the
  implementation up by the span of the `extend`'s target and only checks the bodies.

### What 4.4 does differently from sections 1 to 7

- **The variable table is append-only and the tables of a statement are walked once at its end.** An interned
  `Variable(n)` names a variable by index, so an id may never be reused; a context is therefore a *watermark* into
  `Checker.variables` and not a table of its own. And because an expression is recorded before the variable in its type
  is solved (`const items: List<Int> = []` records `List<_>` and learns `Int64` afterwards), every span that was written
  to while a variable was open is remembered and re-read when the outermost context closes - `pruneAt` puts the
  solutions into `expressionTypes`, `patternTypes`, `resolutions`, `adaptations`, `typeArguments` and `witnesses`. That
  walk is the only thing inference allocates beyond the variable table.
- **Only the outermost context settles and reports.** A block inside an expression has statements of its own, and the
  last one of them belongs to the statement *around* it: `const end = if c { None } else { Some(x) }` learns what `None`
  is from the other branch. Nested contexts are therefore bookkeeping, and "cannot infer" is said once, per statement.
- **A literal solves a variable right away** instead of falling back when the context closes. Design 2.2 defers the
  default, design 2.6 needs `0` to have solved `State` before `fold(0) { sum, number => ... }` reads its closure - and
  the second one is what real code depends on, so the literal adapts to `Int64` / `Float64` and unification takes it
  from there. `Adaptation.Literal` therefore replaces instead of accumulating (`Checker.adaptedLiteral`), because
  picking the right `Int.from` reads its argument before it checks it.
- **The arms of an `if` and a `match` are unified afterwards, not checked against the first one.** Section 2.1 makes the
  first arm the expectation of the others, which reports `declareOne`'s `match` (whose first arm gives a `SymbolId` and
  whose others do not give anything) although its value is never used. So every arm is checked against what was expected
  of the whole - nothing, usually - and `armResult` unifies what came out: where the arms agree that is the value, and
  where they do not they were *statements* and the whole is `Void`, silently.
- **The expected type of a call reaches the result of its callee before its arguments are read** (`matchResult`).
  `employees.collect(maxBy { _.age })` learns the `Item` of `maxBy` from the parameter of `collect` and only then reads
  the closure. Nothing is reported there - the real check happens where the value arrives - and a bare variable is never
  solved with a trait type, because a value *coerces* into one and a coercion may not solve a variable (2.5).
- **A coercion into a trait type solves the arguments of the trait** (`matchedBound`). Every collection parameter of the
  prelude is an `Iterate<Item>` whose `Item` is open at the call, and what carries the answer is the implementation the
  coercion finds: `traitArgumentsOf` gives `[Int64]` for a `List<Int64>` and those unify with the bound's arguments.
- **`Self` is a name, so `Receiver` and `Value` are the same parameter mode** (`sameMode`). `Point.area` read as a value
  is `(self: Point) => Int64`, and `[p, q].map(Point.area)` passes it where `(value: Point) => Int64` is wanted.
- **A witness carries the witnesses of its own bounds**, and they are recorded per call in a table of their own
  (`Tables.witnesses`), because `Dispatch.Direct` has none and a free generic function has bounds. `Dispatch` gets the
  same list, so 4.3's empty lists are filled either way. The whole tree is memoized by the two interned ids.
- **`Compare` is derived for a tuple, and `From<Self>` for every type.** An order is a decision and not a structure, so
  no `type` gets a derived `Compare` - but a tuple has no declaration anybody could write one in, and
  `diagnostics.sort { (_.span.start, _.span.end) }` is the idiom of the language. And `fn sum(): Item where Item:
  Add & From<Int>` asks `Int64` for `From<Int64>`: every type converts from itself, and a reflexive `From` cannot be
  written as a blanket implementation because it would overlap with every other one.
- **A supertrait requirement is satisfied by any implementation of the same target.**
  `extend<Item: Hash> List<Item> with Hash` leans on `extend<Item: Equals> List<Item> with Equals` for the `equals` that
  `Hash with Equals` requires; `providerOf` searches the open implementations (a trait as a target is not in `byHead`)
  and checks the other implementation's bounds under the match.
- **Object safety is narrower than section 4.4 says.** Only a parameter whose type *is* `Self`, and a member without
  `self`, are rejected on a trait-typed value. `Self` in the *result* stays legal: `List<Item>` is a trait and
  `items.added(x)` gives back a value of the same trait type, which is representable.
- **Implicit parameters are reached through the closure frame, not through the scope chain.** A frame holds the names
  and types the expected function type gives, and the binding of one is made the first time it is used, so the unused
  ones cost nothing. `_` means the innermost closure without a parameter list; a *name* reaches outward, and reading one
  from further out captures it. The design's "an implicit parameter name that would shadow a visible name is an error"
  is **not** implemented: the local wins, silently. Reporting it needs a look at the body before it is checked, and
  `any { _ == value }` next to a parameter called `value` is ordinary prelude code that must not be a mistake.
- **A closure body is checked against the expected result even when that result is still a variable.**
  `flatMap { _.manager.toList() }` learns its `Output` from the `Iterate<Output>` the closure has to produce, and that
  is a coercion and not a unification, so `Expectation.None` would lose it. The result is still what the body produces.
- **A closure whose parameters do not line up counts as the type that was asked for**, and its body is checked without
  an expectation: one root cause, one message.
- **A receiver closure and a quotation defer, and the variables of their call are marked as answered**
  (`suppressVariables`). What is missing is the construct (4.7, 4.8) and not an annotation, so "cannot infer" would be a
  message about the checker.
- **A field or a `const` that holds a function is callable with `()`.** `state = step(state, value)` and
  `button.onClick()` need the function type of the member as a call form, which `Candidate.form` does not carry for a
  field. One holding a *receiver* closure (`const perimeter: (self: Rectangle) => Int`) stays 4.7's.
- **A name that denotes a type or a namespace is recorded with the type it names.** `TokenKind` in `TokenKind.Dot` is
  not a value, but recording `Deferred` for it would say "not checked yet" about something that is fully understood, so
  a type name records its type, a constructor records the function type of the constructor, and a namespace records
  `Void`. Nothing reads it: the resolution (`TypeOf`, `Construct`, `Namespace`) is what milestone 5 uses.
- **`?` decides the failure type of a subject that nothing else does** (`matchedFailure`):
  `value.okOr(.Missing("port"))?` in a function that returns `Result<_, ConfigError>` unifies the open failure type with
  `ConfigError`. A failure type that is already known stays what it is and is converted through `From` as always.
- **`if const`, `while const` and a list pattern get their types here**, although 4.5 owns the patterns: the type of the
  subject and the `Item` of its `Iterate` are available, and leaving them `Deferred` would have left a tenth of the
  parser unchecked for no reason.
- **`closure.trb` is the only new file.** `unify.trb` grew the contexts and the unifier, `call.trb` the instantiation,
  the two argument passes and the bounds, `name.trb` the generic targets and the captures, `member.trb` the generic
  members and the overload sets, and `expression.trb` the collection literals, `.Case` and the arms.

### What 4.5 does differently from sections 1 to 7

- **The algorithm is its own file, `usefulness.trb`, and `exhaustive.trb` is the two things around it.** The matrix
  knows exactly one shape - "everything, or one constructor applied to arguments" - and it knows nothing about the
  syntax tree. Turning a `Pattern` into that shape, and turning the answer into a message and into a `MatchPlan`, is the
  other half and it is twice the code. `pattern.trb` stayed what it was: the types a pattern gives its names.
- **`MatchPlan` lives in `exhaustive.trb`, and `context.trb` imports it.** 4.1 and 4.3 put their data model in
  `type.trb`, but a plan is not a type and nothing but the lowering reads it, so `Tables.matches` is the one line that
  crosses over. The two files use each other, as `lowering.trb` and `signature.trb` do.
- **An arm is a list of _branches_, one per alternative.** `1 | 2 | 3` is three, and nested alternatives multiply out,
  because `.Circle(r) | .Ring(r)` keeps `r` at a different path in each of them and a single flat list of tests could
  not say that. Every branch carries its own tests and its own bindings, and the arm matches when one of them does.
- **A plan is recorded for every pattern position, not only for `match`.** `if const`, `while const`, a destructuring
  `const`/`var`, a `for` pattern and a closure parameter are lowered exactly like a `match` with one arm, so they are
  recorded in the same shape - under the span of their **pattern**, while a `match` is recorded under the span of its
  **subject** (which is also where the message about a case that is not handled points). Both are the node milestone 5
  holds in its hand when it needs the plan. A pattern that is a plain name records nothing: there is nothing to decide.
- **A binding is looked up, not remembered.** The plan of an arm is built while the arm's own scope is still open
  (`armPlanOf` is called right after `checkPattern`), because a pattern's names are locals and a local is reachable
  nowhere else. That is what makes `BindingId` in the plan free of a side table.
- **Alternatives bind in a scope of their own and are then declared once.** `.Circle(size) | .Rectangle(size, _)` needs
  every alternative to bind the same names with the same types, so each one is checked in its own scope, they are
  compared, and the names of the first are declared for the arm - which also gives the arm exactly one `BindingId` per
  name, whatever path the value took to it. Before 4.5 the second alternative reported "`size` is already declared".
- **`Int64`, `UInt64`, `Char`, `String` and the decimal types have no coverable range.** Splitting integers into
  intervals is what the design asks for and what happens, but only for the widths whose bounds can be written as
  literals of the language the checker itself runs in - `Int8` to `UInt32`. A `match` over an `Int` therefore always
  needs a `_`, which is true of every real program, and the witness there is `_`. That is the safe direction: the
  checker never reports a `match` that is in fact complete.
- **A pattern the checker does not model stands for itself.** A decimal literal (told apart by the text it is written
  as), a range over anything but an integer type, a list pattern with an item *behind* the `...` - each becomes one
  opaque constructor that covers nothing but itself. So it never makes a `match` exhaustive and never hides an arm
  below it, and `1.5` written twice is still found.
- **Anything already reported keeps the `match` quiet.** A case that does not exist, a pattern with the wrong number of
  fields, a tuple of the wrong width and a subject the checker could not type all mark the normalization as broken, and
  then neither exhaustiveness nor reachability is reported: one root cause, one message.
- **The witness search stops where nothing is covered.** A matrix without rows means "every value here is missing", so
  the witness is `_` per column instead of a walk into the constructors - which is not only cheaper but the only reason
  the descent ends at all, because a `shared type` may contain itself. A depth limit of 24 behind that turns the
  remaining case into an answer rather than a hang.
- **Three optimizations carry the performance, and all three were needed.** A row of nothing but wildcards in the
  matrix answers both questions without looking at the column at all (that is the `_` arm, and it is what makes 4.5
  cost +0.7 s over the whole repository instead of minutes). A case, a single value and an opaque constructor cover
  exactly themselves in every split, so a `match` over sixty token kinds never builds the constructors of its type.
  And a name or `_` as the pattern of a binding is irrefutable whatever its type is, which is almost every binding of a
  program.
- **An arm is reachable when one of its alternatives is.** `1 | 1` is redundant in itself and the arm still matches, so
  the redundancy of a single alternative is not reported - only of a whole arm (gap 25).
- **The refutability of a closure parameter is only checked where the parameter type is known.** At
  `items.map { Some(value) => value }` the `Item` is still an inference variable when the closure is read, so nothing
  is said; with an expected function type (`const unwrap: (Int?) => Int`) it is.
- **`while const P` with an irrefutable pattern reads "so this `while` never ends".** Design 5.5's "Use `const`" is the
  message for `if const`; in a loop that advice would be wrong, and what the author wrote is an endless loop.
- **More than three witnesses are counted.** Up to sixteen are collected so that the message can say "and 2 more" with
  a number instead of "and more", and only beyond that does it give up on the count.
- **The one finding in the sources was `compiler/src/ir/instantiate.trb`**, whose `match` over `TypeForm` handled every
  case but `Deferred` - which now reports the same way `Invalid` does.

### What 4.6 does differently from sections 1 to 7

- **A `Place` is a root plus a list of steps, not a tree.** `Place(root, steps, at)` with
  `PlaceRoot.Local | .Declared | .Temporary | .Unknown` and
  `PlaceStep.Field | .Index | .Range | .TupleField`. Section 5.1's nested form makes every question about a path a
  recursion; the flat one makes "is one path a prefix of the other" a loop over two lists, which is what exclusivity
  asks about every access. Two cases of the design fall away with it: **`.Receiver` is a `Local`** whose binding
  `isReceiver` (`LocalBinding` says so already), and **`.Unknown` is the fourth root** - a construct a later
  sub-milestone resolves, or one a message was already given about, and every rule of this pass stays quiet about it.
- **`PlaceRoot.Declared` is the root the design has no case for.** A top-level `const` or `var` of a module, a script
  or an entry file is a *symbol* and not a local (milestone 3 declares it, and `checkTopLevelBinding` asks for its
  type), so `counter.increment()` at the top of `examples/tour/src/03-types.trb` has no `BindingId` to point at.
  Whether such a root may be written is read back off its declaration site (`DeclarationSiteKind.Constant(binding)`),
  which is the only place the `var` survives.
- **A step carries the type of its base.** `Index(base, at, literal)` and `Range(base, at)` hold the interned type of
  what they index, because deciding `MutableIndexed` needs it and milestone 5 needs to know which `set` or `replace`
  to emit. `Index` also holds the key **as it was written where that was a literal**, which is the whole of "two
  indexes can be told apart": `items[0]` and `items[1]` are disjoint, `items[i]` and `items[j]` are not, and a window
  overlaps everything of its base.
- **`placeOf` never checks an expression a second time.** It reads `resolutions` and `expressionTypes` out of the
  tables, which are filled by the time anybody asks for a place, so the whole sub-milestone is a walk over the syntax
  tree plus two map lookups per node. That is what keeps it off the hot path - and it is why `placeOf` takes
  `var checker` although it decides nothing: the memoized member lookups behind `traitArgumentsOf` do.
- **`private(var)` carries its `var` in the modifier.** `private(var) balance: Int = 0` parses to
  `Visibility.PrivateVar` with `Field.isVar == false`, so "is this a `var` field" was the two of them together - until
  4.9 made the signature say it (`FieldSignature.isVar`), which is where it belongs: the place only asks *who* may write
  the field, never whether anybody may.
- **`private` is checked as gap 29 states it, not per package.** A private field is writable inside the body of its
  type and inside every `extend` of it in the same package - both are "`Self` is this type here" - and the *head* of
  the type is compared and not the whole type, so a member of `Holder<Item>` may write a field of a `Holder<Int>`.
- **A tuple position is as writable as the binding that holds the tuple.** A tuple has no `var` markers to consult and
  no declaration anybody could put one in, so `var pair = (Counter(), 1)` makes `pair.0` a place.
- **Exclusivity is two halves, and the second one is where the traps live.** A `var` receiver and every `var` argument
  are pushed on a stack of open accesses as they are checked - so the arguments after them and every closure argument
  run inside them, which is the DSL rule - and at the end of the call everything it *reads* is held against the whole
  stack. That second half is what rejects `f(checker, checker.something)` and
  `pending.removeAt(pending.length() - 1)`: the receiver reference is formed before the arguments are evaluated, so a
  read of the same path inside them is a second access. The read walk only runs while the stack is not empty, and it
  does not descend into a path it has already checked (the base is a prefix) or into a closure body (that body was
  already checked while the accesses were open). A read that stands in a statement of its own inside an open access,
  with no call of its own, is therefore not caught - the conservatism boundary of the implementation.
- **The message for a conflict is one shape with two notes.** The catalogue's two texts are one message
  ("`root` is being changed by `div` right now") plus the note that fits: "`items[i]` and `items[j]` cannot be told
  apart. Use `items.swapAt(i, j)`" where both sides pass through an index, and "While a `var` access runs, the same
  path cannot be reached a second time" otherwise. The checker cannot print the source text of a path, so the message
  names the root and its field steps and writes `[...]` for an index.
- **A dead change needs the change to be the *only* effect.** Design 5.3 counts every change; the implementation
  counts a change to a place only where the call produces `Void`/`Never` or its result is thrown away, plus every
  assignment. `cursor.next()` hands its value on, so the change to `cursor` is not what the statement is for and
  `fn first() { var cursor = iterate()  cursor.next() }` - the idiom gap 2 asks for - is not a mistake.
- **A change through a step reads the old value; only `x = value` replaces the whole binding.** So the read that
  resolving the target produced stays for `x.part = value` and goes away for `x = value`, and the change itself is
  noted *after* the value has been read - otherwise `total = total + 1` would count its own right-hand side as the
  read that keeps it alive.
- **A reference is exempt, and three things are one.** A `var` parameter, a `var fn` receiver and the names an `if var` pattern
  binds into its subject all write into a place of the caller, so `LocalBinding.isParameter` marks all three and the
  dead-change rule skips them. It is also what a closure may not carry off (BACKEND gap 14). A `const` binding and an
  exempt one are never recorded at all, which is what keeps the use list of one body short in a pass that names
  `checker` on every second line.
- **A verb in a loop keeps itself alive.** `for x in xs { tally.increment() }` reads `tally` and writes it, so the
  loop brings the read back around to the change and the rule says nothing, even where nothing after the loop reads
  the total. Design 5.3 asks for exactly one pass per loop and no fixpoint, and this is the price.
- **`if var` does not hold an open access over its body.** Design 5.5 says the subject is inside a `var` access while
  the body runs, but the names the pattern binds *are* paths into that subject, so an access to it would conflict with
  every use of them - and `if var Some(inner) = current { inner.next() }`, which gap 3 exists for, would be an error.
  The subject is checked as a place, the place is recorded under its span (which is how milestone 5 knows the
  bindings are references into it), and the bindings are marked as references.
- **`ClosureKind` is decided by where the closure is written, and nothing else.** A closure that stands straight as an
  argument of a call is `.Local`, everything else is `.Escaping`. BACKEND gap 14 adds "and is not stored by the
  callee", which needs an interprocedural answer the checker does not have; the conservative half is the one that
  matters, because a closure that is bound to a name, returned or built in a literal is exactly what may outlive the
  call. Naming the receiver - `self`, or a member of it written without it - now counts as capturing it, which is what
  makes "the receiver may not be carried off" reportable at all.
- **`break` cannot leave a closure** is reported where the closure has a loop around it, which needs the loop depth
  *outside* the closure: `ClosureFrame.enclosingLoopDepth`. Without one the message stays 4.2's
  "`break` is only allowed inside of a `for` or a `while`".
- **`mutation.trb` is the second file.** `place.trb` is the places, the mutability walk, exclusivity, gap 20 and the
  `break` question; `mutation.trb` is the two rules that need the whole body - dead changes and the closure kinds -
  and the bookkeeping they rest on. The hooks elsewhere are four lines in `call.trb` (the accesses of a call), three
  in `statement.trb` (assignment, the loops, settling a body), two in `expression.trb` (`if var`), two in `name.trb`
  (a read, and the receiver as a capture) and three in `closure.trb` (the loop depth, the reference parameters, the
  kind).

### What 4.7 does differently from sections 1 to 7

- **A receiver is a parameter, so a receiver closure is an ordinary closure.** `checkClosure` needed one question
  (`receiverIndexOf`) and one branch (`declareReceiver`): everything else - the parameters from the expected function
  type, the captures, the result from the body - is what every closure does. A receiver closure whose expected type has
  parameters *after* the receiver keeps them as implicit ones, and `ClosureFrame.implicitFrom` is the offset that makes
  `Adaptation.ImplicitParameter` count them the way the declaration does.
- **Naming the receiver makes nothing implicit.** `html { root => ... }` binds `root` and sets the implicit receiver to
  *nothing*, instead of leaving the one around the closure implicit. Section 3.1 only says that the innermost receiver
  is the implicit one; reading it as "the next one out becomes implicit again" would be exactly the scope leaking gap 4
  rules out, and naming the parameter is what the concept offers instead of a second implicit receiver.
- **`Tables.receivers` is what milestone 5 reads for a receiver closure**, keyed by the span of the closure:
  `ReceiverClosure(binding, annotation, isVar, isImplicit)`. An implicit receiver has no node in the tree that could
  carry its binding, and every name that means a member of it records `Adaptation.ImplicitSelf` with that very binding -
  which is now also a **capture** of every closure in between (gap 19), exactly as an ordinary binding is.
- **A property command does not care about the call style.** Gap 15 makes "a call of a non-callable member writes it"
  the whole rule, so `port 8080` and `tls(port == 8443)` take the same path, and a bare trailing closure
  (`database { ... }`) is `CallStyle.Parentheses` in the tree anyway. The one place the style still decides is a field
  that *holds* a function: a command assigns, parentheses call ("calling a function in a field always needs
  parentheses").
- **`.Configure` needs a type that has a configuration.** A block on a field is the body of
  `(var self: FieldType) => Void`, which is only meaningful for a `type` that is written down: a `native` type has no
  fields anybody could set, and a type with cases is chosen and not filled in, so both get "`port` is a `Int64`, which
  has no configuration a block could fill" instead of a message about whatever the block contains.
- **A property command leaves the path it is written through to 4.6**, and since that slice is in it goes through it:
  `checkPropertyWrite` is `place.trb`'s `checkAssignment` with another message, so `port 8080` asks about a `var` field,
  a `private(var)` one, a `const` root and a temporary exactly as `=` does, records its `Place` for milestone 5, and
  counts as a change for the dead-change rule.
- **A call of a local binding that is not a function is reported here**, because section 3.3's "a local binding is
  written with `=`" has no other home: `count 1` reads "`count` is a `Int64`, and only a function can be called".
- **A method *is* a constant that holds a receiver closure**, so `member.trb` treats one that was written that way
  (`const perimeter: (self: Rectangle) => Int`) exactly like a `fn`: `Rectangle.perimeter` is the constant,
  `rectangle.perimeter` is it bound to the value, and `rectangle.perimeter()` calls it.
- **An annotated `const` has its initializer checked** (`signature.trb`). The annotation *is* the signature, so nothing
  asked for the value again and the body of `const perimeter: (self: Rectangle) => Int = { ... }` was never looked at.
  What that uncovered is 2.3's second paragraph: `const size: Int8 = 3 + 4` was an `Int64` sum and therefore an error,
  because the expected type stopped at the operator instead of reaching the literals inside it. It reaches them now, and
  the constant evaluator of milestone 5 no longer carries an expected type by hand (BACKEND, "What 5.2 does
  differently").
- **A receiver script is a module.** Everything the checker keeps - a scope, the tables, the diagnostics, the statistics
  - is per module, so `project.trb` and every file a `Sandbox.load` names become modules (`semantics/graph.trb`), marked
  with `Module.receiver`. They are not importable (`resolveImport` skips them), their statements are checked as
  *ordinary* statements and not as top-level declarations of a module - a `const` of a script is a local of the closure,
  which is what lets `const binary = name.substringAfter("/") ?? name` read the receiver - and a `use` in one is an
  error, because everything beyond the prelude is granted where the script is loaded (gap 34).
- **Which files those are is decided syntactically**, before anything is checked (`semantics/scripts.trb`): a walk that
  looks for `Sandbox.load<Value>("literal")` by shape. The receiver type is built afterwards, from the type argument, in
  a type position of the module that loads it. A path that is not a literal is silently not checked - only the sandbox
  of milestone 7 can know what it was.
- **The path of a script is resolved against the directory of the project**, not against the file that loads it: that is
  where the program runs, and it is where `examples/config-dsl/config.trb` sits.
- **`project.trb` is only checked where the workspace has `std/project`**, the new package that declares `Project`,
  `Dependencies`, `Build`, `Test`, `Workspace` and `Registry`. Without it there is no type to check a manifest against,
  and the static reader (`project/manifest.trb`, which stays exactly as it is until milestone 7) keeps working alone.
- **Gap 42 is the parser's already:** `startsCommandArgument` never accepted `.`, so `level .Debug` has always been the
  member path `level.Debug`. What 4.7 adds is the note that says so, on the message about the missing member.

### What 4.8 does differently from sections 1 to 7

- **One hook in `checkExpression` covers every position `Expression<Value>` can stand in.** An argument, a binding
  (gap 37), a field default, a `return` - they all arrive at `checkExpression` with the expectation, so the branch that
  quotes sits there and not at each of them. That is also why `closure.trb`'s `isQuotation` branch is gone: a closure
  never reaches `checkClosure` with an `Expression<Value>` expected any more, it is quoted one level higher.
- **The tree is not stored, the span is.** The tree of a quotation *is* the syntax tree under that span, and 4.1 to 4.7
  already recorded the type of every node of it (`expressionTypes`), what every name and every call resolved to
  (`resolutions`), and what the source did not spell out (`adaptations`). `Quotation` therefore holds only what a second
  walk could **not** work out: the captures in the order `Expression.captures()` returns them with their `Encode`
  witness, the parameters of each quoted closure by its span (`lambdas`), and which `Parameter` or `Captured` node every
  name inside becomes (`names`, by span). 1200 `assert`s in the repository would otherwise carry 1200 copies of their
  own source text.
- **A quotation is a closure as far as the scope chain is concerned.** `checkQuotation` pushes a `ClosureFrame` whose
  watermark is the current end of `Checker.bindings`, so `noteCapture` collects exactly the bindings from *around* the
  quotation - for a quoted closure and for a bare `assert(limit > 1)` alike, and correctly through any number of nested
  closures. The frame declares itself as "has parameters", so `_` still means the innermost closure the source wrote.
- **A name that already holds a quotation is passed through.** `Query.filter(isAdult)` and `Quoted.of(predicate)` hand
  the tree they have on instead of quoting it into an `Expression<Expression<...>>`, which could not exist anyway (an
  `Expression` is not `Encode`). It has to be decided before the argument is checked, and in a bidirectional checker the
  type of an expression is known without checking it only for a **name** - so a name (a local, or a top-level `const`) is
  the rule and everything else is quoted. `filter(predicateOf())` quotes the *call* into a `Call` node, which is right.
- **A tuple, a map literal and a range are quotable as constructions.** The concept's list of quotable constructs names
  "constructors", and a tuple literal, `["a": 1]` and `0..2` are exactly that: a `Construct` node, a `Call` of `Map.of`,
  a `Construct` of `Range`. 23 `assert`s of the repository compare against one of the three, and `ExpressionNode` needs
  no new kind for them - which is what "a new node kind comes with a new version of the language" is about.
- **A forbidden construct gets one message, at the construct.** The quotable set is validated in one walk *after* the
  expression was checked, and the first construct that has no node stops it: "A `const` cannot appear in a quotation:
  `Expression<Bool>` holds a single expression" for every statement, `?`, an `if` without an `else`, a pattern in an
  `if`, a second statement in a quoted closure; "`match` cannot be quoted yet"; and "A spread has no node in an
  expression tree" for a spread and for a closure parameter that destructures (a `Lambda` node names its parameters).
- **`Adaptation.Force` is new, and `LocalBinding.isLazy` is what records it.** Design 3.4 says a `lazy` argument records
  `Adaptation.Lazy`; nothing said how milestone 5 finds the *reads* that have to force the cell, and a
  `Resolution.Local` cannot say it on its own. So a `lazy` parameter's binding is marked, and every read of one records
  `Force`. Only a `fn` parameter is marked: `lazy` in a function *type* is legal, but no closure in the repository fills
  one, and threading the mode through `checkPattern` for that would be a change to every pattern.
- **A `lazy` argument records its captures in `Tables.captures`.** The thunk is a closure (BACKEND 1.6: a `Lazy(T)` cell
  holding a closure), so it needs the same list a closure needs, and `Adaptation.Lazy` at the same span is what says the
  span is a thunk and not an ordinary closure. `??` goes through the same two calls, because it *is*
  `orElse(fallback: lazy Value)`.
- **`nameOf` is not an intrinsic.** The concept calls it "an ordinary function over `Expression<Value>`", and that is
  exactly what it is: `nameOf(user.email)` quotes its argument like any other quoted parameter and reads the tree at
  runtime. The checker has no case for it.
- **The `Encode` bound needed nine implementations in the prelude.** `Encode` existed for `Bool`, `Int8`, `Int64`,
  `String`, `Option`, `List`, `Set` and `Map` and for nothing else, so a `Char` or a `Float64` anywhere inside a value
  made it unencodable - which made the whole syntax tree (`CharLiteral`), the whole `Checker` (`LiteralValue.Character`)
  and every `Vector2` uncapturable. `Int16`, `Int32`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `Float32`, `Float64`,
  `Decimal` and `Char` now carry `Encode` (`std/prelude/src/encoding.trb`); `Decode` for them is still missing, because
  it needs a `TryFrom` back that only three of them have.
- **Nothing is said about a capture whose type is not concrete**, exactly as `reportUnmetBound` does not: what a generic
  parameter of the enclosing declaration implements is decided where *that* one is called.
- **An implicit receiver that a quotation reads is a capture without a name entry.** A member of `self` written without
  it has no node in the tree that could carry the span, so the receiver's binding is in `Quotation.captures` (its value
  has to reach `captures()`) while `Adaptation.ImplicitSelf` is what tells milestone 5 which capture the `Field` node
  hangs off. The same holds for a module-level `const` read inside a quotation: it is compile-time evaluable (gap 27),
  so it is a `Literal` node and not a capture, and `Resolution.Constant` is all that is recorded.
- **A thunk and a quotation are a closure for 4.6's rules too**, and nothing had to be added for that: both are checked
  where they are written, so a name in one is an ordinary read of the binding around it (which keeps a change before it
  alive, wherever the callee evaluates it) and a capture of it (which makes that binding a shared box the dead-change
  rule leaves alone). A quoted expression is never run at the call site, but it holds no `var` access open either: it
  contains no statement and no assignment, because the quotable walk rejects both.

### What 4.9 does differently from sections 1 to 7

- **Top-level code is forbidden in a module that somebody *imports*, not in every file that is not an entry file.**
  The rule exists for one reason - "an imported module consists of declarations only, so there is no initialization
  order and a cyclic import is harmless" - and the design's own message says so (``. `./syntax/lexer.trb` is
  imported``). A file that nothing imports cannot create an initialization order, so it is a script: that is what the
  twelve files of `examples/tour` are, and there is no `src/main.trb` they could be the entry of. The `src/lib.trb` of a
  named package is always a module, because it is what other packages import. `Checker.importedModules` is one walk over
  every `use` of the workspace, kept.
- **`private(var)` *is* the `var`.** The concept writes `private(var) balance: Int = 0` without a second `var` and then
  writes the field from inside, so the modifier is what makes the field mutable - privately. `FieldSignature.isVar` is
  therefore true for a `private(var)` field, and `private(var)` on a field is never an error by itself. What it *is* an
  error on is everything that is not a field (gap 30) - and a second `var` next to it ("`private(var)` already says
  `var`").
- **Who may write a field is decided in one place, `place.trb`.** 4.9 asked it of the target of an assignment and of a
  property command, 4.6 asks it of every step of every path, and both gave the same message - so the walk over the path
  owns it and `isInsideType` is what it asks. A field that this code may not even *name* is 4.9's message alone: a
  wholly `private` one was already reported where it was named, so the place says nothing a second time, and
  `private(var)` - public to read, private to write - is the one visibility a place decides about.
- **Visibility is decided outside the memoized member lookup.** `lookupMember` keeps its answer per module, receiver and
  name; whether the code may *name* the member depends on the declaration the use stands in, so `findMember` answers and
  `requireVisibleMember` judges, once per use.
- **"Inside the type" is `Self` plus the package.** A private member is reachable where the `Self` of the enclosing
  declaration has the same head symbol *and* the current package declares it - which is exactly "the body of the type
  and every `extend` of it in the same package" (gap 29) without a separate table.
- **"The result type is mandatory" means "it is never inferred", not "write `: Void`".** CONCEPT.md's own `trait
  Collection<Item>` writes `var fn add(value: Item)` without one, so the rule cannot be that every signature
  spells `Void` out: for a `public` function and for a trait method an **omitted result type is `Void`**, and nothing is
  taken from the body. `public fn emit(var builder: Builder) { ... }` therefore stays exactly as it is written, and the
  error is at the value such a body produces: "A `public` function does not infer its result: declare it (`: Int64`)" (a
  trait method accordingly), with the ordinary `Expected `Void`` suppressed because the body is walked without an
  expectation. `ResultRule` in `context.trb` carries which of the three it is through `BodyState`, `signature.trb`
  decides it from the modifiers (and from `Self` being the owner, for a trait), and `inferredSignature` keeps the `Void`
  it registered instead of the body's answer. Applied to the repository the rule found **nothing**: all 74 declarations
  that omit a result type produce `Void` already.
- **The rule is about the `public` *modifier*, not about every non-private member.** Members are public unless marked
  `private`, so the literal reading would ask it of every method of every type. Design 2.7 says "`public fn` without a
  return type", and that is what is implemented; whether a member of a `public type` should be included is an open
  question of 4.10.
- **A `public` declaration may not name a type that is private to its file.** CONCEPT.md is silent; without the rule a
  `public fn` hands out values of a type its caller has no name for, which is the one thing "top-level declarations are
  private to their file unless `public`" is there to prevent. Parameters, results, fields, case fields, the members of a
  `public trait` and the type of a `public const` are the surface that is checked. It found three: `Query` in
  `examples/query-provider`, `HttpError` in `std/http` and `JsonError` in `std/json`, all of which named a private
  `...Kind` type in a field.
- **The evaluator of a compile-time constant is `constant.trb`, and it folds with checked arithmetic.** Stage 0 panics on
  integer overflow, so a checker that computed `9223372036854775807 + 1` would take the whole compiler with it instead of
  reporting the line: every result is tested *before* it is built (by division for the product, against `Int.maximum` and
  `Int.minimum` for the sum). Floating point overflow to an infinity is not reported, only a division by zero - a literal
  `Float` expression cannot reach an infinity otherwise.
- **The shape pass runs *after* the bodies of a module.** Whether `Point(0, 0)` is a constructor call or a function call
  is a question only the resolution answers, so `checkShape` is the last thing `checkModule` does. Diagnostics are sorted
  by span per file, so the order they are found in is not observable.
- **A construct that is not checked yet counts as constant.** A resolution of `Deferred` in an initializer means "a later
  sub-milestone decides", and reporting "this is not a constant" about it would be a message about the checker.
- **`isSame` is found by name.** `WellKnown.symbolNamed("isSame")` is enough; the special case is one comparison at a call
  site and needs no entry of its own in `wellknown.trb`.
- **The parameters of a function and the top level of its body are one scope.** 4.2 made the body a scope of its own,
  which let a `const` shadow a parameter silently; the concept is silent, and "no silent shadowing" is the rule everywhere
  else in the language. `Checker.shadowsParameter` is the whole implementation: `enterBody` clears the scopes, so the
  parameters are always the first scope of a body and its top level the second. A nested block and a closure keep their
  own scope.
- **`self` without a receiver is reported.** It was silently `Deferred`, which made `height: Int = self.width` a field
  default that nobody objected to. A `static fn` gets the same message.
- **Gap 22 was already recorded.** `Implementation.isNative` exists since 4.3 and `checkOneImplementation` skips a native
  implementation, so "a required trait member without a body in a `native type` is a requirement on the runtime" needed
  nothing but the test. What 4.9 adds is that only a package of the standard library may write `native` at all.
- **What is left off, and why:**
  - **Where `await()` is allowed** stays milestone 7's, as section 5.6 says. Everything that is checkable without data
    flow (the enclosing result type is a `Task`, an entry file, a closure passed to `spawn`) needs the `spawn` of
    `std/prelude` to mean something first.
  - **A closure passed to `spawn` may not capture a `var` binding**, and **a shared object does not cross a task
    boundary**: the captures are 4.4's table and the rule is milestone 7's.
  - **Gap 20** (a `var` binding may not be initialized from a `const` path to a shared object) is a rule about a *place*
    and belongs to 4.6. `isSharedType` is public for it.
  - **The types a `foreign` signature may name** (`Pointer<Value>`, `CString`, the sized numbers, a `foreign type`
    struct): none of those exist in the AST or in `std/` yet. What is checked is the shape of a `foreign` block - only
    function declarations, without bodies, with result types.

### What 4.10 does differently from sections 1 to 7

- **`Deferred` is checked for, not counted down.** The number in `--statistics` is 0 for the whole repository, and the
  way it stays 0 is one choke point instead of an audit of 87 `defer*` sites: `checkModule` ends with
  `reportUnchecked`, which reports "The checker did not work out the type of this expression" where a module has an
  expression without a type **and no message at all**. Every remaining `defer*` path is therefore what `Invalid` is -
  "a message was already given about this" (a parse error, a name that does not resolve, a call that could not be
  resolved) - and a hole in the checker names itself at the span instead of hiding in a counter. It found one right
  away: the test harness's `Map` trait had no `isEmpty`, and a test had been asserting "no problems" about a call that
  was silently not checked.
- **Exclusivity follows the model of Swift, which replaces 4.6's stricter rule.** The `var` access of a call begins
  once **every argument has been evaluated**, so what the arguments read has ended before it starts. Three conflicts
  are left, and `Access` carries what it takes to tell them apart: `call` (a serial per call) and `closureDepth` (how
  many closure bodies were being checked). `checkNewAccess` conflicts with an access of the *same* call always
  (`swap(a, a)`, `move(list[0], list)`, `a.merge(a)` where both are `var`), and with an access from further out only
  when the new one stands inside a closure of it; `checkAccess` - which every read and every write goes through - only
  ever conflicts with an access whose closure argument this line stands in. A quotation does not count as a closure
  here, because it is not run at the call site (`runningClosures` is bumped by `checkClosure` alone). Six of the
  fourteen tests of `exclusivity.test.trb` turned from "this is an error" into "this is ordinary code", which is the
  whole point: `items.removeAt(items.length() - 1)` was the most common line the stricter rule rejected.
- **The 39+ hoists in the sources stay, and CONTRIBUTING trap 1 is reworded.** `f(checker, checker.something)` is
  legal in the *language* now and still breaks *stage 0*, which takes a `var` argument before the other arguments are
  evaluated. So the trap is a limitation of the interpreter, not a rule of TorbScript, and it says so.
- **A `.Case` whose type only a following `?` would decide is an error.** `value.okOr(.Missing("port"))?` left seven
  expressions of `examples/tour/src/06-errors.trb` unchecked, because the failure type of the subject is still a bare
  variable when the argument is read and only `matchedFailure` (4.4) settles it afterwards. A type that *is* a variable
  names no type at all, so the short form gets "`.Missing` needs its type here: nothing expects one before the `?`"
  and the tour writes `ConfigError.Missing("port")`. A type that merely *contains* a variable is a different matter -
  in `Result<Int, Failure>` the case's owner is `Result` whatever the failure turns out to be - and stays silent.
- **A literal type's `parse` is generated, and its members are reportable.** `directTraitsOf` gives a `.Literals` form
  the traits of its base **minus** the base's `Parse` **plus** `Parse<LiteralParseError>`, which is what makes
  `Status.tryFrom(text)` the literal type's own (`0 | 1 | 3` must not inherit `Int64`'s `Parse<NumberParseError>` - both
  take a `String` and the overload could not be resolved). `searchMember` asks the traits before it falls back to the
  base, so the `Self` of `parse` is the literal type. `canDerive` no longer says yes to every trait for a literal type
  but to exactly the seven the concept names (`TryFrom<String, _>`, `Show`, `Equals`, `Hash`, `Encode`, `Decode`,
  `Into<base>`) -
  `Compare` is not among them, because an order over three strings is a decision exactly as it is for a `type`. And
  `isFullyKnown` now holds for a literal type, so a member it does not have is reported like any other.
- **`value.into` is typed, not deferred.** The callee of the one call the receiver alone cannot answer is
  `(self: Source) => Target`, which is a fact about it and not a construct a later pass looks at.
- **The resolution of an inner call survives a dot.** `resolveTarget`'s fallback branch read `Resolution.Deferred` for
  anything that is not a name or a member path, and `memberTarget` then wrote that over what the inner call had already
  recorded - so `text.trim().replace(a, b)` lost the `Callable` of `trim()`. The branch reads the recorded resolution
  back instead (`resolutionHere`). **The back end's workaround for this can go.**
- **`torb check --timings` prints the wall time of every pass**, and it stays: the checker runs on a tree-walking
  interpreter, so "which pass got slower" comes up with every change and should not need a profiler. `Stopwatch` only
  reads the clock where the flag asked for it, and the clock is one new native (`Clock.milliseconds()`, in `std/time`
  and in stage 0) because an `Instant` and a `Duration` per measurement would be two allocations for a number.
  What it shows is that the premise was wrong: **lexing and parsing is 60% of the run** (22 s of 37 s over 191 files),
  the shape pass of 4.9 costs 0.15 s, and the checker's own share is the 13 s of "bodies". The one accidental cost
  inside it that was found and removed: the member-lookup cache built its key by interpolating four values into a
  string, for every member of every expression of the program - the key is a tuple now, and so are the keys of the
  trait answers, the witnesses, the derivations, the derived index, the visible extensions and `byTraitAndHead`.
- **What is left off, and why:**
  - **The messages about a missing `Equals`/`Compare`/`Hash`/`Show`** (4.3's note) need `Equals` and `Hash` for
    `List`, `Set` and `Map` in the prelude first: without them the message would be about the standard library. The
    prelude change is small, the number of new diagnostics it would turn on across `std/` and `examples/` is not, so
    it wants a slice of its own.
  - **An unmet `where` clause on a member**, **object safety for an operator on two trait-typed values**, **an
    implicit parameter name that shadows a visible name** and the **exhaustiveness leftovers** (a closure parameter
    re-checked after the inference context closes, a qualified case path against the subject, an impossible pattern in
    `if const`, `Int64`/`UInt64`/`Char` ranges) are all still open, for the same reason: each is a message with its own
    tests and none of them is what kept an expression from having a type.
  - **`EntityId (Int64)` in a message** needs the alias to survive out of the type, which is a side table keyed by the
    span of the reference. It is cheap and correct, and it is not in.
  - **A literal above the `Int64` maximum** (`UInt64.maximum` as `18446744073709551615`) needs the checker to hold a
    value that does not fit its own `Int`. `LiteralValue.Integer` is an `Int64` and the IR reads it, so the change
    crosses into milestone 5's contract; `UInt64.maximum` stays `UInt64.minimum.bitwiseNot()`, which is exact.

### What the collection literals add (gap 51)

A slice of its own after 4.10, because `listLiteralType` handed back *any* expected collection type unverified:
`const wrong: Array<Int, 4> = [1, 2, 3]` and `const names: Set<String> = ["a", "b"]` both checked without a problem, and
nothing told a back end how such a literal is built.

- **A list literal has three shapes and a fourth answer, decided by the expected type alone,** and which one it is now
  stands in `Tables.collectionLiterals` at the literal's span:

  ```trb
  public type CollectionLiteral {
    case Default
    case InlineArray(item: TypeId, size: Int)
    case FromIterate(target: TypeId, item: TypeId, witness: Witness)
  }
  ```

  `Default` means "build the collection this literal's own type names, item by item" - the default implementation where
  the trait is what is expected (`ArrayList` for a list, `TrieMap` for a map), and that very type where a concrete
  implementation of the trait is (`const numbers: TrieList<Int> = [1, 2]`). **A concrete implementation is therefore
  `Default` and not `FromIterate`:** a literal knows its items, so building the type that is asked for directly *is*
  the fast path the concept asks the compiler to find, and going through `Target.from` would be a list built to be
  thrown away. (While this was written it was also the only answer resolution could give, because both
  `extend<Item> List<Item> with From<Iterate<Item>>` and the `with From<Iterate<Item>>` on `ArrayList` itself applied
  to `ArrayList<Int>`. Gap 53 settled that: only `ArrayList`'s own does, and `.to<ArrayList<Int>>()` works. The reason
  above is the one that stands.)
- **`Array<Item, Size>` is checked before anything else**, because it is the one target that is not built from a
  collection at all: the items go into the inline slots. The number of them is static, so it has to be `Size`
  (`` `Array<Int64, 4>` has 4 items, and this literal has 3 ``), and where `Size` is still an open variable the count
  solves it - which is what makes `fn total<const Size: Int>(values: Array<Int, Size>)` callable as `total([1, 2, 3])`.
  A `Size` that is a const *parameter* (`Array<Float, Columns>` inside a generic type) says nothing: what `Columns` is,
  the caller decides. A `...` contributes its own size where its operand is an `Array` of a known size, and is otherwise
  `` `List<Int64>` does not say how many items it has, so `...` cannot fill an `Array` ``.
- **Every other target goes through `From<Iterate<Item>>`**, the one collection protocol the language has, and `Item` is
  what the target itself iterates (`Iterate<Item>`), or - for a target that is not iterable at all - the
  `Iterate<Item>` its own `From` takes. So a literal accepts exactly the targets `.to<Target>()` accepts, trait types
  included: `Set<String>` and `Queue<Int>` are `From<Iterate<Item>>` through an `extend` whose target is the trait, and
  the `Witness` that is recorded is that implementation with its inner witnesses (`String: Hash`). For a target that is
  not iterable at all the `Item` comes from its own `From`, and **which** of several `From` implementations that is,
  `iterableSourceOf` answers - so `const text: String = ['a', 'b']` is `String.from(['a', 'b'])` and gap 50's blanket
  `From<Never>` does not get in the way (gap 53).
- **A type that is none of these is an error** instead of a silent acceptance:
  `` A list literal cannot become a `Point`: `Point` is not `From<Iterate<Item>>` ``, and for a map literal
  `` ... is not `From<Iterate<(Key, Value)>>` ``. A trait type the collection merely *coerces* to is deliberately not
  one of these: `print [1, 2]` fills a `...values: Show`, and `const numbers: Iterate<Int> = []` takes its item type
  from the `Iterate<Item>` of the expectation. Such a literal keeps the expected type as its own, as before - but the
  coercion is now held to, so `const ordered: Compare = [1, 2]` gets the existing
  `` `List<Int64>` does not implement `Compare` ``.
- **Map literals had the same hole and are fixed by the same three rules**, minus the `Array` one. **Set literals have
  none of it, because the syntax has no set literal yet:** `{a, b}` is in the concept and nothing in the parser builds a
  node for it, so there was nothing to verify. When it arrives it is `listLiteralType` with `Set` in place of `List`.
- **A literal is the only place the checker counts items.** There is no factory whose number of arguments becomes the
  `Size`, because no signature can say that: an Array is built from a literal, from `Array.filled(value)` or
  `Array.generated { index => ... }` (both take `Size` from the expected type by ordinary inference) or from
  `Array.from(items)`, which counts at run time and answers an `Option`. `Array [1, 2, 3, 4]` is not a spelling of a
  factory either: it already parses as an index expression, and a command's first argument may not start with `[`.
- **`WellKnown.array`** joins `list` and `map`: the literal rule asks for the symbol at every literal.
- **A `const` parameter that nothing solves gets its own note.** `Array.filled(0.0)` with no expected type is
  `` Cannot infer `Size` of `Array` `` with the note "`` `Size` `` is a `const` parameter and is never inferred from
  an argument: write it in the type that is expected of this value". The ordinary note names the closure parameter,
  the result and the written type argument, and none of the three applies to a value: `VariableState.isConst` carries
  `ParameterDeclaration.isConst` from the instantiation to `reportUnsolved`.

### What the follow-ups add (gaps 53 and 54)

- **The dead-change rule reaches the top level of a file** (gap 54). A top-level `var` is a declaration, not a local, so
  `BindingUse` became `ValueUse` with a `UseRoot` of `.Local(binding)` or `.Declared(symbol)`, `Checker.isTopLevel` says
  which body is the statement sequence of a file, and `Checker.readDeclarations` is every top-level `var` that a
  function, a closure or another top-level initializer reads. The message and its two notes are unchanged - the rule is
  the same rule - and `settleChanges` at the end of `checkModule` now has something to settle. It found six dead changes
  in `examples/tour`.
- **An `extend` whose target is a trait type implements the trait for the trait-typed value** (gap 53), and reaches a
  concrete implementer only through the coercion of the receiver: every member of the implemented trait has to take
  `self` and none may answer `Self`. `reachesImplementers` guards the one branch of `matchTarget` that let a
  trait-targeted implementation match any implementer, so `ArrayList<Int>` has exactly one candidate for
  `From<Iterate<Int>>` - its own - and `.to<ArrayList<Int>>()`, `.to<TrieSet<Int>>()` and a literal expected as one all
  resolve. `Show`/`Equals`/`Hash`/`Encode` still reach implementers, so `ArrayList<Int>.show()` is unchanged.
- **`iterableSourceOf`** is the question the collection protocol really has: not "what is the argument of `From` for this
  type" (which has no single answer - `From` is the one trait a type implements several times) but "which of its `From`
  implementations takes an `Iterate`". That is what brings `const text: String = ['a', 'b']` back.

### What the conformance round found (gaps 55 to 59)

Eight documentation writers tested every claim of CONCEPT.md against `torb check` while they wrote the reference pages,
and what they reported is a list of programs the checker **accepted**. That is the dangerous direction, so this round is
its own slice. Two of the findings were not findings at all, and they are worth as much as the rest:

- **Destructuring in a binding was never broken in a body.** `const (quotient, remainder) = divide(7, 2)` and
  `const Point(x, y) = p` check inside a function and always did; what bound nothing was the **top level** of a file
  (gap 57). Wave 1 read the second as the first.
- **`swap(a, a)` was reported** - for a local. The documentation writes the rule with a top-level `var`, and `overlaps`
  read every negative `rootKey` as "this path leads nowhere" while a `PlaceRoot.Declared` gets a negative number of its
  own. One comparison.
- **"A captured variable must implement `Encode`" does fire.** `assert(session.name == "a")` on a `shared type` reads
  "`session` is captured here and `Session` is not `Encode`"; the writer's probe missed the path.

What the round changed, by root cause rather than by finding:

- **Three questions were asked of a type that says nothing about itself, and two of those types do say something.**
  `isUndecided` (a mismatch), `isFullyKnown` (a missing member, a missing case, `?.`, `??`, an operator, a `for`
  subject) and the operator messages all asked "is this type decided enough to report about". A **trait type** is
  decided - the only thing that becomes one is a value that implements its bounds, and the coercion has been tried by
  then - so `wantsSquare(shape)`, `takesInt(numbers)` (a `List<Item>` is a trait) and `shape.perimeter()` are ordinary
  messages now. A **generic parameter** is decided too: what it has is its bounds, and those stand at its declaration.
  Turning both on removed five uses of 4.10's "The checker did not work out the type of this expression" - the message
  about a bug of the compiler - and cost nothing in the repository.
- **An operator asks for its trait like every other member.** 4.3 recorded the witness of `==`, `<` and `"{x}"` and
  said nothing where it found none, because the prelude had no `Equals`, `Hash` or `Show` for its collections;
  `std/collections` carries all three now, and the messages are on. That uncovered gap 55 (`Never`).
  **`??` joined them:** an operator is a trait exactly when it is a method call, and `a ?? b` *is* `a.orElse(b)`, so
  `std/core` declares `OrElse<Value>` and the checker resolves `??` through it instead of hard-coding `Option` and
  `Result`. `?.` and `?` stay what they are - the first would need `Self<Output>`, the second leaves the enclosing
  function, and no method can do either.
- **A rule that was only asked in one of two places.** A bound on a *function's* type parameter was checked at every
  call and the same bound on a *type's* was checked nowhere (`HashMap<Float, String>`); `noteArgumentClosures` was
  called for the arguments of a call and not for the block of a property command; a spread was checked against the
  parameter's item type without asking whether the parameter is variadic at all; an aliased case was looked up by the
  name that was *written* in both an expression and a pattern.
- **Four rules of CONCEPT that nothing enforced:** `await()` (gap 58), `?` outside an `Option`/`Result` (gap 59), one
  namespace of members, and a `static fn` reached through a value. Plus the three small ones: a literal type cannot
  be extended, a `fn` type parameter has no default, and an `Array` index that is written out is held against the size
  in its type.
- **Two things a closure did that it should not.** A closure whose body coerced into the expected result had its own
  result read back out and was then reported a second time through a function type, which has no variance; and an
  implicit parameter name that shadows a visible local was silently the local (gap 56, which reverses 4.4's note - the
  rule fires at the *use*, and it found five genuinely ambiguous reads in the sources).

What was deliberately left open, and why:

- **`EntityId (Int64)` in a message.** 4.10 already named it: the alias has to survive out of the type, which is a side
  table keyed by the span of the reference. Still true, still not in.
- **A `?` in a body whose result is *inferred*.** `fn probe() { read()? }` produces `Void` and is accepted, because the
  result is not settled when the `?` is checked. Closing it needs the question asked again after the body with the span
  kept.
- **Which closures a callee runs *as a task*.** Gap 58 enforces the half that is decidable and exempts every closure,
  because `Source.produce { sink => ... }` runs one as a task and its parameter type says nothing about it. A way for a
  signature to say so is milestone 7's.
- **`Decimal` and `nan`.** `std/number`'s doc comment says using `Decimal` should be "a compile error that names the
  milestone"; the checker is right and the doc comment is wrong - `Decimal` is a declared type with real signatures, and
  a front end that refuses a type the standard library declares would have to refuse `Task` and `Channel` with it. What
  is missing is a back end. And `Float`'s not-a-number value printing as `NaN` where CONCEPT writes `nan` is runtime
  text (`runtime/`), so it belongs to 5.10 and not here.

### What the naming rules add

Three decisions of the owner, and one slice, because all three are about the same thing: the first letter of a name
already decided something (a pattern binds or names a case), and everything else about a name was a convention nothing
read.

- **A name is ASCII** (`[A-Za-z_][A-Za-z0-9_]*`). Both lexers read a word as the whole run of Unicode word characters
  and report **one** diagnostic for it - "A name is written in ASCII letters, digits and `_`", with the note that text
  and comments may contain anything - and keep the token an `Identifier`, so nothing else is reported about the line.
  Reporting per character or dropping the token would both turn one mistake into a list. Strings, char literals,
  comments and doc comments stay full Unicode.
- **`startsUpperCase` is `A` to `Z`** in both parsers, where it used to be "the character changes when it is
  lowercased". For ASCII the two agree, so nothing about an existing file changes; what goes away is the question what
  the first letter of a name in another script means.
- **The spelling of a name** is 5.6's new pass, and **an unread binding of a refutable pattern** is 5.5's new rule.
- **What the repository had to be fixed for:** nothing. `check ..` over the whole workspace reports no problem of
  either rule, and `torb canon --check --rule unused-bindings ..` changes 0 of 293 files. What did need fixing were the
  **in-memory sources of the checker's own tests**, which are strings and out of the canon tool's reach: 14 test sources
  in `exhaustive`, `statements`, `lower-match` and `check` bound names their arms never read, or used `fn Point()` to
  show two declarations of one name.

### What the checker follow-up round adds

Seven rules the checker did not enforce, gathered from the rounds that found them. Three are `docs/CONCURRENCY.md`
section 12 (gaps 58, 67 and section 13's 1, 2, 3 and 17 above), two close gaps 53 and 68, and two are their own:

- **`private` reaches one FILE, not one package** (the owner's decision). `isInsideDeclaration` asks whether the
  declaration stands in the module that is being checked, and nothing else - so an `extend` of the same file sees the
  private member, a free function of that file sees it too (it could not before), and an `extend` in another file of
  the package no longer does. The three messages (`is private to`, `cannot be passed from here`,
  `this pattern cannot read it`) say "in the file that declares `X`" in their notes. One place in the repository
  wrote a `private(var)` field of another file, an `extend Column<Node>` of `examples/ecs-probe-2`, and it goes
  through the public members now.
- **A constant index that is out of range is a compile error and not a panic**, where how many items there are is
  known without running anything: an `Array<Item, Size>`, whose size is part of its type, and a collection literal
  that stands right there (`[1, 2, 3][5]`). A negative index is counted too - `numbers[-1]` is a `Unary(Negate, 1)`
  and not a literal, which is why it used to slip through. The note names `get(index)`, the form that answers an
  `Option`.
- **A command call after an index** parses. `mounted[index].process elapsed` is the command form of
  `mounted[index].process(elapsed)`, because an index in the middle of a member path is still a member path. The
  index joins the path only when a `.name` follows it, which leaves rule 3 of the command canon alone: `f [1]` is
  still always indexing, and `print [1, 2]` is still the error that says to write parentheses. The attempt is
  speculative, and a `Parser` is a value, so taking it back undoes the position and the diagnostics together.

---

## 9. Spec gaps

Where CONCEPT.md is silent, ambiguous or contradictory for a checker. Each with a proposal in the spirit of the
language: consistent, stable, simple, no annotations, explicit over clever.

**1. `Void`, `Never` and `Range` are never declared, and `Void` is used as a value.**
"Built-in Types": "`Void` is the type with exactly one value, `Never` the type of expressions that do not return."
Neither is in `std/prelude`, and `Range` is used in signatures (`operators.trb`, `string.trb`, `list.trb`) and by
`0..10` without existing anywhere. `Ok(Void)` (in `compiler/src/main.trb`, `cli/files.trb`,
`examples/tour/src/06-errors.trb`) uses `Void` as a _value_, which contradicts "A type never flows as a value".
_Proposal:_ the prelude declares `public native type Void`, `public native type Never` and
`public type Range<Value> { start: Value?, end: Value?, inclusive: Bool }` with
`extend Range<Int> with Iterate<Int>` (only when `start` is present) and `Slice` support; the unit value is written
`Void()`, an ordinary zero-field constructor call, and the five call sites are changed. _Reason:_ it needs no new rule
at all - "calls without arguments always need `()`" already covers it - and it keeps types out of expression
positions.

_Decision:_ accepted, with one change: **the one value of `Void` is the keyword literal `void`**, not `Void()` and
not `Void`. The values of a built-in type are lowercase literals, exactly as `true` and `false` are the values of
`Bool`, and an uppercase name is a type or a case - there is no exception. `Ok(void)`, `return void`,
`const nothing = void`; `Void` in an expression is an error that teaches the spelling ("`Void` is a type, not a value:
its one value is written `void`"). A function without a result type returns `Void`, and a block that ends in a
statement has the value `void`. `Void`, `Never` and `Range<Value>` are declared in the standard library as proposed
(`std/core/src/void.trb`, `range.trb`).

(An earlier decision made `Void` both the type and its value, as `Unit` is in Kotlin. It was the one name in the
language that was both, and that exception is what this replaces.)

**2. `using` passes a temporary to a `var` parameter.**
"`var` Paths": "A temporary is not a `var` path: `iterate().next()` is a compile error." But
`fn using<Resource: Close, Value>(var resource: Resource, ...)` is documented and used as
`using File.open(path)? { ... }` (`std/prelude/src/control.trb`, `std/fs/src/lib.trb`,
`examples/tour/src/08-control-flow.trb`).
_Proposal:_ a temporary is a valid _argument_ for a `var` parameter (the callee is its only owner, "copy in, copy out"
is exact), and stays invalid as the _base of a path_ (`iterate().next()`, `f().x = 1`). _Reason:_ the rule's purpose
is "a change that is thrown away is a mistake", which only applies when the change would have to be written back
somewhere.

_Decision:_ accepted.

**3. What does `if var Some(x) = e` bind?**
"Patterns also work in bindings and conditions": "`if var Some(iterator) = current` - `var` instead of `const`: the
binding is mutable". If that is a mutable _copy_, then `inner.next()` in
`std/prelude/src/stages.trb` (`FlatMappedIterator`) loses the advance and every `if var` is a dead change by
construction.
_Proposal:_ `if var P = place` binds `var` paths _into_ the place, exactly like a `var` parameter; the subject must be
a mutable place, and the body is inside a `var` access to it. _Reason:_ it is the only reading that makes `if var`
useful, it matches `var` parameters, and it fixes `FlatMappedIterator`.

_Decision:_ accepted.

**4. The receiver lookup order contradicts itself.**
"Configuration DSL": "local scope, then the _innermost_ receiver, then the surrounding `self`, then the module. **Only
the innermost receiver is implicit.**" The `project.trb` walkthrough relies on the second sentence
("`binary` is read before the block, because only the innermost receiver is implicit").
_Proposal:_ delete "then the surrounding `self`". Exactly one receiver is implicit, in a method as in a receiver
closure. _Reason:_ two implicit receivers are exactly the scope leaking that `@DslMarker` exists for, and the concept
already promises to avoid it.

_Decision:_ accepted.

**5. Blanket implementations are used but never described.**
`std/prelude/src/convert.trb` has `extend<Source, Target> Source with Into<Target> where Target: From<Source>`, and
`option.trb`/`result.trb` have `extend<Value, Target: From<Iterate<Value>>> Option<Target> with From<...>`. The
"Coherence" bullet only talks about `extend X with Trait`.
_Proposal:_ an implementation whose target is a bare generic parameter is a blanket implementation and is allowed when
the package owns the trait; two implementations of one trait may never overlap; disjointness is proved only by
different target heads or by bounds on the same subject that no type can satisfy together, so two blanket
implementations of one trait always overlap. _Reason:_ the prelude needs them, and a conservative overlap rule keeps
resolution decidable and error messages readable.

_Decision:_ accepted.

**6. Default type arguments (`trait Add<Other = Self, Output = Self>`) are not in the concept.**
The AST has `GenericParameter.default`, the prelude depends on it (`with Add` must mean `Add<Self, Self>`).
_Proposal:_ keep them for `type` and `trait` (not for `fn`), they may refer to earlier parameters and to `Self`, and a
defaulted argument is filled in, never inferred. _Reason:_ without them every `with Add` in the language would read
`with Add<Self, Self>`.

_Decision:_ accepted.

**7. Is a generic method callable on a trait-typed value? (the Open Question)**
"`Encode`/`Decode`: generic methods on a trait-typed value need dictionary passing … it is a requirement for the back
ends."
_Proposal:_ as in 4.4 - every trait-typed value carries a witness table per bound; a generic call passes one witness
per bound; the checker records the witness tree, so a back end monomorphizes when it contains no `.Object` and passes
tables otherwise. **Object safety is checked per call, not per type:** a member that mentions `Self` in a parameter or
result, or that has no `self`, cannot be called on a trait-typed value. _Reason:_ it keeps `List<Show & Hash>` and
`fn audit(entry: Show & Encode)` (both in `examples/tour/src/12-type-system.trb`) legal while rejecting only the
calls that have no meaning, and it needs no new syntax.

_Decision:_ accepted.

**8. Coercion to a trait type is nowhere stated.**
"Traits": "A trait can be used as a type" - but nothing says that `Square` is assignable to `Shape`, which
`const shapes: List<Shape> = [Square(2.0), Circle(1.0)]` and every `Iterate` parameter in the prelude require.
_Proposal:_ name the four coercions (value to trait value, trait value to fewer bounds/supertrait, `Never` to
anything, literal to literal type), state that they apply only where a type is expected and never solve an inference
variable, and state that there is **no variance** (`List<Square>` is not a `List<Shape>`). _Reason:_ it is the one
place where the language has subtyping, and leaving it implicit makes inference and error messages unpredictable.

_Decision:_ accepted.

**9. Conditional members of a trait.**
`iteration.trb` has `fn toSet(): Set<Item> where Item: Hash`, `collection.trb` has
`fn contains(value: Item): Bool where Item: Equals`, `map.trb` has `fn of(...) where Key: Hash`. The concept's
"Traits" section knows `where` only on declarations.
_Proposal:_ a member's `where` clause makes the member available only when it is satisfied; it is not a requirement
for implementors; an unsatisfied use reports the unmet bound. _Reason:_ the collection traits cannot be written
otherwise, and it is the same rule as for a conditional `extend`.

_Decision:_ accepted.

**10. `List + List` in the examples.**
"Collections": "Lists have no `+`: `Add.add` and `add(value)` would be the same member. Use `addedAll`." But
`examples/query-provider/src/query.trb:53,60` and `sql.trb:64` write `statement.parameters + fragment.parameters`.
_Proposal:_ fix the examples to `addedAll`. _Reason:_ the rule is right (one member namespace), the examples are
simply older than it.

_Decision:_ accepted.

**11. Bare cases in `std` and the examples.**
"A case is written with its type or with a leading dot. Never bare." But `std/prelude/src/compare.trb:29,33` write
`compare(other) == Less` and `examples/config-dsl/src/server-config.trb:29` writes `var level: LogLevel = Info`.
_Proposal:_ fix them to `.Less`, `.Greater`, `.Info`. _Reason:_ these are the first three findings of the rule and
cost nothing to fix.

_Decision:_ accepted.

**12. `?.` on a member that returns an `Option`.**
"`?.` and `??` have no nodes of their own. They are what they mean: calls of `Option.map`/`flatMap`." Which one, when?
`examples/game-engine/tests/world.test.trb:42` compares `first()?.position()` with `Some(Vector2.zero)`, so it must
flatten.
_Proposal:_ `?.` is `map`, and `flatMap` when the member's result is itself an `Option`; `?.` never produces a nested
`Option`, and it is not defined on `Result`. _Reason:_ a nested `Option` from `?.` has no use, and the rule is one
sentence.

_Decision:_ accepted.

**13. `??` on a `Result`.**
`examples/tour/src/12-type-system.trb:14` writes `Status.tryFrom(text) ?? "offline"`, which is a `Result`. The concept
lists `??` only under "One Vocabulary" for both, without saying that the operator covers both.
_Proposal:_ `a ?? b` is `orElse` on `Option` and on `Result`, `b` is `lazy` and is checked against the `Value`.
_Reason:_ both types have `orElse` already; anything else would be an arbitrary restriction.

_Decision:_ accepted.

**14. `by` and default members.**
"Distinct Types": "`with A, B by field` implements the listed traits by delegating to the field." It does not say
whether the trait's default members are delegated or come from the trait.
`examples/tour/src/03-types.trb:271` (`distance.max(Meters(10.0))`) needs an answer.
_Proposal:_ `by` forwards the _required_ members only; default members come from the trait, over the delegated ones.
_Reason:_ it needs no `Self` rewrapping for defaults, and a default that is written in terms of the required members
stays correct by construction.

_Decision:_ accepted. Refined later: `by` binds to one element of the `with` list, not to the whole list, and only
on a type with exactly one field named by `by` itself (CONCEPT, "Distinct Types", Decision Log). The forwarding rule
here is unchanged - it now just applies per trait instead of per type.

**15. Property commands with parentheses.**
"Property commands": "a _command_ on a field never calls it, it writes it", and "`=` works, too". But
`examples/tour/src/09-dsl.trb:99` writes `tls(port == 8443)` with parentheses, on a `Bool` field.
_Proposal:_ one rule - _a call of a non-callable member writes it_, with or without parentheses. _Reason:_ the
formatter's canon already demands parentheses when the first argument starts with `(`, so forbidding the parenthesized
form would make correct code unwritable.

_Decision:_ accepted.

**16. Labels in patterns are thrown away.**
The concept shows `Point(x: 0, y: 0)` as a pattern ("Cases, types (positional or labeled)"), but
`compiler/src/syntax/parser/patterns.trb:76` drops the label ("the label is documentation, fields are matched by
position"), so `Point(y: 0, x: 1)` silently matches `x = 0`.
_Proposal:_ keep the label in the tree (a `FieldPattern { label: Name?, pattern: Pattern }`) and check it. _Reason:_ a
label that is not checked is worse than none; patterns should mirror the constructor, where labels are checked.

_Decision:_ accepted, and "mirrors the constructor" is the whole rule: a sub-pattern without a label fills the next
field from the left, a labeled one names the field it matches, and the labeled ones follow the positional ones -
exactly what an argument list does. A pattern that does not name every field ends in `...` (CONCEPT, "Algebraic Data
Types and Pattern Matching").

**17. Named tuples and type identity.**
"Built-in Types": a label is "a name for `.0`, nothing more", and destructuring is positional. So is
`(lowest: Int, highest: Int)` the same type as `(Int, Int)`, and as `(highest: Int, lowest: Int)`?
_Proposal:_ labels are not part of type identity (the types are the same), but a label mismatch at the same position
is an error, and a labeled tuple may be used where an unlabeled one is expected and back. _Reason:_ it keeps "nothing
more" true for the layout while preventing a silent swap.

_Decision:_ accepted.

**18. Which discarded values are errors?**
"Bindings": "the discarded result of a method that only reads its receiver (`list.added(4)` as a statement)". That leaves
`Email.tryFrom(text)` and `1 + 2` as statements undecided, while `parser.bump()` and `cursor.next()` (both discarded in
`compiler/src/syntax/parser/parser.trb` and `std/prelude/src/stages.trb`) must stay legal.
_Proposal:_ an expression statement must have type `Void` or `Never`, unless the call has a `var` receiver or a `var`
argument. _Reason:_ one rule instead of a list, it catches the real mistakes, and everything a `var` makes effectful
stays writable.

_Decision:_ accepted.

**19. A captured `var` binding is a reference that escapes.**
"`var` Paths": "**References are second-class.** They only exist as a `var` parameter or a `var fn` receiver" and "A captured
`var` binding is shared between the closure and its scope". A closure that captures a `var` and is returned or stored
therefore _does_ escape, which the "Execution Model" confirms ("`var` bindings captured by closures are tracked by a
cycle collector") and which "References are second-class" denies.
_Proposal:_ say it explicitly: a captured `var` binding is not a reference but a shared box; it may escape, it is
reference counted, and it is the only sharing of a variable. The checker then treats every call that may run such a
closure as an access to the binding (exclusivity) and exempts the binding from the dead-change rule. _Reason:_ the
implementation already has to do this; only the sentence about second-class references needs the exception.

_Decision:_ accepted. **Superseded on 2026-09-22:** a closure that captures a `var` binding may not escape its scope,
the rule a `var` parameter already has. The escaping box let copies of a value share state, let a `spawn` race through
it, bypassed exclusivity (`const clear = { list = [] }` passed beside `list`) and formed cycles no collector will ever
reclaim; state that has to escape is a `shared type`. Decided, not yet enforced by the checker (CONCEPT, "`var` Paths
and `var` Parameters").

**20. Is a `var` binding to a `shared` object allowed to come from a `const` path?**
"Identity": "A `const` binding to a shared object is a read-only view". Nothing forbids
`var writable = view`, which would make every read-only view bypassable in one line.
_Proposal:_ for a `shared type`, a `var` binding, `var` field or `var` argument may not be initialized from a `const`
path to the same object; the object itself is copied only when it is not shared, which it always is. _Reason:_
without the rule, "no `var` path, no mutation" is vacuous for exactly the types where identity matters.

_Decision:_ accepted.

**21. `&&`, `||` and `!` are not traits.**
"Traits": "Operators are traits: `+` is `Add.add` …" - the list does not contain the logical operators, and it cannot:
they short-circuit.
_Proposal:_ state that `&&`, `||` and `!` are built in on `Bool`, short-circuit, and cannot be overloaded. _Reason:_
a trait method evaluates its argument, so a trait would change the semantics.

_Decision:_ accepted.

**22. `native type X with Trait {}` has no member bodies.**
`numeric.trb` and `string.trb` declare `public native type Int64 with Signed, Hash {}`, and a comment says the
runtime provides the members. The concept's "Foreign Functions" section only says that `native` declarations are
implemented by the runtime.
_Proposal:_ in a `native` type, a required trait member without a body is a requirement on the runtime; the checker
records it so a back end can report a missing intrinsic, and only `std` packages may do it. _Reason:_ otherwise every
primitive type is an error, and the requirement must not get lost silently.

_Decision:_ accepted.

**23. The generated `Show` format is not specified.**
`docs/ARCHITECTURE.md` makes the differential tests compare syntax trees "through the generated `Show`", so the
format is part of the contract between two implementations, but nothing writes it down.
_Proposal:_ fix it: `Type(field: value, ...)`, a case as `Case(field: value)` or as its bare name, `String` quoted
with escapes, `Char` in single quotes, `Float` always with a decimal point, `List` as `[a, b]`, `Map` as `["k": v]`,
`Set` as `{a, b}`, tuples as `(a, b)` with labels when there are any, `Option` as `Some(x)`/`None`. _Reason:_ two
implementations cannot agree on an unspecified format.

_Decision:_ accepted. Because `String.show()` has to stay the text itself (interpolation is `Show.show`), the
quoting is a second member: `Show.showNested`, defaulting to `show()` and overridden by `String` and `Char` alone.
Generated `Show` implementations and the collection ones call it for everything they nest. An empty `Map` is `[:]`,
as its literal.

Two points of the format were settled while milestone 5.10 implemented it, and
[docs/BACKEND.md](BACKEND.md) ("What 5.10 does differently") carries the reasons:
**a tuple shows as `(a, b)` without labels** - gap 17 took the labels out of type identity, so they are not in the
layout and a compiled back end cannot write them, which supersedes "with labels when there are any" above - and
**`Show` of `void` is `void`**, the literal that writes it, because "the values of the built-in types are lowercase
literals, never their type name" (CONCEPT, Decision Log). A nested `String` and `Char` are escaped with `\n`, `\r`,
`\t`, `\\`, the quote and `\u{h}` below `0x20` and at `0x7F`.

**24. `copy` has no written signature.**
"Values": "`copy` is generated for every `type`" - with which parameters?
_Proposal:_ `fn copy(<field>: <Type> = <the current value>, ...): Self`, all fields in declaration order, all
optional, private fields not passable from outside (as for the constructor), and no `copy` for a `shared type`.
_Reason:_ it is the only reading that matches `p.copy(y: 30)` and the visibility rules of the constructor.

_Decision:_ accepted.

**25. Is an unreachable `match` arm an error or a warning?**
"Algebraic Data Types": exhaustiveness is required, redundancy is not mentioned.
_Proposal:_ an error ("This arm is never reached"). _Reason:_ dead changes and discarded values are errors for the
same reason - with value semantics they are always a mistake.

_Decision:_ accepted.

**26. What is the scope of a field default?**
"Construction": "Fields with a default value can be omitted." It does not say what a default may read.
_Proposal:_ a field default is evaluated at every construction in a scope without `self` and without the other
fields. _Reason:_ field order must not be observable, and a default that depends on another field is what a factory
is for.

_Decision:_ accepted.

**27. What exactly is "compile-time evaluable"?**
"Modules and Packages": "top-level `const` initializers of modules must be compile-time evaluable".
_Proposal:_ literals, unary minus on a literal, tuple/list/map literals of such, constructor and case-constructor
calls whose arguments are such, string interpolation of such, and references to other compile-time constants. No
function calls, no `native` calls. _Reason:_ it is the largest set that needs no evaluator in the checker, and
`const origin = Point(0, 0)` and `const pi: Float64 = 3.14...` both fit.

_Decision:_ accepted, plus the arithmetic, comparison and logical operators of the built-in number types and of
`Bool`, and string interpolation of such - so `const frameTime = 1.0 / 60.0` is compile-time evaluable. Still no
function calls and no `native` calls (`+` on `String` is a call, interpolation is not).

**28. Which files may contain top-level code?**
"Top-level code is only allowed in entry files and scripts." `compiler/tests/*.test.trb` consists of nothing but
top-level `group`/`test` calls.
_Proposal:_ entry files (`src/main.trb`), scripts, receiver scripts and `tests/*.test.trb` may contain top-level
code, and top-level `?`/`await()`. _Reason:_ the test framework is ordinary functions, so its files have to be
scripts.

_Decision:_ accepted.

**29. How far does `private` reach?**
"Visibility": `private` is "invisible from outside", and "`extend` without a trait for a type of your own package is
simply a part of the type, in whatever file it is written". Do those extensions see private members?
_Proposal:_ yes - a private member is visible in the declaring type's body and in every `extend` of that type in the
same package, nowhere else. _Reason:_ "part of the type" has to mean something, and the package is already the unit
of coherence.

_Decision:_ accepted.

**30. Is `private(var)` valid anywhere but on a field?**
The AST allows it on every `Modifiers`.
_Proposal:_ only on a field; anywhere else it is an error. _Reason:_ "the `var` is private" has no meaning for a
member that has no `var`.

_Decision:_ accepted.

**31. Can a trait be used as a namespace for members that implementations provide?**
`std/prelude/src/collections/map.trb:84` calls `Map.from(...)`, which exists only through
`extend<Key: Hash, Value> Map<Key, Value> with From<...>`; `list.trb` does the same with `List.from`.
_Proposal:_ `Trait.member` resolves to the trait's own members first, then to members of implementations whose target
is the trait itself; two such implementations are an ambiguity error. _Reason:_ the prelude's factories
(`List.of`, `Map.of`, `Map.from`) depend on it and it needs no new syntax.

_Decision:_ accepted.

**32. Are two literal types with the same members really one type across files?**
"Literal Types": "Two literal types are the same type if they have the same members." That makes literal types
structural while everything else is nominal - including for `extend` (can two files both
`extend "tcp" | "udp" with ...`?) and for coherence (who owns the type?).
_Proposal:_ keep structural identity for assignability, and forbid `extend` on a literal type (only the generated
members exist). _Reason:_ a structural type has no owner, so the orphan rule cannot be stated for it.

_Decision:_ accepted.

**33. `Self` as a trait argument in a supertrait.**
`collection.trb` writes `trait Collection<Item> with Iterate<Item>, Length, Accumulator<Item, Self>`, while
"One Vocabulary" rules out "`Self<U>`, F-bounded tricks".
_Proposal:_ allow `Self` in any type position inside a trait, including as a trait argument; it is a type, not a type
constructor, so it costs nothing. _Reason:_ the collection traits need it and it is not higher-kinded.

_Decision:_ accepted.

**34. Does `Sandbox.load<T>` restrict the prelude?**
"Receiver Scripts": "The script can only reach what the receiver type exposes, plus the pure parts of the prelude."
"Pure" is not defined anywhere, and a purity analysis is a new concept.
_Proposal:_ a receiver script is checked as the body of `(var self: T) => Void` with the file scope "prelude plus
nothing"; what a script may reach beyond that is a **module allowlist** (`modules "std/text"`), not a purity
analysis - the prelude has no IO to begin with. _Reason:_ capabilities are already visible from imports, so the
existing mechanism is enough and no new attribute is needed.

_Decision:_ accepted.

**35. `Show` for the standard collections is missing.**
`print` is `fn print(...values: Show)` and `compiler/src/main.trb` prints a syntax tree and lists, but the prelude has
no `Show` implementation for `List`, `Map`, `Set`, `Option`, tuples or function types.
_Proposal:_ add `extend<Item: Show> List<Item> with Show` and the same for `Set`, `Map`, `Option`, `Result` and
tuples, in the format of gap 23; derived `Show` for a type then requires all fields to be `Show`, which holds.
_Reason:_ without them the first `print` of the compiler does not check.

_Decision:_ accepted. `List`, `Set`, `Map`, `Option` and `Result` carry the `extend`s in the files that declare
them. Tuples and function types cannot be written in TorbScript, so their `Show` is generated like a type's.

**36. Does `assert` force `Encode` onto everything it captures?**
"Quoted Expressions": "Captured variables must be `Encode`". `compiler/tests/parser.test.trb` writes
`assert(parsed.diagnostics.isEmpty())`, so the whole `Parsed` value must be `Encode`.
_Proposal:_ keep the requirement (it is what makes a failure explainable) and give the error a note that names
`assert` as the reason; a value that cannot be `Encode` is compared in a `match` instead. _Reason:_ relaxing it would
need an "either `Encode` or not" in a type, which the language does not have.

_Decision:_ accepted.

**37. Is `Expression<Value>` allowed as the type of a binding?**
"Quoted Expressions": "If a parameter (or binding) has the type `Expression<Value>`" - but nothing says what
`const e: Expression<Bool> = x > 1` means for `?`, statements or captures.
_Proposal:_ a binding behaves exactly like a parameter: the initializer is checked as a `Value` and quoted, with the
same restrictions. _Reason:_ one rule for both, and the tables already key a quotation by the span of the expression.

_Decision:_ accepted.

**38. Overflow of a const argument and of a literal.**
"Execution Model": "Integer overflow panics, in every back end." A literal that does not fit its adapted type is a
compile-time question that the concept does not mention.
_Proposal:_ a literal that does not fit the type it adapts to is an error at compile time ("`300` does not fit into
`Int8`"), and so is an `Array` index that is known to be out of bounds (the tour already promises the second one).
_Reason:_ a panic for something that is written in the source is a lint the compiler can do for free.

_Decision:_ accepted.

**39. Can `extend` add a `case` or a field?**
Nothing says no, and the AST allows `MemberKind.Case` inside an `ExtendDeclaration`.
_Proposal:_ an `extend` may add constants and functions only; a field or a case in an `extend` is an error ("fields
and cases belong to the declaration of the type"). _Reason:_ exhaustiveness and the generated constructor must be
decidable from the declaration alone.

_Decision:_ accepted.

**40. Does `for` over a `var` path iterate the original or a copy?**
"Collections": "`for x in xs` … iterates over a copy, so changing `xs` inside of the loop is safe" - which conflicts
with the exclusivity rule if the subject counted as an open access.
_Proposal:_ the subject is evaluated once into a temporary; it is not an open access, and the loop variable is a
`const` copy of each item. _Reason:_ it is what the concept promises, and it makes the exclusivity check simpler.

_Decision:_ accepted.

**41. Is `public const` allowed at top level?**
"Visibility": "Top-level declarations are private to their file unless marked `public`" - which of them can be
`public` is never said, and the parser does not accept `public const` yet.

_Decision:_ **`public const` at top level is legal** (a module exports a constant; its initializer is compile-time
evaluable, gap 27). **`public var` at top level is not** - a module has no mutable state. The parser has to follow.

**42. A command argument that starts with `.`**
A consequence of gap 11: `level Debug` becomes `level .Debug`, which lexes as the member path `level.Debug` -
whitespace never changes the meaning of a token sequence.

_Decision:_ `.` joins `(`, `[`, `-` and `!` in the list of tokens a first command argument must not start with.
Write `level(.Debug)` (which gap 15 makes a property command with parentheses) or `level = .Debug`.

**43. What satisfies a bound on a trait-typed value, and what `Void` implements.**
"Traits": "Every trait-typed value carries a witness table per bound" and "a trait value to fewer bounds or to a
supertrait" say what *coercion* does, and nothing says whether the same value may be the **witness** of a bound an
implementation asks for - which `extend<Value: Show, Failure: Show> Result<Value, Failure> with Show` needs for
`Result<Void, Error>`, where `Failure` is the trait `Error` and `trait Error with Show`. The second half is `Void`:
a signature says it as the type of the language, while `with Equals, Hash, Show` stands on the `native type Void` of
`std/core`, so nothing connected the two and `Void` implemented nothing at all.
_Proposal:_ a trait-typed value, `&` intersections included, satisfies a bound on any trait of the supertrait closure
of its own traits, and the witness recorded is `Witness.Object(annotation, bound)` - the value's own table, which the
back end reads as `WitnessRoot.Value(slot, bound)` plus the steps through `WitnessTable.nested` (BACKEND 1.4). And
trait resolution asks about the declaration of `Void` wherever the type `Void` is the subject. _Reason:_ both are the
same question - "which declaration says what this type implements" - and neither needs a new witness shape.

_Decision:_ accepted. The supertrait half was already implemented (`boundArgumentsOf` searches the closure and
`witnessOfBound` records the `Object` witness) but had no test; the `Void` half was the actual defect behind
"`Result<Void, Error>` does not implement `Show`" and is fixed in `resolveUncached` and `directTraitsOf`. Object
safety is untouched: what a trait value may *call* is decided per call (gap 7), and an operator is deliberately not
checked there - witnessing a bound is not a call.

**44. What a bare `return` contributes to an inferred result.**
Section 5.4 says a body without a declared result *is* the result, and 5.5 that a `loop` without a `break` never
ends - so a function whose whole body is such a loop infers `Never`. Nothing said what a `return` **without a value**
inside that loop contributes: `returnTypes` only ever collected the types of valued returns, so it stayed empty and
the `Never` of the loop won. `Parser.recoverToLineEnd` in the compiler is exactly that function, and the back end had
to refuse it ("a `return` in a function whose result was inferred as `Never`") because the body contradicted its own
signature.
_Proposal:_ a bare `return` contributes `Void`, like the end of a body that produces nothing. _Reason:_ a `return`
without a value says "this function is done and produces nothing", and that is `Void` whatever the block around it
does; `Never` is for what never gets there at all.

_Decision:_ accepted. Such a function infers `Void`, the back end finding stays where it is as a guard and is now
unreachable for this shape. What a valued `return` that disagrees with the last expression of the body does is
unchanged, and still says nothing: `inferredResult` unifies the two and drops the answer where they do not fit, for a
bare `return` exactly as for a valued one. That is its own gap, not this one.

**45. A `Bool` field that is named like a question.**
"Naming" knows "verbs change, participles return" and nothing about `Bool`s, while `std/core`'s `Range` had
`isInclusive: Bool` next to methods like `isEmpty()` - so at a use site the reader could not tell data from work
without looking for parentheses.
_Proposal:_ a `Bool` field, parameter or binding is an adjective or a participle (`inclusive`, `discarded`, `signed`);
a question that is *computed* is a method whose name starts with `is` or `has` (`isEmpty()`, `hasGuard()`).
_Reason:_ it is the same rule as "what could later be computed is a method from the start" read from the other side,
and it makes the shape of a name say which of the two it is.

_Decision:_ accepted. `Range.isInclusive` is `Range.inclusive` (CONCEPT, "Lexical Structure"; `compiler/CONTRIBUTING.md`).
The compiler's own `is...` fields are **not** renamed with it: about 60 of them are named after keywords (`isVar`,
`isStatic`, `isPublic`, `isNative`, `isShared`, `isConst`), and `var: Bool` is not a name - each one needs its own
decision, either another word or a type instead of a flag, which runs after the fixpoint with the method conversion.

**46. Which case does a bare uppercase pattern name mean when the matched type has one of the same name?**
Since an imported case may be written bare in a pattern (`None =>`, `Fail(problem) =>`), the name is resolved through
the scope. `checkPattern` only used that resolution to decide that the name *is* a case and then matched it **by name**
against the subject - so `Fail =>` against `compiler/src/ir/decision.trb`'s `DecisionNode`, which has a case `Fail` of
its own, type checked as `DecisionNode.Fail` while stage 0 and both back ends resolve the name to `Result.Fail` and no
arm ever matches. Dropping a dot broke 11 tests exactly this way.
_Proposal:_ a bare uppercase pattern name is resolved through the scope, and the case it names has to belong to the
matched type; anything else is written with its dot. Where the matched type has no case of that name at all, the
existing message ("`Node` has no case `Fail`") stays, because it names the real mistake.
_Reason:_ one resolution, used for one thing. A name resolves where it is written, and the subject decides nothing
about which declaration it means - which is the whole reason the dot exists for everything that is not imported.

_Decision:_ accepted. The message is `` `Fail` is the case `Result.Fail`, and the value matched is a `Node` `` with the
note "Write `.Fail` for the case of `Node`" (`pattern.trb`, tests in `compiler/tests/statements.test.trb`).

**47. Does the body of a *closure* that answers a `Task` produce the `Value`?**
"Concurrency" says a function that returns `Task<Value>` has a body that produces the `Value`, and `checkFunctionBody`
implements it. Nothing said it for a closure, and `checkClosureBody` checked the body against the `Task` itself - so
`const pull: () => Task<Result<Item?, Failure>> = { Ok None }` reported "Cannot infer `Failure` of `Ok`", and every
source of `std/stream` that is built from a closure was unwritable.
_Proposal:_ a closure whose expected result is a `Task<Value>` has a body that produces the `Value`, exactly as a
declaration does; what the closure *is* stays the function type that was asked for.
_Reason:_ a closure is a function value, and the rule is about functions. Two answers for the same question would mean
that moving a body from a `fn` into a closure changes what it has to produce.

_Decision:_ accepted (`closure.trb`: `taskValueOf` for the expectation, `taskOf` to wrap an inferred body back up).
Where the expected result is still an inference variable nothing changes, because `taskValueOf` only answers for a
`Task` that is already known. Where `await()` is allowed stays milestone 7's rule and is unaffected.

**48. Does the dead-change rule apply to a binding that holds a shared object?**
Rule 5.3 reports a change no read follows, on the grounds that "with value semantics a change nobody reads is always a
mistake". A `shared type` has no value semantics: `var end = channel.sink()` followed by `end.close()` and nothing else
was reported as a dead change, with a note that calls the binding a copy - while it is the one object everybody holds.
_Proposal:_ the rule says nothing about a binding whose type has an identity (a `shared type`, or a value of a
`shared trait` - the predicate of gap 20). Such bindings get no entry at all.
_Reason:_ the justification of the rule is the copy. Where there is no copy, the change is visible to every other
holder and "nobody reads it here" is not evidence of anything.

_Decision:_ accepted (`mutation.trb`, `isCounted`). A value that merely *contains* a shared object keeps the rule:
changing the value is still a change of a copy, and only the object inside it is shared.

**49. May a `var fn` method answer a `Task`?**
"Concurrency" and `docs/STREAMS.md` argued that it may not, because "an exclusive `var` access cannot stay open across an
`await`" - which is why `Source.next` first took `self` and every stateful source of `std/stream` had to hide its state
in the `var` bindings its closures captured. But the sentence is only true for a **value**: there a `var` is an exclusive
in-out access whose "copy in, copy out" ends with the call. For a `shared type` there is no copy, `var` is the permission
to change the one object, and CONCEPT's own `Connection` example already hands out two `var` paths to one object.
_Proposal:_ a `var fn` method of a `shared type` or a `shared trait` may answer a `Task`, and exclusivity says nothing
about a `var` access to a shared object across an `await`. The reverse is an error with a message of its own: a `var fn` receiver
or any `var` parameter whose type is a **value** on a function that answers a `Task`, because the copy back would happen
when the call returns - before the task has run - and the change would be lost. An ordinary `trait` counts as a value,
because a value may implement it.
_Reason:_ it is the same distinction the language already makes everywhere else between a value and an object, and it is
what lets `Source.next()` mirror `Iterator.next()` instead of hiding a cursor in a closure.

_Decision:_ accepted. The positive half needed no change - a `var fn` plus `Task` already checked, and so did two pulls
in a row, a `var` source handed to a function that awaits it, and a `var` field pulled across an `await`; tests in
`compiler/tests/program-shape.test.trb` hold it. The negative half was silent and is now
`` `take` changes `self` and answers a `Task`, and `Bag` is a value `` (and `` ... `Counter` is not a `shared trait` ``
for a trait), in `signature.trb`, so it covers a trait requirement and a `native` declaration as well as a body.

Two consequences for an API built on this. **Reading is a `var fn`, wrapping only reads:** a temporary is no `var`
path, so if `map`/`filter`/`through` were `var fn`s no pipeline could be written as one expression. Handing a source to
a wrapper is therefore a hand-over, and the wrapper pulls from then on. **A pipeline that is read gets a name**
(`var users = body.through(...).checked()` and then `users.toList()`), exactly as `var cursor = iterate()` does, and the
message for forgetting it already says so.

**50. `?` on a `Result<Value, Never>`.**
`Channel`'s reading end cannot fail, so `channel.source().next().await()` answers a `Result<Item?, Never>`. `?` converts
a failure through `From`, and nothing implemented `From<Never>`, so every such result had to be taken apart with a
pattern although the failure cannot exist.
_Proposal:_ a blanket `extend<Target> Target with From<Never>` in `std/core`, whose body is the argument - `Never`
coerces to anything, so there is nothing to write.
_Reason:_ the conversion is total and the implementation is forced; leaving it out only makes the infallible case the
awkward one.

_Decision:_ accepted. It does not overlap any other `From` implementation of the repository (checked: `std/core`,
`std/text`, `std/number`, `std/http`, `std/fs`, `std/json` and the examples all still resolve), because an overlap would
need a second implementation for `Never` as the source, and `Never` has no values to convert.
**51. What does a collection literal become, and which types may one adapt to?**
"Collections and Iteration" says `[1, 2]` is a `List` and `["a": 1]` a `Map`, and "literals adapt to the expected type"
is stated for numbers. Nothing said what an `Array<Item, Size>` or a `Set<String>` does with one - `listLiteralType`
handed *any* expected collection type back unverified, so `const wrong: Array<Int, 4> = [1, 2, 3]` and
`const origin: Point = [1, 2]` both checked without a problem, and no table said how the literal is built. A const parameter also cannot be written in terms of an argument count, so nothing could
link the number of arguments of a factory to its `Size`.
_Proposal:_ a collection literal adapts to exactly three things and nothing else - the collection its expected type
names, built directly; an `Array<Item, Size>`, filled inline, whose number of items has to be `Size` (an open `Size` is
solved by the count, a spread has to come from an `Array` too); and any `From<Iterate<Item>>` target, built from the
collection, which is the one collection protocol the language has. Anything else is an error that names what a target has
to be. What the literal became is recorded per span (`CollectionLiteral`), so a back end reads the decision instead of
making it again. A literal is the only place items are counted - `Array [1, 2, 3, 4]` is an index expression, and a
factory whose argument count became the `Size` would be a signature that lies, so the language has none.
_Reason:_ it needs no new trait - "no `Collectable`/`FromIterator`" stays - and it puts the one decision a back end
cannot make in the one place that already knows the expected type. The fast path for a literal is the compiler's job
precisely because the compiler knows the items; for a *value* it is a type pattern inside `from`, which is an ordinary
generic call.

_Decision:_ accepted. Three points had to be decided beyond the proposal:
- **A concrete implementation of the literal's own trait is `Default`, not `FromIterate`** (`ArrayList<Int>`,
  `TrieList<Int>`, `HashMap<K, V>`): the literal builds the type that is asked for directly. Going through
  `Target.from` would build a collection to throw it away, and resolution cannot even name the implementation - the
  standard library's trait-level `extend ... with From<Iterate<Item>>` and the inherent one on `ArrayList` both apply,
  which `resolveBound` reports as ambiguous. That overlap is gap 5's rule to settle, and `.to<ArrayList<Int>>()` has it
  too.
- **A trait type the collection only coerces to is not a target** (`Show` for `print [1, 2]`, `Iterate<Int>`,
  `Collection<Int>`): the literal stays the expected type as it did before, its item type comes from the
  `Iterate<Item>` of the expectation where there is one, and the coercion is verified so that
  `const ordered: Compare = [1, 2]` is reported with the existing "does not implement" message.
- **Set literals are not touched**, because the parser has no set literal: `{a, b}` is in the concept and in no
  `ExpressionKind`. The rule is written so that adding one is `listLiteralType` with `Set` in place of `List`.

**52. Is a temporary with an identity a `var` path, and where does gap 20 hold?**
Gap 2 settled that "a temporary is not a `var` path" (`iterate().next()`), with the reason that a change to something
that is thrown away is always a mistake. That reason is about a **value**: the change would be lost with the copy the
temporary is. An object with an identity has no copy, nothing is lost, and nobody else holds a view of something that was
just produced - so the rule was protecting nothing there, while costing every chain over a stream its one-expression
form (`file.lines().take(5).toList()` would have needed a `var` binding per step once the wrapping members took
`var self`). The other half of the same question is gap 20 ("a read-only view cannot be widened"), which was enforced in
one of its four places: a `var` binding of a named `shared type`. A `var` **field** filled by a generated constructor was
not checked - the constructor's parameters are values, so the `var` argument rule never saw them - and a trait-typed value
of a `shared trait` was not recognised as shared at all, because `place.trb` and `declaration.trb` both had a helper
called `isSharedType` and only one of them knew about `.Traits`.
_Proposal:_ a temporary whose type has an identity counts as a `var` path, for a `var fn` receiver and for a `var`
argument alike; a temporary value keeps the error it has. Gap 20 is then enforced in all four places - binding, field
(including through the generated constructor, and inside the declaring type as well), argument, and trait-typed value of
a `shared trait` - which needs the two helpers to become one.
_Reason:_ both halves say the same thing, which is that the promise is about a **path** and not about a type: what a
`const` withholds is what goes through that one binding, field or argument, and a value that was just made has no such
path behind it. With the pair in place the standard library needs no exception: everything that reads a stream, now or one
wrapper later, is a `var fn`, and nothing can be read through a `const` handle.

_Decision:_ accepted. `problemOfRoot` answers `Writable` for a temporary with an identity (`place.trb`), the single
`isSharedType` lives in `declaration.trb` and answers precisely (a `shared type`, a trait-typed value whose every trait is
a `shared trait`, a generic parameter with a shared bound - and `false` for a bare parameter, because the language has no
bound that says "shared"), and `requireSharedFields` walks a constructor's arguments against its fields after they have
been checked, since the paths it asks about are the ones checking them recorded. Tests in `compiler/tests/places.test.trb`.

**A result of a call is not a path.** `var writable = view()` is ordinary and stays legal, whatever `view()` answers: the
read-only view is a property of the binding, the field or the argument it goes through, **not of the type**. There are no
const types in this language, and adding one for shared objects would mean a second type for every shared type, an
inference rule for where it appears and a coercion between the two. Whoever hands an object out of a function hands out
the permission with it - that is what returning it means - and a function that wants to keep it to itself does not return
it.

The one thing that is left is that the language has no `move`: after `source.map(f)` the wrapper and the original both
have a `var` path to the same object. That is not a hole in gap 20 (both are `var`) but the absence of linearity, and
`docs/STREAMS.md` §2 says what the `Source` contract promises instead - one puller, consumed once. The narrower case of a
**value that contains a shared object** (`Option<Close>` in a `var` field) is not reached by gap 20 either, for the same
reason gap 20 does not look inside values; nothing in `std/` relies on it, and it is a follow-up rather than a decision.

**53. What does an `extend` whose target is a *trait type* implement?**
CONCEPT says `extend<Item> List<Item> with Show` "makes every list showable" and that
`extend<Item> List<Item> with From<Iterate<Item>>` "makes `List<Item>` itself a valid target of `to<List<Item>>()`" -
one sentence about every implementer, one about the trait-typed value, and nothing about which of the two it is. The
checker read it as "every implementer": `matchTarget` lets a trait-targeted implementation match any type that implements
the trait. So `ArrayList<Int>` had **two** candidates for `From<Iterate<Int>>` - its own and the extension's - and
`resolveBound` called it ambiguous, which made `.to<ArrayList<Int>>()` fail and a literal expected as an `ArrayList<Int>`
unable to name its own `from`.
_Proposal:_ such an `extend` implements the trait **for the trait-typed value**. Its members reach a concrete implementer
through the coercion of the receiver, which is what makes every list showable and what lets
`extend<Failure> Sink<Bytes, Failure> { fn addText ... }` reach a `File`. It is *not* a blanket implementation for every
implementer: a member without `self`, or one that answers `Self`, would answer "some `List`" where an `ArrayList` is
required, which is unsound. So a concrete implementer gets it only when every member of the implemented trait takes
`self` and none of them answers `Self`; a `Self` in a *parameter* is fine, because there the concrete value coerces into
the trait.
_Reason:_ it is object safety (gap 7) read from the implementation side, it needs no new syntax, and it is the only
reading under which both of CONCEPT's sentences are true at once.

_Decision:_ accepted; CONCEPT "Traits" says it now. `reachesImplementers` in `implementation.trb` answers it per trait
and keeps the answer, and it asks the **declaration** of the trait and not its signatures: bound resolution runs while
signatures are still being built, and one that is not ready yet would give a different answer than the same question a
moment later. `Show`, `Equals`, `Hash` and `Encode` reach implementers; `From` and `Decode` do not. Two notes:
- **The blanket `From<Never>` of gap 50 was a different defect and is fixed at the asking end.** `resolveTrait(String,
  From)` has no single answer by design - `From` is the one trait a type implements several times (`Int.from` for five
  widths, and `From<Never>` for *every* type) - so "the argument of `From` for a `String`" is rightly `Ambiguous`, and
  preferring one candidate over another would be arbitrary. The question the collection protocol actually has is "which
  of them takes an `Iterate`", and `iterableSourceOf` answers that one. `const text: String = ['a', 'b']` works again.
- **A concrete implementation of a collection trait stays `CollectionLiteral.Default`** (gap 51): now that resolution can
  name `ArrayList<Int>`'s own `from`, that is no longer the reason - the reason is the one that was always first, that a
  literal knows its items and building the type that is asked for directly *is* the fast path the concept asks the
  compiler to find.

**54. Does the dead-change rule reach the top level of a file?**
Rule 5.3 reports a change no read follows, and `checkModule` settles the statements of a file like any other body - but a
top-level `var` is a **declaration** and not a local, so `noteRead` and `noteChange` never saw one: `var counters = [...]`
/ `var first = counters[0]` / `first.increment()` at the top level of `src/main.trb` checked silently while the very same
lines inside a function were reported. Nothing says what the rule should be there either, because the statements of the
file are not the whole life of the declaration: a function of the module reads the same one, and nothing says where that
function is called.
_Proposal:_ a top-level `var` is counted, and a change of one is dead when **no statement after it reads it and nothing
else in the module does**. "Something else" is a function, a closure inside one, the initializer of a top-level `const` -
every body other than the statement sequence of the file. An exported one is never counted, because a module that has not
been checked yet may read it.
_Reason:_ it is the same rule with the same justification (the change is thrown away), and the extra condition is what
the difference between a local and a declaration actually is. Anything weaker leaves the most common shape of the copy
trap - a script - unchecked.

_Decision:_ accepted. `BindingUse` is `ValueUse` with a `UseRoot` of `.Local(binding)` or `.Declared(symbol)` (the two
roots of a `Place` the rule counts), `Checker.isTopLevel` says which body is the statement sequence of the file, and
`Checker.readDeclarations` collects every top-level `var` that something else reads. Three consequences:
- **A read in the initializer of another top-level binding counts as "something else",** because that initializer is a
  body of its own (`checkInitializer`) and is reached lazily through `resultOf`. So `var first = counters[0]` keeps
  `counters` alive whatever follows. That is conservative in the safe direction - it never reports a change that is read -
  and making it exact would mean checking a top-level initializer inside the statement body, where a read of it would be
  discarded with that body's own uses instead of ordered against the statements.
- **Order does not matter.** Every function of a module is walked before `settleChanges` runs at the end of
  `checkModule`, so a function declared *above* the `var` it reads keeps it alive exactly as one below it does.
- **`public var` needs no exception in practice**: gap 41 makes it illegal at top level, and the rule still says that an
  exported value is not counted, so the two cannot drift apart.

It found six real dead changes in `examples/tour` (`01-bindings-and-values.trb`, `03-types.trb` twice,
`07-collections.trb` twice, `12-type-system.trb`), all of them a value that is changed to illustrate something and then
never read - each is now followed by the `print` that shows the effect, which is what the example wanted to say anyway.
**55. What does `Never` implement?**
CONCEPT declares `Never` as "the type of expressions that do not return" and says it converts to every type; gap 1
declares it as `public native type Never {}`, with no `with` list. So a bound on `Never` held for nothing, and
`Source<Item, Never>` - the reading end of a channel, which the concept describes as the one that *cannot fail* - was
not showable, because the generated `Show` of a `Result<Value, Failure>` asks its `Failure` for one. The same held for
`Equals`, `Hash` and `Encode`, and for every user type parameterized by a failure type.
_Proposal:_ a bound on `Never` holds, whatever the trait is. There is no value of `Never`, so nothing can ever reach
the member: the requirement is vacuous, exactly as the coercion of `Never` into every type is.
_Reason:_ it is the same argument that makes `Never` coerce to everything, read from the other side, and it is the only
reading under which `Result<Value, Never>` - which gap 50 already blesses - is an ordinary type.

_Decision:_ accepted, and it is the **checker's** rule and not a `with` list in the standard library: writing
`native type Never with Show, Equals, Hash, Encode` would make the runtime owe four members that can never be called
(gap 22) and would still be a fixed list, while the rule is about every trait. `resolveUncached` answers `Bounded` for a
`NeverType` subject before the absorption check, which is what makes the witness a well-formed `Witness.Object` whose
table no branch of any generated member reaches. Asked **without** a bound (`resolveTrait(Never, Add)`, which is what
an operator asks) the answer stays `Missing`: which arguments `Add` would have for a `Never` is nothing the question
says, and `Never` absorbs the message anyway.

**56. When exactly does an implicit closure parameter "shadow a name that is visible at the closure"?**
CONCEPT ("Trailing Closures"): "The implicit parameter can be named by the *type of the function* … If such a name
would shadow a name that is visible at the closure, it is a compile error (no silent shadowing)." 4.4 left the rule out
and said why: read as "the expected function type names a parameter `value` and a `value` is visible", it rejects
`any { _ == value }` in `std/collections` - four more sites like it - where the closure never names `value` as its
parameter at all. Read that way the rule is about a *declaration* that was never written down.
_Proposal:_ the rule fires at the **use**. A bare name inside a closure that declared no parameters, which resolves
through the local scope chain while the same closure's expected function type names a parameter of that name, is the
error - because there the local silently wins and nothing in the source says which of the two is meant. `_`, `_2`, … can
collide with nothing. A closure that wrote its parameters down is an ordinary scope and may shadow, which CONCEPT says
two sections earlier ("a nested block and a closure are scopes of their own"). A name that is not a local is not
shadowed either: the implicit parameter comes *before* the file scope and the prelude in the lookup order (design 3.1),
so there the implicit one wins and it wins visibly.
_Reason:_ it is the reading under which the rule is about exactly the ambiguity it exists for, and it is the only one
that can be decided where the name stands - 4.4's objection was that reporting it needs a look at the body before it is
checked, and at the use there is no such need.

_Decision:_ accepted. `namesImplicitParameter` in `closure.trb` asks the frames, `resolveName` reports before it hands
the local back, and the message is the catalogue's minus the line of the shadowed name (a `Diagnostic` carries notes,
not a second span). It found **five** ambiguous reads in the sources, and every one of them really was ambiguous -
`any { _ == value }` in `Collection.contains`, `(0..count).map { value }` in `List.filled`,
`indexed().find { _.item == value }` in `List.indexOf`, `box.update { _.added value }` in `Queueing.add` and
`indicators.any({ value.startsWith _ })` in the documentation's own index writer. All five name the parameter now.

**57. Does a binding that destructures work at the top level of a file?**
CONCEPT's own examples of patterns in bindings are `const (quotient, remainder) = divide(7, 2)` and
`const Point(x, y) = p`, and nothing says where they may stand. Inside a body they always worked. At the top level of a
file they bound **nothing**: milestone 3 declares a name for a plain `.Binding` pattern and for nothing else, and
`checkTopLevelBinding` asked for the type of that one name - so the pattern was never checked, no name existed, and the
only symptom was "Cannot find `quotient`" wherever it was read. Stage 0 binds them and resolves them from a function of
the module, so the runtime and the checker disagreed. Two things make the top level different from a body, though: a
top-level `const` of a **module** is a compile-time constant that milestone 5 *folds*, and a `public` one is a name of
the module's interface.
_Proposal:_ a destructuring binding at the top level of a file declares one symbol per name it binds, exactly as a plain
one declares one - so a function of the module reads it like any other declaration. It is allowed where the top level is
ordinary code (an entry file, a script, a `tests/*.test.trb` file) and refused for a **module's** `const` and for an
**exported** one, where one projection out of a pattern is neither foldable nor a name anybody can import.
_Reason:_ it is the reading that makes CONCEPT's two examples work wherever a reader would write them, it is what stage 0
already does, and the one restriction is the difference between a statement and a declaration that has to be folded or
exported - not a rule about patterns.

_Decision:_ accepted. `boundNamesOf` in `symbols.trb` is the walk (milestone 3 declares the names),
`DeclarationSiteKind.Destructured(binding)` is the site every one of them shares, and `checkDestructuredBinding` checks
the initializer and the pattern **once** - in a scope that is thrown away again, so the names stay declarations and do
not also become locals of the statement sequence. Each name then reads its own type out of `patternTypes` at its own
span, which is exactly what `checkPattern` recorded there. `requireSingleName` is the restriction, with one message per
reason ("A module's `const` binds one name" / "An exported `const` binds one name"). A `var` pattern is one `var` for
all of its names, which is what `isDeclaredVar` reads off the binding.

**58. Where is `await()` allowed, when a closure may be the body of a task?**
CONCEPT ("Concurrency") is exact about it: "`await()` is allowed in functions that return a `Task`, in closures passed to
`spawn`, and at the top level of entry files and scripts. Everywhere else it is a compile error: a function that waits
says so in its return type." Nothing enforced it, so `fn sum(a: Int, b: Int): Int { spawn { a }.await() }` checked. The
list has a hole, though: `Source.produce { sink => ... }` runs its closure as a task exactly as `spawn` does, and its
parameter is an ordinary `(var sink: Sink<Item, Failure>) => Result<Void, Failure>` - `std/stream`'s own doc comment
awaits inside one and `examples/tour/src/13-streams.trb` does too. There is no way to write down "this closure is run as
a task", and a closure has no other way to know.
_Proposal:_ enforce the half that is decidable. A body may wait when its declared result is a `Task<...>` or when it is
the statement sequence of an entry file or a script; every **closure** may, because nothing in the language says which
closures a callee runs as a task. A field default, a parameter default and a top-level `const` of a module may not - the
first two are evaluated wherever the value is built, the third is folded at compile time.
_Reason:_ the case the rule exists for is a *function* that waits without saying so, and that is exactly the half this
covers. Turning a closure into an error would reject `Source.produce`, which is the API the concept itself describes, and
narrowing the exemption to `spawn` by name would reject it too. The missing piece is a way for a signature to say
"this closure is a task", and that is milestone 7's to design, not a rule to guess at here.

_Decision:_ accepted. `Checker.awaitsTasks` carries the answer through `BodyState`, `checkFunctionBody` sets it from
`taskValueOf(result)`, `checkModule` and `checkReceiverScript` set it for a file's statement sequence, `checkInitializer`
takes it as a parameter (a top-level `const` of an entry file or a script gets it, a default does not), and
`checkClosureBody` sets it to `true`. `requireAwaitAllowed` in `call.trb` recognizes the call by the member's name plus a
receiver that is a `Task`, which is the one thing that makes it this call. The rule found nothing in the repository.

**The exemption for closures is gone**, and with it the missing piece this gap named. A closure answers the placement
question for itself: the one `spawn` is handed becomes a task (the call says so, not the closure's type), and so does
one whose **declared result** is a `Task<...>` - which is the way a signature says "this closure is run as a task".
`Source.produce`'s `body` parameter is a `(var sink: Sink<Item, Failure>) => Task<Result<Void, Failure>>` now, and its
own body still produces the `Result` directly, exactly as the body of a `fn` that answers a `Task` does. Every other
closure is `` `await()` is only allowed in a closure that becomes a task ``, with `Task.all` and `task.map` in the
note: `tasks.map({ _.await() })` would block the worker that pulls the pipeline, which is the case the rule exists
for. One place in the repository had to change, the `Source.produce` of `examples/tour/src/13-streams.trb`, and it did
not: only the signature it fills did.

**59. Is `?` legal in a function whose result is neither an `Option` nor a `Result`?**
CONCEPT says it is not, and 4.3 said nothing about it on purpose: `fetchUser(id).await()?` stands in a function that
returns a `Task<Result<...>>`, and where `await()` is allowed was milestone 7's. So a `?` in a function that returns a
`List<Int>` checked, and the first thing to object was `ir/lower/match.trb` with "not supported by the back end yet"
(milestone 5.10) - a message the docs gate never reaches.
_Proposal:_ report it, with the `Task` as the one exemption: a result that is a `Task<Option<...>>` or a
`Task<Result<...>>` stays silent, because there the `?` is about the value behind the task. The top level of an entry
file keeps its own rule (the program ends with the error) and so does a body whose result is inferred, where nothing is
decided yet.
_Reason:_ `?` hands the failure back to the caller, so there has to be somewhere for it to go. That is not a rule about
concurrency, and the `Task` shape is the only reason it was ever silent.

_Decision:_ accepted (`requireTryResult` in `expression.trb`). One consequence for the back end: the branch of
`ir/lower/match.trb` that reports a `?` in a body that returns neither is now reachable only from **the top level of an
entry file**, where the program really does end with the error - inside a function the checker has already said so.
What stays open is a body whose result is *inferred*: `fn probe() { read()? }` produces `Void` and is accepted, because
the result is not settled when the `?` is checked. Closing it needs the question asked again after the body, with the
span kept, and it is a message and not a hole in the types.

**60. May a package convert its own type into a foreign one?**
Rule 2 of coherence asked the package to own the target or the trait, so `extend Float64 with From<Celsius>` was
refused - neither `Float64` nor `From` is yours - and `extend Celsius with Into<Float64>` overlapped the blanket over
`From`. A package could therefore not convert its own type into a foreign one at all, which is the most ordinary thing
a conversion does.
_Proposal:_ count a type named as an **argument** of the trait as owning the implementation. Rust's RFC 2451 is the
model, in its stricter reading: only the top level of an argument counts, and only a named type, so `From<List<Celsius>>`
is not yours.
_Reason:_ the implementation still has exactly one possible author. The owner of `Celsius` reaches it through the
argument, the owners of `Float64` and of `From` through the two existing ways, and a fourth package owns none of the
three - and the overlap rule still rejects two implementations that could unify.

_Decision:_ accepted (`ownsTraitArgument` in `implementation.trb`). A **blanket** target keeps the old rule: it covers
every type, so no owned argument narrows it to a line only one package could write, which is Rust's reason as well. The
message of rule 2 is unchanged, and its note gains ", or to the package of a type named as an argument of the trait"
where the trait has arguments - a trait without them has no third way in and says nothing about one.

**61. What does a hand-written implementation of a trait that has a blanket implementation say?**
`extend Celsius with Into<Float64>` got the generic overlap message, which names a file of `std/` and a rule about
disjointness. Neither tells the reader the one thing they need: `Into` is not implemented by hand, `From` is.
_Proposal:_ where one of two overlapping implementations covers every type and the other does not, report at the
hand-written one and build the advice from the blanket's own `where` clause with the concrete types substituted.
_Reason:_ the blanket already says what to implement instead - `Target: From<Source>` *is* the line to write - so the
message can be derived rather than special-cased per trait.

_Decision:_ accepted (`reportBlanketOverlap` and `blanketAdviceOf`). `` `Into` comes from `From` for every type `` with
the note "Write `extend Float64 with From<Celsius>`: the blanket implementation in `std/core` makes `Celsius.into()` out
of that". The advice is only given where the `where` clause is **one bound on one trait**, which every blanket of the
standard library is; anything else keeps the plain overlap message, because a clause of several bounds is a sentence and
not a line to write. The member in the note is the blanket's own, where it provides exactly one.

**62. How does `tryInto()` find its target?**
`into()` is a well-known call whose target is the expected type (`conversionType`). `TryInto<Target, Failure>` joins it,
and it needs two types rather than one: `TryFrom` carries the failure as well.
_Proposal:_ read both out of the `Result` that is expected of the call. `Target` and `Failure` are the two arguments of
an expected `Result<Target, Failure>`; the receiver is the `Source`; and the conversion is the `TryFrom` implementation
of the target, exactly as `into()` uses the `From` of its target.
_Reason:_ it is the same rule as `into()` and needs no new inference, only a second reading of the expected type.

_Decision:_ accepted (`fallibleConversionType` in `expression.trb`, `WellKnown.tryFrom`). A `?` on the call says the
target alone; entry 64 is where the failure comes from there. A call with nothing above it that says a type gets "The
target of `tryInto()` is not known here", with the note that names `Target.tryFrom(value)`.

No blanket makes every `From` a `TryFrom` with `Never` as the failure: it would overlap every hand-written `TryFrom`,
because disjointness is proved by heads alone.

**63. Is `value.into()` always the `From` of the target?**
`isConversionCall` caught every argument-less call of a member named `into` that had an expected type, and asked
nothing about the receiver. `Into<Target>` is also a *type*, though, and a value of it has `into` as the trait's one
required member - so `fn open(path: Into<Path>) { const target: Path = path.into() }` was told
`` `Into<Path>` does not convert into `Path` `` about the one call the parameter exists for.
_Proposal:_ the conversion is what happens where the receiver's own type has no such member. Look the member up on the
receiver once its type is known; where it is there, the call is an ordinary one and goes through `checkCall`.
_Reason:_ it is what the special case always meant. A concrete receiver has no `into`, because the blanket over `From`
cannot be resolved without knowing the target - which is exactly why the expected type has to decide. A receiver that
already has the member needs no rule of the language at all.

_Decision:_ accepted (`hasMemberNamed` in `expression.trb`, for `into` and for `tryInto` alike). The receiver is
inferred by `conversionType` and then again by `resolveTarget` inside `checkCall`; that is idempotent for a type and a
resolution, and a diagnostic that would arrive twice is dropped by `reportHere`, which keeps one message per span **and**
text. **Two neighbouring holes stay open and are not this entry:** `path.into()` under `<Source: Into<Path>>` type
checks and reaches neither back end, and a `where` clause whose *subject* is a concrete type is not honoured -
`fn open<Source>(path: Source): Path where Path: From<Source> { Path.from path }` answers
`` `Path` has no member `from` ``.

**64. What does a `?` tell the expression under it?**
`tryType` infers its operand with no expectation, so `const port: Int = text.tryInto()?` leaves `tryInto()` without a
target although the binding names one. It is *the* way to read text once `TryFrom<String, Failure>` is the only one, so
the call has to work.
_Proposal:_ a `?` passes what is expected of **itself** down as `Expectation.Unwrapped(annotation)` - a fact and not an
expected type. Nothing is checked against it, no literal adapts to it, and the one reader is `fallibleConversionType`:
the target is that annotation, and the failure is the one of the target's `TryFrom<Source, Failure>` implementations
that takes the receiver's type.
_Reason:_ the operand of a `?` is an `Option` or a `Result` *of* what is expected, and which of the two only the operand
says - so an ordinary expected type would report a mismatch at every `?` that stands over an `Option`. A separate case
says exactly as much as is true, and therefore cannot change an inference that works without it.

_Decision:_ accepted (`Expectation.Unwrapped`, `conversionPartsOf` and `soleConversionFailure` in `expression.trb`,
`fallibleFailuresOf` in `implementation.trb`). The failure is looked up the way `iterableSourceOf` looks up the `From`
that takes an `Iterate`: `TryFrom` is a trait a type implements several times, so "the arguments of `TryFrom` for this
type" is `Ambiguous` while "the failure for *this* source" has one answer wherever the list has one entry. None says
`` `Int64` does not convert into `Port` `` with the `extend` to write; several say `` `String` converts into `Port` in
more than one way `` and name them, because only a written-out `Result<Target, Failure>` tells them apart.

**65. Which type does a range literal have?**
One `Range<Value>` with two `Option` ends makes "this range has a start" a fact about a field, so `for index in ..10`
and `(0..).length()` could only be found at run time - and a panic in std is for a caller's mistake that no type can
express.
_Proposal:_ one type per pair of ends, chosen by the syntax: `a..b`/`a..=b` is a `Range<Value>`, `a..` a
`RangeFrom<Value>`, `..b`/`..=b` a `RangeTo<Value>`. `rangeLiteralType` picks by which ends were written, and the
implementations follow: `Range<Int>` is `Iterate` and `Length`, `RangeFrom<Int>` is `Iterate` alone, `RangeTo<Int>`
is neither. What accepts every form takes the trait `Bounds<Value>`.
_Reason:_ both mistakes become the ordinary messages the checker already has - "`RangeTo<Int64>` is not `Iterate`"
and "`RangeFrom<Int64>` has no member `length`" - each with a note that names the missing end, and no `expect` is
left in `std/core/src/range.trb`.

_Decision:_ accepted (`rangeLiteralType` and `isRange` in `expression.trb`, `WellKnown.rangeFrom`/`rangeTo`/`bounds`).
`a[from..to]` coerces the range to `Bounds<Int>` at the brackets (`coerceToBounds`), because that is what
`Slice.slice` declares - so a range of anything but an `Int` is reported there and the witness the back ends call
`lowest`/`highest` through is recorded on the index expression. `inclusive` stays a `Bool` field of `Range` and
`RangeTo`: the type answers "which ends", not "which ends and whether the last one counts", which would be six types.

**66. May a trait-typed value stand where a type parameter is expected?**
`fn bare<Value>(shape: Area): Value { shape }` checked clean: `checkValue` gives up on a pair it cannot unify when
either side is "undecided", and a generic parameter was on that list. Stage 0 printed the `Square`, and the IR verifier
answered *"the function returns `Record(Square)` and `return` carries `Object(Area)`"*. A named type rejected the same
line.
_Proposal:_ a trait type handed to a type parameter is a mismatch, checked before the undecided pair is let through and
reported as the ordinary `` Expected `Value`, found `Area` `` with a note that says why. The check walks both types in
parallel (`traitForParameter` in `expression.trb`), so `Box<Area>` for a `Box<Value>` is the same message one level
down.
_Reason:_ the language has four coercions and every one of them runs **towards** a trait type. Inside the body that
declares it a type parameter is opaque: it stands for the one type the call site chose, and a trait-typed value is any
type that implements the trait, so the one is never the other. Where the call site *did* instantiate the parameter with
that trait type, substitution has put the trait type in both places long before this is asked and `unify` answers.

_Decision:_ accepted. "Undecided" keeps its meaning for everything else - a parameter still says nothing about a
literal (`Pair 0` inside `fn oneOf<Item: Numeric>`), about a `const` parameter used as a value (`0..Rows`) and about a
`Task` the checker has not settled - because only the *pair* "a trait type here, a parameter there" is decided, and only
that pair is reported. Nothing in the repository depended on the hole (`check ..` stays at 319 files, no problems).

**67. What may a closure handed to `spawn` capture?**
CONCEPT ("Concurrency", and again under the mutation rules) says it plainly: *"Closures passed to `spawn` cannot
capture `var` bindings."* The checker accepted one, and accepted two `spawn`s reading the same variable, which is the
data race the design says cannot happen - a captured `var` binding is not a copy but a **shared box**, the one value of
the language that two scopes reach.
_Proposal:_ `requireTaskCaptures` in `call.trb` runs after the arguments of a call are checked, on a call whose target
is the `spawn` of the prelude (`WellKnown.spawn`). For every closure argument it reads the captures the closure itself
recorded and reports each `var` one, naming the variable and pointing at the closure that would take it.
_Reason:_ the capture is what is wrong, not the second one: one task that shares a variable with the scope that spawned
it is already a race with that scope. So there is no counting and no "moved into one task" bookkeeping - two `spawn`s
give two messages, one at each closure, which is also the pair of sites a reader has to see.

_Decision:_ accepted. It keys on the **symbol** and not on the name, so a local `fn spawn` of a program is not this
rule; a library that spawns a closure on your behalf (`Source.produce`) is not covered, which is the same boundary gap
58 already draws for `await()`. Nothing in the repository depended on the hole: `std/stream`s one `spawn` captures two
`const`s.

**Two more things a task may not take with it** were added with the rest of `docs/CONCURRENCY.md` section 12. A
**top-level `var`** the closure reads is refused like a captured one: it is no capture at all - a declaration is one
place the whole file shares and nothing copies it - so the closure records the declarations its body read next to its
captures, and the rule asks both lists. And a capture whose type **contains a `shared type`** is refused, because a
task gets copies and an object is not copied: `containsShared` in `declaration.trb` computes the same fixpoint the
layout does (BACKEND 1.3), walking generic arguments, fields and case fields, with `Task` and `Channel` as the two
exceptions. The same predicate answers for a `Channel<Item>`, asked where that type is **written** - a signature, an
annotation, a construction - rather than at `sink().add`, so it is one message per item type instead of one per call.

**68. What decides which implementation an overloaded call means?**
The language keeps one overload form - a trait with a **parameter**, implemented more than once on one type - and the
checker resolved only the case where the trait argument was the type of the **first** parameter. `game.attachAt true,
"eight"` took the first-declared implementation, `const named: String? = game.valueOf()` took it too, a bound with two
instantiations (`World: Store<Position> & Store<Velocity>`) type checked and printed the same value twice, and the
operator path reported `` `Board` does not implement `Multiply` `` where the named call `board.multiply(Scale(2))`
resolved.
_Proposal:_ **one mechanism, asked of everything the call says.** `chooseOverload` infers every written argument once
(a closure and a spread say nothing - a closure is read *from* the parameter it fills), keeps the candidates each
argument fits, and where more than one is left narrows by the type the call is expected to produce. Exactly one is the
answer; several are an ambiguity that lists them; none is the message that lists what exists. The operator path asks
the same two questions of `appliedTraitsOf(receiver, Multiply)` - the list `traitArgumentsOf` collapses into
`Ambiguous` - and hands the chosen **bound** to `recordTraitCall`, so the member and the dispatch both come from it.
_Reason:_ the first parameter was never the rule, only the case the old code could see. "Which implementation" and
"which argument" are one question, and the expected type is the half of it that a call with no arguments at all
consists of.

_Decision:_ accepted (`chooseOverload`, `fitsArguments`, `narrowByResult` in `call.trb`; `fitsType` and
`appliedTraitsOf` in `implementation.trb`; `operatorBound` in `expression.trb`; `boundMember`/`dispatchOfBound` in
`member.trb`). Two details the repository decided:

- **An exact fit beats one that cannot be ruled out.** `fitsType` is generous where it cannot see - a generic
  parameter, a variable, anything unsettled fits - so a blanket candidate fits every call. Where some candidate matches
  the written types by `sameType`, only those are considered: without it `toList().encode(encoder)` in `std/encoding`
  became an ambiguity between the `Encoder` of `Encode` and the `Target` of a blanket.
- **The ambiguity is reported and the first candidate is still used.** One root cause, one message, and everything
  after it is checked against a signature rather than against nothing.

**Not done, and exactly where it stands:** a **user** type that gets `TryFrom<String, _>` from a trait it comes `with`
and writes another `TryFrom` of its own still reaches the wrong implementation (docs/BACKEND.md, "One conversion, one
implementation"). The member the type writes is found by rule 2 of the lookup, so the overload set of rule 5 is never
built, and merging the two lists means asking `traitsOf` **during** member lookup. That answer is a closed-world one
and it is memoized: asked before the run has derived the implementations it generates on first use, it caches a closure
without them, and the witness tables of an unrelated program come out different - measured, the `Equals` instance of
`std/core`s `Ordering` disappears from the lowering of `syntax/source.trb` (118 functions become 117). Reading the
implementation index instead of `traitsOf` avoids that and runs into the second half: the signature of a trait may be
built while this very member lookup is running, and `typeSignatureOf` panics.

**Done, and it took both halves.** The caches are invalidated: `deriveFor` bumps an `answerGeneration` and throws away
`traitClosures`, `memberAnswers` and `memberMissing`, and an answer that was *being* worked out while one was added is
given and not kept. `traitsOf`'s recursion guard moved into a table of its own (`closuresRunning`), so throwing the
answers away cannot remove it. The symptom that measured it: `"{point}"` derives `Show` for `Point`, and the
`point.show()` after it used to be `` `Point` has no member `show` ``.

On top of that the overload set is merged at the lookup, for a **`static`** member only: `withTraitOverloads` in
`member.trb` adds every candidate the traits of the type provide to the one its body writes, dropping a duplicate by
declaration or by signature. That is what `String.from(path)` needed - `String` writes `static fn
from(value: Iterate<Char>)` in its own body for `with From<Iterate<Char>>`, and `std/path` adds
`extend String with From<Path>`. The order of the lookup is not undone by it: the member the body writes carries
`Candidate.preferred`, and where nothing in the call tells the candidates apart it is the answer and **no ambiguity is
reported**, because rule 2 comes before rule 5. An instance member is untouched - it has no such form, and `a.b` is
the hottest question of the pass.

What is still open is one step away from it: a member lookup does not *start* a derivation, so `point.show()` in a
program where nothing else asked for `Show` is still `` `Point` has no member `show` ``. The derivation happens where
a trait is resolved, and the lookup walks the traits a type already has.

**69. Does member lookup see a blanket implementation, and is there an inherent one?**
`extend<World: Query> World with Pairs { fn doubled(): Int { ... } }` and then `game.doubled()` answered `` `Game`
has no member `doubled` ``, although `resolveTrait(Game, Pairs)` finds that very implementation: `traitsOf` - the list
member lookup walks - skipped every implementation whose target is a bare parameter, because "a blanket target binds
nothing from the type alone". The inherent form `extend<World: Query> World { ... }` checked clean and answered
"cannot extend `World`: it is not a type" when it ran.
_Proposal:_ `addBlanketTrait` adds a blanket to the list of traits a type has when the target matches and the
implementation`s own bounds hold, which is the whole condition a blanket has. And the inherent form is rejected: CONCEPT
defines a blanket as an implementation *whose target is a bare type parameter* and allows one "when the package owns the
**trait**".
_Reason:_ without a trait there is nothing the coherence rule could license, nothing that keeps the implementation
unique, and nothing a using file could name - an extension member of a foreign type is imported by the **head** of its
target, and a bare parameter has no head.

_Decision:_ accepted, with two limits that the repository measured:

- **Only for a concrete receiver.** What a generic parameter has is what its bounds say. Adding blankets to a
  parameter gives `Target` of `fn to<Target: From<Iterate<Item>>>` a second `From` - the `From<Never>` every type has
  - and `Target.from()` becomes an ambiguity no call site can resolve.
- **Only where the type decides the trait`s own arguments.** `extend<Source, Target> Source with Into<Target>` would
  put `Into<Target>` with an open `Target` on every type at all, and what that means is decided at a call.

`Never` also stopped absorbing in `fitsType`: it coerces to everything and nothing coerces to it, so the `From<Never>`
every type now has must not fit every argument of `Type.from(...)` - and it is left out of the list a message offers,
because nobody writes it and nobody can call it.

**70. Four smaller gaps of `docs/LINEAR.md` section 12, and which of them were still there.**
_Decision:_ two were, two were not, and both halves are recorded because a stale gap costs the next round a day.

- **A type parameter`s default reaches a member of a concrete `extend` (gap 4).** `Vector2.zero` where `zero` lives in
  `extend Vector2<Float>` reported "Cannot infer `Scalar` of `Vector2`" although the declaration says
  `Scalar: Numeric = Float`. A member access on a bare type name has nothing else to go on, so `withDeclaredDefaults`
  in `expression.trb` puts the declared defaults in where the checker`s own variables stand, and the variables are
  unified with them so the rest of the statement agrees. A parameter without a default fills in nothing: then the name
  really says nothing about its arguments, and `` `Box` has no member `zero` `` is the right message.
- **`Void` has no member (gap 13).** `print(x).round()` answered "The checker did not work out the type of this
  expression - this is a bug of the compiler". `isFullyKnown` did not count `Void` as a known type, so
  `reportUnknownMember` said nothing and the expression stayed untyped. `Void` is the type with exactly one value and
  its members are the ones the standard library declares on it, so a member it has not is an ordinary mistake.
  The note is the trap the line almost always is: **a `(` right after the name of a call is its argument list,
  whatever whitespace stands in between**, so `print (0..4).length()` is `print(0..4)` and then `.length()` on what it
  produced. Both spellings now name `Void`, and the note says to write `print((0..4).length())`.
- **A static member through a type parameter (gap 7) works in the checker.** `Scalar.zero()` inside
  `fn total<Scalar: Zero & Add>` resolves and records `Dispatch.Forwarded`, and `generics.test.trb` has pinned it
  since 4.9 ("`Target.from(value)` reaches a static member of a bound"). What LINEAR quotes - `Unknown name `Scalar`` -
  is **stage 0**, not this pass; the lowering finds the witness in `Witness.Forwarded(parameter, bound)`.
- **A member-level `where` on a method of a generic `type` (gap 11) works too.** `fn length(): Int where Item:
  Absolute` inside `type Pair<Item: Counted>` reaches `absolute()` in its own body, and a `Pair(Plain(1), Plain(2))`
  whose `Item` is not `Absolute` hears `` `Plain` does not implement `Absolute` `` at the call. `addBound` writes a
  member`s `where` into `parameterBounds` like an inline bound, which is what makes it visible in the body.

Counting `Void` as a known type also **unmasked a true positive** of an older rule: `?` needs the enclosing function
to have a place for a failure, and `requireTryResult` was silent for a body that produces `Void` for the same reason.
Two snippets of `docs/language/concurrency-and-streams/streams.md` wrote `fn printAll(var source: Source<Int, Never>):
Task<Void>` with a `?` in the loop head; they answer `Task<Result<Void, Never>>` and end in `Ok void` now, which is
what the rule has always said. Nothing in `std/` or `compiler/` was in that shape.

**71. Is a type parameter rigid against *every* type, and what happens to a numeric literal then?**
Entry 66 closed the trait-typed half of the hole and left the rest: `fn textOf<Item: Numeric>(): Item { "x" }`,
`fn boundless<Item>(): Item { 1 }` and `fn viaBinding<Item: Numeric>(): Item { const value: Item = 1 }` all checked
clean, and only the IR verifier caught them. The reason is one line: `isUndecided` counted a generic parameter as a
type that says nothing about what may arrive at it.
_Proposal:_ a type parameter is decided, and the two things that really depended on the hole get the rules they need:

- **A `const` parameter stays undecided.** `<const Size: Int>` is a **value** and not a type: `Size` read as a value is
  an `Int` the call site fills in, and `for row in 0..Rows` of `examples/tour` is ordinary code.
- **A numeric literal adapts to the parameter itself.** CONCEPT ("No `default` keyword and no `Default` trait") says
  the neutral element of an algorithm over a `Scalar: Numeric` is the **literal**, so `0` and `1` have to *be* the
  parameter. `boundImplies` in `expression.trb` asks whether the parameter`s bounds imply `Numeric` (an integer
  literal) or `Real` (a decimal one), reading the declaration and its supertraits rather than `traitsOf` - this runs at
  every literal of every program, and the closure of "every trait a type has" derives implementations on first use.
  What the literal becomes is recorded as `Adaptation.Literal(<the parameter>)`; what that means for a non-primitive
  instantiation is the lowering`s question.

_Reason:_ inside the body that declares it a parameter stands for the one type the call site chose. `"x"` is not that
type, `1` is not either - unless the bound says the parameter is a number, which is the one exception the decision log
writes down.

_Decision:_ accepted. `Real` is exported from `std/prelude` for it, because a decimal literal needs a bound that
implies it and the prelude is where the language looks its traits up. **One place in the repository depended on the
hole:** `Source.toList`, `Source.count` and `Source.fold` in `std/stream/src/source.trb` wrote `collect listing()` as
the body of a `Task<Result<...>>` - the body of such a function produces the **value** of the `Task`, so it was a
`Task<Result<...>>` where a `Result<...>` was wanted, and the parameters `Item`/`Failure` in the types were what kept
it quiet. All three `await()` now.

**72. Is `From` reflexive, and may an `Into<Path>` parameter take a `Path`?**
_Decision:_ **yes, and CONCEPT already decided it** ("Conversions"): *"Every type has `From<Self>`, and that conversion
is the value itself. It is not written down anywhere and could not be: a blanket `extend<Value> Value with
From<Value>` would overlap with every other implementation of `From`."* So `std/core` must **not** get that blanket,
and the checker is right to accept a `Path` for a parameter of type `Into<Path>`: `canDerive` in `derive.trb` answers
yes for a `From` whose source is its target, and `deriveFor` records a `DerivedImplementation(Path, From<Path>,
["from"])` like any other generated one. What the back end reported - a `Path -> Path` instance of the **blanket**
`Into` whose body calls the written `Path.from(String)` with a `Path` - is the lowering picking the written
implementation where the witness names the derived one: **the generated reflexive `From` needs a body of its own, and
that body is the value.** Nothing about it is a checker change.

**73. `==` on some instantiations of a generic record - not reproduced here.**
The back-end round reports that `recordTraitCall` records no member for `==` on a generic type with a concrete
`extend`. Probed in both shapes the report names - `type Pair<Item: Numeric>` with `extend Pair<Float64>` holding a
`const` that constructs the type, and with a `fn` that constructs it - `Pair<Float64> == Pair<Float64>` and
`Pair<Int64> == Pair<Int64>` both resolve to `call equals` with `implementation Pair<Float64>: Equals` and
`implementation Pair<Int64>: Equals`. A third probe with the real shape of `std/linear`'s `Vector2` - the same
parameter default, two concrete `extend`s holding `const`s that construct it, the alias `Float` in both the default and
the `extend` - answers the same thing. So the shape that loses the resolution is a different one, and the record is
still owed (LINEAR 15).

**The mechanism to look at first is the memoization, and it is the one entry 68 already names.** `findMember` keeps a
*negative* answer per `(module, receiver, isStatic, name)` in `memberMissing`, and `traitsOf` keeps the closure of
"every trait a type has" per type - both of them answers about a **closed world that still grows**, because `Equals`,
`Hash`, `Show` and the reflexive `From` are derived on first use. A query for `Vector2<Float>.equals` that happens
before the `Equals` of that instantiation was derived caches "there is none" for the rest of the run, and
`Vector2<Int>` keeps its record because its first query happened after. That also explains why the reproduction depends
on the whole program rather than on the declaration: which query comes first does. A `traitsOf`/`memberMissing` that is
invalidated when a derived implementation is added would close this, entry 68's remaining half **and** the
`traitsDeclaring` route to the `TryFrom` hole in one change, and it is the round to do next.

One line of `checkCall` is written the way it is because of the **native** back end, and the reason belongs here:
`var target = resolveTarget ...` followed by `target = chooseOverload(checker, target, ...)` does not build. Whether a
parameter is `owned` or `borrowed` in the IR is decided by the callee: the old `chooseOverload` returned the very
`target` it was given on one path, which makes the parameter `owned` and the assignment a hand-over; the new one never
returns it, so the parameter is `borrowed` and the caller still owns the slot it is writing into. The ownership
verifier then answers *"%7 is overwritten while it still owns a value"* over the whole function (22 findings, and the
compiler does not build - the `suite` gate is what catches it). Binding the answer first (`const chosen = ...` and then
`target = chosen`) is correct and builds. **The finding for `ir/`:** `x = f(x)` where `f` borrows `x` has to release
the old value after the call instead of before it; today the shape only works when the callee happens to take its
argument over.
