# Performance

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
value narrowed to another trait, a value two implementations meet in, and every parameter and result of a function
something other than a `Call` of the program reaches - a member of a witness table above all, whose thunk casts the
erased receiver to the payload and calls exactly that signature. `ArrayList.iterator` is one of those, which is why
`for` over a list still allocates an iterator (finding 5, round P6).

### 1.5 What is checked

Integer overflow, division by zero and every index are checked in every profile, because they are semantics and not
diagnostics. The check is `__builtin_add_overflow` plus a cold branch. What it costs depends entirely on what else the
code is doing: in a loop whose chain is latency-bound it is free (`benchmarks/arithmetic`), and in a recursion
whose whole body is three arithmetic operations and two calls it is **2.37x** (`benchmarks/call-depth`). Finding 8 says
which of those checks are provably unnecessary.

### 1.6 What a user pays for at the call site, in one table

| Written | Lowered to | Counts touched | Allocations |
|---------|------------|----------------|-------------|
| `a + b` on `Int` | `torb_add_i64`, inline, a cold branch | 0 | 0 |
| `point.x` on an inline record | a field read | 0 | 0 |
| `f(x)` where `x` is `Int` | a direct call | 0 | 0 |
| `f(text)` where `text` lives on | a direct call, borrowed | 0 | 0 |
| `deep.a.b.c = v`, inline all the way | one store | 0 | 0 |
| `holder.big.d = v`, `big` boxed | `MakeUnique` then one store | 1 call | 0 or 1 |
| `for index in 0..n` | a counted loop | 0 | 0 |
| `{ _ * 2 }` capturing nothing | a code pointer and `NULL` | 0 | 0 |
| `{ _ * factor }` as the argument of a call | a code pointer and an environment on the frame | 1 release | 0 |
| `{ _ * factor }` stored, returned or kept by the callee | a code pointer and a heap environment | 1 release | **1** |
| `numbers[index]` on a literal list | a direct call, `get`, an `Option`, `expect` | 0 | 0 |
| `for value in numbers` | a boxed iterator, then `MakeUnique` and an indirect call per turn | 1 per turn | **1 per loop** |
| `numbers[index] = v` | `MakeUnique`, a direct call | 1 per write | 0 |
| `grid[row][column] = v` | read the row (retain), `MakeUnique` (**copies it**), write it back | 4 per write | **2 per write** |
| `for value in numbers` where nothing else implements the trait | a concrete cursor on the frame, a direct `next` | 0 | 0 |
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
| `for index in 0..n` | **holds** | No `Range` value and no iterator: `%4 = intrinsic less.i64 %2, %3` and a counted back edge - the same three blocks the hand written `while` loop lowers to |
| A closure that captures nothing | **holds** | `s1.environment = NULL` - no allocation, and the thunk is a `static` function gcc can inline |
| Calling a closure through a parameter | **holds** | `benchmarks/closure` is **0.98x** against a C function pointer plus a context struct |
| **A closure that captures and does not escape** | **holds** where the callee is known not to keep it | `torb_environment_on_frame((torb_environment *)&s1_environment, NULL)` and no `torb_allocate`: the environment is a local of the frame. A closure a callee *stores* - which every `Iterable.map` does - is a block, because the pointer would outlive nothing |
| **`x = f(x)` at the last use** | **holds** | `%5 = call ..._appended(%0 owned last)` and no `retain` in front of it: the assignment defines `%0`, so the argument is the last use and the `makeUnique` inside the callee finds a count of one. `benchmarks/accumulate` allocates **20 blocks and copies 1.05 MB** for 60 000 appends, where the C twin does 16 and 1.05 MB |
| **A retain or release of a literal** | **holds** | A slot whose every definition is a `Constant` is left out of the counted set, so the body of `Indexed.at` holds no `torb_text_release` of the message its `expect` carries |
| **String interpolation** | **holds** | `const torb_text_part parts[4] = { { .kind = TORB_PART_TEXT, .text = s5 }, { .kind = TORB_PART_SIGNED, .signed_value = s1_index }, ... }` and one `torb_text_concat_parts`: **2 000 003 allocations** for 2 000 000 interpolations, which is the one `String` each of them answers |
| **Integer overflow checking** | **does not hold in general** | Free where arithmetic is not the bottleneck (`arithmetic` 0.95x), and **2.37x** where it is (`call-depth`). A C twin isolates it: the same recursion is 0.21 s plain, 0.38 s with all three operations checked, 0.28 s with only the addition - which is the only one that can overflow |
| **The dispatch of a collection literal** | **holds** | `[1, 2, 3]` is written as `Object(List<Int64>)` and is a `List(Int64)` by the time a back end sees it: `%0 = move %1`, and `numbers[index]` two functions away is `call n_std_..._ArrayList_get__Int64(%0 borrowed, %1 borrowed)` inside an `Indexed.at` gcc inlines. No box, no table, no indirect call |
| **A trait-typed value with one implementation** | **holds** | A local written as the trait, a result of the trait and a parameter of the trait are one class and one concrete value (`compiler/tests/devirtualize.test.trb`). A `var fn` member on one is the ordinary `var` path: `call t_..._Tally_bump(&%0 borrowed)` |
| **A list literal, used as a list** | **does not hold** | The dispatch is gone and the rest is not: `numbers[index]` is still a bounds check, an `Option` and an `expect` per element against a load, and the C twin vectorizes its sum. `benchmarks/list-index` lost **31%** of its time (28.56x to 19.70x against its twin in one session) |
| **`for value in collection`** | **does not hold** | `ArrayList.iterator` is a member of the `Iterable` table and a table member's signature may not move, so it still answers a heap `Object(Iterator<T>)` and every turn is a `makeUnique` plus an indirect call. `benchmarks/list-iterate` lost **12%**, which is what happened around the loop and not in it. A user `Iterable` that nothing boxes is already a concrete cursor and a direct `next` |
| **A field of a record in a list** | **does not hold** | `points[index].y = v` still reads the element out, writes the temporary and writes it back - two direct calls now instead of two indirect ones. `benchmarks/record-write` lost **31%** of its time |
| **A nested index write** | **does not hold** | `grid[row][column] = v` copies the whole row, because the grid still holds it: **7 688 813 allocations and 31.7 GB copied** where the C twin does 803 and 5.1 MB. The element of a `List<List<Int>>` is storage the container owns, so it keeps its box |
| **A pipeline of `map` and `filter`** | **does not hold** | Four allocations per pipeline instead of five, and one indirect call per stage per element: the stages are built around a trait-typed `self` in a **field** and their iterators come out of table members. `benchmarks/pipeline` is **28.67x** and lost 13% |

