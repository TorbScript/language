//! Loads an entry file and everything it imports, and registers the declarations: types, traits, functions,
//! constants, `extend` blocks. The syntax trees are leaked on purpose: they live as long as the process, and every
//! part of the interpreter can hold plain `&'static` references into them.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use torb_syntax::ast::{self, DeclarationKind, MemberKind, StatementKind, TypeKind, UseItems, UseSource};
use torb_syntax::{LineIndex, Span};

use crate::value::{Environment, Function, Value};

const PRELUDE: &str = include_str!("prelude.trb");

pub struct Module {
    pub path: PathBuf,
    pub source: &'static str,
    pub file: &'static ast::File,
    pub scope: RefCell<HashMap<&'static str, Item>>,
    /// Top-level code of an entry file runs here, and the functions of the file see it.
    pub top_level: RefCell<Option<Rc<Environment>>>,
}

impl Module {
    pub fn top_level(&self) -> Rc<Environment> {
        self.top_level.borrow().clone().expect("the top-level scope is created with the module")
    }

    pub fn location(&self, span: Span) -> String {
        let (line, column) = LineIndex::new(self.source).line_and_column(span.start);
        format!("{}:{line}:{column}", display_path(&self.path))
    }
}

/// Canonical paths on Windows start with a verbatim prefix that nobody wants to read.
pub fn display_path(path: &Path) -> String {
    let text = path.display().to_string();
    match text.strip_prefix(r"\\?\") {
        Some(stripped) => stripped.to_string(),
        None => text,
    }
}

#[derive(Clone)]
pub enum Item {
    Value(Value),
    /// `const` of a module or of a type: evaluated on first use, so there is no initialization order
    Constant(Rc<Constant>),
}

pub struct Constant {
    pub name: &'static str,
    pub value_expression: &'static ast::Expression,
    pub annotation: Option<&'static ast::TypeReference>,
    pub module: Rc<Module>,
    pub owner: Option<Rc<TypeInfo>>,
    pub value: RefCell<Option<Value>>,
    pub is_evaluating: Cell<bool>,
}

pub struct FieldInfo {
    pub name: &'static str,
    pub is_var: bool,
    pub annotation: &'static ast::TypeReference,
    pub default: Option<&'static ast::Expression>,
}

pub struct CaseInfo {
    pub name: &'static str,
    pub fields: Vec<FieldInfo>,
}

pub struct TypeInfo {
    pub name: &'static str,
    pub module: Rc<Module>,
    pub is_shared: bool,
    pub fields: Vec<FieldInfo>,
    pub cases: Vec<CaseInfo>,
    pub methods: RefCell<HashMap<&'static str, Rc<FunctionInfo>>>,
    pub statics: RefCell<HashMap<&'static str, Item>>,
    /// `from` exists once per source type, so it is kept apart and chosen by the type of the argument
    pub conversions: RefCell<Vec<Rc<FunctionInfo>>>,
    pub traits: RefCell<Vec<&'static ast::TypeReference>>,
}

impl TypeInfo {
    pub fn fields_of(&self, case: Option<usize>) -> &[FieldInfo] {
        match case {
            Some(case) => &self.cases[case].fields,
            None => &self.fields,
        }
    }

    pub fn case_index(&self, name: &str) -> Option<usize> {
        self.cases.iter().position(|case| case.name == name)
    }
}

pub struct TraitInfo {
    pub name: &'static str,
    pub module: Rc<Module>,
    pub supertraits: &'static [ast::TypeReference],
    /// Default methods. They are copied into every type that implements the trait.
    pub methods: RefCell<Vec<&'static ast::FunctionDeclaration>>,
    pub statics: RefCell<HashMap<&'static str, Rc<FunctionInfo>>>,
}

pub struct FunctionInfo {
    pub name: &'static str,
    pub declaration: &'static ast::FunctionDeclaration,
    pub module: Rc<Module>,
    pub owner: Option<Rc<TypeInfo>>,
    /// The scope a local `fn` was declared in. Functions of a module see the top-level scope of their file.
    pub environment: Option<Rc<Environment>>,
}

