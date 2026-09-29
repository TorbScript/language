# Random Numbers

**Status: planned** — there is no `std/random` and no seeded generator. What exists (2026-09-29) is the other half:
`Entropy` of `std/os/entropy`, the operating system's source of randomness, for keys, nonces and an unpredictable seed
(section 4). This record is a stub that keeps the decided shape (2026-09-21); the design with probes comes before the
package is built, and it blocks `std/identifier` ([URI.md](URI.md) section 14, slice 6).

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

- The algorithm, and whether it is a public promise: a documented generator (a counter-based one, as JAX uses, or a
  small permuted congruential one) makes recorded numbers stable across versions of the toolchain.
- The distributions that come with the package (uniform integers in a range, floats in `[0, 1)`, normal, shuffling
  and sampling of a list), and which of them wait for `std/statistics`.

## 3. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `std/random`: the generator, seeding, splitting, uniform integers and floats, shuffling | nothing |
| 2 | An unpredictable seed from the operating system | **built** as `Entropy` of `std/os` (section 4), before slice 1 |
| 3 | `std/identifier` (URI.md slice 6) | 1 |

## 4. Cryptographic randomness, as built

**Decided and built 2026-09-29**, because the first-party packages needed nonces before anything needed a generator
([FIRST-PARTY.md](FIRST-PARTY.md) section 8, gap 5). It answers the two questions section 2 had open about it.

- **Where: `std/os`, module `std/os/entropy`, type `Entropy`** - not `std/random`. The operating system's entropy is
  a capability, and `std/random` stays pure: a package that makes random values from a generator it was handed needs
  no capability, one that asks the system for bytes does. A seeded generator and the system's source are two types in
  two packages, so one is never mistaken for the other; slice 1 takes its seed as a value, `Random.seeded
  Entropy.int64()`, and never imports `std/os`.
- **The capability is `entropy`, its own** (`capabilityOf` of `compiler/src/package/archive.trb`), and not "the
  operating system": entropy reveals nothing about the machine - it makes a program unrepeatable, which is what the
  clock does too - so a SCRAM client that makes a nonce is not reported as asking about the machine, and a sandbox
  grants it alone with `modules "std/os/entropy"`. `Entropy` re-exported by `std/os` reaches the same capability.
- **The surface**: `Entropy.bytes(count): List<UInt8>` and `Entropy.int64(): Int64`, eight bytes whose 64 bits are all
  random, for a seed. No `Result`: where the system does not answer, it panics. None of the sources below fails once
  the system has started, there is no safe way on without randomness, and a failure a caller could handle invites a
  weaker source in its place - Go 1.24 made `crypto/rand.Read` crash for the same reason.
- **The sources**, in `runtime/platform.c` (`torb_platform_random_bytes`), which the IO core's DNS identifiers now use
  as well: `BCryptGenRandom` with the system's preferred generator on Windows, loaded on first use; `getrandom(2)` on
  Linux and FreeBSD, which needs no file and so works in a chroot without `/dev` and with a full table of descriptors;
  `getentropy` on macOS and in the browser, where emscripten answers it with `crypto.getRandomValues`.
