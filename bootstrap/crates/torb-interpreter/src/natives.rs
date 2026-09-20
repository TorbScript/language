//! The part of the standard library that the bootstrap interpreter implements itself. Names and meanings follow
//! `std/`; pipelines are eager here (a stage returns a list), which a correct program cannot observe unless it relies
//! on laziness (infinite sources, side effects in stages).

use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
use std::io::Write;
use std::rc::Rc;

use crate::interpreter::{failure, slice_bounds, Arguments, Eval, Flow, Interpreter};
use crate::program::Item;
use crate::value::{Function, Key, Object, Range, Table, Value};

pub fn global(name: &'static str) -> Option<Value> {
    Some(match name {
        "Some" | "Ok" | "Fail" => Value::Function(Rc::new(Function::Wrap(name))),
        "None" => Value::NONE,
        "print" | "printError" | "readLine" | "panic" | "test" | "group" | "do" | "describe" => {
            Value::Function(Rc::new(Function::Native { owner: "", name }))
        }
        _ => return None,
    })
}

pub fn constant(owner: &str, name: &str) -> Option<Value> {
    Some(match (owner, name) {
        ("Float", "pi") => Value::Float(std::f64::consts::PI),
        ("Float", "infinity") => Value::Float(f64::INFINITY),
        ("Int", "minimum") => Value::Int(i64::MIN),
        ("Int", "maximum") => Value::Int(i64::MAX),
        _ => return None,
    })
}

/// Methods every `type` gets generated.
pub fn is_object_method(name: &str) -> bool {
    matches!(name, "copy" | "show" | "equals" | "notEquals" | "hash")
}

/// The items of everything that is `Iterable`.
pub fn items_of(value: &Value) -> Eval<Vec<Value>> {
    if crate::profile::is_enabled() {
        crate::profile::add("items_of", if let Value::List(items) = value { items.len() } else { 0 });
    }
    match value {
        Value::List(items) => Ok(items.to_vec()),
        Value::Set(table) => Ok(table.keys().cloned().collect()),
        Value::Map(table) => Ok(table.iter().map(|(key, value)| Value::tuple(vec![key.clone(), value.clone()])).collect()),
        Value::Range(Range { start, end: Some(end), .. }) => Ok((*start..*end).map(Value::Int).collect()),
        Value::Range(Range { end: None, .. }) => Err(failure("This range has no end. The bootstrap interpreter can only use it in `for`")),
        Value::Option(option) => Ok(option.iter().map(|value| (**value).clone()).collect()),
        other => Err(failure(format!("A value of type {} is not iterable", other.type_name()))),
    }
}

/// Verbs: they change their receiver, so the interpreter moves it out of its path for the call.
pub fn is_mutating(receiver: &Value, name: &str) -> bool {
    match receiver {
        Value::List(_) => matches!(
            name,
            "add" | "addAll" | "insert" | "remove" | "removeAt" | "clear" | "reverse" | "sort" | "swapAt" | "set" | "compact"
        ),
        Value::Map(_) => matches!(name, "add" | "addAll" | "set" | "remove" | "clear" | "merge" | "getOrSet"),
        Value::Set(_) => matches!(name, "add" | "addAll" | "remove" | "clear" | "removeAll" | "retainAll"),
        _ => false,
    }
}

fn text_of(value: &Value, what: &str) -> Eval<String> {
    match value {
        Value::Text(text) => Ok(text.as_str().to_string()),
        other => Err(failure(format!("`{what}` must be a String, this is a value of type {}", other.type_name()))),
    }
}

fn int_of(value: &Value, what: &str) -> Eval<i64> {
    match value {
        Value::Int(value) => Ok(*value),
        other => Err(failure(format!("`{what}` must be an Int, this is a value of type {}", other.type_name()))),
    }
}

fn position_of(value: &Value, what: &str) -> Eval<usize> {
    usize::try_from(int_of(value, what)?).map_err(|_| failure(format!("`{what}` must not be negative")))
}

fn prelude_object(interpreter: &Interpreter, name: &str, fields: Vec<Value>) -> Value {
    let item = interpreter.program.prelude.scope.borrow().get(name).cloned();
    match item {
        Some(Item::Value(Value::Type(info))) => Value::Object(Rc::new(Object { info, case: None, fields })),
        _ => Value::text(name),
    }
}

fn ordering(interpreter: &Interpreter, ordering: Ordering) -> Value {
    let item = interpreter.program.prelude.scope.borrow().get("Ordering").cloned();
    let Some(Item::Value(Value::Type(info))) = item else { return Value::Void };
    let case = info.case_index(match ordering {
        Ordering::Less => "Less",
        Ordering::Equal => "Equal",
        Ordering::Greater => "Greater",
    });
    Value::Object(Rc::new(Object { info, case, fields: Vec::new() }))
}

fn truth(interpreter: &mut Interpreter, function: &Value, arguments: Vec<Value>) -> Eval<bool> {
    match interpreter.call_function(function, arguments)? {
        Value::Bool(value) => Ok(value),
        other => Err(failure(format!("The closure must return a Bool, it returned {}", interpreter.describe(&other)))),
    }
}

// --- Static functions ---------------------------------------------------------------------------------------------------

