//! Loads an entry file and everything it imports, and registers the declarations: types, traits, functions,
//! constants, `extend` blocks. The syntax trees are leaked on purpose: they live as long as the process, and every
//! part of the interpreter can hold plain `&'static` references into them.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hasher};
use std::path::{Path, PathBuf};
use std::rc::Rc;

use torb_syntax::ast::{self, DeclarationKind, MemberKind, StatementKind, TypeKind, UseItems, UseSource};
use torb_syntax::{LineIndex, Span};

use crate::value::{mark, Environment, Function, Value};

const PRELUDE: &str = include_str!("prelude.trb");

/// FNV-1a over the name. Everything the interpreter looks up while it runs is keyed by a short name, and the hasher
/// of the standard library is built to resist collisions an attacker picks, which costs several times as much.
pub struct NameHasher(u64);

impl Default for NameHasher {
    fn default() -> NameHasher {
        NameHasher(0xcbf2_9ce4_8422_2325)
    }
}

impl Hasher for NameHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        let mut hash = self.0;
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x100_0000_01b3);
        }
        self.0 = hash;
    }
}

/// A table keyed by a name of the program.
pub type Names<Value> = HashMap<&'static str, Value, BuildHasherDefault<NameHasher>>;

pub struct Module {
    pub path: PathBuf,
    /// The path a location of this module is written as: `torbscript/compiler/src/ir/print.trb`. See `stable_path`.
    pub stable: String,
    pub source: &'static str,
    pub file: &'static ast::File,
    pub scope: RefCell<Names<Item>>,
    /// Top-level code of an entry file runs here, and the functions of the file see it.
    pub top_level: RefCell<Option<Rc<Environment>>>,
}

impl Module {
    pub fn top_level(&self) -> Rc<Environment> {
        self.top_level.borrow().clone().expect("the top-level scope is created with the module")
    }

