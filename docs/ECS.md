# Entities, Components and Scenes

**Status: proposed** — there is no `std/ecs` or `std/scene` yet; `examples/ecs-probe` and `examples/ecs-probe-2` are
the probes, and the slices of section 13 wait on the language gaps of section 12.

An open set of component types that **packages bring with them**, dense typed storage, queries that are ordinary
pipelines, a behaviour tree of trait values above the data, and scenes that are TorbScript. This is the specification
of `std/ecs` and `std/scene`, of what they expect from the value packages under them, and of the one thing
they refuse to do: forget the type of a value.

```text
  a package owns its components          a program admits it with one field     two layers, separable

  package acme/sprite                    type World {                           data      Column<Sprite>   plain
    type Sprite                            var places: Places                             pairs<A, B>      a pipeline
    type Sprites                           var sprites: Sprites                 behaviour SceneNode          a trait
      var sprites: Column<Sprite>          var behaviours: Column<SceneNode>              process(delta)    a method
    extend Sprites with Store<Sprite>    }                                      bridge    Column<SceneNode>  one column
    fn render<W: Store<Sprite>>(w: W)
  ──────────────────────────────────────────────────────────────────────────────────────────────────────────────
  the set of components is open          the world is a value, so a snapshot    no reflection anywhere
  because packages declare their own     is a binding and a rollback an         because a value is its constructor
  and bound their systems by Store       assignment                             and openness is a trait, not a cast
```

