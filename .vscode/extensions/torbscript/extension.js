// Highlights ```trb code blocks in the Markdown PREVIEW.
// (The editor is handled by the TextMate grammars in ./syntaxes, the preview uses highlight.js classes instead.)

const KEYWORDS = new Set([
  'if', 'else', 'match', 'for', 'in', 'while', 'break', 'continue', 'return',
  'const', 'var', 'fn', 'type', 'trait', 'extend', 'foreign', 'case', 'use', 'from', 'as',
  'public', 'private', 'native', 'shared', 'lazy', 'with', 'where', 'by',
]);
const LITERALS = new Set(['true', 'false', 'None', 'Void', 'self', 'Self']);
const BUILTINS = new Set(['print', 'panic', 'assert', 'do', 'spawn', 'Some', 'Ok', 'Error']);

const TOKEN = new RegExp([
  /(\/\/[^\n]*)/,                                                                  // 1 line comment
  /(r"""[\s\S]*?"""|r"[^"\n]*"|"""[\s\S]*?"""|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])+')/, // 2 string / char
  /(\b0x[\da-fA-F_]+\b|\b0b[01_]+\b|\b\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?\b)/,     // 3 number
  /\b([A-Za-z_]\w*)\b/,                                                            // 4 identifier
].map((part) => part.source).join('|'), 'g');

function escapeHtml(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function span(className, text) {
  return `<span class="${className}">${escapeHtml(text)}</span>`;
}

/** Block comments nest, so they are cut out by hand before the regular expression runs. */
function splitBlockComments(code) {
  const parts = [];
  let start = 0;
  let index = 0;
  while (index < code.length) {
    const char = code[index];
    if (char === '"' || char === "'") {
      // Skip strings, so `"/*"` does not start a comment
      const triple = code.startsWith('"""', index);
      const quote = triple ? '"""' : char;
      let end = index + quote.length;
      while (end < code.length && !code.startsWith(quote, end) && (triple || code[end] !== '\n')) {
        end += code[end] === '\\' ? 2 : 1;
      }
      index = Math.min(code.length, end + quote.length);
    } else if (code.startsWith('//', index)) {
      const end = code.indexOf('\n', index);
      index = end === -1 ? code.length : end;
    } else if (code.startsWith('/*', index)) {
      let depth = 0;
      let end = index;
      do {
        if (code.startsWith('/*', end)) { depth++; end += 2; }
        else if (code.startsWith('*/', end)) { depth--; end += 2; }
        else { end++; }
      } while (depth > 0 && end < code.length);
      parts.push({ comment: false, text: code.slice(start, index) });
      parts.push({ comment: true, text: code.slice(index, end) });
      start = index = end;
    } else {
      index++;
    }
  }
  parts.push({ comment: false, text: code.slice(start) });
  return parts;
}

function highlightCode(code) {
  let html = '';
  let last = 0;
  let previousWord = '';
  TOKEN.lastIndex = 0;
  for (let match = TOKEN.exec(code); match; match = TOKEN.exec(code)) {
    html += escapeHtml(code.slice(last, match.index));
    last = match.index + match[0].length;
    const [text, comment, string, number, word] = match;
    if (comment) {
      html += span('hljs-comment', text);
    } else if (string) {
      html += span('hljs-string', text);
    } else if (number) {
      html += span('hljs-number', text);
    } else if (KEYWORDS.has(word)) {
      html += span('hljs-keyword', text);
    } else if (LITERALS.has(word)) {
      html += span('hljs-literal', text);
    } else if (BUILTINS.has(word)) {
      html += span('hljs-built_in', text);
    } else if (/^[A-Z]/.test(word)) {
      html += span('hljs-type', text);
    } else if (previousWord === 'fn' || /^\s*(<[^<>()]*>)?\(/.test(code.slice(last))) {
      html += span('hljs-title function_', text);
    } else {
      html += escapeHtml(text);
    }
    previousWord = word || '';
  }
  return html + escapeHtml(code.slice(last));
}

function highlight(code) {
  return splitBlockComments(code)
    .map((part) => (part.comment ? span('hljs-comment', part.text) : highlightCode(part.text)))
    .join('');
}

function activate() {
  return {
    extendMarkdownIt(md) {
      const fallback = md.options.highlight;
      md.options.highlight = (code, lang, attrs) => {
        if (lang && /^(trb|torbscript)$/i.test(lang.trim())) {
          return highlight(code);
        }
        return fallback ? fallback(code, lang, attrs) : '';
      };
      return md;
    },
  };
}

module.exports = { activate, highlight };
