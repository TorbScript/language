//! Recursive descent parser. It always produces a tree; problems become diagnostics and `Error` nodes.

mod declarations;
mod expressions;
mod patterns;
mod types;

use crate::ast::*;
use crate::diagnostic::Diagnostic;
use crate::span::Span;
use crate::token::{DocComment, Keyword, Token, TokenKind};

pub struct Parser<'source> {
    source: &'source str,
    tokens: Vec<Token>,
    docs: Vec<DocComment>,
    /// The first doc comment that was not looked at yet
    next_doc: usize,
    position: usize,
    diagnostics: Vec<Diagnostic>,
    /// `false` in the heads of `if`/`for`/`while`/`match` and in the arguments of a command call: there a `{` that
    /// follows an expression belongs to the statement, not to the expression.
    trailing_closures: bool,
}

/// A position to rewind to after a failed speculation.
#[derive(Clone, Copy)]
struct Checkpoint {
    position: usize,
    next_doc: usize,
    diagnostics: usize,
}

impl<'source> Parser<'source> {
    pub fn new(source: &'source str, tokens: Vec<Token>, docs: Vec<DocComment>) -> Self {
        debug_assert!(matches!(tokens.last(), Some(Token { kind: TokenKind::EndOfFile, .. })));
        Parser { source, tokens, docs, next_doc: 0, position: 0, diagnostics: Vec::new(), trailing_closures: true }
    }

    /// The doc comment directly in front of the current token (nothing but whitespace in between).
    fn take_doc(&mut self) -> Option<String> {
        let start = self.span().start;
        let mut found = None;
        while self.next_doc < self.docs.len() && self.docs[self.next_doc].span.end <= start {
            found = Some(self.next_doc);
            self.next_doc += 1;
        }
        let doc = &self.docs[found?];
        self.source[doc.span.end as usize..start as usize].trim().is_empty().then(|| doc.text.clone())
    }

    pub fn finish(self) -> Vec<Diagnostic> {
        self.diagnostics
    }

    pub fn parse_file(&mut self) -> File {
        let statements = self.statements_until(|kind| *kind == TokenKind::EndOfFile);
        File { statements }
    }

    // --- Cursor -------------------------------------------------------------------------------------------------

    fn token(&self) -> &Token {
        &self.tokens[self.position]
    }

    fn kind(&self) -> &TokenKind {
        &self.token().kind
    }

    fn kind_at(&self, offset: usize) -> &TokenKind {
        &self.tokens[(self.position + offset).min(self.tokens.len() - 1)].kind
    }

    fn span(&self) -> Span {
        self.token().span
    }

    fn previous_span(&self) -> Span {
        self.tokens[self.position.saturating_sub(1)].span
    }

