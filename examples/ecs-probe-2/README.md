# ecs-probe-2

The evidence for the decision in [`docs/ECS.md`](../../docs/ECS.md): **packages bring their own component types, and
the language needs no `Any` for it.** A package owns a component type, a column group that stores it and the systems
over it; a program holds one field per package's group; a package system is bounded by `Store<Component>` and names no
world type at all.

Everything in this package is green. `torb check examples/ecs-probe-2` answers `7 files, no problems`,
`torb run examples/ecs-probe-2` and the binary of `torb build examples/ecs-probe-2` print the same seven lines:

```text
moved: Some(GlobalPosition(value: Vector2(x: 0.5, y: 0.0))) Some(GlobalPosition(value: Vector2(x: 0.75, y: 0.0)))
changed since the frame began: 1
settled: Some(Position(value: Vector2(x: 0.5, y: 0.0))) Some(Velocity(value: Vector2(x: 0.5, y: 0.0)))
after the despawn: 1, in the snapshot: 2
after the rollback: 2
the tree: ["ship", "hull", "turret"]
one frame: ["ship at 0.5", "free frame 1 at 0.25"]
```

## What each module proves

| Module | Claim |
|--------|-------|
| `src/ecs.trb` | `Entity`, a dense `Column<Component>` with change stamps, and the `Store<Component>` bound. Nothing is erased. |
| `src/space.trb` | A package owns a component type, a column group and a `Store` implementation, plus the hierarchy pass over `Parent`. |
| `src/motion.trb` | A second package, and a system generic over `Space: Store<Position>, Motion: Store<Velocity>` - a package system over another package's component. |
| `src/schedule.trb` | Two systems over two disjoint groups as one call with two `var` parameters. |
| `src/behaviour.trb` | A `Node` trait, a heterogeneous tree of values changed in place, and a `Column<Node>` - the behaviour layer stored by the data layer. |
| `src/main.trb` | A program that names two packages' groups and nothing else; the snapshot and the rollback. |

## What is not in this package, and the diagnostic it produces

Four forms were probed as packages of their own and are recorded here rather than kept, because a package that does
not build proves nothing.

**1. ~~Two `Store<Component>` implementations on one type: the checker resolves them, the mangler does not.~~ Closed
(2026-09-22):** the name of a member of an `extend` now carries the applied trait, and the form below builds and
runs. This is what would let a program's own `World` carry `Store<Position>` *and* `Store<Velocity>`, so that a package system takes
one world instead of one group per package.

```trb
extend World with Store<Position> {
  var fn attach(value: Position) { positions.add value }
}

extend World with Store<Velocity> {
  var fn attach(value: Velocity) { velocities.add value }
}
```

`check` answers `2 files, no problems`, and resolution is correct in both directions - `world.attach Position(1.0)`
picks the first implementation and `const column: Column<Velocity> = world.column()` picks the second, which a
negative probe confirms (`Position` has no member `dx`). The back end then answers:

```text
internal error: the generated C did not compile. This is a bug in torb, please report it.
  gcc: error: conflicting types for 't_..._World_attach'; have 'void(T_..._World *, T_..._Velocity)'
  gcc: note: previous declaration of 't_..._World_attach' with type 'void(T_..._World *, T_..._Position)'
```

`World.attach` carried no arguments of its own in the mangled name. That was gap 1 of `docs/ECS.md` and item 16 of
`docs/LINEAR.md` section 12.

**2. A world generic over a list of component types.** `type World<Components>` and `const world: World<(Position,
Velocity)> = World()` both check and run, and a system `fn describe<Components>(world: World<Components>) where
Components: Has<Position>` checks. What does not hold is the bound:

```text
error: `(Position, Velocity)` does not implement `Has<Position>`
   --> src/main.trb:47:7
    |
47  | print describe(world)
    |       ^^^^^^^^^^^^^^^
    = `describe` asks for it
```

The implementation `extend (Position, Velocity) with Has<Position>` is accepted as a declaration in the same file and
is still not found when a bound asks for it; the same `extend` on a **named** type is found. Even with it, one
`extend` per tuple shape is not a design, and the blanket that would replace it needs a pack:

```text
error: Expected a name, found `...`
 --> src/pack.trb:3:18
  |
3 | public fn tuples<...Components>(): Int {
  |                  ^^^
```

**3. The erased column is closed at the checker.** The keyed heterogeneous map of a controlled `Any` cannot be
written:

```text
error: Expected `Column<Component>`, found `ColumnStorage`
  --> src/main.trb:44:27
   |
44 |       Some(found) => Some found
   |                           ^^^^^
```

and so is the three-line form with nothing about an ECS in it:

```text
error: Expected `Value`, found `Area`
  --> src/bare.trb:16:3
   |
16 |   shape
   |   ^^^^^
   = A type parameter is opaque inside the body that declares it: it stands for one type the call site chose, and
     a trait-typed value is any of them. Take the value as the parameter (`value: Value`) or answer the trait
```

**4. A per-type identity through a trait works.** `trait Component { static fn key(): ComponentKey }` and
`fn keyOf<Subject>(): ComponentKey where Subject: Component { Subject.key() }` check and run, and print
`ComponentKey(name: "probe/Position")`. So the *key* half of a type-keyed store exists today; only the cast does not,
which is the whole of finding 3.

## What the checker proves that nothing here has to

`both world.places, world.places, settle, settle` is rejected:

```text
error: `world.places` is being changed by `both` right now
   --> src/main.trb:56:20
    |
56  | both world.places, world.places, settle, settle
    |                    ^^^^^^^^^^^^
    = While a `var` access runs, the same path cannot be reached a second time
```

That is the disjointness a parallel schedule needs, already proved, at the call site of `both`.
