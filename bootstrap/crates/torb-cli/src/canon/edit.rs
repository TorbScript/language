//! Text replacements in the coordinates of the original source, and the document they are applied to one at a time.

/// One replacement of a byte range of the **original** source. Nothing outside of the range is touched, which is why
/// line endings, comments and everything else stay byte-identical.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Replacement {
    pub start: u32,
    pub end: u32,
    pub text: String,
}

impl Replacement {
    pub fn new(start: u32, end: u32, text: impl Into<String>) -> Self {
        Replacement { start, end, text: text.into() }
    }
}

/// What an edit does, for the counts of the report.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum EditKind {
    /// `f(a)` becomes `f a`
    ToCommand,
    /// `f a` becomes `f(a)`
    ToParentheses,
    /// A `"""` string is written in the indented form
    IndentedString,
    /// `.None` becomes `None`
    CasePattern,
    /// A binding of a refutable pattern that nobody reads becomes `_`
    UnusedBinding,
    /// `while true {` becomes `loop {`
    EndlessLoop,
}

impl EditKind {
    /// Whether the syntax tree has to come out the same (modulo spans and `CallStyle`). Three rules change the tree on
    /// purpose - that is the point of them - so those are checked by parsing alone.
    pub fn preserves_the_tree(self) -> bool {
        !matches!(self, EditKind::CasePattern | EditKind::UnusedBinding | EditKind::EndlessLoop)
    }
}

/// One edit of the canon: the replacements that only make sense together (the `(` and the `)` of a call are one
/// edit), and where it is about, for the report and for a deterministic order.
#[derive(Debug, Clone)]
pub struct Edit {
    pub kind: EditKind,
    pub position: u32,
    pub replacements: Vec<Replacement>,
}

impl Edit {
    pub fn new(kind: EditKind, position: u32, replacements: Vec<Replacement>) -> Self {
        Edit { kind, position, replacements }
    }
}

/// The source with the edits that were accepted so far. Edits keep the offsets of the original source; the document
/// remembers what every accepted replacement shifted and translates them.
pub struct Document {
    text: String,
    /// `(end of a replaced range in the original source, how much the text grew there)`, in ascending order
    shifts: Vec<(u32, isize)>,
}

impl Document {
    pub fn new(source: &str) -> Self {
        Document { text: source.to_string(), shifts: Vec::new() }
    }

    pub fn into_text(self) -> String {
        self.text
    }

    /// Where an offset of the original source sits in the current text.
    fn offset(&self, original: u32) -> usize {
        let shift: isize = self.shifts.iter().take_while(|(end, _)| *end <= original).map(|(_, shift)| shift).sum();
        (original as isize + shift) as usize
    }

    /// What the edit would produce, without keeping it. The replacements of one edit never overlap, so applying them
    /// from the last to the first keeps the offsets of the ones that are still to come.
    pub fn with(&self, edit: &Edit) -> String {
        let mut replacements = edit.replacements.clone();
        replacements.sort_by_key(|replacement| replacement.start);
        let mut text = self.text.clone();
        for replacement in replacements.iter().rev() {
            let (start, end) = (self.offset(replacement.start), self.offset(replacement.end));
            text.replace_range(start..end, &replacement.text);
        }
        text
    }

    /// Keeps what `with` produced and records the shifts of the edit.
    pub fn accept(&mut self, edit: &Edit, text: String) {
        self.text = text;
        for replacement in &edit.replacements {
            let shift = replacement.text.len() as isize - (replacement.end - replacement.start) as isize;
            let position = self.shifts.partition_point(|(end, _)| *end <= replacement.end);
            self.shifts.insert(position, (replacement.end, shift));
        }
    }
}
