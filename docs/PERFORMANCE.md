# Performance

**Status: partly implemented** — rounds P1 to P8 are done (P8 for the arithmetic); P9 to P12 wait until after the VM.

What a construct of TorbScript costs after lowering and `-O2`, which abstractions are free and which are not, and the
findings that stand between the two. The goal this document is written against is
[docs/ARCHITECTURE.md](ARCHITECTURE.md)'s: **fast and memory-frugal, almost on the level of a low-level language, with
zero-cost abstractions wherever possible - a one-field type must vanish completely.**

Everything below is measured on one machine (Windows 11, 16 cores, gcc 13.2.0 MinGW-W64 UCRT,
`-std=c11 -O2 -g0 -Wall -Wextra`) against the compiler as it stands. Every claim carries its evidence: an excerpt of
`torb ir`, an excerpt of the emitted C, or a number from `benchmarks/`. A claim with no evidence is not in this
document.

```text
the source ─► the IR ─► program.c ─┐
                                    ├─► gcc -O2, separate translation units, no LTO ─► the binary
             runtime/*.c ───────────┘
```

---

## 1. The performance model

What a user can rely on, in the order the costs matter.

### 1.1 A value is its bytes, and a binding is a name for them

A record whose estimated size is at most 32 bytes and that is not recursive is **`Inline`**: a C `struct`, passed and
returned in registers, copied by assignment, never counted. `type Meters { value: Float64 }` is
`layout ... inline size 8` and a `double` in the C. A record over 32 bytes, or one in a cycle of the field graph, is
**`Boxed`**: a pointer to a counted block, so copying it is a retain and writing through it is a copy on write.

Nesting is free. `Outer { Middle { Inner { count: Int } } }` is three layouts of `inline size 8`, and
`deep.middle.inner.count = 7` is the single instruction `write %0.middle.inner.count`.

### 1.2 What carries a count

`String`, the runtime containers (list, map, set), a `Boxed` layout, a trait-typed value, a closure with an
environment, a `Shared` object and a captured `var` `Box`. Nothing else. Counts are plain integers, not atomic
([BACKEND 2.5](BACKEND.md)).

A count changes at three places, and all three are calls into `runtime/memory.c`:

| Operation | What it is | What it costs |
|-----------|------------|---------------|
| `Retain` | `count += 1` unless the count is the immortal sentinel | a call, two loads, a store |
| `Release` | `count -= 1`, then drop and free at zero | a call, plus the drop glue at zero |
| `MakeUnique` | nothing when `count == 1`, a shallow copy plus a retain of every child otherwise | a call, and **O(n) when the value is shared** |

`program.c` and the runtime are separate translation units and this toolchain has no working LTO
([BACKEND 6.3](BACKEND.md)), so none of the three inlines. Finding 13 measures what that is worth, and the answer is
"less than it looks".

### 1.3 Parameters are borrowed, and the last use is a move

A parameter is `Borrowed` by default: the caller keeps the count for the whole call, so an ordinary call changes no
count at all. A parameter is `Owned` where the callee stores the value, and at an `Owned` position whose operand is not
live afterwards the argument is a `Move` and not a `Copy` - that is what makes `list = list.added(x)` change in place.
Finding 1 is the one shape where the liveness says "still live" although nothing reads the value again.

### 1.4 Monomorphization, and the one place it stops

A generic call whose witness is known is monomorphized: `Vector2<Int>` is a `struct { int64_t f_x, f_y; }` and
`a.plus(b)` is a direct call. A generic call that reaches a trait-typed value is **dictionary passing**: one instance
per bound, and every member call is a load of a function pointer out of a table plus an indirect call, which no C
compiler devirtualizes because the table is read out of the value.

A collection literal **was** the second kind. `[1, 2, 3]` has the type `List<Int>`, and `List` is a trait, so the
lowering writes `Object(List<Int64>)`: a heap box holding the `torb_list`, plus one witness pointer. Round P5 takes
that box back off again wherever the program says which container is inside it
(`compiler/src/ir/devirtualize.trb`, finding 2): every slot and every function result a value flows between is one
class, and a class whose producers are all one payload with the same static tables becomes that payload - slots,
parameters, result, and every `callWitness` on it the direct `call` of the member the table named.

**Dictionary passing is therefore what two implementations in one place cost, not what a collection costs.** What
still boxes is what the analysis may not follow: a value in a **field** of a record, a value a closure captures, a
value narrowed to another trait, and a value two implementations meet in.

A **member of a witness table** used to be on that list and no longer is. Its signature may not move - the thunk casts
the erased receiver to the payload and calls exactly that signature - so round P6 stopped trying to move it and copies
it instead: the pass runs twice, and between the two runs every frozen function a direct call already names gets a copy
under a name no table holds (`specializeFrozenCallees`). The table keeps the original, the direct call site gets the
copy, and the second run reads the copy's result as an ordinary location. `ArrayList.iterate` is what that is for, and
the cursor of a `for` over a list, a map, a set or a string is a record on the frame since (finding 5).

### 1.5 What is checked

Integer overflow, division by zero and every index are checked in every profile, because they are semantics and not
diagnostics. The check is `__builtin_add_overflow` plus a cold branch. What it costs depends entirely on what else the
code is doing: in a loop whose chain is latency-bound it is free (`benchmarks/arithmetic`), and in a recursion
whose whole body is three arithmetic operations and two calls it is **2.37x** (`benchmarks/call-depth`).

**A check that cannot fire is not emitted.** Round P8 added a forward interval analysis over one function
(`compiler/src/ir/ranges.trb`): what a slot can hold at one point, from an integer constant, from the operation that
computed it, and from the comparison a `Branch` stands on. An `Add`, `Subtract`, `Multiply` or `Negate` whose result
provably fits its own width becomes the plain C operator, which is what makes the counter of every `for` and every
`index - 1` under a guard free. Nothing about the semantics moved: unknown is the whole range of the type, so an
operation the analysis does not follow keeps its check, and a division still checks its divisor because that is not a
question about a width. Finding 8 has the numbers and says what is still on the table.

### 1.6 What a user pays for at the call site, in one table

| Written | Lowered to | Counts touched | Allocations |
|---------|------------|----------------|-------------|
| `a + b` on `Int` | `torb_add_i64`, inline, a cold branch | 0 | 0 |
| `point.x` on an inline record | a field read | 0 | 0 |
| `f(x)` where `x` is `Int` | a direct call | 0 | 0 |
| `f(text)` where `text` lives on | a direct call, borrowed | 0 | 0 |
| `deep.a.b.c = v`, inline all the way | one store | 0 | 0 |
| `holder.big.d = v`, `big` boxed | `MakeUnique` then one store | 1 call | 0 or 1 |
| `for index in 0..n` | a counted loop, the increment unchecked | 0 | 0 |
| `{ _ * 2 }` capturing nothing | a code pointer and `NULL` | 0 | 0 |
| `{ _ * factor }` as the argument of a call | a code pointer and an environment on the frame | 1 release | 0 |
| `{ _ * factor }` stored, returned or kept by the callee | a code pointer and a heap environment | 1 release | **1** |
| `numbers[index]` on a literal list | a direct call, `get`, an `Option`, `expect` | 0 | 0 |
| `numbers[index] = v` | `MakeUnique`, a direct call | 1 per write | 0 |
| `grid[row][column] = v` | `MakeUnique` of the grid, an `Element` step into it, the write through that address | 2 per write | 0 |
| `points[index].y = v` | `MakeUnique` of the list, an `Element` step, one store | 1 per write | 0 |
| `grid[row][column] = v` where the row is boxed at some other use | the same, plus one `MakeUnique` of the box at the element's address | 3 per write | 0 |
| `for value in numbers` | a concrete cursor on the frame, a direct `next`, an `Option` per turn | 0 | 0 |
| `for value in x` where two implementations meet in `x` | a boxed iterator, then `MakeUnique` and an indirect call per turn | 1 per turn | 1 per loop |
| `"{a} and {b}"` of numbers, `Bool`, `Char` or text | one concatenation over the values themselves | 0 | **1** |
| `"{a} and {b}"` of a type with its own `Show` | one `Show.show` per such part, then one concatenation | - | **those parts + 1** |
| `map[key] ?? 0` then `map.set key, n` | two probes, the key hashed twice | 1 | 0 or 1 |

---

## 2. The zero-cost contract

An abstraction is zero-cost when the program that uses it and the program that writes out what it means compile to the
same machine code. Each row says **holds** or **does not hold** and names the evidence. Ratios are from section 4.

