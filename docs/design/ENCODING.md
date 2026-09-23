# Encoding

**Status: implemented, slices 1-6 of section 14.** The vocabulary is in `std/encoding`, the three forms are derived by
the checker and generated per (type, format) by the native back end (`compiler/src/ir/lower/encoding.trb`), and
`std/json` is written in TorbScript on top of it. `with Encode by value` (slice 7) and `std/xml` (slice 8) are open;
what the implementation decided where the design left a choice is section 16.

**A value is its constructor call.** The compiler knows every type's constructor and offers it in three forms — written
(`Encode`), read (`Decode`) and described without a value (`Describe`). That is the whole mechanism. There is no list of
magic traits, no annotation, no second trait per format, and nothing in a type ever names a format.

```text
                    ┌─── Encode ───→  written      ┐
a type's            │                              │
constructor  ───────┼─── Decode ───→  read         ├──→  Encoder / Decoder / Describer   ← what a format implements
                    │                              │
                    └─── Describe ─→  described    ┘     (a bound, never a type)

what is special about ONE type in ONE format  ──→  a mapping VALUE in that format's own DSL
                                                   found by the qualified type name `record` carries
```

- **[1. The principle](#1-the-principle)**
- **[2. The three traits](#2-the-three-traits)** — final signatures
- **[3. Derivation](#3-derivation)** — the exact rule, as a table
- **[3a. A capsule is written as its source type](#3a-a-capsule-is-written-as-its-source-type)** — the conversion pair
- **[4. The vocabulary](#4-the-vocabulary)** — scalars and four shapes, and what is removed against today
- **[5. `Describe` in full](#5-describe-in-full)**
- **[6. Mappings](#6-mappings)** — the contract between a format and its DSL, type names, the decode side
- **[7. Static dispatch, and what happens to `Encode` as a type](#7-static-dispatch-and-what-happens-to-encode-as-a-type)**
- **[8. The adjustment ladder](#8-the-adjustment-ladder)** — seven steps, one example each
- **[9. Format families](#9-format-families)** — checked against the vocabulary
- **[10. The lab](#10-the-lab)** — what it proved and what it changed
- **[11. How it feels](#11-how-it-feels)** — in numbers
- **[12. Where this comes from](#12-where-this-comes-from)** — serde, Codable, kotlinx, autodocodec, scodec, tapir, Jackson, EF Core, Elm
- **[13. What the language and the compiler must provide](#13-what-the-language-and-the-compiler-must-provide)**
- **[14. Migration](#14-migration)** — slices that each land green
- **[15. What the owner decided](#15-what-the-owner-decided)**

The lab that proves all of this is [`examples/encoding-lab`](../examples/encoding-lab): the vocabulary declared locally,
four sample types with the code the compiler would derive written out by hand, and seven formats — JSON, CSV, XML with a
mapping DSL, a Protobuf-like packed binary format, SQL DDL with row binding, command line arguments and logfmt. It type
checks, and 34 tests assert the claims of this document one by one; the test package builds natively. The lab keeps
its own copy of the vocabulary and its derived code written by hand, which is what makes it a proof of the design and
not of the implementation; `tests/conformance/encoding-*.trb` are the programs that pin what the compiler derives.

---

## 1. The principle

A type's constructor is the one place that says what the type consists of. It is generated from the fields, it contains
no logic, and its parameters are exactly the data a caller has to supply. So it is also the right answer to every
question a format asks:

| A format asks | The constructor answers |
|---------------|-------------------------|
| what is in this value? | its arguments, with the field names |
| how do I build one? | the call itself |
| what does this type look like, without a value? | its parameter list, with types, defaults and doc comments |

Three consequences follow, and they are the whole design:

**Derivation is one condition, not three.** All three forms are derived exactly when the constructor is usable from
outside, over exactly its parameters. So what is written can always be read back — a round trip is not a property that
has to be tested per type, it is a property of the rule.

**Skipping needs no annotation.** A `private` field with a default is not a parameter of the constructor from outside, so
it is not part of any of the three forms. A cache, a memo, a derived summary: they stay out, and the type already said
so with `private`.

**A validated type does not get its fields written or read.** `Email` has a `private value: String` with no default, so
the constructor is not usable from outside, so no field list is derived and a decoded `Email` cannot skip
`Email.tryFrom`. The invariant is not protected by a rule about decoding; it is protected by the same rule that
protects construction. What such a type gets instead is section 3a.

## 2. The three traits

```trb
/** A value writes itself. */
public trait Encode {
  fn encode<Target: Encoder>(var target: Target)
}

/** A value reads itself back. */
public trait Decode {
  static fn decode<Source: Decoder>(var source: Source): Result<Self, DecodeError>
}

/** A type describes its structure, without a value. */
public trait Describe {
  static fn describe<Target: Describer>(var target: Target)
}
```

Three traits, three single methods, three names that are the method's name: the naming rule decides `Describe`, and
`Describable` is not a word this language uses.

`Encode` and `Decode` stay **implicit**: a type that can have them has them, with no `with` to write. `Describe` is
implicit too — it is the same constructor, and a schema generator that had to ask every type in the world to opt in
would be useless. The cost of implicitness is one diagnostic, and the diagnostic is at the call site and names the chain:

```text
error: `User` is not `Decode`
 --> src/main.trb:12:24
  |
12 | const user = json.decode<User>(text)?
  |                          ^^^^
  = its field `avatar: Image` is not `Decode`: `Image` has a private field `pixels` with no default, so its
    constructor cannot be used from outside. Write `decode` for `Image` by hand, or give `pixels` a default
```

One root cause, one message, and the field chain in the note — three levels deep if it has to be
(`User.avatar → Image.palette → Color`).

`DecodeError` collects the field chain on its way out, so a message names where the input was wrong and not only what
was wrong:

```trb
public type DecodeError with Show, Error {
  message: String
  path: List<String> = []

  fn inside(segment: String): DecodeError { copy path: path.inserted(0, segment) }
}
```

```text
price.currency: a value is needed
```

## 3. Derivation

**The parameter list.** The constructor's parameters, from outside, are the fields a caller can pass: everything except
a `private` field. `private(var)` is publicly readable and publicly constructible — the `private` is on the `var`, not on
the field — so a `private(var)` field **is** part of all three forms.

**The condition.** All three are derived when both hold:

1. the constructor is usable from outside — every `private` field has a default; and
2. every passable parameter's type carries the same trait.

| The type | Derived | Why |
|----------|---------|-----|
| `type` whose every `private` field has a default, and whose passable parameters all carry the trait | all three | the constructor is usable, over its parameters |
| `type` with a `private` field without a default (`Email`) — a **capsule** | through its conversion pair, section 3a | the constructor is not usable from outside, so the fields are not the parameter list |
| `type` with a `private` field **with** a default (a cache) | all three, **without that field** | it is not a parameter from outside. No annotation said so |
| `type` with a `private(var)` field | all three, **with** that field | publicly readable, publicly constructible |
| a passable parameter whose type lacks the trait | none, and the error at the call site names the chain | |
| `type` with cases | all three, as a `variant` over the cases, each case over its own fields | a case is a constructor too |
| a generic `type` (`Page<Item>`) | conditional: `Page<Order>` has what `Order` has | one implementation, monomorphized per instantiation |
| a literal type (`type Status = "draft" \| "sent"`) | all three, as its base | it already has `Show`, `Equals`, `Hash`, `TryFrom<String, _>` |
| a tuple `(Int, String)` | all three, over its positions | it has no declaration anybody could write them in |
| `Option<Value>` | conditional on `Value` | `None` is `nothing()`, `Some(v)` is `v` |
| `List`, `Set`, `Map` | conditional on the item, key and value | `sequence` and `map` |
| a function type | none | there is nothing to write |
| a `trait` | none | there is nothing structural |
| a `shared type` | **never** | it has an identity, not a value. Only `Show` is generated for one |
| a `native type` | only what it declares | the runtime decides what it is |

**Fields with a default may be missing on the way in.** A derived `decode` uses the default when the input has no such
field, and it evaluates the default only then — a default is evaluated at every construction, and a value that is
present must not pay for one.

**The order is the declaration order**, everywhere: the fields of a record, the columns of a table, the positions of a
tuple, the options of a `--help` text, the default field numbers of a wire format. It is the one ordering a type
already has.

## 3a. A capsule is written as its source type

A type whose constructor is closed from outside is a **capsule**
([the language page](language/types/data-or-capsule.md)): private fields, a `static fn` factory that is the only way
in, accessors, and one conversion pair. Its fields are not a parameter list a caller could be handed, so the pair takes
their place — and the pair is exactly the two functions the type already had to write.

**The pair is the one type `Source` for which both directions exist.** `Self` has `TryFrom<Source, Failure>` or
`From<Source>`, and `Source` has `From<Self>`. The reflexive `From<Self>` that every type carries never counts, and
neither does `From<Never>`: both hold for every type at all, so a pair built from one would be no decision of the
type's own.

```trb
type Title {
  text: String
}

type Slug with From<Title> {
  private storedText: String

  static fn from(value: Title): Slug {
    Self value.text.toLowerCase().replace(" ", "-")
  }

  fn text(): String {
    storedText
  }
}

extend Title with From<Slug> {
  static fn from(value: Slug): Title {
    Title value.text()
  }
}

print Slug.from(Title("Data Or Capsule")).text()
```

That is the whole declaration, and these two are derived from it:

```text
extend Slug with Encode, Decode {
  fn encode(var encoder: Encoder) {
    Title.from(self).encode(encoder)          // the way back, then that value's own `encode`
  }

  static fn decode(var decoder: Decoder): Result<Slug, DecodeError> {
    Ok(Slug.from(Title.decode(decoder)?))     // the source, then the way in
  }
}
```

**A fallible way in becomes a `DecodeError`.** Where the pair is a `TryFrom<Source, Failure>`, the derived `decode`
answers `DecodeError(failure.show())` for a `Fail`, so the check the factory exists to force also runs for a value that
came out of a document. Where both a `TryFrom` and a `From` reach the same `Source`, the `TryFrom` is used: it is the
one that can refuse.

**With no pair, or with more than one, there is no `Decode`,** and the message names the rule and what is missing (rule
6 of the language page lists both texts). The way **out** is different, and deliberately: it cannot break an invariant,
it is what `describe(value)` and a failing `assert` show, and the generated `Show` of the same type prints its private
fields too — so without a pair `Encode` stays the field-wise one, exactly as it is for every other type.

**What this costs.** A capsule is written *as* its source, with no `record(typeName)` around it, so no type name
reaches the format and a mapping keyed by one (section 6) cannot pick a capsule out. That is the trade the rule makes:
a `Path` in a JSON document is its text and not an object, which is what a reader of that document expects, and a type
that needs the name writes its own `encode`.

## 4. The vocabulary

A format implements one trait per direction. Every method is free of type parameters and free of closures, so an
implementation is a straight-line state machine and derived code is straight-line code.

```trb
public trait Encoder {
  var fn nothing()
  var fn bool(value: Bool)
  var fn int(value: Int64)
  var fn unsigned(value: UInt64)
  var fn float(value: Float64)
  var fn decimal(value: Decimal)
  var fn string(value: String)
  var fn bytes(value: List<UInt8>)

  /** `length` is known for a collection and unknown for a pipeline: a binary format writes it in front. */
  var fn sequence(length: Int?)
  var fn map(length: Int?)
  /** `typeName` is the qualified name of the declaration: the key a format finds a mapping by. */
  var fn record(typeName: String)
  var fn variant(typeName: String, name: String)
  /** The name of the field whose value comes next. */
  var fn field(name: String)
  /** Closes the innermost `sequence`, `map`, `record` or `variant`. */
  var fn finish()
}

public trait Decoder {
  /** Consumes a "nothing" if one is there. This is how `Option` decides between `None` and `Some`. */
  var fn nothing(): Bool

  var fn bool(): Result<Bool, DecodeError>
  var fn int(): Result<Int64, DecodeError>
  var fn unsigned(): Result<UInt64, DecodeError>
  var fn float(): Result<Float64, DecodeError>
  var fn decimal(): Result<Decimal, DecodeError>
  var fn string(): Result<String, DecodeError>
  var fn bytes(): Result<List<UInt8>, DecodeError>

  var fn sequence(): Result<Int?, DecodeError>
  var fn map(): Result<Int?, DecodeError>
  /** `true` while one more item of the open sequence or entry of the open map follows, and positions on it. */
  var fn hasNext(): Result<Bool, DecodeError>
  var fn record(typeName: String): Result<Void, DecodeError>
  /** Opens a variant and answers which case is there. */
  var fn variant(typeName: String): Result<String, DecodeError>
  /** Positions on the field `name`. `false` when the input has no such field: the field's default applies. */
  var fn field(name: String): Result<Bool, DecodeError>
  var fn finish(): Result<Void, DecodeError>
}
```

**Eight scalars and four shapes**, and the scalar names are the names of the language's own types, so a format learns no
second vocabulary. Narrow numbers widen on the way in and are checked on the way back, so nothing is lost. There is no
tree in between: a value is written while the type describes itself.

**Two rules beyond "four shapes":**

**A record that announces no field is a wrapper.** `record(typeName)`, one value, `finish()` — that is what
`with Encode by value` generates for a single-field type, and it is how a well-known type keeps its name. JSON, XML, CSV
and logfmt write the value and nothing around it; SQL reads the name and picks a column type; CBOR reads the name and
writes a tag. Without this rule a wrapper either loses its identity (`Instant` becomes an indistinguishable `Int`) or
gains a field name nobody wrote (`{"value": ...}`). This is the one rule the lab added to the approved direction; see
section 10.

**A field that is written as `nothing()` may simply be absent.** XML omits the element, a wire format omits the key,
CSV writes an empty cell. On the way back `field(name)` answers `false` and the type's default applies. Absence and "the
default applies" are the same thing, in every format, with one rule.

### What is removed and renamed against today's `std/encoding`

| Today | New | Why |
|-------|-----|-----|
| `SequenceEncoder`, `MapEncoder`, `RecordEncoder` | gone; `Encoder.field`, `Encoder.finish` | a sub-encoder needs an associated type the language does not have, and a trait-typed `var` closure parameter it cannot pass |
| `SequenceDecoder`, `MapDecoder`, `RecordDecoder` | gone; `Decoder.hasNext`, `Decoder.field`, `Decoder.finish` | the same, plus `RecordDecoder.field<Value>` is not object safe |
| `Decoder.record<Output>(typeName, read: (var fields) => ...)` | `Decoder.record(typeName)` + `finish` | a generic member with a closure over a trait-typed `var` is neither object safe nor passable |
| `fn encode(var encoder: Encoder)` | `fn encode<Target: Encoder>(var target: Target)` | a `var` argument cannot be coerced to a trait-typed `var` parameter, and a format needs its own encoder back |
| `describe(value: Encode): String`, a `native` | `fn describe<Value: Encode>(value: Value): String`, written in TorbScript | it is a format like any other: an `Encoder` that builds an `EncodedValue` and shows it. One native fewer |
| `XmlEncode` / `XmlDecode` | gone | XML is a mapping value in `std/xml`'s DSL. The other tens of thousands of formats get no trait of their own either |
| `Format.encodeAll` / `decodeAll` | `format.encode value` / `format.decode<Type>(input)` | one entry per format per direction |
| — | `Describe`, `Describer`, `Structure`, `FieldDescription`, `FieldDefault` | new: the structure without a value |
| — | `EncodedValue` | new: a value without its type (section 7) |

`Format<Failure>`, `Format.items` and `Format.encoded` stay exactly as [STREAMS.md](STREAMS.md) section 10 specifies:
streaming happens at the level of the element, a framer finds where one element ends, and the element itself is decoded
synchronously through the ordinary `Decode`.

## 5. `Describe` in full

```trb
public trait Describer {
  var fn bool()
  var fn int()
  var fn unsigned()
  var fn float()
  var fn decimal()
  var fn string()
  var fn bytes()

  /** The description that follows is the one that may be absent. */
  var fn optional()
  var fn sequence()
  var fn map()
  /** Opens a record. `false`: this target knows the type already and does not want the body again. */
  var fn record(typeName: String): Bool
  var fn variant(typeName: String): Bool
  /** One case of the open variant. Its fields follow, `finish` closes it. */
  var fn variantCase(name: String)
  /** The next description belongs to this field. */
  var fn field(description: FieldDescription)
  var fn finish()
}

/** What the compiler knows about one constructor parameter, and a schema or a help text needs. */
public type FieldDescription {
  name: String
  /** The field's doc comment, as written. This is what an `@description("...")` annotation is for elsewhere. */
  documentation: String = ""
  default: FieldDefault = FieldDefault.Required
}

public type FieldDefault {
  /** No default: the field has to be there. */
  case Required
  /** A default the description cannot write as data - a closure, or a value of a type without `Encode`. */
  case Computed
  /** A default that is static data, and this is it. */
  case Constant(value: EncodedValue)
}
```

The same method names as `Encoder`, without values. That is not symmetry for its own sake: a format author who has
written an `Encoder` can write a `Describer` without learning anything, and the derived code of the two forms reads the
same way.

**The doc comment is the description.** `/** The order this belongs to. */` on a field arrives as
`FieldDescription.documentation`, because a doc comment is part of the syntax tree. `@description`, `@doc`, `@Schema`
and the rest of the annotation family have nothing left to do.

**Recursion terminates by construction.** `record` and `variant` hand the type name over **and then** ask whether the
body is wanted. A target that has seen the name already answers `false` and refers to the type by name. A type that
contains itself therefore walks exactly once:

```trb
/** DERIVED for `Category { name: String, children: List<Category> = [] }` */
fn describe<Target: Describer>(var target: Target) {
  if !target.record("lab/Category") {
    return
  }
  target.field FieldDescription("name", documentation: "What the category is called")
  target.string()
  target.field FieldDescription(
    "children",
    documentation: "The categories below this one",
    default: FieldDefault.Constant(EncodedValue.Sequence([])),
  )
  target.sequence()
  Category.describe target
  target.finish()
  target.finish()
}
```

```text
Record("lab/Category", [name: Text, children: Sequence(Reference("lab/Category"))])
```

**A schema is a value, and the conversion is written once.** Walking a visitor is harder than walking a tree, and every
schema format wants a tree, so `std/encoding` ships the one `Describer` that builds one:

```trb
public type Structure {
  case Boolean
  case Integer
  case Natural
  case Floating
  case Exact                                    // `Decimal`
  case Text
  case Binary
  case Optional(of: Structure)
  case Sequence(of: Structure)
  case Mapping(key: Structure, value: Structure)
  case Record(typeName: String, fields: List<StructureField>)
  /** A type that is its one value (`with Encode by value`). It keeps its name, so a format can map it. */
  case Wrapper(typeName: String, of: Structure)
  case Variant(typeName: String, cases: List<StructureCase>)
  /** A type that is already being described further up: the schema refers to it by name. */
  case Reference(typeName: String)
}

public type StructureField {
  description: FieldDescription
  of: Structure
}

public fn structureOf<Value: Describe>(): Structure
```

In the lab, CSV, SQL and the command line are written against `Structure` and never touch `Describer` at all. A format
that needs to stream a schema (a `.proto` writer over a thousand messages) implements `Describer` directly.

## 6. Mappings

**What is special about one type in one format is a value in that format's own DSL.** Not an annotation on the type, not
a second trait, not a word in `std/encoding`'s vocabulary.

```trb
const xml = Xml.format {
  map<Price> {
    element "price"
    attribute { _.currency }
    text { _.amount }
  }
  map<Instant> {
    text { _.iso8601() }
  }
}

print xml.encode(order)
// <Order><id>7</id><customer>ada@example.test</customer><price currency="EUR">19.99</price>…
```

### The contract between a format and its DSL

Five rules. A format author who follows them gets nesting, decoding and early errors for free.

1. **A format instance holds its mappings.** `Xml.format { … }` answers an `Xml` that carries
   `Map<String, XmlMapping>`; `xml.encode order` uses them. Mappings are not global, not registered and not ambient:
   two parts of one program may hold two differently mapped `Xml` values, and neither can affect the other. This is EF
   Core's `OnModelCreating` and Jackson's mixins, without the global mapper.
2. **The key is the qualified type name** that `record(typeName)` and `variant(typeName)` carry anyway. So a mapping
   applies **wherever the type appears**: `Price` inside `Order` inside a `Page<Order>` inside a `List`, without a type
   test, without specialization, and without `Order` knowing that `Price` has a mapping.
3. **A field reference is a quoted expression.** `attribute { _.currency }` is checked as an ordinary expression against
   `Price`, so a misspelled or removed field **does not compile**, and the name comes out of the tree via `nameOf`.
   A mapping for a field that does not exist is impossible, not diagnosed.
4. **The decode side uses the same mapping.** The format looks the type name up again — on the way in it needs the
   reverse direction too (element name to type name), which is one `Map` built when the format is built. Nothing about
   a mapping is write-only, and a format that is tolerant on the way in (a field that was written as an attribute read
   back out of a child element) is tolerant because it chose to be, not because the mapping forced it.
5. **Everything that can go wrong goes wrong when the format is built.** `Xml.format` answers a
   `Result<Xml, XmlProblem>` and validates once: an element name collision, a mapping for a type whose fields do not fit
   it, two fields claiming the text content. A format that is built is a format that works.

### What `typeName` has to be

`typeName` is the **declaration's qualified name**: the package name, the module path inside it, and the type.
`"acme/orders/Price"`, `"std/time/Instant"`, `"lab/Order"`. Three properties matter:

- **Unique across the program.** The package name is globally unique in the workspace and two declarations in one module
  cannot share a name, so the triple is.
- **Stable across a recompilation, a reordering and a rename of a field.** It is written in the source; nothing derived
  from layout or from an internal id appears in it.
- **Free of type arguments.** `Page<Order>` and `Page<Shipment>` both announce `"acme/Page"`, so a mapping for `Page`
  applies to every instantiation, and `Order`'s own mapping is found when `Order` announces itself one level down.
  A format that needs to tell instantiations apart is a format that needs a schema, and a schema comes from `Describe`.

A **case** of a variant announces the variant's name plus the case name, both of them, because `variant(typeName, name)`
carries two arguments. On the describing side a case is `variantCase(name)` inside the open `variant`.

`typeName` is not a path anybody can look a type up by. It is a string that travels with a value; there is no
`Type.forName`, and this design adds none.

### What a format author writes to offer a DSL

In the lab, XML's mapping DSL is **60 lines**: a mapping type with three `var fn` methods (`element`, `attribute`,
`text`), a `roleOf` question over it, a builder type with one `map` method, and the validation loop in `Xml.format`.
A format with one adjustment (CSV's `flatten`) is **12 lines**. That is the whole cost of "bring your own DSL", and it
is ordinary TorbScript: a receiver closure over a `var` value, which the language already has and the configuration DSL
already uses.

### Well-known types

`std/time/Instant` in CBOR is tag 1, in BSON a UTC datetime, in SQL a `TIMESTAMP`, in JSON an ISO 8601 string. All four
are the same mechanism: `Instant` is a wrapper (`with Encode by value`), so it announces
`record("std/time/Instant")`, and each format's mapping keys on that name. `Instant` knows about none of them, and none
of them knows about `Instant`'s fields.

## 7. Static dispatch, and what happens to `Encode` as a type

This is the hardest technical question in the design, and it has one answer with one consequence.

**Why bounded generics are forced.** Two findings of the checker and the back end:

- **A required member with type parameters of its own is not object safe.** `Decoder.record<Output>` and
  `RecordDecoder.field<Value>` cannot sit in a witness table, so `Decoder` cannot be a trait type at all. Flattening the
  vocabulary (section 4) removes the generic members, but not the second problem.
- **A `var` argument cannot be coerced to a trait-typed `var` parameter.** A place is not a value: `var encoder: Encoder`
  cannot receive a `var jsonEncoder: JsonEncoder`, because the coercion would have to build a new value and the
  changes would be written back into it and lost.

And one requirement that decides it even if both were fixed: **a format needs its own encoder back.** `Json.encode`
builds a `JsonEncoder`, hands it over, and then reads `target.output` out of it. Through a trait-typed binding the
concrete type is gone and there is nothing to read. Making the encoder a `shared type` would work and is wrong: an
encoder is not an object with an identity, and every `Encode` implementation in the world would pay for the allocation.

So the vocabulary is bounded generics — `fn encode<Target: Encoder>(var target: Target)` — and it is monomorphized
like every other generic. The derived `encode` of `Order` writing into a `JsonEncoder` is a direct call chain with no
table lookup, which is the point.

**The consequence: `Encode` is no longer usable as a type.** Its one member has a type parameter, so `Encode` is not
object safe, and `List<Encode>` does not exist. Three places used it:

| Used to be | Becomes |
|------------|---------|
| `RecordEncoder.field(name: String, value: Encode)` | gone. Derived code writes `target.field "name"` and then the value's own `encode`, with the field's static type |
| `fn describe(value: Encode): String`, `Json.encode(value: Encode)` | `fn describe<Value: Encode>(value: Value): String`, `fn encode<Value: Encode>(value: Value): String`. Generic functions, not trait-typed parameters. The call site does not change |
| `Expression.captures(): List<Encode>` | `List<EncodedValue>` |

**`EncodedValue` is what replaces `Encode` as a type.** Not nothing, and not a tree every value travels through:

```trb
/** Any `Encode` value, held without its type. */
public type EncodedValue with Show, Encode, Decode {
  case Nothing
  case Bool(value: Bool)
  case Int(value: Int64)
  case Unsigned(value: UInt64)
  case Float(value: Float64)
  case Exact(value: Decimal)
  case String(value: String)
  case Bytes(value: List<UInt8>)
  case Sequence(items: List<EncodedValue>)
  case Mapping(entries: List<EncodedValue>)
  case Record(typeName: String, fields: List<EncodedField>)
  case Variant(typeName: String, name: String, fields: List<EncodedField>)

  static fn of<Value: Encode>(value: Value): EncodedValue
}
```

It is built by an ordinary `Encoder` (`Values`, in `std/encoding`), it writes itself into any other encoder, and it
loses nothing — every scalar of the vocabulary has a case, including `Decimal` and bytes. It is used in exactly three
places, all of which are "a value whose type has already been left behind":

- **a quotation's captures.** A SQL provider binds capture *i* as a parameter; `assert` shows it as text. Both read an
  `EncodedValue`, which is what they wanted from `Encode` in the first place.
- **a field default in a schema** (`FieldDefault.Constant`), which a DDL statement and a `--help` line print.
- **`rendered(value)`** (today's free function `describe(value)`, renamed so that `describe` means one thing), which is
  `EncodedValue.of(value).show()`.

This is not the rejected `Data`/`ToData`/`FromData` design. That one made **every** value travel through a tree, built
the document twice and lost precision. `EncodedValue` is one format among many — an `Encoder` that writes into memory —
and nothing goes through it unless a program asks for a value without its type. Every other format writes directly.

**The one thing that is genuinely lost** is a heterogeneous collection of statically typed values: `List<Encode>` as a
list of things that will later be written with full fidelity by a format chosen later. What replaces it is
`List<EncodedValue>`, which is the same thing with the scalars already widened. Nothing in the standard library needs
more than that.

**What the checker and the back end must support** for this to work, in the order the design depends on it:

1. **A static trait member reached through a bound**, monomorphized: `Value.decode(source)` where `Value: Decode`, and
   `Item.describe target` where `Item: Describe`. The checker resolves it today; the back ends must emit it, and stage 0
   cannot (it has no types at run time).
2. **A trait method with type parameters of its own, monomorphized per (implementation, argument) pair.** `Encode` has
   one member and it is generic; there is no witness table for it and there must not be one. `compiler/src/ir/witness.trb`
   currently refuses a derived `encode` for exactly this reason.
3. **Overload resolution over two traits that give one type a method of the same name.** Two visible traits with an
   `encode` on `Float64` produce an overload set, and resolution compares the caller's type parameter with the trait
   method's own by identity and rejects both. Extension visibility (a trait's extensions are visible where the trait is)
   removes the ambiguity in the normal case; the identity comparison is a bug either way.
4. **`with Encode by value` for a `var` parameter.** Delegation of a method that takes a `var` parameter must forward
   the path and not a copy.

## 8. The adjustment ladder

Seven steps, in the order a program reaches for them. Nothing above step 0 is needed for the common case, and every
step is a line somebody wrote on purpose.

**0 — nothing.** The type is written, the three forms exist.

```trb
type Order {
  id: Int
  price: Price
}

print Json().encode(order)
```

**1 — an option of the format.** A convention of the format is not a property of the type.

```trb
print Json(naming: .SnakeCase).encode(order)          // "log_label" instead of "logLabel"
```

**2 — visibility.** A field that is not part of the data says so with `private` and a default.

```trb
type Order {
  id: Int
  /** Kept so a list view does not build it twice. */
  private summary: String = ""                        // not in any of the three forms
}
```

**3 — delegation.** A wrapper is its value, and keeps its name.

```trb
type Email with Encode by value, Describe by value, TryFrom<String, String> {
  private value: String

  static fn tryFrom(text: String): Result<Email, String> { … }
}

extend Email with Decode {                            // reading goes through `parse`: the invariant holds
  static fn decode<Source: Decoder>(var source: Source): Result<Email, DecodeError> {
    source.record("acme/Email")?
    const text = source.string()?
    source.finish()?
    Email.tryFrom(text).mapError { message => DecodeError message }
  }
}
```

`by value` is sound for `Encode` and `Describe`, which forward. It is available for `Decode` too and then means exactly
what it says — "the wrapper is its value, no check" — which is right for a distinct type and wrong for a validated one.
A type with an invariant writes `decode`, because `by value` would hand back the field's `decode` and wrapping that is
the skipped check.

**4 — a mapping in the format's DSL.** One type, one format, no annotation.

```trb
const xml = Xml.format {
  map<Price> {
    attribute { _.currency }
    text { _.amount }
  }
}
```

**5 — `encode`/`decode` by hand.** This changes the representation in **every** format, which is why it is step 5 and
not step 1.

```trb
extend Duration with Encode, Decode {
  fn encode<Target: Encoder>(var target: Target) {
    target.string iso8601()                           // `PT1H30M` in JSON, in CBOR, in a database row
  }
  …
}
```

**6 — the format's own tree.** A document nobody has a type for.

```trb
const document = Json().parse(text)?                  // `EncodedValue`, or `XmlNode` for XML
```

## 9. Format families

Every family, against the vocabulary. "Needs" is what it needs beyond `Encode`/`Decode`.

| Family | Examples | Needs | Fits |
|--------|----------|-------|------|
| text tree | JSON, TOML, YAML, query strings | a naming option | yes. The baseline |
| self-describing binary | CBOR, MessagePack, BSON, Amazon Ion | `length` in front (it is there), a mapping for tags of well-known types | yes |
| schema binary | Protobuf, Thrift, Avro, Cap'n Proto | field numbers from a mapping, `Describe` to emit the schema file | yes. The default numbering is the declaration order |
| tabular / columnar | CSV, TSV, Parquet, Arrow, a spreadsheet | `Describe` for the header and the column order, a `flatten` mapping, an early error for what is not flat | yes, and the error is early rather than per row |
| row binding | SQL, a key-value store | `Describe` for the DDL, `Encode` for the parameters, a column-type override by type name | yes |
| binary layout | a fixed-width record, a network header, a file format | bit widths and a byte order from a mapping | yes for the shape. It needs bytes the language cannot make yet (section 13) |
| line streams | logfmt, syslog, a Prometheus exposition | nothing. A path made of field names | yes. 116 lines in the lab |
| human formats | `--help` and argument parsing, a form, a settings page, a table view | `Describe` for the doc comments and the defaults | yes, and this is what `Describe` was added for |
| document formats | XML, HTML, Markdown | a mapping for attributes and text content; a tree type for documents that have no type | yes for data that travels as XML; the tree (`XmlNode`) for documents. No `XmlEncode` |

**What does not fit, and does not pretend to.** A format whose *shape* is the content — a Markdown document, an HTML
page, a Word file — is not data binding. It gets a tree type as a value in its own package and ordinary functions over
it, and `Encode`/`Decode` work on that tree the way they work on any other ADT.

## 10. The lab

[`examples/encoding-lab`](../examples/encoding-lab) declares the vocabulary locally, writes out by hand exactly the code
the compiler would derive for seven types (a record with defaults and a private cache, a validated wrapper, a variant, a
nested record, a generic container, a self-referential tree, a row-shaped record), and implements seven formats against
it. `torb run` runs it, `torb test` runs 34 assertions over it.

**Three things the lab changed in the design.**

1. **A record that announces no field is a wrapper** (section 4). The approved direction said a well-known type is
   mapped by its type name — and a wrapper that wrote only its inner value had no name to be mapped by. `Email`
   describing itself as `string()` is indistinguishable from a `String`, so SQL cannot give it a `VARCHAR(320)` and CBOR
   cannot tag it. Making `by value` generate `record(typeName)` + one value + `finish()` costs each format about four
   lines, keeps the natural output (`"ada@example.test"`, not `{"value": …}`), and makes `Structure.Wrapper` carry the
   name a schema needs. Without this rule, point 3 of the direction — "the same way for well-known types" — does not
   work at all.
2. **The vocabulary is flat, and one trait per direction instead of four.** The direction said "move to bounded
   generics"; the lab showed that this also removes the reason the sub-encoder traits existed. A sub-encoder would need
   an associated type (`Encoder.record` would have to name the type it hands to the closure), which the language does not
   have, and `Self` in a closure parameter would force every format's sub-encoder to be the same type as its encoder.
   `field` + `finish` on the one trait is smaller, has no closures, and made every format in the lab a plain state
   machine. The writing side went from 4 traits and 15 members to 1 trait and 14 members.
3. **The decode side of a buffering format is a parser plus one shared `Decoder`.** JSON, XML, CSV, the command line and
   logfmt all have to buffer a record anyway (their fields are unordered, or their input is a line), so they parse into
   an `EncodedValue` and hand it to one `ValueDecoder` that `std/encoding` ships. Only the packed binary format writes a
   `Decoder` of its own, which is the case the pull vocabulary exists for. This was not planned and it is the single
   biggest reason a new format is cheap: logfmt's whole decode side is 30 lines.

**One design claim the lab weakened.** `by value` is right, and stage 0 runs a delegated `var` method against a copy and
drops the change without a word, so the lab writes `Email`'s three forms out by hand. The form in step 3 of the ladder
type checks; only running it needs the fix in section 13.

**What the lab leaves out, and why.** `Decimal` is in the vocabulary of this document and not in the lab's copy of it:
stage 0 has no `Decimal` arithmetic and no `Show` for one, so a format could not write the value it was handed. The
lab's money amount is a `Float64`, which a real `Price` would not be. `bytes` is in the lab's traits and every format
answers it, but nothing round-trips through it, for the reason in section 13 item 5. Everything else in this document
runs.

## 11. How it feels

### What a user writes

Line counts, for the common case and for one step up the ladder.

| Format | The common case | One adjustment | What the adjustment is |
|--------|-----------------|----------------|------------------------|
| JSON | **1** — `Json().encode order` | **1** — `Json(naming: .SnakeCase).encode order` | a format option (step 1) |
| CSV | **2** — build, then `csv.header()` / `csv.row value` | **+1** — `flatten { _.price }` | a nested record becomes two columns (step 4) |
| XML | **1** — `Xml.format { }` then `xml.encode order` | **+4** — `map<Price> { element; attribute; text }` | attribute and text content (step 4) |
| packed binary | **1** — `Packed.format { }` then `packed.encode order` | **+1** — `numbers<Price> { … }` | fixed field numbers (step 4) |
| SQL | **2** — build, then `createTable()` / `insert value` | **+3** — `table`, `primaryKey`, `column<Email>` | table name, key, column type (step 4) |
| CLI | **2** — `Arguments.of<Shipment>()`, then `help()` / `decode` | **0** | the doc comments were already there |
| logfmt | **1** — `Logfmt().encode value` | — | it has no options |

**Zero lines on any type, in every row of that table.** The types in the lab mention no format, no annotation and no
option; `Order` does not know that XML exists, and `Price` does not know it has a mapping.

### What a format author writes

Lines in the lab, code lines in brackets (comments and blanks removed).

| Format | Total [code] | What is in it |
|--------|--------------|---------------|
| logfmt | 148 [116] | the encoder (85), the line parser (30). **Written last, on the clock** |
| arguments | 194 [160] | the option list from `Structure` (50), `--help` (12), the parser (45), name spelling (15) |
| CSV | 252 [184] | the column list from `Structure` (35), the cell encoder (75), the row parser (40), the DSL (12) |
| SQL | 294 [221] | `ColumnType` (25), the column list (45), DDL (15), the parameter encoder (85), the DSL (20) |
| packed binary | 449 [348] | the encoder (130), its **own** `Decoder` (140), varints (20), the DSL (35) |
| JSON | 437 [365] | the encoder (130), the parser (150), the naming option (40) |
| XML | 508 [399] | the encoder (150), the parser (110), node to value (60), the DSL (60) |
| the shared vocabulary | 1113 [816] | the 6 traits (130), `EncodedValue` + its builder (180), `Structure` + its builder (280), `ValueDecoder` (170) |
| the derived code, by hand | 442 [327] | 7 types × 3 forms. About 8 lines per form for a 2-field record, 20 for a 7-field one |

**The ten-minute format.** logfmt took 11 minutes and 148 lines, of which 116 are code and 85 are the encoder — which is
a straight-line state machine with one `put`. Its decode side is 30 lines because `ValueDecoder` is shared. Nothing had
to be registered, no type had to be touched, and it works on every type in the lab including the variant and the nested
record. That is the answer to "a user must be able to bring their own format simply", and it is a better answer than the
design expected, for the reason in section 10.3.

### What was awkward

- **The derived `describe` is the longest of the three forms** — 34 lines for a 7-field record, because every field
  carries a `FieldDescription` with a doc comment and a default. It is generated code, so length costs nothing, but it
  is the one place where reading the derived form does not immediately read as "the constructor".
- **The flattening of nested records into paths is written three times** in the lab (CSV, SQL, the command line), almost
  identically. That is a `std/encoding` helper waiting to be written: "a record as a flat map of paths" is what every
  tabular and row-shaped format wants. It is not part of the vocabulary; it is a convenience next to `Structure`.
- **A variant in a flat format has no good answer.** CSV, SQL and a command line all reject `Status` with an early
  error, correctly, and the user's only way forward is step 5 (write `encode` by hand) or a different type. No
  annotation would have helped; the shape is genuinely wrong.
- **`hasNext` both tests and positions**, which is one subtle line in the `Decoder` contract that a format author has to
  read. The alternative — `next()` answering an `Option` — needs a generic member.

### What surprised

- **Nesting needed no case of its own anywhere.** Every encoder in the lab has a frame stack and every frame is one of
  two or three kinds. `record`, `variant`, `sequence` and `map` all push, `finish` pops, and that is the whole control
  flow. Four shapes plus `finish` turned out to be less machinery than four traits.
- **XML's tolerance came for free.** Because the node-to-value step puts attributes and child elements into one field
  list, a field that was written as an attribute reads back out of a child element with no code at all. serde's XML
  crates need `@attr`/`$text` naming hacks for the same job.
- **`Describe` is used more than `Decode`.** Three of the seven formats (CSV, SQL, CLI) need the structure and two of
  them (SQL, CLI) barely need `Decode` at all. Splitting "described" out of "read" was the right call, and the CLI
  format — which has no `Encode` whatsoever — is the cleanest evidence.

### Against serde and kotlinx

The same task: an `Order` with a nested `Price`, to be written as JSON with snake_case keys, as XML with `currency` as an
attribute and `amount` as the element text, and as Protobuf with fixed field numbers.

| | On the type | At the format |
|---|-------------|---------------|
| **TorbScript** | nothing | `Json(naming: .SnakeCase)`; `Xml.format { map<Price> { attribute { _.currency }  text { _.amount } } }`; `Packed.format { numbers<Price> { … } }` |
| **serde** | `#[derive(Serialize, Deserialize)]`, `#[serde(rename_all = "snake_case")]` on the type, `#[serde(rename = "@currency")]` and `#[serde(rename = "$text")]` on the **fields** — which then leak into the JSON output too, so the type needs two variants or a shadow DTO | the format has no say |
| **kotlinx** | `@Serializable`, `@SerialName`, `@XmlElement(false)`/`@XmlSerialName` from xmlutil, `@ProtoNumber(1)` per field. The class now depends on JSON, XML and Protobuf; the documented remedy is a duplicate DTO per format | the format has no say |

The difference is not verbosity, it is **direction**. In serde and kotlinx a format-specific fact is written on the
domain type, so the type depends on every format it ever travels in, and two formats that disagree force a second copy
of the type. Here the fact is written on the format, which is the thing that knows it. The mechanism for that —
lambda-checked field references in a mapping held by the format — is EF Core's, and EF Core is the one prior system that
got this right.

## 12. Where this comes from

| System | Mechanism | Got right | Hurt | Taken |
|--------|-----------|-----------|------|-------|
| **serde** | `Serialize`/`Deserialize` × `Serializer`/`Deserializer` over a fixed 29-type data model, `#[derive]` | the data model as a narrow waist: N+M, zero cost, AOT-friendly, no reflection. The pull `Visitor` is what makes zero-copy possible | the data model is JSON-with-extras, so XML smuggles structure into field **names** (`@attr`, `$text`, `$value`); format-specific facts travel as struct/newtype names ([serde_json#505](https://github.com/serde-rs/json/issues/505)); attributes are silently format-dependent (`flatten` needs `deserialize_any`, so with postcard it compiles, serializes and then *returns wrong data*, [serde#2674](https://github.com/serde-rs/serde/issues/2674)); no schema without a value, hence a separate `schemars` that re-parses the attributes; `is_human_readable` is one boolean the type branches on and it does not survive wrappers ([serde#2704](https://github.com/serde-rs/serde/issues/2704)); the orphan rule forces `remote = "…"` shadow copies | the narrow waist, the pull decoder, N+M. **Rejected:** name smuggling, type-side attributes, `is_human_readable` (leniency is an option of the format, not a question a type asks) |
| **Swift Codable** | synthesized `CodingKeys` + container objects | the zero-config default: one word on the type | all-or-nothing customisation — renaming one property means writing out every `CodingKeys` case or the whole `init(from:)`; no per-format customisation on the type, so the escape hatch moved into the coder (`keyDecodingStrategy`), which then cannot express many key spellings; `any Encodable` does not satisfy a generic constraint | the zero-config default, and the lesson that "the coder decides the naming" is right but must not be the *only* escape hatch |
| **kotlinx.serialization** | `KSerializer` + **`SerialDescriptor`**: the structure reachable without a value | the descriptor. It is what makes `.proto` and JSON-Schema generation possible in one framework | the annotations are on the class, so the class depends on every format (`@ProtoNumber`, `@XmlSerialName`); the documented remedy is duplicate DTOs; `@SerialInfo` is limited to what annotation parameters allow | **`Describe` is their descriptor**, with the doc comment instead of an annotation and with the format-specific half moved off the type entirely |
| **Haskell autodocodec** | one codec *value*; encode, decode and JSON Schema fall out of it | one artefact, so the three cannot drift; a field's docstring is an argument to the combinator, so documentation cannot be forgotten | no derivation at all, by design, so verbosity scales with field count; the applicative chain pins field order and two same-typed fields in the wrong order still typecheck | "the three forms come from one place" — here that place is the constructor, so derivation stays |
| **Scala scodec** | `Codec[A]` as a composable value over `BitVector` | a codec can be *computed* — length-prefixed, tag-dispatched, conditional on an earlier field — which no attribute design can express | entirely manual; HList-shaped combinators give heavy types and poor errors; a codec is bound to one wire format | the reminder that step 5 of the ladder (write it by hand) must stay open and pleasant |
| **tapir `Schema[T]`** | a derived schema value, patched afterwards with `.modify(_.field)(…)` | derivation for the 95%, and lens-style post-hoc edits with **type-checked field paths** that never touch the class | implicit-scope management; recursive types need special handling; full auto-derivation is a compile-time cost | the shape of the mapping DSL: derived by default, adjusted by a type-checked field reference from outside |
| **Jackson mixins** | annotations on a separate class, matched onto the target by name | exactly the right instinct: mapping config outside the domain type, so a third-party class can be configured | matching is by name and signature only, so a renamed member silently stops applying; registration is imperative and per-`ObjectMapper` | the instinct, with the checking Jackson gave up: a quoted expression instead of a string |
| **EF Core fluent mapping** | a mapping DSL held by the context, field references as lambdas | type-checked, refactor-safe, IDE-navigable field references, no annotations, no strings, strictly more expressive than the attribute form | verbose; the configuration sits far from the class; nothing forces completeness; illegal lambda shapes fail at run time | **the model**: `Xml.format { map<Price> { attribute { _.currency } } }` is `modelBuilder.Entity<Blog>().Property(b => b.Url)`. Completeness is forced here, because `Xml.format` validates against `Describe` |
| **Elm decoders** | `Decoder a` as a plain value, `mapN`, no derivation | the purest "a codec is a value": composable, testable, every failure is data | `map8` is the ceiling; positional `mapN` transposes same-typed fields silently; encode and decode are two programs for one format and drift apart | the warning: encode and decode must come from **one** declaration, which is why all three forms are derived from the constructor and never written separately unless a human insists |

## 13. What the language and the compiler must provide

Ordered by how much the design depends on it. Each with the smallest change that would do.

| # | What is missing | Smallest change | The design needs it for |
|---|-----------------|-----------------|-------------------------|
| 1 | **A trait method with type parameters of its own, monomorphized.** `witness.trb` refuses a derived `encode` because `Encoder`'s members are all `var fn`s and `Decoder.record<Output>` is not object safe. **Partly closed:** a member with type parameters of its own is called through a trait-typed value as one slot of the table per list of arguments the program calls it with (`genericSlotOf` in `ir/witness.trb`; `std/stream`'s `Stage.onto<Final>`, `tests/conformance/generic-trait-members.trb`), so `record<Output>` has a slot now. **Closed for the derived forms:** `encode`, `decode` and `describe` are generated as one body per (type, format) pair and called directly, never through a table (`ir/lower/encoding.trb`; `tests/conformance/encoding-round-trip.trb`) | emit `Encode.encode<Target>` as a monomorphized call per (type, encoder) pair instead of a witness entry. The trait is never a type, so no table is needed | everything. Without it no format runs in a compiled binary |
| 2 | **A static trait member reached through a bound at run time.** `Value.decode(source)`, `Item.describe target`. **Closed in the native back end:** a call through a bound is monomorphized (`docs/design/URI.md`, probe 5), and a generic one taken as a function value - `structureOf(Order.describe)` - is instantiated from the function type it is used as, through the implementation the checker named (`lowerNamedFunctionValue`; `examples/encoding-lab/tests`, `tests/conformance/methods-through-types.trb`) | both back ends: monomorphize the call. Stage 0: it has no types at run time, so a derived `decode` of a generic type cannot run there at all — which is a reason to finish the self-hosted back end, not to change stage 0 | the decode and describe side of every generic type (`List<Item>`, `Option<Value>`, `Page<Item>`) |
| 3 | **`typeName<Type>()`**, a compile-time constant | a checker intrinsic that folds to the declaration's qualified name. CONCEPT already names it under Quoted Expressions | `map<Price> { … }` in every format's DSL. Without it a mapping is keyed by a string the user types |
| 4 | **Quoted expressions at run time**, and `nameOf` over them | stage 0 support for `Expression<Value>` parameters other than `assert`'s (it special-cases `assert` and hands a plain closure to everything else) | `attribute { _.currency }`. Without it a mapping names a field with a string, and the check moves from compile time to format-build time |
| 5 | **`UInt8.tryFrom(Int64)` and `String` from bytes.** **Closed:** `std/number` narrows an `Int64` into every smaller width and a `UInt64` into an `Int64` through the runtime's checked conversions, and `std/stream`'s `textOf(bytes)` decodes UTF-8 in TorbScript - both build natively (`tests/conformance/narrowing.trb`) | two conversions in `std/number` and `std/text`, plus their natives | **any** byte-oriented format. The lab's packed format uses `List<Int64>` as its byte type because a `UInt8` cannot be made from a number at run time, and a decoded string cannot be rebuilt from bytes |
| 6 | **Bit access to a `Float64`** (`bits()`/`fromBits()`) | two natives on `Float64`/`Float32` | IEEE-754 in a binary format. The lab writes a float as its decimal text |
| 7 | **Extension visibility, and overload resolution over a type parameter** | a trait's extensions are visible where the trait is (already decided, TODO "Extension-Sichtbarkeit"); and overload resolution must unify a type parameter instead of comparing type ids | writing the new vocabulary next to the old one at all. Two `Encode`s on `Float64` make an overload set the checker rejects |
| 8 | **`by value` for a method with a `var` parameter** | delegation forwards the `var` path, not a copy. Stage 0 drops the change silently today | step 3 of the ladder |
| 9 | **The doc comment of a field, at run time**. **Closed:** the derived `describe` embeds each field's doc comment as a text constant (`tests/conformance/encoding-describe.trb`) | the checker already has it in the syntax tree; the derived `describe` embeds it as a string literal | `FieldDescription.documentation`, and therefore every `--help` text and every schema description |
| 10 | **A method and a case field must not share a name** | a diagnostic: `Structure.fields` as a case field and `fields()` as a method type check today, and stage 0 resolves the field | nothing in the design; found while writing the lab, and it is a hole in "a type has one namespace of members" |

## 14. Migration

Eight slices. Each one lands with the repository checking green, `torb test` passing and `canon --check` clean.
Slices 1 to 6 landed together: the flat vocabulary makes the old `std/json` natives unwritable, and a derived form needs
the new vocabulary to exist, so none of them is green without the others.

| Slice | Status |
|-------|--------|
| 1 the vocabulary | **done** - `std/encoding/src/lib.trb`; every hand-written implementation in `std/`, `examples/` and `docs/` moved |
| 2 the derivation rule | **done** for the rule (`isPassable` in `semantics/checker/derive.trb`). **Open:** the call-site message does not name the field chain yet |
| 3 `Describe` | **done** - derived, generated natively, doc comments embedded, a constant default written as data. A tuple has no `Describe` (it has no name and no field names) |
| 4 `EncodedValue` and `Structure` | **done** - `std/encoding/src/values.trb`, `structure.trb`; `rendered` replaces the `describe` native |
| 5 `Expression.captures()` | **done** in the declarations; the natives behind it are still the planned ones of `Expression` |
| 6 `std/json` in TorbScript | **done** - no native left; the planned `Json.*` and `describe` rows are gone from the manifest |
| 7 `with Encode by value` | **open** - `by` delegation is not built natively at all yet |
| 8 `std/xml` | **open** |

**Slice 1 — the vocabulary.** `std/encoding`: `Encoder` and `Decoder` become one flat trait each, `Encode`/`Decode` take
a bound instead of a trait-typed parameter, the six sub-traits go, the prelude's export list follows. `std/json`'s
natives change signature with it. Nothing derives yet; the hand-written implementations in `std/` are rewritten. Gate:
the repository checks, the JSON tests pass.

**Slice 2 — the derivation rule in the checker.** `derive.trb`: the condition becomes "the constructor is usable from
outside", for `Encode` as well as `Decode`; `everyFieldImplements` walks only the passable parameters, so a `private`
field with a default no longer blocks and no longer appears. The call-site diagnostic names the field chain. Gate: new
checker tests with the exact messages.

**Slice 3 — `Describe`.** A new well-known trait next to `Encode`/`Decode`, derived under the same condition, with
`FieldDescription`, `FieldDefault`, `Describer`. The derived body is generated in lowering and by both back ends. The
field's doc comment is embedded as a literal. Gate: checker tests, a back-end test per shape.

**Slice 4 — `EncodedValue` and `Structure`.** Both in `std/encoding`, with the two `Describer`/`Encoder`
implementations that build them (`Values`, `Structures`) and the shared `ValueDecoder`. `describe(value)` becomes
`rendered(value)` and stops being a native; the prelude exports `EncodedValue`. Gate: `torb test`, and one native fewer in `runtime/`.

**Slice 5 — `Expression.captures()`.** It answers `List<EncodedValue>`; `assert` reads it; the "captured variables must
be `Encode`" rule stays exactly as it is, because `EncodedValue.of` needs `Encode`. Gate: the expression tests.

**Slice 6 — `std/json` in TorbScript.** The encoder, the parser and the framer on the new vocabulary, with an optional
native fast path. `Json.value` becomes `EncodedValue.of`. Gate: the JSON tests, the stream tests.

**Slice 7 — `with Encode by value` as the wrapper form**, and the well-known types of `std/` that use it (`Instant`,
`Duration`, `Decimal`-shaped wrappers). Gate: a round-trip test per wrapper, and a format-side test that reads the
type name.

**Slice 8 — `std/xml`.** `XmlNode` as the tree, `Xml.format { map<T> { … } }` as the DSL. `XmlEncode` and `XmlDecode`
are never written.

**The prose that has to change with it**, named by the page the `docs check` gate points at:

| Page | What changes |
|------|--------------|
| `docs/language/reflection/encode-and-decode.md` | the two traits become three; the derivation rule; the wrapper form |
| `docs/language/reflection/encoders.md` | the whole vocabulary: one trait per direction, `field`/`finish`, the bound |
| `docs/language/reflection/no-reflection.md` | `typeName<Type>()` joins the syntactic bridges |
| `docs/explanation/why-no-reflection.md` | "one generated trait pair" becomes three forms of one constructor; the rejected `Data` tree is contrasted with `EncodedValue`; the "a format with a document model of its own gets its own traits" paragraph is replaced by mappings |
| `docs/standard-library/encoding.md` | the vocabulary, `Describe`, `Structure`, `EncodedValue` |
| `docs/standard-library/json.md`, `docs/how-to/read-and-write-json.md` | `Json()` is a value with options; `json.encode value` |
| `docs/language/functions/quoted-expressions.md` | `captures()` answers `List<EncodedValue>` |
| `CONCEPT.md`, "Types, Values and Reflection" | the principle, the three traits, the derivation rule, the removal of the `XmlEncode` paragraph |
| `CONCEPT.md`, Decision Log | one entry: a value is its constructor call, and format-specific facts live in the format |
| `docs/design/STREAMS.md`, section 10 | `Format`'s two whole-value methods are renamed to `encode`/`decode`; the framing half is unchanged |

**What `assert` and `describe` become.** `assert` keeps its signature and reads `List<EncodedValue>` instead of
`List<Encode>`; its message does not change. `describe` becomes `rendered` (section 15, decision 1):
`fn rendered<Value: Encode>(value: Value): String`, written in TorbScript over `EncodedValue.show()`.

## 15. What the owner decided

Everything technical in this document is decided by the design; these four were questions of taste or direction, and
the owner answered them.

1. **The trait is `Describe`.** The rule gives the name (a single-method trait is named like its method). The free
   function that renders a value as text for a message is `rendered(value)`, so the package has no `describe` that
   means two things.
2. **`EncodedValue` is exported by the prelude.** `assert`, quotations and every schema default depend on it. It is an
   ordinary ADT built by an ordinary format, exactly like `JsonValue`, and nothing goes through it unless a program
   asks for a value without its type.
3. **A format may reject an unknown field, as an option of the format, and the default is tolerant.** The rule is the
   same for every format of the standard library: `strict: true` reports an input field no type asked for
   ("this document had a `discount` and nothing read it"); without it the field is ignored.
4. **"A record as a flat map of paths" lives in `std/encoding`**, as a convenience next to `Structure`, because every
   tabular, row-shaped or flag-shaped format wants it. It is not vocabulary. Not written yet: no format of the standard
   library needs it before `std/xml`, and the first tabular one writes it.

## 16. What the implementation decided

Technical choices the design left open, decided while building slices 1 to 6.

1. **A derived form is a straight line of steps.** The compiler generates only the shape - which parameters, in which
   order, which case, where a default is taken - and every step is a public generic function of
   `std/encoding/src/derived.trb` (`encodeField`, `decodeRequired`, `decodeHasField`, `describeField`, ...),
   instantiated for the field's type and the format. What a derived form does can therefore be read in TorbScript, and
   a hand-written form can be made of the same pieces.
2. **`typeName` is spelled** package name, module path inside the package without `src/` and `.trb` (an entry module,
   `lib` or `main`, left out), then the type: `"torbscript/example-tour/11-data/User"`, `"std/time/Instant"`.
3. **`EncodedValue` has no `Exact` case yet.** The native runtime has no `Decimal` (its natives are planned), and a
   `Decimal` field in a case makes the whole type unbuildable - so every program that renders a value would be refused.
   An exact number is held as its text, which is exact, and `ValueDecoder.decimal` reads it back. The case comes with
   the runtime's `Decimal`.
4. **`EncodedValue` writes itself and is not read back as itself.** The pull vocabulary has no question "what comes
   next", which reading an arbitrary value needs; its derived `Decode` reads the case-tagged form a derived `encode`
   would write. A document without a type is read with a format's own `parse`.
5. **`Format` keeps `encodeAll`/`decodeAll`.** A text format's own `encode` answers its text (`Json().encode(order)` is a
   `String`), and one member namespace cannot hold that and the trait's byte-level `encode` under one name. The members
   stay static and use the format's default options; a `Json` value with options has its own `encode` and `decode`.
6. **`Json` is a value** (`Json()`, `Json(naming: .SnakeCase, strict: true)`). `JsonValue` stays JSON's own tree
   (step 6 of the ladder, `json.parse`), and `json.value(x)` is a value as that tree; `EncodedValue.of` is the
   format-free one.
7. **JSON's numbers.** A number without a fraction or an exponent is read as a whole number, one beyond `Int64` as an
   unsigned one, and everything else as a `Float64`, so the whole range of `Int64` and `UInt64` round-trips exactly.
   Infinity and "not a number" have no JSON and are written as `null`. A map's keys are texts: a number key is quoted,
   and `ValueDecoder` reads a text key as whatever the key type asks for.
8. **`Naming` is shared** and lives in `std/encoding`, with the two spellings the ladder names: `Unchanged` and
   `SnakeCase`. It spells field names; case names are written as declared.
9. **A capsule without a pair** keeps `Encode` field by field, over every field, and has neither `Decode` nor
   `Describe`: a description of a closed constructor would describe something nobody can build.
10. **A tuple has `Encode` and `Decode`, as a sequence of its positions, and no `Describe`**: `Structure` has no shape
    without a name and without field names.

### Open, found while building it

- **The call-site message of slice 2** still names the one field and not the chain (`User.avatar → Image.palette`).
- **A literal type decodes natively as its base, without the check.** A generic instance is keyed by the IR type, and a
  literal type is its base there, so `decode<Level>` is `decode<String>`; the checker's `decodeLiteral` path is never
  reached by a binary. Keying an instance by the checker type, or lowering a literal type's members through its own
  `tryFrom`, closes it (the literal type's `tryFrom` itself is not built natively either).
- **Two members of `extend`s of two traits over one item, in one module, share a C name.** `List<Item>.decode` and
  `Set<Item>.decode` were one symbol: the name of a member of an `extend` of a trait carries its module, its name and
  its type arguments, but not the trait it extends (`symbolPathOf` in `ir/instantiate.trb`). `std/encoding` keeps
  `Set`'s implementations in `src/set.trb` until the name carries the extended trait.
- **A checker false negative:** an implementation written with the old signature (`fn encode(var encoder: Encoder)`)
  is accepted as `Encode`'s `fn encode<Target: Encoder>(var target: Target)`; the requirement check does not compare a
  member's own type parameters.
- **`Decimal` in a format**: `Values.decimal`, `ValueDecoder.decimal` and `JsonEncoder.decimal` call the planned
  `Decimal` natives, so they are the three findings `ir --statistics .` counts for encoding until the runtime has one.
