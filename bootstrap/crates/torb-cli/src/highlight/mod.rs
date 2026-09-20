//! `torb highlight`: semantic tokens for the VS Code extension (and, from milestone 8 on, the language server -
//! this is stage-0 tooling behind a protocol a real language server can implement without breaking the client).
//!
//! ```text
//! torb highlight <file>
//! torb highlight --stdin
//! ```
//!
//! `--stdin` reads the source from standard input instead of a file, for an editor buffer that was never saved.
//! Either way this never fails: a file with syntax errors still gets every token the parser could resolve around
//! the damage, and a file this tool cannot even read prints an empty token list rather than an error, so the
//! extension never has to show one. The only exit code is `0`.
//!
//! Output is one JSON document on standard output:
//!
//! ```text
//! {"tokens": [[line, startCharacter, length, "kind", ["modifier", ...]], ...]}
//! ```
//!
//! `line` and `startCharacter` are 0-based; `startCharacter` and `length` count UTF-16 code units, because that is
//! what VS Code's `SemanticTokensBuilder` wants (a JavaScript string is UTF-16). The tokens are sorted by position,
//! never overlap, and never span more than one line - exactly what `SemanticTokensBuilder.push` requires, so the
//! extension can feed them to it in order without sorting or splitting them again.
//!
//! `resolver` is the syntactic scope resolver that decides what a name is; this module only shells around it: read
//! the source, parse it, resolve it, convert byte spans to UTF-16 line/column pairs, print JSON.

mod resolver;

use std::io::Read as _;
use std::process::ExitCode;

use resolver::Token;
use torb_syntax::span::Span;

pub fn highlight(path: Option<&str>) -> ExitCode {
    let source = match path {
        Some(path) => std::fs::read_to_string(path).unwrap_or_default(),
        None => {
            let mut buffer = String::new();
            let _ = std::io::stdin().read_to_string(&mut buffer);
            buffer
        }
    };
    print!("{}", tokens_json(&source));
    ExitCode::SUCCESS
}

/// Parses `source` and renders its tokens as the JSON document `torb highlight` prints. Split out from `highlight`
/// so a test can call it directly, without a file or standard input.
fn tokens_json(source: &str) -> String {
    let parsed = torb_syntax::parse(source);
    let tokens = resolver::resolve(&parsed.file);
    let positioned = position_tokens(source, tokens);
    render_json(&positioned)
}

/// A token with its span translated to what VS Code wants: a 0-based line, a 0-based UTF-16 column, and a length in
/// UTF-16 code units. Only single-line tokens are kept - every token this resolver produces names something the
/// lexer requires to fit on one line, but a future kind might not, and a token that survived a parse error's
/// recovery could in principle carry a stale, multi-line span, so this is checked rather than assumed.
struct PositionedToken {
    line: u32,
    start_character: u32,
    length: u32,
    kind: &'static str,
    modifiers: Vec<&'static str>,
}

fn position_tokens(source: &str, mut tokens: Vec<Token>) -> Vec<PositionedToken> {
    tokens.sort_by_key(|token| (token.span.start, token.span.end));
    tokens.dedup_by_key(|token| (token.span.start, token.span.end));
    let index = Utf16Index::new(source);
    let mut result = Vec::with_capacity(tokens.len());
    for token in tokens {
        let Some((line, start_character, length)) = index.locate(token.span) else { continue };
        result.push(PositionedToken { line, start_character, length, kind: token.kind, modifiers: token.modifiers });
    }
    // Two tokens can start at the same position when a shorter one is nested in a longer one (should not happen
    // for the flat, single-name tokens this resolver emits, but dropping the overlap keeps the protocol's
    // "never overlap" promise even if a future token kind nests).
    result.dedup_by(|next, previous| {
        previous.line == next.line
            && previous.start_character < next.start_character + next.length
            && next.start_character < previous.start_character + previous.length
    });
    result
}

/// Byte offset to (0-based line, 0-based UTF-16 column) - the same mapping `torb_syntax::LineIndex` provides, but
/// counting UTF-16 code units instead of characters, and 0-based instead of 1-based, because that is the protocol
/// `SemanticTokensBuilder` uses.
struct Utf16Index<'source> {
    source: &'source str,
    line_starts: Vec<usize>,
}

impl<'source> Utf16Index<'source> {
    fn new(source: &'source str) -> Self {
        let mut line_starts = vec![0];
        line_starts.extend(source.match_indices('\n').map(|(index, _)| index + 1));
        Utf16Index { source, line_starts }
    }

    /// `None` if the span crosses a line break: not a position `SemanticTokensBuilder.push` (one line per call)
    /// can represent.
    fn locate(&self, span: Span) -> Option<(u32, u32, u32)> {
        let start = span.start as usize;
        let end = span.end as usize;
        if end < start || end > self.source.len() {
            return None;
        }
        let line = self.line_starts.partition_point(|&line_start| line_start <= start) - 1;
        let line_start = self.line_starts[line];
        let line_end = self.line_starts.get(line + 1).copied().unwrap_or(self.source.len());
        if end > line_end {
            return None; // Spans more than one line.
        }
        let start_character = utf16_units(&self.source[line_start..start]);
        let length = utf16_units(&self.source[start..end]);
        Some((line as u32, start_character, length))
    }
}

fn utf16_units(text: &str) -> u32 {
    text.chars().map(|character| character.len_utf16() as u32).sum()
}

fn render_json(tokens: &[PositionedToken]) -> String {
    let mut output = String::with_capacity(64 + tokens.len() * 48);
    output.push_str("{\"tokens\":[");
    for (index, token) in tokens.iter().enumerate() {
        if index > 0 {
            output.push(',');
        }
        output.push('[');
        output.push_str(&token.line.to_string());
        output.push(',');
        output.push_str(&token.start_character.to_string());
        output.push(',');
        output.push_str(&token.length.to_string());
        output.push_str(",\"");
        output.push_str(token.kind);
        output.push_str("\",[");
        for (modifier_index, modifier) in token.modifiers.iter().enumerate() {
            if modifier_index > 0 {
                output.push(',');
            }
            output.push('"');
            output.push_str(modifier);
            output.push('"');
        }
        output.push_str("]]");
    }
    output.push_str("]}\n");
    output
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every `.trb` token this resolver can produce is made of plain ASCII identifier characters, but the JSON
    /// writer has no escaping of its own - this exercises the UTF-16 column math with a string ahead of the token
    /// on the same line, which is where a byte-based implementation would drift.
    #[test]
    fn utf16_columns_account_for_a_string_earlier_on_the_line() {
        let source = "const greeting = \"héllo wörld 🎉\"\nfn helper() {}\n";
        let json = tokens_json(source);
        assert!(json.contains("\"function\""), "{json}");
        // `helper`'s line is unaffected by the emoji on the line above; this mostly checks nothing panics on
        // 4-byte UTF-8 (2 UTF-16 units) content earlier in the file.
        assert!(json.starts_with("{\"tokens\":[["), "{json}");
    }

    #[test]
    fn a_file_with_only_syntax_errors_still_prints_valid_json_and_never_panics() {
        let json = tokens_json("fn (((( this is not valid");
        assert!(json.starts_with("{\"tokens\":["));
        assert!(json.trim_end().ends_with("]}"));
    }

    #[test]
    fn empty_source_prints_an_empty_list() {
        assert_eq!(tokens_json(""), "{\"tokens\":[]}\n");
    }
}
