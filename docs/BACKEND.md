# The Back Ends (Milestones 5-7)

**Status: historical** — the plan and the log of milestones 5 and 6, which are done, kept as the record of how the
back end was built; the parts of milestone 7 that are still ahead are marked as such.

> **A historical record.** This document was written while stage 0 — the Rust interpreter and front end under
> `bootstrap/` — still existed, and most of it reads in that present tense: "compared with stage 0", "the gate runs on
> stage 0 and natively". Stage 0 was deleted on 2026-09-22 (`docs/RUST-EXIT.md`); the C back end is the only
> implementation today and the bytecode VM of milestone 7 will be the second. Where a row of section 6 or a gap of
> section 7 decides something that still holds, it holds; where it describes stage 0, a cycle collector or "no
> destructors", read it as history and follow the note beside it — `docs/design/DESTRUCTORS.md` and `docs/design/CONCURRENCY.md`
> are where those questions are decided now.

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
  /** Computed to a fixpoint over the field graph. Drives the `spawn`/`Channel` rules. */
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
  dictionary-passing path, and it is what keeps `List<Show & Hash>` from exploding the instance count.
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
- **A trait-typed value whose payload the whole program agrees on is that payload.** `ir/devirtualize.trb` runs between
  the lowering and the ownership pass: it joins every slot and every function result a value flows between into one
  class, and a class whose producers are all a `TraitValue` of the same payload with the same static tables loses its
  box - the slots, the parameters and the result take the concrete type, the `TraitValue` becomes a `Copy`, and every
  `CallWitness` becomes the direct `Call` of the member the table names. So dictionary passing is what a value with two
  implementations in one place costs, not what every collection literal costs.
  ([docs/PERFORMANCE.md](PERFORMANCE.md) finding 2.)
- **A frozen member gets a copy for the call sites that know the payload.** A member of a witness table may not move
  its signature, so `ArrayList.iterate` answers the boxed `Iterator<Item>` however concrete the receiver is. The pass
  therefore runs twice, and between the two runs `specializeFrozenCallees` copies every frozen function a **direct**
  call already names and whose signature holds an `Object` - the table keeps the original, the direct call site gets
  the copy, and the second run reads the copy's result and parameters as ordinary locations. That is what makes the
  cursor of a `for` over a list, a map, a set or a string a record on the frame with a direct `next`
  ([docs/PERFORMANCE.md](PERFORMANCE.md) finding 5).

### 1.5 Mangling

Deterministic, ASCII, collision free, and stable under anything that does not change the program:

```text
name      ::= prefix "_" package "_" module "_" member ( "__" instance )?
prefix    ::= "t" function | "T" layout | "w" witness table | "d" element descriptor | "s" static value
component ::= escaped  ( [A-Za-z0-9] kept, everything else "_xHH_" of its UTF-8 bytes )
instance  ::= the canonical type argument list, escaped, at most 160 characters,
              plus "_h" and 16 hexadecimal digits of its FNV-1a-64 when it was longer
```

