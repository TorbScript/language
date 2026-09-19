//! The compiler in `compiler/` is run by the bootstrap interpreter and compared with the Rust implementation it is
//! a port of. As long as both exist, neither can drift.

use std::path::{Path, PathBuf};
use std::process::Command;

fn collect(directory: &Path, files: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).expect("readable directory") {
        let path = entry.expect("readable entry").path();
        if path.is_dir() {
            if !path.ends_with("target") && !path.ends_with("node_modules") {
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

/// Tokens and syntax trees of every `.trb` file in the repository, including the files that are full of errors.
#[test]
fn the_front_end_written_in_torbscript_agrees_with_the_bootstrap_front_end() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let compiler = root.join("compiler");
    let mut files = Vec::new();
    collect(&root, &mut files);
    assert!(files.len() > 50, "expected the TorbScript files of the repository, found {}", files.len());

    for file in &files {
        let file = file.to_str().expect("UTF-8 path");
        let compiler = compiler.to_str().expect("UTF-8 path");
        assert!(torb(&["tokens", file]) == torb(&["run", compiler, "tokens", file]), "different tokens for {file}");

        let expected = torb(&["ast", file]);
        let actual = torb(&["run", compiler, "ast", file]);
        if expected != actual {
            let position = expected.bytes().zip(actual.bytes()).position(|(left, right)| left != right).unwrap_or(0);
            let context = |text: &str| {
                String::from_utf8_lossy(&text.as_bytes()[position.saturating_sub(200)..(position + 200).min(text.len())]).to_string()
            };
            panic!(
                "different syntax trees for {file}
bootstrap: ...{}...
compiler:  ...{}...",
                context(&expected),
                context(&actual)
            );
        }
    }
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
    for file in &files {
        let expected = std::fs::read_to_string(file.with_extension("expected")).expect("an .expected file next to the script");
        let actual = torb(&["run", file.to_str().expect("UTF-8 path")]);
        assert!(expected.replace("\r\n", "\n") == actual.replace("\r\n", "\n"), "unexpected output of {}:\n{actual}", file.display());
    }
}

#[test]
fn the_tests_of_the_compiler_pass() {
    let tests = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../compiler/tests");
    let output = torb(&["test", tests.to_str().expect("UTF-8 path")]);
    assert!(output.contains(" passed, 0 failed"), "{output}");
}
