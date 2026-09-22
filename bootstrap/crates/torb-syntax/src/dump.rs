//! Prints a syntax tree the way the generated `Show` of `compiler/src/syntax/ast.trb` prints it. Type, case and field
//! names are the ones of the TorbScript tree, so the output of both parsers can be compared character by character.

use crate::ast::*;
use crate::span::Span;

pub fn dump(file: &File) -> String {
    format!("SourceFile(statements: {})", list(&file.statements, statement))
}

fn list<Item>(items: &[Item], show: impl Fn(&Item) -> String) -> String {
    format!("[{}]", items.iter().map(show).collect::<Vec<_>>().join(", "))
}

fn option<Item>(item: &Option<Item>, show: impl Fn(&Item) -> String) -> String {
    match item {
        Some(item) => format!("Some({})", show(item)),
        None => "None".to_string(),
    }
}

/// One character inside a quoted `String` or `Char`, exactly as `Show.showNested` writes it (decided gap 23). It is the
/// same table as `escape_char` in `torb-interpreter` and `torb_escape_char` in `runtime/text.c`, because this dump is
/// compared with the one the generated `Show` produces, character by character.
fn escaped(character: char, quote: char) -> String {
    match character {
        '\n' => "\\n".to_string(),
        '\r' => "\\r".to_string(),
        '\t' => "\\t".to_string(),
        '\\' => "\\\\".to_string(),
        _ if character == quote => format!("\\{quote}"),
        _ if (character as u32) < 0x20 || character as u32 == 0x7F => format!("\\u{{{:x}}}", character as u32),
        _ => character.to_string(),
    }
}

fn text(text: &str) -> String {
    format!("\"{}\"", text.chars().map(|character| escaped(character, '"')).collect::<String>())
}

fn span(span: &Span) -> String {
    format!("Span(start: {}, end: {})", span.start, span.end)
}

fn name(name: &Name) -> String {
    format!("Name(text: {}, span: {})", text(&name.text), span(&name.span))
}

fn use_item(item: &UseItem) -> String {
    format!("UseItem(path: {}, alias: {})", list(&item.path, name), option(&item.alias, name))
}

fn modifiers(modifiers: &Modifiers) -> String {
    format!("Modifiers(visibility: {:?}, isNative: {}, isShared: {})", modifiers.visibility, modifiers.native, modifiers.shared)
}

fn declaration(declaration: &Declaration) -> String {
    let kind = match &declaration.kind {
        DeclarationKind::Use(usage) => {
            let items = match &usage.items {
                UseItems::Names(items) => format!("Names(items: {})", list(items, use_item)),
                UseItems::All { alias } => format!("All(alias: {})", name(alias)),
            };
            let source = match &usage.source {
                UseSource::Module(path) => format!("Module(path: {})", text(path)),
                UseSource::Local => "Local".to_string(),
            };
            format!("Use(declaration: UseDeclaration(items: {items}, source: {source}))")
        }
        DeclarationKind::Function(declaration) => format!("Function(declaration: {})", function(declaration)),
        DeclarationKind::Type(declaration) => format!(
            "Type(declaration: TypeDeclaration(name: {}, generics: {}, traits: {}, whereClauses: {}, members: {}))",
            name(&declaration.name),
            list(&declaration.generics, generic_parameter),
            list(&declaration.traits, trait_clause),
            list(&declaration.where_clauses, where_clause),
            list(&declaration.members, member),
        ),
        DeclarationKind::Alias(declaration) => format!(
            "Alias(declaration: AliasDeclaration(name: {}, generics: {}, target: {}))",
            name(&declaration.name),
            list(&declaration.generics, generic_parameter),
            type_reference(&declaration.target),
        ),
        DeclarationKind::Trait(declaration) => format!(
            "Trait(declaration: TraitDeclaration(name: {}, generics: {}, supertraits: {}, members: {}))",
            name(&declaration.name),
            list(&declaration.generics, generic_parameter),
            list(&declaration.supertraits, type_reference),
            list(&declaration.members, member),
        ),
        DeclarationKind::Extend(declaration) => format!(
            "Extend(declaration: ExtendDeclaration(generics: {}, target: {}, traits: {}, whereClauses: {}, members: {}))",
            list(&declaration.generics, generic_parameter),
            type_reference(&declaration.target),
            list(&declaration.traits, type_reference),
            list(&declaration.where_clauses, where_clause),
            list(&declaration.members, member),
        ),
        DeclarationKind::Foreign(declaration) => format!(
            "Foreign(declaration: ForeignDeclaration(library: {}, members: {}))",
            text(&declaration.library),
            list(&declaration.members, member),
        ),
        DeclarationKind::Constant(inner) => format!("Constant(binding: {})", binding(inner)),
    };
    format!(
        "Declaration(doc: {}, modifiers: {}, kind: {kind}, span: {})",
        option(&declaration.doc, |doc| text(doc)),
        modifiers(&declaration.modifiers),
        span(&declaration.span)
    )
}

