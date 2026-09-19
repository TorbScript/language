//! Source text to tokens. The lexer never fails: what it does not understand becomes a diagnostic and is skipped.

use crate::diagnostic::Diagnostic;
use crate::span::Span;
use crate::token::{DocComment, Keyword, TextPart, Token, TokenKind};

pub struct Lexed {
    pub tokens: Vec<Token>,
    pub docs: Vec<DocComment>,
    pub diagnostics: Vec<Diagnostic>,
}

pub fn lex(source: &str) -> Lexed {
    lex_range(source, Span::new(0, source.len()))
}

/// Lexes a part of a file (used for the expressions inside of string literals). Spans stay relative to the file.
pub fn lex_range(source: &str, range: Span) -> Lexed {
    let mut lexer = Lexer {
        source,
        position: range.start as usize,
        end: range.end as usize,
        tokens: Vec::new(),
        docs: Vec::new(),
        diagnostics: Vec::new(),
    };
    lexer.run();
    let tokens = remove_insignificant_newlines(lexer.tokens);
    Lexed { tokens, docs: lexer.docs, diagnostics: lexer.diagnostics }
}

struct Lexer<'source> {
    source: &'source str,
    position: usize,
    end: usize,
    tokens: Vec<Token>,
    docs: Vec<DocComment>,
    diagnostics: Vec<Diagnostic>,
}

/// The text of a doc comment: without `/**`, `*/` and the ` * ` in front of every line.
fn doc_text(comment: &str) -> String {
    let inner = &comment[3..comment.len() - 2];
    let lines: Vec<&str> = inner
        .split('\n')
        .map(|line| {
            let line = line.trim_end_matches('\r');
            match line.trim_start().strip_prefix('*') {
                Some(rest) => rest.strip_prefix(' ').unwrap_or(rest),
                None => line,
            }
        })
        .collect();
    lines.join("\n").trim().to_string()
}

