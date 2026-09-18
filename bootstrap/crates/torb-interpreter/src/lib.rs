//! The bootstrap interpreter of TorbScript.
//!
//! It exists for one purpose: to run the TorbScript compiler that is written in TorbScript (`compiler/`) until that
//! compiler can compile itself. It walks the syntax tree, has no type checker, and implements the parts of the
//! standard library it needs natively. What it deliberately does not do is listed in `bootstrap/README.md`.

mod interpreter;
mod natives;
mod program;
mod value;

use std::path::Path;

pub use interpreter::{Failure, TestReport};

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
    match interpreter.run() {
        Ok(()) => Outcome::Finished(std::mem::take(&mut interpreter.tests)),
        Err(failure) => Outcome::Failed(failure),
    }
}
