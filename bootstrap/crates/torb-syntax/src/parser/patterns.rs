use super::Parser;
use crate::ast::*;
use crate::token::{Keyword, TokenKind};

impl Parser<'_> {
    /// `1 | 2 | 3`
    pub(super) fn pattern(&mut self) -> Pattern {
        let start = self.span();
        let first = self.single_pattern();
        if !self.at(TokenKind::Pipe) {
            return first;
        }
        let mut alternatives = vec![first];
        while self.eat(TokenKind::Pipe) {
            alternatives.push(self.single_pattern());
        }
        Pattern { kind: PatternKind::Or(alternatives), span: start.to(self.previous_span()) }
    }

    fn single_pattern(&mut self) -> Pattern {
        let start = self.span();
        let kind = match self.kind() {
            TokenKind::Identifier if self.text(start) == "_" => {
                self.bump();
                PatternKind::Wildcard
            }
            TokenKind::Identifier => self.name_or_variant_pattern(),
            // `.Circle(radius)`, `.Empty`, `.Rectangle(width, ...)`
            TokenKind::Dot => {
                self.bump();
                let name = self.name();
                let (fields, has_rest) = if self.eat(TokenKind::ParenOpen) { self.variant_fields() } else { (Vec::new(), false) };
                PatternKind::ImplicitVariant { name, fields, has_rest }
            }
            TokenKind::ParenOpen => {
                self.bump();
                let mut items = self.comma_separated(TokenKind::ParenClose, Self::pattern);
                if items.len() == 1 {
                    items.remove(0).kind
                } else {
                    PatternKind::Tuple(items)
                }
            }
            TokenKind::BracketOpen => self.list_pattern(),
            TokenKind::Integer
            | TokenKind::Float
            | TokenKind::Char(_)
            | TokenKind::Text(_)
            | TokenKind::Minus
            | TokenKind::Keyword(Keyword::True | Keyword::False | Keyword::Void) => self.literal_pattern(),
            _ => {
                self.error_here(format!("Expected a pattern, found {}", self.kind().describe()));
                PatternKind::Error
            }
        };
        Pattern { kind, span: start.to(self.previous_span()) }
    }

    /// `value`, `None`, `Some(value)`, `Shape.Circle(radius)`
    fn name_or_variant_pattern(&mut self) -> PatternKind {
        let mut path = vec![self.name()];
        while self.at(TokenKind::Dot) && *self.kind_at(1) == TokenKind::Identifier {
            self.bump();
            path.push(self.name());
        }
        if self.eat(TokenKind::ParenOpen) {
            let (fields, has_rest) = self.variant_fields();
            return PatternKind::Variant { path, fields, has_rest };
        }
        // A name that starts with an uppercase letter is never a binding: it is a case the scope has to know (`None`)
        if path.len() > 1 || starts_upper_case(&path[0].text) {
            return PatternKind::Variant { path, fields: Vec::new(), has_rest: false };
        }
        PatternKind::Name(path.remove(0).text)
    }

    /// The fields of `Circle(radius: r)` and the `...` of `Config(host, ...)`: the label is kept, and the type
    /// checker decides which field a sub-pattern matches. A `...` comes last and comes once - the rule a list
    /// pattern has for its own.
    fn variant_fields(&mut self) -> (Vec<FieldPattern>, bool) {
        let mut has_rest = false;
        let written = self.comma_separated(TokenKind::ParenClose, |parser| {
            if parser.eat(TokenKind::Ellipsis) {
                if has_rest {
                    let span = parser.previous_span();
                    parser.error("A pattern can only have one `...`", span);
                }
                has_rest = true;
                return None;
            }
            if has_rest {
                let span = parser.span();
                parser.error("A `...` comes last in a pattern: it stands for every field behind it", span);
            }
            let label = (*parser.kind() == TokenKind::Identifier && *parser.kind_at(1) == TokenKind::Colon).then(|| {
                let label = parser.name();
                parser.bump();
                label
            });
            Some(FieldPattern { label, pattern: parser.pattern() })
        });
        (written.into_iter().flatten().collect(), has_rest)
    }

    /// `[]`, `[first, second]`, `[first, ...rest]`, `[..., last]`
    fn list_pattern(&mut self) -> PatternKind {
        self.bump();
        let mut rest = None;
        let mut count = 0;
        let items = self.comma_separated(TokenKind::BracketClose, |parser| {
            if parser.eat(TokenKind::Ellipsis) {
                let name = parser.at(TokenKind::Identifier).then(|| parser.name());
                if rest.is_some() {
                    let span = parser.previous_span();
                    parser.error("A list pattern can only have one `...`", span);
                }
                rest = Some(RestPattern { position: count, name });
                return None;
            }
            count += 1;
            Some(parser.pattern())
        });
        PatternKind::List { items: items.into_iter().flatten().collect(), rest }
    }

    /// `0`, `"text"`, `'a'`, `-1`, `1..=9`
    fn literal_pattern(&mut self) -> PatternKind {
        let start = self.literal_pattern_value();
        let inclusive = self.at(TokenKind::DotDotEqual);
        if inclusive || self.at(TokenKind::DotDot) {
            self.bump();
            let end = self.literal_pattern_value();
            return PatternKind::Range { start: Box::new(start), end: Box::new(end), inclusive };
        }
        PatternKind::Literal(Box::new(start))
    }

    fn literal_pattern_value(&mut self) -> Expression {
        let start = self.span();
        if self.eat(TokenKind::Minus) {
            let operand = self.primary_expression();
            return Expression {
                kind: ExpressionKind::Unary { operator: UnaryOperator::Negate, operand: Box::new(operand) },
                span: start.to(self.previous_span()),
            };
        }
        self.primary_expression()
    }
}

/// Whether a name starts with an uppercase letter, which is what decides between a binding and a case. A name is
/// ASCII, so "uppercase" is `A` to `Z` and nothing else: `_found` and `found` bind and `Found` does not.
fn starts_upper_case(text: &str) -> bool {
    text.starts_with(|first: char| first.is_ascii_uppercase())
}
