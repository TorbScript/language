//! The `torb` command line tool.

mod canon;
mod highlight;

use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

use torb_syntax::{Diagnostic, LineIndex};

const USAGE: &str = "\
torb - the TorbScript toolchain

Usage:
  torb run <file> [arguments]   Run a file (or `src/main.trb` of a project directory)
  torb test [path] [--jobs N]   Run every `*.test.trb` below the path (default: tests), N files at a
                                time in one process each (default: as many as the machine has cores)
  torb parse <path>...   Check the syntax of files or directories (recursively, *.trb)
  torb tokens <file>     Print the tokens of a file
  torb ast <file>        Print the syntax tree of a file
  torb canon <path>...   Write sources in the canon of the formatter (`torb canon --help`)
  torb highlight <file>  Print semantic tokens as JSON, for editors (`torb highlight --stdin` reads standard input)
  torb help              Show this text
";

fn main() -> ExitCode {
    // The interpreter walks the syntax tree recursively: give it room
    let thread = std::thread::Builder::new().stack_size(1 << 30).spawn(dispatch).expect("a thread for the interpreter");
    thread.join().unwrap_or(ExitCode::from(101))
}

fn dispatch() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    match arguments.split_first() {
        Some((command, rest)) if command == "run" && !rest.is_empty() => run(&rest[0], rest[1..].to_vec()),
        Some((command, rest)) if command == "test" && test_arguments(rest).is_some() => {
            let (path, jobs) = test_arguments(rest).expect("just checked");
            test(&path, jobs)
        }
        Some((command, paths)) if command == "parse" && !paths.is_empty() => parse(paths),
        Some((command, paths)) if command == "tokens" && paths.len() == 1 => tokens(&paths[0]),
        Some((command, paths)) if command == "ast" && paths.len() == 1 => ast(&paths[0]),
        Some((command, rest)) if command == "highlight" && rest == ["--stdin"] => highlight::highlight(None),
        Some((command, paths)) if command == "highlight" && paths.len() == 1 => highlight::highlight(Some(&paths[0])),
        Some((command, rest)) if command == "canon" && matches!(rest.first().map(String::as_str), Some("--help" | "help")) => {
            print!("{}", canon::USAGE);
            ExitCode::SUCCESS
        }
        Some((command, rest)) if command == "canon" => canon::canon(rest),
        Some((command, _)) if command == "help" || command == "--help" => {
            print!("{USAGE}");
            ExitCode::SUCCESS
        }
        _ => {
            eprint!("{USAGE}");
            ExitCode::from(2)
        }
    }
}

fn run(path: &str, arguments: Vec<String>) -> ExitCode {
    let mut entry = PathBuf::from(path);
    if entry.is_dir() {
        entry = entry.join("src/main.trb");
    }
    match torb_interpreter::run(&entry, arguments) {
        torb_interpreter::Outcome::Finished(_) => ExitCode::SUCCESS,
        outcome => report(outcome),
    }
}

/// `torb test [path] [--jobs N]`. `None` if the arguments are not that.
fn test_arguments(arguments: &[String]) -> Option<(String, usize)> {
    let mut path = None;
    let mut jobs = None;
    let mut rest = arguments.iter();
    while let Some(argument) = rest.next() {
        if let Some(count) = argument.strip_prefix("--jobs=") {
            jobs = Some(count.parse::<usize>().ok()?);
        } else if argument == "--jobs" {
            jobs = Some(rest.next()?.parse::<usize>().ok()?);
        } else if argument.starts_with('-') || path.is_some() {
            return None;
        } else {
            path = Some(argument.clone());
        }
    }
    Some((path.unwrap_or_else(|| "tests".to_string()), jobs.unwrap_or_else(cores).max(1)))
}

fn cores() -> usize {
    std::thread::available_parallelism().map_or(1, |count| count.get())
}

