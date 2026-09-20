//! The fixpoint of milestone 6.2: the compiler compiles itself to the **same** C twice.
//!
//! Stage 1 is `compiler/` run by this interpreter, stage 2 is the binary stage 1 built, stage 3 is the binary stage 2
//! built. `program.c` of stage 1 and of stage 2 have to be byte identical, and so does stage 3's - which is what says
//! that the emitter is a pure function of the program and that the binary is a correct implementation of it. A binary
//! that disagreed with the interpreter about one iteration order, one uninitialized slot or one float would show up
//! here as the first differing byte and nowhere else.
//!
//! It takes minutes and needs a C compiler, so it is `#[ignore]`d:
//!
//! ```text
//! cargo test --release --test fixpoint -- --ignored --nocapture
//! ```

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

/// The repository root as a path a **user** would type. `canonicalize` answers Windows' verbatim form (`\\?\C:\...`),
/// which is what the API wants and not what a command line carries - and which neither stage accepts as an argument, so
/// the prefix comes off here rather than surprising the driver.
fn repository() -> PathBuf {
    let found = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..").canonicalize().expect("the repository root");
    let shown = found.to_string_lossy().to_string();
    PathBuf::from(shown.strip_prefix("\\\\?\\").unwrap_or(&shown))
}

/// The same order `torb build` uses. Without one of them there is nothing to compile with, and the test says so
/// instead of failing.
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

/// One build, from the repository root so that the driver finds `runtime/` by walking up. The seconds are printed
/// because the point of the whole milestone is that the binary is the fast one.
fn build(what: &str, program: &Path, arguments: &[&str], root: &Path) -> PathBuf {
    let started = Instant::now();
    let output =
        Command::new(program).args(arguments).current_dir(root).output().unwrap_or_else(|problem| panic!("{what} runs: {problem}"));
    let seconds = started.elapsed().as_secs_f64();
    assert!(
        output.status.success(),
        "{what} failed after {seconds:.1}s:\n{}\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    eprintln!("{what}: {seconds:.1}s");
    // `--output <path>` puts `program.c` next to the binary, so the directory of the output is where both are
    let last = arguments.last().expect("an output path");
    Path::new(last).parent().expect("an output directory").join("program.c")
}

#[test]
#[ignore = "minutes, and needs a C compiler: cargo test --release --test fixpoint -- --ignored --nocapture"]
fn the_compiler_compiles_itself_to_the_same_c_twice() {
    if !has_c_compiler() {
        eprintln!("skipped: no C compiler found (tried $TORB_CC, clang, gcc, cc). The C back end needs one.");
        return;
    }
    let root = repository();
    let compiler = root.join("compiler");
    let compiler = compiler.to_str().expect("UTF-8 path").to_string();
    // A directory of this test process, so that a second run somewhere else cannot overwrite a `program.c` between the
    // build and the comparison
    let scratch = std::env::temp_dir().join(format!("torb-fixpoint-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&scratch);
    let two = scratch.join("stage2/torb");
    let three = scratch.join("stage3/torb");
    let four = scratch.join("stage4/torb");

    let one_c = build(
        "stage 1 (the interpreter) builds the compiler",
        Path::new(env!("CARGO_BIN_EXE_torb")),
        &["run", &compiler, "build", &compiler, "--output", two.to_str().expect("UTF-8 path")],
        &root,
    );
    let two_binary = binary_of(&two);

    let two_c = build(
        "stage 2 (the binary) builds the compiler",
        &two_binary,
        &["build", &compiler, "--output", three.to_str().expect("UTF-8 path")],
        &root,
    );
    let three_binary = binary_of(&three);

    let one = std::fs::read(&one_c).expect("the C of stage 1");
    let two_bytes = std::fs::read(&two_c).expect("the C of stage 2");
    assert_same("stage 1", &one, "stage 2", &two_bytes);
    eprintln!("stage 1 and stage 2 agree on {} bytes of C", one.len());

    // Stage 3 only has to emit: a third binary would say nothing a third `program.c` does not
    let three_c = build(
        "stage 3 emits the C of the compiler",
        &three_binary,
        &["build", &compiler, "--emit-c", "--output", four.to_str().expect("UTF-8 path")],
        &root,
    );
    let three_bytes = std::fs::read(&three_c).expect("the C of stage 3");
    assert_same("stage 1", &one, "stage 3", &three_bytes);
    eprintln!("stage 3 agrees as well: the fixpoint holds");

    let _ = std::fs::remove_dir_all(&scratch);
}

/// The first differing byte with the lines around it, because "the files differ" says nothing about 66 megabytes.
fn assert_same(left_name: &str, left: &[u8], right_name: &str, right: &[u8]) {
    if left == right {
        return;
    }
    let at = left.iter().zip(right.iter()).position(|(a, b)| a != b).unwrap_or(left.len().min(right.len()));
    let around = |bytes: &[u8]| {
        let from = at.saturating_sub(300);
        let to = (at + 300).min(bytes.len());
        String::from_utf8_lossy(&bytes[from..to]).to_string()
    };
    panic!(
        "the C of {left_name} ({} bytes) and of {right_name} ({} bytes) differ at byte {at}:
{left_name}: ...{}...
{right_name}: ...{}...",
        left.len(),
        right.len(),
        around(left),
        around(right)
    );
}
