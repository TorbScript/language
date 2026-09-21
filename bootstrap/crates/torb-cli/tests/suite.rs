//! The headline gate of milestone 5.11: **the compiler's own tests run from the compiled binary**.
//!
//! Stage 0 runs `compiler/tests/` one process per file through the interpreter. The binary runs all of them in *one*
//! process - one C translation unit that holds every test file plus a generated `main` that calls each file's entry
//! with the file's name in front of it - and the two runs have to print the same report and leave with the same code.
//!
//! What is compared is the whole of standard output: the line per file, the line per test, the four lines of a
//! failure, the blank line and the summary. That is the strictest thing there is to compare and it is what
//! `runtime/test.c` writing the format in one place is for.
//!
//! It builds the compiler and then compiles the whole test suite, so it takes minutes and needs a C compiler:
//!
//! ```text
//! cargo test --release --test suite -- --ignored --nocapture
//! ```

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

/// The repository root as a path a **user** would type: `canonicalize` answers Windows' verbatim form, which no
/// driver accepts as an argument.
fn repository() -> PathBuf {
    let found = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..").canonicalize().expect("the repository root");
    let shown = found.to_string_lossy().to_string();
    PathBuf::from(shown.strip_prefix("\\\\?\\").unwrap_or(&shown))
}

fn has_c_compiler() -> bool {
    if std::env::var("TORB_CC").is_ok_and(|given| !given.is_empty()) {
        return true;
    }
    ["clang", "gcc", "cc"]
        .iter()
        .any(|candidate| Command::new(candidate).arg("--version").output().is_ok_and(|output| output.status.success()))
}

/// A C compiler on Windows appends `.exe` to an output name without an extension.
fn binary_of(path: &Path) -> PathBuf {
    if path.exists() {
        return path.to_path_buf();
    }
    let mut name = path.as_os_str().to_os_string();
    name.push(".exe");
    PathBuf::from(name)
}

/// One run, from `bootstrap/` - which is the directory both commands of the gate list are written from, and therefore
/// the directory `../compiler/tests` is relative to in both reports.
fn run(what: &str, program: &Path, arguments: &[&str], from: &Path) -> (String, i32, f64) {
    let started = Instant::now();
    let output =
        Command::new(program).args(arguments).current_dir(from).output().unwrap_or_else(|problem| panic!("{what} runs: {problem}"));
    let seconds = started.elapsed().as_secs_f64();
    let code = output.status.code().expect("an exit code");
    eprintln!("{what}: {seconds:.1}s, exit {code}");
    if !output.stderr.is_empty() {
        eprintln!("{what} wrote to stderr:\n{}", String::from_utf8_lossy(&output.stderr));
    }
    (String::from_utf8_lossy(&output.stdout).to_string(), code, seconds)
}

#[test]
#[ignore = "minutes, and needs a C compiler: cargo test --release --test suite -- --ignored --nocapture"]
fn the_compiled_compiler_runs_the_compilers_own_tests() {
    if !has_c_compiler() {
        eprintln!("skipped: no C compiler found (tried $TORB_CC, clang, gcc, cc). The C back end needs one.");
        return;
    }
    let root = repository();
    let from = root.join("bootstrap");
    let compiler = root.join("compiler");
    let compiler = compiler.to_str().expect("UTF-8 path").to_string();
    let scratch = std::env::temp_dir().join(format!("torb-suite-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&scratch);
    let built = scratch.join("torb");
    let built_text = built.to_str().expect("UTF-8 path").to_string();

    let stage_0 = Path::new(env!("CARGO_BIN_EXE_torb"));
    let (_, code, seconds) =
        run("stage 0 builds the compiler", stage_0, &["run", &compiler, "build", &compiler, "--output", &built_text], &from);
    assert!(code == 0, "the compiler did not build");
    eprintln!("the compiler was built in {seconds:.1}s");

    // The binary builds the whole suite into one program and runs it
    let (native, native_code, native_seconds) =
        run("the binary runs the compiler's tests", &binary_of(&built), &["test", "../compiler/tests"], &from);

    // ...and the same suite through the interpreter, one process per file
    let (interpreted, interpreted_code, interpreted_seconds) =
        run("stage 0 runs the compiler's tests", stage_0, &["test", "../compiler/tests"], &from);

    eprintln!("native: {native_seconds:.1}s (build and run), stage 0: {interpreted_seconds:.1}s");
    assert_same_report(&interpreted, &native);
    assert!(interpreted_code == native_code, "stage 0 left with {interpreted_code} and the binary with {native_code}");
    eprintln!("{}", summary_of(&native).expect("a summary line"));

    let _ = std::fs::remove_dir_all(&scratch);
}

/// The last non-empty line, which is `N passed, M failed (K files)`.
fn summary_of(report: &str) -> Option<&str> {
    report.lines().rev().find(|line| !line.trim().is_empty())
}

/// The two reports, line by line, with the first line that differs named. "The reports differ" says nothing about
/// fifteen hundred lines.
fn assert_same_report(interpreted: &str, native: &str) {
    if interpreted == native {
        return;
    }
    let left: Vec<&str> = interpreted.lines().collect();
    let right: Vec<&str> = native.lines().collect();
    for index in 0..left.len().max(right.len()) {
        let one = left.get(index).copied();
        let other = right.get(index).copied();
        if one == other {
            continue;
        }
        panic!(
            "the two reports differ at line {}:\nstage 0: {}\nthe binary: {}\n(stage 0 has {} lines, the binary {})\nsummaries: {} / {}",
            index + 1,
            one.unwrap_or("<nothing>"),
            other.unwrap_or("<nothing>"),
            left.len(),
            right.len(),
            summary_of(interpreted).unwrap_or("<none>"),
            summary_of(native).unwrap_or("<none>")
        );
    }
}
