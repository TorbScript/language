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

A collection literal is the second kind. `[1, 2, 3]` has the type `List<Int>`, and `List` is a trait, so the literal's
IR type is `Object(List<Int64>)`: a heap box holding the `torb_list`, plus one witness pointer. Finding 2 is that, and
findings 3, 4, 5, 9 and 10 wait on it.

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
| `numbers[index]` on a literal list | indirect call, `get`, an `Option`, `expect` | 0 | 0 |
| `for value in numbers` | a boxed iterator, then `MakeUnique` and an indirect call per turn | 1 per turn | **1 per loop** |
| `numbers[index] = v` | `MakeUnique`, indirect call | 1 per write | 0 |
| `grid[row][column] = v` | read the row (retain), `MakeUnique` (**copies it**), write it back | 4 per write | **2 per write** |
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
| **A list literal, used as a list** | **does not hold** | `[1, 2, 3]` is `Object(List<Int64>)`: one heap box on top of the list, and `numbers[index]` is `callWitness value %0 bound 0 member 15`. `benchmarks/list-index` is **13.12x** |
| **`for value in collection`** | **does not hold** | A heap-allocated `Object(Iterator<T>)`, then `makeUnique %3` and an indirect call every turn. `benchmarks/list-iterate` is **47.95x**, and slower than the counted index loop over the same data |
| **A field of a record in a list** | **does not hold** | `points[index].y = v` reads the element out, writes the temporary and writes it back through two indirect calls. `benchmarks/record-write` is **47.53x** |
| **A nested index write** | **does not hold** | `grid[row][column] = v` copies the whole row, because the grid still holds it: **7 688 814 allocations and 31.7 GB copied** where the C twin does 803 and 5.1 MB |
| **A pipeline of `map` and `filter`** | **does not hold** | One boxed stage record and one boxed iterator per stage: five allocations per pipeline, and one indirect call per stage per element. `benchmarks/pipeline` is **18.26x** |

Every row that does not hold has one cause: **a value whose concrete type the compiler knows is boxed behind a trait
anyway**, and everything that reads or writes through it is an indirect call the C compiler cannot see through. That is
finding 2 - the overflow checks are the one exception, and they are finding 8.

---

## 3. The findings

Ranked by (gain x frequency) / effort. Each entry says what is generated, what it costs, where the fix goes, what it is
worth, what it risks, and the test that pins it.

| # | Pattern | Cost | Fix | Effort |
|---|---------|------|-----|--------|
| 1 | `x = f(x)` at the last use | was >231x, 120 006 allocations, 20 GB copied | a `Write` with no steps defines its base | **done, round P1** |
| 2 | A collection literal is a trait-typed value | 13x to 48x, and 66 830 bytes of C where 3 748 would do | a devirtualization peephole over the IR | large |
| 3 | A nested index write copies the row | 82x, 2 allocations per write | an `Element` path step into the concrete list | medium, waits on 2 |
| 4 | A field of a record in a list | 48x | the same `Element` path step | small, waits on 3 |
| 5 | `for` over a collection | 48x, one box per loop, a `makeUnique` per turn | the concrete iterator, then a counted loop | medium, waits on 2 |
| 6 | String interpolation | was 3 allocations per interpolation | a text part that is still a number | **done, round P4** |
| 7 | A non-escaping closure environment | was one allocation per closure made | the environment on the frame | **done, round P3** |
| 8 | Overflow checks that cannot fire | 2.37x on call-heavy code, 1.3x of it removable | a local range analysis | medium |
| 9 | A pipeline of stages | 18x, five allocations per pipeline | falls out of 2 and 5 | none of its own |
| 10 | A map read-modify-write, and `map[key].field = v` | 4.89x, the key hashed twice | `TakeOut`/`PutBack` on the map | medium, waits on 2 |
| 11 | A `Release` of a literal text | was one call per list index read | a dead-count rule in the ownership pass | **done, round P2** |
| 12 | Witness members that nothing calls | 48 thunks in a three-line program | whole-program member liveness | large |
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

### F2. A collection literal is a trait-typed value, so every access is an indirect call

