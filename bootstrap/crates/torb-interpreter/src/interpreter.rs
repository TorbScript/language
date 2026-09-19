//! Evaluates the syntax tree directly. There is no type checker in front of it: a correct program runs correctly, an
//! incorrect one fails at runtime (or, where types would have decided something, behaves as documented in the README).

use std::rc::Rc;

use torb_syntax::ast::{
    self, Argument, BinaryOperator, Block, CallStyle, Condition, Expression, ExpressionKind, Pattern, PatternKind, Statement,
    StatementKind, TextSegment, TypeKind, TypeReference, UnaryOperator,
};
use torb_syntax::Span;

use crate::natives;
use crate::profile;
use crate::program::{builtin_type, Constant, FunctionInfo, Item, Module, Program, TypeInfo};
use crate::value::{compare_values, Closure, Environment, Function, ImplicitCase, Object, Range, Tuple, Value};

pub enum Flow {
    Return(Value),
    Break,
    Continue,
    Failure(Box<Failure>),
}

pub struct Failure {
    pub message: String,
    pub location: Option<String>,
    pub trace: Vec<String>,
}

pub type Eval<T = Value> = Result<T, Flow>;

/// A failure without a location. The nearest call site adds it.
pub fn failure(message: impl Into<String>) -> Flow {
    Flow::Failure(Box::new(Failure { message: message.into(), location: None, trace: Vec::new() }))
}

/// Evaluated arguments of a call to something that is not a declared function.
#[derive(Default)]
pub struct Arguments {
    pub positional: Vec<Value>,
    pub labeled: Vec<(&'static str, Value)>,
}

impl Arguments {
    pub fn positional(values: Vec<Value>) -> Arguments {
        Arguments { positional: values, labeled: Vec::new() }
    }

    /// The argument at a position, or the one with the label of that parameter.
    pub fn get(&self, index: usize, label: &str) -> Option<Value> {
        self.positional.get(index).or_else(|| self.labeled.iter().find(|(name, _)| *name == label).map(|(_, value)| value)).cloned()
    }

    pub fn required(&self, index: usize, label: &str) -> Eval {
        self.get(index, label).ok_or_else(|| failure(format!("Missing argument `{label}`")))
    }
}

struct Frame {
    return_type: Option<&'static TypeReference>,
    module: Rc<Module>,
}

/// A `var` path, resolved: a binding and the way from it to the value. Index expressions are evaluated once.
pub struct Place {
    environment: Rc<Environment>,
    slot: usize,
    steps: Vec<Step>,
}

enum Step {
    Field(usize),
    Index(Value),
    /// Always the last step
    Slice(usize, usize),
}

#[derive(Default)]
pub struct TestReport {
    pub groups: Vec<String>,
    pub passed: usize,
    pub failed: usize,
}

pub struct Interpreter {
    pub program: Program,
    pub arguments: Vec<String>,
    pub tests: TestReport,
    frames: Vec<Frame>,
    /// Scopes nobody references anymore. Every block, every turn of a loop and every call needs one, so allocating
    /// them was the single biggest cost of a run; a closure keeps its scope alive, which is why one is only taken
    /// back when its reference count is down to one again.
    scopes: Vec<Rc<Environment>>,
    /// The buffers of matched arguments, for the same reason: one per call of a declared function.
    argument_buffers: Vec<Vec<Option<Value>>>,
}

struct BoundArguments {
    values: Vec<Option<Value>>,
    /// `var` parameters: what the function leaves in the parameter is written back to the path
    places: Vec<(&'static str, Place)>,
}

impl Interpreter {
    pub fn new(program: Program, arguments: Vec<String>) -> Interpreter {
        Interpreter {
            program,
            arguments,
            tests: TestReport::default(),
            frames: Vec::new(),
            scopes: Vec::new(),
            argument_buffers: Vec::new(),
        }
    }

    /// A buffer of `length` empty places for the arguments of one call, from the pool if there is one.
    fn argument_buffer(&mut self, length: usize) -> Vec<Option<Value>> {
        let mut buffer = self.argument_buffers.pop().unwrap_or_default();
        buffer.clear();
        buffer.resize(length, None);
        buffer
    }

    fn release_argument_buffer(&mut self, mut buffer: Vec<Option<Value>>) {
        if self.argument_buffers.len() < 512 {
            buffer.clear();
            self.argument_buffers.push(buffer);
        }
    }

    /// A scope below another one, from the pool if there is one.
    fn scope(&mut self, parent: &Rc<Environment>) -> Rc<Environment> {
        self.scope_of(parent.module.clone(), parent.clone())
    }

    /// The scope of a call: its module is the one the function was written in, not the one of its caller.
    fn scope_of(&mut self, module: Rc<Module>, parent: Rc<Environment>) -> Rc<Environment> {
        match self.scopes.pop() {
            Some(mut scope) => {
                Rc::get_mut(&mut scope).expect("a scope in the pool is not referenced anywhere").reuse(module, parent);
                scope
            }
            None => Environment::of_call(module, parent),
        }
    }

    /// Offers a scope back to the pool. Nothing happens if something still references it.
    fn release(&mut self, mut scope: Rc<Environment>) {
        if self.scopes.len() >= 512 {
            return;
        }
        if let Some(inner) = Rc::get_mut(&mut scope) {
            inner.empty();
            self.scopes.push(scope);
        }
    }

    /// Runs the top-level code of the entry file. A top-level `?` ends the program with the error.
    pub fn run(&mut self) -> Result<(), Failure> {
        let entry = self.program.entry.clone();
        let environment = entry.top_level();
        self.frames.push(Frame { return_type: None, module: entry.clone() });
        let result = self.exec_statements(&entry.file.statements, &environment);
        self.frames.pop();
        match result {
            Ok(_) | Err(Flow::Break | Flow::Continue) => Ok(()),
            Err(Flow::Return(Value::Result(Err(error)))) => {
                let message = self.show(&error, true).unwrap_or_else(|_| "an error".to_string());
                Err(Failure { message, location: None, trace: Vec::new() })
            }
            Err(Flow::Return(_)) => Ok(()),
            Err(Flow::Failure(failure)) => Err(*failure),
        }
    }

    fn located<T>(&self, result: Eval<T>, environment: &Environment, span: Span) -> Eval<T> {
        result.map_err(|flow| match flow {
            Flow::Failure(mut failure) => {
                if failure.location.is_none() {
                    failure.location = Some(environment.module.location(span));
                }
                Flow::Failure(failure)
            }
            other => other,
        })
    }

    fn fail<T>(&self, environment: &Environment, span: Span, message: impl Into<String>) -> Eval<T> {
        self.located(Err(failure(message)), environment, span)
    }

    // --- Statements -------------------------------------------------------------------------------------------------

    pub fn exec_block(&mut self, block: &'static Block, environment: &Rc<Environment>) -> Eval {
        let scope = self.scope(environment);
        let result = self.exec_statements(&block.statements, &scope);
        self.release(scope);
        result
    }

    fn exec_statements(&mut self, statements: &'static [Statement], environment: &Rc<Environment>) -> Eval {
        profile::add("hoist.scan", statements.len());
        // Local functions are hoisted
        for statement in statements {
            if let StatementKind::Declaration(ast::Declaration { kind: ast::DeclarationKind::Function(function), .. }) = &statement.kind {
                let is_top_level = environment.is_top_level;
                if !is_top_level {
                    let info =
                        FunctionInfo::new(&function.name.text, function, environment.module.clone(), None, Some(environment.clone()));
                    let value = Value::Function(Rc::new(Function::Declared { info: Rc::new(info), receiver: None }));
                    environment.declare(&function.name.text, value, false);
                }
            }
        }
        let mut result = Value::Void;
        for statement in statements {
            result = self.exec_statement(statement, environment)?;
        }
        Ok(result)
    }

    fn exec_statement(&mut self, statement: &'static Statement, environment: &Rc<Environment>) -> Eval {
        if profile::is_enabled() {
            profile::count(statement_kind(&statement.kind));
        }
        match &statement.kind {
            StatementKind::Declaration(declaration) => {
                let is_top_level = environment.is_top_level;
                let is_function = matches!(declaration.kind, ast::DeclarationKind::Function(_));
                if !is_top_level && !is_function {
                    return self.fail(
                        environment,
                        statement.span,
                        "The bootstrap interpreter only supports local functions, no local types",
                    );
                }
            }
            StatementKind::Binding(binding) => {
                let value = self.binding_value(binding, environment)?;
                if !self.bind_pattern(&binding.pattern, &value, environment, binding.is_var)? {
                    return self.fail(environment, binding.pattern.span, format!("The pattern does not match {}", self.describe(&value)));
                }
            }
            StatementKind::Assignment { target, value } => {
                let value = self.eval(value, environment)?;
                let Some(place) = self.resolve_place(target, environment)? else {
                    return self.fail(environment, target.span, "This is not something that can be assigned to");
                };
                let value = match (self.peek(&place), &value) {
                    (Ok(Value::Float(_)), _) => to_float(value),
                    (Ok(Value::Object(current)), Value::Implicit(implicit)) => self.resolve_implicit(implicit, &current.info)?,
                    _ => value,
                };
                let result = self.put(&place, value);
                self.located(result, environment, target.span)?;
            }
            StatementKind::For { pattern, iterable, body } => {
                let iterable_value = self.eval(iterable, environment)?;
                self.exec_for(pattern, iterable_value, body, environment, iterable.span)?;
            }
            StatementKind::While { condition, body } => loop {
                let scope = self.scope(environment);
                let goes_on = self.check_condition(condition, &scope)?;
                if !goes_on {
                    self.release(scope);
                    break;
                }
                let outcome = self.exec_statements(&body.statements, &scope);
                self.release(scope);
                match outcome {
                    Err(Flow::Break) => break,
                    Ok(_) | Err(Flow::Continue) => {}
                    Err(other) => return Err(other),
                }
            },
            StatementKind::Return(value) => {
                let value = match value {
                    Some(value) => self.eval(value, environment)?,
                    None => Value::Void,
                };
                return Err(Flow::Return(value));
            }
            StatementKind::Break => return Err(Flow::Break),
            StatementKind::Continue => return Err(Flow::Continue),
            StatementKind::Expression(expression) => return self.eval(expression, environment),
        }
        Ok(Value::Void)
    }

    fn binding_value(&mut self, binding: &'static ast::Binding, environment: &Rc<Environment>) -> Eval {
        // A top-level constant of the entry file is also what the functions of the file see: evaluate it once
        if let (false, PatternKind::Name(name)) = (binding.is_var, &binding.pattern.kind) {
            if environment.is_top_level {
                let item = environment.module.scope.borrow().get(name.as_str()).cloned();
                if let Some(Item::Constant(constant)) = item {
                    if std::ptr::eq(constant.value_expression, &binding.value) {
                        return self.force(&constant);
                    }
                }
            }
        }
        let value = self.eval(&binding.value, environment)?;
        // Only `.Case` needs to know which type `Self` is here, and only then is it worth looking it up
        let owner = match &value {
            Value::Implicit(_) => match environment.lookup("Self") {
                Some(Value::Type(owner)) => Some(owner),
                _ => None,
            },
            _ => None,
        };
        let expected = self.expect(value, binding.annotation.as_ref(), &environment.module, owner.as_ref());
        self.located(expected, environment, binding.value.span)
    }

