---
title: Multi-line strings
summary: A `\"\"\"` string is dedented by the indentation of its first line with content, so a block of text reads at the indentation of the code around it instead of jammed against the left margin.
kind: reference
status: stable
order: 4
keywords:
  - triple quote
  - dedent
  - raw string
source:
  - CONCEPT.md#strings
---

A `"""..."""` string spans more than one line and is dedented: the indentation of its first line with content is
removed from every line, so the text can be written at the indentation of the call around it rather than pushed
against the left margin of the file.

## Example

```trb check
fn generated(): String {
  const header = """
    #include <stdint.h>

    static int64_t counted(void)
    """
  header
}

print generated()
```

## Syntax

```text
"""
  text
  """                                     dedented by the indentation of the first content line
"""text"""                                one line: unchanged, not dedented
r"""
  text
  """                                     the raw form: no escapes, no interpolation
```

## Rules

1. **A line break directly after the opening `"""` is not part of the string.** Trailing spaces before it are
   allowed and are dropped along with it, which is why the example above has no blank first line even though the
   opening `"""` is followed by a newline.

2. **The indentation of the first line that has content is the reference, and it is removed from every line.** A line
   indented less than the reference, or indented with something other than the reference followed by more (a tab
   where the reference has a space), is a lexer error at that line - never a silent guess. A blank line becomes empty
   regardless of its own indentation.

   ```trb error
   fn generated(): String {
     const header = """
       line one
     line two
       """
     header
   }
   // error: This line is indented less than the first line of the string
   ```

3. **If the closing `"""` stands alone on its line, that line's indentation is not part of the string, and the string
   ends with the line break of the last content line.** The example above ends with a newline after
   `static int64_t counted(void)`, because the closing `"""` is on its own line.

4. **If the closing `"""` does not stand alone, the string ends exactly where it appears, with no added line break.**

   ```trb check
   fn generated(): String {
     const header = """
       line one
       line two"""
     header
   }

   print generated()
   ```

5. **`"""text"""` on one line is unchanged.** Dedenting only applies to a string that really spans more than one
   physical line; a one-line triple-quoted string is exactly its content.

6. **An interpolated value is never dedented, only the literal text around it, and an interpolation counts as content
   for rule 2.** Escapes in the literal text run after dedenting, not before.

7. **`r"""..."""` is the raw form: no escapes, no interpolation, same dedenting rules otherwise.** It is what a
   multi-line path, regular expression or JSON body is written in.

## What this is not

**Dedenting is not based on counting spaces from the left margin of the file.** It is based on the first content
line's own indentation, so the same string is unaffected by how deeply the call around it is nested.

```trb check
fn wrapped(): String {
  if true {
    const nested = """
      one
      two
      """
    return nested
  }
  ""
}

print wrapped()
```

```trb error
fn generated(): String {
  const header = """
      line one
    line two
      """
  header
}
// error: This line is indented less than the first line of the string
```

The second block is not "line two, indented one level less than the surrounding code" - it is indented less than
`line one`, the reference the first block established, which is exactly rule 2.

## Related

- [Literals](literals.md) - the single-line string form, and the escapes a non-raw string has.
- [String interpolation](string-interpolation.md) - what `{expression}` does inside a multi-line string, too.
- [Command calls](command-calls.md) - the same indentation rule stated as part of the formatter canon.