pub fn call_static(interpreter: &mut Interpreter, owner: &str, name: &str, arguments: Arguments) -> Eval {
    match (owner, name) {
        ("", "print") => {
            let mut shown = Vec::new();
            for value in &arguments.positional {
                shown.push(interpreter.show(value, true)?);
            }
            // A closed pipe (`torb run ... | head`) is not a reason to panic
            let _ = writeln!(std::io::stdout(), "{}", shown.join(" "));
            Ok(Value::Void)
        }
        ("", "printError") => {
            let mut shown = Vec::new();
            for value in &arguments.positional {
                shown.push(interpreter.show(value, true)?);
            }
            let _ = writeln!(std::io::stderr(), "{}", shown.join(" "));
            Ok(Value::Void)
        }
        ("", "readLine") => {
            let mut line = String::new();
            Ok(match std::io::stdin().read_line(&mut line) {
                Ok(read) if read > 0 => Value::some(Value::text(line.trim_end_matches(['\n', '\r']))),
                _ => Value::NONE,
            })
        }
        ("", "panic") => {
            let message = arguments.required(0, "message")?;
            Err(failure(format!("panic: {}", interpreter.show(&message, true)?)))
        }
        ("", "do") => interpreter.call_function(&arguments.required(0, "body")?, Vec::new()),
        ("", "describe") => Ok(Value::text(&interpreter.show(&arguments.required(0, "value")?, false)?)),
        ("", "group") => {
            let name = text_of(&arguments.required(0, "name")?, "name")?;
            interpreter.tests.groups.push(name);
            let result = interpreter.call_function(&arguments.required(1, "body")?, Vec::new());
            interpreter.tests.groups.pop();
            result
        }
        ("", "test") => {
            let mut name = interpreter.tests.groups.join(" > ");
            if !name.is_empty() {
                name.push_str(" > ");
            }
            name.push_str(&text_of(&arguments.required(0, "name")?, "name")?);
            match interpreter.call_function(&arguments.required(1, "body")?, Vec::new()) {
                Err(Flow::Failure(problem)) => {
                    interpreter.tests.failed += 1;
                    println!("  FAILED  {name}");
                    println!("          {}", problem.message.replace('\n', "\n          "));
                    if let Some(location) = &problem.location {
                        println!("          at {location}");
                    }
                }
                _ => {
                    interpreter.tests.passed += 1;
                    println!("  ok      {name}");
                }
            }
            Ok(Value::Void)
        }

        ("List", "new") => Ok(Value::list(Vec::new())),
        ("List", "of") => Ok(Value::list(arguments.positional)),
        ("List", "from") => Ok(Value::list(items_of(&arguments.required(0, "items")?)?)),
        ("List", "filled") => {
            let count = position_of(&arguments.required(0, "count")?, "count")?;
            Ok(Value::list(vec![arguments.required(1, "value")?; count]))
        }
        ("Set", "new") => Ok(Value::Set(Rc::new(Table::default()))),
        ("Set", "of") => Ok(set_of(arguments.positional)),
        ("Set", "from") => Ok(set_of(items_of(&arguments.required(0, "items")?)?)),
        ("Map", "new") => Ok(Value::Map(Rc::new(Table::default()))),
        ("Map", "of") => map_of(arguments.positional),
        ("Map", "from") => map_of(items_of(&arguments.required(0, "entries")?)?),

        ("String", "from") => {
            let value = arguments.required(0, "value")?;
            if let Value::List(items) = &value {
                if items.iter().all(|item| matches!(item, Value::Char(_))) {
                    let text: String =
                        items.iter().filter_map(|item| if let Value::Char(character) = item { Some(*character) } else { None }).collect();
                    return Ok(Value::text(&text));
                }
            }
            Ok(Value::text(&interpreter.show(&value, true)?))
        }

        ("Int", "parse" | "parseDigits") => {
            let text = text_of(&arguments.required(0, "text")?, "text")?;
            let radix = if name == "parseDigits" { int_of(&arguments.required(1, "radix")?, "radix")? } else { 10 };
            if !(2..=36).contains(&radix) {
                return Err(failure("`radix` must be between 2 and 36"));
            }
            Ok(match i64::from_str_radix(&text.replace('_', ""), radix as u32) {
                Ok(value) => Value::ok(Value::Int(value)),
                Err(_) => Value::error(prelude_object(interpreter, "NumberParseError", vec![Value::text(&text)])),
            })
        }
        ("Int", "from") => match arguments.required(0, "value")? {
            Value::Char(character) => Ok(Value::Int(i64::from(u32::from(character)))),
            Value::Int(value) => Ok(Value::Int(value)),
            Value::Bool(value) => Ok(Value::Int(i64::from(value))),
            other => Err(failure(format!("There is no `Int.from` for {}. For a Float, use `Int.tryFrom`", interpreter.describe(&other)))),
        },
        ("Int", "tryFrom") => match arguments.required(0, "value")? {
            Value::Float(value) if value.fract() == 0.0 && value.abs() < 9.2e18 => Ok(Value::ok(Value::Int(value as i64))),
            Value::Float(value) => Ok(Value::error(prelude_object(interpreter, "NumberRangeError", vec![Value::text(&value.to_string())]))),
            Value::Int(value) => Ok(Value::ok(Value::Int(value))),
            other => Err(failure(format!("There is no `Int.tryFrom` for {}", interpreter.describe(&other)))),
        },
        ("Float", "parse") => {
            let text = text_of(&arguments.required(0, "text")?, "text")?;
            Ok(match text.replace('_', "").parse::<f64>() {
                Ok(value) => Value::ok(Value::Float(value)),
                Err(_) => Value::error(prelude_object(interpreter, "NumberParseError", vec![Value::text(&text)])),
            })
        }
        ("Float", "from") => match arguments.required(0, "value")? {
            Value::Int(value) => Ok(Value::Float(value as f64)),
            Value::Float(value) => Ok(Value::Float(value)),
            other => Err(failure(format!("There is no `Float.from` for {}", interpreter.describe(&other)))),
        },
        ("Char", "tryFrom") => {
            let code = int_of(&arguments.required(0, "value")?, "value")?;
            Ok(match u32::try_from(code).ok().and_then(char::from_u32) {
                Some(character) => Value::ok(Value::Char(character)),
                None => Value::error(prelude_object(interpreter, "NumberRangeError", vec![Value::text(&code.to_string())])),
            })
        }

        ("File", "readText") => {
            let path = text_of(&arguments.required(0, "path")?, "path")?;
            Ok(match std::fs::read_to_string(&path) {
                Ok(text) => Value::ok(Value::text(&text)),
                Err(error) => Value::error(io_error(interpreter, &path, &error)),
            })
        }
        ("File", "writeText") => {
            let path = text_of(&arguments.required(0, "path")?, "path")?;
            let text = text_of(&arguments.required(1, "text")?, "text")?;
            Ok(match std::fs::write(&path, text) {
                Ok(()) => Value::ok(Value::Void),
                Err(error) => Value::error(io_error(interpreter, &path, &error)),
            })
        }
        ("File", "absolutePath") => {
            let path = text_of(&arguments.required(0, "path")?, "path")?;
            Ok(match std::path::absolute(&path) {
                Ok(absolute) => Value::ok(Value::text(&crate::program::display_path(&absolute))),
                Err(error) => Value::error(io_error(interpreter, &path, &error)),
            })
        }
        ("File", "exists") => Ok(Value::Bool(std::path::Path::new(&text_of(&arguments.required(0, "path")?, "path")?).exists())),
        ("File", "isDirectory") => Ok(Value::Bool(std::path::Path::new(&text_of(&arguments.required(0, "path")?, "path")?).is_dir())),
        ("File", "list") => {
            let path = text_of(&arguments.required(0, "path")?, "path")?;
            let entries = std::fs::read_dir(&path).and_then(|entries| entries.collect::<Result<Vec<_>, _>>());
            Ok(match entries {
                Ok(entries) => {
                    let mut names: Vec<String> = entries.iter().map(|entry| entry.file_name().to_string_lossy().to_string()).collect();
                    names.sort();
                    Value::ok(Value::list(names.iter().map(|name| Value::text(name)).collect()))
                }
                Err(error) => Value::error(io_error(interpreter, &path, &error)),
            })
        }
        ("File", "createDirectory") => {
            let path = text_of(&arguments.required(0, "path")?, "path")?;
            Ok(match std::fs::create_dir_all(&path) {
                Ok(()) => Value::ok(Value::Void),
                Err(error) => Value::error(io_error(interpreter, &path, &error)),
            })
        }
        ("Process", "arguments") => Ok(Value::list(interpreter.arguments.iter().map(|argument| Value::text(argument)).collect())),
        ("Process", "exit") => {
            let code = int_of(&arguments.required(0, "code")?, "code")?;
            std::process::exit(code as i32)
        }
        // `torb build` needs this to find a C compiler and to run it. A program that cannot be started at all is an
        // `IoError`; one that ran and failed is an exit code, which is what tells the two apart.
        ("Process", "run") => {
            let command = text_of(&arguments.required(0, "command")?, "command")?;
            let given = items_of(&arguments.required(1, "arguments")?)?;
            let mut parameters = Vec::new();
            for value in &given {
                parameters.push(text_of(value, "argument")?);
            }
            Ok(match std::process::Command::new(&command).args(&parameters).output() {
                Ok(output) => Value::ok(prelude_object(
                    interpreter,
                    "ProcessOutput",
                    vec![
                        Value::Int(output.status.code().unwrap_or(-1) as i64),
                        Value::text(&String::from_utf8_lossy(&output.stdout)),
                        Value::text(&String::from_utf8_lossy(&output.stderr)),
                    ],
                )),
                Err(error) => Value::error(io_error(interpreter, &command, &error)),
            })
        }
        // The compiler times its own passes with this one (`torb check --timings`). Monotonic, in milliseconds, counted
        // from the first call: only differences between two readings are meaningful.
        ("Clock", "milliseconds") => {
            use std::sync::OnceLock;
            static START: OnceLock<std::time::Instant> = OnceLock::new();
            let start = START.get_or_init(std::time::Instant::now);
            Ok(Value::Int(start.elapsed().as_millis() as i64))
        }
        ("Environment", "get") => {
            let name = text_of(&arguments.required(0, "name")?, "name")?;
            Ok(match std::env::var(&name) {
                Ok(value) => Value::some(Value::text(&value)),
                Err(_) => Value::NONE,
            })
        }
        ("", _) => Err(failure(format!("Unknown function `{name}`"))),
        _ => Err(failure(format!("The bootstrap interpreter does not know `{owner}.{name}`"))),
    }
}

