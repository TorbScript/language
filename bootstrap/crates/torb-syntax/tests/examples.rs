//! The examples are the conformance suite of the grammar: every `.trb` file in the repository has to parse.

use std::path::{Path, PathBuf};

fn collect(directory: &Path, files: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).expect("readable directory") {
        let path = entry.expect("readable entry").path();
        if path.is_dir() {
            collect(&path, files);
        } else if path.extension().is_some_and(|extension| extension == "trb") {
            files.push(path);
        }
    }
}

#[test]
fn all_examples_parse() {
    // Everything that is TorbScript in this repository: the examples, the standard library, the compiler
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let mut files = Vec::new();
    for directory in ["examples", "std", "compiler"] {
        if root.join(directory).is_dir() {
            collect(&root.join(directory), &mut files);
        }
    }
    assert!(files.len() > 40, "expected the examples, found {} files", files.len());

    let mut problems = Vec::new();
    for file in &files {
        let source = std::fs::read_to_string(file).expect("readable file");
        let lines = torb_syntax::LineIndex::new(&source);
        for diagnostic in torb_syntax::parse(&source).diagnostics {
            let (line, column) = lines.line_and_column(diagnostic.span.start);
            problems.push(format!("{}:{line}:{column}: {}", file.display(), diagnostic.message));
        }
    }
    assert!(problems.is_empty(), "{}", problems.join("\n"));
}
