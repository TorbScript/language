# Binary Formats

**Status: built (2026-09-27)** — the owner decided on 2026-09-27 that the standard library has one package for binary
formats, `std/binary`, with the types `ByteReader`, `ByteWriter`, `BitReader` and `BitWriter`. It exists, and
`std/dns` (the wire format), `std/compression` (the DEFLATE bits and the gzip header) and `std/archive` (the tar
headers) read and write with it; `std/digest` writes its padding and its digest with it (decision 9).

**A read never panics, and a write never fails.** Every read answers a `Result` whose failure names the byte and what
was expected there, so bytes from a file or the network are read without a check in front of every field; a format
turns that failure into its own error through `From`, and `?` does the rest. Every write takes the width type of its
field, so the range of a number is checked once, where the caller narrows it, with the caller's own words.

```text
   Bytes ──→ ByteReader(bytes, order:) ── uint8()..uint64(), int8()..int64(), float32(), float64()
                 │                        varUInt(), varInt(encoding:), bytes(n), text(n), terminatedText()
                 ├── limited(n) ──→ a reader over the next n bytes, the parent moved past them
                 └── Result<_, ReadError> ──→ From ──→ DnsError, CompressionError, ArchiveError

   ByteWriter(order:) ── the same writes of width types, append, reserve16()/reserve32() ─→ patch(mark, value)
   BitReader(bytes, order:) / BitWriter(order:) ── bits(n), bit(), alignToByte()   (BitOrder: LSB or MSB first)
```

## Contents