`t_std_x2f_collections_list_ArrayList_add__Int64`. Ids never appear in a name: a name is a function of the
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
| `for x in xs`                | `xs` into a temporary, `Iterate.iterate()`, loop head calls `next()` on a local `var`, `Switch` on the `Option` |
| `"{a} and {b}"`              | `Show.show`/`showNested` per part, then one `Intrinsic.TextConcat(parts)` - never repeated `String.add`      |
| defaults                     | `Adaptation.DefaultArgument`: the default expression is lowered **at the call site**, after the written arguments |
| variadics / `...e`           | Build a list at the call site; `Spread` calls `addAll` through the recorded `Iterate` witness                |
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
| `Shared`, `Box`                                    | yes (the header's `color` stays unused: there is no cycle collector, section 2.4) | as above |
| `Reference` (`var` parameter, a `var fn` receiver, `var` path) | **no**  | section 2.5                                      |

- **A slot has exactly one owner.** Every operand position is `Borrowed` (the value must be live at that point; no
  count changes) or `Owned` (the position takes the count).
- **Parameters are borrowed by default.** The caller keeps the count for the whole call. That removes a
  retain/release pair from every call, which dominates in a compiler that passes `Token`s and `String`s everywhere.
- **A parameter is `Owned` when the callee stores the value.** Two sources, no interprocedural analysis:
  1. The natives manifest declares it (`ArrayList.add`, `Map.set`, `Channel.send`, `ArrayStack.push`, ...).
  2. `Construct`, `BoxNew`, `TraitValue` and `Return` are always `Owned` positions.
  3. A **summary** per function, computed from the already lowered body (section 2.2, phase 2): a parameter whose
     only use is a `Copy` into a slot that is later `MakeUnique`d or returned is `Owned`.
     Rule 3 is exactly what makes the participles free: `fn added(value: Item): Self { var result = self  result.add(value)  result }`
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

Therefore an interior pointer needs no count, no lifetime and no write barrier. `MakeUnique` is what makes the pointer
safe to write through: after it the storage has count 1 and, by exclusivity, one path. It happens **after the arguments
of the call have been evaluated**, which is where the `var` access begins (TYPECHECKER 5.2, after the model of Swift) -
not when the path is formed. That is what makes `items.removeAt(items.length() - 1)` work: the read of `items` for the
argument finishes first, and only then is the storage made unique. The **one** exception to the reference rules is a
captured `var` binding, which is a `Box` and not a reference (gap 19): it is counted, it may escape, and it is the only
sharing of a variable in the language.

`TakeOut`/`PutBack` is the second form, for containers whose storage is not contiguous (the hash map, the tries): read
the value into a slot, run the access against a `Reference` to that slot, write it back. Literally the concept's "take
it out, change it, put it back - without a copy" (`var` Paths section). The lowering picks the form by the container's
`RuntimeKind`, so both back ends do the same thing.

### 2.4 Cycles

**There is no cycle collector, and there will not be one** (decided 2026-09-22, `docs/design/DESTRUCTORS.md` section 9). The
plan written here first — Bacon/Rajan synchronous cycle collection with trial deletion over `Shared` objects and
`Box`es, a color and a `trace` function per header, a candidate buffer per task — is withdrawn: a tracing pass over a
plain-refcount heap would make the moment a destructor (`close()`) runs non-deterministic again.

- Values cannot form cycles. A `Box` cannot either, once a closure that captures a `var` binding may not escape its
  scope (CONCEPT, "`var` Paths and `var` Parameters"; decided, not yet enforced), so only `Shared` objects can.
- Cycles between shared objects are designed out instead: trees and graphs hold handles (an `Entity`), a callback a
  `shared type` stores takes its owner as a receiver, and `Weak<Target>` comes to `std` only if the leak reports show a
  need.
- **A leaked cycle is visible.** `torb build --report-leaks` prints the live block count at exit, the conformance suite
  and every `std` test assert zero, and the report is to name the type of each block still alive.
- The header's `color` field is `TORB_COLOR_NONE` for every block and stays until the header is next reorganised.

### 2.5 Non-atomic counts, per-task heaps, channel transfer

Counts are plain integers. Every task owns a heap (a bump allocator with size-class free lists); nothing is shared
implicitly, so nothing has to be atomic. (The candidate buffer of the withdrawn cycle collector is gone with it.) Blocks carry the id of their owning heap in the
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

**A check that cannot fire is not emitted.** `ir/ranges.trb` is a forward interval analysis over one function - an
integer constant, the operation that computed a slot, and the comparison a `Branch` stands on are its only facts - and
an `Add`, `Subtract`, `Multiply` or `Negate` whose result provably fits its own width is marked `isChecked: false` on
the `Intrinsic`. It prints as `add.i64.unchecked` and the C back end emits the plain operator for it. Nothing about the
*semantics* moves: unknown is the whole range of the type, an operation whose exact result interval does not fit an
`Int64` keeps its check, and a dropped check that could fire would be a missing panic - which is why the analysis
widens rather than guesses at every step ([docs/PERFORMANCE.md](PERFORMANCE.md) finding 8).

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

#### One conversion, one implementation

The manifest is keyed by `"{owner}.{member}"`, and a type may have one member name for **several** conversions:
`Int64.tryFrom` reads text and narrows a `Float64`, which are two functions of the runtime. Two rules keep that
unambiguous, and both of them matter for the same reason - the name alone is not the question.

- **Every row of such a member carries its source in parentheses**, and none of them is written without one:
  `Int64.tryFrom(String)` and `Int64.tryFrom(Float64)`. `nativeNamed` asks the plain name first and the qualified one
  after it, so nothing else in the manifest pays for this and a member that has only one row is found as before. A
  source that has **no** row is then a clean finding instead of the other row's function: `Int8.tryFrom(300)` says the
  back end does not support it, where an unqualified `Int8.tryFrom` row would have narrowed text.
- **A type implements every conversion it has directly**, and never leaves one to a trait that requires it. `Numeric`
  requires `TryFrom<String, NumberParseError>`, and each numeric type still writes that `extend` itself: with only the
  requirement, `TryFrom` would have exactly one *direct* implementation on `Int64` - the narrowing one - and
  `resolveTrait` would answer that one for both calls rather than `Ambiguous`, which is what makes the checker fall
  back to the applied bound (`dispatchOf` in `semantics/checker/member.trb`). The remaining hole is a **user** type in
  that shape: a `type` that gets `TryFrom<String, _>` from a trait it comes `with` and writes another `TryFrom` of its
  own reaches the wrong implementation, and the IR verifier is what catches it.

`Int64.from` is in the same family and is **not** qualified: its single row converts through
`IntrinsicOperation.Convert`, which is the same cast for every one of its seven sources, so the one row is right for
all of them. The rows `Int64.fromChar` and `Float64.fromFloat32` are named after no member and are reached by nothing.

### 3.8 `runtime/` and its tests

```text
runtime/
├ include/torb.h              Public header: header, text, list, map, closure, panic, task
├ include/torb_natives.h      Generated from the manifest (torb natives --header)
├ memory.c                    Heap, header, retain/release/make-unique, immortal values
├ panic.c                     torb_panic, overflow, bounds, the shadow stack, exit codes
├ text.c                      UTF-8, slices, concatenation, comparison, hashing, float and integer formatting
├ list.c  map.c               The one list and the one ordered hash table, over element descriptors
├ number.c                    Checked arithmetic, conversions, parsing, the bit and wrapping operations of gap 3
├ console.c  process.c        print/printError, arguments, exit
├ file.c  clock.c  environment.c   std/fs, std/time, std/environment
├ task.c                      Scheduler, Task, Channel (milestone 7.3)
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

**As built for the C back end** (`docs/design/CONCURRENCY.md` section 16, "The compiler half, as built"): no block is
split and there is no `TaskFrame` layout. The lowering makes the resume function (`FunctionKind.TaskResume`, its result
the `Value`) and a constructor that is one `TaskNew`, and writes a `Suspend` in front of the call that reads an
`await()`; the ownership pass runs over the resume function unchanged. What lives in the frame and what every stop
releases are read off the finished body (`ir/suspension.trb`), and the back end writes the frame as a struct of those
slots, the switch over the state, and one stop path per state and per loop back-edge.

The **scheduler is a FIFO run queue**, single threaded in 7.3, in `runtime/task.c` and in the VM with the same
algorithm and the same order - so a program whose tasks do no real IO produces identical output in both back ends,
and `10-async.trb` has a stable `.expected`. `Channel` is a ring buffer plus two waiter queues; `send` on a full
channel and `receive` on an empty one suspend. `all(a, b)` and `Task.all` are ordinary functions over that.

7.7 adds threads: one worker per core, one heap per worker, and a task that has **started** is pinned to its worker,
because moving it would move a heap. `spawn` puts the task in a worker's inbox as a transferable message, and the
worker that runs it first copies the captures into its own heap then — so a task that has not started yet owns nothing
in any heap and **may be stolen by any idle worker** (`docs/design/CONCURRENCY.md` section 9 and slice H, which replaced the
first plan here of round robin with no stealing at all). `await` across workers sets an atomic flag and enqueues on the
owner. Only the channel transfer, the inbox and the ready flags are atomic; counts stay plain (section 2.5).

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
`tests/language/*.trb` with their `.expected` files, extended by `.expected` files for the tour, and it is
run against **the C back end and later the VM** by the same runner (and was run against stage 0 until stage 0 was
deleted).

| # | Scope | Files | Tests | Depends on |
|---|---|---|---|---|
| **5.1** | **Done.** IR data types, layouts, the representation classes, the layout model, mangling, instantiation from a checker type, the builder, the verifier, the text format | `ir/ir.trb`, `ir/layout.trb`, `ir/mangle.trb`, `ir/instantiate.trb`, `ir/build.trb`, `ir/verify.trb`, `ir/print.trb` | Hand-built IR to text; mangling is stable and collision free; layouts and instantiation by table | M4.1 |
| **5.R1** | **Done.** Runtime skeleton and the manifest format: header, heap, retain/release/make-unique, `torb_text`, panic, overflow, console, the manifest with the header it renders, the C test harness | `runtime/*`, `backend/c/natives.trb` | `runtime/tests` (67, zero live blocks after each); the generated header compiles against the runtime and the table's shape is pinned | - |
| **5.2** | **Done.** Lowering: functions, slots, blocks, literals, locals, arithmetic intrinsics, calls of top-level functions and concrete methods, fields, constructors, tuples, `if`, `while`, `for` over `Range<Int>`, `return`, blocks, the const evaluator, static values | `ir/lower/*.trb`, `ir/instances.trb`, `ir/constant.trb` | IR snapshots for ~15 small programs | 5.1, M4.4 |
| **5.3** | **Done.** The C emitter, minimum path. Types, functions, blocks and gotos, `#line`, slot declarations, static data, `main`, the driver's minimum (`torb build`, compiler discovery, `--emit-c`). **Gate: a program of monomorphic functions becomes a native binary and behaves like it does on stage 0** (`print` needs the witness tables of 5.6) | `backend/c/type.trb`, `emission.trb`, `prototype.trb`, `body.trb`, `emit.trb`, `cli/build.trb` | `compiler/tests/emit-c.test.trb` (20); `tests/conformance/*` compiled, run and compared with stage 0 by the conformance runner; `--emit-c` twice is byte identical | 5.2, 5.R1 |
| **5.4** | **Done.** Ownership: the summary pass, liveness, `Copy`/`Move`/`Retain`/`Release` insertion, edge splitting, `MakeUnique`, and the verifier's ownership invariants | `ir/liveness.trb`, `ir/operand.trb`, `ir/ownership.trb`, `ir/ownership-verify.trb` | IR snapshots pinning every insertion point (45 tests in `ownership`, `liveness`, `operand`, `make-unique` and `ownership-verify`); hand-built wrong IR against every message of the verifier; the live-block counter is zero after every conformance script (from 5.3 on) | 5.2 |
| **5.5** | **Done.** ADTs: variant layouts, the niche, `MatchPlan` to decision trees, guards and fallbacks, case constructors, `Option`/`Result`, `?` with its conversion, `??`, `if const`/`while const`, destructuring bindings | `ir/decision.trb`, `ir/lower/match.trb` | `compiler/tests/decision.test.trb` (6 decision trees as text), `lower-match.test.trb` (10 IR snapshots, every one through `verifyOwnedProgram`), `emit-c` additions; `tests/conformance/{adts,errors,matching,states}.trb` run natively with zero live blocks | 5.2, 5.4 |
| **5.6** | **Done.** Generics: instance keys with type arguments, the worklist, witness tables, trait-typed values, per-bound sharing, derived `Show`/`Equals`/`Hash`/`compare`, trait defaults and overrides. **Gate: `tests/conformance/{traits,generics,derived}.trb`** - `basics.trb` needs 5.7 to 5.10 as well (see the note below) | `ir/witness.trb`, `ir/lower/generic.trb`, `ir/lower/derive.trb`, `backend/c/emit.trb` | `compiler/tests/lower-generics.test.trb` (8, instance counts among them), `emit-c` additions (5 pinned C snippets), three native gate programs with zero live blocks | 5.5 |
| **5.7** | **The lists run.** The ABI of the containers, `var fn` members of a **trait-typed value**, the witness of a value as a *place*, element descriptors, `ContainerNew`, the list literal, `a[key]` reads, `for` over a collection, a range as a value; then **the bound on the instance set** (a default nothing overrides is no slot of a table), **nested tables**, `ArrayList.from` and `Range.iterate`/`length`/`show` as TorbScript. **Done since:** the map/set cursor with the map and set literals (`tests/conformance/maps-and-sets.trb`), index paths (`collection-places.trb`, and PERFORMANCE round P7's in-place write), variadics and the spread (`variadics.trb`). **Done in the long tail of 6.1:** list patterns, `String.chars`/`bytes`/`from` and `String.slice` (see the note at the end). **Gate: `tests/conformance/{collections,ranges,collection-index}.trb}` - `language.trb` is a stage-0 script and not a checked program (see the note)** | `ir/element.trb`, `ir/lower/{native,collection}.trb`, `ir/witness.trb`, `backend/c/{natives,emit}.trb`, `std/collections/src/list.trb`, `std/core/src/range.trb` | `tests/conformance/{natives,reassignment,trait-values,collections,ranges,collection-index}.trb`, `compiler/tests/{lower-natives,ir-elements,lower-generics}.test.trb`; `07-collections.trb` is two index-path findings away | 5.3, 5.6, 5.8 |
| **5.8** | **Done.** Closures: closure conversion, environments, escaping or not, boxes for captured `var` bindings, `lazy` cells, function values, receiver closures, property commands. **Gate: `tests/conformance/{closures,counted-closures,dsl}.trb`** - `examples/config-dsl` loads a receiver *script* (7.4) and needs 5.7 and 5.10 besides (see the note below) | `ir/lower/closure.trb`, `ir/capture.trb` | `compiler/tests/lower-closures.test.trb` (19: the IR text, the pinned C, the findings); three native gate programs with zero live blocks | 5.6 |
| **5.9a** | **Done.** `var` parameters and `var fn` receivers: a place as an argument, interior projections through fields, assignment and property commands through a path, `MakeUnique` per counted owner of the path | `ir/lower/place.trb` | `compiler/tests/lower-places.test.trb` (23: the IR text, the pinned C, the verifier's invariants); `tests/conformance/{places,place-counted}.trb` run natively with zero live blocks | 5.4 |
| **5.9b** | The rest of the `var` paths: index paths (`TakeOut`/`PutBack`), slices as windows, `if var`/`while var`, `shared type` objects with their headers and trace functions, `FixedArray`, `Close`/`using` | `ir/lower/place.trb`, `runtime/memory.c` | `01-bindings-and-values.trb`, `03-types.trb`, `08-control-flow.trb` | 5.9a, 5.7 |
| **5.10** | **Done, except what needs a collection.** Text: interpolation, `print`/`printError`, `Show` for every shape in the format of gap 23, float formatting in both back ends, `?.`. **Still open:** `describe`, derived `Encode`/`Decode` and the `std/json` natives, which all wait for 5.7 (see the note below) | `ir/lower/text.trb`, `ir/lower/match.trb`, `runtime/text.c` | `compiler/tests/lower-text.test.trb` (19); `tests/conformance/{interpolation,floats,optional-chain}.trb` run natively, compared with stage 0, zero live blocks | 5.6, 5.7 |
| **5.11** | **The gate holds.** `assert` is lowered, `test`/`group` are functions of the runtime with a recovery point, `torb test <directory>` builds **one** binary for every test file and runs it, and `\n` is `\n` on both implementations. **Gate: `compiler/tests/*.test.trb` run from the native binary - 1482 passed, 0 failed (55 files), the same report as stage 0 line for line.** **Still open:** an `Expression<Value>` as a value - the static tree, `value()`, `captures()` - which nothing of `compiler/` needs | `ir/lower/quote.trb`, `ir/lower/match.trb`, `runtime/test.c`, `runtime/panic.c`, `runtime/console.c`, `cli/test.trb`, `backend/c/emit.trb` | `tests/conformance/{tests,test-failure,assert-values,nested-list-patterns}.trb`, `binary-only/assert-compound-capture.trb`, and the suite gate that compared the two reports | 5.10 |
| **5.12** | **Done for what the compiler needs** (`File.createDirectory`, `Process.run`, `Clock.milliseconds` and the `.Fallible` shape of `std/fs`, see the note of 6.1's long tail). **Runtime half done.** The remaining std natives: `std/fs`, `std/io`, `std/process`, `std/time`, `std/math`, `std/environment`. **Gate: the tour runs** (01-09, 11, 12; `10-async` waits for 7.3) | `runtime/file.c`, `clock.c`, `environment.c`, `number.c` | `.expected` files for every tour module, run on stage 0 and natively | 5.3 (parallel with 5.8-5.11) |
| **5.13** | **Partly done; no profiles yet** (`cli/build.trb` passes `-O2` to every build). The full driver: profiles, the content-hash cache, `torb run` as build-and-execute, `torb test`, output paths from `project.trb`, ICE reporting, `--emit-ir`, the `error:` report of a top-level `?` (it walks `cause()`) and `?` return traces in the debug profile | `cli/build.trb`, `cli/run.trb`, `project/manifest.trb` | Cache hit and miss, a deliberately broken emitter reports an ICE, an error chain of three prints three lines | 5.3 |
| **5.14** | **Done.** Conformance: one runner over stage 0 and the C back end that compares standard output, standard error and the exit code with nothing exempt; the panic format and every recorded divergence closed; `--emit-c` twice byte identical; no absolute path in the output | `runtime/text.c`, `tests/conformance/`, the runner | **57 gate programs**, each run twice and compared byte for byte | 5.1-5.13 |
| **6.1** | **Done.** Compile `compiler/` with stage 1: every missing intrinsic, every crash, every construct the compiler uses and the lowering does not cover yet. **Gate: a `torb` binary exists** | wherever it hurts | `torb check ..` from the new binary gives the same output as stage 1 | 5.14 |
| **6.2** | **Done.** The fixpoint: stage 2 compiles `compiler/` again, the two C files are compared byte for byte, stage 3 emits a third one. `bootstrap/` frozen | `std/iteration/src/concatenate.trb`, `runtime/platform.c` | **The fixpoint gate** | 6.1 |
| **6.3** | **Measured, and one third of it done** (see "What 6.3 measured"): the flags stay, the translation unit is **not** sharded (4.3x faster to compile, 2.3x slower a binary), one witness thunk per member instead of per table entry (-13.3% of the C, -22% of the gcc), the module `const` of the lexer read once per file. **Left:** the mangled names (62.5% of the file), the element-type-blind collection defaults, `R_`/`D_` keyed on a layout's shape, `#line` behind a profile, a budget for `torb build` of the workspace. **The immortal counted static is done** (see the note of its own) | `backend/c/emit.trb`, `syntax/lexer.trb` | A timing test in the suite | 6.2 |
| **7.1** | Bytecode: the format, the emitter from the IR, a disassembler for the snapshots | `backend/bytecode/*.trb` | Disassembly snapshots next to the IR snapshots | 6.2 |
| **7.2** | The interpreter loop, `torb run` through the VM, the conformance suite through the VM. **Gate: C and the VM agree on every script** | `vm/*.trb` | The full suite, both back ends | 7.1 |
| **7.3** | **Done for the C back end** (`docs/design/CONCURRENCY.md` section 16): the lowering of task functions, `spawn`, `await()` and `outcome()`, the cancellation checks, `Channel`, the main task, the FIFO scheduler in C; the VM half waits for the VM, and `10-async.trb` for a `From` conversion of `?` and `std/http`. Tasks: the state-machine transformation in the lowering, `Task`/`spawn`/`await()`/`Channel`, the FIFO scheduler in C and in the VM. **Gate: `10-async.trb` in both back ends** | `ir/lower/task.trb`, `ir/suspension.trb`, `runtime/task.c`, `vm/task.trb` | `10-async.trb`, channel and ordering tests | 7.2 |
| **7.4** | The sandbox: `Script<Value>` (gap 12 below), capability checks at import, limits as counters, panics recovered, embedding the front end | `std/sandbox`, `vm/sandbox.trb` | A script that loops forever, one that imports what it may not, one that panics | 7.2 |
| **7.5** | `project.trb` as a receiver script: `Project` and friends as real types, the static reader deleted after a test asserts both agree | `project/model.trb`, `project/manifest.trb` | Every `project.trb` of the repository, both readers | 7.4 |
| **7.6** | The REPL: the scope chain, the persistent frame, shadowing, generations of redeclared types | `cli/repl.trb`, `vm/session.trb` | A transcript test | 7.4 |
| **7.7** | Threads: workers, per-worker heaps, channel transfer, the inbox and the stealing of unstarted tasks (`docs/design/CONCURRENCY.md` slice H). **No cycle collector** (section 2.4, `docs/design/DESTRUCTORS.md` section 9): the leak report names the type of every block still alive instead | `runtime/task.c` | Parallelism tests, and a leak report test for a program that builds a cycle | 7.3 |

```text
5.1 ─► 5.2 ─► 5.3 ─┬─► 5.4 ─► 5.5 ─► 5.6 ─┬─► 5.7 ─┬─► 5.9b ┐
5.R1 ──────────────┘                      ├─► 5.8 ─┘        ├─► 5.14 ─► 6.1 ─► 6.2 ─► 6.3 ─► 7.1 ─► 7.2 ─┬─► 7.3 ─► 7.7
                   └─► 5.13 ──────────────┤   5.10 ─► 5.11 ─┤                                            ├─► 7.4 ─┬─► 7.5
                       5.12 ──────────────┴─────────────────┘                                            │        └─► 7.6
```

- **5.R1 and 5.12 are the parallel track**: the runtime in C, against the manifest, by an agent who never touches the
  lowering. 5.1 defines the manifest's shape, so 5.R1 can start immediately after it.
- 5.7, 5.8 and 5.12 are independent of each other; 5.9 and 5.10 are independent; 5.13 only needs 5.3. **5.9a needs
  only 5.4** - a `var` parameter is a pointer and no container - so it was pulled in front of 5.6 and 5.7; 5.9b is what
  is left of the row and waits for the containers.
- **Hello world is native at 5.3. The tour runs at 5.12. The fixpoint gate is 6.2.**
- **5.12's runtime half is done**: `runtime/file.c` (open handles), `clock.c`, `environment.c` and the math functions
  in `number.c` all exist and are `.Ready` in the manifest, with `runtime/tests` for each (see `runtime/README.md`
  for the representations chosen - nanosecond `Instant`/`Duration`, the `torb_file` `shared type`). The stream side of
  a file (`File.create`/`chunks`/`add`/`end`) stays `.Planned` for 7.3 with the rest of `std/stream`
  (`docs/design/STREAMS.md` section 14), and `file.lines()` needs no native of its own any more. The tour itself still
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
  `Expression` declares fields in `std/expression`. A `native type` that is *not* in that list and does declare fields -
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
  other subject need `Iterate.iterate()` and the `Option` its `next()` answers, so they are counted and wait for 5.5
  and 5.6. `continue` jumps to the increment block, which is why the increment is a block of its own.
- **`loop { ... }` without a `break` diverges, tracked per loop.** It is one block that jumps back to itself, with no
  condition to evaluate; the block after it stays an empty `unreachable` marker, and nothing is emitted into it - the
  verifier rejects a block that nothing reaches and is not one.
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
  **Superseded by 6.1's long tail below:** it is not counted either any more, because such a `const` is lowered where it
  is *read* and is therefore no declaration of the back end at all.
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
  answers `bool` and writes its payload through an out parameter, and one that is a `var fn` takes a pointer -
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
  `FixedArray` will be the first one (5.9b).
- **One profile, no cache, no output path from `project.trb`.** `torb build [path] [--emit-c] [--output <file>]` writes
  `<project>/build/release/program.c` and the binary next to it. `--profile`, the content-hash cache, `torb run` as
  build-and-execute, `torb test` and `buildOutput`/`buildTarget` in the manifest reader are 5.13's, as the table says.
- **Two decided pieces of 5.13 that the prelude's `Error` trait now makes expressible** (CONCEPT, Error Handling):
  - The `error:` report of a top-level `?` **walks `cause()`**: `error: <the error through Show>`, then one
    `  caused by: <that one through Show>` per link of the chain, until `cause()` answers `None`. Where the error is a
    concrete type rather than the trait value there is nothing to walk and it stays one line, which is what it prints
    today. A cycle cannot happen (errors are values, a value cannot contain itself), so no depth limit is needed.
  - **`?` return traces are Zig's, in the debug profile only.** Every `?` that hands an error on records its source
    location in a small per-task ring buffer, and the report above prints the locations under the chain
    (`  at src/config.trb:12:31`). The release profile emits nothing for it, so it costs nothing there, and no error
    type changes: a value never carries a stack. Panic frames are 7.9 and independent of this.
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
  per panic (`panic`, overflow, division by zero). They are in `tests/conformance/`, each with its `.expected`
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

### What 5.5 does differently

Sections 1 and 3 are the plan; where they did not fit what the checker records, what stage 0 can run or what a C compiler
accepts, the code won and this is the list. Everything else is as written.

- **Two files, and the decision tree is a graph.** `ir/decision.trb` turns a `MatchPlan` into a list of nodes addressed
  by index - a value cannot contain itself - and `ir/lower/match.trb` emits that graph as blocks. The tree decides
  nothing about the language: exhaustiveness and which arm wins were settled by the checker, which is why a value no arm
  matches is `Unreachable` and never a panic.
- **A node is memoized by the rows it was built from**, which is the DAG sharing section 1.6 asks for: two arms that end
  in the same question are one node and therefore one block. Termination is structural rather than a bound - every step
  takes the first test of the first row, so in the child where that test held the row has one test fewer and in every
  other child the row is gone.
- **A path is tested once per outcome, with one exception.** Section 1.6 says "tests memoized so the DAG is shared" and
  the design asks for each path at most once per branch. A row whose check at a path is a *different kind* from the one
  being asked - a range next to a literal - stays on both sides and is asked again, because the matrix cannot merge two
  kinds of question into one node. Everything else (a set of distinct literals, a set of cases) is removed or excluded.
- **Literals are a chain of two-way tests, not a `Switch`.** `Switch` is for the tag; a literal chain leaves it to the C
  compiler to build a jump table where it pays, which is what "dense or sparse is the back end's business" means and what
  section 1.3 already says about a literal type ("a string comparison chain").
- **The names of an arm are one slot per name, shared by every alternative.** `.Circle(r) | .Ring(r)` keeps `r` in a
  different field, and each alternative's block reads its own path into the *same* slot - so one body block serves every
  alternative instead of one copy per alternative. A name is found by the span it was declared under, which is what every
  other binding of the lowering is keyed by.
- **A guard binds before it runs, in its own block.** The bindings of the alternative are read there, the guard is
  evaluated, and the branch goes to the arm's body or on to the next candidate. What the guard bound and the failing path
  does not need is released by the ownership pass at its last use inside that very block - no rule of its own.
- **A path is read again per block rather than once.** A slot has to be defined on every path that reads it, so sharing
  one temporary across blocks would be wrong; and a `Read` of a counted field hands the frame a count, which dies at its
  last use anyway. A nested pattern therefore costs one `Read` per step per block, and the C compiler flattens it.
- **`if var` and `while var` wait for 5.9b.** The row of the table names `if var`, but the name it binds is a *path into
  its subject* (the checker's own note, gap 3), and a path is a `Reference` - which is milestone 5.9's. Binding it by
  value would compile and silently drop a write, so it is a clean finding instead.
- **`??` inlines the thunk the checker asked for.** The checker records `a ?? b` as `orElse` with a `lazy` argument
  (gap 13); a thunk is a closure and closures are 5.8's. So the lowering emits the control flow the `lazy` stands for -
  the fallback in the arm that needs it - which short circuits as decided gap 1 asks and allocates nothing.
  `Lowering.inlinedThunks` records the span whose `Lazy` adaptation is replayed by *not* building one. `?.` is still a
  finding: the checker resolves it to `Option.map`/`flatMap` with a closure, and there is no closure yet.
- **`?` converts the error itself.** `Adaptation.Convert(witness, method)` at the `?` is tried as a call of the `from` the
  checker named; where that is the *generated* `From` of a wrapper case there is no function to call, so the lowering
  builds the case of the enclosing error type that wraps exactly one value of the given one - the case *is* the
  conversion. A `?` in a body that returns neither an `Option` nor a `Result` (the top level of an entry file) is a
  finding that names 5.10: printing `error: <the error through Show>` needs `Show`.
- **Every variant initializer in C is designated.** `(T){ .tag = 1 }` rather than `{ 1, { 0 } }`. A case that carries
  nothing would otherwise leave the `payload` member out, which `-Wmissing-field-initializers` reports, and naming it
  with a bare `{ 0 }` runs into `-Wmissing-braces` as soon as the first group holds a struct of its own - which
  `Option<Option<Int>>` does. Neither warning is raised for a designated initializer, and what is left out is zero
  initialized, which is exactly what a case without fields means.
- **The drop function of a variant layout switches on the tag**, with a `default: break;` and only the cases whose group
  really holds a count. A **niche** layout has no helper and no struct at all: it *is* its payload field, so retaining an
  `Option<String>` is retaining the `String` (`countedFormOf`).
- **A case is a function from its own fields**, which the checker's `caseSignature` decides. A variant type that also
  declares fields of its own therefore cannot be built through a case constructor at all; that is a gap between the
  checker and the language and not one of the back end, and it is named as one.
- **Stage 0's `?` does not convert the error.** It hands the failure on as it is instead of going through the generated
  `From` of a wrapper case, so a gate program that needs one cannot be compared with the interpreter. The conversion is
  pinned by an IR snapshot in `lower-match.test.trb` instead, and `tests/conformance/errors.trb` uses `?` where the
  two error types agree. Unifying stage 0 and the binary is 5.14's, and this is the second entry on its list after the
  panic format.
- **The gate programs still end in `Process.exit`.** `File.writeText` would give a real output comparison, but its
  manifest prototype answers `bool` and writes through an out parameter, so the wrapper is still missing (5.3's list) -
  and `File` itself is a `native type` the runtime represents from 5.9b on.
- **`isSome`, `orElse` and every other member of `Option` and `Result` stay blocked.** They are methods of a *generic
  type*, and an instance of one needs three things 5.6 brings: a type-argument list in the instance key and in the
  mangled name, a substitution threaded through `irTypeOf` while the body is lowered (it passes an empty mapping today),
  and a worklist that seeds an instance per argument list. None of that is small, so nothing of it was done here - what
  5.5 needs of `Option` and `Result` is their *layout*, which 5.1 already builds, and the constructors and patterns over
  it, which are here.

### What 5.6 does differently

Sections 1.4 and 3.1 are the plan; where they did not fit what the language allows, what the checker records or what
portable C can express, the code won and this is the list. Everything else is as written.

- **Three files plus the emitter.** `ir/witness.trb` (what is in a table, where a member of one sits, and which function
  each slot points at), `ir/lower/generic.trb` (which instance a call reaches, once the type arguments of the *enclosing*
  instance are substituted, and the coercion to a trait-typed value), `ir/lower/derive.trb` (the bodies the language
  generates). `ir/instances.trb` gained the arguments, and `backend/c/{type,emit,body}.trb` the C.
- **Nothing is ever passed as a witness parameter, because a trait-typed value carries its own tables.** Section 1.4 says
  a generic function that is shared per bound is reached "with a `.Forwarded` witness that bottoms out in an `.Object`"
  and that the table is appended after the declared parameters. It does not have to be: after substituting the
  instance's arguments, the receiver of such a call *is* the trait-typed value, and `WitnessRoot.Value(slot, bound)` reads
  the table out of it. Every open bound of the language belongs to a value that is in hand - the receiver of the call, the
  argument that is interpolated - so `IrSignature.witnesses` stays empty, `Instruction.Call` never carries a witness, and
  `WitnessRoot.Parameter` is never built. A bound whose table belongs to no value in hand is a static member of a
  trait-typed type, which object safety forbids anyway, and it is a clean finding.
- **The type arguments are what an instance *is*, and they are re-derived rather than read.** An instance is a declaration
  plus one type argument per generic parameter in scope, with the `Self` of a trait in front where the owner is one; the
  key and the mangled name are made of exactly that list. The witnesses the checker recorded at a call site
  (`Tables.witnesses`) are **not** read at all: they are in terms of the *caller's* parameters, and after substituting
  the caller's own arguments every type is closed, so asking `witnessFor` again is both simpler and always right. The
  same substitution is what `irTypeOf` applies to every type a body mentions, which is why nothing inside a lowered body
  knows that it is generic.
- **The members of the supertraits are flattened into the table, and `WitnessTable.nested` stays empty.** A call through a
  table has to spell the member's C function type, and the only trait a back end can name at a `CallWitness` is the bound
  the value carries - so a path through nested tables could not be spelled at all. The order is: the members of the trait
  in declaration order, then the same of every supertrait in declaration order, deduplicated by name.
- **A default member is a slot of the table.** Section 1.4 says a default is not in one. But an implementation may
  *override* a default (`Show.showNested` for a `String`, and any `extend X with T { fn aDefault() ... }`), and which
  of the two a trait-typed value reaches is only known at run time. So every object-safe member is in the table, with the
  override where there is one and an instance of the default body for the target type otherwise. Gap 14 still falls out:
  a delegated implementation declares no override, so `by` forwards the required members only.
- **Three kinds of member are left out of a table**, and every one of them is a member object safety already forbids on a
  trait-typed value: one that mentions `Self` anywhere but as its receiver (`equals`, `compare`, `min`), a static one,
  and one with generic parameters or a `where` clause of its own, whose call would need witnesses appended. A `var fn` receiver
  member is left out as well until 5.9 (`List.add`). Calling one on a trait-typed value is the finding "`add`, which is
  not in the witness table of a trait-typed value".
- **A table is `{ drop, members }` and every member is a thunk.** Section 3.1 writes `Object(bounds)` as
  `struct { torb_object *data; const w_A *a; ... }`, which needs one C struct per applied trait and a function-pointer
  cast at every call. Instead the emitter writes one fixed `torb_witness_table` into the translation unit - the drop
  function of the boxed payload, and a pointer to an array of `void (*)(void)` - and one **thunk** per member, whose
  signature is the erased one (`const void *self` plus the declared parameters) and whose body unwraps the payload and
  calls the real member. A call site converts the member's pointer back to exactly that signature, which is what C
  allows; casting to a signature the function does not have, which the design's shape would need, is not.
  `drop` is in the table because the payload is erased: the drop function is a property of the *target* type, and a table
  is per target type, so `torb_release(value.data, value.w0->drop)` is the release of a trait-typed value.
- **The boxed payload is an ordinary layout.** `T_payload__T_main_Point` is a `LayoutKind.Payload` with one field, pinned
  to `Boxed`, so it gets a C struct, a `Construct`, and the `R_`/`D_` pair the ownership pass already emits for every
  other counted block. A second mechanism beside the layouts would have been a second place to get a retain wrong.
  `TraitValue` of a value that is *already* trait typed (`Show & Hash` narrowed to `Show`) boxes nothing: it copies the
  payload pointer and picks the tables.
- **A generated body is built by the worklist, like a body of the source.** `Lowering.pendingGenerated` is a second list
  with a cursor of its own, drained in the same loop, because a generated member may need an instance of the source and a
  body of the source a generated member. So a derived `Show` of a recursive type terminates for the same reason a
  recursive function does.
- **`Show`, `Equals` and `Hash` are structural; `compare` is not generated for a `type`.** The format is decided gap 23
  (`Type(field: value, ...)`, a case as `Case(field: value)` or as its bare name, everything nested through
  `showNested`). A field that is a primitive is compared with an intrinsic and shown through its own member; everything
  else goes through the field type's own member, which is what makes a nested and a recursive type work. `Hash` mixes
  with the bit intrinsics rather than with `combineHashes`, which the runtime provides from 5.7 on - and it does not have
  to agree with anything, because a hash value is not part of the language (`Show` of a `Map` is insertion order, so
  nothing observable depends on one). `Encode` and `Decode` are still a finding that names 5.10.
- **A member the manifest maps to an intrinsic or to a runtime function gets a generated wrapper.** A table holds
  function pointers, so `Int64.equals` needs a function even where a call of it is one instruction, and a runtime native
  is called through the manifest's prototype - which may need the source location or an out parameter. Both become an
  ordinary generated body whose one statement is the intrinsic or the call, so the one place that knows the runtime's
  conventions stays the one place that knows them.
- **`a < b` on a type is `compare` plus one test of the tag.** The checker resolves `<`, `<=`, `>` and `>=` to
  `Compare.compare`, whose result is an `Ordering` - so the operator is the call plus `tag == Less` or `tag != Greater`,
  exactly what the defaults of `Compare` say. Before 5.6 this path was unreachable and would have put an `Ordering` in a
  `Bool` slot.
- **Every comparison intrinsic may name no numeric type.** 5.2 made that true for `Equal` and `NotEqual`; `Char.compare`
  needs it for `Less` and `Greater` as well, because a `Char` is a code point and not a number. Where the kind is absent
  the verifier demands that both operands have the same type, as it already did for equality.
- **A runtime native that answers the other signedness of the same width is converted, not refused.**
  `Hash.hash(): Int` against `uint64_t torb_hash_i64(int64_t)` is the one difference between a declaration of `std/`
  and its runtime symbol that is a cast and not a wrapper, because the bits are the hash either way
  (`PrototypeMatch.Converted`).
- **`torb build` and `torb ir` ask the checker for every body, not only for the requested modules.** A lowering may reach
  the body of any module - a default member of a trait of `std/prelude`, a generic function of another package - and a
  body whose tables were never filled cannot be lowered at all. `check` gained `checksEveryBody`; what is *reported*
  stays what was asked for, because the file list is filtered by the request and not by this.
- **`matchTypePattern` and `memberOfImplementation` are now public in the checker.** The substitution of an instance is
  the owner of a member matched against the type it was reached on, and which member an implementation provides under a
  name is what `checkRequirements` already decides - asking the checker twice would have been a second place to be wrong.
- **A narrowing coercion to a *supertrait* of a trait-typed value is a finding.** `List<Item>` narrowed to
  `Iterate<Item>` would need the table of `(the payload's type, Iterate<Item>)`, and the payload's type is exactly what
  a trait-typed value has erased - so it can only come out of a `nested` list in the table, which this sub-milestone does
  not build. Coercing a *concrete* value to any of its traits is unaffected, and so is calling an inherited member on a
  trait-typed value (that is what the flattening above is for). 5.7 needs the nested tables for `Iterate` and will add
  them; there are two occurrences in the repository today.
- **`torb ir --statistics` over the repository: 578 of 2441 declarations lowered before 5.6, 852 of 2648 after** (23% to
  32%). "A call on a trait-typed value" (512), "a generic call", "a generic function or a member of a generic type", "a
  derived `Equals` member", "a call that passes a witness table" and "a value used as a trait-typed value" are all gone;
  the new top blockers are a list literal (469, 5.7), string interpolation (264, 5.10), `a[key]` (236, 5.7) and `for` over
  a collection (212, 5.7).
- **The gate is not `basics.trb`.** It cannot be: `print shapes.map({ _.area() })` does not type-check at all
  (`Iterate<Float64>` does not implement `Show`, because `map` answers an `Iterate` and only a named collection is
  `Show`), so the file is a stage-0 script and not a checked program. What is left of it after that is blocked by 5.7
  (list and map literals, `a[key]`, `for` over a collection, the variadic list of `print`), 5.8 (closures), 5.9
  (a `var fn` receiver, `var` parameters, assignment to a place) and 5.10 (string interpolation). The gate of this sub-milestone is
  therefore `tests/conformance/traits.trb`, `generics.trb` and `derived.trb`, each compiled, run, compared with
  stage 0 and asserted to leave zero live blocks.

### What 5.8 does differently

Sections 1 to 3 are the plan; where they did not fit what 5.1 to 5.6 built, what the checker records or what portable C
allows, the code won and this is the list. Everything else is as written.

- **Everything a closure stores is an ordinary `Layout`.** `ir/capture.trb` builds three of them: the **environment** of
  one closure (one field per capture, named after the closure's own function), the **box** a captured `var` binding lives
  in (`T_box__Int64`, one field), and the memo **cell** of a `lazy` parameter (`T_cell__Int64`). That is the same decision
  5.6 made for the boxed payload of a trait-typed value, for the same reason: a layout already carries a C struct, its
  place in the definition order, `containsCounted`, and the `R_`/`D_` pair the ownership pass emits for every counted
  block - so nothing about a closure needs a second way to be retained and dropped, and a capture that is itself counted
  needs no rule at all. `LayoutKind` gained `Box` and `Cell` next to `Environment`, and all three are pinned to `Boxed`
  whatever they cost, because every one of them is *shared*.
- **`IrType.Box`, `IrType.Lazy` and `Instruction.BoxNew` are therefore never produced.** A box is `Record` of a
  `LayoutKind.Box` layout and a cell `Record` of a `LayoutKind.Cell` one; building either is an ordinary `Construct`.
  The three forms stay in the IR because the design names them and the VM may want them, and the C back end says so where
  one would arrive (`a captured `var` binding as a type of its own`). What the two kinds do need is **one** rule of their
  own: `uniqueOwnersOf` never makes a prefix unique whose layout is one of them, because a box is the one place where a
  variable is shared (gap 19) and a cell is what "evaluated at most once" rests on - copying either is exactly what may
  not happen. That is the one change this sub-milestone made to `ir/ownership.trb` besides adding `CallClosure` to
  `writtenPlacesOf`.
- **A `lazy` cell holds `Option<Value>` and the thunk, not a flag and a value.** The drop of a cell has to be the
  ordinary drop of its fields, and `None` releases to nothing; a field that held a half-initialized `Value` would need a
  drop that asks a flag first, which is a second shape of a counted block for one parameter mode. Forcing is therefore
  control flow over the `Option` and needs no instruction: `Intrinsic.LazyForce` is not emitted either.
- **The C type of every closure is `torb_closure`** - the erased `{ void (*code)(void); torb_environment *environment; }`
  of the ABI - and **one thunk per closure** is what makes a call of one well defined. A struct per *signature* would
  have to have its parameter types complete where it is defined, and a layout may hold a closure of its own type, so the
  definition order could not be decided at all. The thunk (`F_<function name>`) takes `torb_environment *`, casts it to
  the environment of that one closure and calls the body, so the function a closure points at really has the signature a
  call site converts its pointer back to - the same trick the witness tables use, and the reason casting a function
  pointer to a signature it does not have never happens. It is also what makes a **named function used as a value** free:
  such a function has no environment parameter, and its thunk ignores the one it is handed.
- **A closure body is recognized by its first parameter.** It is the one kind of function whose first parameter is a
  `Record` of a `LayoutKind.Environment` layout, so `IrFunction` needed no flag and the emitter, the verifier and the
  helper demand all ask the same question. A closure without captures has no environment parameter at all.
- **The environment carries its own `drop` pointer**, as a second ABI field of `torb_environment`, and
  `torb_environment_release` is the one runtime function this sub-milestone added. A closure value has the type of every
  closure of its shape, so the release site cannot know which captures are inside one - the alternative was a third
  pointer in every closure value, which would change what a closure costs (section 1.3's two words).
- **The environment is always a counted heap block, and `isEscaping` is recorded and not yet used.** Section 0 leaves
  "stack or heap environment" to a back end and the flag is in the IR and in the text format, but taking it needs
  liveness to hold every captured value live to the closure's **last use** rather than to the `Closure` instruction: a
  borrowing environment whose captures die right after it was built would read freed memory at the call one line later.
  That is a rule of the liveness pass, so it is 6.3's measurement and not this one's. Round P1 to P4 of
  [docs/PERFORMANCE.md](PERFORMANCE.md) (finding 7) takes the flag without that rule: the environment on the frame
  stays a **counted** block with a block kind that says "never freed", so it owns its captures exactly as a heap one
  does and only the storage differs. What it costs is a second condition beside the flag - the IR has to prove the
  callee does not keep the closure, which is the half of decision 14 the checker does not decide.
- **A capture is an `Owned` position and still cannot be a move.** 5.4's note asked for a mode on `Capture`; a `Capture`
  has no room for `last` and `withOperands` cannot write one back, so a capture at its last use is a retain followed by a
  release rather than a move. One retain/release pair per capture of a value the frame no longer needs, which the
  snapshots of `lower-closures.test.trb` show. Closing it means giving `Capture` an `Argument`.
- **A captured `var` binding is a place from the moment it is declared.** The lowering has to know *before* it reaches
  `var counter = 0` that some closure captures it, so `capturedVariablesOf` walks the whole body once up front and asks
  the checker for a capture list at **every** expression - the argument of a `lazy` parameter and a quotation are closures
  too and record their captures under their own span. From there on `Lowering.boxes` maps the declaration span to the slot
  that holds the box, and every read, every write and every *place rooted in it* goes through `Field(box, 0)`. That last
  one is what 5.9a's hand-over note asked for: `rootReferenceOf` in `ir/lower/place.trb` answers a `PlacePath` now and not
  a `Slot`, so a path simply continues from the box.
- **`Capture.Place` is built by nothing, and the repository needs it nowhere.** A `var` parameter a closure captures would
  have to live in the environment as the pointer it is, and a reference is not a value - so it cannot be the type of a
  field, and `FieldLayout` would need a "this one is a pointer" flag that every walk over a layout then has to know about.
  There is **not one occurrence in the whole repository** (the receiver of a receiver closure is its first *parameter* and
  not a capture), so it is a clean finding that names 5.9b, which needs the same flag for its index paths.
- **`.Assign` and `.AssignClosure` are one lowering.** Whether the value of a property command is `8080` or a closure
  changes what is evaluated and nothing about the write, so `lowerPropertyWrite` needed no case for the second one.
  `.Configure` is the other one: the closure is a *receiver* closure and the field's own place is its `var self`
  argument, so `database { ... }` configures the field where it lies and copies nothing.
- **A constructor and a case used as a value get a generated function.** `names.map Role` and
  `byPath.get(path).map(ModuleId)` hand over something that has no body anywhere, so `t_constructor__T_main_Point_0` is
  generated - the fields as parameters, one `Construct`, memoized by its name - and from there on it is an ordinary
  function value. It is built in `ir/lower/closure.trb` and not through `Generated`, because that machinery is keyed by a
  member symbol with a signature and a constructor has neither.
- **A variadic parameter is a finding of the *body* now, and names 5.7.** `...numbers: Int` is a list at the call site and
  the collection *trait* inside the body (`List<Int>` in a type position is a trait-typed value, 5.1's note), and the two
  shapes only agree once a list is a value of the back end. Before this sub-milestone the closure in
  `numbers.fold 0 { a, b => a + b }` stopped such a body first; now it would produce IR that disagrees with itself, so it
  is refused cleanly instead.
- **Two records the checker did not have, one of them fixed here.** `checkArguments` opened a `var` access only for
  `.Var`, so the argument of a `(var self: R) => Void` parameter - `configure settings`, which is every DSL block - had no
  recorded `Place` at all and milestone 5 could not form the reference. It opens one for `.Receiver(isVar: true)` now,
  **without** noting a change: a receiver closure changes its receiver through its own body, so `var inner = Element()
  build(inner)` is not a dead change and calling it one would reject every nested block of a DSL. The second record is
  still missing: the checker declares every implicit parameter (`_`, `_2`, a name the function type gives) under the span
  of *its own closure* and creates the binding when the name is first used - which is already inside a nested closure, so
  no capture is recorded for one. Reading an outer closure's implicit parameter by position would silently read the inner
  closure's, so the lowering compares the binding's span with the closure it is in and reports
  "a closure that captures an implicit parameter of a closure around it". One entry per closure in the tables closes it.
- **A closure of an entry file sees no top-level binding of that file.** Those bindings are locals of the entry function
  (5.4's list), and a closure is a function of its own - so `enterClosure` clears `entryLocals` and a top-level `const`
  read inside a closure goes through the const evaluator like it does from any other function. A top-level `var` read
  there is the clean finding it already was.
- **`?.` is not a closure and stays a finding.** The checker resolves `a?.m` to `Option.map`/`flatMap` (gap 12), but there
  is no closure in the source to lower - it is a `Tag`, a `Switch` and the member on the payload, the way `??` is inlined.
  What is missing is one record: the checker wraps the member's own result into the `Option` *inside* the function type it
  gives the callee, so nothing says whether `m` answered an `Option` already, and that is exactly what decides whether the
  arm wraps its value or hands it on. Two occurrences in the repository.
- **One bug of 5.6 that closures made reachable.** `objectTypeOf` did not substitute the bounds `Adaptation.ToTraitValue`
  recorded, while `lowerTraitValue` did - so the slot a coercion produced into was `Object(Iterate<Void>)` while its
  tables were for `Iterate<String>`. Every body of `std/iteration` that the closures unlocked reported it, and it is
  fixed where the type is built.
- **The trait-typed values are emitted before the layouts.** An object struct is made of the erased pointers of the ABI
  and of nothing of the program, while a layout may hold one in a field - the environment of a closure over a trait-typed
  value is the first one that does.
- **The verifier knows the shape of a closure.** At a `Closure` it asks that the callee take the closure type's parameters
  after the environment, answer its result, and that the environment hold exactly one field per capture with the type of
  what the frame handed over. That is the check that catches a lowering which fills the wrong field, and it is why the two
  hand-built fixtures of 5.1 and 5.4 now build an environment layout too.
- **`torb ir --statistics` over the repository: 854 of 2662 declarations lowered before 5.8, 977 of 2848 on the same tree
  after, and 1058 of 3086 once 5.7's half and the streams of `std/` were merged in** (32% to 34%; the total grows because
  a closure unlocks instances that were never reached at all). "A closure" (91), "a call of a closure value", "a function
  used as a value", "a `lazy` argument", "an implicit closure parameter" and "`X` used as a function value" are all gone.
  The new top blockers are a list literal (528, 5.7), string interpolation (283, 5.10), `a[key]` (251, 5.7), `for` over a
  collection (244, 5.7) and "a declaration the back end cannot build an instance of" (236, the generic members of the
  collections, 5.7).
- **One internal error is left in `torb ir ..`, and it is not a closure's.** "The generic parameter `Item` was not
  substituted before the back end saw it", from `std/http`: `lowerForeignExpression` switches the *module* to the one a
  **field default** is written in but keeps the caller's substitution, so the default of a field of a *generic* type is
  lowered with the wrong arguments in scope (`completeFields` → `lowerForeignExpression` → `typeAtSpan`). The mapping of
  the constructed type is in hand at `lowerFieldArguments` (its `owner`), so closing it is one argument threaded down.
  Master had that one and nine more of the same kind in `std/http` alone; the nine are gone with the `objectTypeOf` fix
  above.
- **The gate is not `examples/config-dsl`.** Three things stand in front of it, none of them a closure: its entry file
  loads a receiver **script** through `Sandbox.load` (`Script` and `SandboxCapabilities` are milestone 7.4, and the body of
  a receiver script is applied by the sandbox and by nothing else), `ServerConfig` has a variadic parameter and a
  `Set`/`Map`/`List` field (5.7), and every line of its output is interpolated (5.10). What is left of the vocabulary is
  `tests/conformance/closures.trb` (a closure over a captured `var`, one returned and called later, one in a record
  field, function values of a named function, a constructor and a case, a `lazy` forced zero times and once),
  `counted-closures.trb` (the same with `String`s, a trait-typed value and closures in environments - the leak gate) and
  `dsl.trb` (receiver closures and all three property commands). Each is compiled, run, compared with stage 0 and asserted
  to leave zero live blocks.
- **Two lines of the DSL that stage 0 cannot run.** `tls(port == 8443)` and `own.port 80` - a property command written
  with parentheses, and one on a **named** receiver - are read by the interpreter as calls of the field and fail there, so
  `dsl.trb` writes `tls true` and `own.port = 80` instead. Unifying stage 0 and the binary is 5.14's, and this is the third
  entry on its list after the panic format and `?`.

### What 5.9a does differently

Sections 1 to 3 are the plan; where they did not fit what the checker records, what 5.1 to 5.5 built or what stage 0 can
run, the code won and this is the list. Everything else is as written. 5.9a is the first half of the row: `var`
parameters and `var fn` receivers, which need nothing of 5.6 and 5.7 at all.

- **One file, and the place comes from the checker.** `ir/lower/place.trb` is the whole sub-milestone. It reads
  `Tables.places` - the `Place` 4.6 recorded under the span of every argument, receiver, assignment target and `if var`
  subject (TYPECHECKER 5.1) - and translates a root into a slot and a step into a `PathStep`. So the lowering never walks
  an expression a second time, and a path it forms is by construction the path exclusivity was checked for. The
  alternative was a second walk over the syntax tree in the back end, which would have been a second place to disagree
  with the checker about what a place is.
- **`Argument` gained `place: Reference?`, and `slot` is the base of it.** Section 3.2 says "a `Reference` with steps is
  formed at the call site - never passed as a base plus a path", and section 1.6 says the VM gets a (frame, slot, path)
  triple: so the *IR* carries the path and each back end renders it its own way. Putting it on the argument rather than
  adding an instruction that materializes a pointer keeps `IrType` free of a pointer case - references are not values
  (section 2.3) - and `slot` being the base is what makes every pass right without a rule of its own: liveness keeps the
  base live across the call, `operandsOf` borrows it because a place carries no count, and the C emitter takes its
  address. `referenced(place)` is the constructor, and the text format prints `&%0.from.x borrowed`.
- **The `MakeUnique`s of a `var` argument are the *proper prefixes* of its path, and the place itself is the callee's
  business.** `rename(holder.bundle, name)` makes `holder.bundle` unique only if something above it is counted; the
  callee's own write is what makes the block it received unique, and because it holds a pointer into the caller's frame
  the new block is written back through it. That is one rule fewer than it looks: `uniqueOwnersOf` already walked the
  proper prefixes for a `Write`, and a `var` argument is now one more entry in `writtenPlacesOf`.
- **A `var` argument marks its base as mutated for the summary pass.** `fn f(text: String) { var local = text  g(local) }`
  with a `var` parameter on `g` receives `text` **owned**: the copy into `local` reaches storage that is written, and the
  count of written storage has to be the frame's. Without it the write would release a count the caller still holds.
- **An assignment is a `Write` wherever the target is a path, and that now includes a `var` parameter itself.**
  `target = value` through a `var` parameter may not compute into the parameter's slot: the slot holds a pointer, and a
  plain store would leak what the caller had there. A write releases first, which is the contract 5.4 wrote down.
- **A call may not produce into the base of a place it writes through.** `counter = grown(counter)` where `grown` takes
  `counter` by `var` would overwrite - and therefore leak - what the callee has just put there. The lowering produces
  into a slot of its own and stores the result with a `Write`, which releases first; the prepass of 5.4 that saves a slot
  an instruction reads and writes is switched off for such an instruction, because saving the old value into a copy
  would leave the write pointing at the copy.
- **A temporary is a legal argument and is lowered in its position.** `grow(Counter(5))` builds the value into a
  temporary and hands over its address (gap 2): the callee is its only owner, so "copy in, copy out" is exact. A
  temporary is still never the *base* of a path, which is what the checker already rejected.
- **`Resolution.PropertyWrite(.Assign)` is lowered here, because it *is* an assignment.** `port 8080` writes a field
  (gap 15) and the checker recorded the path to that field under the span of the callee - the very same place an `=`
  gets. `.AssignClosure` and `.Configure` need a closure and name milestone 5.8.
- **A `native fn` with a `var` receiver stays a finding.** Every one of them is a member of a container (`ArrayList.add`,
  `Map.set`, `File.close`), the runtime's convention for one is a pointer plus its own out-parameter shapes (5.R1's
  list), and the wrapper belongs with the containers - so the message names milestone 5.7.
- **The type of a `var` parameter is read with the *callee's* instance arguments.** Found through `using` over a
  `shared type`, which produced three internal errors where one clean finding belongs:
  `using<Resource: Close, Value>(var resource: Resource, ...)` declares its `var` parameter as a generic parameter, and
  `parameterTypeAt` instantiated that annotation under the **caller's** substitution - which does not know `Resource`, so
  the back end saw an unsubstituted parameter (one internal error), the temporary the place is formed over became a
  `Void` slot (a second: used before it is defined), and the call then passed a place whose type was not the parameter's
  (a third). It reads the mapping the dispatch decided, exactly as a `lazy` parameter (`lazyItemAt`) and a default
  already do, and what is left of `using Connection() { ... }` is the C back end's one honest finding, "a release of a
  `shared type` object (milestone 5.9b)". The rule is worth stating once for the whole lowering: **anything that comes
  out of the callee's declaration is substituted with the callee's mapping, and only what comes out of the call site is
  substituted with the caller's.**
- **And therefore: a `var` argument that would have to be *coerced* is a clean finding.** With the parameter's type read
  right, two more internal errors of the verifier came out of `std/stream` ("parameter 0 ... is `Object(Source<…>)` and its
  place is `Shared(Iterating<…>)`"): a place is **not a value**, so nothing can put a `TraitValue` in between - the callee
  would write through a box the caller does not have. The checker allows the coercion because it reads the argument as a
  value; `referenceArgumentAt` compares the place's own type with the parameter's and says so instead.
- **`if var` and `while var` are still a finding, and 5.9b's.** They do *not* fall out of this mechanism: a place here is
  formed at a call site and dies with the call, while a pattern binds a name that lives for a whole block - which needs
  the lowering to key a *binding* to a `Reference` instead of to a slot, and every read, every write and every argument
  of such a name to go through it. That is a second mechanism, not this one.
- **A body whose result the checker inferred as `Never` and that does `return` is a finding.** `Parser.recoverToLineEnd`
  was `loop { ... return ... }`: the checker infers `Never` from a body that never *ends*, although the bare
  `return`s inside the loop do leave the function. The IR would then carry a `Return` out of a `_Noreturn` function,
  which the verifier rejects - so it is one clean finding (exactly one function in the repository). Closing it is a rule
  of the checker's inference: a bare `return` should make an inferred result `Void`.
- **The C of a `var` parameter is what section 3.2 promises, plus one spelling rule.** `T *`, and `T **` where the value
  is itself a pointer (a counted block), which `pointerTo` decides - `T * *` is the same type spelled worse. A place that
  *is* a `var` parameter of the current frame is handed on as the pointer it already is and never as `&*p`.
- **`torb ir --statistics` over the repository: 341 of 2432 declarations lowered before, 578 of 2441 after** (14% to
  23%). The 501 that were blocked by "a `var` receiver or a `var` argument" are gone, and the new top blockers are a
  call on a trait-typed value (512, 5.6), a list literal (395, 5.7) and string interpolation (222, 5.10).

### What 5.7 does differently

**5.7 is not finished, and it came in two rounds.** The first round is the ABI the containers need - the two conventions
of `runtime/` as a generated wrapper, and the leak an assignment used to be - plus the measurement of what the rest of
the row really waits for. The second round is that chain, walked as far as it goes without closures: a `var fn` member
of a trait-typed value, element descriptors, `ContainerNew`, and the container literals. What is still open is listed
after both.

**The first round: the two conventions of `runtime/`.**

- **Two files, and the manifest carries the ABI.** `ir/lower/native.trb` (the wrapper) and the two new fields of
  `NativeEntry` in `backend/c/natives.trb` are the sub-milestone. Sections 3.7 and 5.R1 name the two conventions of the
  runtime in prose; they are now *data*, one entry at a time, because the lowering has to act on them and the VM has to
  read the same answer:
  - `addressedParameters: List<Int>` - which declared parameters the runtime takes **by address**. There is one C list
    and one C hash table for every element type (3.1), so an element is `const void *value` whatever it really is.
  - `result: NativeResult` - `.Direct`, `.Optional` (`bool` plus one out parameter, `Value?` around it) or `.Fallible`
    (the same for a `Result`).
- **A native whose ABI is not the declaration's is called through a generated wrapper.** The wrapper is a function of
  the *program* whose signature is exactly what `std/` declares and whose body is the runtime call plus the `Construct`
  of the `Option` or the `Result`. So `Instruction.Call` stays an ordinary call for every pass after the lowering, a
  witness table has a function to point at, and the one place that knows the runtime's conventions is one place. The
  **raw** runtime function keeps the ordinary `t` name of the same declaration and carries the runtime's own signature
  (`RuntimeShape`, `shapedSignature` in `ir/instances.trb`); the wrapper gets the new mangling prefix **`n`**, because
  both are in the program at once and only the wrapper is ever emitted or called.
- **The failure of a `.Fallible` native is built from the error type's own fields, each taken from the parameter of the
  same name.** `Int.tryFrom(text)` fails with `NumberParseError(text)`, which is exactly what the runtime cannot answer
  and what the wrapper has in hand. An error type with a field no parameter names is a clean finding and no guess - which
  is why `Int32.tryFrom` (`NumberRangeError { message }` against `tryFrom(value)`) stays one.
- **`IrParameter.isOut` was added**, and it is the only new field in the IR. An out parameter is a `var` parameter the
  callee never *reads*: the base of such a place starts empty and owns a value afterwards. Without saying so in the IR,
  `verify.trb` reports "used before it is defined" and `ownership-verify.trb` reports "used after it was moved out of"
  for every wrapper. Both read it through one shared `outParameterSlotsOf`, and the text format prints `out` where it
  prints `var` for an ordinary reference.
- **Two spellings the prototype match now accepts, and both are a conversion C does at the call.** An opaque
  `void *`/`const void *` where the lowering passes a reference - that *is* the element-descriptor design, a runtime
  function cannot spell the element type - and the other signedness of one width for a **parameter**, which 5.6 already
  allowed for a result (`combineHashes(first: Int, second: Int)` against `torb_hash_combine(uint64_t, uint64_t)`: the
  bits are the hash either way). `Void` is `torb_void` as a value and `void` as the result of a function, which is what
  the emitter writes for a prototype and what `torb_list_add` is.
- **The finding "a `var` receiver of a `native fn` (milestone 5.7)" is gone.** `List.add`, `Map.set` and `Set.add` are
  ordinary places: the receiver was already a `Reference` after 5.9a, and what was missing was only the wrapper around
  the pointer plus the element by address.
- **`a = b` between two counted locals leaked, and so did every other assignment into a counted slot.** 5.9a found the
  `Copy` without a release; the same hole was in an assignment from a call, from a literal and of a whole counted record.
  An assignment to a counted local is now a `Write` **through** the slot, which releases before it stores - the contract
  section 2 wrote down for a write. Only `containsCountedType` slots go through it, so the arithmetic half of a program
  is byte for byte what it was, and it removes the read-and-write prepass from `total = total + part` (one instruction
  fewer). `tests/conformance/reassignment.trb` is the gate.
- **One bug of the *dispatch*, found on the way.** What stands in front of the dot of `Int.tryFrom(text)` is a **type**,
  and the checker records the type of its *constructor* there (`() => Int64`). That function type was accepted as a
  receiver, so the `Self` of the conversion trait was bound to it, the trait's own arguments were not found for it and
  `Failure` was never substituted - which is why `Int.tryFrom` and `Float.tryFrom` were "not monomorphic" long before this
  sub-milestone. `closedReceiver` now answers `None` for a function type, and `staticDispatch`'s existing fallback (the
  target of the implementation) takes over.

**The second round of 5.7: a `var fn` member of a trait-typed value, and what that made possible.**

- **The witness table carries everything about the erased target that `torb_release` and `torb_make_unique` need.**
  `{ drop, retainChildren, size, nested, members }`, and `MakeUnique` of an `Object` slot is
  `value.data = torb_make_unique(value.data, w0->size, w0->retainChildren, w0->drop)`. Value semantics demand it: a
  `Copy` of a trait-typed value retains the box its payload lives in, so a copy of a `List<Int>` would otherwise see an
  `add` made through the other. Every bound of one value describes the same payload, so the **first** table answers; and
  every trait-typed payload is boxed (5.6 pins `T_payload__X` to `Boxed`), so there is no inline-payload shape that could
  skip the uniqueness.
- **A `var fn` member is in a table again.** Its thunk takes the **address** of the payload inside the box and the box
  is not `const`. The erased receiver is `void *` in *every* thunk, including a read-only one, because a call site knows
  the member's index and not its declaration - one spelling is what makes the cast back well defined C, and the `const`
  of a read-only member is kept one line further in, on the box the thunk unwraps. A native whose ABI is not the
  declaration's is pointed at directly, because the wrapper of `ir/lower/native.trb` already *is* a function of the
  program (a thunk calls a mangled name, and a `FunctionKind.Runtime` has none in C).
- **`WitnessRoot.Value` carries a `Reference` and not a `Slot`.** The table and the payload come out of the same place
  expression, so `holder.items.add(x)` works and not only a bare local. `makeOwnersUnique` in `lower/place.trb` emits the
  whole chain - one `MakeUnique` per counted owner *along* the path, outermost first, and the box at the end. 5.4's pass
  only walks the proper *prefixes* of a place, because the storage a place names is normally the callee's business; for a
  trait-typed receiver it is not, so the lowering takes both halves over, which 5.4 explicitly allows.
- **`ArrayList.iterate` is ordinary TorbScript.** `ListIterator<Item>` in `std/collections/src/list.trb` holds the list
  and an index and its `next` is `items.get(index)` - which the `.Optional` convention of the first round made callable.
  So the runtime keeps one function fewer ("natives stay few"), and the cursor holds the list **by value**, which is what
  makes changing a list while iterating it not change what the cursor walks. `TrieList.iterate` is `.Planned("8")`: its
  cursor is the trie's, and nothing constructs a `TrieList` before the trie exists.
- **Element descriptors are here, and the compiler fills in only what is a function of the language.** `ir/element.trb`
  makes one per element type, memoized by its mangled name, and upgrades one that a list asked for first and a map key
  needs afterwards. `size` and `alignment` are `finishProgram`'s. `retain` and `release` stay `None` in the IR and the
  **emitter** writes them from `item` (`dR_X`, `dD_X`), exactly as it writes the `R_`/`D_` pair of a layout: they act on
  one element in place through a `void *`, which is an ABI shape and not a signature the language has. `equals` and
  `hash` *are* functions of the language and get a thunk (`dE_X`, `dH_X`) around them, so a key in a map and a `==` in
  the source can never disagree. A list asks for neither, because an element that is not `Hash` may still be in a list.
- **`Instruction.ContainerNew(target, kind, elements)` was added**, and it is the only instruction the containers need.
  `torb_list_new` takes an element descriptor and no declaration of `std/` can name one, so this cannot be a `native fn`.
- **`IrParameter.isByAddress` is a different flag from `isReference`.** An element the runtime takes by address is an
  ordinary **value** position - the callee takes the count of it exactly as `ownership` says - and the address is only how
  one C implementation reaches an element whose type it does not know. A place carries no count and this carries one, so
  making them one flag made the ownership verifier reject every `add`.
- **A container literal is lowered** (`ir/lower/collection.trb`): build the default implementation empty over the
  descriptors of its type arguments, fill it through the ordinary `add`/`set` of that implementation, and coerce the
  result to the trait-typed value the literal's type *is*. Which implementation is the default is written in
  `std/collections` (`List.from` answers an `ArrayList`, `Map.from` a `TrieMap`) and the lowering asks the prelude for the
  same name - a wrong answer would be a different container and not a slower one.
- **`objectTypeOf` never substituted the arguments of the enclosing instance**, so a coercion to a *generic* trait inside
  a generic body produced `Object(Iterate<Void>)` while the declared result was `Object(Iterate<Int64>)`. 5.6 never saw
  it because `Show` has no arguments. A bound that is still open after the substitution is now a clean finding
  (`closedBounds`) and no longer an internal error: `Iterate.filter` coerces its `Filtered` stage to `Iterate<Item>` and
  the `Item` the checker recorded there belongs to the stage's parameter list, which nothing has a value for. Closing that
  is a record of the checker; the bodies that hit it all need closures anyway.
- **The struct of a trait-typed value is written before the layouts**, because a layout may hold one in a field. It needs
  nothing but the ABI, so it comes first either way.
- **A witness table demands both helpers of its payload box**, not only the drop function - the `R_` half is what
  `torb_make_unique` calls on the copy, and without seeding the demand from the tables the C did not compile.
- **`torb ir --statistics` over the repository: 869 of 2689 before this round, 5181 of 10200 after, and 7402 of 12543
  once 5.8 was merged** (32% to 50% to 59%).

**The instance count, and what would bound it.** The *instance count* is the number this sub-milestone really moved, and
it is the monomorphization risk of section 7 arriving in practice. What drives it is the **element type**: a container
literal builds the witness table of `List<Item>`, and that one table drags in every default member of `Iterate`,
`Collection`, `MutableIndexed`, `Length` and `Accumulator` *for that one item type* - `map`, `filter`, `fold`, `find`,
`joined`, `added`, `contains`, and so on down. Two small real files (`syntax/source.trb` and `syntax/diagnostic.trb`) with
two element types cost 228 functions, 18 witness tables and 2 element descriptors, and `compiler/tests/lower.test.trb`
asserts all three **exactly**, so an explosion fails a test instead of being noticed as a build time. It is deliberately
not deferred to 5.14: the number to notice a regression against has to exist before the regression.

What would bound it, written down and **not built**: **share the instances of a container whose element is pointer sized
and counted.** `List<String>`, `List<Point>` where `Point` is `Boxed`, and `List<Show>` all move eight bytes and retain
through one indirect call, so one instance keyed by `(the member, "a counted pointer")` could serve all of them - exactly
the way per-bound sharing already serves the trait-typed arguments (section 1.4). The **element descriptor is the
indirection that makes it sound**: the container already calls `retain`, `release`, `equals` and `hash` through it and
never inlines them, so two element types that agree on size, alignment and "counted through one pointer" are
indistinguishable to every body in the table. What would still have to be per type is the *descriptor itself* and any
member that mentions the element by value in a way C can see - a `Construct` of it, an intrinsic over it - so the sharing
is a property of the *body* and not of the container, and the honest first step is to measure which of the ~76 members per
element type are pointer-shaped at all. Milestone 6.3 owns the measurement ("cut the obvious waste: instance count").

**What is still open, and what a list literal now blocks on.** The literal itself lowers; what a `List<Item>` cannot do
yet is what its **table members** cannot do, and every one of them is a clean finding:

| finding | what it is | slice |
|---|---|---|
| a closure, a call of a closure value | the lazy stages of `Iterate` (`filter`, `take`, `sorted`, …) and `sort` | 5.8 |
| `for` over anything but a range of integers | `Collection.addAll`, `Iterate.fold`, `find`, `forEach` | 5.7, done in the third round |
| `a[key]` | `List.swapAt`, `List.first`, `Map.mapValues` | 5.7, done in the third round |
| a range as a value | `Iterate.indexed` is `Zipped(0.., self)` | 5.7, done in the third round - and `Iterate.indexed` is exactly what makes it diverge, below |
| a receiver the back end cannot reach | not a receiver at all: a written type argument, and a static member of a trait type | 5.7, done in the third round |
| `finish`, which neither the source nor the natives manifest provides | `Accumulator.finish` is required by `Accumulator` and provided by the default of its **subtrait** `Collection`; `memberFunctionOf` only looks in the implementation of the trait that requires it and in that trait itself | 5.7, done in the third round |
| `TrieMap.iterate`, `TrieSet.iterate` | the ordered hash table's cursor has to skip tombstones, so it needs one new runtime function (`bool torb_map_entry_after(torb_map, int64_t *cursor, void *key, void *value)`) plus a third convention, `bool` plus **two** out parameters, whose wrapper builds the `(Key, Value)` tuple | 5.7 |

So the next steps of this row, in order: the `finish` provider lookup, `a[key]` reads, `for` over a collection (which
`ListIterator` now makes possible), the map and set cursor with its two-out convention, nested tables for the supertrait
narrowing, and the index paths of 5.9b.

**The third round: the provider lookup, `a[key]`, `for`, and a range as a value.** The four items the second round named
first, plus the two findings that were hiding behind each other.

- **A required member may be provided by a default of any trait of the implementing type's closure.**
  `Accumulator.finish` is required by `Accumulator` and written with a body by its **subtrait** `Collection`, and
  `checkRequirements` accepts exactly that (`hasDefaultInClosure`) - so a back end that looked only in the implementation
  of the requiring trait and in that trait itself refused 602 functions of a program the checker had approved.
  `defaultInClosureOf` in the checker answers the symbol `hasDefaultInClosure` only counted, and `providerOfMember` in
  `ir/witness.trb` asks the three questions the requirement check asks, in its order: the implementation (which is
  `providerOf` and therefore covers the target's own members, its fields and every other implementation of the same
  target), the default of the requiring trait, then the default of any other trait of the closure.
- **"A receiver the back end cannot reach" was two findings, and neither was a receiver.** `to<List<Item>>()` writes its
  type arguments *between* the receiver and the call, so the callee is `.Generic(.Name("to"), …)` and never a `.Member` -
  and `receiverSlot` asked for `Adaptation.ImplicitSelf` at the span of the whole `.Generic` node, where the checker
  records nothing. Unwrapping `.Generic` then uncovered `Target.from self` in `Iterate.to`: a **static** member of a
  generic parameter that turned out to be a trait type. There is no value there to erase, `Traits([List<Int>])` is a
  closed type like any other and `witnessFor` names the one implementation of it, so `.Forwarded` goes dynamic only for a
  member with a receiver now. Object safety says the same thing from the other side: a static member is never in a
  table.
- **`a[key]` is `Indexed.at`, and the panic on a missing key is the language's own.** `at` is a *default* of `std/core`
  whose body is `get(key).expect("Key does not exist")`, so what an index out of range does is decided once and is the
  same in every back end - `lowerIndexRead` needs no rule for it at all. The tail of `lowerCallable` - which of the five
  shapes a member is (`panic`, a table member, a native, a generated body, an instance of a source body) and the `Call`
  for it - became `lowerDispatched`, so an index hands its two slots to the one place that knows those five shapes;
  `lowerWitnessCall` and `lowerNativeCall` take a `Span` instead of an `Expression`, which is all they read out of one.
- **`for` over a collection is BACKEND 1.6 exactly**, and three properties make it correct without a rule of the `for`:
  the cursor is a **`var` local of the frame**, so `Iterator.next()` is an ordinary place and a trait-typed cursor
  has its payload box made unique first; the cursor holds the subject **by value**, so changing the collection inside the
  body does not change what the loop walks; and a counted item is read into the same slot every round, which makes it
  dead on the back edge and its drop an ordinary edge drop. `continue` jumps to the head, where the next value is pulled,
  and `break` to the block after it - so neither needs the step block the counter of a range has. `dispatchedOn` in
  `ir/lower/generic.trb` is the dispatch of a member the **lowering itself** calls, with no call site to read a
  resolution from; it asks the very question a written call asks, so `xs.iterate()` reaches the same function either
  way.
- **A range as a value is one `Construct`.** A `Range` is the one `native type` that declares fields and is no
  `RuntimeKind` (5.1's note), so `3..7` builds its three fields - an `Option` around each end, and `inclusive` as a
  `Bool` - in source order. Nothing about *iterating* one is here.
- **`Range<Int>.iterator` is deliberately still a `.Planned` native, and the reason is not the runtime: the instance set
  diverges.** Writing it as `RangeIterator` in TorbScript (which is what "natives stay few" asks for, and which is two
  dozen lines) makes `Iterate.indexed` reachable - and `indexed(self): Iterate<(Int, Item)>` is `Zipped(0.., self)`. So
  the table of `Iterate<(Int, Item)>` for `Zipped<Int, Item>` holds `indexed` again, which needs
  `Zipped<Int, (Int, Item)>`, which holds `indexed` again, and the worklist never ends: `torb ir ..` over
  `compiler/src/syntax` alone does not finish in five minutes where the whole repository takes two. Every collection's
  table holds `indexed`, and the only thing that kept it unreachable was that `0..` was not a value. **This is the
  monomorphization risk of section 7 arriving as a non-termination and not as a build time**, and it has to be decided
  before `Range.iterate` can be TorbScript: either the instance sharing the note above describes (a container whose
  element is a counted pointer is one instance), or a bound on the depth of an instance's type arguments, or `indexed`
  out of the table (which needs a reason object safety does not give). Milestone 6.3 owns the measurement; the
  two entries stay `.Planned` until then, and the 705 functions that ask for one are the price.
- **`Tables.collectionLiterals` is read, and only `Default` is lowered.** The checker decided which of the three shapes a
  literal is (gap 51, TYPECHECKER 8) and recorded it at the literal's span, so the back end reads that decision instead
  of asking the expected type a second time and answering it differently. `InlineArray` writes its items into the inline
  slots of an `Array<Item, Size>`, which is a `FixedArray` and therefore 5.9b's; `FromIterate` builds the default list
  and hands it to the `from` of the target, and the `from` to call is an ordinary member of the `witness` the checker
  recorded - but every one of them bottoms out in `TrieSet.from` or `ArrayQueue.from`, the same `.Planned` natives as
  `ArrayList.from`, so it waits for the same step and names its target meanwhile. There is **one** `FromIterate` in the
  repository (`std/http`) and no `InlineArray` at all.
- **Two bugs of the lowering that `for` made reachable, both older than this round.** A *destination is a hint*, and the
  one `Void` slot of the function is the wrong hint for a value: an `if` used as a statement has the type `Void`, so both
  of its arms produce into that slot - and an arm whose last expression answers a value nobody takes (`parser.bump()`,
  whose `Token` is discarded) was a call of a `Token` into a `Void` slot. `valueSlot` refuses that one case and keeps
  every other disagreement, because `?.` lowers its member into a slot of the *payload* type on purpose. And `last` on a
  place argument is legal: it is about the **base**, whose own count dies at the call, which is what the `Release` after
  it is for (5.10 found the same thing).
- **`torb ir --statistics` over the repository: 7381 of 12511 before this round, 15257 of 17250 after** (58% to 88%; the
  total grows by a third, because a `for` that lowers reaches every default of `Iterate` for every element type). The
  `finish` (602), `a[key]` (382 plus what the table members behind it unlocked), `for` over anything but a range (1995,
  which grew to 2614 as the other three unlocked bodies) and "a receiver the back end cannot reach" (536) findings are
  all gone. The new top blockers are `Range.iterate` (705, above), "a declaration the back end cannot build an instance
  of" (258), `TrieMap.iterate` (174, the map cursor), the index *paths* of 5.9b (149 + 140 + 34) and `ArrayList.from`
  (140).
- **`ArrayList.from` cannot be a function of the runtime at all**, which its `.Planned("5.7")` entry does not say:
  `from(items: Iterate<Item>)` walks a trait-typed value of the *program* through a witness table, and a C function
  cannot. It is `var result = ArrayList.withCapacity(0)  result.addAll(items)  result` in TorbScript, which every piece
  of now exists - but `withCapacity` has no element descriptor in its declaration, so the wrapper has nowhere to get one
  and `ContainerNew` is the instruction that does. Closing it is `ContainerNew` plus `addAll`, and it is the next step of
  this row together with the map cursor.

**The fourth round: what bounds the instance set, and the first collections that really run.** The third round measured
a worklist that does not end; this round decides why, and then writes the gate programs that three rounds had not
written.

- **Decision: a default member that no implementation in the program overrides is not a slot of a witness table.** It is
  dispatched statically with `Self` bound to the **trait type** of the erased value - the mechanism 5.6 and 5.8 already
  use for a default with generic parameters of its own (`map<Output>` on a trait-typed receiver), and `dynamicDispatch`
  already had the fallback. It is sound because a default body reaches `self` only through the members of its own trait,
  so one instance per trait type serves every implementer, and what runs is what would have run anyway: nobody overrides
  it. This is Rust's `where Self: Sized` on `Iterator::enumerate`, arrived at from the other side. A default that some
  implementation **does** override stays a slot (which of the two runs is a run-time question, 5.6's decision 3), and an
  implementer that does not override it keeps pointing its slot at the per-type instance of the default body - the
  simpler of the two shapes the note allowed, and the one that was already there.
- **"Overridden anywhere in the program" is a closed-world question, and the back end may ask it.** It sees the whole
  program: C is one translation unit, and the VM loads whole modules. `collectOverriddenMembers` in `ir/witness.trb`
  walks every implementation once and collects every name a member can be answered under - the members of an
  `extend ... with` block, the body of a `type ... with`, and the body of the implementation's *target*, which
  `providerOf` reaches too. It **over-approximates on purpose**: reading a name as an override that nothing really
  overrides costs one slot in one table, while missing one would run the default where the override belongs, which is
  observable. Two exclusions are load bearing and both were measured rather than guessed. A target whose head is a
  **trait** contributes nothing, or `extend<Item: Show> List<Item> with Show` would name every default of `List` and the
  decision would be a no-op. And the **fields** of a target are not in it although `providerOf` answers a member with
  one: a field is not a method and cannot be the body of a default, and one field of the compiler's own `WellKnown` is
  called `indexed` - which put `Iterate.indexed` straight back into every table and brought the non-terminating worklist
  back with it (the tripwire measured 967 declarations and eight `do not end` findings until the fields came out). For
  separately compiled packages later this means the table layout of a trait is fixed **per program** - which it already
  is, because a table is emitted with the program that uses it and its member order is computed from the same closed
  world.
- **The effect, measured.** The instance-count tripwire of `compiler/tests/lower.test.trb` went from 208 declarations /
  258 functions / 20 witness tables / 2 element descriptors to **78 / 108 / 24 / 2** - a quarter of the functions, and
  more tables only because the *nested* ones below are built now. Over the whole repository `torb ir --statistics ..`
  went from 15257 of 17250 (88%) to **8301 of 9182 (90%)**: the *total* is what halved, because every collection's table
  used to drag every default of `Iterate`, `Collection`, `MutableIndexed`, `Length` and `Accumulator` in per element
  type. The 705 `Range.iterate` findings and the 140 `ArrayList.from` findings are gone (both are TorbScript now), the
  `a[key]` assignment finding of `List.swapAt` and `List.updated` is gone from every table (neither is instantiated at
  all any more), and the wall time of `torb ir --statistics ..` fell from 1m53s to 1m39s.
- **The backstop, for the case the decision does not cover.** An overridden default that is itself type-growing would
  still not end, so `boundedArguments` in `ir/instances.trb` refuses an instance whose type argument nests deeper than
  **10** and reports "the instances of `deepen` do not end: its type argument `...` nests deeper than 10" at the
  declaration. The deepest argument the repository really instantiates nests four, so nothing legitimate comes near it,
  and a test pins that a self-growing generic function ends in that finding instead of in a hang.
- **`Show` of a collection was unreachable for a reason that has nothing to do with tables.** `showSlot` in
  `ir/lower/text.trb` looked a trait-typed value's `show` up in the value's **own** tables and reported otherwise -
  while the whole point of `extend<Item: Show> List<Item> with Show` is that the implementation is for the **trait
  type**, which is a static call. 5.10 taught `dynamicDispatch` exactly that fallback and this one place kept asking only
  the tables, so `print numbers` and `"{numbers}"` were refused for every collection in the language. It is the same two
  lines here now.
- **Nested witness tables are built** (5.6 left `nested` empty and named this as what needs it). `nestedTablesOf` fills
  one entry per **direct** supertrait, in the order of `supertraitsOfBound`, and `narrowingPathOf` walks that DAG to find
  the bound index plus the `nested` indices a coercion has to follow; the emitter renders a step as `->nested[i]`. The
  indices are a property of the **trait** and of nothing the value erased, which is exactly why a narrowing works at all.
  Two things fell out of building them: a supertrait whose own table cannot be built leaves a **hole** (a null pointer, and
  the narrowing that would have read it is a clean finding, which is what `Lowering.isSilent` is for), and the tables are
  emitted in **topological** order rather than by name alone, because the initializer of a `static const` takes the
  address of its nested tables and a tentative definition of a `const` object is not portable C (MSVC refuses one). The
  order is still a pure function of the set: the name-smallest table whose nested tables are all placed goes next.
- **`ArrayList.from` is TorbScript, and `withCapacity` is the one native the lowering answers itself.**
  `NativeTarget.Container(kind)` is a new manifest target that means `Instruction.ContainerNew`: `torb_list_new` needs one
  element descriptor per type argument, and no declaration of `std/` can name one, so this cannot be a `native fn` at all
  (the third round wrote that down). `ArrayList.from` is then
  `var result: ArrayList<Item> = ArrayList.withCapacity 0  result.addAll items  result`, which is what unblocks
  `toList()`, `to<List<Item>>()` and `Iterate.to`. The capacity is dropped for now: it is a hint, and giving
  `ContainerNew` an operand would change the instruction in both back ends for something that is not observable.
  `TrieList.from`, `TrieSet.from`, `TrieMap.from` and `HashSet.from` stay `.Planned` - the tries need the trie, and the
  hash containers need the cursor below.
- **`ArrayList.withCapacity(0)` also needed the *type* of a static member of a generic type.** `ArrayList.from` calls it
  on `ArrayList` written as a bare name, and what the checker records in front of the dot is the type of the type's
  **constructor** (`(capacity: Int) => ArrayList<Item>`). 5.7's first round made `closedReceiver` answer `None` for a
  function type, which was right for `Int.tryFrom` (`Int64` has no arguments) and leaves a *generic* owner with nothing:
  `constructedReceiver` takes the constructor's **result** as the type the member is reached on, and only where its head
  is the head of the member's own owner - so `show` on a closure value is still not dispatched on the closure's result.
- **`Range<Int>.iterator` and `length` are TorbScript** (`RangeIterator` in `std/core/src/range.trb`, 20 lines), which is
  what the third round could not do because the instance set diverged. `Show` of a `Range` is an
  `extend<Value: Show> Range<Value> with Show` that writes the **source** form (`0..10`, `0..=10`, `..10`, `0..`), and the
  `.Derived` manifest entry for `Range.show` is gone. **`Equals` and `Hash` stay where they are**, although the derived
  body of either needs `Value: Equals` or `Value: Hash` for the two `Option<Value>` fields and is in that sense as
  conditional as `Show` was. The difference is what the derived answer *says*: field by field is the right answer for
  both, so a `Range<Value>` whose `Value` cannot be compared is a clean finding inside a generated body and never a wrong
  one - while a structural `Show` printed `Range(start: Some(0), end: Some(10), inclusive: false)`, which is not what a
  reader writes and not what stage 0 prints. Keeping stage 0
  identical needed two fields on its own `Range`: whether a start was written, and whether the range was inclusive - it
  normalized both away and printed `0..7` for `..7` and `1..4` for `1..=3`. A range of anything but `Int` is still a value
  stage 0 cannot build at all, which is one more entry on 5.14's list.
- **Four bugs the gate programs found, and every one of them is the kind only running finds.**
  - **A witness thunk borrowed everything.** The erased ABI borrows the payload and every argument, because a call site
    knows the member's index and not its declaration - but the real member may take one **owned**: the `self` of
    `ArrayList.iterate`, whose `ListIterator` stores the list, and the value of `ArrayList.add`, which the buffer keeps.
    The thunk is where the counts are made now (the payload through a local of its own, because a read-only member's box
    is `const`). Without it `for x in list` and `list.add(text)` released the same block twice.
  - **A closure body's parameter may never be `Owned`.** `CallClosure`'s callee is a *slot*, so no call site can read the
    summary of the function it will reach, and the type of a closure is the type of every closure of its shape (5.8's
    note) - so the signature in the type cannot answer it either. `summarizeParameters` skips every function a `Closure`
    instruction points at, which also covers a named function used as a value. `Iterate.joined` builds
    `separator + item` and was exactly that.
  - **And therefore a closure that *hands on* its parameter needs a retain, which the pass used to walk past.** The
    other half of the rule above, found by `torb ir ..` over the compiler itself and the one internal error it was left
    with: `items.filterMap({ _ })` is a closure whose body answers its own borrowed parameter, so the frame owns no value
    of its own at all - and `rewriteFunction` left a function without a single *owned* counted slot exactly as the
    lowering wrote it, `return %0 borrowed` included. `verifyOwnedProgram` rejected that, rightly: a `return` keeps what
    it is given. The early exit now asks `holdsCountedValue` (any slot whose *type* carries a count, whoever owns it)
    instead of `countedSlotsOf` (the slots the frame owns), and everything after it is the ordinary rule for a borrowed
    value at an `Owned` position: retain, and say `owned` without `last`. A frame that holds no counted value at all is
    still not walked, so the arithmetic half of a program is byte for byte what the lowering wrote - and the three shapes
    it took (answering the parameter, building it into a record, answering a capture) are pinned in
    `lower-closures.test.trb` plus `tests/conformance/closure-counts.trb`, whose values are all built at run time
    because the storage of a *literal* is immortal and would hide a missing retain.
  - **The out parameter of a `.Optional` wrapper is owned on one path only.** `torb_list_get` writes nothing past the end,
    so liveness released a slot the runtime never wrote - which for a trait-typed element is `NULL->drop`. Both the drop
    edges (`conditionalOutSlotsOf`) and the ownership verifier (`conditionalOutPositions`) know it now; the `true` path
    moves the value into the `Option` it builds, so there is nothing to leak.
  - **`type Point with Show {}` declares the implementation and writes no member.** The checker accepts that for a trait
    the language derives, and the back end only generated a body for an implementation the *checker* had created - the
    origin of an implementation says who wrote it down, not who writes the body.
- **The gate is not `language.trb`**, and it cannot be: like `basics.trb` before it, it is a stage-0 **script** and not a
  checked program. Eleven of its lines are refused by the type checker itself and by no back end - `onStart { "...{port}" }`
  captures the receiver `self` in a closure that may outlive the call, `print counters.map({ _.count })` asks `Iterate<Int>` for
  `Show`, `samples[1..4].sort()` calls `sort` without its `by`, `Seconds` implements none of `Add`, `Compare` or
  `Subtract`, `match` does not handle `[_, _, ...]`, and `visit`/`seen`/`limit`/`toMap` are names that are not there. So
  5.7's row needs a gate that is a program: `tests/conformance/{collections,ranges,collection-index}.trb` are it -
  the literals of every element shape, `for` over a list and over a trait-typed `Iterate` with `break` and `continue`,
  `a[key]` and its panic (exit 101, the message of `Indexed.at` and therefore the language's own), growth over the
  doubling of the buffer, copy on write through a local, a field and a `var` parameter, the closure pipelines, and a range
  in all four spellings. Each is compiled, run, compared with stage 0 byte for byte and asserted to leave zero live
  blocks. **`examples/tour/src/07-collections.trb` is two findings away**, and both are the index *places* of the next
  step: `numbers[0] = 5` and `swapAt`'s `var` argument through `a[key]`.
- **What is left of the row, measured on this tree.** "A declaration the back end cannot build an instance of" (260, the
  biggest single blocker left and not yet diagnosed), the index paths of 5.9b (155 + 34 + 11), `TrieMap.iterate` (137,
  the map and set cursor with its `bool`-plus-two-outs convention, which also blocks **every map and set literal**:
  `iterate` is required by `Iterate`, so the table of `Map<Key, Value>` cannot be built at all and `["a": 1]` does not
  lower), `String.chars` (31), `sort` on a trait-typed value (20), the variadic parameters and arguments (8 + 5), and the
  list patterns (8). The `print` interception of 5.10 therefore still stands, because a variadic parameter is still a
  finding.

**The fifth round: the index paths, and what a `var` path through a container really is.** The fourth round's last
sentence named `numbers[0] = 5` and `swapAt`'s `var` argument as the two findings between the back end and
`examples/tour/src/07-collections.trb`. They were 178 of 483 on this tree, and closing them decided what an index on a
path *is*.

- **Decision: an index step of a path is a take-out and a put-back of two members the language already has, and not
  `Instruction.TakeOut`/`PutBack`.** `points[0].x = 100` reads `points[0]` into a slot of the frame through
  **`Indexed.at`**, writes the `x` of that slot, and puts the element back through **`MutableIndexed.set`**. That is
  literally the concept's model of an index path ("take it out, change it, put it back", `var` Paths) with the two halves
  spelled as the two members `std/core` declares - so what a *missing key* does is decided in `std/` and not in a back
  end: a `var` access panics, because `at` is `get(key).expect("Key does not exist")`, and `map[key] = value` **inserts**,
  because the assignment is `set` and nothing else.
- **Why the two instructions could not serve.** They name a `RuntimeKind` and reach `torb_list_element_reference` /
  `torb_map_take_out`, which needs the receiver to be a *concrete* container. The container of a path in this repository
  is almost always a **trait-typed** value (`List<Int>`, `Map<String, Int>`, and `Self` inside a default member of
  `List`), and a witness table says which members a value has and never that its payload is a `torb_list` -
  `List<Item>` may be implemented by anything. So a trait-typed index path can only go through the trait, and the
  member pair is the only form that serves both. The price is one copy of the element out and one in, which is not
  observable (a value has no identity) and costs a retain plus a release for a counted element. `TakeOut`/`PutBack` stay
  in the IR for the contiguous shapes of 5.9b - `Array<Item, Size>` and a slice window - where the receiver is concrete
  and an interior pointer is the point.
- **The lowering walks the target expression in step with the place's steps.** `PlaceStep.Index` carries only the *span*
  of the key (4.6 had no reason to keep more), and the lowering does not lower from spans - so `indexKeysOf` walks the
  target and collects the expression inside every bracket, from the root outwards, which is the order the steps of a place
  are in. That does not break 5.9a's rule that the lowering never walks an expression twice: the walk collects key
  expressions and nothing else, the path itself is still the one the checker wrote down, and the target is lowered **only**
  through its path and never also as a value.
- **Every key is lowered before the access begins,** in source order, into a slot of its own - which is the evaluation
  order of a `var` path (CONCEPT, `var` Paths, after the model of Swift: all arguments and keys are evaluated first, and
  only then does the access begin). The take-out is already part of the access, so `grid[y][x] = v` lowers `y`, then `x`,
  then reads the row, then writes the cell. The **put-backs run innermost first**: the cell has to be back in the row
  before the row goes back into the grid, or the change would be lost.
- **`PlacePath` carries what has to be put back, and every caller of one emits it after its access.** A place is formed in
  five places (a `var` receiver, a `var` argument of a call and of a closure call, an assignment, a property command), and
  `putBackElements` is one call in each of them - which is why `CallOperands` grew a third list. Nothing on the
  `Lowering` remembers a pending put-back: a place that is formed while another one is being formed would then put the
  wrong element back.
- **An assignment whose last step is an index is a `set` and no `Write`.** That is the whole difference between a write
  and an insert, and it is what makes `grid[y][x] = v` fall out of nothing: everything in front of the last index is an
  ordinary place, taken out and put back around the `set` by the very machinery a `var` argument uses.
- **Copy on write needs no rule of its own,** which is the test of the design. The element the frame holds shares its
  storage with the copy the container still has, so the access - a `Write` through the element's slot, or a `var fn` receiver
  callee writing through the pointer it was handed - makes it unique for the ordinary reason, and `set` releases what the
  container had there. `tests/conformance/collection-places.trb` proves it: the copy of a list taken before
  `numbers[0] = 9` still reads `[1, 2, 3]`, and the same for a list of counted elements and for a nested one.
- **The gate found nothing, and that is worth writing down**: the program was byte identical to stage 0 and left zero live
  blocks the first time it ran, which is what building an index path out of ordinary calls buys - there is no new
  instruction, no new emitter case, no new runtime function and no new ownership rule, so there was nothing new to get
  wrong. The one bug of the round was found by the IR verifier instead: a `places` list has to be **as long as the operand
  list**, because that is how a call says which of its positions takes a place (`borrowedArguments` falls back to
  borrowing everything when the two lengths differ, and the verifier then reported "argument 0 has to be a place").
- **`torb ir --statistics ../compiler`: 7471 of 7954 lowered before, 7691 of 7997 after** (93% to 96%). The three index
  findings (139 `var` arguments, 34 `var` receivers, 5 assignments) are gone.

**The map and the set cursor, which is what blocked every map and every set literal.** `iterate` is a *required* member of
`Iterate`, so the witness table of `Map<Key, Value>` could not be built at all and `["a": 1]` did not lower - 130 findings
that were all one missing cursor.

- **One new runtime function, and it is a native for a reason the doc comment gives**: `torb_map_entry_after(map, &cursor,
  &key, &value)` (plus `torb_set_item_after`, which is the same walk with nothing on the value side). A position is an index
  into the storage's own **entry vector**, and a removal leaves a tombstone in it - so nothing written over `length` and
  `get` can walk a table that has been removed from, which is exactly what "natives stay few" asks to check before adding
  one. It hands out **retained copies**, because the cursor is a value of the program that may outlive a write to the map.
  `torb_map_next` stays for the runtime's own C (borrowed pointers, no retains), and three tests in
  `runtime/tests/map_test.c` pin the tombstone skip, the end of the walk and the retains.
- **Decision: the third convention is `bool` plus one out parameter *per field* of the payload** (`NativeResult
  .OptionalParts`), and not `bool` plus a pointer to the tuple. Two reasons, and both are the same reason: a `(Key, Value)`
  is a layout of the **program**, which `runtime/` may not build (5.R1), and the two halves of an entry live in two arrays
  of the table anyway, so nothing could write one in a single piece. `RuntimeShape.out` became `outs: List<IrTypeId>`, the
  parameters are named `out0`/`out1` where there are several, and the wrapper constructs the tuple and then the `Some`.
  Everything else was already there: the conditional out parameter of the fourth round makes a counted half `owned` on the
  `true` path and releases it on neither other one, for two outs exactly as for one.
- **`iterate` is TorbScript, and so are `from` and `withCapacity`'s replacement.** `MapIterator` holds the table by value
  and an index, `SetIterator` the same; `TrieMap.from` is `withCapacity 0` plus `addAll`, exactly as `ArrayList.from` is;
  and `withCapacity` is a `NativeTarget.Container(MapStorage)`, which is `Instruction.ContainerNew` - so
  `torb_map_with_capacity` is out of the manifest and the element descriptors stay the lowering's business. The runtime has
  **one** function more than before this round and five manifest entries fewer.
- **Two cursors per kind, and the duplication is the price of gap 18.** `TrieMap` and `HashMap` are one hash table until
  milestone 8, but they are two *types*, so a value of one is not a value of the other and one cursor cannot hold both.
  Writing it generically (a bound `EntryAfter<Key, Value>` on a third parameter of the cursor) would put a trait nobody
  needs into the public API of `std/collections`; twelve lines twice says the same thing and goes away with the trie.
- **Three bugs the gate found, and two of them were older than this round.**
  - **A set's element descriptor had no `equals` and no `hash`.** `containerBody` asked "are there several type arguments"
    to decide which of them is a key, which is right for a map and wrong for a set: `torb_set_new` *is* the table with
    nothing on the value side, so its one element is a key. It is `isKeyedContainer(kind)` now.
  - **A member the implementation does not provide may be a default of another trait of the target's closure**, and only
    the *witness table* asked that question (`providerOfMember`, the third round's finding). A direct call asked
    `implementation.members` alone and fell back to the declaration - so `TrieMap.from`'s `addAll` had nothing to call,
    because `add` is required by `Collection` and written with a body by `Map`. Both sides ask `providerOfMember` now.
  - **`values.to<Map<Key, Value>>()` was dispatched without a receiver at all.** The type arguments stand *between* the
    receiver and the call, so the callee is a `.Generic` node; `receiverSlot` unwrapped it since the third round and
    `receiverTypeOf` did not, so the subject of the dispatch was `None`. With it, a **static** member of a trait-typed
    type (`Target.from self`) is dispatched statically wherever `witnessFor` names an implementation - which is what
    `.Forwarded` already did and what object safety says from the other side. `Set.of()` shows what is left: a static
    member of the trait *itself* has no implementation to name, and the honest finding for it is its **variadic**
    parameter.
- **Stage 0 printed a set as `Set.of("a", "b")`,** and `std/collections/src/set.trb` writes `{"a", "b"}`. `Show` is the
  contract between the two implementations (the `Range` of the fourth round was the same kind of divergence), a set has no
  literal of its own so nothing about the braces is a spelling of the source, and the standard library is the side that
  decides - so the interpreter changed, not the library.
- **The gate is `tests/conformance/maps-and-sets.trb`**: the literals over number, `String` and record keys and over a
  collection as the value, insertion order (including a key that is set again), `for (key, value) in map`, `keys()` and
  `values()`, membership, a removal followed by a walk over the tombstone, `a[key]` as a value and as a path
  (`counters["a"].increment()`), copy on write through two copies taken at different times, and `toSet`/`groupBy`/
  `Map.from`. Byte identical to stage 0, `live blocks at exit: 0`.
- **`torb ir --statistics ../compiler`: 7691 of 7997 lowered before, 11357 of 11553 after** (96% to 98%). The *total* grows
  by 3556, because a map and a set that lower reach every default of `Iterate`, `Collection` and `Accumulator` for every
  key and value type they are used with. `TrieMap.iterate` (130) and the "without a receiver" findings are gone.

**`sort` is a default of `List` and written in TorbScript, and `slice` is the one that is still open.** Both were
"`x`, which is not in the witness table of a trait-typed value" (27 + 11), and they are not the same problem at all.

- **Decision: `sort` is a default member of `List` and no requirement of it.** A member with generic parameters of its own
  is no slot of a witness table (its witnesses would have to be appended), so `sort<Key: Compare>` could not be reached on
  a `List<Item>` value as a requirement - while a **default** nothing overrides is dispatched with `Self` bound to the
  trait type (the fourth round's decision), which is exactly what a trait-typed receiver needs. So the answer to "how is
  such a member reached" is: by not being a requirement.
- **And its body is TorbScript, so `torb_list_sort` leaves the manifest.** The comparison is a **closure of the program**,
  and a C function cannot call one: reaching `torb_list_sort(list, compare, context)` would need a fourth convention for
  handing a closure to the runtime, and the closure ABI of the IR is a code pointer plus an environment and not
  `(const void *, const void *, void *)`. One sort that every list shares is worth more than that convention - and it is
  a *bottom-up merge sort over `a[key]`*, `n log n`, stable, no recursion, one buffer that is copied once. Which makes
  `sort` the first real user of the index paths of the fifth round. The native fast path can come back when a closure can
  be handed to the runtime; until then `ArrayList` and `TrieList` declare no `sort` at all.
- **A bug of the ownership pass that `sort` uncovered seventeen times:** a function whose counted slots are all borrowed
  parameters was left exactly as the lowering wrote it, and a `{ _ }` passed to `sort` is exactly that - a closure that
  answers the value it was handed. Its `return` then gave away a count the frame never had ("`return` keeps %0, so the
  operand has to be `owned`", which was the one internal error of `ir --statistics ../compiler` before this round and
  became seventeen when `sort`'s callers started lowering). A closure body's parameter may never be `Owned` (no call site
  can read the summary of the function a `CallClosure` will reach), so the count has to be made where the value is
  answered: `answersBorrowedValue` in `ir/ownership.trb` makes the pass walk such a function, and `decideOperand` then
  emits the one `Retain` in front of the `return` that it always would have. Nothing else about those functions changes.
- **Decision: `slice` is a default of `List`, and a member whose result is `Self` stays out of every table.**
  `Slice.slice(range: Bounds<Int>): Self` is a **required** member that mentions `Self` as its **result**, so no table
  can hold it: the thunk of one would have to box that result, which it can do for *one* bound - its own - and not for a
  value that carries several (`List<Item> & Show` calling `slice` would need the `Show` table of a type the thunk has
  erased). `Self` as a **parameter** (`MutableSlice.replace(range, values: Self)`) can never be in a table at
  all, because a thunk would have to unbox an argument whose type it cannot check. So the answer is the same one `sort`
  got: **write it in `std/collections` and let the trait type dispatch it statically.** `ArrayList` keeps its native, where
  a slice is O(1) and shares the storage; a trait-typed `List<Item>` reaches the default, which is `var result = self`,
  `result.clear()` and one `add` per item of the part - the only way a default can build a `Self` whose type it does not
  know, and the price of not knowing it is one copy of the part. Nothing observable differs between the two, which is what
  the gate asserts. **A `var` path *through* a slice is untouched** and still 5.9b's: a window is what an interior place
  needs, and that is a different mechanism from a slice as a value.
- **And the lookup a default needs was missing in `dynamicDispatch`.** The bound a call resolved is the trait that
  *requires* a member, and another trait of the closure may be the one that writes it - `Slice.slice` is required and
  `List` gives it a body, exactly as `Collection.add` is required and `Map` gives it one. `providerOfMember` asks that
  third question for a witness table and `memberOfImplementationDispatch` asks it for a static call; the dynamic path
  asked only the bound's own members and then reported. With `providedInClosure` in front of it, all 13 `slice` findings
  are gone and **`torb build ../compiler` is down from 17 problems to 4** - a closure that captures a `var` parameter
  (5.9b) and three generated `compare`s of a tuple (5.10), neither of them this row's.
- **`torb ir --statistics ../compiler`: 11357 of 11553 lowered before, 11475 of 11647 after** (98% to 98%, and 172 findings
  left). The 27 `sort` findings and every internal error are gone; three new ones are a **generated `compare` of a tuple**,
  which the compiler sorts by and which 5.10 left as a finding (`Show`, `Equals` and `Hash` are structural, `compare` is
  not generated) - they were unreachable while `sort` was.

**Variadic parameters, variadic arguments and a spread - and why the `print` interception stays.** `...items: Item` is one
parameter that every positional argument from its position on fills; it is a **`List<Item>` the call site builds** and the
**collection trait inside the body**, and those are two different types of the same parameter.

- **The signature says what the body sees, and the call site builds what the signature says.** The checker declares the
  binding of `...values: Show` as `List<Show>`, which in a type position is a trait-typed value - so `parameterTypeOf`
  answers `Object(List<Item>)` and no longer `Runtime(ListStorage, Item)`. Those two disagreed: a body would have read a
  buffer through a pointer to an object. The call site then builds the buffer, fills it and boxes it, which is exactly what
  a list literal does - one function does both (`lowerItemList`), so a variadic argument list and `[1, 2, 3]` can never
  come out differently.
- **A variadic that collects nothing is the empty list and no missing argument.** `print()`, `Set.of()` and
  `List.of()` are calls, which is what the 11 findings of "a parameter without an argument and without a default" really
  were: the list is built before the ordinary argument loop runs, so the position is always filled.
- **Decision: a spread is one `add` per item, written out by the lowering, and not a call of `addAll`.** The fourth round
  decided that a default no implementation overrides has **one** instance with `Self` bound to the trait type - so
  `Collection.addAll` takes an `Object(List<Item>)` place, while what a literal fills is the concrete buffer. Boxing the
  buffer to call it would hand the callee a box of its own and the items would never reach the slot. So `spreadInto` writes
  the loop BACKEND 1.6 describes for a `for`: the subject into a slot, `iterate()`, a head that pulls `next()` from a
  `var` local and switches on the `Option`. That is what makes `total(...numbers.map({ _ * 10 }))` work - a spread of a
  **pipeline**, which is an `Iterate` and no list at all - and it is the same code for a spread in an argument list and one
  in a literal (`[1, ...numbers, 4]`), which was a finding until now.
- **An operator on a trait-typed value was dispatched without a receiver at all.** `operandTypeOf` answered `None` for
  anything but an implementation dispatch, so `replaced == arguments` on two `List<TypeId>`s reported "a call on a
  trait-typed value without a receiver" - all 10 of them. The type of the **left operand** is the receiver an operator has
  (`a + b` is `a.add(b)`), and it is threaded from `lowerBinary`, which is the one place that still has the expressions.
  `Equals.equals` mentions `Self` as a parameter and is in no table, so what this decides is not whether the call is
  dynamic but whose `equals` runs.
- **A case is a function from its own fields, so its defaults are the fields'.** `declaredDefaults` read a *function*
  declaration and a case symbol is not one, so `Instruction.Call(target, callee, arguments, witnesses)` - four arguments
  for a case whose fifth field has a default - was "a parameter without an argument and without a default". Nine findings,
  all in the compiler's own tests, which build IR by hand.
- **Decision: `print` keeps its two instructions, and that is no longer a placeholder.** 5.10 wrote that the interception
  could go once 5.7 built the list. It cannot, and the reason is the same one that makes `ArrayList.from` TorbScript: a
  `List<Show>` would have the **runtime** call `show` through a witness table, which a C function cannot do. `print` would
  therefore have to become TorbScript over a new `printText` native - which moves the one space and the one `\n` out of
  `runtime/console.c`, where 5.R1 put them on purpose, and costs a list plus a concatenation at every call. What this round
  changed is that `PrintParts` is a **fast path** and not the only path: every other variadic call builds its list, so
  nothing of the language waits for it. `print(...parts)` is the one shape the fast path cannot take (the number of
  operands would be a run-time question) and is a clean finding.
- **The gate is `tests/conformance/variadics.trb`**: a variadic read with `for` and with `joined`, a variadic after an
  ordinary parameter, a trait-typed element (`...values: Show`, every argument coerced where it is written), the list
  answered as a value, zero/one/many arguments, a spread alone and mixed with written arguments and twice in one call, a
  spread of a pipeline, a spread in a literal, and `List.of`/`Set.of`/`Map.of`/`List.filled`. Byte identical to stage 0,
  `live blocks at exit: 0`.
- **`torb ir --statistics ../compiler`: 11475 of 11647 before, 11628 of 11780 after** (98% to 98%, 152 findings left). The
  variadic findings (11 + 1), the 10 "without a receiver" and the 9 case defaults are gone; `examples/tour/src/07-collections.trb`
  lowers except for one generic declaration nothing can build an instance of.

**The first round's chain, for the record.** The row of the table says 5.7 depends on 5.8, and this is why - the chain is
longer than "the pipelines need closures":

1. **A list literal is a trait-typed value.** `[1, 2, 3]` has the checker type `Traits([List<Int>])` (5.6's note, "a
   trait name in a type position is an `Object`"), so lowering one means building an `ArrayList<Int>`, filling it, and
   boxing it with the witness table of `(ArrayList<Int>, List<Int>)`. The literal is therefore not reachable without
   that table.
2. **The table of `List<Item>` cannot be built yet, and the first thing it blocks on is `iterate`.** Measured:
   `ArrayList<Int>` coerced to `List<Int>` reports `` `ArrayList.iterate`, which the runtime provides from milestone 5.7
   on ``. `iterate` is a required member of `Iterate`, so it is in every collection's table.
3. **`iterate` should be TorbScript and not a runtime function** ("natives stay few"): a `ListIterator<Item>` over the
   list and an index, whose `next` is `items.get(index)` - which the `.Optional` convention above now makes callable.
4. **But `Iterator.next()` is a `var fn` member**, and a `var fn` member is still left out of a witness table
   (5.6's list). So `for x in xs` cannot pull from a trait-typed iterator.
5. **And putting a `var fn` member back into a table needs one thing the IR cannot express: making the payload box of a
   trait-typed value unique.** A `Copy` of an `Object` retains the box, so two copies share it; a write through a table
   without a `MakeUnique` of that box would change both, which is observable. The box's size and its `R_`/`D_` pair are a
   property of the *target* type, which a trait-typed value has erased - so the **witness table has to carry them**.
   That was the decision the second round took, and it unlocked 4, 3, 2 and 1 in that order.

**Nested witness tables are still not built.** 5.6 left `WitnessTable.nested` empty and named `List<Item>` →
`Iterate<Item>` as what needs it, and the field is in the emitted `torb_witness_table` now. The mechanism is settled -
`nested[i]` is the table of the i-th direct supertrait, and a narrowing reads `value.w0->nested[i]` through
`WitnessSource.steps`, whose indices are a static property of the *trait* and not of the erased target - and what is left
is filling the list in `witnessTableOf` and walking the steps in `witnessExpression`. The two occurrences in the
repository stay the clean finding 5.6 wrote.

- **`torb ir --statistics` over the repository, first round: 854 of 2662 lowered before, 867 of 2685 after** (32% to
  32%). That step was small on purpose - what it added is an *ABI* - and the second round above is where it paid.

### What 5.10 does differently

Sections 1 to 3 and decided gap 23 are the plan; where they did not fit what the checker records, what the IR can express
or what the two back ends can *both* write, the code won and this is the list. Everything else is as written.

- **One new file, and one new pair of intrinsics.** `ir/lower/text.trb` is interpolation, the `Show` of one value, and
  `print`; `?.` went into `ir/lower/match.trb`, next to `?` and `??`, because it is the same shape of control flow over an
  `Option`. Nothing else moved.
- **An interpolated part goes through `show` and never through `showNested`.** That *is* gap 23 (`"{name}"` is the text
  itself, `["a"]` is quoted), and it is why the two members exist at all. Static where the part's type is known, through
  the table the value carries where it is trait typed - the same `memberOfType` a derived `Show` calls per field, so a
  value that is interpolated and one that is nested in another value run the same code.
- **`print` is lowered directly, as two intrinsics with a variable-length operand list.** `print(...values: Show)` is a
  variadic parameter, which is a *list* at the call site and therefore 5.7's. So `Intrinsic.PrintParts` and
  `PrintErrorParts` take the already shown parts, exactly as `TextConcat` does, and the **join stays in
  `runtime/console.c`** - the one space and the one `\n` are one place in one file, which is what 5.R1's decision was
  about. Recognizing the call is two lines (`printOperationOf` against the two symbols `std/prelude` exports), and once
  5.7 builds the list those two lines can go: the argument list is then an ordinary `List<Show>`, and these two
  instructions stay as the fast path of a call with a written argument list. Two consequences worth writing down: an
  argument of `print` is **not** boxed (the coercion to `Show` the checker recorded is deliberately not replayed -
  `lowerUncoercedValue` - because the type of the value already says which `show` runs), and `print` with no arguments is
  one instruction with no operands and writes one empty line.
- **The verifier learned that an intrinsic may answer nothing.** `producesNoValue` is the third state next to "takes a
  target" and "may take one", and the three text-part operations check that *every* operand is a `String` rather than only
  the first two - which is what `verifySameOperands` did for `TextConcat` before.
- **`void`, not `Void` and not `()`.** Stage 0 printed the *type* name and the C runtime printed the empty tuple, which
  the language does not have. Gap 23 does not mention `Void`; CONCEPT's Decision Log does ("The values of the built-in
  types are lowercase literals (`true`, `false`, `void`), never their type name"), so both sides were wrong and both are
  fixed. `torb_show_void` also takes the value now (`show(self)` passes it like any other receiver), which is what makes
  its prototype the declaration's.
- **A tuple shows as `(a, b)`, without labels - and that contradicts gap 23.** Gap 23 says "tuples as `(a, b)` with
  labels when there are any"; gap 17 made labels **not part of type identity**, so `(lowest: Int, highest: Int)` and
  `(Int, Int)` are one layout and the labels are not in it. A compiled back end therefore *cannot* write them, whatever
  the format says, and the interpreter must not either - `Show` is the contract between two implementations. The
  resolution is the one both can keep: no labels. Gap 23's sentence is superseded by gap 17 on this point, and
  `tests/language/language.expected` changed with it (`(1, 9)` where it said `(lowest: 1, highest: 9)`).
- **The panic format is the one divergence this sub-milestone did *not* close, and the binary is the right side.**
  Decided gap 9 spells it out - `panic: <message>`, then `  at src/file.trb:12:5`, exit code **101** - and that is what the
  binary does, while stage 0 prints `error: <message>` with an absolute path and leaves with 1 (and the messages of the
  checked arithmetic differ besides). Nothing about it is a question any more, so there is nothing to decide and nothing
  to fix on the C side; changing the *interpreter* touches every diagnostic of `torb test` and of the compiler's own
  output, and it is already the first entry on 5.14's list ("Unifying stage 0 and the binary is 5.14's"). `native.rs`
  therefore still knows about exactly this one difference: for a program that panics, stage 0 only has to fail.
  **Closed by 5.14** ("What 5.14 decided", below): the interpreter has three kinds of failure now, and nothing about a
  panic is exempt from the comparison any more.
- **One escape table, in three languages.** A nested `String` and `Char` are escaped the way a literal writes them:
  `\n`, `\r`, `\t`, `\\`, the quote, and `\u{h}` below `0x20` and at `0x7F`. It is `torb_escape_char` in
  `runtime/text.c` and `escaped` in `compiler/src/syntax/dump.trb`, and the two agree character by character.
- **Stage 0's float formatting is the notation of gap 4 now, not Rust's `to_string`.** It printed `NaN`, never used the
  exponent form (`1e21` came out with 21 zeros) and dropped the `.0` above `1e16`. Rust's `{:e}` already gives the
  shortest round-tripping digits, so only the *notation* is decided in `show_float` - and it is decided the way
  `torb_format_f64` decides it, against the same table of values. Two unit tests pin it: the table, and a round-trip
  property over 20 000 pseudo-random bit patterns.
- **`-0.0` lost its sign in the const evaluator.** `0.0 - value` is `+0.0` for zero; the negation of a float literal is
  `-value`.
- **A narrower integer of the same signedness may be passed to a wider runtime parameter.** There is one
  `torb_show_i64` for every signed width and one `torb_show_u64` for every unsigned one, and widening an `int8_t` at the
  call is what C does anyway. It is value preserving in exactly that direction, so `isWiderInteger` allows it one way and
  not the other.
- **`manifestOwnerOf` falls back to the implementation's target.** `Void` is `TypeForm.VoidType` and has no nominal name,
  while its members are declared on the `native type Void` of the prelude - so `"{void}"` used to look `show` up under the
  empty owner and find nothing.
- **`?.` needed one record and no closure.** The checker resolves `a?.m` to `Option.map`, or to `flatMap` where the
  member answers an `Option` of its own (gap 12), but the wrapping happens *inside* the function type it gives the member
  - so `Tables.flattenedOptionals` at the span of the member's name is the one thing that says which of the two it was.
  The lowering is then a `Tag`, a `Switch`, the member on the payload and a `Some` or a `None`. The **member itself is
  lowered by the ordinary machinery**: `Lowering.optionalReceivers` puts the payload in place of the base for exactly one
  expression and takes it out again, so a field, a method, a native and a `copy` behind a `?.` all work with no case of
  their own, and the annotation of the wrapped expression is harmless because the destination slot decides the type.
- **A member that is in none of a trait-typed value's tables is not always a dead end.** A trait the value does not carry
  may be implemented **for its trait type**: `extend<Item: Show> List<Item> with Show` of the prelude is exactly that, so
  `"{list}"` on a `List<Item>` value is a *static* call of that implementation. `dynamicDispatch` asks `witnessFor` before
  it reports, which is 116 functions of the repository and turns "`show`, which is not in the witness table" into the
  finding that names the real blocker (`iterate`, 5.7).
- **Two latent bugs that interpolation made reachable, both internal errors of the verifier.** A block whose value is
  discarded handed its destination to its last expression, so `{ errorHere "..."  bump() }` as a `match` arm put a `Token`
  in a `Void` slot (gap 18 makes that statement legal, because the call has a `var` receiver). And `verifyCall` forbade
  `last` on a place argument: `last` is about the *base* and not about the path, so `printGraph found.checker` as the last
  use of `found` releases it after the call, which is the ordinary rule for a borrowed operand.
- **The internal error 5.8 left is closed, and so is its twin.** A **field default** is lowered under the substitution of
  the *constructed type* - the mapping `lowerFieldArguments` has in hand - instead of under the caller's, which is what
  made the default of a field of a generic type of `std/http` report "the generic parameter `Item` was not substituted". A
  **parameter default** gets the callee's instance mapping for the same reason: a default is written in terms of the
  declaration's own parameters, never the caller's.
- **Two shapes of the `Show` table are *not* settled, and neither is reachable yet.** A **function value** is a clean
  finding (`a generated `show` of `Closure((Int64) -> Int64)`): CONCEPT asks for "its type" and nothing says how a type is
  spelled, while stage 0 prints `<function>` - so the spelling is a decision and not an implementation, and the finding is
  the honest state. A **`Range`** is worse, because the two sides already disagree and nothing catches it: stage 0 prints
  `0..10`, and `native type Range<Value> with Equals, Hash, Show` declares no `show`, so the manifest's
  `derivedOf("Range.show", DerivedKind.Show)` makes it structural - `Range(start: Some(0), end: Some(10), inclusive:
  false)`. It is not observable while "a range as a value" is still a finding; **5.9b will make it observable**, and the
  fix belongs in `std/core/src/range.trb` (a `show` of its own that writes `0..10`, `0..`, `0..=10`), not in a back end.
- **`describe`, the derived `Encode`/`Decode` and the `std/json` natives wait for 5.7, not for 5.10.** Measured rather
  than guessed: every member of `Encoder`, `SequenceEncoder`, `MapEncoder`, `RecordEncoder` and `Decoder` takes a
  **a `var fn` member**, which is what a witness table cannot hold until the payload box of a trait-typed value can be made
  unique (step 5 of 5.7's chain). A derived `decode` needs one thing more that **no milestone plans yet**:
  `Decoder.record<Output>` has a generic parameter of its own, so it is not object safe and can never be in a table at
  all - either the trait hands the closure a *concrete* decoder, or `decode` takes its decoder as a generic parameter
  instead of as a trait-typed value. The manifest's five `.Planned` entries (`describe`, `Json.encode`, `Json.decode`,
  `Json.parse`, `Json.value`) name 5.7 now, and the derived-member finding names what is missing instead of a milestone.
- **The top-level `?` names 5.13.** `Show` is what it was waiting for and `Show` is here; what is left is the *report* -
  `error: <the error through Show>` to stderr, exit code 1, walking `cause()`, and the `?` return trace under the chain in
  the debug profile - and that is on 5.13's row together with the profiles and the ICE report (5.3's note already put it
  there).
- **The gate is not `11-data.trb`.** Two of its eight functions were lowered on this branch, and 60 of its 93 (64%) with
  5.7's collections merged in. What blocks the rest is not text: `Email` and `User` describe themselves to an `Encoder`
  (`encoder.string`, `fields.field`) and `Json.encode`/`Json.value` drive one, which is the `var fn` chain above; its
  top-level code needs `for` over a collection, `a[key]` as a `var` argument and `a.keys().toSet().union(...)`, which is
  the rest of 5.7; and its `const user = User(..., Email.tryFrom(...)?)` is the top-level `?` of 5.13. One more thing it
  shows and nothing else does: `a == b` on two `JsonValue`s records **no resolution at all**, because a `JsonValue`
  carries a `List<JsonValue>` and a `Map<String, JsonValue>` and the standard library has no `Equals` for its collections
  - which TYPECHECKER 4.3 deliberately does not report. That is a gap of `std/`, not of the back end. The gate of this
  sub-milestone is therefore `tests/conformance/interpolation.trb` (every shape of gap 23,
  nested quoting, escapes, a multi-line string, a long text built in a loop, `print` with several arguments and with
  none), `floats.trb` (the table of `runtime/tests/text_test.c` plus what only a program can compute) and
  `optional-chain.trb` (`map`, `flatMap`, a field, a method, a chain of two, `??` behind one). Each is compiled, run,
  compared with stage 0 and asserted to leave zero live blocks - and these are the **first gate programs that print**,
  which is what makes the `Show` format a comparison between the two back ends instead of a claim.
- **`torb ir --statistics` over the repository: 1058 of 3086 lowered before, 1460 of 3317 on the same tree after, and
  8191 of 13269 once 5.7's collections and the documentation system were merged in** (34% to 44% to 61%; the total grows
  every time, because a text and a list unlock instances that were never reached at all). "String interpolation" (283)
  and "`?.`" are gone, and so is the internal error - **twice**: the one 5.8 left, and the one the merge brought
  (`boundsOfLiteral` read a literal's type without the substitution of the instance being lowered, so a `[]` inside
  `fn empty<Item>(): List<Item>` built its element descriptor for the *parameter*). The top blockers on the merged tree
  are the rest of 5.7 and 5.9b: `for` over a collection (2113), `finish` of `std/stream` (658), a range as a value (579),
  "a receiver the back end cannot reach" (566) and `a[key]` (422).

### What 6.1's long tail does differently

Milestone 6.1 is "every construct the compiler uses and the lowering does not cover yet, wherever it hurts". The rows
above are the plan; the list below is what the long tail of that row really decided, measured against
`torb ir --statistics ../compiler` - the compiler lowering its own sources, which is the fixpoint's own gate.

**A top-level `const` whose value is not static data is lowered where it is read** (35 findings: 31 reads plus 4
declarations).

- **What can *be* static data is what has an immortal C spelling:** a number, a `Bool`, a `Char`, a text, and an inline
  aggregate of those. A collection is a counted heap block, a case of a `Boxed` variant type is a counted heap block, and
  `"a" + "b"` is a call of `Add.add` - so `const knownFields: List<String> = [...]` (`documentation/schema.trb`),
  `const nullPointer = CExpression.Literal "NULL"` (`backend/c/body.trb`) and a test's source built out of pieces have no
  static value they could become, whatever an evaluator does.
- **The answer is the one the language already gives for a default.** BACKEND 1.6: a field default and a parameter default
  are lowered **at the call site**, at every call. A module `const` is the same kind of thing, so
  `lowerConstantInitializer` lowers the initializer at the read, in the module it is written in. It is sound because the
  checker's own rule (gap 27) keeps such an initializer **pure** - literals, the operators of the number types,
  interpolation, collection literals, constructor calls and other constants, and never a function call - and because
  values have no identity, so no reader can tell a rebuilt list from the one next to it. And it is what keeps the promise
  of the table in 1.6: **there is no module initialization, ever**, not even for a list.
- **A `const` that is not static data is therefore no declaration of the back end at all**, and `seedModule` counts
  neither a `lowered` nor a `stubbed` for one. That is why the total of the statistics moved.
- **The price, and what would remove it.** One build per read instead of one per program. The one place in the compiler
  where that is measurable is `punctuationTable.find(...)` in the lexer: a list of 35 tuples per punctuation token. What
  would remove it is an **immortal counted static** - a list storage in read-only data whose count is the immortal
  sentinel, so that a retain and a release of it are no-ops and a write copies (which is what 2.1 already says about a
  static) - and that is a new `StaticContents` case plus its C spelling plus the VM's. It is 6.3's measurement ("cut the
  obvious waste"), and no program needs it to be correct.
- **The foldable/not-foldable question has to be asked before `finishLayouts` has run**, which is the one subtlety.
  Representations and sizes are a fixpoint over the whole layout table and the const evaluator runs long before it - a
  layout still says `Inline size 0` there, so asking `representation == .Boxed` folded a 48-byte record into a static the
  emitter then refused ("a constant of a counted type"). `isStaticLayout` in `ir/layout.trb` asks the two halves that
  *are* final instead: every field is a type whose size no later round can change (a primitive or a text), and the packed
  size of those fields stays inside the inline limit. Everything else is lowered at its use site, which is never wrong.
- **In an entry file or a test file the checker's purity rule does not apply** (a top-level `const` is ordinary code
  there), so an initializer with a **side effect** would run once per read where stage 0 runs it once. Every one in the
  repository is a text or a list built out of literals, and a *call* is what would make it observable - which is exactly
  what no module may write. Recorded rather than hidden; the shape that closes it is the immortal static above.
- The gate is `tests/conformance/constants.trb`: a folded number, a concatenated text, a list, a `Boxed` record, a
  `const` that names another `const`, each read from a function and from the top-level code, and one read twice in one
  body. Compiled, run, compared with stage 0 byte for byte, `live blocks at exit: 0`.

**`chars()`, `bytes()`, `String.from` and `slice` are ordinary TorbScript, over two new natives** (35 findings).

- **What the language cannot express about a text is reading its raw storage.** There is no `text[i]` and no
  `length()` - "say what you count" - so the two natives are `charAt(offset)` and `byteAt(offset)`, both `.Optional`
  (`bool` plus one out parameter, the convention 5.7 built). Everything above them is TorbScript: `Characters` and
  `CharacterIterator`, `TextBytes` and `TextByteIterator` in `std/text`, the stage-plus-cursor shape every `Iterate` of
  the standard library has. Walking a text is **O(1) per character** - `charAt` decodes at an offset,
  `Char.byteLength()` says how far to move - and nothing ever re-scans what it has walked.
- **`String.from(Iterate<Char>)` cannot be a function of the runtime**, for exactly the reason `ArrayList.from` cannot
  (5.7's fourth round): it walks a trait-typed value of the *program* through a witness table, which C cannot do. The
  manifest's `.Planned` entry for it said "5.7" and was never going to come true. It is one `+` per character now, which
  is O(n²) in the bytes for a long text and what the language has: a builder would need a mutable text, and a `String`
  is a value.
- **`slice` decides what an open end means, in TorbScript, and the native takes two offsets.** `Range` says the receiver
  decides (`..10` is "from the start"), and the runtime's `torb_text_slice(text, from, to, at)` takes the two offsets it
  arrived at - so the declaration is `sliceBytes(from, to)` and `slice(range)` is three lines above it. Before this,
  `String.slice` was a native whose *declaration* took a `Range` and whose symbol took four parameters, which the
  prototype match refused: **no program that slices a text could be emitted at all**, and nothing said so until a gate
  program tried.
- **A `Char` is a conversion source.** `Int.from('A')` is the one conversion the language writes over a `Char`, and a
  `Char` is a `uint32_t` code point - so `conversionKindOf` answers `Unsigned(32)` for one where `numericKindOf` answers
  nothing, the verifier accepts it as the operand of a `Convert`, and no arithmetic intrinsic may name a kind for a
  `Char` (5.6's rule) because that one asks `numericKindOf`.
- **The source kind of a conversion is the operand's own type and not the manifest's.** `Int64` implements `From<Int8>`,
  `From<Int16>`, `From<Int32>`, `From<UInt8>`, `From<UInt16>`, `From<UInt32>` and `From<Char>`, and the manifest is keyed
  by `owner.member`: one `Int64.from` for all seven. The entry decides where the conversion *arrives*; where it starts is
  what the call hands over. That is what makes the second entry of a conversion (`Int64.fromChar`, `Float64.fromFloat32`)
  unnecessary - they are the manifest's only two dead entries now, kept as a record of the shape that was tried.
- **One record the checker did not have: a type that implements one trait several times.** `dispatchOf` resolved the
  implementation by the trait's *symbol*, which for `From` on `Int64` names seven and therefore none - so the call came
  out as `Dispatch.Direct` with the **trait's own requirement** as its member, which has no body anywhere, and the back
  end had nothing to call (`Int.from(byte)` was the finding "`from`, which the natives manifest does not know"). The
  applied **bound** is in hand at that point - every candidate of the overload set `traitMember` builds carries its own
  `From<Char>` - so `resolveBound` answers it. It is one fallback where the answer used to be `Direct`, so nothing that
  resolved before can change.
- **`torb natives --header` exists now** (3.7 named the command, 5.13 owned it): it writes
  `runtime/include/torb_natives.h` from the manifest, and `compiler/tests/natives.test.trb` asserts that the file on disk
  is what the manifest renders - the byte-for-byte half that needed file IO from a test.
- **The instance-count tripwire moved from 78/108/24/2 to 121/169/38/3** declarations/functions/tables/descriptors. Two
  files that walk a text now reach `Characters`, `CharacterIterator`, their `Iterate<Char>` and `Iterator<Char>` tables
  and the payload boxes of both - one set for the whole program, not one per call - which is the same trade 5.7 made when
  `ArrayList.from` and `Range.iterate` became TorbScript. Its comment in `lower.test.trb` says so.
- The gate is `tests/conformance/characters.trb`: every width of UTF-8 (one to four bytes) so that a cursor moving
  by the wrong amount is a wrong character and not a slower loop, a cursor over a **slice** (which may not read the
  storage in front of it), `charAt` past the end, `String.from`, and `Int.from(byte)`. Compiled, run, compared with stage
  0 byte for byte, `live blocks at exit: 0`.
- **One divergence this found and did not close** (5.14 did): `Char.toUpperCase` of a non-ASCII letter. Stage 0 answers Rust's full
  Unicode mapping ('ä' to 'Ä'), the runtime's is ASCII only, and `runtime/README.md` promises "ASCII plus the letters of
  Latin-1" for the *text* functions. It is one more entry on 5.14's list of "unify stage 0 and the binary", and the gate
  program upper-cases an ASCII word so that it tests the cursor and not the table.

**A list pattern is three members of the prelude and nothing of its own** (6 findings, and the compiler's own command
line).

- **`length()` for every test, `at(index)` for every item, `skip(index).toList()` for a rest.** `[]` is `length == 0`,
  `[first, ...rest]` is `length >= 1`, `[..., last]` reads `at(length() - 1 - index)`. Nothing about a list pattern is a
  new mechanism: an item is **no `PathStep`**, because a list has no layout at all - a `List<Item>` value is a trait-typed
  one and reading from it is a call through its witness table, exactly as `a[key]` is. `Indexed.at` is the member, so
  what an index out of range does is decided once in `std/core` and is the same everywhere; here none is ever out of
  range, because the length was tested on the way into the block.
- **A rest is a copy, and it cannot be anything else.** `Slice.slice` answers `Self`, so object safety keeps it out of the
  witness table of a trait-typed value (5.6's rule) - there is no way to *share* the storage from a pattern. `skip` and
  `toList` are ordinary members of `Iterate` and give a list of their own; a value has no identity, so only the cost is
  observable, and it is the cost `toList()` has anywhere.
- **Which trait declares the member is not the back end's business.** `boundDeclaring` walks the closure of the subject's
  own traits and takes the first that declares `length`, `at`, `skip` or `toList`. `length` is `Length`'s in
  `std/iteration` and the `List` trait's own in the miniature prelude of the compiler's tests, and a program may declare a
  collection of its own - one walk answers all three, and a subject that declares none of them is a clean finding that
  names the member.
- **A list *inside* another pattern (`.Wrapped([first])`) is a clean finding.** The container's checker type is what the
  bound is computed from, and a `MatchPlan` carries the type of the **subject** only - nothing says what a value halfway
  down a path is, and for a list there is no layout to ask either. There is no such pattern in the repository.
- The gate is `tests/conformance/list-patterns.trb` (every shape, a rest that is walked, and a recursion over one),
  compiled, run, compared with stage 0 byte for byte, `live blocks at exit: 0`. With this, `compiler/src/main.trb` lowers
  as far as the **top-level `?`** of 5.13, which is what its command line waited for.

**The natives the compiler's own driver needs** (7 findings, and the whole of `std/fs` behind them).

- **`.Fallible` got the shape it always needed: `bool`, one out for the payload, one more per field of the error the
  parameters do not name.** `File.readText`, `writeText`, `absolutePath`, `list` and `createDirectory` all answer a
  `Result<_, IoError>`, and an `IoError` is `(path, message)`: the `path` *is* the parameter of that name (the rule 5.7
  wrote), and the `message` is something only the operating system knows - so the runtime writes it through one more out
  parameter. A payload of **`Void`** takes no out at all (`writeText`), which is why `RuntimeShape.out` became
  `RuntimeShape.outs`. Before this, every one of those five was a clean finding of the *emitter* ("the runtime answers
  `bool` where the lowering expects `Result…`"), so **no program that read a file could be emitted** - and nothing said so
  until a gate program tried, because `torb ir` never looks at a prototype.
- **`Process.run` is TorbScript over one native.** `ProcessOutput` is a record of the program with three fields and the
  convention writes one payload; so `runCollecting(command, arguments, var output, var failure): Int` is the native - the
  exit code, or `-1` with the reason - and `run` builds the `ProcessOutput` and the `IoError` around it. Two things fell
  out of it: the runtime collects **both output streams into one** (`popen` has one pipe; `standardError` is empty until
  `Process.start` brings three, 7.3), and the arguments cross the boundary as the **concrete** `ArrayList<String>`,
  because a trait-typed `List<String>` says nothing about which implementation carries the buffer.
- **`Clock.milliseconds` is a runtime function of its own**, counted from the first reading of the process: an `Instant`
  and a `Duration` would be the same number twice plus a subtraction, and `torb check --timings` reads it per pass.
- **`mkdir -p` is the platform layer's**, like every other path operation (`runtime/platform.c`, nine functions now), and
  "it is already there" is success - a caller that only wants a place to write should not have to ask first.
- **What is still a finding, with the recipe:** a native whose result or argument is a **collection the runtime owns but
  the declaration names as the trait type** - `File.list` and `String.split`, which is what `text.lines()` is. The
  runtime answers a `torb_list` and the declared type is `Object([List<String>])`, so the wrapper has to build the
  concrete `ArrayList<String>` (which is the default implementation `lowerListLiteral` already asks the prelude for) and
  coerce it with `TraitValue` and the table of `(ArrayList<String>, List<String>)`. It is the same two steps for an
  argument, the other way round. That is the next step of this row, and `text.lines()` is on the compiler's own critical
  path, so it is the first thing to do after the driver.
- The gate is `tests/conformance/files.trb`: a directory that is created twice, a file written and read, `exists`,
  `isDirectory`, `absolutePath`, the environment, the clock, and a **child process** that really runs. Compiled, run,
  compared with stage 0 byte for byte, `live blocks at exit: 0`. The C side is `runtime/tests/process_test.c` (four tests,
  new) plus two more in `file_test.c` and one in `clock_test.c`: 91 runtime tests now.

**The top-level `?` of 5.13: `error: <the error>` and exit code 1** (one finding, and it was the compiler's own entry
function).

- **The report is one line, because the error of an entry file is a concrete type.** Walking `cause()` needs the error as
  a trait-typed `Error` value; what an entry file has in hand is its own error type, so there is nothing to walk - which
  is what 5.3's note already said about it. A `?` whose error *is* the trait value gets the chain, and that is one loop
  over `cause()` on top of this block (`Error` is object safe: `cause(self)` answers an `Error?` and mentions `Self`
  nowhere). `Show` is what the line goes through, which is the same member an interpolation calls.
- **Ending the program is an intrinsic, `IntrinsicOperation.ExitWithCode`.** Not a call of `Process.exit`: an entry file
  that never imports `std/process` has no name for that function, while what happens here *is* the language's own
  behaviour (decided gap 9), so both back ends read it out of the IR. The C emitter renders it as `torb_process_exit`,
  which flushes, reports the live blocks and leaves without running anything else; the block that holds it ends
  `unreachable`, exactly as a `Panic` does.
- **The `?` return trace of the debug profile stays planned**, and is named here so nobody looks for it: it is a per-task
  ring buffer of locations that the report prints under the chain, it needs the profiles the driver does not have yet, and
  it changes no error type - so it belongs in exactly this block later.
- **`?` on an `Option` at the top level reports the prefix alone** (`error: `), because a `None` carries nothing to show.
- The gate is `tests/conformance/top-level-error.trb`, whose error is a type of the *program* and not an `IoError`:
  the message of an operating system is localized, and what is under test is the report and not `strerror`. Compiled, run,
  stdout and stderr and the exit code compared; `live blocks at exit: 0` checked by hand, because `native.rs` skips the
  leak gate for a program with a `.stderr` file (a panic leaves nothing to count, and this one is not a panic).

### What the last mile of 6.1 decided

**`torb build ../compiler` produces a binary, and that binary checks the whole repository with the answer stage 1 gives**
- `266 files, no problems`, `168046 of 168046 expressions typed (100%), 0 deferred`, and the same `ir --statistics` to the
byte, in 6.8 seconds where the interpreter takes minutes. This is the list of what was between the collection core and
that binary, and of what the round found out on the way.

**A native whose result or argument is a collection the runtime owns but the declaration names as the trait type.**

- `String.split` (which is what `text.lines()` is), `File.list` and `Process.arguments` all answer a `torb_list` while the
  declaration says `List<String>`, a trait-typed value. A function of `runtime/` cannot build one - that is a boxed payload
  plus one witness pointer per bound, both layouts of the *program* - so the generated wrapper takes the storage and
  coerces it with `TraitValue` and the table of `(ArrayList<String>, List<String>)`, which is the second half of what
  `lowerListLiteral` does with a container it filled itself.
- **The manifest says which entries these are (`coercedCollection`), and the flag cannot be derived.** A collection also
  crosses the boundary as an **element** of another container, and an element is opaque (`const void *value`, `void *out`)
  whatever it holds - so `ArrayList.add` of a `List<Int>` needs no coercion where `String.split` does, and nothing but the
  runtime's own prototype tells the two apart. Deriving it instead produced 24 findings on the compiler about code that
  was already correct.
- **The argument direction is not built, and nothing `.Ready` needs it.** `Process.run` already declares the concrete
  `ArrayList<String>` its symbol takes, and the two that would (`Process.start`, `Task.all`) are `.Planned` for 7.3.
  Reading the storage back *out* of a trait-typed value is a step into its boxed payload, which is no `PathStep` the
  emitter has - the first hop is a `.data` member and not a dereference.
- The gate is `tests/conformance/collection-natives.trb`.

**A case constructor whose field takes its default.** A case is a function from its fields to the type it belongs to, and
what declares it is a `case` and not a `fn` - so looking for a `FunctionDeclaration` answered "no defaults at all" and a
field that took its own became "a parameter without an argument and without a default" (ten of them in the compiler, all
`CStatement.Declaration` of the C writer). The collection core arrived at the same answer independently; this branch's name
for it gave way to master's. Gate: `case-defaults.trb`.

**`Equals` and `Hash` for every collection, and `!=` that negates.**

- A collection is `Equals` (and `Hash`) when its items are, and values compare structurally, as a tuple does. `List`, `Map`
  and `Set` already said so; `Stack`, `Queue` and `Array` say it now - order-dependent like a `List`, because each of the
  three iterates in one order and that order is all a reader of one can see. `ArrayList`, `ArrayStack` and `ArrayQueue`
  need nothing of their own: a `type` generates both and a `native type` gets them derived from its layout.
- What blocked `==` between two collections was the back end: an operator has no callee expression, so the subject was
  known only for a `Dispatch.Implementation`, and `==` on a trait-typed value is `Dispatch.Object`. The left operand's type
  goes along now, and the rest was already there - `equals(self, other: Self)` is not object safe, so the implementation of
  the trait *for the trait-typed type* is a static call, the shape `"{list}"` already went through.
- **`!=` was lowered as `==`** for every type whose equality is a *call* - a record, a case, a collection - because the
  member is `Equals.equals` either way and the negation was dropped. `records.trb`, `derived.trb` and `collections.trb` all
  compared with `==` only, which is why three rounds of gate programs missed it.
- **What a gate program cannot cover, and why:** a `Stack`, a `Queue` and an `Array` are types **stage 0 does not have at
  all** (its world is `builtin_type` plus `prelude.trb`), so no program can compare the two back ends on one; and a `Set`
  or a `Map` compares by walking itself. Gate: `collection-equality.trb`.

**A generic declaration is no declaration of the back end at all.** Monomorphization gives it one instance per argument
list, each counted where the worklist reaches it, and none where nothing calls it - so asking for its instance *without*
arguments could only fail, and counting that said "cannot build an instance of" about code the back end builds a dozen of.
The same answer 6.1's long tail gave for a `const` whose value is no static data. 260 of them in the repository.

**A `project.trb` is not program code.** It is a manifest, read *statically* by `compiler/src/project/`, so neither
`torb ir` nor `torb build` lowers one. The finding about the property command its receiver applies is gone because **the
file is not part of the program**, not because anything is hidden. Every other receiver script stays: a file a
`Sandbox.load` names is code the program runs, and 7.4 compiles it like any other body.

**A planned `native type` and a `Task` result abandon the body with a clean finding.** This is what took the internal
errors of the whole repository to **zero**.

- A `native type` whose representation a later milestone writes has **none at all**: `nominalType` answers `VoidType` for
  one so that a layout it sits in can still be built, and a *value* of it is then a slot nothing ever defines.
  `Clock.now()` answers an `Instant`, the call writes no target because its result looks like `Void`, and the expression
  around it read a slot that was never assigned - two internal errors of the verifier in the tour, about a program that is
  correct. `Instantiation` records which closed types **are or contain** one and `typeAtSpan` abandons there.
- A function that answers a `Task<Value>` has a body that produces the `Value`, so the two disagree by design until 7.3's
  state machine puts them back together. `Iterating.next` of `std/stream` was the third internal error.

**Two natives whose shape the emitter refused, and the float remainder.**

- `Char.tryFrom`, `Int32.tryFrom` and `Int64.tryFrom` answer a `Result<_, NumberRangeError>` whose `message` no parameter
  names - what went out of range is something only the value knows - so the runtime writes it through one more out
  parameter, which is the `.Fallible` convention the `message` of an `IoError` already uses. The whole `*_checked` family
  of `torb_number.h` takes it, so the family stays one shape.
- **`%` on a float is `torb_remainder_f64`/`_f32`,** `fmod`, and it is emitted whether or not a program writes one: every
  `Float64` witness table carries `Numeric.remainder`. A remainder by zero answers `nan` and never panics, exactly as
  `a / 0.0` answers an infinity - only the integers panic (decided gap 2).

**An element descriptor's `equals`/`hash` thunk cast the wrong `const` away.** `const {spelling} *` is a pointer to a const
`spelling`, and for a **boxed** item `spelling` is already `T *` - so the deref handed a `const T *` to a callee that takes
a `T *` and gcc refused it under `-Werror`. What a table may not write is the **storage**, and for a boxed item the storage
holds a pointer, so the const belongs to that pointer: `{spelling} const *`.

**A copy of a boxed value saw a change made through the other one.** The one bug that made the first binary misparse
itself, and the most important sentence of this round.

- The lowering forms the path of a `var fn` member of a trait-typed value **before the representations are decided** - a
  representation is a fixpoint over the whole layout table that `finishProgram` settles - so `makeOwnersUnique` asked "is
  this record counted" of a layout that still said `inline size 0` and left the box out of the `MakeUnique` chain. It was
  the same trap `isStaticLayout` describes one section above, in a second place.
- **The question has to be over-approximated where it cannot be decided.** Every record, variant and tuple on a path is a
  possible owner now, and `dropUncountedMakeUnique` removes the ones that turned out inline after `finishProgram` - so the
  verifier's rule ("a `makeUnique` needs a place that is counted") stays exact and an IR snapshot stays free of noise.
  Asking the other way round loses the one that matters: a missing `MakeUnique` is a write a copy can see, and that is not
  a slower program but a wrong one.
- **How it showed.** `parseClosureParameters` backtracks by keeping a copy of the whole `Parser` and assigning it back, and
  a `Parser` is a boxed record: the trial's own diagnostic went into the box the copy shared, so the restore restored the
  error with everything else and `names.filter({ !_.startsWith("-") })` came out as "Expected a pattern, found `!`" from a
  binary that had just compiled itself. **A compiler is the gate program that finds this class of bug**, because almost
  nothing else copies a big value and throws the copy away. Gate: `boxed-copy.trb`.

**The closed world of a closed-world question is the program, never the tree.**

- "Does any implementation override this default" decides the member list of every witness table (5.7's fourth round), and
  it was asked over every implementation the *checker* indexed - which is every file the `SourceTree` read, and
  `readSourceTree` reads the whole workspace whatever is being built. Adding `examples/encoding-lab` to the repository took
  the instance count of `source.trb`'s program from **121 declarations to 192** without a line of that program changing.
- **Decided: a package next to yours in a workspace must not change your binary.** The world is the packages of the roots,
  everything they depend on transitively, and the prelude each of them names (`programPackagesOf`). The unit is the
  **package** and not the module, because that is the unit of coherence (gap 29) and of what a project declares it depends
  on: an implementation is visible to a program exactly where its package is.
- The tripwire is **115 declarations / 163 functions / 38 tables / 3 descriptors**. The six declarations below 121 are the
  other examples of the repository, which were inflating it by the same fault before the lab made it obvious.
- **Everything else that could have the fault does not have it.** `Checker.derived`, the element descriptors and the
  instance memo are all demand-driven - they hold what something asked for, not what a tree contains - and the depth
  backstop is per instance. The two closed-world tests of `lower.test.trb` lower one program twice, once alone and once
  beside a package that overrides a default of the miniature prelude, and assert the IR text is identical; they fail when
  the restriction is taken out.

**What the VM of 7.x has to know from this round.**

- A native whose result is a collection reaches a **wrapper of the program**, not the runtime symbol: the VM implements
  `torb_text_split` and friends as answering a list storage, and the coercion is already in the IR.
- `MakeUnique` on a place whose storage is not counted **cannot** reach a back end: it is removed after `finishProgram`.
  A back end may assume every `MakeUnique` it sees names counted storage.
- The member list of a witness table is a function of the **program's packages**, not of the workspace. A VM that loads a
  module at runtime (7.4's sandbox) has to build that module's tables against the same world the host was built with, or a
  table index means two things.
- The `Ordering` of a tuple is a **generated function of the program**, lexicographic over each field's own `Compare` - not
  an opcode, and not a comparison the VM may shortcut.

**One divergence recorded for 5.14, not fixed:** a tuple has a `compare` **member** on stage 0 and none in the checker
(which gives a tuple the operators and nothing else), so `pair.compare(other)` runs on stage 0 and is a type error in the
front end. `==` on a tuple records no resolution at all for the same reason, which is why `lowerOperator` asks
`dispatchedOn` for one.

### What 6.2 decided: the fixpoint

**`main.exe build ../compiler` writes the same `program.c` stage 1 wrote, to the byte, and the binary built from that
does it again.** Milestone 6.2 is one bug wide, and the bug was not in the back end at all.

**Joining a text is a merge tree, and a `String` is a value.** This is the whole hang.

- `Iterate.joined` folded `result + separator + item.show()` over its pieces. A `String` is a value, so every step copies
  everything that is already in the accumulator, and `n` pieces copy `O(n²)` bytes. `emittedText` is one `joined` over the
  lines of the translation unit, and the compiler's own is **786457 lines and 66 megabytes**: measured on a probe that
  joins 60000 pieces into 1.4 MB (20.6 seconds natively), that extrapolates to **over three hours** of copying before one
  byte reaches the file. "Ten minutes on one core without a line of C" was the first ten minutes of it.
- **Stage 0 never ran a line of the fold.** `joined`, `String.from` and the whole `Iterate` surface are natives of the
  interpreter (`natives.rs`), and Rust's `parts.join(separator)` walks the bytes once - so the square was paid by the
  emitted C alone and nothing in the repository could see it. That is the shape of the class: *not* a value that shares
  storage where it must not, and *not* a fixpoint that fails to settle, but a **library algorithm whose cost only the
  compiled program pays, because stage 0 answers the same call with Rust**. Every `native` entry of `natives.rs` that has
  a TorbScript body in `std/` is a place where this can happen again.
- `concatenated(pieces, separator)` in `std/iteration/src/concatenate.trb` merges **neighbours pairwise**: every byte is
  copied once per level of the merge tree and there are `log n` levels. 20.6 seconds became **0.068**, and stage 0 takes
  0.065 for the same probe. What comes out is the same text either way - a tree over `n` leaves has `n - 1` forks and each
  fork writes one separator.
- It is a module of its own because `collectors.trb` needs it too and `iteration.trb` already imports that one; and it is
  in `std/iteration` and not in `std/text` because `std/text` depends on that package and not the other way round.
- The same shape in two more places, both fixed with it: `String.from(Iterate<Char>)` appended one character at a time
  (also a native of stage 0, so also invisible), and the `joining` collector accumulated a `String?`.
- **A leading empty piece used to swallow its separator.** `result.isEmpty()` cannot tell "nothing yet" from "the empty
  text", so `["", "b"].joined(separator: ",")` came out as `b` where stage 0 answers `,b`. A list of pieces has no such
  ambiguity, so the bug went with the fold. It is what makes the gate program fail before the fix.
- Gate: `tests/conformance/joined.trb` - every count of pieces (the even/odd carry of the tree), an empty piece in
  every position, a multi-byte separator, non-`String` items, a lazy pipeline, `String.from`, and 40000 pieces so that a
  regression to the square shows in the time the file takes. Byte-equal with stage 0, `live blocks at exit: 0`.
  `concatenated` and `joining` have no program of their own: **stage 0 does not load `std/` at all** (its world is
  `natives.rs` plus the embedded `prelude.trb`), so a program that names either of them has nothing to be compared
  against - which is a limit of every gate program and worth knowing before writing one.

**A comparison the runtime owns answers a sign, not an `Ordering`.** `Float64.compare` is a `.Runtime` entry, because a
total order with `nan` above everything (decided gap 5) is no comparison intrinsic - and `torb_compare_f64` answers an
`int32_t` where the declaration answers `Ordering`, which the emitter refused as a prototype that does not match. So no
program with a `Float64` in a tuple could be built, and `torb ir` never noticed because it never looks at a prototype.
`NativeResult.Ordering` is the fifth shape and costs no new machinery in the signature: a sign in the place of the result
is the same override at position -1 that a coerced collection already uses for its storage. The body is built by
`orderingOutOfSign`, which the generated `compare` of a `String` uses over the sign of `torb_text_compare`, so which case
a negative sign stands for is decided once for both back ends. It covers `Instant.compare` and `Duration.compare` too.
The `Float64` field is back in `tests/conformance/tuple-compare.trb`.

**Everything else about the emitter already agreed.** Before the fix was written, all 41 gate programs of
`tests/conformance/` were emitted by stage 1 and by stage 2 and compared byte for byte: **41 of 41 identical**. So
there was no iteration order, no uninitialized slot, no address-dependent hash and no float formatting to find - the
emitter was already a pure function of the program, and the one thing between 6.1 and the same `program.c` was the cost
of `joined`.

**The second divergence: `Process.run` went through a shell, and the binary therefore found no C compiler.** Stage 2 wrote
the C and then reported `no C compiler found` where stage 1 found gcc on the same PATH - so there was no stage 3.

- `_popen` runs `cmd.exe /c <command line>`, and `cmd` re-parses the quotes by a rule that depends on where the first
  quote stands and on whether what precedes it names an executable file (`cmd /?`). `"gcc" "--version"` arrives as one
  command **named** `gcc" "--version`. Measured with a C probe: neither an extra outer pair of quotes, nor `cmd /s /c`,
  nor the redirect in front of the command works for both that and a nested `cmd /c "echo torb"`, which is what
  `Process.run("cmd", ["/c", ...])` is. **There is no quoting that survives both**, so the shell had to go.
- `runtime/platform.c` runs a child through `CreateProcess` plus one pipe on Windows now, with the arguments quoted by
  the `CommandLineToArgvW` rule the child's own C runtime undoes. Two promises become true with it, and the interpreter
  kept both already: "there is no shell" (`std/process`), and **a program that cannot be started at all is a failure**
  rather than `cmd`'s own exit code 1.
- POSIX keeps `popen`, with every argument in single quotes. `fork` plus `execvp` is what would make that half shell free
  as well, and it is `Process.start`'s job at 7.3; the difference is recorded in `runtime/README.md`.
- **Why no round before this one noticed:** the only gate that runs a child process ran `cmd /c "echo torb"`, and a
  command with **two** quoted arguments happened to survive the rule that a command with one does not. Both shapes are in
  `runtime/tests/process_test.c` and in `tests/conformance/files.trb` now.

**The fixpoint gate is `tools/bootstrap.sh`,** which takes minutes and needs a C compiler:

```text
sh tools/bootstrap.sh
```

It builds the compiler with the seed, builds it again with the binary that came out, and compares the two `program.c`
byte for byte; a seed older than the code generation it built gets a third step, so that two builds of the *same*
compiler are what is compared. A difference is reported as the **first differing byte with the text around it in both
files**, because "the files differ" says nothing about 66 megabytes.

**What the two stages cost.** One machine (16 cores, gcc 13.2, `-O2`), the whole repository or the compiler's own
package, wall time and peak working set of the one process:

| What | Stage 1 (the interpreter) | Stage 2 (the binary) | Factor |
|---|---|---|---|
| `check ..` - 280 files, 178832 expressions | 47.2 s, 726 MB | **7.2 s, 216 MB** | 6.6x |
| `ir --statistics ../compiler` - lower, own, verify | 131.0 s, 1054 MB | **15.0 s, 322 MB** | 8.7x |
| `build ../compiler`, up to the written `program.c` | 158 s, 1409 MB | **20.7 s, 403 MB** | 7.6x |
| the gcc that follows it, on one 65715134-byte file | 95 s | 96 s | 1.0x |
| `build ../compiler` in full | 253.1 s | **116.7 s** | 2.2x |
| `test ../compiler/tests` - 1453 tests, 55 files | 246.9 s | not yet (5.11's second half) | - |

The three build rows are one run of the fixpoint test, so they add up; the memory figures come from a run measured on its
own, because peak working set is the one number a second process in the same run would confuse.

- **The C compiler is the build now**, not the compiler: 96 of stage 2's 116.7 seconds are one gcc on one translation
  unit, and that number is the same for both stages because it is the same C. It is what 6.3's "shard the translation unit
  if it pays" is about, and the only number in the table a faster emitter cannot move.
- **Memory is the same story as time**: the binary needs 403 MB where the interpreter needs 1409 MB for the same work, and
  the shape is the same in all three rows - roughly a third. Nothing here is close to a limit.
- `test ../compiler/tests` runs one process per file, so the peak of the runner says nothing; stage 2 cannot run the
  *suite* until 5.11's second half builds the one binary over all 55 files. Every one of them lowers since 5.11's first
  half, and a single file compiled on its own runs natively already.
- Both stages still answer `check --statistics ..` and `ir --statistics ../compiler` **byte for byte identically**, which
  is what says the speed costs no agreement.

**What the VM of 7.x has to know from this round.**

- **A cost that stage 0 hides is a cost only the back ends pay.** Whatever `natives.rs` answers with Rust while `std/`
  carries a TorbScript body for the same declaration is invisible to every test that runs on stage 0. The VM pays the same
  bill the C back end pays, so a VM that runs `compiler/` will meet exactly the places this round found - and the way to
  find them is a probe compiled by the back end, never a script on stage 0.
- `concatenated` is ordinary TorbScript over `String + String` and needs **no** intrinsic and no new opcode. A VM that
  wants joining to be cheaper implements `Add.add` on a text well; nothing above it has to change.
- **`Process.run` must not go through a shell.** The VM implements `torb_process_run_collecting` like the C runtime does:
  an argument list, never a command line a shell re-parses. A program that cannot be started is `-1` plus a reason.

**What is left, in the order it will bite.**

1. **`torb test` from one native binary, 5.11's second half.** The findings are gone - `assert` is lowered and
   `test`/`group` are functions of the runtime, so a test file compiled on its own already runs natively and prints what
   stage 0 prints. What is missing is the driver: a generated entry that calls all 55 modules in sorted path order,
   `emitProgram` taking more than one entry name, and the counts of the summary line next to `test` in `runtime/`. That
   is the one row of the table above that stage 2 still cannot fill. An `Expression<Value>` as a *value* is the other
   half and blocks nothing: nothing of `compiler/src/` and nothing of `compiler/tests/` uses one.
2. **Reading a collection back out of a trait-typed value** for a native argument, which `Process.start` and `Task.all`
   will want at 7.3: the first hop into a boxed payload is a `.data` member and no `PathStep` the emitter has.
3. **Shard the translation unit, or do not** - 6.3's question, and the table above says it is the only one worth asking
   about the time a build takes: 96 of stage 2's 117 seconds are one gcc on one 65-megabyte file.
4. **Look for more of what `joined` was.** Every `native` of `natives.rs` that has a TorbScript body in `std/` is a cost
   the interpreter hides; and the two the back end already knows about are one build per read of a module `const` (the
   immortal counted static above) and the instance count.

**Two more entries for 5.14's list of differences between stage 0 and the binary**, both found by a gate program of this
round and neither fixed (the first one is closed by 5.14, below; the second waits for 7.3):

- **Stage 0 refuses to compare a `nan` at all** ("the Float NaN and the Float 1.5 cannot be compared"), where the language
  says `nan` is above everything (decided gap 5) and `torb_compare_f64` implements that. So no program can compare the two
  back ends on one, which is why `tuple-compare.trb` has no `nan` in it.
- **A program that cannot be started at all** is an `IoError` in the interpreter and on Windows, and the shell's own exit
  code on POSIX, where `Process.run` still goes through `popen`.

### What 6.3 measured: where the 65 megabytes are

**The build is the C compiler now** (6.2's table: 96 of stage 2's 117 seconds), so the question of this round is not how
fast the compiler is but how much C it writes. Everything below is measured on one machine (16 cores, gcc 13.2 UCRT,
Windows, `-O2`) over the translation unit the compiler emits for itself, and a number taken while something else was
building says so.

**The flags stay as they are, and the translation unit is not sharded.** Measured on the 65 MB file, with the whole
runtime linked in, in one quiet window (the numbers marked *loaded* were taken while another build ran and are only good
as ratios inside their own row):

| what gcc is called with | wall | the binary | `check ..` with it |
|---|---|---|---|
| `-std=c11 -O2 -g0 -Wall -Wextra -Werror`, what the driver does | **98.5 s** | 11296792 | **7.2 s** |
| `-O1` instead | 70.4 s (-29%) | 11678074 | 7.9 s (+3-8%) |
| `-O0` instead | 51.0 s (-48%) | 18652623 | 18.0 s (2.4x slower) |
| `-O2 -pipe` | 109.6 s (+11%) | unchanged | - |
| `-O2` without `-Wall -Wextra -Werror` | 101.5 s (+3%, noise) | unchanged | - |
| `-c -O2`, no link *(loaded)* | 121.9 s | - | - |
| `-fsyntax-only -O2` *(loaded)* | **7.1 s** | - | - |
| 8 shards, `-c -O2`, one at a time *(loaded)* | 112.5 s | - | - |
| **8 shards at once** plus the link *(loaded)* | **20.7 + 2.0 s** | 15620695 | **16.5 s (2.3x slower)** |

- **The C compiler's front end is free and the per-function back end is everything**: `-ftime-report` at `-O2` puts 3% of
  the CPU in parsing and 97% in "opt and generate", 83% of it in the callgraph's function expansion. That is why
  `-fsyntax-only` is 7 seconds and a full compile is a hundred: sharding can win almost linearly, and the floor is low.
- **And that is exactly why sharding is not worth it here.** A real 8-way split (every body round robin, a shared prelude
  of everything else plus a prototype for all 35841 bodies, `static` dropped) compiles in 22.7 seconds instead of 98.5 -
  **4.3x** - and no extra CPU in total. But **the binary that comes out is 2.28x slower** (`check ..` 16.5 s against 7.2 s,
  as slow as `-O0`), because seven of eight call edges then cross a translation unit and nothing inlines across one. For a
  compiler that is built in order to *run* the next stage, that trade is a loss. `-flto=8` would keep the whole-program
  view and parallelize the codegen, and it does not work with this toolchain at all: the assembler refuses the object
  (`too many sections (42392)`) and with `-Wa,-mbig-obj` the bundled `ld` has no LTO plugin.
- So: **sharding needs a partition that keeps the hot call graphs inside one shard**, and until someone has one, the way
  to make the build shorter is to emit **less C** - which is what the rest of this section is about. `-pipe` is a loss on
  Windows and the warnings cost nothing; `-O1` is the flag for a *debug* profile when 5.13 has profiles (-29% build for
  3-8% of the compiler's own speed, and it trips no new warning), and `-O0` is not: it buys 48% of the build for a
  compiler that is 2.4x slower.

**What the 65717562 bytes are.** Parsed with the grammar of `backend/c/writer.trb`, so the parts add up to the file:

| part of `program.c` | count | bytes | share |
|---|---|---|---|
| function definitions | 35841 | 43913582 | 66.8% |
| prototypes, one line each | 35388 | 9073508 | 13.8% |
| static data | 14574 | 9019751 | 13.7% |
| the comment over each definition | 13654 | 1908313 | 2.9% |
| `struct` definitions | 3746 | 910857 | 1.4% |
| forward `typedef`s | 3746 | 851909 | 1.3% |
| *of the definitions:* `#line` directives | 73259 | 3835425 | 5.8% |

| kind of function | count | bytes | share | median | max |
|---|---|---|---|---|---|
| a monomorphic function of the compiler | 2627 | 15764340 | 24.0% | 4068 | 91616 |
| an instance of a generic | 7762 | 11876129 | 18.1% | 1332 | 9055 |
| **a witness thunk** | **16510** | **11636722** | **17.7%** | 689 | 1622 |
| drop glue `D_` | 2729 | 1333538 | 2.0% | 479 | 4540 |
| a native wrapper | 1541 | 1099275 | 1.7% | 531 | 2708 |
| retain glue `R_` | 2314 | 1093539 | 1.7% | 460 | 2705 |
| a generated member (derived, or a wrapper around an intrinsic) | 1542 | 742463 | 1.1% | 267 | 30375 |
| closure bodies and closure thunks | 361 | 202765 | 0.3% | 517 | 5445 |
| element helpers `dR_`/`dD_`/`dE_`/`dH_` | 452 | 116046 | 0.2% | 232 | 617 |

The top three are the compiler's own bodies, the instances of the generics and the thunks. **None of the twenty heaviest
definitions is a duplicate**: eighteen are one-of-a-kind bodies of the compiler (`newChecker` is 91616 bytes, a `match`
over `Instruction` in `verifyInstruction` 81977), so the waste is not in the head of the distribution but in the tail -
14794 of 35841 definitions (41%) have a body that is byte-identical to another one's once the function's own name is
substituted away.

**A witness thunk is a function of what it does and not of the table it sits in, and that is fixed.** A thunk casts
`void *self` to the payload layout of the target and calls one function of the program; nothing else is in it. So
`Accumulator.add`, `Collection.add` and `List.add` of one list type were **three copies of one function**, because
`thunkNameOf` keyed a thunk on *(table, member index)*. It is keyed on *(the member's function, the payload layout)* now
and named `W` plus the member's own name (`W_acme_x2f_app_main_Small_size`), and one thunk is emitted per distinct name
however many tables point at it. 16510 thunks become 8221, and the shorter names shrink the `w_..._members` arrays as
well:

| | before | after | |
|---|---|---|---|
| `program.c` of the compiler | 65717562 bytes | **56994210 bytes** | -13.3% |
| lines | 787016 | 725997 | -7.8% |
| thunk definitions | 16510 | 8221 | -50% |
| gcc `-O2`, the same machine in the same load window | 111.8 s | **87.1 s** | -22% |

A name that two keys would share is a **collision and not a merge**: the keys of a base name are sorted and numbered, so
which one keeps the bare name is a function of the program and not of the order the tables were built in. The gate is the
one every emitter change has - every program of `tests/conformance/` byte-equal with stage 0, zero live blocks, and
the fixpoint, which **holds on the smaller file**: stage 1 and stage 2 agree on 56994748 bytes and stage 3 emits them
again. Stage 2 builds the compiler in 112 s on the machine these numbers come from.

**What is measured and not done, in the order of what it is worth.**

1. **The mangled names are two thirds of the file.** 419044 occurrences of a mangled identifier, 43991922 bytes, mean
   length 105; 53707 distinct names of mean length 139. `program.c` is *one* translation unit whose only external symbols
   are `main` and what `torb.h` declares, so every `t_`/`T_`/`w_`/`d_`/`s_`/`n_`/`W_`/`F_`/`R_`/`D_` name could be a short
   opaque symbol with the readable name in the comment above it: **41058614 bytes, 62.5% of the file**. Two cheap halves of
   it: the `_x2f_` escape for a `/` is 1379431 occurrences (6897155 bytes, 10.5%), and the literal prefix
   `torbscript_x2f_compiler_` is 453545 occurrences (10885080 bytes, 16.6%). It costs nothing semantically and it is the
   biggest lever there is. What it costs is every `torb ir` snapshot and every pinned C in the tests, so it is its own
   round.
2. **1762 instances of six collection defaults never mention their element type** (`Length.isEmpty`,
   `Collection.finish`, `ArrayList.clear/compact/length/reverse`, `TrieMap.clear/length`): 250 copies each of
   `torb_list_length(self)`. One instance per *erased container* rather than per element type is 812599 bytes (1.2%) and
   sound by the same argument the thunks are.
3. **The `R_`/`D_` glue keyed on the layout's *shape*** - size, alignment and the offsets and runtime kinds of its counted
   fields - instead of on the layout: 2729 `D_` collapse to 1363 and 2314 `R_` to 1037 under a key that abstracts the
   layout name, which is 1538440 bytes (2.3%). **This is an upper bound and not a proof**: identical text modulo the layout
   name shows the same operations on the same field *names*, and two layouts can still differ in a field the body never
   mentions. It becomes sound the moment the glue is keyed on the shape itself.
4. **The `#line` directives are 3835425 bytes (5.8%)** at about two per function. A profile that leaves them out, or a
   `#line` whose file is an index into one table of paths, removes almost all of it.
5. **The comment over each definition repeats the name on the next line**: 1908313 bytes, 2.9%.

**A module `const` that is not static data is built where it is read, and the lexer pays for it per token.** The
measurable case is `punctuationTable` in `compiler/src/syntax/lexer.trb`, 35 pairs of a text and a case. The emitted
`punctuationAt` is the proof: it declares 105 slots and builds all 35 tuples and the whole list **inside its own body**,
and it is called once per punctuation character of every file the compiler reads. A compiled probe over the same shape
(10 pairs, 200000 reads, `tests/probe/` and deleted again) measures **1.43 microseconds per read** against
0.83 for the walk alone, so the build is 63% of what such a read costs.

**And it is worth half a second of a `check` of this repository.** `Lexer` reads the table into a field of its own once
per file now, which is the read count an immortal static would have, and `check --timings ..` over the 280 files of the
repository puts "lexing, parsing and the module graph" at **2145 and 2130 ms before, 1858 and 1631 ms after** - 14 to 23%
of that pass, and the pass is a third of `check`. One `const`, one call site.

The shape that removes it everywhere is the one 6.1's long tail named: the value is built **once** into an immortal
counted static (`TORB_IMMORTAL_COUNT`, which `torb_retain`, `torb_release` and `torb_make_unique` already treat as "never
counted, never freed, a write copies"), and a read of the `const` retains that one block. **It is written now** - see
"What the immortal counted static decided" below, which also says what became of the field in the lexer.

**What the VM of 7.x has to know from this round.**

- **A witness thunk is not a thing of a table.** The C back end needs thunks because a table holds function pointers of
  one erased signature; a VM that dispatches through an index into a member list needs none at all, and the lesson that
  survives is the key: what a table entry *is* is *(the target's payload, the member's function)*, and two traits that
  reach the same member of the same type must share whatever stands there.
- **Code size is a cost of the C back end and not of the VM** - bytecode has no 139-byte names and no `#line` - but the
  *instance count* behind it is shared: 7762 instances of generics and 1762 copies of six collection defaults that never
  mention their element type are the same monomorphization in both back ends. A VM that interprets can share an instance
  whose bytecode does not depend on the type arguments; the emitter can only merge identical text.
- **A module `const` that is not static data is built where it is read in the VM too**, unless it holds the built value
  somewhere. The decided shape is one immortal block per `const` and a retain per read, and it is the same decision for
  both back ends because it is the *lowering* that inlines the initializer today.

### What the immortal counted static decided

**A module `const` whose value is no static data is built once, into a block that is never freed.** 6.3 measured the
cost (a list of 35 tuples per punctuation character of every file the compiler reads) and named the shape; this is what
the shape turned out to be, and the two decisions inside it that were open.

**It is an accessor and an initializer, not a `StaticContents` case.** `FunctionKind.ConstantCell(initializer)` is a
function with a signature and no blocks: a read of the constant is an ordinary `Call` of it, and the cell plus the flag
that says the value was built are the **back end's** - two file-scope `static`s beside the function in C, a slot of the
module's frame in the VM. The initializer is an ordinary function of no parameters whose body is the initializer
expression. Nothing else in the IR changed: the ownership pass and its verifier already skip a function without blocks,
and `finishProgram` decides the result mode of both halves like any other signature's.

Why not read-only data, which is what "a list storage whose count is the immortal sentinel" would have been: an
`ExpressionNode` tree is spellable as a `static const struct`, but `"a" + "b"` is a call of `Add.add` and a list literal
is `torb_list_with_capacity` plus one `add` per item, so the general case has no C initializer at all. One mechanism that
covers every shape beats two that cover one each.

**Built at the first read, and that is what keeps the promise.** The language says there is **no module initialization,
ever** (1.6), and a lazily built immortal keeps it exactly: nothing runs before `main`, the order in which the reads
happen decides nothing, and a `const` no path reads is never built. An eager pass before `main` would have to be ordered
somehow, and there is no order the language could name. What makes the laziness unobservable is the checker's own purity
rule for a module constant (docs/TYPECHECKER.md, What exactly is "compile-time evaluable"?) - literals, the operators of
the number types, interpolation, collection literals, constructor calls and other constants, and never a function call -
so there is no side effect whose timing anything could see. In an entry file or a test file, where the rule does not
apply, a top-level `const` is a **local of the entry function** and never reaches this at all, which is what closes the
one hole the previous round recorded.

**Immortal by construction, so the leak gate stays exact.** `torb_begin_immortal()`/`torb_end_immortal()` open a region
around the call of the initializer, and every block `torb_allocate` hands out while one is open is born with
`TORB_IMMORTAL_COUNT`: `torb_retain` and `torb_release` are no-ops on it, `torb_make_unique` copies it, and it is never
freed. The region is what makes the *whole graph* immortal - the list storage, the texts inside it, the boxes of a case -
where marking only the top block would have left its children in the live count. The temporaries the initializer made on
the way become immortal too, which is a fixed number of blocks per program and not a leak that grows.

The counter is split rather than the gate weakened: `torb_report_leaks` writes `live blocks at exit: 0` **and**
`immortal blocks at exit: N`, both are asserted by `tools/conformance.sh`, and `live blocks` keeps
meaning "everything that was counted was freed". The alternative - one number with a note - would have made the gate
unreadable the first time it went wrong.

The accessor is what a read of the constant goes through and nothing else knows the constant is a cell:
`lowerImmortalConstant` in `ir/lower/expression.trb` is that seam.

**The fixpoint holds on it.** Stage 1 and stage 2 agree on **57289851 bytes** of C and stage 3 emits them again; stage 1
takes 250.1 s for the build, stage 2 108.1 s and stage 3 22.0 s for the emit alone. 1453 tests, 98 runtime tests. (After
5.11's first half: **57455864 bytes**, 279.2 s / 106.2 s / 21.9 s, 1469 tests, 108 runtime tests. After 5.11's second
half: **57944973 bytes**, 274.4 s / 123.4 s / 26.2 s, 1482 tests, 121 runtime tests.)

**A body lowered inside another body is one mechanism now.** The initializer is lowered where the constant is first read
and not through the worklist, because a construct the back end does not translate yet has to abandon the body that
*reads* the constant - which a worklist entry drained later could not reach any more. That is the shape a closure body
already had, so `savedFrame`/`enterBody`/`restoreFrame` in `ir/lower/closure.trb` are public and carry the module, the
substitution and the captured variables as well: a `const` is lowered in the module it is written in, whoever reads it.

**What it costs and what it buys.** The lexer's `punctuationTable` is the measurable case, and the previous round had
worked around it with a field of `Lexer` that read the table once per file. With the general mechanism, `check --timings`
of this repository from stage 2 (16 cores, gcc 13.2, `-O2`, three runs):

| `check ..` from stage 2, three runs each | lexing, parsing and the module graph | all passes | `program.c` |
|---|---|---|---|
| the table in a field of `Lexer`, read once per file | 1790, 2023, 2020 ms | 7430, 7514, 7432 ms | 57270499 |
| the `const` read directly, through the accessor | 1787, 1963, 1949 ms | 6928, 7492, 7247 ms | 57270002 |

**So the field is gone.** The direct read is never slower - the medians are 1963 against 2020 ms - and the two numbers are
inside each other's noise, which is the point: the general mechanism makes the workaround unnecessary, so what decides is
that one field, one initializer and one comment about why they exist are no longer there. What the field bought was the
read count; what the accessor costs is a call and a return of a value whose retain is a no-op, and the emitted C is 497
bytes smaller without it.

This is also the honest size of the win overall: 6.3 measured 14 to 23% of the lexing pass for **one** `const`, and the
mechanism now covers every one of them - but the compiler has exactly one `const` that is read per token, so the rest of
them buy correctness of the cost model and not a second half-second.

**A trait member is looked up by name and shape, and a name alone is not enough.** `Show.showNested`'s "generated member
without a receiver" at `std/core/src/convert.trb:113` was not about `showNested` at all: `WellKnown` of the compiler has a
**field** `show: SymbolId?`, `providerOf` of the checker answers a field under the member's name, and the back end took it
as the provider of `Show.show` - whose generated instance then had no receiver, because a field's signature takes no
`self`. One finding refused the whole build, and with it every `Show` of a compound value of the compiler's own types: a
probe that shows one `Checker` lowered 7951 of 7952 functions and now lowers 7965 of 7965.

`declaredMemberOf` in `ir/witness.trb` asks the question the checker's own requirement check asks
(`compareSignatures`: `required.takesSelf == given.takesSelf`), so the back end finds the very declaration that made the
program legal, and a derived member is generated where the name belongs to something else. The checker does not run that
check for a **derived** implementation at all - there is nothing the source wrote to compare - which is why nothing
before the back end noticed.

One divergence this found and did not close: on stage 0, `value.show()` where the value has a field `show` reads the
**field**, because stage 0 has no type checker. So the gate program shows such a value through `print` and never through
`.show()`, and the entry is on 5.14's list below.

**A plan number never reaches a message a user sees.** Findings read `not supported by the back end yet: a quoted
expression (milestone 5.11)` before this. Which sub-milestone will build a construct is a fact about this repository's
plan and not about the program in front of the reader, and a plan that moves leaves the number wrong.
`unsupportedMessage` in `ir/unsupported.trb` is the one place that words it - `<the construct> is not supported by the
native back end yet`, with an optional `: <the reason>` for the four kinds whose reason is not part of the construct - and
both halves go through it: the lowering's `reportUnsupported` for a construct of the program, the emitter's for one of the
IR. The reason is a parameter and not a relative clause inside the construct because a clause that ends in a negation of
its own reads as two sentences fighting each other (`` `Json.encode`, which the runtime does not provide yet is not
supported ... ``). `torb build` prints the emitter's findings as `error: <message>` lines like the
lowering's instead of a header plus an indented list that said the same thing twice, and `torb ir --statistics` still
groups by construct. The manifest keeps `NativeState.Planned` with its milestone as an internal key, because the table
has to say which entries are not written yet; a planned native reads `` `X`, which the runtime does not provide yet ``.

**What the VM of 7.x has to know from this round.**

- **A module `const` that is no static data is a function, not a datum.** The VM implements `FunctionKind.ConstantCell`
  with a slot of its own and a flag, builds the value on the first call, and never frees it. The initializer it names is
  an ordinary function of the program.
- **An immortal region is part of the ABI of a constant, not an optimization.** A VM that counts references has to make
  everything allocated inside one immortal as well, or a read of the constant that is released will free the value under
  the next reader.
- **A counter for immortal blocks is what keeps a leak report honest.** Two numbers, not one.
- **A trait member is `(the name, whether it has a receiver)`.** A table entry resolved by name alone can land on a field
  of the target that carries the member's name.

**Two more entries for 5.14's list of differences between stage 0 and the binary**, both found here and neither fixed:

- **A field that carries the name of a trait member shadows the member on stage 0.** `value.show()` on a value with a
  field `show` reads the field there and calls `Show.show` in the front end. It is the last shape of "stage 0 has no type
  checker" that a gate program can hit.
- **A capture of a quotation that is not a scalar is shown by its name and type natively** (see the 5.11 note below),
  which is a divergence in the text of a failing `assert` and in nothing else.

### What 5.11 needs, measured before it is written

Quoted expressions are the one row left between the compiler and its own tests running natively: **58 findings, every one
`a quoted expression`, every one in a function of `compiler/tests/`** (`torb ir --statistics ../compiler/tests`: 10752 of
10810 instances lowered). Nothing of `compiler/src/` uses a quotation, so no build waits for this. What follows is what
the round has to build, and the three costs that were measured first, because two of them decide the design.

**The value.** `Expression<Value>` is `PrimitiveKind.Runtime(RuntimeKind.ExpressionTree)` today, a placeholder with
pointer size and no contents. It does not have to stay one: the declaration in `std/expression` **has fields**, so
dropping it from the primitive table of `ir/instantiate.trb` gives it the ordinary record layout of its own declaration
(`tree`, `source`, `location`, with the declared field symbols, which is what `fieldIndexOf` matches a field read by), and
the lowering appends what only it knows. The pieces already exist:

- **`value()` is a `lazy` cell.** `cellType`/`cellLayoutOf` build `Option<Value>` plus a thunk, `lowerLazyArgument` builds
  one from an expression, and `lowerForcedRead` forces it and writes the answer back - which is exactly "the ordinary
  value, evaluated at most once". So `Expression.value` is a native the *lowering* owns and not a function of `runtime/`,
  and the two `.Planned("5.11")` runtime entries go away rather than being written.
- **The captures are in `Quotation.captures`** (`semantics/checker/context.trb`), as `BindingId`s in the order
  `captures()` answers them. The closure machinery reads its captures from `Tables.captures` by span instead
  (`capturePlansOf`), and a quotation must **not** be recorded there: when the quoted expression *is* a closure literal
  (`filter { _.age >= minAge }`) the quotation and the closure have the **same span**, and the second write would take the
  closure's own plan away. So the plans are built from the `Quotation`, with `capturePlansOf` refactored to take the
  binding list it already maps.

**`assert` as `std/expression` writes it cannot be lowered, and the reason is not the quotation.** Its failing path calls
`describe(values[index])` over `List<Encode>`, and two measurements decide what a capture may be:

- **`describe` is `.Planned("5.7")`** and a call of it is a clean finding today. It cannot be a function of `runtime/` at
  all (it drives `Encode`, whose `Encoder` is a value of the *program*), and ENCODING's decided replacement is
  `EncodedValue.of(value).show()` - which needs `Encode` per captured type.
- **What per-captured-type machinery costs.** A probe that shows one whole `Checker` (`"{checker}"`) needs **1731
  instances** and is blocked by one finding of its own: `a generated member without a receiver` at
  `std/core/src/convert.trb:113`, which is `Show.showNested`'s instance for a type whose signature comes out with no
  parameters. The compiler's own tests capture exactly such values (`assert(functionName(found.program, ...) == "...")`
  captures `found`, which holds a whole checker and a whole IR program), so a quotation that boxes every capture into
  `Encode`, or shows every capture through `Show`, pulls the transitive closure of that trait over the compiler's types
  into the test binary - and one unshowable type anywhere in 2801 asserts refuses the whole build.

So **how a capture is stored and how it is shown is one function of the lowering and nothing else may know it**, which is
also what keeps ENCODING's redesign local when it lands (`captures()` answers `List<EncodedValue>` then, and `assert` reads
`.show()` off one). The message a failing `assert` prints is what that function decides.

**Decided: (c), a scalar by value and everything else by name and type.** A failing `assert` shows a capture that is an
`Int*`, a `UInt*`, a `Float*`, a `Bool`, a `String` or a `Char` by its value, and every other capture as
`found: LoweredProgram`. It keeps two things out of the test binary that (a) would put in it: the per-captured-type
instance cost (1731 instances for one `Checker`), and the "one unshowable type refuses the whole build" problem, which is
what 2801 asserts over the compiler's own types would mean. Full values come with ENCODING's redesign, where a capture
*is* an `EncodedValue` and needs monomorphized generic trait methods. It is a **recorded divergence from stage 0 in the
text of a failing `assert` and in nothing else**, so it is on 5.14's list; and it lives behind the one function of the
lowering above, which is the seam that makes the redesign local.

The finding that blocked (a) is fixed anyway, and for a reason that had nothing to do with quotations: see "A trait
member is looked up by name and shape" in the note above.

**The tree is static data of a counted type, which the emitter does not have yet.** `StaticContents.Aggregate` of a
`Boxed` layout reports `a constant of a counted type` (`emit.trb`), an `ExpressionNode` is recursive and therefore boxed,
`Literal(value: Encode, of)` is a boxed *trait* value and `arguments: List<ExpressionNode>` is a runtime container with a
side buffer - none of the three has a static form. **This is the same immortal counted static the module `const` above
needs**, so the two are one piece of work: a `static const struct { torb_header header; ... }` with
`TORB_IMMORTAL_HEADER`, and a value that is a pointer to it. Building the tree at the quote site with ordinary
instructions instead is the alternative and costs its allocations per *evaluation* of the quotation.

**`test` and `group` can be functions of the runtime, because the runtime can call a closure.** Every closure value's
`code` points at a thunk with the erased signature `R (*)(torb_environment *, ...)` (`emitClosureThunk`), and a closure
without captures has a thunk too - so `torb_test_case(torb_text name, torb_closure body)` casts and calls. What it needs
beyond that is a **recovery point**: `torb_finish_panic` in `runtime/panic.c` renders the message into a fixed buffer and
`_exit`s, and a test runner needs it to `longjmp` back instead while a test is running. A recovered panic runs no release,
so **a program that recovers leaks what the aborted frame held** and the leak gate cannot apply to one. (What was written
here before - that only passing tests could be compared, because the interpreter names an absolute path in the `at` line
of a failed test - is no longer true: 5.14 made a runtime location the stable path on both sides, so a *failing* test is
compared byte for byte as well, and `test-failure.trb` does.)

**`main.exe test ../compiler/tests` is one binary for all 55 files.** One binary per file is not an option: every test
file imports the harness and through it the whole compiler, so it would be 55 gcc runs over 55 translation units the size
of the compiler's own. The whole suite is 10810 instances against the compiler's 13629, so one program that holds every
test module plus a generated entry that runs them in order is roughly the size of `program.c` and one gcc. The lowering
already takes a **list** of entry modules (`lowerWorkspace ... modules`); what is missing is an entry that calls each
module's entry function with the file's name printed in front of it, and `emitProgram` taking more than one entry name.
The counts of the summary line belong in `runtime/` next to `test` itself, so that both back ends print one format.

### What 5.11 decided: `assert` is the back end's, `test` is the runtime's, and a run is one binary

**`assert` is lowered and the quotation behind it is never built.** `assert(condition)` needs three things - whether the
condition held, what the reader wrote, and what the condition read from around it - and an `Expression<Bool>` value
carries none of them any cheaper than the lowering already has them: the condition is the ordinary `Bool` the checker
typed, the source is the bytes of its span, and the captures are `Quotation.captures`. So `ir/lower/quote.trb` writes the
branch and the `panic` itself, and the **58 findings `a quoted expression` in `compiler/tests/` are 0**: `torb ir
--statistics ../compiler/tests` lowers 12318 of 12319 functions, and the one that is left is a list pattern inside
another pattern and has nothing to do with quotations.

That is also what keeps the cost the measurement above found out of the binary. The body `std/expression` writes hands
every capture on as an `Encode` and asks `describe` for the text; lowering *that* would put one `Encode` implementation
in the binary per captured type and refuse the whole build for one type that has none, which is what 2801 assertions
over the compiler's own types would mean.

**What a failed assertion shows is one function and nothing else knows it.** `describedCapture` answers the line per
capture, and it is the seam ENCODING's redesign lands on: when a capture *is* an `EncodedValue`, only that function
changes. Today it answers

- a **scalar** - every integer type, every float, `Bool`, `Char`, `String` - as `name = the Int 3`, which is the
  interpreter's own description of a value to the byte, so a failing assertion reads the same way on both
  implementations. The word after `the` is the language's name for the *kind* of value and not the name of the type
  (`Int` for an `Int64`, `Float` for a `Float64`), because that is the vocabulary stage 0 has and the text is what is
  being compared;
- everything else as `found: Point` - its name and its type.

The second one is a **divergence from stage 0 in the text of a failing assertion and in nothing else**, so no gate
program of the byte-compared suite can hold one: `tests/conformance/binary-only/` is where it is pinned instead,
built and run as a binary alone, with the README naming what differs and why.

**Stage 0's `assert` panics.** It failed as the *interpreter* before - `error:` and exit code 1 - which is a kind of
ending a compiled program has no counterpart for, so a failing assertion could not be compared at all. The language says
`assert` panics, `ir/lower/quote.trb` emits a panic, and now both write `panic: Assertion failed: ...`, the same site and
101.

**`test` and `group` are functions of the runtime, and what they needed was a recovery point.** The runtime can call a
closure - a closure value's `code` is a thunk with an erased environment - so the calls themselves are ordinary. What is
not ordinary is that a test whose body panics has to be *reported* and the next test has to run: `torb_begin_recovery`
in `runtime/panic.c` makes `torb_finish_panic` fill a `torb_recovery` and `longjmp` into the frame that runs the test,
instead of writing to stderr and leaving. The point is taken away before the jump, so a panic while a failure is being
reported is an ordinary panic and never a jump into a frame that is gone.

**A run that recovers leaks, and the gate says so rather than being loosened.** A recovered panic runs nothing on the way
out - no release, no `Close`, no destructor - exactly as an ordinary panic runs nothing, so everything the aborted frames
held stays allocated. `tests/conformance/test-failure.trb` reports `live blocks at exit: 1` and carries a
`.leaks` file beside it that holds that sentence; the runner reads the file and skips the leak gate for that one
program. An exemption that has to be written down next to the program is not a hole in the gate.

**The report is one format in one place.** `runtime/test.c` writes `  ok      <group> > <name>` and the four lines of a
failure, character for character what `natives.rs` writes on stage 0, for the same reason `print` joins its parts in the
runtime: two implementations of one format are two chances to disagree, and `torb test` compares the two line by line.
`tests/conformance/{tests,test-failure}.trb` are the two gate programs, byte equal on both sides.

**`torb test <directory>` is one binary, and the generated `main` is where the files are a list.** Every test file
imports its harness and through it whatever it tests, so one binary per file would be one C compile of a translation
unit that size per file - and 6.3 measured that the C compiler is where a build's time goes. The whole suite together
is about **77 MB** of C, a third more than the compiler's own 58 MB, and one gcc of it. So `emitProgram` takes
a list of `ProgramEntry` (a mangled entry name, and the file it is the top-level code of) instead of one name, and the
`main` it writes is

```c
torb_process_start(argument_count, argument_values);
torb_test_file("../compiler/tests/adts.test.trb", 34);
t_torbscript_x2f_compiler_tests_x2f_adts_x2e_test();
...
int code = torb_test_finish();
torb_process_finish();
return code;
```

`torb_test_file` and `torb_test_finish` are in `runtime/test.c`, beside the line of a single test, for the reason the
line itself is there: **one format in one place**. The counts of the run, the blank line, `N passed, M failed (K files)`
and the exit code are the runtime's, so the two implementations of `torb test` cannot drift apart. The order is the
order of the files' paths, and a file that declares things and runs nothing still prints its name and calls nothing.

**A failing file does not stop the run**, which is what makes one binary behave like fifty-five processes: a test whose
body panics lands in the recovery point of `runtime/panic.c`, is reported, and the next test - in that file or in the
next one - runs.

**`--jobs` is accepted and ignored.** Inside one process there is nothing to spread over cores: the files share one
heap, the report is one stream of lines in the order of the files, and threads would buy a run nothing that the one C
compile in front of it did not already dominate. Rejecting the flag would only break a command line written for stage 0.
`docs/tooling/torb-test.md` says so.

**What the run forwards, and what that costs today.** The driver runs the binary as a child and writes what it wrote.
`Process.start`, which hands out the two pipes apart, is 7.3's, so `Process.run` collects both streams as one text: the
report arrives when the run is over rather than while it happens, and a panic that escaped every recovery point - a
file whose *top level* panicked - lands on standard output with the rest. Everything the report itself is goes to
standard output anyway, so nothing of the comparison is affected.

**Measured** on 16 cores with gcc 13.2, all of it in one run of the suite gate (808.08 s): stage 0 builds the compiler in **294.2 s**; the built
`torb` builds the whole suite and runs it in **208.5 s**, almost all of which is the one gcc - the binary itself runs
all 1482 tests in **under a second**; stage 0 runs the same suite in **305.4 s**, one process per file on as many
cores as there are. The slow way for comparison: the same build driven by the interpreter
(`torb run ../compiler test ../compiler/tests`) takes **358 s**. That test is the gate: it builds the compiler, runs
`<the binary> test ../compiler/tests`, and compares the whole report with stage 0's line by line, plus the exit code.

**`\n` is `\n` everywhere, and that is the runtime's job.** On Windows the C runtime opens `stdout` and `stderr` in
*text* mode, so every `\n` a compiled program wrote into a pipe or a file became `\r\n` - the same program producing
different bytes on two platforms, and a conformance runner that had to fold them away before comparing anything.
`torb_process_start` puts both streams into **binary mode** instead, and the conformance runner normalises nothing:
what a program wrote is compared as it wrote it, and a `\r` that turns up
is a difference the suite is meant to catch. The expectation *files* are still folded, because git may check one out
with either ending. The console path is untouched by the stream mode - `WriteConsoleW` takes UTF-16 straight to the
console handle - and a console still breaks lines correctly because its own processed-output mode is what turns a `\n`
into a new line there.

**The runtime's own two reports go through the path `print` goes through.** A panic report and the lines of the test
report are rendered into fixed buffers and are never a `torb_text` - a report that has to allocate is a report that
cannot be written when the heap is gone - so they used to go straight to `fprintf`, which on a live Windows console
writes UTF-8 bytes through the console's own code page and turns every non-ASCII character of a message or a test name
into noise. `torb_write_line` in `console.c` is the same dispatch `print` uses, with a console path that converts on the
**stack** so that the no-allocation promise holds; a line too long for that buffer, or one that is not valid UTF-8,
falls back to the raw bytes exactly as `print` does. A pipe gets byte-identical output either way, which is what the
suite compares.

**A list pattern inside another pattern is lowered**, which was the last finding in the compiler's own tests. A list has
no layout, so the container a `length()` or an `at(index)` runs on cannot be read off the layouts the way a field can;
`containerTypeOf` walks the subject's type step by step instead and asks the checker the questions the pattern check
asked on the way in - `caseNamed` for a case field, `fieldsOf` for a field, the type's form for a tuple position, and
`itemOfSubject` for an item of an `Iterate`. The subject is substituted first, so a nested list inside an instance of a
generic reaches that instance's type arguments. `torb ir --statistics ../compiler/tests` lowers **12375 of 12375**
functions.

**What 5.11 did not do, and what it costs.** One half is open and nothing this round decided blocks it:

- **An `Expression<Value>` as a value.** `Expression.value` and `Expression.captures` are still `.Planned("5.11")` and a
  quotation that is not the argument of `assert` is still `a quoted expression`. What it needs is written above under
  "What 5.11 needs": drop `Expression` from the primitive table of `ir/instantiate.trb` so it gets the record layout of
  its own declaration, append the memo cell and the captures to it, and build the static `ExpressionNode` tree through
  the immortal counted static - one `FunctionKind.ConstantCell` per quotation site, which exists now and is what makes
  the tree a build-once. Nothing of `compiler/src/` and nothing of `compiler/tests/` needs it, which is why the gate of
  this milestone holds without it.

**What the VM of 7.x has to know from this round.**

- **A recovery point is part of the ABI of a panic.** A VM that runs `std/test` has to let a panic land in the frame that
  runs the test, and it may not release anything on the way there: a recovered panic unwinds nothing, which is the same
  promise an ordinary panic makes.
- **The report of a test is the runtime's, not a back end's** - the line, the counts and the summary. One format in one
  place, or the two implementations drift apart line by line.
- **A program has a list of entry functions and not one.** `torb test` is the first thing that builds one, and the VM
  needs the same shape: the entries run in order, each one is the top-level code of one module, and the tail of the run
  answers the exit code.
- **What a failed `assert` shows is one function of the lowering.** A VM that shows more than a scalar by value pays the
  per-captured-type cost this round measured; it is a decision about the binary and not about the language.
- **`\n` is `\n`.** Whatever a back end writes a line through, the bytes that reach a pipe are the bytes the program
  wrote. A platform's text mode is a property of the machine and never of the program.

### What 5.14 decided: one observable behaviour

**The conformance suite compares everything a program can be observed doing, and nothing is exempt.** Before this
sub-milestone `native.rs` knew about one allowed difference - a program that panicked only had to *fail* on stage 0 - and
that hole is closed: standard output, standard error and the exit code are compared byte for byte for all 57 programs of
`tests/conformance/`, and a program with no `.stderr` file promises to write nothing there at all. `tests/conformance/README.md` is the contract, and it names which program pins which
behaviour.

**The one thing read loosely is the position inside a frame of `std/`.** A `.stderr` file writes
`  at std/core/src/option.trb:_:_`, because a line of the standard library moves whenever a comment above it is edited
and what a program promises is *which file* panicked. Stage 0 answers those bodies with a native and has no line for one
at all, so it writes the file without a position, which reads the same way. That is a position nothing promises, not a
behaviour.

**A program ends in one of three ways, and the interpreter now says which.** `Failure` carries a `FailureKind`:

| | Report | Exit code | Compared |
|---|--------|-----------|----------|
| `Panic` - `panic`, overflow, division by zero, an index out of range, `expect` | `panic: <message>` and `  at <path>:<line>:<column>` | **101** | yes |
| `Error` - a top-level `?` that failed | `error: <the error through Show>` and one `  caused by:` per link of `cause()` | 1 | the first line and the code; the chain waits for the back end |
| `Interpreter` - a name that is nowhere, a method a value does not have, a `var` path that was moved out | `error: <message>`, the site, and `  in <function>` per call | 1 | no counterpart |

The third kind is why the interpreter did not simply start printing `panic:` for every failure: stage 1's type checker
rejects every program that reaches one, so a compiled program has nothing to compare against it, and its frames are what
a crash of the toolchain is debugged with. **A panic prints two lines and no more**, the way the release profile of a
compiled program does; `TORB_FRAMES=1` adds the frames and, under a panic whose site is a file of `std/`, the site in the
program.

**A runtime location is the stable path,** `torbscript/compiler/src/ir/print.trb`: the package's name plus the file below
the package's directory, which is exactly what `pathsOfModules` of the lowering interns. Stage 0 has no workspace, so the
loader walks up to the nearest `project.trb` and reads its `name "..."` statically - 30 lines in `program.rs`, cached per
directory. A problem of *loading* keeps the path of the machine, because it is editor-facing rather than compared.

**Every divergence that was recorded is closed, and the side that was wrong was not always stage 0.**

| What differed | Which side was wrong | How it is pinned |
|---------------|----------------------|------------------|
| The panic report and the exit code (`error:` and 1 against `panic:` and 101) | stage 0 | every `.stderr` of the suite |
| `Integer overflow` against ``arithmetic overflow in `*` ``, with the operator in it | stage 0 | `overflow.trb`, `negate-overflow.trb` |
| `Division by zero` against ``division by zero in `/` `` | stage 0 | `division-by-zero.trb`, `remainder-by-zero.trb` |
| An index out of range: stage 0's own message against `Indexed.at`'s `Key does not exist` | stage 0 | `collection-index.trb` |
| A slice out of range and a reversed one | stage 0 | `slice-out-of-range.trb`, `slice-reversed.trb` |
| A text sliced past its end, and an offset inside a character | stage 0 | `text-slice-past-end.trb` |
| `expect` on `None` and on a `Fail`: the message, and which file of `std/core` the frame names | stage 0 | `expect-none.trb`, `expect-failure.trb` |
| An absolute path, with Windows' verbatim `\\?\` prefix, in the site of a failure | stage 0 | every `.stderr` with a frame of the program |
| Stage 0 refused to compare a `nan` at all | stage 0 | `float-order.trb` |
| `sorted` put a `nan` where it started, because `List.sort` compared with `<=` | **`std/collections`** | `float-order.trb` |
| `Char.toUpperCase` of a non-ASCII letter: Rust's full Unicode against ASCII only, and `'ß'` became `'S'` | **both** | `character-case.trb` |
| `String.toUpperCase`: Rust's full Unicode against a byte-wise ASCII loop | **both** | `character-case.trb` |
| `Char.isDigit`/`isLetter`/`isWhitespace`: Rust's tables against the runtime's approximation | stage 0 | `character-case.trb` |
| A `project.trb` was an ordinary module wherever `test { input "." }` swept it in, so it was reported as a syntax error | the front end | `torb check tests/native` |
| A top-level `?` walked `cause()` in neither back end, though CONCEPT shows the chain | stage 0 (fixed), the back end (open) | `stage-0-only/error-chain.trb` |

**What still differs, and why.**

- **`Show` of a function value.** The owner decided the source spelling of its type (`(Int64) => Int64`); stage 0 prints
  `<function>` and the back end reports "a generated `show` of `Closure(...)` is not supported by the native back end yet".
  Both
  halves are real work - stage 0 has no types and would have to spell one from the declaration's annotations, including
  `Int` to `Int64` - and neither is on the way to anything. No gate program can exist until the back end can emit one, so
  this is the one entry that moves to 6.3's list rather than being closed here.
- **A top-level `?` whose error carries `Error` walks `cause()` on stage 0 and not in the binary.** CONCEPT shows the
  `  caused by:` lines and the interpreter writes them now; `reportFailure` in `ir/lower/match.trb` writes the first line
  and exits, and says at the function that the loop over `cause()` is one loop on top of it. Found by a gate program
  written for this milestone, so the program waits in `tests/conformance/stage-0-only/error-chain.trb` - run and
  compared on stage 0, and it moves up one directory the day the loop exists. **That subdirectory is the waiting room,
  not an exception**: the runner has a second test for it and the README says what is in it and why.
- **A range of anything but `Int`.** Stage 0's `Range` holds two integers, so `"a".."c"` is a value it cannot build.
  `ranges.trb` says so.
- **A tuple has a `compare` *member* on stage 0 and none in the checker.** The checker gives a tuple the operators and
  nothing else, which is the language's answer; stage 0 answering one more member is a thing no valid program can
  observe, because a program that calls it does not type check. Recorded in `tuple-compare.trb`, not a divergence a gate
  program can pin.
- **A `?` that converts its error through a generated `From`.** Stage 0 hands the error on unchanged, so a program that
  needs the conversion cannot be compared. `errors.trb` says so, and `lower-match.test.trb` pins the back end's half.
- **`Process.run` of a program that does not exist** is an `IoError` in the interpreter and on Windows, and the shell's
  own exit code on POSIX, where the child still goes through `popen`. `fork` plus `execvp` is `Process.start`'s job at
  7.3, and `runtime/README.md` records the difference.
- **`tls(port == 8443)` in `dsl.trb`** is written `tls true`, because stage 0 reads the parenthesized form as a call of
  the field. A limitation of stage 0's parser-free property commands, and the program says so.
- **A field that carries the name of a trait member shadows the member on stage 0.** `value.show()` on a value with a
  field `show` reads the field there, where the front end resolves `Show.show`. Stage 0 has no type checker, so there is
  nothing to decide; `show-compound.trb` shows such a value through `print` alone and says why.
- **A capture of a quotation that is not a scalar is shown by its name and type in a failing `assert` natively** and by
  its value on stage 0 (see "What 5.11 decided"). It is a divergence in the text of a failing assertion and in nothing
  else, and it closes when ENCODING's `EncodedValue` lands. Pinned since 5.11 by
  `tests/conformance/binary-only/assert-compound-capture.trb`, which is built and run as a binary alone.

**Three language decisions this needed** - each of them a question the language had not answered, decided the simplest
consistent way and written into CONCEPT:

1. **On a float every *operator* is IEEE-754, and only `compare` is the total order.** `<`, `<=`, `>` and `>=` join
   `==`: all of them `false` next to a `nan`. It is what the intrinsic already emitted, what C, Rust and Java all do, and
   it keeps a float comparison branch-free. The price is that a float is the one type where the operator and the member
   behind it disagree - so **everything that orders values calls `compare`**, and `List.sort`, `minBy` and `maxBy` were
   changed to. An IEEE `<=` in a merge leaves a `nan` wherever it started, which is the bug `float-order.trb` found.
2. **Case mapping is the simple one-to-one mapping of one code point, over ASCII and the letters of Latin-1.** A `Char`
   holds one code point, so `'ß'.toUpperCase()` is `'ß'`: its upper case is `SS`, answering `'S'` would be wrong, and
   answering a `String` would make the type of the result depend on the value. `'ÿ'` to `'Ÿ'` is the one pair that
   reaches out of Latin-1; `×` and `÷` are symbols. `String.toUpperCase` is that mapping per character, so a mapped text
   is exactly as many bytes as it was. `runtime/text.c` holds those five functions, and `character-case.trb` pins them.
3. **A failure of the *interpreter* is its own kind of ending,** neither a panic nor a top-level `?`, and it keeps stage
   0's own report with the calls it came through. Without the third kind, making stage 0 conform would have meant either
   printing `panic:` for every crash of the toolchain (and leaving with 101) or losing the frames.

**`language.trb` type checks again, and both test workspaces are under a check gate.** It was a stage-0 script the
checker rightly rejected in 11 places; repairing it was worth more than replacing it, because a 280-line program that
exercises traits, delegation, patterns, receiver closures and value semantics in one run is a different kind of test from
57 small ones. What it needed: `.toList()` on four pipelines that were printed, `sort({ _ })` instead of `sort()`, a `fn`
inside a block turned into a top-level function with parameters (a `fn` in a block is not a closure), a map built with a
loop instead of a `toMap` that does not exist, a closure that no longer captures the receiver `self`, and the three hand-written
operator traits deleted in favour of the prelude's - which is why `Add`, `Subtract`, `Multiply`, `Divide`, `Remainder`,
`Negate` and `Compare` are in stage 0's `prelude.trb` now: `a + b` means the *prelude's* `Add`, and a script that
declares a trait of its own name does not get the operator. `tests/language/` and `tests/conformance/` are
each a workspace of their own, and `torb check tests/conformance tests/language` is the second `check` of the
gate list. The only TorbScript left outside a `check` is `tests/parser-cases/`, `tests/lexer-cases/` (deliberate errors)
and `.vscode/extensions/torbscript/samples/tokens.trb`, which is not a program at all and only has to parse.

**15 gate programs were added for behaviour that had none**, chosen by reading the language reference for rules with
observable run-time behaviour: `copies.trb` (a value has no identity, so a second name is a second value),
`closure-captures.trb` (a captured `var` binding is one shared box, one closure per turn of a loop), `integer-division.trb` (truncation towards
zero, the remainder's sign, `(a / b) * b + a % b == a` over eight pairs), `sort-stability.trb`, `match-order.trb` (the
first arm wins, a false guard falls through, a guard runs only for its own arm), `character-case.trb`, `float-order.trb`,
and seven panics: `negate-overflow.trb`, `remainder-by-zero.trb`, `expect-none.trb`, `expect-failure.trb`,
`slice-out-of-range.trb`, `slice-reversed.trb`, `text-slice-past-end.trb`. The fifteenth, `error-chain.trb`, found a divergence
nobody had recorded and waits in `stage-0-only/` - which is what a gate program is for.

### How the C emitter is written

The emitter used to build its C by concatenating and interpolating strings: a statement, an expression, a struct member
and a whole helper function were all `String`. Three things were therefore easy to get subtly wrong and invisible in
review - where a parenthesis goes, how deep a line is indented, where a blank line belongs - and every slice of
milestone 5 added more of it. `backend/c/writer.trb` is the replacement, and 5.6 to 5.11 extend it rather than going
back to text.

**Everything with structure is a value.** Four types, and every case of them is one the emitter really builds - it is
not a C syntax tree for its own sake, and a case nothing uses is deleted rather than kept for later:

| Type           | Cases                                                                                                                                  |
|----------------|----------------------------------------------------------------------------------------------------------------------------------------|
| `CExpression`  | `Name`, `Literal`, `Member`, `PointerMember`, `AddressOf`, `Dereference`, `Call`, `Cast`, `Unary`, `Binary`, `Conditional`, `Initializer`, `Designated`, `CompoundLiteral` |
| `CStatement`   | `Declaration`, `Assignment`, `Evaluate`, `If`, `Switch`, `Goto`, `Label`, `Return`, `Break`, `Block`, `Joined`, `LineDirective`, `Comment` |
| `CMember`      | `Field`, `Group` (an unnamed struct under a name), `Union`                                                                               |
| `CDeclaration` | `Typedef`, `Struct`, `Prototype`, `Definition`, `StaticData`, `Comment`, `Fixed`                                                         |

There is deliberately **no `enum`** and no top-level `union`: nothing emits one, and `sizeof(T)` is a `Call` of a
`Name`, which needs no case of its own. A **type spelling stays a `String`** - deciding it is `type.trb`'s job and
nothing ever takes one apart again - and so does a function head, which is types and a name all the way down.

**One place decides a parenthesis.** `rendered(expression, limit)` in `writer.trb`: every node has a precedence (0 a
name, 1 a postfix operator, 2 a prefix one, 3 to 12 the binary ladder, 13 the question mark), every place says the
loosest it allows, and a node that binds looser than its place gets a bracket. Nothing else in the back end ever writes
one. Two rules in it are chosen rather than forced by C, and both are there because the generated C is read by people:
the **condition of a `?:` is always bracketed**, and the right side of two binary operators of equal rank is, so
`a - (b + c)` never has to be re-derived from the ladder. An operand that binds tighter than its place never gets one,
which is why `(int64_t)torb_text_compare(a, b)` and `(torb_bytes *)&s_storage` come out flat.

**One place decides indentation and line breaks.** `CWriter.depth`, two spaces a level, threaded through the renderer.
A `Label` and a `#line` go to the left margin whatever the depth is. A construct spans one line where it can: an `if`
whose body is a single one-line statement, a `case` whose body is one, and `Joined`, which is what the `(void)` casts
of every unread slot are. `StaticData` is the only declaration the emitter ever breaks over two lines, for the immortal
storage of a string literal whose type is a whole anonymous struct; **nothing wraps by column**, because a mangled name
makes a line far longer than 120 and cannot be broken at all.

**One place decides the blank lines.** `needsBlankLine`: a section `Comment`, a `Struct`, a `Definition` and a `Fixed`
block each open a paragraph; a `Typedef`, a `Prototype` and a `StaticData` belong to the list above them. `emission.write`
is the only way anything reaches the output and asks that question once, so no part of the emitter has to remember the
shape of the file. A `Definition` carries its own comment, which is why the comment of a function and its body are not
two paragraphs.

**When a `"""` block instead of the writer.** Judge by braces, because stage 0 needs `\{` for every one of them
(`compiler/CONTRIBUTING.md`, trap 6) and a block full of `\{` is less readable than the values, not more. Fixed C text
with few braces and a hole or two is a `"""` block with interpolation: the head of the translation unit (the comment and
the two `#include`s) and `main`. Everything whose shape depends on the program - a body, a helper, a struct, an
initializer - is the writer, even where the skeleton around it looks fixed: the retain and drop helpers are a
`CDeclaration.Definition` whose body is built, not a template with a hole.

**`prototype.trb` stays string-based on purpose.** It takes a *manifest* prototype apart - text that comes from
`natives.trb` and is compared spelling by spelling - and never builds C.

### Integration of 5.3 and 5.4

5.3 (the C emitter) and 5.4 (ownership) were written in parallel against section 2's contract. What did not fit, and how
it was closed:

- **`torb build` ran the emitter over phase-one IR.** The pipeline is now lower → `insertOwnership` →
  `verifyOwnedProgram` → emit, which is what `torb ir` already did. The driver's internal-error check is the *extended*
  verifier, so a body that leaks a count on one path never reaches a C compiler.
- **The three contracts that are not instructions are kept now** (5.4's list): a `Read` of a counted field retains into
  the target, a `Write` through a counted place releases what was there before it stores, and a `Call` result arrives
  owned (which it already did, because a callee returns `owned`). `Copy` is an assignment plus one more count and `Move`
  the assignment alone, and for an **inline aggregate** both of them mean "per counted field" - which is exactly the
  `R_<layout>`/`D_<layout>` pair. `retainStatement`/`releaseStatement` in `backend/c/body.trb` is the one place that
  knows every shape of the ABI, and the child lines of a helper are the same two functions over `value->f_x`.
- **`R_<layout>` and `D_<layout>` are emitted per *half*, not per pair.** An unused `static` function is a warning and
  `-Werror` is on, so a layout that is only ever released gets a `D_` and no `R_`. The demand is two flag sets over the
  layouts, seeded from the positions that really call one (`Copy`, `Retain`, `Release`, `Read`, `Write`, `MakeUnique`) and
  closed over the fields, because a helper walks its layout's fields. Everything is declared before anything is defined,
  so a helper may call the helper of a field whatever the order of the names is.
- **A niche layout has no helper at all.** It *is* its payload field (section 3.1), so `countedFormOf` unwraps it and
  retaining an `Option<String>` is retaining the `String` - there is no struct a helper could be written against.
- **The leak gate.** `runtime/memory.c` already counted live blocks and had `torb_report_leaks`; what was missing was a
  way for a binary to ask for it. `torb_process_finish()` writes the count to stderr when `TORB_REPORT_LEAKS=1` is in the
  environment, the generated `main` calls it before `return 0`, and `torb_process_exit` calls it too - a program that
  ends in `Process.exit` never reaches `main`'s return, and `_exit` runs no `atexit` handler. `native.rs` asserts
  `live blocks at exit: 0` for every gate program that does not panic. A **panic** is not one of the two ends: it aborts
  without running anything (decided gap 9), so what it leaves behind is not a leak.
- **A top-level binding of an entry file is a local of its entry function**, in source order, and no longer a module
  constant - 5.3's wart, which made `var counter = 0` unassignable. `Lowering.entryLocals` maps the symbol the module
  declared to the slot, so a *name* of it later in the file finds the slot; `seedModule` skips those bindings, because
  counting them twice would move the progress bar. A module that runs nothing is not an entry file, so its top-level
  `const` stays static data and "there is no module initialization, ever" is untouched. A top-level `var` read from a
  *function* of the same file is a clean finding: it would read the static the const evaluator makes of the initializer.
  The checker records no pattern type for a top-level binding (it checks one through its declaration), so the lowering
  falls back to the declared result of the symbol.
- **`Instruction.Call` gained `at: LocationId?`**, which 5.3 had named as the one thing that kept `torb_text_slice`,
  `torb_text_repeat` and `torb_list_with_capacity` from being callable at all. The lowering fills it in at every call, the
  text format prints ` at path:line:column` after the arguments, and `PrototypeMatch.WithLocation` now appends
  `TORB_LOCATION(...)` instead of being a finding. The field has a default, so only *patterns* over `Call` had to change.

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

### What the boundary to the operating system decided: UTF-8 above it, the platform's own form below it

**Everything crosses `runtime/platform.c` as UTF-8, and the Windows half converts.** A `String` is UTF-8 and the narrow
half of the Windows API is the code page of the machine, which is a different text: on a machine with code page 1252,
`grüße.txt` handed to `fopen` names a file called `grÃ¼ÃŸe.txt`, and `日本.txt` names one that spells `日` in bytes that
code page reads as three other characters. Where that code page has a character for every byte the mangling is a
bijection, so a program that writes its own files and reads them back never notices - and it is wrong all the same,
because the name on the disk is not the name the program meant: a file `git checkout` wrote cannot be opened, what the
file manager shows is mojibake, and a code page that loses (932 has no character for most byte pairs) fails outright.
So the Windows half calls the
wide API throughout (`_wfopen`, `_wmkdir`, `GetFileAttributesW`, `FindFirstFileW`, `CreateProcessW`, `_wgetenv`,
`GetCurrentDirectoryW`, `DeleteFileW`, `RemoveDirectoryW`), and two helpers convert at the boundary:
`torb_platform_wide` (UTF-8 to UTF-16, with `MB_ERR_INVALID_CHARS`) and `torb_platform_utf8` (back, with
`WC_ERR_INVALID_CHARS`). **A conversion that fails is the answer the call gives for a name that is not there** - there is
no new error kind for text that cannot be a name. The POSIX half converts nothing: a path is bytes there and a UTF-8
`String` is bytes.

**A path is handed over in its extended-length form above the limit and in the plain form below it.**
`torb_platform_system_path` turns every `/` into `\` and, where the fully qualified path is longer than `MAX_PATH - 13`
characters, hands over `\\?\C:\...` (a share becomes `\\?\UNC\server\share\...`), normalised through
`GetFullPathNameW` because that form allows no `.`, no `..` and nothing relative. Not always, because the prefix does not
merely lift a limit: it switches the path off the operating system's own parsing, where `..` and a trailing dot are
literal name parts, `NUL` and `CON` stop naming devices, and a relative path is impossible - which would make every
ordinary path one this file resolved instead of the one the caller wrote. `MAX_PATH - 12` is the threshold because it is
the length a directory may have for files to be creatable inside it, so one number serves a path that is opened and a
path that is created in. The extended form exists inside that one function and reaches no value of a program.

**The program's own arguments come from `GetCommandLineW`.** The `argv` of `main` is not UTF-8 on Windows: the C runtime
builds it from the wide command line through the code page of the machine, which on a 1252 machine turns `ü` into one
byte 0xFC - not UTF-8 at all - and `日` into a question mark, which nothing can undo. `torb_platform_arguments` reads the
wide command line and splits it with `CommandLineToArgvW`, by exactly the rule `torb_append_windows_argument` writes when
it builds a command line for a child; POSIX has no source of its own and `argv` is what `Process.arguments()` answers
there.

**`File.absolutePath` answers one form**: forward slashes, an upper-cased drive letter, `.` and `..` resolved as text,
no prefix of the operating system's own. `torb_file_absolute_path` is the definition, and no platform call gives that
form: the usual one keeps `..` on POSIX, keeps the separators of the platform and spells the drive letter the way the
working directory happens to, and the one that resolves needs the file to exist and answers `\\?\`.

**A directory entry whose name has no UTF-8 spelling is an `IoError`** that names the directory, on both sides: a
`String` is always valid UTF-8, so there is no value for such a name and no replacement character is invented.

**Standard output is raw bytes into a pipe or a file, and no code page is ever set.** `print` writes the UTF-8 bytes of a
`String` as they are, which is what the conformance suite compares through a pipe; `SetConsoleOutputCP` never runs,
because it is a setting of the console window that outlives this process, not one of the program writing to it.

**A live Windows console is read and written as UTF-16 instead.** `runtime/console.c` asks `GetConsoleMode` on the
handle behind each standard stream once, cached for the life of the process, to tell a real console from a pipe or a
file; where it is one, `print` converts the UTF-8 text with `torb_platform_wide` and writes it with `WriteConsoleW` in
chunks that never split a surrogate pair, and `readLine` converts the other way with `ReadConsoleW`. A pipe, a file,
and text that turns out not to be valid UTF-8 all still get the raw bytes exactly as before - the conversion only ever
runs on the one target whose own code page was never going to render a `String`'s bytes correctly.

`tests/conformance/non-ascii-paths.trb`, `long-paths.trb`, `absolute-path-form.trb` and
`process-non-ascii-argument.trb` are what hold the two implementations to all of it, and
`runtime/tests/platform_test.c` is what holds the two conversions and the one threshold; `runtime/tests/console_test.c`
holds the console's own chunk boundary and its fallback, the parts of this that do not need a console to test.

### What generic numbers decided: the value in hand says which instance

A library that is generic over its number type - `std/linear`, `std/geometry` - asks the lowering four questions, and
all four have one answer: **the type the call site really has is what names the instance**, and a declaration that
still holds a parameter names none.

**An operator is dispatched on its operand and not on the implementation that declares it.** `a + b` is `Add.add`, and
the dispatch has two candidates for "which type is this reached on": the target of the implementation the checker
resolved, and the written left operand. For an ordinary `extend Int64 with Add` they are the same type; for
`extend<Scalar: Signed> Vector2<Scalar> with Negate` the target is `Vector2<Scalar>`, which no instance can be built
from, while the operand is `Vector2<Int64>` with the enclosing instance's arguments already in it. So `operandTypeOf`
in `ir/lower/call.trb` prefers the operand wherever the target is not closed, and `a + b` reaches exactly the function
`a.add(b)` reaches - inside a generic body as well, where the scalar is the caller's parameter. `generic-operators.trb`
is the gate. **The VM needs nothing new for it**: the instance set is the one the method form already produced.

**`==` and `<` on a record go through the implementation the language generates.** The checker records a *member* for an
operator only where `lookupMember` finds a declaration, and a generated `equals` is not one - the same hole a tuple's
structural comparison has always been in. `structuralOperator` therefore asks the question a `for` asks about
`iterate` (`dispatchedOn`) for a nominal operand as well as for a tuple, which reaches the body `ir/lower/derive.trb`
writes. Whether the checker should record the member instead is a question of the checker; the lowering answers the
same way either way.

**A `const` of a generic type is one value per type argument.** `Box<Int>.empty` and `Box<String>.empty` are one
declaration and two values, whose lists hold elements of two different types. The caches of `ir/constant.trb` and of
the immortal cell in `ir/lower/expression.trb` are therefore keyed by the **instance** - the declaration plus the
arguments the read decided, exactly as a function instance is keyed - the name of the static and of the cell is built
from those arguments, and the initializer is lowered under the substitution they make. `Resolution.Constant` carries
the type the constant was reached on, which is where the arguments come from; a read that leaves one open is a clean
finding and not an unsubstituted parameter reaching the back end. `generic-constants.trb` is the gate.

**The blanket `Into` builds.** `extend<Source, Target> Source with Into<Target> where Target: From<Source>` declares its
own parameters, so the trait's arguments do not fill them in declaration order: the receiver decides `Source`, and
`Target` comes out of matching the implementation's `with` clause against the bound the call resolved.
`memberMappingOf` in `ir/witness.trb` therefore asks the parameter whether it is the `Self` of a trait, which is
declared in no source, or one an `extend` wrote down, and matches the capability against the bound for the second.
Filling them in the trait's order instead binds `Source` to the trait's argument and leaves `Target` with nothing, and
an instance cannot be built from that. The inner witness the `where` asks for needs no entry of its own: once the two
are bound, the body `Target.from self` is the ordinary static call `Path.from(text)`. The gate is
`binary-only/blanket-into.trb`, for the reason below.

**Two conformance divergences this records**, both of them stage 0's and both of them closing with the VM:

- **Stage 0 cannot resolve `into()` at all.** The target type of the conversion stands nowhere near the call - a
  `String` value in hand says nothing about whether a `Path` or something else is wanted - and an untyped interpreter
  has no other source for it, so it answers ``the String "a/b.txt" has no method `into` ``. It is not a bug that can be
  fixed in the interpreter; it closes when the VM runs the compiler's own instances.
  `tests/conformance/binary-only/blanket-into.trb` pins the compiled side.
- **Stage 0 keeps one cell per `const` declaration and not one per type argument.** It has no types, so
  `Box<Int>.empty` and `Box<String>.empty` are one value there - which is the same cause as `extend Vector2<Float>` and
  `extend Vector2<Int>` being indistinguishable for it (docs/design/LINEAR.md, section 12). `generic-constants.trb` stays a
  program both implementations run because the value it reads is empty either way, and `std/linear` keeps every
  instantiation's constants in a concrete `extend` for the same reason.

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
integer types come `with` (`std/number/src/lib.trb`): they are then ordinary trait-member requirements on the
runtime - one manifest entry per member per width, section 3.7 - and usable as a bound, which a generic hash needs.
`addedWrapping` and `multipliedWrapping` are an `extend UInt64`. One detail the proposal leaves open is decided with
it: **a shift by a negative amount or by the width of the type or more panics,** like every other operation that
leaves its range.

**4. `Show` of a `Float` is unspecified beyond "a decimal point".** Gap 23 of TYPECHECKER.md: "a `Float` always
carries a decimal point". Stage 0 prints Rust's `to_string` (`[3.141592653589793, 6.0, 0.0]` in
`tests/language/basics.expected`), which is the shortest round-tripping decimal.
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
UTF-8: the only ways in are literals, slices at character boundaries, `String.from(Iterate<Char>)` and runtime
functions that validate - so reading a file whose bytes are not UTF-8 is an `IoError`, never a replacement character.
_Reason:_ a total `String` removes a replacement-character rule from every back end and from `chars()`.

_Decision:_ accepted.

**8. Recursion depth is not mentioned, and "tail calls are guaranteed" cannot be kept in general.** Execution Model:
"Tail calls in tail position are guaranteed." Portable C cannot guarantee a general tail call, and the language has
no stack limit at all - while `retry` in `std/core/src/control.trb` is tail recursive and the parser recurses with
the nesting of an expression.
_Proposal:_ guarantee **direct self-recursion in tail position** (the lowering turns it into a jump to the entry
block, which both back ends do identically) and say so; every other call uses the stack. A per-task **frame counter**
with a limit (default 100 000, `--stack-limit`) panics with "stack overflow" - the only portable way to make the VM,
which has its own frame list, and the C binary, which has a C stack, agree on when it happens. _Reason:_ an
unspecified crash is not a semantics, and the guarantee that can be kept is the one that `retry` and every fold need.

_Decision:_ accepted. The counter was never built, and 2026-09-22 replaced it: a function that calls another compares
the stack pointer with a limit worked out once from the real bounds of the stack, because a count cannot keep frames of
unknown size inside a C stack and the comparison costs less (docs/PERFORMANCE.md F15). There is no `--stack-limit`.

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

_Decision:_ accepted. **Superseded on 2026-09-22** by `docs/design/DESTRUCTORS.md`: `close()` is the destructor of a
`shared type`, run by the last release. The three objections are answered there — a moved-out slot is cleared and is its
own drop flag, fields are released in reverse declaration order, and a panic runs no `close()` — and a slot whose type
may contain a `Close` object is released at the end of its scope in reverse declaration order instead of where
section 2 puts other releases.

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
`Script.apply(var value: Value): Result<Void, SandboxError>`. Load-time failures (syntax, types, a module the
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
second-class. They only exist as a `var` parameter or a `var fn` receiver ... They cannot be stored in a field, returned, or
captured by a closure that is stored."
_Proposal:_ the checker records per closure whether it escapes, and a closure may capture a `var` parameter or
the receiver of a `var fn` **only** when it does not: conservatively, when it is written directly as an argument of a call and is not
stored by the callee. Everything else captures values and `Box`es. `ClosureKind.Local | .Escaping` goes into the
tables. _Reason:_ the lowering needs it to choose a stack environment (which is also the optimization that makes the
DSL and the pipelines free), and without it the rule quoted above is unenforced.

_Decision:_ accepted. **Extended on 2026-09-22:** a closure that captures a `var` *binding* follows the same rule — it
may only be `Local` — so "everything else captures values and `Box`es" narrows to values: a `Box` never outlives its
scope. Decided, not yet enforced by the checker (CONCEPT, "`var` Paths and `var` Parameters").

**15. `isSame` is callable on values.** `std/core/src/shared.trb`:
`public native fn isSame<Object>(first: Object, second: Object): Bool`, with no bound. On a value the answer would
expose whether the implementation shares storage.
_Proposal:_ the checker rejects `isSame` on a non-`shared` type, as a named special case next to object safety, with
the message "`isSame` compares identity, and a `Point` is a value. Use `==`." _Reason:_ the language has no bound
that says "shared", and adding one for a single function is worse than one special case in the checker.

_Decision:_ accepted.

**16. `Expression` quoting is not free after all.** Quoted Expressions: "Static data, created at compile time.
Quoting costs nothing at runtime", and `native fn captures(): List<Encode>`.
_Proposal:_ correct the sentence: the **tree** is static data and costs nothing; the captures are collected at the
quotation site into a list of trait-typed values, so quoting a closure with captures costs one small allocation per
evaluation. _Reason:_ `captures()` has to return the current values, which cannot be static, and `assert` is in every
test - the cost should be written down rather than discovered.

_Decision:_ accepted. The Decision Log entry about quoting being free is corrected with it.

**17. `for x in ..10` and `Range.length()` of an open range.** A range with two `Option` ends makes "has a start" a
condition about a field's value, so `iterate()` and `length()` could only panic.
_Proposal:_ **the type says which ends there are**, chosen by the syntax: `a..b` is a `Range`, `a..` a `RangeFrom`,
`..b` a `RangeTo`. No field is optional, `Range<Int>` is `Iterate` and `Length`, `RangeFrom<Int>` is `Iterate` and
endless, `RangeTo<Int>` is neither - so `for index in ..10` and `(0..).length()` are refused where they are written,
each with the reason. What accepts every form takes the trait `Bounds<Value>` (`lowest`, `highest`,
`includesHighest`, `contains` as a default), which is what `Slice.slice` declares.
_Reason:_ a panic in std is only for a caller's mistake that no type can express, and this one a type expresses
(`compiler/CONTRIBUTING.md`, "What panics in std"). `inclusive` stays a `Bool` field: pulling it into the type as well
would give six types for a distinction no body makes.

_Decision:_ accepted. The cost is one box per slice, because `Bounds<Int>` in `Slice.slice` is a trait-typed
parameter: a slice is not in an inner loop, and a generic parameter there would need a type argument at every
`a[from..to]` the checker records.

**18. The manifest of natives is also a list of what does not exist yet.** Decided gap 22 requires that a missing
intrinsic is a compile error. Today `std/` declares `Decimal`, `Float32` arithmetic, `TrieList`/`TrieMap`/`TrieSet`
and all of `std/http` as `native`.
_Proposal:_ `NativeState.Planned(milestone)` (section 3.7): using one is a clean compile error naming the milestone,
never a link error; the trie names are documented aliases of the array and hash implementations until milestone 8,
which is observable only through performance because iteration order and `Show` are the same either way. _Reason:_
the alternative is a binary that fails at link time with a mangled name in the message.

_Decision:_ accepted.

**Risks, not gaps.**

- **Stage 0 has to run the lowering** *(historical: stage 0 is deleted, and the self-hosted compiler runs its own
  lowering)*. It has no type checker, so a lowering bug shows up as a wrong C file, not as
  an error. Mitigation: the IR text snapshots, an IR **verifier** (every slot defined before use, every block
  terminated, every managed slot released on every path) that runs in the debug profile of `torb build` and in the
  tests, and the live-block counter.
- **The instance count.** Monomorphization over `compiler/` plus `std/` with `List<Item>` everywhere can multiply. It
  **does**: the note of 5.7 has the numbers, the exact assertions that notice a regression, and the sharing that would
  bound it.
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
