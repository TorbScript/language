//! The conformance runner: every program in `bootstrap/tests/native/` is compiled to a native binary, run, and
//! compared with the same program on stage 0.
//!
//! **Everything a program can be observed doing is compared, and nothing is exempt**: its standard output, its
//! standard error, and its exit code. A program that panics is compared like every other one - the same two lines and
//! the same 101 - which is what milestone 5.14 is. The one thing that is read loosely is the *position* inside a frame
//! of `std/`, because a line of the standard library moves whenever a comment above it is edited and what a program
//! promises is which file panicked; `without_library_positions` below says exactly how.
//!
//! **`\n` is `\n`.** Nothing normalises line endings of what a program wrote: the runtime puts `stdout` and `stderr`
//! into binary mode on Windows, so a compiled binary writes the same bytes into a pipe that stage 0 writes. Only the
//! expectation *files* are folded, because git may check one out with either ending.
//!
//! `bootstrap/tests/native/README.md` is the contract this test enforces, and how a program is added to it. The one
//! subdirectory, `stage-0-only/`, is a waiting room rather than an exception: a program lands there when the back end
//! cannot produce the behaviour yet and stage 0 already answers what the language says, and the second test below runs
//! those on stage 0 alone.

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

/// An expectation **file**, whose `\r\n` is folded: git may check one out with either line ending, and what the file
/// holds is the text of the expectation and not its bytes on this machine.
fn expected_file(program: &Path, extension: &str) -> Option<String> {
    let path = program.with_extension(extension);
    std::fs::read_to_string(path).ok().map(|text| text.replace("\r\n", "\n"))
}

/// What a program **wrote**, as it wrote it. Nothing is folded here: `\n` is `\n` on both implementations, because the
/// runtime puts `stdout` and `stderr` into binary mode on Windows (`torb_process_start`) and stage 0 writes what Rust
/// writes. A `\r` that turns up in this text is a difference the suite is meant to catch.
fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).to_string()
}

/// A scratch directory of this test process, which is what keeps two runs at once from disturbing each other.
fn scratch(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("{name}-{}", std::process::id()))
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
    // One directory per test process, because two runs at once (another worktree, a second `cargo test`) would
    // otherwise overwrite each other's `program.c` between the build and the comparison
    let output = scratch("torb-native-tests");
    let twice = scratch("torb-native-tests-again");
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

        // The leak gate: with `TORB_REPORT_LEAKS=1` the runtime writes its two block counts where the program ends, and a
        // program that frees what it allocated reports zero live ones. A panic runs nothing (decided gap 9), so what it
        // leaves behind is not a leak and the gate does not apply to one.
        //
        // The second number is the **immortal** blocks: the value of a module constant is built once into a block that is
        // never freed by construction, and counting those apart is what keeps the first number exact. Both lines are
        // asserted, so the accounting cannot be turned off without a test saying so.
        //
        // A `.leaks` file beside the program is the one exemption, and it is not a hole in the gate: it says that the
        // run **recovers** a panic - a test whose body panicked - and a recovered panic releases nothing on the way
        // out, exactly as an ordinary panic releases nothing. The file holds the reason, so an exemption nobody can
        // justify cannot be added silently.
        if expected_file(file, "stderr").is_none() && expected_file(file, "leaks").is_none() {
            let counted = Command::new(&binary).env("TORB_REPORT_LEAKS", "1").output().expect("the compiled program runs");
            assert!(
                text(&counted.stderr).contains("live blocks at exit: 0\n"),
                "the binary of {name} does not report zero live blocks:\n{}",
                text(&counted.stderr)
            );
            assert!(
                text(&counted.stderr).contains("immortal blocks at exit: "),
                "the binary of {name} does not report its immortal blocks:\n{}",
                text(&counted.stderr)
            );
        }

        if let Some(expected) = expected_file(file, "expected") {
            assert!(text(&native.stdout) == expected, "unexpected output of the binary of {name}:\n{}", text(&native.stdout));
        }
        if let Some(expected) = expected_file(file, "exit") {
            assert!(code.to_string() == expected.trim(), "the binary of {name} left with {code}, expected {}", expected.trim());
        }
        let reported = without_library_positions(&text(&native.stderr));
        match expected_file(file, "stderr") {
            Some(expected) => assert!(reported == expected, "unexpected stderr of the binary of {name}:\n{reported}"),
            // A program without a `.stderr` file promises to write nothing there at all
            None => assert!(reported.is_empty(), "the binary of {name} writes to stderr and has no `.stderr` file:\n{reported}"),
        }

        // And the same program on stage 0: the same three observations, and nothing is exempt
        let interpreted = torb(&["run", program]);
        let interpreted_code = interpreted.status.code().expect("an exit code");
        assert!(
            text(&interpreted.stdout) == text(&native.stdout),
            "stage 0 and the binary of {name} print differently:\n{}\n{}",
            text(&interpreted.stdout),
            text(&native.stdout)
        );
        assert!(
            without_library_positions(&text(&interpreted.stderr)) == reported,
            "stage 0 and the binary of {name} report differently:\n{}\n{reported}",
            without_library_positions(&text(&interpreted.stderr))
        );
        assert!(interpreted_code == code, "stage 0 left {name} with {interpreted_code}, the binary with {code}");
    }

    // Nothing here is read after the loop, and a temporary directory per process would otherwise pile up
    let _ = std::fs::remove_dir_all(&output);
    let _ = std::fs::remove_dir_all(&twice);
}

