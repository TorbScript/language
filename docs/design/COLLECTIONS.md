# Collections

**Status: partly implemented** — C2b to C6 and `for var` (C9, section 3.11) are in the code; the words per kind
decided on 2026-09-22 (section 6b) revise C1 and C2 and land in the rename round.

One family, from the cursor to the byte buffer. This is the specification of `std/iteration`, `std/collections` and the
part of `std/core` the language itself reaches into — which traits exist, what each one is for, which words they spend,
and what has to change so that one meaning has one word. Nothing here decides a question of the runtime: every trait is
an ordinary trait and every implementation is an ordinary type.

```text
                      Iterate<Item>             Length                  (the words of section 6b,
                      iterate()                 length()                 decided 2026-09-22)
              ┌──────────────┬──────────────┼──────────────┬──────────────┐
           List<Item>     Set<Item>    Map<Key, Value>  Stack<Item>    Queue<Item>
           append         insert       set              push           enqueue
           removeAt       remove       remove           pop, peek      dequeue, peek
           + MutableIndex            + MutableIndex
           + MutableSlice

                      Accumulator<Item, Output>   add(), finish(), isDone()
                      one run of a pipeline - a type BESIDE the collections, never one of them
```

**Where the family stands.** Slices C2b, C3, C4, C5 and C6 are in: `List.sorted` answers `Self`, the checker rejects
both holes of section 4, gaps 1 and 2 of the back end are closed, every factory answers `Self`, and the run and the
container are told apart (`Accumulator` stands alone, `Collector` is merged into it). C1 and C2 are in the code as well
— `Stack` and `Queue` spend `add`/`remove`, `Collection` owns `add`, `clear`, `compact`, `count`, `contains` and the
participles — and **the owner's decision of 2026-09-22 revises both** (section 6b): every kind keeps its own words,
the `Collection` trait is deleted, and `Iterable` becomes `Iterate`. That lands in the rename round (C2c, two commits,
section 6a); until it has, the code and `docs/language` still show the C1/C2 words. `for var` (section 3.11) is
built. Sections 1 and 2 are the survey the design was argued from and are kept as written;
section 3 is the design with sections 3.2, 3.3 and the stack and queue rows of 3.9 superseded by 6b, and section 5
says which slices are still open.

