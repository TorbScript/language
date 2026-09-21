//! Text in, text out for every condition of the canon, in both directions.

use super::{rewrite, Rule};

/// What the default rules (`calls` and `strings`) make of a source, and what a second run makes of that: the canon is
/// idempotent, so the two are the same.
fn canon(source: &str) -> String {
    apply(source, &[Rule::Calls, Rule::Strings])
}

fn apply(source: &str, rules: &[Rule]) -> String {
    let once = rewrite(source, rules);
    assert!(once.dropped.is_empty(), "the safety net dropped an edit of `{source}`: {:?}", once.dropped);
    let twice = rewrite(&once.text, rules);
    assert_eq!(twice.text, once.text, "not idempotent: `{source}`");
    once.text
}

/// The source is already in the canon: nothing changes, and nothing is reported.
fn unchanged(source: &str) {
    assert_eq!(canon(source), source);
}

// --- The calls that become commands ---------------------------------------------------------------------------------

#[test]
fn a_call_in_command_position_loses_its_parentheses() {
    assert_eq!(canon("print(\"Hello\")"), "print \"Hello\"");
    assert_eq!(canon("fn f() {\n  return Fail(problem)\n}"), "fn f() {\n  return Fail problem\n}");
    assert_eq!(canon("const role = Role(name)"), "const role = Role name");
    assert_eq!(canon("var role = Role(name)"), "var role = Role name");
    assert_eq!(canon("public const role = Role(name)"), "public const role = Role name");
    assert_eq!(canon("fn f() {\n  role = Role(name)\n}"), "fn f() {\n  role = Role name\n}");
    assert_eq!(canon("const email = Email.tryFrom(text)"), "const email = Email.tryFrom text");
    assert_eq!(canon("names.map(Role)"), "names.map Role");
    assert_eq!(canon("builder.add(CStatement.Break)"), "builder.add CStatement.Break");
    assert_eq!(canon("self.builder.add(item)"), "self.builder.add item");
    assert_eq!(canon("route(\"/health\", to: \"health\")"), "route \"/health\", to: \"health\"");
}

#[test]
fn a_call_nested_in_an_argument_keeps_its_parentheses() {
    assert_eq!(canon("fn f() {\n  return Ok(Some(x))\n}"), "fn f() {\n  return Ok Some(x)\n}");
    assert_eq!(canon("const x = f(g(h(1)))\n"), "const x = f g(h(1))\n");
    // In a list, a tuple, a map and an interpolation
    unchanged("const x = [f(1), g(2)]");
    unchanged("const x = (f(1), g(2))");
    unchanged("const x = [\"a\": f(1)]");
    unchanged("const x = \"value: {f(1)}\"");
    // Behind an operator, and as the target of one
    unchanged("const x = f(1) + g(2)");
    unchanged("const x = -f(1)");
    // As the target of a member access, an index, a `?` and another call
    unchanged("const x = f(1).length()");
    unchanged("const x = f(1)[0]");
    unchanged("const x = f(1)?");
    unchanged("const x = f(1)(2)");
}

#[test]
fn a_call_in_the_head_of_a_statement_keeps_its_parentheses() {
    unchanged("fn f() {\n  if ready(now) {\n    stop()\n  }\n}");
    unchanged("fn f() {\n  while ready(now) {\n    stop()\n  }\n}");
    unchanged("fn f() {\n  for item in items(of: list) {\n    stop()\n  }\n}");
    unchanged("fn f() {\n  match kind(of: item) {\n    _ => 0\n  }\n}");
    unchanged("fn f() {\n  if const Some(x) = lookup(id) {\n    stop()\n  }\n}");
}

#[test]
fn a_call_without_arguments_keeps_its_parentheses() {
    unchanged("const length = list.length()");
    unchanged("fn f() {\n  stop()\n}");
}

#[test]
fn a_first_argument_that_cannot_start_a_command_keeps_the_parentheses() {
    // `f (a)` is a tuple to the eye and a group to the parser, `f [1]` is indexing, `f -1` a subtraction
    unchanged("const x = f((a, b))");
    unchanged("const x = f([1, 2])");
    unchanged("const x = f(-1)");
    unchanged("const x = f(!ready)");
    unchanged("const x = f(.Case)");
    unchanged("const x = f(...items)");
    // `as`, `by` and `from` are contextual keywords: as a first argument they would end the command
    unchanged("const x = f(from)");
    unchanged("const x = f(as)");
    // As a label they are fine
    assert_eq!(canon("const x = f(from: a)"), "const x = f from: a");
}