    /// Where something happened while the program ran, as a panic and a failure of the interpreter write it. The path
    /// is the stable one, so that a panic of stage 0 is the panic of a compiled program to the byte.
    pub fn location(&self, span: Span) -> String {
        let (line, column) = LineIndex::new(self.source).line_and_column(span.start);
        format!("{}:{line}:{column}", self.stable)
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

/// The name of a package, as its `project.trb` writes it, and the directory that file stands in.
struct PackageOf {
    name: String,
    directory: PathBuf,
}

/// The name in `name "torbscript/compiler"`, from the first line of a `project.trb` that carries one.
///
/// A `project.trb` is read statically and not evaluated (`docs/BACKEND.md`), so a line is all there is to find. Stage 0
/// loads no workspace at all, and this is the one thing about a project it has to know.
fn package_name_in(source: &str) -> Option<String> {
    for line in source.lines() {
        let Some(rest) = line.trim_start().strip_prefix("name") else { continue };
        let Some(rest) = rest.strip_prefix([' ', '\t']) else { continue };
        let Some(quoted) = rest.trim_start().strip_prefix('"') else { continue };
        if let Some((name, _)) = quoted.split_once('"') {
            return Some(name.to_string());
        }
    }
    None
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
    /// `mark(name)`: every member access looks for a name among the fields of a type
    mark: u32,
    pub is_var: bool,
    pub annotation: &'static ast::TypeReference,
    pub default: Option<&'static ast::Expression>,
}

pub struct CaseInfo {
    pub name: &'static str,
    mark: u32,
    pub fields: Vec<FieldInfo>,
}

pub struct TypeInfo {
    pub name: &'static str,
    pub module: Rc<Module>,
    pub is_shared: bool,
    pub fields: Vec<FieldInfo>,
    pub cases: Vec<CaseInfo>,
    pub methods: RefCell<Names<Rc<FunctionInfo>>>,
    pub statics: RefCell<Names<Item>>,
    /// `from` exists once per source type, so it is kept apart and chosen by the type of the argument
    pub conversions: RefCell<Vec<Rc<FunctionInfo>>>,
    pub traits: RefCell<Vec<&'static ast::TypeReference>>,
    /// `with Add & Subtract by value`: one entry per element of the `with` list that has a `by`, capability and
    /// field name, before the capability is expanded into the trait names `delegates` dispatches by.
    delegate_clauses: Vec<(&'static ast::TypeReference, &'static str)>,
    /// Every required member `delegate_clauses` forwards, flattened to `(member name, field name)` by
    /// `apply_delegations`: the interpreter has no type checker, so a delegated call is found by name, the same way
    /// `+` finds `add`.
    delegates: RefCell<Vec<(&'static str, &'static str)>>,
    /// One bit per name the type has, from any of its fields, cases, methods and statics. Most questions about a name
    /// are "is this a member of the receiver, or of `Self`?", and for a top-level function the answer is no - this
    /// answers that without looking at the fields or the methods at all.
    names: Cell<u64>,
}

impl TypeInfo {
    /// Whether the type can have a member with this name. `false` is certain, `true` has to be checked.
    pub fn may_have(&self, name: &str) -> bool {
        self.names.get() & crate::value::bit_of(mark(name)) != 0
    }

    /// Records that the type has a member with this name.
    fn remember(&self, name: &str) {
        self.names.set(self.names.get() | crate::value::bit_of(mark(name)));
    }

    pub fn fields_of(&self, case: Option<usize>) -> &[FieldInfo] {
        match case {
            Some(case) => &self.cases[case].fields,
            None => &self.fields,
        }
    }

    /// Where the field with this name is, in the type itself or in one of its cases.
    pub fn field_position(&self, case: Option<usize>, name: &str) -> Option<usize> {
        let fields = self.fields_of(case);
        crate::profile::add("scan.fields", fields.len());
        let mark = mark(name);
        fields.iter().position(|field| field.mark == mark && field.name == name)
    }

    /// The field a required member with this name is delegated to (`with Add & Subtract by value`), if any.
    pub fn delegated_field_of(&self, name: &str) -> Option<&'static str> {
        self.delegates.borrow().iter().find(|(member, _)| *member == name).map(|(_, field)| *field)
    }

    pub fn case_index(&self, name: &str) -> Option<usize> {
        crate::profile::add("scan.cases", self.cases.len());
        let mark = mark(name);
        self.cases.iter().position(|case| case.mark == mark && case.name == name)
    }
}

pub struct TraitInfo {
    pub name: &'static str,
    pub module: Rc<Module>,
    pub supertraits: &'static [ast::TypeReference],
    /// Default methods. They are copied into every type that implements the trait.
    pub methods: RefCell<Vec<&'static ast::FunctionDeclaration>>,
    /// Required members (`self`, no body): only the names are kept, which is all `with ... by field` needs to know
    /// what to forward (the interpreter has no type checker to ask instead).
    pub required: RefCell<Vec<&'static str>>,
    pub statics: RefCell<Names<Rc<FunctionInfo>>>,
}

pub struct FunctionInfo {
    pub name: &'static str,
    pub declaration: &'static ast::FunctionDeclaration,
    pub module: Rc<Module>,
    pub owner: Option<Rc<TypeInfo>>,
    /// The scope a local `fn` was declared in. Functions of a module see the top-level scope of their file.
    pub environment: Option<Rc<Environment>>,
    /// Answered for every call, several times: worth reading off the declaration once
    has_self: bool,
    is_var_self: bool,
    parameters: &'static [ast::Parameter],
}

impl FunctionInfo {
    pub fn new(
        name: &'static str,
        declaration: &'static ast::FunctionDeclaration,
        module: Rc<Module>,
        owner: Option<Rc<TypeInfo>>,
        environment: Option<Rc<Environment>>,
    ) -> FunctionInfo {
        let has_self = has_self(declaration);
        FunctionInfo {
            name,
            declaration,
            module,
            owner,
            environment,
            has_self,
            is_var_self: has_self && declaration.parameters[0].is_var,
            parameters: &declaration.parameters[usize::from(has_self)..],
        }
    }

    pub fn has_self(&self) -> bool {
        self.has_self
    }

    pub fn is_var_self(&self) -> bool {
        self.is_var_self
    }