impl Lexer<'_> {
    fn run(&mut self) {
        while let Some(character) = self.peek() {
            let start = self.position;
            match character {
                '\n' => {
                    self.position += 1;
                    self.push(TokenKind::Newline, start);
                }
                _ if character.is_whitespace() => self.position += character.len_utf8(),
                '/' if self.peek_at(1) == Some('/') => self.skip_line_comment(),
                '/' if self.peek_at(1) == Some('*') => self.skip_block_comment(),
                '"' => self.text(start, false),
                'r' if self.peek_at(1) == Some('"') => {
                    self.position += 1;
                    self.text(start, true);
                }
                '\'' => self.character(start),
                _ if character.is_ascii_digit() => self.number(start),
                _ if character == '_' || character.is_alphabetic() => self.word(start),
                _ => self.punctuation(start, character),
            }
        }
        let end = self.end;
        self.push(TokenKind::EndOfFile, end);
    }

    fn peek(&self) -> Option<char> {
        self.peek_at(0)
    }

    fn peek_at(&self, offset: usize) -> Option<char> {
        self.source[self.position..self.end].chars().nth(offset)
    }

    fn rest(&self) -> &str {
        &self.source[self.position..self.end]
    }

    fn push(&mut self, kind: TokenKind, start: usize) {
        self.tokens.push(Token { kind, span: Span::new(start, self.position) });
    }

    fn error(&mut self, message: impl Into<String>, start: usize) {
        self.diagnostics.push(Diagnostic::error(message, Span::new(start, self.position.max(start + 1))));
    }

    fn skip_line_comment(&mut self) {
        let length = self.rest().find('\n').unwrap_or(self.rest().len());
        self.position += length;
    }

    /// Block comments do not nest: the first `*/` ends the comment.
    fn skip_block_comment(&mut self) {
        let start = self.position;
        match self.rest()[2..].find("*/") {
            Some(length) => {
                self.position += length + 4;
                let comment = &self.source[start..self.position];
                if comment.starts_with("/**") && comment.len() > 4 {
                    self.docs.push(DocComment { text: doc_text(comment), span: Span::new(start, self.position) });
                }
            }
            None => {
                self.position = self.end;
                self.error("This comment is never closed", start);
            }
        }
    }

    fn word(&mut self, start: usize) {
        while let Some(character) = self.peek() {
            if character == '_' || character.is_alphanumeric() {
                self.position += character.len_utf8();
            } else {
                break;
            }
        }
        let kind = match Keyword::from_text(&self.source[start..self.position]) {
            Some(keyword) => TokenKind::Keyword(keyword),
            None => TokenKind::Identifier,
        };
        self.push(kind, start);
    }

    fn number(&mut self, start: usize) {
        let after_dot = matches!(self.tokens.last(), Some(Token { kind: TokenKind::Dot, .. }));
        if self.rest().starts_with("0x") || self.rest().starts_with("0b") {
            self.position += 2;
            self.digits(|character| character.is_ascii_hexdigit());
            return self.push(TokenKind::Integer, start);
        }
        self.digits(|character| character.is_ascii_digit());
        let mut kind = TokenKind::Integer;
        // `tuple.0.1` and `1..5` are not floats, `16.megabytes()` is a call on an integer
        let has_fraction = !after_dot && self.peek() == Some('.') && self.peek_at(1).is_some_and(|next| next.is_ascii_digit());
        if has_fraction {
            self.position += 1;
            self.digits(|character| character.is_ascii_digit());
            kind = TokenKind::Float;
        }
        if !after_dot && matches!(self.peek(), Some('e' | 'E')) {
            let sign = usize::from(matches!(self.peek_at(1), Some('+' | '-')));
            if self.peek_at(1 + sign).is_some_and(|next| next.is_ascii_digit()) {
                self.position += 1 + sign;
                self.digits(|character| character.is_ascii_digit());
                kind = TokenKind::Float;
            }
        }
        self.push(kind, start);
    }

    fn digits(&mut self, accepts: impl Fn(char) -> bool) {
        while let Some(character) = self.peek() {
            if character == '_' || accepts(character) {
                self.position += 1;
            } else {
                break;
            }
        }
    }

    fn character(&mut self, start: usize) {
        self.position += 1;
        let value = match self.peek() {
            Some('\\') => {
                self.position += 1;
                self.escape()
            }
            Some(character) if character != '\'' && character != '\n' => {
                self.position += character.len_utf8();
                Some(character)
            }
            _ => None,
        };
        if self.peek() == Some('\'') {
            self.position += 1;
        } else {
            return self.error("A character literal contains exactly one character and ends with `'`", start);
        }
        match value {
            Some(value) => self.push(TokenKind::Char(value), start),
            None => self.error("This character literal is empty", start),
        }
    }

    /// Reads what follows a `\`.
    fn escape(&mut self) -> Option<char> {
        let start = self.position - 1;
        let character = self.peek()?;
        self.position += character.len_utf8();
        Some(match character {
            'n' => '\n',
            'r' => '\r',
            't' => '\t',
            '0' => '\0',
            '\\' | '"' | '\'' | '{' | '}' => character,
            'u' if self.peek() == Some('{') => {
                let digits_end = self.rest().find('}')?;
                let code = u32::from_str_radix(&self.rest()[1..digits_end], 16).ok().and_then(char::from_u32);
                self.position += digits_end + 1;
                match code {
                    Some(code) => code,
                    None => {
                        self.error("This is not a Unicode scalar value", start);
                        '\u{FFFD}'
                    }
                }
            }
            _ => {
                self.error(format!("Unknown escape sequence `\\{character}`"), start);
                character
            }
        })
    }

    /// `"..."`, `"""..."""`, and their raw variants `r"..."`, `r"""..."""`. The position is at the first quote.
    fn text(&mut self, start: usize, raw: bool) {
        let quotes = if self.rest().starts_with("\"\"\"") { 3 } else { 1 };
        self.position += quotes;
        // A triple-quoted string is dedented (see `dedent_line`) only if it really spans more than one line: a
        // one-line `"""text"""` is left exactly as written.
        let multiline = quotes == 3 && self.has_multiple_lines(raw);
        if multiline {
            self.skip_leading_line_break();
        }
        let mut parts = Vec::new();
        let mut literal = String::new();
        let mut reference: Option<String> = None;
        let mut at_line_start = multiline;
        loop {
            let Some(character) = self.peek() else {
                return self.error("This string is never closed", start);
            };
            if character == '"' && self.rest().starts_with(&"\"\"\""[..quotes]) {
                self.position += quotes;
                break;
            }
            if character == '\n' && quotes == 1 {
                return self.error("This string is never closed. Multi-line strings use `\"\"\"`", start);
            }
            if at_line_start {
                at_line_start = false;
                self.dedent_line(&mut reference);
                continue;
            }
            self.position += character.len_utf8();
            match character {
                '\\' if !raw => literal.extend(self.escape()),
                '{' if !raw => {
                    let expression_start = self.position;
                    if !self.skip_interpolation() {
                        return self.error("This `{` inside of a string is never closed. A literal brace is written `\\{`", start);
                    }
                    if !literal.is_empty() {
                        parts.push(TextPart::Literal(std::mem::take(&mut literal)));
                    }
                    parts.push(TextPart::Expression(Span::new(expression_start, self.position - 1)));
                }
                '\n' if multiline => {
                    literal.push(character);
                    at_line_start = true;
                }
                _ => literal.push(character),
            }
        }
        if !literal.is_empty() || parts.is_empty() {
            parts.push(TextPart::Literal(literal));
        }
        self.push(TokenKind::Text(parts), start);
    }

    /// Whether a `"""`/`r"""` string starting right after the opening quotes spans more than one physical line,
    /// i.e. contains a raw line break before the closing `"""`. An interpolation cannot contain one, so this is
    /// enough to tell a one-line `"""text"""` apart from a real block. Leaves `self.position` unchanged.
    fn has_multiple_lines(&mut self, raw: bool) -> bool {
        let saved = self.position;
        let mut found = false;
        loop {
            match self.peek() {
                None => break,
                Some('"') if self.rest().starts_with("\"\"\"") => break,
                Some('\n') => {
                    found = true;
                    break;
                }
                Some('\\') if !raw => {
                    self.position += 1;
                    self.skip_raw_escape();
                }
                Some('{') if !raw => {
                    self.position += 1;
                    if !self.skip_interpolation() {
                        break;
                    }
                }
                Some(character) => self.position += character.len_utf8(),
            }
        }
        self.position = saved;
        found
    }

    /// Advances past an escape sequence without decoding it. Used only to look for a raw line break before the
    /// real, diagnostic-producing `escape` runs (which happens later, once dedenting is settled).
    fn skip_raw_escape(&mut self) {
        let Some(character) = self.peek() else { return };
        self.position += character.len_utf8();
        if character == 'u' && self.peek() == Some('{') {
            if let Some(length) = self.rest().find('}') {
                self.position += length + 1;
            }
        }
    }

    /// Rule 1 of multi-line strings: a line break directly after the opening `"""` (optionally after trailing
    /// spaces) is not part of the string.
    fn skip_leading_line_break(&mut self) {
        let rest = self.rest();
        let indent_length = rest.find(|character: char| character != ' ' && character != '\t').unwrap_or(rest.len());
        let after = &rest[indent_length..];
        if after.starts_with("\r\n") {
            self.position += indent_length + 2;
        } else if after.starts_with('\n') {
            self.position += indent_length + 1;
        }
    }

    /// Called at the start of a physical line inside of a multi-line string. Strips this line's share of the
    /// indentation of the first line that has content (the reference), or the whole line if it has none itself (a
    /// blank line, or the line of a lone closing `"""`). A line indented less than the reference, or whose
    /// indentation is not the reference followed by more, is a lexer error and is left exactly as written.
    fn dedent_line(&mut self, reference: &mut Option<String>) {
        let rest = self.rest();
        let indent_length = rest.find(|character: char| character != ' ' && character != '\t').unwrap_or(rest.len());
        let after = &rest[indent_length..];
        let has_no_content = after.starts_with('\n') || after.starts_with('\r') || after.starts_with("\"\"\"");
        if has_no_content {
            self.position += indent_length;
            return;
        }
        match reference {
            None => {
                *reference = Some(rest[..indent_length].to_string());
                self.position += indent_length;
            }
            Some(reference) => {
                if rest.starts_with(reference.as_str()) {
                    self.position += reference.len();
                } else {
                    let indent_start = self.position;
                    self.position += indent_length;
                    self.error("This line is indented less than the first line of the string", indent_start);
                    self.position = indent_start;
                }
            }
        }
    }

    /// Moves behind the `}` that closes an interpolation. Braces and strings inside of it may nest.
    fn skip_interpolation(&mut self) -> bool {
        let mut depth = 1;
        while let Some(character) = self.peek() {
            self.position += character.len_utf8();
            match character {
                '{' => depth += 1,
                '}' => {
                    depth -= 1;
                    if depth == 0 {
                        return true;
                    }
                }
                '"' => {
                    while let Some(inner) = self.peek() {
                        self.position += inner.len_utf8();
                        match inner {
                            '\\' => self.position += self.peek().map_or(0, char::len_utf8),
                            '"' => break,
                            _ => {}
                        }
                    }
                }
                '\n' => return false,
                _ => {}
            }
        }
        false
    }

    fn punctuation(&mut self, start: usize, character: char) {
        use TokenKind::*;
        let table: [(&str, TokenKind); 35] = [
            ("...", Ellipsis),
            ("..=", DotDotEqual),
            ("..", DotDot),
            ("?.", QuestionDot),
            ("??", QuestionQuestion),
            ("=>", Arrow),
            ("==", EqualEqual),
            ("!=", BangEqual),
            ("<=", LessEqual),
            (">=", GreaterEqual),
            ("&&", AndAnd),
            ("||", OrOr),
            (".", Dot),
            ("?", Question),
            ("=", Equal),
            ("!", Bang),
            ("<", Less),
            (">", Greater),
            ("+", Plus),
            ("-", Minus),
            ("*", Star),
            ("/", Slash),
            ("%", Percent),
            (",", Comma),
            (":", Colon),
            ("(", ParenOpen),
            (")", ParenClose),
            ("[", BracketOpen),
            ("]", BracketClose),
            ("{", BraceOpen),
            ("}", BraceClose),
            // Not part of the language, but they get a better message than "unexpected character"
            (";", Newline),
            ("|", Pipe),
            ("&", AndAnd),
            ("^", Star),
        ];
        let Some((text, kind)) = table.iter().find(|(text, _)| self.rest().starts_with(text)) else {
            self.position += character.len_utf8();
            return self.error(format!("Unexpected character `{character}`"), start);
        };
        self.position += text.len();
        match *text {
            ";" => self.error("There are no semicolons. A statement ends at the end of its line", start),
            "&" | "^" => self.error(format!("There is no `{text}` operator"), start),
            _ => self.push(kind.clone(), start),
        }
    }
}