#[test]
fn an_operator_at_the_top_level_of_an_argument_keeps_the_parentheses() {
    unchanged("fn f() {\n  assert(sum == 3)\n}");
    unchanged("fn f() {\n  print(count + 1)\n}");
    unchanged("const x = f(a ?? b)");
    unchanged("const x = f(a?)");
    unchanged("const x = f(0..10)");
    unchanged("const x = f(a && b)");
    unchanged("const x = f(a, b > c)");
    unchanged("const x = f(a as: b)");
    // Inside of an argument an operator is none of the command's business
    assert_eq!(canon("const x = f(g(a + b))"), "const x = f g(a + b)");
}

#[test]
fn arguments_over_several_lines_keep_the_parentheses() {
    unchanged("const x = f(\n  a,\n  b,\n)");
    unchanged("const x = f(a,\n  b)");
}

#[test]
fn a_brace_between_the_parentheses_keeps_them() {
    // Without the parentheses the `{` would become the trailing closure of `print`
    unchanged("fn f() {\n  print(numbers.map({ _ * 2 }))\n}");
    unchanged("const x = f(if ready { 1 } else { 2 })");
    unchanged("fn f() {\n  print(match x {\n    _ => 1\n  })\n}");
}

#[test]
fn a_trailing_closure_comes_along() {
    assert_eq!(canon("test(\"adds\") {\n  stop()\n}"), "test \"adds\" {\n  stop()\n}");
    // Without parentheses in the source there is nothing to take away
    unchanged("html {\n  stop()\n}");
    unchanged("f() {\n  stop()\n}");
}

#[test]
fn a_callee_that_is_not_a_path_keeps_the_parentheses() {
    unchanged("const x = load<Config>(path)");
    unchanged("const x = pair.0(a)");
    unchanged("const x = maybe?.run(a)");
    unchanged("const x = items[0](a)");
    unchanged("const x = (a).b(c)");
}

#[test]
fn a_command_in_front_of_a_comma_keeps_its_parentheses() {
    // An arm of a `match` on one line: `.A => print 1, .B => ...` would read `.B => ...` as another argument
    unchanged("fn f() {\n  match x {\n    .A => print(1), .B => print(2)\n  }\n}");
    // The fields of a case are separated by commas too
    unchanged("type Shape {\n  case Circle(radius: Float = scaled(1.0), color: Int = 0)\n}");
}

#[test]
fn a_default_of_a_parameter_is_not_command_position_but_a_field_is() {
    unchanged("fn f(limit: Int = maximum(1)) {}");
    assert_eq!(canon("type Config {\n  limit: Int = maximum(1)\n}"), "type Config {\n  limit: Int = maximum 1\n}");
}

#[test]
fn the_body_of_a_match_arm_and_of_a_closure_are_command_positions() {
    assert_eq!(canon("fn f() {\n  match x {\n    .A => Ok(x)\n  }\n}"), "fn f() {\n  match x {\n    .A => Ok x\n  }\n}");
    assert_eq!(canon("const f = { x =>\n  Ok(x)\n}"), "const f = { x =>\n  Ok x\n}");
    // The guard of an arm is a head, like the head of an `if`
    unchanged("fn f() {\n  match x {\n    a if ready(a) => 0\n  }\n}");
}

/// A command whose callee is a field of the type it stands in does not call it, it **writes** it (gap 15, and
/// "calling a function in a field always needs parentheses"). Neither direction may touch such a call.
#[test]
fn a_call_of_a_field_of_the_enclosing_type_is_left_exactly_as_it_is() {
    unchanged("type Fold {\n  step: (Int) => Int\n  fn add(var self) {\n    state = step(1)\n  }\n}");
    unchanged("type Fold {\n  step: (Int) => Int\n  fn add(var self) {\n    state = self.step(1)\n  }\n}");
    unchanged("type Config {\n  port: Int\n  fn fill(var self) {\n    port 8080\n  }\n}");
    unchanged("type Config {\n  port: Int\n  fn fill(var self) {\n    port port + 1\n  }\n}");
    // A method of the same name as a field of another type is still converted
    assert_eq!(
        canon("type Config {\n  port: Int\n  fn fill(var self) {\n    log(port)\n  }\n}"),
        "type Config {\n  port: Int\n  fn fill(var self) {\n    log port\n  }\n}"
    );
    // Outside of the type the name is nobody's field
    assert_eq!(canon("type Config {\n  port: Int\n}\nport(8080)"), "type Config {\n  port: Int\n}\nport 8080");
}

