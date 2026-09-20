//! Rule `loops`, **off by default**: `while true {` becomes `loop {`.
//!
//! The endless loop has its own word, so "never ends" is a property of the syntax and not of a condition. The rule
//! changes the *tree* of a file (`While` becomes `Loop`), so it stays out of the default run and out of the safety net
//! that compares syntax trees; what it does is pinned by its own text-in, text-out tests.
//!
//! Only the head is touched: `while` and the `true` after it are replaced by `loop`, and everything from the `{` on
//! stays byte-identical. `while false` is left alone - it is not an endless loop, and the checker has nothing against
//! it.

use torb_syntax::ast::*;

use super::edit::{Edit, EditKind, Replacement};

/// One edit per `while true`, from the `w` of `while` to the end of the `true` that follows it.
pub fn edits(source: &str, endless: &[&Statement]) -> Vec<Edit> {
    let mut edits: Vec<Edit> = endless
        .iter()
        .filter_map(|statement| {
            let StatementKind::While { condition, .. } = &statement.kind else { return None };
            let Condition::Expression(test) = condition else { return None };
            let start = statement.span.start;
            let end = test.span.end;
            // The head has to read exactly `while` ... `true`: anything else is not this rule's business
            let head = source.get(start as usize..end as usize)?;
            if !head.starts_with("while") || !head.ends_with("true") {
                return None;
            }
            Some(Edit::new(EditKind::EndlessLoop, start, vec![Replacement::new(start, end, "loop")]))
        })
        .collect();
    edits.sort_by_key(|edit| edit.position);
    edits
}