| Abstraction | Verdict | Evidence |
|-------------|:-------:|----------|
| A one-field type (`type Meters { value: Float64 }`) | **holds** | `layout T_..._Meters inline size 8 align 8 fields(value: Float64)`; the C is `struct { double f_value; }` and `plus` is `s4 = s2 + s3`. `benchmarks/wrapper` is **0.97x** against the `double` loop |
| A newtype-like wrapper as the item of a list | **holds** | The element descriptor is `{ 8, 8, NULL, NULL, NULL, NULL }` - no retain, no release, no equals and no hash emitted for it |
| Nested inline records, and a deep field write | **holds** | `deep.middle.inner.count = 7` is one `write %0.middle.inner.count = %4` |
| `copy(field: value)` on an inline record | **holds** | `first.copy(count: 2)` is `construct T_..._Small (%23 borrowed)`; the source is not read at all |
| `Option<T>` where `T` is pointer-like | **holds** | `layout T_std_..._Option__String niche(0, 0) size 16 align 8` - the same 16 bytes a `String` is |
| `Option<Int>` | **holds, as documented** | `inline size 16 align 8 variants(Some(value: Int64), None)`: a tag beside the payload, which is what BACKEND 1.3 promises and what C would write by hand |
| An enum without a payload | **holds** | `layout T_..._Colour inline size 4 align 4` - a `uint32_t` tag |
| Generic math after monomorphization | **holds** | `Vector2<Int>` is `struct { int64_t f_x, f_y; }`; `plus` and `dot` are direct calls with no witness table in sight |
| `static` members and a `static const` | **holds** | `Big.origin` is one immortal block, built once, read through one `static` function that gcc inlines |
| A method call on a concrete type | **holds** | `t_..._Meters_plus(s1_sum, s4_step)` - a direct call, and gcc inlines it |
| The calling convention | **holds** | The recursion of `benchmarks/call-depth` compiles to the same shape as the C twin; a hand written C twin **with the same overflow checks** runs in the same time (0.38 s against the binary's 0.38 s on the same machine) |
| `for index in 0..n` | **holds** | No `Range` value and no iterator: `%4 = intrinsic less.i64 %2, %3` and a counted back edge - the same three blocks the hand written `while` loop lowers to, and `%2 = intrinsic add.i64.unchecked %2, %10` for the increment, because the head of the loop bounds it (finding 8) |
| A closure that captures nothing | **holds** | `s1.environment = NULL` - no allocation, and the thunk is a `static` function gcc can inline |
| Calling a closure through a parameter | **holds** | `benchmarks/closure` is **0.98x** against a C function pointer plus a context struct |
| **A closure that captures and does not escape** | **holds** where the callee is known not to keep it | `torb_environment_on_frame((torb_environment *)&s1_environment, NULL)` and no `torb_allocate`: the environment is a local of the frame. A closure a callee *stores* - which every `Iterate.map` does - is a block, because the pointer would outlive nothing |
| **`x = f(x)` at the last use** | **holds** | `%5 = call ..._appended(%0 owned last)` and no `retain` in front of it: the assignment defines `%0`, so the argument is the last use and the `makeUnique` inside the callee finds a count of one. `benchmarks/accumulate` allocates **20 blocks and copies 1.05 MB** for 60 000 appends, where the C twin does 16 and 1.05 MB |
| **A retain or release of a literal** | **holds** | A slot whose every definition is a `Constant` is left out of the counted set, so the body of `Index.at` holds no `torb_text_release` of the message its `expect` carries |
| **String interpolation** | **holds** | `const torb_text_part parts[4] = { { .kind = TORB_PART_TEXT, .text = s5 }, { .kind = TORB_PART_SIGNED, .signed_value = s1_index }, ... }` and one `torb_text_concat_parts`: **2 000 003 allocations** for 2 000 000 interpolations, which is the one `String` each of them answers |
| **Integer overflow checking of a counter or a guarded value** | **holds in the emitted code** | The increment of every `for index in 0..n` and every `index - 1` under `if index < 2` is the plain C operator: `s5 = s0_index - s4;` and `intrinsic subtract.i64.unchecked %0, %4` (`compiler/tests/ranges.test.trb`). It is worth little to *this* C compiler - `call-depth` moved 378 ms to 371, inside the band - because gcc was already folding those branches itself; what it is worth is that the IR no longer carries them, and the VM has no folding of its own |
| **Integer overflow checking in general** | **does not hold** | A sum of two values nothing bounds keeps its check, which is what `fibonacci(n-1) + fibonacci(n-2)` is: **2.55x** against a C twin with no checks at all. The C twin isolates it - the same recursion is 0.21 s plain, 0.38 s with all three operations checked and 0.28 s with only the addition - so the addition is the part that is left, and it is the one that can really overflow |
| **The dispatch of a collection literal** | **holds** | `[1, 2, 3]` is written as `Object(List<Int64>)` and is a `List(Int64)` by the time a back end sees it: `%0 = move %1`, and `numbers[index]` two functions away is `call n_std_..._ArrayList_get__Int64(%0 borrowed, %1 borrowed)` inside an `Index.at` gcc inlines. No box, no table, no indirect call |
| **A trait-typed value with one implementation** | **holds** | A local written as the trait, a result of the trait and a parameter of the trait are one class and one concrete value (`compiler/tests/devirtualize.test.trb`). A `var fn` member on one is the ordinary `var` path: `call t_..._Tally_bump(&%0 borrowed)` |
| **A list literal, used as a list** | **does not hold** | The dispatch is gone and the rest is not: `numbers[index]` is still a bounds check, an `Option` and an `expect` per element against a load, and the C twin vectorizes its sum. `benchmarks/list-index` lost **31%** of its time (28.56x to 19.70x against its twin in one session) |
| **The cursor of `for value in collection`** | **holds** | `slot %2 local iterator: Record(T_..._ListIterator__Int64)` and `%4 = call t_..._ListIterator_next__Int64(&%2 borrowed)`: no box, no `makeUnique` in the head, no indirect call. A list, a map, a set and a `String` all do this, and so does a user `Iterate`. `benchmarks/list-iterate` allocates **24 blocks** for 40 loops where it allocated 64 - the same 24 `benchmarks/list-index` allocates for the same data |
| **`for value in collection` as a whole** | **does not hold** | The `Option` per turn and the cross-unit `torb_list_get` are what is left, and gcc cannot see through either: `list-iterate` is **44x** against a pointer walk that vectorizes, which is 1.9x the counted index loop over the same list. Round P11 (a `for` over a concrete list as a counted loop over `Element` steps) is what removes the rest |
| **A field of a record in a list** | **does not hold**, and it is the *read* that is left | `points[index].y = v` is `makeUnique` of the list and one store through an `Element` step; the record never moves. `benchmarks/record-write` lost **59%** of its time for it and is **12.61x**, and what is left of the ratio is the `Index.at` on the right-hand side - a call, a bounds check and a copy of the record for one field |
| **A nested index write** | **does not hold**, and no allocation is left | `grid[row][column] = v` writes into the grid's own storage: **7 688 813 allocations became 8 813** - all of them the construction of the grid - and 31.65 GB of copying became 13.3 MB. What is left is that the row of a `List<List<Int>>` is still an `Object` box - it goes *into* the outer list, which the devirtualization may not follow - so the write is one `makeUnique` of that box at the element's address and one indirect call |
| **A pipeline of `map` and `filter`** | **does not hold** | Three allocations per pipeline instead of four, and one indirect call per stage per element: the stages are built around a trait-typed `self` in a **field**, which the devirtualization may not follow, so their cursors are still boxed although the outermost one is not. `benchmarks/pipeline` is **21.77x** |

Every row that does not hold **used to** have one cause: a value whose concrete type the compiler knows, boxed behind a
trait anyway. Round P5 removed that cause wherever the program decides the payload, round P6 removed the last of it
that was a *signature* rather than a value, and round P7 removed the read-copy-write of an element. What the rows above
are now is what is underneath all three: the `Option` round trip of every `next` and every `get` (a standard library
question), the bounds check of an index (the half of finding 8 that is still open), and a trait-typed value in a
**field** - which is what the stages of a pipeline are built around and what the row of a `List<List<Int>>` still is.

---

## 3. The findings

Ranked by (gain x frequency) / effort. Each entry says what is generated, what it costs, where the fix goes, what it is
worth, what it risks, and the test that pins it.

| # | Pattern | Cost | Fix | Effort |
|---|---------|------|-----|--------|
| 1 | `x = f(x)` at the last use | was >231x, 120 006 allocations, 20 GB copied | a `Write` with no steps defines its base | **done, round P1** |
| 2 | A collection literal is a trait-typed value | was 13x to 48x, one box per literal, one indirect call per access | a whole-program devirtualization over the IR | **done, round P5** |
| 3 | A nested index write copies the row | was >451x, 2 allocations per write | an `Element` path step into the concrete list | **done, round P7** |
| 4 | A field of a record in a list | was 52x | the same `Element` path step | **done, round P7** |
| 5 | `for` over a collection | was one box per loop and a `makeUnique` per turn | a copy of the frozen `iterate()` for the direct call sites | **done, round P6** |
| 6 | String interpolation | was 3 allocations per interpolation | a text part that is still a number | **done, round P4** |
| 7 | A non-escaping closure environment | was one allocation per closure made | the environment on the frame | **done, round P3** |
| 8 | Overflow checks that cannot fire | was 2.37x on call-heavy code | a local range analysis over the IR | **done, round P8** for the arithmetic; the bounds check is open |
| 9 | A pipeline of stages | 22x, three allocations per pipeline | a trait-typed value in a field | large |
| 10 | A map read-modify-write, and `map[key].field = v` | 6.4x, the key hashed twice, and the value still copied | `TakeOut`/`PutBack` on the map | medium |
| 11 | A `Release` of a literal text | was one call per list index read | a dead-count rule in the ownership pass | **done, round P2** |
| 12 | Witness members that nothing calls | 24 thunks in a three-line program, and every table emitted | whole-program member liveness | large |
| 13 | Reference counts cannot be inlined | measured: -14% to +9%, no decision | nothing yet | - |
| 14 | A module `const` rebuilt where it is read | 63% of a read, already fixed | recorded, not open | - |
| 15 | The stack check | 1-3% on a recursion of three operations; a frame counter was 1.74x | one comparison with the stack limit at entry | **done** |
| 16 | A range of a trait-typed list runs the default `slice` | was a copy of the range, one call per item, where `ArrayList` shares the storage in O(1) | a slot for a member that answers `Self`, added once it is called | **done** |

### F1. `x = f(x)` is a move, because the assignment defines the slot

**Pattern.** `numbers = appended(numbers, index)`, `state = state.with(key, value)` - the participle style the whole
language is written in.

**What is generated.**

```text
b2:
  %5 = call t_..._appended(%0 owned last, %2 borrowed)
  write %0 = %5 owned last
```

No `retain` in front of the call, so `makeUnique %2` inside `appended` finds a count of one and copies nothing.

**Why it works.** A `Write` whose `reference.steps` is empty **defines** its base, which is what `definedSlot`
(`compiler/src/ir/verify.trb`) answers for it. Three things follow from that one rule and nothing else is written
anywhere:

- the base is not a *use*, so the backward liveness walk lets the last real use of the old value be the argument of the
  call - and an `Owned` argument at a last use is a `Move` (`compiler/src/ir/operand.trb`);
- the old value is released where it dies: after the instruction that read it last, at the definition that nothing
  reads (`var t = make()  t = other`), or on the edge into the block that overwrites it - all three by the rules the
  ownership pass already had;
- the C back end's `Write` stores without releasing, because the ownership pass released. A **`var` parameter** is the
  exception: its storage is the caller's and no slot of this frame counts what is in it, so the release stays at the
  store (`compiler/src/backend/c/body.trb`).

A panic runs nothing on the way out - no release, no drop (CONCEPT, "A panic is output") - so a call that panics
between the move and the store leaves what it was handed, exactly as a panic anywhere else does.

**The cost it removed.** O(length) per append became amortized O(1). `benchmarks/accumulate` at 60 000 appends:
**20 allocations and 1.05 MB copied**, against 16 allocations and 1.05 MB in the C twin - the same bytes, which is what
"the last use is a move" means. The retained shape paid 120 006 allocations and 20.0 GB for the same 60 000 appends.

**What holds it.** The extended verifier reports `%n is overwritten while it still owns a value` at a write whose slot
was not emptied, which is the invariant in one sentence; `compiler/tests/ownership.test.trb` pins the IR of the loop;
and `reassignment.trb`, `participle.trb` and `counted.trb` of the conformance suite run every shape of an overwrite
with the leak gate on.

### F2. A collection literal is a trait-typed value, and the program says which one

**Pattern.** `var x = [1, 2, 3]`, `var m: Map<String, Int> = [:]` - every collection in every program.

**What was generated.** The checker gives `[1, 2, 3]` the type `List<Int>`, which is a *trait*, so the IR type is
`Object(List<Int64>)` and the lowering coerces the freshly built `ArrayList` into a box.
`compiler/src/ir/lower/collection.trb` says so in its own module comment: "The **type of a literal is a trait-typed
value** ... it does *not* name the implementation."

```text
%1 = new List(d_Int64)
%0 = traitValue %1 owned last tables(table w_std_x2f_collections_list_List__List_Int64_Int64)
makeUnique %0
callWitness value %0 bound 0 member 3(%6 borrowed)
```

**What is generated now.** `compiler/src/ir/devirtualize.trb`, between `lowerWorkspace` and `insertOwnership`:

```text
%1 = new List(d_Int64)
%0 = move %1
makeUnique %0
call n_std_x2f_collections_list_ArrayList_add__Int64(&%0 borrowed, %6 borrowed)
```

and `numbers[index]`, two functions away, is `call n_std_x2f_collections_list_ArrayList_get__Int64(%0 borrowed, %1
borrowed)` inside an `Index.at` whose `self` is a `List(Int64)` - a call gcc inlines, which is what makes the bounds
check and the `Option` visible to it at all.

**How it decides.** The unit is not a slot but a **class of locations**: every slot of every function plus the result
of every function, joined wherever a value flows between two of them - an argument into a parameter, a result out of a
call, a `Return`, a `Read` or a `Write` of the whole slot. A class loses its box when every producer is a `TraitValue`
of the **same** payload with the **same** static tables and nothing in it is poisoned. Then the slots, the parameters
and the result take the payload's type, the `TraitValue` becomes a `Copy` - a `Move` at its last use - and every
`CallWitness` on it becomes the direct `Call` of the member the table named, with the receiver as the first argument
and as a *place* where the member takes `var self`.

Everything the analysis cannot follow poisons what stands in it: a value built into a record, captured by a closure,
narrowed to another trait, handed to an erased ABI (a `CallClosure`, or any argument of a `CallWitness`), read out of a
path, or taken out of a container. So does every parameter and result of a function that something other than a `Call`
of the program reaches - a **member of a witness table** above all, whose thunk casts the erased receiver to the
payload and calls exactly that signature, and with it a function a `Closure` points at, a function of the runtime, a
helper of an element descriptor and the accessor of a module `const`. Two implementations that meet in one place
poison it too, which is the case dictionary passing exists for.

**The annotation is not what decides it.** `const items: List<Int> = [1, 2, 3]` loses its box like every other literal:
what the analysis follows is where a value *goes*, not how a binding was written. `benchmarks/list-index` writes `var
numbers: List<Int> = []`, hands it out of `filled()` as `List<Int>` and indexes it in `total(numbers: List<Int>)`, and
all five positions - the local, the result, the temporary, the parameter, and the `self` of the `Index.at` instance -
are one class holding one `torb_list`.

**Nothing about the language changed.** The checker is untouched, the type of a literal is still the trait, and the
diagnostics, `Show` and every message say what they said. Two things in the *output* moved: the IR text, and one line
of the C back end - a witness table is emitted with **external** linkage now, because a table the devirtualization made
unreachable would otherwise be a `static const` that nothing reads, which is an error under
`-Wunused-const-variable -Werror`. The runtime gained `torb_map_make_unique` and `torb_set_make_unique` beside
`torb_list_make_unique`, because a `var` path through a concrete map needs the same one-line call a list already had.

**What it costs and what it bought.** Section 4 has the table. The shape of it: `list-index` **-31%** (399 ms to
275 ms) and `record-write` **-31%** (1.25 s to 0.87 s) with the same allocation counts, because what went away is the
dispatch and not a block; `map-count` **-20%**, `pipeline` **-13%** and `list-iterate` **-12%**. `pipeline` also loses
one of its five allocations per pipeline (125 to 105 for 20 pipelines), because the `Filtered` stage is a record now
instead of a box. The compiler's own `program.c` is **274 875 bytes smaller** for the same
sources (63 265 243 against 62 990 368), which is what the direct calls and the boxes that are not built save; the
three-line list program of finding 12 goes from 25 696 to 25 102 bytes and keeps all 24 of its witness thunks, because
a table that nothing reaches is still emitted. That is finding 12 and round P10, and it is now the biggest single item
in the C of a small program.

**The risk.** A class that is peeled although something still needs the box is a wrong program. Three things hold it:
the analysis poisons by construction (a position it does not recognize is poisoned, never ignored), every question the
IR verifier would ask about the direct call is asked *before* the rewrite - the member has to be a plain function of
the program with no witness parameters, the receiver exactly the payload, every argument the type its parameter
declares, the result what the slot at the call site holds - and a class that fails any of them is poisoned instead of
half rewritten.

**What is left on the table.** A class is all or nothing: one use that has to be a box keeps the box for every
other use of the same value. Boxing **at that use instead** - inserting a `TraitValue` in front of the one argument, the
one field or the one return that needs it - would cover `rows.add cells` in `benchmarks/nested-write`, where the inner
list is peeled everywhere except where it goes into the outer one. It costs one allocation at the point of escape and
nothing anywhere else, so it is worth measuring rather than assuming; it is the first thing to try if a later round
wants more of this finding.

**The test.** `compiler/tests/devirtualize.test.trb` pins both halves: a value written as the trait and used in its own
frame is the payload, a result and a parameter of the trait become the payload together, a `var fn` member is the
ordinary `var` path of a concrete value - and, on the other side, a value built into a record keeps its box, two
implementations in one parameter keep the dispatch, and a member of a witness table keeps the signature its thunk
calls. `compiler/tests/lower-collections.test.trb` pins the loop whose cursor is concrete, and the conformance suite
runs all 84 programs with the leak gate on.

### F3. A nested index write goes into the grid's own storage

**Pattern.** `grid[row][column] = value`, `world.entities[id].health = 1`, `rows[i].add(x)`.

**What was generated.** The **outer** list was concrete after round P5 and its `set` a direct call; the row is an
element of a container and therefore a value the frame had a count of, so the `MakeUnique` in the middle copied it.

```text
%10 = read %0
%11 = call t_std_..._Index_at__...(%10 borrowed last, %4 borrowed)   # the row - retains it
release %10
...
makeUnique %11                                                # count is 2, so this COPIES the row
callWitness value %11 bound 0 member 8(%7 borrowed, %15 borrowed)
makeUnique %0
call n_std_x2f_collections_list_ArrayList_set__...(&%0 borrowed, %4 borrowed, %11 owned last)
```

**What is generated now.** `compiler/src/ir/elements.trb`, a peephole after `devirtualizeProgram`:

```text
makeUnique %0
makeUnique %0[%4]
callWitness value %0[%4] bound 0 member 8(%7 borrowed, %13 borrowed)
```

Nothing is taken out and nothing is put back. `%0[%4]` is a `PathStep.Element` into the grid's own storage, so the
`makeUnique` on it finds a count of one - the grid is the only owner of the row - and copies nothing.

**How it decides.** The round trip the lowering writes is a `Read` of the container, `Index.at` into a slot of the
frame, the access, and `MutableIndex.set` back. The pass looks for exactly that quadruple inside one block and
rewrites it only when all of this holds: the put-back is the runtime's own `torb_list_set` (through the wrapper that
every `native fn` of `std/` reaches it by), the take-out is a member the **lowering recorded** as an index read
(`IrProgram.elementReaders`, by symbol, so a copy `specializeFrozenCallees` made is the same member), the container is
a `Runtime(ListStorage, Item)`, the element has no owner but the container, every use of the element in between is a
**place** and not a value read, and nothing in between touches the container or the key. Everything that cannot be
proved is proved false, and what the pass refuses keeps the copy it always had.