fn io_error(interpreter: &Interpreter, path: &str, error: &std::io::Error) -> Value {
    prelude_object(interpreter, "IoError", vec![Value::text(path), Value::text(&error.to_string())])
}

fn set_of(items: Vec<Value>) -> Value {
    let mut table = Table::default();
    for item in items {
        table.insert(item, Value::Void);
    }
    Value::Set(Rc::new(table))
}

fn entry_of(value: &Value) -> Eval<(Value, Value)> {
    match value {
        Value::Tuple(tuple) if tuple.items.len() == 2 => Ok((tuple.items[0].clone(), tuple.items[1].clone())),
        _ => Err(failure("An entry of a map is a tuple `(key, value)`")),
    }
}

fn map_of(entries: Vec<Value>) -> Eval {
    let mut table = Table::default();
    for entry in &entries {
        let (key, value) = entry_of(entry)?;
        table.insert(key, value);
    }
    Ok(Value::Map(Rc::new(table)))
}

// --- Methods ------------------------------------------------------------------------------------------------------------

pub fn call_method(interpreter: &mut Interpreter, receiver: &mut Value, name: &str, arguments: Arguments) -> Eval {
    if let Some(result) = common_method(interpreter, receiver, name, &arguments)? {
        return Ok(result);
    }
    let result = match receiver {
        Value::Int(_) | Value::Float(_) => number_method(receiver, name, &arguments)?,
        Value::Char(character) => char_method(*character, name)?,
        Value::Text(_) => text_method(interpreter, receiver, name, &arguments)?,
        Value::List(_) => list_method(interpreter, receiver, name, &arguments)?,
        Value::Map(_) => map_method(interpreter, receiver, name, &arguments)?,
        Value::Set(_) => set_method(receiver, name, &arguments)?,
        Value::Range(range) => match (name, range.end) {
            ("contains", end) => {
                let value = int_of(&arguments.required(0, "value")?, "value")?;
                Some(Value::Bool(value >= range.start && end.is_none_or(|end| value < end)))
            }
            ("length", Some(end)) => Some(Value::Int((end - range.start).max(0))),
            _ => None,
        },
        Value::Option(_) | Value::Result(_) => wrapper_method(interpreter, receiver, name, &arguments)?,
        Value::Object(_) => object_method(receiver, name, &arguments)?,
        _ => None,
    };
    if let Some(result) = result {
        return Ok(result);
    }
    if matches!(receiver, Value::List(_) | Value::Set(_) | Value::Map(_) | Value::Range(_)) {
        let items = items_of(receiver)?;
        if let Some(result) = iterable_method(interpreter, items, name, &arguments)? {
            return Ok(result);
        }
    }
    Err(failure(format!("{} has no method `{name}`", interpreter.describe(receiver))))
}