fn generic_parameter(parameter: &GenericParameter) -> String {
    format!(
        "GenericParameter(isConst: {}, name: {}, bounds: {}, default: {})",
        parameter.is_const,
        name(&parameter.name),
        list(&parameter.bounds, type_reference),
        option(&parameter.default, type_reference),
    )
}

fn where_clause(clause: &WhereClause) -> String {
    format!("WhereClause(subject: {}, bounds: {})", type_reference(&clause.subject), list(&clause.bounds, type_reference))
}

fn trait_clause(clause: &TraitClause) -> String {
    format!("TraitClause(capability: {}, delegate: {})", type_reference(&clause.capability), option(&clause.delegate, name))
}

fn function(function: &FunctionDeclaration) -> String {
    format!(
        "FunctionDeclaration(name: {}, generics: {}, parameters: {}, returnType: {}, whereClauses: {}, body: {})",
        name(&function.name),
        list(&function.generics, generic_parameter),
        list(&function.parameters, parameter),
        option(&function.return_type, type_reference),
        list(&function.where_clauses, where_clause),
        option(&function.body, block),
    )
}

fn parameter(parameter: &Parameter) -> String {
    format!(
        "Parameter(doc: {}, name: {}, isVar: {}, isVariadic: {}, annotation: {}, default: {})",
        option(&parameter.doc, |doc| text(doc)),
        name(&parameter.name),
        parameter.is_var,
        parameter.is_variadic,
        option(&parameter.annotation, type_reference),
        option(&parameter.default, expression),
    )
}

fn member(member: &Member) -> String {
    let kind = match &member.kind {
        MemberKind::Field(declaration) => format!("Field(field: {})", field(declaration)),
        MemberKind::Constant(declaration) => format!("Constant(binding: {})", binding(declaration)),
        MemberKind::Function(declaration) => format!("Function(declaration: {})", function(declaration)),
        MemberKind::Case(case) => {
            format!("Case(declaration: CaseDeclaration(name: {}, fields: {}))", name(&case.name), list(&case.fields, field))
        }
    };
    format!(
        "Member(doc: {}, modifiers: {}, kind: {kind}, span: {})",
        option(&member.doc, |doc| text(doc)),
        modifiers(&member.modifiers),
        span(&member.span)
    )
}

fn field(field: &Field) -> String {
    format!(
        "Field(doc: {}, name: {}, isVar: {}, annotation: {}, default: {})",
        option(&field.doc, |doc| text(doc)),
        name(&field.name),
        field.is_var,
        type_reference(&field.annotation),
        option(&field.default, expression),
    )
}

fn type_reference(reference: &TypeReference) -> String {
    let kind = match &reference.kind {
        TypeKind::Named { path, arguments } => format!("Named(path: {}, arguments: {})", list(path, name), list(arguments, type_reference)),
        TypeKind::Optional(inner) => format!("Optional(inner: {})", type_reference(inner)),
        TypeKind::Tuple(fields) => format!(
            "TupleType(fields: {})",
            list(fields, |field| format!(
                "TupleTypeField(label: {}, annotation: {})",
                option(&field.label, name),
                type_reference(&field.annotation)
            ))
        ),
        TypeKind::Literals(values) => format!("Literals(values: {})", list(values, expression)),
        TypeKind::Intersection(members) => format!("Intersection(members: {})", list(members, type_reference)),
        TypeKind::Function { parameters, result } => format!(
            "FunctionType(parameters: {}, result: {})",
            list(parameters, |parameter| format!(
                "FunctionTypeParameter(name: {}, isVar: {}, annotation: {})",
                option(&parameter.name, name),
                parameter.is_var,
                type_reference(&parameter.annotation)
            )),
            type_reference(result),
        ),
        TypeKind::Lazy(inner) => format!("Lazy(inner: {})", type_reference(inner)),
        TypeKind::Error => "Invalid".to_string(),
    };
    format!("TypeReference(kind: {kind}, span: {})", span(&reference.span))
}

