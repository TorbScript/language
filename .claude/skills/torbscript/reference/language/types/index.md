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
- **[Copy and equality](copy-and-equality.md)** - Assigning, passing or capturing a value copies it, and Equals, Hash and copy are generated for a type without being written, each only if every field supports it.
- **[The generated Show](generated-show.md)** - Show is generated for every type without being written, and its text is fixed so that two implementations of the language print the same thing for the same value.
- **[Methods and static functions](methods.md)** - Declaring self makes a function a method instead of a static function, and a type has one namespace of members, so a field and a method can never share a name.
- **[Verbs and participles](verbs-and-participles.md)** - A verb changes its receiver in place and declares var self, and its participle answers a changed copy instead, so calling the verb through a const path names the participle in its error.
- **[Mutation and var paths](var-paths.md)** - A change needs an unbroken var path from the binding down to the field being changed, and a var parameter or var self is a reference that cannot outlive the call it belongs to.
- **[Exclusivity](exclusivity.md)** _(draft)_ - CONCEPT.md specifies that two var accesses of the same call may not target the same path, and today's checker accepts the textbook counter-example instead of rejecting it.
- **[Shared types](shared-types.md)** - A shared type has an identity instead of a value, so assigning it never copies, isSame compares which object rather than which content, and Equals, Hash and copy are not generated for it.
- **[Conversions](conversions.md)** - From provides Into for free, TryFrom is for a conversion that can fail, Parse is for text, and the language has exactly four coercions that apply only where a type is expected.
- **[Property commands](property-commands.md)** - A command call on a field writes it instead of calling it, which is what lets a configuration block read like plain data without a single hand-written setter.

<!-- torb:index:end -->