impl FunctionInfo {
    pub fn has_self(&self) -> bool {
        has_self(self.declaration)
    }

    pub fn is_var_self(&self) -> bool {
        self.has_self() && self.declaration.parameters[0].is_var
    }

    /// The parameters without `self`.
    pub fn parameters(&self) -> &'static [ast::Parameter] {
        &self.declaration.parameters[usize::from(self.has_self())..]
    }
}

fn has_self(declaration: &ast::FunctionDeclaration) -> bool {
    declaration.parameters.first().is_some_and(|parameter| parameter.name.text == "self")
}

pub struct Program {
    pub entry: Rc<Module>,
    pub prelude: Rc<Module>,
    /// Methods that `extend` adds to built-in types, by the name of the type
    pub extensions: HashMap<&'static str, HashMap<&'static str, Rc<FunctionInfo>>>,
}

/// The canonical name of a built-in type. All integer types are one type for the bootstrap interpreter, and the
/// implementations of a collection trait are the trait.
pub fn builtin_type(name: &str) -> Option<&'static str> {
    Some(match name {
        "Int" | "Int8" | "Int16" | "Int32" | "Int64" | "UInt" | "UInt8" | "UInt16" | "UInt32" | "UInt64" => "Int",
        "Float" | "Float32" | "Float64" => "Float",
        "Bool" => "Bool",
        "Char" => "Char",
        "String" => "String",
        "List" | "ArrayList" | "TrieList" => "List",
        "Map" | "HashMap" | "TrieMap" => "Map",
        "Set" | "HashSet" | "TrieSet" => "Set",
        "Option" => "Option",
        "Result" => "Result",
        "Range" => "Range",
        "File" => "File",
        "Process" => "Process",
        "Environment" => "Environment",
        _ => return None,
    })
}

struct Loader {
    modules: HashMap<PathBuf, Rc<Module>>,
    order: Vec<Rc<Module>>,
    problems: Vec<String>,
}

pub fn load(entry: &Path) -> Result<Program, Vec<String>> {
    let mut loader = Loader { modules: HashMap::new(), order: Vec::new(), problems: Vec::new() };
    let prelude = loader.add_module(PathBuf::from("<prelude>"), PRELUDE.to_string());
    let entry = match loader.load_file(entry) {
        Some(entry) => entry,
        None => return Err(loader.problems),
    };
    if !loader.problems.is_empty() {
        return Err(loader.problems);
    }

    for module in &loader.order {
        declare(module);
    }
    loader.resolve_imports();
    loader.resolve_aliases();
    let mut extensions = HashMap::new();
    for module in &loader.order {
        loader.problems.extend(apply_extends(module, &prelude, &mut extensions));
    }
    for module in &loader.order {
        loader.problems.extend(apply_traits(module, &prelude));
    }
    if !loader.problems.is_empty() {
        return Err(loader.problems);
    }
    Ok(Program { entry, prelude, extensions })
}

impl Loader {
    fn load_file(&mut self, path: &Path) -> Option<Rc<Module>> {
        let path = match path.canonicalize() {
            Ok(path) => path,
            Err(error) => {
                self.problems.push(format!("{}: {error}", path.display()));
                return None;
            }
        };
        if let Some(module) = self.modules.get(&path) {
            return Some(module.clone());
        }
        let source = match std::fs::read_to_string(&path) {
            Ok(source) => source,
            Err(error) => {
                self.problems.push(format!("{}: {error}", path.display()));
                return None;
            }
        };
        let module = self.add_module(path.clone(), source);
        for statement in &module.file.statements {
            let StatementKind::Declaration(ast::Declaration { kind: DeclarationKind::Use(usage), .. }) = &statement.kind else {
                continue;
            };
            let UseSource::Module(path) = &usage.source else { continue };
            if let Some(target) = import_path(&module.path, path) {
                if self.load_file(&target).is_none() {
                    self.problems.push(format!("{}: cannot import \"{path}\"", module.location(statement.span)));
                }
            }
        }
        Some(module)
    }

