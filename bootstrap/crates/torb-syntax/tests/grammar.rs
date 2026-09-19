//! The rules of the grammar that are easy to get wrong.

use torb_syntax::ast::*;

fn parse_ok(source: &str) -> File {
    let parsed = torb_syntax::parse(source);
    assert!(parsed.diagnostics.is_empty(), "{source}\n{:?}", parsed.diagnostics);
    parsed.file
}

fn first_error(source: &str) -> String {
    let parsed = torb_syntax::parse(source);
    parsed.diagnostics.first().unwrap_or_else(|| panic!("expected a problem in: {source}")).message.clone()
}

fn expression(source: &str) -> ExpressionKind {
    let mut file = parse_ok(source);
    match file.statements.remove(0).kind {
        StatementKind::Expression(expression) => expression.kind,
        StatementKind::Binding(binding) => binding.value.kind,
        other => panic!("expected an expression, found {other:?}"),
    }
}

fn call(source: &str) -> (CallStyle, Vec<Argument>) {
    match expression(source) {
        ExpressionKind::Call { style, arguments, .. } => (style, arguments),
        other => panic!("expected a call, found {other:?}"),
    }
}

#[test]
fn command_calls() {
    let (style, arguments) = call("route \"/health\", to: \"health\"");
    assert_eq!(style, CallStyle::Command);
    assert_eq!(arguments.len(), 2);
    assert_eq!(arguments[1].label.as_ref().map(|label| label.text.as_str()), Some("to"));

    // The `{` belongs to the command, like the `{` after the head of an `if`
    let (style, arguments) = call("unless done { stop() }");
    assert_eq!(style, CallStyle::Command);
    assert!(matches!(arguments[1].value.kind, ExpressionKind::Closure(_)));

    // On the right of `=`, and with an `if` as the argument
    assert_eq!(call("const user = Email.parse text").0, CallStyle::Command);
    assert_eq!(call("port if secure { 8443 } else { 8080 }").1.len(), 1);

    // Not commands: `f -1` is a subtraction, `f [1]` is indexing
    assert!(matches!(expression("f -1"), ExpressionKind::Binary { operator: BinaryOperator::Subtract, .. }));
    assert!(matches!(expression("f [1]"), ExpressionKind::Index { .. }));
}

#[test]
fn command_arguments_cannot_have_trailing_closures() {
    assert!(first_error("print numbers.map { _ * 2 }.toList()").contains("belongs to the command call"));
    assert!(first_error("print [1, 2]").contains("always indexing"));
    parse_ok("print(numbers.map { _ * 2 }.toList())");
    parse_ok("print numbers.map({ _ * 2 }).toList()");
}

#[test]
fn generics_are_decided_by_the_token_after_the_closing_bracket() {
    assert!(matches!(expression("a < b"), ExpressionKind::Binary { operator: BinaryOperator::Less, .. }));
    assert!(first_error("a < b > c").contains("do not chain"));
    let ExpressionKind::Call { callee, .. } = expression("load<Config>(path)") else { panic!() };
    assert!(matches!(callee.kind, ExpressionKind::Generic { .. }));
    let ExpressionKind::Call { callee, .. } = expression("Stack<Map<String, Int>>.of()") else { panic!() };
    assert!(matches!(callee.kind, ExpressionKind::Member { .. }));
    parse_ok("const ok = count < slots.length() && index > 0");
}

#[test]
fn closures() {
    let ExpressionKind::Closure(closure) = expression("const f = { a, b => a + b }") else { panic!() };
    assert_eq!(closure.parameters.len(), 2);
    let ExpressionKind::Closure(closure) = expression("const f = { _ * 2 }") else { panic!() };
    assert!(closure.parameters.is_empty());
    let ExpressionKind::Closure(closure) = expression("const f = { user: User => user.name }") else { panic!() };
    assert!(closure.parameters[0].annotation.is_some());

    // A closure on its own line is a value, not the trailing closure of the line above
    let file = parse_ok("fn counter(): () => Int {\n  var count = 0\n  {\n    count = count + 1\n    count\n  }\n}");
    let StatementKind::Declaration(Declaration { kind: DeclarationKind::Function(function), .. }) = &file.statements[0].kind else {
        panic!()
    };
    assert_eq!(function.body.as_ref().unwrap().statements.len(), 2);
}