**It is not a change of the lowering**, which is what the P6/P8 round wrote down here: `takenElement` runs while the
container is still `Object(List<Int>)`, and the devirtualization is two passes later. One thing in the lowering did
move: the **value of an assignment is lowered before the path is formed**, because a take-out is part of the *access*
and BACKEND 2.3 says an access begins once everything the assignment needs has been evaluated - the keys first, in
source order, then the value, then the access. Without it `grid[row][column] = failing()` would reach `failing()` at
two different moments depending on whether the write went through the taken-out element or through the step.

**The panic is the language's.** `a[index]` out of range panics `index 9 is out of bounds for a length of 3`, because
that is what the `at` of `ArrayList` and of `Array` says. So `PathStep.Element` carries a `LocationId`, which the pass
reads **out of the body the call would have run** - the site of the one `panic` of `at` - and
`torb_list_element_address` panics with the index and the length there. A reader whose body does not have that one
panic is not rewritten at all.

**The cost it removed.** `benchmarks/nested-write` writes 3 840 000 cells of an 800 x 800 grid. It paid
**7 688 813 allocations and 31.7 GB copied** for it, against 803 allocations and 5.1 MB in C. Section 4 has what it
pays now: **8 813 allocations**, every one of them the construction of the grid, and 64% of the time gone.

**What is left.** The row of a `List<List<Int>>` is still an `Object` box, because it goes *into* the outer list and
the devirtualization poisons a value that is built into a container. So a write is one `makeUnique` of that box at the
element's address and one witness call through it - no copy and no allocation, and one indirect call. Boxing **at the
use instead of at the class** (finding 2, "what is left on the table") is what would take the rest.

**The risk.** An interior pointer is valid only while no other write to the container can happen, which is what
exclusivity guarantees (BACKEND 2.3); the pass asks the IR the same question once more over the window it rewrites.

**The test.** `compiler/tests/elements.test.trb` pins both halves: the shapes that lose the round trip, and a
container two implementations meet in, which keeps it. `tests/conformance/nested-write.trb` runs every shape of the
write with the leak gate on and proves copy on write - a copy taken before a write still reads what it read - and
`tests/conformance/element-place-panic.trb` pins that a **write** out of range says what a read says.

### F4. A field of a record in a list is one store through the list

**Pattern.** `points[index].y = value` - how every entity-component loop is written.

**What was generated.** `Index.at` into a temporary, `write %8.y`, then `MutableIndex.set` back - both of them a
direct call since round P5, and still a round trip that copies the record in each direction:

```text
%8 = call t_std_..._Index_at__...(%7 borrowed last, %4 borrowed)
write %8.y = %12 borrowed
makeUnique %0
call n_std_x2f_collections_list_ArrayList_set__...(&%0 borrowed, %4 borrowed, %8 borrowed)
```

**What is generated now.** The same `Element` step as finding 3, and the record never moves:

```text
%7 = call t_std_..._Index_at__...(%0 borrowed, %4 borrowed)
%8 = read %7.x
%9 = constant s_literal__Int64_1
%10 = intrinsic add.i64 %8, %9
makeUnique %0
write %0[%4].y = %10 borrowed
```

The `makeUnique %0` is the ordinary one the ownership pass inserts for every counted owner on the path of a write; the
element itself is an inline record and carries no count, so there is nothing else to make unique.

**The cost it removed.** `benchmarks/record-write` writes 30 000 000 fields: **958 398 microseconds to 394 118**, a
**59%** cut, with the same 23 allocations - what went away is not a block but the read, the copy and the second
bounds-checked call per write. Against its C twin it is **12.61x**, where it was 28.71x before the round and 75.60x
before round P5.

**What is left.** The read on the right-hand side (`made[index].x`) is still `Index.at`, which is a call, a bounds
check and a copy of the whole record for one field. That is the *read* half, and it is the open half of finding 8 plus
the `Option` of `get` - not this finding.

**The risk.** As finding 3.

**The test.** As finding 3, with `benchmarks/record-write` as the budget.

### F5. `for` over a collection: the cursor is a record on the frame

**Pattern.** Every `for` over anything that is not a range - the most written construct in the language.

**What was generated.**

```text
%3 = callWitness value %0 bound 0 member 5()     # iterate(): an Object(Iterator<T>) on the heap
b1:
  makeUnique %3                                  # every turn
  %5 = callWitness value %3 bound 0 member 0()   # next(): an indirect call answering an Option
  %6 = tag %5
  switch %6 case 0 b2, otherwise b4
```

Round P5 had already removed all of that for an `Iterate` **nothing boxes**, and the containers of `std/` were the
case that was left: `ArrayList.iterate` is a member of the `Iterate` table, a table member's signature may not move,
and so the member answered the boxed `Iterator<Item>` however concrete its receiver was.

**What is generated now.**

```text
%2 = call t_std_x2f_collections_list_ArrayList_iterator__Int64_x24_direct(%0 owned last)
b1:
  %4 = call t_std_x2f_collections_list_ListIterator_next__Int64(&%2 borrowed)
  %5 = tag %4
  switch %5 case 0 b2, otherwise b4
```

with `slot %2 local iterator: Record(T_std_x2f_collections_list_ListIterator__Int64)`.

**How it decides.** Not by moving the frozen signature but by **copying the member**. `devirtualizeProgram` runs the
whole analysis twice, and between the two runs `specializeFrozenCallees` copies every frozen function that a **direct**
call already names and whose signature holds an `Object` at all, under a name no table holds
(`..._x24_direct`, where `_x24_` is how the mangler writes a `$` and no source name can produce one). The direct call
sites are pointed at the copy; the table keeps the original, so the erased ABI is untouched and a program that really
dispatches pays exactly what it paid. The second run then reads the copy's result and its parameters as ordinary
locations, and the class of the cursor has one producer - the `TraitValue` of `ListIterator<Item>` inside the copy - so
it loses its box like any other.

A copy is only made for a callee that is called, which is what keeps it from being dead code: eleven copies in the
two-file program of `compiler/tests/lower.test.trb`.

**What it costs in code size.** The compiler's own program holds **1 021** copies and is **1 682 633 bytes** of C
larger for them - 2.6% of 64 MB, and the whole of what this round's code generation added. **334 of the 1 021 gained
nothing**: their signature came out of the second run exactly as it went in, because the class they are in was poisoned
for some other reason. A round that wants those 0.5 MB back can have them cheaply - the copies are appended after every
other function and nothing but a `Call` this pass wrote ever points at one, so compacting them away is a remap of a
contiguous tail of `FunctionId`s over the `Call` instructions and nothing else. It is the same ledger finding 12 is
about, and it belongs in that round.

**What it covers.** A list, a map, a set, a `String`'s `chars()` and every user `Iterate`. `Iterate.filter` is a
table member too, so a stage of a pipeline is answered as a `Record(Filtered)` now rather than a box.

**Level 3 turned out to be unnecessary.** The plan was a dominator walk that hoists a `makeUnique` out of a loop head.
There is nothing left to hoist: a concrete cursor is an `inline` record, and `dropUncountedMakeUnique` removes the
`makeUnique` of a place that carries no count at all. A loop head holds one only where the cursor is still a box, which
is where the dispatch is real.

**The cost it removed.** One allocation per loop, one `makeUnique` and one indirect call per turn.
`benchmarks/list-iterate` allocates **24 blocks** for 40 loops where it allocated 64 - the same 24
`benchmarks/list-index` allocates for the same data - and `benchmarks/pipeline` 85 where it allocated 105.

**What is left.** The `Option` per turn and the cross-unit `torb_list_get`, neither of which gcc can see through, so
`list-iterate` is still **44x** against a pointer walk that vectorizes. That is round P11 (a `for` over a concrete list
as a counted loop over `Element` steps) plus the standard library question of an `Iterator` that answers "is there one"
and "the value" separately.

**The test.** `compiler/tests/devirtualize.test.trb` pins all three halves: the cursor of a `for` whose iterable is
concrete is a record on the frame although a table names the member, the member the table names keeps the box it
promised, and a loop over a value that two implementations meet in keeps its dispatch, its box and its `makeUnique`.

### F6. An interpolation is one allocation, because a part may still be a number

**Pattern.** `"{a} and {b}"`, and `print` of anything that is not already a string.

**What is generated.**

```text
%5 = constant s_literal__String_row_x20_
%6 = constant s_literal__String__x3a__x20_
%8 = intrinsic remainder.i64 %1, %7
%4 = intrinsic textConcat %5, %1, %6, %8
```

