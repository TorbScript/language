---
title: Checked literals
summary: A string literal where a Path, a Uri, a UriTemplate, a Regex or a resource type is expected is read by the compiler where it is written, and one that is not valid is a compile error at that line; a template and a pattern are read verbatim.
kind: reference
status: stable
order: 21
keywords:
  - checked literal
  - literal rule
  - Uri literal
  - Regex literal
  - UriTemplate
  - Path literal
  - resource
  - verbatim
source:
  - docs/design/URI.md#9-a-literal-adapts-to-a-checked-type
  - compiler/src/semantics/checker/literal.trb
---

A string literal whose expected type is one of a closed list of types of the standard library is a value of that type,
read by the compiler where it is written. A literal that is not a valid value of the type is a compile error at that
line, and a `String` value never becomes one: text that is only known when the program runs goes through the type's own
`tryFrom`, which says what was wrong with it.

## Example

```trb check
use Regex from "std/regex"
use UriTemplate from "std/uri"

fn host(of: Uri): String {
  of.host()?.show() ?? "none"
}

type OrderPath {
  id: Int
}

print host("https://Example.TEST/orders")

const year: Regex = "(?P<year>\d{4})-\d{2}"
print year.find("due 2026-10")?.named("year")

const orders: UriTemplate<OrderPath> = "/orders/{id}"
print orders.expandedFrom(OrderPath(7))

const config: Path = "config/app.trb"
print config.name()
```

`\d{4}` needs no `raw`: where a `Regex` or a `UriTemplate` is expected, the literal is the pattern or the template as
it stands, and `{id}` is the template's variable, not an interpolation.

## Syntax

```text
fn open(target: Uri) { ... }
open "https://example.test/a"                    a literal where a Uri is expected
const pattern: Regex = "\w+@\w+"                 read verbatim: no escape sequences, no interpolation
const route: UriTemplate<Order> = "/o/{id}"      read verbatim, and checked against the fields of Order
Uri.tryFrom(text)?                               a String value, read while the program runs
```

## Rules

1. **The list is closed.** `Path` (`std/path`), `Uri` and `UriReference` (`std/uri`), `UriTemplate` (`std/uri`),
   `Regex` (`std/regex`), and `Resource`, `EmbeddedBytes` and `EmbeddedText` (`std/resource`). A type of your own with
   `TryFrom<String, _>` is not on it: the compiler would have to run the program it is compiling to check the literal.

2. **Only a literal is read, and only where the type itself is expected.** A parameter, an annotated binding, a field, a
   return value and the items of a list literal expect a type. `const text = "https://…"` followed by `open(text)` is a
   `String` handed to a `Uri`, and so is a literal where a `Uri?` is expected - exactly as `1` does not adapt to a
   `Float?`.

3. **A literal that is not a valid value is an error at the literal.** The compiler runs the type's own parser on it:
   `Uri.tryFrom`, `UriTemplate.tryFrom`, `Regex.tryFrom`. A message about a pattern points at the character it fails
   at; a relative reference where a `Uri` is expected gets a message of its own.

   ```trb error
   use Regex from "std/regex"

   const broken: Regex = "ab(cd"
   // error: `ab(cd` is not a regular expression: a `(` is not closed
   ```

4. **A `Regex` and a `UriTemplate` literal are read verbatim.** Their backslashes and braces are the pattern's and the
   template's grammar, so the literal has no escape sequences and no interpolation, and `raw"..."` is not needed. The
   same text where a `String` is expected keeps its escapes, and there `\d` is an error.

5. **Every other checked literal cannot be interpolated.** A `Uri`, a `Path` or a resource literal is read where it is
   written, and a hole in it would be filled only when the program runs.

   ```trb error
   const name = "guide"
   const page: Uri = "https://example.test/{name}"
   // error: A `Uri` literal is parsed where it is written, so it cannot be interpolated
   ```

6. **A `String` value is not converted.** The message names the function that reads one while the program runs.

   ```trb error
   fn open(target: Uri) {}

   const address = "https://example.test/"
   open address
   // error: Expected `Uri`, found `String`
   ```

7. **A template typed against a record is checked against its fields.** Every variable is a field, every field without
   a default is a variable, and a field with a default is at most a query variable (`{?page}`), which a request may
   leave out. `route(template, to: Route.Order)` checks the template against the case it names the same way.

   ```trb error
   use UriTemplate from "std/uri"

   type OrderPath {
     id: Int
   }

   const orders: UriTemplate<OrderPath> = "/orders/{identifier}"
   // error: `identifier` of `/orders/{identifier}` is not a field of `OrderPath`
   // error: `OrderPath.id` has no default, and `/orders/{identifier}` has no variable for it
   ```

8. **A resource literal names a file of the package, relative to the file that writes it.** A file that is not there,
   one outside the package, and one whose name differs only in case are errors at the literal, on every platform.

9. **The value is built once per program.** The compiler records what it read, and the first evaluation builds the
   value that every later one - a `Regex` literal inside a loop included - reads again.

## What this is not

**Not a conversion.** Nothing turns a `String` into a `Uri` implicitly, and there is no `Into<Uri>` parameter: the
literal is the one case the compiler can check, and every other text goes through a `Result`.

**Not a new kind of literal.** There is no `/.../` for a pattern and no `u"..."` for a URI: the expected type decides,
the way it decides that `1` is a `Float` where a `Float` is expected.

## Related

- [Literal types](literal-types.md) - the other place a string literal adapts to the expected type.
- [Strings](strings.md) - escape sequences and interpolation, which a verbatim literal does not have.
- [std/uri](../../standard-library/uri.md) - `Uri`, `UriTemplate` and routes.
- [std/regex](../../standard-library/regex.md) - the patterns a `Regex` literal is compiled with.