/// `Show`, `Equals`, `Hash` and `Compare` of everything.
fn common_method(interpreter: &mut Interpreter, receiver: &Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    Ok(Some(match name {
        "show" if arguments.positional.is_empty() => Value::text(&interpreter.show(receiver, true)?),
        "equals" => Value::Bool(*receiver == arguments.required(0, "other")?),
        "notEquals" => Value::Bool(*receiver != arguments.required(0, "other")?),
        "hash" if arguments.positional.is_empty() => {
            let mut hasher = std::collections::hash_map::DefaultHasher::new();
            Key(receiver.clone()).hash(&mut hasher);
            Value::Int(hasher.finish() as i64)
        }
        "compare" => {
            let result = interpreter.compare(receiver, &arguments.required(0, "other")?)?;
            ordering(interpreter, result)
        }
        "min" | "max" if !matches!(receiver, Value::List(_) | Value::Set(_) | Value::Map(_) | Value::Range(_)) => {
            let other = arguments.required(0, "other")?;
            let is_less = interpreter.compare(receiver, &other)?.is_le();
            if is_less == (name == "min") {
                receiver.clone()
            } else {
                other
            }
        }
        "clamp" => {
            let low = arguments.required(0, "low")?;
            let high = arguments.required(1, "high")?;
            if interpreter.compare(receiver, &low)?.is_lt() {
                low
            } else if interpreter.compare(receiver, &high)?.is_gt() {
                high
            } else {
                receiver.clone()
            }
        }
        _ => return Ok(None),
    }))
}

fn number_method(receiver: &Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    Ok(Some(match (receiver, name) {
        (Value::Int(value), "absolute") => Value::Int(value.checked_abs().ok_or_else(|| failure("Integer overflow"))?),
        (Value::Float(value), "absolute") => Value::Float(value.abs()),
        (Value::Float(value), "squareRoot") => Value::Float(value.sqrt()),
        (Value::Float(value), "floor") => Value::Float(value.floor()),
        (Value::Float(value), "ceiling") => Value::Float(value.ceil()),
        (Value::Float(value), "round") => Value::Float(value.round()),
        (Value::Float(value), "isNaN") => Value::Bool(value.is_nan()),
        (Value::Float(value), "isCloseTo") => {
            let other = match arguments.required(0, "other")? {
                Value::Float(other) => other,
                Value::Int(other) => other as f64,
                _ => return Err(failure("`other` must be a Float")),
            };
            let tolerance = match arguments.get(1, "tolerance") {
                Some(Value::Float(tolerance)) => tolerance,
                _ => 0.000001,
            };
            Value::Bool((value - other).abs() <= tolerance)
        }
        _ => return Ok(None),
    }))
}

fn char_method(character: char, name: &str) -> Eval<Option<Value>> {
    Ok(Some(match name {
        "isDigit" => Value::Bool(character.is_numeric()),
        "isLetter" => Value::Bool(character.is_alphabetic()),
        "isWhitespace" => Value::Bool(character.is_whitespace()),
        "toUpperCase" => Value::Char(character.to_uppercase().next().unwrap_or(character)),
        "toLowerCase" => Value::Char(character.to_lowercase().next().unwrap_or(character)),
        "byteLength" => Value::Int(character.len_utf8() as i64),
        _ => return Ok(None),
    }))
}

