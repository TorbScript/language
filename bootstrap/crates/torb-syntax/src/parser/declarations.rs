use super::Parser;
use crate::ast::*;
use crate::token::{Keyword, TextPart, TokenKind};

impl Parser<'_> {
    pub(super) fn declaration(&mut self) -> Declaration {
        let doc = self.take_doc();
        let start = self.span();
        let modifiers = self.modifiers();
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::Use) => self.use_declaration(),
            TokenKind::Keyword(Keyword::Fn) => DeclarationKind::Function(self.function()),
            TokenKind::Keyword(Keyword::Type) => self.type_declaration(),
            TokenKind::Keyword(Keyword::Trait) => self.trait_declaration(),
            TokenKind::Keyword(Keyword::Extend) => self.extend_declaration(),
            TokenKind::Keyword(Keyword::Foreign) => self.foreign_declaration(),
            _ => {
                self.error_here(format!(
                    "Expected a declaration (`fn`, `type`, `trait`, `extend`, `use`), found {}",
                    self.kind().describe()
                ));
                DeclarationKind::Use(UseDeclaration { items: UseItems::Names(Vec::new()), source: UseSource::Module(String::new()) })
            }
        };
        Declaration { doc, modifiers, kind, span: start.to(self.previous_span()) }
    }

    fn modifiers(&mut self) -> Modifiers {
        let mut modifiers = Modifiers::default();
        loop {
            match self.kind() {
                TokenKind::Keyword(Keyword::Public) => modifiers.visibility = Visibility::Public,
                TokenKind::Keyword(Keyword::Private) => {
                    // `private(var) balance: Int`
                    if *self.kind_at(1) == TokenKind::ParenOpen && *self.kind_at(2) == TokenKind::Keyword(Keyword::Var) {
                        self.bump();
                        self.bump();
                        self.bump();
                        self.expect(TokenKind::ParenClose);
                        modifiers.visibility = Visibility::PrivateVar;
                        continue;
                    }
                    modifiers.visibility = Visibility::Private;
                }
                TokenKind::Keyword(Keyword::Native) => modifiers.native = true,
                TokenKind::Keyword(Keyword::Shared) => modifiers.shared = true,
                _ => return modifiers,
            }
            self.bump();
        }
    }

    /// `use A, B from "./file"`, `use * as http from "std/net/http"`
    fn use_declaration(&mut self) -> DeclarationKind {
        self.bump();
        // `use "./text-extensions"`
        if matches!(self.kind(), TokenKind::Text(_)) {
            let source = UseSource::Module(self.plain_text("the path of a module"));
            return DeclarationKind::Use(UseDeclaration { items: UseItems::OnlyExtensions, source });
        }
        let items = if self.eat(TokenKind::Star) {
            if !self.at_word("as") {
                self.error_here("`use *` needs a name: `use * as name from \"...\"`");
            } else {
                self.bump();
            }
            UseItems::All { alias: self.name() }
        } else {
            let mut names = vec![self.name()];
            while self.eat(TokenKind::Comma) {
                names.push(self.name());
            }
            UseItems::Names(names)
        };
        if self.at_word("from") {
            self.bump();
        } else {
            self.error_here("Expected `from \"...\"`");
        }
        // `use Some, None from Option`: the cases of a type
        if self.at(TokenKind::Identifier) {
            let mut path = vec![self.name()];
            while self.at(TokenKind::Dot) && *self.kind_at(1) == TokenKind::Identifier {
                self.bump();
                path.push(self.name());
            }
            return DeclarationKind::Use(UseDeclaration { items, source: UseSource::Type(path) });
        }
        DeclarationKind::Use(UseDeclaration { items, source: UseSource::Module(self.plain_text("the path of a module")) })
    }

    /// A string literal without interpolation.
    fn plain_text(&mut self, what: &str) -> String {
        if let TokenKind::Text(parts) = self.kind().clone() {
            self.bump();
            if let [TextPart::Literal(text)] = parts.as_slice() {
                return text.clone();
            }
            let span = self.previous_span();
            self.error(format!("Expected {what} as a plain string, without `{{...}}`"), span);
            return String::new();
        }
        self.error_here(format!("Expected {what} as a string, found {}", self.kind().describe()));
        String::new()
    }

    pub(super) fn function(&mut self) -> FunctionDeclaration {
        self.bump();
        let name = self.name();
        let generics = self.generic_parameters();
        self.expect(TokenKind::ParenOpen);
        let parameters = self.with_trailing_closures(true, |parser| parser.comma_separated(TokenKind::ParenClose, Self::parameter));
        let return_type = self.eat(TokenKind::Colon).then(|| self.type_reference());
        let where_clauses = self.where_clauses();
        let body = self.at(TokenKind::BraceOpen).then(|| self.block());
        FunctionDeclaration { name, generics, parameters, return_type, where_clauses, body }
    }

    fn parameter(&mut self) -> Parameter {
        let doc = self.take_doc();
        let is_variadic = self.eat(TokenKind::Ellipsis);
        let is_var = self.eat_keyword(Keyword::Var);
        let name = if self.at_keyword(Keyword::SelfValue) { self.name_or_keyword() } else { self.name() };
        let annotation = self.eat(TokenKind::Colon).then(|| self.type_reference());
        if annotation.is_none() && name.text != "self" && !name.text.is_empty() {
            self.error(format!("The parameter `{}` needs a type", name.text), name.span);
        }
        let default = self.eat(TokenKind::Equal).then(|| self.expression());
        Parameter { doc, name, is_var, is_variadic, annotation, default }
    }

    /// `<Key: Hash + Equals, Value = Self>`
    pub(super) fn generic_parameters(&mut self) -> Vec<GenericParameter> {
        if !self.eat(TokenKind::Less) {
            return Vec::new();
        }
        self.comma_separated(TokenKind::Greater, |parser| {
            let is_const = parser.eat_keyword(Keyword::Const);
            let name = parser.name();
            let bounds = if parser.eat(TokenKind::Colon) { parser.bounds() } else { Vec::new() };
            if is_const && bounds.len() != 1 {
                parser.error(format!("A const parameter needs a type: `const {}: Int`", name.text), name.span);
            }
            let default = parser.eat(TokenKind::Equal).then(|| parser.type_reference());
            GenericParameter { is_const, name, bounds, default }
        })
    }

    /// `Hash + Equals`
    fn bounds(&mut self) -> Vec<TypeReference> {
        let mut bounds = vec![self.single_type()];
        while self.eat(TokenKind::Plus) {
            bounds.push(self.single_type());
        }
        bounds
    }

    /// `where Key: Hash, Value: Show`
    fn where_clauses(&mut self) -> Vec<WhereClause> {
        let mut clauses = Vec::new();
        if !self.eat_keyword(Keyword::Where) {
            return clauses;
        }
        loop {
            let subject = self.type_reference();
            self.expect(TokenKind::Colon);
            clauses.push(WhereClause { subject, bounds: self.bounds() });
            if !self.eat(TokenKind::Comma) {
                return clauses;
            }
        }
    }

    /// `with Add, Compare`
    fn trait_list(&mut self) -> Vec<TypeReference> {
        if !self.eat_keyword(Keyword::With) {
            return Vec::new();
        }
        let mut traits = vec![self.type_reference()];
        while self.eat(TokenKind::Comma) {
            traits.push(self.type_reference());
        }
        traits
    }

    fn type_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let name = self.name();
        let generics = self.generic_parameters();
        if self.eat(TokenKind::Equal) {
            return DeclarationKind::Alias(AliasDeclaration { name, generics, target: self.type_reference() });
        }
        let traits = self.trait_list();
        let delegate = if self.at_word("by") {
            self.bump();
            Some(self.name())
        } else {
            None
        };
        let where_clauses = self.where_clauses();
        let members = self.members();
        DeclarationKind::Type(TypeDeclaration { name, generics, traits, delegate, where_clauses, members })
    }

    fn trait_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let name = self.name();
        let generics = self.generic_parameters();
        let supertraits = self.trait_list();
        let members = self.members();
        DeclarationKind::Trait(TraitDeclaration { name, generics, supertraits, members })
    }

    fn extend_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let generics = self.generic_parameters();
        let target = self.type_reference();
        let traits = self.trait_list();
        let where_clauses = self.where_clauses();
        let members = self.members();
        DeclarationKind::Extend(ExtendDeclaration { generics, target, traits, where_clauses, members })
    }

    fn foreign_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let library = self.plain_text("the name of a library");
        DeclarationKind::Foreign(ForeignDeclaration { library, members: self.members() })
    }

    /// `{ ... }` of a type, trait or extension. A missing body is an empty one (`native type Bool`).
    fn members(&mut self) -> Vec<Member> {
        let mut members = Vec::new();
        // The body may start on its own line, after a long `with ...` or `where ...`
        if self.at(TokenKind::Newline) && *self.kind_at(1) == TokenKind::BraceOpen {
            self.bump();
        }
        if !self.eat(TokenKind::BraceOpen) {
            return members;
        }
        loop {
            self.skip_newlines();
            if self.at(TokenKind::BraceClose) || self.at(TokenKind::EndOfFile) {
                break;
            }
            let before = self.position;
            members.push(self.member());
            if !matches!(self.kind(), TokenKind::Newline | TokenKind::BraceClose | TokenKind::EndOfFile) {
                self.error_here(format!("Expected the end of the member, found {}", self.kind().describe()));
                self.recover_to_line_end();
            }
            if self.position == before {
                self.bump();
            }
        }
        self.expect(TokenKind::BraceClose);
        members
    }

    fn member(&mut self) -> Member {
        let doc = self.take_doc();
        let start = self.span();
        let modifiers = self.modifiers();
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::Fn) => MemberKind::Function(self.function()),
            TokenKind::Keyword(Keyword::Const) => MemberKind::Constant(self.binding()),
            TokenKind::Keyword(Keyword::Case) => {
                self.bump();
                let name = self.name();
                let fields = if self.eat(TokenKind::ParenOpen) {
                    self.comma_separated(TokenKind::ParenClose, |parser| parser.field(false))
                } else {
                    Vec::new()
                };
                MemberKind::Case(Case { name, fields })
            }
            _ => {
                let is_var = self.eat_keyword(Keyword::Var);
                MemberKind::Field(self.field(is_var))
            }
        };
        Member { doc, modifiers, kind, span: start.to(self.previous_span()) }
    }

    fn field(&mut self, is_var: bool) -> Field {
        let doc = self.take_doc();
        let name = self.name();
        self.expect(TokenKind::Colon);
        let annotation = self.type_reference();
        let default = self.eat(TokenKind::Equal).then(|| self.with_trailing_closures(true, Self::command_expression));
        Field { doc, name, is_var, annotation, default }
    }
}