```c
const torb_text_part parts[4] = { { .kind = TORB_PART_TEXT, .text = s5 },
                                  { .kind = TORB_PART_SIGNED, .signed_value = s1_index },
                                  { .kind = TORB_PART_TEXT, .text = s6 },
                                  { .kind = TORB_PART_SIGNED, .signed_value = s8 } };
s4_line = torb_text_concat_parts(parts, 4);
```

No `Show.show` at all: `torb_text_concat_parts` measures the parts, allocates the result once and writes each part into
it with the same formatter its `torb_show_*` uses.

**Which parts.** Exactly the types the prelude declares `native`, which `textPartKindOf` (`compiler/src/ir/layout.trb`)
answers for: every integer width, every float width, `Bool`, `Char`, `Void`, and `String`, whose `Show` is the identity.
A second implementation of a trait for a type is an error, so no program can give one of them a `show` of its own. A
value with a `Show` the program has to call - a record, a case, a tuple, a trait-typed value - is a text before the
concatenation reads it, exactly as it was.

**The cost it removed.** `benchmarks/interpolation`: **2 000 003 allocations** for 2 000 000 interpolations, against 2
in C, where the shown shape paid 6 000 004. One allocation per interpolation is the `String` it answers, which a
language with value semantics cannot do without - the C twin writes into a stack buffer and keeps nothing. The same fix
halves a program that only builds keys: `benchmarks/map-count` allocates **50 110** blocks against 100 111.

**What holds it.** `interpolation.trb` and `floats.trb` of the conformance suite compare every shown form against stage
0 byte for byte, which is what holds the *format* rather than the type list; `runtime/tests/text_test.c` compares each
part kind against the matching `torb_show_*`; and `compiler/tests/lower-text.test.trb` pins the IR and the C.

**`print` is the shape beside it** and keeps its texts: the join with one space and one `\n` is the runtime's, and the
parts of a variadic call are not the parts of one text. `print "{a} {b}"` pays one allocation, because the
interpolation inside it is the one text.

### F7. A closure the callee cannot keep has its environment on the frame

**Pattern.** A closure written straight as the argument of a call that only calls it: `applied(1, { _ * factor })`, and
every receiver closure and DSL block.

**What is generated.**

```c
T_environment__t_closure__..._scaled_0 s1_environment = { 0 };
...
torb_environment_on_frame((torb_environment *)&s1_environment, NULL);
s1_environment.f_factor = s0_factor;
s1.code = (void (*)(void))F_t_closure__..._scaled_0;
s1.environment = (torb_environment *)&s1_environment;
s2 = t_..._applied(s1);
torb_environment_release(s1.environment);
```

The environment is a local of the frame, declared beside the slots so no `goto` crosses an initialization. It is a whole
environment in every other way: `torb_environment_on_frame` writes count one, the drop of its layout and the block kind
`TORB_BLOCK_FRAME_ENVIRONMENT`, which `torb_environment_release` answers by running out the count and running the drop
without a free. So every capture is released exactly once, and nothing is allocated.

**Which closures.** Two answers have to agree, and BACKEND's decision 14 names both: "conservatively, when it is
written directly as an argument of a call **and is not stored by the callee**".

- The checker says `local`, which is the first half: the closure stands straight as a call argument
  (`compiler/src/ir/lower/closure.trb`).
- The IR says the callee does not keep it, which is the second: the slot the `Closure` wrote is used only as an
  argument of a call no callee of which may keep that parameter, as the callee of a `CallClosure`, or by the `Release`
  this frame emitted (`framedClosures` in `compiler/src/backend/c/body.trb`).

"May keep" is a summary of its own (`compiler/src/ir/kept.trb`) and not the ownership summary. A parameter can be
`borrowed` and still be kept: an argument of a witness call or of a closure call is borrowed whatever the member at
the other end does with it (`compiler/src/ir/operand.trb`), so a callee whose body is `actions.add action` on a
trait-typed `List` stores the closure behind a borrowed parameter. The summary is a least fixpoint over the call graph:
a parameter is kept when it reaches a store, a `Construct`, a capture, a `Copy`, a `Return`, a place, an argument of a
`CallClosure`, or a parameter of a callee that keeps it. A witness call asks every member it can reach - the member at
its index in every table of the value's trait, which is the whole set because the program is closed - so
`names.contains(name)` on a `List<String>` keeps its frame environment: no `find` of a `List` table keeps its
predicate. A witness whose tables cannot be named keeps every argument. Everything that cannot be proved is proved
false.

`Iterate.map` is the case that shows why the second half is needed: the closure is `local`, and `map` builds it into a
`Mapped` record it answers, which the frame may answer on. Its parameter is `Owned`, so the environment is a block.

**The gain.** One allocation per closure creation, and the environment's fields become locals that gcc can keep in
registers. It does not show in `benchmarks/closure`, where the closure is made once outside the loop; it shows wherever
a closure is made inside one.

**What holds it.** `closure-frame.trb` of the conformance suite makes a closure with a counted capture inside a loop,
two at once, and one the callee keeps and calls after the call has ended, with the leak gate on;
`closure-kept-by-callee.trb` has callees that keep it through a trait-typed `List`, a closure call, a bound and a
trait-typed value;
`compiler/tests/lower-closures.test.trb` pins both halves of the C; `runtime/tests/memory_test.c` pins that a frame
environment costs no block and still drops its captures.

### F8. An overflow check that cannot fire is not emitted

**Pattern.** Arithmetic on a value whose range the surrounding code already decided - `index - 1` under
`if index < 2 { return index }`, and every loop counter.

**The cost it removed.** It depends on what else is going on, and the spread is the finding. A C twin of
`benchmarks/call-depth`, the same recursion three ways on the same machine:

| Variant | fib(40) |
|---------|---------|
| No checks at all | 0.21 s |
| All three operations checked, the location passed by value | 0.38 s |
| Only the addition checked - the one that can actually overflow | 0.28 s |
| All three checked, the location passed as a pointer to a static | 0.44 s |

So the checks were 1.8x here, and **the two that cannot fire are now the two the middle row leaves out**. The wall
time barely moved for it: the same recursion is 378 ms without the change and 371 ms with it, fastest of fifteen runs
each, which is inside this machine's own band for that program. The reason is in the generated C - `torb_subtract_i64`
is a `static inline` of `torb_number.h` and its `__builtin_sub_overflow` stands under `if (s0_index < 2) return ...`,
so **gcc's own value range propagation was already folding those two branches away.** What the round changes is
therefore not what gcc emits here but what the *IR* says, and that is where it will be read: the VM has no value range
propagation to fold anything with, and neither has a C compiler without the overflow builtins (the `#else` arm of
BACKEND 3.3 is four operations and a shift per addition).

**What is generated.**

```text
b2: # torbscript/benchmarks/call-depth.trb:10 join
  %4 = constant s_literal__Int64_1
  %5 = intrinsic subtract.i64.unchecked %0, %4 at torbscript/benchmarks/call-depth.trb:13:13
  %6 = call t_..._fibonacci(%5 borrowed)
  %7 = constant s_literal__Int64_2
  %8 = intrinsic subtract.i64.unchecked %0, %7 at torbscript/benchmarks/call-depth.trb:13:36
  %9 = call t_..._fibonacci(%8 borrowed)
  %10 = intrinsic add.i64 %6, %9 at torbscript/benchmarks/call-depth.trb:13:3
```

The addition of the two results keeps its check, because nothing bounds what a call answers - and it is the one that
can really overflow.

**How it decides.** `compiler/src/ir/ranges.trb`, a forward interval analysis per function, between the devirtualization
and the ownership pass. One `Interval` per slot, and three sources of facts and no others: an integer `Constant`, the
arithmetic that computed a slot, and the comparison a `Branch` stands on, which narrows both of its operands in the two
successors. The fixpoint is reverse postorder with **widening at the head of a loop** - and only over the slots that
loop writes, because widening the rest would take the bounds of an outer `_round` away inside an inner loop - followed
by a narrowing pass that reads the same equations once more from the settled answer.

**Unknown is the whole range of the type, never "no fact".** A slot the analysis does not follow holds anything its
width allows; every instruction resets what it may define before it says anything, including every slot a callee may
write through a `var` argument; the bounds are exact `Int64` arithmetic, so an operation whose result interval cannot
even be written down keeps its check. That is what makes a wrong answer impossible rather than unlikely.

**What it covers.** The increment of every `for index in 0..n`, including a nested one and the counter around it;
`index - 1` and `index - 2` under a guard; arithmetic on values the function knows exactly. What it does not:
`sum = sum + value`, a field read, a call result, and `UInt64`, whose upper bound does not fit the `Int64` the intervals
are computed in.

**The bounds check of a list** is the other half this finding promised, and `ir/bounds.trb` removes it where a loop
proves the index: `for index in 0..list.length()` over a list the body does not change reads `list[index]` as one
`Element` step with `checked` off - `torb_list_item` in C, no comparison, no copy of a shared storage - instead of the
call of the `at` of `ArrayList` and its `Option`. The proof is a forward analysis of two facts, `index >= 0` and
`index < length` (read off the guard of the loop, where `length` is the one `length()` read of a list that is written
nowhere else in the function); anything it cannot prove keeps the call and its panic. The VM keeps its comparison
(`ListItemAddress`) and drops only the copy. `tests/conformance/list-bounds-proven.trb` holds both sides.

**The risk.** A wrong range is a missing panic, which is an observable difference between the two back ends.
`tests/conformance/range-checks.trb` is the program that holds it: a guarded subtraction and a loop counter that the
analysis *does* drop, with their answers pinned, and an addition nothing bounds that still panics with
``arithmetic overflow in `+` `` at its own line. `overflow.trb`, `negate-overflow.trb` and `division-by-zero.trb` are
unchanged and still green.

**The test.** `compiler/tests/ranges.test.trb` pins both halves - the dropped check and the kept one, the guard that
bounds one end and not the other, and a division, which keeps its check because a divisor is not a width -
and `compiler/tests/lower.test.trb` pins the counted loop with `add.i64.unchecked` in its step block.

### F9. A pipeline builds one box per stage and one per stage's iterator

**Pattern.** `list.map { }.filter { }.sum()`.

**What is generated.**

```text
%1 = closure t_closure__..._0 captures() local
%2 = call t_std_..._Iterate_map__...(%0 owned last, %1 owned last)     # a Mapped record, boxed
%3 = closure t_closure__..._1 captures() local
%4 = callWitness value %2 bound 0 member 1(%3 borrowed last)            # a Filtered record, boxed
%5 = call t_std_..._Iterate_sum__...(%4 borrowed last)
```

and driving it allocates a `FilteredIterator`, a `MappedIterator` and a `ListIterator`, each boxed.

**The cost.** `benchmarks/pipeline` allocates 85 blocks for 20 pipelines against 25 for the same data read once:
**three allocations per pipeline** after round P5 made the `Filtered` stage a record instead of a box and round P6 made
the outermost cursor one, and per element two indirect calls and three `Option` round trips. The ratio against the
fused loop is **21.77x**.

**Why the three that are left.** `Iterate.map` and `Iterate.filter` are reached with `Self` bound to the **trait
type**, so their one instance builds a `Mapped`/`Filtered` record around a trait-typed `self` - a value in a **field**,
which the devirtualization may not follow, and the `source` of a stage's *iterator* is a field of the same shape. So
the stage that is answered to the caller is peeled, and what sits inside it is not.

**The fix.** Not finding 5 any more, which is done: what is left is the one place the devirtualization may not go at
all. Boxing **at the use instead of at the class** - a `TraitValue` inserted in front of the one field that needs it,
which finding 2 names as the first thing to try - is what would reach it, and it is the same rewrite `rows.add cells`
in `benchmarks/nested-write` wants. What stays even then is the `Option` per stage per element, which an `Iterator`
that answers "is there one" and "the value" separately would remove - a standard library question and not a back end
one.

**The gain.** From about twenty times a hand-written loop to whatever the `Option` round trip leaves.

**The risk.** None beyond finding 2.

**The test.** `benchmarks/pipeline` as a budget, and its allocation column, which is exact.

### F10. A map read-modify-write is two probes, and a write through `map[key]` is four

**Pattern.** `const seen = counts.get(word) ?? 0` then `counts.set word, seen + 1` - the word counter every program
has. And `byName[key].count = 5`, which round P7 left here on purpose: an `Element` step reaches the contiguous list
and nothing else, so a write through a **map** is still the take-out and the put-back of two members, with the value
copied in each direction. The interior pointer a map needs is `torb_map_slot`, and that is this round.

