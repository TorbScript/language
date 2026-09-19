# The Back Ends (Milestones 5-7)

The typed IR, the C back end with its runtime, self-compilation, and the bytecode VM with tasks and the sandbox.
Everything here consumes what the type checker produced ([docs/TYPECHECKER.md](TYPECHECKER.md) section 7.1) and nothing
here decides a question of the language: the IR *is* the semantics, and two back ends read the same IR. That is what
design principle 5 rests on ("Nothing observable may differ between interpreter and compiled binary").

The order of everything below follows one goal, the **fixpoint**: stage 1 (the compiler on stage 0) compiles
`compiler/` to C, a C compiler builds `torb`, that `torb` compiles `compiler/` again, and both C outputs are byte
identical. Features the compiler itself does not use come after that gate, not before it. Verified by grep:
`compiler/` contains no `shared type`, no `spawn`/`await()`/`Task`, no `Sandbox`, no named collection implementation
(only `[...]` and `[:]` literals), 189 `List<`, 19 `Map<`, 6 `Set<` and no `Float` arithmetic at all.

```text
CheckedProgram ─► lowering ─► typed IR ─┬─► C source ─► clang/gcc/cl ─► native binary     (torb build, torb run)
                                        └─► bytecode ─► VM (written in TorbScript)        (torb run, sandbox, repl)
```

## 0. What is decided where

| Decided once, in the lowering                                  | Left to a back end                                  |
|----------------------------------------------------------------|-----------------------------------------------------|
| Monomorphic instance set, instance keys, mangled names          | Nothing - names are in the IR                       |
| Layouts and the representation class (inline / boxed / niche)   | Field offsets (C compiler), slot sizes (VM)         |
| Witness tables and which call passes one                        | Nothing                                             |
| Last use, moves, `Retain`/`Release`/`MakeUnique`, drop points   | Whether a no-op retain is skipped (both do)         |
| `match` plans to decision trees                                 | `Switch` as a C `switch` or a jump table            |
| Closure conversion, environment layout, escaping or not         | Stack or heap environment (both follow the IR flag) |
| State-machine transformation for tasks                          | The scheduler's queue implementation                |
| Drop of every temporary, incl. `return`/`break`/`?` paths       | Nothing                                             |
| Evaluation order, panic messages, overflow checks               | Nothing                                             |
| Static data: compile-time constants, `Expression` trees         | Encoding (C initializers vs. constant pool)         |

Constraints that shape every decision: the lowering runs on **stage 0**, an untyped tree walker - near-linear
algorithms, all program-wide data in one `var program: IrProgram`, ids instead of references, no recursion deeper than
the source nests. And every emitted artifact is **sorted before it is written**, so the output never depends on
traversal order (section 3.6).

---

## 1. The IR

### 1.1 Shape: basic blocks over numbered slots

**Decision: a control flow graph of basic blocks holding three-address instructions over numbered slots, not a
structured tree and not SSA.**

- **Because of the VM.** A register machine's bytecode is a near 1:1 encoding of this form: a slot is a register, an
  instruction is an opcode plus slot indices. A structured tree would need a second lowering for the VM, and then the
  two back ends would no longer read the same thing.
- **Because of C.** C has `goto` and labels, so a CFG maps directly; the loss is readability, which `#line` directives
  and one comment per block repair (section 3.6). A structured tree would read better in C and buy nothing else.
- **Because of the tasks.** The state-machine transformation (section 5.3) is a CFG transformation. On a tree it would
  need a CFG first.
- **Because of last use.** Backward liveness over a block graph is 40 lines and near-linear (section 2.2). On a tree,
  loops need the same fixpoint anyway, in a shape that is harder to iterate.
- **Not SSA.** Phi nodes would buy optimization the C compiler already does, and cost a destruction pass before C and
  before bytecode. Slots are mutable, assigned by the lowering, and never reused for a different type.

```trb
public type IrProgram {
  var functions: List<IrFunction> = []
  var layouts: List<Layout> = []
  var witnessTables: List<WitnessTable> = []
  var elements: List<ElementDescriptor> = []
  var statics: List<StaticValue> = []
  var locations: List<Location> = []
  /** Instance key to FunctionId: the memo of the monomorphization worklist. */
  var instances: Map<String, FunctionId> = [:]
  var pending: List<Instance> = []
}

public type IrFunction {
  name: String                      // The mangled name. Unique, deterministic, the only identity a back end needs.
  origin: SymbolId                  // For messages and `torb ir`
  signature: IrSignature
  var slots: List<SlotDeclaration> = []
  var blocks: List<Block> = []
  /** A function whose body is a state machine over `Task<Value>` (section 5.3). */
  kind: FunctionKind = .Plain
}

public type Block {
  var instructions: List<Instruction> = []
  terminator: Terminator
  /** The source construct this block came from. Only a comment in the output. */
  label: String
}

public type SlotDeclaration {
  type: IrTypeId
  role: SlotRole                    // .Parameter(index), .Local, .Temporary, .Environment, .Result
  /** A name from the source, for `torb ir` and for `#line`-friendly C identifiers. Never load bearing. */
  name: String
}
```

### 1.2 Instructions and terminators

```trb
public type Instruction {
  case Constant(target: Slot, value: StaticId)
  /** Ownership moves: the source is dead afterwards. Inserted by section 2. */
  case Move(target: Slot, source: Slot)
  /** Ownership is shared: a retain for a managed type, a plain copy otherwise. */
  case Copy(target: Slot, source: Slot)
  case Retain(slot: Slot)
  case Release(slot: Slot)
  /** Before every write through a counted owner on a path. A no-op when the count is 1. */
  case MakeUnique(reference: Reference)

  case Call(target: Slot?, callee: FunctionId, arguments: List<Argument>)
  /** A closure value, a function in a field, a `lazy` force. */
  case CallClosure(target: Slot?, callee: Slot, arguments: List<Argument>)
  /** A member of a witness table: trait-typed receiver, or a forwarded witness. */
  case CallWitness(target: Slot?, witness: WitnessSource, member: Int, arguments: List<Argument>)
  /** Arithmetic, comparisons, casts, tag reads, string building: what the C back end emits inline. */
  case Intrinsic(target: Slot?, intrinsic: Intrinsic, arguments: List<Slot>, at: LocationId)

  case Construct(target: Slot, layout: LayoutId, variant: Int, fields: List<Argument>)
  case Read(target: Slot, reference: Reference)
  case Write(reference: Reference, value: Argument)
  /** The variant index of a value of a variant layout. */
  case Tag(target: Slot, source: Slot)

  case Closure(target: Slot, function: FunctionId, captures: List<Capture>, isEscaping: Bool)
  case BoxNew(target: Slot, value: Argument)
  case TraitValue(target: Slot, value: Argument, tables: List<WitnessSource>)

  /** `var` path through a non-contiguous container: take out, change, put back (section 1.5). */
  case TakeOut(target: Slot, container: Reference, key: Slot, kind: ContainerKind)
  case PutBack(container: Reference, key: Slot, value: Slot, kind: ContainerKind)

  case Panic(message: Argument, at: LocationId)
  /** Only in a task body (section 5.3). */
  case Suspend(state: Int, awaited: Slot)
}

public type Terminator {
  case Jump(target: BlockId)
  case Branch(condition: Slot, then: BlockId, otherwise: BlockId)
  /** Variant tags, integer and char literals. Dense or sparse is the back end's business. */
  case Switch(scrutinee: Slot, values: List<Int>, targets: List<BlockId>, otherwise: BlockId)
  case Return(value: Argument?)
  /** `panic`, `Process.exit`, a `Never` call, an exhausted decision tree. */
  case Unreachable
}

/** How one operand is consumed. Filled in by the ownership pass (section 2). */
public type Argument {
  slot: Slot
  ownership: Ownership              // .Borrowed, .Owned
  isLastUse: Bool = false
}

/** A place: the base slot plus a static path of steps. Never counted, never escapes (section 2.5). */
public type Reference {
  base: Slot
  steps: List<PathStep>
}

public type PathStep {
  case Field(layout: LayoutId, index: Int)
  case Variant(layout: LayoutId, variant: Int, index: Int)
  case TupleField(layout: LayoutId, index: Int)
  /** `Array<Item, Size>` and the contiguous list: an interior pointer after `MakeUnique`. */
  case Element(item: IrTypeId, index: Slot)
  /** Dereference a boxed layout, a `Box`, or a `shared` object. */
  case Contents(layout: LayoutId)
}
```

There are **no cleanup edges and no landing pads**: there are no exceptions, and a panic aborts without running
anything (gap 9). Every drop is an explicit `Release` in a block.

### 1.3 Types and layouts

After monomorphization every type in the IR is concrete.

```trb
public type IrType {
  case Integer(width: Int, isSigned: Bool)
  case Float(width: Int)
  case Bool
  case Char
  case Void
  case Text                                        // String: storage plus offset plus length
  case Record(layout: LayoutId)
  case Variant(layout: LayoutId)
  case Tuple(layout: LayoutId)
  case FixedArray(item: IrTypeId, size: Int)
  case Closure(signature: SignatureId)
  /** A trait-typed value: the boxed payload plus one witness pointer per bound. */
  case Object(bounds: List<TraitId>)
  /** A `shared type` object: counted, with a trace function, may be in a cycle. */
  case Shared(layout: LayoutId)
  /** A captured `var` binding. Counted, traced, may be in a cycle. */
  case Box(item: IrTypeId)
  /** `lazy Value` as a parameter: a one-shot memo cell. */
  case Lazy(item: IrTypeId)
  /** What the runtime owns: ArrayList, the map, the set, Task, Channel, Expression, Decimal. */
  case Runtime(kind: RuntimeKind, arguments: List<IrTypeId>)
}

public type Layout {
  name: String                                     // Mangled; also the C type name
  fields: List<FieldLayout>                        // Variant layouts: one group per variant
  variants: List<VariantLayout>
  representation: Representation
  /** Computed to a fixpoint over the field graph. Drives the `spawn`/`Channel` rules and the cycle collector. */
  containsShared: Bool
  containsCounted: Bool
}

public type Representation {
  /** Copied, no count. All fields inline, estimated size at most 32 bytes, not recursive. */
  case Inline
  /** Reference counted block. Big types and every type that is in a cycle of the field graph. */
  case Boxed
  /** `Option<P>` where the payload of `Some` is one pointer-like field: `None` is the null pointer. */
  case Niche(variant: Int, field: Int)
}
```

- **The representation class is in the IR, not in a back end.** It decides where a `Retain` is needed, so the two
  back ends may not compute it separately. Size is estimated with a fixed model (`Int8` 1 ... `Int64` 8, `Float32` 4,
  `Float64` 8, `Bool` 1, `Char` 4, pointer 8, C-like alignment padding); the actual layout is the C compiler's
  business, because it is not observable.
- **Recursive types are boxed.** A strongly connected component of the field graph with more than one edge, or a
  self edge, makes every layout in it `Boxed` - that is what "Recursive ADTs (trees): reference counted nodes" in the
  Execution Model means. `type Node { next: Node }` never gets here: the checker reports it (TYPECHECKER 1.3).
- **The niche.** `Option<T>` where `T` is `Boxed`, `Shared`, `Box`, `Closure` with an environment, `Runtime` or
  `Text` (its storage pointer) uses the null pointer for `None`, so `Option` of a list costs nothing. `Option<Int>`
  is a tagged inline record. A nested `Option<Option<T>>` cannot use the niche twice and falls back to a tag - which
  is fine, because gap 12 removed the only way `?.` produced one.
- **Named tuple labels do not exist in the IR.** Gap 17 made them not part of type identity, so `(lowest: Int,
  highest: Int)` and `(Int, Int)` are one layout.
- **Literal types erase to their base type.** `"online" | "offline"` is `Text`; `match` on one is a string comparison
  chain (or a length-then-bytes switch, an emitter detail).
- **Aliases and `Never` are gone.** `Never` appears only as the result of a function, which is `_Noreturn` in C.

### 1.4 Generics: monomorphization and witnesses

The checker already decided which is which (TYPECHECKER 4.4): a call whose `Witness` tree contains no `.Object` and
no `.Forwarded` that reaches an `.Object` can be monomorphized. The lowering follows that and never re-decides.

- **Worklist.** Roots are the entry file's top-level code, and for `torb test` every test module's top-level code.
  An instance is `(SymbolId, typeArguments, witnessArguments)`; `instances` maps its canonical key string to a
  `FunctionId`. Nothing unreachable is emitted - "Generated code only exists where it is used" (Reflection section).
- **Code is shared where a witness is passed.** A generic function that is reached with a `.Forwarded` witness that
  bottoms out in an `.Object` is instantiated **per bound, not per type**: the type argument is `Object([Show])`, so
  `fn describe<Value: Show>(value: Value)` has one instance for every trait-typed argument. That is the whole
  dictionary-passing path, and it is what keeps `List<Show + Hash>` from exploding the instance count.
- **Witness tables** are static data: one table per `(ImplementationId, typeArguments)`, holding a function pointer
  per required member of the trait **in declaration order**, then the supertraits' tables in declaration order, then
  the witnesses of the trait's own generic parameters. Default members are *not* in the table: a call of a default
  member is a call of the monomorphic instance of the default body with the table passed in. Gap 14 (`by` forwards
  the required members only) falls out of this for free.
- **Derived implementations** (`Show`, `Equals`, `Hash`, `copy`, `Encode`, `Decode`, `From` of a wrapping case, the
  members of a literal type) have no source. The lowering generates their bodies from the layout, one instance per
  monomorphic type, keyed by `(DerivedId, type)`. `derive.trb` in the checker says *which* members; `lower/derive.trb`
  says what they do.
- **Const generic arguments** are part of the key and become integer/text constants in the body. `Array<Float, 16>`
  and `Array<Float, 16>` are one instance.

### 1.5 Mangling

Deterministic, ASCII, collision free, and stable under anything that does not change the program:

```text
name      ::= prefix "_" package "_" module "_" member ( "__" instance )?
prefix    ::= "t" function | "T" layout | "w" witness table | "d" element descriptor | "s" static value
component ::= escaped  ( [A-Za-z0-9] kept, everything else "_xHH_" of its UTF-8 bytes )
instance  ::= the canonical type argument list, escaped, at most 160 characters,
              plus "_h" and 16 hexadecimal digits of its FNV-1a-64 when it was longer