#[test]
fn lines() {
    assert_eq!(parse_ok("a\nb").statements.len(), 2);
    assert_eq!(parse_ok("const x = items\n  .filter { _ > 1 }\n  .toList()").statements.len(), 1);
    assert_eq!(parse_ok("const x = a +\n  b").statements.len(), 1);
    assert!(first_error("a; b").contains("no semicolons"));
    assert!(first_error("var x").contains("needs a value"));
}

#[test]
fn declarations() {
    let file = parse_ok("public shared type Connection with Close {\n  url: String\n  private(var) sent: Int = 0\n  fn send(var self, message: String) { sent = sent + 1 }\n}");
    let StatementKind::Declaration(declaration) = &file.statements[0].kind else { panic!() };
    assert!(declaration.modifiers.shared);
    let DeclarationKind::Type(declaration) = &declaration.kind else { panic!() };
    assert_eq!(declaration.members.len(), 3);
    assert_eq!(declaration.members[1].modifiers.visibility, Visibility::PrivateVar);

    parse_ok("type Meters with Add, Compare by value {\n  value: Float\n}");
    parse_ok("type Handler = (request: String, var context: Context) => Result<String, Failure>");
    parse_ok(
        "extend<Source, Target> Source with Into<Target> where Target: From<Source> {\n  fn into(self): Target { Target.from(self) }\n}",
    );
    parse_ok("use * as http from \"std/net/http\"\npublic use Stack, ArrayStack from \"./collections/stack\"");
    assert!(first_error("fn record(type: String) {}").contains("keyword"));
}

#[test]
fn public_constants_at_top_level() {
    // A plain `const` stays a `Binding` statement
    let file = parse_ok("const pi = 3.14");
    assert!(matches!(file.statements[0].kind, StatementKind::Binding(_)));

    // Modifiers route it through `Declaration` instead, with the same `Binding` payload
    let file = parse_ok("public const pi = 3.14");
    let StatementKind::Declaration(declaration) = &file.statements[0].kind else { panic!() };
    assert_eq!(declaration.modifiers.visibility, Visibility::Public);
    let DeclarationKind::Constant(binding) = &declaration.kind else { panic!() };
    assert!(!binding.is_var);

    // `private const` is accepted, it is the default anyway
    parse_ok("private const secret = 1");

    // `public var` is an error: a module has no mutable state
    assert!(first_error("public var mutable = 1").contains("A module has no mutable state"));

    // The doc comment of a top-level `const` is attached, whether it stays a `Binding` or becomes a `Declaration`
    let file = parse_ok("/** Pi. */\npublic const pi = 3.14");
    let StatementKind::Declaration(declaration) = &file.statements[0].kind else { panic!() };
    assert_eq!(declaration.doc.as_deref(), Some("Pi."));
    let DeclarationKind::Constant(binding) = &declaration.kind else { panic!() };
    assert_eq!(binding.doc, None);

    let file = parse_ok("/** Pi. */\nconst pi = 3.14");
    let StatementKind::Binding(binding) = &file.statements[0].kind else { panic!() };
    assert_eq!(binding.doc.as_deref(), Some("Pi."));
}

#[test]
fn labels_in_variant_patterns_are_kept() {
    let ExpressionKind::Match { arms, .. } = expression("match shape {\n  Point(x: 0, y: 0) => 0\n}") else { panic!() };
    let PatternKind::Variant { fields, .. } = &arms[0].pattern.kind else { panic!() };
    assert_eq!(fields.len(), 2);
    assert_eq!(fields[0].label.as_ref().map(|label| label.text.as_str()), Some("x"));
    assert_eq!(fields[1].label.as_ref().map(|label| label.text.as_str()), Some("y"));

    // Without a label, fields are still matched by position
    let ExpressionKind::Match { arms, .. } = expression("match shape {\n  .Circle(radius) => radius\n}") else { panic!() };
    let PatternKind::ImplicitVariant { fields, .. } = &arms[0].pattern.kind else { panic!() };
    assert_eq!(fields.len(), 1);
    assert!(fields[0].label.is_none());
}

