---
title: std/binary
summary: ByteReader, ByteWriter, BitReader and BitWriter - numbers of every width in either byte order, IEEE 754 floats, LEB128, runs of bytes and text, read with a ReadError instead of a panic and written into a growable buffer.
kind: package
status: stable
order: 184
keywords:
  - std/binary
  - ByteReader
  - ByteWriter
  - BitReader
  - BitWriter
  - ReadError
  - byte order
  - endianness
  - LEB128
  - varint
source:
  - std/binary/src/lib.trb
  - std/binary/src/reader.trb
  - std/binary/src/writer.trb
  - std/binary/src/bits.trb
  - std/binary/src/error.trb
---

`std/binary` reads and writes binary formats. A `ByteReader` is a cursor over `Bytes` whose every read answers a
`Result`, so bytes from a file or the network are read without a check in front of each field and cannot make a
program panic; a `ByteWriter` is a growable buffer with the same typed writes. `BitReader` and `BitWriter` do the same
for numbers packed into bits. `std/dns`, `std/compression` and `std/archive` read and write their formats with it. It is
ordinary TorbScript with no native, and the design is BINARY.md.

## Import

```trb fragment
use ByteReader, ByteWriter, BitReader, BitWriter, ReadError from "std/binary"
```

```trb check
use ByteReader, ByteWriter from "std/binary"

var writer = ByteWriter()
writer.uint16 443
writer.varUInt 300
writer.terminatedText "host"
var reader = ByteReader writer.bytes()
print reader.uint16()
print reader.varUInt()
print reader.terminatedText()
```

`ByteOrder` (of `std/core`) is re-exported, so a file that names an order needs no second import.

## Declarations

### `ByteReader`

```trb fragment
public type ByteReader {
  input: Bytes
  order: ByteOrder = ByteOrder.BigEndian

  fn position(): Int
  fn offset(): Int
  fn remaining(): Int
  var fn seek(position: Int): Result<Void, ReadError>
  var fn skip(count: Int): Result<Void, ReadError>
  var fn uint8(): Result<UInt8, ReadError>
  var fn uint16(order: ByteOrder? = None): Result<UInt16, ReadError>
  var fn uint32(order: ByteOrder? = None): Result<UInt32, ReadError>
  var fn uint64(order: ByteOrder? = None): Result<UInt64, ReadError>
  var fn int8(): Result<Int8, ReadError>
  var fn int16(order: ByteOrder? = None): Result<Int16, ReadError>
  var fn int32(order: ByteOrder? = None): Result<Int32, ReadError>
  var fn int64(order: ByteOrder? = None): Result<Int64, ReadError>
  var fn float32(order: ByteOrder? = None): Result<Float64, ReadError>
  var fn float64(order: ByteOrder? = None): Result<Float64, ReadError>
  var fn varUInt(): Result<UInt64, ReadError>
  var fn varInt(encoding: SignedEncoding = SignedEncoding.SignExtended): Result<Int64, ReadError>
  var fn bytes(count: Int): Result<Bytes, ReadError>
  var fn limited(count: Int): Result<Self, ReadError>
  var fn text(count: Int): Result<String, ReadError>
  var fn terminatedText(): Result<String, ReadError>
}
```

`ByteReader(bytes)` reads big endian, the order of network protocols; `ByteReader(bytes, order: ByteOrder.LittleEndian)`
reads little endian, and every read of a number can name its own order with `order: Some(.LittleEndian)`. Every read
moves the cursor past what it read, and **a read that fails moves nothing**.

- The numbers answer the width types: `uint16()` a `UInt16`, `int32()` an `Int32` in two's complement. `Int.from(...)`
  widens every one of them but `UInt64`.
- `float32()` and `float64()` read the IEEE 754 bit patterns. Both answer a `Float64`: a binary32 widens to one exactly,
  and a `Float32` has no arithmetic yet. A `nan` reads as `Float64.nan`, without its payload.
- `varUInt()` is unsigned LEB128 (protobuf's varint, WebAssembly's `u64`); `varInt()` is signed LEB128 as WebAssembly
  and DWARF write it, and `varInt(encoding: .Zigzag)` is protobuf's `sint64`. At most 10 bytes; a longer encoding of a
  small value is read, as both formats allow.
- `bytes(count)` answers the next bytes, `text(count)` the next bytes as UTF-8, and `terminatedText()` the UTF-8 up to a
  NUL byte, which it reads too.
- `limited(count)` answers a reader over the next `count` bytes only, and moves this one past them: a record whose
  length comes first is read with it, and a read past its end fails even where the parent has more.
- `position()` counts from the start of the reader's own bytes (0 for a new limited reader); `offset()` counts from the
  start of `input` - the byte a `ReadError` names. `seek(position)` moves to a position, where the end is one too.

```trb check
use ByteReader, ReadError from "std/binary"

var reader = ByteReader([0, 5, 104, 101, 108, 108, 111, 33])
const length = Int.from reader.uint16().expect("a length")
var record = reader.limited(length).expect("the record")
print record.text(5)
print reader.uint8()
print(record.uint8() == Fail(ReadError.Truncated(7, "a UInt8", 1, 0)))
```

