//! Runtime values. Everything is a value: storage is reference counted and copied on write (`Rc::make_mut`), which is
//! exactly the memory model of the language, minus the optimizations.

use std::cell::{Cell, RefCell};
use std::cmp::Ordering;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::rc::Rc;

use torb_syntax::ast;

use crate::program::{FunctionInfo, Module, TraitInfo, TypeInfo};

#[derive(Clone)]
pub enum Value {
    Void,
    Bool(bool),
    Int(i64),
    Float(f64),
    Char(char),
    Text(Text),
    List(Rc<Vec<Value>>),
    Map(Rc<Table>),
    /// A table whose values are `Void`
    Set(Rc<Table>),
    Tuple(Rc<Tuple>),
    Range(Range),
    Option(Option<Rc<Value>>),
    Result(Result<Rc<Value>, Rc<Value>>),
    Object(Rc<Object>),
    Function(Rc<Function>),
    /// `.Circle(2.0)` before the expected type is known. There is no type checker, so the type is found where the
    /// value arrives: an annotation, a parameter, a field, the other side of `==`.
    Implicit(Rc<ImplicitCase>),
    // Not values of the language, but what a name can refer to: `Point`, `Show`, `List`, `math` (namespace import)
    Type(Rc<TypeInfo>),
    Trait(Rc<TraitInfo>),
    Builtin(&'static str),
    Module(Rc<Module>),
}

/// A string or a slice of one. Slicing shares the buffer, as the language promises. The offsets are `u32`, which keeps
/// `Value` at 32 bytes instead of 40 - it is moved and cloned for every step the interpreter takes.
#[derive(Clone)]
pub struct Text {
    buffer: Rc<str>,
    start: u32,
    end: u32,
}

impl Text {
    pub fn new(text: &str) -> Text {
        crate::profile::add("text.allocate", text.len());
        assert!(text.len() <= u32::MAX as usize, "the bootstrap interpreter handles strings up to 4 GB");
        Text { buffer: Rc::from(text), start: 0, end: text.len() as u32 }
    }

    pub fn as_str(&self) -> &str {
        &self.buffer[self.start as usize..self.end as usize]
    }

