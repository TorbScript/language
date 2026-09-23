# generic-scalar

The regression test for what a library generic over a number type runs into, per
[`docs/design/LINEAR.md`](../../docs/design/LINEAR.md) section 12, items 2, 10 and 14: an operator reaching the very
function its method form reaches, a `const` of a generic type instantiated once per type argument, a numeric literal
inside a body generic over its number type, and the constants `Numeric` requires of every number type - `Item.zero`
and `Item.one` read through the parameter.

`torb check examples/generic-scalar` answers "no problems", and `torb run examples/generic-scalar` builds and runs
`src/main.trb`, printing five lines from the native binary.