fn text_method(interpreter: &mut Interpreter, receiver: &Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let Value::Text(receiver_text) = receiver else { return Ok(None) };
    let text = receiver_text.as_str();
    let part = |index: usize, label: &str| -> Eval<String> { text_of(&arguments.required(index, label)?, label) };
    Ok(Some(match name {
        // `chars()` and `bytes()` are TorbScript over `charAt`/`byteAt` in `std/text` now; stage 0 keeps its own fast
        // answer because it runs the whole toolchain, and an interpreted cursor per character would show up in every run
        "chars" => Value::list(text.chars().map(Value::Char).collect()),
        "bytes" => Value::list(text.bytes().map(|byte| Value::Int(i64::from(byte))).collect()),
        // The character that begins at a byte offset, and the byte at one: `None` at and past the end, a panic on an
        // offset inside a character (decided gap 7)
        "charAt" => {
            let offset = int_of(&arguments.required(0, "offset")?, "offset")?;
            let found = match usize::try_from(offset) {
                Ok(offset) if offset < text.len() => match text.get(offset..) {
                    Some(rest) => rest.chars().next().map(Value::Char),
                    None => return Err(failure(format!("The offset {offset} is inside of a character"))),
                },
                _ => None,
            };
            Value::from_option(found)
        }
        "byteAt" => {
            let offset = int_of(&arguments.required(0, "offset")?, "offset")?;
            let found = match usize::try_from(offset) {
                Ok(offset) => text.as_bytes().get(offset).map(|byte| Value::Int(i64::from(*byte))),
                Err(_) => None,
            };
            Value::from_option(found)
        }
        "byteLength" => Value::Int(text.len() as i64),
        "isEmpty" => Value::Bool(text.is_empty()),
        "isBlank" => Value::Bool(text.trim().is_empty()),
        "slice" => {
            let Value::Range(range) = arguments.required(0, "range")? else { return Err(failure("`range` must be a Range")) };
            let (from, to) = slice_bounds(range, text.len())?;
            match receiver_text.slice(from, to) {
                Some(slice) => Value::Text(slice),
                None => return Err(failure(format!("The offsets {from}..{to} are inside of a character"))),
            }
        }
        // `slice` is TorbScript over this in `std/text` now: what an open end means is the receiver's decision, so the
        // native takes the two offsets it arrived at. Stage 0 keeps both, the way it keeps its own `chars`
        "sliceBytes" => {
            let from = int_of(&arguments.required(0, "from")?, "from")?;
            let to = int_of(&arguments.required(1, "to")?, "to")?;
            let (from, to) = (usize::try_from(from).unwrap_or(0), usize::try_from(to).unwrap_or(0));
            if to > text.len() || from > to {
                return Err(failure(format!("The offsets {from}..{to} are outside of a text of {} bytes", text.len())));
            }
            match receiver_text.slice(from, to) {
                Some(slice) => Value::Text(slice),
                None => return Err(failure(format!("The offsets {from}..{to} are inside of a character"))),
            }
        }
        "contains" => Value::Bool(text.contains(&part(0, "part")?)),
        "startsWith" => Value::Bool(text.starts_with(&part(0, "prefix")?)),
        "endsWith" => Value::Bool(text.ends_with(&part(0, "suffix")?)),
        "indexOf" => Value::from_option(text.find(&part(0, "part")?).map(|position| Value::Int(position as i64))),
        "substringBefore" => {
            let found = text.find(&part(0, "part")?);
            Value::from_option(found.and_then(|position| receiver_text.slice(0, position)).map(Value::Text))
        }
        "substringAfter" => {
            let part = part(0, "part")?;
            let found = text.find(&part);
            Value::from_option(found.and_then(|position| receiver_text.slice(position + part.len(), text.len())).map(Value::Text))
        }
        "trim" => Value::text(text.trim()),
        "toUpperCase" => Value::text(&text.to_uppercase()),
        "toLowerCase" => Value::text(&text.to_lowercase()),
        "replace" => Value::text(&text.replace(&part(0, "part")?, &part(1, "replacement")?)),
        "split" => Value::list(text.split(&part(0, "separator")?).map(Value::text).collect()),
        "lines" => Value::list(text.split('\n').map(Value::text).collect()),
        "repeat" => Value::text(&text.repeat(position_of(&arguments.required(0, "times")?, "times")?)),
        "add" => Value::text(&format!("{text}{}", interpreter.show(&arguments.required(0, "other")?, true)?)),
        _ => return Ok(None),
    }))
}

