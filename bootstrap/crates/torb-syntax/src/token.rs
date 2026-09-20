use crate::span::Span;

#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

/// `/** ... */`. Doc comments are not tokens: the parser attaches them to the declaration that follows them.
#[derive(Debug, Clone, PartialEq)]
pub struct DocComment {
    /// Without the comment markers and the ` * ` margin. Markdown.
    pub text: String,
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    Identifier,
    Keyword(Keyword),
    Integer,
    Float,
    Char(char),
    /// A string literal. Interpolated expressions are kept as source ranges and parsed by the parser.
    Text(Vec<TextPart>),

    // Brackets
    ParenOpen,
    ParenClose,
    BracketOpen,
    BracketClose,
    BraceOpen,
    BraceClose,

    // Punctuation
    Comma,
    Colon,
    Dot,
    DotDot,
    DotDotEqual,
    Ellipsis,
    Arrow,
    Question,
    QuestionDot,
    QuestionQuestion,

    // Operators
    Equal,
    EqualEqual,
    BangEqual,
    Bang,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    /// Only in patterns: `1 | 2 | 3`
    Pipe,
    /// An intersection of traits: `Compare & Show`. There are no bit operators, so this is free.
    Ampersand,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    /// A line break that ends a statement. Line breaks that do not are removed by the lexer.
    Newline,
    EndOfFile,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TextPart {
    Literal(String),
    /// The source range between `{` and `}`.
    Expression(Span),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Keyword {
    Const,
    Var,
    Fn,
    Type,
    Trait,
    Extend,
    Foreign,
    Case,
    Use,
    Public,
    Private,
    Native,
    Shared,
    Lazy,
    If,
    Else,
    Match,
    For,
    In,
    While,
    /// `loop { ... }`: the endless loop. Without a `break` its type is `Never`.
    Loop,
    Break,
    Continue,
    Return,
    True,
    False,
    /// The one value of `Void`, like `true` and `false` are the values of `Bool`
    Void,
    SelfValue,
    SelfType,
    With,
    Where,
}

impl Keyword {
    /// `from`, `as` and `by` are contextual: they are ordinary identifiers for the lexer.
    pub fn from_text(text: &str) -> Option<Keyword> {
        Some(match text {
            "const" => Keyword::Const,
            "var" => Keyword::Var,
            "fn" => Keyword::Fn,
            "type" => Keyword::Type,
            "trait" => Keyword::Trait,
            "extend" => Keyword::Extend,
            "foreign" => Keyword::Foreign,
            "case" => Keyword::Case,
            "use" => Keyword::Use,
            "public" => Keyword::Public,
            "private" => Keyword::Private,
            "native" => Keyword::Native,
            "shared" => Keyword::Shared,
            "lazy" => Keyword::Lazy,
            "if" => Keyword::If,
            "else" => Keyword::Else,
            "match" => Keyword::Match,
            "for" => Keyword::For,
            "in" => Keyword::In,
            "while" => Keyword::While,
            "loop" => Keyword::Loop,
            "break" => Keyword::Break,
            "continue" => Keyword::Continue,
            "return" => Keyword::Return,
            "true" => Keyword::True,
            "false" => Keyword::False,
            "void" => Keyword::Void,
            "self" => Keyword::SelfValue,
            "Self" => Keyword::SelfType,
            "with" => Keyword::With,
            "where" => Keyword::Where,
            _ => return None,
        })
    }
}

impl TokenKind {
    /// A line that ends with one of these continues on the next line.
    pub fn continues_line_after(&self) -> bool {
        use TokenKind::*;
        matches!(
            self,
            Comma
                | Colon
                | Dot
                | QuestionDot
                | Arrow
                | Equal
                | EqualEqual
                | BangEqual
                | Less
                | LessEqual
                | GreaterEqual
                | AndAnd
                | OrOr
                | QuestionQuestion
                | Plus
                | Ampersand
                | Minus
                | Star
                | Slash
                | Percent
                | ParenOpen
                | BracketOpen
        )
    }

    /// A line that starts with one of these continues the previous line.
    pub fn continues_line_before(&self) -> bool {
        use TokenKind::*;
        matches!(
            self,
            Dot | QuestionDot
                | AndAnd
                | OrOr
                | QuestionQuestion
                | Plus
                | Ampersand
                | Star
                | Slash
                | Percent
                | EqualEqual
                | BangEqual
                | LessEqual
                | GreaterEqual
                | Equal
                | Keyword(self::Keyword::With)
                | Keyword(self::Keyword::Where)
                | Keyword(self::Keyword::Else)
        )
    }

    pub fn describe(&self) -> &'static str {
        use TokenKind::*;
        match self {
            Identifier => "a name",
            Keyword(_) => "a keyword",
            Integer | Float => "a number",
            Char(_) => "a character",
            Text(_) => "a string",
            ParenOpen => "`(`",
            ParenClose => "`)`",
            BracketOpen => "`[`",
            BracketClose => "`]`",
            BraceOpen => "`{`",
            BraceClose => "`}`",
            Comma => "`,`",
            Colon => "`:`",
            Dot => "`.`",
            DotDot => "`..`",
            DotDotEqual => "`..=`",
            Ellipsis => "`...`",
            Arrow => "`=>`",
            Question => "`?`",
            QuestionDot => "`?.`",
            QuestionQuestion => "`??`",
            Equal => "`=`",
            EqualEqual => "`==`",
            BangEqual => "`!=`",
            Bang => "`!`",
            Less => "`<`",
            LessEqual => "`<=`",
            Greater => "`>`",
            GreaterEqual => "`>=`",
            AndAnd => "`&&`",
            OrOr => "`||`",
            Pipe => "`|`",
            Ampersand => "`&`",
            Plus => "`+`",
            Minus => "`-`",
            Star => "`*`",
            Slash => "`/`",
            Percent => "`%`",
            Newline => "the end of the line",
            EndOfFile => "the end of the file",
        }
    }
}
