//! The compiler in `compiler/` is run by the bootstrap interpreter and compared with the Rust implementation it is
//! a port of. As long as both exist, neither can drift.
//!
//! Every comparison here is one process per file and independent of the others, so they run on as many threads as the
//! machine has cores. What is reported is still the first problem in the order of the files.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

fn collect(directory: &Path, files: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).expect("readable directory") {
        let path = entry.expect("readable entry").path();
        if path.is_dir() {
            // A hidden directory is not the repository's source: `.claude/worktrees` holds whole checkouts that
            // change while this test reads them.
            let hidden = path.file_name().is_some_and(|name| name.to_string_lossy().starts_with('.'));
            if !hidden && !path.ends_with("target") && !path.ends_with("node_modules") {
                collect(&path, files);
            }
        } else if path.extension().is_some_and(|extension| extension == "trb") {
            files.push(path);
        }
    }
}

fn torb(arguments: &[&str]) -> String {
    let output = Command::new(env!("CARGO_BIN_EXE_torb")).args(arguments).output().expect("torb runs");
    assert!(output.status.success(), "torb {arguments:?} failed:\n{}", String::from_utf8_lossy(&output.stderr));
    String::from_utf8(output.stdout).expect("UTF-8 output")
}

/// Does the same work for every item, on as many threads as the machine has cores, and returns what each of them
/// produced in the order of the items. `std::thread::scope` needs nothing beyond the standard library.
fn for_each_in_parallel<Item: Sync, Problem: Send>(
    items: &[Item],
    work: impl Fn(&Item) -> Option<Problem> + Send + Sync,
) -> Vec<Option<Problem>> {
    let next = AtomicUsize::new(0);
    let problems: Vec<Mutex<Option<Problem>>> = items.iter().map(|_| Mutex::new(None)).collect();
    let threads = std::thread::available_parallelism().map_or(2, |count| count.get()).min(items.len().max(1));
    std::thread::scope(|scope| {
        for _ in 0..threads {
            scope.spawn(|| loop {
                let index = next.fetch_add(1, Ordering::Relaxed);
                let Some(item) = items.get(index) else { break };
                *problems[index].lock().expect("no thread fails while it holds the lock") = work(item);
            });
        }
    });
    problems.into_iter().map(|problem| problem.into_inner().expect("no thread fails while it holds the lock")).collect()
}

/// The first problem in the order of the items, so that a failure does not depend on which thread was first.
fn first_problem(problems: Vec<Option<String>>) {
    if let Some(problem) = problems.into_iter().flatten().next() {
        panic!("{problem}");
    }
}

/// Tokens and syntax trees of every `.trb` file in the repository, including the files that are full of errors.
#[test]
fn the_front_end_written_in_torbscript_agrees_with_the_bootstrap_front_end() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let compiler = root.join("compiler");
    let mut files = Vec::new();
    collect(&root, &mut files);
    assert!(files.len() > 50, "expected the TorbScript files of the repository, found {}", files.len());
    files.sort();

    let problems = for_each_in_parallel(&files, |file| {
        let file = file.to_str().expect("UTF-8 path");
        let compiler = compiler.to_str().expect("UTF-8 path");
        if torb(&["tokens", file]) != torb(&["run", compiler, "tokens", file]) {
            return Some(format!("different tokens for {file}"));
        }

        let expected = torb(&["ast", file]);
        let actual = torb(&["run", compiler, "ast", file]);
        if expected == actual {
            return None;
        }
        let position = expected.bytes().zip(actual.bytes()).position(|(left, right)| left != right).unwrap_or(0);
        let context = |text: &str| {
            String::from_utf8_lossy(&text.as_bytes()[position.saturating_sub(200)..(position + 200).min(text.len())]).to_string()
        };
        Some(format!(
            "different syntax trees for {file}
bootstrap: ...{}...
compiler:  ...{}...",
            context(&expected),
            context(&actual)
        ))
    });
    first_problem(problems);
}

