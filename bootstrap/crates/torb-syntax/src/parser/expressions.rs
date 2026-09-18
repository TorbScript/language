use super::Parser;
use crate::ast::*;
use crate::lexer;
use crate::token::{Keyword, TextPart, TokenKind};

/// Binding power of binary operators, higher binds tighter. Comparisons and ranges do not chain.
fn binary_operator(kind: &TokenKind) -> Option<(u8, BinaryOperator)> {
    use BinaryOperator::*;
    Some(match kind {
        TokenKind::OrOr => (1, Or),
        TokenKind::AndAnd => (2, And),
        TokenKind::EqualEqual => (3, Equal),
        TokenKind::BangEqual => (3, NotEqual),
        TokenKind::Less => (3, Less),
        TokenKind::LessEqual => (3, LessOrEqual),
        TokenKind::Greater => (3, Greater),
        TokenKind::GreaterEqual => (3, GreaterOrEqual),
        TokenKind::QuestionQuestion => (4, Coalesce),
        TokenKind::Plus => (6, Add),
        TokenKind::Minus => (6, Subtract),
        TokenKind::Star => (7, Multiply),
        TokenKind::Slash => (7, Divide),
        TokenKind::Percent => (7, Remainder),
        _ => return None,
    })
}

fn expression_error(span: crate::span::Span) -> Expression {
    Expression { kind: ExpressionKind::Error, span }
}

const COMPARISON: u8 = 3;
const COALESCE: u8 = 4;
const RANGE: u8 = 5;