/// The programs of `bootstrap/tests/native/stage-0-only/`, which are run and compared on stage 0 alone.
///
/// A program lands there when the C back end cannot produce the behaviour *yet* and stage 0 already answers what the
/// language says - so there is nothing to compare, and the program still pins the answer instead of waiting. Each one
/// says in its doc comment why it is there and what has to exist for it to move up one directory. The three
/// expectation files are read exactly as they are above.
#[test]
fn every_stage_0_only_program_matches_its_expectations() {
    let directory = repository().join("bootstrap/tests/native/stage-0-only");
    let files = programs(&directory);
    assert!(!files.is_empty(), "expected the programs that only stage 0 can run");
    for file in &files {
        let name = file.file_stem().expect("a file name").to_str().expect("UTF-8 name").to_string();
        let interpreted = torb(&["run", file.to_str().expect("UTF-8 path")]);
        let code = interpreted.status.code().expect("an exit code");
        if let Some(expected) = expected_file(file, "expected") {
            assert!(text(&interpreted.stdout) == expected, "unexpected output of {name}:\n{}", text(&interpreted.stdout));
        }
        if let Some(expected) = expected_file(file, "exit") {
            assert!(code.to_string() == expected.trim(), "{name} left with {code}, expected {}", expected.trim());
        }
        let reported = without_library_positions(&text(&interpreted.stderr));
        match expected_file(file, "stderr") {
            Some(expected) => assert!(reported == expected, "unexpected stderr of {name}:\n{reported}"),
            None => assert!(reported.is_empty(), "{name} writes to stderr and has no `.stderr` file:\n{reported}"),
        }
    }
}

/// The programs of `bootstrap/tests/native/binary-only/`, which are built and run as a binary alone.
///
/// A program lands there when the two implementations **deliberately** answer differently, so there is nothing to
/// compare and the divergence is still pinned instead of being untested. There is one, and the README names it: a
/// failing `assert` shows a capture that is not a scalar by its name and its type, where stage 0 shows its value. Each
/// program says in its doc comment why it is there and what would close the difference.
///
/// The leak gate does not run here: every one of these ends in a failed test, which is a recovered panic.
#[test]
fn every_binary_only_program_matches_its_expectations() {
    let root = repository();
    let directory = root.join("bootstrap/tests/native/binary-only");
    let files = programs(&directory);
    assert!(!files.is_empty(), "expected the programs that only the binary can be compared against");
    let output = scratch("torb-native-binary-only");
    let compiler_path = root.join("compiler");
    let compiler_path = compiler_path.to_str().expect("UTF-8 path");
    for file in &files {
        let name = file.file_stem().expect("a file name").to_str().expect("UTF-8 name").to_string();
        let target = output.join(&name);
        let target_text = target.to_str().expect("UTF-8 path").to_string();
        let built = torb(&["run", compiler_path, "build", file.to_str().expect("UTF-8 path"), "--output", &target_text]);
        assert!(built.status.success(), "torb build {name} failed:\n{}\n{}", text(&built.stdout), text(&built.stderr));
        let native = Command::new(binary_of(&target)).output().expect("the compiled program runs");
        let code = native.status.code().expect("an exit code");
        if let Some(expected) = expected_file(file, "expected") {
            assert!(text(&native.stdout) == expected, "unexpected output of {name}:\n{}", text(&native.stdout));
        }
        if let Some(expected) = expected_file(file, "exit") {
            assert!(code.to_string() == expected.trim(), "{name} left with {code}, expected {}", expected.trim());
        }
        let reported = without_library_positions(&text(&native.stderr));
        match expected_file(file, "stderr") {
            Some(expected) => assert!(reported == expected, "unexpected stderr of {name}:\n{reported}"),
            None => assert!(reported.is_empty(), "{name} writes to stderr and has no `.stderr` file:\n{reported}"),
        }
    }
    let _ = std::fs::remove_dir_all(&output);
}