**What is generated.** The map is a concrete `Map(Key, Value)` since round P5 and both probes are direct calls of the
runtime's own functions, so what is left is that there are **two** of them:

```text
%11 = call n_std_x2f_collections_map_TrieMap_get__String_Int64(%1 borrowed, %7 borrowed)
...
makeUnique %1
call n_std_x2f_collections_map_TrieMap_set__String_Int64(&%1 borrowed, %7 borrowed, %14 borrowed)
```

**The cost.** `benchmarks/map-count` is **6.39x** against one probe of an open addressing table, and allocates 50 108
blocks against 50 004. Round P5 took 20% of its time off - both probes are direct calls of `torb_map_get` and
`torb_map_set` now - and what is left is that there are two probes and two hashes.

**The fix.** `Instruction.TakeOut` / `PutBack` with `RuntimeKind.Map` - the two instructions exist in the IR for exactly
this and are reached by nothing - plus a `torb_map_slot` in `runtime/map.c` that answers an interior pointer valid until
the next write to the table. The receiver is concrete now, which is what the instructions needed. For the
read-modify-write, a `Map` member that reaches the same mechanism (`getOrInsert`, or `update(key) { }`), so that a user
who writes the two-step form keeps it and a user who writes the one-step form gets one probe.

**What round P7 leaves for it.** The *shape* is written down now: `ir/elements.trb` recognizes the take-out and the
put-back of an index path and replaces them with a place, and it refuses a map because a key is not an index and
`PathStep.Element` steps into contiguous storage. A map wants its own step or the two instructions, and the pass that
finds the round trip is the same walk either way.

**The gain.** Half the probes of a map-heavy program, and the copy of the value in each direction.

**The risk.** An interior pointer into a table that rehashes is the classic bug; the pointer may not survive a write,
and exclusivity is what says no write can happen in between.

**The test.** An IR snapshot showing one `takeOut` and one `putBack` instead of two `callWitness`, and
`benchmarks/map-count` as a budget.

### F11. A static value is never counted, so a literal costs no call

**Pattern.** `numbers[index]`, and every `expect` and `panic` whose message is a literal.

**What is generated.**

```text
%2 = callWitness value %0 bound 0 member 9(%1 borrowed)
%3 = constant s_literal__String_Key_x20_does_x20_not_x20_exist
%4 = call t_std_x2f_core_option_Option_expect__Int64(%2 borrowed, %3 borrowed)
return %4 borrowed
```

No `retain` and no `release` of `%3`.

**Why.** A slot whose **every** definition is a `Constant` holds a static value of the program, and a static is emitted
with `TORB_IMMORTAL_HEADER`: its count is the sentinel that `torb_retain`, `torb_release` and `torb_make_unique` all
answer with "never counted, never freed". Such a slot is left out of the counted set (`immortalSlotsOf` in
`compiler/src/ir/liveness.trb`), so liveness has nothing to say about it, the edges carry no release of it, and the
extended verifier asks nothing about a value that nothing owns. A position that *keeps* it still says `owned`, because
that is the contract of the position; it just needs no retain to satisfy it.

Three exclusions make it exact: a **parameter** holds whatever the caller handed over; a slot a write, a `MakeUnique`, a
`TakeOut`, a `PutBack` or a `var` argument reaches stops being the static, because making an immortal value unique
copies it and the copy is an ordinary counted block; and a slot some other instruction defines on another path holds
that value there.

**The cost it removed.** One cross-unit call per list index read, per map index read and per `expect` anywhere - a call
whose body is a comparison and a return, which gcc cannot see through because `program.c` and the runtime are separate
translation units with no working LTO (finding 13).

**What holds it.** `compiler/tests/ownership.test.trb` pins a body where a static and a value the frame really owns
stand side by side, and the extended verifier would report a leak if a slot that does own something were left out.

### F12. A witness table instantiates every member, called or not

**Pattern.** Every trait-typed value. Since round P5 a collection literal is not one any more, and the tables are
emitted anyway - which is what makes this the biggest single item left in the C of a small program.

**What is generated.** `w_std_..._List__List_Int64_Int64` holds seventeen members, among them `filter`, `toList`,
`forEach`, `count`, `insert`, `removeAt`, `reverse` and `compact`. A program that only calls `add` and `length` gets an
instance and a thunk for all seventeen, because the table has to be filled.

**The cost.** The three-line program of finding 2 is **25 102 bytes of C with 24 witness thunks** in it, against 3 424
bytes and none for the same program with a function instead of a list. Round P5 took 594 bytes off it and not one
thunk, because a table nothing reaches is still emitted. At the scale of the compiler itself,
[BACKEND 6.3](BACKEND.md) measured witness thunks at 17.7% of 65 MB before they were keyed on the member rather than the
table, and generic instances at 18.1%.

**The fix.** A member index that no `CallWitness` in the whole program ever asks for, for a given trait, is dead in
every table of that trait: the entry is `NULL` and the instance is not emitted. The lowering has the whole-program view
that needs. The half that makes it a fixpoint rather than a scan is a `CallWitness` through a forwarded witness, where
the member index is decided one level up.

**A second ledger belongs to the same round.** Round P6 copies a frozen member for the direct call sites that can peel
it, and **334 of the 1 021 copies in the compiler's own program gained nothing** - 0.5 MB of C for a signature that came
out of the second run exactly as it went in. The copies are appended after every other function and nothing but a
`Call` the devirtualization wrote ever points at one, so dropping the useless ones is a remap of a contiguous tail of
`FunctionId`s and not a new analysis (finding 5).

**The gain.** Bounded above by the thunk share plus part of the instance share, and it shortens the build of the
compiler itself, which is 87 seconds of gcc today.

**The risk.** A table that a later round makes reachable from somewhere the scan did not see is a null function pointer
at run time. It needs the IR verifier to assert that every `NULL` entry is unreachable and not merely unreached.

**The test.** A size budget on the C of the smallest list program, and the fixpoint.

### F13. Reference counts cannot be inlined, and it matters less than it looks

**Pattern.** Every counted value, everywhere. `torb_retain`, `torb_release` and `torb_make_unique` are calls into
`runtime/memory.c`, and [BACKEND 6.3](BACKEND.md) measured that `-flto` does not work with this toolchain at all ("the
assembler refuses the object, `too many sections`").

**What was measured.** A copy of `runtime/` whose three fast paths are `static inline` in `torb.h`, linked against the
**same** `program.c` the suite built, fastest of five runs each:

| Program | The runtime as it is | All three inline | Only `torb_retain` inline |
|---------|---------------------:|-----------------:|--------------------------:|
| `list-iterate` | 1 064 ms | 1 131 ms (+6%) | 1 034 ms (-6%) |
| `pipeline` | 863 ms | 744 ms (**-14%**) | 856 ms (-1%) |
| `record-write` | 1 292 ms | 1 234 ms (-5%) | 1 324 ms (+2%) |
| `list-index` | 557 ms | 544 ms (-2%) | 616 ms (+9%) |

**The finding is the spread.** Inlining the fast paths is worth up to 14% and costs up to 9%, and which one depends on
whether the extra code in the hot loop is worth the call it saves. So the cross-unit call is **not** what the ratios in
section 4 are made of - the indirect calls and the copies are, and this is a measurement to redo once findings 2 to 5
have removed them, not a round to schedule now.

### F14. A module `const` of a collection is rebuilt where it is read

Already found, already fixed, and recorded here because it is the shape that keeps coming back.
[BACKEND 6.3](BACKEND.md) measured `punctuationTable` in the lexer at **1.43 microseconds per read against 0.83 for the
walk alone** before the value became one immortal block. The rule that follows from it is in section 6: a `const` whose
initializer is not static data is built once and read with a retain, in both back ends.

### F15. A recursion that is too deep is a panic, for one comparison per call

**Pattern.** Every function that calls program code - a function of the program, a closure, a member of a witness
table - begins with `TORB_CHECK_STACK(TORB_LOCATION(...))`: the address of a local against `torb_stack_limit`, which
`torb_process_start` works out once from the real bounds of the main thread's stack plus a reserve of 128 KiB for the
panic itself. A leaf, and the self-recursion in tail position that is a jump, pay nothing.

**What it costs.** On `benchmarks/call-depth`, a recursion whose whole body is a comparison, two subtractions and a
checked addition, which is the worst case there is. The same C built four ways, nine runs each, wall time with the
process start included (fastest, then median):

| Variant | fastest | median |
|---------|---------|--------|
| No check | 436 ms | 464 ms |
| The stack check, through the runtime's global | 441 ms | 481 ms |
| The stack check, through a `static` copy of the limit | 454 ms | 479 ms |
| A frame counter, `static`, incremented and compared at the entry and decremented before each return | 709 ms | 724 ms |

The check is four instructions - two loads, a `lea` and a compare - and a branch nothing takes, and it is inside this
machine's band; `run.sh` put the binary at 373-394 ms without it and 393-413 ms with it over four sessions. The
`static` copy saves nothing, so the check reads the runtime's global. A counter is **1.74x** on the same program: it
writes memory twice per call and has to be undone on every way out of a function.

**Why not the counter the design first asked for** (decided gap 8 of BACKEND.md): besides the cost, a count cannot
keep a C stack from overflowing. The same probe (`tests/conformance/stack-overflow.trb`) panics between 20 000 and 40 000 frames of a
two-slot function on this machine's 2 MiB main thread stack, so a limit of 100 000 frames would never have been
reached - the program crashed on the guard page first, with no message and an exit code of the operating system. The
comparison with the real limit is what turns that crash into a panic with exit code 101.

**What it risks.** A leaf or a function of the runtime below the last check that needs more than the reserve still
reaches the guard page, and so does a recursion inside the runtime itself (a `D_` drop function over a very deep
value). A thread other than the main one needs its own limit when tasks get threads of their own.

### F16. A range of a trait-typed list reaches the list's own `slice`

**Done (2026-09-27).** The findings below were written before; what was done, and what it measured:

- **A member that answers exactly `Self` gets a slot of the table once the program calls it** on a trait-typed value,
  behind the fixed members, the way a member with type parameters of its own does (docs/BACKEND.md 1.4). The slot
  points at `t_answer__<member>_<table>`, which calls the implementation's member - `ArrayList.slice` - and boxes its
  answer with that very table: on a trait-typed receiver `Self` is the receiver's own trait type, and the answer is a
  payload of the receiver's type again. A receiver of several bounds gets its own tables put around the payload at the
  call site. Where the devirtualization knows the payload, the call is the direct one.
- **Lazy, because the fixed slot was measured first.** A `slice` in every table of `List` and of its supertrait `Slice`
  instantiated `ArrayList.slice` and two functions around it for every item type of a program, sliced or not: the
  compiler's C grew by 5.5% (69.97 to 73.81 MB). The lazy slot grows it by 0.1% (70.03 MB), for the sixteen item
  types the compiler really slices.
- **`benchmarks/binary-formats.trb`** (section 4.1), the same `std/` built by the compiler before and after: the
  allocation count of the whole program went from **20 587 650 to 19 298 507** (1 289 143 fewer, 6.3%), the bytes
  allocated from 689.8 to 652.9 MB, and the DNS decoding - the ranges of `ByteReader.bytes` and of every text a record
  holds - from 2.065 to 1.957 seconds (fastest of 21 alternating runs, pinned to one core at high priority) and from
  2.272 to 2.084 (median), and 1.983 to 1.889 and 2.166 to 2.061 in a second sweep. gzip and SHA-256 did not move.
  `tests/conformance/list-slice-override.trb` pins the behaviour in both back ends.

What follows is the finding as it was written.

**Pattern.** `bytes[from..to]` where `bytes: Bytes` - `List<UInt8>`, a trait. Every binary format reads a run of bytes
this way, and `std/dns` did for every label and every record's data before `std/binary` (docs/design/BINARY.md
section 4).

**What is generated.** A call of the default member `List.slice`: it copies the receiver, `clear()`s the copy and
`append`s the items of the range one by one, each an indirect call and a `torb_make_unique`. `ArrayList.slice` - an
offset and a length over the same storage, `torb_list_slice`, O(1) and no allocation - is never reached: the C of
`benchmarks/binary-formats.trb` has no call of `ArrayList.sliceBetween` at all. A default member is not a slot of the
witness table (BACKEND.md, "Witness tables"), and the override in `ArrayList` does not make it one here.