**Pattern.** `var x = [1, 2, 3]`, `var m: Map<String, Int> = [:]` - every collection in every program.

**What is generated.** The checker gives `[1, 2, 3]` the type `List<Int>`, which is a *trait*, so the IR type is
`Object(List<Int64>)` and the lowering coerces the freshly built `ArrayList` into a box.
`compiler/src/ir/lower/collection.trb` says so in its own module comment: "The **type of a literal is a trait-typed
value** ... it does *not* name the implementation."

```text
%1 = new List(d_Int64)
...
%0 = traitValue %1 owned last tables(table w_std_x2f_collections_list_List__List_Int64_Int64)
%17 = callWitness value %16 bound 0 member 15(%14 borrowed)
```

```c
T_payload__List_Int64 *box = (T_payload__List_Int64 *)torb_allocate(sizeof(T_payload__List_Int64), TORB_BLOCK_OBJECT);
box->f_value = s2;
s5.data = (torb_object *)box;
s5.w0 = &w_std_x2f_collections_list_List__List_Int64_Int64;
...
s17 = ((T_object__... (*)(void *, int64_t))s16.w0->members[15])(s16.data, s14);
```

**The cost.** One allocation per literal, one indirect call per access, and - because a witness table has to be filled -
every member of every trait in the bound set is instantiated whether the program calls it or not (finding 12).
`const numbers = [1, 2, 3]  print numbers.length()` emits **66 830 bytes of C in 77 definitions, 48 of them witness
thunks**. The same program with a function instead of a list emits **3 748 bytes in 3 definitions and no thunks**.

There is no way out of it in the source: `const numbers: ArrayList<Int> = [1, 2, 3]` is refused with
``a list literal of a type that is not a `List` is not supported by the native back end yet``.

**The fix.** A devirtualization peephole over the IR of one function, in a new `compiler/src/ir/devirtualize.trb` run
between `lowerWorkspace` and `insertOwnership`: a slot of type `Object(bounds)` that is **defined by a `TraitValue` in
the same function** and whose every use is a `CallWitness`, a `Read`, a `Write` or an argument of a callee that could
take the payload type, is replaced by its payload, every `CallWitness` on it by the direct `Call` of the member the
table names, and the `TraitValue` itself is deleted. Nothing about the type system changes; the pass answers a question
the IR already contains.

**The gain.** It is the enabling change for findings 3, 4, 5, 9 and 10, so its own share is the smaller half: the box
per literal, the indirect call per access, and the instances the witness table no longer has to be filled with. The
rows it moves are `list-index` (13.12x), `list-iterate` (47.95x), `record-write` (47.53x) and `pipeline` (18.26x).

**The risk.** A `TraitValue` whose slot escapes - returned, stored in a field, captured by an escaping closure, passed
to a parameter that is declared as the trait - may not be peeled. The pass has to be conservative in exactly the way the
capture analysis already is, and a wrong answer here is a wrong program and not a slow one. That is what makes it its
own round with its own gate.

**The test.** An IR snapshot in `compiler/tests/`: the IR of `var x = [1, 2, 3]  print x[1]` holds no `traitValue` and
no `callWitness`. Plus the size of the emitted C for that program as a budget.

### F3. A nested index write copies the inner container

**Pattern.** `grid[row][column] = value`, `world.entities[id].health = 1`, `rows[i].add(x)`.

**What is generated.**

```text
%16 = read %0
%17 = callWitness value %16 bound 0 member 15(%14 borrowed)   # Indexed.at - retains the row
release %16
makeUnique %17                                                # count is 2, so this COPIES the row
callWitness value %17 bound 0 member 13(%15 borrowed, %18 borrowed)
makeUnique %0
callWitness value %0 bound 0 member 13(%14 borrowed, %17 owned last)
```

**The cost.** O(width of the row) per write. `benchmarks/nested-write` writes 3 840 000 cells of an 800 x 800 grid and
pays **7 688 814 allocations and 31.7 GB copied** for it, against 803 allocations and 5.1 MB in C: **two allocations per
write** - the object box and the row's storage - and **82x** the time.

