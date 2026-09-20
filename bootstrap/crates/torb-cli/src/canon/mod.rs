//! `torb canon`: rewrites TorbScript sources into the canon of the formatter, over the syntax tree and never with a
//! regular expression.
//!
//! This is tooling for the bootstrap phase. Milestone 8's `torb format`, written in TorbScript, enforces the same canon
//! and replaces this command; until then this is how the canon is applied to a whole checkout. It stays small on
//! purpose: it only moves parentheses and indentation, and it never reprints a file.
//!
//! ```text
//! torb canon [--check] [--rule calls|strings|imported-case-patterns]... <path>...
//! ```
//!
//! `calls` and `strings` run by default; `--rule` picks a set instead. `--check` writes nothing and leaves with a
//! non-zero code if something would change. The command is idempotent: a second run over the same tree changes
//! nothing.
//!
//! **The safety net.** Every edit is applied on its own and the file is parsed again. The edit only stays if the
//! syntax tree is the one from before with every span and every `CallStyle` erased (`canonical`), and if the file still
//! has no diagnostics; otherwise it is dropped and reported. A file that does not parse to begin with is skipped
//! whole - `bootstrap/tests/parser-cases/` and `bootstrap/tests/lexer-cases/` hold files that are broken on purpose
//! and are not even read. Nothing outside of an edited byte range is touched, so line endings and everything else stay
//! byte-identical.

mod bindings;
mod calls;
mod edit;
mod patterns;
mod strings;
mod walk;

#[cfg(test)]
mod tests;

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use edit::{Document, EditKind};

pub const USAGE: &str = "\
torb canon - write TorbScript sources in the canon of the formatter

Usage:
  torb canon [--check] [--rule <rule>]... <path>...

Rules (`calls` and `strings` by default):
  calls                    A call is a command where the grammar allows it, and has parentheses where it does not
  strings                  A multi-line `\"\"\"` string is indented one level deeper than the line it starts on
  imported-case-patterns   `.None` becomes `None` for a case a `use` imported (changes the tree, off by default)
  unused-bindings          A binding of a refutable pattern that nobody reads becomes `_` (changes the tree, off by default)

  --check   Write nothing, list what would change, leave with a non-zero code
";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Rule {
    Calls,
    Strings,
    ImportedCasePatterns,
    UnusedBindings,
}

impl Rule {
    fn from_text(text: &str) -> Option<Rule> {
        match text {
            "calls" => Some(Rule::Calls),
            "strings" => Some(Rule::Strings),
            "imported-case-patterns" => Some(Rule::ImportedCasePatterns),
            "unused-bindings" => Some(Rule::UnusedBindings),
            _ => None,
        }
    }
}

struct Options {
    check: bool,
    rules: Vec<Rule>,
    paths: Vec<String>,
}

fn options(arguments: &[String]) -> Option<Options> {
    let mut check = false;
    let mut rules = Vec::new();
    let mut paths = Vec::new();
    let mut rest = arguments.iter();
    while let Some(argument) = rest.next() {
        if argument == "--check" {
            check = true;
        } else if let Some(name) = argument.strip_prefix("--rule=") {
            rules.push(Rule::from_text(name)?);
        } else if argument == "--rule" {
            rules.push(Rule::from_text(rest.next()?)?);
        } else if argument.starts_with('-') {
            return None;
        } else {
            paths.push(argument.clone());
        }
    }
    if paths.is_empty() {
        return None;
    }
    if rules.is_empty() {
        rules = vec![Rule::Calls, Rule::Strings];
    }
    Some(Options { check, rules, paths })
}