**What it costs.** The first `ByteReader.limited` took a range of its input for every DNS record: 412 432 allocations
more than the hand-written reader over the benchmark's 72 000 records, all of them the copied data. A reader that holds
the same input and a window of it brought the count back to within 32 of the hand-written one.

**Where the fix goes.** The witness table of `List` for `ArrayList` (and `TrieList`) carries the override of `slice`,
or the devirtualization of F2 turns the call into the direct one where the payload is known. **Worth**: every range of
`Bytes` in the standard library, O(n) copies to O(1). **Risk**: a range then keeps its whole storage alive, which is
what `torb_list_compact` is for.

---

## 4. The benchmark table

`benchmarks/` holds one program per pattern and the C a careful C programmer would write for the same work.
`sh benchmarks/run.sh --allocations` produces this; `benchmarks/README.md` says how to read it.

Windows 11, 16 cores, gcc 13.2.0 (MinGW-W64 x86_64-ucrt-posix-seh), `-std=c11 -O2 -g0 -Wall -Wextra`. Both sides are
built by a `torb` binary directly (`TORB_COMPILER=`), **before** by the compiler of the previous commit and **after** by
the compiler round P7 produced. Times are the fastest of **nine** runs, in microseconds, net of the process floor that
`nothing.trb` measures.

**Round P7 re-measured the six programs a place, an element or a collection is in**; the six that hold none - the whole
top half of the table - were left at what the P6/P8 sweep measured, because nothing this round changed can reach them.
A row whose before and after come from different sweeps says so in the last column.

The **c** column is the after sweep's; the before sweep measured the same binaries and got numbers between 0.8x and 1.3x
of these, which is what one machine does to itself between two sweeps. **Read the torb column and the allocation
column**; the ratio says where a row stands today and not how it moved.

| Program | torb before | torb after | c | ratio after | allocations before | after | sweep |
|---------|------------:|-----------:|--:|------------:|-------------------:|------:|-------|
| `arithmetic` | 201 097 | 189 885 | 183 664 | **1.03x** | 3 | 3 | P6/P8 |
| `closure` | 156 926 | 143 397 | 160 362 | **0.89x** | 3 | 3 | P6/P8 |
| `wrapper` | 177 129 | 175 669 | 172 798 | **1.01x** | 3 | 3 | P6/P8 |
| `interpolation` | 280 192 | 270 840 | 163 574 | **1.65x** | 2 000 003 | 2 000 003 | P6/P8 |
| `call-depth` | 384 121 | 378 353 | 147 812 | **2.55x** | 3 | 3 | P6/P8 |
| `accumulate` | 4 255 | 1 347 | under 2 000 | **>0.67x** | 19 | 19 | P6/P8 |
| `map-count` | 302 997 | 312 830 | 54 061 | **5.78x** | 50 048 | 50 048 | P7 |
| `record-write` | 958 398 | **394 118** | 31 233 | **12.61x** | 23 | 23 | P7 |
| `list-index` | 299 674 | 309 942 | 23 384 | **13.25x** | 24 | 24 | P7 |
| `pipeline` | 749 717 | 814 379 | 35 514 | **22.93x** | 85 | 85 | P7 |
| `list-iterate` | 877 448 | 901 738 | 23 066 | **39.09x** | 24 | 24 | P7 |
| `nested-write` | 926 913 | **330 684** | 1 627 | **>165.34x** | 7 688 813 | **8 813** | P7 |

What round P7 moved, in the column that is a property of the program and not of the machine:

| Program | allocations | bytes copied | torb time | what the element step removed |
|---------|------------:|-------------:|----------:|-------------------------------|
| `nested-write` | 7 688 813 to **8 813** | 31.65 GB to **13.3 MB** | **-64%** | the copy of the whole row per write, and the object box that copy needed. What is left is the **construction** of the grid - 800 rows grown from nothing - so the writes themselves allocate nothing at all |
| `record-write` | unchanged at 23 | 33.55 MB, unchanged | **-59%** | the read of the element, the copy of the record in each direction and the second bounds-checked call per write. No block was ever involved: a `Point` is inline |
| `map-count`, `list-index`, `list-iterate`, `pipeline` | unchanged | unchanged | +3%, +3%, +3%, +9% | nothing - none of them writes through an index path. The moves are the machine between two sweeps, and the allocation column says so |

The C twin of `nested-write` measured 1 627 microseconds in this sweep against 9 005 in the before sweep, which is the
noise the section below warns about; the torb column and the allocation column are what this row says.

What rounds P6 and P8 moved, in the column that is a property of the program and not of the machine:

| Program | allocations | torb time | what the two rounds removed |
|---------|------------:|----------:|-----------------------------|
| `list-iterate` | 64 to **24** | **-15%** | the boxed cursor of every loop, the `makeUnique` in every loop head and the indirect `next` per turn: the same 24 blocks `list-index` allocates for the same data |
| `pipeline` | 105 to **85** | -6% | one box per pipeline - the outermost cursor is a record now. The three that are left sit in **fields** of the stages (finding 9) |
| `map-count` | 50 108 to **50 048** | -2% | the cursor of the one `for` over the map |
| `list-index` | unchanged | -10% | nothing of P6 is in it; the counter's overflow check is, and the rest is the machine |
| `call-depth` | unchanged | -2%, inside the band | the two subtractions are the plain C operator, and gcc was folding them already (finding 8) |
| `nested-write`, `record-write` | unchanged | -10%, -5% | nothing: both wait on round P7, and this much is the machine |
| every other program | unchanged | - | there is no collection and no bounded arithmetic in them |

What round P5 had moved:

| Program | torb time | what the devirtualization removed |
|---------|----------:|-----------------------------------|
| `list-index` | **-31%** | every access through the box: `Index.at` takes a `List(Int64)` and calls `torb_list_get` directly, and gcc inlines the whole chain |
| `record-write` | **-31%** | the two indirect calls of the read-copy-write; the round trip itself is finding 4 |
| `map-count` | **-20%** | the two probes are direct calls of `torb_map_get` and `torb_map_set`; that there are two of them is finding 10 |
| `pipeline` | **-13%**, 125 to 105 allocations | one boxed stage per pipeline: `Filtered` is a record now |
| `list-iterate` | **-12%** | the calls around the loop, not the loop |

What rounds P1 to P4 had moved, against the audit's own first run of the same suite:

| Program | allocations before | after | what removed them |
|---------|-------------------:|------:|-------------------|
| `accumulate` | 120 006 (20.0 GB) | **20** (1.05 MB) | P1: the participle hands the list on instead of holding it |
| `interpolation` | 6 000 004 | **2 000 003** | P4: a part that is a number needs no `String` of its own |
| `map-count` | 100 111 | **50 110** | P4: the keys of the table are an interpolation |
| `closure` | 5 | **3** | P3, and the one interpolation of its own output |
| every other program | 4 to 7 688 815 | one fewer | P4: the line each of them prints is one allocation |

Four things this table does not say on its own:

- **A ratio with a `>` in front of it is a lower bound.** The C twin of that row does a few hundred microseconds of
  work, which is inside the noise of the process floor, so the runner computes against two milliseconds instead. Both
  sides of `accumulate` are that small, which is what round P1 left of it; the allocation column is the exact
  statement there.
