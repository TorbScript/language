# query-provider

The probe of `Expression<Value>` as a query builder: `src/query.trb`'s `Query` mirrors `Iterate` - `filter`, `sorted`,
`take` - but records each step as a quoted expression instead of running it, and `src/sql.trb` translates the tree
to a `Fragment` of SQL only when the caller finally asks for the statement. `src/main.trb` runs the same query in
memory, where the closures execute, and translated, where they do not, plus `isLucky` to show what happens when a
provider cannot translate one predicate.

`torb check examples/query-provider` answers `4 files, no problems`: every line here type checks. `torb build` and
`torb run` do not - `error: a quoted expression is not supported by the native back end yet` - because quoted
expressions are not lowered by the native back end yet; only `torb check` runs this probe today.
