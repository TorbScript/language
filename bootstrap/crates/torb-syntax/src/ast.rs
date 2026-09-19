//! The syntax tree. It mirrors the source: nothing is resolved, desugared or checked here.

use crate::span::Span;

#[derive(Debug, Clone, PartialEq)]
pub struct Name {
    pub text: String,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub struct File {
    pub statements: Vec<Statement>,
}

// --- Declarations ---------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Visibility {
    /// Members: public. Top-level declarations: private to the file.
    #[default]
    Default,
    Public,
    Private,
    /// `private(var)`: everybody reads, only the type writes.
    PrivateVar,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Modifiers {
    pub visibility: Visibility,
    pub native: bool,
    pub shared: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Declaration {
    /// The `/** ... */` in front of the declaration
    pub doc: Option<String>,
    pub modifiers: Modifiers,
    pub kind: DeclarationKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum DeclarationKind {
    Use(UseDeclaration),
    Function(FunctionDeclaration),
    Type(TypeDeclaration),
    Alias(AliasDeclaration),
    Trait(TraitDeclaration),
    Extend(ExtendDeclaration),
    Foreign(ForeignDeclaration),
    /// `public const pi = 3.14` at the top level of a module. `public var` is an error: a module has no mutable
    /// state. `Binding::doc` is always `None` here, the doc comment is `Declaration::doc`.
    Constant(Binding),
}

#[derive(Debug, Clone, PartialEq)]
pub struct UseDeclaration {
    pub items: UseItems,
    pub source: UseSource,
}

#[derive(Debug, Clone, PartialEq)]
pub enum UseSource {
    /// `from "./file"`, `from "std/fs"`
    Module(String),
    /// `use Shape.Circle`: no `from`, so every path is resolved in the file's own scope
    Local,
}

/// One item of a `use` list: a path and the local name it gets (`Option.Some as Just`). The last segment is what is
/// imported, the ones before it the type it is a case of. Every name carries its own span, so a message about the
/// export points at the export and one about the local name at the alias.
#[derive(Debug, Clone, PartialEq)]
pub struct UseItem {
    pub path: Vec<Name>,
    pub alias: Option<Name>,
}

impl UseItem {
    /// The name that is imported: the last segment of the path. Empty only after a parse error.
    pub fn name(&self) -> &Name {
        self.path.last().expect("a use item always has at least one segment")
    }

    /// The name the importing file sees. Everything that is about the *export* keeps naming `name()` instead.
    pub fn local_name(&self) -> &Name {
        self.alias.as_ref().unwrap_or_else(|| self.name())
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum UseItems {
    Names(Vec<UseItem>),
    /// `use * as http from "..."`
    All {
        alias: Name,
    },
    /// `use "./text-extensions"`: only what the module adds to types with `extend`
    OnlyExtensions,
}

#[derive(Debug, Clone, PartialEq)]
pub struct GenericParameter {
    /// `<const Length: Int>`: a value instead of a type. Its type is the only item of `bounds`.
    pub is_const: bool,
    pub name: Name,
    pub bounds: Vec<TypeReference>,
    pub default: Option<TypeReference>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct WhereClause {
    pub subject: TypeReference,
    pub bounds: Vec<TypeReference>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct FunctionDeclaration {
    pub name: Name,
    pub generics: Vec<GenericParameter>,
    pub parameters: Vec<Parameter>,
    pub return_type: Option<TypeReference>,
    pub where_clauses: Vec<WhereClause>,
    /// `None` for `native` functions and for requirements of a trait.
    pub body: Option<Block>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Parameter {
    pub doc: Option<String>,
    pub name: Name,
    pub is_var: bool,
    /// `...items: Item`
    pub is_variadic: bool,
    /// `self` and `var self` have no annotation.
    pub annotation: Option<TypeReference>,
    pub default: Option<Expression>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TypeDeclaration {
    pub name: Name,
    pub generics: Vec<GenericParameter>,
    pub traits: Vec<TraitClause>,
    pub where_clauses: Vec<WhereClause>,
    pub members: Vec<Member>,
}

/// One element of a `with` list: `Add & Subtract by value` is one clause, its capability an intersection of two
/// traits. `by` binds to this element alone, not to the list (`with Show, Add & Subtract by value, Compare by value`
/// derives `Show` and delegates the other two, each to `value`).
#[derive(Debug, Clone, PartialEq)]
pub struct TraitClause {
    pub capability: TypeReference,
    pub delegate: Option<Name>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct AliasDeclaration {
    pub name: Name,
    pub generics: Vec<GenericParameter>,
    pub target: TypeReference,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TraitDeclaration {
    pub name: Name,
    pub generics: Vec<GenericParameter>,
    pub supertraits: Vec<TypeReference>,
    pub members: Vec<Member>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExtendDeclaration {
    pub generics: Vec<GenericParameter>,
    pub target: TypeReference,
    pub traits: Vec<TypeReference>,
    pub where_clauses: Vec<WhereClause>,
    pub members: Vec<Member>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ForeignDeclaration {
    pub library: String,
    pub members: Vec<Member>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Member {
    pub doc: Option<String>,
    pub modifiers: Modifiers,
    pub kind: MemberKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum MemberKind {
    Field(Field),
    Constant(Binding),
    Function(FunctionDeclaration),
    Case(Case),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Field {
    /// Only for the fields of a case. The doc comment of a field of a type belongs to its `Member`.
    pub doc: Option<String>,
    pub name: Name,
    pub is_var: bool,
    pub annotation: TypeReference,
    pub default: Option<Expression>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Case {
    pub name: Name,
    pub fields: Vec<Field>,
}

// --- Types ----------------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
pub struct TypeReference {
    pub kind: TypeKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TypeKind {
    /// `List<Item>`, `http.Response`, `Self`
    Named {
        path: Vec<Name>,
        arguments: Vec<TypeReference>,
    },
    /// `Item?`
    Optional(Box<TypeReference>),
    /// `(Int, String)`, `(lowest: Int, highest: Int)`
    Tuple(Vec<TupleTypeField>),
    /// `"online" | "offline"`. A single literal is also a const argument: `Array<Float, 16>`
    Literals(Vec<Expression>),
    /// `Show & Encode`: a value that implements all of these traits
    Intersection(Vec<TypeReference>),
    Function {
        parameters: Vec<FunctionTypeParameter>,
        result: Box<TypeReference>,
    },
    /// `lazy Value`, only as the type of a parameter
    Lazy(Box<TypeReference>),
    Error,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TupleTypeField {
    pub label: Option<Name>,
    pub annotation: TypeReference,
}

#[derive(Debug, Clone, PartialEq)]
pub struct FunctionTypeParameter {
    pub name: Option<Name>,
    pub is_var: bool,
    pub annotation: TypeReference,
}

// --- Statements -----------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
pub struct Block {
    pub statements: Vec<Statement>,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Statement {
    pub kind: StatementKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum StatementKind {
    Declaration(Declaration),
    Binding(Binding),
    Assignment { target: Expression, value: Expression },
    For { pattern: Pattern, iterable: Expression, body: Block },
    While { condition: Condition, body: Block },
    Return(Option<Expression>),
    Break,
    Continue,
    Expression(Expression),
}

/// `const x: Int = 1`, `var (a, b) = pair`
#[derive(Debug, Clone, PartialEq)]
pub struct Binding {
    /// The doc comment in front of a top-level `const`/`var` **statement** (`StatementKind::Binding`). `None` when
    /// this binding is the payload of a `Declaration` or a `Member`: the doc comment belongs to that instead, so it
    /// is never duplicated.
    pub doc: Option<String>,
    pub is_var: bool,
    pub pattern: Pattern,
    pub annotation: Option<TypeReference>,
    pub value: Expression,
}

/// The head of `if` and `while`: an expression, or a pattern binding (`if const Some(x) = lookup()`).
#[derive(Debug, Clone, PartialEq)]
pub enum Condition {
    Expression(Box<Expression>),
    Binding { is_var: bool, pattern: Pattern, value: Box<Expression> },
}

// --- Expressions ----------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
pub struct Expression {
    pub kind: ExpressionKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ExpressionKind {
    Integer(String),
    Float(String),
    Bool(bool),
    Char(char),
    Text(Vec<TextSegment>),
    /// A name, including `_`, `_2`, `self` and `Self`
    Name(String),
    /// `.Circle`: a case of the type that is expected here
    ImplicitMember(Name),
    /// `List<Int>` in front of `.of(...)` or `(...)`
    Generic {
        target: Box<Expression>,
        arguments: Vec<TypeReference>,
    },
    /// `(1, "one")`, `(lowest: 1, highest: 9)`
    Tuple(Vec<Argument>),
    List(Vec<Argument>),
    Map(Vec<(Expression, Expression)>),
    Member {
        target: Box<Expression>,
        name: Name,
        optional: bool,
    },
    Index {
        target: Box<Expression>,
        index: Box<Expression>,
    },
    Call {
        callee: Box<Expression>,
        arguments: Vec<Argument>,
        style: CallStyle,
    },
    Unary {
        operator: UnaryOperator,
        operand: Box<Expression>,
    },
    Binary {
        operator: BinaryOperator,
        left: Box<Expression>,
        right: Box<Expression>,
    },
    Range {
        start: Option<Box<Expression>>,
        end: Option<Box<Expression>>,
        inclusive: bool,
    },
    /// Postfix `?`
    Try(Box<Expression>),
    Closure(Closure),
    If {
        condition: Condition,
        then: Block,
        otherwise: Option<Box<Expression>>,
    },
    Match {
        subject: Box<Expression>,
        arms: Vec<MatchArm>,
    },
    /// Only as the body of a match arm and as the `else` of an `if`
    Block(Block),
    Error,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TextSegment {
    Literal(String),
    Expression(Expression),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CallStyle {
    /// `f(a, b)`, possibly with a trailing closure
    Parentheses,
    /// `f a, b`
    Command,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Argument {
    pub label: Option<Name>,
    /// `...items`
    pub is_spread: bool,
    pub value: Expression,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Closure {
    pub parameters: Vec<ClosureParameter>,
    pub body: Block,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ClosureParameter {
    pub pattern: Pattern,
    pub annotation: Option<TypeReference>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub guard: Option<Expression>,
    pub body: Expression,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnaryOperator {
    Negate,
    Not,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    And,
    Or,
    /// `??`
    Coalesce,
}

// --- Patterns -------------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
pub struct Pattern {
    pub kind: PatternKind,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PatternKind {
    Wildcard,
    /// A bare name always binds. Cases are written `.Case` or `Type.Case`.
    Name(String),
    Literal(Box<Expression>),
    Range {
        start: Box<Expression>,
        end: Box<Expression>,
        inclusive: bool,
    },
    Tuple(Vec<Pattern>),
    /// `[first, ...rest]`
    List {
        items: Vec<Pattern>,
        rest: Option<RestPattern>,
    },
    /// `Some(x)`, `Shape.Circle(radius)`, `Point(x, y)`
    Variant {
        path: Vec<Name>,
        fields: Vec<FieldPattern>,
    },
    /// `.Circle(radius)`, `.Empty`: a case of the type of the value
    ImplicitVariant {
        name: Name,
        fields: Vec<FieldPattern>,
    },
    /// `1 | 2 | 3`
    Or(Vec<Pattern>),
    Error,
}

#[derive(Debug, Clone, PartialEq)]
pub struct RestPattern {
    /// Number of patterns in front of the rest
    pub position: usize,
    pub name: Option<Name>,
}

/// `Point(x: 0, y: 0)`: the label is documentation today. The type checker will verify that it names the field at
/// this position; fields are still matched by position, never by label.
#[derive(Debug, Clone, PartialEq)]
pub struct FieldPattern {
    pub label: Option<Name>,
    pub pattern: Pattern,
}
