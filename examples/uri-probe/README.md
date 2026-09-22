# uri-probe

The probe of [`docs/design/URI.md`](../../docs/design/URI.md), run as a compiled binary rather than only type
checked: `src/uri.trb` parses and normalizes a `Uri` by hand per RFC 3986 with `Show`, `Equals`, `Hash` and `Compare`
over what it holds; `src/urn.trb` is the one refinement the design keeps as its own type; `src/file.trb` is the
bridge between `Path` and `Uri`, neither direction a `From`; and `src/storage.trb` probes the scheme-driven driver
layer - one capability trait, two drivers, and a registry that is an ordinary value with no reflection and no import
side effect. It is a probe and not the package: it covers the forms the design's tables name and nothing else, and it
has no IDNA.

`torb check examples/uri-probe` answers `6 files, no problems`, and `torb build examples/uri-probe` and
`torb run examples/uri-probe` both build and run every probe in `src/main.trb` - parsing, normalization, reference
resolution, `Path` both ways, `Equals`/`Hash`/`Compare` on the capsule, and the two drivers.