    /// The parameters without `self`.
    pub fn parameters(&self) -> &'static [ast::Parameter] {
        self.parameters
    }
}

fn has_self(declaration: &ast::FunctionDeclaration) -> bool {
    declaration.parameters.first().is_some_and(|parameter| parameter.name.text == "self")
}

pub struct Program {
    pub entry: Rc<Module>,
    pub prelude: Rc<Module>,
    /// Methods that `extend` adds to built-in types, by the name of the type
    pub extensions: Names<Names<Rc<FunctionInfo>>>,
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
        // One value type for all three: the interpreter has no types, and a range value carries which ends it has
        "Range" | "RangeFrom" | "RangeTo" => "Range",
        "File" => "File",
        "Process" => "Process",
        "Environment" => "Environment",
        "Clock" => "Clock",
        _ => return None,
    })
}

struct Loader {
    modules: HashMap<PathBuf, Rc<Module>>,
    order: Vec<Rc<Module>>,
    problems: Vec<String>,
    /// The package a directory belongs to, remembered per directory: every module of a package walks the same way up.
    packages: HashMap<PathBuf, Option<Rc<PackageOf>>>,
}

pub fn load(entry: &Path) -> Result<Program, Vec<String>> {
    let mut loader = Loader { modules: HashMap::new(), order: Vec::new(), problems: Vec::new(), packages: HashMap::new() };
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
    let mut extensions = Extensions::default();
    for module in &loader.order {
        loader.problems.extend(apply_extends(module, &prelude, &mut extensions));
    }
    for module in &loader.order {
        apply_member_aliases(module, &prelude, &mut extensions);
    }
    for module in &loader.order {
        loader.problems.extend(apply_traits(module, &prelude));
    }
    for module in &loader.order {
        apply_delegations(module, &prelude);
    }
    if !loader.problems.is_empty() {
        return Err(loader.problems);
    }
    Ok(Program { entry, prelude, extensions })
}

impl Loader {
    /// How a location of this file is written while the program runs: the package's name plus the file below the
    /// package's directory (`torbscript/compiler/src/ir/print.trb`), with forward slashes.
    ///
    /// This is `pathsOfModules` of `compiler/src/ir/lower/lower.trb`, character for character, so that a panic of
    /// stage 0 names the file a compiled program names: no working directory and no machine ever reaches the output,
    /// which is what lets the conformance suite compare the two reports byte for byte. The package is the nearest
    /// directory above the file that holds a `project.trb`; a file that belongs to no project is its own name and
    /// nothing else, the way a script is.
    fn stable_path(&mut self, path: &Path) -> String {
        let name = |path: &Path| path.file_name().map_or(String::new(), |name| name.to_string_lossy().to_string());
        let Some(package) = self.package_of(path.parent()) else { return name(path) };
        let Ok(relative) = path.strip_prefix(&package.directory) else { return name(path) };
        let relative = relative.components().map(|part| part.as_os_str().to_string_lossy()).collect::<Vec<_>>().join("/");
        if package.name.is_empty() {
            return relative;
        }
        format!("{}/{relative}", package.name)
    }

