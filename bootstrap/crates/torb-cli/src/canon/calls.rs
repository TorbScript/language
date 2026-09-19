//! Rule `calls`: the call canon **K2**. A call is written as a command - without parentheses - wherever the grammar
//! allows it, and gets parentheses wherever it does not.
//!
//! A call is a command when all of this holds:
//!
//! 1. it stands in command position (see `walk.rs`),
//! 2. the callee is a name or a member path (`Ok`, `Email.parse`, `roles.map`; not `a?.b`, not `pair.0`, not
//!    `load<Config>`),
//! 3. it has at least one argument, and the first one is not a spread,
//! 4. the first argument starts with a token the grammar accepts after the callee of a command
//!    (`starts_command_argument` of the parser),
//! 5. no argument has an operator at its top level (`-`, `!`, `?`, `??`, ranges, comparisons, arithmetic, `&&`, and a
//!    generic argument list),
//! 6. the arguments are on one line (a trailing closure may go over several),
//! 7. and there is no `{` between the parentheses: the `{` of a closure inside an argument would become the trailing
//!    closure of the command.
//!
//! Everything else keeps or gets parentheses - which is why everything nested always has them (`Ok Some(x)`, never
//! `Ok Some x`).
//!
//! On top of that, a call whose callee names a field of the type it stands in is not touched at all, in either
//! direction: there the parentheses are not style but meaning (a command *writes* the field, `checker/command.trb`).
//!
//! Where the rule is deliberately more careful than the grammar, it says so at the check.

use torb_syntax::ast::*;
use torb_syntax::token::{Keyword, Token, TokenKind};

use super::edit::{Edit, EditKind, Replacement};
use super::walk::CallSite;

pub fn edits(source: &str, tokens: &[Token], sites: &[CallSite]) -> Vec<Edit> {
    let mut edits: Vec<Edit> = sites.iter().filter_map(|site| edit_for(source, tokens, site)).collect();
    edits.sort_by_key(|edit| edit.position);
    edits
}

fn edit_for(source: &str, tokens: &[Token], site: &CallSite) -> Option<Edit> {
    let ExpressionKind::Call { callee, arguments, style } = &site.call.kind else { return None };
    // A call whose callee could be a field of the enclosing type is written the way it is written: there the
    // parentheses carry meaning, not style (see `CallSite::callee_could_be_a_field`)
    if site.callee_could_be_a_field {
        return None;
    }
    match style {
        CallStyle::Parentheses => to_command(source, tokens, site, callee, arguments),
        CallStyle::Command => to_parentheses(source, tokens, site, callee, arguments),
    }
}

/// `f(a, b)` becomes `f a, b`, and `f(a) { ... }` becomes `f a { ... }`.
fn to_command(source: &str, tokens: &[Token], site: &CallSite, callee: &Expression, arguments: &[Argument]) -> Option<Edit> {
    if !site.is_command_position || !is_command_path(callee) || !is_written_as_a_command_path(tokens, callee.span) {
        return None;
    }
    // `f { ... }` is a call with a trailing closure and no parentheses at all: there is nothing to take away. A `(`
    // that does not sit directly behind the callee (`f (a)`, `f /* why */ (a)`) is left alone, so that no comment and
    // no space outside of an edited range is ever lost.
    let open = index_of_token_at(tokens, callee.span.end).filter(|&index| tokens[index].kind == TokenKind::ParenOpen)?;
    let close = matching_parenthesis(tokens, open)?;
    let (open, close) = (tokens[open].span, tokens[close].span);

    // A trailing closure stands behind the `)` and is not part of the argument list in the source
    let plain = &arguments[..plain_arguments(tokens, arguments, close.start)];
    let (first, last) = (plain.first()?, plain.last()?);
    if first.is_spread || !starts_command_argument(source, tokens, argument_start(first)) {
        return None;
    }
    if plain.iter().any(has_top_level_operator) {
        return None;
    }
    // A `{` inside the parentheses belongs to a closure, an `if` or a `match` there. Without the parentheses the
    // parser would read it as the trailing closure of the command instead.
    if tokens.iter().any(|token| token.kind == TokenKind::BraceOpen && token.span.start > open.start && token.span.start < close.start) {
        return None;
    }
    // Nothing between the last argument and the `)` (a trailing comma, a space, a comment), and all on one line
    if close.start != last.value.span.end || source[callee.span.end as usize..close.end as usize].contains('\n') {
        return None;
    }
    // A command call swallows what follows a comma as another argument: the arms of a `match` on one line, a field of
    // a case with a default. In command position but in front of a comma, the parentheses stay.
    if next_token(tokens, site.call.span.end).is_some_and(|token| token.kind == TokenKind::Comma) {
        return None;
    }
    let replacements = vec![Replacement::new(open.start, open.end, " "), Replacement::new(close.start, close.end, "")];
    Some(Edit::new(EditKind::ToCommand, site.call.span.start, replacements))
}

/// `f a, b` becomes `f(a, b)` where a condition of K2 does not hold: a nested call, an operator in an argument, or
/// arguments over several lines.
fn to_parentheses(source: &str, tokens: &[Token], site: &CallSite, callee: &Expression, arguments: &[Argument]) -> Option<Edit> {
    // A command call in the source has no `(` behind its callee, at least one argument, and a callee that is a path
    let plain = &arguments[..plain_arguments(tokens, arguments, callee.span.end)];
    let (first, last) = (plain.first()?, plain.last()?);
    let arguments_end = last.value.span.end;
    let over_several_lines = source[callee.span.end as usize..arguments_end as usize].contains('\n');
    if site.is_command_position && !plain.iter().any(has_top_level_operator) && !over_several_lines {
        return None;
    }
    // Only whitespace stands between the callee and its first argument; a comment there would be lost
    let between = &source[callee.span.end as usize..argument_start(first) as usize];
    if between.is_empty() || !between.chars().all(|character| character == ' ' || character == '\t') {
        return None;
    }
    let replacements =
        vec![Replacement::new(callee.span.end, argument_start(first), "("), Replacement::new(arguments_end, arguments_end, ")")];
    Some(Edit::new(EditKind::ToParentheses, site.call.span.start, replacements))
}