#[test]
fn a_space_or_a_comment_in_front_of_the_parentheses_keeps_them() {
    unchanged("const x = f (a)");
    unchanged("const x = f/* why */(a)");
    unchanged("const x = f(a )");
    unchanged("const x = f(a,)");
}

// --- The commands that get parentheses ------------------------------------------------------------------------------

#[test]
fn a_command_with_an_operator_in_an_argument_gets_parentheses() {
    assert_eq!(canon("fn f() {\n  print count + 1\n}"), "fn f() {\n  print(count + 1)\n}");
    assert_eq!(canon("fn f() {\n  print a, b == c\n}"), "fn f() {\n  print(a, b == c)\n}");
    assert_eq!(canon("fn f() {\n  print value?\n}"), "fn f() {\n  print(value?)\n}");
    assert_eq!(canon("fn f() {\n  print 0..10\n}"), "fn f() {\n  print(0..10)\n}");
    assert_eq!(canon("fn f() {\n  print a ?? b\n}"), "fn f() {\n  print(a ?? b)\n}");
}

#[test]
fn a_command_over_several_lines_gets_parentheses() {
    assert_eq!(canon("fn f() {\n  print a,\n    b\n}"), "fn f() {\n  print(a,\n    b)\n}");
}

#[test]
fn a_command_keeps_its_trailing_closure_outside_of_the_parentheses() {
    assert_eq!(canon("fn f() {\n  retry 3 + 1 {\n    stop()\n  }\n}"), "fn f() {\n  retry(3 + 1) {\n    stop()\n  }\n}");
}

#[test]
fn a_command_in_a_default_of_a_case_field_gets_parentheses() {
    assert_eq!(
        canon("type Shape {\n  case Circle(radius: Float = scaled 1.0)\n}"),
        "type Shape {\n  case Circle(radius: Float = scaled(1.0))\n}"
    );
}

// --- Multi-line strings ---------------------------------------------------------------------------------------------

#[test]
fn a_flush_left_string_is_indented() {
    // A brace inside of a string of a test source needs `\{`, or it starts an interpolation
    assert_eq!(
        canon("fn f() {\n  const source = \"\"\"\nfn main() \\{\n  print \"x\"\n\\}\n\"\"\"\n}"),
        "fn f() {\n  const source = \"\"\"\n    fn main() \\{\n      print \"x\"\n    \\}\n    \"\"\"\n}"
    );
}

#[test]
fn an_over_indented_string_is_brought_back() {
    assert_eq!(
        canon("const text = \"\"\"\n        first\n          nested\n        \"\"\""),
        "const text = \"\"\"\n  first\n    nested\n  \"\"\""
    );
}

#[test]
fn a_string_that_is_already_in_the_form_is_left_alone() {
    unchanged("const text = \"\"\"\n  first\n  second\n  \"\"\"");
    unchanged("fn f() {\n  const text = \"\"\"\n    first\n    \"\"\"\n}");
}

#[test]
fn a_blank_line_inside_a_string_becomes_empty() {
    assert_eq!(canon("const text = \"\"\"\n  first\n   \t \n  last\n  \"\"\""), "const text = \"\"\"\n  first\n\n  last\n  \"\"\"");
}

#[test]
fn a_raw_string_and_an_interpolated_one_are_indented_too() {
    assert_eq!(canon("const text = r\"\"\"\nfirst\\n\n\"\"\""), "const text = r\"\"\"\n  first\\n\n  \"\"\"");
    assert_eq!(canon("const text = \"\"\"\na{1}\nb\n\"\"\""), "const text = \"\"\"\n  a{1}\n  b\n  \"\"\"");
    assert_eq!(canon("const text = \"\"\"\n\\{literal}\n\"\"\""), "const text = \"\"\"\n  \\{literal}\n  \"\"\"");
}

#[test]
fn a_one_line_string_is_left_alone() {
    unchanged("const text = \"\"\" content \"\"\"");
    unchanged("const text = \"one line\"");
}