```

`t_std_x2f_prelude_collections_x2f_list_ArrayList_add__Int64`. Ids never appear in a name: a name is a function of the
*source path plus the type arguments*, so it does not move when an unrelated module is added. C reserved spellings
cannot be hit (every name starts with a lowercase prefix letter and never contains `__` except as the instance
separator - which is why the separator is checked against the escaped components, and `_x5f_` is how a source
underscore is written when it would create one).

### 1.6 How every construct is lowered

| Source                       | IR                                                                                                      |
|------------------------------|---------------------------------------------------------------------------------------------------------|
| `fn f(a: Int)`               | `IrFunction` with parameter slots in declaration order; `self` first                                      |
| `var x = e`                  | A local slot. A `var` that some closure captures becomes `Box(T)` and every access a `Contents` step       |
| `var p: Counter` parameter   | `Reference` in, `PathStep` on top of it. In C a `T *`, in the VM a (frame, slot, path) triple              |
| `world.entities[id].health = 1` | `MakeUnique` per counted owner outermost first, then `Write` through one `Reference`                     |
| `map[key].add(x)`            | `TakeOut` / call / `PutBack` when the container is not contiguous; `Element` step when it is                |
| `samples[1..4].sort { _ }`   | A `Reference` with an `Element`-range step after `MakeUnique`: a window, no copy                            |
| closure                      | `Closure(function, captures, isEscaping)`. `const` captures by `Copy`, `var` captures as a retained `Box`, a `var` reference only when `isEscaping` is false |
| receiver closure             | An ordinary closure whose first parameter is the receiver, `Reference` for `(var self: R)`                   |
| `lazy Value` argument        | `BoxNew` of a `Lazy(T)` cell holding a capture-free-or-not closure; each use is `Intrinsic.LazyForce`        |
| `match`                      | `MatchPlan` to a decision tree of `Switch`/`Branch`/comparison blocks, tests memoized so the DAG is shared  |
| `e?`                         | `Tag` + `Switch`; the error arm calls the recorded `From.from` and `Return`s after releasing everything live |
| `for x in xs`                | `xs` into a temporary, `Iterable.iterator()`, loop head calls `next()` on a local `var`, `Switch` on the `Option` |
| `"{a} and {b}"`              | `Show.show`/`showNested` per part, then one `Intrinsic.TextConcat(parts)` - never repeated `String.add`      |
| defaults                     | `Adaptation.DefaultArgument`: the default expression is lowered **at the call site**, after the written arguments |
| variadics / `...e`           | Build a list at the call site; `Spread` calls `addAll` through the recorded `Iterable` witness                |
| `port 8080`, `db { ... }`    | `Resolution.PropertyWrite`: `.Assign` a `Write`, `.AssignClosure` a `Closure` then a `Write`, `.Configure` a `Reference` to the field passed to the closure |
| `p.copy(y: 30)`              | `Construct` from the old fields plus the overrides; when the source is at its last use and `Boxed`, `MakeUnique` plus `Write` instead - this is how `p = p.copy(y: 30)` stays in place |
| `with Show` derived          | A generated body per instance (section 1.4)                                                                 |
| top-level code of `main.trb` | One `IrFunction` `t_main`, statements in source order; `Process.arguments()` is a runtime call               |
| top-level `const` of a module | A `StaticValue`, evaluated by the const evaluator. There is no module initialization, ever                   |
| `panic "x"` / overflow       | `Panic` / `Intrinsic.AddChecked` etc., each with a `LocationId`                                              |
| `using r { }`                | Nothing special: an ordinary call with a `Reference` argument and a closure                                  |

### 1.7 Text format

One deterministic text form, used by `torb ir <path>`, by `torb build --emit-ir` and by the snapshot tests.

```text
layout T_main_Point inline fields(x: Int64, y: Int64)

fn t_main_translated(self: Record(T_main_Point) borrowed, deltaX: Int64, deltaY: Int64) -> Record(T_main_Point) {
  slot %0 parameter self: Record(T_main_Point)
  slot %1 parameter deltaX: Int64
  slot %2 parameter deltaY: Int64
  slot %3 temporary: Int64
  slot %4 temporary: Int64
  slot %5 temporary: Record(T_main_Point)
b0: # main.trb:19 body
  %3 = intrinsic add.i64 %0.x, %1 at main.trb:20:11
  %4 = intrinsic add.i64 %0.y, %2 at main.trb:20:26
  %5 = construct T_main_Point (%3 owned, %4 owned)
  return %5 owned
}
```

Rules that make it a good snapshot: blocks in the order the lowering created them, slots in declaration order,
one instruction per line, `owned`/`borrowed`/`last` written out on every argument, source locations as
`path:line:column` with forward slashes relative to the workspace root, and nothing else on a line. A test that
compares this text catches an ownership regression far earlier than a C-level test can.

---

## 2. Ownership in the IR

### 2.1 The rules

| Kind of value                                      | Counted | Written where                                    |
|----------------------------------------------------|:-------:|--------------------------------------------------|
| `Integer`, `Float`, `Bool`, `Char`, `Void`, `FixedArray` of such, `Inline` layout of such | no | copied, never touched |
| `Text`, `Runtime` (list, map, set, Task, Channel), `Boxed` layout, `Object`, `Closure` with an environment | yes | `Retain`/`Release`/`MakeUnique` |
| `Shared`, `Box`                                    | yes, plus a color for the cycle collector | as above |
| `Reference` (`var` parameter, `var self`, `var` path) | **no**  | section 2.5                                      |

- **A slot has exactly one owner.** Every operand position is `Borrowed` (the value must be live at that point; no
  count changes) or `Owned` (the position takes the count).
- **Parameters are borrowed by default.** The caller keeps the count for the whole call. That removes a
  retain/release pair from every call, which dominates in a compiler that passes `Token`s and `String`s everywhere.
- **A parameter is `Owned` when the callee stores the value.** Two sources, no interprocedural analysis:
  1. The natives manifest declares it (`ArrayList.add`, `Map.set`, `Channel.send`, `ArrayStack.push`, ...).
  2. `Construct`, `BoxNew`, `TraitValue` and `Return` are always `Owned` positions.
  3. A **summary** per function, computed from the already lowered body (section 2.2, phase 2): a parameter whose
     only use is a `Copy` into a slot that is later `MakeUnique`d or returned is `Owned`.
     Rule 3 is exactly what makes the participles free: `fn added(self, value: Item): Self { var result = self  result.add(value)  result }`
     receives `self` owned, so at a call site where the receiver is dead the `Copy` is a `Move`, the count stays 1,
     `MakeUnique` is a no-op, and `list = list.added(x)` changes in place. This is the promise of
     [docs/ARCHITECTURE.md](ARCHITECTURE.md) ("Last use is a move"), and it is a property of the IR, not of a back end.
- **Last use is a move.** At an `Owned` position whose operand is not live afterwards: `Move`, no retain. At an
  `Owned` position whose operand is live afterwards: `Copy`, a retain. At a `Borrowed` position whose operand is not
  live afterwards: borrow, then `Release` after the instruction.
- **Make unique before every write.** For each `PathStep` whose owner is counted, `MakeUnique` outermost first. At
  runtime it is `if (count == 1) { } else { shallow copy, retain the children, release the old }`. A static value's
  count is the immortal sentinel, so a write to a static always copies.
- **Getting a wrong answer costs speed, never correctness.** A missing `Owned` costs one copy, a superfluous one
  costs one retain. So the summary pass may be a heuristic, and it is run exactly once (no fixpoint between
  functions), which keeps the lowering order independent.

### 2.2 Last-use analysis

Three linear passes over the whole IR, in this order. No pass depends on the order in which functions were lowered.

**Phase 1 - lower.** Emit blocks and instructions with every argument `Borrowed`, no `Copy`, no `Release`, no
`MakeUnique`. Explicit `MakeUnique` for writes is emitted here (it is a property of the path, not of liveness).

**Phase 2 - summaries.** Per function, in one pass over its blocks: for each parameter, is its only occurrence a
`Copy`/`Move` whose target is later written through or returned? If yes, mark the parameter `Owned` in the function's
`IrSignature`. Local, no recursion, no ordering.

**Phase 3 - liveness and insertion.** Per function:

1. Compute successors from the terminators, then predecessors. Order the blocks in reverse postorder; the reverse of
   that order is the iteration order for a backward analysis.
2. Live sets are bitsets over the managed slots only (a `List<Int>` of 64-bit words; trivial slots are never in a
   set, which keeps the sets small in practice).
3. Iterate to a fixpoint:
   `liveOut(B) = union over successors S of liveIn(S)`,
   `liveIn(B) = uses(B) union (liveOut(B) minus definitions(B))`,
   where `uses(B)` and `definitions(B)` are computed by walking B's instructions backwards.
   A **use is any operand occurrence, borrowed or owned** - the set means "this frame still owns the value here".
   Source is structured, so the CFG is reducible and reverse postorder converges in `loop nesting depth + 1`
   iterations; the implementation iterates until nothing changes and asserts the bound in a test.
4. Walk each block backwards with `live = liveOut(B)`. For each instruction, for each operand **in reverse of
   evaluation order** (so that `f(x, x)` marks only the second occurrence as the last use):
   - if the slot is in `live`: `Owned` position gets a `Copy` before the instruction, `Borrowed` position gets
     nothing.
   - if the slot is not in `live`: `Owned` position becomes a `Move` with `isLastUse`, `Borrowed` position keeps the
     borrow and gets a `Release` **after** the instruction.
   - then add the slot to `live`.
   After the instruction's operands: if it defines a managed slot that is not in `live` at that point, the value is
   dead immediately (`const _ = list.added(4)`) - emit a `Release` right after. Then remove the defined slot from
   `live`.
5. **Drops on edges.** For every edge `B -> S`: `drops(B -> S) = liveOut(B) minus liveIn(S)`. With one successor the
   `Release`s go at the end of B. With several, the edge is split into a new block that holds only the releases and a
   `Jump`. This is the whole mechanism for the early exits: `return` in the middle of a loop, `break`, `continue`,
   the error arm of `?`, and the arms of a `match` that bind different things all become ordinary edges with their own
   drop sets. Nothing special is written for them.
6. `Return` is an `Owned` position. `Unreachable` after a `Panic` drops nothing (gap 9).

`using`/`Close` need no rule at all: `using` is an ordinary function, `close` an ordinary method, and the only
observable destruction order in the language is the order in which `using` blocks nest. TorbScript has **no
destructors**; releases are not observable (gap 10).

### 2.3 Why `var` paths need no counting

A `Reference` is the base slot plus a static path. It is valid for exactly the duration of the call it was formed
for, and:

- the caller holds the base live across the call (the call is a use of the base, so liveness keeps it),
- **exclusivity** is checked statically by the checker (TYPECHECKER 5.2), so no second path to the same storage can
  be active,
- references are second class - they cannot be returned, stored in a field or captured by an escaping closure - so
  nothing can dangle.

Therefore an interior pointer needs no count, no lifetime and no write barrier. `MakeUnique` at the moment the path
is formed is what makes the pointer safe to write through: after it the storage has count 1 and, by exclusivity, one
path. The **one** exception is a captured `var` binding, which is a `Box` and not a reference (gap 19): it is counted,
it may escape, and it is the only sharing of a variable in the language.

`TakeOut`/`PutBack` is the second form, for containers whose storage is not contiguous (the hash map, the tries): read
the value into a slot, run the access against a `Reference` to that slot, write it back. Literally the concept's "take
it out, change it, put it back - without a copy" (`var` Paths section). The lowering picks the form by the container's
`RuntimeKind`, so both back ends do the same thing.

### 2.4 Cycles

Values cannot form cycles; only `Shared` objects and `Box`es can. **Bacon/Rajan synchronous cycle collection with
trial deletion**, over those two kinds and nothing else:

- The header of a `Shared` object and of a `Box` carries `count`, a color (`black`, `gray`, `white`, `purple`) and a
  pointer to a `trace` function. Records, lists, maps and strings have no color and are never scanned.
- `trace` is emitted per `Shared` layout and per `Box` item type, and visits only the fields that can *reach* a
  `Shared` or `Box` - which the layout's `containsShared` flag says. For most layouts that is nothing, and the trace
  function is `NULL`.
- A `Release` that leaves a non-zero count on a colored object appends it to the task's **candidate buffer** (marking
  it `purple`). The collector runs when the buffer passes a threshold (10 000 entries) and once at task shutdown:
  mark gray with trial decrements, scan, collect white.
- **Until milestone 7.7 the collector does not exist and a cycle leaks.** Correctness is unaffected, `compiler/` has
  no `shared type` at all, and `torb build --report-leaks` prints the live block count at exit so a regression shows
  up in the conformance suite.

### 2.5 Non-atomic counts, per-task heaps, channel transfer

Counts are plain integers. Every task owns a heap (a bump allocator with size-class free lists) and a candidate
buffer; nothing is shared implicitly, so nothing has to be atomic. Blocks carry the id of their owning heap in the
header.

`Channel.send(value)`: the value is a value, so either it is copied into the receiver's heap, or - when every counted
block it reaches has count 1 - the blocks are **transferred** (the receiving heap adopts them; a later free routes the
block back to its owning heap's foreign-free list, which the owner drains). A value with `containsShared` never
crosses a channel; the checker rejects that (Concurrency section: "`shared type` objects ... are confined to the task
that created them"). With the single-threaded scheduler of 7.3 none of this is load bearing yet: the transfer is a
move and the heap is one. 7.7 turns it on.

---

## 3. The C back end

Portable C11, no dependency but libc (and `<threads.h>`/pthreads/Win32 from 7.7 on). Builds with clang, gcc and MSVC.

### 3.1 Every IR type in C

| IR type                        | C                                                                                     |
|--------------------------------|---------------------------------------------------------------------------------------|
| `Integer(w, signed)`           | `int8_t` ... `int64_t`, `uint8_t` ... `uint64_t`                                        |
| `Float(32/64)`                 | `float`, `double`                                                                      |
| `Bool`                         | `bool` (`<stdbool.h>`)                                                                 |
| `Char`                         | `uint32_t`, a Unicode scalar value                                                     |
| `Void`                         | `uint8_t` as a value, `void` as a result type                                           |
| `Never` result                 | `_Noreturn void`                                                                       |
| `Text`                         | `torb_text` = `{ torb_bytes *storage; uint32_t offset; uint32_t length; }`               |
| `Record(Inline)`               | `struct T_x { ... }`, passed and returned by value                                      |
| `Record(Boxed)`                | `T_x *` to `{ torb_header header; fields }`                                             |
| `Variant(Inline)`              | `struct { uint32_t tag; union { struct {...} v0; ... } payload; }`                       |
| `Variant(Niche(...))`          | the payload field itself; `NULL` is the empty variant                                   |
| `Tuple`                        | `struct T_tuple_x { f0; f1; ... }`                                                      |
| `FixedArray(item, n)`          | `struct { item items[n]; }` - a struct, so it assigns and returns by value               |
| `Closure(sig)`                 | `struct { R (*code)(torb_environment *, ...); torb_environment *environment; }`          |
| `Object(bounds)`               | `struct { torb_object *data; const w_A *a; const w_B *b; }`                              |
| `Shared(layout)`               | `T_x *` with a colored header                                                           |
| `Box(item)`                    | `torb_box_x *` = `{ torb_header header; item value; }`                                   |
| `Lazy(item)`                   | `torb_lazy_x *` = `{ header; uint8_t state; item value; closure thunk; }`                |
| `Runtime(List, Item)`          | `torb_list` = `{ torb_list_storage *storage; uint32_t offset; uint32_t length; }`         |
| `Runtime(Map/Set, ...)`        | `torb_map` / `torb_set`, one counted storage pointer each                                |

A trait-typed value always boxes its payload (`torb_object`), because a witness member takes `void *self`. Boxing a
small value on coercion is not observable (no `sizeof`, no layout, no reflection).

**Generic runtime containers without templates.** `ArrayList<Item>` must store `Item` inline, and the runtime is
hand-written C that must not be generated. So there is **one** implementation per container, parameterized by a
static **element descriptor** the compiler emits per element type:

```c
typedef struct {
  uint32_t size, align;
  void (*retain)(void *);          /* NULL when the element is trivial */
  void (*release)(void *);
  bool (*equals)(const void *, const void *);
  uint64_t (*hash)(const void *);
} torb_element;
```

`static const torb_element d_Int64 = { 8, 8, NULL, NULL, t_Int64_equals_raw, t_Int64_hash_raw };` One indirect call
per element retain, skipped entirely when the pointers are `NULL`. The VM uses the same descriptors, so the two
implementations of `ArrayList` are one.

### 3.2 Calling convention

- Parameters in declaration order; `self` first; witness tables appended after the declared parameters in bound
  order; a closure body takes `torb_environment *environment` as its first parameter.
- A `var` parameter is `T *`. A `Reference` with steps is formed at the call site (`&list->storage->data[i]` after
  `MakeUnique`) - never passed as a base plus a path.
- Results: by value, except when the layout model estimates more than 64 bytes; then the IR signature says
  `resultMode: .ByPointer` and the caller passes `T *result` as the first parameter. The lowering decides, both back
  ends obey, so the choice can be tuned by measurement without touching semantics.
- Functions returning `Never` are `_Noreturn`; the emitter adds `TORB_UNREACHABLE()` after a call of one so no
  compiler warns about a missing return.

### 3.3 Panics, overflow, bounds

```c
static inline int64_t torb_add_i64(int64_t a, int64_t b, torb_location at) {
#if TORB_HAS_OVERFLOW_BUILTINS                      /* clang, gcc */
  int64_t r; if (__builtin_add_overflow(a, b, &r)) torb_panic_overflow("+", at); return r;
#else                                               /* MSVC and anything else */
  uint64_t r = (uint64_t)a + (uint64_t)b;
  if ((((uint64_t)a ^ r) & ((uint64_t)b ^ r)) >> 63) torb_panic_overflow("+", at);
  return (int64_t)r;
#endif
}
```

Multiplication uses `__int128` where it exists and a division check otherwise. Division checks `b == 0` and
`a == INT64_MIN && b == -1`. **Overflow checks are on in every profile** - they are semantics, not diagnostics
(Execution Model: "Integer overflow panics, in every back end"). `torb_panic` writes to stderr and calls `_exit(101)`
without running anything (gap 9). Bounds checks on `list[i]`, `text[a..b]` and `Array` are likewise always on; a
constant index into an `Array` is checked by the checker instead (gap 38).

### 3.4 Strings

UTF-8, immutable, a value. `torb_bytes` is `{ torb_header header; uint32_t capacity; uint8_t data[]; }` (a C11
flexible array member, which MSVC accepts). A `torb_text` is a slice of one: `offset`, `length`, so `text[3..]` is
O(1) and shares the storage, `byteLength()` is a field read, and a literal is an immortal `torb_bytes` in read-only
data. **No small-string optimization in v1**: it doubles the code path of every string operation for a win the
compiler does not need (its strings are slices of source files). `compact()` is the explicit way to stop a small
slice from pinning big storage.

Slicing validates: an offset past the end, a reversed range, or an offset on a UTF-8 continuation byte panics with
the offset in the message (gap 7). `String` is therefore always valid UTF-8 - a file whose bytes are not are an
`IoError`, not a replacement character.

### 3.5 Collections

**One list and one hash table, for both the array and the trie names.**

- `torb_list_storage`: contiguous, `{ header; const torb_element *element; uint32_t length, capacity; max_align_t data[]; }`.
  Growth doubles. Slices share it (`offset`, `length`), which is what "a slice is a `List` again that shares the
  storage and starts at index 0" needs.
- `torb_map_storage`: open addressing with a separate **insertion-ordered entry vector** (bucket array of indices
  plus an entry array), the shape of an ordered hash map. So iteration is insertion order (gap 6), removal leaves a
  tombstone and does not reorder, and compaction happens when tombstones exceed half the entries.
- A set is a map whose value descriptor has size 0.
- `ArrayList` and `TrieList` both map to the list; `TrieMap`/`HashMap` and `TrieSet`/`HashSet` both map to the table.
  That is observable only through performance: iteration order is insertion order for all of them, and `Show` prints
  `[a, b]` / `["k": v]` / `{a, b}` either way. **The persistent variants (a HAMT plus an insertion-order vector) come
  in milestone 8**, after the fixpoint - the compiler never constructs one, and the manifest records which name is
  still an alias so the decision is visible.
- `ArrayStack` and `ArrayQueue` are plain TorbScript over a list and need no runtime at all.

### 3.6 Output shape and determinism

- **One translation unit.** `out/program.c` for the whole program plus the runtime's own `.c` files. Simplest, and
  it removes every question about declaration order between units. `compiler/` plus `std/` is roughly 8 000 lines of
  TorbScript today and lands well inside what clang, gcc and MSVC compile in under a minute. Milestone 6.3 adds
  sharding when the measurement says so: `shards = N`, a function goes to shard `fnv1a(mangled) % N` (stable under
  insertion, unlike index ranges), with all declarations in one generated `program.h`. The fixpoint compares the
  shards concatenated in sorted order, so the shard count never changes the comparison.
- **Everything is sorted before it is written**: layouts, element descriptors, witness tables, static values and
  functions, each by mangled name. So the C file is a pure function of the *instance set*, not of the traversal that
  found it - which is what makes the fixpoint test about the compiler and not about a `Map` iteration order.
- **No path, time or machine ever leaks in.** `#line <line> "src/syntax/lexer.trb"` with forward slashes, relative to
  the workspace root. No `__DATE__`, no absolute paths, no build id. The emitter writes `\n` line endings on every
  platform.