    fn exec_for(
        &mut self,
        pattern: &'static Pattern,
        iterable: Value,
        body: &'static Block,
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval<()> {
        let run = |interpreter: &mut Interpreter, item: Value| -> Eval<bool> {
            let scope = interpreter.scope(environment);
            if !interpreter.bind_pattern(pattern, &item, &scope, false)? {
                return interpreter.fail(environment, pattern.span, format!("The pattern does not match {}", interpreter.describe(&item)));
            }
            let outcome = interpreter.exec_statements(&body.statements, &scope);
            interpreter.release(scope);
            match outcome {
                Err(Flow::Break) => Ok(false),
                Ok(_) | Err(Flow::Continue) => Ok(true),
                Err(other) => Err(other),
            }
        };
        // Ranges can be infinite, everything else is a copy of the items (changing the collection inside is safe)
        if let Value::Range(range) = iterable {
            let mut current = range.start;
            while range.end.is_none_or(|end| current < end) {
                if !run(self, Value::Int(current))? {
                    break;
                }
                current += 1;
            }
            return Ok(());
        }
        let items = natives::items_of(&iterable);
        let items = self.located(items, environment, span)?;
        for item in items {
            if !run(self, item)? {
                break;
            }
        }
        Ok(())
    }

    fn check_condition(&mut self, condition: &'static Condition, scope: &Rc<Environment>) -> Eval<bool> {
        match condition {
            Condition::Expression(expression) => match self.eval(expression, scope)? {
                Value::Bool(value) => Ok(value),
                other => self.fail(scope, expression.span, format!("A condition must be a Bool, this is {}", self.describe(&other))),
            },
            Condition::Binding { is_var, pattern, value } => {
                let value = self.eval(value, scope)?;
                self.bind_pattern(pattern, &value, scope, *is_var)
            }
        }
    }

    // --- Expressions ------------------------------------------------------------------------------------------------

    /// The interpreter walks the tree, so this runs for every node of a program and is entered 255 million times per
    /// `check ..`. What matters as much as what it does is how large its stack frame is: everything that needs room -
    /// a `String`, a `Vec`, a message - is in a function of its own, which keeps this frame small enough to stay in
    /// the cache while the recursion goes down. Do not inline those back in.
    pub fn eval(&mut self, expression: &'static Expression, environment: &Rc<Environment>) -> Eval {
        let span = expression.span;
        if profile::is_enabled() {
            profile::count(expression_kind(&expression.kind));
        }
        match &expression.kind {
            ExpressionKind::Name(name) => self.lookup_name(name, environment, span),
            ExpressionKind::Call { callee, arguments, style } => {
                let result = self.eval_call(callee, arguments, *style, environment, span);
                self.located(result, environment, span)
            }
            ExpressionKind::Binary { operator, left, right } => self.binary(*operator, left, right, environment, span),
            ExpressionKind::Member { target, name, optional } => self.eval_member(target, name, *optional, environment, span),
            ExpressionKind::Bool(value) => Ok(Value::Bool(*value)),
            ExpressionKind::VoidLiteral => Ok(Value::Void),
            ExpressionKind::Char(value) => Ok(Value::Char(*value)),
            ExpressionKind::Integer(text) => self.eval_integer(text, environment, span),
            ExpressionKind::Index { target, index } => {
                let target_value = self.eval(target, environment)?;
                let index_value = self.eval(index, environment)?;
                let result = self.index(&target_value, &index_value);
                self.located(result, environment, span)
            }
            ExpressionKind::Unary { operator, operand } => {
                let value = self.eval(operand, environment)?;
                let result = self.unary(*operator, value);
                self.located(result, environment, span)
            }
            ExpressionKind::Generic { target, .. } => self.eval(target, environment),
            ExpressionKind::Block(block) => self.exec_block(block, environment),
            ExpressionKind::If { condition, then, otherwise } => self.eval_if(condition, then, otherwise, environment),
            ExpressionKind::Match { subject, arms } => self.eval_match(subject, arms, environment),
            ExpressionKind::Text(segments) => self.eval_text(segments, environment),
            ExpressionKind::Try(inner) => self.eval_try(inner, environment, span),
            _ => self.eval_rest(expression, environment, span),
        }
    }

    #[inline(never)]
    fn eval_integer(&mut self, text: &'static str, environment: &Rc<Environment>, span: Span) -> Eval {
        match parse_integer(text) {
            Some(value) => Ok(Value::Int(value)),
            None => self.fail(environment, span, format!("`{text}` does not fit into an Int")),
        }
    }

    #[inline(never)]
    fn eval_member(
        &mut self,
        target: &'static Expression,
        name: &'static ast::Name,
        optional: bool,
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        let target_value = self.eval(target, environment)?;
        if optional {
            return match target_value {
                Value::Option(None) => Ok(Value::NONE),
                Value::Option(Some(inner)) => {
                    let member = self.member(&inner, &name.text, environment, span)?;
                    Ok(flatten_option(member))
                }
                other => self.fail(environment, span, format!("`?.` needs an Option, this is {}", self.describe(&other))),
            };
        }
        self.member(&target_value, &name.text, environment, span)
    }

    #[inline(never)]
    fn eval_text(&mut self, segments: &'static [TextSegment], environment: &Rc<Environment>) -> Eval {
        if let [TextSegment::Literal(literal)] = segments {
            return Ok(Value::text(literal));
        }
        let mut text = String::new();
        for segment in segments {
            match segment {
                TextSegment::Literal(literal) => text.push_str(literal),
                TextSegment::Expression(inner) => {
                    let value = self.eval(inner, environment)?;
                    let shown = self.show(&value, true);
                    text.push_str(&self.located(shown, environment, inner.span)?);
                }
            }
        }
        Ok(Value::text(&text))
    }

    #[inline(never)]
    fn eval_try(&mut self, inner: &'static Expression, environment: &Rc<Environment>, span: Span) -> Eval {
        let value = self.eval(inner, environment)?;
        match value {
            Value::Option(Some(value)) => Ok((*value).clone()),
            Value::Option(None) => Err(Flow::Return(Value::NONE)),
            Value::Result(Ok(value)) => Ok((*value).clone()),
            Value::Result(Err(error)) => {
                let converted = self.convert_error((*error).clone());
                let converted = self.located(converted, environment, span)?;
                Err(Flow::Return(Value::error(converted)))
            }
            other => self.fail(environment, span, format!("`?` needs an Option or a Result, this is {}", self.describe(&other))),
        }
    }

    #[inline(never)]
    fn eval_if(
        &mut self,
        condition: &'static Condition,
        then: &'static Block,
        otherwise: &'static Option<Box<Expression>>,
        environment: &Rc<Environment>,
    ) -> Eval {
        let scope = self.scope(environment);
        if self.check_condition(condition, &scope)? {
            let result = self.exec_statements(&then.statements, &scope);
            self.release(scope);
            return result;
        }
        self.release(scope);
        match otherwise {
            Some(otherwise) => self.eval(otherwise, environment),
            None => Ok(Value::Void),
        }
    }

    #[inline(never)]
    fn eval_match(&mut self, subject: &'static Expression, arms: &'static [ast::MatchArm], environment: &Rc<Environment>) -> Eval {
        let value = self.eval(subject, environment)?;
        for arm in arms {
            let scope = self.scope(environment);
            if !self.bind_pattern(&arm.pattern, &value, &scope, false)? {
                self.release(scope);
                continue;
            }
            if let Some(guard) = &arm.guard {
                match self.eval(guard, &scope)? {
                    Value::Bool(true) => {}
                    Value::Bool(false) => {
                        self.release(scope);
                        continue;
                    }
                    other => {
                        return self.fail(environment, guard.span, format!("A guard must be a Bool, this is {}", self.describe(&other)))
                    }
                }
            }
            let result = match &arm.body.kind {
                ExpressionKind::Block(block) => self.exec_statements(&block.statements, &scope),
                _ => self.eval(&arm.body, &scope),
            };
            self.release(scope);
            return result;
        }
        self.fail(environment, subject.span, format!("No arm of this `match` matches {}", self.describe(&value)))
    }

    /// The kinds of node that are neither frequent nor small. They are here so that `eval` does not have to make room
    /// for what they need.
    #[inline(never)]
    fn eval_rest(&mut self, expression: &'static Expression, environment: &Rc<Environment>, span: Span) -> Eval {
        match &expression.kind {
            ExpressionKind::Float(text) => match text.replace('_', "").parse() {
                Ok(value) => Ok(Value::Float(value)),
                Err(_) => self.fail(environment, span, format!("`{text}` is not a number")),
            },
            ExpressionKind::ImplicitMember(name) => Ok(Value::Implicit(Rc::new(ImplicitCase { name: &name.text, arguments: None }))),
            ExpressionKind::Tuple(arguments) => {
                let mut tuple = Tuple { labels: Vec::new(), items: Vec::new() };
                for argument in arguments {
                    tuple.labels.push(argument.label.as_ref().map(|label| label.text.as_str()));
                    tuple.items.push(self.eval(&argument.value, environment)?);
                }
                Ok(Value::Tuple(Rc::new(tuple)))
            }
            ExpressionKind::List(arguments) => {
                let mut items = Vec::with_capacity(arguments.len());
                for argument in arguments {
                    let value = self.eval(&argument.value, environment)?;
                    if argument.is_spread {
                        let spread = natives::items_of(&value);
                        items.extend(self.located(spread, environment, argument.value.span)?);
                    } else {
                        items.push(value);
                    }
                }
                Ok(Value::list(items))
            }
            ExpressionKind::Map(entries) => {
                let mut table = crate::value::Table::default();
                for (key, value) in entries {
                    let key = self.eval(key, environment)?;
                    let value = self.eval(value, environment)?;
                    table.insert(key, value);
                }
                Ok(Value::Map(Rc::new(table)))
            }
            ExpressionKind::Range { start, end, inclusive } => {
                let bound = |interpreter: &mut Interpreter, side: &'static Option<Box<Expression>>| -> Eval<Option<i64>> {
                    let Some(side) = side else { return Ok(None) };
                    match interpreter.eval(side, environment)? {
                        Value::Int(value) => Ok(Some(value)),
                        other => interpreter.fail(
                            environment,
                            side.span,
                            format!("A range needs Int bounds, this is {}", interpreter.describe(&other)),
                        ),
                    }
                };
                let start = bound(self, start)?.unwrap_or(0);
                let end = bound(self, end)?.map(|end| if *inclusive { end + 1 } else { end });
                Ok(Value::Range(Range { start, end }))
            }
            ExpressionKind::Closure(closure) => {
                Ok(Value::Function(Rc::new(Function::Closure(Closure { ast: closure, environment: environment.clone(), signature: None }))))
            }
            ExpressionKind::Error => self.fail(environment, span, "This expression did not parse"),
            // Everything else is answered by `eval` itself
            _ => self.fail(environment, span, "This expression did not parse"),
        }
    }

    fn lookup_name(&mut self, name: &'static str, environment: &Rc<Environment>, span: Span) -> Eval {
        if let Some(value) = environment.lookup(name) {
            if let Value::Function(function) = &value {
                if let Function::Lazy(lazy) = &**function {
                    return self.force_lazy(lazy);
                }
            }
            return Ok(value);
        }
        // Members of `self` and of `Self` are implicit
        if let Some(receiver) = environment.lookup("self") {
            if let Some(value) = self.implicit_member(&receiver, name) {
                return Ok(value);
            }
        }
        // Statics of `Self` are implicit, its cases are not: `.Circle` or `Shape.Circle`
        if let Some(Value::Type(owner)) = environment.lookup("Self") {
            if !owner.may_have(name) || owner.case_index(name).is_none() {
                if let Some(value) = self.type_member(&owner, name)? {
                    return Ok(value);
                }
            }
        }
        let item = environment.module.scope.borrow().get(name).cloned();
        let item = item.or_else(|| self.program.prelude.scope.borrow().get(name).cloned());
        match item {
            Some(Item::Value(value)) => return Ok(value),
            Some(Item::Constant(constant)) => return self.force(&constant),
            None => {}
        }
        if let Some(value) = natives::global(name) {
            return Ok(value);
        }
        if let Some(builtin) = builtin_type(name) {
            return Ok(Value::Builtin(builtin));
        }
        // Inside of `extend String { ... }`: the native methods of `self`
        if let Some(receiver) = environment.lookup("self") {
            if !matches!(receiver, Value::Object(_)) {
                return Ok(Value::Function(Rc::new(Function::NativeMethod { receiver, name })));
            }
        }
        self.fail(environment, span, format!("Unknown name `{name}`"))
    }