fn list_method(interpreter: &mut Interpreter, receiver: &mut Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let Value::List(items) = receiver else { return Ok(None) };
    // Participles: copy, change the copy, return it
    let verb = match name {
        "added" => Some("add"),
        "addedAll" => Some("addAll"),
        "inserted" => Some("insert"),
        "updated" => Some("set"),
        "removedAt" => Some("removeAt"),
        "removed" => Some("remove"),
        "reversed" => Some("reverse"),
        _ => None,
    };
    if let Some(verb) = verb {
        let mut copy = Value::List(items.clone());
        list_method(interpreter, &mut copy, verb, arguments)?;
        return Ok(Some(copy));
    }
    Ok(Some(match name {
        "length" => Value::Int(items.len() as i64),
        "isEmpty" => Value::Bool(items.is_empty()),
        "isNotEmpty" => Value::Bool(!items.is_empty()),
        "get" => {
            let index = int_of(&arguments.required(0, "index")?, "index")?;
            Value::from_option(usize::try_from(index).ok().and_then(|index| items.get(index)).cloned())
        }
        "first" if arguments.positional.is_empty() => Value::from_option(items.first().cloned()),
        "last" => Value::from_option(items.last().cloned()),
        "contains" => Value::Bool(items.contains(&arguments.required(0, "value")?)),
        "indexOf" => {
            let value = arguments.required(0, "value")?;
            Value::from_option(items.iter().position(|item| *item == value).map(|position| Value::Int(position as i64)))
        }
        "slice" => {
            let Value::Range(range) = arguments.required(0, "range")? else { return Err(failure("`range` must be a Range")) };
            let (from, to) = slice_bounds(range, items.len())?;
            Value::list(items[from..to].to_vec())
        }
        "add" => {
            Rc::make_mut(items).push(arguments.required(0, "value")?);
            Value::Void
        }
        "addAll" => {
            Rc::make_mut(items).extend(items_of(&arguments.required(0, "values")?)?);
            Value::Void
        }
        "insert" => {
            let index = position_of(&arguments.required(0, "index")?, "index")?;
            if index > items.len() {
                return Err(failure(format!("Index {index} is out of bounds (the length is {})", items.len())));
            }
            Rc::make_mut(items).insert(index, arguments.required(1, "value")?);
            Value::Void
        }
        "set" => {
            let index = position_of(&arguments.required(0, "index")?, "index")?;
            if index >= items.len() {
                return Err(failure(format!("Index {index} is out of bounds (the length is {})", items.len())));
            }
            Rc::make_mut(items)[index] = arguments.required(1, "value")?;
            Value::Void
        }
        "remove" => {
            let value = arguments.required(0, "value")?;
            match items.iter().position(|item| *item == value) {
                Some(position) => {
                    Rc::make_mut(items).remove(position);
                    Value::Bool(true)
                }
                None => Value::Bool(false),
            }
        }
        "removeAt" => {
            let index = int_of(&arguments.required(0, "index")?, "index")?;
            match usize::try_from(index).ok().filter(|index| *index < items.len()) {
                Some(index) => Value::some(Rc::make_mut(items).remove(index)),
                None => Value::NONE,
            }
        }
        "clear" => {
            Rc::make_mut(items).clear();
            Value::Void
        }
        "compact" => Value::Void,
        "reverse" => {
            Rc::make_mut(items).reverse();
            Value::Void
        }
        "swapAt" => {
            let first = position_of(&arguments.required(0, "first")?, "first")?;
            let second = position_of(&arguments.required(1, "second")?, "second")?;
            if first >= items.len() || second >= items.len() {
                return Err(failure(format!("Index out of bounds (the length is {})", items.len())));
            }
            Rc::make_mut(items).swap(first, second);
            Value::Void
        }
        "sort" => {
            let sorted = sort(interpreter, items.to_vec(), arguments.get(0, "by"))?;
            *items = Rc::new(sorted);
            Value::Void
        }
        _ => return Ok(None),
    }))
}

fn sort(interpreter: &mut Interpreter, items: Vec<Value>, by: Option<Value>) -> Eval<Vec<Value>> {
    let mut keyed = Vec::with_capacity(items.len());
    for item in items {
        let key = match &by {
            Some(by) => interpreter.call_function(by, vec![item.clone()])?,
            None => item.clone(),
        };
        keyed.push((key, item));
    }
    let mut problem = None;
    keyed.sort_by(|(left, _), (right, _)| match interpreter.compare(left, right) {
        Ok(ordering) => ordering,
        Err(flow) => {
            problem.get_or_insert(flow);
            Ordering::Equal
        }
    });
    match problem {
        Some(flow) => Err(flow),
        None => Ok(keyed.into_iter().map(|(_, item)| item).collect()),
    }
}

fn map_method(interpreter: &mut Interpreter, receiver: &mut Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let Value::Map(table) = receiver else { return Ok(None) };
    let verb = match name {
        "added" => Some("add"),
        "addedAll" | "merged" => Some("merge"),
        "updated" => Some("set"),
        "removed" => Some("remove"),
        _ => None,
    };
    if let Some(verb) = verb {
        let mut copy = Value::Map(table.clone());
        map_method(interpreter, &mut copy, verb, arguments)?;
        return Ok(Some(copy));
    }
    Ok(Some(match name {
        "length" => Value::Int(table.len() as i64),
        "isEmpty" => Value::Bool(table.is_empty()),
        "isNotEmpty" => Value::Bool(!table.is_empty()),
        "get" => Value::from_option(table.get(&arguments.required(0, "key")?).cloned()),
        "containsKey" => Value::Bool(table.contains(&arguments.required(0, "key")?)),
        "keys" => Value::list(table.keys().cloned().collect()),
        "values" => Value::list(table.iter().map(|(_, value)| value.clone()).collect()),
        "set" => {
            Rc::make_mut(table).insert(arguments.required(0, "key")?, arguments.required(1, "value")?);
            Value::Void
        }
        "add" => {
            let (key, value) = entry_of(&arguments.required(0, "entry")?)?;
            Rc::make_mut(table).insert(key, value);
            Value::Void
        }
        "addAll" | "merge" => {
            for entry in items_of(&arguments.required(0, "other")?)? {
                let (key, value) = entry_of(&entry)?;
                Rc::make_mut(table).insert(key, value);
            }
            Value::Void
        }
        "remove" => Value::from_option(Rc::make_mut(table).remove(&arguments.required(0, "key")?)),
        "clear" => {
            Rc::make_mut(table).clear();
            Value::Void
        }
        "getOrSet" => {
            let key = arguments.required(0, "key")?;
            if let Some(existing) = table.get(&key) {
                return Ok(Some(existing.clone()));
            }
            let created = interpreter.call_function(&arguments.required(1, "create")?, Vec::new())?;
            Rc::make_mut(table).insert(key, created.clone());
            created
        }
        "mapValues" => {
            let transform = arguments.required(0, "transform")?;
            let mut result = Table::default();
            for (key, value) in table.iter() {
                result.insert(key.clone(), interpreter.call_function(&transform, vec![value.clone()])?);
            }
            Value::Map(Rc::new(result))
        }
        _ => return Ok(None),
    }))
}