- **Readable.** One comment per function with its qualified TorbScript name and signature, one per block with its
  `label`, `#line` before every statement so a debugger and a C compiler error point into the `.trb` file. All slots
  are declared at the top of the function (so no `goto` crosses an initialization), named `s3_deltaX` from the slot's
  source name where there is one.
- **Identifiers.** Section 1.5. Labels are `L<block index>`.
- **Profiles.** `debug`: `-O0 -g`, runtime invariant assertions on, a shadow stack of locations for panic traces.
  `release`: `-O2 -DNDEBUG`, no shadow stack, panic prints only its own location. Bounds and overflow checks are on
  in both.

### 3.7 `native` declarations and the manifest

Decided gap 22 requires that a missing intrinsic is a compile error. The contract between the lowering and the
runtime is one table in the compiler:

```trb
public type NativeEntry {
  /** `"ArrayList.add"`, `"String.trim"`, `"print"`. The owner is empty for a free function. */
  name: String
  target: NativeTarget
  /** Which parameters the callee stores, so the ownership pass knows (section 2.1). */
  ownedParameters: List<Int> = []
  state: NativeState = .Ready
}

public type NativeTarget {
  /** Emitted inline by the C back end, an opcode in the VM. */
  case Intrinsic(intrinsic: Intrinsic)
  /** A function of the runtime, called by name. */
  case Runtime(function: String)
  /** Generated from the layout: derived members of native types. */
  case Derived(derived: DerivedKind)
}

public type NativeState {
  case Ready
  /** `Decimal`, `Float32` arithmetic, `std/http`: a clean compile error naming the milestone. */
  case Planned(milestone: String)
}
```

- `torb build` reports `` `Decimal.add` is not implemented yet (milestone 8) `` at the call site, never a link error.
- A `native fn` in `std/` with no entry at all is an **internal** error of the compiler, reported with the
  declaration's location. A test walks every `native` declaration of `std/` and asserts it is in the table.
- `torb natives --header` writes `runtime/include/torb_natives.h` (prototypes for every `.Runtime` target) from the
  same table, and a test asserts the file on disk matches - so the two sides cannot drift, and the C compiler
  enforces the signatures.
- `native` types that ask the runtime for trait members (`public native type Int64 with Signed, Hash {}`) are
  `.Intrinsic` or `.Derived` entries, one per member per width.

### 3.8 `runtime/` and its tests

```text
runtime/
├ include/torb.h              Public header: header, text, list, map, closure, panic, task
├ include/torb_natives.h      Generated from the manifest (torb natives --header)
├ memory.c                    Heap, header, retain/release/make-unique, immortal values, cycle candidates
├ panic.c                     torb_panic, overflow, bounds, the shadow stack, exit codes
├ text.c                      UTF-8, slices, concatenation, comparison, hashing, float and integer formatting
├ list.c  map.c               The one list and the one ordered hash table, over element descriptors
├ number.c                    Checked arithmetic, conversions, parsing, the bit and wrapping operations of gap 3
├ console.c  process.c        print/printError, arguments, exit
├ file.c  clock.c  environment.c   std/fs, std/time, std/environment
├ task.c                      Scheduler, Task, Channel (milestone 7.3)
├ collect.c                   Trial deletion (milestone 7.7)
└ tests/
  ├ harness.h                 20 lines: TORB_CHECK, a counter, a main
  └ *_test.c                  One per source file
```

Tests: `runtime/tests` compiles and runs with the same C compiler `torb build` found (`torb build --test-runtime`).
The C tests cover what TorbScript cannot reach - refcount edge cases, make-unique under aliasing, the hash table's
tombstones, UTF-8 boundary panics, float formatting round trips, overflow on every width. Everything reachable from
TorbScript is tested from TorbScript instead, through the conformance suite. `memory.c` keeps a live-block counter in
the debug profile, and the conformance runner asserts it is zero at exit, which is the leak test.

**The runtime and the lowering are developed in parallel.** The manifest is the contract: 5.R1 writes the header and
the skeleton, 5.2/5.3 write the lowering against the manifest, and neither blocks the other.

---

## 4. The driver

```text
torb build [path] [--profile debug|release] [--emit-c] [--emit-ir] [--out <path>]
torb run [path] [arguments...]          Until the VM exists: build into the cache, then execute
torb test [path]                        Compile the test modules into one binary and run it
torb ir <path>                          The IR of a file or project, in the text format of section 1.7
```

- **`torb run` builds and executes, and says so.** Until milestone 7.2 `torb run` compiles the program into
  `<workspace>/.torb/cache/<profile>/<key>/program` and runs it, forwarding the arguments, the three standard
  streams and the exit code. It prints nothing extra on a cache hit. After 7.2 the VM becomes the default and this
  path stays as `torb run --native`.
