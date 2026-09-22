---
title: Reflection
summary: Why there is no runtime reflection, the four syntactic bridges that connect a type to a value instead, and the generated Encode and Decode pair that covers serialization.
kind: index
status: stable
order: 90
---

Types and values never mix at runtime. This section says what replaces the reflection another language would reach
for: four compile-time bridges from a type to a value, and one generated trait pair for everything that would
otherwise need to look a type up.

## What belongs here

What does not belong here: quoted expressions (`Expression<Value>`), which read code that already ran through name
resolution and type checking and are in `language/functions/`. Every page in this folder is a reference page: an
example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[There is no reflection](no-reflection.md)** - A type never flows as a value, so there is no Type type, no typeof and no Class.forName - only four syntactic bridges connect a type to a value, all resolved at compile time.
- **[Encode and Decode](encode-and-decode.md)** - Encode and Decode are generated the same way Equals and Show are, so a type describes itself to any format's Encoder and reads itself from its Decoder without a line of hand-written serialization code.
- **[Encoder and Decoder](encoders.md)** - Encoder and Decoder each name every scalar the language has - bool, int, unsigned, float, decimal, string, bytes - plus the four shapes a value can take, sequence, map, record and variant.

<!-- torb:index:end -->