/// `Ok`, `Email.parse`, `self.builder.add` - what `command_path` of the parser reads. `a?.b` is not one, and neither
/// is `pair.0`: after the `.` the parser wants a name or a keyword.
fn is_command_path(callee: &Expression) -> bool {
    match &callee.kind {
        ExpressionKind::Name(_) => true,
        ExpressionKind::Member { target, name, optional } => {
            !optional && name.text.starts_with(|character: char| character == '_' || character.is_alphabetic()) && is_command_path(target)
        }
        _ => false,
    }
}

/// Whether the callee is also **written** as a command path: `(a).b` is the same tree as `a.b`, but the parser does not
/// read a command there, because a command starts with a name. So the tokens of the callee have to be exactly what
/// `command_path` reads: a name or `self`/`Self`, then any number of `.` and a name or a keyword.
fn is_written_as_a_command_path(tokens: &[Token], span: torb_syntax::Span) -> bool {
    let first = tokens.partition_point(|token| token.span.start < span.start);
    let behind = tokens.partition_point(|token| token.span.start < span.end);
    let path = &tokens[first..behind];
    let Some((head, rest)) = path.split_first() else { return false };
    if !matches!(head.kind, TokenKind::Identifier | TokenKind::Keyword(Keyword::SelfValue | Keyword::SelfType)) {
        return false;
    }
    rest.chunks(2).all(|step| {
        step.len() == 2 && step[0].kind == TokenKind::Dot && matches!(step[1].kind, TokenKind::Identifier | TokenKind::Keyword(_))
    })
}

/// Where an argument starts in the source: at its label, if it has one.
fn argument_start(argument: &Argument) -> u32 {
    argument.label.as_ref().map_or(argument.value.span.start, |label| label.span.start)
}

/// An operator at the top level of an argument keeps the parentheses: `assert a == b` would read as `(assert a) == b`.
/// A generic argument list counts too (`f List<Int>` is a comparison to the parser until it is not).
fn has_top_level_operator(argument: &Argument) -> bool {
    matches!(
        argument.value.kind,
        ExpressionKind::Unary { .. }
            | ExpressionKind::Binary { .. }
            | ExpressionKind::Range { .. }
            | ExpressionKind::Try(_)
            | ExpressionKind::Generic { .. }
    )
}

/// How many arguments are written in the argument list. The parser appends the trailing closure of a call to its
/// arguments, but in the source it stands behind the `)` (or behind the last argument of a command), so it is not part
/// of what the parentheses have to enclose. A closure that is introduced by a comma is an ordinary argument
/// (`f a, { x => x }`).
fn plain_arguments(tokens: &[Token], arguments: &[Argument], after: u32) -> usize {
    let Some(last) = arguments.last() else { return 0 };
    let is_closure = last.label.is_none() && !last.is_spread && matches!(last.value.kind, ExpressionKind::Closure(_));
    let start = last.value.span.start;
    let after_a_comma = previous_token(tokens, start).is_some_and(|token| token.kind == TokenKind::Comma);
    if is_closure && start > after && !after_a_comma {
        return arguments.len() - 1;
    }
    arguments.len()
}

/// `starts_command_argument` of the parser, asked at an offset of the source. `if` and `match` are left out on
/// purpose: they bring a `{` with them, which check 7 rejects anyway, so the tool never has to reason about them.
fn starts_command_argument(source: &str, tokens: &[Token], offset: u32) -> bool {
    let Some(index) = index_of_token_at(tokens, offset) else { return false };
    let next_is_colon = tokens.get(index + 1).is_some_and(|next| next.kind == TokenKind::Colon);
    match &tokens[index].kind {
        TokenKind::Identifier => !matches!(&source[tokens[index].span.range()], "as" | "by" | "from") || next_is_colon,
        TokenKind::Integer | TokenKind::Float | TokenKind::Char(_) | TokenKind::Text(_) => true,
        TokenKind::Keyword(Keyword::True | Keyword::False | Keyword::SelfValue | Keyword::SelfType) => true,
        // A label that is a keyword: `move from: a, to: b`
        TokenKind::Keyword(_) => next_is_colon,
        _ => false,
    }
}

/// The index of the token that starts exactly at `offset`. The expressions inside of a string literal are lexed on
/// their own, so an offset inside of an interpolation has no token here - and gets no edit.
fn index_of_token_at(tokens: &[Token], offset: u32) -> Option<usize> {
    let index = tokens.partition_point(|token| token.span.start < offset);
    tokens.get(index).filter(|token| token.span.start == offset).map(|_| index)
}

/// The first token at or behind `offset`.
fn next_token(tokens: &[Token], offset: u32) -> Option<&Token> {
    tokens.get(tokens.partition_point(|token| token.span.start < offset))
}

/// The last token in front of `offset`.
fn previous_token(tokens: &[Token], offset: u32) -> Option<&Token> {
    tokens[..tokens.partition_point(|token| token.span.start < offset)].last()
}

fn matching_parenthesis(tokens: &[Token], open: usize) -> Option<usize> {
    let mut depth = 0;
    for (offset, token) in tokens[open..].iter().enumerate() {
        match token.kind {
            TokenKind::ParenOpen => depth += 1,
            TokenKind::ParenClose => {
                depth -= 1;
                if depth == 0 {
                    return Some(open + offset);
                }
            }
            _ => {}
        }
    }
    None
}
