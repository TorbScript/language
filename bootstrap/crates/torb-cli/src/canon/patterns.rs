//! Rule `imported-case-patterns`, **off by default**: in a pattern, `.Case` becomes `Case` for a payload-less case
//! that a `use` brought into the file's scope - `.None` becomes `None`.
//!
//! It waits for the parser change that makes a bare imported case name a case pattern instead of a binding; until that
//! lands, the rule changes what a file means and is therefore not part of the default run and not covered by the
//! safety net that compares syntax trees. What it does is pinned by its own text-in, text-out tests.
//!
//! Both spellings of the import are understood: `use Some, None from Option` (the cases of a type) and
//! `use Option.Some from "std/core"` (a case out of a module). `Some`, `None`, `Ok` and `Fail` come from the prelude
//! and count as imported unless the file declares or imports something of that name itself.

use std::collections::HashSet;

use torb_syntax::ast::*;

use super::edit::{Edit, EditKind, Replacement};

/// The cases the prelude puts into every file.
const PRELUDE: [&str; 4] = ["Some", "None", "Ok", "Fail"];

pub fn edits(source: &str, file: &File, patterns: &[&Pattern]) -> Vec<Edit> {
    let imported = imported_cases(file);
    let mut edits: Vec<Edit> = patterns
        .iter()
        .filter_map(|pattern| {
            let PatternKind::ImplicitVariant { name, fields } = &pattern.kind else { return None };
            // Only a case without a payload: `.Some(x)` is already written the way the parser reads it
            if !fields.is_empty() || !imported.contains(&name.text) {
                return None;
            }
            let dot = pattern.span.start;
            if !source[dot as usize..].starts_with('.') {
                return None;
            }
            Some(Edit::new(EditKind::CasePattern, dot, vec![Replacement::new(dot, dot + 1, "")]))
        })
        .collect();
    edits.sort_by_key(|edit| edit.position);
    edits
}

/// The case names a file can write without a `.` in front: what a `use` imported, plus the prelude's four where the
/// file does not shadow them.
fn imported_cases(file: &File) -> HashSet<String> {
    let mut cases = HashSet::new();
    let mut shadowed = HashSet::new();
    for statement in &file.statements {
        let declaration = match &statement.kind {
            StatementKind::Declaration(declaration) => declaration,
            StatementKind::Binding(binding) => {
                if let PatternKind::Name(name) = &binding.pattern.kind {
                    shadowed.insert(name.clone());
                }
                continue;
            }
            _ => continue,
        };
        match &declaration.kind {
            DeclarationKind::Use(usage) => {
                let UseItems::Names(items) = &usage.items else { continue };
                for item in items {
                    let local = item.alias.as_ref().unwrap_or(&item.name).text.clone();
                    let local = local.rsplit('.').next().unwrap_or(&local).to_string();
                    // `use Some, None from Option`, or `use Option.Some from "std/core"`: a case, not a whole type
                    let is_case = matches!(usage.source, UseSource::Type(_)) || item.name.text.contains('.');
                    if is_case {
                        cases.insert(local);
                    } else {
                        shadowed.insert(local);
                    }
                }
            }
            DeclarationKind::Function(inner) => {
                shadowed.insert(inner.name.text.clone());
            }
            DeclarationKind::Type(inner) => {
                shadowed.insert(inner.name.text.clone());
            }
            DeclarationKind::Alias(inner) => {
                shadowed.insert(inner.name.text.clone());
            }
            DeclarationKind::Trait(inner) => {
                shadowed.insert(inner.name.text.clone());
            }
            DeclarationKind::Constant(inner) => {
                if let PatternKind::Name(name) = &inner.pattern.kind {
                    shadowed.insert(name.clone());
                }
            }
            DeclarationKind::Extend(_) | DeclarationKind::Foreign(_) => {}
        }
    }
    for name in PRELUDE {
        if !shadowed.contains(name) {
            cases.insert(name.to_string());
        }
    }
    cases
}
