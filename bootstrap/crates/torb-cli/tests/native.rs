//! The end to end test of the C back end: every program in `bootstrap/tests/native/` is compiled to a native binary,
//! run, and compared with the same program on stage 0.
//!
//! This is the seed of the conformance runner of milestone 5.14. What it compares is what a program can be observed
//! doing: its standard output, its exit code and - where a `.stderr` file says so - the panic it prints.
//!
//! Stage 0 and the compiled binary do not agree about a **panic** yet: the interpreter prints `error: <message>` with
//! an absolute path and leaves with exit code 1, while the language says `panic: <message>` with a path relative to the
//! workspace root and exit code 101 (decided gap 9); the messages of the checked arithmetic differ as well
//! (`Integer overflow` against ``arithmetic overflow in `*```). So for a program that panics, stage 0 only has to fail,
//! and what it prints is compared against the binary's `.stderr` alone. Unifying the two is 5.14's, and it is the one
//! difference this test is allowed to know about.

use std::path::{Path, PathBuf};
use std::process::Command;

fn repository() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..").canonicalize().expect("the repository root")
}

fn torb(arguments: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_torb")).args(arguments).output().expect("torb runs")
}

/// The same order `torb build` uses. Without one of them there is nothing to compile with, and the test says so
/// instead of failing.
fn c_compiler() -> Option<String> {
    if let Ok(given) = std::env::var("TORB_CC") {
        if !given.is_empty() {
            return Some(given);
        }
    }
    for candidate in ["clang", "gcc", "cc"] {
        if Command::new(candidate).arg("--version").output().is_ok_and(|output| output.status.success()) {
            return Some(candidate.to_string());
        }
    }
    None
}

fn programs(directory: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    for entry in std::fs::read_dir(directory).expect("readable directory") {
        let path = entry.expect("readable entry").path();
        if path.extension().is_some_and(|extension| extension == "trb") && path.file_name().is_some_and(|name| name != "project.trb") {
            found.push(path);
        }
    }
    found.sort();
    found
}

fn expected_file(program: &Path, extension: &str) -> Option<String> {
    let path = program.with_extension(extension);
    std::fs::read_to_string(path).ok().map(|text| text.replace("\r\n", "\n"))
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).replace("\r\n", "\n")
}

/// The binary a build produced. A C compiler on Windows appends `.exe` to an output name without an extension.
fn binary_of(path: &Path) -> PathBuf {
    if path.exists() {
        return path.to_path_buf();
    }
    let mut name = path.as_os_str().to_os_string();
    name.push(".exe");
    PathBuf::from(name)
}

#[test]
fn every_native_program_behaves_like_it_does_on_stage_0() {
    let root = repository();
    let compiler = match c_compiler() {
        Some(found) => found,
        None => {
            eprintln!("skipped: no C compiler found (tried $TORB_CC, clang, gcc, cc). The C back end needs one.");
            return;
        }
    };
    eprintln!("compiling with {compiler}");

    let directory = root.join("bootstrap/tests/native");
    let files = programs(&directory);
    assert!(!files.is_empty(), "expected the programs of the native test suite");
    let output = std::env::temp_dir().join("torb-native-tests");
    let twice = std::env::temp_dir().join("torb-native-tests-again");
    let compiler_path = root.join("compiler");
    let compiler_path = compiler_path.to_str().expect("UTF-8 path");

    for file in &files {
        let name = file.file_stem().expect("a file name").to_str().expect("UTF-8 name").to_string();
        let program = file.to_str().expect("UTF-8 path");
        let target = output.join(&name);
        let target_text = target.to_str().expect("UTF-8 path").to_string();

        // The binary, through the whole driver: check, lower, emit, find a C compiler, compile
        let built = torb(&["run", compiler_path, "build", program, "--output", &target_text]);
        assert!(built.status.success(), "torb build {name} failed:\n{}\n{}", text(&built.stdout), text(&built.stderr));

        let generated = output.join("program.c");
        let source = std::fs::read_to_string(&generated).expect("the generated C");
        assert!(
            !source.contains(root.to_str().expect("UTF-8 path")) && !source.contains(":\\") && !source.contains(":/"),
            "the generated C of {name} contains an absolute path"
        );

        // `--emit-c` is a pure function of the program: twice is byte identical
        let again = twice.join(&name);
        let again_text = again.to_str().expect("UTF-8 path").to_string();
        let emitted = torb(&["run", compiler_path, "build", program, "--emit-c", "--output", &again_text]);
        assert!(emitted.status.success(), "torb build --emit-c {name} failed:\n{}", text(&emitted.stderr));
        let second = std::fs::read_to_string(twice.join("program.c")).expect("the generated C");
        assert!(second == source, "the C of {name} is not the same the second time");

        let binary = binary_of(&target);
        let native = Command::new(&binary).output().expect("the compiled program runs");
        let code = native.status.code().expect("an exit code");

        if let Some(expected) = expected_file(file, "expected") {
            assert!(text(&native.stdout) == expected, "unexpected output of the binary of {name}:\n{}", text(&native.stdout));
        }
        if let Some(expected) = expected_file(file, "exit") {
            assert!(code.to_string() == expected.trim(), "the binary of {name} left with {code}, expected {}", expected.trim());
        }
        let panics = expected_file(file, "stderr");
        if let Some(expected) = &panics {
            assert!(text(&native.stderr) == *expected, "unexpected stderr of the binary of {name}:\n{}", text(&native.stderr));
        }

        // And the same program on stage 0: the same output, and the same exit code unless it panics
        let interpreted = torb(&["run", program]);
        assert!(
            text(&interpreted.stdout) == text(&native.stdout),
            "stage 0 and the binary of {name} print differently:\n{}\n{}",
            text(&interpreted.stdout),
            text(&native.stdout)
        );
        match panics {
            None => {
                let interpreted_code = interpreted.status.code().expect("an exit code");
                assert!(interpreted_code == code, "stage 0 left {name} with {interpreted_code}, the binary with {code}");
            }
            Some(_) => {
                assert!(
                    !interpreted.status.success(),
                    "stage 0 does not report the panic of {name} at all:\n{}",
                    text(&interpreted.stderr)
                );
            }
        }
    }
}