/// **What a program printed comes before the panic that ended it.** The runner above reads the two streams apart, so
/// the order *between* them is not something it can see: this builds `panic-after-output.trb` and runs it with both
/// of them redirected into one file, which is what a terminal and a log do.
#[test]
fn a_panic_is_written_after_everything_the_program_printed() {
    let root = repository();
    let Some(_) = c_compiler() else {
        eprintln!("no C compiler found (TORB_CC, clang, gcc, cc) - skipping");
        return;
    };
    let file = root.join("bootstrap/tests/native/panic-after-output.trb");
    let output = scratch("torb-native-panic-order");
    let target = output.join("panic-after-output");
    let target_text = target.to_str().expect("UTF-8 path").to_string();
    let compiler_path = root.join("compiler");
    let built =
        torb(&["run", compiler_path.to_str().expect("UTF-8 path"), "build", file.to_str().expect("UTF-8 path"), "--output", &target_text]);
    assert!(
        built.status.success(),
        "torb build failed:
{}
{}",
        text(&built.stdout),
        text(&built.stderr)
    );
    let merged = output.join("both.txt");
    let stream = std::fs::File::create(&merged).expect("a writable scratch file");
    let errors = stream.try_clone().expect("a second handle on the same file");
    let status = Command::new(binary_of(&target))
        .stdout(std::process::Stdio::from(stream))
        .stderr(std::process::Stdio::from(errors))
        .status()
        .expect("the compiled program runs");
    assert!(status.code() == Some(101), "expected a panic, left with {:?}", status.code());
    let written = std::fs::read_to_string(&merged).expect("what the program wrote");
    let printed = written.find("after the default was passed over").expect("the last line the program printed");
    let panicked = written.find("panic: a default that panics").expect("the panic");
    assert!(
        printed < panicked,
        "the panic was written before the output:
{written}"
    );
    let _ = std::fs::remove_dir_all(&output);
}

/// A panic inside the standard library names a line of `std/`, and that line moves whenever a comment above it is
/// edited. What a program promises is *which file* of the library panics, so the position in a `std/` frame reads
/// `_:_` in a `.stderr` file; a frame of the program itself keeps its position.
///
/// Stage 0 answers those bodies with a native and has no line for one at all, so it writes the file alone
/// (`at std/core/src/option.trb`) - which reads as `_:_` here as well. That is the one thing about a panic the two
/// implementations do not have to agree on, and it is a position that nothing promises rather than a behaviour.
fn without_library_positions(stderr: &str) -> String {
    let mut result = String::new();
    for line in stderr.split_inclusive('\n') {
        let body = line.trim_end_matches(['\r', '\n']);
        let ending = &line[body.len()..];
        match library_frame(body) {
            Some(path) => {
                result.push_str(path);
                result.push_str(":_:_");
            }
            None => result.push_str(body),
        }
        result.push_str(ending);
    }
    result
}

/// `  at std/core/src/option.trb` of a line that is a frame of the standard library, with or without a position.
fn library_frame(body: &str) -> Option<&str> {
    let is_position =
        |part: Option<&str>| part.is_some_and(|digits| !digits.is_empty() && digits.bytes().all(|byte| byte.is_ascii_digit()));
    if !body.trim_start().starts_with("at std/") {
        return None;
    }
    let mut parts = body.rsplitn(3, ':');
    let (column, row, path) = (parts.next(), parts.next(), parts.next());
    match path {
        Some(path) if is_position(row) && is_position(column) => Some(path),
        // The file alone, the way stage 0 writes a frame of a body it answers natively
        _ => Some(body),
    }
}