**The fix.** `compiler/src/ir/lower/place.trb` already says why it cannot be done today, in the doc comment of
`TakenElement`: "the container of a path is almost always a **trait-typed** value ... whose payload the back end may not
index". After finding 2 the container is a concrete `Runtime(List, Item)`, and the existing `Instruction.TakeOut` /
`Instruction.PutBack` with `RuntimeKind.List` - or a `PathStep.Element` after one `MakeUnique` on the outer list, which
is what BACKEND 1.6 writes down - replaces the read-copy-write round trip with an interior pointer.
`torb_list_element_reference` is already in `runtime/list.c`.

**The gain.** The whole of it: from O(row) per write to one store, and from two allocations per write to none.

**The risk.** An interior pointer is only valid while no other write to the container can happen, which is exactly what
exclusivity already guarantees (BACKEND 2.3) - so the risk is that the lowering forms the pointer *before* the arguments
have been evaluated. That is the `items.removeAt(items.length() - 1)` case, and it is already written down.

**The test.** A native program in `bootstrap/tests/native/` with a `.leaks`-style companion and an IR snapshot that
asserts no `makeUnique` stands on a row the container still holds.

### F4. A field of a record in a list is a read-copy-write round trip

**Pattern.** `points[index].y = value` - how every entity-component loop is written.

**What is generated.** `Indexed.at` into a temporary, `write %20.x`, then `MutableIndexed.set` back:

```text
%20 = callWitness value %19 bound 0 member 15(%18 borrowed)
write %20.x = %21 borrowed
makeUnique %10
callWitness value %10 bound 0 member 13(%18 borrowed, %20 borrowed)
```

**The cost.** Two indirect calls, two bounds checks and a copy of the record in each direction, for what is one store.
`benchmarks/record-write` is **47.53x**, and one of the two worst ratios in the table that are not quadratic.

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

1. After finding 2 the receiver is concrete, so `iterator()` is a direct call answering a `ListIterator<Item>` - an
   `inline` record - and `next()` is a direct call the C compiler inlines. The box and the `makeUnique` are gone.
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

**The cost.** `benchmarks/pipeline` allocates 125 blocks for 20 pipelines against 25 for the same data read once:
**five allocations per pipeline**, and per element three indirect calls and three `Option` round trips. The ratio against
the fused loop is **18.26x**.

**The fix.** Findings 2 and 5 remove the boxes and turn the three `next()` calls into direct calls that gcc can inline
into one loop, which is stage fusion without a fusion pass. What stays is the `Option` per stage per element, which an
`Iterator` that answers "is there one" and "the value" separately would remove - a standard library question and not a
back end one.

**The gain.** From about twenty times a hand-written loop to whatever the `Option` round trip leaves.

**The risk.** None beyond findings 2 and 5.

**The test.** `benchmarks/pipeline` as a budget.

### F10. A map read-modify-write is two probes, and a write through `map[key]` is four

**Pattern.** `const seen = counts.get(word) ?? 0` then `counts.set word, seen + 1` - the word counter every program
has. And `byName[key].count = 5`.

**What is generated.** For the second one, `Indexed.at`, a write to the temporary, then `MutableIndexed.set` - two
probes of the table, with the key constant materialized twice:

```text
%8 = callWitness value %7 bound 0 member 13(%6 borrowed)
write %8.count = %9 borrowed
makeUnique %0
callWitness value %0 bound 0 member 11(%6 borrowed last, %8 borrowed)
```

**The cost.** `benchmarks/map-count` is **4.89x** against one probe of an open addressing table, and allocates 50 110
blocks against 50 004.

**The fix.** `Instruction.TakeOut` / `PutBack` with `RuntimeKind.Map` - the two instructions exist in the IR for exactly
this and are reached by nothing - plus a `torb_map_slot` in `runtime/map.c` that answers an interior pointer valid until
the next write to the table. After finding 2 the receiver is concrete, which is what the instructions need. For the
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

**Pattern.** Every trait-typed value, so - until finding 2 - every collection.

**What is generated.** `w_std_..._List__List_Int64_Int64` holds seventeen members, among them `filter`, `toList`,
`forEach`, `count`, `insert`, `removeAt`, `reverse` and `compact`. A program that only calls `add` and `length` gets an
instance and a thunk for all seventeen, because the table has to be filled.

