//! Rule `strings`: a multi-line `"""` or `r"""` string is written in the indented form.
//!
//! The opening `"""` stays where it is, every content line is indented two spaces deeper than the line the string
//! starts on, a blank line is empty, and a closing `"""` that stands alone is aligned with the content. The **value**
//! is untouched: the lexer takes the indentation of the first content line as the reference and subtracts it from
//! every line, so shifting all of them by the same amount changes nothing. `\{` and interpolations are never looked
//! at - only the whitespace at the start of a line is replaced.
//!
//! A string whose content starts on the line of the opening `"""` gets a line break of its own: the whitespace between
//! the quotes and the content is that string's reference, so putting the content on the next line at the target
//! indentation keeps the value as well.

use torb_syntax::token::{Token, TokenKind};

use super::edit::{Edit, EditKind, Replacement};

/// How much deeper than the line it starts on the content of a string is indented.
const STEP: &str = "  ";

/// What the rule did not do, with the reason, for the report.
pub struct Skipped {
    pub offset: u32,
    pub reason: String,
}

pub fn edits(source: &str, tokens: &[Token]) -> (Vec<Edit>, Vec<Skipped>) {
    let mut edits = Vec::new();
    let mut skipped = Vec::new();
    for token in tokens {
        if !matches!(token.kind, TokenKind::Text(_)) {
            continue;
        }
        match edit_for(source, token) {
            Outcome::Edit(edit) => edits.push(edit),
            Outcome::Skip(reason) => skipped.push(Skipped { offset: token.span.start, reason }),
            Outcome::Nothing => {}
        }
    }
    (edits, skipped)
}

enum Outcome {
    Edit(Edit),
    Skip(String),
    Nothing,
}

fn edit_for(source: &str, token: &Token) -> Outcome {
    let start = token.span.start as usize;
    let literal = &source[token.span.range()];
    let Some(quotes) = ["\"\"\"", "r\"\"\""].iter().find(|prefix| literal.starts_with(**prefix)).map(|prefix| prefix.len()) else {
        return Outcome::Nothing;
    };
    // A one-line `"""text"""` is not dedented by the lexer and keeps its form
    if !literal.contains('\n') {
        return Outcome::Nothing;
    }
    let close = token.span.end as usize - 3;
    // Without a line break behind the opening quotes the content starts on that line, and the whitespace between the
    // quotes and it is the reference of this string. It moves to the next line, where the target becomes the reference.
    let (body, missing_line_break) = match after_the_leading_line_break(source, start + quotes) {
        Some(body) => (body, false),
        None => (start + quotes, true),
    };
    let target = format!("{}{STEP}", indentation_of_the_line_at(source, start));
    let opening = format!("{}{target}", if source[body..close].contains("\r\n") { "\r\n" } else { "\n" });
    let lines = lines_of(source, body, close);
    // The indentation of the first line that has content is what the lexer subtracts from every line
    let Some(reference) = lines.iter().find(|line| line.has_content).map(|line| &source[line.start..line.content]) else {
        return Outcome::Nothing;
    };
    if missing_line_break && !lines[0].has_content {
        return Outcome::Skip("the string is not in a form the canon knows".to_string());
    }

    let mut replacements = Vec::new();
    for (index, line) in lines.iter().enumerate() {
        let is_the_first = index == 0 && missing_line_break;
        let (end, replacement) = if line.is_the_closing_quotes {
            (line.content, target.as_str())
        } else if !line.has_content {
            (line.content, "")
        } else if !source[line.start..line.end].starts_with(reference) {
            return Outcome::Skip("a content line is indented less than the first one".to_string());
        } else if is_the_first {
            (line.start + reference.len(), opening.as_str())
        } else {
            (line.start + reference.len(), target.as_str())
        };
        if &source[line.start..end] != replacement {
            replacements.push(Replacement::new(line.start as u32, end as u32, replacement));
        }
    }
    if replacements.is_empty() {
        return Outcome::Nothing;
    }
    Outcome::Edit(Edit::new(EditKind::IndentedString, token.span.start, replacements))
}

/// One physical line inside of a multi-line string.
struct Line {
    start: usize,
    /// Where the whitespace at the start of the line ends
    content: usize,
    /// Behind the line break, or at the closing quotes for the last one
    end: usize,
    /// `false` for a blank line and for the line of a closing `"""` that stands alone: what the lexer calls a line
    /// without content and strips whole
    has_content: bool,
    is_the_closing_quotes: bool,
}

/// The lines from `body` (the first byte behind the line break that follows the opening `"""`) up to `close` (the
/// closing quotes). The last one ends in front of the closing quotes: it is empty when the quotes stand alone on their
/// line, and holds content when they follow it (`  last"""`).
fn lines_of(source: &str, body: usize, close: usize) -> Vec<Line> {
    let mut lines = Vec::new();
    let mut start = body;
    loop {
        let line_break = source[start..close].find('\n');
        let end = line_break.map_or(close, |length| start + length + 1);
        let text = &source[start..end];
        let content = start + text.find(|character: char| character != ' ' && character != '\t').unwrap_or(text.len());
        let rest = &source[content..];
        let has_content = !(rest.starts_with('\n') || rest.starts_with('\r') || content == close);
        lines.push(Line { start, content, end, has_content, is_the_closing_quotes: content == close });
        if line_break.is_none() {
            return lines;
        }
        start = end;
    }
}

/// Rule 1 of multi-line strings: a line break directly behind the opening quotes (after trailing spaces) is not part
/// of the string. `None` if the content starts on that line instead.
fn after_the_leading_line_break(source: &str, behind_the_quotes: usize) -> Option<usize> {
    let rest = &source[behind_the_quotes..];
    let length = rest.find(|character: char| character != ' ' && character != '\t')?;
    let after = &rest[length..];
    if after.starts_with("\r\n") {
        return Some(behind_the_quotes + length + 2);
    }
    after.starts_with('\n').then_some(behind_the_quotes + length + 1)
}

/// The whitespace at the start of the line that `offset` is on: the line the statement with the string starts on.
fn indentation_of_the_line_at(source: &str, offset: usize) -> &str {
    let start = source[..offset].rfind('\n').map_or(0, |index| index + 1);
    let length = source[start..offset].find(|character: char| character != ' ' && character != '\t').unwrap_or(offset - start);
    &source[start..start + length]
}