- **[1. Goals](#1-goals)**
- **[2. The package boundary](#2-the-package-boundary)**
- **[3. Reading bytes](#3-reading-bytes)**
- **[4. What a range of bytes costs](#4-what-a-range-of-bytes-costs)**
- **[5. Writing bytes](#5-writing-bytes)**
- **[6. Bits](#6-bits)**
- **[7. The migration, measured](#7-the-migration-measured)**
- **[8. What this is not](#8-what-this-is-not)**
- **[9. Decisions](#9-decisions)**
- **[10. Open](#10-open)**

## 1. Goals

1. **One cursor for every format.** `std/dns`, `std/compression` and `std/archive` each had a reader and a writer of
   their own, with their own bounds checks and their own ways of saying where the bytes ended. They share one now, and
   the next format (a TLS record, a WebAssembly module, a PNG chunk) starts from it.
2. **Bytes from outside cannot make a program panic.** A read checks what it needs against what is left and answers a
   `ReadError` otherwise; there is no read that indexes first.
3. **The failure says where.** `truncated at byte 12: 2 bytes for a UInt16, 1 left` can be checked against a hex dump by
   hand, and every offset counts from the start of the bytes the reader was made from, whatever reader inside another
   found it.
4. **Width types in the signatures.** A `UInt16` field is read as a `UInt16` and written from one, so a port of 70000
   is a failure of the caller's `tryInto()`, not a silently truncated field.
5. **Not slower than the hand-written code it replaces** (section 7).

## 2. The package boundary

`std/binary` is pure: no native, no capability, ordinary TorbScript over `List<UInt8>`, so it runs the same in the VM
and in a native binary and a sandboxed script may import it. It depends on `std/core` (for `ByteOrder`) and
`std/stream` (for `Bytes` and `textOf`), and nothing below it depends on it. The compiler reaches it through
`std/archive`, `std/compression` and `std/digest`, which the package manager uses, so everything in it is written in
the forms the published seed knows: no new syntax and no new native.

## 3. Reading bytes

`ByteReader(bytes, order: ByteOrder.BigEndian)` is a value: `input` (the bytes), `order`, and private fields for the
window it may read and the cursor. **A read moves the cursor past what it read, and a read that fails moves nothing**,
so a format can try one reading and fall back to another. A copy of a reader reads on from where the original stood
without moving it, which is how a format looks ahead.

| Read | Answers | Notes |
|------|---------|-------|
| `uint8()` .. `uint64()` | `UInt8` .. `UInt64` | in the reader's order or `order: Some(...)` |
| `int8()` .. `int64()` | `Int8` .. `Int64` | two's complement |
| `float32()`, `float64()` | `Float64` | IEEE 754 bit patterns; decision 3 |
| `varUInt()`, `varInt(encoding:)` | `UInt64`, `Int64` | LEB128, at most 10 bytes; decision 2 |
| `bytes(count)` | `Bytes` | section 4 |
| `limited(count)` | `ByteReader` | a window of the next bytes; the parent moves past them |
| `text(count)`, `terminatedText()` | `String` | UTF-8; the NUL is read and not part of the text |
| `seek(position)`, `skip(count)` | `Void` | the end is a position too |

`position()` counts from the start of the reader's own window, so a limited reader starts at 0; `offset()` counts from
the start of `input`, and it is the number every `ReadError` names. `remaining()` is what is left before the end of the
window.

`ReadError` has one case per way a read fails, each with the offset: `Truncated(offset, expected, needed, available)`,
`TruncatedBits`, `Unterminated` (no NUL before the end), `Overlong` and `Overflow` (a LEB128 of more than 10 bytes, or
a value its type cannot hold), `InvalidText` (the offset of the byte that breaks the UTF-8) and `OutOfRange` (a seek or
a count below zero). `show()` is one line of English; `offset()` reads the offset of any case.

**A format converts, and names its own parts where it needs to.** `extend DnsError with From<ReadError>` is what `?`
uses anywhere a read's failure is enough. Where a format's own error says more - the DNS names the part of the message
the bytes end in, `the message ends inside the header, which starts at byte 10`, and its tests pin every such text -
the format maps the failure itself: `std/dns` has one generic extension, `reader.uint16().inside("the header", scope)?`,
that turns a `ReadError` into the `DnsError` of that part.

## 4. What a range of bytes costs

**Today a range of a `Bytes` value is a copy.** `Bytes` is `List<UInt8>`, a trait; `bytes[from..to]` on a value of that
type reaches the default member `List.slice`, which empties a copy of the list and appends the items one by one. The
runtime can do better - `ArrayList.slice` shares the storage (`torb_list_slice`, an offset and a length, O(1)) - but a
call through the trait-typed value does not reach that override: the generated C of the benchmark of section 7 has no
call of `ArrayList.sliceBetween` at all, and every range is the default's loop. That is a finding for the back end
(the override of a default member through a witness table), recorded in section 10.

So the reader is built not to need ranges:

- **`limited(count)` copies nothing.** A limited reader holds the same `input` and a window of it (its first and its
  end byte), so reading the data of a DNS record through one costs the reader and nothing else. The first version
  sliced `input`, and the allocation count of the DNS benchmark showed it: 412 432 allocations more than the
  hand-written reader, all of them record data copied; with the window it is the same count to within 32.
- **`bytes(count)` is a range**, because what it answers has to be a `Bytes` of its own; a format that only looks at the
  bytes uses a limited reader instead. When the back end reaches `ArrayList.slice`, `bytes` shares the storage without a
  change here - and then keeps the whole of `input` alive for as long as the answer lives, which is what a range does.

## 5. Writing bytes

`ByteWriter(order: ByteOrder.BigEndian)` is a growable buffer with the same writes: `uint8(value: UInt8)` .. `uint64`,
`int8` .. `int64`, `float32` and `float64` (both from a `Float64`), `varUInt`, `varInt(value, encoding:)`, `append`,
`text` and `terminatedText`. `length()` is how many bytes were written and `bytes()` answers them; a list is a value,
so writing on afterwards does not change what `bytes()` answered.

**A length that comes first is a mark.** `reserve16()` and `reserve32()` write zeros and answer a `Mark<UInt16>` or a
`Mark<UInt32>`; `lengthAfter(mark)` is how many bytes were written after the room, and `patch(mark, value)` writes a
value of the mark's width into it, in the order the room was made with. The width is a type parameter of the mark, so
`patch` of a `Mark<UInt16>` takes a `UInt16` and the range check is again the caller's. The DNS writes the data length
of every record and the length of the EDNS0 options this way.

`terminatedText` writes the text and a NUL; a NUL inside the text ends it early for whoever reads it back, which the
writer does not check (section 10).

## 6. Bits

`BitReader(bytes, order: BitOrder.MostSignificantFirst)` and `BitWriter(order:)` read and write numbers of 0 to 56
bits. **`BitOrder.LeastSignificantFirst`** fills a byte from its lowest bit and a number from its lowest bit: DEFLATE,
GIF's LZW. **`BitOrder.MostSignificantFirst`** fills a byte from its highest bit and a number from its highest bit:
JPEG, H.264, the samples of PNG and PBM. DEFLATE's Huffman codes are packed the other way round from its numbers; that
is DEFLATE's business, and `std/compression` reverses a code before it writes it.

The reader loads a byte when a read needs one of its bits, so fewer than eight bits wait between two reads, and
`position()` - the bytes it has started - is where a format goes on after `alignToByte()`: a stored block of DEFLATE
reads its length with `bits(16)` and its bytes with `bytes(count)`, and gzip reads its trailer at the `position()` the
DEFLATE stream ended on. `seek(position)` starts at the first bit of a byte, which is where a DEFLATE stream inside a
gzip member begins. 56 is the most a read can load into the 64 bits of an `Int` with seven bits still waiting; a width
outside 0 to 56 is a panic, because it is the caller's mistake and not the data's.

## 7. The migration, measured

Every migration kept the behaviour and the output of the tests, and a differential program checked more than the
tests do: every truncation and every flipped byte of a DNS message of 311 bytes with every record type, the encoder's
refusals, every truncation and every flipped byte of a gzip member, a member with every optional header field cut at
every byte, stored blocks cut at every byte, and tar archives cut and changed at every field - 851 lines of results,
the same with the `std/` before the migration and after it.

`benchmarks/binary-formats.trb` decodes a DNS response of 1 800 records (62 484 bytes) 40 times, deflates a megabyte
of text into a gzip member and inflates it 4 times, and hashes the text with SHA-256 4 times. It is built once
against the `std/` before the migration and once against the one after (`TORB_STD=...`), and the two binaries run
alternately, pinned to one core at high priority; seconds, the fastest and the median of the runs.

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

**Where the time went, and what brought it back.** The first version was 3% slower on the DNS and 19% on SHA-256
(which then read its words through a reader too): every read of `input[i]` through the field retained and released the
list, `==` on a `ByteOrder` called the generated `equals`, and the end of the window was a call. The reads now take a
local of the list once, match the order, and compute the end inline, and the DNS record data is read through a reader
nothing else holds, so reading it changes it in place instead of copying it first.

## 8. What this is not

- **Not a serialization framework.** There is no derived `encode` into bytes: `std/encoding`'s `Encode` and `Decode`
  are the formats of values, and a binary `Format` of them (CBOR, MessagePack) would be written with this package.
- **Not a stream.** A reader reads bytes that are all there; a format that arrives in chunks collects them first (a
  DNS message, a gzip member, a tar archive all do), or reads each chunk that is complete. `std/stream` has the stages
  for byte streams.
- **Not `std/http`'s buffer.** The HTTP parser reads lines of a socket into a buffer that grows as bytes arrive
  (`std/http/src/connection.trb`); it reads no numbers of fixed width and has nothing to migrate.

## 9. Decisions

1. **Names.** The owner's: `std/binary`, `ByteReader`, `ByteWriter`, `BitReader`, `BitWriter`, and the reads
   `uint8()` .. `uint64()`, `int8()` .. `int64()`, `float32()`, `float64()`, `varUInt()`, `varInt()`. The rest are
   written out: `SignedEncoding.SignExtended` and `.Zigzag`, `BitOrder.LeastSignificantFirst` and
   `.MostSignificantFirst`, `terminatedText`, `alignToByte`, `lengthAfter`. `ByteOrder` is `std/core`'s, which already
   had the two cases (`ByteOrder.current` answers the target's), and `std/binary` re-exports it.
2. **LEB128 with a sign is sign-extended by default, zigzag on request.** `varInt()` reads signed LEB128 as WebAssembly
   and DWARF write it (`-1` is `7f`); `varInt(encoding: .Zigzag)` reads protobuf's `sint32`/`sint64` (`-1` is `01`).
   Protobuf's plain `int64` is `varUInt()` of the two's complement. Both are at most 10 bytes: a tenth byte that says
   another follows is `Overlong`, one whose bits do not fit an `Int64`/`UInt64` is `Overflow`. A longer encoding of a
   small value (`80 00` for 0) is read, as both formats allow; the writer always writes the shortest.
3. **Floats answer `Float64`, and a `nan` loses its payload.** A `Float32` has no arithmetic and no conversion from a
   `Float64` in a compiled program (std/number, `Real`, "Open"), so `float32()` widens the binary32 to the `Float64`
   that holds it exactly, and `ByteWriter.float32` writes the binary32 nearest to a `Float64`, rounded to even, an
   infinity where it is too large. The bit patterns are computed in TorbScript from exact powers of two, with no
   native: that keeps the package inside what the seed knows (section 2), and it is why a `nan` reads as `Float64.nan`
   and writes as the quiet `7ff8000000000000` (`7fc00000`), whatever its payload was. A native `bits()` of the float
   types is section 10.
4. **Every read answers `Result<_, ReadError>`, and a format converts through `From`.** `DnsError`,
   `CompressionError` and `ArchiveError` each have a `From<ReadError>`; where a format's error names more than the
   read's (the part of a DNS message), the format maps the failure where it reads (section 3).
5. **The order of one read is an `Option` parameter.** A default cannot read `self` (a default is evaluated without
   one), so `uint32(order: ByteOrder? = None)` takes the reader's order where it is `None`. An implicit case does not
   look through the `Option` today, so the call is `uint32(order: Some(.LittleEndian))`; section 10.
6. **A limited reader is a window, not a copy** (section 4), and its offsets count from the start of `input`, so a
   failure inside the data of a record names the byte of the whole message.
7. **The writer checks nothing.** Its writes take width types (goal 4), and the one write that could be refused -
   `terminatedText` of a text with a NUL - is documented instead, because a check there would make every text write
   fallible for a mistake nobody makes by accident.
8. **A width outside 0 to 56 of `bits` panics.** It is a constant of the format in every caller, never a number from
   the bytes; `# Panics` says so.
9. **`std/digest` reads its blocks straight from the list.** SHA-256 reads sixteen big-endian words per 64-byte block,
   and `add` checks the 64 bytes once. Read through a `ByteReader`, every word checked the bounds again, answered a
   `Result` and converted through `UInt32.tryFrom`: SHA-256 took a sixth longer (0.141 against 0.167 seconds for 4 MB).
   So the block stays an indexed read, and `finished` writes the padding's length (`uint64`) and the digest's words
   (`uint32`) with a `ByteWriter`, where the cost does not matter.
10. **`std/http` is not migrated.** It has no reader of numbers (section 8).

## 10. Open

- **The override of a default member through a trait-typed value.** `List.slice` of a `Bytes` value runs the default
  and copies, where `ArrayList.slice` shares the storage (section 4); once the witness table reaches the override,
  `ByteReader.bytes` stops copying with no change here. A back-end finding for docs/PERFORMANCE.md.
- **An implicit case through an `Option`.** `order: .LittleEndian` for a parameter of type `ByteOrder?` could resolve in
  `ByteOrder`, as Swift does; the checker says `Option<ByteOrder>` has no case `LittleEndian` today.
- **`Float32` and the bit patterns.** When `Float32` has arithmetic and a conversion from `Float64`, `float32()` can
  answer a `Float32`; a native `Float64.bits()` and `Float64.ofBits(_)` (and the same for `Float32`) would keep a `nan`'s
  payload and replace the arithmetic of `std/binary/src/number.trb`. Both are a new native, so both take the two
  commits of a seed change.
- **A range check of `terminatedText`**, if a format ever needs the writer to refuse a NUL.
- **Stream readers.** A `ByteReader` over a `Source<Bytes, _>` that waits for the bytes a read needs would let a
  format read a socket without collecting a whole message first; nothing needs it yet.
