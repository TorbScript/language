//! One walk over a syntax tree that collects what the rules of the canon need: every call, with whether it stands in
//! _command position_ and whether its callee could be a field, and every pattern.
//!
//! Command position is where `parser/expressions.rs` calls `command_expression`: the start of a statement, the right
//! side of a binding or an assignment, after `return`, after `=>`, and the default of a field of a type. The default
//! of a parameter is parsed as an ordinary expression, and the default of a field of a **case** is separated from the
//! next field by a comma, which a command would swallow - neither counts here.

use torb_syntax::ast::*;

pub struct CallSite<'tree> {
    pub call: &'tree Expression,
    pub is_command_position: bool,
    /// The callee names a field or a constant of the `type`, `trait` or `extend` this call is in, so the parentheses
    /// decide what the call **means**: a command writes the field ("property commands", gap 15), and "calling a
    /// function in a field always needs parentheses". Such a call is left exactly as it is written.
    pub callee_could_be_a_field: bool,
}

#[derive(Default)]
pub struct Sites<'tree> {
    pub calls: Vec<CallSite<'tree>>,
    pub patterns: Vec<&'tree Pattern>,
}

pub fn walk(file: &File) -> Sites<'_> {
    let mut walk = Walk { sites: Sites::default(), fields: Vec::new() };
    walk.statements(&file.statements);
    walk.sites
}

struct Walk<'tree> {
    sites: Sites<'tree>,
    /// The fields and constants of the innermost `type`, `trait` or `extend`
    fields: Vec<String>,
}

impl<'tree> Walk<'tree> {
    fn statements(&mut self, items: &'tree [Statement]) {
        for item in items {
            self.statement(item);
        }
    }

    fn statement(&mut self, item: &'tree Statement) {
        match &item.kind {
            StatementKind::Declaration(inner) => self.declaration(inner),
            StatementKind::Binding(inner) => self.binding(inner),
            // A call is never the target of an assignment, so only the value is treated as command position
            StatementKind::Assignment { target, value } => {
                self.expression(target, false);
                self.expression(value, true);
            }
            StatementKind::For { pattern, iterable, body } => {
                self.pattern(pattern);
                self.expression(iterable, false);
                self.block(body);
            }
            StatementKind::While { condition, body } => {
                self.condition(condition);
                self.block(body);
            }
            StatementKind::Return(value) => {
                if let Some(value) = value {
                    self.expression(value, true);
                }
            }
            StatementKind::Break | StatementKind::Continue => {}
            StatementKind::Expression(inner) => self.expression(inner, true),
        }
    }

    fn declaration(&mut self, item: &'tree Declaration) {
        match &item.kind {
            DeclarationKind::Use(_) | DeclarationKind::Alias(_) => {}
            DeclarationKind::Function(inner) => self.function(inner),
            DeclarationKind::Type(inner) => self.members(&inner.members),
            DeclarationKind::Trait(inner) => self.members(&inner.members),
            DeclarationKind::Extend(inner) => self.members(&inner.members),
            DeclarationKind::Foreign(inner) => self.members(&inner.members),
            DeclarationKind::Constant(inner) => self.binding(inner),
        }
    }

    fn members(&mut self, items: &'tree [Member]) {
        let outer = std::mem::replace(&mut self.fields, field_names(items));
        for item in items {
            match &item.kind {
                // The members of a type end at the end of their line: a command in a default has nothing to swallow
                MemberKind::Field(inner) => {
                    if let Some(default) = &inner.default {
                        self.expression(default, true);
                    }
                }
                MemberKind::Constant(inner) => self.binding(inner),
                MemberKind::Function(inner) => self.function(inner),
                // The fields of a case are separated by commas, which a command call would read as its own arguments
                MemberKind::Case(inner) => {
                    for field in &inner.fields {
                        if let Some(default) = &field.default {
                            self.expression(default, false);
                        }
                    }
                }
            }
        }
        self.fields = outer;
    }

    fn function(&mut self, item: &'tree FunctionDeclaration) {
        for parameter in &item.parameters {
            if let Some(default) = &parameter.default {
                self.expression(default, false);
            }
        }
        if let Some(body) = &item.body {
            self.block(body);
        }
    }

    fn binding(&mut self, item: &'tree Binding) {
        self.pattern(&item.pattern);
        self.expression(&item.value, true);
    }

    fn condition(&mut self, item: &'tree Condition) {
        match item {
            Condition::Expression(inner) => self.expression(inner, false),
            Condition::Binding { pattern, value, .. } => {
                self.pattern(pattern);
                self.expression(value, false);
            }
        }
    }

    fn block(&mut self, item: &'tree Block) {
        self.statements(&item.statements);
    }

