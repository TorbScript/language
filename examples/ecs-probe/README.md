# ecs-probe

The probe of three claims of [`docs/design/ECS.md`](../../docs/design/ECS.md) that only a compiled program can make:
a query is a snapshot, so a structural change during iteration needs no command buffer; copying the world is a
rollback, because a value is copied and not aliased; and a name finds the closure that installs it through an
ordinary registry, without reflection. `src/storage.trb` is the storage the five systems of `src/main.trb` run over.

This package has no `*.test.trb` of its own - what it proves is that it builds and runs at all, not a set of
assertions. `torb check examples/ecs-probe` answers `3 files, no problems`, and `torb run examples/ecs-probe` and the
binary of `torb build examples/ecs-probe` print the same lines. What the language cannot do yet is named in
`docs/design/ECS.md` section 10 and is not attempted here, because a package that does not build proves nothing.