fn block(block: &Block) -> String {
    format!("Block(statements: {}, span: {})", list(&block.statements, statement), span(&block.span))
}

fn statement(statement: &Statement) -> String {
    let kind = match &statement.kind {
        StatementKind::Declaration(inner) => format!("Declare(declaration: {})", declaration(inner)),
        StatementKind::Binding(inner) => format!("Bind(binding: {})", binding(inner)),
        StatementKind::Assignment { target, value } => format!("Assign(target: {}, value: {})", expression(target), expression(value)),
        StatementKind::For { pattern: binding, iterable, body } => {
            format!("For(pattern: {}, iterable: {}, body: {})", pattern(binding), expression(iterable), block(body))
        }
        StatementKind::While { condition: head, body } => format!("While(condition: {}, body: {})", condition(head), block(body)),
        StatementKind::Loop { body } => format!("Loop(body: {})", block(body)),
        StatementKind::Return(value) => format!("Return(value: {})", option(value, expression)),
        StatementKind::Break => "Break".to_string(),
        StatementKind::Continue => "Continue".to_string(),
        StatementKind::Expression(inner) => format!("Evaluate(expression: {})", expression(inner)),
    };
    format!("Statement(kind: {kind}, span: {})", span(&statement.span))
}

fn binding(binding: &Binding) -> String {
    format!(
        "Binding(doc: {}, isVar: {}, pattern: {}, annotation: {}, value: {})",
        option(&binding.doc, |doc| text(doc)),
        binding.is_var,
        pattern(&binding.pattern),
        option(&binding.annotation, type_reference),
        expression(&binding.value),
    )
}

fn condition(condition: &Condition) -> String {
    match condition {
        Condition::Expression(inner) => format!("Test(expression: {})", expression(inner)),
        Condition::Binding { is_var, pattern: binding, value } => {
            format!("Bind(isVar: {is_var}, pattern: {}, value: {})", pattern(binding), expression(value))
        }
    }
}

fn argument(argument: &Argument) -> String {
    format!("Argument(label: {}, isSpread: {}, value: {})", option(&argument.label, name), argument.is_spread, expression(&argument.value))
}