#[test]
fn content_on_the_line_of_the_opening_quotes_moves_to_the_next_line() {
    assert_eq!(canon("const text = \"\"\"first\nsecond\n\"\"\""), "const text = \"\"\"\n  first\n  second\n  \"\"\"");
    // The whitespace between the quotes and the content is the reference of that string and goes with it
    assert_eq!(canon("const text = r\"\"\"  first\n    nested\n  last\"\"\""), "const text = r\"\"\"\n  first\n    nested\n  last\"\"\"");
    // A line that is indented less than the reference is a lexer error, so that file never reaches the rule at all
    let outcome = rewrite("const text = r\"\"\"  first\nflush\n  \"\"\"", &[Rule::Strings]);
    assert_eq!(outcome.text, "const text = r\"\"\"  first\nflush\n  \"\"\"");
    assert_eq!(outcome.skipped, vec!["it does not parse".to_string()]);
}

#[test]
fn the_closing_quotes_may_sit_on_the_last_content_line() {
    assert_eq!(canon("const text = \"\"\"\nfirst\nlast\"\"\""), "const text = \"\"\"\n  first\n  last\"\"\"");
}

#[test]
fn the_value_of_every_rewritten_string_is_the_one_from_before() {
    for source in [
        "const text = \"\"\"\nfn main() \\{\n  print \"x\"\n\\}\n\"\"\"",
        "const text = \"\"\"\n        first\n          nested\n\n        last\n        \"\"\"",
        "const text = r\"\"\"\nfirst\\n\n{ignored}\n\"\"\"",
        "const text = \"\"\"\na{1}\n  b{2}\n\"\"\"",
        "const text = r\"\"\"  const found = 1\n    .Circle(radius) => radius\n  print found\"\"\"",
        "const text = r\"\"\"static void f(void) \\{\n  goto L1;\n\\}\"\"\"",
    ] {
        let rewritten = canon(source);
        assert_ne!(rewritten, source, "nothing happened to `{source}`");
        assert_eq!(text_of(&rewritten), text_of(source), "the value changed: `{source}`");
    }
}

/// The literal parts of the single string literal of a source, exactly as the lexer read them. An interpolation is a
/// range of the source and moves with it, so it only counts as a part that is there.
fn text_of(source: &str) -> Vec<String> {
    use torb_syntax::token::{TextPart, TokenKind};
    let lexed = torb_syntax::lexer::lex(source);
    assert!(lexed.diagnostics.is_empty(), "{:?}", lexed.diagnostics);
    let token = lexed.tokens.iter().find(|token| matches!(token.kind, TokenKind::Text(_))).expect("a string");
    let TokenKind::Text(parts) = &token.kind else { unreachable!() };
    parts
        .iter()
        .map(|part| match part {
            TextPart::Literal(text) => format!("literal {text:?}"),
            TextPart::Expression(_) => "an interpolation".to_string(),
        })
        .collect()
}

// --- The rule that is not part of the default run -------------------------------------------------------------------

#[test]
fn imported_case_patterns_are_off_by_default() {
    unchanged("use Option.None from \"std/core\"\nfn f() {\n  match x {\n    .None => 0\n    _ => 1\n  }\n}");
}

#[test]
fn an_imported_payload_less_case_loses_its_dot() {
    let rules = [Rule::ImportedCasePatterns];
    assert_eq!(
        apply("use Option.None from \"std/core\"\nfn f() {\n  match x {\n    .None => 0\n    _ => 1\n  }\n}", &rules),
        "use Option.None from \"std/core\"\nfn f() {\n  match x {\n    None => 0\n    _ => 1\n  }\n}"
    );
    // Without `from` the path is resolved in the file's own scope, and a case out of it counts the same way
    assert_eq!(
        apply("use Shape.Empty\nfn f() {\n  match x {\n    .Empty => 0\n    _ => 1\n  }\n}", &rules),
        "use Shape.Empty\nfn f() {\n  match x {\n    Empty => 0\n    _ => 1\n  }\n}"
    );
    // A single name is a whole export and not a case, so it shadows instead of importing one
    assert_eq!(
        apply("use Empty from \"./shape\"\nfn f() {\n  match x {\n    .Empty => 0\n    _ => 1\n  }\n}", &rules),
        "use Empty from \"./shape\"\nfn f() {\n  match x {\n    .Empty => 0\n    _ => 1\n  }\n}"
    );
    // The prelude's four are imported everywhere
    assert_eq!(
        apply("fn f() {\n  match x {\n    .None => 0\n    _ => 1\n  }\n}", &rules),
        "fn f() {\n  match x {\n    None => 0\n    _ => 1\n  }\n}"
    );
    // A case with a payload is written the way the parser reads it already
    assert_eq!(
        apply("fn f() {\n  match x {\n    .Some(value) => value\n    _ => 0\n  }\n}", &rules),
        "fn f() {\n  match x {\n    .Some(value) => value\n    _ => 0\n  }\n}"
    );
    // A case that is not imported keeps its dot
    assert_eq!(
        apply("fn f() {\n  match x {\n    .Dot => 0\n    _ => 1\n  }\n}", &rules),
        "fn f() {\n  match x {\n    .Dot => 0\n    _ => 1\n  }\n}"
    );
    // A file that declares a name of the prelude shadows it
    assert_eq!(
        apply("type None {}\nfn f() {\n  match x {\n    .None => 0\n    _ => 1\n  }\n}", &rules),
        "type None {}\nfn f() {\n  match x {\n    .None => 0\n    _ => 1\n  }\n}"
    );
}