fn set_method(receiver: &mut Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let Value::Set(table) = receiver else { return Ok(None) };
    let verb = match name {
        "added" => Some("add"),
        "addedAll" | "union" => Some("addAll"),
        "removed" => Some("remove"),
        "difference" => Some("removeAll"),
        "intersection" => Some("retainAll"),
        _ => None,
    };
    if let Some(verb) = verb {
        let mut copy = Value::Set(table.clone());
        set_method(&mut copy, verb, arguments)?;
        return Ok(Some(copy));
    }
    Ok(Some(match name {
        "length" => Value::Int(table.len() as i64),
        "isEmpty" => Value::Bool(table.is_empty()),
        "isNotEmpty" => Value::Bool(!table.is_empty()),
        "contains" => Value::Bool(table.contains(&arguments.required(0, "value")?)),
        "isSubsetOf" => match arguments.required(0, "other")? {
            Value::Set(other) => Value::Bool(table.keys().all(|key| other.contains(key))),
            _ => return Err(failure("`other` must be a Set")),
        },
        "add" => {
            Rc::make_mut(table).insert(arguments.required(0, "value")?, Value::Void);
            Value::Void
        }
        "addAll" => {
            for item in items_of(&arguments.required(0, "values")?)? {
                Rc::make_mut(table).insert(item, Value::Void);
            }
            Value::Void
        }
        "remove" => Value::Bool(Rc::make_mut(table).remove(&arguments.required(0, "value")?).is_some()),
        "removeAll" => {
            for item in items_of(&arguments.required(0, "values")?)? {
                Rc::make_mut(table).remove(&item);
            }
            Value::Void
        }
        "retainAll" => {
            let keep = set_of(items_of(&arguments.required(0, "values")?)?);
            let Value::Set(keep) = keep else { unreachable!() };
            let removed: Vec<Value> = table.keys().filter(|key| !keep.contains(key)).cloned().collect();
            for item in removed {
                Rc::make_mut(table).remove(&item);
            }
            Value::Void
        }
        "clear" => {
            Rc::make_mut(table).clear();
            Value::Void
        }
        _ => return Ok(None),
    }))
}

/// `Option` and `Result` share their vocabulary.
fn wrapper_method(interpreter: &mut Interpreter, receiver: &Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let (inner, problem): (Option<Value>, Option<Value>) = match receiver {
        Value::Option(option) => (option.as_ref().map(|value| (**value).clone()), None),
        Value::Result(Ok(value)) => (Some((**value).clone()), None),
        Value::Result(Err(error)) => (None, Some((**error).clone())),
        _ => return Ok(None),
    };
    let is_option = matches!(receiver, Value::Option(_));
    let rewrap = |value: Value| if is_option { Value::some(value) } else { Value::ok(value) };
    Ok(Some(match name {
        "isSome" | "isOk" => Value::Bool(inner.is_some()),
        "isNone" | "isError" => Value::Bool(inner.is_none()),
        "map" => match inner {
            Some(inner) => rewrap(interpreter.call_function(&arguments.required(0, "transform")?, vec![inner])?),
            None => receiver.clone(),
        },
        "flatMap" => match inner {
            Some(inner) => interpreter.call_function(&arguments.required(0, "transform")?, vec![inner])?,
            None => receiver.clone(),
        },
        "mapError" => match problem {
            Some(problem) => Value::error(interpreter.call_function(&arguments.required(0, "transform")?, vec![problem])?),
            None => receiver.clone(),
        },
        "filter" => match inner {
            Some(inner) if truth(interpreter, &arguments.required(0, "predicate")?, vec![inner.clone()])? => receiver.clone(),
            _ => Value::NONE,
        },
        "forEach" => {
            if let Some(inner) = inner {
                interpreter.call_function(&arguments.required(0, "action")?, vec![inner])?;
            }
            Value::Void
        }
        "toList" => Value::list(inner.into_iter().collect()),
        "ok" => Value::from_option(inner),
        "orElse" => match inner {
            Some(inner) => inner,
            None => arguments.required(0, "fallback")?,
        },
        "okOr" => match inner {
            Some(inner) => Value::ok(inner),
            None => Value::error(arguments.required(0, "error")?),
        },
        "expect" => match (inner, problem) {
            (Some(inner), _) => inner,
            (None, Some(problem)) => {
                let message = interpreter.show(&arguments.required(0, "message")?, true)?;
                return Err(failure(format!("panic: {message}: {}", interpreter.show(&problem, true)?)));
            }
            (None, None) => return Err(failure(format!("panic: {}", interpreter.show(&arguments.required(0, "message")?, true)?))),
        },
        _ => return Ok(None),
    }))
}

fn object_method(receiver: &Value, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let Value::Object(object) = receiver else { return Ok(None) };
    if name != "copy" {
        return Ok(None);
    }
    let fields = object.info.fields_of(object.case);
    let mut copy = (**object).clone();
    if !arguments.positional.is_empty() {
        return Err(failure("`copy` takes labeled arguments: `copy(x: 1)`"));
    }
    for (label, value) in &arguments.labeled {
        match fields.iter().position(|field| field.name == *label) {
            Some(position) => copy.fields[position] = value.clone(),
            None => return Err(failure(format!("`{}` has no field `{label}`", object.info.name))),
        }
    }
    Ok(Some(Value::Object(Rc::new(copy))))
}

