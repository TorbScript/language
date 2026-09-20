//! Rule `unused-bindings`: in a **refutable** pattern - an arm of a `match`, an `if const`/`if var`, a `while const` -
//! a binding whose name is nowhere in the arm's guard or body becomes `_`.
//!
//! The rule the checker enforces is the one about reading a binding, and this is only the sweep that brings a whole
//! checkout to it. It works without name resolution and therefore stays on the safe side: a use is "the name occurs as a
//! word anywhere in the text of the guard or the body". A comment, a string literal and an interpolation all count, and
//! so does a name that a nested pattern happens to bind again - every one of those leaves the binding exactly as it is,
//! and the checker then says what is left for a human.
//!
//! A name that starts with `_` is already the documented form and is never touched.
//!
//! Like `imported-case-patterns` this rule changes the syntax *tree* (`Name` becomes `Wildcard`), so it is outside the
//! safety net that compares trees and is checked by parsing alone, and it stays out of the default run.

use torb_syntax::ast::*;
use torb_syntax::span::Span;

use super::edit::{Edit, EditKind, Replacement};
use super::walk::BindingSite;

pub fn edits(source: &str, sites: &[BindingSite<'_>]) -> Vec<Edit> {
    let mut edits = Vec::new();
    for site in sites {
        let mut names = Vec::new();
        collect_names(site.pattern, &mut names);
        for name in names {
            let text = &source[name.start as usize..name.end as usize];
            // `_reason` keeps the name as documentation and is the form the rule points at, never one it rewrites
            if text.starts_with('_') {
                continue;
            }
            if site.regions.iter().any(|region| contains_word(&source[region.start as usize..region.end as usize], text)) {
                continue;
            }
            edits.push(Edit::new(EditKind::UnusedBinding, name.start, vec![Replacement::new(name.start, name.end, "_")]));
        }
    }
    edits.sort_by_key(|edit| edit.position);
    edits
}

/// The span of every bare name the pattern binds. `...rest` is left out on purpose: dropping its name changes more than
/// one token, and the checker reports the few that exist.
fn collect_names(pattern: &Pattern, found: &mut Vec<Span>) {
    match &pattern.kind {
        PatternKind::Name(_) => found.push(pattern.span),
        PatternKind::Tuple(items) | PatternKind::Or(items) => {
            for item in items {
                collect_names(item, found);
            }
        }
        PatternKind::List { items, .. } => {
            for item in items {
                collect_names(item, found);
            }
        }
        PatternKind::Variant { fields, .. } | PatternKind::ImplicitVariant { fields, .. } => {
            for field in fields {
                collect_names(&field.pattern, found);
            }
        }
        PatternKind::Wildcard | PatternKind::Literal(_) | PatternKind::Range { .. } | PatternKind::Error => {}
    }
}

/// Whether the name stands in the text as a word of its own: not as a part of a longer name. Everything else about the
/// place it stands in is deliberately ignored, because ignoring it can only keep a binding that the checker then names.
fn contains_word(text: &str, name: &str) -> bool {
    let bytes = text.as_bytes();
    text.match_indices(name).any(|(start, found)| {
        let end = start + found.len();
        let before = start == 0 || !is_word_byte(bytes[start - 1]);
        let after = end == bytes.len() || !is_word_byte(bytes[end]);
        before && after
    })
}

fn is_word_byte(byte: u8) -> bool {
    byte == b'_' || byte.is_ascii_alphanumeric()
}