    /// Whether a bare uppercase pattern name stands for something: a binding of a surrounding scope, a name of the
    /// module or of the prelude, a built-in type, or one of the four cases the interpreter knows without a declaration.
    fn knows_case_name(&self, name: &str, environment: &Rc<Environment>) -> bool {
        if matches!(name, "Some" | "None" | "Ok" | "Fail" | "Self") {
            return true;
        }
        if environment.lookup(name).is_some() || environment.module.scope.borrow().contains_key(name) {
            return true;
        }
        self.program.prelude.scope.borrow().contains_key(name) || builtin_type(name).is_some()
    }

    fn implicit_member(&self, receiver: &Value, name: &'static str) -> Option<Value> {
        match receiver {
            Value::Object(object) if object.info.may_have(name) => {
                if let Some(position) = object.info.field_position(object.case, name) {
                    return Some(object.fields[position].clone());
                }
                profile::count("methods.get");
                if let Some(method) = object.info.methods.borrow().get(name).cloned() {
                    return Some(Value::Function(Rc::new(Function::Declared { info: method, receiver: Some(receiver.clone()) })));
                }
                if let Some(field) = object.info.delegated_field_of(name) {
                    return Some(Value::Function(Rc::new(Function::Delegated { object: object.clone(), field, name })));
                }
                natives::is_object_method(name)
                    .then(|| Value::Function(Rc::new(Function::NativeMethod { receiver: receiver.clone(), name })))
            }
            Value::Object(object) => {
                if let Some(field) = object.info.delegated_field_of(name) {
                    return Some(Value::Function(Rc::new(Function::Delegated { object: object.clone(), field, name })));
                }
                natives::is_object_method(name)
                    .then(|| Value::Function(Rc::new(Function::NativeMethod { receiver: receiver.clone(), name })))
            }
            _ => {
                let method = self.program.extensions.get(receiver.type_name())?.get(name)?.clone();
                Some(Value::Function(Rc::new(Function::Declared { info: method, receiver: Some(receiver.clone()) })))
            }
        }
    }

    fn force(&mut self, constant: &Rc<Constant>) -> Eval {
        if let Some(value) = constant.value.borrow().clone() {
            return Ok(value);
        }
        if constant.is_evaluating.replace(true) {
            return Err(failure(format!("The constant `{}` depends on itself", constant.name)));
        }
        let environment = Environment::child(&constant.module.top_level());
        if let Some(owner) = &constant.owner {
            environment.declare("Self", Value::Type(owner.clone()), false);
        }
        let value = self.eval(constant.value_expression, &environment);
        constant.is_evaluating.set(false);
        let value = self.expect(value?, constant.annotation, &constant.module, constant.owner.as_ref())?;
        *constant.value.borrow_mut() = Some(value.clone());
        Ok(value)
    }

    fn force_lazy(&mut self, lazy: &crate::value::Lazy) -> Eval {
        if let Some(value) = lazy.value.borrow().clone() {
            return Ok(value);
        }
        let value = self.eval(lazy.expression, &lazy.environment)?;
        *lazy.value.borrow_mut() = Some(value.clone());
        Ok(value)
    }

    /// `target.name` without a call.
    fn member(&mut self, target: &Value, name: &'static str, environment: &Rc<Environment>, span: Span) -> Eval {
        let found = match target {
            Value::Module(module) => match module.scope.borrow().get(name).cloned() {
                Some(Item::Value(value)) => Some(value),
                Some(Item::Constant(constant)) => Some(self.force(&constant)?),
                None => None,
            },
            Value::Type(info) => self.type_member(info, name)?,
            Value::Trait(info) => info
                .statics
                .borrow()
                .get(name)
                .map(|function| Value::Function(Rc::new(Function::Declared { info: function.clone(), receiver: None }))),
            Value::Builtin(owner) => natives::constant(owner, name).or(Some(Value::Function(Rc::new(Function::Native { owner, name })))),
            Value::Tuple(tuple) => {
                let position = name.parse::<usize>().ok().or_else(|| tuple.labels.iter().position(|label| *label == Some(name)));
                position.and_then(|position| tuple.items.get(position).cloned())
            }
            Value::Object(_) => self.implicit_member(target, name),
            _ => self
                .implicit_member(target, name)
                .or_else(|| Some(Value::Function(Rc::new(Function::NativeMethod { receiver: target.clone(), name })))),
        };
        match found {
            Some(value) => Ok(value),
            None => self.fail(environment, span, format!("{} has no member `{name}`", self.describe(target))),
        }
    }

    /// `Point.origin`, `Point.square`, `Shape.Circle`, `Point.area` (the method as a function that takes `self`).
    fn type_member(&mut self, info: &Rc<TypeInfo>, name: &str) -> Eval<Option<Value>> {
        // `from` is generated, everything else the type has is in its filter of names
        if !info.may_have(name) && name != "from" {
            return Ok(None);
        }
        if let Some(case) = info.case_index(name) {
            if info.cases[case].fields.is_empty() {
                return Ok(Some(Value::Object(Rc::new(Object { info: info.clone(), case: Some(case), fields: Vec::new() }))));
            }
            return Ok(Some(Value::Function(Rc::new(Function::Constructor { info: info.clone(), case: Some(case) }))));
        }
        let item = info.statics.borrow().get(name).cloned();
        match item {
            Some(Item::Value(value)) => return Ok(Some(value)),
            Some(Item::Constant(constant)) => return self.force(&constant).map(Some),
            None => {}
        }
        if name == "from" && (!info.conversions.borrow().is_empty() || !info.cases.is_empty()) {
            return Ok(Some(Value::Function(Rc::new(Function::Conversion(info.clone())))));
        }
        let method = info.methods.borrow().get(name).cloned();
        Ok(method.map(|method| Value::Function(Rc::new(Function::Declared { info: method, receiver: None }))))
    }

    fn index(&mut self, target: &Value, index: &Value) -> Eval {
        match (target, index) {
            (Value::List(items), Value::Int(position)) => match usize::try_from(*position).ok().and_then(|position| items.get(position)) {
                Some(item) => Ok(item.clone()),
                None => Err(failure(format!("Index {position} is out of bounds (the length is {})", items.len()))),
            },
            (Value::List(items), Value::Range(range)) => {
                let (from, to) = slice_bounds(*range, items.len())?;
                Ok(Value::list(items[from..to].to_vec()))
            }
            (Value::Text(text), Value::Range(range)) => {
                let (from, to) = slice_bounds(*range, text.as_str().len())?;
                match text.slice(from, to) {
                    Some(slice) => Ok(Value::Text(slice)),
                    None => Err(failure(format!("The offsets {from}..{to} are inside of a character"))),
                }
            }
            (Value::Map(table), key) => match table.get(key) {
                Some(value) => Ok(value.clone()),
                None => Err(failure(format!("There is no entry for {}", self.describe(key)))),
            },
            (Value::Object(object), _) => {
                let method = object.info.methods.borrow().get("at").or(object.info.methods.borrow().get("get")).cloned();
                match method {
                    Some(method) => {
                        let is_get = method.name == "get";
                        let (value, scope) = self.call_declared(&method, Some(target.clone()), vec![Some(index.clone())])?;
                        self.release(scope);
                        match (is_get, value) {
                            (true, Value::Option(Some(value))) => Ok((*value).clone()),
                            (true, _) => Err(failure(format!("There is no entry for {}", self.describe(index)))),
                            (false, value) => Ok(value),
                        }
                    }
                    None => Err(failure(format!("{} cannot be indexed", self.describe(target)))),
                }
            }
            _ => Err(failure(format!("{} cannot be indexed with {}", self.describe(target), self.describe(index)))),
        }
    }

    fn unary(&mut self, operator: UnaryOperator, value: Value) -> Eval {
        match (operator, &value) {
            (UnaryOperator::Negate, Value::Int(number)) => number.checked_neg().map(Value::Int).ok_or_else(|| failure("Integer overflow")),
            (UnaryOperator::Negate, Value::Float(number)) => Ok(Value::Float(-number)),
            (UnaryOperator::Negate, Value::Object(_)) => self.call_method_by_name(&value, "negate", Vec::new()),
            (UnaryOperator::Not, Value::Bool(flag)) => Ok(Value::Bool(!flag)),
            _ => Err(failure(format!("This operator cannot be applied to {}", self.describe(&value)))),
        }
    }

    fn binary(
        &mut self,
        operator: BinaryOperator,
        left: &'static Expression,
        right: &'static Expression,
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        let left_value = self.eval(left, environment)?;
        // The right side of these is only evaluated when needed
        match (operator, &left_value) {
            (BinaryOperator::And, Value::Bool(false)) => return Ok(Value::Bool(false)),
            (BinaryOperator::Or, Value::Bool(true)) => return Ok(Value::Bool(true)),
            (BinaryOperator::And | BinaryOperator::Or, Value::Bool(_)) => {
                return match self.eval(right, environment)? {
                    Value::Bool(value) => Ok(Value::Bool(value)),
                    other => self.fail(environment, right.span, format!("Expected a Bool, this is {}", self.describe(&other))),
                };
            }
            (BinaryOperator::Coalesce, Value::Option(Some(value)) | Value::Result(Ok(value))) => return Ok((**value).clone()),
            (BinaryOperator::Coalesce, Value::Option(None) | Value::Result(Err(_))) => return self.eval(right, environment),
            _ => {}
        }
        let right_value = self.eval(right, environment)?;
        let result = self.operate(operator, left_value, right_value);
        self.located(result, environment, span)
    }

    fn operate(&mut self, operator: BinaryOperator, left: Value, right: Value) -> Eval {
        use BinaryOperator::*;
        // `kind == .Dot`: the other side says which type is meant
        let (left, right) = match (&left, &right) {
            (Value::Object(object), Value::Implicit(implicit)) => (left.clone(), self.resolve_implicit(implicit, &object.info)?),
            (Value::Implicit(implicit), Value::Object(object)) => (self.resolve_implicit(implicit, &object.info)?, right.clone()),
            _ => (left, right),
        };
        // The same one level down: `kinds() == [.Number, .Dot]`. A list, a tuple or `Some(...)` on either side of
        // `==`/`!=` resolves its `.Case`s against the value at the same position on the other side, instead of
        // comparing an unresolved `Implicit` structurally (which is never equal to anything, and so was a silent
        // `false`).
        let (left, right) = match operator {
            Equal | NotEqual if contains_implicit(&left) => (self.resolve_implicits_in(left, &right)?, right),
            Equal | NotEqual if contains_implicit(&right) => (left.clone(), self.resolve_implicits_in(right, &left)?),
            _ => (left, right),
        };
        match operator {
            Equal => return Ok(Value::Bool(left == right)),
            NotEqual => return Ok(Value::Bool(left != right)),
            Less | LessOrEqual | Greater | GreaterOrEqual => {
                let ordering = self.compare(&left, &right)?;
                return Ok(Value::Bool(match operator {
                    Less => ordering.is_lt(),
                    LessOrEqual => ordering.is_le(),
                    Greater => ordering.is_gt(),
                    _ => ordering.is_ge(),
                }));
            }
            _ => {}
        }
        // Literals adapt to the expected type. Without types, that is: an Int next to a Float is a Float.
        let (left, right) = match (&left, &right) {
            (Value::Float(_), Value::Int(_)) => (left, to_float(right)),
            (Value::Int(_), Value::Float(_)) => (to_float(left), right),
            _ => (left, right),
        };
        match (&left, &right) {
            (Value::Int(a), Value::Int(b)) => {
                let result = match operator {
                    Add => a.checked_add(*b),
                    Subtract => a.checked_sub(*b),
                    Multiply => a.checked_mul(*b),
                    Divide if *b == 0 => return Err(failure("Division by zero")),
                    Remainder if *b == 0 => return Err(failure("Division by zero")),
                    Divide => a.checked_div(*b),
                    _ => a.checked_rem(*b),
                };
                result.map(Value::Int).ok_or_else(|| failure("Integer overflow"))
            }
            (Value::Float(a), Value::Float(b)) => Ok(Value::Float(match operator {
                Add => a + b,
                Subtract => a - b,
                Multiply => a * b,
                Divide => a / b,
                _ => a % b,
            })),
            (Value::Text(a), Value::Text(b)) if operator == Add => Ok(Value::text(&format!("{}{}", a.as_str(), b.as_str()))),
            (Value::Object(_), _) => {
                let method = match operator {
                    Add => "add",
                    Subtract => "subtract",
                    Multiply => "multiply",
                    Divide => "divide",
                    _ => "remainder",
                };
                self.call_method_by_name(&left, method, vec![right])
            }
            _ => Err(failure(format!("This operator cannot be applied to {} and {}", self.describe(&left), self.describe(&right)))),
        }
    }