    fn text(&self, span: Span) -> &'source str {
        &self.source[span.range()]
    }

    fn at(&self, kind: TokenKind) -> bool {
        *self.kind() == kind
    }

    fn at_keyword(&self, keyword: Keyword) -> bool {
        *self.kind() == TokenKind::Keyword(keyword)
    }

    /// `from`, `as` and `by` are contextual keywords.
    fn at_word(&self, word: &str) -> bool {
        self.at(TokenKind::Identifier) && self.text(self.span()) == word
    }

    fn bump(&mut self) -> Token {
        let token = self.token().clone();
        if self.position < self.tokens.len() - 1 {
            self.position += 1;
        }
        token
    }

    fn eat(&mut self, kind: TokenKind) -> bool {
        let found = self.at(kind);
        if found {
            self.bump();
        }
        found
    }

    fn eat_keyword(&mut self, keyword: Keyword) -> bool {
        self.eat(TokenKind::Keyword(keyword))
    }

    fn expect(&mut self, kind: TokenKind) -> bool {
        if self.eat(kind.clone()) {
            return true;
        }
        self.error_here(format!("Expected {}, found {}", kind.describe(), self.kind().describe()));
        false
    }

    fn skip_newlines(&mut self) {
        while self.eat(TokenKind::Newline) {}
    }

    fn error_here(&mut self, message: impl Into<String>) {
        let span = self.span();
        self.error(message, span);
    }

    fn error(&mut self, message: impl Into<String>, span: Span) {
        // One problem per position is enough, the rest are usually consequences
        if self.diagnostics.last().is_some_and(|last| last.span == span) {
            return;
        }
        self.diagnostics.push(Diagnostic::error(message, span));
    }

    /// The same, with the line to write instead. A parse error rarely has one; a form that was removed always does.
    fn error_with_note(&mut self, message: impl Into<String>, note: impl Into<String>, span: Span) {
        if self.diagnostics.last().is_some_and(|last| last.span == span) {
            return;
        }
        self.diagnostics.push(Diagnostic::error(message, span).with_note(note));
    }

    fn checkpoint(&self) -> Checkpoint {
        Checkpoint { position: self.position, next_doc: self.next_doc, diagnostics: self.diagnostics.len() }
    }

    fn rewind(&mut self, checkpoint: Checkpoint) {
        self.position = checkpoint.position;
        self.next_doc = checkpoint.next_doc;
        self.diagnostics.truncate(checkpoint.diagnostics);
    }

    fn failed_since(&self, checkpoint: Checkpoint) -> bool {
        self.diagnostics.len() > checkpoint.diagnostics
    }

    fn with_trailing_closures<Output>(&mut self, allowed: bool, parse: impl FnOnce(&mut Self) -> Output) -> Output {
        let before = std::mem::replace(&mut self.trailing_closures, allowed);
        let output = parse(self);
        self.trailing_closures = before;
        output
    }

    fn name(&mut self) -> Name {
        let span = self.span();
        if self.eat(TokenKind::Identifier) {
            return Name { text: self.text(span).to_string(), span };
        }
        if let TokenKind::Keyword(_) = self.kind() {
            self.error_here(format!("`{}` is a keyword and cannot be used as a name here", self.text(span)));
            self.bump();
        } else {
            self.error_here(format!("Expected a name, found {}", self.kind().describe()));
        }
        Name { text: String::new(), span }
    }

    /// After a `.` and as a label, keywords are ordinary names.
    fn name_or_keyword(&mut self) -> Name {
        let span = self.span();
        if let TokenKind::Keyword(_) = self.kind() {
            self.bump();
            return Name { text: self.text(span).to_string(), span };
        }
        self.name()
    }

    /// `separator`-separated items up to `close`. Trailing separators are fine.
    fn comma_separated<Item>(&mut self, close: TokenKind, mut item: impl FnMut(&mut Self) -> Item) -> Vec<Item> {
        let mut items = Vec::new();
        self.skip_newlines();
        while !self.at(close.clone()) && !self.at(TokenKind::EndOfFile) {
            let before = self.position;
            items.push(item(self));
            self.skip_newlines();
            if !self.eat(TokenKind::Comma) {
                break;
            }
            self.skip_newlines();
            if self.position == before {
                self.bump();
            }
        }
        self.expect(close);
        items
    }

    // --- Statements ---------------------------------------------------------------------------------------------

    fn block(&mut self) -> Block {
        let start = self.span();
        if !self.expect(TokenKind::BraceOpen) {
            return Block { statements: Vec::new(), span: start };
        }
        self.block_after_brace(start)
    }

    fn block_after_brace(&mut self, start: Span) -> Block {
        let statements = self.with_trailing_closures(true, |parser| parser.statements_until(|kind| *kind == TokenKind::BraceClose));
        self.expect(TokenKind::BraceClose);
        Block { statements, span: start.to(self.previous_span()) }
    }

    fn statements_until(&mut self, is_end: impl Fn(&TokenKind) -> bool) -> Vec<Statement> {
        let mut statements = Vec::new();
        loop {
            self.skip_newlines();
            if is_end(self.kind()) || self.at(TokenKind::EndOfFile) {
                return statements;
            }
            let before = self.position;
            statements.push(self.statement());
            if !matches!(self.kind(), TokenKind::Newline | TokenKind::BraceClose | TokenKind::EndOfFile) {
                self.error_here(format!("Expected the end of the statement, found {}", self.kind().describe()));
                self.recover_to_line_end();
            }
            if self.position == before {
                self.bump();
            }
        }
    }

    /// Skips the rest of a broken statement. Balanced braces are skipped as a whole.
    fn recover_to_line_end(&mut self) {
        let mut depth = 0;
        loop {
            match self.kind() {
                TokenKind::EndOfFile => return,
                TokenKind::Newline if depth == 0 => return,
                TokenKind::BraceClose if depth == 0 => return,
                TokenKind::BraceOpen => depth += 1,
                TokenKind::BraceClose => depth -= 1,
                _ => {}
            }
            self.bump();
        }
    }

    fn statement(&mut self) -> Statement {
        let start = self.span();
        let kind = match self.kind() {
            TokenKind::Keyword(
                Keyword::Public
                | Keyword::Private
                | Keyword::Native
                | Keyword::Shared
                | Keyword::Fn
                | Keyword::Type
                | Keyword::Trait
                | Keyword::Extend
                | Keyword::Use
                | Keyword::Foreign,
            ) => StatementKind::Declaration(self.declaration()),
            TokenKind::Keyword(Keyword::Const | Keyword::Var) => StatementKind::Binding(self.binding()),
            TokenKind::Keyword(Keyword::For) => self.for_statement(),
            TokenKind::Keyword(Keyword::While) => {
                self.bump();
                let condition = self.condition();
                StatementKind::While { condition, body: self.block() }
            }
            TokenKind::Keyword(Keyword::Loop) => {
                self.bump();
                StatementKind::Loop { body: self.block() }
            }
            TokenKind::Keyword(Keyword::Return) => {
                self.bump();
                let has_value = !matches!(self.kind(), TokenKind::Newline | TokenKind::BraceClose | TokenKind::EndOfFile);
                StatementKind::Return(has_value.then(|| self.command_expression()))
            }
            TokenKind::Keyword(Keyword::Break) => {
                self.bump();
                StatementKind::Break
            }
            TokenKind::Keyword(Keyword::Continue) => {
                self.bump();
                StatementKind::Continue
            }
            _ => {
                let target = self.command_expression();
                if self.eat(TokenKind::Equal) {
                    StatementKind::Assignment { target, value: self.command_expression() }
                } else {
                    StatementKind::Expression(target)
                }
            }
        };
        Statement { kind, span: start.to(self.previous_span()) }
    }

    fn binding(&mut self) -> Binding {
        let doc = self.take_doc();
        let is_var = self.at_keyword(Keyword::Var);
        self.bump();
        let pattern = self.pattern();
        let annotation = self.eat(TokenKind::Colon).then(|| self.type_reference());
        if !self.at(TokenKind::Equal) {
            self.error_here("A binding needs a value: there are no uninitialized bindings and no default values");
            let value = Expression { kind: ExpressionKind::Error, span: self.span() };
            return Binding { doc, is_var, pattern, annotation, value };
        }
        self.bump();
        Binding { doc, is_var, pattern, annotation, value: self.command_expression() }
    }

    fn for_statement(&mut self) -> StatementKind {
        self.bump();
        let pattern = self.pattern();
        self.expect(TokenKind::Keyword(Keyword::In));
        let iterable = self.head_expression();
        StatementKind::For { pattern, iterable, body: self.block() }
    }

    /// The head of `if` and `while`.
    fn condition(&mut self) -> Condition {
        if matches!(self.kind(), TokenKind::Keyword(Keyword::Const | Keyword::Var)) {
            let is_var = self.at_keyword(Keyword::Var);
            self.bump();
            let pattern = self.pattern();
            self.expect(TokenKind::Equal);
            return Condition::Binding { is_var, pattern, value: Box::new(self.head_expression()) };
        }
        Condition::Expression(Box::new(self.head_expression()))
    }

    /// An expression in front of a `{` that belongs to the statement.
    fn head_expression(&mut self) -> Expression {
        self.with_trailing_closures(false, Self::expression)
    }
}