impl Parser<'_> {
    pub(super) fn expression(&mut self) -> Expression {
        self.binary_expression(0)
    }

    /// An expression in _command position_ (start of a statement, right of `=`, after `return` and `=>`):
    /// `print "Hello"`, `route "/health", to: "health"`, `const user = Email.parse text`.
    pub(super) fn command_expression(&mut self) -> Expression {
        let checkpoint = self.checkpoint();
        let start = self.span();
        if let Some(callee) = self.command_path() {
            if self.starts_command_argument() {
                return self.command_call(start, callee);
            }
        }
        self.rewind(checkpoint);
        self.expression()
    }

    /// `print`, `Email.parse`, `root.attribute`
    fn command_path(&mut self) -> Option<Expression> {
        if !matches!(self.kind(), TokenKind::Identifier | TokenKind::Keyword(Keyword::SelfValue | Keyword::SelfType)) {
            return None;
        }
        let start = self.span();
        let mut path = Expression { kind: ExpressionKind::Name(self.text(start).to_string()), span: start };
        self.bump();
        while self.at(TokenKind::Dot) && matches!(self.kind_at(1), TokenKind::Identifier | TokenKind::Keyword(_)) {
            self.bump();
            let name = self.name_or_keyword();
            path = Expression {
                kind: ExpressionKind::Member { target: Box::new(path), name, optional: false },
                span: start.to(self.previous_span()),
            };
        }
        Some(path)
    }

    /// What can follow the callee of a command call. `(`, `[`, `-` and `!` cannot: `f [1]` is always indexing and
    /// `f -1` is always a subtraction.
    fn starts_command_argument(&self) -> bool {
        match self.kind() {
            TokenKind::Identifier => !matches!(self.text(self.span()), "as" | "by" | "from") || *self.kind_at(1) == TokenKind::Colon,
            TokenKind::Integer | TokenKind::Float | TokenKind::Char(_) | TokenKind::Text(_) => true,
            TokenKind::Keyword(Keyword::True | Keyword::False | Keyword::SelfValue | Keyword::SelfType | Keyword::If | Keyword::Match) => {
                true
            }
            // A label that is a keyword: `move from: a, to: b`
            TokenKind::Keyword(_) => *self.kind_at(1) == TokenKind::Colon,
            _ => false,
        }
    }

    fn command_call(&mut self, start: crate::span::Span, callee: Expression) -> Expression {
        let mut arguments = self.with_trailing_closures(false, |parser| {
            let mut arguments = vec![parser.argument()];
            while parser.eat(TokenKind::Comma) {
                arguments.push(parser.argument());
            }
            arguments
        });
        self.check_argument_order(&arguments);
        // The `{` belongs to the command, like the `{` after the head of an `if`
        if self.trailing_closures && self.at(TokenKind::BraceOpen) {
            let closure = self.closure();
            arguments.push(Argument { label: None, is_spread: false, value: closure });
            if matches!(self.kind(), TokenKind::Dot | TokenKind::QuestionDot | TokenKind::BraceOpen) {
                self.error_here("The `{ ... }` in front of this belongs to the command call, like the `{` after the head of an `if`. To pass a closure to one of the arguments, call the command with parentheses: `print(list.map { ... })`");
                self.recover_to_line_end();
            }
        }
        Expression {
            kind: ExpressionKind::Call { callee: Box::new(callee), arguments, style: CallStyle::Command },
            span: start.to(self.previous_span()),
        }
    }

    /// Labeled arguments follow the positional ones. (A trailing closure is added after this check.)
    fn check_argument_order(&mut self, arguments: &[Argument]) {
        let Some(first_label) = arguments.iter().position(|argument| argument.label.is_some()) else { return };
        if let Some(positional) = arguments[first_label..].iter().find(|argument| argument.label.is_none()) {
            self.error(
                "Positional arguments come first, labeled arguments follow them: `connect(host, timeout: 10)`",
                positional.value.span,
            );
        }
    }

    fn argument(&mut self) -> Argument {
        let is_spread = self.eat(TokenKind::Ellipsis);
        let has_label = matches!(self.kind(), TokenKind::Identifier | TokenKind::Keyword(_)) && *self.kind_at(1) == TokenKind::Colon;
        let label = has_label.then(|| {
            let label = self.name_or_keyword();
            self.bump();
            label
        });
        Argument { label, is_spread, value: self.expression() }
    }

    fn binary_expression(&mut self, minimum: u8) -> Expression {
        let start = self.span();
        // `..end`: a range without a start
        let mut left = if matches!(self.kind(), TokenKind::DotDot | TokenKind::DotDotEqual) && minimum <= RANGE {
            self.range_expression(start, None)
        } else {
            self.unary_expression()
        };
        loop {
            if matches!(self.kind(), TokenKind::DotDot | TokenKind::DotDotEqual) && minimum <= RANGE {
                left = self.range_expression(start, Some(left));
                continue;
            }
            let Some((power, operator)) = binary_operator(self.kind()) else { break };
            if power < minimum {
                break;
            }
            self.bump();
            // `??` groups to the right, everything else to the left
            let right = self.binary_expression(if power == COALESCE { power } else { power + 1 });
            left = Expression {
                kind: ExpressionKind::Binary { operator, left: Box::new(left), right: Box::new(right) },
                span: start.to(self.previous_span()),
            };
            if power == COMPARISON && binary_operator(self.kind()).is_some_and(|(next, _)| next == COMPARISON) {
                self.error_here("Comparisons do not chain. Use `&&`: `a < b && b < c`");
            }
        }
        left
    }

    /// `0..10`, `0..=10`, `1..` (open end), `..10` (open start)
    fn range_expression(&mut self, start: crate::span::Span, left: Option<Expression>) -> Expression {
        let inclusive = self.at(TokenKind::DotDotEqual);
        self.bump();
        let has_end = !matches!(
            self.kind(),
            TokenKind::ParenClose
                | TokenKind::BracketClose
                | TokenKind::BraceClose
                | TokenKind::BraceOpen
                | TokenKind::Comma
                | TokenKind::Newline
                | TokenKind::EndOfFile
        );
        let end = has_end.then(|| Box::new(self.binary_expression(RANGE + 1)));
        Expression { kind: ExpressionKind::Range { start: left.map(Box::new), end, inclusive }, span: start.to(self.previous_span()) }
    }

    fn unary_expression(&mut self) -> Expression {
        let start = self.span();
        let operator = match self.kind() {
            TokenKind::Minus => UnaryOperator::Negate,
            TokenKind::Bang => UnaryOperator::Not,
            _ => return self.postfix_expression(),
        };
        self.bump();
        let operand = self.unary_expression();
        Expression { kind: ExpressionKind::Unary { operator, operand: Box::new(operand) }, span: start.to(self.previous_span()) }
    }

    fn postfix_expression(&mut self) -> Expression {
        let start = self.span();
        let mut expression = self.primary_expression();
        loop {
            let kind = match self.kind() {
                TokenKind::Dot | TokenKind::QuestionDot => {
                    let optional = self.at(TokenKind::QuestionDot);
                    self.bump();
                    // `pair.0`
                    let name = if self.at(TokenKind::Integer) {
                        let span = self.bump().span;
                        Name { text: self.text(span).to_string(), span }
                    } else {
                        self.name_or_keyword()
                    };
                    ExpressionKind::Member { target: Box::new(expression), name, optional }
                }
                TokenKind::ParenOpen => {
                    self.bump();
                    let arguments =
                        self.with_trailing_closures(true, |parser| parser.comma_separated(TokenKind::ParenClose, Self::argument));
                    self.check_argument_order(&arguments);
                    ExpressionKind::Call { callee: Box::new(expression), arguments, style: CallStyle::Parentheses }
                }
                TokenKind::BracketOpen => {
                    self.bump();
                    let index = self.with_trailing_closures(true, Self::expression);
                    if self.at(TokenKind::Comma) {
                        self.error_here(
                            "`name [...]` is always indexing. To pass a list to a command call, use parentheses: `print([1, 2])`",
                        );
                        self.recover_to_line_end();
                        return expression_error(start.to(self.previous_span()));
                    }
                    self.expect(TokenKind::BracketClose);
                    ExpressionKind::Index { target: Box::new(expression), index: Box::new(index) }
                }
                TokenKind::Question => {
                    self.bump();
                    ExpressionKind::Try(Box::new(expression))
                }
                TokenKind::Less => match self.generic_arguments() {
                    Some(arguments) => ExpressionKind::Generic { target: Box::new(expression), arguments },
                    None => return expression,
                },
                TokenKind::BraceOpen if self.trailing_closures && Self::takes_trailing_closure(&expression) => {
                    let closure = Argument { label: None, is_spread: false, value: self.closure() };
                    match expression.kind {
                        ExpressionKind::Call { callee, mut arguments, style: CallStyle::Parentheses } => {
                            arguments.push(closure);
                            ExpressionKind::Call { callee, arguments, style: CallStyle::Parentheses }
                        }
                        _ => ExpressionKind::Call { callee: Box::new(expression), arguments: vec![closure], style: CallStyle::Parentheses },
                    }
                }
                _ => return expression,
            };
            expression = Expression { kind, span: start.to(self.previous_span()) };
        }
    }

    /// `html { ... }`, `list.map { ... }`, `fold(0) { ... }`, `load<Config> { ... }` - but not `1 { ... }`.
    fn takes_trailing_closure(expression: &Expression) -> bool {
        match &expression.kind {
            ExpressionKind::Name(_) | ExpressionKind::Member { .. } | ExpressionKind::Generic { .. } => true,
            // One trailing closure per call
            ExpressionKind::Call { arguments, style: CallStyle::Parentheses, .. } => !arguments.last().is_some_and(|last| {
                last.label.is_none() && matches!(last.value.kind, ExpressionKind::Closure(_)) && last.value.span.end == expression.span.end
            }),
            _ => false,
        }
    }

    /// `<` starts generic arguments if what follows parses as types, is closed by `>`, and the token after that
    /// cannot continue a comparison. Otherwise it is "less than". Decided without knowing what the names mean.
    fn generic_arguments(&mut self) -> Option<Vec<TypeReference>> {
        let checkpoint = self.checkpoint();
        self.bump();
        let mut arguments = vec![self.type_reference()];
        while self.eat(TokenKind::Comma) {
            arguments.push(self.type_reference());
        }
        let closed = self.eat(TokenKind::Greater);
        let follows = matches!(
            self.kind(),
            TokenKind::ParenOpen
                | TokenKind::ParenClose
                | TokenKind::BracketClose
                | TokenKind::BraceOpen
                | TokenKind::BraceClose
                | TokenKind::Colon
                | TokenKind::Comma
                | TokenKind::Dot
                | TokenKind::Question
                | TokenKind::EqualEqual
                | TokenKind::BangEqual
                | TokenKind::Newline
                | TokenKind::EndOfFile
        );
        if closed && follows && !self.failed_since(checkpoint) {
            return Some(arguments);
        }
        self.rewind(checkpoint);
        None
    }

    pub(super) fn primary_expression(&mut self) -> Expression {
        let start = self.span();
        let kind = match self.kind().clone() {
            TokenKind::Integer => {
                self.bump();
                ExpressionKind::Integer(self.text(start).replace('_', ""))
            }
            TokenKind::Float => {
                self.bump();
                ExpressionKind::Float(self.text(start).replace('_', ""))
            }
            TokenKind::Char(value) => {
                self.bump();
                ExpressionKind::Char(value)
            }
            TokenKind::Text(parts) => {
                self.bump();
                ExpressionKind::Text(self.text_segments(parts))
            }
            TokenKind::Keyword(Keyword::True) => {
                self.bump();
                ExpressionKind::Bool(true)
            }
            TokenKind::Keyword(Keyword::False) => {
                self.bump();
                ExpressionKind::Bool(false)
            }
            TokenKind::Identifier | TokenKind::Keyword(Keyword::SelfValue | Keyword::SelfType) => {
                self.bump();
                ExpressionKind::Name(self.text(start).to_string())
            }
            // `.Circle(2.0)`, `.Empty`: a case of the expected type
            TokenKind::Dot if *self.kind_at(1) == TokenKind::Identifier => {
                self.bump();
                ExpressionKind::ImplicitMember(self.name())
            }
            TokenKind::ParenOpen => {
                self.bump();
                let mut items = self.with_trailing_closures(true, |parser| parser.comma_separated(TokenKind::ParenClose, Self::argument));
                if items.len() == 1 && items[0].label.is_none() && !items[0].is_spread {
                    items.remove(0).value.kind
                } else {
                    ExpressionKind::Tuple(items)
                }
            }
            TokenKind::BracketOpen => self.with_trailing_closures(true, Self::list_or_map),
            TokenKind::BraceOpen => return self.closure(),
            TokenKind::Keyword(Keyword::If) => self.if_expression(),
            TokenKind::Keyword(Keyword::Match) => self.match_expression(),
            _ => {
                self.error_here(format!("Expected an expression, found {}", self.kind().describe()));
                ExpressionKind::Error
            }
        };
        Expression { kind, span: start.to(self.previous_span()) }
    }

    /// `[1, 2, ...more]`, `["a": 1]`, `[]`, `[:]`
    fn list_or_map(&mut self) -> ExpressionKind {
        self.bump();
        self.skip_newlines();
        if self.at(TokenKind::Colon) && *self.kind_at(1) == TokenKind::BracketClose {
            self.bump();
            self.bump();
            return ExpressionKind::Map(Vec::new());
        }
        let mut entries = Vec::new();
        let mut is_map = false;
        let items = self.comma_separated(TokenKind::BracketClose, |parser| {
            let is_spread = parser.eat(TokenKind::Ellipsis);
            let value = parser.expression();
            if !is_spread && parser.eat(TokenKind::Colon) {
                is_map = true;
                entries.push((value.clone(), parser.expression()));
            }
            Argument { label: None, is_spread, value }
        });
        if !is_map {
            return ExpressionKind::List(items);
        }
        if entries.len() != items.len() {
            let span = self.previous_span();
            self.error("Every entry of a map literal is `key: value`", span);
        }
        ExpressionKind::Map(entries)
    }

    /// The expressions inside of a string literal are source ranges. They are lexed and parsed like a file of their own.
    fn text_segments(&mut self, parts: Vec<TextPart>) -> Vec<TextSegment> {
        parts
            .into_iter()
            .map(|part| match part {
                TextPart::Literal(text) => TextSegment::Literal(text),
                TextPart::Expression(range) => {
                    let lexed = lexer::lex_range(self.source, range);
                    let mut parser = Parser::new(self.source, lexed.tokens, Vec::new());
                    let expression = parser.expression();
                    if !parser.at(TokenKind::EndOfFile) {
                        parser.error_here("Expected `}` after the expression. A literal brace is written `\\{`");
                    }
                    self.diagnostics.extend(lexed.diagnostics);
                    self.diagnostics.extend(parser.finish());
                    TextSegment::Expression(expression)
                }
            })
            .collect()
    }

    /// `{ ... }`, `{ value => ... }`, `{ a, b => ... }`, `{ user: User => ... }`
    pub(super) fn closure(&mut self) -> Expression {
        let start = self.span();
        self.bump();
        self.skip_newlines();
        let parameters = self.closure_parameters().unwrap_or_default();
        let body = self.block_after_brace(start);
        let span = body.span;
        Expression { kind: ExpressionKind::Closure(Closure { parameters, body }), span }
    }

    /// Parameters are recognized by trying: they are there if a list of patterns is followed by `=>`.
    fn closure_parameters(&mut self) -> Option<Vec<ClosureParameter>> {
        let checkpoint = self.checkpoint();
        let mut parameters = Vec::new();
        loop {
            let pattern = self.pattern();
            let annotation = self.eat(TokenKind::Colon).then(|| self.type_reference());
            parameters.push(ClosureParameter { pattern, annotation });
            if !self.eat(TokenKind::Comma) {
                break;
            }
        }
        if self.failed_since(checkpoint) || !self.eat(TokenKind::Arrow) {
            self.rewind(checkpoint);
            return None;
        }
        Some(parameters)
    }

    fn if_expression(&mut self) -> ExpressionKind {
        self.bump();
        let condition = self.condition();
        let then = self.block();
        let otherwise = self.eat_keyword(Keyword::Else).then(|| {
            let start = self.span();
            let kind = if self.at_keyword(Keyword::If) { self.if_expression() } else { ExpressionKind::Block(self.block()) };
            Box::new(Expression { kind, span: start.to(self.previous_span()) })
        });
        ExpressionKind::If { condition, then, otherwise }
    }

    fn match_expression(&mut self) -> ExpressionKind {
        self.bump();
        let subject = self.head_expression();
        let mut arms = Vec::new();
        if !self.expect(TokenKind::BraceOpen) {
            return ExpressionKind::Match { subject: Box::new(subject), arms };
        }
        loop {
            self.skip_newlines();
            if self.at(TokenKind::BraceClose) || self.at(TokenKind::EndOfFile) {
                break;
            }
            let before = self.position;
            arms.push(self.with_trailing_closures(true, Self::match_arm));
            self.eat(TokenKind::Comma);
            if !matches!(self.kind(), TokenKind::Newline | TokenKind::BraceClose | TokenKind::EndOfFile) {
                self.error_here(format!("Expected the end of the match arm, found {}", self.kind().describe()));
                self.recover_to_line_end();
            }
            if self.position == before {
                self.bump();
            }
        }
        self.expect(TokenKind::BraceClose);
        ExpressionKind::Match { subject: Box::new(subject), arms }
    }

    /// `pattern => value`, `pattern if guard => value`, `pattern => { statements }`
    fn match_arm(&mut self) -> MatchArm {
        let pattern = self.pattern();
        let guard = self.eat_keyword(Keyword::If).then(|| self.head_expression());
        self.expect(TokenKind::Arrow);
        self.skip_newlines();
        let body = if self.at(TokenKind::BraceOpen) {
            // The body of an arm is a block, not a closure - like the body of an `if`
            let block = self.block();
            let span = block.span;
            Expression { kind: ExpressionKind::Block(block), span }
        } else {
            self.command_expression()
        };
        MatchArm { pattern, guard, body }
    }
}