    pub fn compare(&mut self, left: &Value, right: &Value) -> Eval<std::cmp::Ordering> {
        if let Some(ordering) = compare_values(left, right) {
            return Ok(ordering);
        }
        if let Value::Object(object) = left {
            if object.info.methods.borrow().contains_key("compare") || object.info.delegated_field_of("compare").is_some() {
                let result = self.call_method_by_name(left, "compare", vec![right.clone()])?;
                if let Value::Object(ordering) = &result {
                    match ordering.case.map(|case| ordering.info.cases[case].name) {
                        Some("Less") => return Ok(std::cmp::Ordering::Less),
                        Some("Equal") => return Ok(std::cmp::Ordering::Equal),
                        Some("Greater") => return Ok(std::cmp::Ordering::Greater),
                        _ => {}
                    }
                }
            }
        }
        Err(failure(format!("{} and {} cannot be compared", self.describe(left), self.describe(right))))
    }

    /// `?` converts the error to the error type of the function (through `From`, or a case that wraps it).
    fn convert_error(&mut self, error: Value) -> Eval {
        let Some(frame) = self.frames.last() else { return Ok(error) };
        let Some(TypeReference { kind: TypeKind::Named { path, arguments }, .. }) = frame.return_type else { return Ok(error) };
        if path.last().is_none_or(|name| name.text != "Result") || arguments.len() != 2 {
            return Ok(error);
        }
        let TypeKind::Named { path: target, .. } = &arguments[1].kind else { return Ok(error) };
        let Some(target) = target.last() else { return Ok(error) };
        let item = frame.module.scope.borrow().get(target.text.as_str()).cloned();
        let Some(Item::Value(Value::Type(info))) = item else { return Ok(error) };
        Ok(self.convert_into(&info, error.clone())?.unwrap_or(error))
    }

    /// `Target.from(value)`. `None` if there is no conversion.
    pub fn convert_into(&mut self, info: &Rc<TypeInfo>, value: Value) -> Eval<Option<Value>> {
        if matches!(&value, Value::Object(object) if Rc::ptr_eq(&object.info, info)) {
            return Ok(Some(value));
        }
        let source = value.type_name().to_string();
        let accepts = |reference: &TypeReference| match &reference.kind {
            TypeKind::Named { path, .. } => path.last().is_some_and(|name| builtin_type(&name.text).unwrap_or(&name.text) == source),
            _ => false,
        };
        let conversion = info
            .conversions
            .borrow()
            .iter()
            .find(|function| function.parameters().first().and_then(|parameter| parameter.annotation.as_ref()).is_some_and(accepts))
            .cloned();
        if let Some(conversion) = conversion {
            let (result, scope) = self.call_declared(&conversion, None, vec![Some(value)])?;
            self.release(scope);
            return Ok(Some(result));
        }
        // A case that wraps exactly one value of this type generates `From`
        let case = info.cases.iter().position(|case| case.fields.len() == 1 && accepts(case.fields[0].annotation));
        Ok(case.map(|case| Value::Object(Rc::new(Object { info: info.clone(), case: Some(case), fields: vec![value] }))))
    }

    /// A value arrives where a type is written down: `.Case` finds its type, literals adapt.
    fn expect(
        &mut self,
        value: Value,
        annotation: Option<&'static TypeReference>,
        module: &Rc<Module>,
        owner: Option<&Rc<TypeInfo>>,
    ) -> Eval {
        profile::count("expect");
        let Value::Implicit(implicit) = &value else { return Ok(adapt(value, annotation)) };
        let Some(annotation) = annotation else { return Ok(value) };
        match &annotation.kind {
            TypeKind::Optional(_) => match (implicit.name, &implicit.arguments) {
                ("None", None) => return Ok(Value::NONE),
                ("Some", Some((positional, _))) if positional.len() == 1 => {
                    return Ok(Value::some(positional[0].clone()));
                }
                _ => {}
            },
            TypeKind::Named { path, .. } => {
                let name = path.last().map_or("", |name| name.text.as_str());
                let item = module.scope.borrow().get(name).cloned().or_else(|| self.program.prelude.scope.borrow().get(name).cloned());
                let info = match (name, item, owner) {
                    ("Self", _, Some(owner)) => Some(owner.clone()),
                    (_, Some(Item::Value(Value::Type(info))), _) => Some(info),
                    _ => None,
                };
                if let Some(info) = info {
                    return self.resolve_implicit(implicit, &info);
                }
            }
            _ => {}
        }
        Err(failure(format!(
            "The bootstrap interpreter cannot tell which type `.{}` belongs to here. Write the type in front of it",
            implicit.name
        )))
    }

    fn resolve_implicit(&mut self, implicit: &ImplicitCase, info: &Rc<TypeInfo>) -> Eval {
        let Some(case) = info.case_index(implicit.name) else {
            return Err(failure(format!("`{}` has no case `.{}`", info.name, implicit.name)));
        };
        match &implicit.arguments {
            None if info.cases[case].fields.is_empty() => {
                Ok(Value::Object(Rc::new(Object { info: info.clone(), case: Some(case), fields: Vec::new() })))
            }
            None => Err(failure(format!("`.{}` has fields: `.{}(...)`", implicit.name, implicit.name))),
            Some((positional, labeled)) => {
                self.construct(info, Some(case), Arguments { positional: positional.clone(), labeled: labeled.clone() })
            }
        }
    }

    /// `kinds() == [.Number, .Dot]`: `value` is one side of `==`/`!=` and still holds a `.Case` somewhere inside of a
    /// list, a tuple or `Some(...)`; `template` is the other side, at the same position, which is where the type
    /// comes from (`expect` does the same for a single value at an annotation). A `.Case` that has no counterpart to
    /// resolve against - the shapes differ, or the position on `template` is not a value of that case's type - is a
    /// runtime error: silently leaving it unresolved would make the comparison structurally `false` no matter what
    /// the other side is.
    fn resolve_implicits_in(&mut self, value: Value, template: &Value) -> Eval {
        match &value {
            Value::Implicit(implicit) => match template {
                Value::Object(object) => self.resolve_implicit(implicit, &object.info),
                _ => Err(failure(format!(
                    "The bootstrap interpreter cannot tell which type `.{}` belongs to here. Write the type in front of it",
                    implicit.name
                ))),
            },
            Value::List(items) if contains_implicit(&value) => match template {
                Value::List(other) if items.len() == other.len() => {
                    let mut resolved = Vec::with_capacity(items.len());
                    for (item, counterpart) in items.iter().zip(other.iter()) {
                        resolved.push(self.resolve_implicits_in(item.clone(), counterpart)?);
                    }
                    Ok(Value::list(resolved))
                }
                _ => Err(failure("The bootstrap interpreter cannot tell which type a `.Case` in this list belongs to: the other side of `==` is not a list of the same length")),
            },
            Value::Tuple(tuple) if contains_implicit(&value) => match template {
                Value::Tuple(other) if tuple.items.len() == other.items.len() => {
                    let mut resolved = Vec::with_capacity(tuple.items.len());
                    for (item, counterpart) in tuple.items.iter().zip(other.items.iter()) {
                        resolved.push(self.resolve_implicits_in(item.clone(), counterpart)?);
                    }
                    Ok(Value::Tuple(Rc::new(Tuple { labels: tuple.labels.clone(), items: resolved })))
                }
                _ => Err(failure("The bootstrap interpreter cannot tell which type a `.Case` in this tuple belongs to: the other side of `==` is not a matching tuple")),
            },
            Value::Option(Some(inner)) if contains_implicit(&value) => match template {
                Value::Option(Some(other)) => Ok(Value::some(self.resolve_implicits_in((**inner).clone(), other)?)),
                _ => Err(failure("The bootstrap interpreter cannot tell which type a `.Case` in this `Some(...)` belongs to: the other side of `==` is not `Some(...)`")),
            },
            _ => Ok(value),
        }
    }

    // --- Patterns ---------------------------------------------------------------------------------------------------

