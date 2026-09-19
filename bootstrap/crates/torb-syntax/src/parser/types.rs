use super::Parser;
use crate::ast::*;
use crate::token::{Keyword, TokenKind};

impl Parser<'_> {
    /// A type, including `Show & Encode` (a value that implements several traits).
    pub(super) fn type_reference(&mut self) -> TypeReference {
        let start = self.span();
        let first = self.single_type();
        if self.at(TokenKind::Plus) {
            self.error_here("Traits are combined with `&`: `Compare & Show`");
        } else if !self.at(TokenKind::Ampersand) {
            return first;
        }
        let mut members = vec![first];
        while self.eat(TokenKind::Ampersand) || self.eat(TokenKind::Plus) {
            members.push(self.single_type());
        }
        TypeReference { kind: TypeKind::Intersection(members), span: start.to(self.previous_span()) }
    }

    /// A type without `&`. This is what the items of a bound list are (`where Item: Hash & Equals`).
    pub(super) fn single_type(&mut self) -> TypeReference {
        let start = self.span();
        if self.eat_keyword(Keyword::Lazy) {
            let inner = self.type_reference();
            return TypeReference { kind: TypeKind::Lazy(Box::new(inner)), span: start.to(self.previous_span()) };
        }
        let mut result = self.primary_type();
        while self.eat(TokenKind::Question) {
            result = TypeReference { kind: TypeKind::Optional(Box::new(result)), span: start.to(self.previous_span()) };
        }
        if self.at(TokenKind::Pipe) {
            self.error_here(
                "Only literals can be combined with `|` (`\"online\" | \"offline\"`). There are no unions of types: declare a type with cases",
            );
            while self.eat(TokenKind::Pipe) {
                self.primary_type();
            }
        }
        result
    }

    fn primary_type(&mut self) -> TypeReference {
        let start = self.span();
        let kind = match self.kind() {
            TokenKind::ParenOpen => self.tuple_or_function_type(),
            TokenKind::Float => {
                self.error_here("A float cannot be a literal type");
                self.bump();
                TypeKind::Error
            }
            kind if Self::starts_literal(kind) => self.literal_type(),
            TokenKind::Identifier | TokenKind::Keyword(Keyword::SelfType) => {
                let mut path = vec![self.name_or_keyword()];
                while self.at(TokenKind::Dot) && *self.kind_at(1) == TokenKind::Identifier {
                    self.bump();
                    path.push(self.name());
                }
                let arguments =
                    if self.eat(TokenKind::Less) { self.comma_separated(TokenKind::Greater, Self::type_reference) } else { Vec::new() };
                TypeKind::Named { path, arguments }
            }
            _ => {
                self.error_here(format!("Expected a type, found {}", self.kind().describe()));
                TypeKind::Error
            }
        };
        TypeReference { kind, span: start.to(self.previous_span()) }
    }

    /// `"online" | "offline"`, `1 | 2 | 3`. A single literal is also what a const argument looks like: `Array<Float, 16>`.
    fn literal_type(&mut self) -> TypeKind {
        let mut literals = vec![self.type_literal()];
        while self.eat(TokenKind::Pipe) {
            if Self::starts_literal(self.kind()) {
                literals.push(self.type_literal());
            } else if matches!(self.kind(), TokenKind::Float) {
                self.error_here("A float cannot be a literal type");
                self.bump();
            } else {
                self.error_here("Only literals can be combined with `|`. There are no unions of types: declare a type with cases");
                self.primary_type();
            }
        }
        TypeKind::Literals(literals)
    }

    /// A text, integer, character or boolean literal, or `-` in front of one: what may stand where a type stands.
    /// A float is deliberately excluded (rejected explicitly, with its own message) and never reaches here.
    fn starts_literal(kind: &TokenKind) -> bool {
        matches!(
            kind,
            TokenKind::Text(_)
                | TokenKind::Integer
                | TokenKind::Char(_)
                | TokenKind::Minus
                | TokenKind::Keyword(Keyword::True | Keyword::False)
        )
    }

    fn type_literal(&mut self) -> Expression {
        let start = self.span();
        if self.eat(TokenKind::Minus) {
            let operand = self.primary_expression();
            let kind = ExpressionKind::Unary { operator: UnaryOperator::Negate, operand: Box::new(operand) };
            return Expression { kind, span: start.to(self.previous_span()) };
        }
        let literal = self.primary_expression();
        if let ExpressionKind::Text(segments) = &literal.kind {
            if segments.iter().any(|segment| matches!(segment, TextSegment::Expression(_))) {
                self.error("A literal type cannot contain `{...}`", literal.span);
            }
        }
        literal
    }

    /// `(Int, String)` and `(lowest: Int, highest: Int)` are tuples, `(value: Int, String) => Bool` is a function.
    fn tuple_or_function_type(&mut self) -> TypeKind {
        self.bump();
        let parameters = self.comma_separated(TokenKind::ParenClose, |parser| {
            let is_var = parser.eat_keyword(Keyword::Var);
            let is_named = matches!(parser.kind(), TokenKind::Identifier | TokenKind::Keyword(_)) && *parser.kind_at(1) == TokenKind::Colon;
            let name = is_named.then(|| {
                let name = parser.name_or_keyword();
                parser.bump();
                name
            });
            FunctionTypeParameter { name, is_var, annotation: parser.type_reference() }
        });
        if self.eat(TokenKind::Arrow) {
            return TypeKind::Function { parameters, result: Box::new(self.type_reference()) };
        }
        if let Some(parameter) = parameters.iter().find(|parameter| parameter.is_var) {
            self.error("`var` only makes sense in a function type. Is a `=> Result` missing?", parameter.annotation.span);
        }
        if parameters.len() == 1 && parameters[0].name.is_none() {
            return parameters.into_iter().next().map_or(TypeKind::Error, |parameter| parameter.annotation.kind);
        }
        TypeKind::Tuple(
            parameters.into_iter().map(|parameter| TupleTypeField { label: parameter.name, annotation: parameter.annotation }).collect(),
        )
    }
}
