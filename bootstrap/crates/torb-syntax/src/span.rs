/// A range of bytes in a source file.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Span {
    pub start: u32,
    pub end: u32,
}

impl Span {
    pub fn new(start: usize, end: usize) -> Self {
        Span { start: start as u32, end: end as u32 }
    }

    pub fn to(self, other: Span) -> Span {
        Span { start: self.start.min(other.start), end: self.end.max(other.end) }
    }

    pub fn range(self) -> std::ops::Range<usize> {
        self.start as usize..self.end as usize
    }
}

/// Translates byte offsets to lines and columns (both starting at 1, columns count characters).
pub struct LineIndex<'source> {
    source: &'source str,
    line_starts: Vec<usize>,
}

impl<'source> LineIndex<'source> {
    pub fn new(source: &'source str) -> Self {
        let mut line_starts = vec![0];
        line_starts.extend(source.match_indices('\n').map(|(index, _)| index + 1));
        LineIndex { source, line_starts }
    }

    pub fn line_and_column(&self, offset: u32) -> (usize, usize) {
        let offset = (offset as usize).min(self.source.len());
        let line = self.line_starts.partition_point(|&start| start <= offset) - 1;
        let column = self.source[self.line_starts[line]..offset].chars().count();
        (line + 1, column + 1)
    }

    pub fn line_text(&self, line: usize) -> &'source str {
        let start = self.line_starts[line - 1];
        let end = self.line_starts.get(line).map_or(self.source.len(), |&next| next);
        self.source[start..end].trim_end_matches(['\n', '\r'])
    }
}
