//! The scope resolver behind `torb highlight`: a syntactic (not semantic - there is no type checker in stage 0) pass
//! over the syntax tree that decides, for every name, whether it is a type, a trait, a case, a generic parameter, a
//! function, a method, a parameter, a local variable or a field - and whether it is `readonly` (a `const`) or
//! `mutable` (a `var`). See `bootstrap/README.md` for the JSON protocol this feeds and the rules in prose.
//!
//! What it cannot know, because it never sees a type: the actual type of an arbitrary receiver (`value.field` is
//! guessed to be a field, `value.method()` is a method only because it is written with a call after a dot - both are
//! syntactic rules from CONCEPT.md, not lookups), and anything imported through a single-segment `use` from another
//! file (its kind is guessed from the case of its first letter). A name that resolves to nothing gets no token, so
//! the TextMate grammar's guess stays.

use std::collections::HashMap;

use torb_syntax::ast::*;
use torb_syntax::span::Span;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    pub span: Span,
    pub kind: &'static str,
    pub modifiers: Vec<&'static str>,
}

/// The primitive types of `CONCEPT.md`, always known regardless of what the file declares or imports.
const PRIMITIVE_TYPES: &[&str] = &[
    "Int", "Int8", "Int16", "Int32", "Int64", "UInt", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Float32", "Float64", "Decimal",
    "Bool", "Char", "String", "Void", "Never",
];

/// `Some`, `None`, `Ok`, `Fail`: the prelude imports them into every file (`CONTRIBUTING.md`, "Cases"), so they are
/// bare cases everywhere, not only where a file writes `use Option.Some from "std/core"` itself.
const PRELUDE_CASES: &[&str] = &["Some", "None", "Ok", "Fail"];

pub fn resolve(file: &File) -> Vec<Token> {
    let mut file_scope = FileScope::default();
    file_scope.collect_statements(&file.statements, true);
    let mut resolver = Resolver::new(file_scope);
    resolver.statements(&file.statements);
    resolver.tokens
}

// --- The file's global names ---------------------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum GlobalKind {
    Type,
    Trait,
}

#[derive(Debug, Clone, Copy)]
struct FieldSymbol {
    is_var: bool,
    /// A `static` value of the type, and not a field of every value
    is_static: bool,
}

#[derive(Debug, Clone, Copy)]
struct MethodSymbol {
    has_self: bool,
    /// A `var fn`: it changes its receiver, which the editor underlines like a `var` field
    changes_the_receiver: bool,
}

#[derive(Debug, Default, Clone)]
struct TypeInfo {
    kind: Option<GlobalKind>,
    fields: HashMap<String, FieldSymbol>,
    methods: HashMap<String, MethodSymbol>,
    cases: HashMap<String, ()>,
}

#[derive(Default)]
struct FileScope {
    /// `type`, `trait` and `type X = ...` declarations, and the types an `extend` adds to (which may or may not be
    /// declared in this file - `extend` on a type from another module still gets an entry, with `kind: None`).
    types: HashMap<String, TypeInfo>,
    /// Top-level `fn` declarations only - a nested one is a local name instead (see `Resolver::statements`), never
    /// added here, so it cannot leak into an unrelated scope the way `a_nested_fn_does_not_see_the_locals_around_it`
    /// checks for.
    functions: HashMap<String, ()>,
    /// Every member name of the file that is a `var fn` everywhere it is declared. Without a type checker the
    /// receiver of `counter.grow 1` has no type, so this is what makes a call of a `var fn` underlined all the same:
    /// a name that is a `var fn` in one type and something else in another is left out and stays plain.
    changing_methods: HashMap<String, bool>,
    /// Top-level `const`/`var` bindings (readonly unless `is_var`).
    globals: HashMap<String, bool>,
    /// `use * as name`
    namespaces: HashMap<String, ()>,
    /// Cases imported by a two-segment (or longer) `use` path, or a one-segment one that starts uppercase - both
    /// are unambiguous: only cases are ever imported through a type.
    imported_cases: HashMap<String, ()>,
    /// A single-segment `use` whose target file this tool never opens: guessed from the first letter.
    imported_types: HashMap<String, ()>,
    imported_functions: HashMap<String, ()>,
}

impl FileScope {
    /// Recursively collects `type`/`trait`/`extend`/`use`/`type =` declarations wherever they are written -
    /// including inside of a block, which the grammar allows even though it is unusual (an over-approximation: such
    /// a declaration really is scoped to the block it is in - see the module doc comment). A `const`/`var` binding
    /// is only ever added as a *global*, though, when `top_level` is true: this is only ever the case for
    /// `file.statements` itself, never for the body of a block reached through recursion (a `for`/`if`/`while`'s
    /// body, a function's or a closure's body, ...) - those already get a real local scope from the second pass
    /// (`Resolver::block`/`Resolver::function_declaration`), and treating their bindings as file-global besides
    /// would leak them into sibling scopes that must not see them (this is exactly what
    /// `a_nested_fn_does_not_see_the_locals_around_it` guards against). A nested `fn` is deliberately not collected
    /// here at all: it is a local name, scoped to the block that declares it (see `Resolver::statements`).
    fn collect_statements(&mut self, statements: &[Statement], top_level: bool) {
        for statement in statements {
            self.collect_statement(statement, top_level);
        }
    }

    fn collect_statement(&mut self, statement: &Statement, top_level: bool) {
        match &statement.kind {
            StatementKind::Declaration(declaration) => self.collect_declaration(declaration, top_level),
            StatementKind::Binding(binding) => {
                if top_level {
                    for (name, _) in pattern_names(&binding.pattern) {
                        self.globals.insert(name, binding.is_var);
                    }
                }
                self.collect_expression(&binding.value);
            }
            StatementKind::Assignment { target, value } => {
                self.collect_expression(target);
                self.collect_expression(value);
            }
            StatementKind::For { iterable, body, .. } => {
                self.collect_expression(iterable);
                self.collect_statements(&body.statements, false);
            }
            StatementKind::While { condition, body } => {
                self.collect_condition(condition);
                self.collect_statements(&body.statements, false);
            }
            StatementKind::Loop { body } => self.collect_statements(&body.statements, false),
            StatementKind::Return(Some(expression)) | StatementKind::Expression(expression) => self.collect_expression(expression),
            StatementKind::Return(None) | StatementKind::Break | StatementKind::Continue => {}
        }
    }

    fn collect_condition(&mut self, condition: &Condition) {
        match condition {
            Condition::Expression(expression) => self.collect_expression(expression),
            Condition::Binding { value, .. } => self.collect_expression(value),
        }
    }

    /// Descends into every place a declaration or a binding can occur inside of an expression, so that a `use` or a
    /// `type` written inside of a closure or an `if` is still known everywhere in the file. This is an
    /// over-approximation (such a declaration really is scoped to the block it is in) that keeps the resolver simple;
    /// see the module doc comment.
    fn collect_expression(&mut self, expression: &Expression) {
        match &expression.kind {
            ExpressionKind::Text(segments) => {
                for segment in segments {
                    if let TextSegment::Expression(inner) = segment {
                        self.collect_expression(inner);
                    }
                }
            }
            ExpressionKind::Generic { target, .. } => self.collect_expression(target),
            ExpressionKind::Tuple(arguments) | ExpressionKind::List(arguments) => {
                for argument in arguments {
                    self.collect_expression(&argument.value);
                }
            }
            ExpressionKind::Map(entries) => {
                for (key, value) in entries {
                    self.collect_expression(key);
                    self.collect_expression(value);
                }
            }
            ExpressionKind::Member { target, .. } => self.collect_expression(target),
            ExpressionKind::Index { target, index } => {
                self.collect_expression(target);
                self.collect_expression(index);
            }
            ExpressionKind::Call { callee, arguments, .. } => {
                self.collect_expression(callee);
                for argument in arguments {
                    self.collect_expression(&argument.value);
                }
            }
            ExpressionKind::Unary { operand, .. } => self.collect_expression(operand),
            ExpressionKind::Binary { left, right, .. } => {
                self.collect_expression(left);
                self.collect_expression(right);
            }
            ExpressionKind::Range { start, end, .. } => {
                if let Some(start) = start {
                    self.collect_expression(start);
                }
                if let Some(end) = end {
                    self.collect_expression(end);
                }
            }
            ExpressionKind::Try(inner) => self.collect_expression(inner),
            ExpressionKind::Closure(closure) => self.collect_statements(&closure.body.statements, false),
            ExpressionKind::If { condition, then, otherwise } => {
                self.collect_condition(condition);
                self.collect_statements(&then.statements, false);
                if let Some(otherwise) = otherwise {
                    self.collect_expression(otherwise);
                }
            }
            ExpressionKind::Match { subject, arms } => {
                self.collect_expression(subject);
                for arm in arms {
                    if let Some(guard) = &arm.guard {
                        self.collect_expression(guard);
                    }
                    self.collect_expression(&arm.body);
                }
            }
            ExpressionKind::Block(block) => self.collect_statements(&block.statements, false),
            ExpressionKind::Integer(_)
            | ExpressionKind::Float(_)
            | ExpressionKind::Bool(_)
            | ExpressionKind::VoidLiteral
            | ExpressionKind::Char(_)
            | ExpressionKind::Name(_)
            | ExpressionKind::ImplicitMember(_)
            | ExpressionKind::Error => {}
        }
    }