#[test]
fn patterns_and_strings() {
    parse_ok("match value {\n  0 => \"zero\"\n  1 | 2 | 3 => \"small\"\n  4..=9 => \"medium\"\n  -1 => \"minus one\"\n  [first, ...rest] => \"list\"\n  Some((a, b)) if a < b => \"pair\"\n  _ => {\n    log value\n    \"other\"\n  }\n}");
    parse_ok("if const Some(user) = find(id) { print user.name }");
    let ExpressionKind::Text(segments) = expression("\"a {map[\"key\"]} \\{b}\"") else { panic!() };
    assert_eq!(segments.len(), 3);
    assert!(first_error("\"{1 +}\"").contains("Expected"));
}

#[test]
fn literal_types_const_parameters_tuples_and_intersections() {
    let file = parse_ok("type Status = \"online\" | \"offline\"");
    let StatementKind::Declaration(Declaration { kind: DeclarationKind::Alias(alias), .. }) = &file.statements[0].kind else { panic!() };
    let TypeKind::Literals(literals) = &alias.target.kind else { panic!() };
    assert_eq!(literals.len(), 2);

    parse_ok("fn connect(transport: \"tcp\" | \"udp\" = \"tcp\", retries: 0 | 1 | -1 = 1) {}");
    assert!(first_error("fn width(value: Int | String) {}").contains("Only literals"));
    assert!(first_error("fn width(value: \"auto\" | Int) {}").contains("Only literals"));

    // `true`/`false` are literals in a type position, too: what a const generic parameter of type `Bool` accepts.
    parse_ok("type Toggle<const Flag: Bool> {\n  var enabled: Toggle<true>\n  var disabled: Toggle<false>\n}");
    // A float is never a literal type, in or outside of a `const` generic argument.
    assert!(first_error("type Broken<const Size: Float> {\n  var value: Broken<1.5>\n}").contains("A float cannot be a literal type"));
    assert!(first_error("const rate: 1.5 = 1.5").contains("A float cannot be a literal type"));

    let file = parse_ok("type Matrix<const Rows: Int, const Columns: Int> {\n  var cells: Array<Array<Float, Columns>, Rows>\n}");
    let StatementKind::Declaration(Declaration { kind: DeclarationKind::Type(matrix), .. }) = &file.statements[0].kind else { panic!() };
    assert!(matrix.generics.iter().all(|parameter| parameter.is_const));
    parse_ok("var pixel: Array<UInt8, 4> = Array.filled(255)\nconst m = Matrix<2, 3>()");
    assert!(first_error("type Broken<const Size> {}").contains("needs a type"));

    parse_ok("fn bounds(values: List<Int>): (lowest: Int, highest: Int)? { None }");
    let ExpressionKind::Tuple(items) = expression("const range = (lowest: 1, highest: 9)") else { panic!() };
    assert_eq!(items[1].label.as_ref().map(|label| label.text.as_str()), Some("highest"));
    assert!(matches!(expression("const grouped = (1 + 2)"), ExpressionKind::Binary { .. }));

    let file = parse_ok("fn audit(entry: Show & Encode, entries: List<Show & Hash>) where Item: Hash & Equals {}");
    let StatementKind::Declaration(Declaration { kind: DeclarationKind::Function(function), .. }) = &file.statements[0].kind else {
        panic!()
    };
    assert!(matches!(function.parameters[0].annotation.as_ref().unwrap().kind, TypeKind::Intersection(_)));
    assert_eq!(function.where_clauses[0].bounds.len(), 2);

    // `+` in a type position recovers as `&`, with one diagnostic and no follow-up errors
    let parsed = torb_syntax::parse("fn audit(entry: Show + Encode + Hash) {}");
    assert_eq!(parsed.diagnostics.len(), 1);
    assert_eq!(parsed.diagnostics[0].message, "Traits are combined with `&`: `Compare & Show`");
    let StatementKind::Declaration(Declaration { kind: DeclarationKind::Function(recovered), .. }) = &parsed.file.statements[0].kind else {
        panic!()
    };
    let TypeKind::Intersection(members) = &recovered.parameters[0].annotation.as_ref().unwrap().kind else { panic!() };
    assert_eq!(members.len(), 3);

    // Still a comparison
    parse_ok("const small = count < 16\nconst between = 0 < index && index < 16");
}