#[test]
fn unused_bindings_are_off_by_default() {
    unchanged("fn f() {\n  match x {\n    Some(value) => true\n    _ => false\n  }\n}");
}

#[test]
fn a_binding_of_a_refutable_pattern_that_is_nowhere_in_the_arm_becomes_a_wildcard() {
    let rules = [Rule::UnusedBindings];
    assert_eq!(
        apply("fn f() {\n  match x {\n    Some(value) => true\n    _ => false\n  }\n}", &rules),
        "fn f() {\n  match x {\n    Some(_) => true\n    _ => false\n  }\n}"
    );
    // A bare name binds and matches everything, which is the arm the rule is really about
    assert_eq!(
        apply("fn f() {\n  match x {\n    1 => 0\n    limit => 2\n  }\n}", &rules),
        "fn f() {\n  match x {\n    1 => 0\n    _ => 2\n  }\n}"
    );
    // Every position of a tuple, a list and a case, as deep as they go
    assert_eq!(
        apply("fn f() {\n  match x {\n    (a, [b, Some(c)]) => 0\n    _ => 1\n  }\n}", &rules),
        "fn f() {\n  match x {\n    (_, [_, Some(_)]) => 0\n    _ => 1\n  }\n}"
    );
}

#[test]
fn a_binding_the_arm_uses_is_left_exactly_as_it_is() {
    let rules = [Rule::UnusedBindings];
    let leaves = |source: &str| assert_eq!(apply(source, &rules), source);
    // In the body, in a guard, and in the body of the arm a guard belongs to
    leaves("fn f() {\n  match x {\n    Some(value) => value\n    _ => 0\n  }\n}");
    leaves("fn f() {\n  match x {\n    Some(value) if value > 1 => 0\n    _ => 1\n  }\n}");
    leaves("fn f() {\n  match x {\n    Some(value) if ready() => value\n    _ => 0\n  }\n}");
    // The use may be anywhere in the body, however deep
    leaves("fn f() {\n  match x {\n    Some(value) => {\n      print \"got {value}\"\n    }\n    _ => {}\n  }\n}");
    // `_name` is the documented form and is never rewritten
    leaves("fn f() {\n  match x {\n    Some(_reason) => true\n    _ => false\n  }\n}");
    // A name that stands in a comment or a string counts as a use: the rule sees text, and the checker says the rest
    leaves("fn f() {\n  match x {\n    Some(value) => {\n      // value is ignored here\n      0\n    }\n    _ => 1\n  }\n}");
    // A longer name that only contains the binding's name is not a use of it
    assert_eq!(
        apply("fn f() {\n  match x {\n    Some(value) => values\n    _ => 0\n  }\n}", &rules),
        "fn f() {\n  match x {\n    Some(_) => values\n    _ => 0\n  }\n}"
    );
}

#[test]
fn every_alternative_of_a_pattern_is_rewritten_together() {
    let rules = [Rule::UnusedBindings];
    // The language requires every alternative to bind the same names, so all of them have to change or none
    assert_eq!(
        apply("fn f() {\n  match x {\n    Some(value) | Ok(value) => true\n    _ => false\n  }\n}", &rules),
        "fn f() {\n  match x {\n    Some(_) | Ok(_) => true\n    _ => false\n  }\n}"
    );
    assert_eq!(
        apply("fn f() {\n  match x {\n    Some(value) | Ok(value) => value\n    _ => 0\n  }\n}", &rules),
        "fn f() {\n  match x {\n    Some(value) | Ok(value) => value\n    _ => 0\n  }\n}"
    );
}