    fn add_module(&mut self, path: PathBuf, source: String) -> Rc<Module> {
        let source: &'static str = Box::leak(source.into_boxed_str());
        let parsed = torb_syntax::parse(source);
        let lines = LineIndex::new(source);
        for diagnostic in &parsed.diagnostics {
            let (line, column) = lines.line_and_column(diagnostic.span.start);
            self.problems.push(format!("{}:{line}:{column}: {}", display_path(&path), diagnostic.message));
        }
        let file: &'static ast::File = Box::leak(Box::new(parsed.file));
        let module =
            Rc::new(Module { path: path.clone(), source, file, scope: RefCell::new(HashMap::new()), top_level: RefCell::new(None) });
        *module.top_level.borrow_mut() = Some(Environment::root(module.clone()));
        self.modules.insert(path, module.clone());
        self.order.push(module.clone());
        module
    }

    /// Copies imported items into the scope of the importing module. Re-exports form chains, so this runs until
    /// nothing changes anymore.
    fn resolve_imports(&mut self) {
        let mut unresolved = Vec::new();
        loop {
            let mut changed = false;
            unresolved.clear();
            for module in &self.order {
                for statement in &module.file.statements {
                    let StatementKind::Declaration(ast::Declaration { kind: DeclarationKind::Use(usage), .. }) = &statement.kind else {
                        continue;
                    };
                    let path = match &usage.source {
                        UseSource::Module(path) => path,
                        UseSource::Type(path) => {
                            changed |= import_cases(module, path, &usage.items, &mut unresolved);
                            continue;
                        }
                    };
                    // Everything from "std/..." is built in
                    let Some(target) = import_path(&module.path, path) else { continue };
                    let Some(target) = target.canonicalize().ok().and_then(|path| self.modules.get(&path)) else { continue };
                    match &usage.items {
                        UseItems::All { alias } => {
                            let name: &'static str = &alias.text;
                            changed |= module.scope.borrow_mut().insert(name, Item::Value(Value::Module(target.clone()))).is_none();
                        }
                        // The extensions of every loaded module apply everywhere: the interpreter does not check visibility
                        UseItems::OnlyExtensions => {}
                        UseItems::Names(names) => {
                            for name in names {
                                let text: &'static str = &name.text;
                                if module.scope.borrow().contains_key(text) {
                                    continue;
                                }
                                let item = target.scope.borrow().get(text).cloned();
                                match item {
                                    Some(item) => {
                                        module.scope.borrow_mut().insert(text, item);
                                        changed = true;
                                    }
                                    None => {
                                        unresolved.push(format!("{}: \"{path}\" does not declare `{text}`", module.location(name.span)))
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if !changed {
                break;
            }
        }
        self.problems.append(&mut unresolved);
    }

    /// `type EntityId = Int`: the alias refers to whatever the target refers to. Aliases of function and tuple types
    /// have nothing a program could refer to at runtime.
    fn resolve_aliases(&mut self) {
        for module in &self.order {
            for statement in &module.file.statements {
                let StatementKind::Declaration(ast::Declaration { kind: DeclarationKind::Alias(alias), .. }) = &statement.kind else {
                    continue;
                };
                let TypeKind::Named { path, .. } = &alias.target.kind else { continue };
                let target: &str = &path[0].text;
                let value = match module.scope.borrow().get(target) {
                    Some(item) => Some(item.clone()),
                    None => builtin_type(target).map(|name| Item::Value(Value::Builtin(name))),
                };
                if let Some(value) = value {
                    module.scope.borrow_mut().insert(&alias.name.text, value);
                }
            }
        }
    }
}

/// `use Circle, Empty from Shape`: the cases of a type as names of the module. Returns whether something was added.
fn import_cases(module: &Rc<Module>, path: &'static [ast::Name], items: &'static UseItems, unresolved: &mut Vec<String>) -> bool {
    let UseItems::Names(names) = items else { return false };
    let owner = module.scope.borrow().get(path[0].text.as_str()).cloned();
    // The cases of `Option` and `Result` are built in
    let Some(Item::Value(Value::Type(info))) = owner else { return false };
    let mut changed = false;
    for name in names {
        let text: &'static str = &name.text;
        if module.scope.borrow().contains_key(text) {
            continue;
        }
        let Some(case) = info.case_index(text) else {
            unresolved.push(format!("{}: `{}` has no case `{text}`", module.location(name.span), info.name));
            continue;
        };
        let value = if info.cases[case].fields.is_empty() {
            Value::Object(Rc::new(crate::value::Object { info: info.clone(), case: Some(case), fields: Vec::new() }))
        } else {
            Value::Function(Rc::new(Function::Constructor { info: info.clone(), case: Some(case) }))
        };
        module.scope.borrow_mut().insert(text, Item::Value(value));
        changed = true;
    }
    changed
}

/// `None` for package imports ("std/fs"): the bootstrap interpreter has everything it knows built in.
fn import_path(importer: &Path, path: &str) -> Option<PathBuf> {
    if !path.starts_with("./") && !path.starts_with("../") {
        return None;
    }
    let directory = importer.parent().unwrap_or(Path::new("."));
    Some(directory.join(format!("{path}.trb")))
}

fn declare(module: &Rc<Module>) {
    for statement in &module.file.statements {
        match &statement.kind {
            StatementKind::Declaration(declaration) => declare_one(module, declaration),
            StatementKind::Binding(binding) => {
                // Top-level constants are visible to the functions of the file, whenever they are called
                if let (ast::PatternKind::Name(name), false) = (&binding.pattern.kind, binding.is_var) {
                    let constant = Constant {
                        name,
                        value_expression: &binding.value,
                        annotation: binding.annotation.as_ref(),
                        module: module.clone(),
                        owner: None,
                        value: RefCell::new(None),
                        is_evaluating: Cell::new(false),
                    };
                    module.scope.borrow_mut().insert(name, Item::Constant(Rc::new(constant)));
                }
            }
            _ => {}
        }
    }
}

fn declare_one(module: &Rc<Module>, declaration: &'static ast::Declaration) {
    let mut scope = module.scope.borrow_mut();
    match &declaration.kind {
        DeclarationKind::Function(function) => {
            let info = Rc::new(FunctionInfo {
                name: &function.name.text,
                declaration: function,
                module: module.clone(),
                owner: None,
                environment: None,
            });
            scope.insert(&function.name.text, Item::Value(Value::Function(Rc::new(Function::Declared { info, receiver: None }))));
        }
        DeclarationKind::Type(declaration_of_type) => {
            let info = declare_type(module, declaration_of_type, declaration.modifiers.shared);
            scope.insert(&declaration_of_type.name.text, Item::Value(Value::Type(info)));
        }
        DeclarationKind::Trait(declaration_of_trait) => {
            let info = Rc::new(TraitInfo {
                name: &declaration_of_trait.name.text,
                module: module.clone(),
                supertraits: &declaration_of_trait.supertraits,
                methods: RefCell::new(Vec::new()),
                statics: RefCell::new(HashMap::new()),
            });
            for member in &declaration_of_trait.members {
                let MemberKind::Function(function) = &member.kind else { continue };
                if function.body.is_none() {
                    continue;
                }
                if has_self(function) {
                    info.methods.borrow_mut().push(function);
                } else {
                    let function = Rc::new(FunctionInfo {
                        name: &function.name.text,
                        declaration: function,
                        module: module.clone(),
                        owner: None,
                        environment: None,
                    });
                    info.statics.borrow_mut().insert(function.name, function);
                }
            }
            scope.insert(&declaration_of_trait.name.text, Item::Value(Value::Trait(info)));
        }
        // `public var` is an error the checker reports; the bootstrap interpreter does not check visibility, and a
        // top-level `var` exports nothing, like the plain `StatementKind::Binding` above.
        DeclarationKind::Constant(binding) => {
            if let (ast::PatternKind::Name(name), false) = (&binding.pattern.kind, binding.is_var) {
                let constant = Constant {
                    name,
                    value_expression: &binding.value,
                    annotation: binding.annotation.as_ref(),
                    module: module.clone(),
                    owner: None,
                    value: RefCell::new(None),
                    is_evaluating: Cell::new(false),
                };
                scope.insert(name, Item::Constant(Rc::new(constant)));
            }
        }
        DeclarationKind::Use(_) | DeclarationKind::Alias(_) | DeclarationKind::Extend(_) | DeclarationKind::Foreign(_) => {}
    }
}

/// `private(var)` is a `var` field: who may write it is a question for the type checker.
fn field_info(field: &'static ast::Field, is_private_var: bool) -> FieldInfo {
    FieldInfo {
        name: &field.name.text,
        is_var: field.is_var || is_private_var,
        annotation: &field.annotation,
        default: field.default.as_ref(),
    }
}

fn declare_type(module: &Rc<Module>, declaration: &'static ast::TypeDeclaration, is_shared: bool) -> Rc<TypeInfo> {
    let mut fields = Vec::new();
    let mut cases = Vec::new();
    for member in &declaration.members {
        match &member.kind {
            MemberKind::Field(field) => fields.push(field_info(field, member.modifiers.visibility == ast::Visibility::PrivateVar)),
            MemberKind::Case(case) => {
                cases.push(CaseInfo { name: &case.name.text, fields: case.fields.iter().map(|field| field_info(field, false)).collect() })
            }
            MemberKind::Constant(_) | MemberKind::Function(_) => {}
        }
    }
    let info = Rc::new(TypeInfo {
        name: &declaration.name.text,
        module: module.clone(),
        is_shared,
        fields,
        cases,
        methods: RefCell::new(HashMap::new()),
        statics: RefCell::new(HashMap::new()),
        conversions: RefCell::new(Vec::new()),
        traits: RefCell::new(declaration.traits.iter().collect()),
    });
    add_members(&info, module, &declaration.members);
    info
}

/// Functions and constants of a `type` or `extend` body.
fn add_members(info: &Rc<TypeInfo>, module: &Rc<Module>, members: &'static [ast::Member]) {
    for member in members {
        match &member.kind {
            MemberKind::Function(function) => {
                let name: &'static str = &function.name.text;
                let function = Rc::new(FunctionInfo {
                    name,
                    declaration: function,
                    module: module.clone(),
                    owner: Some(info.clone()),
                    environment: None,
                });
                if function.has_self() {
                    info.methods.borrow_mut().insert(name, function);
                } else if name == "from" {
                    info.conversions.borrow_mut().push(function);
                } else {
                    info.statics
                        .borrow_mut()
                        .insert(name, Item::Value(Value::Function(Rc::new(Function::Declared { info: function, receiver: None }))));
                }
            }
            MemberKind::Constant(binding) => {
                if let ast::PatternKind::Name(name) = &binding.pattern.kind {
                    let constant = Constant {
                        name,
                        value_expression: &binding.value,
                        annotation: binding.annotation.as_ref(),
                        module: module.clone(),
                        owner: Some(info.clone()),
                        value: RefCell::new(None),
                        is_evaluating: Cell::new(false),
                    };
                    info.statics.borrow_mut().insert(name, Item::Constant(Rc::new(constant)));
                }
            }
            MemberKind::Field(_) | MemberKind::Case(_) => {}
        }
    }
}

fn lookup(module: &Module, prelude: &Module, name: &str) -> Option<Item> {
    let own = module.scope.borrow().get(name).cloned();
    own.or_else(|| prelude.scope.borrow().get(name).cloned())
}

fn named(reference: &'static ast::TypeReference) -> Option<&'static str> {
    match &reference.kind {
        TypeKind::Named { path, .. } => path.last().map(|name| name.text.as_str()),
        _ => None,
    }
}

/// A `&'static str` for a name that was built at runtime.
pub fn intern(name: &str) -> &'static str {
    thread_local! {
        static NAMES: RefCell<HashMap<String, &'static str>> = RefCell::new(HashMap::new());
    }
    NAMES.with(|names| {
        let mut names = names.borrow_mut();
        if let Some(existing) = names.get(name) {
            return *existing;
        }
        let leaked: &'static str = Box::leak(name.to_string().into_boxed_str());
        names.insert(name.to_string(), leaked);
        leaked
    })
}

type Extensions = HashMap<&'static str, HashMap<&'static str, Rc<FunctionInfo>>>;

fn apply_extends(module: &Rc<Module>, prelude: &Rc<Module>, extensions: &mut Extensions) -> Vec<String> {
    let mut problems = Vec::new();
    for statement in &module.file.statements {
        let StatementKind::Declaration(ast::Declaration { kind: DeclarationKind::Extend(extend), .. }) = &statement.kind else {
            continue;
        };
        let Some(target) = named(&extend.target) else {
            problems.push(format!("{}: the bootstrap interpreter can only extend named types", module.location(extend.target.span)));
            continue;
        };
        match lookup(module, prelude, target) {
            Some(Item::Value(Value::Type(info))) => {
                add_members(&info, module, &extend.members);
                info.traits.borrow_mut().extend(extend.traits.iter());
            }
            Some(Item::Value(Value::Trait(info))) => {
                for member in &extend.members {
                    if let MemberKind::Function(function) = &member.kind {
                        if has_self(function) && function.body.is_some() {
                            info.methods.borrow_mut().push(function);
                        }
                    }
                }
            }
            other => {
                let builtin = match other {
                    Some(Item::Value(Value::Builtin(name))) => Some(name),
                    _ => builtin_type(target),
                };
                let Some(builtin) = builtin else {
                    problems.push(format!("{}: cannot extend `{target}`: it is not a type", module.location(extend.target.span)));
                    continue;
                };
                for member in &extend.members {
                    let MemberKind::Function(function) = &member.kind else { continue };
                    if has_self(function) && function.body.is_some() {
                        let name: &'static str = &function.name.text;
                        let function =
                            Rc::new(FunctionInfo { name, declaration: function, module: module.clone(), owner: None, environment: None });
                        extensions.entry(builtin).or_default().insert(name, function);
                    }
                }
            }
        }
    }
    problems
}

/// Copies the default methods of the traits (and of their supertraits) into the types that implement them.
fn apply_traits(module: &Rc<Module>, prelude: &Rc<Module>) -> Vec<String> {
    let mut problems = Vec::new();
    let types: Vec<Rc<TypeInfo>> = module
        .scope
        .borrow()
        .values()
        .filter_map(|item| match item {
            Item::Value(Value::Type(info)) if Rc::ptr_eq(&info.module, module) => Some(info.clone()),
            _ => None,
        })
        .collect();
    for info in types {
        let mut pending: Vec<(&'static ast::TypeReference, Rc<Module>)> =
            info.traits.borrow().iter().map(|reference| (*reference, info.module.clone())).collect();
        let mut seen: Vec<&'static str> = Vec::new();
        while let Some((reference, context)) = pending.pop() {
            let Some(name) = named(reference) else { continue };
            if seen.contains(&name) {
                continue;
            }
            seen.push(name);
            // Traits of the standard library that the interpreter implements itself (`Show`, `Equals`, `Hash`, ...)
            let Some(Item::Value(Value::Trait(implemented))) = lookup(&context, prelude, name) else { continue };
            for method in implemented.methods.borrow().iter() {
                let method_name: &'static str = &method.name.text;
                if info.methods.borrow().contains_key(method_name) {
                    continue;
                }
                if info.fields.iter().any(|field| field.name == method_name) {
                    problems.push(format!(
                        "{}: `{}` has a field `{method_name}`, which is also a method of `{name}`",
                        info.module.location(reference.span),
                        info.name
                    ));
                }
                let function = FunctionInfo {
                    name: method_name,
                    declaration: method,
                    module: implemented.module.clone(),
                    owner: Some(info.clone()),
                    environment: None,
                };
                info.methods.borrow_mut().insert(method_name, Rc::new(function));
            }
            pending.extend(implemented.supertraits.iter().map(|supertrait| (supertrait, implemented.module.clone())));
        }
    }
    problems
}