/// Stages and terminal operations of `Iterable`. Stages are eager here.
fn iterable_method(interpreter: &mut Interpreter, items: Vec<Value>, name: &str, arguments: &Arguments) -> Eval<Option<Value>> {
    let function = |label: &str| arguments.required(0, label);
    Ok(Some(match name {
        "toList" => Value::list(items),
        "toSet" => set_of(items),
        "toMap" => map_of(items)?,
        "count" | "length" => Value::Int(items.len() as i64),
        "isEmpty" => Value::Bool(items.is_empty()),
        "isNotEmpty" => Value::Bool(!items.is_empty()),
        "first" => Value::from_option(items.into_iter().next()),
        "contains" => Value::Bool(items.contains(&arguments.required(0, "value")?)),
        "map" => {
            let transform = function("transform")?;
            let mut result = Vec::with_capacity(items.len());
            for item in items {
                result.push(interpreter.call_function(&transform, vec![item])?);
            }
            Value::list(result)
        }
        "filter" => {
            let predicate = function("predicate")?;
            let mut result = Vec::new();
            for item in items {
                if truth(interpreter, &predicate, vec![item.clone()])? {
                    result.push(item);
                }
            }
            Value::list(result)
        }
        "flatMap" => {
            let transform = function("transform")?;
            let mut result = Vec::new();
            for item in items {
                result.extend(items_of(&interpreter.call_function(&transform, vec![item])?)?);
            }
            Value::list(result)
        }
        "filterMap" | "mapWhile" => {
            let transform = function("transform")?;
            let mut result = Vec::new();
            for item in items {
                match interpreter.call_function(&transform, vec![item])? {
                    Value::Option(Some(value)) => result.push((*value).clone()),
                    Value::Option(None) if name == "mapWhile" => break,
                    Value::Option(None) => {}
                    other => {
                        return Err(failure(format!("The closure must return an Option, it returned {}", interpreter.describe(&other))))
                    }
                }
            }
            Value::list(result)
        }
        "take" => {
            let amount = position_of(&arguments.required(0, "amount")?, "amount")?;
            Value::list(items.into_iter().take(amount).collect())
        }
        "skip" => {
            let amount = position_of(&arguments.required(0, "amount")?, "amount")?;
            Value::list(items.into_iter().skip(amount).collect())
        }
        "takeWhile" => {
            let predicate = function("predicate")?;
            let mut result = Vec::new();
            for item in items {
                if !truth(interpreter, &predicate, vec![item.clone()])? {
                    break;
                }
                result.push(item);
            }
            Value::list(result)
        }
        "zip" => {
            let other = items_of(&arguments.required(0, "other")?)?;
            Value::list(items.into_iter().zip(other).map(|(left, right)| Value::tuple(vec![left, right])).collect())
        }
        "indexed" => {
            Value::list(items.into_iter().enumerate().map(|(index, item)| Value::tuple(vec![Value::Int(index as i64), item])).collect())
        }
        "sorted" => Value::list(sort(interpreter, items, arguments.get(0, "by"))?),
        "joined" => {
            let separator = match arguments.get(0, "separator") {
                Some(separator) => text_of(&separator, "separator")?,
                None => String::new(),
            };
            let mut parts = Vec::with_capacity(items.len());
            for item in items {
                parts.push(interpreter.show(&item, true)?);
            }
            Value::text(&parts.join(&separator))
        }
        "forEach" => {
            let action = function("action")?;
            for item in items {
                interpreter.call_function(&action, vec![item])?;
            }
            Value::Void
        }
        "fold" => {
            let mut state = arguments.required(0, "initial")?;
            let combine = arguments.required(1, "combine")?;
            for item in items {
                state = interpreter.call_function(&combine, vec![state, item])?;
            }
            state
        }
        "find" => {
            let predicate = function("predicate")?;
            let mut found = None;
            for item in items {
                if truth(interpreter, &predicate, vec![item.clone()])? {
                    found = Some(item);
                    break;
                }
            }
            Value::from_option(found)
        }
        "any" | "all" => {
            let predicate = function("predicate")?;
            let mut result = name == "all";
            for item in items {
                if truth(interpreter, &predicate, vec![item])? != (name == "all") {
                    result = name != "all";
                    break;
                }
            }
            Value::Bool(result)
        }
        "sum" => {
            let mut total = Value::Int(0);
            for item in items {
                total = match (&total, &item) {
                    (Value::Int(a), Value::Int(b)) => Value::Int(a.checked_add(*b).ok_or_else(|| failure("Integer overflow"))?),
                    (Value::Int(a), Value::Float(b)) => Value::Float(*a as f64 + b),
                    (Value::Float(a), Value::Float(b)) => Value::Float(a + b),
                    (Value::Float(a), Value::Int(b)) => Value::Float(a + *b as f64),
                    _ => return Err(failure("`sum` needs numbers")),
                };
            }
            total
        }
        "groupBy" => {
            let key = function("key")?;
            let mut groups = Table::default();
            for item in items {
                let group = interpreter.call_function(&key, vec![item.clone()])?;
                match groups.get_mut(&group) {
                    Some(Value::List(existing)) => Rc::make_mut(existing).push(item),
                    _ => {
                        groups.insert(group, Value::list(vec![item]));
                    }
                }
            }
            Value::Map(Rc::new(groups))
        }
        _ => return Ok(None),
    }))
}
