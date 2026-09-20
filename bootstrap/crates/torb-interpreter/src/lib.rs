//! The bootstrap interpreter of TorbScript.
//!
//! It exists for one purpose: to run the TorbScript compiler that is written in TorbScript (`compiler/`) until that
//! compiler can compile itself. It walks the syntax tree, has no type checker, and implements the parts of the
//! standard library it needs natively. What it deliberately does not do is listed in `bootstrap/README.md`.

mod characters;
mod interpreter;
mod natives;
mod profile;
mod program;
mod value;

use std::path::Path;

pub use interpreter::{wants_frames, Failure, FailureKind, TestReport};

/// What a process that panicked leaves with (CONCEPT, "A panic is output"). `TORB_PANIC_EXIT_CODE` in `runtime/torb.h`
/// is the same number, because both back ends have to agree on it.
pub const PANIC_EXIT_CODE: u8 = 101;

pub enum Outcome {
    Finished(TestReport),
    /// Syntax errors, missing imports: nothing was run
    NotLoaded(Vec<String>),
    Failed(Failure),
}

/// Runs the top-level code of a file. `arguments` is what `Process.arguments()` returns.
pub fn run(entry: &Path, arguments: Vec<String>) -> Outcome {
    let program = match program::load(entry) {
        Ok(program) => program,
        Err(problems) => return Outcome::NotLoaded(problems),
    };
    let mut interpreter = interpreter::Interpreter::new(program, arguments);
    let outcome = match interpreter.run() {
        Ok(()) => Outcome::Finished(std::mem::take(&mut interpreter.tests)),
        Err(failure) => Outcome::Failed(failure),
    };
    profile::report();
    outcome
}