### `ReadError`

```trb fragment
public type ReadError with Show, Error {
  case Truncated(offset: Int, expected: String, needed: Int, available: Int)
  case TruncatedBits(offset: Int, needed: Int, available: Int)
  case Unterminated(offset: Int, available: Int)
  case Overlong(offset: Int, expected: String)
  case Overflow(offset: Int, expected: String)
  case InvalidText(offset: Int)
  case OutOfRange(offset: Int, position: Int, length: Int)

  fn offset(): Int
}
```

Why a read failed, and where: `truncated at byte 12: 2 bytes for a UInt16, 1 left`. A format turns it into its own
error with an `extend ... with From<ReadError>`, and `?` converts on the way out of every function that answers that
error: `DnsError`, `CompressionError` and `ArchiveError` do.

### `ByteWriter`

```trb fragment
public type ByteWriter {
  order: ByteOrder = ByteOrder.BigEndian

  fn length(): Int
  fn bytes(): Bytes
  var fn uint8(value: UInt8)
  var fn uint16(value: UInt16, order: ByteOrder? = None)
  var fn uint32(value: UInt32, order: ByteOrder? = None)
  var fn uint64(value: UInt64, order: ByteOrder? = None)
  var fn int8(value: Int8)
  var fn int16(value: Int16, order: ByteOrder? = None)
  var fn int32(value: Int32, order: ByteOrder? = None)
  var fn int64(value: Int64, order: ByteOrder? = None)
  var fn float32(value: Float64, order: ByteOrder? = None)
  var fn float64(value: Float64, order: ByteOrder? = None)
  var fn varUInt(value: UInt64)
  var fn varInt(value: Int64, encoding: SignedEncoding = SignedEncoding.SignExtended)
  var fn append(bytes: Bytes)
  var fn text(value: String)
  var fn terminatedText(value: String)
  var fn reserve16(order: ByteOrder? = None): Mark<UInt16>
  var fn reserve32(order: ByteOrder? = None): Mark<UInt32>
  fn lengthAfter<Width>(mark: Mark<Width>): Int
  var fn patch<Width: Into<Int>>(mark: Mark<Width>, value: Width)
}
```

**Nothing here fails.** The writes take width types, so a number that does not fit its field is refused where the
caller narrows it - `UInt16.tryFrom(port)` or `port.tryInto()` - with the caller's own error. `float32` writes the
binary32 nearest to a `Float64`, rounded to even. A length that comes before what it counts is left open with
`reserve16()` or `reserve32()` and filled in with `patch` once `lengthAfter(mark)` is known:

```trb check
use ByteWriter from "std/binary"

var writer = ByteWriter()
const mark = writer.reserve16()
writer.text "hello"
writer.patch mark, UInt16.tryFrom(writer.lengthAfter(mark)).expect("a short length")
print writer.bytes()
```

### `BitReader` and `BitWriter`

```trb fragment
public type BitReader {
  input: Bytes
  order: BitOrder = BitOrder.MostSignificantFirst

  fn position(): Int
  fn remaining(): Int
  var fn bits(width: Int): Result<Int, ReadError>
  var fn bit(): Result<Bool, ReadError>
  var fn alignToByte()
  var fn seek(position: Int): Result<Void, ReadError>
  var fn bytes(count: Int): Result<Bytes, ReadError>
}

public type BitWriter {
  order: BitOrder = BitOrder.MostSignificantFirst

  fn length(): Int
  fn bytes(): Bytes
  var fn bits(value: Int, width: Int)
  var fn bit(value: Bool)
  var fn alignToByte()
}
```

Numbers of 0 to 56 bits. In `BitOrder.MostSignificantFirst` - JPEG, H.264, the samples of PNG - the first bit read is
the highest of the number; in `BitOrder.LeastSignificantFirst` - DEFLATE - it is the lowest. `position()` of a reader
is the number of bytes it has started, which is where a format goes on after `alignToByte()`; `bytes()` of a writer
fills a byte that is not full yet with zero bits and leaves the writer as it was.

```trb check
use BitOrder, BitReader, BitWriter from "std/binary"

var writer = BitWriter order: BitOrder.LeastSignificantFirst
writer.bit true
writer.bits 1, 2
var reader = BitReader writer.bytes(), order: BitOrder.LeastSignificantFirst
print reader.bit()
print reader.bits(2)
```

## Pitfalls

**`bytes(count)` copies today.** A range of a `Bytes` value reaches the default `List.slice`, which copies the items,
where `ArrayList`'s own range would share the storage (BINARY.md section 4). A format that only
looks at a record reads it through `limited(count)`, which copies nothing.

**The order of a single read is `Some(...)`.** `order: .LittleEndian` does not find the case through the `Option` the
parameter is; write `order: Some(.LittleEndian)`, or make the reader little endian.

**A width outside 0 to 56 is a panic** of `bits`, because it is the caller's mistake and not the data's.

## Related

- [std/dns](dns.md), [std/compression](compression.md) and [std/archive](archive.md) - the formats written with it.
- [std/stream](stream.md) - `Bytes`, and `textOf`, which `text` and `terminatedText` decode with.
- [The standard library](index.md) - the other packages.