- **[1. The inventory](#1-the-inventory)** — every trait and type, who uses it, and what it costs
- **[2. Where the others are](#2-where-the-others-are)** — Rust, Swift, Kotlin, Scala, Clojure, Java
- **[3. The target design](#3-the-target-design)** — the tree, the words, construction, iteration, indexing, ergonomics
- **[4. What the language must provide](#4-what-the-language-must-provide)** — numbered gaps, smallest fix each
- **[5. Slices](#5-slices)** — one agent each, with gates
- **[6a. What a rename of a name the compiler knows costs](#6a-what-a-rename-of-a-name-the-compiler-knows-costs)** — the seed, measured
- **[6b. The words per kind: the decision of 2026-09-22](#6b-the-words-per-kind-the-decision-of-2026-09-22)** — `Iterate`, one vocabulary per kind, no `Collection`
- **[6c. Commit 1 landed](#6c-commit-1-landed)** — the compiler accepts both names; where commit 2 finds the fallbacks
- **[6. Open, for the owner](#6-open-for-the-owner)**

Every declaration below was written into a probe file under `tests/language/` — where the workspace makes
`std` resolve — and run through `build/release/torb check`. A snippet marked **type checks today** was accepted as
written; where one is not, the prose names the gap of section 4 that stands in its way, and every diagnostic quoted is
the compiler's own, word for word. The fenced blocks of this page are not type checked by `docs check`, which lexes
them and nothing more; that is why the probes exist.

---

## 1. The inventory

*The survey the design of section 3 was argued from, with the greps and the probes that produced it. Where a slice of
section 5 is marked done, section 3 and section 4 say what the family spends instead; section 6b is where the words
ended up.*

### 1.1 The tree as it is

```text
std/iteration
  Iterator<Item>              next()                                                 1 member
  Iterable<Item>              iterator() + 12 lazy stages + 14 terminals            27 members
  Length                      length(), isEmpty(), isNotEmpty()                      3 members
  Accumulator<Item, Output>   add(), finish(), isDone()                              3 members
  Collector<Item, Output>     start()                                                1 member
  Stage<Input, Output>        onto(), then()                                         2 members
  Staged<Input, Item>         the driver of Iterable.through                        (public, not exported)
  Queueing<Item>              the tail of a pulled stage chain
  Mapped Filtered FilterMapped MappedWhile FlatMapped Taken Skipped TakenWhile Zipped Sorted
                              one Iterable type and one Iterator type each         (public, not exported)
  Grouping<Item, Key>         groupingBy(...) before then(...)

std/collections
  Collection<Item>            with Iterable, Length, Accumulator<Item, Self>         8 members
    List<Item>                with MutableIndex<Int, Item>, MutableSlice          18 members
      ArrayList  TrieList  ListIterator
    Set<Item>                                                                       10 members
      TrieSet  HashSet  SetIterator  HashSetIterator
    Map<Key, Value>           with MutableIndex<Key, Value>                       13 members
      TrieMap  HashMap  MapIterator  HashMapIterator
    Stack<Item>                                                                      7 members
      ArrayStack
    Queue<Item>                                                                      7 members
      ArrayQueue

std/core
  Index<Key, Value>         get(), at()            a[key]
  MutableIndex<Key, Value>  set()                  a[key] = v
  Slice                       slice()                a[from..to]
  MutableSlice                replace(), compact()   a[from..to] = v
  Array<Item, const Size>     with Iterable, Length, MutableIndex<Int, Item>
  Range RangeFrom RangeTo Bounds<Value>  RangeIterator

std/stream
  Source<Item, Failure>       next()                 the asynchronous Iterator
  Sink<Item, Failure>         add(), end()           the asynchronous Accumulator (end() was finish() then)
  Pulling Pushing Buffered Staged<Input, Item, Failure>
  Bytes = List<UInt8>

planned
  Buffer<Item>                ECS gap 9, BACKEND — an in-place write when there is one owner, swapRemove
  Window<Item>                CONCURRENCY section 6 — Length and Index, and nothing that resizes
  Parallel<Item>              CONCURRENCY section 10 — the Iterable vocabulary, terminals answer a Task
  Merge<Item, Output>         CONCURRENCY section 5 — with Collector, one associative merge
```

### 1.2 Who is bounded by what, outside its own file

The greps are the surprise of this section.

| Trait | Used as a bound or a field type outside the file that declares it |
|---|---|
| `Iterable<Item>` | everywhere — 80-odd sites across `std`, the compiler and the examples |
| `Iterator<Item>` | every `iterator()` result, and the tour's own `Countdown` |
| `Accumulator<Item, Output>` | `std/stream` (four framers), `std/json`, the tests |
| `Collector<Item, Output>` | `Iterable.collect`, `Grouping.then`, `std/stream` |
| `Length` | **nowhere as a bound.** Three `with` lists name it: `Collection`, `Array`, `Range<Int>` |
| `Collection<Item>` | **two lines, both in the tour**: `examples/tour/src/07-collections.trb:67` and `:76` |
| `Stack<Item>`, `Queue<Item>` | **the tour only**, `07-collections.trb:82` and `:233`–`248`. Nothing else in the repository constructs one |

The compiler is the largest TorbScript program that exists, and where it needs a stack or a queue it writes a `List`:
`compiler/src/ir/witness.trb:940` is `steps.removeAt(steps.length() - 1)`, `compiler/src/documentation/skill.trb:315`
is `kept.removeAt 0`, and `std/iteration/staged.trb:115` and `std/stream/src/source.trb:362` both drain a queue with
`waiting.removeAt 0` — which is O(n) per item and is the shape `ArrayQueue` exists to replace.

### 1.3 What a `List<Item>` carries, and what can be dispatched

`Iterable` declares 27 members. `torb ir` of a three-line program over a `List<UInt8>` prints the witness tables, and
they are shorter than the traits:

```text
witness w_..._Iterable__List_UInt8_UInt8 Iterable<UInt8> for List(UInt8)
  members(iterator, filter, toList, forEach) nested()
witness w_..._Slice__List_UInt8 Slice for List(UInt8) members() nested()
witness w_..._MutableSlice__List_UInt8 MutableSlice for List(UInt8) members(compact) nested(Slice)
witness w_..._List__List_UInt8_UInt8 List<UInt8> for List(UInt8)
  members(insert, removeAt, reverse, add, clear, iterator, filter, toList, forEach,
          length, isEmpty, finish, set, get, compact) nested(...)
```

**Four of `Iterable`'s 27 members are in its witness table.** The other 23 are generic (`map<Output>`, `fold<State>`,
`collect<Output>`) or answer `Self`, and neither is a witness slot: a generic member's own witnesses would have to be
appended to the table, and a thunk that boxes a `Self` result cannot know the other bounds of the value it was called
on. They are dispatched statically with `Self` bound to the *trait* type, which is what makes `List.sort` and
`List.slice` reachable on a `List<Item>` value at all (`std/collections/src/list.trb`, the doc comments of `sort` and
`slice` say so).

The consequence for this document: **`Slice`'s witness table is empty, and `MutableSlice`'s holds only `compact`.**
Both traits exist to bind an operator, not to be dispatched through.

One more number from the same dump: `element d_UInt8 UInt8 size 1 align 1`. A `List<UInt8>` stores one byte per item,
so `Bytes` already costs what a byte buffer costs.

### 1.4 One meaning, several words

| Meaning | Words today | Where |
|---|---|---|
| put an item in | `add`, `addAll`, `push`, `enqueue`, `insert`, `set`, `merge`, `union` | `Collection`, `Stack`, `Queue`, `List`, `Map`, `Set` |
| take an item out | `remove`, `removeAt`, `removeAll`, `pop`, `dequeue`, `retainAll`, `clear` | `Collection`, `List`, `Set`, `Map`, `Stack`, `Queue` |
| how many | `length`, `count` | `Length`, `Iterable` |
| the next item without taking it | `first`, `peek` | `Iterable`, `Stack`, `Queue` |
| is it in there | `contains`, `containsAll`, `containsKey`, `isSubsetOf` | `Collection`, `Map`, `Set` |
| the participle of adding | `added`, `addedAll`, `pushed`, `enqueued`, `inserted`, `updated`, `merged`, `union` | five traits |
| the participle of removing | `removed`, `removedAt`, `popped`, `dequeued` | four traits |
| join two partial results | `merge` (`Map`), `merge` (`Merge`, CONCURRENCY 5) | one word, two meanings |

Four of these are exact synonyms, with the proof in the body:

- `Stack.push` is `add`. The body of `Stack.add` is `push value`.
- `Queue.enqueue` is `add`. The body of `Queue.add` is `enqueue value`.
- `Stack.peek` and `Queue.peek` are `Iterable.first`. Both bodies are `first()`.
- `Map.merge` is `Collection.addAll`. The body of `Map.merge` is `addAll other`, and `Map.merged` is `addedAll`.
- `Set.union` is `Collection.addedAll`. The body is `addedAll other`.
- `Set.isSubsetOf(other)` is `other.containsAll(self)`. The body is `all other.contains`.

`Stack.pushed` and `Queue.enqueued` are one line each and that line is `added value`.

### 1.5 The asymmetries

1. **`sorted` is the one participle that does not answer `Self`.** `Iterable.sorted(by:)` is a lazy stage answering
   `Iterable<Item>`, and `List` does not override it. So the verb `list.sort { … }` leaves a `List` and its participle
   does not:

   ```text
   error: `Iterable<Int64>` does not implement `List<Int64>`
    --> probe.trb:4:27
     |
   4 | const sorted: List<Int> = numbers.sorted({ _ })
     |                           ^^^^^^^^^^^^^^^^^^^^^
   ```

2. **`removeAt` answers the item, `removedAt` drops it.** For a list that is defensible — `list[index]` is there to be
   read first — and it is why `Stack.popped` and `Queue.dequeued` had to answer `(Item, Self)?` instead.
3. **`Map.set` has no participle of its own name**, so the participle is `updated`. That is written down in the source
   and is the right call; it is listed here because it is the one place the rule bends.
4. **`Collection.add` is a re-declaration.** `Collection` comes `with Accumulator<Item, Self>`, which already requires
   `var fn add(value: Item)`. The trait declares it a second time.
5. **`Collection.finish` is `self`,** which is the whole of being an `Accumulator`. It is one line and it is correct.
6. **`Stack` and `Queue` each declare their own `of`,** because `List.of` cannot be shared — see 1.6.
7. **`Staged` names two types in two packages**: `std/iteration/staged.trb`'s `Staged<Input, Item>` and
   `std/stream/src/source.trb`'s `Staged<Input, Item, Failure>`. Only the second is re-exported by its `lib.trb`; the
   first is `public` and unreachable from outside its package. `std/iteration` re-exports `Queueing`, which is the same
   kind of implementation detail, and not `Staged`.
8. **Two iterator types per collection.** `MapIterator`/`HashMapIterator` and `SetIterator`/`HashSetIterator` are the
   same twenty lines twice, because `TrieMap` and `HashMap` are two `native type`s while the trie does not exist and a
   value of one is not a value of the other. The source says so.
9. **Traits with one implementation**: `Stack` (`ArrayStack`), `Queue` (`ArrayQueue`). `Slice` and `MutableSlice` have
   no dispatchable member at all.

### 1.6 The two defects that started this

**(a) `Stack.add` and `Stack.push`, `Queue.add` and `Queue.enqueue`.** Section 1.4 has the bodies. `add` comes from
`Collection`, which a stack needs so that `addAll`, a collector and a channel can fill it; `push` and `enqueue` are the
words the data structure is famous for. Both are declared, and one is implemented in terms of the other.

**(b) `List.of` builds an `ArrayList` for every implementation.** The declaration is

```trb fragment
static fn of(...items: Item): List<Item> {
  items
}
```

— a `static fn` default whose result type is the *trait* and whose body is a list literal, which is an `ArrayList`. So
`TrieList.of(1, 2)` is not a `TrieList`, and the checker says so exactly:

```text
error: Expected `TrieList<Int64>`, found `List<Int64>`
 --> probe.trb:3:29
  |
3 | const made: TrieList<Int> = TrieList.of(1, 2)
  |                             ^^^^^^^^^^^^^^^^^
```

`List.filled` has the same shape and the same defect. `Set.of`, `Map.of`, `Stack.of` and `Queue.of` avoid it by naming
the default implementation in the body (`TrieSet.from items`, `ArrayStack.from items`), which is the same bug written
so that it cannot be seen: an implementation that inherits `of` gets somebody else's type.

### 1.7 What `Collection` adds over its own supertraits

`Collection<Item> with Iterable<Item>, Length, Accumulator<Item, Self>` declares eight members. Subtract what the
supertraits already give:

| Member | What it is |
|---|---|
| `add` | already required by `Accumulator` |
| `finish` | already required by `Accumulator`; the body is `self` |
| `clear` | **its own, and required** |
| `addAll` | a default over `add` |
| `added`, `addedAll` | the participles — they need `Self`, so they need a trait |
| `contains`, `containsAll` | defaults over `any`, with `Item: Equals` |

So the honest answer is: **`Collection` adds `clear`, and it is the home of the participles.** As a *bound* it buys
nothing that cannot be written out, and the two tour lines were probed both ways:

```trb fragment
fn describe<Item>(items: Iterable<Item> & Length): String
fn fillWithSquares<Target: Accumulator<Int, Target>>(var target: Target, upTo: Int)
```

**type checks today**, and runs: the probe prints `3 items` for a list and `1 items` for a map. That is the measure of
what `Collection` is worth as a bound, and it is why section 3 keeps it for a different reason than the one it has.

## 2. Where the others are

**Rust.** `Iterator` is one required method and about seventy-five provided ones; `IntoIterator` is what `for` takes,
`FromIterator` is what `collect` targets and `Extend` is bulk `add`. There is no collection trait: a signature names
`Vec<T>`, `&[T]` or `impl IntoIterator`. `Vec::push`, `VecDeque::push_back`, `HashSet::insert` and `BinaryHeap::push`
are four words for one meaning, chosen per type.

*Taken:* one required method plus many provided ones on `Iterator` — `Iterable` is built that way. `FromIterator` is
`From<Iterable<Item>>`, and `Extend` is `addAll`. *Rejected:* having no collection trait. With value semantics a
parameter is already read-only, so `fn lookup(table: Map<String, Int>)` costs nothing and says everything; Rust needs
`&[T]` because it has to say who owns the storage. And `VecDeque` as a second concrete type is the thing `List` as a
trait makes unnecessary — see section 3.4.

**Swift.** `Sequence`, `Collection`, `BidirectionalCollection`, `RandomAccessCollection`, `MutableCollection`,
`RangeReplaceableCollection`, `SetAlgebra` — six protocols and an `Index` associated type before a type can be
iterated with an index. `Array`, `ArraySlice` and `ContiguousArray` are value types with copy-on-write, and
swift-collections adds `Deque`, `OrderedSet` and `OrderedDictionary`.

*Taken:* everything about the memory model. **A Swift `Array` is our `ArrayList`**: a value, copied on assignment,
sharing storage until a write. The slice-shares-storage rule is the same one, and `ArraySlice`'s indices-do-not-reset
trap is the one thing this language fixed by making a slice start at zero again. *Rejected:* the protocol ladder.
Swift's `Collection` is about **traversal cost** — `startIndex`, `index(after:)`, and a `BidirectionalCollection` for
walking backwards — while ours is about **filling**. Sorting the implementations by cost is what the *implementation
name* does here (`ArrayList` against `TrieList`), and a cost is not a shape. Also rejected:
`RangeReplaceableCollection` as the home of `append` — a protocol named after one method that then carries another.

**Kotlin.** `Iterable`, `Collection` (`size`, `isEmpty`, `contains`, `containsAll`, `iterator`), `MutableCollection`
(`add`, `remove`, `addAll`, `removeAll`, `retainAll`, `clear`), then `List`/`MutableList`, `Set`/`MutableSet`,
`Map`/`MutableMap`. `Sequence` is the lazy vocabulary beside the eager `Iterable` one. `ArrayDeque<E>` is a
`MutableList<E>`.

*Taken:* Kotlin's `Collection` is almost exactly the one this document keeps, and **`ArrayDeque` being a
`MutableList` rather than a trait of its own is the precedent for section 3.4.** *Rejected:* the read/write split as
two traits. `const` and `var` already say it, on the binding, which is CONCEPT's rule and needs no second name per
kind. Also rejected: two vocabularies for one pipeline — Kotlin has `map` twice, eager on `Iterable` and lazy on
`Sequence`, and a reader has to know which one is in hand. Here the stages of `Iterable` are lazy and there is one.

**Scala 2.13.** The redesign: `IterableOnce`, `Iterable`, `Seq`, `IndexedSeq`, `LinearSeq`, `Set`, `Map`, with
`IterableFactory` and `Builder` replacing `CanBuildFrom`. `mutable.Stack` was deprecated in 2.12 in favour of `List`
and reinstated in 2.13 on top of the freshly added `ArrayDeque`; `Queue` exists immutable and mutable. `+:`, `:+`,
`++`, `++:` and `:::` are five operators for prepend, append and concatenate.

*Taken:* `IterableFactory.from` is exactly the construction rule of section 3.5, and the 2.13 story is the warning
this document is written against: a collection library is redone when the *construction* mechanism is wrong, not when
a method is missing. *Rejected:* the operator zoo — CONCEPT already forbids `+` on a list, because `Add.add` and
`add(value)` would be one member. Also rejected: the strict/lazy `view` split, for Kotlin's reason. **And the
deprecation of `Stack` is the finding of section 1.2 in another language**: nothing reaches for it when a list is
there.

**Clojure.** Persistent vectors, lists, maps and sets. `conj` adds "where it is efficient" — the end of a vector, the
front of a list — and `peek`/`pop` take from the same end `conj` put it. `into` is `From<Iterable<Item>>`, and
transducers are `Stage`.

*Taken:* **`conj` is `add`, and `peek`/`pop` are the structure's own end.** Clojure is the language that already did
what section 3.3 proposes: one verb whose position the data structure decides, with the structure's name carrying the
word. Transducers are `Stage`, which `std/iteration` took already (STREAMS section 12 says so). *Rejected:* letting
one call site mean two things. `(conj [1 2] 3)` is `[1 2 3]` and `(conj '(1 2) 3)` is `(3 1 2)`, and only the runtime
value says which. Here the *type* says it: a `Stack` and a `Queue` are two traits and a signature names one.

**Java, the anti-example.** `Collection` has `add`/`remove`/`contains`/`size`. `Queue` adds `offer`/`poll`/`peek`
beside `add`/`remove`/`element`, which differ only in throwing against answering `null`. `Deque` then has, for the
head alone: `addFirst`, `offerFirst`, `push` to insert; `removeFirst`, `pollFirst`, `pop`, `remove`, `poll` to take;
`getFirst`, `peekFirst`, `element`, `peek` to look — **twelve names for three operations at one end.** And
`java.util.Stack extends Vector`, which is a synchronised list nobody wants, kept because it cannot be removed.

*Taken:* nothing. *Rejected:* all of it, and the reason it happened is worth naming. Java needed two names per
operation because it had no `Option`: one form throws and one answers `null`. This language has `Option`, so
`remove(): Item?` is the only form there is. It needed `push`/`pop` on `Deque` so that `Deque` could replace `Stack`
without changing call sites. And it needed `Deque` at all because `LinkedList` and `ArrayList` are classes rather than
implementations of one trait.

## 3. The target design

### 3.1 The tree

```text
                      Iterate<Item>             Length
                      iterate()                 length()
        ┌──────────────┬──────────────┬──────────────┬──────────────┐
     List<Item>     Set<Item>    Map<Key, Value>  Stack<Item>    Queue<Item>
     append          insert       set             push           enqueue
     removeAt        remove       remove          pop, peek      dequeue, peek
     + MutableIndex<Int, Item>  + MutableIndex
     + MutableSlice               <Key, Value>
     ArrayList                 TrieSet    TrieMap   ArrayStack    ArrayQueue
     TrieList                  HashSet    HashMap   ConsStack     BankersQueue
     RingList
```

*This is the tree after section 6b.* The first version of this section kept `Collection<Item>` between the two
reading traits and the five kinds, with `add()`, `clear()`, the participles, `contains` and `count` in it, and gave
`Stack` and `Queue` the words `add`/`remove`. The owner's decision of 2026-09-22 took `Collection` out and gave every
kind its own words; sections 3.2 and 3.3 below are kept as the argument that was made, each with a note.

### 3.2 `Collection` stays, and its job is one sentence

> **Superseded by section 6b.** The `Collection` trait is deleted: as a bound nothing needs it (section 1.7), and as
> the owner of `add` it put one word on five different meanings. `clear`, `compact` and `contains` are declared by each
> kind that has them. The argument below is kept as it was made.

**A `Collection` is a finite thing you can fill, and it is where the participles live.**

```trb fragment
public trait Collection<Item>
  with Iterable<Item>, Length
{
  /** Adds a value, in place. Where it goes is the structure's business. */
  var fn add(value: Item)

  /** Removes every value, in place. */
  var fn clear()

  /** Gives the value storage of its own, sized exactly to its length. */
  var fn compact()

  /** The size is known, so counting does not walk. */
  fn count(): Int {
    length()
  }

  var fn addAll(values: Iterable<Item>)
  fn added(value: Item): Self
  fn addedAll(values: Iterable<Item>): Self
  fn contains(value: Item): Bool where Item: Equals
  fn containsAll(values: Iterable<Item>): Bool where Item: Equals
}
```

Four decisions are in that block.

**`add` is declared here and nowhere else, and `Accumulator` is gone from the `with` list.** A collection is a
container, not a run: it has no result to give at the end and no `isDone()` to answer, so it shows neither. What
gathers a pipeline into a collection is a type **beside** it - section 3.8.

**`count()` answers `length()`.** `Iterable.count()` walks every value; on anything that knows its size that is the
wrong answer to give a reader who wrote the shorter word. A subtrait may write a supertrait's default — `Collection`
already does it for `Accumulator.finish`, and `compiler/src/ir/witness.trb:195` and
`compiler/src/semantics/checker/implementation.trb:2176` are the two places that make it work. It was probed as a
whole trait and **type checks today**, and the probe runs natively and prints `2`.

**`compact` moves here from `MutableSlice`.** Giving a value storage of its own is a property of every copy-on-write
container, not of being sliceable, and it was the only dispatchable member `MutableSlice` had (section 1.3). After the
move `Slice` and `MutableSlice` are pure operator markers, which is what `Slice` already is.

**`contains` and `containsAll` stay.** They are the only members that let a caller ask a question of any collection
without knowing which kind it is, the `Item: Equals` bound keeps them off the types that cannot answer, and `Set` and
`Map` override `contains` natively. `isSubsetOf` goes: it is `other.containsAll(self)` written backwards and nothing
calls it.

**What `Collection` is not.** It is not the bound to reach for. Section 1.7 shows both tour lines written without it,
and the rule that follows is: **a signature asks for the smallest thing it uses.** `Iterable<Item>` to read,
`Iterable<Item> & Length` to read and size, `Accumulator<Item, Self>` to fill, `Collection<Item>` when it really needs
both ends and the participles.

### 3.3 `Stack` and `Queue`: two words, not eight

> **Superseded by section 6b.** A stack spends `push`/`pop`/`peek` and a queue `enqueue`/`dequeue`/`peek`: "on top"
> and "at the back" are different meanings, and the structure's own words say which. The cost table and the
> implementations of this section are unchanged; only the words are. The argument below is kept as it was made.

Both traits are `Collection<Item>` plus one verb and one participle.

```trb fragment
public trait Stack<Item> with Collection<Item> {
  /** Builds a stack from its arguments; the last item ends up on top. */
  static fn of(...items: Item): Self where Self: From<Iterable<Item>> {
    Self.from items
  }

  /** Removes the item the structure gives next - for a stack the top - and answers it. */
  var fn remove(): Item?

  /** The participle of `remove`: the item, and the rest. */
  fn removed(): (Item, Self)? {
    var rest = self
    const taken = rest.remove()?
    Some((taken, rest))
  }
}
```

`Queue<Item>` is the same block with "front" for "top" and "the first item ends up at the front". **Type checks
today**, with two implementations of `Stack` — an array one and a persistent cons list — and the participle chain
`kept.added(3).removed()`.

**Eight words become two.** `push`, `pop`, `enqueue`, `dequeue`, `pushed`, `popped`, `enqueued`, `dequeued` and `peek`
are gone. `add` comes from `Collection`, `added` comes from `Collection`, `remove()` and `removed()` are here, and
**`peek` is `Iterable.first()`** — which is what `peek`'s body already was, and which works because both iterators run
in the order the structure hands items out.

**The type name carries the word.** `stack.remove()` and `queue.remove()` read correctly in both places because the
receiver says which end is meant, exactly as `conj` does in Clojure and `removeFirst` does not have to in Kotlin.

**The collision with `Set.remove(value)` and `List.remove(value)` is not one, and the language enforces that.** A type
may not carry both spellings, because a type has one member namespace:

```text
error: `remove` is already declared in `Both`
  --> probe.trb:28:10
   |
28 |   var fn remove(): Item? {
   |          ^^^^^^
   = A type has one namespace of members: a field, a `const`, a `fn` and a case cannot share a name
```

That is the right outcome. A type is a stack or a set, never both, and the compiler says so at the declaration rather
than leaving a reader to work out which `remove` a call means. **`take()` was the alternative and it is worse**:
`Iterable.take(amount)` is a lazy stage every pipeline uses, so `stack.take()` beside `items.take(10)` would put one
word on two meanings — the defect this document exists to remove.

**The participle rule, stated.** A participle answers the changed copy. Where the verb also answers a value that
cannot be got back afterwards, the participle answers the pair. `list.removedAt(index)` answers `Self`, because
`list[index]` is there to be read first; `stack.removed()` answers `(Item, Self)?`, because a stack has no index.

**What can implement them.** A trait exists so that there can be more than one, and there can be:

| Implementation | `add` | `remove` | `added` (a kept copy) | For |
|---|---|---|---|---|
| `ArrayStack` — a `List` used from the end | amortised O(1) | O(1) | O(n), the list is copied | the worklist, the default |
| `ConsStack` — a persistent cons list | O(1) | O(1) | **O(1), the tail is shared** | backtracking, undo, a parser's context |
| `ArrayQueue` — a ring buffer | amortised O(1) | O(1) | O(n) | the breadth-first worklist, the default |
| `BankersQueue` — two cons lists, front and rear reversed | O(1) | amortised O(1) | **O(1) amortised, shared** | a queue kept in many versions |
| `TrieList` used as either | O(log n) | O(log n) | O(log n) | neither — a trie pays for indexing nobody uses here |

The last row is the honest one: `TrieList` is the wrong structure for a stack or a queue. A trie buys cheap copies of
a *big indexed* value, and a stack has no index; a cons list buys the same cheap copy for a fraction of the constant.
So `TrieList` stays a `List` implementation and the persistent stack and queue are their own types.

The array implementations are written in plain TorbScript over a `List`, which is how `ArrayStack` and `ArrayQueue`
already work and is why `runtime/list.c` stays the one storage the family is built on.

### 3.4 No `Deque` trait, and no `PriorityQueue` trait

**A deque is a `List` implementation.** Java needed `Deque` because `ArrayList` and `LinkedList` are classes; Kotlin
did not, and made `ArrayDeque` a `MutableList`. Here `List` is a trait and the whole point of a trait is that the cost
profile is the implementation's business: `RingList` is a `List` whose `insert 0` and `removeAt 0` are O(1), and every
signature that says `List<Item>` takes it. A `Deque` *trait* would add a fourth name for each end (`addFirst`,
`addLast`, `removeFirst`, `removeLast`) on top of `add`, `insert`, `removeAt` and `last`, which is section 2's Java
row happening here. And it could not extend `Stack` anyway: `List.remove(value)` and `Stack.remove()` cannot live in
one type.

**A priority queue is not a `Queue`.** A `Queue`'s contract is that items come out in the order they went in; a heap
breaks it. It is also not worth a trait, because a trait with one implementation is the smell listed in section 1.5 —
so it is a type, and the ordering is a value it carries rather than a bound on `Item`:

```trb fragment
public type Ordered<Item> with Collection<Item> {
  private var items: List<Item> = []
  private key: (value: Item) => Int

  /** Builds an empty one ordered by `key`. */
  static fn by(key: (value: Item) => Int): Ordered<Item> {
    Self([], key)
  }

  var fn add(value: Item)
  /** Removes the value with the smallest key and answers it. */
  var fn remove(): Item?
}
```

**Type checks today** and runs natively, printing `2` and `Some(1)` for `add 3`, `add 1`, `count()`, `remove()`. It
carries the same two words as `Stack` and `Queue` without claiming either contract, and `Ordered.by { … }` says at the
construction site what "next" means — which is the same thing `List.sort(by:)` does and needs no `Compare` bound on
`Item`.

### 3.5 Construction: one rule

**Every factory of a trait builds `Self`, through `From<Iterable<Item>>`.**

```trb fragment
static fn of(...items: Item): Self where Self: From<Iterable<Item>> {
  Self.from items
}

static fn filled(count: Int, value: Item): Self where Self: From<Iterable<Item>> {
  Self.from((0..count).map({ _ => value }))
}
```

The requirement is on the **member** and not on the trait. That is forced: `Set<Item>` cannot require
`From<Iterable<Item>>` of every `Item`, because `TrieSet.from` needs `Item: Hash` and the trait's own implementation
is `extend<Item: Hash> Set<Item> with From<Iterable<Item>>`. A trait-level requirement was probed and is a hole in the
checker — see gap 3.

Three things follow, and all three were probed.

**`TrieList.of(1, 2)` builds a `TrieList`.** That is defect (b), fixed by the result type alone.

**`List.of(1, 2, 3)` keeps working and needs no diagnostic.** `Self` is bound to the trait type there, and the trait
type has its own `From` — `extend<Item> List<Item> with From<Iterable<Item>>`, which already exists and already picks
`ArrayList`. The probe declares a trait with the member-level `where`, an `extend … with From` beside it, and calls
the factory on the trait: `const onTheTrait: Pushdown<Int> = Pushdown.of(1, 2)` **type checks today**. So the rule is
behaviour-preserving for every call site in the repository and changes the answer only where the answer was wrong.

**It does not compile natively yet, and neither does the shape it replaces.** `ArrayStack.of(1, 2, 3)`, written
against `std` as it stands, is refused by the back end:

```text
error: `ArrayList.add`, whose declaration is not monomorphic is not supported by the native back end yet
internal error: The generic parameter `Item` was not substituted before the back end saw it
```

`TrieList.of(1, 2)` is refused with the same two lines. A trait's `static fn` default reached through an
implementation type does not monomorphize today, so this is gap 1 and it is a debt the family already carries rather
than a cost of the change.

**Literals are untouched.** `[1, 2]` is a `List<Int>` and builds an `ArrayList`; `["a": 1]` is a `Map<String, Int>`
and builds a `TrieMap`; `[1, 2, 2, 3]` against an expected `Set<Int>` goes through `Set.from`. Whether the *static
type* of a literal should be the concrete implementation is
[PERFORMANCE](PERFORMANCE.md)'s question, and finding 2 answers it: the fix is a devirtualization peephole over the
IR, and "Nothing about the type system changes; the pass answers a question the IR already contains." Round **P5** is
where that is decided. This design asks one thing of P5 and asks for nothing else: **that `[1, 2]` keep the static
type `List<Int>`**, because every factory above returns `Self` and a literal whose type was `ArrayList<Int>` would
make `const numbers: List<Int> = [1, 2]` a conversion instead of a value.

Whether the default map implementation should be `TrieMap` while the default list implementation is `ArrayList` is a
separate question and is left alone: both are documented aliases of the flat structures until the tries exist, so the
inconsistency is in the names and not in the behaviour.

### 3.6 Iteration

**One vocabulary, and the differences are justified.** `Iterator.next` and `Source.next`, `Accumulator.add` and
`Sink.add`, and `Stage` shared between both worlds — STREAMS section 1 has the table. An accumulator ends with
`finish()`, which answers the result of the run, and a sink with `end()`, which answers only whether the end went
through (6a says why it is not `close()`). The one word that is not shared is `Iterate.iterate` (`Iterable.iterator`
until the rename round), because a `Source` *is* the flow and has nothing to hand out.

**`Staged` is one name in two packages and stays one.** `std/iteration`'s `Staged<Input, Item>` loses `public`: it is
the return type of `Iterable.through`, which is declared as `Iterable<Output>`, so nothing outside the package needs
the name. `std/stream`'s `Staged<Input, Item, Failure>` keeps it and keeps its export. `Queueing` stays public and
exported, because a driver outside `std/iteration` reaches it (STREAMS section 13, point 6) — and that asymmetry
becomes the rule rather than an accident: **a type is `public` when something outside the package names it, and not
otherwise.** The ten stage types (`Mapped`, `Filtered`, …) are `public` and re-exported by nothing, so they lose
`public` too, which is what their own `lib.trb` already says: "The stage types themselves are not exported."

**One iterator type per collection stays two while there are two tables.** `MapIterator` and `HashMapIterator` are the
same twenty lines because `TrieMap` and `HashMap` are two `native type`s and a value of one is not a value of the
other. That is a consequence of the trie not existing, and it disappears with the trie; it is not a design decision to
reverse.

**What `for` needs.** [PERFORMANCE](PERFORMANCE.md) finding 5 measures a `for` over a collection at 47.95x a pointer
walk: a boxed `Object(Iterator<T>)` per loop, a `makeUnique` per turn, an `Option` round trip per item. Round **P6**
fixes levels 1 and 3 and **P11** fixes level 2. This design makes P6's job easier in exactly one way and it is worth
saying: **`ListIterator` is an `inline` record of two fields** — the list and an index — so once the receiver is
concrete, `iterator()` is a direct call answering a value and `next()` is a direct call the C compiler can inline.
Every iterator in `std/collections` has that shape already (`items` plus a cursor, held by value), and the rule to
keep is: **an iterator is a record of the container and a position, never a type with storage of its own.**

The one thing P6 and P9 leave behind is the `Option` per item, and PERFORMANCE says where the answer would be: an
`Iterator` that answers "is there one" and "the value" separately. That is a question for `std/iteration` and it is
not opened here, because it doubles the required surface of every iterator to save a tag.

### 3.7 Indexing and slicing

**The four traits are the right cut, and the probe is why.** Folding `Slice` into a second instantiation of `Index`
— `Index<Bounds<Int>, Self>` beside `Index<Int, Item>` — is the obvious simplification and it does not work. The
declaration is accepted and then both operators stop resolving:

```text
error: `Twice` does not implement `Index`, so `a[key]` has no meaning for it
error: `Twice` does not implement `Slice`, so `a[from..to]` has no meaning for it
  = Operators are traits: a type has the ones it comes `with`, and nothing else
```

Two instantiations of one trait on one type would collide in the member namespace anyway — one `get` each — which is
the same rule section 3.3 relies on. So `a[key]` and `a[from..to]` stay two operators bound to two traits, and
`MutableIndex` and `MutableSlice` stay the writing halves. After `compact` moves to `Collection` (section 3.2),
`MutableSlice` is exactly `replace`.

**`list[i]` panics and `get` is the `Option`.** That is decided, it is what `Index.at`'s doc comment says, and
nothing here touches it.

**What each storage is for.**

| | What it is | Size | Traits | Reach for it |
|---|---|---|---|---|
| `Array<Item, const Size>` | inline slots, the count in the type | fixed at compile time | `Iterable`, `Length`, `MutableIndex` | vectors, matrices, colours, hashes, foreign structs |
| `List<Item>` | the growable sequence, copy on write | any | `Collection`, `MutableIndex`, `MutableSlice` | everything that grows |
| `Buffer<Item>` (planned) | a `List` with an in-place promise and `swapRemove` | any | `Collection`, `MutableIndex`, `MutableSlice` | ECS columns, tensors, frame budgets |
| `Window<Item>` (planned) | a borrowed section of a `Buffer`, for one call | fixed for the region | `Length`, `Index` — **and nothing that resizes** | data parallelism inside one system |
| `Bytes` = `List<UInt8>` | a list of bytes | any | everything `List` has | stream chunks, encodings, HTTP bodies |

`Array` is not a collection and is in `std/core` because the language refers to it — a list literal against an
expected `Array` writes straight into the inline slots. `Buffer` is ECS gap 9 and BACKEND's heap kernel; its three
promises are an in-place write when there is one owner, two disjoint `var` windows, and `swapRemove` without a shift.
`Window` is CONCURRENCY section 6, and its whole definition is what it does *not* have: no `add`, no `insert`, no
`removeAt`, because a resize would reallocate the caller's block from another thread.

**`Bytes` stays `List<UInt8>`.** The objection is that a byte buffer should not pay a list's per-element cost, and it
does not: `torb ir` prints `element d_UInt8 UInt8 size 1 align 1`, so a `List<UInt8>` is byte-packed storage with a
length. What a real byte buffer would add over it is the `Buffer` promise — an in-place write when there is one owner
— and that is gap 8 for every element type, not a second type for one. So `Bytes` becomes `Buffer<UInt8>` when
`Buffer` exists, and the alias is the only line that changes.

### 3.8 Accumulation

**One `add`, and `Collection` owns it.** `Accumulator<Item, Output>` stands alone with `add`, `finish` and `isDone`,
and a `Collection` does **not** implement it. The two are different things: a collection is a container that values
end up in, an accumulator is one *run* of a pipeline, with a result and a "far enough" question a container has no
answer for. There is no trait between them either — no `Fill`, no `Accept` — because nothing outside a run needs
"some thing with `add`": a driver that fills a caller's container takes that container's own type or an
`Accumulator`, never an abstraction of `add`.

**What gathers into a collection is a type beside it**, which is the `Collector`/`Collectors.toList()` model of Java.
`ListAccumulator<Item>` is the one `std/iteration` ships and `listing()` answers it; `into<Target>()` is the general
one for any `From<Iterable<Item>>` target — it gathers into a `List` and calls `Target.from` once at `finish()`. A
package that owns a collection may ship an accumulator that writes straight into it (`HashSetAccumulator`, saving the
intermediate list and the second pass over the duplicates); **nothing picks such a one up automatically**, and a
caller who wants it names it at the call (`collect(HashSetAccumulator<Int>())`). Which run a pipeline makes is written
down, never inferred from the target type — the alternative would be a second dispatch axis nobody can read at the
call site.

**`Collector` is gone, merged into `Accumulator`** — section 3.8a has the probe. `Merge<Item, Output> with
Accumulator<Item, Output>` (CONCURRENCY section 5) goes beside it in `std/iteration` when `parallel()` arrives. That forces one rename here: **`Map.merge` and `Map.merged`
are deleted.** Their bodies are `addAll other` and `addedAll other`, so the words they occupy are already taken by
`Collection`, and `Merge.merge` is a different meaning — joining two partial results of one collector. One word, one
meaning, and the caller writes `ages.addAll(more)` and `ages.addedAll(more)`.

**`Set`'s algebra stays a triple.** `union`, `intersection` and `difference` are nouns that never change either
operand. Dropping `union` alone would leave two thirds of a vocabulary everybody knows. The three are functions of the
type - `Set.union(first, second)` - because a symmetric operation belongs to neither operand (section 6, question 3),
and `insertedAll` is the member that fills one set from anything that iterates.

### 3.8a `Collector` merged into `Accumulator`: the probe, and what it costs

`Collector` described a run (`start(): Accumulator`) and `Accumulator` was the state of one. **With value semantics
those are the same type**: a copy of a value is a fresh run, so `collect` fills a copy of what it was handed and
`groupingBy(...).then(downstream)` copies the downstream per group instead of `start()`ing it.

The question was decided by a probe and not by taste. A miniature of the whole vocabulary — a driver that copies what
it is given, a fold-shaped accumulator, a bounded one that answers `isDone()`, a grouping one that copies its
downstream per group, and an endless source — was written, type checked and **run as a native binary**. All three
things that had to hold, held:

| What had to hold | What the probe printed |
|---|---|
| one description drives several runs, independently | `6`, `30`, `6` for the same accumulator over three sources |
| `then(downstream)` gives every group its own run | `[4: ["pear", "kiwi", "plum"], 3: ["fig"]]` and `[4: 3, 3: 1]` for two downstreams |
| an endless source still stops on `isDone()` | `[0, 1, 2, 3]`, twice, from an infinite counter |

Every collector of `std/iteration` was then written that way with no loss: `collector(initial, finish:, step:)`
answers a `FoldAccumulator` whose `state` starts at `initial`, `into`/`listing` answer a fresh gatherer, `Grouping`
keeps the downstream and copies it, and `Stage.onto` already took an `Accumulator` and was untouched. `start()` is
deleted and one trait is gone.

**The one thing it costs, written down:** the type no longer distinguishes a fresh description from a half-filled
run. An accumulator that has already been `add`ed to and is then handed to `collect` or to `then` starts every run
from what is in it. That is a footgun and not a breakage — the same one a partly consumed `Iterator` is — and the
pitfall is in the doc comment of `Accumulator`. `Collector` would have bought a type that says "fresh", at the price
of a factory member on every collector in the language.

### 3.9 Ten call sites, side by side

| | Today | Target |
|---|---|---|
| build a list | `List.of(1, 2, 3)`, `[1, 2, 3]` | unchanged |
| build a specific one | `TrieList.of(1, 2)` — **an `ArrayList`** | `TrieList.of(1, 2)` — a `TrieList` |
| add | `list.add(x)`, `list.added(x)` | unchanged |
| add to a stack | `stack.push(x)` **or** `stack.add(x)` | `stack.push(x)` (6b; C1 had made it `stack.add(x)`) |
| take from a stack | `stack.pop()` | `stack.pop()` (6b; C1 had made it `stack.remove()`) |
| take from a queue | `queue.dequeue()` | `queue.dequeue()` (6b; C1 had made it `queue.remove()`) |
| look without taking | `stack.peek()` **or** `stack.first()` | `stack.peek()`, `queue.peek()` (6b) |
| keep the old version | `stack.pushed(x)`, `stack.popped()` | `stack.pushed(x)`, `stack.popped()` (6b) |
| iterate | `for item in items { … }` | unchanged |
| index, slice | `list[i]`, `list.get(i)`, `list[1..4]` | unchanged |
| map, filter, sum | `items.filter({…}).map({…}).sum()` | unchanged |
| sort | `list.sort({…})`; `list.sorted({…})` is an **`Iterable`** | `list.sorted({…})` is a `List` |
| group | `items.groupBy({…})`, `collect(groupingBy({…}))` | unchanged |
| join | `items.joined(separator: ", ")` | unchanged |
| how many | `list.length()`; `list.count()` **walks** | `list.count()` answers `length()` |
| merge two maps | `ages.merge(more)` | `ages.addAll(more)` |

The table is the argument for the size of this change. Eleven of sixteen rows are unchanged, because the family is
right; the five that move are the ones where a reader had a choice of words and the two where the answer was wrong.

### 3.10 What the design may not cost

1. **A literal keeps its concrete implementation.** Section 3.5: P5 devirtualizes the IR, the type system stays.
2. **`for` without a boxed iterator.** Section 3.6: every iterator stays an inline record of a container and a
   position, so P6 has a direct call to make.
3. **Participles stay moves.** `stack = stack.added(x)` at the last use is a `Move` and the `makeUnique` inside finds
   a count of one — PERFORMANCE finding 1, round P1, done. Nothing in section 3.3 takes a receiver by anything other
   than value, so the rule keeps applying.
4. **Copy on write, per structure.** `ArrayList` copies its buffer, `TrieList` copies one path, `ConsStack` copies
   nothing, `ArrayStack` copies its list. The table of section 3.3 is the contract, and the doc comment of each
   implementation is where a caller reads it.
5. **One `list.c`.** `ArrayList`, `TrieList`, `ArrayStack`, `ArrayQueue`, `ConsStack` and `BankersQueue` are all a
   `List` or are written over one. The runtime gains nothing from this document.
6. **`TrieList` becomes a real trie**, or it loses the name. While it is a documented alias of `ArrayList` the cost
   table of section 3.3 is a promise the implementation does not keep, and defect (b) was only visible because
   somebody tried to build one.

### 3.11 `for var`: every element in place

**Decided 2026-09-22, built 2026-09-29.** `for var element in container` binds `element` to each **slot** of the
container in turn, as a `var` reference, for the duration of one turn of the body — Rust's `iter_mut`, not Swift's
`for var`, which binds a mutable *copy* and is the copy trap of CONCEPT's `var` paths written as a loop:

```trb fragment
var particles: List<Particle> = spawnParticles()

for var particle in particles {
  particle.position = particle.position + particle.velocity
  particle.age = particle.age + 1
}

var stock: Map<String, Int> = ["apple": 3, "pear": 0]

for (name, var count) in stock {
  count = count + 10
}
```

The first loop changes every particle of `particles` itself; the second changes every value of `stock` and leaves
every key alone. Today both have to be written through the path — a loop over `0..particles.length()` whose body
writes `particles[index].age` — or with `update`, which is what CONCEPT's "the variable of a `for` loop is a `const`"
sends a reader to.

**It is sugar over `MutableIndex`, so it works for every container with slots.** The loop visits the keys of the
container and opens `container[key]` as a `var` path for each of them — exactly the access `container[key].field = x`
already is (CONCEPT, "`var` Paths"). That takes one member beside `set`: `MutableIndex<Key, Value>` gains
`fn keys(): Iterate<Key>`, the keys the loop visits, in iteration order. A `List`, an `Array` and a slice answer their
indices as a `Range` (`0..length()`, no allocation), a `Map` answers its keys in insertion order. `Map.keys()` exists
already with that signature; the member is called `keys` for a list too, because a trait has one name per member and
an index *is* a list's key. **A user container joins by implementing that one trait**, and nothing else about the loop
knows which container it is.

**The rules.**

1. **The container is a `var` path** — a `var` binding, a `var` parameter, a `var fn` receiver or a path through
   `var` fields from one of those. A `const` container or a temporary is rejected, the same way `const` rejects every
   other change.
2. **The body may not touch the container otherwise.** While a turn holds `container[key]` open, reading or changing
   `container` — or a path above or below it — in any other way is an exclusivity error, exactly as for a closure
   argument of a call that changes a path (CONCEPT, "Exclusivity"). This is what makes it sound to hold a reference into
   the storage: nothing in the body can grow, shrink, rehash or reassign the container under it. The check is static
   and conservative, like every other exclusivity check.
3. **The element is a reference, not a binding.** It is changed by assignment (`particle = Particle.resting`), through
   its fields, and by `var fn` calls; it may be passed as a `var` argument and captured by a closure that does not
   escape, and never stored, returned or captured by an escaping closure — the rules of a `var` parameter.
4. **A `Map` keeps its keys constant.** The pattern is `(key, var value)`; `for var entry in map` is rejected, because
   a key cannot change in place without moving the entry.
5. **`Set` and plain `Iterate` sources are rejected, with a message that says why.** A set element cannot be changed
   in place, because the change could change its hash and so its place; a plain `Iterate<Item>` has no slots at all:

   ```text
   error: `for var` changes the slots of a container, and a `Set` has none: an element's place depends on its value
     = Remove the element and insert the changed one, or build a new set with `map`

   error: `for var` needs a container with slots (`MutableIndex`), and `items` is only an `Iterate<Int>`
     = Build a new collection with `map`, or loop over a `List` held in a `var`
   ```

   *(Proposed.)*
6. **`break`, `continue`, `return` and `?` mean what they mean in every `for`.** Leaving the loop closes the access
   of the current turn, and nothing is written back that was not already written in place.

**The lowering is an index loop over element paths.** No iterator object, no per-element copy, no `Option` per item:
a list becomes `for index in 0..length()` whose body works on the element's address (the in-place index write of
PERFORMANCE round P7), and a map walks its entry vector the same way. The keys are taken from the container before
the first turn only in the sense that rule 2 guarantees they cannot change — no snapshot is made.

**As built** (`compiler/src/semantics/checker/slots.trb`, `lowerForSlots` in `compiler/src/ir/lower/statement.trb`).
The slot is no binding of the frame. It is an entry of `Lowering.slotAliases`: the container's path, evaluated once
before the loop - its root and the key of every `a[key]` on it - and `PlaceStep.Index` of the turn's key behind it.
Every use of the name continues that path, exactly as `container[key].field = x` does: a read takes the element out
with `Index.at` and puts nothing back, a change is `MutableIndex.set` in the same statement. So every change lands
where it is written, and `break`, `continue`, `return` and `?` leave nothing to write back. The keys of a `List`
(any type that implements it, which is the contract of `List.keys()`) and of an `Array` are counted from `0` to
`length()`, without an iterator; every other container answers `keys()` once and is pulled like any `Iterate`.

What that costs is what the index loop it replaces costs, measured on an 800 by 800 grid of `List<List<Int>>`, six
rounds, release build: `rows[row][column] = rows[row][column] + 1` 2.1 s, the same change through two nested
`for var` 1.9 s; over one flat `List<Int>` of 640 000 items, 0.13 s against 0.19 s (noise of ±0.05 s). Both write
through the round trip of PERFORMANCE finding 3, which `ir/elements.trb` turns into one `Element` step once the
container is concrete and copies a counted element where it stays trait-typed - `for var` changes nothing about
that, in either direction. What the built loop does not do yet:

- A closure that captures the slot is refused by the back end ("a closure that captures the slot of a `for var`"),
  as one that captures a `var` parameter is: rule 3 allows a closure that does not escape, and the lowering has no
  place to hand it.
- A `Map`'s keys come from `keys()`, whose iterator keeps a count of the map's storage, so the first change of a
  value copies the map once per loop. Walking the entry vector directly, as the paragraph above plans, is not built.
- A debugger shows the key of a turn and not the slot, which is no local of the frame.

*Slice:* C9 in section 5.

## 4. What the language must provide

**Gap 1. Closed.** A trait's `static fn` default now monomorphizes through an implementation type.
`ArrayStack.of(1, 2, 3)` and `TrieList.of(1, 2)` were refused with
`` `ArrayList.add`, whose declaration is not monomorphic is not supported by the native back end yet`` and an
`internal error: The generic parameter `Item` was not substituted before the back end saw it`.

The cause was one line of `constructedReceiver` in `compiler/src/ir/lower/generic.trb`. What stands in front of the
dot of `ArrayStack.of(...)` is a **type**, and the checker records the type of its *constructor* there; the lowering
took that constructed type as the receiver only where its head was the head of the member's own owner. A trait's
static default belongs to `Self`, which has no head at all, so the receiver was dropped — and with it every argument
of the trait, so the body was instantiated with `Item` still a parameter.

The fix is `implementingReceiver` beside it: where the owner is a trait's `Self` parameter, the member takes no
`self`, and the constructed type really implements the declaring trait (the same question `memberMappingOf` asks to
fill the trait's own arguments in), the type in front of the dot **is** what `Self` stands for — exactly as it is for
an instance default. The two conditions are what keep a function *value* out: a closure's `show()` takes `self` and
is never reached this way.

**Gap 2. Closed, and it was closed before this round.** `Self.from items` inside a default whose `Self` is the trait
type was expected to be refused with
`` `from`, which is not in the witness table of a trait-typed value is not supported by the native back end yet``. It
is not: the `.Object` branch of `dispatchedMember` already answers a **static** member of a trait-typed type with the
one implementation `witnessFor` names, because there is no payload to erase and object safety keeps such a member out
of every table anyway. Probed against the compiler as it stood *before* gap 1 was fixed: a trait with
`static fn of(...): Self where Self: From<Iterable<Item>>` whose body is `Self.from items`, called on the trait,
built and printed `[1, 2]`.

**Gap 3. Closed.** A trait's own `with` list is a promise about **every** instantiation, because it is what member
lookup reads — so an implementation of that supertrait *for the trait type* may not be narrower than the promise. The
checker rejects `extend<Item: Hash> Mound<Item> with From<Iterable<Item>>` beside
`trait Mound<Item> with From<Iterable<Item>>` at the `extend`:

```text
error: `Mound` comes with `From` for every instantiation, and this implementation holds only for some
  = A trait's own `with` list is what member lookup reads, so `From` resolves on every `Mound` while nothing
    implements it for the rest: drop the bounds of this `extend`, or take `From` out of `trait Mound`'s `with` list
    and ask for it at the member (`where Self: From<...>`)
```

The note names the fix section 3.5 takes: the requirement belongs on the **member** (`where Self: From<Iterable<Item>>`)
and not on the trait. A trait that bounds its own parameter said the same thing where the promise is read and is
untouched, and so is an implementation of a trait the target does not name in its own `with` list.

**Gap 4. Closed, and the rule is one body per instantiation.** Two instantiations of one trait on one type stay
legal — `Multiply<Board, Board>` beside `Multiply<Int, Board>`, `From<A>` beside `From<B>` — because that is how a
type overloads over a trait argument. What is not legal is writing the members **once** for two of them: a type has one
namespace of members, so the body it writes is the implementation of one instantiation and of no other. The checker
reports the second entry of the `with` list and names the first:

```text
error: `Twice` comes with `Index` more than once, and `at` is the body of the first one
  = A type has one namespace of members, so one body per instantiation is one `extend` per instantiation:
    `extend Twice with Index<...> { fn at(...) }`
```

A body that writes the member twice keeps the message the member namespace already has (``already declared``), so the
two spellings of one mistake get one root cause each. The form the note names — one instantiation in the `with` list
and the next in an `extend` — type checks and resolves.

**Gap 5. Closed.** `List.sorted(by:)` answers `Self`, written over `sort`, so
`const ordered: List<Int> = numbers.sorted({ _ })` is a binding and not a conversion.

It cost one line of the checker beside the `std` change, which the smallest fix did not foresee. `traitMembers` built
an **overload set** out of `List.sorted` and `Iterable.sorted` — two candidates with the same parameter types and
different results — and every call site became ambiguous. A subtrait's member **overrides** the one it inherits
instead of standing beside it, and only two traits of which neither is the other's supertrait are an overload set
(`From<Int8>` beside `From<Char>`). `traitsOf` lists a bound before its supertraits, so the overriding declaration is
the one already found.

**Gap 6. `Buffer<Item>`** — ECS gap 9 and [BACKEND](BACKEND.md)'s heap kernel: an in-place write where the storage has
one owner, two disjoint `var` windows, and `swapRemove` without a shift. Section 3.7 depends on it for `Bytes` and for
the ECS columns, and nothing else in this document does.

**Gap 7. `Window<Item>` and `Plain`** — [CONCURRENCY](CONCURRENCY.md) gaps 4, 5 and 6. Section 3.7 names the shape;
that document owns it.

**Gap 8. `Merge<Item, Output>`** — [CONCURRENCY](CONCURRENCY.md) section 5. It arrives with `parallel()` and it needs
`Map.merge` to be gone first, which is slice C4. *Built 2026-09-26, with `parallel()`'s `collect` (CONCURRENCY section 14, slice B).*

**Gap 9. `for` over a concrete collection** — [PERFORMANCE](PERFORMANCE.md) findings 2 and 5, rounds P5, P6 and P11.
Section 3.6 says what this family owes them: an iterator that is an inline record.

## 5. Slices

Each slice is one agent. The repository is bulk-edited with node scripts that check an exact match before they write —
a PowerShell array has destroyed files here before — and every slice leaves the four gates green:
`torb docs check docs`, `torb docs index --check docs`, `torb check .` with no problems, and
`torb canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops .`
reporting zero files.

**C1 — the words of `Stack` and `Queue`. Done, and revised by 6b.** Both traits became `Collection<Item>` plus
`remove(): Item?` and `removed(): (Item, Self)?`; `add` and `added` came from `Collection`, and looking without taking
was `Iterable.first()`. `ArrayStack` and `ArrayQueue` write `add` and `remove` directly, `ArrayQueue`'s own count field
is `lengthValue` so that it does not stand in the way of `Collection.count()`, and the tour keeps its stack and its
queue in those words. **The rename round turns this back** into `push`/`pop`/`peek` and `enqueue`/`dequeue`/`peek`
(section 6b); what stays of C1 is the one-verb-one-participle shape and the participle rule.

**C2 — `Collection`'s job. Done, and revised by 6b.** `Collection` declares `clear`, `compact`, `count`, `addAll`,
`added`, `addedAll`, `contains` and `containsAll`, and `add` itself (since C2b it no longer comes from `Accumulator`).
`compact` moved in from `MutableSlice`, which is now exactly `replace`; it is a **default that does nothing**, because
a storage without spare room has nothing to hand back, and `ArrayList`, `TrieList`, `ArrayStack` and `ArrayQueue`
override it. `count()` answers `length()`. `Set.isSubsetOf`, `Map.merge` and `Map.merged` are gone; the one caller
outside `std` was `compiler/src/highlight/scope.trb`, which writes `addAll`. **The rename round deletes the trait**
(section 6b): `clear`, `compact`, `contains` and `count` move into each kind that has them, and `Set.isSubsetOf`,
`Map.merge` and `Map.merged` stay gone.

**C3 — `List.sorted`, and the participle rule written down. Done.** Gap 5 above, the rule in
`docs/language/types/verbs-and-participles.md` (a participle answers `Self`; where the verb also answers a value the
copy cannot get back, it answers the pair) and in CONCEPT's verb table, and
`tests/conformance/collection-words.trb` as the program that runs it.

**C4 — the back end, gaps 1 and 2. Done.** `implementingReceiver` in `compiler/src/ir/lower/generic.trb` (gap 1
above); gap 2 turned out to be closed already and is now pinned. `tests/conformance/static-defaults.trb` is the
program: `ArrayStack.of`, `TrieList.of`, `ArrayQueue.of`, `TrieSet.of`, `TrieMap.of`, `List.filled`,
`ArrayList.filled`, the same members on the trait, and a user trait whose static default is called on **two**
implementers and once on the trait itself.

**`TrieList` lost two planned natives on the way.** `TrieList.of` answers a `TrieList`, which needs `TrieList.from`,
which was `plannedRuntimeOf(..., "8")` — so it could not be built at all. `from` and `iterator` are now ordinary
TorbScript over `withCapacity` and `get`, exactly as `ArrayList`'s are and for the same reason (`from` walks a
trait-typed `Iterable` of the *program*, which a C function cannot do); `TrieListIterator` is the cursor beside
`ListIterator`, the second of the pair section 1.5 point 8 describes. Two entries left
`compiler/src/backend/c/natives.trb`, and the runtime gained nothing.

**C5 — construction builds `Self`. Done.** `of` and `filled` on `List`, `Set`, `Map`, `Stack` and `Queue` answer
`Self` through `Self.from(...)`, with `where Self: From<Iterable<Item>>` **on the member** (`Set.of` and `Map.of`
carry their `Item: Hash`/`Key: Hash` beside it). `List.of` on the trait keeps working and needed no diagnostic, which
section 3.5 had probed and the conformance program now runs natively: `Self` binds the trait type there and the trait
has its own `From`.

**C6 — the checker, gaps 3 and 4. Done.** The wording and the rule of each are under gap 3 and gap 4 above. The
promise check is `checkTraitPromise` in `compiler/src/semantics/checker/implementation.trb`, run per implementation
beside the orphan rule; the one-body check is `requireOneBodyPerInstantiation` in `declaration.trb`, beside the member
namespace rule it defers to. `compiler/tests/implementations.test.trb` and `traits.test.trb` pin both texts and the
cases that stay silent.

**C7 — the implementations that make the traits worth having.** `RingList` (a `List` with O(1) at both ends),
`ConsStack`, `BankersQueue`, `Ordered`. Each is plain TorbScript over a `List` and each needs one benchmark against
the array implementation.
*Gate:* the four, plus `tests/conformance/` programs and a row in `benchmarks/`.
*Estimate:* medium, and it is the slice that proves the owner's fixed point — a trait exists so that there can be more
than one implementation.

**C8 — visibility.** `std/iteration`'s `Staged` and the ten stage types lose `public`; the rule of section 3.6 goes in
`std/iteration/src/lib.trb`'s module comment.
*Gate:* the four.
*Estimate:* small, and it must run after C2 so that two agents do not edit `std/iteration` at once.

**C2b — the run and the container, told apart. Done.** `Accumulator<Item, Output>` stands alone (`add`, `finish`,
`isDone`); `Collection` declares `add` itself and implements `Accumulator` nowhere, so a container no longer shows a
`finish()` or an `isDone()` it has no answer for. `Collector` is merged into `Accumulator` (section 3.8a) and
`start()` is deleted. `ListAccumulator<Item>` is the shipped gatherer and `into<Target>()` the general one.
`Sink.finish()` became `Sink.end()` — see section 6a for why it is not `close()`.

**C2c — the words per kind, and the rename. The rename round, two commits.** Section 6b has the decided words and 6a
the recipe: commit 1 teaches the compiler both names (`Iterate` beside `Iterable`, `append` beside `add` for the list
literal) and refreshes the seed; commit 2 sweeps `std`, the compiler, the examples, the tests and the docs with a
checked script and drops the fallbacks. `Add` is not renamed.
*Gate:* the four, the fixpoint, and no `Iterable`, `Collection<` or `iterator()` left outside the history of the
records.

**C9 — `for var`** (section 3.11). **Built 2026-09-29.** The checker: the form in the `for` head, rules 1 to 6, the two
rejection messages pinned. The standard library: `keys()` on `MutableIndex`, answered by `List`, `Array`, the slices and
`Map`. The lowering: an index loop whose body works on the element's address, no iterator. It is a syntax change, and
it took one commit and not the two of 6a because nothing the seed compiles writes it yet: the first `for var` in
`compiler/` or `std/` needs a seed that knows the form first.
*Gate:* a conformance program per container (list, array, slice, map values, a user `MutableIndex`) -
`tests/conformance/slot-loops.trb`, natively and in the VM; a checker test per rejected shape -
`compiler/tests/slot-loops.test.trb`; and the IR of a list loop showing no iterator and no `Option`, pinned there too.

What is left is C2c, then C7 and C8 whenever there is room. Only C7 adds a type.

## 6a. What a rename of a name the compiler knows costs

Three renames were asked for in one round and all three are blocked by the **same** mechanism, which is worth writing
down once because it decides the order of every future round that touches `std`'s vocabulary.

**The compiler resolves a handful of `std` names by literal string.** `compiler/src/semantics/checker/wellknown.trb`
asks the prelude's exports for `"Iterable"`; `checker/expression.trb` resolves `a + b` to the symbol named `"Add"`
and the member named `"add"`; `ir/lower/collection.trb` lowers every list literal by asking `ArrayList` for the member
named `"add"` and every map literal for `"set"`. A name in that set cannot be changed in `std` alone: the **seed** —
the `torb` a checkout bootstraps from, which by definition predates the change — still looks for the old one.

Measured, not argued:

| Rename | Sites | What happens with the current seed |
|---|---|---|
| `Iterable` → `Iterate` (`Sequence` was the alternative, rejected: a `Set` and a `Map` are not sequences) — **decided** | **872** in 146 files (394 in 83 `.trb`, 478 in 63 `.md`; `FromIterable`, `lowerForIterable`, `itemOfIterableBound`, `reportNotIterable`, `anyIterable` fall out of the same word-boundary sweep) | applied and reverted: `torb check .` went to **208 problems in 195 of 372 files** — every `for` loop and every collection literal stops resolving. An `Iterable` alias beside the new name makes it green again, which is the proof that the name is the only thing missing. |
| `Add` → `Plus` (and `add` → `plus`) — **no longer planned** | 313 for `Add` alone | applied to `std/core` and reverted: `end - start + 1` in `std/core/src/range.trb` became "The checker did not work out the type of this expression". An alias does **not** help here, because the *member* name is wired too. The rename existed to free `add` for the containers; with the words of 6b no container spends `add` any more, so `Add` stays (owner, 2026-09-22). |
| `List.add` → `List.append` — **decided** | ~1900 `.add` call sites | not attempted: `memberOfType(lowering, container, bound, "add", at)` is how a list literal is lowered, so the seed could not *build* the compiler at all. |

**The recipe, two commits.** First: teach the compiler both names (`symbolNamed("Iterate") ?? symbolNamed("Iterable")`
and the same for the operator and the container member) and refresh the seed from that build. Second: sweep `std`,
the compiler, the examples, the tests and the docs with a checked script, and drop the fallbacks. Neither commit is
large; what cannot happen is both in one, and no amount of care inside one round changes that.

**`Sink.finish()` is `end()` and not `close()`.** The decision was "a stream's end is not a result, and `finish` is
the `Accumulator`'s word", which is right; `close()` is not available for it. `Sink` comes `with Close`, whose
`var fn close()` is the **abrupt** end — it releases the target, cannot fail, and is what the last release runs — and a type
has one namespace of members, so the graceful end (`Task<Result<Void, Failure>>`, flushes, reports) cannot share the
name. `end()` says the same thing as `close` about a *stream* without claiming the word `Close` owns. The other way
out would have been to take `Close` off `Sink`; the owner's decision that `close()` is the language's destructor
(`docs/design/DESTRUCTORS.md`) settles it the other way: a sink keeps `Close`, its `close()` is the abrupt end the last release
runs, and `end()` is the graceful one a program calls and awaits itself.

## 6b. The words per kind: the decision of 2026-09-22

**Decided by the owner, recorded here; the code and `docs/language` follow it since the rename round (C2c, section 6d).**

| Trait | Its own words | Participles (the rule of 3.3) |
|---|---|---|
| `Iterate<Item>` (was `Iterable`) | `iterate()` (was `iterator()`), and the stages and terminals it already has | — |
| `List<Item>` | `append`, `appendAll`, and the index words it has (`insert`, `removeAt`) | `appended`, `appendedAll`, `inserted`, `removedAt` |
| `Set<Item>` | `insert`, `insertAll`, `remove` | `inserted`, `insertedAll`, `removed` |
| `Map<Key, Value>` | `set`, `remove` | `updated` for `set` (1.5 point 3), `removed` |
| `Stack<Item>` | `push`, `pop`, `peek` | none (see below) |
| `Queue<Item>` | `enqueue`, `dequeue`, `peek` | none (see below) |
| `Collection<Item>` | **deleted** | — |
| `Add` (the operator) | **stays** — `Add` → `Plus` is no longer planned | — |

The five kinds come `with Iterate<Item>, Length` directly, and `clear`, `compact`, `contains` and `count` are declared
by each kind that has them instead of being inherited. `peek()` answers the item `pop()` or `dequeue()` would take,
without taking it.

**Why one vocabulary per kind.** "`add` for everything is a sledgehammer" (the owner): on top of a stack, at the back
of a queue or a list, and somewhere in a set are four different meanings, and one word for all of them makes a reader
look up the receiver's type to know what a call does. The words everybody knows for each structure say it at the call.
The objection section 3.3 raised — Java's twelve names for three operations — was about several words for **one**
meaning on one type; here every type has one word per meaning.

**Why `Collection` goes.** As a bound it was never needed: section 1.2 found two uses, both in the tour, and section
1.7 wrote both as `Iterate<Item> & Length` and an `Accumulator`. With every kind keeping its own verb there is no
shared `add` left for it to own, and a trait that only groups is documentation. The word stays for prose and for the
package name `std/collections`.

**Why `Iterate` and `iterate()`.** A single-method trait is named like its method (`Hash` for `hash()`), and no trait
name ends in `-able`. `Sequence` was the alternative and is rejected, because a `Set` and a `Map` are iterated and are
not sequences.

**Why `Add` stays.** The rename to `Plus` existed so that `a + b` and a container's `add(value)` would not be one member
name. With `append`, `insert` and `push` no container spends `add` any more; `Accumulator.add` stays, and an
accumulator is a run and not an operand of `+`.

**Participles only where they already read naturally (decided in C2c).** The table first proposed `pushed`,
`popped(): (Item, Self)?`, `enqueued` and `dequeued(): (Item, Self)?`. The language review argued against them and the
rename round followed it: a participle that answers a pair beside the rest is a second word for what a `var` copy does
(`var rest = stack`, then `rest.pop()`), and `popped` reads as a past tense of the item and not as "the stack without
its top". `List`, `Set` and `Map` keep the participles they had (`appended`, `inserted`, `updated`, `removed`, ...),
because there the copy answers `Self` and nothing is lost. `Stack` and `Queue` lose the `added`/`removed()` of C1 and get
none; `peek()` replaces the `first()` C1 pointed to (`first()` is still there, from `Iterate`).

**`Accumulator.add` stays.** An accumulator is a run a pipeline pushes into and not a container, so the reasoning that
took `add` away from the five kinds does not reach it; `Sink.add` stays for the same reason.

**What it revises.** C1 and C2 are turned back where they chose `add`/`remove` for stacks and queues and a shared
`Collection`; 3.2 and 3.3 carry notes; questions 1 and 2 of section 6 are answered.

## 6c. Commit 1 landed

The renames the owner settled: the trait `Iterable` becomes `Iterate` and its member `iterator()` becomes `iterate()`
(a single-method trait is named like its method), `List.add` becomes `append`, `Set.add` becomes `insert`, and
`Map.set` stays. **`Add` and `add` stay** - `a + b` is not renamed. The compiler now accepts **both** names wherever it
resolves one of these words by literal string, new name first and old name second. Nothing in `std`, the docs or the
tests was renamed. Every site carries the same line, so commit 2 finds all of them with one search:

```text
grep -rn "Rename fallback: delete after the seed knows the new name." compiler/src
```

| Site | What it accepts |
|---|---|
| `compiler/src/semantics/checker/wellknown.trb`: `wellKnownOf` | the prelude's `Iterate`, else `Iterable` (`WellKnown.iterable`, which is all the checker's `for` and `...` ask) |
| `compiler/src/semantics/checker/name.trb`: `openRangeNote` | the note on `(..10).iterate()` as on `(..10).iterator()` |
| `compiler/src/ir/lower/collection.trb`: `iterableBoundOf` | the `for` bound `Iterate`, else `Iterable` |
| `compiler/src/ir/lower/collection.trb`: `cursorMemberOf` | the member that makes the cursor: `iterate` where the bound declares it, else `iterator`. Asked through `traitMemberNamed`, because `dispatchedOn` reports a name it misses |
| `compiler/src/ir/lower/collection.trb`: `spreadInto` | `...values` makes its cursor through `cursorMemberOf` |
| `compiler/src/ir/lower/statement.trb`: `lowerCursor` | a `for` makes its cursor through `cursorMemberOf` |
| `compiler/src/ir/lower/collection.trb`: `lowerItemList` | a list literal fills through `append` when the container has it, else `add`. It asks `hasMemberOfType` first, because `memberOfType` reports on a miss and abandons the body |
| `compiler/src/ir/witness.trb`: `hasMemberOfType` | the probe the line above uses. It has no other caller and goes with the fallback |
| `compiler/src/ir/lower/context.trb`: `nativeNamed`, `renamedFrom` | a native the manifest does not list under its new name answers with the row of its old one: `ArrayList.append` and `HashSet.insert` are the `add` rows (`torb_list_add`, `torb_set_add`), `Array.iterate` the planned `Array.iterator` row |

The map literal's `"set"` has no fallback because it keeps its name. No Set literal exists, so `Set.insert` reaches the
compiler through the manifest and nowhere else.

**The manifest was not given a second row.** `compiler/tests/natives.test.trb` compares the checked-in
`runtime/include/torb_natives.h` with what the manifest renders, and the header lists every name above its symbol, so
an alias row would change a generated file in `runtime/` twice for nothing. Commit 2 **renames** the rows instead:
`{owner}.add` becomes `{owner}.append` in `listEntries` and `{owner}.insert` in `setEntries`, and `"iterator"` becomes
`"iterate"` in `arrayMembers` - whose runtime symbol is built from the member (`torb_array_{member}`), so there the
symbol has to keep its spelling or change on purpose. Then it runs `torb natives --header runtime` and updates the names
the test asserts (`ArrayList.add`, `HashSet.add`, `"add"` in the member lists, the comment
`/* ArrayList.add, TrieList.add */`).

**Measured with this compiler.** Each rename was applied to a scratch copy of `std` and reverted:

- `Iterable` → `Iterate` and `iterator` → `iterate` (178 and 63 sites in `std`): `check std/core std/collections
  std/iteration std/text` says "32 files, no problems"; the seed reports 7 problems there, every one "The checker did not
  work out the type of this expression" on a `for` or a collection call. A built program with a `for` over a list, a
  `for` over a range, a spread and `more.iterate()` prints `12`, `[1, 2, 3, 4]` and `Some(1)`.
- `List` declaring `append`, `ArrayList`/`TrieList` declaring `native append` and `HashSet`/`TrieSet` declaring
  `native insert`: a list literal and two `insert`s build and run (`[1, 2, 3]`, `3`, `true`), and the emitted C fills the
  literal through `ArrayList_append__Int64` and wraps `TrieSet_insert__Int64` around `torb_set_add`.

**Not known to the compiler, so a plain sweep:** `Stack.add`/`remove` → `push`/`pop`, `Queue.add`/`remove` →
`enqueue`/`dequeue`, their `first()` → `peek()`, and the trait `Collection` - no string in `compiler/src` looks any of
them up, and no native row names a stack or a queue. The compiler's *messages* that say `Iterable` (`statement.trb`,
`call.trb`, `expression.trb`) are prose and go with the sweep, together with the tests that quote them.

## 6d. Commit 2 landed

The old names are gone: every fallback of section 6c is deleted (`hasMemberOfType`, `cursorMemberOf`, `renamedFrom`, the
second branch of `wellKnownOf` and of `openRangeNote`), and the compiler looks up `Iterate`, `iterate` and `append`
only. `std/collections/src/collection.trb` is deleted; each kind declares `clear`, `count` (answering `length()`) and,
where it has them, `compact` and `contains`. `containsAll` went with `Collection`: nothing called it. A `Map` has no
`add(entry)` and no `addAll` any more - `TrieMap.from` is a `for` over the entries with `set`.

The natives manifest renames its rows (`{owner}.append` in `listEntries`, `{owner}.insert` in `setEntries`, `"iterate"` in
`arrayMembers`, and `File.end` for the planned `File.finish`); the C symbols keep their spelling (`torb_list_add`,
`torb_set_add`), because the manifest maps a member to a symbol and a rename of `runtime/` would buy nothing. The header
only changes the names above the two symbols.

**How the call sites were found.** A regex cannot tell `list.add` from `accumulator.add`, the checker can: the sweep
renamed `std` first and then read every "`T` has no member `m`" of `check .`, `check tests/conformance tests/language`
and `docs check docs`, choosing the new word from the head of `T` (`List` → `append`, `Set` → `insert`, `Stack` →
`push`/`pop`, `Queue` → `enqueue`/`dequeue`), until a round found nothing. Where the receiver's element type is a type
parameter the checker says "The checker did not work out the type of this expression" instead of naming the member (a
checker defect worth its own round: inside a `var fn add`, `items.add value` on a `List<Item>` field reports that and
not "has no member"); those few were read off the source and were all lists.

## 6. Open, for the owner

Everything technical above is decided and argued. These six were taste and direction, and all six are answered.

**1. `remove()` on a stack and a queue.** Section 3.3 spends one word for "the item the structure gives next", so
`stack.remove()` and `queue.remove()` read alike and `Set.remove(value)` is a different member of a different trait.
The alternative is to keep a verb per structure (`pop`, `dequeue`) and accept that `add` and `push` are both there.
Clojure's `pop` and Kotlin's `removeFirst` are the two precedents, and they disagree.
**Answered (2026-09-22):** a verb per structure — `push`/`pop`/`peek` and `enqueue`/`dequeue`/`peek` — and no `add`
beside them, because no kind spends `add` any more (section 6b).

**2. `Stack` and `Queue` become structurally identical.** Both are `Collection<Item>` plus `remove()` and `removed()`;
only the contract differs. That is deliberate — the type name carries the word — but it means a shared supertrait
could be declared and is not. Should there be one, named after its method, so that "a worklist, either way" is
writable?
**Answered (2026-09-22):** the premise is gone — with their own words the two traits are no longer identical, and
`Collection` itself is deleted. No worklist supertrait.

**3. `Set.union`.** Its body is `insertedAll` (was `addedAll`). It stays because `union`, `intersection` and `difference` are a vocabulary
and two thirds of one is worse than three thirds with an overlap. The other reading is that one word per meaning
admits no exception.
**Answered by the owner (2026-09-23):** the triple stays, as functions of the type: `Set.union(first, second)`,
`Set.intersection(first, second)` and `Set.difference(first, second)`, because a symmetric operation belongs to
neither operand. `insertedAll` stays the instance form - filling one set from anything that iterates - so the overlap
is gone: the member and the function of the type say different things.

**4. The name of the ordered structure.** `Ordered.by { _.priority }`, or `Heap`, or `Priority`. `Ordered` says what
the contract is and not how it is built, which is the rule every other implementation name breaks on purpose
(`ArrayList`, `TrieMap`).
**Answered by the owner (2026-09-23):** `Heap` - the name of the structure, like every other implementation name of
the family.

**5. The name of the deque implementation.** `RingList` says the structure, which matches `ArrayList` and `TrieList`.
`ArrayDeque` says the word everybody knows and would be the only implementation name in the family that names a
contract rather than a structure.
**Answered by the owner (2026-09-23):** `RingList`.

**6. Whether the tour keeps `Stack` and `Queue`.** They are the only two places in the repository that construct one
(section 1.2), and the compiler writes `list.removeAt(list.length() - 1)` instead. Scala deprecated `mutable.Stack`
for exactly this reason and then brought it back. The traits stay either way — that is the fixed point — but the tour
could show `ConsStack` doing something a `List` cannot, which is the honest case for them.
**Answered by the owner (2026-09-23):** the tour keeps them, and shows what a `List` cannot do: `examples/tour/src/07-collections.trb` runs a
breadth-first search over a `Queue` (constant-time `dequeue` from the ring, where a list moves every item behind
the front) and declares a `ConsStack` of its own whose copies share their tails.