/// Line breaks only end a statement if they are not inside of `(`/`[`, not after a token that needs a right side, and
/// not before a token that needs a left side.
fn remove_insignificant_newlines(tokens: Vec<Token>) -> Vec<Token> {
    let mut result: Vec<Token> = Vec::with_capacity(tokens.len());
    // The flag marks the body of a `match`: there a line that starts with `.` is an arm (`.Circle(r) => ...`)
    let mut brackets: Vec<(TokenKind, bool)> = Vec::new();
    let mut pending_match: Option<usize> = None;
    let mut index = 0;
    while index < tokens.len() {
        let token = &tokens[index];
        match token.kind {
            TokenKind::Keyword(Keyword::Match) => {
                // `text.match(...)` and `match: value` are names
                let is_name = matches!(result.last(), Some(Token { kind: TokenKind::Dot | TokenKind::QuestionDot, .. }))
                    || tokens.get(index + 1).is_some_and(|next| next.kind == TokenKind::Colon);
                if !is_name {
                    pending_match = Some(brackets.len());
                }
            }
            TokenKind::BraceOpen => {
                let is_match_body = pending_match == Some(brackets.len());
                if is_match_body {
                    pending_match = None;
                }
                brackets.push((TokenKind::BraceOpen, is_match_body));
            }
            TokenKind::ParenOpen | TokenKind::BracketOpen => brackets.push((token.kind.clone(), false)),
            TokenKind::ParenClose | TokenKind::BracketClose | TokenKind::BraceClose => {
                brackets.pop();
            }
            TokenKind::Newline => {
                let mut next = index;
                while tokens[next].kind == TokenKind::Newline {
                    next += 1;
                }
                let inside_brackets = matches!(brackets.last(), Some((TokenKind::ParenOpen | TokenKind::BracketOpen, _)));
                let starts_arm = matches!(brackets.last(), Some((_, true))) && tokens[next].kind == TokenKind::Dot;
                let after =
                    result.last().is_none_or(|previous| previous.kind.continues_line_after() || previous.kind == TokenKind::Newline);
                let before = tokens[next].kind.continues_line_before() && !starts_arm;
                index = next;
                if !(inside_brackets || after || before) {
                    result.push(token.clone());
                }
                continue;
            }
            _ => {}
        }
        result.push(token.clone());
        index += 1;
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    fn kinds(source: &str) -> Vec<TokenKind> {
        let lexed = lex(source);
        assert!(lexed.diagnostics.is_empty(), "{:?}", lexed.diagnostics);
        lexed.tokens.into_iter().map(|token| token.kind).collect()
    }

    #[test]
    fn numbers_ranges_and_tuple_indices() {
        use TokenKind::*;
        assert_eq!(kinds("1..5"), [Integer, DotDot, Integer, EndOfFile]);
        assert_eq!(kinds("1.5e3"), [Float, EndOfFile]);
        assert_eq!(kinds("pair.0.1"), [Identifier, Dot, Integer, Dot, Integer, EndOfFile]);
        assert_eq!(kinds("16.megabytes"), [Integer, Dot, Identifier, EndOfFile]);
        assert_eq!(kinds("0xFF 1_000"), [Integer, Integer, EndOfFile]);
    }

    #[test]
    fn interpolation_may_contain_strings() {
        let tokens = kinds(r#""a: {map["a"]} \{literal}""#);
        let TokenKind::Text(parts) = &tokens[0] else { panic!() };
        assert_eq!(parts.len(), 3);
        assert_eq!(parts[0], TextPart::Literal("a: ".into()));
        assert_eq!(parts[2], TextPart::Literal(" {literal}".into()));
    }

    #[test]
    fn newlines() {
        use TokenKind::*;
        assert_eq!(kinds("a\nb"), [Identifier, Newline, Identifier, EndOfFile]);
        assert_eq!(kinds("a +\nb"), [Identifier, Plus, Identifier, EndOfFile]);
        assert_eq!(kinds("a\n  .b"), [Identifier, Dot, Identifier, EndOfFile]);
        assert_eq!(kinds("f(\na,\nb\n)"), [Identifier, ParenOpen, Identifier, Comma, Identifier, ParenClose, EndOfFile]);
        assert_eq!(kinds("/* a /* not nested */ x // y"), [Identifier, EndOfFile]);
    }

    /// The literal text of a source that is a single `Text` token with a single `TextPart::Literal`.
    fn literal_of(source: &str) -> String {
        let lexed = lex(source);
        assert!(lexed.diagnostics.is_empty(), "{:?}", lexed.diagnostics);
        let TokenKind::Text(parts) = &lexed.tokens[0].kind else { panic!("not a string: {:?}", lexed.tokens) };
        let [TextPart::Literal(text)] = &parts[..] else { panic!("not one literal part: {parts:?}") };
        text.clone()
    }

    #[test]
    fn multiline_strings_drop_the_leading_line_break() {
        assert_eq!(literal_of("\"\"\"\ncontent\n\"\"\""), "content\n");
        // A one-line triple-quoted string is unchanged, leading spaces included.
        assert_eq!(literal_of("\"\"\" content \"\"\""), " content ");
    }

    #[test]
    fn multiline_strings_are_dedented_by_their_first_line() {
        assert_eq!(literal_of("\"\"\"\n  first\n  second\n  \"\"\""), "first\nsecond\n");
        // More indentation than the reference is kept, and blank lines become empty regardless of their whitespace.
        assert_eq!(literal_of("\"\"\"\n  first\n    nested\n\n  \t \n  last\n  \"\"\""), "first\n  nested\n\n\nlast\n");
        // The closing `\"\"\"` need not be indented at all.
        assert_eq!(literal_of("\"\"\"\n  first\n  second\n\"\"\""), "first\nsecond\n");
    }

    #[test]
    fn multiline_raw_strings_are_dedented_too() {
        let lexed = lex("r\"\"\"\n  first\\n\n  {ignored}\n  \"\"\"");
        assert!(lexed.diagnostics.is_empty(), "{:?}", lexed.diagnostics);
        let TokenKind::Text(parts) = &lexed.tokens[0].kind else { panic!() };
        assert_eq!(parts, &[TextPart::Literal("first\\n\n{ignored}\n".into())]);
    }

    #[test]
    fn a_line_indented_less_than_the_reference_is_an_error() {
        let lexed = lex("\"\"\"\n  first\nsecond\n  \"\"\"");
        assert_eq!(lexed.diagnostics.len(), 1);
        assert_eq!(lexed.diagnostics[0].message, "This line is indented less than the first line of the string");
        // The string still lexes, with the offending line unchanged.
        let TokenKind::Text(parts) = &lexed.tokens[0].kind else { panic!() };
        assert_eq!(parts, &[TextPart::Literal("first\nsecond\n".into())]);
    }

    #[test]
    fn a_line_whose_indentation_is_not_the_reference_is_an_error() {
        // Tabs do not match a reference of spaces, even at the same or greater width.
        let lexed = lex("\"\"\"\n  first\n\tsecond\n  \"\"\"");
        assert_eq!(lexed.diagnostics.len(), 1);
        assert_eq!(lexed.diagnostics[0].message, "This line is indented less than the first line of the string");
    }

    #[test]
    fn interpolation_counts_as_content_and_is_never_dedented() {
        let lexed = lex("\"\"\"\n  a{1}\n  b{2}\n  \"\"\"");
        assert!(lexed.diagnostics.is_empty(), "{:?}", lexed.diagnostics);
        let TokenKind::Text(parts) = &lexed.tokens[0].kind else { panic!() };
        assert_eq!(parts[0], TextPart::Literal("a".into()));
        assert_eq!(parts[2], TextPart::Literal("\nb".into()));
    }
}