    fn arguments(&mut self, items: &'tree [Argument]) {
        for item in items {
            self.expression(&item.value, false);
        }
    }

    fn expression(&mut self, node: &'tree Expression, is_command_position: bool) {
        if let ExpressionKind::Call { callee, .. } = &node.kind {
            let callee_could_be_a_field = field_name_of(callee).is_some_and(|name| self.fields.iter().any(|field| field == name));
            self.sites.calls.push(CallSite { call: node, is_command_position, callee_could_be_a_field });
        }
        match &node.kind {
            ExpressionKind::Integer(_)
            | ExpressionKind::Float(_)
            | ExpressionKind::Bool(_)
            | ExpressionKind::VoidLiteral
            | ExpressionKind::Char(_)
            | ExpressionKind::Name(_)
            | ExpressionKind::ImplicitMember(_)
            | ExpressionKind::Error => {}
            ExpressionKind::Text(segments) => {
                for segment in segments {
                    if let TextSegment::Expression(inner) = segment {
                        self.expression(inner, false);
                    }
                }
            }
            ExpressionKind::Generic { target, .. } => self.expression(target, false),
            ExpressionKind::Tuple(items) | ExpressionKind::List(items) => self.arguments(items),
            ExpressionKind::Map(entries) => {
                for (key, value) in entries {
                    self.expression(key, false);
                    self.expression(value, false);
                }
            }
            ExpressionKind::Member { target, .. } => self.expression(target, false),
            ExpressionKind::Index { target, index } => {
                self.expression(target, false);
                self.expression(index, false);
            }
            ExpressionKind::Call { callee, arguments, .. } => {
                self.expression(callee, false);
                self.arguments(arguments);
            }
            ExpressionKind::Unary { operand, .. } => self.expression(operand, false),
            ExpressionKind::Binary { left, right, .. } => {
                self.expression(left, false);
                self.expression(right, false);
            }
            ExpressionKind::Range { start, end, .. } => {
                for side in [start, end].into_iter().flatten() {
                    self.expression(side, false);
                }
            }
            ExpressionKind::Try(inner) => self.expression(inner, false),
            ExpressionKind::Closure(closure) => {
                for parameter in &closure.parameters {
                    self.pattern(&parameter.pattern);
                }
                self.block(&closure.body);
            }
            ExpressionKind::If { condition, then, otherwise } => {
                self.condition(condition);
                self.block(then);
                if let Some(otherwise) = otherwise {
                    self.expression(otherwise, false);
                }
            }
            ExpressionKind::Match { subject, arms } => {
                self.expression(subject, false);
                for arm in arms {
                    self.pattern(&arm.pattern);
                    if let Some(guard) = &arm.guard {
                        self.expression(guard, false);
                    }
                    self.expression(&arm.body, true);
                }
            }
            ExpressionKind::Block(inner) => self.block(inner),
        }
    }

    fn pattern(&mut self, node: &'tree Pattern) {
        self.sites.patterns.push(node);
        match &node.kind {
            PatternKind::Wildcard | PatternKind::Name(_) | PatternKind::Error => {}
            PatternKind::Literal(value) => self.expression(value, false),
            PatternKind::Range { start, end, .. } => {
                self.expression(start, false);
                self.expression(end, false);
            }
            PatternKind::Tuple(items) | PatternKind::Or(items) => {
                for item in items {
                    self.pattern(item);
                }
            }
            PatternKind::List { items, .. } => {
                for item in items {
                    self.pattern(item);
                }
            }
            PatternKind::Variant { fields, .. } | PatternKind::ImplicitVariant { fields, .. } => {
                for field in fields {
                    self.pattern(&field.pattern);
                }
            }
        }
    }
}

/// The fields and the constants of a type, trait or extend: the names a command inside one of its functions writes
/// instead of calls.
fn field_names(members: &[Member]) -> Vec<String> {
    members
        .iter()
        .filter_map(|member| match &member.kind {
            MemberKind::Field(field) => Some(field.name.text.clone()),
            MemberKind::Constant(binding) => match &binding.pattern.kind {
                PatternKind::Name(name) => Some(name.clone()),
                _ => None,
            },
            _ => None,
        })
        .collect()
}

/// The field a callee could name: `step` and `self.step` inside of the type that has the field `step`. A field of
/// somebody else's value (`config.port`) is not visible here - the type checker sees those.
fn field_name_of(callee: &Expression) -> Option<&str> {
    match &callee.kind {
        ExpressionKind::Name(name) => Some(name),
        ExpressionKind::Member { target, name, optional: false } if matches!(&target.kind, ExpressionKind::Name(inner) if inner == "self") => {
            Some(&name.text)
        }
        _ => None,
    }
}