    fn collect_declaration(&mut self, declaration: &Declaration, top_level: bool) {
        match &declaration.kind {
            DeclarationKind::Use(use_declaration) => self.collect_use(use_declaration),
            DeclarationKind::Function(function) => {
                if top_level {
                    self.functions.insert(function.name.text.clone(), ());
                }
                if let Some(body) = &function.body {
                    self.collect_statements(&body.statements, false); // A function's own body is never top-level.
                }
            }
            DeclarationKind::Type(type_declaration) => {
                let info = type_info_of(&type_declaration.members, Some(GlobalKind::Type));
                self.collect_changing_methods(&info);
                self.types.insert(type_declaration.name.text.clone(), info);
            }
            DeclarationKind::Alias(alias) => {
                self.types.entry(alias.name.text.clone()).or_default();
            }
            DeclarationKind::Trait(trait_declaration) => {
                let info = type_info_of(&trait_declaration.members, Some(GlobalKind::Trait));
                self.collect_changing_methods(&info);
                self.types.insert(trait_declaration.name.text.clone(), info);
            }
            DeclarationKind::Extend(extend) => {
                if let Some(name) = simple_type_name(&extend.target) {
                    let addition = type_info_of(&extend.members, None);
                    self.collect_changing_methods(&addition);
                    let entry = self.types.entry(name).or_default();
                    entry.fields.extend(addition.fields);
                    entry.methods.extend(addition.methods);
                    entry.cases.extend(addition.cases);
                }
            }
            DeclarationKind::Foreign(foreign) => {
                for member in &foreign.members {
                    if let MemberKind::Function(function) = &member.kind {
                        self.functions.insert(function.name.text.clone(), ());
                    }
                }
            }
            DeclarationKind::Constant(binding) => {
                if top_level {
                    for (name, _) in pattern_names(&binding.pattern) {
                        self.globals.insert(name, binding.is_var);
                    }
                }
            }
        }
    }

    /// A name stays in the table only while every declaration of it in this file agrees that it is a `var fn`.
    fn collect_changing_methods(&mut self, info: &TypeInfo) {
        for (name, method) in &info.methods {
            let entry = self.changing_methods.entry(name.clone()).or_insert(method.changes_the_receiver);
            *entry = *entry && method.changes_the_receiver;
        }
    }

    fn collect_use(&mut self, use_declaration: &UseDeclaration) {
        match &use_declaration.items {
            UseItems::All { alias } => {
                self.namespaces.insert(alias.text.clone(), ());
            }
            UseItems::Names(items) => {
                for item in items {
                    let local_name = item.local_name().text.clone();
                    if item.path.len() > 1 {
                        self.imported_cases.insert(local_name, ());
                    } else if starts_upper_case(&local_name) {
                        self.imported_types.insert(local_name, ());
                    } else {
                        self.imported_functions.insert(local_name, ());
                    }
                }
            }
        }
    }
}

/// Scans the members of a `type`/`trait`/`extend` body once, for both the global table and a `TypeScope` (the same
/// shape, built the same way).
fn type_info_of(members: &[Member], kind: Option<GlobalKind>) -> TypeInfo {
    let mut info = TypeInfo { kind, ..TypeInfo::default() };
    for member in members {
        match &member.kind {
            MemberKind::Field(field) => {
                info.fields.insert(field.name.text.clone(), FieldSymbol { is_var: field.is_var, is_static: false });
            }
            MemberKind::Constant(binding) => {
                for (name, _) in pattern_names(&binding.pattern) {
                    info.fields.insert(name, FieldSymbol { is_var: false, is_static: true });
                }
            }
            MemberKind::Function(function) => {
                let receiver = function.parameters.first().filter(|parameter| parameter.name.text == "self");
                let symbol =
                    MethodSymbol { has_self: receiver.is_some(), changes_the_receiver: receiver.is_some_and(|parameter| parameter.is_var) };
                info.methods.insert(function.name.text.clone(), symbol);
            }
            MemberKind::Case(case) => {
                info.cases.insert(case.name.text.clone(), ());
            }
        }
    }
    info
}

/// The name an `extend`'s target, a `Shape.Circle` pattern, or a `Type.member` expression ultimately refers to: the
/// last segment of a `Named` type (`http.Response` -> `Response`).
fn simple_type_name(type_reference: &TypeReference) -> Option<String> {
    match &type_reference.kind {
        TypeKind::Named { path, .. } => path.last().map(|name| name.text.clone()),
        _ => None,
    }
}

/// Every name a pattern binds, with its span - used for bindings (`const (a, b) = pair`) and for the fields of a
/// `type`'s `const` member (which is also a `Binding`, in case it ever destructures).
fn pattern_names(pattern: &Pattern) -> Vec<(String, Span)> {
    let mut names = Vec::new();
    collect_pattern_names(pattern, &mut names);
    names
}

fn collect_pattern_names(pattern: &Pattern, names: &mut Vec<(String, Span)>) {
    match &pattern.kind {
        PatternKind::Name(text) => names.push((text.clone(), pattern.span)),
        PatternKind::Tuple(items) => items.iter().for_each(|item| collect_pattern_names(item, names)),
        PatternKind::List { items, rest } => {
            items.iter().for_each(|item| collect_pattern_names(item, names));
            if let Some(RestPattern { name: Some(name), .. }) = rest {
                names.push((name.text.clone(), name.span));
            }
        }
        PatternKind::Or(alternatives) => alternatives.iter().for_each(|item| collect_pattern_names(item, names)),
        PatternKind::Variant { fields, .. } | PatternKind::ImplicitVariant { fields, .. } => {
            fields.iter().for_each(|field| collect_pattern_names(&field.pattern, names))
        }
        PatternKind::Wildcard | PatternKind::Literal(_) | PatternKind::Range { .. } | PatternKind::Error => {}
    }
}

/// "Uppercase" is `A` to `Z` - the same rule `torb_syntax`'s parser uses to tell a pattern binding from a case, and
/// the one the checker reports at every declaration. A name is ASCII, so there is nothing else it could be.
fn starts_upper_case(text: &str) -> bool {
    text.starts_with(|first: char| first.is_ascii_uppercase())
}

// --- Local scope ------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum LocalKind {
    Variable,
    Parameter,
    /// A `fn` nested in a block: a local name, but still a `function` token, never a `variable`.
    Function,
}

#[derive(Debug, Clone, Copy)]
struct LocalSymbol {
    kind: LocalKind,
    is_var: bool,
}

type Scope = HashMap<String, LocalSymbol>;

/// The role a pattern's names play: a closure parameter is `parameter`, everything else (`for`, `if const`, a match
/// arm, a local `const`/`var`) is `variable`.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Role {
    Variable,
    Parameter,
}

impl Role {
    fn local_kind(self) -> LocalKind {
        match self {
            Role::Variable => LocalKind::Variable,
            Role::Parameter => LocalKind::Parameter,
        }
    }

    fn token_kind(self) -> &'static str {
        match self {
            Role::Variable => "variable",
            Role::Parameter => "parameter",
        }
    }
}