/// `torb parse`: the same files, the same problems, rendered the same way.
#[test]
fn both_toolchains_render_diagnostics_the_same_way() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let compiler = root.join("compiler");
    let cases = root.join("bootstrap/tests/parser-cases/errors.trb");
    let expected =
        Command::new(env!("CARGO_BIN_EXE_torb")).args(["parse", cases.to_str().expect("UTF-8 path")]).output().expect("torb runs");
    let actual = Command::new(env!("CARGO_BIN_EXE_torb"))
        .args(["run", compiler.to_str().expect("UTF-8 path"), "parse", cases.to_str().expect("UTF-8 path")])
        .output()
        .expect("torb runs");
    assert!(!expected.status.success() && !actual.status.success(), "both report the problems through their exit code");
    assert!(String::from_utf8_lossy(&expected.stderr) == String::from_utf8_lossy(&actual.stderr));

    // And the compiler finds its own sources, the standard library and the examples free of problems
    let sources = [compiler.clone(), root.join("std"), root.join("examples")];
    let mut arguments = vec!["run", compiler.to_str().expect("UTF-8 path"), "parse"];
    arguments.extend(sources.iter().map(|path| path.to_str().expect("UTF-8 path")));
    assert!(torb(&arguments).ends_with(
        "files, no problems
"
    ));
}

/// `torb check` over the repository: the module graph, the symbols, every name in a type position and the type every
/// one of those positions stands for, in `std/`, `compiler/` and `examples/`. The standard library and the examples
/// are the conformance suite, so this has to stay free of problems. That every type position really becomes a type
/// is asserted in `compiler/tests/types.test.trb`, which checks the same repository in process.
#[test]
fn the_compiler_resolves_the_names_of_the_whole_repository() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let compiler = root.join("compiler");
    let output = torb(&["run", compiler.to_str().expect("UTF-8 path"), "check", root.to_str().expect("UTF-8 path")]);
    assert!(output.ends_with("files, no problems\n"), "{output}");
    let checked: usize = output.split(' ').next().unwrap_or_default().parse().unwrap_or_default();
    assert!(checked > 50, "expected every file of the workspace to be checked, checked {checked}");
}

/// `bootstrap/tests/scripts/*.trb` with their expected output: the behavior of the interpreter itself.
#[test]
fn scripts_print_what_they_should() {
    let scripts = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/scripts");
    let mut files = Vec::new();
    collect(&scripts, &mut files);
    assert!(!files.is_empty());
    files.sort();
    let problems = for_each_in_parallel(&files, |file| {
        let expected = std::fs::read_to_string(file.with_extension("expected")).expect("an .expected file next to the script");
        let actual = torb(&["run", file.to_str().expect("UTF-8 path")]);
        (expected.replace("\r\n", "\n") != actual.replace("\r\n", "\n"))
            .then(|| format!("unexpected output of {}:\n{actual}", file.display()))
    });
    first_problem(problems);
}

/// Stage 0 has no checker, so an uppercase pattern name - which the parser made a case, by its first letter - is looked
/// up when the pattern runs. One that is nowhere is a loud failure instead of an arm that quietly matches nothing.
#[test]
fn an_uppercase_pattern_name_that_is_no_case_fails_loudly() {
    let script = std::env::temp_dir().join(format!("torb-unknown-case-{}.trb", std::process::id()));
    std::fs::write(&script, "const found = match 1 {\n  Nome => 1\n  _ => 2\n}\nprint found\n").expect("a writable scratch file");
    let output = Command::new(env!("CARGO_BIN_EXE_torb")).args(["run", script.to_str().expect("UTF-8 path")]).output().expect("torb runs");
    let _ = std::fs::remove_file(&script);
    assert!(!output.status.success(), "expected a failure, printed {}", String::from_utf8_lossy(&output.stdout));
    let problem = String::from_utf8_lossy(&output.stderr);
    assert!(problem.contains("`Nome` is not a case in scope"), "{problem}");
}

#[test]
fn the_tests_of_the_compiler_pass() {
    let tests = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../compiler/tests");
    let output = torb(&["test", tests.to_str().expect("UTF-8 path")]);
    assert!(output.contains(" passed, 0 failed"), "{output}");
}