    /// The nearest package above a directory, or `None` where there is none up to the root.
    fn package_of(&mut self, directory: Option<&Path>) -> Option<Rc<PackageOf>> {
        let directory = directory?;
        if let Some(found) = self.packages.get(directory) {
            return found.clone();
        }
        let manifest = directory.join("project.trb");
        let found = match std::fs::read_to_string(&manifest) {
            Ok(source) => {
                Some(Rc::new(PackageOf { name: package_name_in(&source).unwrap_or_default(), directory: directory.to_path_buf() }))
            }
            Err(_) => self.package_of(directory.parent()),
        };
        self.packages.insert(directory.to_path_buf(), found.clone());
        found
    }

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
                    // A problem of loading is editor-facing and keeps the path of the machine, unlike a location
                    // something that *ran* is at
                    let (line, column) = LineIndex::new(module.source).line_and_column(statement.span.start);
                    let where_it_is = format!("{}:{line}:{column}", display_path(&module.path));
                    self.problems.push(format!("{where_it_is}: cannot import \"{path}\""));
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
        let stable = self.stable_path(&path);
        let module = Rc::new(Module {
            path: path.clone(),
            stable,
            source,
            file,
            scope: RefCell::new(Names::default()),
            top_level: RefCell::new(None),
        });
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
                        // `use Shape.Circle`: no module, so the path starts in the scope of this module itself
                        UseSource::Local => {
                            changed |= import_local(module, &usage.items, &mut unresolved);
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
                        UseItems::Names(items) => {
                            for item in items {
                                let text: &'static str = &item.path[0].text;
                                // `use IoError as FileProblem from "..."`: the module gets the name under the alias
                                let local: &'static str = &item.local_name().text;
                                if module.scope.borrow().contains_key(local) {
                                    continue;
                                }
                                let found = target.scope.borrow().get(text).cloned();
                                let Some(found) = found else {
                                    // `use String.shout from "acme/text"`: the first segment is a type of *this*
                                    // module, not an export of the target - and every `extend` applies everywhere
                                    // here, so there is nothing to bind (`apply_member_aliases` does the `as` half).
                                    if item.path.len() > 1 {
                                        continue;
                                    }
                                    unresolved
                                        .push(format!("{}: \"{path}\" does not declare `{text}`", module.location(item.path[0].span)));
                                    continue;
                                };
                                // `use Option, Option.Some from "./option"`: the segments after the first are cases
                                match walk_path(found, &item.path) {
                                    Some(found) => {
                                        module.scope.borrow_mut().insert(local, found);
                                        changed = true;
                                    }
                                    None => unresolved.push(missing_step(module, item)),
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

/// `use Shape.Circle`: a `use` without a module, so every path starts in the scope of the importing module itself.
/// Returns whether something was added.
fn import_local(module: &Rc<Module>, items: &'static UseItems, unresolved: &mut Vec<String>) -> bool {
    let UseItems::Names(items) = items else { return false };
    let mut changed = false;
    for item in items {
        // `use Shape.Circle as Round`: the case is bound under the alias
        let local: &'static str = &item.local_name().text;
        if module.scope.borrow().contains_key(local) {
            continue;
        }
        let start = module.scope.borrow().get(item.path[0].text.as_str()).cloned();
        // Not there yet, or a built-in type whose cases the interpreter knows without a declaration
        let Some(start) = start else { continue };
        match walk_path(start, &item.path) {
            Some(found) => {
                module.scope.borrow_mut().insert(local, found);
                changed = true;
            }
            None => unresolved.push(missing_step(module, item)),
        }
    }
    changed
}

/// The segments after the first one of a `use` path, from the item the first one named.
fn walk_path(start: Item, path: &'static [ast::Name]) -> Option<Item> {
    let mut current = start;
    for name in &path[1..] {
        current = step_into(&current, &name.text)?;
    }
    Some(current)
}

/// One step of a `use` path: a case of a type, or a name a namespace import stands for.
fn step_into(owner: &Item, name: &str) -> Option<Item> {
    let Item::Value(value) = owner else { return None };
    match value {
        Value::Module(target) => target.scope.borrow().get(name).cloned(),
        Value::Type(info) => {
            let case = info.case_index(name)?;
            let value = if info.cases[case].fields.is_empty() {
                Value::Object(Rc::new(crate::value::Object { info: info.clone(), case: Some(case), fields: Vec::new() }))
            } else {
                Value::Function(Rc::new(Function::Constructor { info: info.clone(), case: Some(case) }))
            };
            Some(Item::Value(value))
        }
        _ => None,
    }
}

/// `use Shape.Round`: the step that found nothing, named after the segment in front of it.
fn missing_step(module: &Rc<Module>, item: &'static ast::UseItem) -> String {
    let owner = &item.path[item.path.len() - 2].text;
    let name = &item.name().text;
    format!("{}: `{owner}` has no case `{name}`", module.location(item.name().span))
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
            let info = Rc::new(FunctionInfo::new(&function.name.text, function, module.clone(), None, None));
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
                required: RefCell::new(Vec::new()),
                statics: RefCell::new(Names::default()),
            });
            for member in &declaration_of_trait.members {
                let MemberKind::Function(function) = &member.kind else { continue };
                if function.body.is_none() {
                    if has_self(function) {
                        info.required.borrow_mut().push(&function.name.text);
                    }
                    continue;
                }
                if has_self(function) {
                    info.methods.borrow_mut().push(function);
                } else {
                    let function = Rc::new(FunctionInfo::new(&function.name.text, function, module.clone(), None, None));
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
        mark: mark(&field.name.text),
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
            MemberKind::Case(case) => cases.push(CaseInfo {
                name: &case.name.text,
                mark: mark(&case.name.text),
                fields: case.fields.iter().map(|field| field_info(field, false)).collect(),
            }),
            MemberKind::Constant(_) | MemberKind::Function(_) => {}
        }
    }
    let mut delegate_clauses = Vec::new();
    for clause in &declaration.traits {
        if let Some(field) = &clause.delegate {
            delegate_clauses.push((&clause.capability, field.text.as_str()));
        }
    }
    let info = Rc::new(TypeInfo {
        name: &declaration.name.text,
        module: module.clone(),
        is_shared,
        fields,
        cases,
        methods: RefCell::new(Names::default()),
        statics: RefCell::new(Names::default()),
        conversions: RefCell::new(Vec::new()),
        traits: RefCell::new(declaration.traits.iter().map(|clause| &clause.capability).collect()),
        delegate_clauses,
        delegates: RefCell::new(Vec::new()),
        names: Cell::new(0),
    });
    for field in &info.fields {
        info.remember(field.name);
    }
    for case in &info.cases {
        info.remember(case.name);
    }
    add_members(&info, module, &declaration.members);
    info
}

/// Functions and constants of a `type` or `extend` body.
fn add_members(info: &Rc<TypeInfo>, module: &Rc<Module>, members: &'static [ast::Member]) {
    for member in members {
        match &member.kind {
            MemberKind::Function(function) => {
                let name: &'static str = &function.name.text;
                let function = Rc::new(FunctionInfo::new(name, function, module.clone(), Some(info.clone()), None));
                info.remember(name);
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
                    info.remember(name);
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

type Extensions = Names<Names<Rc<FunctionInfo>>>;

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
                        let function = Rc::new(FunctionInfo::new(name, function, module.clone(), None, None));
                        extensions.entry(builtin).or_default().insert(name, function);
                    }
                }
            }
        }
    }
    problems
}

/// `use String.shout as yell from "acme/text"`: stage 0 has no visibility rule - every `extend` of every loaded module
/// applies everywhere - so an imported member needs nothing here. The `as` half does: the type gets the same member a
/// second time, under the name the importing file wrote. That makes the alias global, which is as close as an untyped
/// stage 0 gets to a per-file name.
fn apply_member_aliases(module: &Rc<Module>, prelude: &Rc<Module>, extensions: &mut Extensions) {
    for statement in &module.file.statements {
        let StatementKind::Declaration(ast::Declaration { kind: DeclarationKind::Use(usage), .. }) = &statement.kind else {
            continue;
        };
        let UseItems::Names(items) = &usage.items else { continue };
        for item in items {
            if item.path.len() < 2 {
                continue;
            }
            let Some(alias) = &item.alias else { continue };
            let alias: &'static str = &alias.text;
            let declared: &str = &item.name().text;
            let owner: &str = &item.path[item.path.len() - 2].text;
            match lookup(module, prelude, owner) {
                Some(Item::Value(Value::Type(info))) => {
                    let method = info.methods.borrow().get(declared).cloned();
                    if let Some(method) = method {
                        info.remember(alias);
                        info.methods.borrow_mut().insert(alias, method);
                        continue;
                    }
                    let found = info.statics.borrow().get(declared).cloned();
                    if let Some(found) = found {
                        info.remember(alias);
                        info.statics.borrow_mut().insert(alias, found);
                    }
                }
                other => {
                    let builtin = match other {
                        Some(Item::Value(Value::Builtin(name))) => Some(name),
                        _ => builtin_type(owner),
                    };
                    let Some(builtin) = builtin else { continue };
                    let found = extensions.get(builtin).and_then(|members| members.get(declared)).cloned();
                    if let Some(found) = found {
                        extensions.entry(builtin).or_default().insert(alias, found);
                    }
                }
            }
        }
    }
}

/// The trait names one element of a `with` list stands for: one for a plain name, several for an `&` group
/// (`Add & Subtract`). Anything else (a bound that did not resolve to a name) contributes none.
fn trait_names_of(reference: &'static ast::TypeReference) -> Vec<&'static str> {
    match &reference.kind {
        TypeKind::Intersection(members) => members.iter().filter_map(named).collect(),
        _ => named(reference).into_iter().collect(),
    }
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
        let mut pending: Vec<(&'static str, Span, Rc<Module>)> = info
            .traits
            .borrow()
            .iter()
            .flat_map(|reference| trait_names_of(reference).into_iter().map(|name| (name, reference.span, info.module.clone())))
            .collect();
        let mut seen: Vec<&'static str> = Vec::new();
        while let Some((name, span, context)) = pending.pop() {
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
                        info.module.location(span),
                        info.name
                    ));
                }
                let function = FunctionInfo::new(method_name, method, implemented.module.clone(), Some(info.clone()), None);
                info.remember(method_name);
                info.methods.borrow_mut().insert(method_name, Rc::new(function));
            }
            pending.extend(implemented.supertraits.iter().flat_map(|supertrait| {
                trait_names_of(supertrait).into_iter().map(|name| (name, supertrait.span, implemented.module.clone()))
            }));
        }
    }
    problems
}