- **Output paths.** From `project.trb`: `build { input "src/main.trb", output "build/{target}/torb" }`. The static
  manifest reader (`compiler/src/project/manifest.trb`) gains `buildOutput` and `buildTarget`, and keeps the
  interpolation as literal text; the driver substitutes `{target}` (the profile) and `{binary}` (the part of `name`
  after the `/`). `--out` overrides. A workspace root builds its members in dependency order.
- **Incremental rebuild by content hash.** The cache key is a 64-bit FNV-1a over: the bytes of every file in the
  `SourceTree`, its relative path, the profile, the compiler's version string and the C compiler's identification
  line. On a hit, nothing runs. Otherwise the C is written and compiled. There is no finer granularity in v1 - one
  key for the whole program - because `torb build` of the workspace takes seconds and the interesting incrementality
  is the language server's (milestone 8). Writing the hash needs the wrapping and bit operations of gap 3.
- **Finding a C compiler.** In order: `$TORB_CC`; then `clang`, `gcc`, `cc` on `PATH`; then `cl` on `PATH`
  (a "x64 Native Tools" prompt on Windows - `torb` never tries to run `vcvars64.bat` itself, because configuring
  another process's environment from a build tool is a source of bugs, not of convenience). If none is found:

  ```text
  error: no C compiler found. `torb build` needs one until the native back end emits machine code.
    tried: $TORB_CC, clang, gcc, cc, cl
    on Windows: winget install LLVM.LLVM, or open a "x64 Native Tools" prompt
    `torb build --emit-c` writes the C without compiling it
  ```

  Exit code 3. **`--emit-c` always works**, with no compiler installed at all - which is what CI uses for the
  fixpoint diff, and what keeps the whole pipeline testable on a bare machine.
- Flags: clang/gcc `-std=c11 -O2 -g0 -o <out> program.c runtime/*.c -lm` (`-pthread` from 7.7);
  MSVC `/std:c11 /O2 /Fe<out> program.c runtime\*.c`. Warnings are on (`-Wall -Wextra`, `/W3`) and a warning in
  generated code is a bug in the emitter, so CI builds with `-Werror`.
- **A C compiler error is an internal compiler error.** Never the user's fault:

  ```text
  internal error: the generated C did not compile. This is a bug in torb, please report it.
    kept: .torb/cache/release/8f1c.../program.c
    cc: program.c:14203: error: ...
  ```

  The first 20 lines of the compiler's output, the kept `.c` file, exit code 70.
- **`torb test` natively.** The test files are scripts (gap 28), so each becomes a function. The driver generates one
  entry module that calls them in sorted path order; `test`/`group` are runtime functions that collect results,
  print them in the format stage 0 prints, and set the exit code. So `compiler/tests/*.test.trb` move from stage 0 to
  the native binary unchanged.

---

## 5. The VM (milestone 7)

### 5.1 Bytecode

**Register based**, derived mechanically from the IR: a slot is a register, an instruction is an opcode plus operands.
A stack machine would need a translation pass out of the three-address IR and would raise the dispatch count, which
is the only cost that matters here - the interpreter loop is *itself* compiled by the C back end, so it is real
machine code and dispatch dominates.

- One instruction per `Int64`: opcode in bits 0-7, three operands of 16 bits, a `Wide` prefix for larger indices.
  Encoding needs the bit operations of gap 3.
- A `Chunk` per function: the instruction list, a constant pool index, the slot count and a location table
  (instruction index to `LocationId`) for panic messages.
- Blocks disappear into offsets; `Jump`/`Branch`/`Switch` become relative offsets. Everything else maps one to one,
  including `Retain`/`Release`/`MakeUnique`/`TakeOut`/`PutBack` - which is the point: the VM does not re-derive
  ownership, it executes what the lowering decided, so a leak or a copy is the same in both back ends.
- Milestone 7 keeps bytecode in memory. A file format (`.torbc`, with a version and a hash of its inputs) is
  milestone 8, together with the IR cache.

### 5.2 The interpreter

`compiler/src/vm/`: `value.trb` (a tagged value for the VM's own heap), `frame.trb`, `interpret.trb` (the loop),
`native.trb` (the same manifest, dispatched by id), `heap.trb`. Written in TorbScript, compiled by the C back end
like everything else - so "interpreted and compiled behave the same" is tested by running the conformance suite
through both, and the VM's own speed is the C back end's problem.

The loop is a `match` over the opcode in a `while`, with the frame's registers in one `var` list. Values in the VM
are the runtime's values: the VM calls the same `runtime/` functions for strings, lists and maps through the natives
manifest, so there is exactly one `ArrayList` in the process and a compiled function can be called from bytecode and
back with no marshalling.

### 5.3 Tasks

**The state-machine transformation happens in the lowering, so both back ends share it.** A function whose result is
`Task<Value>`, and a closure passed to `spawn`:

1. Split every block at an `await()`. Each split point gets a state number.
2. Locals live across a split move into a `TaskFrame` layout (a `Boxed` record owned by the `Task` object); locals
   that are not become ordinary slots again.
3. The function becomes `resume(task: Task<Value>, state: Int) -> Poll<Value>` with a `Switch` on `state` in the
   entry block, `Suspend(state, awaited)` where the `await()` was, and `Complete` on the paths that produce the
   value. `Task<Value>` is a `Shared` object holding the frame, the state, the result and a waiter list.
4. `spawn { ... }` allocates the task, copies the captures (a closure passed to `spawn` cannot capture a `var`, so
   there is no box to share) and enqueues it.

The **scheduler is a FIFO run queue**, single threaded in 7.3, in `runtime/task.c` and in the VM with the same
algorithm and the same order - so a program whose tasks do no real IO produces identical output in both back ends,
and `10-async.trb` has a stable `.expected`. `Channel` is a ring buffer plus two waiter queues; `send` on a full
channel and `receive` on an empty one suspend. `all(a, b)` and `Task.all` are ordinary functions over that.

7.7 adds threads: one worker per core, one heap per worker, a task is pinned to the worker that created it (no work
stealing - stealing would move a heap), `spawn` distributes round robin and copies the captures into the target heap,
`await` across workers sets an atomic flag and enqueues on the owner. Only the channel transfer and the ready flags
are atomic; counts stay plain (section 2.5).

### 5.4 The sandbox

`Sandbox.load<Value>(path, capabilities)` embeds the front end (lexer, parser, semantics, checker, lowering,
bytecode) and the VM into any binary that uses it. Nothing else does - the compiler's own binary has them anyway.

- **Capabilities are checked at module import**, before anything runs: the script's `use` list is matched against
  `modules`, and a module that is not allowed is a `SandboxError` with the line of the import. That is possible
  because "there is no reflection, no `eval` and no dynamic import" (Packages section), so the import list *is* the
  capability list.
- `files`/`environment` roots are a per-sandbox list the runtime's file and environment functions consult when the
  current task is sandboxed. A script can only reach them through an allowed module anyway; the root check is the
  second lock.
- **Limits are VM counters**: `steps` decremented per dispatch, `memory` from the sandbox's own heap (its own
  allocator, so the limit is exact and the teardown is one free), `time` compared against a monotonic clock every
  4096 steps. A limit that is hit stops the script, and `Script.apply` returns the `SandboxError` (gap 12).
- **A panic inside a sandboxed script is recoverable**, and it is the only place where one is: the VM is
  interpreting, so it can stop, and the script's heap is separate. It becomes the `SandboxError` of `apply`.
- `project.trb` and `project.lock.trb` are this mechanism with the receiver `Project`. 7.5 declares `Project`,
  `Build`, `Test`, `Dependencies` and `Workspace` as real types in `compiler/src/project/model.trb`, replaces
  `manifest.trb`'s static reader with `Sandbox.load<Project>`, and keeps a test that asserts the two agree on every
  `project.trb` in the repository before the static reader is deleted.
- **The REPL** (7.6) keeps a `Checker`, a growing chain of scopes and one VM frame whose registers survive between
  inputs. Each input is checked as a block in a new child scope, lowered, compiled and run; redefining a name is
  ordinary shadowing, and a type that is declared again gets a new layout whose mangled name carries a generation, so
  old values keep theirs and print as `Point#1` (the Open Question in CONCEPT.md, answered by the mangling that
  already exists).

---

## 6. Sub-milestones

Each is one agent session: roughly 600-1500 lines of TorbScript or C plus tests. Tests are IR text snapshots
(section 1.7), and C output that is compiled and run against expected stdout. The runnable conformance suite is
`bootstrap/tests/scripts/*.trb` with their `.expected` files, extended by `.expected` files for the tour, and it is
run against **stage 0, the C back end and later the VM** by the same runner.

| # | Scope | Files | Tests | Depends on |
|---|---|---|---|---|
| **5.1** | **Done.** IR data types, layouts, the representation classes, the layout model, mangling, instantiation from a checker type, the builder, the verifier, the text format | `ir/ir.trb`, `ir/layout.trb`, `ir/mangle.trb`, `ir/instantiate.trb`, `ir/build.trb`, `ir/verify.trb`, `ir/print.trb` | Hand-built IR to text; mangling is stable and collision free; layouts and instantiation by table | M4.1 |
| **5.R1** | **Done.** Runtime skeleton and the manifest format: header, heap, retain/release/make-unique, `torb_text`, panic, overflow, console, the manifest with the header it renders, the C test harness | `runtime/*`, `backend/c/natives.trb` | `runtime/tests` (67, zero live blocks after each); the generated header compiles against the runtime and the table's shape is pinned | - |
| **5.2** | **Done.** Lowering: functions, slots, blocks, literals, locals, arithmetic intrinsics, calls of top-level functions and concrete methods, fields, constructors, tuples, `if`, `while`, `for` over `Range<Int>`, `return`, blocks, the const evaluator, static values | `ir/lower/*.trb`, `ir/instances.trb`, `ir/constant.trb` | IR snapshots for ~15 small programs | 5.1, M4.4 |
| **5.3** | **Done.** The C emitter, minimum path. Types, functions, blocks and gotos, `#line`, slot declarations, static data, `main`, the driver's minimum (`torb build`, compiler discovery, `--emit-c`). **Gate: a program of monomorphic functions becomes a native binary and behaves like it does on stage 0** (`print` needs the witness tables of 5.6) | `backend/c/type.trb`, `emission.trb`, `prototype.trb`, `body.trb`, `emit.trb`, `cli/build.trb` | `compiler/tests/emit-c.test.trb` (20); `bootstrap/tests/native/*` compiled, run and compared with stage 0 by `bootstrap/crates/torb-cli/tests/native.rs`; `--emit-c` twice is byte identical | 5.2, 5.R1 |
| **5.4** | **Done.** Ownership: the summary pass, liveness, `Copy`/`Move`/`Retain`/`Release` insertion, edge splitting, `MakeUnique`, and the verifier's ownership invariants | `ir/liveness.trb`, `ir/operand.trb`, `ir/ownership.trb`, `ir/ownership-verify.trb` | IR snapshots pinning every insertion point (45 tests in `ownership`, `liveness`, `operand`, `make-unique` and `ownership-verify`); hand-built wrong IR against every message of the verifier; the live-block counter is zero after every conformance script (from 5.3 on) | 5.2 |
| **5.5** | ADTs: variant layouts, the niche, `MatchPlan` to decision trees, guards and fallbacks, `Option`/`Result`, `?` with its conversion, `if const`/`if var`/`while const` | `ir/lower/match.trb`, `ir/decision.trb` | Snapshots of the decision trees; `04-adts-and-matching.trb` and `06-errors.trb` run | 5.2, 5.4 |
| **5.6** | Generics: instance keys, the worklist, witness tables, trait-typed values, per-bound sharing, derived `Show`/`Equals`/`Hash`/`copy`. **Gate: `bootstrap/tests/scripts/basics.trb` produces its `.expected` natively** | `ir/lower/generic.trb`, `ir/witness.trb`, `ir/lower/derive.trb` | basics.trb; instance counts are asserted so an accidental explosion fails a test | 5.5 |
| **5.7** | Collections: the list and the ordered hash table in C, element descriptors, the natives of `List`/`Map`/`Set`/`String`, `Iterable` pipelines (ordinary TorbScript once closures work). **Gate: `language.trb` passes** | `runtime/list.c`, `runtime/map.c`, `runtime/text.c`, manifest entries | language.trb, `07-collections.trb` | 5.3, 5.6, 5.8 |
| **5.8** | Closures: closure conversion, environments, escaping or not, `Box`es for captured `var` bindings, `lazy` cells, receiver closures, property commands. **Gate: `examples/config-dsl` runs** | `ir/lower/closure.trb`, `ir/capture.trb` | config-dsl, `09-dsl.trb`, `02-functions.trb` | 5.6 |
| **5.9** | `var` paths: `var` parameters, interior projections, `TakeOut`/`PutBack`, slices as windows, `shared type` objects with their headers and trace functions, `Close`/`using` | `ir/lower/place.trb`, `runtime/memory.c` | `01-bindings-and-values.trb`, `03-types.trb`, `08-control-flow.trb` | 5.4, 5.7 |
| **5.10** | Text and data: interpolation, `Show` for every shape in the format of gap 23, float formatting, `describe`, derived `Encode`/`Decode`, `std/json` | `runtime/text.c` (float), `ir/lower/derive.trb`, `std/json` natives | `11-data.trb`, the `Show` format is pinned by a table-driven test | 5.6, 5.7 |
| **5.11** | `Expression<Value>`: static trees, captures, `assert`, `test`/`group` and `torb test` natively. **Gate: `compiler/tests/*.test.trb` run from the native binary** | `ir/lower/quote.trb`, `runtime/`, `cli/test.trb` | The compiler's own tests | 5.10 |
| **5.12** | **Runtime half done.** The remaining std natives: `std/fs`, `std/io`, `std/process`, `std/time`, `std/math`, `std/environment`. **Gate: the tour runs** (01-09, 11, 12; `10-async` waits for 7.3) | `runtime/file.c`, `clock.c`, `environment.c`, `number.c` | `.expected` files for every tour module, run on stage 0 and natively | 5.3 (parallel with 5.8-5.11) |
| **5.13** | The full driver: profiles, the content-hash cache, `torb run` as build-and-execute, `torb test`, output paths from `project.trb`, ICE reporting, `--emit-ir` | `cli/build.trb`, `cli/run.trb`, `project/manifest.trb` | Cache hit and miss, a deliberately broken emitter reports an ICE | 5.3 |
| **5.14** | Conformance and determinism: one runner over stage 0 and the C back end, `--emit-c` twice byte identical for the whole workspace, no absolute path in the output, timing budget | `compiler/tests/backend.test.trb`, the runner | Everything above | 5.1-5.13 |
| **6.1** | Compile `compiler/` with stage 1: every missing intrinsic, every crash, every construct the compiler uses and the lowering does not cover yet. **Gate: a `torb` binary exists** | wherever it hurts | `torb check ..` from the new binary gives the same output as stage 1 | 5.14 |
| **6.2** | The fixpoint: stage 2 compiles `compiler/` again, the two C files are compared byte for byte, both binaries pass the conformance suite. `bootstrap/` frozen | the runner | **The fixpoint gate** | 6.1 |
| **6.3** | Performance and size: measure, shard the translation unit if it pays, cut the obvious waste (instance count, string copies), state a budget for `torb build` of the workspace | `backend/c/emit.trb` | A timing test in the suite | 6.2 |
| **7.1** | Bytecode: the format, the emitter from the IR, a disassembler for the snapshots | `backend/bytecode/*.trb` | Disassembly snapshots next to the IR snapshots | 6.2 |
| **7.2** | The interpreter loop, `torb run` through the VM, the conformance suite through the VM. **Gate: stage 0, C and the VM agree on every script** | `vm/*.trb` | The full suite, three back ends | 7.1 |
| **7.3** | Tasks: the state-machine transformation in the lowering, `Task`/`spawn`/`await()`/`Channel`, the FIFO scheduler in C and in the VM. **Gate: `10-async.trb` in both back ends** | `ir/lower/task.trb`, `runtime/task.c`, `vm/task.trb` | `10-async.trb`, channel and ordering tests | 7.2 |
| **7.4** | The sandbox: `Script<Value>` (gap 12 below), capability checks at import, limits as counters, panics recovered, embedding the front end | `std/sandbox`, `vm/sandbox.trb` | A script that loops forever, one that imports what it may not, one that panics | 7.2 |
| **7.5** | `project.trb` as a receiver script: `Project` and friends as real types, the static reader deleted after a test asserts both agree | `project/model.trb`, `project/manifest.trb` | Every `project.trb` of the repository, both readers | 7.4 |
| **7.6** | The REPL: the scope chain, the persistent frame, shadowing, generations of redeclared types | `cli/repl.trb`, `vm/session.trb` | A transcript test | 7.4 |
| **7.7** | Threads and the cycle collector: workers, per-task heaps, channel transfer, trial deletion, the leak counter at zero for a program that builds a cycle | `runtime/collect.c`, `runtime/task.c` | Cycle and parallelism tests | 7.3 |

```text
5.1 ─► 5.2 ─► 5.3 ─┬─► 5.4 ─► 5.5 ─► 5.6 ─┬─► 5.7 ─┬─► 5.9 ─┐
5.R1 ──────────────┘                      ├─► 5.8 ─┘        ├─► 5.14 ─► 6.1 ─► 6.2 ─► 6.3 ─► 7.1 ─► 7.2 ─┬─► 7.3 ─► 7.7
                   └─► 5.13 ──────────────┤   5.10 ─► 5.11 ─┤                                            ├─► 7.4 ─┬─► 7.5
                       5.12 ──────────────┴─────────────────┘                                            │        └─► 7.6
```

- **5.R1 and 5.12 are the parallel track**: the runtime in C, against the manifest, by an agent who never touches the
  lowering. 5.1 defines the manifest's shape, so 5.R1 can start immediately after it.
- 5.7, 5.8 and 5.12 are independent of each other; 5.9 and 5.10 are independent; 5.13 only needs 5.3.
- **Hello world is native at 5.3. The tour runs at 5.12. The fixpoint gate is 6.2.**
- **5.12's runtime half is done**: `runtime/file.c` (open handles), `clock.c`, `environment.c` and the math functions
  in `number.c` all exist and are `.Ready` in the manifest, with `runtime/tests` for each (see `runtime/README.md`
  for the representations chosen - nanosecond `Instant`/`Duration`, the `torb_file` `shared type`). `File.lines`
  stays `.Planned`, but for 5.7 rather than 5.12: it answers an `Iterable`, whose ABI is 5.7's. The tour itself still
  cannot run natively until the lowering this milestone does not touch (5.3's emitter, and whichever of 5.4-5.11 a
  module's constructs need) exists to call these symbols.

### What 5.1 does differently from sections 1 to 3

Sections 1 to 3 are the plan; where they did not fit what milestone 4.1 actually built, or what the language allows,
the code won and this is the list. Everything else is as written.

- **Seven files, not four.** `ir/ir.trb` (ids, types, instructions, the program), `ir/layout.trb` (layouts, the size
  model, `finishProgram`), `ir/mangle.trb`, `ir/instantiate.trb` (a checker `TypeId` plus a substitution to an IR
  type), `ir/build.trb` (the builder every lowering threads), `ir/verify.trb`, `ir/print.trb`. **`torb ir` is not
  wired yet**: there is nothing to lower, so the command arrives with 5.2. The printer it will use is here.
- **Case names that would shadow a prelude type are renamed**, the same rule that makes the syntax tree say
  `TupleType` and the checker `VoidType`: `IrType.Floating`, `.Boolean`, `.Character`, `.VoidType`, `.NeverType`,
  `.SharedObject`. `RuntimeKind` says `ListStorage`, `MapStorage`, `SetStorage`, `TaskObject`, `ChannelObject`,
  `ExpressionTree`, `DecimalNumber` for the same reason.
- **`IrType.ConstantValue(value, of)` was added.** A const generic argument is part of an instance's argument list
  (section 1.4), so it has to have a form - otherwise `Array<Float, 16>` cannot be a *key*. It is never the type of a
  slot, a field or a parameter, and the verifier says so. `IrType.NeverType` was added for the result of a function
  that does not return, which is `_Noreturn` in C.
- **`Instruction.Call` carries `witnesses: List<WitnessSource>`.** Section 1.4 says witness tables are appended after
  the declared parameters; without a place for them a dictionary-passing call could not be written at all.
- **`ContainerKind` is `RuntimeKind`.** Section 2.3 says the lowering picks the form of `TakeOut`/`PutBack` by the
  container's `RuntimeKind`, so a second enumeration with the same cases would only be a second place to be wrong.
- **`WitnessSource` is flat**: a `WitnessRoot` (a static table, a witness parameter, or one of the bounds of a
  trait-typed value) plus a `List<Int>` of steps through the `nested` tables. A recursive case would be a value type
  that contains itself, which the language does not have.
- **`Block` gained `at: LocationId?`.** The text format of section 1.7 prints `b0: # main.trb:19 body`, which is a
  location *and* a label, and the C emitter needs the same thing for `#line`. `Layout` gained `size`, `alignment`,
  `isRecursive`, `commonFieldCount`, `origin` and `arguments`, so that a layout answers for itself.
- **`resultMode` is not part of the interning key of a signature**, and `finishProgram` fills it in. It follows from
  the size of the result, and a size is only final once every layout is - which is after the last instance was
  lowered. `finishProgram` also fills in the sizes of the element descriptors, and it has to run before anything is
  printed, verified or emitted.
- **Representations are a fixpoint over the whole layout table, not a recursion over one layout.** Whether a field is
  a pointer or bytes depends on the representation of its own layout, so a type that contains itself would never
  terminate. The layouts that are *on* a cycle of the by-value field graph are pinned to `Boxed` first (found by a
  walk per layout, which is why the answer does not depend on the order in which the layouts were created), and what
  is left is a DAG that settles in as many sweeps as the field graph is deep.
- **A type may carry fields of its own and cases.** The design's `Layout` has no room for that; here the common
  fields come first in `fields`, `commonFieldCount` says how many, and every variant group follows them. A layout with
  common fields never uses the niche.
- **The hash of a name that is too long is not FNV-1a-64.** FNV needs wrapping 64 bit multiplication, and every
  arithmetic operation of the language panics on overflow: gap 3's `multipliedWrapping` is `native` and the runtime
  does not exist yet. `nameHash` is two polynomial hashes over prime moduli, each below 2^31, printed as the same 16
  hexadecimal digits. When the wrapping operations arrive this can be swapped - it changes every long name, so it is
  recorded here.
- **A source underscore is always escaped as `_x5f_`**, not only where it would create a `__`. That is what makes the
  escaping injective: an `_` in a mangled name then comes from an escape or from a component separator and never from
  a name. A `__` can still appear where a component begins with an escape (`_foo`); that is not a collision, because
  the number of components of a name is fixed, and C reserved spellings are still impossible because every name
  starts with its prefix letter.
- **A tuple layout is `T_tuple__<field types>`.** The mangling grammar of section 1.5 assumes a source path, and a
  tuple has none. Labels are not part of type identity (gap 17), so `(lowest: Int, highest: Int)` and `(Int, Int)`
  are one layout and one name.
- **An `Intrinsic` is an operation plus an optional numeric type** (`add.i64`, `less.f64`, `convert.i32.i64`,
  `textConcat`), and it takes **slots**. The example in section 1.7 writes `%0.x` as an operand of an intrinsic, which
  the instruction cannot express - a field read is its own `Read`, which is the form the snapshot tests show.
- **Every parameter writes its ownership out** in the header of a function, not only `self` as in the example. It is
  what the summary pass of 5.4 decides, so a snapshot should pin it everywhere.
- **The verifier does not check that every managed slot is released on every path.** Nothing emits a `Release` before
  5.4, so the check would reject every function; it belongs to the pass that inserts them. Everything else the risks
  section asks for is here, plus operand and result types, witness sources, and "a block nothing reaches has to be an
  empty `unreachable` marker".
- **A closure counts as a niche payload** and is always treated as counted. Section 1.3 says "a `Closure` with an
  environment", but nothing in the *type* of a closure says whether it has one; the null test is on the code pointer,
  which is never null, and a retain of an empty environment is a no-op at runtime. `containsShared` is `true` for
  every `Object` (a trait-typed value may hold anything its bounds allow) and `false` for a `Closure` until 5.8 gives
  a closure type its environment layout.
- **`Expression`, `Task`, `Channel` and `Decimal` are `Runtime` kinds**, as section 1.3 lists them, although
  `Expression` declares fields in `std/prelude`. A `native type` that is *not* in that list and does declare fields -
  `Range` is the one - is an ordinary record; a `native type` with neither fields nor cases and no entry is an
  internal error of the compiler, reported through `IrProgram.problems`.
- **A trait name in a type position is an `Object`.** The checker lowers `List<Int>` in a type position to
  `Traits([List<Int>])` (a trait-typed value), not to the `ArrayList` that implements it, so that is what arrives
  here. `ArrayList<Int>` is the `Runtime(ListStorage, [Int64])`.
- **The monomorphization key of a *type* is the substituted checker `TypeId`.** Checker types are interned, so a
  closed `TypeId` is already the canonical name of one type; `Instantiation.memo` is keyed by it, and two routes to
  the same type share one IR type, one layout and one name. The key of an *instance* (`instanceKey`) is the symbol
  plus the ids of its arguments and witness tables, and it never leaves the run that built it - a *name* is what has
  to be stable across runs.

### What 5.2 does differently

Sections 1 to 3 are the plan; where they did not fit what 5.1 built, what the checker records or what stage 0 can run,
the code won and this is the list. Everything else is as written.

- **Eight files, and `Lowering` is the value everything is threaded through.** `ir/lower/context.trb` (the state, the
  reporting, the interning of locations and static values, the natives index), `ir/lower/expression.trb`,
  `ir/lower/statement.trb`, `ir/lower/call.trb`, `ir/lower/function.trb` (one body, the entry function, seeding),
  `ir/lower/lower.trb` (the worklist and what `torb ir` prints), `ir/instances.trb` and `ir/constant.trb`. The
  **builder is deliberately not a field of `Lowering`**: a lowering function takes `var lowering: Lowering` and
  `var builder: FunctionBuilder` side by side, so that emitting an instruction never reads a field of the same place a
  `var` argument has just taken out - trap 1 of `compiler/CONTRIBUTING.md`.
- **A construct of a later sub-milestone makes the function an `unreachable` stub, and the stub is already in the
  program.** The worklist adds every instance with a one-block `unreachable` body *before* it lowers it (a function
  that calls itself needs its own id), and a body that hits something outside this sub-milestone is thrown away. So a
  caller of it is still a well typed `Call`, the verifier is happy with the stub, and 5.5 to 5.9 are a sequence of
  replacements rather than a rewrite. Only the **first** construct per function is recorded: everything after it walks
  a body whose slots were abandoned, and one root cause gets one message - the rule the checker's diagnostics follow.
  The counts are therefore *per function blocked*, which is what makes them a progress bar.
- **`FunctionKind.Runtime(symbol)` was added.** A `native fn` the manifest maps to a function of `runtime/` is an
  `IrFunction` with a signature, no blocks and the C symbol in its kind - because there is no instruction for "call the
  runtime" and section 5.3's file list has no lowering in it, so the lowering of a native call has to be here. A call
  of one is an ordinary `Call`, the ownership pass of 5.4 reads its modes out of its signature like any other
  callee's, and `torb ir` prints it as `runtime fn t_... = torb_text_is_empty`. `panic` is the one exception: it has
  `Instruction.Panic` and an `unreachable` after it.
- **`==` and `!=` may name no numeric type.** The manifest of 5.R1 has `Bool.equals` and `Char.equals` as
  `IntrinsicOperation.Equal` *without* a kind while `Int64.equals` has one, and the verifier of 5.1 demanded a kind for
  every comparison. So the kind is now optional for exactly those two operations (`needsNumericKind` versus
  `allowsNumericKind`), and where it is absent the verifier demands that both operands have the same type instead.
- **The verifier's definedness analysis was wrong across blocks.** It intersected what was defined at the *beginning*
  of each predecessor, so nothing a predecessor's own instructions wrote ever reached its successors and every branch
  in a lowered function was reported. It now intersects what *leaves* each predecessor (`definedIn` union
  `definitions`), which is what "defined on every path" means.
- **`Copy` is what phase one emits to put a value in a slot**, and every operand position is `Borrowed`. Section 2.2
  says phase one writes no `Copy`, which is about the copies *ownership* needs; a binding initialized from another slot
  and an argument that is `Owned` are 5.4's, and rule 3 of section 2.1 ("a parameter whose only use is a `Copy` into a
  slot that is later returned") already assumes the `Copy` is there. The snapshots of this sub-milestone therefore say
  `borrowed` everywhere, including at `Construct` and `Return`.
- **A destination is passed down instead of joining afterwards.** `lowerExpression` takes the slot the caller already
  has - the local a value is bound to, the one slot both arms of an `if` produce into - so the arms of an `if` need no
  phi node and no move, `&&` is a branch over one slot, and a body's last expression is returned out of the slot it is
  already in. One value has one defining slot, which is what 5.4 needs.
- **One `Void` slot per function holds every result nobody takes.** An `if` without an `else` used as a statement, and
  every expression of a body that was abandoned, answer with that one slot rather than a temporary each.
- **`for` is lowered without an iterator only where the range is written out.** `for index in a..b` with both ends and
  an integer item becomes a counter, a `less` and an `add`; an inclusive range, a range without both ends and every
  other subject need `Iterable.iterator()` and the `Option` its `next()` answers, so they are counted and wait for 5.5
  and 5.6. `continue` jumps to the increment block, which is why the increment is a block of its own.
- **`while true` without a `break` diverges, tracked per loop.** The block after it stays an empty `unreachable`
  marker, and nothing is emitted into it - the verifier rejects a block that nothing reaches and is not one.
- **An irrefutable tuple pattern binds here, not in 5.5.** `const (line, column) = lines.lineAndColumn(offset)` is one
  `Read` of a `TupleField` step per position, which needs no decision tree at all; every pattern that decides which
  *case* a value is waits for 5.5.
- **A path in a location is the package's name plus the file below the package's directory**
  (`torbscript/compiler/src/ir/print.trb`), not the way the path was typed on the command line. A location reaches the
  generated C as a `#line` directive and the fixpoint of milestone 6 compares two C files byte for byte, so no working
  directory may leak into one.
- **The constant evaluator decides no type of its own**: every part of an initializer takes the type the checker
  recorded for it. It carried an expected type at first, because the initializer of an annotated top-level `const` was
  never checked; 4.7 checks it, and an expected numeric type flows through the arithmetic operators into the literals
  inside them (design 2.3), so `const minimum: Int8 = -128` and `const size: Int8 = 3 + 4` both have their `Int8` on
  every literal and the range check reads it from there.
- **A constant that is not compile-time evaluable is counted, not reported.** Gap 11 makes overflow, a division by zero
  and a `nan` compile errors at the expression, and those are `Diagnostic`s of the program. "This is not a constant at
  all" is a rule of the *checker* (gap 27, 4.9's) and would be a false positive here, so it is an unsupported construct
  like any other - which is what `const maximum: UInt64 = UInt64.minimum.bitwiseNot()` in the prelude is today.
- **`torb ir` seeds every monomorphic declaration of the modules it was asked about**, not only what the entry file
  reaches. "Nothing unreachable is emitted" is `torb build`'s rule (5.13), and a progress bar must not depend on what
  one entry file happens to call. `lowerWorkspace` takes the choice as a flag.
- **A chained call reads its target from the callee's span.** Reading the member of `text.trim().replace(a, b)` records
  `Resolution.Deferred` at the span of the inner call, over the answer the inner call had already put there, and the
  two spans begin at the same byte - so the span of the *callee* is what stands in. Whether a call has type arguments
  is asked of the call's own span, which is where they are recorded. See the records the checker does not have yet,
  below.
- **What `torb ir` prints:** the IR text, then the errors of the program, then the statistics with one line per
  unsupported construct sorted by count. `--statistics` leaves the IR out. Everything the verifier finds is printed as
  an internal error, because it is one.

**One record the checker does not have yet**, which the lowering works around rather than waits for:

1. **The resolution of a call whose receiver is a call is overwritten with `Resolution.Deferred`** (`memberTarget` in
   `semantics/checker/expression.trb` writes `checker.resolved(base.span, owner.resolution)` after the base has
   recorded its own answer). Needed: that `resolveTarget` not record `Deferred` over an answer that is already there,
   or a table keyed by the callee's span alone.

The second one, **the unchecked initializer of an annotated top-level `const`**, arrived with 4.7: it is checked
against its annotation, `const size: Int8 = 3 + 4` is an `Int8` sum, and the expected type the evaluator carried by
hand is gone.

It does not block 5.2; it would remove a workaround. The two tables the risks section asks for
(`bindings: List<BindingDeclaration>` and the per-closure escape flag) are **not** needed yet: a local is found by the
span of the name it was declared under, which is exactly what `LocalBinding.at` carries.

### What 5.3 does differently

Sections 3 and 4 are the plan; where they did not fit what 5.1 and 5.2 built, what portable C allows or what stage 0
can run, the code won and this is the list. Everything else is as written.

- **Five files, not two.** `backend/c/type.trb` (every IR type as C, the type definition of every layout and the order
  they have to come in), `emission.trb` (the lines, the findings, slot names, `#line` and `TORB_LOCATION`),
  `prototype.trb` (a manifest prototype, taken apart), `body.trb` (one function body) and `emit.trb` (the translation
  unit). `cli/build.trb` is the driver.
- **What the back end cannot emit yet is a *finding*, not an internal error.** The emitter answers its lines *and* a
  deduplicated list of constructs with the milestone that brings each of them ("a trait-typed value (milestone 5.6)"),
  and `torb build` prints them and refuses. So nothing ever reaches a C compiler that could fail there, and the
  progress of milestone 5 is measurable in the back end the same way `torb ir --statistics` measures the lowering.
- **Every type is checked before anything is written.** One pass over the layouts the emitted functions reach and over
  every slot, parameter and result asks whether there is a C spelling at all; everything after it can spell a type
  without asking. Layouts that nothing reaches are not emitted, which is what keeps a type the back end cannot spell
  yet (a closure, an `Array`, a `Task`) from refusing a program that never mentions it.
- **A runtime native is called only where the manifest's prototype says it can be.** The prototype text of
  `NativeEntry` is taken apart and compared to the signature the lowering built, spelling by spelling. That is what
  catches the two conventions of 5.R1 before the C compiler does: a native whose result is an `Option` or a `Result`
  answers `bool` and writes its payload through an out parameter, and one that takes a `var self` takes a pointer -
  both need a wrapper the lowering does not build yet, and both are a finding that names the symbol.
- **`Instruction.Call` carries no location, so a native that takes one cannot be called yet.** `torb_text_slice`,
  `torb_text_repeat` and `torb_list_with_capacity` take a `torb_location` for the panic they may raise, and the
  instruction has no field for it; guessing `torb_location_unknown` would make a panic message wrong, which is
  observable, so it is a finding instead. One `at: LocationId?` on `Call` removes it.
- **`Copy` retains and nothing releases yet.** `Copy` of a `Text`, a list, a map or a counted block is a real retain,
  because that is what the instruction means (section 1.2) - but the matching `Release` is 5.4's, so a program that
  builds a string leaks it. Leaking is not observable and a double free is, so this is the one thing that is
  deliberately still wrong. The two functions `torb_release` and `torb_make_unique` need per layout (`D_<layout>` and
  `R_<layout>`) are generated, for the layouts something really releases - which is none of them before 5.4, because an
  unused `static` function is a warning and the generated C compiles with `-Werror`.
- **Warning-free is part of the output, not of the flags.** `-Wall -Wextra -Werror` is passed on every build, so the
  emitter has to keep the C clean: every slot is zero initialized (`= { 0 }`, which is a universal initializer in C11)
  because a `goto` graph defeats a C compiler's flow analysis, a slot nothing reads is cast to `void` once, a label
  nothing jumps to is left out, a layout without fields carries one byte (an empty struct is not C), a string literal's
  byte array holds at least one byte, and a constant nothing names is not emitted at all. The output happens to be
  `-Wpedantic` clean as well (checked by hand with gcc 13), but the driver does not ask for it: `#line` and a `goto`
  graph are exactly the shapes a pedantic mode is most likely to complain about in a future compiler version.
- **A field is `f_<escaped name>`, a variant group `v<index>`.** A field called `default` or `int` would be a C keyword
  and a tuple position (`0`, `1`) is not an identifier at all, so every member carries the prefix. `torb_result` is the
  result pointer of a `.ByPointer` signature, which no mangled name can collide with.
- **`resultMode` is always `.ByValue` in practice**, so the `.ByPointer` path is written but unexercised: a layout above
  32 bytes is `Boxed` and therefore a pointer, and nothing else can be bigger than the 64 byte limit yet. A
  `FixedArray` will be the first one (5.9).
- **One profile, no cache, no output path from `project.trb`.** `torb build [path] [--emit-c] [--output <file>]` writes
  `<project>/build/release/program.c` and the binary next to it. `--profile`, the content-hash cache, `torb run` as
  build-and-execute, `torb test` and `buildOutput`/`buildTarget` in the manifest reader are 5.13's, as the table says.
- **The runtime is found by walking up from the working directory** (or `$TORB_RUNTIME`): until the compiler compiles
  itself it is always run from inside its own checkout, and a compiled `torb` will know where it was installed.
- **Three natives stage 0 was missing.** `Process.run(command, arguments): Result<ProcessOutput, IoError>` (the driver
  runs the C compiler with it; `.Planned(5.13)` in the manifest, because its runtime side is a `bool` plus out
  parameters and the wrapper around it), `File.createDirectory` (`.Planned(5.12)`, `mkdir -p` for the output directory)
  and `Environment.get`, which `std/environment` and the manifest already had. `std/process` gained `ProcessOutput`.
- **A `native type` the runtime does not represent yet is a finding of the lowering.** `IrProgram.planned` was added
  next to `problems`, and `instantiate.trb` reports `Script`, `Sandbox`, `Instant`, `File`, `Json` and friends as "not
  supported yet (milestone N)" instead of "internal error: the native type `Script` has no representation in the back
  end", which is what `torb ir ..` over the repository used to end in. `torb ir --statistics` prints them under the
  constructs.
- **The gate is not `print "Hello"`.** `print` is `print(...values: Show)`: a variadic list of trait-typed values, which
  needs the element descriptors of 5.7 and the witness tables of 5.6 - the emitter cannot reach it from here, and
  special-casing it in the compiler would let the two back ends disagree about what `print` does. The gate is therefore
  a program that computes with everything 5.2 lowers and ends in `Process.exit` with a computed code, plus one program
  per panic (`panic`, overflow, division by zero). They are in `bootstrap/tests/native/`, each with its `.expected`
  (stdout), `.exit` and, where it panics, its `.stderr`.
- **Stage 0 and a compiled binary do not agree about a panic yet.** The interpreter prints `error: <message>` with an
  absolute path and leaves with 1, the binary prints `panic: <message>` with a path relative to the workspace root and
  leaves with 101 (decided gap 9), and the messages of the checked arithmetic differ (`Integer overflow` against
  `` arithmetic overflow in `*` ``). The end-to-end test knows about exactly this one difference: for a program that
  panics, stage 0 only has to fail. Unifying them is 5.14's.

### What 5.4 does differently

Section 2 is the plan; where it did not fit what 5.1 and 5.2 built, or where it was wrong about a case, the code won and
this is the list. Everything else is as written.

- **Four files, and one of them is the whole answer to "which positions are there".** `ir/liveness.trb` (which slots a
  frame counts, postorder, the backward fixpoint), `ir/operand.trb` (`operandsOf` and `withOperands`, the one pair that
  knows the shape of every instruction, plus `SlotFacts` and `consumedSlotsOf`), `ir/ownership.trb` (the summary, the
  rewrite, `MakeUnique`, edge splitting) and `ir/ownership-verify.trb` (the invariants). A new instruction is added in
  `operand.trb` and the three passes over it need no change at all.
- **`Counted` is `containsCountedType`, not `isManagedType`.** The table of section 2.1 puts an "`Inline` layout of
  such" in the *not counted* row, meaning an inline layout of numbers - but an inline record of two `String`s carries no
  count of its own and owns one on each of its fields. So a copy of it retains both, a release of it releases both, and
  the pass tracks it like any other value. Only **`MakeUnique`** asks the narrower question (`isManagedType`), because
  only storage that is *shared* has to be made unique: a value in a slot is nobody else's. `expectManaged` in
  `verify.trb` was widened the same way, which is the one change this sub-milestone made to a file it did not own.
- **A retain is a `Retain`, not a `Copy`.** Section 2.2 step 4 says an `Owned` position whose operand is live afterwards
  "gets a `Copy` before the instruction". A `Copy` needs a target slot, so that would mean a new slot and a rewritten
  operand at every retain; `Retain(slot)` in front of the instruction plus `owned` on the operand is the same semantics
  with neither. Where the instruction *is* a `Copy`, the instruction carries the answer instead: a `Copy` whose source is
  at its last use becomes a `Move`, and one whose source is still live stays a `Copy`, which already means "retain".
- **The transfer function puts the definition before the uses, and step 4's order is not used.** Step 4 walks the
  operands against `liveOut` and asks about the defined slot afterwards, which makes an instruction that reads and writes
  one slot (`%2 = textConcat %2, %1`) release the value it has just produced. The transfer is
  `live before = uses union (live after minus the defined slot)` - kill, then gen - which is also what the set equation
  of step 3 says at block granularity.
- **An instruction that reads and writes one counted slot gets a `Move` in front of it.** `total = total + other` is
  `%2 = intrinsic textConcat %2, %1`, and the old value of `%2` has to be released *after* the concatenation read it and
  *before* the slot holds the new one - which no order of instructions can do while the old value has no slot of its own.
  A prepass writes `%9 = move %2` and rewrites the reads, and then every rule after it is the ordinary rule for a value
  that dies. It is the only thing the pass adds slots for.
- **The summary is a fixpoint over the call graph, not one pass.** Section 2.1's last bullet says the summary runs
  exactly once with no fixpoint between functions. But rule 3's own example needs one: `self.added(a).added(b)` makes the
  *caller's* `self` escape only because `added`'s parameter is already `Owned`, and one pass over the functions in
  lowering order would answer differently depending on that order. It is a worklist over the callers, ownership only
  ever grows from `Borrowed` to `Owned`, so the least fixpoint is unique, recursion is not a special case, and the answer
  does not depend on anything but the program. The rule itself is also generalized: a parameter is `Owned` when its value
  escapes the frame at all (returned, built into something, stored, handed to another `Owned` position, or copied into a
  slot that is later written through), not only when its "only use" is such a copy - which is strictly better, because a
  parameter that is read *and* returned then costs no retain either.
- **Borrowed and `var` parameters are not in a live set at all.** A value the frame does not own has no last use here:
  the caller keeps its count for the whole call, so it is never moved out of and never released, and where such a value
  is needed owned it is retained. Leaving them out makes that structural rather than a rule, and it is why
  `fn length(text: String)` comes out of the pass with not one instruction added.
- **An unmanaged operand keeps the mode phase one gave it.** Section 2.2 makes `Return` an `Owned` position, which for an
  `Int64` would print `owned` and mean nothing: nothing is counted, so nothing is decided. A function that counts
  nothing is not even walked, and the arithmetic half of a program is therefore byte for byte what the lowering wrote -
  which a test asserts by comparing the two texts.
- **A `Release` before a `return` is never needed, because a value dies at its last use.** Section 2.2 step 5 puts the
  early exits on the edges, and that is exactly what happens; what the section does not say is that nothing else is
  needed. The one value that dies without a use to die at is a parameter the caller handed over and the body never reads,
  and that gets its `Release` at the top of the entry block.
- **`Capture` has no room for a mode**, so a `Closure` retains what it captures and never moves it (`Capture.Value` and
  `.Boxed` are positions that take the count, but nothing can say `last` about them). The frame's own count still dies at
  its last use, so a capture at the last use is a retain and a release. 5.8 can do better once a capture carries a mode.
  `CallWitness` arguments are `Borrowed` for the same kind of reason: which parameters a witness member keeps is the
  signature of the member, which arrives with 5.6. Both are a missing `Owned`, which costs one copy and never
  correctness.
- **A signature that is *interned* is never changed.** A closure type is `Closure(IrSignatureId)` and the ownership of
  the parameters is part of a signature's interning key, so promoting one would change type identity. Only the signature
  a *function* carries by value is written to, and a `CallClosure` therefore reads `Borrowed` until 5.8 gives a closure
  its own summary.
- **`MakeUnique` is inserted by this pass, not by the lowering.** Section 2.2 phase 1 puts it in the lowering, "because
  it is a property of the path and not of liveness". It is - and there is no lowering that writes a place yet (an
  assignment to one is 5.9's), so the hook lives here, in front of every `Write` and every `TakeOut`, one per counted
  owner of the path and outermost first. A write the lowering already made unique is left alone, which is what a
  `MakeUnique` directly in front of it says, so 5.7 and 5.9 can take the decision over without this pass doubling it.
- **The verifier's invariants are a file of their own, and `verifyOwnedProgram` is the two verifiers together.** They are
  only true of the IR *after* this pass, so `verifyFunction` cannot demand them (a `torb ir --no-ownership` prints phase
  one and is verified by the old rules). What was added: every counted value is consumed or released exactly once on
  every path, as a forward analysis over four states per slot whose fourth state - owning on one path and empty on
  another - is how a leak on one path is found; nothing is used after it was moved out of; nothing is released twice; a
  slot is not overwritten while it still owns a value; a parameter the caller keeps the count of is never released and
  never moved out of; and a position that keeps a managed value says `owned`, with a call's arguments matching the
  callee's signature exactly.
- **What the emitter has to do, beyond the instructions.** Three contracts that are not instructions and that 5.3 has to
  keep, because the IR counts on them: a `Read` of a counted field hands the frame a count of it (it retains); a `Call`
  answers a value the caller owns; and a `Write` through a counted place releases what was there before it stores the new
  value - which is why the value operand of a write is `owned`. `Copy` retains, `Move` does not, and for an inline
  aggregate both of them mean "per counted field".

### What 5.R1 does differently

Sections 2 and 3 are the plan; where portable C or the split between runtime and program did not fit them, the code
won. Everything else is as written. The one-page version of the ABI is [runtime/README.md](../runtime/README.md).

- **Two conventions that section 3 does not name, and that half of the manifest needs.** A runtime function may not
  build a type of the *program*, because `Option`, `Result`, `Ordering` and `IoError` are layouts the lowering emits.
  So: a native whose result is an `Option` or a `Result` is a function that answers `bool` and writes the payload
  through an out parameter (`torb_list_get`, `torb_text_index_of`, `torb_file_read_text`), and the lowering builds the
  wrapper around it; a native whose result is `Ordering` is a `.Derived` entry. That is why `compare` is derived for
  every integer width, and why `Float64.compare` - a total order with `nan` above everything, which no comparison
  intrinsic gives - is the one `.Runtime` comparison.
- **`Release` and `MakeUnique` take the drop function as an argument.** `void torb_release(void *block,
  torb_drop_function drop)`, and make-unique takes a "retain the children" function as well, which is exactly the
  `{ shallow copy, retain the children, release the old }` of section 2.1. The alternative was a type descriptor
  pointer in every block header, which section 1.3 does not have room for and which would cost eight bytes per block.
  The emitter knows both functions statically, per layout, the same way it knows an element descriptor.
- **`NativeEntry` gained `prototype: String`.** The generated header has to be a pure function of the table, so the
  table carries the C prototype of every ready `.Runtime` target. `renderNativesHeader` then sorts by symbol and
  prints one comment line with the natives that share it. `torb natives --header` is the *command* and arrives with
  the driver (5.13); the renderer and the checked-in `runtime/include/torb_natives.h` are here, and two tests keep
  them together: `runtime/tests/natives_header_test.c` includes the generated header after `torb.h` so the C compiler
  compares both declarations of every symbol, and `compiler/tests/natives.test.trb` pins the table's shape and its
  counts. The byte-for-byte comparison of the file on disk needs file IO from a test, which is 5.13's.
- **`runtime/platform.c` is a file section 3.8 does not list.** Windows and POSIX behind five functions (path kind,
  working directory, directory listing, whole-file read and write), so no other file in the runtime has an `#ifdef`.
- **`absolutePath` is text arithmetic, as `std/fs` promises, so it needs no `realpath`.** The working directory plus
  the path, separators normalized to forward slashes, `.` and `..` resolved textually, a drive letter upper-cased.
  Deterministic and identical on both platforms, and it works for a file that does not exist.
- **The checked arithmetic is `static inline` in a second header** (`include/torb_number.h`), macro-generated per
  width, because the C compiler has to see through it: an addition is one instruction plus a branch and must not
  become a call. The consequence is that it can only be an `.Intrinsic` target - a `static inline` cannot also be
  declared with external linkage in the generated header - so `addedWrapping`, `multipliedWrapping` and the checked
  narrowing conversions of `TryFrom`, which the manifest maps to a runtime symbol, are ordinary functions in
  `number.c`.
- **`absolute` is derived, not an intrinsic.** `IntrinsicOperation` has no `Absolute`, and it should not: the body is
  `if self < 0 { -self } else { self }`, so the overflow of the smallest value falls out of `Negate` for free.
- **`print` joins in the runtime.** `print(...values: Show)` maps to `torb_print_parts(const torb_text *, size_t)`,
  which writes the shown parts separated by one space and one `\n` - the format stage 0 already prints. Putting the
  join in the emitter would let the two back ends disagree about it.
- **The float formatting is a `printf` search, not Grisu.** `printf("%.*e")` with 1 up to 17 significant digits, the
  first answer that `strtod` turns back into the identical bit pattern wins; then the notation is chosen from the
  decimal exponent (the exponent form below -6 and at 21 and above) and `.0` is appended where neither `.` nor `e`
  is in the result. That is by construction the shortest round-tripping decimal of decided gap 4, about 90 lines
  instead of 250, and it is the reference a Ryu routine will be tested against. `%g` cannot be used: it picks its
  notation from the precision it was given, so `100.0` would print as `1e2`.
- **An empty list allocates a storage of capacity zero.** `torb_list` is `{ storage, offset, length }` as section 3.1
  says, which leaves no room for the element descriptor - so the descriptor lives in the storage and an empty list has
  one. The alternative is a fourth word in every list value.
- **A text and a list are limited to 4 GiB and 2^32 elements**, because `offset` and `length` are `uint32_t` as
  section 3.1 writes them. Both limits panic rather than wrap.
- **`torb_list_storage` does not use `max_align_t`.** MSVC's C mode does not reliably declare it. The elements begin
  at the struct's size rounded up to the element's alignment, which `malloc` already satisfies.
- **The list's write path is one function.** `torb_list_prepare(list, extra)` makes the storage unique, moves a slice
  into a storage of its own and grows in one place, so "copy exactly once" is one piece of code rather than one per
  method. A slice never owns the whole storage, so a write through a slice always copies - which is what keeps it
  from reaching its parent.
- **Panics in the runtime's own tests are caught in process.** `torb_set_panic_hook` lets a test build `longjmp` out
  of a panic, compare the message and go on. Child processes were the alternative; this one is portable, identical on
  Windows and POSIX, and keeps the suite one binary. `torb build` never installs a hook.
- **The live-block counter is on in every profile,** not only in debug as section 3.8 says. It is one increment per
  allocation, the harness needs it after *every* test, and `--report-leaks` should not need a different build.
- **Case mapping and character classification are ASCII plus the letters of Latin-1,** with `isWhitespace` knowing the
  common `White_Space` code points. Full Unicode tables are milestone 8; the compiler's own identifiers are ASCII.
  This is the one place where the runtime is knowingly incomplete rather than unimplemented, and
  `runtime/README.md` says so next to it.

---

## 7. Risks and spec gaps

Every place where CONCEPT.md is silent or ambiguous for a back end, with a proposal in the spirit of the language:
consistent, stable, simple, deterministic. The section is quoted each time.

**1. Evaluation order is only half specified.** Execution Model: "Evaluation order is left to right." Silent on
receiver versus arguments, on `a[i] = f()`, on where a default argument is evaluated, and on the interaction with
`Adaptation.Reorder` (labeled arguments are written out of declaration order).
_Proposal:_ source order, always. The receiver first, then the arguments **in written order** (the reorder to
declaration order happens after evaluation, into slots), then the defaults in declaration order. In `place = value`
the place's own subexpressions come first, then the value. `&&`/`||` short circuit, `??` evaluates its right side only
when it is needed. _Reason:_ "left to right" can only mean the order a reader sees; anything else makes a side effect
in an argument unpredictable.

_Decision:_ accepted.

**2. Integer division and remainder are not specified.** Execution Model gives overflow ("Integer overflow panics")
and nothing else. `Divide` and `Remainder` are traits with no documented semantics for the built-in types.
_Proposal:_ truncation toward zero, remainder takes the sign of the dividend (`-7 / 2 == -3`, `-7 % 2 == -1`);
`x / 0` and `x % 0` panic; `Int.minimum / -1` and `Int.minimum % -1` panic as overflow. _Reason:_ C11 and Rust agree
on exactly this, so every back end gets it for one instruction instead of a correction.

_Decision:_ accepted. The spec says it without naming `Int.minimum` (which no type declares yet): "the smallest value
of a signed type divided by `-1`".

**3. The language has no bit operations, and cannot express a hash function.** Traits section: "Operators are traits:
`+` is `Add.add` ..." - the list has no `&`, `|`, `^`, `<<`, `>>`, and `Hash` is `native` for every primitive. But
`Hash` for a user type, UTF-8 decoding, the bytecode encoding and the build cache's content hash all need them, and
`+` panics on overflow so a wrapping hash cannot be written either.
_Proposal:_ `native fn bitwiseAnd/bitwiseOr/bitwiseExclusiveOr/bitwiseNot`, `shiftedLeft(by:)`,
`shiftedRight(by:)` on the integer types (arithmetic shift for the signed ones), plus `addedWrapping` and
`multipliedWrapping` on `UInt64` only. No new operators. _Reason:_ methods need no precedence rules and no new
tokens (`|` already means a literal-type union), keeping them off the signed types keeps "overflow panics" true
everywhere a `+` is written, and without them three parts of the toolchain cannot be written in TorbScript at all.

_Decision:_ accepted, with the names as spelled here. The six bit methods are one trait, `Bits`, that the eight
integer types come `with` (`std/prelude/src/numeric.trb`): they are then ordinary trait-member requirements on the
runtime - one manifest entry per member per width, section 3.7 - and usable as a bound, which a generic hash needs.
`addedWrapping` and `multipliedWrapping` are an `extend UInt64`. One detail the proposal leaves open is decided with
it: **a shift by a negative amount or by the width of the type or more panics,** like every other operation that
leaves its range.

**4. `Show` of a `Float` is unspecified beyond "a decimal point".** Gap 23 of TYPECHECKER.md: "a `Float` always
carries a decimal point". Stage 0 prints Rust's `to_string` (`[3.141592653589793, 6.0, 0.0]` in
`bootstrap/tests/scripts/basics.expected`), which is the shortest round-tripping decimal.
_Proposal:_ the shortest decimal string that parses back to the same `Float64`, with `.0` appended when the result
contains neither `.` nor `e`; `nan`, `inf`, `-inf`, and `-0.0` prints `-0.0`. _Reason:_ two implementations compare
their output through `Show`, and `printf("%.17g")` is neither shortest nor identical across libcs - so the runtime
carries its own conversion (a Grisu/Ryū shortest-representation routine, about 250 lines of C, tested by a round-trip
test over a million values).

_Decision:_ accepted.

**5. `==` and `compare` on floats contradict each other.** `numeric.trb` has `trait Numeric with ... Equals,
Compare, ...` and `public native type Float64 with Signed {}`, so a `Float` is both `Equals` and `Compare` - but
IEEE-754 equality is not an equivalence relation and there is no total order with `nan` in it.
_Proposal:_ `==` is IEEE-754 (`nan != nan`, `0.0 == -0.0`); `compare` is a **total** order that puts `nan` above
everything and treats `-0.0` as equal to `0.0`, so `sorted` terminates; `Float32`/`Float64` stay deliberately not
`Hash` (they already are not), so a float can never be a `Map` key and the `nan` key problem does not exist.
Document all three sentences next to `isCloseTo`. _Reason:_ any "fixed" equality would make `==` disagree with `<`,
and sorting must not depend on the comparison sort's pivot choice.

_Decision:_ accepted.

**6. The iteration order of `Map` and `Set` is unspecified, and it is observable.** Collections: "`Show` ... a `Map`
`["k": v]`", and [docs/ARCHITECTURE.md](ARCHITECTURE.md) makes the generated `Show` the contract between two
implementations. `basics.expected` contains `["a": 3, "b": 2, "c": 1]`; the bootstrap's own comment says
"deterministic iteration is a rule of the language".
_Proposal:_ **insertion order, for every `Map` and `Set` implementation**; removal does not reorder the rest; an
empty map shows as `[:]`. _Reason:_ iteration order reaches the output, so it is part of the language, and insertion
order is the only one a reader can predict. It costs the hash table an index vector (which makes iteration faster
anyway) and the persistent trie an insertion-order vector next to the HAMT.

_Decision:_ accepted.

**7. String offsets: only one error case is named.** Strings: "An offset inside of a character panics." Nothing about
an offset past the end, a reversed range, or where invalid UTF-8 could come from.
_Proposal:_ an offset greater than `byteLength()`, a start greater than the end, and an offset on a UTF-8
continuation byte each panic, with the offset and the length in the message. `String` is therefore always valid
UTF-8: the only ways in are literals, slices at character boundaries, `String.from(Iterable<Char>)` and runtime
functions that validate - so reading a file whose bytes are not UTF-8 is an `IoError`, never a replacement character.
_Reason:_ a total `String` removes a replacement-character rule from every back end and from `chars()`.

_Decision:_ accepted.

**8. Recursion depth is not mentioned, and "tail calls are guaranteed" cannot be kept in general.** Execution Model:
"Tail calls in tail position are guaranteed." Portable C cannot guarantee a general tail call, and the language has
no stack limit at all - while `retry` in `std/prelude/src/control.trb` is tail recursive and the parser recurses with
the nesting of an expression.
_Proposal:_ guarantee **direct self-recursion in tail position** (the lowering turns it into a jump to the entry
block, which both back ends do identically) and say so; every other call uses the stack. A per-task **frame counter**
with a limit (default 100 000, `--stack-limit`) panics with "stack overflow" - the only portable way to make the VM,
which has its own frame list, and the C binary, which has a C stack, agree on when it happens. _Reason:_ an
unspecified crash is not a semantics, and the guarantee that can be kept is the one that `retry` and every fold need.

_Decision:_ accepted.

**9. What a panic prints, what it runs, and the exit code.** Error Handling: "`panic "unreachable"` - Bugs. Not
catchable, aborts the task." Nothing about the message format, the exit code, whether `Close` runs, or what "the
task" means when there is one.
_Proposal:_ to stderr, `panic: <message>`, then `  at src/file.trb:12:5` for the panic site and, in the debug
profile, the frames of the task; exit code **101**; **no drop, no `Close`, no destructor runs** - a panic is a bug,
and running more code in a broken program is how bugs get worse. With no supervision in the language a panic aborts
the **process**; the one exception is a sandboxed script, which the VM stops and reports as a `SandboxError`
(gap 12). A top-level `?` is not a panic: it prints `error: <the error through Show>` and exits with 1.
_Reason:_ the panic message is output, so the conformance suite compares it, and the two back ends must agree on it
to the character.

_Decision:_ accepted.

**10. "Deterministic destruction" has no destructors behind it.** Execution Model: "Deterministic cleanup enables
`using file { ... }`", and Memory Model: "No tracing garbage collector. Deterministic destruction is part of the
language (`using`, `Close`)."
_Proposal:_ state that there are **no destructors**: `Close` is an ordinary method, `using` an ordinary function, and
the only observable destruction order is the nesting of `using` blocks. Releases happen where section 2 puts them and
are not observable. _Reason:_ a destructor would need drop flags, a field order rule and a story for a panic in one -
and `using` already covers everything that must be deterministic.

_Decision:_ accepted.

**11. Overflow and division by zero in a compile-time constant.** Gap 27 fixed what is compile-time evaluable
("plus the arithmetic ... of the built-in number types"), and Built-in Types says "overflow that is written in the
source is caught where it is written".
_Proposal:_ a compile-time constant whose evaluation overflows, divides by zero or produces a `nan` from a literal
expression is a **compile error at that expression**. One evaluator, in the checker, and the lowering asserts it
reaches the same value. _Reason:_ a program that cannot start is worse than one that does not compile, and this is
free.

_Decision:_ accepted.

**12. A sandboxed script has nowhere to report a failure.** Receiver Scripts: `Sandbox.load` returns
`Result<(var self: Value) => Void, SandboxError>`, and the capabilities include `limits steps:`. But the *script's
body* runs when the host applies that closure, and a closure of type `(var self: Value) => Void` cannot say that the
step limit was hit or that the script panicked.
_Proposal:_ `Sandbox.load<Value>(path, capabilities): Result<Script<Value>, SandboxError>` with
`Script.apply(self, var value: Value): Result<Void, SandboxError>`. Load-time failures (syntax, types, a module the
script may not import) come from `load`, run-time failures (steps, memory, time, a panic inside the script) from
`apply`. _Reason:_ a sandbox whose failures abort the host is not a sandbox, and the panic that is recoverable is
exactly the one that happens inside an interpreter with its own heap.

_Decision:_ accepted. `Script<Value>` is declared in `std/sandbox/src/lib.trb`; the call sites in CONCEPT.md and
`examples/config-dsl` load a script and call `apply` on it.

**13. Parameter defaults have no evaluation rule.** Arguments: `fn connect(host: String, port: Int = 5432)`. Gap 26
settled field defaults ("in a scope without `self` and without the other fields") but not parameter defaults - and
`SandboxCapabilities.limits` has `memory: Int = 64.megabytes()`, a call.
_Proposal:_ the same rule: a parameter default is evaluated **at the call site**, at every call, in the scope of the
declaration - no `self`, no other parameters. _Reason:_ one rule for both kinds of default, the lowering stays local
to the call site, and a default cannot depend on an argument order that is not visible.

_Decision:_ accepted.

**14. A closure that captures a `var` reference may not escape, and nobody records it.** `var` Paths: "References are
second-class. They only exist as a `var` parameter or `var self` ... They cannot be stored in a field, returned, or
captured by a closure that is stored."
_Proposal:_ the checker records per closure whether it escapes, and a closure may capture a `var` parameter or
`var self` **only** when it does not: conservatively, when it is written directly as an argument of a call and is not
stored by the callee. Everything else captures values and `Box`es. `ClosureKind.Local | .Escaping` goes into the
tables. _Reason:_ the lowering needs it to choose a stack environment (which is also the optimization that makes the
DSL and the pipelines free), and without it the rule quoted above is unenforced.

_Decision:_ accepted.

**15. `isSame` is callable on values.** `std/prelude/src/shared.trb`:
`public native fn isSame<Object>(first: Object, second: Object): Bool`, with no bound. On a value the answer would
expose whether the implementation shares storage.
_Proposal:_ the checker rejects `isSame` on a non-`shared` type, as a named special case next to object safety, with
the message "`isSame` compares identity, and a `Point` is a value. Use `==`." _Reason:_ the language has no bound
that says "shared", and adding one for a single function is worse than one special case in the checker.

_Decision:_ accepted.

**16. `Expression` quoting is not free after all.** Quoted Expressions: "Static data, created at compile time.
Quoting costs nothing at runtime", and `native fn captures(self): List<Encode>`.
_Proposal:_ correct the sentence: the **tree** is static data and costs nothing; the captures are collected at the
quotation site into a list of trait-typed values, so quoting a closure with captures costs one small allocation per
evaluation. _Reason:_ `captures()` has to return the current values, which cannot be static, and `assert` is in every
test - the cost should be written down rather than discovered.

_Decision:_ accepted. The Decision Log entry about quoting being free is corrected with it.

**17. `for x in ..10` and `Range.length()` of an open range.** `range.trb`: `extend Range<Int> with Iterable<Int>,
Length` with the comment "Panics for a range without both ends", and CONCEPT ("A `Range<Int>` with a start is
`Iterable<Int>`") makes it a type-level condition that the type cannot express (the ends are `Option` fields).
_Proposal:_ `iterator()` on a range without a start panics ("a range without a start has no first value"), `length()`
on a range without both ends panics, `0..` iterates forever, and both messages are pinned by the conformance suite.
_Reason:_ the condition is about a field's value, so it belongs at runtime; a compile-time version would need
dependent types.

_Decision:_ accepted.

**18. The manifest of natives is also a list of what does not exist yet.** Decided gap 22 requires that a missing
intrinsic is a compile error. Today `std/` declares `Decimal`, `Float32` arithmetic, `TrieList`/`TrieMap`/`TrieSet`
and all of `std/http` as `native`.
_Proposal:_ `NativeState.Planned(milestone)` (section 3.7): using one is a clean compile error naming the milestone,
never a link error; the trie names are documented aliases of the array and hash implementations until milestone 8,
which is observable only through performance because iteration order and `Show` are the same either way. _Reason:_
the alternative is a binary that fails at link time with a mangled name in the message.

_Decision:_ accepted.

**Risks, not gaps.**

- **Stage 0 has to run the lowering.** It has no type checker, so a lowering bug shows up as a wrong C file, not as
  an error. Mitigation: the IR text snapshots, an IR **verifier** (every slot defined before use, every block
  terminated, every managed slot released on every path) that runs in the debug profile of `torb build` and in the
  tests, and the live-block counter.
- **The instance count.** Monomorphization over `compiler/` plus `std/` with `List<Item>` everywhere can multiply.
  Mitigation: per-bound sharing (section 1.4), the element-descriptor design that keeps containers out of the
  instance set, and an asserted instance count in the conformance suite so an explosion fails a test.
- **MSVC C11.** Flexible array members, `_Noreturn`, `<stdbool.h>` and designated initializers are fine from
  VS 2019 16.8; `__builtin_*_overflow` is not, hence the portable fallback (section 3.3). CI builds the runtime with
  all three compilers.
- **The fixpoint compares C, not binaries.** A C compiler may embed paths or timestamps in debug information and is
  not required to be deterministic under link-time optimization. The gate is "stage1 `program.c` equals stage2
  `program.c` byte for byte", plus "both binaries pass the conformance suite". Stated so nobody chases a
  nondeterminism that is not ours.
- **Two tables milestone 4 does not export yet.** The lowering needs `bindings: List<BindingDeclaration>`
  (owner, `isVar`, type - to find the captured `var` bindings) and the per-closure escape flag of gap 14. Both are
  additive to `Tables` and should be agreed with the checker's agent before 5.2 starts.