    fn bind_pattern(&mut self, pattern: &'static Pattern, value: &Value, environment: &Rc<Environment>, is_var: bool) -> Eval<bool> {
        profile::count("pattern.bind");
        match &pattern.kind {
            PatternKind::Wildcard => Ok(true),
            // A bare name always binds. Cases are `.Case` or `Type.Case`
            PatternKind::Name(name) => {
                environment.declare(name, value.clone(), is_var);
                Ok(true)
            }
            PatternKind::ImplicitVariant { name, fields } => {
                // The values stay where they are: copying them out was an allocation per `Some(x)` and per `.Case(x)`
                let payload: &[Value] = match (name.text.as_str(), value) {
                    ("Some", Value::Option(Some(inner))) => std::slice::from_ref(&**inner),
                    ("None", Value::Option(None)) => &[],
                    ("Ok", Value::Result(Ok(inner))) => std::slice::from_ref(&**inner),
                    ("Fail", Value::Result(Err(inner))) => std::slice::from_ref(&**inner),
                    (case, Value::Object(object)) => match object.info.case_index(case) {
                        Some(index) if object.case == Some(index) => &object.fields,
                        Some(_) => return Ok(false),
                        None => return self.fail(environment, name.span, format!("`{}` has no case `.{case}`", object.info.name)),
                    },
                    _ => return Ok(false),
                };
                if fields.len() > payload.len() {
                    return self.fail(
                        environment,
                        pattern.span,
                        format!("`.{}` has {} fields, the pattern has {}", name.text, payload.len(), fields.len()),
                    );
                }
                for (field, item) in fields.iter().zip(payload) {
                    if !self.bind_pattern(&field.pattern, item, environment, is_var)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            PatternKind::Literal(literal) => {
                let expected = self.eval(literal, environment)?;
                Ok(match (&expected, value) {
                    (Value::Int(expected), Value::Float(actual)) => *expected as f64 == *actual,
                    _ => expected == *value,
                })
            }
            PatternKind::Range { start, end, inclusive } => {
                let start = self.eval(start, environment)?;
                let end = self.eval(end, environment)?;
                let (Some(low), Some(high)) = (compare_values(value, &start), compare_values(value, &end)) else { return Ok(false) };
                Ok(low.is_ge() && if *inclusive { high.is_le() } else { high.is_lt() })
            }
            PatternKind::Tuple(patterns) => {
                let Value::Tuple(tuple) = value else { return Ok(false) };
                if tuple.items.len() != patterns.len() {
                    return Ok(false);
                }
                for (pattern, item) in patterns.iter().zip(&tuple.items) {
                    if !self.bind_pattern(pattern, item, environment, is_var)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            PatternKind::List { items: patterns, rest } => {
                let Value::List(items) = value else { return Ok(false) };
                let Some(rest) = rest else {
                    if items.len() != patterns.len() {
                        return Ok(false);
                    }
                    for (pattern, item) in patterns.iter().zip(items.iter()) {
                        if !self.bind_pattern(pattern, item, environment, is_var)? {
                            return Ok(false);
                        }
                    }
                    return Ok(true);
                };
                if items.len() < patterns.len() {
                    return Ok(false);
                }
                let tail_length = patterns.len() - rest.position;
                let tail_start = items.len() - tail_length;
                for (pattern, item) in patterns[..rest.position].iter().zip(items.iter()) {
                    if !self.bind_pattern(pattern, item, environment, is_var)? {
                        return Ok(false);
                    }
                }
                for (pattern, item) in patterns[rest.position..].iter().zip(&items[tail_start..]) {
                    if !self.bind_pattern(pattern, item, environment, is_var)? {
                        return Ok(false);
                    }
                }
                if let Some(name) = &rest.name {
                    environment.declare(&name.text, Value::list(items[rest.position..tail_start].to_vec()), is_var);
                }
                Ok(true)
            }
            PatternKind::Variant { path, fields } => {
                let name = path.last().map_or("", |name| name.text.as_str());
                // The parser made a bare uppercase name a case, by its first letter. Stage 0 has no checker, so a name
                // that is nowhere is reported here instead of quietly matching nothing.
                if path.len() == 1 && fields.is_empty() && !self.knows_case_name(name, environment) {
                    return self.fail(environment, pattern.span, format!("`{name}` is not a case in scope"));
                }
                let payload: &[Value] = match (name, value) {
                    ("Some", Value::Option(Some(inner))) => std::slice::from_ref(&**inner),
                    ("None", Value::Option(None)) => &[],
                    ("Ok", Value::Result(Ok(inner))) => std::slice::from_ref(&**inner),
                    ("Fail", Value::Result(Err(inner))) => std::slice::from_ref(&**inner),
                    (_, Value::Object(object)) => {
                        // `Shape.Circle(r)` is a case, `Point(x, y)` is a type. A case without its type is `.Circle(r)`
                        let is_case = path.len() > 1 && object.case.is_some() && object.info.case_index(name) == object.case;
                        let is_type = path.len() == 1 && object.case.is_none() && object.info.name == name;
                        // ...or `Circle(r)` / `Empty` after `use Shape.Circle, Shape.Empty`
                        let imported = environment.module.scope.borrow().get(name).cloned();
                        let is_imported_case = path.len() == 1 && names_case_of(&imported, object);
                        if !is_case && !is_type && !is_imported_case {
                            return Ok(false);
                        }
                        &object.fields
                    }
                    _ => return Ok(false),
                };
                if fields.len() > payload.len() {
                    return self.fail(
                        environment,
                        pattern.span,
                        format!("`{name}` has {} fields, the pattern has {}", payload.len(), fields.len()),
                    );
                }
                for (field, item) in fields.iter().zip(payload) {
                    if !self.bind_pattern(&field.pattern, item, environment, is_var)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            PatternKind::Or(alternatives) => {
                for alternative in alternatives {
                    let declared = environment.slots.borrow().len();
                    if self.bind_pattern(alternative, value, environment, is_var)? {
                        return Ok(true);
                    }
                    environment.slots.borrow_mut().truncate(declared);
                }
                Ok(false)
            }
            PatternKind::Error => Ok(false),
        }
    }

    // --- Places (`var` paths) ---------------------------------------------------------------------------------------

    /// `None` if the expression is not a path (a temporary).
    fn resolve_place(&mut self, expression: &'static Expression, environment: &Rc<Environment>) -> Eval<Option<Place>> {
        profile::count("place.resolve");
        match &expression.kind {
            ExpressionKind::Name(name) => Ok(self.resolve_name_place(name, environment)),
            ExpressionKind::Member { target, name, optional: false } => {
                let Some(mut place) = self.resolve_place(target, environment)? else { return Ok(None) };
                let position = match self.peek(&place)? {
                    Value::Object(object) => object.info.field_position(object.case, &name.text),
                    Value::Tuple(tuple) => {
                        name.text.parse::<usize>().ok().or_else(|| tuple.labels.iter().position(|label| *label == Some(name.text.as_str())))
                    }
                    _ => None,
                };
                let Some(position) = position else { return Ok(None) };
                profile::count("place.step");
                place.steps.push(Step::Field(position));
                Ok(Some(place))
            }
            ExpressionKind::Index { target, index } => {
                let Some(mut place) = self.resolve_place(target, environment)? else { return Ok(None) };
                let step = match self.eval(index, environment)? {
                    Value::Range(range) => {
                        let length = match self.peek(&place)? {
                            Value::List(items) => items.len(),
                            // A slice of a string is a value, not a path
                            _ => return Ok(None),
                        };
                        let bounds = slice_bounds(range, length);
                        let (from, to) = self.located(bounds, environment, index.span)?;
                        Step::Slice(from, to)
                    }
                    index => Step::Index(index),
                };
                profile::count("place.step");
                place.steps.push(step);
                Ok(Some(place))
            }
            _ => Ok(None),
        }
    }

    fn resolve_name_place(&mut self, name: &str, environment: &Rc<Environment>) -> Option<Place> {
        if let Some((scope, slot)) = environment.find_shared(name) {
            return Some(Place { environment: scope, slot, steps: Vec::new() });
        }
        // A field of the implicit `self`
        let (scope, slot) = environment.find_shared("self")?;
        let position = match &scope.slots.borrow()[slot].value {
            Value::Object(object) => object.info.field_position(object.case, name)?,
            _ => return None,
        };
        Some(Place { environment: scope, slot, steps: vec![Step::Field(position)] })
    }

    fn peek(&mut self, place: &Place) -> Eval {
        profile::count("place.peek");
        let mut current = place.environment.slots.borrow()[place.slot].value.clone();
        for step in &place.steps {
            current = self.peek_step(&current, step)?;
        }
        Ok(current)
    }

    fn peek_step(&mut self, value: &Value, step: &Step) -> Eval {
        match (step, value) {
            (Step::Field(position), Value::Object(object)) => Ok(object.fields[*position].clone()),
            (Step::Field(position), Value::Tuple(tuple)) => Ok(tuple.items[*position].clone()),
            (Step::Index(index), _) => self.index(value, index),
            (Step::Slice(from, to), Value::List(items)) => Ok(Value::list(items[*from..*to].to_vec())),
            _ => Err(failure("This path does not exist anymore")),
        }
    }

    /// Moves the value out of the path (so that its storage is not shared while it is changed). `put` brings it back.
    fn take(&mut self, place: &Place) -> Eval {
        profile::count("place.take");
        let mut slots = place.environment.slots.borrow_mut();
        let slot = &mut slots[place.slot];
        if !slot.is_var {
            return Err(failure(format!("`{}` is a `const`: it cannot be changed", slot.name)));
        }
        if let Some(Step::Slice(from, to)) = place.steps.last() {
            return match navigate(&mut slot.value, &place.steps[..place.steps.len() - 1])? {
                Value::List(items) => Ok(Value::list(items[*from..*to].to_vec())),
                _ => Err(failure("Only lists have slices that can be changed")),
            };
        }
        let target = navigate(&mut slot.value, &place.steps)?;
        Ok(std::mem::replace(target, Value::Void))
    }

    fn put(&mut self, place: &Place, value: Value) -> Eval<()> {
        profile::count("place.put");
        let mut slots = place.environment.slots.borrow_mut();
        let slot = &mut slots[place.slot];
        if !slot.is_var {
            return Err(failure(format!("`{}` is a `const`: it cannot be changed", slot.name)));
        }
        let Some((last, parents)) = place.steps.split_last() else {
            slot.value = value;
            return Ok(());
        };
        match (last, navigate(&mut slot.value, parents)?) {
            (Step::Index(key), Value::Map(table)) => {
                Rc::make_mut(table).insert(key.clone(), value);
            }
            (Step::Slice(from, to), Value::List(items)) => {
                let replacement = natives::items_of(&value)?;
                Rc::make_mut(items).splice(*from..*to, replacement);
            }
            (_, parent) => *navigate(parent, std::slice::from_ref(last))? = value,
        }
        Ok(())
    }

    // --- Calls ------------------------------------------------------------------------------------------------------

    fn eval_call(
        &mut self,
        callee: &'static Expression,
        arguments: &'static [Argument],
        style: CallStyle,
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        match &callee.kind {
            ExpressionKind::Member { target, name, optional } => {
                self.call_method(Some(target), &name.text, *optional, arguments, environment, span)
            }
            ExpressionKind::Generic { target, .. } => self.eval_call(target, arguments, style, environment, span),
            ExpressionKind::Name(name) if name != "self" && environment.find(name).is_none() => {
                let receiver = environment.lookup("self");
                let is_field = matches!(&receiver, Some(Value::Object(object))
                    if object.info.may_have(name) && object.info.field_position(object.case, name).is_some());
                let is_method = match &receiver {
                    Some(Value::Object(object)) => object.info.may_have(name) && object.info.methods.borrow().contains_key(name.as_str()),
                    Some(other) => {
                        self.program.extensions.get(other.type_name()).is_some_and(|methods| methods.contains_key(name.as_str()))
                    }
                    None => false,
                };
                // A second reference to `self` would turn every change inside of the call into a copy
                drop(receiver);
                // `database { ... }` is a command, too: a call without parentheses (it ends where its closure ends)
                let is_bare_closure = matches!(arguments, [argument]
                    if matches!(argument.value.kind, ExpressionKind::Closure(_)) && argument.value.span.end == span.end);
                if is_field && (style == CallStyle::Command || is_bare_closure) {
                    return self.property_command(callee, name, arguments, environment, span);
                }
                if is_method && !is_field {
                    return self.call_method(None, name, false, arguments, environment, span);
                }
                if name == "assert" {
                    return self.assert(arguments, environment, span);
                }
                let function = self.eval(callee, environment)?;
                self.call_with_arguments(function, arguments, environment, span)
            }
            _ => {
                let function = self.eval(callee, environment)?;
                self.call_with_arguments(function, arguments, environment, span)
            }
        }
    }

    /// `assert` takes an `Expression<Bool>`: the message shows the source.
    fn assert(&mut self, arguments: &'static [Argument], environment: &Rc<Environment>, span: Span) -> Eval {
        let Some(condition) = arguments.first() else { return self.fail(environment, span, "`assert` needs a condition") };
        match self.eval(&condition.value, environment)? {
            Value::Bool(true) => Ok(Value::Void),
            Value::Bool(false) => {
                let source = &environment.module.source[condition.value.span.range()];
                let mut message = format!("Assertion failed: {source}");
                if let ExpressionKind::Binary { left, right, .. } = &condition.value.kind {
                    for side in [left, right] {
                        if !matches!(
                            side.kind,
                            ExpressionKind::Integer(_) | ExpressionKind::Text(_) | ExpressionKind::Bool(_) | ExpressionKind::Char(_)
                        ) {
                            let value = self.eval(side, environment)?;
                            message.push_str(&format!("\n  {} = {}", &environment.module.source[side.span.range()], self.describe(&value)));
                        }
                    }
                }
                self.fail(environment, span, message)
            }
            other => self.fail(environment, span, format!("`assert` needs a Bool, this is {}", self.describe(&other))),
        }
    }

    /// A command on a field never calls it, it writes it: `port 8080`, `onStart { ... }`, `database { url "..." }`.
    fn property_command(
        &mut self,
        callee: &'static Expression,
        name: &str,
        arguments: &'static [Argument],
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        let [argument] = arguments else {
            return self.fail(environment, span, format!("`{name}` is a field: a command on it takes exactly one value"));
        };
        let Some(place) = self.resolve_place(callee, environment)? else {
            return self.fail(environment, span, format!("`{name}` cannot be changed here"));
        };
        let value = self.eval(&argument.value, environment)?;
        let current = self.peek(&place)?;
        let configures = matches!(&value, Value::Function(_)) && !matches!(&current, Value::Function(_));
        if !configures {
            let value = if matches!(current, Value::Float(_)) { to_float(value) } else { value };
            self.put(&place, value)?;
            return Ok(Value::Void);
        }
        // The closure is a receiver closure that configures the value of the field in place
        let Value::Function(function) = &value else { unreachable!() };
        let Function::Closure(closure) = &**function else {
            return self.fail(environment, span, format!("`{name}` is configured with a closure"));
        };
        drop(current);
        let receiver = self.take(&place)?;
        let scope = Environment::child(&closure.environment);
        scope.declare("self", receiver, true);
        let result = self.run_closure_body(closure, &scope);
        let receiver = scope.lookup("self").unwrap_or(Value::Void);
        self.put(&place, receiver)?;
        result.map(|_| Value::Void)
    }

    /// `target.name(arguments)`. Without a target, the receiver is the implicit `self`.
    fn call_method(
        &mut self,
        target: Option<&'static Expression>,
        name: &'static str,
        optional: bool,
        arguments: &'static [Argument],
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        let mut place = match target {
            Some(target) => self.resolve_place(target, environment)?,
            None => self.resolve_name_place("self", environment),
        };
        let mut receiver = match (&place, target) {
            (Some(place), _) => self.peek(place)?,
            (None, Some(target)) => self.eval(target, environment)?,
            (None, None) => return self.fail(environment, span, "There is no `self` here"),
        };
        if optional {
            receiver = match receiver {
                Value::Option(None) => return Ok(Value::NONE),
                Value::Option(Some(inner)) => (*inner).clone(),
                other => return self.fail(environment, span, format!("`?.` needs an Option, this is {}", self.describe(&other))),
            };
            place = None;
        }
        let result = self.dispatch_method(receiver, place, name, arguments, environment, span)?;
        Ok(if optional { flatten_option(result) } else { result })
    }

    fn dispatch_method(
        &mut self,
        receiver: Value,
        place: Option<Place>,
        name: &'static str,
        arguments: &'static [Argument],
        environment: &Rc<Environment>,
        span: Span,
    ) -> Eval {
        let method = match &receiver {
            Value::Module(_) | Value::Type(_) | Value::Trait(_) => {
                let function = self.member(&receiver, name, environment, span)?;
                return self.call_with_arguments(function, arguments, environment, span);
            }
            Value::Builtin(owner) => {
                let arguments = self.eval_arguments(arguments, environment)?;
                return profile::timed(name, || natives::call_static(self, owner, name, arguments));
            }
            Value::Object(object) if object.info.may_have(name) => {
                if let Some(position) = object.info.field_position(object.case, name) {
                    let function = object.fields[position].clone();
                    return self.call_with_arguments(function, arguments, environment, span);
                }
                profile::count("methods.get");
                let method = object.info.methods.borrow().get(name).cloned();
                method
            }
            Value::Object(_) => None,
            other => self.program.extensions.get(other.type_name()).and_then(|methods| methods.get(name)).cloned(),
        };

        if let Some(method) = method {
            let bound = self.bind_arguments(method.parameters(), arguments, environment)?;
            let receiver = match (&place, method.is_var_self()) {
                (Some(place), true) => {
                    drop(receiver);
                    self.take(place)?
                }
                (None, true) => {
                    return self.fail(
                        environment,
                        span,
                        format!("`{name}` changes its receiver and needs a `var` path, this is a temporary value"),
                    );
                }
                _ => receiver,
            };
            let (result, scope) = self.call_declared(&method, Some(receiver), bound.values)?;
            if let (Some(place), true) = (&place, method.is_var_self()) {
                self.put(place, scope.lookup("self").unwrap_or(Value::Void))?;
            }
            self.write_back(bound.places, &scope)?;
            self.release(scope);
            return Ok(result);
        }

        // `orElse` and `okOr` take their argument `lazy`
        if matches!(name, "orElse" | "okOr") {
            match (&receiver, name) {
                (Value::Option(Some(value)) | Value::Result(Ok(value)), "orElse") => return Ok((**value).clone()),
                (Value::Option(Some(value)), "okOr") => return Ok(Value::ok((**value).clone())),
                _ => {}
            }
        }
        // `with Add & Subtract by value`: a required member without a body of its own is forwarded to the field
        if let Value::Object(object) = &receiver {
            if let Some(field) = object.info.delegated_field_of(name) {
                let arguments = self.eval_arguments(arguments, environment)?;
                return self.delegated_call(object, field, name, arguments.positional);
            }
        }
        let arguments = self.eval_arguments(arguments, environment)?;
        if !natives::is_mutating(&receiver, name) {
            let mut receiver = receiver;
            return profile::timed(name, || natives::call_method(self, &mut receiver, name, arguments));
        }
        let Some(place) = place else {
            return self.fail(
                environment,
                span,
                format!("`{name}` changes its receiver and needs a `var` path, this is a temporary value"),
            );
        };
        drop(receiver);
        let mut target = self.take(&place)?;
        let result = profile::timed(name, || natives::call_method(self, &mut target, name, arguments));
        self.put(&place, target)?;
        result
    }

    fn write_back(&mut self, places: Vec<(&'static str, Place)>, scope: &Environment) -> Eval<()> {
        for (name, place) in places {
            self.put(&place, scope.lookup(name).unwrap_or(Value::Void))?;
        }
        Ok(())
    }

    fn eval_arguments(&mut self, arguments: &'static [Argument], environment: &Rc<Environment>) -> Eval<Arguments> {
        if profile::is_enabled() && !arguments.is_empty() {
            profile::add("allocate.arguments", arguments.len());
        }
        let mut result = Arguments::default();
        for argument in arguments {
            let value = self.eval(&argument.value, environment)?;
            match &argument.label {
                Some(label) => result.labeled.push((&label.text, value)),
                None if argument.is_spread => {
                    let items = natives::items_of(&value);
                    result.positional.extend(self.located(items, environment, argument.value.span)?);
                }
                None => result.positional.push(value),
            }
        }
        Ok(result)
    }

    /// Matches the arguments of a call to the parameters of a declared function and evaluates them the way the
    /// parameter asks for: a value, a `var` path, or not at all (`lazy`).
    fn bind_arguments(
        &mut self,
        parameters: &'static [ast::Parameter],
        arguments: &'static [Argument],
        environment: &Rc<Environment>,
    ) -> Eval<BoundArguments> {
        let mut bound = BoundArguments { values: self.argument_buffer(parameters.len()), places: Vec::new() };
        let mut variadic = Vec::new();
        let mut next = 0;
        let is_function =
            |parameter: &ast::Parameter| matches!(parameter.annotation, Some(TypeReference { kind: TypeKind::Function { .. }, .. }));
        for (position, argument) in arguments.iter().enumerate() {
            let index = match &argument.label {
                Some(label) => match parameters.iter().position(|parameter| parameter.name.text == label.text) {
                    Some(index) => index,
                    None => return self.fail(environment, label.span, format!("There is no parameter `{}`", label.text)),
                },
                None => {
                    let last = parameters.len().saturating_sub(1);
                    let is_trailing_closure = position + 1 == arguments.len()
                        && matches!(argument.value.kind, ExpressionKind::Closure(_))
                        && next < last
                        && bound.values[last].is_none()
                        && is_function(&parameters[last])
                        && !is_function(&parameters[next]);
                    if is_trailing_closure {
                        last
                    } else if next >= parameters.len() {
                        return self.fail(environment, argument.value.span, "Too many arguments");
                    } else if parameters[next].is_variadic {
                        next
                    } else {
                        next += 1;
                        next - 1
                    }
                }
            };
            let parameter = &parameters[index];
            if parameter.is_variadic {
                let value = self.eval(&argument.value, environment)?;
                if argument.is_spread {
                    let items = natives::items_of(&value);
                    variadic.extend(self.located(items, environment, argument.value.span)?);
                } else {
                    variadic.push(value);
                }
                continue;
            }
            let value = if parameter.is_var {
                let Some(place) = self.resolve_place(&argument.value, environment)? else {
                    return self.fail(
                        environment,
                        argument.value.span,
                        format!("`{}` is a `var` parameter and needs a `var` path", parameter.name.text),
                    );
                };
                let taken = self.take(&place);
                let taken = self.located(taken, environment, argument.value.span)?;
                bound.places.push((&parameter.name.text, place));
                taken
            } else if matches!(parameter.annotation, Some(TypeReference { kind: TypeKind::Lazy(_), .. })) {
                Value::Function(Rc::new(Function::Lazy(crate::value::Lazy {
                    expression: &argument.value,
                    environment: environment.clone(),
                    value: std::cell::RefCell::new(None),
                })))
            } else {
                self.eval(&argument.value, environment)?
            };
            bound.values[index] = Some(adapt(value, parameter.annotation.as_ref()));
        }
        if let Some(index) = parameters.iter().position(|parameter| parameter.is_variadic) {
            bound.values[index] = Some(Value::list(variadic));
        }
        Ok(bound)
    }

    /// Runs a declared function. Returns the scope of the call, too: `var` parameters are read back from it.
    pub fn call_declared(
        &mut self,
        info: &Rc<FunctionInfo>,
        receiver: Option<Value>,
        values: Vec<Option<Value>>,
    ) -> Eval<(Value, Rc<Environment>)> {
        profile::count("call.declared");
        let Some(body) = &info.declaration.body else {
            return Err(failure(format!("`{}` has no body. The bootstrap interpreter does not know this native function", info.name)));
        };
        let parent = info.environment.clone().unwrap_or_else(|| info.module.top_level());
        let scope = self.scope_of(info.module.clone(), parent);
        if let Some(owner) = &info.owner {
            scope.declare("Self", Value::Type(owner.clone()), false);
        }
        if let Some(receiver) = receiver {
            scope.declare("self", receiver, info.is_var_self());
        }
        let mut values = values;
        for (position, parameter) in info.parameters().iter().enumerate() {
            let value = match (values.get_mut(position).and_then(Option::take), &parameter.default) {
                (Some(value), _) => self.expect(value, parameter.annotation.as_ref(), &info.module, info.owner.as_ref())?,
                (None, Some(default)) => adapt(self.eval(default, &scope)?, parameter.annotation.as_ref()),
                (None, None) => return Err(failure(format!("Missing argument `{}` in the call of `{}`", parameter.name.text, info.name))),
            };
            scope.declare(&parameter.name.text, value, parameter.is_var);
        }
        self.release_argument_buffer(values);
        if self.frames.len() > 2_000 {
            return Err(failure("The call stack is too deep (infinite recursion?)"));
        }
        self.frames.push(Frame { return_type: info.declaration.return_type.as_ref(), module: info.module.clone() });
        let result = self.exec_statements(&body.statements, &scope);
        self.frames.pop();
        match result {
            Ok(value) | Err(Flow::Return(value)) => {
                let value = self.expect(value, info.declaration.return_type.as_ref(), &info.module, info.owner.as_ref())?;
                Ok((value, scope))
            }
            Err(Flow::Break | Flow::Continue) => Err(failure("`break` and `continue` only work inside of a loop")),
            Err(Flow::Failure(mut failure)) => {
                if failure.trace.len() < 12 {
                    let owner = info.owner.as_ref().map_or(String::new(), |owner| format!("{}.", owner.name));
                    failure.trace.push(format!("in {owner}{}", info.name));
                }
                Err(Flow::Failure(failure))
            }
        }
    }

    fn run_closure_body(&mut self, closure: &Closure, scope: &Rc<Environment>) -> Eval {
        self.frames.push(Frame { return_type: None, module: scope.module.clone() });
        let result = self.exec_statements(&closure.ast.body.statements, scope);
        self.frames.pop();
        match result {
            Ok(value) | Err(Flow::Return(value)) => Ok(value),
            other => other,
        }
    }

    fn call_closure(&mut self, closure: &Closure, arguments: Vec<Value>) -> Eval<(Value, Rc<Environment>)> {
        profile::count("call.closure");
        let scope = self.scope(&closure.environment);
        let parameters = &closure.ast.parameters;
        if parameters.is_empty() {
            // Implicit parameters: `_`, `_2`, ... and the names the parameter type gives them
            for (position, value) in arguments.into_iter().enumerate() {
                let signature = closure.signature.and_then(|signature| signature.get(position));
                if let Some(name) = signature.and_then(|parameter| parameter.name.as_ref()) {
                    if name.text != "self" && closure.environment.find(&name.text).is_some() {
                        return Err(failure(format!(
                            "The implicit parameter `{}` would shadow a name that is visible here. Name the parameter of the closure",
                            name.text
                        )));
                    }
                    scope.declare(&name.text, value.clone(), signature.is_some_and(|parameter| parameter.is_var));
                }
                let implicit = if position == 0 { "_" } else { crate::program::intern(&format!("_{}", position + 1)) };
                scope.declare(implicit, value, false);
            }
        } else {
            if parameters.len() != arguments.len() {
                return Err(failure(format!("This closure takes {} parameters, it was called with {}", parameters.len(), arguments.len())));
            }
            for (position, (parameter, value)) in parameters.iter().zip(arguments).enumerate() {
                let is_var = closure.signature.and_then(|signature| signature.get(position)).is_some_and(|parameter| parameter.is_var);
                let value = adapt(value, parameter.annotation.as_ref());
                if !self.bind_pattern(&parameter.pattern, &value, &scope, is_var)? {
                    return Err(failure(format!("The parameter pattern does not match {}", self.describe(&value))));
                }
            }
        }
        let result = self.run_closure_body(closure, &scope)?;
        Ok((result, scope))
    }

    /// Calls a function value with evaluated arguments. This is what native functions use for their callbacks.
    pub fn call_function(&mut self, function: &Value, mut arguments: Vec<Value>) -> Eval {
        match function {
            Value::Function(function) => match &**function {
                Function::Declared { info, receiver } => {
                    let receiver = match receiver {
                        Some(receiver) => Some(receiver.clone()),
                        None if info.has_self() && !arguments.is_empty() => Some(arguments.remove(0)),
                        None => None,
                    };
                    let mut values: Vec<Option<Value>> = arguments.into_iter().map(Some).collect();
                    let parameters = info.parameters();
                    if let Some(index) = parameters.iter().position(|parameter| parameter.is_variadic) {
                        let rest = values.split_off(index.min(values.len()));
                        values.push(Some(Value::list(rest.into_iter().flatten().collect())));
                    }
                    let values = values
                        .into_iter()
                        .zip(parameters)
                        .map(|(value, parameter)| value.map(|value| adapt(value, parameter.annotation.as_ref())))
                        .collect();
                    let (result, scope) = self.call_declared(info, receiver, values)?;
                    self.release(scope);
                    Ok(result)
                }
                Function::Closure(closure) => {
                    let (result, scope) = self.call_closure(closure, arguments)?;
                    self.release(scope);
                    Ok(result)
                }
                Function::Native { owner, name } => natives::call_static(self, owner, name, Arguments::positional(arguments)),
                Function::NativeMethod { receiver, name } => {
                    let mut receiver = receiver.clone();
                    natives::call_method(self, &mut receiver, name, Arguments::positional(arguments))
                }
                Function::Delegated { object, field, name } => {
                    let (object, field, name) = (object.clone(), *field, *name);
                    self.delegated_call(&object, field, name, arguments)
                }
                Function::Constructor { info, case } => self.construct(info, *case, Arguments::positional(arguments)),
                Function::Conversion(info) => self.convert(info, Arguments::positional(arguments)),
                Function::Wrap(name) => wrap(name, arguments),
                Function::Lazy(lazy) => {
                    let value = self.force_lazy(lazy)?;
                    self.call_function(&value, arguments)
                }
            },
            Value::Type(info) => self.construct(info, None, Arguments::positional(arguments)),
            Value::Builtin(owner) => natives::call_static(self, owner, "new", Arguments::positional(arguments)),
            other => Err(failure(format!("{} is not a function", self.describe(other)))),
        }
    }

    fn convert(&mut self, info: &Rc<TypeInfo>, arguments: Arguments) -> Eval {
        let value = arguments.required(0, "value")?;
        let description = self.describe(&value);
        self.convert_into(info, value)?.ok_or_else(|| failure(format!("`{}` has no `from` for {description}", info.name)))
    }

    /// Calls a function value with the arguments of a call expression: labels, defaults, `var` paths.
    fn call_with_arguments(&mut self, function: Value, arguments: &'static [Argument], environment: &Rc<Environment>, span: Span) -> Eval {
        if let Value::Function(inner) = &function {
            match &**inner {
                Function::Declared { info, receiver } => {
                    // `Point.area(p)`: the method as a function, `self` is the first argument
                    let (receiver, arguments) = match (receiver, arguments.split_first()) {
                        (None, Some((first, rest))) if info.has_self() => (Some(self.eval(&first.value, environment)?), rest),
                        (receiver, _) => (receiver.clone(), arguments),
                    };
                    let bound = self.bind_arguments(info.parameters(), arguments, environment)?;
                    let (result, scope) = self.call_declared(info, receiver, bound.values)?;
                    self.write_back(bound.places, &scope)?;
                    self.release(scope);
                    return Ok(result);
                }
                Function::Closure(closure)
                    if closure.signature.is_some_and(|signature| signature.iter().any(|parameter| parameter.is_var)) =>
                {
                    let signature = closure.signature.unwrap_or_default();
                    let mut values = Vec::new();
                    let mut places = Vec::new();
                    for (position, argument) in arguments.iter().enumerate() {
                        let Some(parameter) = signature.get(position).filter(|parameter| parameter.is_var) else {
                            values.push(self.eval(&argument.value, environment)?);
                            continue;
                        };
                        let Some(place) = self.resolve_place(&argument.value, environment)? else {
                            return self.fail(environment, argument.value.span, "This is a `var` parameter and needs a `var` path");
                        };
                        values.push(self.take(&place)?);
                        let name = match closure.ast.parameters.get(position).map(|parameter| &parameter.pattern.kind) {
                            Some(PatternKind::Name(name)) => Some(name.as_str()),
                            Some(_) => None,
                            None => parameter.name.as_ref().map(|name| name.text.as_str()),
                        };
                        places.push((name.unwrap_or("_"), place));
                    }
                    let (result, scope) = self.call_closure(closure, values)?;
                    self.write_back(places, &scope)?;
                    self.release(scope);
                    return Ok(result);
                }
                _ => {}
            }
        }
        let arguments = self.eval_arguments(arguments, environment)?;
        match &function {
            Value::Function(inner) => match &**inner {
                Function::Constructor { info, case } => self.construct(info, *case, arguments),
                Function::Native { owner, name } => natives::call_static(self, owner, name, arguments),
                Function::NativeMethod { receiver, name } => {
                    let mut receiver = receiver.clone();
                    natives::call_method(self, &mut receiver, name, arguments)
                }
                _ => self.call_function(&function, arguments.positional),
            },
            Value::Type(info) => self.construct(info, None, arguments),
            Value::Builtin(owner) => natives::call_static(self, owner, "new", arguments),
            Value::Implicit(implicit) if implicit.arguments.is_none() => Ok(Value::Implicit(Rc::new(ImplicitCase {
                name: implicit.name,
                arguments: Some((arguments.positional, arguments.labeled)),
            }))),
            other => self.fail(environment, span, format!("{} is not a function", self.describe(other))),
        }
    }

    pub fn call_method_by_name(&mut self, receiver: &Value, name: &'static str, arguments: Vec<Value>) -> Eval {
        if let Some(function) = self.implicit_member(receiver, name) {
            if matches!(&function, Value::Function(inner) if matches!(&**inner, Function::Declared { .. })) {
                return self.call_function(&function, arguments);
            }
        }
        if let Value::Object(object) = receiver {
            if let Some(field) = object.info.delegated_field_of(name) {
                return self.delegated_call(object, field, name, arguments);
            }
        }
        let mut receiver = receiver.clone();
        natives::call_method(self, &mut receiver, name, Arguments::positional(arguments))
    }

    /// `with Add & Subtract by value`: unwraps a `Self` argument to the field, calls the field's own method, and
    /// wraps a result of the field's type back into `Self` (`Seconds + Seconds` is `Seconds`, not `Int`). The
    /// interpreter has no type checker, so "of the field's type" is decided dynamically, the same way the field
    /// itself was picked by name and not by signature.
    fn delegated_call(&mut self, object: &Rc<Object>, field: &'static str, name: &'static str, arguments: Vec<Value>) -> Eval {
        let Some(position) = object.info.field_position(object.case, field) else {
            return Err(failure(format!("`{}` has no field `{field}` to delegate to", object.info.name)));
        };
        let receiver = object.fields[position].clone();
        let unwrapped: Vec<Value> = arguments
            .into_iter()
            .map(|value| match &value {
                Value::Object(other) if Rc::ptr_eq(&other.info, &object.info) => other.fields[position].clone(),
                _ => value,
            })
            .collect();
        // `add`, `subtract`, ... are never *called* on a native number or a `String` (`operate` is the only path to
        // them, the same way `+` reaches a hand-written `add`), so a delegated arithmetic member goes through it too.
        let result = match (arithmetic_operator_of(name), unwrapped.as_slice()) {
            (Some(operator), [only]) => self.operate(operator, receiver.clone(), only.clone()),
            _ => self.call_method_by_name(&receiver, name, unwrapped),
        }?;
        if result.type_name() == receiver.type_name() {
            let mut fields = object.fields.clone();
            fields[position] = result;
            return Ok(Value::Object(Rc::new(Object { info: object.info.clone(), case: object.case, fields })));
        }
        Ok(result)
    }

    /// The one constructor of a type: fields in declaration order, positional or labeled, defaults for the rest.
    pub fn construct(&mut self, info: &Rc<TypeInfo>, case: Option<usize>, arguments: Arguments) -> Eval {
        profile::count("call.construct");
        if info.is_shared {
            return Err(failure(format!("`{}` is a `shared type`. The bootstrap interpreter does not support identity yet", info.name)));
        }
        let fields = info.fields_of(case);
        if case.is_none() && !info.cases.is_empty() {
            return Err(failure(format!("`{}` is constructed through its cases (`{}.{}`)", info.name, info.name, info.cases[0].name)));
        }
        if arguments.positional.len() > fields.len() {
            return Err(failure(format!("`{}` has {} fields, {} values were passed", info.name, fields.len(), arguments.positional.len())));
        }
        if let Some((label, _)) = arguments.labeled.iter().find(|(label, _)| !fields.iter().any(|field| field.name == *label)) {
            return Err(failure(format!("`{}` has no field `{label}`", info.name)));
        }
        let mut values = Vec::with_capacity(fields.len());
        for (position, field) in fields.iter().enumerate() {
            let value = match (arguments.get(position, field.name), field.default) {
                (Some(value), _) => value,
                (None, Some(default)) => {
                    let scope = Environment::child(&info.module.top_level());
                    scope.declare("Self", Value::Type(info.clone()), false);
                    self.eval(default, &scope)?
                }
                (None, None) => return Err(failure(format!("Missing value for the field `{}` of `{}`", field.name, info.name))),
            };
            values.push(self.expect(value, Some(field.annotation), &info.module, Some(info))?);
        }
        profile::count("allocate.Object");
        Ok(Value::Object(Rc::new(Object { info: info.clone(), case, fields: values })))
    }

    // --- Show -------------------------------------------------------------------------------------------------------

    /// What string interpolation and `print` produce. Nested strings and characters are quoted.
    pub fn show(&mut self, value: &Value, is_top_level: bool) -> Eval<String> {
        profile::count("show");
        Ok(match value {
            Value::Void => "Void".to_string(),
            Value::Bool(value) => value.to_string(),
            Value::Int(value) => value.to_string(),
            Value::Float(value) if value.fract() == 0.0 && value.abs() < 1e16 => format!("{value:.1}"),
            Value::Float(value) => value.to_string(),
            Value::Char(value) if is_top_level => value.to_string(),
            Value::Char(value) => format!("'{value}'"),
            Value::Text(text) if is_top_level => text.as_str().to_string(),
            Value::Text(text) => format!("\"{}\"", text.as_str().replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n")),
            Value::List(items) => format!("[{}]", self.show_all(items.iter())?),
            Value::Set(table) => format!("Set.of({})", self.show_all(table.keys())?),
            Value::Map(table) if table.is_empty() => "[:]".to_string(),
            Value::Map(table) => {
                let mut entries = Vec::new();
                for (key, value) in table.iter() {
                    entries.push(format!("{}: {}", self.show(key, false)?, self.show(value, false)?));
                }
                format!("[{}]", entries.join(", "))
            }
            Value::Tuple(tuple) => {
                let mut items = Vec::new();
                for (label, item) in tuple.labels.iter().zip(&tuple.items) {
                    let shown = self.show(item, false)?;
                    items.push(label.map_or(shown.clone(), |label| format!("{label}: {shown}")));
                }
                format!("({})", items.join(", "))
            }
            Value::Range(Range { start, end: Some(end) }) => format!("{start}..{end}"),
            Value::Range(Range { start, end: None }) => format!("{start}.."),
            Value::Option(None) => "None".to_string(),
            Value::Option(Some(value)) => format!("Some({})", self.show(value, false)?),
            Value::Result(Ok(value)) => format!("Ok({})", self.show(value, false)?),
            Value::Result(Err(value)) => format!("Fail({})", self.show(value, false)?),
            Value::Object(object) => {
                if object.info.methods.borrow().contains_key("show") {
                    return match self.call_method_by_name(value, "show", Vec::new())? {
                        Value::Text(text) => Ok(text.as_str().to_string()),
                        other => Err(failure(format!("`show` must return a String, this is {}", self.describe(&other)))),
                    };
                }
                let fields = object.info.fields_of(object.case);
                let name = object.case.map_or(object.info.name, |case| object.info.cases[case].name);
                if fields.is_empty() && object.case.is_some() {
                    return Ok(name.to_string());
                }
                let mut shown = Vec::new();
                for (field, value) in fields.iter().zip(&object.fields) {
                    shown.push(format!("{}: {}", field.name, self.show(value, false)?));
                }
                format!("{name}({})", shown.join(", "))
            }
            Value::Function(_) => "<function>".to_string(),
            Value::Implicit(implicit) => format!(".{}", implicit.name),
            Value::Type(info) => info.name.to_string(),
            Value::Trait(info) => info.name.to_string(),
            Value::Builtin(name) => name.to_string(),
            Value::Module(module) => format!("<module {}>", module.path.display()),
        })
    }

    fn show_all<'a>(&mut self, values: impl Iterator<Item = &'a Value>) -> Eval<String> {
        let mut shown = Vec::new();
        for value in values {
            shown.push(self.show(value, false)?);
        }
        Ok(shown.join(", "))
    }

    /// For messages of the interpreter: the value with its type, never fails.
    pub fn describe(&self, value: &Value) -> String {
        match value {
            Value::Text(text) => format!("the String \"{}\"", text.as_str()),
            Value::Int(_) | Value::Float(_) | Value::Bool(_) | Value::Char(_) => {
                let mut shown = String::new();
                if let Value::Int(value) = value {
                    shown = value.to_string();
                } else if let Value::Float(value) = value {
                    shown = value.to_string();
                } else if let Value::Bool(value) = value {
                    shown = value.to_string();
                } else if let Value::Char(value) = value {
                    shown = format!("'{value}'");
                }
                format!("the {} {shown}", value.type_name())
            }
            Value::Object(object) => match object.case {
                Some(case) => format!("a `{}.{}`", object.info.name, object.info.cases[case].name),
                None => format!("a `{}`", object.info.name),
            },
            Value::Option(None) => "None".to_string(),
            Value::Void => "Void".to_string(),
            other => format!("a value of type {}", other.type_name()),
        }
    }
}

/// The names the counters of `TORB_PROFILE` use for the nodes of the syntax tree.
fn expression_kind(kind: &ExpressionKind) -> &'static str {
    match kind {
        ExpressionKind::Integer(_) => "eval.Integer",
        ExpressionKind::Float(_) => "eval.Float",
        ExpressionKind::Bool(_) => "eval.Bool",
        ExpressionKind::VoidLiteral => "eval.Void",
        ExpressionKind::Char(_) => "eval.Char",
        ExpressionKind::Text(_) => "eval.Text",
        ExpressionKind::Name(_) => "eval.Name",
        ExpressionKind::ImplicitMember(_) => "eval.ImplicitMember",
        ExpressionKind::Generic { .. } => "eval.Generic",
        ExpressionKind::Tuple(_) => "eval.Tuple",
        ExpressionKind::List(_) => "eval.List",
        ExpressionKind::Map(_) => "eval.Map",
        ExpressionKind::Member { .. } => "eval.Member",
        ExpressionKind::Index { .. } => "eval.Index",
        ExpressionKind::Call { .. } => "eval.Call",
        ExpressionKind::Unary { .. } => "eval.Unary",
        ExpressionKind::Binary { .. } => "eval.Binary",
        ExpressionKind::Range { .. } => "eval.Range",
        ExpressionKind::Try(_) => "eval.Try",
        ExpressionKind::Closure(_) => "eval.Closure",
        ExpressionKind::If { .. } => "eval.If",
        ExpressionKind::Match { .. } => "eval.Match",
        ExpressionKind::Block(_) => "eval.Block",
        ExpressionKind::Error => "eval.Error",
    }
}

fn statement_kind(kind: &StatementKind) -> &'static str {
    match kind {
        StatementKind::Declaration(_) => "exec.Declaration",
        StatementKind::Binding(_) => "exec.Binding",
        StatementKind::Assignment { .. } => "exec.Assignment",
        StatementKind::For { .. } => "exec.For",
        StatementKind::While { .. } => "exec.While",
        StatementKind::Return(_) => "exec.Return",
        StatementKind::Break => "exec.Break",
        StatementKind::Continue => "exec.Continue",
        StatementKind::Expression(_) => "exec.Expression",
    }
}

fn parse_integer(text: &str) -> Option<i64> {
    // `1_000_000` is rare and every other literal would pay for the copy that removes the separators
    if text.contains('_') {
        return parse_integer(&text.replace('_', ""));
    }
    if let Some(digits) = text.strip_prefix("0x") {
        return i64::from_str_radix(digits, 16).ok();
    }
    if let Some(digits) = text.strip_prefix("0b") {
        return i64::from_str_radix(digits, 2).ok();
    }
    text.parse().ok()
}

fn to_float(value: Value) -> Value {
    match value {
        Value::Int(value) => Value::Float(value as f64),
        other => other,
    }
}

/// The reverse of `operate`'s own name-per-operator mapping: what a delegated `Add`, `Subtract`, `Multiply`,
/// `Divide` or `Remainder` calls its required member - a native number or a `String` has no callable method for
/// one, only the operator itself, so `delegated_call` has to reach it the same way `+` does.
fn arithmetic_operator_of(name: &str) -> Option<BinaryOperator> {
    match name {
        "add" => Some(BinaryOperator::Add),
        "subtract" => Some(BinaryOperator::Subtract),
        "multiply" => Some(BinaryOperator::Multiply),
        "divide" => Some(BinaryOperator::Divide),
        "remainder" => Some(BinaryOperator::Remainder),
        _ => None,
    }
}

/// Whether a `.Case` is still waiting for its type somewhere inside of a list, a tuple or `Some(...)`.
fn contains_implicit(value: &Value) -> bool {
    match value {
        Value::Implicit(_) => true,
        Value::List(items) => items.iter().any(contains_implicit),
        Value::Tuple(tuple) => tuple.items.iter().any(contains_implicit),
        Value::Option(Some(inner)) => contains_implicit(inner),
        _ => false,
    }
}

/// The part of "the expected type decides" that can be done without a type checker: integer literals become floats,
/// and closures learn the names of their implicit parameters.
fn adapt(value: Value, annotation: Option<&'static TypeReference>) -> Value {
    let Some(annotation) = annotation else { return value };
    match (&value, &annotation.kind) {
        // Only `Float`, `Float32` and `Float64` can make an Int a Float, and this runs for every argument of every call
        (Value::Int(_), TypeKind::Named { path, .. })
            if path.len() == 1 && path[0].text.starts_with('F') && builtin_type(&path[0].text) == Some("Float") =>
        {
            to_float(value)
        }
        (Value::Function(function), TypeKind::Function { parameters, .. }) => match &**function {
            Function::Closure(closure) if closure.signature.is_none() => Value::Function(Rc::new(Function::Closure(Closure {
                ast: closure.ast,
                environment: closure.environment.clone(),
                signature: Some(parameters),
            }))),
            _ => value,
        },
        _ => value,
    }
}

/// `a?.b` is `map` if `b` is a value and `flatMap` if it is an Option.
fn flatten_option(value: Value) -> Value {
    match value {
        Value::Option(_) => value,
        other => Value::some(other),
    }
}

/// Whether an imported name is the very case this object is: a constructor where the case has fields, the one value
/// of the case where it has none (`use Shape.Circle, Shape.Empty`).
fn names_case_of(imported: &Option<Item>, object: &Rc<crate::value::Object>) -> bool {
    match imported {
        Some(Item::Value(Value::Function(function))) => matches!(&**function, Function::Constructor { info, case }
            if Rc::ptr_eq(info, &object.info) && *case == object.case),
        Some(Item::Value(Value::Object(other))) => Rc::ptr_eq(&other.info, &object.info) && other.case == object.case,
        _ => false,
    }
}

fn wrap(name: &str, mut arguments: Vec<Value>) -> Eval {
    if arguments.len() != 1 {
        return Err(failure(format!("`{name}` takes exactly one value")));
    }
    let value = arguments.remove(0);
    Ok(match name {
        "Some" => Value::some(value),
        "Ok" => Value::ok(value),
        _ => Value::error(value),
    })
}

pub fn slice_bounds(range: Range, length: usize) -> Eval<(usize, usize)> {
    let from = usize::try_from(range.start).ok();
    let to = match range.end {
        Some(end) => usize::try_from(end).ok(),
        None => Some(length),
    };
    match (from, to) {
        (Some(from), Some(to)) if from <= to && to <= length => Ok((from, to)),
        _ => Err(failure(format!(
            "The range {}..{} is out of bounds (the length is {length})",
            range.start,
            range.end.map_or(String::new(), |end| end.to_string())
        ))),
    }
}

/// Walks down a path for writing. Storage that is shared is copied on the way (`Rc::make_mut`).
fn navigate<'value>(mut current: &'value mut Value, steps: &[Step]) -> Eval<&'value mut Value> {
    for step in steps {
        if profile::is_enabled() {
            match current {
                Value::List(items) if Rc::strong_count(items) > 1 => profile::add("copy_on_write.List", items.len()),
                Value::Map(table) if Rc::strong_count(table) > 1 => profile::add("copy_on_write.Map", table.len()),
                Value::Object(object) if Rc::strong_count(object) > 1 => profile::add("copy_on_write.Object", object.fields.len()),
                _ => {}
            }
        }
        current = match (step, current) {
            (Step::Field(position), Value::Object(object)) => {
                let field = &object.info.fields_of(object.case)[*position];
                if !field.is_var {
                    return Err(failure(format!("The field `{}` of `{}` is not a `var`", field.name, object.info.name)));
                }
                &mut Rc::make_mut(object).fields[*position]
            }
            (Step::Field(position), Value::Tuple(tuple)) => &mut Rc::make_mut(tuple).items[*position],
            (Step::Index(Value::Int(index)), Value::List(items)) => {
                let length = items.len();
                match usize::try_from(*index).ok().filter(|position| *position < length) {
                    Some(position) => &mut Rc::make_mut(items)[position],
                    None => return Err(failure(format!("Index {index} is out of bounds (the length is {length})"))),
                }
            }
            (Step::Index(key), Value::Map(table)) => match Rc::make_mut(table).get_mut(key) {
                Some(value) => value,
                None => return Err(failure("There is no entry for this key")),
            },
            _ => return Err(failure("This path cannot be changed")),
        };
    }
    Ok(current)
}
