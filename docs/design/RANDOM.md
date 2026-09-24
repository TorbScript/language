# Random Numbers

**Status: planned** — there is no `std/random`, and nothing in `std` makes a random number. This record is a stub that
keeps the decided shape (2026-09-21); the design with probes comes before the package is built, and it blocks
`std/identifier` ([URI.md](URI.md) section 14, slice 6).

**A source of randomness is a value that can be seeded and split, not a global generator.** Two programs that start
from the same seed make the same numbers, and a computation that hands a split of its generator to each of its parts
makes the same numbers whichever part runs first. That is the shape of JAX's keys, and it is what reproducible
training ([COMPUTE.md](COMPUTE.md)), reproducible tests and deterministic simulations need.

## 1. What is decided

- **A value, passed as an argument.** A function that needs randomness takes it, as `Uuid.version7(source, at:)` does
  in URI.md section 12. A package that makes random values therefore needs no capability, and a sandboxed script has
  randomness exactly when it was handed a generator.
- **Seedable**: `Random.seeded 42` makes the same sequence on every machine and in both back ends.
- **Splittable**: a generator yields independent generators for parallel work (`parallel()`, tasks, the systems of
  [ECS.md](ECS.md)), so the numbers do not depend on the order the work runs in.
- **Value semantics decide the mutation**: drawing a number changes the generator, so drawing takes a `var` binding,
  and a copy of a generator repeats its numbers. That is visible at the call, never a surprise.

## 2. Open

- Where an unpredictable seed comes from: the operating system's entropy is a capability, so it belongs with the
  other capabilities (`std/os`) rather than in the pure package.
- The algorithm, and whether it is a public promise: a documented generator (a counter-based one, as JAX uses, or a
  small permuted congruential one) makes recorded numbers stable across versions of the toolchain.
- The distributions that come with the package (uniform integers in a range, floats in `[0, 1)`, normal, shuffling
  and sampling of a list), and which of them wait for `std/statistics`.
- Cryptographic randomness: a separate type backed by the operating system, so that a seeded generator is never
  mistaken for one.

## 3. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `std/random`: the generator, seeding, splitting, uniform integers and floats, shuffling | nothing |
| 2 | An unpredictable seed from the operating system | 1 |
| 3 | `std/identifier` (URI.md slice 6) | 1 |