/// Every required member a trait asks for, transitively over its supertraits: what `apply_delegations` forwards to a
/// field. Only the name is kept - the interpreter has no type checker to ask what the signature is.
fn required_members_of(start: &Rc<TraitInfo>, prelude: &Rc<Module>) -> Vec<&'static str> {
    let mut found: Vec<&'static str> = start.required.borrow().clone();
    let mut pending: Vec<(&'static str, Rc<Module>)> = start
        .supertraits
        .iter()
        .flat_map(|reference| trait_names_of(reference).into_iter().map(|name| (name, start.module.clone())))
        .collect();
    let mut seen: Vec<&'static str> = vec![start.name];
    while let Some((name, context)) = pending.pop() {
        if seen.contains(&name) {
            continue;
        }
        seen.push(name);
        let Some(Item::Value(Value::Trait(implemented))) = lookup(&context, prelude, name) else { continue };
        found.extend(implemented.required.borrow().iter().copied());
        pending.extend(
            implemented
                .supertraits
                .iter()
                .flat_map(|reference| trait_names_of(reference).into_iter().map(|name| (name, implemented.module.clone()))),
        );
    }
    found
}

/// `with Add & Subtract by value`: every required member `Add` and `Subtract` ask for is dispatched to `value` at
/// the call, exactly the way `+` is dispatched to a hand-written `add` (`operate`) - the interpreter has no type
/// checker in front of it, so this is found by name and not by signature.
fn apply_delegations(module: &Rc<Module>, prelude: &Rc<Module>) {
    let types: Vec<Rc<TypeInfo>> = module
        .scope
        .borrow()
        .values()
        .filter_map(|item| match item {
            Item::Value(Value::Type(info)) if Rc::ptr_eq(&info.module, module) => Some(info.clone()),
            _ => None,
        })
        .collect();
    for info in &types {
        for (reference, field) in &info.delegate_clauses {
            if info.field_position(None, field).is_none() {
                continue;
            }
            for name in trait_names_of(reference) {
                let Some(Item::Value(Value::Trait(implemented))) = lookup(module, prelude, name) else { continue };
                for member in required_members_of(&implemented, prelude) {
                    let mut delegates = info.delegates.borrow_mut();
                    if !delegates.iter().any(|(existing, _)| *existing == member) {
                        delegates.push((member, field));
                    }
                }
            }
        }
    }
}