pub fn canon(arguments: &[String]) -> ExitCode {
    let Some(options) = options(arguments) else {
        eprint!("{USAGE}");
        return ExitCode::from(2);
    };
    let mut files = Vec::new();
    for path in &options.paths {
        if let Err(error) = collect_files(Path::new(path), &mut files) {
            eprintln!("error: {path}: {error}");
            return ExitCode::from(2);
        }
    }
    files.sort();

    let mut report = Report::default();
    for file in &files {
        let source = match std::fs::read_to_string(file) {
            Ok(source) => source,
            Err(error) => {
                eprintln!("error: {}: {error}", file.display());
                return ExitCode::from(2);
            }
        };
        report.files += 1;
        let outcome = rewrite(&source, &options.rules);
        for reason in outcome.skipped {
            report.skipped.push(format!("{}: {reason}", file.display()));
        }
        for reason in outcome.dropped {
            report.dropped.push(format!("{}: {reason}", file.display()));
        }
        for (kind, count) in outcome.counts {
            report.count(kind, count);
        }
        if outcome.text == source {
            continue;
        }
        report.changed.push(file.clone());
        if options.check {
            continue;
        }
        if let Err(error) = std::fs::write(file, &outcome.text) {
            eprintln!("error: {}: {error}", file.display());
            return ExitCode::from(2);
        }
    }
    report.print(options.check);
    if options.check && !report.changed.is_empty() {
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}

#[derive(Default)]
struct Report {
    files: usize,
    changed: Vec<PathBuf>,
    to_command: usize,
    to_parentheses: usize,
    strings: usize,
    patterns: usize,
    bindings: usize,
    dropped: Vec<String>,
    skipped: Vec<String>,
}

impl Report {
    fn count(&mut self, kind: EditKind, count: usize) {
        match kind {
            EditKind::ToCommand => self.to_command += count,
            EditKind::ToParentheses => self.to_parentheses += count,
            EditKind::IndentedString => self.strings += count,
            EditKind::CasePattern => self.patterns += count,
            EditKind::UnusedBinding => self.bindings += count,
        }
    }

    fn print(&self, check: bool) {
        for file in &self.changed {
            println!("{}", file.display());
        }
        for reason in &self.skipped {
            println!("skipped {reason}");
        }
        for reason in &self.dropped {
            println!("dropped {reason}");
        }
        let verb = if check { "would change" } else { "changed" };
        println!(
            "\n{} of {} files {verb}: {} calls became commands, {} got parentheses, {} strings were indented, {} case patterns, {} unread bindings",
            self.changed.len(),
            self.files,
            self.to_command,
            self.to_parentheses,
            self.strings,
            self.patterns,
            self.bindings,
        );
        if !self.dropped.is_empty() {
            println!("{} edits were dropped by the safety net", self.dropped.len());
        }
    }
}

#[derive(Default)]
struct Outcome {
    text: String,
    counts: Vec<(EditKind, usize)>,
    /// Why a whole file, or one string inside of it, was left alone
    skipped: Vec<String>,
    /// An edit the safety net threw away, with what it would have changed
    dropped: Vec<String>,
}

/// The canon of one source text. Public for the tests; every rule is here, so a test can ask for one of them alone.
fn rewrite(source: &str, rules: &[Rule]) -> Outcome {
    let mut outcome = Outcome { text: source.to_string(), ..Outcome::default() };
    let Some(baseline) = canonical(source) else {
        outcome.skipped.push("it does not parse".to_string());
        return outcome;
    };
    let lexed = torb_syntax::lexer::lex(source);
    let parsed = torb_syntax::parse(source);
    let sites = walk::walk(&parsed.file);

    let mut edits = Vec::new();
    if rules.contains(&Rule::Calls) {
        edits.extend(calls::edits(source, &lexed.tokens, &sites.calls));
    }
    if rules.contains(&Rule::Strings) {
        let (found, skipped) = strings::edits(source, &lexed.tokens);
        edits.extend(found);
        let lines = torb_syntax::LineIndex::new(source);
        for skip in skipped {
            let (line, column) = lines.line_and_column(skip.offset);
            outcome.skipped.push(format!("{line}:{column}: {}", skip.reason));
        }
    }
    edits.sort_by_key(|edit| edit.position);
    // The rule that changes the tree comes last, so that it starts from a file the safety net has settled
    if rules.contains(&Rule::ImportedCasePatterns) {
        edits.extend(patterns::edits(source, &parsed.file, &sites.patterns));
    }
    if rules.contains(&Rule::UnusedBindings) {
        edits.extend(bindings::edits(source, &sites.bindings));
    }

    let mut document = Document::new(source);
    let mut expected = baseline;
    let mut counts = std::collections::BTreeMap::new();
    for edit in &edits {
        let candidate = document.with(edit);
        let after = canonical(&candidate);
        let stays = match edit.kind.preserves_the_tree() {
            true => after.as_deref() == Some(expected.as_str()),
            false => after.is_some(),
        };
        if !stays {
            let lines = torb_syntax::LineIndex::new(source);
            let (line, column) = lines.line_and_column(edit.position);
            outcome.dropped.push(format!("{line}:{column}: {:?} would change more than the way it is written", edit.kind));
            continue;
        }
        if !edit.kind.preserves_the_tree() {
            expected = after.expect("just checked that it parses");
        }
        document.accept(edit, candidate);
        *counts.entry(edit.kind).or_insert(0) += 1;
    }
    outcome.counts = counts.into_iter().collect();
    outcome.text = document.into_text();
    outcome
}

/// The syntax tree of a source as text, with every span and every `CallStyle` erased: what an edit of the canon must
/// not change. `None` if the source does not parse cleanly.
///
/// It works on the output of `torb_syntax::dump`, the same text the two front ends are compared with, because that
/// prints every field of every node - a tree that is equal here is equal everywhere except in the two places the canon
/// is allowed to touch.
fn canonical(source: &str) -> Option<String> {
    let parsed = torb_syntax::parse(source);
    if !parsed.diagnostics.is_empty() {
        return None;
    }
    Some(without_spans_and_styles(&torb_syntax::dump::dump(&parsed.file)))
}

fn without_spans_and_styles(dump: &str) -> String {
    let mut result = String::with_capacity(dump.len());
    let mut rest = dump;
    while let Some(index) = rest.find(['S', 's']) {
        let (before, at) = rest.split_at(index);
        result.push_str(before);
        if let Some(after) = at.strip_prefix("Span(start: ") {
            let end = after.find(')').map_or(after.len(), |index| index + 1);
            result.push_str("Span");
            rest = &after[end..];
        } else if let Some(after) = at.strip_prefix("style: Command") {
            result.push_str("style: *");
            rest = after;
        } else if let Some(after) = at.strip_prefix("style: Parentheses") {
            result.push_str("style: *");
            rest = after;
        } else {
            result.push_str(&at[..1]);
            rest = &at[1..];
        }
    }
    result.push_str(rest);
    result
}

/// Every `.trb` file below the paths, without the hidden directories, `target`, `node_modules` and the two directories
/// of files that are broken on purpose.
fn collect_files(path: &Path, files: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if path.is_dir() {
        for entry in std::fs::read_dir(path)? {
            let entry = entry?.path();
            let name = entry.file_name().map(|name| name.to_string_lossy().to_string()).unwrap_or_default();
            if entry.is_dir() {
                if !name.starts_with('.') && !matches!(name.as_str(), "target" | "node_modules" | "parser-cases" | "lexer-cases") {
                    collect_files(&entry, files)?;
                }
            } else if entry.extension().is_some_and(|extension| extension == "trb") {
                files.push(entry);
            }
        }
        return Ok(());
    }
    std::fs::metadata(path)?;
    files.push(path.to_path_buf());
    Ok(())
}