fn expression(node: &Expression) -> String {
    let boxed = |inner: &Option<Box<Expression>>| match inner {
        Some(inner) => format!("Some({})", expression(inner)),
        None => "None".to_string(),
    };
    let kind = match &node.kind {
        ExpressionKind::Integer(value) => format!("IntegerLiteral(text: {})", text(value)),
        ExpressionKind::Float(value) => format!("FloatLiteral(text: {})", text(value)),
        ExpressionKind::Bool(value) => format!("BoolLiteral(value: {value})"),
        ExpressionKind::VoidLiteral => "VoidLiteral".to_string(),
        ExpressionKind::Char(value) => format!("CharLiteral(value: '{}')", escaped(*value, '\'')),
        ExpressionKind::Text(segments) => format!(
            "TextLiteral(segments: {})",
            list(segments, |segment| match segment {
                TextSegment::Literal(literal) => format!("Literal(text: {})", text(literal)),
                TextSegment::Expression(inner) => format!("Interpolated(expression: {})", expression(inner)),
            })
        ),
        ExpressionKind::Name(value) => format!("Name(text: {})", text(value)),
        ExpressionKind::ImplicitMember(member) => format!("ImplicitMember(name: {})", name(member)),
        ExpressionKind::Generic { target, arguments } => {
            format!("Generic(target: {}, arguments: {})", expression(target), list(arguments, type_reference))
        }
        ExpressionKind::Tuple(items) => format!("TupleLiteral(items: {})", list(items, argument)),
        ExpressionKind::List(items) => format!("ListLiteral(items: {})", list(items, argument)),
        ExpressionKind::Map(entries) => {
            format!("MapLiteral(entries: {})", list(entries, |(key, value)| format!("({}, {})", expression(key), expression(value))))
        }
        ExpressionKind::Member { target, name: member, optional } => {
            format!("Member(target: {}, name: {}, isOptional: {optional})", expression(target), name(member))
        }
        ExpressionKind::Index { target, index } => format!("Index(target: {}, index: {})", expression(target), expression(index)),
        ExpressionKind::Call { callee, arguments, style } => {
            format!("Call(callee: {}, arguments: {}, style: {style:?})", expression(callee), list(arguments, argument))
        }
        ExpressionKind::Unary { operator, operand } => format!("Unary(operator: {operator:?}, operand: {})", expression(operand)),
        ExpressionKind::Binary { operator, left, right } => {
            format!("Binary(operator: {operator:?}, left: {}, right: {})", expression(left), expression(right))
        }
        ExpressionKind::Range { start, end, inclusive } => {
            format!("RangeLiteral(start: {}, end: {}, isInclusive: {inclusive})", boxed(start), boxed(end))
        }
        ExpressionKind::Try(inner) => format!("Try(inner: {})", expression(inner)),
        ExpressionKind::Closure(closure) => format!(
            "Closure(closure: Closure(parameters: {}, body: {}))",
            list(&closure.parameters, |parameter| format!(
                "ClosureParameter(pattern: {}, annotation: {})",
                pattern(&parameter.pattern),
                option(&parameter.annotation, type_reference)
            )),
            block(&closure.body),
        ),
        ExpressionKind::If { condition: head, then, otherwise } => {
            format!("If(condition: {}, then: {}, otherwise: {})", condition(head), block(then), boxed(otherwise))
        }
        ExpressionKind::Match { subject, arms } => format!(
            "Match(subject: {}, arms: {})",
            expression(subject),
            list(arms, |arm| format!(
                "MatchArm(pattern: {}, guard: {}, body: {})",
                pattern(&arm.pattern),
                option(&arm.guard, expression),
                expression(&arm.body)
            )),
        ),
        ExpressionKind::Block(inner) => format!("Block(block: {})", block(inner)),
        ExpressionKind::Error => "Invalid".to_string(),
    };
    format!("Expression(kind: {kind}, span: {})", span(&node.span))
}

fn pattern(node: &Pattern) -> String {
    let kind = match &node.kind {
        PatternKind::Wildcard => "Wildcard".to_string(),
        PatternKind::Name(value) => format!("Binding(name: {})", text(value)),
        PatternKind::Literal(value) => format!("Literal(value: {})", expression(value)),
        PatternKind::Range { start, end, inclusive } => {
            format!("RangePattern(start: {}, end: {}, isInclusive: {inclusive})", expression(start), expression(end))
        }
        PatternKind::Tuple(items) => format!("TuplePattern(items: {})", list(items, pattern)),
        PatternKind::List { items, rest } => format!(
            "ListPattern(items: {}, rest: {})",
            list(items, pattern),
            option(rest, |rest| format!("RestPattern(position: {}, name: {})", rest.position, option(&rest.name, name))),
        ),
        PatternKind::Variant { path, fields, has_rest } => {
            format!("Variant(path: {}, fields: {}, hasRest: {has_rest})", list(path, name), list(fields, field_pattern))
        }
        PatternKind::ImplicitVariant { name: case, fields, has_rest } => {
            format!("ImplicitVariant(name: {}, fields: {}, hasRest: {has_rest})", name(case), list(fields, field_pattern))
        }
        PatternKind::Or(patterns) => format!("Alternatives(patterns: {})", list(patterns, pattern)),
        PatternKind::Error => "Invalid".to_string(),
    };
    format!("Pattern(kind: {kind}, span: {})", span(&node.span))
}

fn field_pattern(field: &FieldPattern) -> String {
    format!("FieldPattern(label: {}, pattern: {})", option(&field.label, name), pattern(&field.pattern))
}
