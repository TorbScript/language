# Entities, Components and Scenes

An open set of component types, dense typed storage, queries that are ordinary pipelines, and scenes that are
TorbScript. This is the specification of `std/ecs` and `std/scene`, of what they expect from the value packages under
them, and of the one thing they refuse to do: forget the type of a value.

```text
  a program declares its world            std/ecs owns the machinery          std/scene reads and writes it

  type World {                            Column<Component>   dense           a .trb file through a receiver
    var positions: Column<Position>       query2<A, B>        a pipeline      Sandbox.load<Scene<World>>
    var velocities: Column<Velocity>      Schedule            stages          instance, overrides, references
  }                                       Parent              depth order     saved as the constructor call
  ────────────────────────────────────────────────────────────────────────────────────────────────────────
  the set of components is open           nothing is erased                   no reflection anywhere
  because every program has its own       because nothing is downcast         because a value is its constructor
```

- **[1. The component model](#1-the-component-model)** — what identifies a component type, and where its value lives
- **[2. Storage](#2-storage)** — dense columns, a world as a value, and what `Buffer<Item>` has to give
- **[3. Queries](#3-queries)** — arity without variadic type parameters, and five systems written out
- **[4. Structural change while a query runs](#4-structural-change-while-a-query-runs)** — and why there is no command buffer
- **[5. Systems and scheduling](#5-systems-and-scheduling)** — access sets, stages, the fixed step, parallelism
- **[6. Hierarchy](#6-hierarchy)** — parent as a component, and what `std/transform` owns
- **[7. The scene DSL](#7-the-scene-dsl)** — a scene, its loader, instancing, overrides, and the round trip
- **[8. What an editor needs](#8-what-an-editor-needs)**
- **[9. The package cut](#9-the-package-cut)**
- **[10. Where this comes from](#10-where-this-comes-from)** — Bevy, flecs, Unity DOTS, Godot, EnTT
- **[11. What the language and the compiler must provide](#11-what-the-language-and-the-compiler-must-provide)**
- **[12. Migration](#12-migration)** — slices that each land green
- **[13. What this is not](#13-what-this-is-not)**
- **[14. Open](#14-open)**

The probe that carries every capability claim below is [`examples/ecs-probe`](../examples/ecs-probe): the storage, five
systems, a structural change during a query, a snapshot, a rollback and a name-to-installer registry. It checks, it runs
on stage 0, it builds natively, and the two print the same bytes. A claim this document makes and that package does not
show is marked as unproven where it stands.

---

## 1. The component model

**A component type is an ordinary `type`.** It carries no trait, it registers nothing, and it knows nothing about a
world:

```trb fragment
public type Position {
  value: Vector2
}

public type Velocity {
  value: Vector2
}
```

The set is open because every program declares its own. There is no closed `Component` sum as in
[`examples/game-engine`](../examples/game-engine), where adding a component type means editing a type of the engine, and
there is no registration step either.

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
[ENCODING.md](ENCODING.md) already needs for its mappings and which the checker does not have yet
([section 11](#11-what-the-language-and-the-compiler-must-provide), item 6). A key is used for **naming**: the word a
scene file writes, the label an inspector shows, the entry in a schedule's access set, the text of a diagnostic. It is
never used to find storage, and there is no `ComponentKey` to type back.

### Where a value lives

This is the whole question, and it has one honest answer: **`std/ecs` never holds a value whose type it has forgotten.**

The obvious design — `Map<ComponentKey, AnyColumn>` in one `World`, with a `columnOf<Component>()` that takes the column
back out — needs a checked downcast, which is Rust's `Any` + `TypeId` and is what every ECS on the research list is
built on. CONCEPT rules it out twice: "there is **no runtime reflection**" and "There is no 'any value' type in the
language."

A probe confirms that the route is not merely unwise, it is closed, and that the checker currently pretends otherwise:

```trb fragment
public trait AnyColumn {
  fn length(self): Int
}

public type World {
  private(var) columns: Map<String, AnyColumn> = [:]

  fn columnOf<Component>(self, name: String): Column<Component>? {
    match columns.get(name) {
      Some(column) => Some column                   // a trait value where `Column<Component>` is expected
      None => None
    }
  }
}
```

`check` answers **"no problems"**, stage 0 prints `Some(1)`, and `build` answers:

```text
internal error: …World_columnOf__…Position b1: the field `value` is `Record(…Column__…Position)`
  and %5 is `Object(AnyColumn)`
```

The same hole in three lines, with nothing about an ECS in it:

```trb fragment
public fn bare<Value>(shape: Area): Value {
  shape
}
```

```text
internal error: …bare__…Square b0: the function returns `Record(…Square)` and `return` carries `Object(Area)`
```

A **named** type rejects it correctly (`Expected `Square`, found `Area``); only a **type parameter** accepts it. So the
erasure route is a checker bug wearing a design's clothes, and closing the bug closes the route.

### What replaces it

The world is the **program's own type**, with one column per component type, and every value sits in a field whose type
names it:

```trb fragment
public type World {
  var positions: Column<Position> = Column()
  var velocities: Column<Velocity> = Column()
  var colliders: Column<Collider> = Column()
}
```

`std/ecs` reaches into it through one trait:

```trb fragment
/** A world that keeps a column of `Component`. */
public trait Store<Component> {
  fn attach(var self, value: Component, to: Entity)
  fn detach(var self, key: ComponentKey, from: Entity)
  fn column(self): Column<Component>
}
```

Two properties follow. Every member of `std/ecs` that touches a component is generic over it and monomorphized, so
there is no table lookup and no boxing in a hot loop. And the *set* of component types is open at the level of the
program, not at the level of a running world — which is the same openness Godot and Unity have at design time, minus
the reflection they pay for it with at run time.

**The price is three members per component type**, mechanical and identical every time. It is the price until the
compiler derives them from the fields, under exactly the rule `Encode` uses ("the constructor is usable from outside"):
a field of type `Column<Component>` in a type that says `with World` is an implementation of `Store<Component>`. Until
then a program writes them, and until [gap 1](#11-what-the-language-and-the-compiler-must-provide) is closed it cannot
write more than one of them at all — which is why `examples/ecs-probe` uses the columns directly and this section is the
design rather than the state.

### `Entity`

```trb fragment
/** What components are attached to. */
public type Entity with Equals, Hash, Show {
  index: Int
  /** Counts up every time `index` is handed out again, so a stale handle is detectable. */
  generation: Int
}
```

The generation is not optional. Without it an entity that was despawned and whose index was reused answers somebody
else's component, silently, and a system that kept a handle for one frame is a bug that reproduces once a week. With it
`column.get(entity)` answers `None`, which is what the caller already has to handle.

## 2. Storage

**Dense columns with a sparse index, not archetype tables.**

```trb fragment
public type Column<Component> {
  private(var) owners: List<Entity> = []
  private(var) values: List<Component> = []
  private(var) rows: Map<Entity, Int> = [:]
  private(var) stamps: List<Int> = []
  private(var) clock: Int = 0
}
```

`values` is dense and in insertion order, so a query walks it without a gap; `rows` answers "does this entity have one";
`remove` swaps the last row into the hole so neither half ever shifts. This is EnTT's model, and it is chosen over
archetypes for one reason that is about this language and not about performance:

**An archetype table has to move a value between tables when a component is added, and the code that moves it is code
that does not know the type of what it moves.** In Bevy that is `ptr::copy_nonoverlapping` over a `ComponentInfo` with a
recorded layout and a drop function; here it would be the erasure of [section 1](#1-the-component-model). A dense column
moves a value only *within its own column*, so every move happens inside one monomorphized body that knows exactly what
it is holding. The storage model follows from "nothing is erased", not from a benchmark.

Three more things the choice buys: attaching and detaching a component is O(1) and disturbs no other column, which is
what an ECS with many rarely-used tag components wants; there is no archetype explosion; and a query over two columns is
a walk plus a probe, which is an ordinary pipeline rather than a table matcher.

What it costs is the thing archetypes are good at: a query over *many* columns probes once per column per row, where an
archetype iterates one contiguous block. That is the trade, it is written down here, and it is revisitable the day the
language can move a value it cannot name.

### A world is a value

`type World`, not `shared type World`. Copying it is a copy of the value, and the compiled probe shows the two are
independent:

```trb fragment
const snapshot = world
expire world, 1.0
print "after expiring: {world.velocities.length()}, in the snapshot: {snapshot.velocities.length()}"

world = snapshot
```

```text
after expiring: 1, in the snapshot: 2
after the rollback: 2
```

So a snapshot is a binding and a rollback is an assignment. Replay, undo, rollback netcode and "what did the world look
like three frames ago" are a `List<World>` and need no mechanism at all. **What a copy costs is not measured here**: the
execution model of CONCEPT says the storage of a `List` and a `Map` is shared until somebody writes to it, so the
expectation is one refcount bump per column and a copy of exactly the columns a frame changed. That is an expectation
and not a claim; nothing in this repository measures it yet.

A `shared type` world would give none of this, and it would confine the world to one task
([Identity](../CONCEPT.md#identity-shared-type)). The value is the right choice and the snapshot is the reason.

### Determinism

`Fixed` from `std/linear` makes every coordinate bit-identical across machines and back ends. For a lockstep simulation
the ECS has to add the second half of it: **iteration order must not depend on a hash or on an address.** Two rules do
that, and both hold for the storage above.

- A query iterates the **column**, whose order is insertion order, and probes the index. It never iterates the index.
- `remove` is a swap with the last row, which is deterministic given the same sequence of operations — and the same
  sequence is what lockstep means.

The language helps: `Map` and `Set` iterate in insertion order in every implementation, which is a rule of the language
rather than of an implementation. So a world of `Vector2<Fixed>` components stepped by the same inputs produces the same
bytes on every machine, and the `Show` of a world is a usable checksum.

### What the design needs from `Buffer<Item>`

A column is three `List`s today, which is correct and is not the shape a frame budget wants. The planned heap kernel has
to give three things before the columns move onto it:

1. **A guaranteed in-place write when there is one owner.** A system that writes a column of ten thousand positions must
   not copy it. `List` promises copy-on-write, which is the right *semantics* and says nothing about when the copy
   happens; `Buffer<Item>` has to promise that a write through a `var` path whose storage has one owner is in place. This
   is the same promise `std/tensor` needs and TODO records as "garantierte In-place-Änderung bei einem Besitzer".
2. **Two disjoint `var` windows into one buffer.** This is what a parallel system needs
   ([section 5](#5-systems-and-scheduling)) and what a data-parallel loop over a tensor needs. `list[from..to]` as a
   `var` path is the shape; what is missing is handing out two of them at once, which exclusivity forbids today because
   it cannot compare two ranges statically.
3. **`swapRemove` without a shift**, so the dense half of a column stays O(1) to remove from without three `removeAt`
   calls that each shift a tail.

`Array<Item, const Size: Int>` is not the answer here — a column grows — and it does not run either: a literal that
fills an `Array<Int, 4>` answers *"a literal that fills an `Array` of 4 items is not supported by the native back end
yet"*.

## 3. Queries

A query answers an `Iterable` of tuples. That is the whole shape:

```trb fragment
public fn query<World, Component>(world: World): Iterable<(Entity, Component)>
public fn query2<World, First, Second>(world: World): Iterable<(Entity, First, Second)>
public fn query3<World, First, Second, Third>(world: World): Iterable<(Entity, First, Second, Third)>
public fn query4<World, First, Second, Third, Fourth>(world: World): Iterable<(Entity, First, Second, Third, Fourth)>
```

### Four arities, because variadic type parameters are the honest answer and do not exist

`fn tuples<...Components>()` is five parse errors today, starting with *"Expected a name, found `...`"*. And there is no
way around it: the language has no macros by design, `Array`'s const parameters have no arithmetic, and a trait cannot
abstract over a type constructor. Every system on the research list solves this with code generation — Bevy's
`all_tuples!`, DOTS's source generators, flecs's string DSL — and this language has decided against all three.

So **the honest answer is a variadic type parameter, and the smallest form of it is a pack that expands in exactly two
places**: as the element list of a tuple type, and as the subject of a bound.

```trb fragment
public fn query<World, ...Components>(world: World): Iterable<(Entity, ...Components)>
  where World: Store<...Components> {
```

No indexing into the pack, no arithmetic on its length, no mapping a type function over it — the same restriction const
parameters already live under, and enough for every query anybody writes. It would also serve `all(taskA, taskB)` and
`zip`, which are the other two places the standard library writes the same function four times.

Until it exists there are four names. The digit is the arity the way `Vector2`'s digit is the width, and the day the
pack lands three of the four disappear and `query` stays. Four is where it stops: a system that reads five component
types is a system that wants two queries.

### Reading and writing

**A query answers values, and a write goes back through the world.** Not `var` in the pattern: a `for` variable is a
`const` by the language's rule, and a closure cannot bind a `var` parameter at all — `{ var position => … }` answers
*"A binding needs a value: there are no uninitialized bindings and no default values"*. And not a `Write<Position>`
marker either: that marker exists in Bevy so the *scheduler* can read the access set out of the signature, and
[section 5](#5-systems-and-scheduling) declares the access set instead, so the marker would be decoration.

```trb fragment
fn movement(var world: World, elapsed: Duration) {
  for (entity, position, velocity) in query2<World, Position, Velocity>(world) {
    world.attach Position(position.value + velocity.value * elapsed.seconds()), to: entity
  }
}
```

That is one slot write per row into a dense column. Where a system changes many components of one entity it writes them
one at a time, which reads better than a batched form would.

In-place mutation without a write-back does exist and the compiled probe uses it, but only from **inside** the type that
owns the column, because `private(var)` hands outsiders a read-only path:

```trb fragment
extend Column<Position> {
  fn slide(var self) {
    for index in 0..values.length() {
      values[index].x = values[index].x + 1.0
    }
  }
}
```

`values[index].x = …` is a `var` path through a field and an index, and it compiles and runs in both stages. What does
not exist is the shape that would let a *caller* do it: `column.each { var position => … }`. Passing a named `fn` with a
`var` parameter works and is what the probe does, and a named function cannot capture, so a system that needs the frame
time cannot use it. That is [gap 4](#11-what-the-language-and-the-compiler-must-provide).

### Filters

**An ECS filter is a filter.** A query answers an `Iterable`, so the language's own vocabulary is the whole story:

| What an ECS calls it | What is written |
|----------------------|-----------------|
| `With<Other>` | `query3<World, A, B, Other>` — the join *is* the with-filter |
| `Without<Other>` | `query2<World, A, B>(world).filter({ !world.column<Other>().has(_.0) })` |
| `Changed<A>` | `world.column<A>().changedSince(stamp)` |
| `Added<A>` | the same, against the stamp the entity's row got when it was created |
| an enableable component | a `Bool` field of the component, tested in a `filter` — no structural change, which is what DOTS added enableable components for |

Change detection is a stamp per row and a clock per column, which is Bevy's mechanism without the macro:

```trb fragment
/** The entities whose value was written after `stamp`. */
fn changedSince(self, stamp: Int): Iterable<Entity>
```

The probe runs it: `changed since the frame began: 1`.

### Five systems, as a user writes them

These are the call sites, from [`examples/ecs-probe`](../examples/ecs-probe) with the `Store` layer written the way
[section 1](#1-the-component-model) specifies it rather than the way the probe has to work around it.

**Movement.**

```trb fragment
fn movement(var world: World, elapsed: Duration) {
  for (entity, position, velocity) in query2<World, Position, Velocity>(world) {
    world.attach Position(position.value + velocity.value * elapsed.seconds()), to: entity
  }
}
```

**Collision pairs.** Each pair once, over the smaller column.

```trb fragment
fn collisions(world: World): List<(Entity, Entity)> {
  const all = query2<World, GlobalPosition, Collider>(world).toList()
  var touching: List<(Entity, Entity)> = []
  for first in 0..all.length() {
    for second in (first + 1)..all.length() {
      const (here, hereAt, hereReach) = all[first]
      const (there, thereAt, thereReach) = all[second]
      const reach = hereReach.radius + thereReach.radius
      if hereAt.value.distanceSquaredTo(thereAt.value) <= reach * reach {
        touching.add((here, there))
      }
    }
  }
  touching
}
```

**Parent-child transform propagation.** One pass, in depth order ([section 6](#6-hierarchy)).

```trb fragment
fn propagate(var world: World) {
  for entity in depthOrder(world) {
    const local = world.column<Transform2>().get(entity) ?? Transform2.identity
    const base = match world.column<Parent>().get(entity) {
      Some(parent) => world.column<GlobalTransform2>().get(parent.entity)?.value ?? Transform2.identity
      None => Transform2.identity
    }
    world.attach GlobalTransform2(base.composed(with: local)), to: entity
  }
}
```

**Spawning bullets**, while the query over the shooters is running.

```trb fragment
fn fire(var world: World) {
  for (_shooter, position, velocity) in query2<World, Position, Velocity>(world) {
    const bullet = world.spawn()
    world.attach position, to: bullet
    world.attach Velocity(velocity.value * 4.0), to: bullet
    world.attach Lifetime(0.05.seconds()), to: bullet
  }
}
```

**Despawning on lifetime**, while the query over the lifetimes is running.

```trb fragment
fn expire(var world: World, elapsed: Duration) {
  for (entity, lifetime) in query<World, Lifetime>(world) {
    const remaining = lifetime.remaining - elapsed
    if remaining <= Duration.zero {
      world.despawn entity
    } else {
      world.attach Lifetime(remaining), to: entity
    }
  }
}
```

Both of the last two change the *structure* of the world while a query over it is running, and neither needs anything
for it. That is the next section.

## 4. Structural change while a query runs

**There is no command buffer, and value semantics is the reason.**

The rule is one line of CONCEPT, about `for` and nothing else: *"The subject is evaluated once, into a temporary, so it
is not an open `var` access: changing `xs` inside of the loop is safe and does not affect the loop."* A query's subject
is the world, so the loop walks a **copy** of it and every spawn, despawn and attach inside the loop lands in the live
one. The iteration cannot be invalidated, because what it iterates is not what is being changed.

The compiled probe shows both directions:

```text
moving before: 1, entities with a velocity after: 2      // `fire` spawned while the query ran
after expiring: 1, in the snapshot: 2                    // `expire` despawned while the query ran
```

What every other system on the list needs for this, and what it costs them:

| System | Mechanism | What it costs |
|--------|-----------|---------------|
| Bevy | `Commands`, a `CommandQueue` applied at a sync point | a spawn is not visible until the next stage, so a system pair that spawns and reads has to be ordered by hand |
| Unity DOTS | `EntityCommandBuffer`, played back at a sync point; a structural change invalidates every chunk array | the same, plus a safety system that has to detect the invalidation |
| flecs | `ecs_defer_begin`/`ecs_defer_end` around every iteration | a deferred `set` reads back the old value inside the same loop |
| EnTT | documented per operation: removing the current entity's component is safe, others are not | the rule is in prose and the compiler does not hold anybody to it |

Here the change is immediate and visible the moment it is written, and the *query* is the thing that is stale. That is
the better default: a system that wants to see its own spawns runs a second query, which is one line and says so, while
a system that must not see them — every physics pass — gets the guarantee for free.

**The semantics are a snapshot, and the cost is a copy that nobody has measured.** A query holds a copy of the world for
its duration, which under the storage rule of the execution model is a refcount bump per column, and a write to a column
during the loop then makes that column's storage unshared and copies it once. For a world whose columns are large and
whose systems all write, that is one copy per column per frame. It is the one place where this design could be too
expensive, it is exactly what `Buffer<Item>`'s in-place promise is for, and it is written down here rather than
discovered later.

## 5. Systems and scheduling

**A system is an ordinary function.**

```trb fragment
fn movement(var world: World, elapsed: Duration)
```

Nothing marks it, nothing registers it, and it can be called directly in a test.

### The access set is declared, because it cannot be read

Bevy reads a system's access set out of its parameter types, because a Bevy system takes `Query<(&A, &mut B)>` and the
types *are* the access set. Here a system takes the whole world, so its signature says nothing, and there is no
reflection to look inside the body with. The access set is therefore a value next to the function:

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
systems in one stage write the same key, and the input a scheduler needs the day it can act on it.

### What a scheduler may do today: nothing

A scheduler that runs two systems at once needs two `var` paths into disjoint parts of one world value, handed to two
tasks. The language does not have that, and a probe shows how far away it is:

- `spawn` is `Unknown name `spawn`` on stage 0, so nothing about tasks runs there at all.
- The checker **accepts** a `var` binding captured by a `spawn` closure, although CONCEPT says "Closures passed to
  `spawn` cannot capture `var` bindings". `check` answers "no problems" for two `spawn` blocks that both write one
  world.
- The native back end refuses the argument that would carry it: *"a `var` argument through a top-level `var` read from a
  function is not supported by the native back end yet"*.

So `Schedule.run` is a loop over the systems of a stage, in declared order, and the access sets are checked for a
conflict and otherwise only documented. The parallel form is [gap 8](#11-what-the-language-and-the-compiler-must-provide)
and it is the same gap `std/tensor` has for a data-parallel loop over disjoint buffer sections — one fix serves both,
which is an argument for doing it in the `Buffer<Item>` round rather than in an ECS round.

### Stages and the fixed step

```trb fragment
public type Schedule<World> {
  private(var) stages: List<Stage<World>> = []
  private(var) accumulated: Duration = Duration.zero
  var step: Duration = Duration.milliseconds(16)
}
```

Four stages, and a program may add its own: `Input`, `Fixed`, `Update`, `Late`. The fixed stage runs zero or more whole
times per frame out of an accumulator, and the accumulator is a `Duration` and not a `Float` — `Duration` is an exact
count, so an hour of frames cannot drift the way a repeatedly-added `1.0 / 60.0` does. A simulation that has to be
bit-identical runs its geometry over `Fixed` from `std/linear`, whose whole point is that it is integer arithmetic; the
schedule's job is only to hand it the same number of steps on every machine, which an exact accumulator does.

### Resources and events

**There are no resources.** A resource exists in Bevy and flecs because a system is handed the world and nothing else,
so a singleton needs somewhere to live. Here the world is the program's own type, so a resource is a field of it:

```trb fragment
public type World {
  var positions: Column<Position> = Column()
  var score: Int = 0
  var input: InputState = InputState()
}
```

One subsystem fewer, no `Res<T>`/`ResMut<T>`, no type-keyed map, and the access set names a field instead of a
component key where it has to.

**An event is a column that a system drains.** `Events<Event>` is a list with two buffers and a read cursor per
consumer, exactly Bevy's shape, and it is an ordinary value in a field. There are no signals and no observers: a
callback stored in a world would be a function value in a field, which the language allows and which would make a world
neither comparable nor showable, and a scene file that connects one would need to name a function the host owns. What
Godot's signals do is done here by a system that reads the events another system wrote.

## 6. Hierarchy

**Parent is a component, children are derived, and depth order is the ECS's one job.**

```trb fragment
/** Whose space this entity's transform is written in. */
public type Parent {
  entity: Entity
}
```

Not a relationship pair as in flecs: a pair `(ChildOf, target)` is a first-class *id* built at run time out of two
entity ids, which means a query engine that matches on ids, which means storage keyed by something other than a type —
the erasure of [section 1](#1-the-component-model) again, one level up. flecs's relationships are the best idea in the
research and they need exactly the machinery this language refuses to build.

Not a built-in tree either: a tree in the world would be a second structure to keep in step with the columns, and a
despawn would have to find every node that names the despawned one anyway.

So: one column of `Parent`, and `std/ecs` computes the order.

```trb fragment
/** Every entity with a transform, every parent in front of its children. */
public fn depthOrder<World>(world: World): List<Entity> where World: Store<Parent>
```

The probe runs it and one pass is enough: a child reads a global its parent has already written. Propagation itself is
one system and it belongs to `std/transform`'s vocabulary, not to the ECS's: the ECS owns the *order*, `std/transform`
owns `Transform2`/`Transform3`, their composition, and the coordinate spaces that make `composed(with:)` type-safe.
`GlobalTransform2` is a separate component and not a field of `Transform2`, so that "what the author set" and "what the
frame computed" cannot be confused and a system that only reads globals declares only that.

A cycle in the parent column is a bug, and `depthOrder` panics on one with the entity in the message. It cannot be
prevented by the type system and it must not be silently broken into a forest.

## 7. The scene DSL

**A scene is a `.trb` file, evaluated as a receiver script against a receiver the host declares.** This is
`project.trb`'s mechanism and [`examples/config-dsl`](../examples/config-dsl)'s, with a scene builder as the receiver
type, so the scene file is type checked, autocompleted and sandboxed with nothing new in the language.

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
`collider`, `sprite` and `health` are methods of the entity builder that the host declares — one line each, the same way
`examples/config-dsl`'s `ServerConfig` declares `route`. `attach Spawner(…)` is the generic escape hatch for a component
that has no shorthand, and a probe confirms a generic command resolves inside a receiver closure. The `for` loop and the
`const` are ordinary code, because a scene file is a program: that is the whole reason for choosing a language over a
data format.

**References between entities inside one scene** are names, resolved after the file has run. `aims at: "ship"` records
a name; when the script is done, the builder walks the recorded references and turns each into the `Entity` the name
was given. It cannot be an `Entity` while the file runs, because `"ship"` may be written before the entity exists, and a
name that nothing declared is a `SceneError` naming the line.

**Instancing and overrides.** `instance "scenes/asteroid.trb", named: "…" { … }` loads another scene as a child of the
current entity and applies the block to it afterwards, so the block is the override — Godot's model exactly, and Godot's
`.tscn` stores only the overridden properties for the same reason. The host opens the file, never the script: `instance`
is a method of the receiver, so a scene file needs no file capability at all and cannot read anything the host did not
offer. Nesting has a depth limit and a cycle between two scene files is a `SceneError`.

### The loader

```trb fragment
use Sandbox from "std/sandbox"
use Int64.megabytes from "std/sandbox"
use Scene, SceneError from "std/scene"

/** Loads `scenes/level.trb` into a world of this program's own type. */
fn level(): Result<World, SceneError> {
  const script = Sandbox.load<Scene<World>>("./scenes/level.trb") {
    modules "acme/game/components", "std/linear", "std/time"
    limits steps: 1_000_000, memory: 16.megabytes(), time: 2.seconds()
  }?

  var scene: Scene<World> = Scene(root: "./scenes")
  script.apply(scene)?

  var world = World()
  scene.build(world)?
  Ok world
}
```

**What the sandbox must allow**, and nothing else: the module that declares the component types, so `Spawner(…)` and
`Vector2(…)` are names the file can write; `std/linear` and `std/time` for the values it writes them with; and the
limits, because a scene file is a program and `loop { }` is a thing somebody types. **No files, no environment, no
clock, no processes, no network**: a scene that reads the outside world is not a scene, and `instance` goes through the
receiver so the host stays the only thing that opens a file. Nothing here is new — the capability list is CONCEPT's and
the receiver type is the whitelist, which is the sentence the sandbox design already stands on.

The one thing missing is the sandbox itself: `Sandbox` is `Unknown name `Sandbox`` on stage 0, so none of this runs
until the VM ([gap 10](#11-what-the-language-and-the-compiler-must-provide)).

### User-defined component types inside a scene file

A scene file **may** declare a type, and the checker and the native back end both accept a local `type` today (stage 0
refuses it: *"The bootstrap interpreter only supports local functions, no local types"*). But a local type is a *helper*
— a spawn table, a wave description — and it can never be a component, because a component needs a column and only the
host's world has columns. A scene file that wants a new component type says so by the program growing one, which is a
recompile, which is honest: in a language without reflection, a component type is a compile-time fact.

That is a real difference to Godot, where a scene can carry a script that declares a class. It is the price of the
compiled path, and the thing it buys is that a scene file that type checks cannot fail at load.

### Saving, and the round trip

**A value is its constructor call, so the saved form of a scene is TorbScript.** The writer is an `Encoder` in the sense
of [ENCODING.md](ENCODING.md) whose output is source:

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
hand-written scene changes shape the first time the editor saves it, which is a thing to say in the tool and not a thing
to solve in the format.

### How a name finds its decoder, without reflection

For the `.trb` path: **it does not have to.** The scene file is type checked against the receiver, so `Position` is
resolved by the compiler, and `attach Position(Vector2(0.0, 0.0))` is a call whose types are known before anything runs.
That is strictly better than Bevy's `TypeRegistry` and flecs's meta, both of which exist only because their scene format
is data and has to look a name up at run time.

For the two paths where a name really does arrive as text — an inspector that lets a human add a component by name, and
a binary scene format — there is a registry the program fills, and the compiled probe shows it needs nothing from the
language:

```trb fragment
public type Registry<World> {
  private(var) installers: Map<ComponentKey, (var world: World, entity: Entity, value: EncodedValue) => Void> = [:]

  fn register(var self, key: ComponentKey, install: (var world: World, entity: Entity, value: EncodedValue) => Void) {
    installers[key] = install
  }
}
```

```trb fragment
registry.register(componentKey<Position>(), { world, entity, value =>
  if const Ok(position) = Position.decode(Values(value)) {
    world.attach position, to: entity
  }
})
```

The map has forgotten the component type; **the closure under the key never did**, because it was written where the type
was known, so `Position.decode` monomorphizes like any other call. One line per component type, in one function, and it
is generated by the compiler from the same rule that derives `Decode` — the constructor's visibility — once that
derivation exists. The probe runs the shape end to end on stage 0 and as a binary:

```text
installed: true true -> Some(Position(value: Vector2(x: 7.0, y: 8.0)))
```

## 8. What an editor needs

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
| an edit, written back | `Component.decode(Values(edited))`, through the registry of [section 7](#7-the-scene-dsl) |

**What is missing is only what ENCODING.md already lists**: `Describe` does not exist yet, `std/encoding` still carries
the old four-trait vocabulary, and `typeName<Type>()` is not a checker intrinsic. Nothing in an inspector needs anything
an ECS would have to invent.

Two things an inspector wants that `Describe` does not give, and the answer to each. A **range** for a slider is a
property of a field and not of a type, and there is no annotation to put it on one — so it is a value in the editor's
own configuration, keyed by the qualified field path, which is the same shape a format's mapping has and the same
argument ENCODING.md makes for mappings living in the format. And a **custom widget** for a type is a mapping value in
the editor's DSL, keyed by `typeName`, for exactly the same reason.

## 9. The package cut

**`std/ecs`.** `Entity`, `Column<Component>`, `Store<Component>`, `World`, the four `query` arities, `System`, `Stage`,
`Schedule`, `Parent`, `depthOrder`, `Events<Event>`, `ComponentKey`. It depends on `std/core`, `std/collections` and
`std/time` and on nothing else — in particular not on `std/linear`, because an ECS that knew what a position is would be
a game engine. Every type in it is a value; there is no `shared type` in the package.

**`std/scene`.** `Scene<World>`, `EntityBuilder`, the loader over `Sandbox`, the writer that emits TorbScript, the
name-to-entity resolution, instancing with overrides, and `Registry<World>`. It depends on `std/ecs`, `std/sandbox` and
`std/encoding`. It is a separate package because loading a scene needs the sandbox and therefore the VM, while
`std/ecs` runs in a binary that embeds nothing.

What stays out, and what the ECS expects from each:

**`std/transform`.** `Transform2`/`Transform3` as a decomposed transformation and the coordinate spaces euclid's phantom
parameter suggests. The ECS expects a value type with an identity, a composition and interpolation, and expects to store
two of them per entity (`Transform2`, `GlobalTransform2`). It expects *nothing* about propagation: the ECS owns the
order, `std/transform` owns the arithmetic applied in it.

**`std/collision`.** Broad phase, narrow phase, contacts. The ECS expects it to take an `Iterable` of `(Entity, shape)`
and answer an `Iterable` of contacts, so that a collision system is a query piped into a function and `std/collision`
never learns what an entity is. A broad phase that wanted to keep a spatial index between frames keeps it as a value in
a field of the program's world, like any other state.

**`std/input`.** Key, pointer and gamepad state as a value per frame. The ECS expects one field in the world and a
system that reads it; it expects the package to have no callbacks, because a callback cannot live in a value.

**`std/asset`.** A handle type and a store keyed by a path, with loading behind `Source` from `std/stream`. The ECS
expects a handle to be a small value it can put in a component (`Sprite { texture: Handle<Image> }`), and expects
nothing about when the bytes arrive: a component holds the handle, a system asks whether it has resolved.

**`std/render`.** The abstract drawing layer, over whichever back end a program picks. The ECS expects to be *read* by
it and never to call it: a renderer runs a query, builds a draw list and hands that to a back end. Nothing in `std/ecs`
names a surface, a device or a frame.

**`std/animation`.** Easing, keyframes, tracks, a state machine, over a `trait Interpolate`. The ECS expects a track to
be a value in a component and the sampling to be a function of a `Duration`, so an animation system is a query and a
write-back.

**`std/curve`.** Bézier curves, splines, polylines, flattening. The ECS expects nothing at all; a path is a value that a
component holds, and a motion system samples it. (`std/path` is the file-path package; the curve package is
`std/curve`.)

**`std/color`.** A colour with a space. The ECS expects it to be a value with `Interpolate`, so it fits a component and
an animation track.

**Nothing in `std/ecs` depends on graphics**, and nothing in it depends on a platform. The whole package compiles and
runs in a program that draws nothing, which is what makes a headless simulation, a server tick and a test the same code.

## 10. Where this comes from

| System | Storage | How a query is typed | Structural change while iterating | Hierarchy | How a scene is written |
|--------|---------|----------------------|-----------------------------------|-----------|------------------------|
| **Bevy** (Rust) | archetype tables, plus opt-in sparse-set storage per component type | `Query<(&A, &mut B), With<C>>`; variadic through `all_tuples!`-generated impls up to 16 | `Commands` into a `CommandQueue`, applied at a sync point | `Parent`/`Children` components, maintained by commands; a propagation system | `DynamicScene` as `.scn.ron`, needing `#[derive(Reflect)]` and a `TypeRegistry` |
| **flecs** (C) | archetype tables, columns per table; cached queries per matching table | a string DSL (`Position, [out] Velocity, (ChildOf, $parent)`) plus a C++ builder | `ecs_defer_begin`/`end`; a staged world for threads | `ChildOf` as a **relationship pair**, a first-class id, traversable in a query | reflection (`ecs_meta`) plus a JSON serializer |
| **Unity DOTS** | 16 KiB archetype **chunks**, component arrays inside; shared and chunk components | `SystemAPI.Query<RefRW<A>, RefRO<B>>` and `IJobEntity`, produced by **source generators** | forbidden; `EntityCommandBuffer` at a sync point, and a structural change invalidates chunk arrays | `Parent`/`LocalToWorld`, a transform system group | authoring `GameObject`s **baked** by `Baker`s into a binary subscene |
| **Godot** | not an ECS: a tree of `Node` objects | no query; a node reaches children by path or by type | free — the tree is the data structure | the scene tree itself; a node may *be* a scene | `.tscn`, a text format; an instance stores only the overridden properties |
| **EnTT** (C++) | one **sparse set** per component type; `group` is the owning, archetype-like variant | `registry.view<A, B>()`, variadic through templates; iterates the smallest pool and probes | documented per operation: the current entity's component may be removed, others may not | none built in; the idiom is a `relationship` component with first/prev/next | none; the application writes one |

**What we take.**

- **The dense typed column** from EnTT, and its O(1) attach and detach — chosen here for a different reason (a column
  never moves a value it cannot name) and landing in the same place.
- **The access set as the scheduler's input** from Bevy, with the set declared rather than read, because the signature
  cannot carry it without variadic type parameters.
- **A component's change tick** from Bevy, as a stamp per row and a clock per column.
- **The relationship idea** from flecs, as far as it goes without erasure: a hierarchy is a component and the traversal
  is a function, not an id system.
- **Enableable components** from DOTS, as a `Bool` field and a `filter` — which is what they are, once a filter is a
  filter.
- **A scene as text, instanced, with only the overrides stored** from Godot. That is the best scene format on the list
  and the one thing this design copies wholesale.
- **The scene tree's nesting** from Godot as the *syntax* of the scene file, where nesting is the parent relation.

**What we leave.**

- **Reflection**, in all three forms it takes on the list: Bevy's `Reflect` plus `TypeRegistry`, flecs's `ecs_meta`,
  Godot's `ClassDB`. Their job here is done by the constructor, which the compiler already knows and already derives
  three forms of.
- **Code generation**: `all_tuples!`, DOTS's source generators, flecs's string DSL. Four arities and a named gap is more
  honest than a macro this language has decided not to have.
- **Archetype chunks.** They are the right answer for a cache and the wrong answer for a language whose values are never
  moved by code that has forgotten their type. Revisit when `Buffer<Item>` exists and when a benchmark says so.
- **The command buffer**, which [section 4](#4-structural-change-while-a-query-runs) replaces with the evaluation rule
  of `for`.
- **Resources**, which exist because a system is handed the world and nothing else; here the world is the program's own
  type.
- **Signals and observers.** A callback in a world would make the world neither comparable nor showable, and a scene
  file that connected one would have to name a function the host owns. Events are a column a system drains.

Two things nobody on the list has and this design does: **a world that is a value**, so a snapshot is a binding and a
rollback is an assignment, and **a scene format that is the language itself**, type checked before it runs and written
back out as the constructor calls it came from.

## 11. What the language and the compiler must provide

In the order it hurts, each with the smallest change and a reproduction. The reproductions are in the scratch form they
were probed in; [`examples/ecs-probe`](../examples/ecs-probe) holds only what is green, because a package that does not
build proves nothing.

**1. Two implementations of one trait with different arguments, on one type, are not reachable.** This is the whole
`Store<Component>` layer of [section 1](#1-the-component-model), and without it a program has no way to say "this world
keeps a column of `Position` *and* a column of `Velocity`".

```trb fragment
public trait Store<Component> {
  fn valueOf(self): Component?
  fn attach(var self, value: Component)
  fn attachAt(var self, slot: Bool, value: Component)
}

extend Game with Store<Int> { … }
extend Game with Store<String> { … }
```

```text
error: Expected `Int64`, found `String`
  --> src/main.trb:54:21
   |
54 | game.attachAt true, "eight"
   |                     ^^^^^^^

error: Expected `Option<String>`, found `Option<Int64>`
  --> src/main.trb:57:24
   |
57 | const named: String? = game.valueOf()
   |                        ^^^^^^^^^^^^^^
```

`game.attach 7` and `game.attach "seven"` both resolve, because the trait's argument is the **first** parameter.
Everything else takes the first-declared implementation: a member whose argument sits in a later parameter, and a member
whose argument is only in the result. Through a bound it is worse than an error — `World: Store<First> & Store<Second>`
type checks, stage 0 prints the same value twice, and the back end answers *"the result is `Option<Position>` and %6 is
`Option<Velocity>`"*. **Smallest change:** resolve the implementation by unifying the trait's arguments against every
argument type *and* the expected type, and report an ambiguity when more than one fits. This is item 12 of
[LINEAR.md](LINEAR.md) section 12, which met it as two `Multiply`s on one matrix; here it is not an inconvenience but the
load-bearing wall.

**2. No variadic type parameters.** `fn tuples<...Components>()` is five parse errors, the first *"Expected a name,
found `...`"*. **Smallest change:** a type-parameter pack that expands in exactly two positions — as the element list of
a tuple type, and as the subject of a bound — with no indexing, no length arithmetic and no mapping. It costs
`query2`/`query3`/`query4` in this package, `all` in `std/task` and `zip` in `std/iteration`.

**3. A blanket implementation's member is not found on a concrete type.**

```trb fragment
extend<World: Query> World with Pairs {
  fn doubled(self): Int {
    size() * 2
  }
}
```

```text
error: `Game` has no member `doubled`
  --> src/main.trb:48:12
   |
48 | print game.doubled()
   |            ^^^^^^^
```

`Game` carries `Query`, the trait is declared in the same file, and the member is still not there. Stage 0 has its own
half of it: an inherent blanket `extend<World: Query> World { … }` type checks and answers *"cannot extend `World`: it
is not a type"* when it runs. **Smallest change:** member lookup considers blanket implementations whose bound the
receiver satisfies. Without it every verb of `std/ecs` is a free function and `world.query2<Position, Velocity>()`
cannot be written at all — the call site of this whole design is a free function because of this one gap.

**4. A closure cannot bind a `var` parameter.**

```trb fragment
column.each { var position =>
  position.x = position.x + 10.0
}
```

```text
error: A binding needs a value: there are no uninitialized bindings and no default values
  --> src/main.trb:52:37
   |
52 | store.positions.each { var position =>
   |                                     ^^
```

The parameter *type* `(var value: Component) => Void` is accepted, and a named `fn` with a `var` parameter can be passed
to it and works in both stages — but a named function cannot capture, so a system that needs the frame time cannot use
one. **Smallest change:** allow `var` in front of a closure parameter name, with the existing escape rule deciding
whether the closure may capture the reference. This is what a dense in-place system wants to be written as.

**5. A trait-typed value is accepted where a type parameter is expected.** A soundness hole, found while probing the
erasure route, and the reason that route looks possible:

```trb fragment
public fn bare<Value>(shape: Area): Value {
  shape
}
```

`check` is clean, stage 0 prints `Square(side: 3)`, and the back end answers *"the function returns `Record(…Square)`
and `return` carries `Object(Area)`"*. A named concrete type rejects the same line correctly. **Smallest change:** a
type parameter unifies with a trait type only when that trait type *is* the parameter's own; a coercion to a trait type
is one of the language's four and none of them run backwards.

**6. `typeName<Type>()` as a checker intrinsic**, and therefore `componentKey<Component>()`. It is item 3 of
[ENCODING.md](ENCODING.md) section 13 and CONCEPT already names it. Without it a component key is a string the program
types twice and a scene file can misspell.

**7. `Describe`, and `Encode`/`Decode` in the shape [ENCODING.md](ENCODING.md) specifies.** `std/encoding` still carries
the four-trait vocabulary with `RecordEncoder`, nothing is derived, and `Describe` does not exist. The scene writer, the
inspector and the registry's decoders are all one of the three. Nothing here is new work for the ECS; it is the encoding
redesign, and `std/scene`'s writing half waits on it.

**8. Two disjoint `var` paths into one value, handed to two tasks.** Nothing about a parallel schedule can be probed
today: `spawn` is `Unknown name `spawn`` on stage 0, the checker accepts a `var` binding captured by a `spawn` closure
although CONCEPT forbids it, and the back end answers *"a `var` argument through a top-level `var` read from a function
is not supported by the native back end yet"*. **Smallest change, and it is not small:** a form that splits one `var`
path into several provably disjoint ones for the duration of one call — over distinct fields, and over distinct sections
of a `Buffer<Item>`. It is the same gap TODO records for a data-parallel loop over a tensor, so one design serves both.

**9. `Buffer<Item>`**, with the three promises of [section 2](#2-storage): an in-place write when there is one owner,
two disjoint `var` windows, and a `swapRemove` without a shift. `Array<Item, const Size: Int>` is not a substitute and
does not run: *"a literal that fills an `Array` of 4 items is not supported by the native back end yet"*.

**10. The sandbox.** `Sandbox` is `Unknown name `Sandbox`` on stage 0, so the whole of `std/scene`'s reading half waits
on the VM. That is expected — the sandbox *is* the VM — and it is why the package cut puts scenes in a package of their
own.

**11. Two things stage 0 cannot do that the back end can.** A local `type` compiles and runs natively and answers *"The
bootstrap interpreter only supports local functions, no local types"* on stage 0, which matters because a scene file may
want a helper type. And stage 0 does not resolve a **type alias** through an import — `"./ecs/world" does not declare
`System`` — which is why `examples/game-engine` does not run there at all and why `examples/ecs-probe` declares no
alias. Both are bootstrap gaps whose answer is the VM, not a change to stage 0.

## 12. Migration

Eight slices, each green on its own, ordered against the plan that is already set: the generic-numbers round, then
`Buffer<Item>`, then the encoding redesign, then the VM and the sandbox.

**Slice 1 — `std/ecs` without `Store`.** `Entity` with its generation, `Column<Component>`, `Events<Event>`,
`ComponentKey` as a hand-written string, `System`/`Stage`/`Schedule` with a sequential runner, `Parent` and
`depthOrder`. The four `query` arities as free functions over a program's world, which is the form
`examples/ecs-probe` already builds. **Needs nothing from the compiler**, so it can start the day the fixpoint holds.
Gate: `torb test`, and a native gate program that steps a world and compares byte for byte with stage 0.

**Slice 2 — `Store<Component>` and the method form.** Needs gaps 1 and 3. Everything of slice 1 that is a free function
becomes a member, the three accessors per component type appear, and the call sites in the documentation change from
`query2<World, Position, Velocity>(world)` to `world.query2<Position, Velocity>()`. This is the slice that decides
whether the package reads like an ECS.

**Slice 3 — the derived `Store`.** A field of type `Column<Component>` in a type that says `with World` derives
`Store<Component>`, under the rule that derives `Encode`. Needs slice 2 and one derivation in the checker. It removes
three lines per component type and is the difference between "declare your world" and "declare your world twice".

**Slice 4 — `componentKey` and the access sets.** Needs gap 6. The schedule reports a conflicting pair, and the keys
stop being strings the program types.

**Slice 5 — columns on `Buffer<Item>`.** Needs the buffer kernel. Nothing above the column changes; the slice is the
column's three fields and a benchmark that says whether a frame copies what it should not.

**Slice 6 — `std/scene`, reading.** Needs the VM and the sandbox. `Scene<World>`, the entity builder, nesting as the
parent relation, names and references, `instance` with overrides, `SceneError` with a line. Gate: the scene of
[section 7](#7-the-scene-dsl) loads into a world that a test asserts entity by entity.

**Slice 7 — `std/scene`, writing, and the registry.** Needs the encoding redesign (`Describe`, `Encode`, `Decode` and
`EncodedValue` in their new shape). The writer emits `attach Constructor(…)`, the registry is derived, and the gate is a
round trip: load, save, load, and the two worlds are equal.

**Slice 8 — parallel systems.** Needs gap 8, which is the `Buffer<Item>` round's disjoint-window work. The scheduler
starts acting on the access sets it has been collecting since slice 4.

`std/transform` comes between slices 1 and 2, because propagation is the first system anybody needs and the ECS's half
of it is finished in slice 1. Everything else of [section 9](#9-the-package-cut) follows the order the owner set for the
engine packages and none of it blocks the ECS.

## 13. What this is not

- **Not a game engine.** It is storage, queries and a schedule. A renderer, a physics solver, an audio mixer and an
  asset pipeline are other packages, and `std/ecs` names none of them.
- **Not a scripting runtime for gameplay code.** A scene file describes a world; it does not run every frame. A program
  that wants scripted behaviour compiles it, or writes a `Behaviour` component whose cases are the behaviours it
  supports.
- **Not Godot's node tree.** There are no nodes, no `_process`, no inheritance and no per-object virtual dispatch. The
  scene *file* borrows Godot's shape because that shape is right; what it builds is columns.
- **Not a general object database.** There is no schema migration, no query planner and no persistence beyond the scene
  format. A world that has to outlive a process is encoded like any other value.
- **Not reflective.** There is no `Type.forName`, no component added by a name that nothing declared, and no plugin that
  brings a component type into a running program. A component type is a compile-time fact.
- **Not archetype-based, for now.** [Section 2](#2-storage) says why, and says what would have to change.
- **Not parallel, for now.** [Section 5](#5-systems-and-scheduling) says exactly how far away that is.

## 14. Open

Everything technical above is decided. These are the questions of taste and direction.

1. **`query2`/`query3`/`query4`, or four words?** The digit is the arity the way `Vector2`'s is the width, and three of
   the four names disappear when the pack lands. The alternative is `query`, `pairs`, `triples`, `quadruples`, which
   reads better at a call site (`for (entity, position, velocity) in pairs<…>(world)`) and stops saying "query".
2. **Does `std/ecs` ever ship a `World` of its own?** Today it cannot, and after gaps 1 and 3 it still should not — the
   set of component types is the program's. But the first thing a newcomer asks for is a `World()` that works without
   declaring anything, and the only way to give them one is a fixed set of components in the package, which is the
   closed sum `examples/game-engine` was criticised for.
3. **Is `std/scene` its own package, or a module of `std/ecs`?** Separate here, because loading needs the sandbox and
   `std/ecs` must run in a binary that embeds nothing. The cost is a second name in every game's dependencies.
4. **May a scene file name `std/linear` and `std/time`?** [Section 7](#7-the-scene-dsl) says yes, because
   `Vector2(1.0, 2.0)` has to be writable. It also means a scene file can compute, loop and call trigonometry, which is
   the point of choosing a language and also the reason a scene can be slow to load.
5. **Does the ECS get events at all, or does a program write its own?** `Events<Event>` is thirty lines and every game
   writes it; putting it in the package makes the package bigger and the decision harder to reverse.
6. **Is `GlobalTransform2` a component or a field?** Separate here, so that "what the author set" and "what the frame
   computed" cannot be confused. It costs a second column and a second lookup per entity.
7. **Where does the frame loop live?** `Schedule.run` steps a world once. Who calls it, how the fixed accumulator is
   fed, and whether `std/ecs` ships a loop at all is a question about `std/render` and the platform, not about the ECS —
   but it is the first thing a program needs and it is currently nobody's.
