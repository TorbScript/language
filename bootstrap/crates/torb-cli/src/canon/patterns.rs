//! Rule `imported-case-patterns`, **off by default**: in a pattern, `.Case` becomes `Case` for a payload-less case
//! that a `use` brought into the file's scope - `.None` becomes `None`.
//!
//! The parser change it waited for has landed: a pattern name that starts with an uppercase letter is a case and no
//! longer a binding. The rule still changes the *tree* of a file (`ImplicitVariant` becomes `Variant`), so it stays out
//! of the default run and out of the safety net that compares syntax trees. What it does is pinned by its own
//! text-in, text-out tests.
//!
//! An import names a case by its path: `use Option.Some from "std/core"`, or `use Shape.Circle` without a module.
//! `None` comes from the prelude and counts as imported unless the file declares or imports something of that name
//! itself. It is the only case of the prelude without a payload: a `.Fail` without fields is never `Result`'s, it is
//! the case of some other type (`DecisionNode.Fail`), and a syntax tool cannot see which type a pattern matches.

use std::collections::HashSet;

use torb_syntax::ast::*;

use super::edit::{Edit, EditKind, Replacement};

/// The payload-less cases the prelude puts into every file.
const PRELUDE: [&str; 1] = ["None"];

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
                    let local = item.local_name().text.clone();
                    // `use Option.Some from "std/core"`, `use Shape.Circle`: a path names a case, a single name a type
                    if item.path.len() > 1 {
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