#[test]
fn the_head_of_an_if_and_of_a_while_are_refutable_too() {
    let rules = [Rule::UnusedBindings];
    assert_eq!(
        apply("fn f() {\n  if const Some(user) = lookup() {\n    stop()\n  }\n}", &rules),
        "fn f() {\n  if const Some(_) = lookup() {\n    stop()\n  }\n}"
    );
    assert_eq!(
        apply("fn f() {\n  if var Some(user) = lookup() {\n    user.stop()\n  }\n}", &rules),
        "fn f() {\n  if var Some(user) = lookup() {\n    user.stop()\n  }\n}"
    );
    // The `else` is outside the binding's scope, so a name that only stands there is no use of it
    assert_eq!(
        apply("fn f() {\n  const x = if const Some(user) = lookup() {\n    1\n  } else {\n    user\n  }\n}", &rules),
        "fn f() {\n  const x = if const Some(_) = lookup() {\n    1\n  } else {\n    user\n  }\n}"
    );
    assert_eq!(
        apply("fn f() {\n  while const Some(item) = next() {\n    count()\n  }\n}", &rules),
        "fn f() {\n  while const Some(_) = next() {\n    count()\n  }\n}"
    );
    assert_eq!(
        apply("fn f() {\n  while const Some(item) = next() {\n    count(item)\n  }\n}", &rules),
        "fn f() {\n  while const Some(item) = next() {\n    count(item)\n  }\n}"
    );
}

#[test]
fn an_irrefutable_binding_is_not_part_of_the_rule() {
    let rules = [Rule::UnusedBindings];
    let leaves = |source: &str| assert_eq!(apply(source, &rules), source);
    leaves("fn f() {\n  const value = 1\n}");
    leaves("fn f() {\n  const (first, second) = pair\n}");
    leaves("fn f() {\n  for item in items {\n    stop()\n  }\n}");
    leaves("fn f() {\n  items.each({ item => stop() })\n}");
    leaves("fn f(value: Int) {\n  stop()\n}");
}

// --- `while true` becomes `loop` ------------------------------------------------------------------------------------

#[test]
fn an_endless_while_becomes_a_loop() {
    let rules = [Rule::Loops];
    assert_eq!(
        apply(
            "fn f() {
  while true {
    step()
  }
}",
            &rules
        ),
        "fn f() {
  loop {
    step()
  }
}"
    );
    // The body is rewritten too, however deep it sits
    assert_eq!(
        apply(
            "fn f() {
  loop {
    while true {
      step()
    }
  }
}",
            &rules
        ),
        "fn f() {
  loop {
    loop {
      step()
    }
  }
}"
    );
    // Everything from the `{` on stays byte-identical, comments and line endings included
    assert_eq!(
        apply(
            "fn f() {
  while true { // forever
    step()
  }
}
",
            &rules
        ),
        "fn f() {
  loop { // forever
    step()
  }
}
"
    );
}

#[test]
fn every_other_loop_is_left_alone() {
    let rules = [Rule::Loops];
    let leaves = |source: &str| assert_eq!(apply(source, &rules), source);
    leaves(
        "fn f() {
  while false {
    step()
  }
}",
    );
    leaves(
        "fn f() {
  while goes_on {
    step()
  }
}",
    );
    leaves(
        "fn f() {
  while const Some(item) = next() {
    step(item)
  }
}",
    );
    leaves(
        "fn f() {
  loop {
    step()
  }
}",
    );
}

// --- The safety net -------------------------------------------------------------------------------------------------

#[test]
fn a_file_that_does_not_parse_is_left_alone() {
    let outcome = rewrite("fn f( {\n", &[Rule::Calls, Rule::Strings]);
    assert_eq!(outcome.text, "fn f( {\n");
    assert_eq!(outcome.skipped, vec!["it does not parse".to_string()]);
}

#[test]
fn line_endings_and_everything_around_an_edit_stay_as_they_are() {
    assert_eq!(
        canon("// a comment\r\nconst role = Role(name)\r\n\r\n// and another\r\n"),
        "// a comment\r\nconst role = Role name\r\n\r\n// and another\r\n"
    );
}

#[test]
fn the_canon_of_the_examples_of_the_decision_is_what_the_decision_says() {
    assert_eq!(canon("fn f() {\n  items.add(Item(name, 2))\n}"), "fn f() {\n  items.add Item(name, 2)\n}");
    assert_eq!(canon("fn f() {\n  return Ok(value)\n}"), "fn f() {\n  return Ok value\n}");
}