**The cost.** The three-line program of finding 2 is 66 830 bytes of C in 77 definitions, **48 of them witness thunks**,
against 3 748 bytes in 3 definitions without the list. At the scale of the compiler itself,
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

Windows 11, 16 cores, gcc 13.2.0 (MinGW-W64 x86_64-ucrt-posix-seh), `-std=c11 -O2 -g0 -Wall -Wextra`, stage 0 built
from this worktree. Times are the fastest of five runs, in microseconds, net of the process floor that `nothing.trb`
measures (torb 72 165, c 70 538). The **allocations** column is the one that does not move with the machine; this run
shared the machine with another build of the compiler, so a row whose two sides are close - `arithmetic`, `wrapper`,
`closure` - can come out either side of 1.00x, and the rows further down carry more noise than the audit's first run
did. The column to compare a later run against is the last one.

| Program | torb | c | ratio | torb allocations | c allocations |
|---------|-----:|--:|------:|-----------------:|--------------:|
| `arithmetic` | 199 450 | 207 979 | **0.95x** | 3 | 2 |
| `wrapper` | 190 172 | 194 509 | **0.97x** | 3 | 2 |
| `closure` | 176 167 | 179 556 | **0.98x** | 3 | 2 |
| `interpolation` | 362 493 | 197 482 | **1.83x** | 2 000 003 | 2 |
| `call-depth` | 428 936 | 180 589 | **2.37x** | 3 | 2 |
| `accumulate` | 8 939 | under 2 000 | **>4.46x** | 20 (1.05 MB) | 16 (1.05 MB) |
| `map-count` | 383 286 | 78 230 | **4.89x** | 50 110 | 50 004 |
| `list-index` | 452 784 | 34 490 | **13.12x** | 25 | 3 |
| `pipeline` | 872 349 | 47 755 | **18.26x** | 125 | 3 |
| `record-write` | 1 355 326 | 28 513 | **47.53x** | 24 | 3 |
| `list-iterate` | 1 084 314 | 22 612 | **47.95x** | 65 | 3 |
| `nested-write` | 1 273 013 | 15 486 | **82.20x** | 7 688 814 (31.7 GB) | 803 (5.1 MB) |

What rounds P1 to P4 moved, against the audit's own first run of the same suite:

| Program | allocations before | after | what removed them |
|---------|-------------------:|------:|-------------------|
| `accumulate` | 120 006 (20.0 GB) | **20** (1.05 MB) | P1: the participle hands the list on instead of holding it |
| `interpolation` | 6 000 004 | **2 000 003** | P4: a part that is a number needs no `String` of its own |
| `map-count` | 100 111 | **50 110** | P4: the keys of the table are an interpolation |
| `closure` | 5 | **3** | P3, and the one interpolation of its own output |
| every other program | 4 to 7 688 815 | one fewer | P4: the line each of them prints is one allocation |

The times moved with them: `accumulate` from 462 812 microseconds to 8 939 - a quadratic shape became a linear one -
and `interpolation` from 455 035 to 362 493. Nothing else in the suite is a shape any of the four rounds touches.

Four things this table does not say on its own:

- **A ratio with a `>` in front of it is a lower bound.** The C twin of that row does a few hundred microseconds of
  work, which is inside the noise of the process floor, so the runner computes against two milliseconds instead. Both
  sides of `accumulate` are that small, which is what round P1 left of it; the allocation column is the exact
  statement there.
- **The C twins of `list-index`, `list-iterate` and `pipeline` vectorize.** gcc turns a sum over an array into a vector
  reduction, and no program whose elements come back from a call can do that. Part of those three ratios is the
  vectorization and not the dispatch - which is an argument for finding 2 rather than against it, because a direct call
  the compiler can inline is what makes the reduction visible again.
- **The ratios are steadier than the absolute times, and the allocation column is exact.** Three runs of the suite on
  the same machine moved `call-depth` between 0.39 s and 1.00 s with its twin moving by the same factor, while its ratio
  stayed between 2.40x and 2.57x. A run that shares the machine with another build moves both sides and moves them by
  different amounts, so a ratio that is close to 1 can come out either side of it. Read the allocation column first; the
  microseconds are there so a later run can be compared against the same shape of number.