- **The C twins of `list-index`, `list-iterate` and `pipeline` vectorize.** gcc turns a sum over an array into a vector
  reduction, and no program whose elements come back from a call can do that. Part of those three ratios is the
  vectorization and not the dispatch; what stands between the loop and a reduction now is the bounds check and the
  `Option` of `get` and `next` (finding 8's open half and a standard library question).
- **The ratios are steadier than the absolute times, and the allocation column is exact.** Three sweeps of the suite in
  one session moved the C twin of `nested-write` between 1 and 4 114 microseconds with nothing about it changing, and
  the C twin of `call-depth` between 139 756 and 155 650. A run that shares the machine with another build moves both
  sides and moves them by different amounts. Read the allocation column first; the microseconds are there so a later run
  can be compared against the same shape of number, measured the same way.
- **Nothing here is a gate.** Section 6 says which of these numbers should become one and in which shape.

### 4.1 The standard library against itself: binary formats

`benchmarks/binary-formats.trb` has no C twin: it measures the migration of `std/dns`, `std/compression`,
`std/archive` and `std/digest` onto `std/binary` (docs/design/BINARY.md) by building the same program against the
`std/` before and after it. The two binaries run alternately, pinned to one core at high priority; seconds, the fastest
of the runs and their median.

| Measure | before, fastest | after, fastest | before, median | after, median |
|---------|----------------:|---------------:|---------------:|--------------:|
| DNS: 72 000 records decoded, sweep 1 | 1.944 | 2.051 | 2.190 | 2.373 |
| DNS: the same, sweep 2 | 2.394 | 2.410 | 2.762 | 2.745 |
| DNS alone, 144 000 records, processor time, 25 runs | 4.438 | 4.313 | 5.297 | 5.422 |
| gzip: 1 MB deflated, sweep 1 | 0.552 | 0.526 | 0.618 | 0.616 |
| gzip: the same, sweep 2 | 0.619 | 0.638 | 0.763 | 0.759 |
| gzip: 4 MB inflated, sweep 1 | 0.334 | 0.325 | 0.412 | 0.388 |
| gzip: the same, sweep 2 | 0.383 | 0.406 | 0.483 | 0.465 |
| SHA-256 of 4 MB, sweep 1 | 0.170 | 0.163 | 0.196 | 0.195 |
| SHA-256, sweep 2 | 0.184 | 0.209 | 0.232 | 0.230 |

Windows 11, 16 cores, gcc 13.2.0, `torb build` (release, `-O2`), 21 runs per sweep, on a machine that other gate runs
shared: the same binary moved by 10 to 20% between sweeps, and **no row separates before from after** - the sign of the
difference flips between the fastest and the median and between the sweeps, from -3% to +8% on the DNS and from -6%
to +6% on gzip. The column that does not move, the allocation count of the whole program, is **20 587 696 before and
20 587 652 after**, and the bytes allocated went from 691.6 to 689.8 MB. SHA-256 still reads its blocks straight from the
list (decision 9 of docs/design/BINARY.md), so its rows measure the writes of `finished` and the machine.

---

## 5. The plan

Each round is one agent's work, in this order. A round names the files it touches and the gate it has to leave green.

| Round | What | Files | Gate | Before the VM? |
|-------|------|-------|------|----------------|
| **P1** | F1: a `Write` with no steps defines its base | `ir/verify.trb`, `ir/operand.trb`, `backend/c/body.trb` | **done** | - |
| **P2** | F11: a static value is never counted | `ir/liveness.trb`, `ir/ownership.trb` | **done** | - |
| **P3** | F7: a closure environment the callee cannot keep, on the frame | `backend/c/body.trb`, `runtime/memory.c`, `runtime/include/torb.h` | **done** | - |
| **P4** | F6: a text part that is still a number | `ir/layout.trb`, `ir/lower/text.trb`, `ir/verify.trb`, `backend/c/body.trb`, `runtime/text.c` | **done** | - |
| **P5** | F2: the whole-program devirtualization | new `ir/devirtualize.trb`, `ir/lower/lower.trb`, `backend/c/body.trb`, `backend/c/emit.trb`, `runtime/map.c` | **done** | - |
| **P6** | F5: a copy of the frozen `iterate()` for the direct call sites | `ir/devirtualize.trb` | **done** | - |
| **P7** | F3 and F4: an `Element` path step into a concrete list | new `ir/elements.trb`, `ir/ir.trb`, `ir/lower/place.trb`, `backend/c/body.trb`, `runtime/list.c` | **done** | - |
| **P8** | F8: the range analysis that removes an overflow check that cannot fire | new `ir/ranges.trb`, `ir/ir.trb`, `ir/print.trb`, `backend/c/body.trb` | **done** for the arithmetic; the bounds check of `a[index]` is open | - |
| **P9** | F10: `TakeOut`/`PutBack` on the map, and the member that reaches it | `ir/lower/place.trb`, `runtime/map.c`, `std/collections` | an IR snapshot, `benchmarks/map-count` | after the VM |
| **P10** | F12: a witness member nothing calls is not emitted | `ir/witness.trb`, `ir/instances.trb`, `ir/verify.trb` | the fixpoint, a size budget | after the VM |
| **P11** | F5 level 2: a `for` over a concrete list is a counted loop | `ir/lower/statement.trb` | the conformance suite, `benchmarks/list-iterate` | after the VM |
| **P12** | F13 again, on the code P5 to P7 leave behind | `runtime/include/torb.h` | the benchmark table | after the VM |

**Before milestone 7** are P1 to P8: each of them is a property of the IR or of the runtime that the VM will read the
same way, so doing them first means the VM is written against the shape that stays. **All eight are done**, and the
rows they moved are in section 4. **After** are P9 to P12: P9 and P11 change what a construct lowers to and are better
decided once there are two back ends to answer to, P10's whole-program dead-member scan is a code-size fix that only
the C back end pays for, and P12 is a measurement whose answer P7 has just changed - the copies it removed are what
finding 13 was waiting on.

Finding 9 (the pipeline) has no round of its own: what is left of it is a trait-typed value in a **field**, which is
the one place the devirtualization may not go, and `benchmarks/pipeline` is how it is read.

**What P7 built.** A new pass, `compiler/src/ir/elements.trb`, between the devirtualization and the ownership
pass: it finds the take-out and the put-back of an index path on a concrete list and replaces them with one
`PathStep.Element`, which the C back end now emits as `torb_list_element_reference`. The step carries the static and
the site of the panic `Index.at` would have produced, read out of that body, so the message stays the one
`std/core` decides. Two
things came with it: the value of an assignment is lowered **before** its path is formed (BACKEND 2.3's own rule for
when an access begins), and a function nothing in the translation unit names is emitted with external linkage, because
a pass that removes the last call of one would otherwise make the C stop compiling. **A map keeps the round trip:** an
`Element` step steps into contiguous storage, and the interior pointer a table needs is P9's `torb_map_slot`.

**What is left of P8.** The **bounds check** half. The interval facts are there and the analysis is written down; what
is missing is a second path through `Index.at`, because the check of `a[index]` is an `Option` the standard library
builds and not an `Intrinsic` with a flag on it. Finding 8 says what such a round would have to build.

---

## 6. What makes it permanent

A number that nobody measures again goes back. Four of these belong in the gates, and one is a feature.

**An IR snapshot for each finding, in `compiler/tests/`.** The IR text form is deterministic and readable
(BACKEND 1.7), so a snapshot is the cheapest possible regression test and it fails far earlier than any C-level test.
The three that matter most:

- `x = f(x)` at the last use shows `owned last` and no `retain` (`compiler/tests/ownership.test.trb`).
- A value whose payload the program decides holds no `traitValue` and no `callWitness`, and one that escapes as the trait
  holds both (`compiler/tests/devirtualize.test.trb`).
- No `makeUnique` stands on a container the surrounding frame still holds - the "no copy on a shared row" rule.
- No loop head holds a `makeUnique`.

**An allocation budget per conformance program.** `tests/conformance/` already runs every program a second time
with `TORB_REPORT_LEAKS=1` and asserts that the live block count is zero. A **second number in the same report** - how
many blocks the program allocated in total - turns the same run into a budget: a program that starts allocating per
element where it used to allocate per loop fails the suite instead of being noticed a year later. It is one counter in
`runtime/memory.c` and one line in the report, and it is the only measurement in this document that does not move with
the machine.

**A size budget for the emitted C.** The three-line list program of finding 2 is the canary for findings 2 and 12; the
compiler's own `program.c` is the canary for everything. One line in CI that asserts both are under a number, with the
number in the file, so a change that makes it bigger has to say so.

**A benchmark ratio budget.** `benchmarks/run.sh` is not a gate today and should not become one as it is - it is wall
time on one machine, and the two lower-bound rows show how far that can be pushed. What can be a gate is the
**allocation column**, which is exact, and a ratio with a wide band (`list-iterate` under 2x rather than under 1.1x) run
on one known machine.

**`torb build --explain-copies`.** The one real weakness of copy on write is that the copy is invisible: nothing in
`grid[row][column] = value` says that it copies a row. The IR knows - a `MakeUnique` on a reference whose base is live
afterwards is exactly a copy that will happen - so the compiler can say so at the source position:

```text
note: this write copies a shared value
 --> src/main.trb:12:3
  |
12 |     rows[row][column] = rows[row][column] + 1
  |     ^^^^^^^^^^^^^^^^^
  = `rows` still holds the row, so the row is not unique and the write copies it (O(length of the row))
  = the copy goes away when the row is taken out and put back: `var cells = rows.removeAt(row)` ... `rows.insert row, cells`
```

It is a flag and not a warning, because a copy is not a mistake - it is the semantics working. What it gives a user is
the one thing value semantics otherwise hides.

---

## 7. What is not measured here

- **The VM.** Everything above is the C back end. The model of section 1 and the instance counts carry over; code size
  does not, because bytecode has no 139-byte names.
- **Peak memory.** The suite counts allocations and bytes asked for, which is exact and portable; peak resident set is
  not obtainable from a POSIX shell on Windows and is left out rather than guessed.
- **The build time of the compiler itself.** [BACKEND 6.3](BACKEND.md) measures where the C comes from; section 8
  measures what compiling it costs, and section 9 what the front end costs before the C exists.
- **Threads and tasks.** Milestone 7.3 and 7.7.

---

## 8. The build time of a large program

A C compiler compiles one translation unit on one core, and the compiler's C was one unit of 60 MB, its test suite's
one of 87 MB. [BACKEND 4.1](BACKEND.md) cuts such a program into a unit of data and up to sixteen units of code that
compile at the same time, optimizes them across each other at the link with gcc's `-flto`, and keeps every object and
the binary in a cache. Measured on the machine of this document (16 threads, 32 GB, gcc 13.2.0), with other work
running beside it, so a number is good to about ten percent.

| What | one unit (before) | units (after) |
|------|-------------------|---------------|
| The C of the compiler, compiled and linked (`release`, 60 MB) | 170 s, one `cc1`, 1.6 GB | 6 s compile + 38 s link (`-flto=16`); a copy of the binary when nothing changed |
| The same, sixteen plain units without `-flto` (for comparison) | | 25 s, sixteen `cc1`, 3.2 GB together |
| The compiler building itself (front end + C) | 353 s | about 230 s; 182 s when its C did not change |
| `tools/bootstrap.sh`, with a seed that splits, empty cache | 725 s (372 + 353) | 395 s (208 + 182) |
| `tools/bootstrap.sh` with today's seed, which does not split yet | 725 s | about 600 s (step 1 is the seed's own compile, 372 s) |
| `torb test --native compiler/tests`: the C (`dev`, 87 MB) | 141 s | 35 s (7 s compile + 25 s link) |
| `torb test --native compiler/tests`, all of it | 712 s (build 386, run 326) | 571 s (build 271, run 300) |
| A small program (`tests/conformance/adts.trb`, `release`) | 19.0 s | 12.2 s on an empty cache, 9.0 s for a new program once the runtime is cached, 8.7 s unchanged |
| `torb check compiler` with the binary that came out | 21.9 s | 23.5 s from plain units (+8%), 21.2 s with `-flto` |
| `torb build ./compiler --emit-c` with the binary that came out | 182 s | 223 s from plain units (+20%), 178 s with `-flto` |
| The compiler's tests, run (`dev`) | 326 s | 373 s from plain units (+14%), 300 to 312 s with `-flto` |
| The size of `torb.exe` | 10.6 MB | 15.3 MB from plain units, 12.9 MB with `-flto` |

So the C compile of a large program is four times as fast with `-flto` and seven times without it, and `-flto` is what
keeps the binary as fast as the one-unit binary was: sixteen plain units lose every inlining between two of them, and
the compiler's own front end ran a fifth slower. A small program is faster because the runtime is compiled once per
flag set and cached, where it used to be compiled into every binary.

What did not change: `program.c` is the same bytes as before, a program with less than 4 MB of code - every program of
the conformance suite and every example - is still one unit, and the front end (checking, lowering, emitting) is the
same 180 to 250 seconds of the compiler's own build it was. That part is now most of a bootstrap, and it is the next
lever: a faster C compile cannot make a bootstrap more than twice as fast.

---

## 9. The front end of a build

Section 8 left the front end - reading, checking, lowering and emitting the C - as most of a bootstrap, and it is all
of a small build: a conformance program took twelve seconds before its C compile started. This section measures where
that time went and records what took it away. `torb build --timings` prints the wall time of every step, the passes of
the checker first, and a sampling profiler over the release binary said which functions the time went to: every few
milliseconds it suspended the process, walked its stack with the unwind tables of the binary and counted every function
on it.

Measured on the machine of section 8 while it was fully loaded - five processes of another checkout spinning since the
day before, two gate runs, a landing and other agents' builds beside it - with the two binaries run alternately, one
right after the other. A number is good to about twenty percent; a ratio between two neighbouring runs is better.

### 9.1 What the time was spent on

| Where | Share | Fixed by |
|-------|-------|----------|
| Whether a value may hold a `Close` object (`mayHoldClose`) | two thirds of the compiler's own build | a `false` is kept for every type its walk entered |
| A write through `checker.tables[module]` | a third of the checker, 37% of a small build | the three tables of a module are an `ArrayList` |
| `torb build` checking every body of its program first | 7 of the 12 s of a small build | the bodies of other packages are checked when the lowering reaches them |
| The lexer reading a text at every character | half of lexing, a quarter of a small build | the first character is compared first |
| `closeHelperDemand` of the C emitter | 5% of the compiler's own build | two counts per round |

**Whether a value may hold a `Close` object** is asked for every temporary the lowering ends (`endTemporaries`) and
every binding it declares (`declareScoped`, `ir/lower/scope.trb`). The walk over a type's fields remembered every
`true`, and a `false` only where it had not come back to a type it was still answering - and in a recursive family of
types, the syntax tree of the compiler, it always had. So every temporary of a type of that family walked the whole
family again, with a linear `contains` on the path: 66% of the samples of `torb build ./compiler`, and its lowering
took about 240 seconds. A `false` at the top of a walk is final for every type the walk entered - each of them reached
only types that answered `false`, or the top would have answered `true` - so all of them keep it now, and the lowering
takes 30.

**A write through a trait-typed list in a field copied what the element held.** `checker.tables` was a `List<Tables>`,
and `tables[module.value].expressionTypes.set(at, id)` through the trait is the round trip of finding 3 - `Index.at`,
the change, `MutableIndex.set` - which the element step cannot take away from a field, because the devirtualization
poisons what is stored in one. The element had a second owner while it changed, so `torb_make_unique` copied the record,
and the map inside it, now held twice, was copied and rehashed by `torb_map_prepare`: the whole expression table of the
module, once for every expression the checker typed. `tables`, `typePositions` and `diagnostics` of the checker are an
`ArrayList` now, which the element step writes in place. The same shape is anywhere a field is a `List` of records or
of collections and one element is written through it; the checker's was the one that cost.

**`torb build` checked every body of its program before it lowered anything.** `check` with `checksEveryBody` checked
every module of every package the program touches, and the program of a conformance program is its whole package: each
of the 237 programs checked all 250 files of `tests/conformance/` and every module of `std` any of them imports.
`torb run` has checked on demand since [docs/design/VM.md](design/VM.md) section 8 - the bodies of what was named, and
every other module the first time the lowering enters it (`enterModule`) - and `torb build` does the same now, with the
fallback `torb run` has: a program the lowering refuses is checked whole and lowered again, so what it is told does not
change. The bodies of a small build went from 7 seconds to 5 milliseconds.

**The lexer started an iterator per character.** `textStartsAt` compares a text through `text.chars()`, a boxed
iterator, and the lexer asked it for each of the 38 entries of the punctuation table at every punctuation character, for
every character of every block comment - every doc comment of `std/` and of the compiler - and for every character of
every string literal. Each entry of the table carries its first character now, and the comment and the string compare
the star and the quote first; the parse of a small build went from 3.8 to 1.9 seconds, and the compiler's own from 11 to
6.

**`closeHelperDemand`** copied both flag lists and compared them whole for every field of every layout, which is
quadratic in the layouts. A mark only turns a flag on, so a round changed something exactly when more flags are on after
it: two counts per round, the same fixpoint in the same rounds.

**What did not change is the C.** The compiler's own C for `windows-x64` and for `linux-x64` (70 MB each, and different
from each other because an arm of `std` for Windows reaches one more instance), a conformance program's and
`tools/release-sync`'s are the same bytes from the binary before these changes and from the one after, and a build
reports what `torb check` of the same paths reports.

### 9.2 Before and after

