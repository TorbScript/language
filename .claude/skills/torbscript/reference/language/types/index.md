---
title: Types
summary: Declaring a type, its fields, its methods, what is generated for it, and how one is changed.
kind: index
status: stable
order: 30
---

Everything about a `type` declaration: fields and their visibility, construction, methods, mutation through a `var`
path, conversions, and `shared type` for identity.

## What belongs here

What does not belong here: cases and pattern matching, which are in `pattern-matching/`. Every page in this folder is a reference page:
an example first, then the syntax, then numbered rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[Declaring a type](declaring-a-type.md)** - One keyword declares every data type. Fields are const unless marked var, members are public unless marked private, and Equals, Hash, Show and copy are generated.
- **[Fields](fields.md)** - A field is const unless marked var, and private or private(var) decide who may read it and who may write it, independently of each other.
- **[Construction](construction.md)** - Every type has exactly one constructor, generated from its fields in declaration order, and it never contains logic - validation and parsing are static factory functions instead.
- **[Data or capsule](data-or-capsule.md)** - A type is data, whose constructor is the way in, or a capsule, whose constructor a private field without a default closes - and then a factory, accessors and one conversion pair take its place.
- **[Copy and equality](copy-and-equality.md)** - Assigning, passing or capturing a value copies it, and Equals, Hash and copy are generated for a type without being written, each only if every field supports it.
- **[The generated Show](generated-show.md)** - Show is generated for every type without being written, and its text is fixed so that two implementations of the language print the same thing for the same value.
- **[Methods and `static fn`s](methods.md)** - A member says what it is with two words - static belongs to the type, var may change - and a type has one namespace of members, so a field and a method can never share a name.
- **[Verbs and participles](verbs-and-participles.md)** - A verb changes its receiver in place and is a var fn, and its participle answers a changed copy instead, so calling the verb through a const path names the participle in its error.
- **[Mutation and var paths](var-paths.md)** - A change needs an unbroken var path from the binding down to the field being changed, and a var parameter or a var fn receiver is a reference that cannot outlive the call it belongs to.
- **[Exclusivity](exclusivity.md)** - Two var accesses of the same call may never target the same path, so swap(a, a) and two indices the checker cannot tell apart are both compile errors, and items.swapAt is the one access that is allowed instead.
- **[Shared types](shared-types.md)** - A shared type has an identity instead of a value, so assigning it never copies, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
- **[Conversions](conversions.md)** - From provides Into for free and TryFrom provides TryInto, text is a source like any other, and the language has exactly four coercions that apply only where a type is expected.
- **[Property commands](property-commands.md)** - A command call on a field writes it instead of calling it, which is what lets a configuration block read like plain data without a single hand-written setter.

<!-- torb:index:end -->