    /// `None` if an offset is out of range or inside of a character.
    pub fn slice(&self, from: usize, to: usize) -> Option<Text> {
        let text = self.as_str();
        if from > to || to > text.len() || !text.is_char_boundary(from) || !text.is_char_boundary(to) {
            return None;
        }
        Some(Text { buffer: self.buffer.clone(), start: self.start + from as u32, end: self.start + to as u32 })
    }
}

#[derive(Clone)]
pub struct Tuple {
    pub labels: Vec<Option<&'static str>>,
    pub items: Vec<Value>,
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Range {
    pub start: i64,
    /// Exclusive. `None` is open-ended (`0..`)
    pub end: Option<i64>,
    /// Whether the source wrote a start at all. `start` is 0 either way; this is what `..7` is shown by
    pub has_start: bool,
    /// Whether the source wrote `..=`. `end` above is exclusive either way; this is what `1..=3` is shown by
    pub inclusive: bool,
}

pub type LabeledValues = Vec<(&'static str, Value)>;

pub struct ImplicitCase {
    pub name: &'static str,
    /// `None` for `.Empty`, the arguments for `.Circle(2.0)`
    pub arguments: Option<(Vec<Value>, LabeledValues)>,
}

#[derive(Clone)]
pub struct Object {
    pub info: Rc<TypeInfo>,
    pub case: Option<usize>,
    pub fields: Vec<Value>,
}

pub enum Function {
    Declared {
        info: Rc<FunctionInfo>,
        receiver: Option<Value>,
    },
    Closure(Closure),
    /// A native function (`print`) or a static function of a built-in type (`List.of`)
    Native {
        owner: &'static str,
        name: &'static str,
    },
    NativeMethod {
        receiver: Value,
        name: &'static str,
    },
    /// `with Add & Subtract by value`: a required member with no body of its own, taken as a value (`self.compare`
    /// inside of `max`'s default body, `compare(other)` being `self.compare(other)`) instead of called outright.
    Delegated {
        object: Rc<Object>,
        field: &'static str,
        name: &'static str,
    },
    Constructor {
        info: Rc<TypeInfo>,
        case: Option<usize>,
    },
    /// `Some`, `Ok`, `Fail`
    Wrap(&'static str),
    /// `Target.from`: chosen by the type of the argument
    Conversion(Rc<TypeInfo>),
    /// The argument of a `lazy` parameter: evaluated at most once, on first use
    Lazy(Lazy),
}

pub struct Lazy {
    pub expression: &'static ast::Expression,
    pub environment: Rc<Environment>,
    pub value: RefCell<Option<Value>>,
}

pub struct Closure {
    pub ast: &'static ast::Closure,
    pub environment: Rc<Environment>,
    /// The type of the parameter the closure was passed to: names the implicit parameters and marks receivers
    pub signature: Option<&'static [ast::FunctionTypeParameter]>,
}

impl Value {
    pub fn text(text: &str) -> Value {
        Value::Text(Text::new(text))
    }

    pub fn some(value: Value) -> Value {
        crate::profile::count("allocate.Some");
        Value::Option(Some(Rc::new(value)))
    }

    pub const NONE: Value = Value::Option(None);

    pub fn ok(value: Value) -> Value {
        crate::profile::count("allocate.Ok");
        Value::Result(Ok(Rc::new(value)))
    }

    pub fn error(value: Value) -> Value {
        crate::profile::count("allocate.Fail");
        Value::Result(Err(Rc::new(value)))
    }

    pub fn list(items: Vec<Value>) -> Value {
        crate::profile::add("allocate.List", items.len());
        Value::List(Rc::new(items))
    }

    pub fn tuple(items: Vec<Value>) -> Value {
        crate::profile::count("allocate.Tuple");
        Value::Tuple(Rc::new(Tuple { labels: vec![None; items.len()], items }))
    }

    pub fn from_option(value: Option<Value>) -> Value {
        value.map_or(Value::NONE, Value::some)
    }

    /// The name of the type as the language knows it (for messages and for finding extension methods).
    pub fn type_name(&self) -> &str {
        match self {
            Value::Void => "Void",
            Value::Bool(_) => "Bool",
            Value::Int(_) => "Int",
            Value::Float(_) => "Float",
            Value::Char(_) => "Char",
            Value::Text(_) => "String",
            Value::List(_) => "List",
            Value::Map(_) => "Map",
            Value::Set(_) => "Set",
            Value::Tuple(_) => "Tuple",
            Value::Range(_) => "Range",
            Value::Option(_) => "Option",
            Value::Result(_) => "Result",
            Value::Object(object) => object.info.name,
            Value::Function(_) => "Function",
            Value::Implicit(_) => "a case without a type",
            Value::Type(_) => "a type",
            Value::Trait(_) => "a trait",
            Value::Builtin(_) => "a built-in type",
            Value::Module(_) => "a module",
        }
    }
}

/// Structural equality. (The bootstrap interpreter does not call user-defined `equals`.)
impl PartialEq for Value {
    fn eq(&self, other: &Value) -> bool {
        match (self, other) {
            (Value::Void, Value::Void) => true,
            (Value::Bool(a), Value::Bool(b)) => a == b,
            (Value::Int(a), Value::Int(b)) => a == b,
            (Value::Float(a), Value::Float(b)) => a == b,
            (Value::Char(a), Value::Char(b)) => a == b,
            (Value::Text(a), Value::Text(b)) => a.as_str() == b.as_str(),
            (Value::List(a), Value::List(b)) => Rc::ptr_eq(a, b) || a == b,
            (Value::Map(a), Value::Map(b)) => Rc::ptr_eq(a, b) || a.equals(b),
            (Value::Set(a), Value::Set(b)) => Rc::ptr_eq(a, b) || a.equals(b),
            (Value::Tuple(a), Value::Tuple(b)) => a.items == b.items,
            (Value::Range(a), Value::Range(b)) => a == b,
            (Value::Option(a), Value::Option(b)) => a == b,
            (Value::Result(a), Value::Result(b)) => a == b,
            (Value::Object(a), Value::Object(b)) => {
                Rc::ptr_eq(a, b) || (Rc::ptr_eq(&a.info, &b.info) && a.case == b.case && a.fields == b.fields)
            }
            (Value::Function(a), Value::Function(b)) => Rc::ptr_eq(a, b),
            _ => false,
        }
    }
}

/// The order of `<`, `sorted` and `min`/`max` for built-in values. Objects are compared through their `compare` method
/// by the interpreter, not here.
pub fn compare_values(left: &Value, right: &Value) -> Option<Ordering> {
    match (left, right) {
        (Value::Int(a), Value::Int(b)) => Some(a.cmp(b)),
        (Value::Float(a), Value::Float(b)) => a.partial_cmp(b),
        (Value::Char(a), Value::Char(b)) => Some(a.cmp(b)),
        (Value::Bool(a), Value::Bool(b)) => Some(a.cmp(b)),
        (Value::Text(a), Value::Text(b)) => Some(a.as_str().cmp(b.as_str())),
        (Value::Tuple(a), Value::Tuple(b)) => compare_sequences(&a.items, &b.items),
        (Value::List(a), Value::List(b)) => compare_sequences(a, b),
        (Value::Option(a), Value::Option(b)) => match (a, b) {
            (None, None) => Some(Ordering::Equal),
            (None, Some(_)) => Some(Ordering::Less),
            (Some(_), None) => Some(Ordering::Greater),
            (Some(a), Some(b)) => compare_values(a, b),
        },
        _ => None,
    }
}

fn compare_sequences(left: &[Value], right: &[Value]) -> Option<Ordering> {
    for (a, b) in left.iter().zip(right) {
        match compare_values(a, b)? {
            Ordering::Equal => {}
            other => return Some(other),
        }
    }
    Some(left.len().cmp(&right.len()))
}

/// A value as the key of a table.
#[derive(Clone)]
pub struct Key(pub Value);

impl PartialEq for Key {
    fn eq(&self, other: &Key) -> bool {
        self.0 == other.0
    }
}

impl Eq for Key {}

impl Hash for Key {
    fn hash<H: Hasher>(&self, state: &mut H) {
        hash_value(&self.0, state);
    }
}

fn hash_value<H: Hasher>(value: &Value, state: &mut H) {
    crate::profile::count("table.hash_node");
    match value {
        Value::Void => 0.hash(state),
        Value::Bool(value) => value.hash(state),
        Value::Int(value) => value.hash(state),
        Value::Float(value) => value.to_bits().hash(state),
        Value::Char(value) => value.hash(state),
        Value::Text(value) => value.as_str().hash(state),
        Value::List(items) => items.iter().for_each(|item| hash_value(item, state)),
        Value::Tuple(tuple) => tuple.items.iter().for_each(|item| hash_value(item, state)),
        Value::Range(range) => range.hash(state),
        Value::Option(value) => {
            value.is_some().hash(state);
            if let Some(value) = value {
                hash_value(value, state);
            }
        }
        Value::Result(value) => match value {
            Ok(value) | Err(value) => hash_value(value, state),
        },
        Value::Object(object) => {
            object.info.name.hash(state);
            object.case.hash(state);
            object.fields.iter().for_each(|field| hash_value(field, state));
        }
        // Tables are equal in any order, so only the size can go into the hash
        Value::Map(table) | Value::Set(table) => table.len().hash(state),
        Value::Function(_) | Value::Implicit(_) | Value::Type(_) | Value::Trait(_) | Value::Builtin(_) | Value::Module(_) => {
            1.hash(state);
        }
    }
}

/// A hash table that iterates in insertion order (deterministic iteration is a rule of the language).
#[derive(Clone, Default)]
pub struct Table {
    entries: Vec<Option<(Value, Value)>>,
    index: HashMap<Key, usize>,
}

impl Table {
    pub fn len(&self) -> usize {
        self.index.len()
    }

    pub fn is_empty(&self) -> bool {
        self.index.is_empty()
    }

    pub fn get(&self, key: &Value) -> Option<&Value> {
        crate::profile::count("table.get");
        let position = *self.index.get(&Key(key.clone()))?;
        self.entries[position].as_ref().map(|(_, value)| value)
    }

    pub fn get_mut(&mut self, key: &Value) -> Option<&mut Value> {
        let position = *self.index.get(&Key(key.clone()))?;
        self.entries[position].as_mut().map(|(_, value)| value)
    }

    pub fn contains(&self, key: &Value) -> bool {
        crate::profile::count("table.contains");
        self.index.contains_key(&Key(key.clone()))
    }

    pub fn insert(&mut self, key: Value, value: Value) -> Option<Value> {
        crate::profile::count("table.insert");
        if let Some(existing) = self.get_mut(&key) {
            return Some(std::mem::replace(existing, value));
        }
        self.index.insert(Key(key.clone()), self.entries.len());
        self.entries.push(Some((key, value)));
        None
    }

    pub fn remove(&mut self, key: &Value) -> Option<Value> {
        let position = self.index.remove(&Key(key.clone()))?;
        let removed = self.entries[position].take().map(|(_, value)| value);
        if self.entries.len() > 32 && self.index.len() * 2 < self.entries.len() {
            self.entries.retain(Option::is_some);
            for (position, entry) in self.entries.iter().enumerate() {
                if let Some((key, _)) = entry {
                    self.index.insert(Key(key.clone()), position);
                }
            }
        }
        removed
    }

    pub fn clear(&mut self) {
        self.entries.clear();
        self.index.clear();
    }

    pub fn iter(&self) -> impl Iterator<Item = &(Value, Value)> {
        self.entries.iter().flatten()
    }

    pub fn keys(&self) -> impl Iterator<Item = &Value> {
        self.iter().map(|(key, _)| key)
    }

    fn equals(&self, other: &Table) -> bool {
        self.len() == other.len() && self.iter().all(|(key, value)| other.get(key) == Some(value))
    }
}

/// One scope. Closures keep their scope alive, which is how a captured `var` is shared between a closure and its scope.
pub struct Environment {
    pub module: Rc<Module>,
    pub parent: Option<Rc<Environment>>,
    pub slots: RefCell<Vec<Slot>>,
    /// The one scope the top-level code of this module runs in. Constants of an entry file are evaluated there.
    pub is_top_level: bool,
    /// One bit per name this scope declares. A name whose bit is missing is not in this scope, so looking it up
    /// somewhere in the chain does not have to read the slots at all - and most names are not in most scopes.
    /// Names share bits; then the slots decide. Bits are only ever added, so the filter can never hide a binding.
    names: Cell<u64>,
}

pub struct Slot {
    pub name: &'static str,
    /// `mark(name)`, so that a scan over the slots compares integers
    mark: u32,
    pub value: Value,
    pub is_var: bool,
}

/// A fingerprint of a name, from its length and its first and last character. Looking a name up means scanning a
/// list of names - the slots of a scope, the fields of a type, the cases of a type - and comparing these instead of
/// the characters is what makes those scans cheap. Names that share a fingerprint are still compared character by
/// character, so a collision only costs time.
pub fn mark(name: &str) -> u32 {
    let bytes = name.as_bytes();
    let first = u32::from(bytes.first().copied().unwrap_or(0));
    let last = u32::from(bytes.last().copied().unwrap_or(0));
    ((bytes.len() as u32) << 16) | (first << 8) | last
}

/// The bit of a name in a filter of 64 bits: one for the names a scope declares, one for the members a type has.
/// A name whose bit is missing is certainly not in there; names share bits, so a bit that is set proves nothing.
pub fn bit_of(mark: u32) -> u64 {
    1u64 << (mark.wrapping_mul(0x9E37_79B9) >> 26)
}

impl Environment {
    pub fn root(module: Rc<Module>) -> Rc<Environment> {
        Rc::new(Environment { module, parent: None, slots: RefCell::new(Vec::new()), is_top_level: true, names: Cell::new(0) })
    }

    pub fn child(parent: &Rc<Environment>) -> Rc<Environment> {
        crate::profile::count("environment.child");
        Rc::new(Environment {
            module: parent.module.clone(),
            parent: Some(parent.clone()),
            slots: RefCell::new(Vec::new()),
            is_top_level: false,
            names: Cell::new(0),
        })
    }

    /// A scope that is not below the one it was written in: the body of a function sees its module, not its caller.
    pub fn of_call(module: Rc<Module>, parent: Rc<Environment>) -> Rc<Environment> {
        crate::profile::count("environment.child");
        Rc::new(Environment { module, parent: Some(parent), slots: RefCell::new(Vec::new()), is_top_level: false, names: Cell::new(0) })
    }

    /// Makes a scope that nobody references anymore into a new one. It keeps the capacity its slots already have,
    /// which is what a pool of scopes saves: a block, a turn of a loop and a call each need one.
    pub fn reuse(&mut self, module: Rc<Module>, parent: Rc<Environment>) {
        self.module = module;
        self.parent = Some(parent);
        self.slots.get_mut().clear();
        self.names.set(0);
    }

    /// Drops everything the scope holds, so that nothing stays alive while it waits to be used again.
    pub fn empty(&mut self) {
        self.parent = None;
        self.slots.get_mut().clear();
        self.names.set(0);
    }

    pub fn declare(&self, name: &'static str, value: Value, is_var: bool) {
        crate::profile::count("environment.declare");
        let mark = mark(name);
        self.names.set(self.names.get() | bit_of(mark));
        self.slots.borrow_mut().push(Slot { name, mark, value, is_var });
    }

    pub fn lookup(&self, name: &str) -> Option<Value> {
        crate::profile::count("environment.lookup");
        self.find(name).map(|(environment, position)| environment.slots.borrow()[position].value.clone())
    }

    /// The scope and the position of the innermost binding with this name.
    pub fn find(&self, name: &str) -> Option<(&Environment, usize)> {
        crate::profile::count("environment.find");
        let mark = mark(name);
        let bit = bit_of(mark);
        let mut current = self;
        loop {
            if current.names.get() & bit != 0 {
                if crate::profile::is_enabled() {
                    crate::profile::add("environment.scope_searched", current.slots.borrow().len());
                }
                let found = current.slots.borrow().iter().rposition(|slot| slot.mark == mark && slot.name == name);
                if let Some(position) = found {
                    return Some((current, position));
                }
            }
            crate::profile::count("environment.scope_walked");
            current = current.parent.as_deref()?;
        }
    }

    pub fn find_shared(self: &Rc<Environment>, name: &str) -> Option<(Rc<Environment>, usize)> {
        crate::profile::count("environment.find_shared");
        let mark = mark(name);
        let bit = bit_of(mark);
        let mut current = self;
        loop {
            if current.names.get() & bit != 0 {
                let position = current.slots.borrow().iter().rposition(|slot| slot.mark == mark && slot.name == name);
                if let Some(position) = position {
                    return Some((current.clone(), position));
                }
            }
            current = current.parent.as_ref()?;
        }
    }
}
