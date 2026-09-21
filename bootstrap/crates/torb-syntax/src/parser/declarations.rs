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
            TokenKind::Keyword(Keyword::Const | Keyword::Var) => self.constant_declaration(modifiers),
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

    /// `public const pi = 3.14` at the top level of a module. `public var` is an error: a module has no mutable
    /// state, so there is nothing a top-level `var` could export.
    fn constant_declaration(&mut self, modifiers: Modifiers) -> DeclarationKind {
        let binding = self.binding();
        if binding.is_var && modifiers.visibility == Visibility::Public {
            self.error("A module has no mutable state: only `const` can be public", binding.pattern.span);
        }
        DeclarationKind::Constant(binding)
    }

    /// `use A, B from "./file"`, `use Option.Some from "./option"`, `use Shape.Circle`, `use * as http from "std/http"`
    fn use_declaration(&mut self) -> DeclarationKind {
        self.bump();
        // `use "acme/text"`: nothing runs when a module is imported, so a `use` without names would mean nothing
        if matches!(self.kind(), TokenKind::Text(_)) {
            let start = self.span();
            let source = UseSource::Module(self.plain_text("the path of a module"));
            let path = match &source {
                UseSource::Module(path) => path.clone(),
                UseSource::Local => String::new(),
            };
            self.error_with_note(
                "A `use` names what it imports",
                format!("Write the names: `use Name from \"{path}\"`, a member of a type `use String.shout from \"{path}\"`"),
                start.to(self.previous_span()),
            );
            return DeclarationKind::Use(UseDeclaration { items: UseItems::Names(Vec::new()), source });
        }
        let items = if self.eat(TokenKind::Star) {
            if !self.at_word("as") {
                self.error_here("`use *` needs a name: `use * as name from \"...\"`");
            } else {
                self.bump();
            }
            UseItems::All { alias: self.name() }
        } else {
            let mut names = vec![self.use_item()];
            while self.eat(TokenKind::Comma) {
                names.push(self.use_item());
            }
            UseItems::Names(names)
        };
        if self.at_word("from") {
            self.bump();
        } else {
            // `use Shape.Circle`: without `from` every path is resolved in this file's own scope, which is what a
            // type declared here needs. A path of one segment would name something that is in scope already.
            if is_local_use(&items) {
                return DeclarationKind::Use(UseDeclaration { items, source: UseSource::Local });
            }
            self.error_here("Expected `from \"...\"`");
        }
        // `use Some, None from Option` is gone: after `from` there is always a module
        if self.at(TokenKind::Identifier) {
            let start = self.span();
            let mut path = vec![self.name()];
            while self.at(TokenKind::Dot) && *self.kind_at(1) == TokenKind::Identifier {
                self.bump();
                path.push(self.name());
            }
            let span = start.to(self.previous_span());
            self.error(type_source_message(&path, &items), span);
            return DeclarationKind::Use(UseDeclaration { items, source: UseSource::Module(String::new()) });
        }
        DeclarationKind::Use(UseDeclaration { items, source: UseSource::Module(self.plain_text("the path of a module")) })
    }

    /// `Option.Some as Just`: one item of a `use` list, with its path and the local name it is to get.
    fn use_item(&mut self) -> UseItem {
        let mut path = vec![self.name()];
        while self.at(TokenKind::Dot) && *self.kind_at(1) == TokenKind::Identifier {
            self.bump();
            path.push(self.name());
        }
        // `use A as B from "..."`: `as` is a word, not a keyword, so a type named `as` stays possible
        if !self.at_word("as") {
            return UseItem { path, alias: None };
        }
        self.bump();
        UseItem { path, alias: Some(self.name()) }
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
        // A method does not list its receiver: the parameter list is what the caller writes. `self` still stands in a
        // function *type* (`(var self: Config) => Void`), which is parsed in `types.rs` and never reaches here.
        if self.at_keyword(Keyword::SelfValue) {
            return self.written_receiver(doc, is_var, is_variadic);
        }
        let name = self.name();
        let annotation = self.eat(TokenKind::Colon).then(|| self.type_reference());
        if annotation.is_none() && !name.text.is_empty() {
            self.error(format!("The parameter `{}` needs a type", name.text), name.span);
        }
        let default = self.eat(TokenKind::Equal).then(|| self.expression());
        Parameter { doc, name, is_var, is_variadic, annotation, default }
    }

    /// `fn area(self)` and `fn translate(var self, ...)`: the two spellings the language traded for `fn area()` and
    /// `var fn translate(...)`, each with the line it is written as now.
    fn written_receiver(&mut self, doc: Option<String>, is_var: bool, is_variadic: bool) -> Parameter {
        let written = self.name_or_keyword();
        let note = match is_var {
            true => "Write `var fn name(...)`: `var` says the method may change its receiver",
            false => "Write `fn name(...)`: what stands in the parentheses is what the caller writes",
        };
        self.error_with_note("A method does not list `self`", note, written.span);
        let annotation = self.eat(TokenKind::Colon).then(|| self.type_reference());
        // An empty name is the parser's placeholder: the member keeps the receiver it was given, not this one
        Parameter { doc, name: Name { text: String::new(), span: written.span }, is_var, is_variadic, annotation, default: None }
    }

    /// `<Key: Hash & Equals, Value = Self>`
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

    /// `Hash & Equals`
    fn bounds(&mut self) -> Vec<TypeReference> {
        let mut bounds = vec![self.single_type()];
        if self.at(TokenKind::Plus) {
            self.error_here("Traits are combined with `&`: `Compare & Show`");
        }
        while self.eat(TokenKind::Ampersand) || self.eat(TokenKind::Plus) {
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

    /// `with Add, Compare`, for a `trait`'s supertraits and an `extend`'s implementations - neither has a field of
    /// its own to delegate to, so a stray `by` is rejected here rather than parsed and ignored.
    fn trait_list(&mut self) -> Vec<TypeReference> {
        if !self.eat_keyword(Keyword::With) {
            return Vec::new();
        }
        let mut traits = vec![self.type_reference()];
        while self.eat(TokenKind::Comma) {
            traits.push(self.type_reference());
        }
        if self.at_word("by") {
            self.error_here("`by` delegates to a field: only a `type` has fields to name");
            self.bump();
            self.name();
        }
        traits
    }

    /// `with Show, Add & Subtract by value, Compare by value`: `by` binds to the one element directly in front of
    /// it, which may be an `&` group.
    fn delegated_trait_list(&mut self) -> Vec<TraitClause> {
        if !self.eat_keyword(Keyword::With) {
            return Vec::new();
        }
        let mut traits = vec![self.trait_clause()];
        while self.eat(TokenKind::Comma) {
            traits.push(self.trait_clause());
        }
        traits
    }

    fn trait_clause(&mut self) -> TraitClause {
        let capability = self.type_reference();
        let delegate = if self.at_word("by") {
            self.bump();
            Some(self.name())
        } else {
            None
        };
        TraitClause { capability, delegate }
    }

    fn type_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let name = self.name();
        let generics = self.generic_parameters();
        if self.eat(TokenKind::Equal) {
            return DeclarationKind::Alias(AliasDeclaration { name, generics, target: self.type_reference() });
        }
        let traits = self.delegated_trait_list();
        let where_clauses = self.where_clauses();
        let members = self.members(true);
        DeclarationKind::Type(TypeDeclaration { name, generics, traits, where_clauses, members })
    }

    fn trait_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let name = self.name();
        let generics = self.generic_parameters();
        let supertraits = self.trait_list();
        let members = self.members(true);
        DeclarationKind::Trait(TraitDeclaration { name, generics, supertraits, members })
    }

    fn extend_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let generics = self.generic_parameters();
        let target = self.type_reference();
        let traits = self.trait_list();
        let where_clauses = self.where_clauses();
        let members = self.members(true);
        DeclarationKind::Extend(ExtendDeclaration { generics, target, traits, where_clauses, members })
    }

    fn foreign_declaration(&mut self) -> DeclarationKind {
        self.bump();
        let library = self.plain_text("the name of a library");
        DeclarationKind::Foreign(ForeignDeclaration { library, members: self.members(false) })
    }

    /// `{ ... }` of a type, trait or extension. A missing body is an empty one (`native type Bool`). `receiver` is
    /// false for a `foreign` block: what it holds are free functions of a C library, with no type to belong to.
    fn members(&mut self, receiver: bool) -> Vec<Member> {
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
            members.push(self.member(receiver));
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

    fn member(&mut self, receiver: bool) -> Member {
        let doc = self.take_doc();
        let start = self.span();
        let modifiers = self.modifiers();
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::Static) if receiver => self.static_member(),
            TokenKind::Keyword(Keyword::Fn) => MemberKind::Function(self.method(receiver, false)),
            TokenKind::Keyword(Keyword::Var) if *self.kind_at(1) == TokenKind::Keyword(Keyword::Fn) && receiver => {
                self.bump();
                MemberKind::Function(self.method(true, true))
            }
            TokenKind::Keyword(Keyword::Const) => self.constant_member(),
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

    /// `fn area(): Int` and `var fn translate(deltaX: Int)`: the receiver is not written, so it is put back here. Its
    /// span is the name of the function, which is where a message about the receiver has to point.
    fn method(&mut self, receiver: bool, is_var: bool) -> FunctionDeclaration {
        let mut function = self.function();
        if !receiver {
            return function;
        }
        let name = Name { text: "self".to_string(), span: function.name.span };
        function.parameters.insert(0, Parameter { doc: None, name, is_var, is_variadic: false, annotation: None, default: None });
        function
    }

    /// `static origin = Point(0, 0)`, `static fn square(size: Int): Self`. `const` is what a member is without a word
    /// of its own, so `static const origin = ...` is the same thing written out.
    fn static_member(&mut self) -> MemberKind {
        let keyword = self.span();
        self.bump();
        if self.at_keyword(Keyword::Var) {
            let span = keyword.to(self.span());
            self.bump();
            self.error_with_note(
                format!("`{} var` does not exist", crate::token::STATIC),
                "A type has no mutable state: what belongs to the type is a constant of it",
                span,
            );
        } else {
            self.eat_keyword(Keyword::Const);
        }
        if self.at(TokenKind::Keyword(Keyword::Fn)) {
            return MemberKind::Function(self.function());
        }
        let binding = self.binding_body(None, false);
        // `static pi: Self` without a value: a trait that requires a constant is not in the language yet
        if matches!(binding.value.kind, ExpressionKind::Error) && binding.annotation.is_some() {
            self.error_with_note(
                "A constant of the type needs a value",
                "A trait cannot require one without a value yet: declare it in every implementation",
                binding.pattern.span,
            );
        }
        MemberKind::Constant(binding)
    }

    /// `const x: Int` is the long way to write the field `x: Int`. Without a type it names neither reading, and the
    /// message offers both.
    fn constant_member(&mut self) -> MemberKind {
        let keyword = self.span();
        self.bump();
        if *self.kind_at(1) != TokenKind::Colon {
            let name = self.name();
            let span = keyword.to(name.span);
            self.error_with_note(
                format!("`{}` is neither a field nor a constant of the type", name.text),
                format!("A field is `{0}: Type`, a constant of the type is `{1} {0} = ...`", name.text, crate::token::STATIC),
                span,
            );
            let annotation = TypeReference { kind: TypeKind::Error, span: name.span };
            let default = self.eat(TokenKind::Equal).then(|| self.with_trailing_closures(true, Self::command_expression));
            return MemberKind::Field(Field { doc: None, name, is_var: false, annotation, default });
        }
        MemberKind::Field(self.field(false))
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

/// Whether a `use` without `from` is the local form: every item names a type of this file and a case of it
/// (`use Shape.Circle`). A single bare name would rebind what is in scope already, so that stays a missing `from`.
fn is_local_use(items: &UseItems) -> bool {
    let UseItems::Names(items) = items else { return false };
    !items.is_empty() && items.iter().all(|item| item.path.len() > 1)
}

/// `use Some, None from Option` was the old spelling of a case import. The message teaches the path form instead.
fn type_source_message(path: &[Name], items: &UseItems) -> String {
    let owner = path.iter().map(|name| name.text.as_str()).collect::<Vec<_>>().join(".");
    let message = format!("`from` takes a module in quotes, not the type `{owner}`");
    let UseItems::Names(items) = items else { return message };
    match items.first() {
        Some(first) => {
            format!("{message}: a case is imported by its path, `use {owner}.{} from \"...\"`", first.name().text)
        }
        None => message,
    }
}