- **Nothing here is run under `cargo test`.** Section 6 says which of these numbers should become a gate and in which
  shape.

---

## 5. The plan

Each round is one agent's work, in this order. A round names the files it touches and the gate it has to leave green.

| Round | What | Files | Gate | Before the VM? |
|-------|------|-------|------|----------------|
| **P1** | F1: a `Write` with no steps defines its base | `ir/verify.trb`, `ir/operand.trb`, `backend/c/body.trb` | **done** | - |
| **P2** | F11: a static value is never counted | `ir/liveness.trb`, `ir/ownership.trb` | **done** | - |
| **P3** | F7: a closure environment the callee cannot keep, on the frame | `backend/c/body.trb`, `runtime/memory.c`, `runtime/include/torb.h` | **done** | - |
| **P4** | F6: a text part that is still a number | `ir/layout.trb`, `ir/lower/text.trb`, `ir/verify.trb`, `backend/c/body.trb`, `runtime/text.c` | **done** | - |
| **P5** | F2: the devirtualization peephole | new `ir/devirtualize.trb`, `ir/lower/lower.trb`, `cli/build.trb` | the fixpoint, the conformance suite, an IR snapshot with no `traitValue`, the size of the smallest list program | **yes** - P6 to P9 all wait on it |
| **P6** | F5 levels 1 and 3: the concrete iterator, and the hoisted `makeUnique` | `ir/ownership.trb`, `ir/lower/statement.trb` | an IR snapshot with no `makeUnique` in a loop head, `benchmarks/list-iterate` | yes |
| **P7** | F3 and F4: an `Element` path step into a concrete list | `ir/lower/place.trb`, `runtime/list.c` | a native program with a `.leaks`-style companion, an IR snapshot, `benchmarks/nested-write` | yes |
| **P8** | F8: the range analysis that removes a check that cannot fire | `ir/lower/expression.trb` or a new `ir/ranges.trb` | the panic programs of the conformance suite, `benchmarks/call-depth` | yes |
| **P9** | F10: `TakeOut`/`PutBack` on the map, and the member that reaches it | `ir/lower/place.trb`, `runtime/map.c`, `std/collections` | an IR snapshot, `benchmarks/map-count` | after the VM |
| **P10** | F12: a witness member nothing calls is not emitted | `ir/witness.trb`, `ir/instances.trb`, `ir/verify.trb` | the fixpoint, a size budget | after the VM |
| **P11** | F5 level 2: a `for` over a concrete list is a counted loop | `ir/lower/statement.trb` | the conformance suite, `benchmarks/list-iterate` | after the VM |
| **P12** | F13 again, on the code P5 to P7 leave behind | `runtime/include/torb.h` | the benchmark table | after the VM |

**Before milestone 7** are P1 to P8: each of them is a property of the IR or of the runtime that the VM will read the
same way, so doing them first means the VM is written against the shape that stays. P1 to P4 are done and the rows they
moved are in section 4. **After** are P9 to P12: P9 and P11
change what a construct lowers to and are better decided once there are two back ends to answer to, P10's whole-program
dead-member scan is a code-size fix that only the C back end pays for, and P12 is a measurement whose answer changes
once P5 to P7 have run.

Finding 9 (the pipeline) has no round of its own: it is what P5 and P6 leave behind, and `benchmarks/pipeline` is how it
is read.

---

## 6. What makes it permanent

A number that nobody measures again goes back. Four of these belong in the gates, and one is a feature.

**An IR snapshot for each finding, in `compiler/tests/`.** The IR text form is deterministic and readable
(BACKEND 1.7), so a snapshot is the cheapest possible regression test and it fails far earlier than any C-level test.
The three that matter most:

- `x = f(x)` at the last use shows `owned last` and no `retain` (`compiler/tests/ownership.test.trb`).
- No `makeUnique` stands on a container the surrounding frame still holds - the "no copy on a shared row" rule.
- No loop head holds a `makeUnique`.

**An allocation budget per conformance program.** `bootstrap/tests/native/` already runs every program a second time
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