fn test(path: &str, jobs: usize) -> ExitCode {
    let mut files = Vec::new();
    if let Err(error) = collect_files(Path::new(path), &mut files) {
        eprintln!("error: {path}: {error}");
        return ExitCode::from(2);
    }
    files.retain(|file| file.to_string_lossy().ends_with(".test.trb"));
    files.sort();
    let (passed, failed) = if jobs > 1 && files.len() > 1 { test_in_parallel(&files, jobs) } else { test_one_after_another(&files) };
    println!(
        "
{passed} passed, {failed} failed ({} files)",
        files.len()
    );
    if failed == 0 {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

fn test_one_after_another(files: &[PathBuf]) -> (usize, usize) {
    let (mut passed, mut failed) = (0, 0);
    for file in files {
        println!("{}", file.display());
        match torb_interpreter::run(file, Vec::new()) {
            torb_interpreter::Outcome::Finished(tests) => {
                passed += tests.passed;
                failed += tests.failed;
            }
            outcome => {
                report(outcome);
                failed += 1;
            }
        }
    }
    (passed, failed)
}

/// One process per test file, `jobs` of them at a time. The interpreter is built on `Rc`, so a process is what an
/// independent run is; what they printed is passed on in the order of the files, so the output does not depend on
/// which of them finished first.
fn test_in_parallel(files: &[PathBuf], jobs: usize) -> (usize, usize) {
    let executable = match std::env::current_exe() {
        Ok(executable) => executable,
        Err(error) => {
            eprintln!("error: cannot find the running `torb` to run the files in parallel: {error}");
            return test_one_after_another(files);
        }
    };
    let next = AtomicUsize::new(0);
    let outputs: Vec<Mutex<Option<std::process::Output>>> = files.iter().map(|_| Mutex::new(None)).collect();
    std::thread::scope(|scope| {
        for _ in 0..jobs.min(files.len()) {
            scope.spawn(|| loop {
                let index = next.fetch_add(1, Ordering::Relaxed);
                let Some(file) = files.get(index) else { break };
                let output = Command::new(&executable).arg("test").arg(file).args(["--jobs", "1"]).output();
                *outputs[index].lock().expect("no thread fails while it holds the lock") = output.ok();
            });
        }
    });
    let (mut passed, mut failed) = (0, 0);
    for (file, output) in files.iter().zip(outputs) {
        let output = output.into_inner().expect("no thread fails while it holds the lock");
        let Some(output) = output.filter(|output| counts_of(&String::from_utf8_lossy(&output.stdout)).is_some()) else {
            println!("{}", file.display());
            eprintln!("error: {}: the process that runs this file did not report its tests", file.display());
            failed += 1;
            continue;
        };
        let text = String::from_utf8_lossy(&output.stdout).to_string();
        let (body, file_passed, file_failed) = counts_of(&text).expect("just checked");
        println!("{body}");
        eprint!("{}", String::from_utf8_lossy(&output.stderr));
        passed += file_passed;
        failed += file_failed;
    }
    (passed, failed)
}

/// Splits what one test process printed into its lines and the counts of the summary it ends with.
fn counts_of(text: &str) -> Option<(&str, usize, usize)> {
    let (body, summary) = text.rsplit_once("\n\n")?;
    let mut words = summary.split_whitespace();
    let passed = words.next()?.parse().ok()?;
    if words.next()? != "passed," {
        return None;
    }
    let failed = words.next()?.parse().ok()?;
    Some((body, passed, failed))
}

fn report(outcome: torb_interpreter::Outcome) -> ExitCode {
    match outcome {
        torb_interpreter::Outcome::Finished(_) => ExitCode::SUCCESS,
        torb_interpreter::Outcome::NotLoaded(problems) => {
            for problem in &problems {
                eprintln!("error: {problem}");
            }
            ExitCode::from(2)
        }
        torb_interpreter::Outcome::Failed(failure) => {
            eprintln!("error: {}", failure.message);
            if let Some(location) = &failure.location {
                eprintln!("  at {location}");
            }
            for entry in &failure.trace {
                eprintln!("  {entry}");
            }
            ExitCode::FAILURE
        }
    }
}

fn parse(paths: &[String]) -> ExitCode {
    let mut files = Vec::new();
    for path in paths {
        if let Err(error) = collect_files(Path::new(path), &mut files) {
            eprintln!("error: {path}: {error}");
            return ExitCode::from(2);
        }
    }
    files.sort();

    let mut problems = 0;
    let mut files_with_problems = 0;
    for file in &files {
        let source = match std::fs::read_to_string(file) {
            Ok(source) => source,
            Err(error) => {
                eprintln!("error: {}: {error}", file.display());
                return ExitCode::from(2);
            }
        };
        let parsed = torb_syntax::parse(&source);
        if parsed.diagnostics.is_empty() {
            continue;
        }
        files_with_problems += 1;
        problems += parsed.diagnostics.len();
        let lines = LineIndex::new(&source);
        for diagnostic in &parsed.diagnostics {
            eprintln!("{}", render(file, &lines, diagnostic));
        }
    }

    if problems == 0 {
        println!("{} files, no problems", files.len());
        return ExitCode::SUCCESS;
    }
    eprintln!("{problems} problems in {files_with_problems} of {} files", files.len());
    ExitCode::FAILURE
}

fn tokens(path: &str) -> ExitCode {
    let source = match std::fs::read_to_string(path) {
        Ok(source) => source,
        Err(error) => {
            eprintln!("error: {path}: {error}");
            return ExitCode::from(2);
        }
    };
    // The same format as `compiler/src/syntax/dump.trb`: the two lexers are tested against each other
    let lexed = torb_syntax::lexer::lex(&source);
    for token in &lexed.tokens {
        println!("{}..{} {}", token.span.start, token.span.end, dump_kind(&token.kind));
    }
    for diagnostic in &lexed.diagnostics {
        println!("error {}..{} {}", diagnostic.span.start, diagnostic.span.end, diagnostic.message);
    }
    ExitCode::SUCCESS
}

/// The same format as `print parsed.file` in the compiler: the two parsers are tested against each other.
fn ast(path: &str) -> ExitCode {
    let source = match std::fs::read_to_string(path) {
        Ok(source) => source,
        Err(error) => {
            eprintln!("error: {path}: {error}");
            return ExitCode::from(2);
        }
    };
    let parsed = torb_syntax::parse(&source);
    println!("{}", torb_syntax::dump::dump(&parsed.file));
    for diagnostic in &parsed.diagnostics {
        println!("error {}..{} {}", diagnostic.span.start, diagnostic.span.end, diagnostic.message);
    }
    ExitCode::SUCCESS
}

fn dump_kind(kind: &torb_syntax::token::TokenKind) -> String {
    use torb_syntax::token::{TextPart, TokenKind};
    let escape = |text: &str| text.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n").replace('\r', "\\r");
    match kind {
        TokenKind::Keyword(keyword) => format!("Reserved({keyword:?})"),
        TokenKind::Integer => "IntegerLiteral".to_string(),
        TokenKind::Float => "FloatLiteral".to_string(),
        TokenKind::Char(value) => format!("CharLiteral({})", u32::from(*value)),
        TokenKind::Text(parts) => {
            let parts: Vec<String> = parts
                .iter()
                .map(|part| match part {
                    TextPart::Literal(text) => format!("Literal(\"{}\")", escape(text)),
                    TextPart::Expression(span) => format!("Expression({}..{})", span.start, span.end),
                })
                .collect();
            format!("TextLiteral[{}]", parts.join(", "))
        }
        other => format!("{other:?}"),
    }
}

fn collect_files(path: &Path, files: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if path.is_dir() {
        for entry in std::fs::read_dir(path)? {
            let entry = entry?.path();
            let is_hidden = entry.file_name().is_some_and(|name| name.to_string_lossy().starts_with('.'));
            if entry.is_dir() && !is_hidden && !entry.ends_with("target") && !entry.ends_with("node_modules") {
                collect_files(&entry, files)?;
            } else if entry.extension().is_some_and(|extension| extension == "trb") {
                files.push(entry);
            }
        }
        return Ok(());
    }
    // Reports a missing file with the message of the operating system
    std::fs::metadata(path)?;
    files.push(path.to_path_buf());
    Ok(())
}

fn render(file: &Path, lines: &LineIndex, diagnostic: &Diagnostic) -> String {
    let (line, column) = lines.line_and_column(diagnostic.span.start);
    let (end_line, end_column) = lines.line_and_column(diagnostic.span.end);
    let text = lines.line_text(line);
    let width = if end_line == line { (end_column - column).max(1) } else { text.chars().count().saturating_sub(column - 1).max(1) };
    let gutter = " ".repeat(line.to_string().len());
    let mut output = format!(
        "error: {}\n{gutter}--> {}:{line}:{column}\n{gutter} |\n{line} | {text}\n{gutter} | {}{}\n",
        diagnostic.message,
        file.display(),
        " ".repeat(column - 1),
        "^".repeat(width),
    );
    for note in &diagnostic.notes {
        output.push_str(&format!("{gutter} = {note}\n"));
    }
    output
}