| What | before (478f467b) | after |
|------|-------------------|-------|
| A small program, `torb build --emit-c tests/conformance/adts.trb` | 11.1 to 13.5 s: parsing 3.3 to 3.8, bodies 6.7 to 7.5 | 2.9 to 3.2 s: parsing 1.9, bodies 0.005 |
| `torb check tests/conformance/adts.trb` | 4.1 to 4.6 s | 2.7 to 3.1 s |
| A tool, `torb build --emit-c tools/release-sync` | 9.9 to 11.8 s | 6.1 to 8.2 s |
| `torb check tools/release-sync` | 3.6 to 3.9 s | 2.3 to 2.7 s |
| The compiler's own front end, `torb build --emit-c ./compiler` | 340 s: checking 48, lowering 248, ownership 7, verifying 12, emitting the C 24, writing it 2 | 88 s: checking 20, lowering 30, ownership 7, verifying 11, emitting the C 17, writing it 2 |
| `torb check compiler` | 43 s: parsing 11, bodies 28 | 21 s: parsing 6, bodies 13 |
| `sh tools/conformance.sh`, 4 jobs, both runs at the same time | 4102 s | 3427 s |

The suite gained least, because the front end is now the smaller part of a program's turn: its C compile, its run and
the leak check are the rest, and the two runs shared a machine that was loaded twice over - the run after finished
eleven minutes earlier, and the run before had the machine more to itself for those minutes. A bootstrap builds the
compiler twice, so it saves the difference of the last row but two twice once the seed is one of these binaries; until
then its first step is the seed's, as slow as before.

### 9.3 What is next

**The parse.** A small build is four fifths parsing now: the whole workspace of the program, 2.7 MB for a
conformance program (the 250 files of its package and all of `std`), at about a megabyte per second - one `Char` and
one offset per character of `SourceText`, every token appended to a list that is copied once more without its
insignificant line breaks, the text of every interpolation lexed a second time. A lexer over the bytes of the text, or
the parsed `std` kept across runs, is the next lever of every small build and of the conformance suite.

**The compiler's own build** is five steps of one size now - the lowering with the bodies of `std` it checks on the way,
emitting the C, the checker's bodies, verifying, ownership - and the profile already names a first finding in three of
them: the verifier's definedness is a `List<Bool>` per block, compared whole per round (a quarter of verifying); a
dispatched call builds the members of its witness table again (`tableMembersOf`, a fifth of the lowering); and
`settleEscapes` walks every escape check of the program again after each module the lowering checks on demand, which is
quadratic in the modules it enters. The C compile of section 8 is the larger half of a bootstrap again.

### 9.4 The second round: what the time was spent on

The list of 9.3, taken in order, on the same machine, which was loaded more heavily still - other agents' bootstraps
and gate runs, and a C compile of the compiler that took three times its usual minute. So this round reads the
**processor time** of each run next to its wall time (user and kernel, from `GetProcessTimes`): a loaded machine
stretches the wall time of a run by whatever else runs beside it, and its processor time far less. The two binaries
still alternate run by run, and a ratio is taken between neighbours. The profiler of round one gained an unwinder of its
own - it copies the image of the process once and walks each stack with the image's unwind tables itself - which takes
a sample in microseconds where `StackWalk64` took milliseconds.

| Where | Share | Fixed by |
|-------|-------|----------|
| Lexing: a decoded copy of every character, each read through a witness table, the token list copied once more | half of parsing, and parsing was four fifths of a small build | a lexer over the bytes of the text |
| Looking at the current token: a read of the list per look | 6% of parsing | the parser keeps the current token in a field |
| Whether a task may take a value (`taskProblemOf`) | 7% of the compiler's own build | nothing found at the top of a walk is final for every type it entered |
| The definedness of the verifier | nine tenths of verifying the IR | a `BitSet` per block; the liveness of the ownership pass the same |
| The entry of a block in the range analysis (`entryOf`) | a third of finishing the lowering | a list of predecessors per function |
| The members of a witness table (`tableMembersOf`) | a sixth of the lowering | built once per bound and subject |
| `normalizePath` under the name of every symbol | 4% of the compiler's own build | a normalized path is answered as it is |
| What a call through a witness table may run (`traitMembersOf`) | all of `settleEscapes`, 3% | kept per member and bound until an implementation is added |
| The ownership check comparing every merged state whole | a tenth of it | the merge says whether it changed anything |

**The lexer read a decoded copy of the file, one character at a time, through a trait.** `SourceText.of` decoded every
character of a file into a `List<Char>` and wrote the byte offset of each into a `List<Int>` - a quarter of lexing, and
twelve bytes per character held for as long as the module lives - and every `peek` read that list through a `List`
field, which is a call through its witness table with two counts taken and dropped around it. The lexer reads the text
itself now: a position is a byte offset, `peek` is `String.charAtByte`, and a comment is skipped with one search for
`*/` or for the line break over the bytes; no byte of a character past its first is ASCII, so the search cannot stop
inside one. A string literal keeps its `String` reading as slices of the source between the escape sequences, the
interpolations and the indentation it loses, where it appended every character to a `List<Char>` and made a `String` of
that with one `show()` per character; its verbatim reading, which only `writtenTextOf` asks for, is not built for any
other literal. The line breaks that end no statement are dropped as the token after them arrives, instead of in a second
pass over a copy of the list, and the punctuation is a `match` on its first character, where it was a walk over a list
of 38 tuples. The parser reads its tokens from an `ArrayList`, keeps the one it stands on in a field that `bump` moves,
and answers its questions of the form "is it one of these" with a `match` instead of a list literal built per question.

Every token and every tree of the 933 `.trb` files of the repository, and of 800 generated snippets of broken strings,
comments, escapes and characters of every width, is the same from both lexers (`torb tokens`, `torb ast`). The snippets
found the one mistake the rewrite made before any file did: a character that is no punctuation and wider than a byte,
whose second byte the punctuation looked at.

**`taskProblemOf` was `mayHoldClose` again.** Whether a closure may cross into a task asks whether each capture may hold
an object (`unseenInside`, `containsShared`), and both kept an answer only where the walk had not come back to a type it
was still answering. In the syntax tree of the compiler it always had, so every closure body walked the whole family of
the tree again. Round one's argument holds for both: nothing found at the top of a walk is final for every type the walk
found nothing in, because anything found below it would have been found at the top.

**The definedness was nine tenths of verifying, not a quarter.** It was a `List<Bool>` per block, intersected over the
predecessors slot by slot and compared whole every round - and `List.equals` is `indexed().all { ... }`, an iterator and
a closure per slot. A `BitSet` (`compiler/src/ir/bit-set.trb`) keeps 64 slots to an `Int` and compares word by word, and
what leaves a block is kept beside what arrives in it instead of being joined again for each successor. The liveness of
the ownership pass joins and compares the same way and turns its sets into the flags the pass reads once they settle;
the ownership check merges a state and says whether it changed, and its rounds that report nothing build no location
and list no reads.

**The range analysis asked every block whether it precedes.** `entryOf` walked all blocks of the function for every block
of every round of its fixpoint and asked each of them for its successors: quadratic in the blocks of a function. The
predecessors are a list per block now, in the order the join took them, the natural loop of a back edge walks them
instead of every block, and the entries of two rounds are compared field by field.

**`tableMembersOf` is a question with one answer per program**: a signature, the supertraits of a bound, and which
defaults the closed world overrides, which is collected once. It is kept per bound and subject. **`settleEscapes`
re-walked no escape check**: what cost was `traitMembersOf`, which walks every implementation of the program for a call
through a witness table and was asked again for every such call site each time the lowering checked a module on demand.
It is kept per member and bound with the number of implementations it saw, so a derived implementation - which the
checker adds when somebody first asks for it - makes it stale. **`normalizePath`** split and joined every path it was
handed, and spelling the name of a symbol asks it for the path of the symbol's module three times (`moduleComponentOf`);
a path that is normalized already is answered as it is.

**What did not change is the C, and what a check reports.** The compiler's own C for `windows-x64` and for `linux-x64`,
and the C of five conformance programs and of `tools/release-sync`, are the same bytes from the binary before this round
and from the one after, and so is every token and every tree of the repository.

**Measured and not taken: the counts inline.** `torb_retain` and `torb_release` are calls into `runtime/memory.c`, and a
fifth of the samples of the compiler's own build are in them. A fast path of each, `static inline` in `torb.h` - a block
no other thread can see is one increment or decrement, everything else the call - took 5 to 10% of the processor time of
every case below. Finding 13 measured the same change on the benchmarks at up to 14% faster and up to 9% slower
depending on the loop, so it stays a measurement to redo against the benchmarks rather than a change of this round.

### 9.5 Before and after, the second round

The processor time of each case, the mean of three runs (two for the compiler's own), with the binary of round one
and the binary of round two alternating:

| What | round one | round two | ratio |
|------|----------:|----------:|------:|
| A small program, `torb build --emit-c tests/conformance/adts.trb` | 2.70 s | 1.39 s | 1.9x |
| `torb check tests/conformance/adts.trb` | 2.66 s | 1.34 s | 2.0x |
| A tool, `torb build --emit-c tools/release-sync` | 6.31 s | 4.18 s | 1.5x |
| `torb check tools/release-sync` | 2.23 s | 1.11 s | 2.0x |
| The compiler's own front end, `torb build --emit-c ./compiler` | 79.3 s | 49.3 s | 1.6x |
| `torb check compiler` | 19.3 s | 9.8 s | 2.0x |
| `torb parse std compiler tests`, 830 files | 5.66 s | 2.19 s | 2.6x |

The steps of one pair of `torb build --emit-c ./compiler` from the same runs, in wall time on the loaded machine - which
is why they add up to more than the processor time above, and why the step nobody touched, emitting the C, is the one to
read them against:

| Step | round one | round two |
|------|----------:|----------:|
| lexing, parsing and the module graph | 11.0 s | 3.7 s |
| the bodies the checker checks | 15.9 s | 7.5 s |
| checking, all passes | 28.4 s | 12.3 s |
| lowering | 39.2 s | 17.1 s |
| ownership | 7.4 s | 6.0 s |
| verifying | 14.5 s | 5.9 s |
| emitting the C | 18.9 s | 18.7 s |
| writing the C | 2.6 s | 2.6 s |

At a quieter moment the build of round two took 45 s of wall time: checking 8.1 (parsing 1.6, bodies 5.7), lowering
13.3, ownership 4.5, verifying 3.5, emitting the C 13.1, writing it 2.0. A small build is about a second, of which the
parse is still two thirds.

### 9.6 What is next

**The parse, still.** It is two thirds of a small build: 2.7 MB at two and a half megabytes a second, where round one
read one. What is left of it is how the parser handles tokens and trees, not how the lexer reads characters - every
token is a counted record and every look at its kind takes a count and drops it, every node an allocation. Two levers do
not depend on any of that: the files of a program are independent, so they can be parsed in parallel and handed back in
order (`std/parallel` does exactly that, if the tree may cross back as the copy it makes), and the parsed `std` could be
kept between runs.

**`List.equals` is an iterator and a closure per item.** The verifier, the range analysis and the ownership check no
longer ask it; a loop in `std/collections` would take the cost away from every program that compares lists - a change of
`std`, and so of the C of every program that does.

**The compiler's own build** is now emitting the C, the lowering and the checker's bodies, a third each of what remains,
with ownership and verifying at a tenth. The profile names `Iterate.joined` over the lines of the program - 70 MB of C
joined twice, once for `program.c` and once more per unit, pairwise in `log n` passes - at 4% of it, which one copy into
a buffer of the right size would take; and the range analysis at 6%, most of it reading and appending the
`List<Interval>` of each block through its witness table.

**A write through a trait-typed list** - `self.tables[module].types.set(at, id)` where `tables` is a `List` field - is
still the round trip of finding 3: `Index.at`, the change, `MutableIndex.set`, all through the witness table, with
the element owned twice while it changes, so that the change copies it. Round one fixed the one that cost by making that
field an `ArrayList`. The whole class can go in the IR instead of field by field, and not in this round, because it
takes a native and a seed: `List.update(index, change)` already exists with a default that is the round trip; `ArrayList`
would answer it natively, making its storage unique and running the change through the element's address as
`PathStep.Element` does; and the lowering would write a nested write through a container that stayed boxed as a call of
`update` whose closure is the rest of the path. What makes it more than a peephole: the closure may evaluate nothing the
assignment has not evaluated before the access (BACKEND 2.3: the keys, then the value, then the access), a key out of
range must panic at the site of `at` as the element step reads it, and the VM needs the same member. A plain take-out
and put-back is no answer: a generic container has no value of `Item` to leave behind, and `removeAt` and `insert` would
shift the list twice.