/// The type/trait/extend body a method is being resolved in, so that a bare name can resolve to `self`'s fields,
/// constants and methods, and so `Type.Case`/`Type.method` can be resolved for the type declared right here.
#[derive(Clone)]
struct TypeScope {
    name: String,
}

struct Resolver {
    file_scope: FileScope,
    scopes: Vec<Scope>,
    /// A stack of frames, one per `<...>` currently open (a type's, a function's, an `extend`'s) - const generics
    /// included, since `Array<Item, Size>` colors `Size` the same way as `Item`.
    generics: Vec<HashMap<String, ()>>,
    current_type: Option<TypeScope>,
    /// `Some(is_var)` for every use of `self` inside of the method currently being resolved (`None` outside of one,
    /// or inside of a function that has no `self` parameter), so that `var self` colors every `self` in its body
    /// `mutable`, not only its declaration.
    self_is_var: Option<bool>,
    tokens: Vec<Token>,
}

impl Resolver {
    fn new(file_scope: FileScope) -> Self {
        Resolver { file_scope, scopes: Vec::new(), generics: Vec::new(), current_type: None, self_is_var: None, tokens: Vec::new() }
    }

    fn push(&mut self, span: Span, kind: &'static str, modifiers: &[&'static str]) {
        self.tokens.push(Token { span, kind, modifiers: modifiers.to_vec() });
    }

    fn is_generic(&self, name: &str) -> bool {
        self.generics.iter().any(|frame| frame.contains_key(name))
    }

    fn find_local(&self, name: &str) -> Option<LocalSymbol> {
        self.scopes.iter().rev().find_map(|scope| scope.get(name).copied())
    }

    fn current_type_info(&self) -> Option<&TypeInfo> {
        let current = self.current_type.as_ref()?;
        self.file_scope.types.get(&current.name)
    }

    fn is_known_case(&self, name: &str) -> bool {
        PRELUDE_CASES.contains(&name)
            || self.file_scope.imported_cases.contains_key(name)
            || self.file_scope.types.values().any(|info| info.cases.contains_key(name))
    }

    fn is_known_type(&self, name: &str) -> bool {
        PRIMITIVE_TYPES.contains(&name) || self.file_scope.types.contains_key(name) || self.file_scope.imported_types.contains_key(name)
    }

    // --- Statements -------------------------------------------------------------------------------------------

    fn statements(&mut self, statements: &[Statement]) {
        // Hoisted, so a nested `fn` is visible to every statement of its own block (including itself, for
        // recursion), but - because its body gets a fresh scope stack in `function_declaration` - never to the
        // locals around it. A block-nested `fn` is a `function`, never a `method`: `current_type` only ever governs
        // whether a call resolves to an implicit `self`, and a block is not a type's body.
        if let Some(scope) = self.scopes.last_mut() {
            for statement in statements {
                if let StatementKind::Declaration(Declaration { kind: DeclarationKind::Function(function), .. }) = &statement.kind {
                    scope.insert(function.name.text.clone(), LocalSymbol { kind: LocalKind::Function, is_var: false });
                }
            }
        }
        for statement in statements {
            self.statement(statement);
        }
    }

    fn statement(&mut self, statement: &Statement) {
        match &statement.kind {
            StatementKind::Declaration(declaration) => self.declaration(declaration),
            StatementKind::Binding(binding) => self.binding(binding),
            StatementKind::Assignment { target, value } => {
                self.expression(target);
                self.expression(value);
            }
            StatementKind::For { pattern, iterable, body } => {
                self.expression(iterable);
                self.scopes.push(Scope::new());
                self.pattern(pattern, Role::Variable, false);
                self.block_contents(body);
                self.scopes.pop();
            }
            StatementKind::While { condition, body } => self.condition_and_block(condition, body),
            StatementKind::Loop { body } => {
                self.scopes.push(Scope::new());
                self.block_contents(body);
                self.scopes.pop();
            }
            StatementKind::Return(Some(expression)) | StatementKind::Expression(expression) => self.expression(expression),
            StatementKind::Return(None) | StatementKind::Break | StatementKind::Continue => {}
        }
    }

    fn binding(&mut self, binding: &Binding) {
        self.expression(&binding.value);
        if let Some(annotation) = &binding.annotation {
            self.type_reference(annotation);
        }
        // A top-level binding was already declared as a global by `FileScope`; a local one needs a slot here.
        if !self.scopes.is_empty() {
            self.pattern(&binding.pattern, Role::Variable, binding.is_var);
        } else {
            self.pattern_tokens_only(&binding.pattern, Role::Variable, binding.is_var);
        }
    }

    /// A top-level binding's name is already a global; this only emits its tokens, without adding a duplicate local.
    fn pattern_tokens_only(&mut self, pattern: &Pattern, role: Role, is_var: bool) {
        self.scopes.push(Scope::new());
        self.pattern(pattern, role, is_var);
        self.scopes.pop();
    }

    fn condition_and_block(&mut self, condition: &Condition, body: &Block) {
        match condition {
            Condition::Expression(expression) => {
                self.expression(expression);
                self.block(body);
            }
            Condition::Binding { is_var, pattern, value } => {
                self.expression(value);
                self.scopes.push(Scope::new());
                self.pattern(pattern, Role::Variable, *is_var);
                self.block_contents(body);
                self.scopes.pop();
            }
        }
    }

    fn declaration(&mut self, declaration: &Declaration) {
        match &declaration.kind {
            // `use A, B from "..."` and `use Shape.Circle`: the imported names are colored where they are used
            // (`name_use`, `type_path`, `variant_pattern_path`), not at the `use` line itself - this tool does not
            // know their real kind (type, trait, function, ...) without opening the module they come from, except
            // for `use * as name`, which always declares a `namespace`.
            DeclarationKind::Use(UseDeclaration { items: UseItems::All { alias }, .. }) => {
                self.push(alias.span, "namespace", &["declaration"]);
            }
            DeclarationKind::Use(_) => {}
            // A `fn` reached through `statement()`/`declaration()` is always top-level or block-nested, never a
            // member of a `type`/`trait`/`extend` (those are parsed as `Member`s and go through `member()` instead).
            // It is a `function`, and - like a closure, but unlike a closure - it does not implicitly see `self`.
            DeclarationKind::Function(function) => {
                let outer_type = self.current_type.take();
                self.function_declaration(function, false, true);
                self.current_type = outer_type;
            }
            DeclarationKind::Type(type_declaration) => self.type_declaration(type_declaration),
            DeclarationKind::Alias(alias) => {
                self.push(alias.name.span, "type", &["declaration"]);
                self.with_generics(&alias.generics, |resolver| resolver.type_reference(&alias.target));
            }
            DeclarationKind::Trait(trait_declaration) => self.trait_declaration(trait_declaration),
            DeclarationKind::Extend(extend) => self.extend_declaration(extend),
            DeclarationKind::Foreign(foreign) => {
                for member in &foreign.members {
                    self.foreign_member(member);
                }
            }
            DeclarationKind::Constant(binding) => self.pattern_tokens_only_after_value(binding),
        }
    }

    fn pattern_tokens_only_after_value(&mut self, binding: &Binding) {
        self.expression(&binding.value);
        if let Some(annotation) = &binding.annotation {
            self.type_reference(annotation);
        }
        self.pattern_tokens_only(&binding.pattern, Role::Variable, binding.is_var);
    }

    fn foreign_member(&mut self, member: &Member) {
        match &member.kind {
            MemberKind::Function(function) => self.function_declaration(function, false, true),
            MemberKind::Field(field) => {
                self.push(field.name.span, "property", &declaration_modifiers(field.is_var));
                self.type_reference(&field.annotation);
            }
            MemberKind::Constant(binding) => self.pattern_tokens_only_after_value(binding),
            MemberKind::Case(_) => {}
        }
    }

    fn with_generics<T>(&mut self, generics: &[GenericParameter], body: impl FnOnce(&mut Self) -> T) -> T {
        let mut frame = HashMap::new();
        for parameter in generics {
            frame.insert(parameter.name.text.clone(), ());
        }
        self.generics.push(frame);
        for parameter in generics {
            self.push(parameter.name.span, "typeParameter", &["declaration"]);
            for bound in &parameter.bounds {
                self.type_reference(bound);
            }
            if let Some(default) = &parameter.default {
                self.type_reference(default);
            }
        }
        let result = body(self);
        self.generics.pop();
        result
    }

    fn where_clauses(&mut self, clauses: &[WhereClause]) {
        for clause in clauses {
            self.type_reference(&clause.subject);
            for bound in &clause.bounds {
                self.type_reference(bound);
            }
        }
    }

    fn type_declaration(&mut self, declaration: &TypeDeclaration) {
        self.push(declaration.name.span, "type", &["declaration"]);
        self.with_generics(&declaration.generics.clone(), |resolver| {
            for clause in &declaration.traits {
                resolver.trait_clause(clause);
            }
            resolver.where_clauses(&declaration.where_clauses);
            let scope = TypeScope { name: declaration.name.text.clone() };
            resolver.members(&declaration.members, scope);
        });
    }

    fn trait_declaration(&mut self, declaration: &TraitDeclaration) {
        self.push(declaration.name.span, "interface", &["declaration"]);
        self.with_generics(&declaration.generics.clone(), |resolver| {
            for supertrait in &declaration.supertraits {
                resolver.type_reference(supertrait);
            }
            let scope = TypeScope { name: declaration.name.text.clone() };
            resolver.members(&declaration.members, scope);
        });
    }

    fn extend_declaration(&mut self, declaration: &ExtendDeclaration) {
        self.with_generics(&declaration.generics.clone(), |resolver| {
            resolver.type_reference(&declaration.target);
            for trait_reference in &declaration.traits {
                resolver.type_reference(trait_reference);
            }
            resolver.where_clauses(&declaration.where_clauses);
            if let Some(name) = simple_type_name(&declaration.target) {
                let scope = TypeScope { name };
                resolver.members(&declaration.members, scope);
            }
        });
    }

    fn trait_clause(&mut self, clause: &TraitClause) {
        self.type_reference(&clause.capability);
        if let Some(delegate) = &clause.delegate {
            self.push(delegate.span, "property", &[]);
        }
    }

    fn members(&mut self, members: &[Member], scope: TypeScope) {
        let outer = self.current_type.replace(scope);
        for member in members {
            self.member(member);
        }
        self.current_type = outer;
    }

    fn member(&mut self, member: &Member) {
        match &member.kind {
            MemberKind::Field(field) => {
                self.push(field.name.span, "property", &declaration_modifiers(field.is_var));
                self.type_reference(&field.annotation);
                if let Some(default) = &field.default {
                    self.expression(default);
                }
            }
            MemberKind::Constant(binding) => {
                self.expression(&binding.value);
                if let Some(annotation) = &binding.annotation {
                    self.type_reference(annotation);
                }
                for (_, span) in pattern_names(&binding.pattern) {
                    self.push(span, "property", &["declaration", "readonly", "static"]);
                }
            }
            MemberKind::Function(function) => self.function_declaration(function, true, true),
            MemberKind::Case(case) => {
                self.push(case.name.span, "enumMember", &["declaration"]);
                for field in &case.fields {
                    self.push(field.name.span, "property", &declaration_modifiers(field.is_var));
                    self.type_reference(&field.annotation);
                    if let Some(default) = &field.default {
                        self.expression(default);
                    }
                }
            }
        }
    }

    /// `fresh_scope`: false only when this function's parameters must be added to an already-open scope stack (never
    /// the case today - every declaration site opens its own - kept for symmetry with `resolve`'s design doc).
    fn function_declaration(&mut self, function: &FunctionDeclaration, is_method: bool, fresh_scope: bool) {
        let self_parameter = function.parameters.first().filter(|parameter| parameter.name.text == "self");
        let mut modifiers = vec!["declaration"];
        if is_method && self_parameter.is_none() {
            modifiers.push("static");
        }
        // A `var fn` is underlined at its declaration as at every call of it
        if self_parameter.is_some_and(|parameter| parameter.is_var) {
            modifiers.push("mutable");
        }
        self.push(function.name.span, if is_method { "method" } else { "function" }, &modifiers);

        let saved_scopes = if fresh_scope { Some(std::mem::take(&mut self.scopes)) } else { None };
        let saved_self = std::mem::replace(&mut self.self_is_var, self_parameter.map(|parameter| parameter.is_var));
        self.scopes.push(Scope::new());
        self.with_generics(&function.generics.clone(), |resolver| {
            for parameter in &function.parameters {
                resolver.parameter(parameter);
            }
            if let Some(return_type) = &function.return_type {
                resolver.type_reference(return_type);
            }
            resolver.where_clauses(&function.where_clauses);
            if let Some(body) = &function.body {
                resolver.block_contents(body);
            }
        });
        self.scopes.pop();
        self.self_is_var = saved_self;
        if let Some(saved) = saved_scopes {
            self.scopes = saved;
        }
    }

    fn parameter(&mut self, parameter: &Parameter) {
        // The receiver of a method is not written, and its span is the name of the function, which carries the
        // method's own token. Only a written parameter gets one here.
        if parameter.name.text != "self" {
            self.push(parameter.name.span, "parameter", &declaration_modifiers(parameter.is_var));
        }
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(parameter.name.text.clone(), LocalSymbol { kind: LocalKind::Parameter, is_var: parameter.is_var });
        }
        if let Some(annotation) = &parameter.annotation {
            self.type_reference(annotation);
        }
        if let Some(default) = &parameter.default {
            self.expression(default);
        }
    }

    // --- Types --------------------------------------------------------------------------------------------------

    fn type_reference(&mut self, type_reference: &TypeReference) {
        match &type_reference.kind {
            TypeKind::Named { path, arguments } => {
                self.type_path(path);
                for argument in arguments {
                    self.type_reference(argument);
                }
            }
            TypeKind::Optional(inner) | TypeKind::Lazy(inner) => self.type_reference(inner),
            TypeKind::Tuple(fields) => {
                for field in fields {
                    if let Some(label) = &field.label {
                        self.push(label.span, "property", &["declaration"]);
                    }
                    self.type_reference(&field.annotation);
                }
            }
            TypeKind::Literals(literals) => {
                for literal in literals {
                    self.expression(literal);
                }
            }
            TypeKind::Intersection(members) => {
                for member in members {
                    self.type_reference(member);
                }
            }
            TypeKind::Function { parameters, result } => {
                for parameter in parameters {
                    if let Some(name) = &parameter.name {
                        self.push(name.span, "parameter", &["declaration"]);
                    }
                    self.type_reference(&parameter.annotation);
                }
                self.type_reference(result);
            }
            TypeKind::Error => {}
        }
    }

    /// `List<Int>`, `http.Response`, `Self`. `Self` is left to the TextMate grammar (`storage.type.self` there
    /// already), and so is a namespace's member (`http.Response`'s `Response`): this tool never opens `"std/http"`
    /// to learn what it exports.
    fn type_path(&mut self, path: &[Name]) {
        if path.is_empty() {
            return;
        }
        if path.len() == 1 {
            let name = &path[0];
            if name.text == "Self" {
                return;
            }
            if self.is_generic(&name.text) {
                self.push(name.span, "typeParameter", &[]);
                return;
            }
            if let Some(info) = self.file_scope.types.get(&name.text) {
                let kind = if info.kind == Some(GlobalKind::Trait) { "interface" } else { "type" };
                self.push(name.span, kind, &[]);
                return;
            }
            if PRIMITIVE_TYPES.contains(&name.text.as_str()) {
                self.push(name.span, "type", &["defaultLibrary"]);
                return;
            }
            if self.file_scope.imported_types.contains_key(&name.text) {
                self.push(name.span, "type", &[]);
            }
            return;
        }
        // A namespaced type (`http.Response`): color the namespace segment when it is known, leave the rest to
        // TextMate.
        if self.file_scope.namespaces.contains_key(&path[0].text) {
            self.push(path[0].span, "namespace", &[]);
        }
    }

    // --- Patterns -----------------------------------------------------------------------------------------------

    fn pattern(&mut self, pattern: &Pattern, role: Role, is_var: bool) {
        match &pattern.kind {
            PatternKind::Wildcard => {}
            PatternKind::Name(text) => {
                self.push(pattern.span, role.token_kind(), &declaration_modifiers(is_var));
                if let Some(scope) = self.scopes.last_mut() {
                    scope.insert(text.clone(), LocalSymbol { kind: role.local_kind(), is_var });
                }
            }
            PatternKind::Literal(expression) => self.expression(expression),
            PatternKind::Range { start, end, .. } => {
                self.expression(start);
                self.expression(end);
            }
            PatternKind::Tuple(items) => items.iter().for_each(|item| self.pattern(item, role, is_var)),
            PatternKind::List { items, rest } => {
                items.iter().for_each(|item| self.pattern(item, role, is_var));
                if let Some(RestPattern { name: Some(name), .. }) = rest {
                    self.push(name.span, role.token_kind(), &declaration_modifiers(is_var));
                    if let Some(scope) = self.scopes.last_mut() {
                        scope.insert(name.text.clone(), LocalSymbol { kind: role.local_kind(), is_var });
                    }
                }
            }
            PatternKind::Or(alternatives) => alternatives.iter().for_each(|item| self.pattern(item, role, is_var)),
            PatternKind::Variant { path, fields } => {
                self.variant_pattern_path(path);
                for field in fields {
                    if let Some(label) = &field.label {
                        self.push(label.span, "property", &[]);
                    }
                    self.pattern(&field.pattern, role, is_var);
                }
            }
            PatternKind::ImplicitVariant { name, fields } => {
                self.push(name.span, "enumMember", &self.prelude_modifiers(&name.text));
                for field in fields {
                    if let Some(label) = &field.label {
                        self.push(label.span, "property", &[]);
                    }
                    self.pattern(&field.pattern, role, is_var);
                }
            }
            PatternKind::Error => {}
        }
    }

    fn prelude_modifiers(&self, name: &str) -> Vec<&'static str> {
        if PRELUDE_CASES.contains(&name) {
            vec!["defaultLibrary"]
        } else {
            Vec::new()
        }
    }

    /// `value` (a plain destructure of a `type`'s fields, positional), `None` (an imported case), `Shape.Circle` (a
    /// case, always - a positional type destructure never has more than one path segment; see the module doc).
    fn variant_pattern_path(&mut self, path: &[Name]) {
        if path.len() > 1 {
            for name in &path[..path.len() - 1] {
                self.type_path(std::slice::from_ref(name));
            }
            let case = path.last().expect("checked len > 1");
            self.push(case.span, "enumMember", &[]);
            return;
        }
        let name = &path[0];
        if self.is_known_case(&name.text) {
            self.push(name.span, "enumMember", &self.prelude_modifiers(&name.text));
        } else if self.is_known_type(&name.text) {
            self.push(name.span, "type", &[]);
        }
        // Otherwise: an uppercase name this tool cannot place without opening another file. No token - the
        // TextMate grammar already colors a capitalized bare word as a type, which is the more common case.
    }

    // --- Expressions ----------------------------------------------------------------------------------------------

    fn expression(&mut self, expression: &Expression) {
        match &expression.kind {
            ExpressionKind::Integer(_)
            | ExpressionKind::Float(_)
            | ExpressionKind::Bool(_)
            | ExpressionKind::VoidLiteral
            | ExpressionKind::Char(_)
            | ExpressionKind::Error => {}
            ExpressionKind::Text(segments) => {
                for segment in segments {
                    if let TextSegment::Expression(inner) = segment {
                        self.expression(inner);
                    }
                }
            }
            ExpressionKind::Name(text) => self.name_use(text, expression.span),
            ExpressionKind::ImplicitMember(name) => self.push(name.span, "enumMember", &self.prelude_modifiers(&name.text)),
            ExpressionKind::Generic { target, arguments } => {
                self.expression(target);
                for argument in arguments {
                    self.type_reference(argument);
                }
            }
            ExpressionKind::Tuple(arguments) | ExpressionKind::List(arguments) => {
                for argument in arguments {
                    self.tuple_or_list_argument(argument);
                }
            }
            ExpressionKind::Map(entries) => {
                for (key, value) in entries {
                    self.expression(key);
                    self.expression(value);
                }
            }
            ExpressionKind::Member { target, name, .. } => self.member_access(target, name, false),
            ExpressionKind::Index { target, index } => {
                self.expression(target);
                self.expression(index);
            }
            ExpressionKind::Call { callee, arguments, .. } => self.call(callee, arguments),
            ExpressionKind::Unary { operand, .. } => self.expression(operand),
            ExpressionKind::Binary { left, right, .. } => {
                self.expression(left);
                self.expression(right);
            }
            ExpressionKind::Range { start, end, .. } => {
                if let Some(start) = start {
                    self.expression(start);
                }
                if let Some(end) = end {
                    self.expression(end);
                }
            }
            ExpressionKind::Try(inner) => self.expression(inner),
            ExpressionKind::Closure(closure) => self.closure(closure),
            ExpressionKind::If { condition, then, otherwise } => {
                self.condition_and_block(condition, then);
                if let Some(otherwise) = otherwise {
                    self.expression(otherwise);
                }
            }
            ExpressionKind::Match { subject, arms } => {
                self.expression(subject);
                for arm in arms {
                    self.scopes.push(Scope::new());
                    self.pattern(&arm.pattern, Role::Variable, false);
                    if let Some(guard) = &arm.guard {
                        self.expression(guard);
                    }
                    self.expression(&arm.body);
                    self.scopes.pop();
                }
            }
            ExpressionKind::Block(block) => self.block(block),
        }
    }

    fn tuple_or_list_argument(&mut self, argument: &Argument) {
        if let Some(label) = &argument.label {
            self.push(label.span, "property", &[]);
        }
        self.expression(&argument.value);
    }

    fn closure(&mut self, closure: &Closure) {
        self.scopes.push(Scope::new());
        for parameter in &closure.parameters {
            self.pattern(&parameter.pattern, Role::Parameter, false);
            if let Some(annotation) = &parameter.annotation {
                self.type_reference(annotation);
            }
        }
        self.block_contents(&closure.body);
        self.scopes.pop();
    }

    fn block(&mut self, block: &Block) {
        self.scopes.push(Scope::new());
        self.block_contents(block);
        self.scopes.pop();
    }

    /// Like `block`, but without opening a new scope: used where the scope is already open (a function's own
    /// parameters and body share one frame, and so do a `for`/`if const`'s pattern and its body).
    fn block_contents(&mut self, block: &Block) {
        self.statements(&block.statements);
    }

    /// A bare name in expression position: `_` (implicit closure parameter), a local, a field of `self`, a case, a
    /// type used as a value (a constructor reference, `Point`), a function, or nothing this tool can place.
    fn name_use(&mut self, text: &str, span: Span) {
        if text == "self" {
            // Colored only for its `mutable` modifier (`var self`); a plain `self` is left to the TextMate
            // grammar's own `variable.language.self` scope, so it does not fight the semantic legend's default.
            if let Some(true) = self.self_is_var {
                self.push(span, "parameter", &["mutable"]);
            }
            return;
        }
        if text == "Self" {
            return; // Left to the TextMate grammar's own `support.type.self` scope.
        }
        if text.starts_with('_') && text[1..].chars().all(|character| character.is_ascii_digit()) {
            self.push(span, "parameter", &[]);
            return;
        }
        if self.is_generic(text) {
            self.push(span, "typeParameter", &[]);
            return;
        }
        if let Some(local) = self.find_local(text) {
            match local.kind {
                LocalKind::Function => self.push(span, "function", &[]),
                LocalKind::Variable => self.push(span, "variable", &readonly_modifiers(local.is_var)),
                LocalKind::Parameter => self.push(span, "parameter", &readonly_modifiers(local.is_var)),
            }
            return;
        }
        if let Some(info) = self.current_type_info() {
            if let Some(field) = info.fields.get(text) {
                self.push(span, "property", &field_modifiers(field));
                return;
            }
            if let Some(method) = info.methods.get(text) {
                let modifiers = method_modifiers(method);
                self.push(span, "method", &modifiers);
                return;
            }
        }
        if starts_upper_case(text) {
            if self.is_known_case(text) {
                self.push(span, "enumMember", &self.prelude_modifiers(text));
            } else if self.is_known_type(text) {
                self.push(span, "type", &[]);
            }
            return;
        }
        if self.file_scope.functions.contains_key(text) || self.file_scope.imported_functions.contains_key(text) {
            self.push(span, "function", &[]);
            return;
        }
        if self.file_scope.namespaces.contains_key(text) {
            self.push(span, "namespace", &[]);
            return;
        }
        if let Some(is_var) = self.file_scope.globals.get(text) {
            self.push(span, "variable", &readonly_modifiers(*is_var));
        }
    }

    /// The target of a `Member`/`Call`, classified so that the dotted name can be resolved against what that target
    /// actually declares, when this tool can tell.
    fn member_target_kind(&self, target: &Expression) -> MemberTargetKind {
        if let ExpressionKind::Name(text) = &target.kind {
            if text == "self" || text == "Self" {
                if let Some(current) = &self.current_type {
                    return MemberTargetKind::Type(current.name.clone());
                }
            }
            if self.find_local(text).is_none() {
                if self.file_scope.types.contains_key(text) {
                    return MemberTargetKind::Type(text.clone());
                }
                if self.file_scope.namespaces.contains_key(text) {
                    return MemberTargetKind::Namespace;
                }
            }
        }
        MemberTargetKind::Other
    }

    fn member_access(&mut self, target: &Expression, name: &Name, called: bool) {
        self.expression(target);
        match self.member_target_kind(target) {
            MemberTargetKind::Type(type_name) => {
                if let Some(info) = self.file_scope.types.get(&type_name) {
                    if let Some(field) = info.fields.get(&name.text) {
                        self.push(name.span, "property", &field_modifiers(field));
                        return;
                    }
                    if let Some(method) = info.methods.get(&name.text) {
                        let modifiers = method_modifiers(method);
                        self.push(name.span, "method", &modifiers);
                        return;
                    }
                    if info.cases.contains_key(&name.text) {
                        self.push(name.span, "enumMember", &[]);
                        return;
                    }
                }
                if called {
                    self.push(name.span, "method", &[]);
                } else if PRELUDE_CASES.contains(&name.text.as_str()) {
                    self.push(name.span, "enumMember", &["defaultLibrary"]);
                }
            }
            MemberTargetKind::Namespace => {
                if called {
                    self.push(name.span, "function", &[]);
                }
            }
            MemberTargetKind::Other => {
                // The receiver has no type here, so only a name that is a `var fn` wherever this file declares it
                // is underlined - see `FileScope::changing_methods`
                let changes = called && self.file_scope.changing_methods.get(&name.text) == Some(&true);
                let modifiers: &[&str] = if changes { &["mutable"] } else { &[] };
                self.push(name.span, if called { "method" } else { "property" }, modifiers);
            }
        }
    }

    fn call(&mut self, callee: &Expression, arguments: &[Argument]) {
        let is_constructor = match &callee.kind {
            ExpressionKind::Member { target, name, .. } => {
                self.member_access(target, name, true);
                false
            }
            ExpressionKind::Name(text) => self.call_name(text, callee.span),
            _ => {
                self.expression(callee);
                false
            }
        };
        for argument in arguments {
            if let Some(label) = &argument.label {
                self.push(label.span, if is_constructor { "property" } else { "parameter" }, &[]);
            }
            self.expression(&argument.value);
        }
    }

    /// A bare call `name(...)`/`name arg`: a local (a stored closure), a method (implicit `self`, if the enclosing
    /// type declares one), a function, or a type used as a constructor (`Point(1, 2)`, `Point 1, 2`). Returns
    /// whether it is a constructor call, so the caller can color labeled arguments as fields instead of parameters.
    fn call_name(&mut self, text: &str, span: Span) -> bool {
        if let Some(local) = self.find_local(text) {
            match local.kind {
                LocalKind::Function => self.push(span, "function", &[]),
                LocalKind::Variable => self.push(span, "variable", &readonly_modifiers(local.is_var)),
                LocalKind::Parameter => self.push(span, "parameter", &readonly_modifiers(local.is_var)),
            }
            return false;
        }
        if let Some(info) = self.current_type_info() {
            if let Some(method) = info.methods.get(text) {
                let modifiers = method_modifiers(method);
                self.push(span, "method", &modifiers);
                return false;
            }
        }
        if starts_upper_case(text) {
            if self.is_known_case(text) {
                self.push(span, "enumMember", &self.prelude_modifiers(text));
                return false;
            }
            if self.is_known_type(text) {
                self.push(span, "type", &[]);
                return true;
            }
            return false;
        }
        if self.file_scope.functions.contains_key(text) || self.file_scope.imported_functions.contains_key(text) {
            self.push(span, "function", &[]);
            return false;
        }
        if let Some(is_var) = self.file_scope.globals.get(text) {
            self.push(span, "variable", &readonly_modifiers(*is_var));
        }
        false
    }
}

