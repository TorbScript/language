# generic-scalar

The probe of what a library generic over a number type used to run into, per
[`docs/design/LINEAR.md`](../../docs/design/LINEAR.md) section 12, items 2 and 14: an operator reaching the very
function its method form reaches, a `const` of a generic type instantiated once per type argument, and - the gap
that remained - a numeric literal inside a body generic over its number type, which the checker used to type as
`Int64` and accept where the type parameter was expected.

That gap is closed. `torb check examples/generic-scalar` answers "no problems", and `torb build .` and `torb run .`
both build and run `src/main.trb`, printing the same four lines from the native binary and the interpreter. This
package is now the regression test for that fix rather than the open probe it started as.