- **[1. How the engines are built](#1-how-the-engines-are-built)** — Unity, Unity DOTS, Godot, Bevy, flecs, and what
  carries over
- **[2. The decision: an open component set without `Any`](#2-the-decision-an-open-component-set-without-any)** — four
  designs, seven probes, and what each showed
- **[3. The data layer](#3-the-data-layer)** — component, entity, column, columns, and the program's world
- **[4. Queries](#4-queries)** — arity without variadic type parameters, and five systems written out
- **[5. Structural change while a query runs](#5-structural-change-while-a-query-runs)** — and why there is no command
  buffer
- **[6. Systems and scheduling](#6-systems-and-scheduling)** — access sets, stages, the fixed step, and the two kinds
  of parallelism
- **[7. Hierarchy](#7-hierarchy)** — parent as a component, and what `std/transform` owns
- **[8. The behaviour layer](#8-the-behaviour-layer)** — one tree, one system, and where a downcast would have been
- **[9. Scenes](#9-scenes)** — a scene, its loader, instancing, overrides, and the round trip
- **[10. What an editor needs](#10-what-an-editor-needs)**
- **[11. The package cut](#11-the-package-cut)**
- **[12. What the language and the compiler must provide](#12-what-the-language-and-the-compiler-must-provide)**
- **[13. Slices](#13-slices)** — each one lands green
- **[14. What this is not](#14-what-this-is-not)**
- **[15. Open, for the owner](#15-open-for-the-owner)**

Two packages carry the capability claims below. [`examples/ecs-probe`](../examples/ecs-probe) is the storage, five
systems, a structural change during a query, a snapshot, a rollback and a name-to-installer registry.
[`examples/ecs-probe-2`](../examples/ecs-probe-2) is the composition this document decides on: two packages that each
own a component type and its columns, a package system bounded by both, two systems over two packages' disjoint
columns in one call, and a behaviour tree stored in a column. Both check, run and build, and the four designs that
were probed and rejected are recorded with their diagnostics in `examples/ecs-probe-2/README.md`.

**The code blocks of this document are not checked by `docs check`**, because a design document describes a library
that does not exist yet. Every block below was written into a probe package and put through `torb check` by hand, and
a block whose form the checker refuses today names the gap of
[section 12](#12-what-the-language-and-the-compiler-must-provide) that is in its way. This is the convention
[CONCURRENCY.md](CONCURRENCY.md) states in its own preamble.

---

## 1. How the engines are built

Five systems, five answers to the same four questions: what a component is and where it lives, how a system says what
it touches, how a package that the engine has never heard of adds a component type, and what the tree is.

### Unity, the classic object model

A `GameObject` is a name, a `Transform` and a list of `Component` objects; a `MonoBehaviour` is a C# class that the
runtime instantiates, and behaviour and data sit in the same object. `GetComponent<T>()` walks that list and compares
types. The scene file stores a component by its type's name and its fields by reflection, and the editor's inspector
is reflection over the same fields. Messages (`Update`, `OnCollisionEnter`) are found by name at load time.

What it buys is the thing everybody copies: **a package ships a class, the user adds it in the inspector, and nothing
in the engine had to know about it.** What it costs is a managed object and a virtual call per component per frame, a
dictionary probe per `GetComponent`, and a scene format that breaks when a type is renamed. What breaks is that every
missing component is a null reference discovered while the game runs.

### Unity DOTS

A component is a `struct` with `IComponentData`; a system is `SystemBase` or `ISystem`; a query is
`SystemAPI.Query<RefRW<Position>, RefRO<Velocity>>` or an `IJobEntity`, and both of those are rewritten by a **source
generator** into an `EntityQuery` plus a job with a declared dependency. Storage is archetype **chunks** of 16 KiB,
one array per component inside a chunk, so a system walks contiguous memory and Burst vectorises it. Component types
are numbered by a `TypeIndex` that the build assigns.

What it buys is an order of magnitude and more: the boid benchmarks that circulate put a `MonoBehaviour` run at
roughly 8 frames a second against 42 for single-threaded ECS and 165 for ECS with Burst and jobs. What it costs is
that a structural change — adding or removing a component — copies the entity between chunks and invalidates every
array a job is holding, so structural changes go into an `EntityCommandBuffer` and are played back at a sync point.
What breaks is that the safety system is the only thing between a stale chunk array and silent corruption, and that
the source generators are a second compiler nobody can step through.

### Godot

There is no ECS. A `Node` is an object in a tree; the tree *is* the data structure; a node is behaviour and data in
one, and `_ready`, `_process(delta)` and `_physics_process(delta)` are virtual methods the engine calls in tree order.
A scene is a `.tscn` file, a scene can be instanced inside another scene, and an instance stores **only the properties
it overrides**. Signals are a per-object list of connections, groups are a name-to-node multimap, and a node reaches
another by path (`get_node("../Turret") as Sprite2D`).

What it buys is the best editing story on the list: one concept, a text format with clean diffs, and instancing that
is real inheritance of data. What it costs is one object and one virtual call per node per frame, and a `ClassDB` that
registers every class by name. What breaks is the path: `get_node` is a string lookup and a cast, so renaming a node
is a run-time error in a file nobody edited.

### Bevy

A component is a `struct` with `#[derive(Component)]`, and the derive does nothing but mark it; the world hands the
type a `ComponentId` the first time it sees it. Storage is archetype tables of **type-erased columns** — raw bytes
plus a recorded layout and a drop function — keyed by `ComponentId`, with sparse-set storage available per component
type. A system is a plain function whose *parameters are its access set*: `Query<(&Position, &mut Velocity), With<Ship>>`
says what it reads and writes, and the scheduler reads that out of the signature and runs non-conflicting systems at
once. Variadic tuples come from an `all_tuples!` macro that writes the implementations up to arity 16. The hierarchy is
a `Parent`/`Children` pair of components and a propagation system, and required components pull a component's
dependencies in when it is inserted.

What it buys is the composition model to beat: a package is a crate with its components, its systems and a `Plugin`,
and `app.add_plugins(ThatPlugin)` is the whole integration. What it costs is `unsafe` under every query and a
`ComponentId` registry per world. What breaks is nothing at the type level — which is exactly why it is the design to
argue against, rather than around.

### flecs

A component is any C struct, and **a component is an entity**: its id is an entity id, so ids are values and the
whole system is reflective by construction. Queries are a string DSL (`Position, [out] Velocity, (ChildOf, $parent)`)
or a C++ builder. The hierarchy is `(ChildOf, target)` — a **relationship pair**, a first-class id built at run time
out of two entity ids, traversable inside a query with `cascade`.

What it buys is the most expressive query language on the list: relationships, transitivity, traversal, all in the
query engine. What it costs is that the id space is per world and computed while the program runs, and that the query
string is not checked by anything but the query parser. What breaks is that two worlds disagree about what a given id
means.

### The five side by side

| | Unity classic | Unity DOTS | Godot | Bevy | flecs |
|---|---|---|---|---|---|
| **What a component is** | a `MonoBehaviour` object | an `IComponentData` struct | a `Node` subclass: data **and** behaviour | a `#[derive(Component)]` struct | any C struct, which is also an entity |
| **Registered** | never; found by reflection | by a source generator, as a `TypeIndex` | in `ClassDB`, by name | lazily, as a `ComponentId` | explicitly, as an entity id |
| **Stored** | one object per component, a list per `GameObject` | 16 KiB archetype chunks, one array per component | one object per node, owned by its parent | archetype tables of type-erased columns; sparse sets opt in | archetype tables, columns per table |
| **A system names its access** | nowhere; `GetComponent<T>()` anywhere | `RefRW`/`RefRO` in the query, read by a generator | nowhere; a node reaches a sibling by path | in the signature: `Query<(&A, &mut B), With<C>>` | in a query string: `Position, [out] Velocity` |
| **Who acts on it** | nobody | the job dependency graph | nobody | the scheduler, which serialises conflicts | the multithreaded scheduler, over staged worlds |
| **A package adds a component** | ship an assembly; the inspector finds the class | ship an assembly; the baker writes the type | ship an addon; `ClassDB` registers the class | ship a crate with a `Plugin` | declare it at run time |
| **The tree** | `Transform` parenting, dirty flags down | `Parent`/`Child` components, a transform system group | the scene tree itself; a node may *be* a scene | `Parent`/`Children` components, one propagation system | `(ChildOf, target)` as a relationship pair |
| **A scene** | a YAML asset, types named by string, fields by reflection | authoring `GameObject`s baked into a binary subscene | `.tscn`, text, only the overrides stored | `.scn.ron`, needing `Reflect` and a `TypeRegistry` | JSON over `ecs_meta` |
| **The price** | an object and a virtual call per component | a structural change copies an entity between chunks | an object and a virtual call per node | `unsafe` under every query | ids computed at run time |
| **What breaks** | a missing component is a run-time null | a stale chunk array after a structural change | `get_node(path) as T` is a run-time cast | nothing at the type level | two worlds disagree about an id |

### What this language takes, and what it refuses

**Taken.**

- **Bevy's composition model**, as the whole point: a package owns its component types, its systems and its schedule,
  and a program adds it in one line. [Section 2](#2-the-decision-an-open-component-set-without-any) does it without
  the `ComponentId` registry underneath.
- **Bevy's access set as the scheduler's input**, and its change ticks, as a stamp per row and a clock per column.
- **The dense typed column** from EnTT's sparse sets over DOTS's chunks, for a reason about this language rather than
  about a cache: a chunk moves a value with code that has forgotten the value's type.
- **Godot's scene format** wholesale: text, instanced, and only the overrides stored. And Godot's nesting as the
  *syntax* of a scene file, where nesting is the parent relation.
- **Godot's node**, as [the behaviour layer](#8-the-behaviour-layer): one trait, `ready`, `process(delta)`, children,
  and tree order. Its openness is a trait type, which this language has, rather than a `ClassDB`, which it does not.
- **flecs's relationship idea**, as far as it goes without erasure: a hierarchy is a component and a traversal is a
  function.
- **DOTS's enableable components**, as a `Bool` field and a `filter`, which is what they are once a filter is a
  filter.

**Refused, with the reason.**

- **Reflection, in all four forms it takes**: Unity's serializer, Godot's `ClassDB`, Bevy's `Reflect` plus
  `TypeRegistry`, flecs's `ecs_meta`. CONCEPT states it twice — "there is **no runtime reflection**" and "There is no
  'any value' type in the language" — and what reflection is needed for here is done by the constructor, which the
  compiler already knows and already derives three things from.
- **Type erasure in storage.** Bevy's `Column` of bytes with a recorded layout and drop function is the single
  mechanism that everything else on the list rests on, and it is the one this language cannot have.
  [Section 2](#2-the-decision-an-open-component-set-without-any) prices what it would cost.
- **Code generation**: DOTS's source generators, Bevy's `all_tuples!`, flecs's string DSL. A named gap is more honest
  than a macro a language has decided not to have.
- **Archetype chunks**, for the same reason as erasure, and revisitable when `Buffer<Item>` exists and a benchmark
  says so.
- **The command buffer**, which [section 5](#5-structural-change-while-a-query-runs) replaces with the evaluation rule
  of `for`.
- **Resources**, which exist because a system is handed the world and nothing else; here a singleton is a field.
- **Signals as connections and observers as callbacks.** A callback stored in a node is a function value in a field,
  which makes the node neither comparable nor showable and unwritable to a scene file.
  [Section 8](#8-the-behaviour-layer) does what signals do with events.
- **Node paths.** `get_node("../Turret") as Sprite2D` is a string lookup and a cast; a typed field is both, checked.

## 2. The decision: an open component set without `Any`

The requirement is Bevy's and Unity's: **a package brings its own component types, and a program that has never heard
of them can use the package's systems.** The question is whether that needs a value whose type the storage has
forgotten.

Four designs were written out and probed. The probes are in `examples/ecs-probe-2/README.md` with their exact
diagnostics; what each one showed is below.

### (a) A type-keyed store, with the cast confined to `std/ecs`

The shape everybody uses: `World` holds `Map<ComponentKey, ColumnStorage>` where `ColumnStorage` is a trait, and
`column<Component>()` takes the column back out by casting. The argument for it is real — the key and the type are
bound together in exactly one place, so it is a *keyed heterogeneous map where the key carries the type*, Rust's
`TypeMap`, and not a general `Any`.

**The key half exists today.** A trait that requires a per-type identity, reached through a type parameter, checks and
runs:

```trb fragment
public trait Component {
  static fn key(): ComponentKey
}

fn keyOf<Subject>(): ComponentKey where Subject: Component {
  Subject.key()
}
```

`print keyOf<Position>()` answers `ComponentKey(name: "probe/Position")`, natively.

**The cast half is closed, and closed well.** The checker refuses to hand a trait-typed value back as a type
parameter:

```text
error: Expected `Column<Component>`, found `ColumnStorage`
  --> src/main.trb:44:27
   |
44 |       Some(found) => Some found
   |                           ^^^^^
```

and refuses the three-line form with nothing about an ECS in it:

```text
error: Expected `Value`, found `Area`
   = A type parameter is opaque inside the body that declares it: it stands for one type the call site chose, and
     a trait-typed value is any of them. Take the value as the parameter (`value: Value`) or answer the trait
```

This is a change of fact from the first version of this document, which recorded the same two lines as *accepted* and
called the route a checker bug wearing a design's clothes. The bug is fixed, the diagnostic names the rule, and the
route is shut.

**What opening it would cost.** Two rules of the language, not one:

1. **A run-time identity per type, comparable as a value.** `Component.key()` above is written by the programmer and
   is therefore not an identity — two packages can write the same string. A trustworthy key has to be granted by the
   compiler, which is `typeof` under another name, and CONCEPT's first sentence about types is that "A type never
   flows as a value."
2. **A checked cast from a trait type to a type parameter.** That is the one coercion the language does not have, and
   it must run in both back ends identically, which means every binary carries a per-type tag for every component
   type whether or not anything casts it.

**What it would buy.** Exactly one thing: a program would not have to name the columns it stores. Under (b) that is
**one field per package**. Two rules of the language for one field is not a trade; it is a capitulation.

There is a third cost that is easy to miss. A `Column<Component>` taken out of a map by value is a *copy*, and putting
it back is a second one. Every `attach` would be a probe, a cast, a copy out and a copy back, where the composed
design writes one slot. The erased design is not only broader, it is slower at the thing an ECS does most.

### (b) The world composed from packages, statically

**Decided.** A package owns three things: its component types, its **columns** — one `Column<Component>` per
component the package owns — and its systems, each bounded by the components it touches.

```trb fragment
// package acme/sprite
public type Sprite {
  texture: Handle<Image>
  layer: Int = 0
}

public type Sprites {
  var sprites: Column<Sprite> = Column()
}

extend Sprites with Store<Sprite> {
  fn column(): Column<Sprite> {
    sprites
  }

  var fn attach(entity: Entity, value: Sprite) {
    sprites.put entity, value
  }

  var fn detach(entity: Entity) {
    sprites.remove entity
  }
}
```

A program admits the package with one field, and a package system names bounds rather than a world:

```trb fragment
public type World {
  var places: Places = Places()
  var sprites: Sprites = Sprites()
}

public fn render<Space, Art>(space: Space, art: Art) where Space: Store<GlobalTransform2>, Art: Store<Sprite>
```

**This works today, end to end.** `examples/ecs-probe-2` is two packages, each with its own component types and
columns, a system `movement<Space, Motion>` generic over `Store<Position>` and `Store<Velocity>`, and a program that
names the two packages' columns and nothing else. It checks, runs and builds, and the snapshot and the rollback hold:

```text
moved: Some(GlobalPosition(value: Vector2(x: 0.5, y: 0.0))) Some(GlobalPosition(value: Vector2(x: 0.75, y: 0.0)))
after the despawn: 1, in the snapshot: 2
after the rollback: 2
```

Nothing is erased, everything is monomorphized, there is no map lookup and no witness table in a hot loop, and the
world is a value.

**The one wall, and it is a mangling wall.** A world that carried `Store<Position>` *and* `Store<Velocity>` would let
a package system take one world instead of one group per package. The checker resolves that correctly in both
directions — by the argument type for `world.attach entity, Position(…)` and by the expected type for
`const column: Column<Velocity> = world.column()`, both confirmed with a negative probe — and the back end then
answers:

```text
internal error: the generated C did not compile. This is a bug in torb, please report it.
  gcc: error: conflicting types for 't_..._World_attach'; have 'void(T_..._World *, T_..._Velocity)'
  gcc: note: previous declaration of 't_..._World_attach' with type 'void(T_..._World *, T_..._Position)'
```

`World.attach` carried no arguments of its own in the mangled name. That was
[gap 1](#12-what-the-language-and-the-compiler-must-provide), and it is closed: the name of a member of an `extend` now
carries the applied trait, so one world can carry `Store<Position>` and `Store<Velocity>` at once.

**The form this design is *not*.** The obvious reading of "a world generic over a component list" is
`World<(Position, Velocity, Sprite)>` with a bound `Has<Position>` over the tuple. That was probed and it is dead
twice over. The world type and the tuple argument check and run; a system
`fn describe<Components>(world: World<Components>) where Components: Has<Position>` checks; and the implementation
does not hold:

```text
error: `(Position, Velocity)` does not implement `Has<Position>`
    = `describe` asks for it
```

`extend (Position, Velocity) with Has<Position>` is accepted as a declaration and is not found when a bound asks for
it, while the same `extend` on a named type is found ([gap 13](#12-what-the-language-and-the-compiler-must-provide)).
And even with it, one `extend` per tuple shape is not a design; the blanket that would replace it needs a pack, which
is five parse errors starting with *"Expected a name, found `...`"*. **The named form is better anyway**: it forces no
order on the list, it lets `std/ecs` stay out of the world type, and a package group is a name a human reads instead
of a position in a tuple.

### (c) Components as cases of one sum type

Rejected in three lines, which is all it deserves. A `type Component { case Position(…) case Velocity(…) }` is what
[`examples/game-engine`](../examples/game-engine) does, and **a package cannot add a case**: cases belong to the
declaration of the type, because exhaustiveness and the generated constructor have to be decidable from it alone. A
program would edit an engine type to add its own component, which is the opposite of the requirement.

### (d) Godot's way, typed

**Decided, for the behaviour layer.** Nodes are values in a tree, `SceneNode` is a trait, each package's node type
implements it, and a tree is a `List<SceneNode>` or a field of a node. Trait types are the openness, and the language
has them: a trait can be used as a type, a trait-typed value carries a witness table per bound, and a `List<Show &
Hash>` is legal.

**It works today, including in-place mutation through a trait-typed element.** The probe is a `Ship` whose children
are `List<SceneNode>`, mutated through `mounted[index].process(elapsed)`, held in a `List<SceneNode>` and stepped
through `tree[0].process(0.5)`:

```text
["ship", "hull", "turret"]
ship at 1.0
["hull frame 1 at 0.5", "turret frame 1 at 0.75"]
```

**Where a downcast would have been.** Godot writes `get_node("../Turret") as Sprite2D`, and `tree.each<Sprite>()`
would need exactly the cast of (a). It is not needed, and the replacement is not a workaround:

- **A parent reaches a child it cares about through a field of its own type**, typed and checked
  (`var turret: Turret`), rather than through a path and a cast. That is the same information, minus the run-time
  failure.
- **The changing walk is written by the node**, because the node knows its own children by name. `var fn process` on
  `Ship` calls `process` on its own fields and on its `List<SceneNode>`.
- **The reading walk is generic**, over `fn children(): List<SceneNode>`, and is written once for every node type
  there will ever be.
- **Everything cross-cutting goes through the columns**, not through the tree. A renderer does not ask the tree which
  nodes are sprites; it runs a query over `Column<Sprite>`.

So (d) sits exactly where Bevy puts the tree: the behaviour is a node, the data is a component, and the two meet in a
column. [Section 8](#8-the-behaviour-layer) is the design.

### The decision in one paragraph

**The data layer is (b): the program's own world type, composed from the columns that packages ship, with every
system bounded by `Store<Component>` for the components it touches.** Nothing in it is erased, everything in it is
monomorphized, and a world is a value. **The behaviour layer is (d): a `SceneNode` trait whose implementations
packages bring, a tree of values, and dispatch through a witness table.** The two meet in `Column<SceneNode>` — a
column whose component type is a trait — which the probe runs. **(a) is refused** because it costs two rules of the
language and buys one field per package, and because the checker already closed it. **(c) is refused** because a
package cannot add a case.

The prior this document was written to test held, with one correction: the data layer does **not** need variadic type
parameters or a type list to let packages add components. It needs one name per package group and one fix in the
mangler.

## 3. The data layer

### A component type is an ordinary `type`

It carries no trait, it registers nothing, and it knows nothing about a world:

```trb fragment
public type Position {
  value: Vector2
}

public type Velocity {
  value: Vector2
}
```

The set is open because **packages declare their own**, and a program composes them. There is no registration step and
no closed sum.

### `Entity`

```trb fragment
/** What components are attached to. */
public type Entity with Equals, Hash, Show {
  index: Int
  /** Counts up every time `index` is handed out again, so a stale handle is detectable. */
  generation: Int = 0
}
```

The generation is not optional. Without it an entity that was despawned and whose index was reused answers somebody
else's component, silently, and a system that kept a handle for one frame is a bug that reproduces once a week. With
it `column.get(entity)` answers `None`, which is what the caller already has to handle.

### What identifies a component type at run time

`componentKey<Component>()`, a compile-time constant:

```trb fragment
/** The name a scene file, an inspector and a schedule know a component type by. */
public type ComponentKey with Equals, Hash, Show {
  name: String
}

public fn componentKey<Component>(): ComponentKey
```

It folds to `typeName<Component>()` — the declaration's qualified name, `"acme/game/Position"` — which
[ENCODING.md](ENCODING.md) already needs and which the checker answers `Cannot find `typeName` here` for today
([gap 6](#12-what-the-language-and-the-compiler-must-provide)). A key is used for **naming**: the word a scene file
writes, the label an inspector shows, the entry in a schedule's access set, the text of a diagnostic. It is never used
to find storage, and there is no `ComponentKey` to type back. A trait member (`static fn key(): ComponentKey`) is the
stopgap that works today, and it is a stopgap because two packages can write one string.

### Storage: dense columns with a sparse index

```trb fragment
public type Column<Component> {
  private(var) owners: List<Entity> = []
  private(var) values: List<Component> = []
  private(var) rows: Map<Entity, Int> = [:]
  private(var) stamps: List<Int> = []
  private(var) clock: Int = 0
}
```

`values` is dense and in insertion order, so a query walks it without a gap; `rows` answers "does this entity have
one"; `remove` swaps the last row into the hole so neither half ever shifts. This is EnTT's model, and it is chosen
over archetypes for one reason that is about this language and not about performance:

**An archetype table has to move a value between tables when a component is added, and the code that moves it is code
that does not know the type of what it moves.** In Bevy that is `ptr::copy_nonoverlapping` over a `ComponentInfo` with
a recorded layout and a drop function; here it would be the erasure of
[section 2](#2-the-decision-an-open-component-set-without-any). A dense column moves a value only *within its own
column*, so every move happens inside one monomorphized body that knows exactly what it is holding. The storage model
follows from "nothing is erased", not from a benchmark.

Three more things the choice buys: attaching and detaching is O(1) and disturbs no other column, which is what an ECS
with many rarely-used tag components wants; there is no archetype explosion; and a query over two columns is a walk
plus a probe, which is an ordinary pipeline rather than a table matcher.

What it costs is the thing archetypes are good at: a query over *many* columns probes once per column per row, where
an archetype iterates one contiguous block. That is the trade, it is written down here, and it is revisitable the day
the language can move a value it cannot name.

### Columns, and what a package ships

```trb fragment
/** What a package asks of a world: a column of one component type the package owns. */
public trait Store<Component> {
  fn column(): Column<Component>
  var fn attach(entity: Entity, value: Component)
  var fn detach(entity: Entity)
}
```

A package's **columns** are an ordinary type with one `Column<Component>` field per component type the package owns,
and one `Store<Component>` implementation per field. It is the unit a program names, the unit a system takes, and the
unit a schedule hands to a worker.

**The price is three members per component type**, mechanical and identical every time. It is the price until the
compiler derives them from the fields, under exactly the rule `Encode` uses ("the constructor is usable from outside"):
a field of type `Column<Component>` in a type that says `with Columns` is an implementation of `Store<Component>`. A
second derivation removes the rest: a field whose *type* carries `Store<Component>` lends it upward, so a program's
world carries every bound its columns carry and a system can take one world.
[Gap 12](#12-what-the-language-and-the-compiler-must-provide) is both derivations, and
[gap 1](#12-what-the-language-and-the-compiler-must-provide) is what has to land first for a single type to carry more
than one of them.

### A world is a value

`type World`, not `shared type World`. Copying it is a copy of the value, and the compiled probe shows the two are
independent:

```trb fragment
const snapshot = world
world.despawn ship
print "after the despawn: {world.places.positions.length()}, in the snapshot: {snapshot.places.positions.length()}"

world = snapshot
```

```text
after the despawn: 1, in the snapshot: 2
after the rollback: 2
```

So a snapshot is a binding and a rollback is an assignment. Replay, undo, rollback netcode and "what did the world look
like three frames ago" are a `List<World>` and need no mechanism at all. **What a copy costs is not measured here**:
the execution model of CONCEPT says the storage of a `List` and a `Map` is shared until somebody writes to it, so the
expectation is one refcount bump per column and a copy of exactly the columns a frame changed. That is an expectation
and not a claim; nothing in this repository measures it yet.

A `shared type` world would give none of this, and it would confine the world to one task
([Identity](../CONCEPT.md#identity-shared-type)). The value is the right choice and the snapshot is the reason.

### Determinism

`Fixed` from `std/linear` makes every coordinate bit-identical across machines and back ends. For a lockstep
simulation the ECS has to add the second half: **iteration order must not depend on a hash or on an address.** Two
rules do that, and both hold for the storage above.

- A query iterates the **column**, whose order is insertion order, and probes the index. It never iterates the index.
- `remove` is a swap with the last row, which is deterministic given the same sequence of operations — and the same
  sequence is what lockstep means.

The language helps: `Map` and `Set` iterate in insertion order in every implementation, which is a rule of the language
rather than of an implementation. So a world of `Vector2<Fixed>` components stepped by the same inputs produces the
same bytes on every machine, and the `Show` of a world is a usable checksum.

### What the design needs from `Buffer<Item>`

A column is three `List`s today, which is correct and is not the shape a frame budget wants. The planned heap kernel
has to give three things before the columns move onto it:

1. **A guaranteed in-place write when there is one owner.** A system that writes a column of ten thousand positions
   must not copy it. `List` promises copy-on-write, which is the right *semantics* and says nothing about when the
   copy happens; `Buffer<Item>` has to promise that a write through a `var` path whose storage has one owner is in
   place. This is the same promise `std/tensor` needs.
2. **Two disjoint `var` windows into one buffer**, which is what data parallelism inside one system needs
   ([section 6](#6-systems-and-scheduling)) and what a data-parallel loop over a tensor needs.
   [CONCURRENCY.md](CONCURRENCY.md) section 6 specifies them as `windows` plus `Window<Item>`.
3. **`swapRemove` without a shift**, so the dense half of a column stays O(1) to remove from without three `removeAt`
   calls that each shift a tail.

`Array<Item, const Size: Int>` is not the answer here — a column grows — and it does not run either: a literal that
fills an `Array<Int, 4>` answers *"a literal that fills an `Array` of 4 items is not supported by the native back end
yet"*.

## 4. Queries

A query answers an `Iterate` of tuples. That is the whole shape:

```trb fragment
public fn query<Space, Component>(space: Space): Iterate<(Entity, Component)>
public fn pairs<Space, First, Second>(space: Space): Iterate<(Entity, First, Second)>
public fn triples<Space, First, Second, Third>(space: Space): Iterate<(Entity, First, Second, Third)>
public fn quadruples<Space, First, Second, Third, Fourth>(space: Space): Iterate<(Entity, First, Second, Third, Fourth)>
```

### Four arities, because variadic type parameters are the honest answer and do not exist

`fn tuples<...Components>()` is five parse errors, the first *"Expected a name, found `...`"*. And there is no way
around it: the language has no macros by design, `Array`'s const parameters have no arithmetic, and a trait cannot
abstract over a type constructor. Every system on the research list solves this with code generation — Bevy's
`all_tuples!`, DOTS's source generators, flecs's string DSL — and this language has decided against all three.

So **the honest answer is a variadic type parameter, and the smallest form of it is a pack that expands in exactly two
places**: as the element list of a tuple type, and as the subject of a bound.

```trb fragment
public fn query<Space, ...Components>(space: Space): Iterate<(Entity, ...Components)>
  where Space: Store<...Components> {
```

No indexing into the pack, no arithmetic on its length, no mapping a type function over it — the same restriction
const parameters already live under, and enough for every query anybody writes. It would also serve `all(taskA, taskB)`
and `zip`, which are the other two places the standard library writes one function four times.

Until it exists there are four names: `query`, `pairs`, `triples`, `quadruples` — readable at the call site
(`for (entity, position, velocity) in pairs(space, motion)`), with no digit to parse the way `query2` needed one, and
the day the pack lands three of the four disappear anyway and `query` stays. Four is where it stops: a system that
reads five component types is a system that wants two queries. **This is an ergonomic gap and not a structural
one** — nothing about packages bringing components depends on it.

### Reading and writing

**A query answers values, and a write goes back through the group.** Not `var` in the pattern: a `for` variable is a
`const` by the language's rule, and a closure cannot bind a `var` parameter at all
([gap 4](#12-what-the-language-and-the-compiler-must-provide)). And not a `Write<Position>` marker either: that marker
exists in Bevy so the *scheduler* can read the access set out of the signature, and
[section 6](#6-systems-and-scheduling) declares the access set instead, so the marker would be decoration.

```trb fragment
public fn movement<Space, Motion>(var space: Space, motion: Motion, elapsed: Duration)
  where Space: Store<Position>, Motion: Store<Velocity> {
  for (entity, position, velocity) in pairs(space, motion) {
    space.attach entity, Position(position.value + velocity.value * elapsed.seconds())
  }
}
```

That is one slot write per row into a dense column. Where a system changes many components of one entity it writes
them one at a time, which reads better than a batched form would.

In-place mutation without a write-back does exist and the compiled probe uses it, but only from **inside** the type
that owns the column, because `private(var)` hands outsiders a read-only path:

```trb fragment
extend Column<Position> {
  var fn slide() {
    for index in 0..values.length() {
      values[index].x = values[index].x + 1.0
    }
  }
}
```

`values[index].x = …` is a `var` path through a field and an index, and it compiles and runs. What does not exist is
the shape that would let a *caller* do it: `column.each { var position => … }` answers *"A binding needs a value:
there are no uninitialized bindings and no default values"*. Passing a named `fn` with a `var` parameter works, and a
named function cannot capture, so a system that needs the frame time cannot use one. That is
[gap 4](#12-what-the-language-and-the-compiler-must-provide).

### Filters

**An ECS filter is a filter.** A query answers an `Iterate`, so the language's own vocabulary is the whole story:

| What an ECS calls it | What is written |
|----------------------|-----------------|
| `With<Other>` | `triples(space, motion, other)` — the join *is* the with-filter |
| `Without<Other>` | `pairs(space, motion).filter({ !other.column().has(_.0) })` |
| `Changed<A>` | `space.column().changedSince(stamp)` |
| `Added<A>` | the same, against the stamp the entity's row got when it was created |
| an enableable component | a `Bool` field of the component, tested in a `filter` — no structural change, which is what DOTS added enableable components for |

Change detection is a stamp per row and a clock per column, which is Bevy's mechanism without the macro:

```trb fragment
/** The entities whose value was written after `stamp`. */
fn changedSince(stamp: Int): Iterate<Entity>
```

The probe runs it: `changed since the frame began: 1`.

### Five systems, as a user writes them

**Movement**, across two packages' components.

```trb fragment
public fn movement<Space, Motion>(var space: Space, motion: Motion, elapsed: Duration)
  where Space: Store<Position>, Motion: Store<Velocity> {
  for (entity, position, velocity) in pairs(space, motion) {
    space.attach entity, Position(position.value + velocity.value * elapsed.seconds())
  }
}
```

**Collision pairs.** Each pair once, over the smaller column.

```trb fragment
fn collisions<Space, Bodies>(space: Space, bodies: Bodies): List<(Entity, Entity)>
  where Space: Store<GlobalPosition>, Bodies: Store<Collider> {
  const all = pairs(space, bodies).toList()
  var touching: List<(Entity, Entity)> = []
  for first in 0..all.length() {
    for second in (first + 1)..all.length() {
      const (here, hereAt, hereReach) = all[first]
      const (there, thereAt, thereReach) = all[second]
      const reach = hereReach.radius + thereReach.radius
      if hereAt.value.distanceSquaredTo(thereAt.value) <= reach * reach {
        touching.append((here, there))
      }
    }
  }
  touching
}
```

**Parent-child transform propagation.** One pass, in depth order ([section 7](#7-hierarchy)).

```trb fragment
var fn propagate() {
  for entity in depthOrder() {
    const local = positions.get(entity)?.value ?? Vector2.zero
    const base = match parents.get(entity) {
      Some(parent) => globals.get(parent.entity)?.value ?? Vector2.zero
      None => Vector2.zero
    }
    globals.put entity, GlobalPosition(base + local)
  }
}
```

**Spawning bullets**, while the query over the shooters is running.

```trb fragment
fn fire<Space, Motion>(var space: Space, var motion: Motion, var entities: Entities)
  where Space: Store<Position>, Motion: Store<Velocity> & Store<Lifetime> {
  for (_shooter, position, velocity) in pairs(space, motion) {
    const bullet = entities.spawn()
    space.attach bullet, position
    motion.attach bullet, Velocity(velocity.value * 4.0)
    motion.attach bullet, Lifetime(0.05.seconds())
  }
}
```

**Despawning on lifetime**, while the query over the lifetimes is running.

```trb fragment
fn expire<Motion>(var motion: Motion, elapsed: Duration) where Motion: Store<Lifetime> {
  for (entity, lifetime) in query(motion) {
    const remaining = lifetime.remaining - elapsed
    if remaining <= Duration.zero {
      motion.detach entity
    } else {
      motion.attach entity, Lifetime(remaining)
    }
  }
}
```

Both of the last two change the *structure* while a query over it is running, and neither needs anything for it. That
is the next section. `fire` also shows the shape of gap 1: `Motion: Store<Velocity> & Store<Lifetime>` is one type
carrying two `Store`s, which the checker resolves and the mangler does not.

## 5. Structural change while a query runs

**There is no command buffer, and value semantics is the reason.**

The rule is one line of CONCEPT, about `for` and nothing else: *"The subject is evaluated once, into a temporary, so
it is not an open `var` access: changing `xs` inside of the loop is safe and does not affect the loop."* A query's
subject is a group, so the loop walks a **copy** of it and every spawn, despawn and attach inside the loop lands in the
live one. The iteration cannot be invalidated, because what it iterates is not what is being changed.

The compiled probe shows both directions:

```text
moving before: 1, entities with a velocity after: 2      // `fire` spawned while the query ran
after expiring: 1, in the snapshot: 2                    // `expire` despawned while the query ran
```

What every other system on the list needs for this, and what it costs them:

| System | Mechanism | What it costs |
|--------|-----------|---------------|
| Bevy | `Commands` into a `CommandQueue`, applied at a sync point | a spawn is not visible until the next stage, so a system pair that spawns and reads has to be ordered by hand |
| Unity DOTS | `EntityCommandBuffer`, played back at a sync point; a structural change invalidates every chunk array | the same, plus a safety system that has to detect the invalidation |
| flecs | `ecs_defer_begin`/`ecs_defer_end` around every iteration | a deferred `set` reads back the old value inside the same loop |
| EnTT | documented per operation: removing the current entity's component is safe, others are not | the rule is in prose and the compiler does not hold anybody to it |
| Godot | free — the tree is the data structure, and a node may be removed while it is walked | `queue_free` defers it anyway, because a listener may still hold the node |

Here the change is immediate and visible the moment it is written, and the *query* is the thing that is stale. That is
the better default: a system that wants to see its own spawns runs a second query, which is one line and says so, while
a system that must not see them — every physics pass — gets the guarantee for free.

**The semantics are a snapshot, and the cost is a copy that nobody has measured.** A query holds a copy of a group for
its duration, which under the storage rule of the execution model is a refcount bump per column, and a write during the
loop then makes that column's storage unshared and copies it once. For a world whose columns are large and whose
systems all write, that is one copy per column per frame. It is the one place where this design could be too
expensive, it is exactly what `Buffer<Item>`'s in-place promise is for, and it is written down here rather than
discovered later.

## 6. Systems and scheduling

**A system is an ordinary function.** Nothing marks it, nothing registers it, and it can be called directly in a test.
A package system takes the groups it touches, under bounds:

```trb fragment
public fn movement<Space, Motion>(var space: Space, motion: Motion, elapsed: Duration)
  where Space: Store<Position>, Motion: Store<Velocity>
```

### The access set is declared, because it cannot be read

Bevy reads a system's access set out of its parameter types, because a Bevy system takes `Query<(&A, &mut B)>` and the
types *are* the access set. Here a system takes whole groups, so its signature says which groups but not which
columns, and there is no reflection to look inside the body with. The access set is therefore a value next to the
function:

```trb fragment
/** One step of a schedule: what it does, and what it touches. */
public type System<World> {
  name: String
  reads: Set<ComponentKey> = Set.of()
  writes: Set<ComponentKey> = Set.of()
  run: (var world: World, elapsed: Duration) => Void
}

const movement = System<World>(
  "movement",
  reads: Set.of(componentKey<Velocity>()),
  writes: Set.of(componentKey<Position>()),
  run: moveEverything,
)
```

**This is a promise the language does not check**, and saying so plainly is better than a marker type that looks like a
proof and is not one. What the declaration buys is real and limited: a deterministic order, a diagnostic when two
systems in one stage write the same key, and the input a scheduler needs.

### The two kinds of parallelism, and which one an ECS wants

[CONCURRENCY.md](CONCURRENCY.md) section 6 builds **data parallelism inside one system**: one `Buffer` split into
disjoint `Window`s, one body per window, over `Item: Plain`. Its section 15, question 7, asks whether the **field
case** — two *different* systems over disjoint parts of one world, at the same time — is wanted at all. This design
answers it.

**It is wanted, and it is the smaller half of the two.** The reasons are three, and they are specific to an ECS:

1. **A system reads several columns, so a window over one column is not its unit of work.** `movement` reads a
   `Column<Velocity>` and reads and writes a `Column<Position>`; splitting either one into windows splits nothing the
   system can use, because row *n* of one column has nothing to do with row *n* of the other. The unit of work an ECS
   schedule has is a **system over a group**.
2. **The elements are not all `Plain`.** A window carries `Item: Plain`, and a component that holds a `Handle`, a
   `String` or a behaviour is not. So the window split serves the numeric columns and leaves the rest sequential,
   while the field case parallelises every system a schedule holds.
3. **The proof already exists, and it is the call site's.** Two `var` arguments naming two different fields of one
   value are accepted, and a path and a prefix of it are rejected. That is not a plan; the checker does it:

   ```text
   error: `world.places` is being changed by `both` right now
       = While a `var` access runs, the same path cannot be reached a second time
   ```

**So the field case is not a new proof, it is a new runtime.** The call shape works today — `examples/ecs-probe-2`
runs two systems over two disjoint groups through one function with two `var` parameters, and the aliasing form is
rejected:

```trb fragment
public fn both<First, Second>(
  var first: First,
  var second: Second,
  run: (var subject: First) => Void,
  alongside: (var subject: Second) => Void,
) {
  run first
  alongside second
}
```

```trb fragment
both world.places, world.motions, settle, halve
```

What is missing is that `run` and `alongside` run on the same worker. The fix is the fork-join barrier
[CONCURRENCY.md](CONCURRENCY.md) gap 7 already needs for `windows`, called with several `var` parameters instead of
several windows of one. **[Gap 8](#12-what-the-language-and-the-compiler-must-provide) therefore narrows** from "a
form that splits one `var` path into several provably disjoint ones" to "a fixed-arity fork-join call whose subjects
are several `var` paths the checker has already proved disjoint" — and the two parallelisms stay separate, because
they answer different questions.

Until the barrier exists, `Schedule.run` is a loop over the systems of a stage, in declared order, and the access sets
are checked for a conflict and otherwise only documented.

### Stages and the fixed step

```trb fragment
public type Schedule<World> {
  private(var) stages: List<Stage<World>> = []
  private(var) accumulated: Duration = Duration.zero
  var step: Duration = Duration.milliseconds(16)
}
```

Four stages, and a program may add its own: `Input`, `Fixed`, `Update`, `Late`. The fixed stage runs zero or more
whole times per frame out of an accumulator, and the accumulator is a `Duration` and not a `Float` — `Duration` is an
exact count, so an hour of frames cannot drift the way a repeatedly-added `1.0 / 60.0` does. A simulation that has to
be bit-identical runs its geometry over `Fixed` from `std/linear`; the schedule's job is only to hand it the same
number of steps on every machine, which an exact accumulator does.

### Resources and events

**There are no resources.** A resource exists in Bevy and flecs because a system is handed the world and nothing else,
so a singleton needs somewhere to live. Here a package ships a group, and a singleton is a field of it:

```trb fragment
public type Motions {
  var velocities: Column<Velocity> = Column()
  var gravity: Vector2 = Vector2(0.0, -9.81)
}
```

One subsystem fewer, no `Res<T>`/`ResMut<T>`, no type-keyed map, and the access set names a field instead of a
component key where it has to.

**An event is a column that a system drains.** `Events<Event>` is a list with two buffers and a read cursor per
consumer, exactly Bevy's shape, and it is an ordinary value in a field of a group. There are no signals and no
observers: a callback stored in a world would be a function value in a field, which the language allows and which
would make a world neither comparable nor showable, and a scene file that connected one would need to name a function
the host owns. What Godot's signals do is done here by a system that reads the events another system wrote, and
[section 8](#8-the-behaviour-layer) says what that looks like from a node.

## 7. Hierarchy

**Parent is a component, children are derived, and depth order is the ECS's one job.**

```trb fragment
/** Whose space this entity's transform is written in. */
public type Parent {
  entity: Entity
}
```

Not a relationship pair as in flecs: a pair `(ChildOf, target)` is a first-class *id* built at run time out of two
entity ids, which means a query engine that matches on ids, which means storage keyed by something other than a type —
the erasure of [section 2](#2-the-decision-an-open-component-set-without-any) again, one level up. flecs's
relationships are the best idea in the research and they need exactly the machinery this language refuses to build.

Not a built-in tree either: a tree in the world would be a second structure to keep in step with the columns, and a
despawn would have to find every node that names the despawned one anyway.

So: one column of `Parent`, and the group that owns it computes the order.

```trb fragment
/** Every entity with a transform, every parent in front of its children. */
fn depthOrder(): List<Entity>
```

The probe runs it and one pass is enough: a child reads a global its parent has already written. Propagation itself is
one system and it belongs to `std/transform`'s vocabulary, not to the ECS's: the ECS owns the *order*, `std/transform`
owns `Transform2`/`Transform3`, their composition, and the coordinate spaces that make `composed(with:)` type-safe.
`GlobalTransform2` is a separate component and not a field of `Transform2`, so that "what the author set" and "what the
frame computed" cannot be confused and a system that only reads globals declares only that.

A cycle in the parent column is a bug, and `depthOrder` panics on one with the entity in the message. It cannot be
prevented by the type system and it must not be silently broken into a forest.

## 8. The behaviour layer

The owner's second requirement: **a behaviour system on top that brings everything back into one tree and one
system.** That is Godot's shape, and this is Godot's shape with the two things Godot pays for removed — the reflective
class registry and the run-time cast.

### One trait

```trb fragment
/** A node is a value. A tree is a value. Dispatch is a witness-table call. */
public trait SceneNode {
  fn name(): String
  var fn ready(var world: World)
  var fn process(var world: World, elapsed: Duration)
  fn children(): List<SceneNode>
}
```

A package declares its node types and says `with SceneNode`. That is the whole registration, and it is checked: there
is no `ClassDB`, no name lookup, and no class that exists only in a string.

### One tree

The tree is a value, held wherever the program wants it, and the probe runs a heterogeneous one — a `Ship` whose
children are `List<SceneNode>`, holding two `Sprite`s — walked, dispatched and **changed in place**:

```text
["ship", "hull", "turret"]
ship at 1.0
["hull frame 1 at 0.5", "turret frame 1 at 0.75"]
```

The two walks are different on purpose:

- **The changing walk is written by the node.** `var fn process` calls `process` on the node's own fields, because a
  node knows its own children by name. `mounted[index].process(elapsed)` is a `var` path through an index into a
  trait-typed element, and it compiles and runs.
- **The reading walk is written once**, over `fn children(): List<SceneNode>`, for every node type there will ever be.

**There is no `node as Sprite`.** A parent that needs a child of a type it cares about holds it in a field of that
type (`var turret: Turret`), which is the same information Godot puts in a path, minus the run-time failure. This is
the one place where the design is strictly better than the engine it is taken from.

### One system, and how it meets the columns

**A behaviour is a component whose type is a trait.** `Column<SceneNode>` works — the probe stores a `Ship` and a
`Sprite` in one column, steps both and reads both back:

```trb fragment
/** One frame of the behaviour layer: a system of the data layer, like any other. */
public fn step(var behaviours: Column<SceneNode>, var world: World, elapsed: Duration) {
  for entity in world.places.depthOrder() {
    behaviours.run entity, world, elapsed
  }
}
```

So:

- **The tree order is the `Parent` column's depth order**, computed by [section 7](#7-hierarchy). A node does not have
  to hold its children to be in the tree; holding them is how a node *drives* them, and the column is how the frame
  drives all of them.
- **A node reads and writes components** through the world it is handed, so a behaviour can set a `Position` a
  renderer will read, and a query can find every entity a behaviour touched.
- **`ready` runs when the behaviour is attached**, which is a structural change like any other and therefore immediate
  ([section 5](#5-structural-change-while-a-query-runs)).

### What a signal is

Godot's signal is an object-to-object connection, stored per object, invoked synchronously. Neither half survives here:
a stored callback is a function value in a field, which makes a node neither comparable nor showable and a scene file
unable to write it; and a `Channel` is a `shared type`, confined to one task, which would confine every node holding
one ([CONCURRENCY.md](CONCURRENCY.md) section 1).

**A signal is an event column a system drains** — the same mechanism as [section 6](#6-systems-and-scheduling)'s
events, seen from a node. A node writes `world.events.emit Damaged(entity, amount)`; whoever cares reads the column in
a later stage. The ordering is the schedule's, which is the thing Godot's synchronous signals make hard to reason
about, and the events are values, which means a replay replays them.

### The two layers are separable

**A game may use the data layer alone**, and `examples/ecs-probe` does: columns, queries, systems, a schedule, and no
`SceneNode` anywhere. Nothing in `std/ecs` names `SceneNode`, and `std/scene` depends on `std/ecs` and not the other
way round.

**A game may use both**, and then the split is the one Godot users already draw by hand: scripted, few, per-object
behaviour goes in nodes; uniform, many, per-frame work goes in systems over columns. The price of a node is a witness
table call and a value that is not `Plain`, so a behaviour column can never be windowed for data parallelism
([section 6](#6-systems-and-scheduling)). That is the correct price for the correct thing: behaviours are the slow,
few, scripted part, and components are the many, fast, plain part.

**What a game may not do is put the whole simulation in nodes.** That is Unity classic, it costs a virtual call per
object per frame, and the reason DOTS exists.

## 9. Scenes

**A scene is a `.trb` file, evaluated as a receiver script against a receiver the host declares.** This is
`project.trb`'s mechanism and [`examples/config-dsl`](../examples/config-dsl)'s, with a scene builder as the receiver
type, so the scene file is type checked, autocompleted and sandboxed with nothing new in the language. It is an **L2
typed resource** in the sense of [RESOURCES.md](RESOURCES.md) section 7: the path is a literal and the receiver type is
a type argument, so a type error in a scene is a build error.

### One scene

```trb
// scenes/level.trb — the body is a closure of type `(var self: Scene) => Void`

const spacing = 4.0

entity "ship" {
  transform position: Vector2(0.0, 0.0)
  velocity 1.0, 0.0
  collider radius: 0.5
  sprite "ship.png", layer: 1
  health 100

  entity "turret" {
    transform position: Vector2(0.25, 0.0)
    collider radius: 0.2
    aims at: "ship"
  }
}

for index in 1..=3 {
  instance "scenes/asteroid.trb", named: "asteroid-{index}" {
    transform position: Vector2(Float.from(index) * spacing, 0.0)
    health 30
  }
}

entity "camera" {
  transform position: Vector2(0.0, -10.0)
  follows target: "ship"
}

entity "spawner" {
  attach Spawner(every: 2.seconds(), scene: "scenes/asteroid.trb")
}
```

Everything in it is an existing mechanism. `entity "ship" { … }` is a method with a trailing receiver closure; the
nested `entity` is the same method one level down, and the nesting *is* the parent relation. `transform`, `velocity`,
`collider`, `sprite` and `health` are methods of the entity builder that the host declares — one line each, the same
way `examples/config-dsl`'s `ServerConfig` declares `route`. `attach Spawner(…)` is the generic escape hatch for a
component that has no shorthand, and a probe confirms a generic command resolves inside a receiver closure. The `for`
loop and the `const` are ordinary code, because a scene file is a program: that is the whole reason for choosing a
language over a data format.

**A package's components are writable in a scene** because the package's module is in the allowlist and the host's
builder carries its group. That is the openness of
[section 2](#2-the-decision-an-open-component-set-without-any) reaching the editor: adding a package to a program adds
its components to every scene the program loads, with autocompletion and a build-time type error, which is more than
Unity and Godot give and it is the same one line.

**References between entities inside one scene** are names, resolved after the file has run. `aims at: "ship"` records
a name; when the script is done, the builder walks the recorded references and turns each into the `Entity` the name
was given. It cannot be an `Entity` while the file runs, because `"ship"` may be written before the entity exists, and
a name that nothing declared is a `SceneError` naming the line.

**Instancing and overrides.** `instance "scenes/asteroid.trb", named: "…" { … }` loads another scene as a child of the
current entity and applies the block to it afterwards, so the block is the override — Godot's model exactly, and
Godot's `.tscn` stores only the overridden properties for the same reason. The host opens the file, never the script:
`instance` is a method of the receiver, so a scene file needs no file capability at all and cannot read anything the
host did not offer. Nesting has a depth limit and a cycle between two scene files is a `SceneError`.

### The loader

```trb fragment
use Sandbox from "std/sandbox"
use Int64.megabytes from "std/sandbox"
use Scene, SceneError from "std/scene"

/** Loads `scenes/level.trb` into a world of this program's own type. */
fn level(): Result<World, SceneError> {
  const script = Sandbox.load<Scene<World>>("./scenes/level.trb") {
    modules "acme/game/components", "acme/sprite", "std/linear", "std/time"
    limits steps: 1_000_000, memory: 16.megabytes(), time: 2.seconds()
  }?

  var scene: Scene<World> = Scene(root: "./scenes")
  script.apply(scene)?

  var world = World()
  scene.build(world)?
  Ok world
}
```

**What the sandbox must allow**, and nothing else: the modules that declare the component types, so `Spawner(…)` and
`Vector2(…)` are names the file can write; `std/linear` and `std/time` for the values it writes them with; and the
limits, because a scene file is a program and `loop { }` is a thing somebody types. **No files, no environment, no
clock, no processes, no network**: a scene that reads the outside world is not a scene, and `instance` goes through the
receiver so the host stays the only thing that opens a file. The capability list is CONCEPT's and the receiver type is
the whitelist, which is the sentence the sandbox design already stands on. The block is statically readable in the
sense of [RESOURCES.md](RESOURCES.md) section 7, so this call is checked at build time.

`Sandbox` is declared in `std/sandbox` and the back end answers *"a value of type `Sandbox` is not supported by the
native back end yet"*, so none of this runs until [gap 10](#12-what-the-language-and-the-compiler-must-provide) is
closed.

### User-defined component types inside a scene file

A scene file **may** declare a type, and a local `type` checks and runs. But a local type is a *helper* — a spawn
table, a wave description — and it can never be a component, because a component needs a column and only a group has
columns. A scene file that wants a component type says so by a package growing one, which is a recompile, which is
honest: in a language without reflection, a component type is a compile-time fact.

That is a real difference to Godot, where a scene can carry a script that declares a class. It is the price of the
compiled path, and the thing it buys is that a scene file that type checks cannot fail at load.

### Saving, and the round trip

**A value is its constructor call, so the saved form of a scene is TorbScript.** The writer is an `Encoder` in the
sense of [ENCODING.md](ENCODING.md) whose output is source:

```trb fragment
entity "ship" {
  attach Position(Vector2(0.0, 0.0))
  attach Velocity(Vector2(1.0, 0.0))
  attach Collider(0.5)
}
```

`record("acme/game/Position")` becomes `attach Position(`, each `field` becomes an argument, `finish` closes the
parenthesis. Defaults are omitted, because `Describe` says which fields have one and what it is. The round trip is
closed by construction and not by a test per type: what `Encode` writes, `Decode` reads, and both come from the same
constructor.

The written form is always `attach Constructor(…)` and never the `transform position: …` shorthand, because the
shorthand is the host's vocabulary and the writer does not know which method spells which component. A file a human
wrote in the shorthand and a file the editor wrote in the long form load into the same world; only the diff of a
hand-written scene changes shape the first time the editor saves it, which is a thing to say in the tool and not a
thing to solve in the format.

### How a name finds its decoder, without reflection

For the `.trb` path: **it does not have to.** The scene file is type checked against the receiver, so `Position` is
resolved by the compiler, and `attach Position(Vector2(0.0, 0.0))` is a call whose types are known before anything
runs. That is strictly better than Bevy's `TypeRegistry` and flecs's meta, both of which exist only because their
scene format is data and has to look a name up at run time.

For the two paths where a name really does arrive as text — an inspector that lets a human add a component by name,
and a binary scene format — there is a registry the program fills, and the compiled probe shows it needs nothing from
the language:

```trb fragment
public type Registry<World> {
  private(var) installers: Map<ComponentKey, (var world: World, entity: Entity, value: EncodedValue) => Void> = [:]

  var fn register(key: ComponentKey, install: (var world: World, entity: Entity, value: EncodedValue) => Void) {
    installers[key] = install
  }
}
```

```trb fragment
registry.register(componentKey<Position>(), { world, entity, value =>
  if const Ok(position) = Position.decode(Values(value)) {
    world.places.attach entity, position
  }
})
```

The map has forgotten the component type; **the closure under the key never did**, because it was written where the
type was known, so `Position.decode` monomorphizes like any other call. One line per component type, in one function,
and a package ships the lines for its own components. The probe runs the shape end to end:

```text
installed: true true -> Some(Position(value: Vector2(x: 7.0, y: 8.0)))
```

This is the narrowest honest form of a type-keyed map, and the reason it is honest is that **nothing is ever cast**:
the key finds a function, not a value.

## 10. What an editor needs

An inspector is a list of a component's fields, their types, their documentation and their defaults, filled with a
value's current contents and writing edits back. That is `Describe`, `Encode` and `Decode`, and
[ENCODING.md](ENCODING.md) already specifies all three.

| What the inspector shows | Where it comes from |
|--------------------------|---------------------|
| the component's name in the tree | `componentKey<Component>()`, the qualified declaration name |
| the field rows, in declaration order | `structureOf<Component>()` — `Structure.Record(typeName, fields)` |
| the label of a row | `FieldDescription.name` |
| the tooltip | `FieldDescription.documentation`, which *is* the field's doc comment |
| whether a row may be left empty | `FieldDefault.Required` / `Computed` / `Constant(value)` |
| the widget to draw | the `Structure` of the field: `Floating`, `Text`, `Record("std/linear/Vector2", …)`, `Variant` as a picker |
| the current contents | `EncodedValue.of(value)` |
| an edit, written back | `Component.decode(Values(edited))`, through the registry of [section 9](#9-scenes) |
| **the palette of components to add** | the registry's keys, which are the components the program's groups can store |

The last row is where the composed design shows in the tool: an editor's "Add Component" list is exactly the set a
program admitted, which is checked, spelled once, and cannot contain a component nothing can store. Unity's and
Godot's palettes are whatever the reflective registry found, which is why both have a filter and a search box and
still offer components that fail at run time.

**What is missing is only what ENCODING.md already lists**: `Describe` does not exist yet, `std/encoding` still carries
the old four-trait vocabulary, and `typeName<Type>()` is not a checker intrinsic. Nothing in an inspector needs
anything an ECS would have to invent.

Two things an inspector wants that `Describe` does not give, and the answer to each. A **range** for a slider is a
property of a field and not of a type, and there is no annotation to put it on one — so it is a value in the editor's
own configuration, keyed by the qualified field path, which is the same shape a format's mapping has and the same
argument ENCODING.md makes for mappings living in the format. And a **custom widget** for a type is a mapping value in
the editor's DSL, keyed by `typeName`, for exactly the same reason.

## 11. The package cut

**`std/ecs`.** `Entity`, `Entities` (the allocator), `Column<Component>`, `Store<Component>`, the four `query`
arities, `System`, `Stage`, `Schedule`, `Parent`, `depthOrder`, `Events<Event>`, `ComponentKey`. It depends on
`std/core`, `std/collections` and `std/time` and on nothing else — in particular not on `std/linear`, because an ECS
that knew what a position is would be a game engine. Every type in it is a value; there is no `shared type` in the
package.

**`std/scene`.** One package for two things that meet at the same tree: the `SceneNode` trait, the reading walk,
`Column<SceneNode>` and the frame step of [section 8](#8-the-behaviour-layer); and `Scene<World>`, `EntityBuilder`,
the loader over `Sandbox`, the writer that emits TorbScript, the name-to-entity resolution, instancing with
overrides, and `Registry<World>`. It depends on `std/ecs`, `std/time`, `std/sandbox` and `std/encoding`, and
`std/ecs` does not depend on it, which is what makes the two layers separable and lets `std/ecs` run in a binary
that embeds nothing.

What stays out, and what the ECS expects from each:

**`std/transform`.** `Transform2`/`Transform3` as a decomposed transformation and the coordinate spaces euclid's
phantom parameter suggests. The ECS expects a value type with an identity, a composition and interpolation, and
expects a group to store two of them per entity (`Transform2`, `GlobalTransform2`). It expects *nothing* about
propagation: the ECS owns the order, `std/transform` owns the arithmetic applied in it.

**`std/collision`.** Broad phase, narrow phase, contacts. The ECS expects it to take an `Iterate` of `(Entity, shape)`
and answer an `Iterate` of contacts, so that a collision system is a query piped into a function and `std/collision`
never learns what an entity is. A broad phase that wanted to keep a spatial index between frames keeps it as a field
of its own group, like any other state.

**`std/input`.** Key, pointer and gamepad state as a value per frame. The ECS expects one field in a group and a system
that reads it; it expects the package to have no callbacks, because a callback cannot live in a value.

**`std/asset`.** A handle type and a store keyed by a path, with loading behind `Source` from `std/stream`. The ECS
expects a handle to be a small value it can put in a component (`Sprite { texture: Handle<Image> }`), and expects
nothing about when the bytes arrive: a component holds the handle, a system asks whether it has resolved.

**`std/render`.** The abstract drawing layer, over whichever back end a program picks. The ECS expects to be *read* by
it and never to call it: a renderer runs a query, builds a draw list and hands that to a back end. Nothing in
`std/ecs` names a surface, a device or a frame.

**`std/animation`.** Easing, keyframes, tracks, a state machine, over a `trait Interpolate`. The ECS expects a track to
be a value in a component and the sampling to be a function of a `Duration`, so an animation system is a query and a
write-back.

**`std/curve`.** Bézier curves, splines, polylines, flattening. The ECS expects nothing at all; a path is a value that
a component holds, and a motion system samples it. (`std/path` is the file-path package; the curve package is
`std/curve`.)

**`std/color`.** A colour with a space. The ECS expects it to be a value with `Interpolate`, so it fits a component
and an animation track.

**Nothing in `std/ecs` depends on graphics**, and nothing in it depends on a platform. The whole package compiles and
runs in a program that draws nothing, which is what makes a headless simulation, a server tick and a test the same
code.

## 12. What the language and the compiler must provide

In the order it hurts, each with the smallest change and a reproduction. The reproductions that are green live in
[`examples/ecs-probe-2`](../examples/ecs-probe-2); the red ones are recorded in its README, because a package that does
not build proves nothing.

**1. ~~Two implementations of one trait with different arguments collide in the mangler.~~ Closed (2026-09-22):** the
name of a member of an `extend` whose trait has arguments carries the applied trait (`Store<Position>`), so the two
`attach`es are two functions (`tests/conformance/implementation-trait-arguments.trb` pins it with two `From`s and two
`Multiply`s on one type). What it said: the checker's half of this was closed: `extend World with Store<Position>` next to `extend World with Store<Velocity>` checks, `world.attach entity,
Position(…)` resolves by the argument type, and `const column: Column<Velocity> = world.column()` resolves by the
expected type, both confirmed by a negative probe. The back end then answers:

```text
internal error: the generated C did not compile. This is a bug in torb, please report it.
  gcc: error: conflicting types for 't_..._World_attach'; have 'void(T_..._World *, T_..._Velocity)'
  gcc: note: previous declaration of 't_..._World_attach' with type 'void(T_..._World *, T_..._Position)'
```

**Smallest change:** a mangled name carries the implementation's own trait arguments, in `compiler/src/ir/mangle.trb`.
This is item 16 of [LINEAR.md](LINEAR.md) section 12, met there as two `From`s on one `Path`. Without it a package's
columns hold one component type and a package with three components is three arguments at every call site; with it a
program's world carries every bound its columns carry and a system takes one world.

**2. No variadic type parameters.** `fn tuples<...Components>()` is five parse errors, the first *"Expected a name,
found `...`"*. **Smallest change:** a type-parameter pack that expands in exactly two positions — as the element list
of a tuple type, and as the subject of a bound — with no indexing, no length arithmetic and no mapping. It costs
`pairs`/`triples`/`quadruples` in this package, `all` in `std/task` and `zip` in `std/iteration`. **It is an
ergonomic gap and not a structural one:** [section 2](#2-the-decision-an-open-component-set-without-any) needs none
of it.

**3. ~~A blanket implementation's member is not found on a concrete type.~~ Closed.**
`extend<Subject: Query> Subject with Pairs { fn doubled(): Int { size() * 2 } }` on a `type Game with Query` checks and
answers `6`, natively. So every verb of `std/ecs` may be a member, and `group.pairs<Position, Velocity>()` is
writable.

**4. A closure cannot bind a `var` parameter.**

```text
error: A binding needs a value: there are no uninitialized bindings and no default values
  --> src/main.trb:38:25
   |
38 | points.each { var point =>
   |                         ^^
```

The parameter *type* `(var value: Component) => Void` is accepted, and a named `fn` with a `var` parameter can be
passed to it and works — but a named function cannot capture, so a system that needs the frame time cannot use one.
**Smallest change:** allow `var` in front of a closure parameter name, with the existing escape rule deciding whether
the closure may capture the reference. This is [CONCURRENCY.md](CONCURRENCY.md) gap 5 as well, where the body of
`windows` wants to be a closure.

**5. ~~A trait-typed value is accepted where a type parameter is expected.~~ Closed.** The soundness hole that made
the erased-column route look possible is gone, and the diagnostic names the rule:

```text
error: Expected `Value`, found `Area`
   = A type parameter is opaque inside the body that declares it: it stands for one type the call site chose, and
     a trait-typed value is any of them. Take the value as the parameter (`value: Value`) or answer the trait
```

**6. `typeName<Type>()` as a checker intrinsic**, and therefore `componentKey<Component>()`. `print
typeName<Position>()` answers `Cannot find `typeName` here`. It is item 3 of [ENCODING.md](ENCODING.md) section 13 and
CONCEPT already names it. Without it a component key is a string a package types twice and a scene file can misspell.

**7. `Describe`, and `Encode`/`Decode` in the shape [ENCODING.md](ENCODING.md) specifies.** `std/encoding` still
carries the four-trait vocabulary with `RecordEncoder`, nothing is derived, and `Describe` does not exist. The scene
writer, the inspector and the registry's decoders are all one of the three. Nothing here is new work for the ECS; it
is the encoding redesign, and `std/scene`'s writing half waits on it.

**8. A fork-join call over several `var` paths.** [Section 6](#6-systems-and-scheduling) narrows this gap and answers
[CONCURRENCY.md](CONCURRENCY.md) section 15's seventh question: the field case **is** wanted, because a system's unit
of work is a group and not a window, and because a component that is not `Plain` cannot be windowed at all. The proof
is not missing — the checker rejects `both world.places, world.places, settle, settle` with *"`world.places` is being
changed by `both` right now"* — and the call shape runs today. **Smallest change:** the fork-join barrier of
[CONCURRENCY.md](CONCURRENCY.md) gap 7, exposed as a fixed-arity call whose subjects are several `var` parameters, plus
that document's gaps 2 and 3 so a task boundary refuses a captured `var` and a `shared type`.

**9. `Buffer<Item>`**, with the three promises of [section 3](#3-the-data-layer): an in-place write when there is one
owner, two disjoint `var` windows, and a `swapRemove` without a shift. `Array<Item, const Size: Int>` is not a
substitute and does not run: *"a literal that fills an `Array` of 4 items is not supported by the native back end
yet"*.

**10. The sandbox in the back ends.** `Sandbox`, `Script<Value>` and `SandboxCapabilities` are declared as native types
in `std/sandbox` and the back end answers *"a value of type `Sandbox` is not supported by the native back end yet"*, so
the whole of `std/scene`'s reading half waits on the VM. That is expected — the sandbox *is* the VM — and it is why the
package cut puts scenes in a package of their own.

**11. ~~Two things stage 0 cannot do that the back end can.~~ Closed.** A local `type` and a type alias reached through
an import both check and run, so a scene file may declare a helper type and `std/ecs` may declare `type System<World> =
…`. What remains of the old item is the `Array` literal, which is part of gap 9.

**12. `Store<Component>` derived, and lent through a field.** Two derivations, under the rule that derives `Encode`.
A field of type `Column<Component>` in a type that says `with Columns` derives `Store<Component>` — three members per
component type saved. And a field whose type carries `Store<Component>` **lends** it upward unless the enclosing type
declares its own, so a program's world carries every bound its columns carry without a line per component.
**Smallest change:** one derivation in the checker for the first, and one delegation rule for the second; both need
gap 1 first, because both put several `Store`s on one type.

**13. A trait implementation on a tuple type does not satisfy a bound.**

```text
error: `(Position, Velocity)` does not implement `Has<Position>`
    = `describe` asks for it
```

`extend (Position, Velocity) with Has<Position>` is accepted as a declaration, in the same file as the bound's use, and
is not found; the same `extend` on a named type is found. This is recorded rather than requested: it is what closes
the type-list form of [section 2](#2-the-decision-an-open-component-set-without-any) for good, and the design does not
want it back. **Smallest change, if it is ever wanted:** implementation lookup indexes a structural head the way it
indexes a named one, or the checker rejects the `extend` at its declaration instead of accepting one that can never be
used.

## 13. Slices

Nine slices, each green on its own, ordered against the plan that is already set: the generic-numbers round, then
`Buffer<Item>`, then the encoding redesign, then the VM and the sandbox.

**Slice 1 — `std/ecs`, one `Store` per group type.** `Entity` with its generation, `Column<Component>`,
`Store<Component>`, `Events<Event>`, `ComponentKey` as a hand-written string, `System`/`Stage`/`Schedule` with a
sequential runner, `Parent` and `depthOrder`. The four `query` arities over groups, which is the form
[`examples/ecs-probe-2`](../examples/ecs-probe-2) already builds. **Needs nothing from the compiler.** Gate: `torb
test`, and a native gate program that steps a world and compares byte for byte with the interpreter.

**Slice 2 — the mangler, and one world per program.** Needs gap 1. A group holds every component type its package
owns, a program's world carries every bound, and the call sites in this document change from `movement(space, motion,
…)` to `movement(world, …)`. This is the slice that decides whether the package reads like an ECS.

**Slice 3 — the derived and lent `Store`.** Needs gap 12 and slice 2. It removes three lines per component type and
the delegation line per group, and it is the difference between "declare your world" and "declare your world twice".

**Slice 4 — `componentKey` and the access sets.** Needs gap 6. The schedule reports a conflicting pair, and the keys
stop being strings a package types.

**Slice 5 — `std/scene`, the tree.** The `SceneNode` trait, the reading walk, `Column<SceneNode>` and the frame step.
**Needs nothing from the compiler** — the probe runs all of it — and it comes after slice 2 only so that a node is
handed one world.
Gate: a tree of three node types stepped for ten frames, identical in both back ends, plus a test that a node's write
is visible to a query in the next stage.

**Slice 6 — columns on `Buffer<Item>`.** Needs gap 9. Nothing above the column changes; the slice is the column's
three fields and a benchmark that says whether a frame copies what it should not.

**Slice 7 — `std/scene`, reading.** Needs gap 10. `Scene<World>`, the entity builder, nesting as the parent relation,
names and references, `instance` with overrides, `SceneError` with a line. Gate: the scene of
[section 9](#9-scenes) loads into a world that a test asserts entity by entity.

**Slice 8 — `std/scene`, writing, and the registry.** Needs gap 7. The writer emits `attach Constructor(…)`, the
registry is derived, and the gate is a round trip: load, save, load, and the two worlds are equal.

**Slice 9 — parallel systems.** Needs gap 8, which is the `Buffer<Item>` round's fork-join work. The scheduler starts
acting on the access sets it has been collecting since slice 4, and the gate is that a schedule gives the same world
at one worker and at eight.

`std/transform` comes between slices 1 and 2, because propagation is the first system anybody needs and the ECS's half
of it is finished in slice 1. Everything else of [section 11](#11-the-package-cut) follows the order the owner set for
the engine packages and none of it blocks the ECS.

## 14. What this is not

- **Not a game engine.** It is storage, queries, a schedule and a node trait. A renderer, a physics solver, an audio
  mixer and an asset pipeline are other packages, and `std/ecs` names none of them.
- **Not a scripting runtime for gameplay code.** A scene file describes a world; it does not run every frame. Gameplay
  that runs every frame is a node or a system, and both are compiled.
- **Not Godot's node tree.** [Section 8](#8-the-behaviour-layer) takes Godot's shape and leaves its two mechanisms:
  there is no class registry, no node path and no run-time cast, and a node is a value rather than an object.
- **Not a general object database.** There is no schema migration, no query planner and no persistence beyond the
  scene format. A world that has to outlive a process is encoded like any other value.
- **Not reflective.** There is no `Type.forName`, no component added by a name that nothing declared, and no plugin
  that brings a component type into a *running* program. A component type is a compile-time fact, and a package
  bringing one is a compile-time act.
- **Not archetype-based, for now.** [Section 3](#3-the-data-layer) says why, and says what would have to change.
- **Not parallel, for now.** [Section 6](#6-systems-and-scheduling) says exactly how far away that is, and which of
  the two parallelisms an ECS wants.

## 15. Open, for the owner

Everything technical above is decided and the reason is written next to it. These are the questions of taste and
direction.

1. **`query2`/`query3`/`query4`, or four words?** The digit is the arity the way `Vector2`'s is the width, and three
   of the four names disappear when the pack lands. The alternative is `query`, `pairs`, `triples`, `quadruples`,
   which reads better at a call site (`for (entity, position, velocity) in pairs(space, motion)`) and stops saying
   "query".
   **Decided:** `pairs`, `triples`, `quadruples` — readable at the call site, and the digit disappears with
   variadics anyway. The one-column form keeps the name it has: `query`.
2. **`Store<Component>` and "column group", or other words?** A package ships a `Places`, a `Sprites`, a `Motions`;
   the other candidates are `Storage`, `Columns` and Bevy's own `Plugin`. The word is going to be in every package's
   public surface, so it is worth one look.
   **Decided:** `Store<Component>` stays; a package's column group is called `Columns` — `Places` and `Sprites` are
   examples of one, and the concept word in running prose is "columns".
3. **Does `std/ecs` ever ship a `World` of its own?** The set of component types is the program's, so no. But the
   first thing a newcomer asks for is a `World()` that works without declaring anything, and the only way to give them
   one is a fixed set of components in the package.
   **Decided:** no — `std/ecs` ships no `World`. The newcomer gets `examples/ecs-starter`, a copyable template
   instead of a fixed set of components baked into the package.
4. **Is `std/scene` its own package, or a module of `std/ecs`?** Separate here, so that a game can use the data
   layer alone and so that nothing in `std/ecs` names `SceneNode`. The cost is one more name in a game's
   dependencies.
   **Decided:** `std/scene` is its own package.
5. **May a scene file name `std/linear` and `std/time`?** [Section 9](#9-scenes) says yes, because `Vector2(1.0, 2.0)`
   has to be writable. It also means a scene file can compute, loop and call trigonometry, which is the point of
   choosing a language and also the reason a scene can be slow to load.
   **Decided:** yes — a scene may name both. A scene that loads slowly is what PROJECT.md's resource size warning
   is for.
6. **Does the ECS get events at all, or does a program write its own?** `Events<Event>` is thirty lines and every game
   writes it; putting it in the package makes the package bigger and the decision harder to reverse. It is also what
   [section 8](#8-the-behaviour-layer) puts in place of Godot's signals, which argues for keeping it.
   **Decided:** `Events<Event>` is in the package — it replaces Godot's signals.
7. **Is `GlobalTransform2` a component or a field?** Separate here, so that "what the author set" and "what the frame
   computed" cannot be confused. It costs a second column and a second lookup per entity.
   **Decided:** `GlobalTransform2` is its own component.
8. **Where does the frame loop live?** `Schedule.run` steps a world once. Who calls it, how the fixed accumulator is
   fed, and whether `std/ecs` ships a loop at all is a question about `std/render` and the platform, not about the ECS
   — but it is the first thing a program needs and it is currently nobody's.
   **Decided:** the frame loop lives in `std/scene` — `ready`/`process` with a fixed accumulator; `std/ecs` knows
   only `Schedule.run`. A pure-ECS game writes its own ten-line loop, the way a Bevy game without `DefaultPlugins`
   does.
9. **Does a node see the whole world, or the columns it declares?** [Section 8](#8-the-behaviour-layer) hands
   `var world: World` to `process`, which is one type parameter on `SceneNode` and total access. The alternative is
   a node that names bounds the way a system does, which is more honest and makes `SceneNode` harder to implement
   and a tree harder to type.
   **Decided:** a node sees the whole world (`var world: World` in `process`); access sets belong to systems, where
   they buy parallelism, and a node stays sequential.
