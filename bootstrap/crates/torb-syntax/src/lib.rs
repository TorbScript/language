//! Lexer, syntax tree and parser of TorbScript.
//!
//! ```
//! let parsed = torb_syntax::parse("print \"Hello\"");
//! assert!(parsed.diagnostics.is_empty());
//! ```

pub mod ast;
pub mod diagnostic;
pub mod dump;
pub mod lexer;
pub mod parser;
pub mod span;
pub mod token;

pub use diagnostic::Diagnostic;
pub use span::{LineIndex, Span};

/// The result of parsing one file. The tree is always there, even if there are diagnostics.
#[derive(Debug)]
pub struct Parsed {
    pub file: ast::File,
    pub diagnostics: Vec<Diagnostic>,
}

pub fn parse(source: &str) -> Parsed {
    let lexed = lexer::lex(source);
    let mut parser = parser::Parser::new(source, lexed.tokens, lexed.docs);
    let file = parser.parse_file();
    let mut diagnostics = lexed.diagnostics;
    diagnostics.extend(parser.finish());
    diagnostics.sort_by_key(|diagnostic| diagnostic.span.start);
    Parsed { file, diagnostics }
}
