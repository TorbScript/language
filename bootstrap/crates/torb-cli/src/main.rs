//! The `torb` command line tool.

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use torb_syntax::{Diagnostic, LineIndex};

const USAGE: &str = "\
torb - the TorbScript toolchain

Usage:
  torb run <file> [arguments]   Run a file (or `src/main.trb` of a project directory)
  torb test [path]       Run every `*.test.trb` below the path (default: tests)
  torb parse <path>...   Check the syntax of files or directories (recursively, *.trb)
  torb tokens <file>     Print the tokens of a file
  torb ast <file>        Print the syntax tree of a file
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
        Some((command, rest)) if command == "test" && rest.len() <= 1 => test(rest.first().map_or("tests", String::as_str)),
        Some((command, paths)) if command == "parse" && !paths.is_empty() => parse(paths),
        Some((command, paths)) if command == "tokens" && paths.len() == 1 => tokens(&paths[0]),
        Some((command, paths)) if command == "ast" && paths.len() == 1 => ast(&paths[0]),
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

fn test(path: &str) -> ExitCode {
    let mut files = Vec::new();
    if let Err(error) = collect_files(Path::new(path), &mut files) {
        eprintln!("error: {path}: {error}");
        return ExitCode::from(2);
    }
    files.retain(|file| file.to_string_lossy().ends_with(".test.trb"));
    files.sort();
    let (mut passed, mut failed) = (0, 0);
    for file in &files {
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