Every row that does not hold **used to** have one cause: a value whose concrete type the compiler knows, boxed behind a
trait anyway. Round P5 removed that cause wherever the program decides the payload, and the rows above are what is
underneath it: the `Option` round trip and the bounds check of an index (finding 8 and a standard library question),
the read-copy-write of an element (findings 3 and 4), and the one place the devirtualization may not go - a **member of
a witness table**, whose signature the thunk of every dispatch is built against. That last one is what `iterator()` is,
and it is why the two loop rows moved least.

---

## 3. The findings

Ranked by (gain x frequency) / effort. Each entry says what is generated, what it costs, where the fix goes, what it is
worth, what it risks, and the test that pins it.

| # | Pattern | Cost | Fix | Effort |
|---|---------|------|-----|--------|
| 1 | `x = f(x)` at the last use | was >231x, 120 006 allocations, 20 GB copied | a `Write` with no steps defines its base | **done, round P1** |
| 2 | A collection literal is a trait-typed value | was 13x to 48x, one box per literal, one indirect call per access | a whole-program devirtualization over the IR | **done, round P5** |
| 3 | A nested index write copies the row | >451x, 2 allocations per write | an `Element` path step into the concrete list | medium |
| 4 | A field of a record in a list | 52x | the same `Element` path step | small, waits on 3 |
| 5 | `for` over a collection | 66x, one box per loop, a `makeUnique` per turn | the concrete iterator, then a counted loop | medium |
| 6 | String interpolation | was 3 allocations per interpolation | a text part that is still a number | **done, round P4** |
| 7 | A non-escaping closure environment | was one allocation per closure made | the environment on the frame | **done, round P3** |
| 8 | Overflow checks that cannot fire | 2.37x on call-heavy code, 1.3x of it removable | a local range analysis | medium |
| 9 | A pipeline of stages | 29x, four allocations per pipeline | falls out of 5 | none of its own |
| 10 | A map read-modify-write, and `map[key].field = v` | 6.4x, the key hashed twice | `TakeOut`/`PutBack` on the map | medium |
| 11 | A `Release` of a literal text | was one call per list index read | a dead-count rule in the ownership pass | **done, round P2** |
| 12 | Witness members that nothing calls | 24 thunks in a three-line program, and every table emitted | whole-program member liveness | large |
| 13 | Reference counts cannot be inlined | measured: -14% to +9%, no decision | nothing yet | - |
| 14 | A module `const` rebuilt where it is read | 63% of a read, already fixed | recorded, not open | - |

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
borrowed)` inside an `Indexed.at` whose `self` is a `List(Int64)` - a call gcc inlines, which is what makes the bounds
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
all five positions - the local, the result, the temporary, the parameter, and the `self` of the `Indexed.at` instance -
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

### F3. A nested index write copies the inner container

**Pattern.** `grid[row][column] = value`, `world.entities[id].health = 1`, `rows[i].add(x)`.

**What is generated.** After round P5 the **outer** list is concrete and its `set` is a direct call; the row is an
element of a container and therefore still a trait-typed value, so the copy is unchanged.

```text
%10 = read %0
%11 = call t_std_..._Indexed_at__...(%10 borrowed last, %4 borrowed)   # the row - retains it
release %10
...
makeUnique %11                                                # count is 2, so this COPIES the row
callWitness value %11 bound 0 member 8(%7 borrowed, %15 borrowed)
makeUnique %0
call n_std_x2f_collections_list_ArrayList_set__...(&%0 borrowed, %4 borrowed, %11 owned last)
```

**The cost.** O(width of the row) per write. `benchmarks/nested-write` writes 3 840 000 cells of an 800 x 800 grid and
pays **7 688 813 allocations and 31.7 GB copied** for it, against 803 allocations and 5.1 MB in C: **two allocations per
write** - the object box of the row and the row's storage - and two to three orders of magnitude of the time.

**The fix.** `compiler/src/ir/lower/place.trb` says why it could not be done before round P5, in the doc comment of
`TakenElement`: "the container of a path is almost always a **trait-typed** value ... whose payload the back end may not
index". It is a concrete `Runtime(ListStorage, Item)` now, so the existing `Instruction.TakeOut` /
`Instruction.PutBack` with `RuntimeKind.List` - or a `PathStep.Element` after one `MakeUnique` on the outer list, which
is what BACKEND 1.6 writes down - replaces the read-copy-write round trip with an interior pointer.
`torb_list_element_reference` is already in `runtime/list.c`.

**The gain.** The whole of it: from O(row) per write to one store, and from two allocations per write to none.

**The risk.** An interior pointer is only valid while no other write to the container can happen, which is exactly what
exclusivity already guarantees (BACKEND 2.3) - so the risk is that the lowering forms the pointer *before* the arguments
have been evaluated. That is the `items.removeAt(items.length() - 1)` case, and it is already written down.

**The test.** A native program in `tests/conformance/` with a `.leaks`-style companion and an IR snapshot that
asserts no `makeUnique` stands on a row the container still holds.

### F4. A field of a record in a list is a read-copy-write round trip

**Pattern.** `points[index].y = value` - how every entity-component loop is written.

**What is generated.** `Indexed.at` into a temporary, `write %8.y`, then `MutableIndexed.set` back - both of them a
direct call since round P5, and still a round trip:

```text
%8 = call t_std_..._Indexed_at__...(%7 borrowed last, %4 borrowed)
write %8.y = %12 borrowed
makeUnique %0
call n_std_x2f_collections_list_ArrayList_set__...(&%0 borrowed, %4 borrowed, %8 borrowed)
```

**The cost.** Two calls, two bounds checks and a copy of the record in each direction, for what is one store.
`benchmarks/record-write` lost **31%** of its time when the two calls became direct (75.60x to 52.43x against its twin
in one session); what is left is the round trip itself.

**The fix.** The same as finding 3: an `Element` path step into the concrete list, so the write goes through an interior
pointer and the record never moves.

**The gain.** From about seventy times C to about one.

**The risk.** As finding 3.

**The test.** As finding 3, with `benchmarks/record-write` as the budget.

### F5. `for` over a collection allocates a boxed iterator and pays a `makeUnique` per turn

**Pattern.** Every `for` over anything that is not a range - the most written construct in the language.

**What is generated.**

```text
%3 = callWitness value %0 bound 0 member 5()     # iterator(): an Object(Iterator<T>) on the heap
b1:
  makeUnique %3                                  # every turn
  %5 = callWitness value %3 bound 0 member 0()   # next(): an indirect call answering an Option
  %6 = tag %5
  switch %6 case 0 b2, otherwise b4