enum MemberTargetKind {
    Type(String),
    Namespace,
    Other,
}

fn declaration_modifiers(is_var: bool) -> Vec<&'static str> {
    let mut modifiers = vec!["declaration"];
    modifiers.push(if is_var { "mutable" } else { "readonly" });
    modifiers
}

/// What a call site of a member says about it: `static` for one that belongs to the type, `mutable` for a `var fn`,
/// so that mutation is visible without reading the signature - exactly as it is on a `var` field.
fn method_modifiers(method: &MethodSymbol) -> Vec<&'static str> {
    if !method.has_self {
        return vec!["static"];
    }
    match method.changes_the_receiver {
        true => vec!["mutable"],
        false => Vec::new(),
    }
}

/// What a use of a field says about it: `static` on top for a value that belongs to the type.
fn field_modifiers(field: &FieldSymbol) -> Vec<&'static str> {
    let mut modifiers = readonly_modifiers(field.is_var);
    if field.is_static {
        modifiers.push("static");
    }
    modifiers
}

fn readonly_modifiers(is_var: bool) -> Vec<&'static str> {
    vec![if is_var { "mutable" } else { "readonly" }]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug, Clone, PartialEq, Eq)]
    struct Found {
        text: String,
        kind: &'static str,
        modifiers: Vec<&'static str>,
    }

    fn tokens(source: &str) -> Vec<Found> {
        let parsed = torb_syntax::parse(source);
        resolve(&parsed.file)
            .into_iter()
            .map(|token| Found { text: source[token.span.range()].to_string(), kind: token.kind, modifiers: token.modifiers })
            .collect()
    }

    /// The `nth` (0-based) token whose text is exactly `text` - source snippets below reuse identifiers on purpose
    /// (a declaration and its uses), so most assertions ask for an occurrence, not just "the first one".
    fn nth<'a>(tokens: &'a [Found], text: &str, index: usize) -> &'a Found {
        tokens.iter().filter(|found| found.text == text).nth(index).unwrap_or_else(|| {
            panic!("no occurrence #{index} of `{text}` among {tokens:#?}");
        })
    }

    fn only<'a>(tokens: &'a [Found], text: &str) -> &'a Found {
        let matches: Vec<_> = tokens.iter().filter(|found| found.text == text).collect();
        assert_eq!(matches.len(), 1, "expected exactly one `{text}`, found {matches:#?} among {tokens:#?}");
        matches[0]
    }

    fn absent(tokens: &[Found], text: &str) {
        assert!(tokens.iter().all(|found| found.text != text), "expected no token for `{text}` among {tokens:#?}");
    }

    // --- Generic parameters --------------------------------------------------------------------------------------

    #[test]
    fn generic_parameters_are_typed_at_declaration_and_every_use_including_const_generics() {
        let found = tokens("type Array<Item, const Size: Int> {\n  fn first(): Item { .Empty }\n  fn size(): Int { Size }\n}\n");
        assert_eq!(nth(&found, "Item", 0).kind, "typeParameter");
        assert_eq!(nth(&found, "Item", 0).modifiers, vec!["declaration"]);
        assert_eq!(nth(&found, "Item", 1).kind, "typeParameter"); // the return type of `first`
        assert_eq!(nth(&found, "Size", 0).kind, "typeParameter"); // the const parameter's own declaration
        assert_eq!(nth(&found, "Size", 0).modifiers, vec!["declaration"]);
        assert_eq!(nth(&found, "Size", 1).kind, "typeParameter"); // used as an ordinary expression value
    }

    #[test]
    fn a_functions_own_generic_parameter_is_typed_too() {
        let found = tokens("fn identity<Value>(value: Value): Value {\n  value\n}\n");
        // `Value` appears three times: its own declaration, the parameter's annotation, the return type.
        assert_eq!(nth(&found, "Value", 0).kind, "typeParameter");
        assert_eq!(nth(&found, "Value", 1).kind, "typeParameter");
        assert_eq!(nth(&found, "Value", 2).kind, "typeParameter");
    }

    // --- Types, traits, cases --------------------------------------------------------------------------------------

    #[test]
    fn a_type_declaration_and_a_trait_declaration_are_colored_apart() {
        let found = tokens("type Shape {}\ntrait Show {}\n");
        assert_eq!(only(&found, "Shape").kind, "type");
        assert_eq!(only(&found, "Show").kind, "interface");
    }

    #[test]
    fn cases_are_their_own_kind_in_every_written_form() {
        let source = "\
type Shape {
  case Circle(radius: Float)

  fn area(): Float {
    match self {
      .Circle(radius) => radius
    }
  }
}
const unit = Shape.Circle(1.0)
const other: Shape = .Circle(2.0)
";
        let found = tokens(source);
        assert_eq!(nth(&found, "Circle", 0).kind, "enumMember"); // `case Circle(...)`
        assert_eq!(nth(&found, "Circle", 0).modifiers, vec!["declaration"]);
        assert_eq!(nth(&found, "Circle", 1).kind, "enumMember"); // `.Circle(radius)` in the match arm
        assert_eq!(nth(&found, "Circle", 2).kind, "enumMember"); // `Shape.Circle(1.0)`
        assert_eq!(nth(&found, "Shape", 1).kind, "type"); // the `Shape` in `Shape.Circle`
        assert_eq!(nth(&found, "Circle", 3).kind, "enumMember"); // `.Circle(2.0)`, the expected-type short form
    }

    #[test]
    fn prelude_cases_are_bare_everywhere_with_a_default_library_modifier() {
        let found = tokens("fn parse(text: String): Result<Int, String> {\n  match Int.tryParse(text) {\n    Some(value) => Ok(value)\n    None => Fail(\"bad\")\n  }\n}\n");
        for (name, occurrence) in [("Some", 0), ("Ok", 0), ("None", 0), ("Fail", 0)] {
            let found_token = nth(&found, name, occurrence);
            assert_eq!(found_token.kind, "enumMember", "{name}");
            assert!(found_token.modifiers.contains(&"defaultLibrary"), "{name}: {found_token:?}");
        }
    }

    #[test]
    fn a_type_used_as_a_constructor_stays_a_type_in_expression_and_pattern() {
        let source = "\
type Point {
  x: Int
  y: Int
}
const p = Point(1, 2)
const q = Point 1, 2
const Point(a, b) = q
";
        let found = tokens(source);
        assert_eq!(nth(&found, "Point", 0).kind, "type"); // declaration
        assert_eq!(nth(&found, "Point", 1).kind, "type"); // `Point(1, 2)`
        assert_eq!(nth(&found, "Point", 2).kind, "type"); // `Point 1, 2` (command style)
        assert_eq!(nth(&found, "Point", 3).kind, "type"); // `Point(a, b)` pattern (positional destructure)
        assert_eq!(only(&found, "a").kind, "variable"); // the bound names in the destructure - `variable`, not `type`
        assert_eq!(only(&found, "b").kind, "variable");
    }

    /// A known, documented blind spot (see the module doc comment): nothing in this file says whether a name is a
    /// case or a type read backwards without a `use` or a local declaration to check against (that needs opening
    /// another file, which this tool never does). Such a name gets no token at all - never a guess that could be
    /// wrong - and the TextMate grammar's fallback (which colors a bare capitalized word as a type) is what shows.
    #[test]
    fn an_unresolvable_uppercase_pattern_name_gets_no_token() {
        let found = tokens("fn run(value: Something) {\n  match value {\n    NotImported(x) => x\n  }\n}\n");
        absent(&found, "NotImported");
    }

    // --- Command style vs. call style, function vs. method -------------------------------------------------------

    #[test]
    fn command_style_and_call_style_produce_the_same_token() {
        let found = tokens("fn greet(name: String): String {\n  \"hi\"\n}\nconst a = greet(\"x\")\nconst b = greet \"y\"\n");
        assert_eq!(nth(&found, "greet", 1).kind, "function");
        assert_eq!(nth(&found, "greet", 2).kind, "function");
        assert_eq!(nth(&found, "greet", 1).modifiers, nth(&found, "greet", 2).modifiers);
    }

    #[test]
    fn a_dotted_call_is_a_method_a_bare_call_is_a_function() {
        let source = "\
type Greeter {
  fn greet(): String { \"hi\" }
}
fn greet(): String { \"free\" }
const g = Greeter()
const a = g.greet()
const b = greet()
";
        let found = tokens(source);
        assert_eq!(nth(&found, "greet", 0).kind, "method"); // declaration inside the type
        assert_eq!(nth(&found, "greet", 1).kind, "function"); // top-level declaration
        assert_eq!(nth(&found, "greet", 2).kind, "method"); // `g.greet()`
        assert_eq!(nth(&found, "greet", 3).kind, "function"); // bare `greet()`
    }

    #[test]
    fn a_bare_call_that_matches_a_method_of_the_enclosing_type_is_a_method() {
        let source = "\
type Shape {
  fn area(): Float { 1.0 }
  fn describe(): String {
    area()
    \"shape\"
  }
}
";
        let found = tokens(source);
        assert_eq!(nth(&found, "area", 1).kind, "method"); // the implicit-self call inside `describe`
    }

    #[test]
    fn a_static_member_is_marked_static_and_a_var_fn_is_marked_mutable() {
        let found = tokens("type Point {\n  static fn origin(): Point { Point(0, 0) }\n  x: Int\n  y: Int\n}\n");
        assert!(only(&found, "origin").modifiers.contains(&"static"));

        // A `var fn` is underlined like a `var` field, at its declaration and at every call of it
        let source =
            "type Counter {\n  var count: Int = 0\n  var fn grow() { count = count + 1 }\n}\nfn step(var c: Counter) {\n  c.grow()\n}\n";
        let found = tokens(source);
        assert!(nth(&found, "grow", 0).modifiers.contains(&"mutable"));
        assert!(nth(&found, "grow", 1).modifiers.contains(&"mutable"));
    }

    #[test]
    fn a_static_value_is_a_static_property_where_it_is_declared_and_where_it_is_read() {
        let source = "type Point {
  x: Int
  static origin = Point(0)
  fn isOrigin(): Bool { x == origin.x }
}
print Point.origin
";
        let found = tokens(source);
        for index in 0..3 {
            let token = nth(&found, "origin", index);
            assert_eq!(token.kind, "property");
            assert!(token.modifiers.contains(&"static") && token.modifiers.contains(&"readonly"), "{token:#?}");
        }
        assert!(nth(&found, "origin", 0).modifiers.contains(&"declaration"));
        assert!(!nth(&found, "x", 0).modifiers.contains(&"static"));
    }

    // --- Parameters, locals, fields -----------------------------------------------------------------------------

    #[test]
    fn parameters_are_typed_at_declaration_and_every_use_distinct_from_locals() {
        let found = tokens("fn add(a: Int, b: Int): Int {\n  const total = a + b\n  total\n}\n");
        assert_eq!(nth(&found, "a", 0).kind, "parameter");
        assert_eq!(nth(&found, "a", 1).kind, "parameter");
        assert_eq!(nth(&found, "total", 0).kind, "variable"); // declaration
        assert_eq!(nth(&found, "total", 1).kind, "variable"); // use
    }

    #[test]
    fn a_field_used_bare_inside_a_method_of_its_type_is_a_property() {
        let source = "\
type Counter {
  var sent: Int = 0

  var fn increment() {
    sent = sent + 1
  }
}
";
        let found = tokens(source);
        assert_eq!(nth(&found, "sent", 0).kind, "property"); // the field declaration
        assert!(nth(&found, "sent", 0).modifiers.contains(&"mutable"));
        assert_eq!(nth(&found, "sent", 1).kind, "property"); // assignment target
        assert!(nth(&found, "sent", 1).modifiers.contains(&"mutable"));
        assert_eq!(nth(&found, "sent", 2).kind, "property"); // right-hand side use
    }

    #[test]
    fn field_access_through_a_value_is_a_property_distinct_from_a_parameter() {
        let found = tokens("type Point {\n  x: Int\n}\nfn getX(point: Point): Int {\n  point.x\n}\n");
        assert_eq!(nth(&found, "point", 0).kind, "parameter"); // declaration
        assert_eq!(nth(&found, "point", 1).kind, "parameter"); // `point.x`'s receiver
        assert_eq!(nth(&found, "x", 0).kind, "property"); // the field declaration
        assert_eq!(nth(&found, "x", 1).kind, "property"); // `point.x`
    }

    #[test]
    fn var_and_const_bindings_share_a_kind_and_differ_only_by_modifier() {
        let found = tokens("const a = 1\nvar b = 2\n");
        assert_eq!(only(&found, "a").kind, "variable");
        assert_eq!(only(&found, "b").kind, "variable");
        assert!(only(&found, "a").modifiers.contains(&"readonly"));
        assert!(!only(&found, "a").modifiers.contains(&"mutable"));
        assert!(only(&found, "b").modifiers.contains(&"mutable"));
        assert!(!only(&found, "b").modifiers.contains(&"readonly"));
    }

    #[test]
    fn implicit_closure_parameters_are_parameters() {
        let found = tokens("fn run(): Int {\n  [1, 2].map { _ + 1 }.length()\n}\n");
        assert_eq!(only(&found, "_").kind, "parameter");
    }

    // --- Scoping: shadowing, closures, nested `fn`, patterns --------------------------------------------------------

    #[test]
    fn a_closure_parameter_shadows_an_outer_local() {
        let found = tokens("fn run(): Int {\n  const x = 1\n  [1, 2].map { x => x + 1 }.length()\n}\n");
        assert_eq!(nth(&found, "x", 0).kind, "variable"); // the outer `const x`
        assert_eq!(nth(&found, "x", 1).kind, "parameter"); // the closure parameter
        assert_eq!(nth(&found, "x", 2).kind, "parameter"); // used inside the closure, sees the shadowing parameter
    }

    #[test]
    fn a_nested_fn_does_not_see_the_locals_around_it() {
        let found = tokens("fn outer(): Int {\n  const x = 1\n  fn inner(): Int {\n    x\n  }\n  inner()\n}\n");
        // `x` inside `inner` cannot resolve to `outer`'s local: it gets no token at all (not even a wrong one).
        assert_eq!(nth(&found, "x", 0).kind, "variable");
        assert_eq!(only(&found, "x"), nth(&found, "x", 0)); // there is no second, wrongly-resolved `x`
        assert_eq!(nth(&found, "inner", 0).kind, "function"); // declaration
        assert_eq!(nth(&found, "inner", 1).kind, "function"); // the call at the end of `outer`
    }

    #[test]
    fn for_loop_bindings_are_variables_scoped_to_the_loop_body() {
        let found = tokens("fn run(list: List<Int>) {\n  for item in list {\n    item\n  }\n}\n");
        assert_eq!(nth(&found, "item", 0).kind, "variable");
        assert_eq!(nth(&found, "item", 1).kind, "variable");
    }

    #[test]
    fn if_const_binds_only_inside_the_then_block() {
        let found = tokens("fn run(value: Int?) {\n  if const Some(found) = value {\n    found\n  }\n}\n");
        assert_eq!(nth(&found, "found", 0).kind, "variable");
        assert_eq!(nth(&found, "found", 0).modifiers, vec!["declaration", "readonly"]);
        assert_eq!(nth(&found, "found", 1).kind, "variable");
    }

    #[test]
    fn if_var_marks_the_bound_place_mutable() {
        let found = tokens("fn run(value: Int?) {\n  if var Some(found) = value {\n    found\n  }\n}\n");
        assert!(nth(&found, "found", 0).modifiers.contains(&"mutable"));
    }

    #[test]
    fn match_arm_bindings_are_scoped_to_their_own_arm() {
        let source = "\
fn describe(shape: Shape): String {
  match shape {
    .Circle(radius) => \"r={radius}\"
    .Empty => \"empty\"
  }
}
";
        let found = tokens(source);
        // `radius` appears twice: the arm's bound pattern name, and its use in the interpolated string.
        assert_eq!(nth(&found, "radius", 0).kind, "variable");
        assert_eq!(nth(&found, "radius", 1).kind, "variable");
    }

    #[test]
    fn a_case_pattern_field_label_is_a_property() {
        let found = tokens("fn run(shape: Shape) {\n  match shape {\n    Point(x: a, y: b) => a\n  }\n}\n");
        assert_eq!(only(&found, "x").kind, "property");
        assert_eq!(only(&found, "y").kind, "property");
        // `a` appears twice: the arm's bound pattern name, and its use as the arm's body.
        assert_eq!(nth(&found, "a", 0).kind, "variable");
        assert_eq!(nth(&found, "a", 1).kind, "variable");
    }

    // --- Labeled arguments: constructor (field) vs. function call (parameter) --------------------------------------

    #[test]
    fn a_labeled_argument_of_a_constructor_call_is_a_property_of_a_function_call_a_parameter() {
        let source = "\
type Point {
  x: Int
  y: Int
}
fn move(to: Point) {}
const p = Point(x: 1, y: 2)
move(to: p)
";
        let found = tokens(source);
        // field declaration and the constructor's label share the name `x`, both `property`.
        assert_eq!(nth(&found, "x", 0).kind, "property");
        assert_eq!(nth(&found, "x", 1).kind, "property");
        assert_eq!(nth(&found, "to", 0).kind, "parameter"); // the parameter declaration of `move`
        assert_eq!(nth(&found, "to", 1).kind, "parameter"); // the label at the call site
    }

    // --- `extend`, traits with bounds, `with ... by` delegation ---------------------------------------------------

    #[test]
    fn an_extend_method_is_a_method_of_the_type_it_targets() {
        let source = "\
trait Show {
  fn show(): String
}
type Point {
  x: Int
}
extend Point with Show {
  fn show(): String {
    \"Point\"
  }
}
fn describe(point: Point): String {
  point.show()
}
";
        let found = tokens(source);
        assert_eq!(nth(&found, "Show", 0).kind, "interface"); // the trait's own declaration
        assert_eq!(nth(&found, "Show", 1).kind, "interface"); // its use in `extend Point with Show`
                                                              // `show` is declared inside `extend Point`, so a value of type `Point` resolves it as a method too.
        assert_eq!(nth(&found, "show", 0).kind, "method"); // the trait's required method
        assert_eq!(nth(&found, "show", 1).kind, "method"); // the `extend`'s implementation
        assert_eq!(nth(&found, "show", 2).kind, "method"); // `point.show()`
    }

    #[test]
    fn a_delegate_named_by_with_by_is_a_property() {
        let found = tokens("trait Show {\n  fn show(): String\n}\ntype Wrapper {\n  inner: String\n}\ntype Boxed with Show by inner {\n  inner: String\n}\n");
        assert_eq!(nth(&found, "inner", 1).kind, "property"); // the delegate name in `by inner`
    }

    // --- Namespaces --------------------------------------------------------------------------------------------

    #[test]
    fn a_use_star_as_alias_is_a_namespace() {
        let found = tokens("use * as http from \"std/http\"\nconst response = http.get(\"/\")\n");
        assert_eq!(nth(&found, "http", 0).kind, "namespace"); // the `use * as http` declaration itself
        assert_eq!(nth(&found, "http", 0).modifiers, vec!["declaration"]);
        assert_eq!(nth(&found, "http", 1).kind, "namespace"); // `http.get(...)`
    }

    #[test]
    fn a_namespaced_call_is_a_function_not_a_method() {
        let found = tokens("use * as http from \"std/http\"\nconst response = http.get(\"/\")\n");
        assert_eq!(only(&found, "get").kind, "function");
    }
}