```

Round P5 already removes all of that for an `Iterable` **nothing boxes**: the cursor is the concrete record on the
frame and `next` is a direct call through the place the cursor is, which is what
`compiler/tests/lower-collections.test.trb` pins. The containers of `std/` are the case that is left, and the reason is
named in the fix below.

```c
s3_iterator.data = (torb_object *)torb_make_unique(s3_iterator.data, s3_iterator.w0->size,
                                                   s3_iterator.w0->retain_children, s3_iterator.w0->drop);
s5 = ((T_std_..._Option__... (*)(void *))s3_iterator.w0->members[0])(s3_iterator.data);
```

**The cost.** One allocation per loop - `benchmarks/list-iterate` allocates 65 blocks for 40 loops where
`benchmarks/list-index` allocates 25 for the same data - and, per element, one cross-unit call plus one indirect call
plus an `Option` round trip. The ratio is **47.95x** against a pointer walk, and **2.4x slower than the counted index
loop** over the same list, which is the opposite of what a reader expects.

**The fix.** Three levels, each worth doing on its own.

1. The receiver is concrete since round P5, but `iterator()` is a **member of the `Iterable` table**, and a table
   member's signature may not move: its thunk casts the erased receiver to the payload and calls exactly that
   signature. So `ArrayList.iterator` still answers the boxed `Iterator<Item>`. Two ways out, and P6 picks one: a
   **specialized copy** of a frozen member for the call sites that know the payload - the table keeps the original -
   or `ArrayList.iterator` declaring `ListIterator<Item>` rather than the trait, which makes the answer concrete by
   construction and the table's entry a one-line coercion.
2. A `for` over a value whose concrete type is the runtime list lowers to a **counted loop over `Element` steps**, the
   way `for index in 0..n` already lowers to a counted loop with no `Range` in it
   (`compiler/src/ir/lower/statement.trb`). That is the last indirect call.
3. Independently of both: a `makeUnique` on a slot the frame is the only owner of, inside a loop whose body cannot
   share it, is a no-op after the first turn. A dominator walk in `compiler/src/ir/ownership.trb` hoists it out.

**The gain.** The single row that touches the most programs.

**The risk.** Level 2 changes what a `for` over a list that the body changes does, which the language has to answer
anyway; levels 1 and 3 change nothing observable.

**The test.** An IR snapshot with no `makeUnique` in a loop head, and `benchmarks/list-iterate` at or under the
`list-index` ratio.

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
- The IR says the callee does not keep it, which is the second: the slot the `Closure` wrote is used only as a
  `borrowed` argument of a **direct** call - and the ownership summary makes a parameter `Owned` exactly when the callee
  stores or returns it - as the callee of a `CallClosure`, or by the `Release` this frame emitted
  (`framedClosures` in `compiler/src/backend/c/body.trb`).

Everything that cannot be proved is proved false. An argument of a **witness** call is one of them, because the IR
borrows every argument of a witness call whatever the member's own modes are (`compiler/src/ir/operand.trb`) - so
`items.filter { ... }` on a `List<Item>` value keeps a block until finding 2 makes that call direct. So does an argument
of a `CallClosure`, whose callee is erased, and a `Copy`, a `Return`, a capture and a store.

`Iterable.map` is the case that shows why the second half is needed: the closure is `local`, and `map` builds it into a
`Mapped` record it answers, which the frame may answer on. Its parameter is `Owned`, so the environment is a block.

**The gain.** One allocation per closure creation, and the environment's fields become locals that gcc can keep in
registers. It does not show in `benchmarks/closure`, where the closure is made once outside the loop; it shows wherever
a closure is made inside one.

**What holds it.** `closure-frame.trb` of the conformance suite makes a closure with a counted capture inside a loop,
two at once, and one the callee keeps and calls after the call has ended, with the leak gate on;
`compiler/tests/lower-closures.test.trb` pins both halves of the C; `runtime/tests/memory_test.c` pins that a frame
environment costs no block and still drops its captures.

### F8. Overflow checks that cannot fire

**Pattern.** Arithmetic on a value whose range the surrounding code already decided - `index - 1` under
`if index < 2 { return index }`, and every loop counter.

**The cost.** It depends on what else is going on, and the spread is the finding. A C twin of `benchmarks/call-depth`,
the same recursion three ways on the same machine:

| Variant | fib(40) |
|---------|---------|
| No checks at all | 0.21 s |
| All three operations checked, the location passed by value | 0.38 s |
| Only the addition checked - the one that can actually overflow | 0.28 s |
| All three checked, the location passed as a pointer to a static | 0.44 s |

So the checks are 1.8x here, **removing the two that cannot fire recovers about half of it**, and how the source
location is passed is not the cost - the branches are. In `benchmarks/arithmetic`, where the chain is latency-bound on a
multiply and a division, the same checks cost 1.09x.

**The fix.** A local interval analysis over one function in the lowering: a `Branch` on a comparison narrows the range
of its operand in the successor, a loop counter is bounded by its own guard, and an `Intrinsic.AddChecked` whose operand
ranges cannot leave the width becomes the plain operation. It is the standard shape and it needs no new IR.

**The gain.** Up to half of the check overhead in call-heavy and index-heavy code, and nothing where the checks were
already free. It also removes bounds checks, which is the same analysis.

**The risk.** A wrong range is a missing panic, which is an observable difference between the two back ends. It has to
be conservative by construction (unknown means unbounded) and the conformance suite's `overflow.trb`,
`negate-overflow.trb` and `slice-out-of-range.trb` are what hold it.

**The test.** An IR snapshot showing `intrinsic subtract.i64` rather than the checked form in the guarded branch, and
the panic programs of the conformance suite unchanged.

### F9. A pipeline builds one box per stage and one per stage's iterator

**Pattern.** `list.map { }.filter { }.sum()`.

**What is generated.**

```text
%1 = closure t_closure__..._0 captures() local
%2 = call t_std_..._Iterable_map__...(%0 owned last, %1 owned last)     # a Mapped record, boxed
%3 = closure t_closure__..._1 captures() local
%4 = callWitness value %2 bound 0 member 1(%3 borrowed last)            # a Filtered record, boxed
%5 = call t_std_..._Iterable_sum__...(%4 borrowed last)
```

and driving it allocates a `FilteredIterator`, a `MappedIterator` and a `ListIterator`, each boxed.

**The cost.** `benchmarks/pipeline` allocates 105 blocks for 20 pipelines against 25 for the same data read once:
**four allocations per pipeline** after round P5 made the `Filtered` stage a record instead of a box, and per element
three indirect calls and three `Option` round trips. The ratio against the fused loop is **28.67x**, and the round took
13% of the time off it.

**Why only one of the five.** `Iterable.map` and `Iterable.filter` are reached with `Self` bound to the **trait type**,
so their one instance builds a `Mapped`/`Filtered` record around a trait-typed `self` - a value in a **field**, which
the devirtualization may not follow. The stage that is answered is peeled where its own consumer is concrete, which is
the one that went. The rest waits on finding 5: the three iterators are all answered by table members.

**The fix.** Finding 5 removes the remaining boxes and turns the three `next()` calls into direct calls that gcc can
inline into one loop, which is stage fusion without a fusion pass. What stays is the `Option` per stage per element,
which an `Iterator` that answers "is there one" and "the value" separately would remove - a standard library question
and not a back end one.

**The gain.** From about twenty times a hand-written loop to whatever the `Option` round trip leaves.

**The risk.** None beyond findings 2 and 5.

**The test.** `benchmarks/pipeline` as a budget.

### F10. A map read-modify-write is two probes, and a write through `map[key]` is four

**Pattern.** `const seen = counts.get(word) ?? 0` then `counts.set word, seen + 1` - the word counter every program
has. And `byName[key].count = 5`.

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

---

## 4. The benchmark table

`benchmarks/` holds one program per pattern and the C a careful C programmer would write for the same work.
`sh benchmarks/run.sh --allocations` produces this; `benchmarks/README.md` says how to read it.

Windows 11, 16 cores, gcc 13.2.0 (MinGW-W64 x86_64-ucrt-posix-seh), `-std=c11 -O2 -g0 -Wall -Wextra`. Both sides are
built by a `torb` binary directly (`TORB_COMPILER=`), **before** by the compiler of the previous commit and **after** by
the compiler round P5 produced, so the two columns differ by this round and by nothing else. Times are the fastest of
**nine** runs, in microseconds, net of the process floor that `nothing.trb` measures.

The **c** column is one number for both sides, because it is the same binary: the fastest of the eighteen runs the two
sweeps made of it. That is what makes the two ratios comparable with each other - and what makes them *not* comparable
with the table this one replaces, because the machine ran the C twins about twice as fast in this session as in the
audit's. **Read the torb column and the allocation column**; the ratio says where a row stands today and not how it
moved.

| Program | torb before | torb after | c | ratio before | ratio after | allocations before | after |
|---------|------------:|-----------:|--:|------------:|------------:|-------------------:|------:|
| `arithmetic` | 188 970 | 186 003 | 178 172 | 1.06x | **1.04x** | 3 | 3 |
| `closure` | 154 477 | 155 835 | 153 987 | 1.00x | **1.01x** | 3 | 3 |
| `wrapper` | 165 616 | 178 541 | 175 537 | 0.94x | **1.02x** | 3 | 3 |
| `interpolation` | 259 615 | 260 332 | 158 119 | 1.64x | **1.65x** | 2 000 003 | 2 000 003 |
| `call-depth` | 361 403 | 359 877 | 143 290 | 2.52x | **2.51x** | 3 | 3 |
| `accumulate` | 7 729 | 1 | under 2 000 | >3.86x | **>0.00x** | 20 (1.05 MB) | **19** |
| `map-count` | 335 158 | 267 214 | 41 800 | 8.02x | **6.39x** | 50 110 | **50 108** |
| `list-index` | 398 548 | 274 965 | 13 954 | 28.56x | **19.70x** | 25 | **24** |
| `pipeline` | 802 284 | 696 741 | 24 300 | 33.01x | **28.67x** | 125 | **105** |
| `record-write` | 1 250 692 | 867 464 | 16 544 | 75.60x | **52.43x** | 24 | **23** |
| `list-iterate` | 1 002 058 | 884 384 | 13 329 | 75.18x | **66.35x** | 65 | **64** |
| `nested-write` | 902 720 | 903 639 | under 2 000 | >451.36x | **>451.82x** | 7 688 814 (31.7 GB) | **7 688 813** |

What round P5 moved, in the column that is a property of the program and not of the machine:

| Program | torb time | what the devirtualization removed |
|---------|----------:|-----------------------------------|
| `list-index` | **-31%** | every access through the box: `Indexed.at` takes a `List(Int64)` and calls `torb_list_get` directly, and gcc inlines the whole chain |
| `record-write` | **-31%** | the two indirect calls of the read-copy-write; the round trip itself is finding 4 |
| `map-count` | **-20%** | the two probes are direct calls of `torb_map_get` and `torb_map_set`; that there are two of them is finding 10 |
| `pipeline` | **-13%**, 125 to **105** allocations | one boxed stage per pipeline: `Filtered` is a record now. The other four wait on finding 5 |
| `list-iterate` | **-12%** | the calls around the loop, not the loop: `ArrayList.iterator` is a table member and still answers a box (finding 5) |
| `accumulate` | one allocation | both sides are under the floor; the allocation column is the statement there |
| `nested-write` | unchanged | the outer list is concrete and its `set` is direct, and the copy of the row is all of the cost (finding 3) |
| every other program | unchanged | there is no trait-typed value in them |

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
  vectorization and not the dispatch, and round P5 is what makes the question askable at all: the calls are direct now,
  so what stands between the loop and a reduction is the bounds check and the `Option` of `get` (findings 8 and 5) and
  no longer a function pointer out of a table.
- **The ratios are steadier than the absolute times, and the allocation column is exact.** Four sweeps of the suite in
  one session moved the C twin of `list-iterate` between 13 329 and 28 870 microseconds with nothing about it changing,
  and the torb side of the same row between 883 725 and 1 070 142. A run that shares the machine with another build
  moves both sides and moves them by different amounts. Read the allocation column first; the microseconds are there so
  a later run can be compared against the same shape of number, measured the same way.
- **Nothing here is a gate.** Section 6 says which of these numbers should become one and in which shape.

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
| **P6** | F5 levels 1 and 3: the concrete iterator, and the hoisted `makeUnique` | `ir/witness.trb`, `ir/devirtualize.trb`, `ir/ownership.trb` | an IR snapshot with no `makeUnique` in a loop head, `benchmarks/list-iterate` | yes |
| **P7** | F3 and F4: an `Element` path step into a concrete list | `ir/lower/place.trb`, `runtime/list.c` | a native program with a `.leaks`-style companion, an IR snapshot, `benchmarks/nested-write` | yes |
| **P8** | F8: the range analysis that removes a check that cannot fire | `ir/lower/expression.trb` or a new `ir/ranges.trb` | the panic programs of the conformance suite, `benchmarks/call-depth` | yes |
| **P9** | F10: `TakeOut`/`PutBack` on the map, and the member that reaches it | `ir/lower/place.trb`, `runtime/map.c`, `std/collections` | an IR snapshot, `benchmarks/map-count` | after the VM |
| **P10** | F12: a witness member nothing calls is not emitted | `ir/witness.trb`, `ir/instances.trb`, `ir/verify.trb` | the fixpoint, a size budget | after the VM |
| **P11** | F5 level 2: a `for` over a concrete list is a counted loop | `ir/lower/statement.trb` | the conformance suite, `benchmarks/list-iterate` | after the VM |
| **P12** | F13 again, on the code P5 to P7 leave behind | `runtime/include/torb.h` | the benchmark table | after the VM |

**Before milestone 7** are P1 to P8: each of them is a property of the IR or of the runtime that the VM will read the
same way, so doing them first means the VM is written against the shape that stays. P1 to P5 are done and the rows they
moved are in section 4. **After** are P9 to P12: P9 and P11
change what a construct lowers to and are better decided once there are two back ends to answer to, P10's whole-program
dead-member scan is a code-size fix that only the C back end pays for, and P12 is a measurement whose answer changes
once P5 to P7 have run.

Finding 9 (the pipeline) has no round of its own: it is what P5 and P6 leave behind, and `benchmarks/pipeline` is how it
is read.

**What P5 left for P6 and P7.** The two rounds no longer wait on anything, and each of them now has one named obstacle.

- **P6** is a *table member* problem before it is a loop problem. `iterator()` is in the table of `Iterable`, so
  `ArrayList.iterator` is frozen: its thunk casts the erased receiver to the payload and calls exactly that signature,
  and the devirtualization may not move it. Its result is the boxed `Iterator<Item>`, so a `for` over a list still
  allocates one cursor per loop and pays a `makeUnique` and an indirect call per turn. A user `Iterable` whose
  implementation nothing boxes is already concrete today (`compiler/tests/lower-collections.test.trb` pins the loop with
  a `Record` cursor and a direct `next`), so what P6 has to add is a **specialized copy** of a frozen member for the
  call sites that know the payload - the table keeps the original, the direct call site gets the clone - or the same
  thing by construction: `ArrayList.iterator` declaring `ListIterator<Item>` rather than the trait.
- **P7** is unchanged in shape and cheaper than it was: the outer container of `grid[row][column] = v` and of
  `points[index].y = v` is a concrete `Runtime(ListStorage, Item)` now, which is exactly the receiver
  `Instruction.TakeOut`/`PutBack` and a `PathStep.Element` need. What is still a trait-typed value is the **element**
  of `List<List<Int>>`, because an element sits in storage the container owns and no slot rewrite reaches it.

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
- **The build time of the compiler itself.** [BACKEND 6.3](BACKEND.md) measures it and says what the levers are.
- **Threads and tasks.** Milestone 7.3 and 7.7.
