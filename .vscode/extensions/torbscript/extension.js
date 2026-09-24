// Highlights ```trb code blocks in the Markdown PREVIEW.
// (The editor is handled by the TextMate grammars in ./syntaxes, the preview uses highlight.js classes instead.)
//
// This keyword list must stay in sync with `compiler/src/syntax/token.trb`'s `TokenKind` (the lexer's source of
// truth) plus its three contextual words (`from`, `as`, `by` - ordinary identifiers to the
// lexer, keywords only in the positions this list's caller already restricts them to).

const KEYWORDS = new Set([
  'if', 'else', 'match', 'for', 'in', 'while', 'loop', 'break', 'continue', 'return',
  'const', 'var', 'static', 'fn', 'type', 'trait', 'extend', 'foreign', 'case', 'use', 'from', 'as',
  'public', 'private', 'native', 'shared', 'lazy', 'with', 'where', 'by',
]);
const LITERALS = new Set(['true', 'false', 'void', 'self', 'Self']);
// `Some`, `None`, `Ok`, `Fail` are cases (CONCEPT.md, "Algebraic Data Types"), not ordinary built-in functions -
// they get one shared class distinct from `BUILTINS`, matching the semantic token legend's `enumMember`.
const CASES = new Set(['Some', 'None', 'Ok', 'Fail']);
const BUILTINS = new Set(['print', 'panic', 'assert', 'do', 'spawn']);

const TOKEN = new RegExp([
  /(\/\/[^\n]*)/,                                                                  // 1 line comment
  /(raw"""[\s\S]*?"""|raw"[^"\n]*"|"""[\s\S]*?"""|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])+')/, // 2 string / char
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
    // Not preceded by an identifier character: true for `.Circle` (no receiver) and for `Shape.Circle`'s `Circle`
    // (the dot right before it), never for `shape.Circle` read as one longer word - there is none here, `word` is
    // this token alone. A capitalized word right after a `.` is always a case in this language (CONCEPT.md,
    // "Algebraic Data Types"): types never nest through a dot, only a case or a lowercase namespace does.
    const afterDot = code[match.index - 1] === '.';
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
    } else if (CASES.has(word) || (afterDot && /^[A-Z]/.test(word))) {
      // `Some`, `None`, `Ok`, `Fail`, `.Circle`, `Shape.Circle` - a case, colored apart from an ordinary type.
      html += span('hljs-symbol', text);
    } else if (BUILTINS.has(word)) {
      html += span('hljs-built_in', text);
    } else if (/^[A-Z]/.test(word)) {
      html += span('hljs-type', text);
    } else if (previousWord === 'fn') {
      html += span('hljs-title function_', text);
    } else if (/^\s*(<[^<>()]*>)?\(/.test(code.slice(last))) {
      // A call: `.name(` is a method (CONCEPT.md, "Command Calls") - command style and call style are not told
      // apart here, on purpose (they color the same everywhere else in this extension too).
      html += span(afterDot ? 'hljs-title function_ invoke__' : 'hljs-title function_', text);
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

// --- Semantic tokens: runs `torb highlight --stdin` and turns its JSON into a SemanticTokensBuilder result --------
//
// The TextMate grammar in ./syntaxes is a heuristic; it cannot know whether a name is a field or a local, a case
// or a plain type, a method or a function - that needs the syntax tree, which only the `torb` binary has (there is
// no language server yet; milestone 8 brings one behind this same JSON). This provider is the bridge: one process
// per request, its stdout is one JSON document (`compiler/src/highlight/` documents the exact shape), and if
// the binary is missing, fails, or answers late, this provider gives VS Code no tokens at all - the TextMate
// grammar's colors stand on their own, and the only trace is one line in the "TorbScript" output channel, never a
// popup. See README.md for the settings and the color palette this feeds through `configurationDefaults`.

const TOKEN_TYPES = [
  'type', 'interface', 'typeParameter', 'enumMember', 'namespace', 'function', 'method', 'parameter', 'variable', 'property',
];
const TOKEN_MODIFIERS = ['declaration', 'readonly', 'static', 'defaultLibrary', 'mutable'];

let outputChannel = null;
function log(vscode, message) {
  if (!outputChannel) {
    outputChannel = vscode.window.createOutputChannel('TorbScript');
  }
  outputChannel.appendLine(message);
}

// Where a `torb` is looked for under a workspace folder: the compiler that `sh tools/bootstrap.sh` writes.
const EXECUTABLE_CANDIDATES = [
  ['build', 'release'],
];

/** `torbscript.executablePath`, or the first `torb`/`torb.exe` of `EXECUTABLE_CANDIDATES` that exists under an open
 * workspace folder, or finally the bare command name to try on `PATH`. */
function findExecutable(vscode, fs, path) {
  const vscode_config = vscode.workspace.getConfiguration('torbscript');
  const configured = vscode_config.get('executablePath');
  if (configured) {
    return configured;
  }
  const exeName = process.platform === 'win32' ? 'torb.exe' : 'torb';
  for (const folder of vscode.workspace.workspaceFolders || []) {
    for (const parts of EXECUTABLE_CANDIDATES) {
      const candidate = path.join(folder.uri.fsPath, ...parts, exeName);
      try {
        if (fs.existsSync(candidate)) {
          return candidate;
        }
      } catch (error) {
        // Treated the same as "not found": fall through to the next candidate.
      }
    }
  }
  return exeName === 'torb.exe' ? 'torb' : exeName; // Try PATH under the bare command name either way.
}

class TorbSemanticTokensProvider {
  constructor(vscode, cp, fs, path) {
    this.vscode = vscode;
    this.cp = cp;
    this.fs = fs;
    this.path = path;
    this.legend = new vscode.SemanticTokensLegend(TOKEN_TYPES, TOKEN_MODIFIERS);
    this.current = null; // The child process of the request still in flight, if any.
  }

  provideDocumentSemanticTokens(document, cancellationToken) {
    const { vscode } = this;
    const enabled = vscode.workspace.getConfiguration('torbscript').get('semanticHighlighting.enabled', true);
    const builder = new vscode.SemanticTokensBuilder(this.legend);
    if (!enabled) {
      return builder.build();
    }
    this.killCurrent();
    const executable = findExecutable(vscode, this.fs, this.path);
    return new Promise((resolve) => {
      let settled = false;
      const finish = (result) => {
        if (settled) {
          return;
        }
        settled = true;
        resolve(result);
      };
      let child;
      try {
        child = this.cp.spawn(executable, ['highlight', '--stdin'], { cwd: this.workspaceRoot(document) });
      } catch (error) {
        log(vscode, `could not start "${executable}": ${error.message}`);
        return finish(builder.build());
      }
      this.current = child;
      const timeout = setTimeout(() => {
        log(vscode, `"${executable} highlight --stdin" timed out, killing it`);
        child.kill();
      }, 4000);
      let stdout = '';
      let stderr = '';
      child.stdout.on('data', (chunk) => (stdout += chunk));
      child.stderr.on('data', (chunk) => (stderr += chunk));
      // If the process exits (crashes, or a small file makes it finish before this extension is done writing) the
      // write end of its stdin pipe closes; without a listener here, Node treats that as an *unhandled* error and
      // crashes the whole extension host. `child`'s own `error`/`close` handlers below already report and resolve.
      child.stdin.on('error', () => {});
      child.on('error', (error) => {
        clearTimeout(timeout);
        log(vscode, `could not run "${executable} highlight --stdin": ${error.message}`);
        finish(builder.build());
      });
      child.on('close', () => {
        clearTimeout(timeout);
        if (this.current === child) {
          this.current = null;
        }
        if (stderr.trim()) {
          log(vscode, stderr.trim());
        }
        finish(this.buildTokens(builder, stdout));
      });
      // Kills this request's own process, never whatever `this.current` has become by the time this fires - a
      // newer request may already have replaced it (killCurrent() at the top of this method takes care of that
      // case instead).
      cancellationToken.onCancellationRequested(() => {
        try {
          child.kill();
        } catch (error) {
          // Already gone.
        }
      });
      try {
        child.stdin.write(document.getText());
        child.stdin.end();
      } catch (error) {
        // The process may already have exited (e.g. the executable was not actually runnable); `close` above
        // still fires and resolves the promise.
      }
    });
  }

  killCurrent() {
    if (!this.current) {
      return;
    }
    try {
      this.current.kill();
    } catch (error) {
      // Already gone.
    }
    this.current = null;
  }

  workspaceRoot(document) {
    const folder = this.vscode.workspace.getWorkspaceFolder(document.uri);
    return folder ? folder.uri.fsPath : undefined;
  }

  buildTokens(builder, stdout) {
    let parsed;
    try {
      parsed = JSON.parse(stdout);
    } catch (error) {
      if (stdout.trim()) {
        log(this.vscode, `could not parse its output as JSON: ${error.message}`);
      }
      return builder.build();
    }
    for (const entry of parsed.tokens || []) {
      const [line, startCharacter, length, kind, modifiers] = entry;
      if (!TOKEN_TYPES.includes(kind)) {
        continue; // A future `torb` may know kinds this extension's legend does not yet declare.
      }
      const range = new this.vscode.Range(line, startCharacter, line, startCharacter + length);
      try {
        builder.push(range, kind, (modifiers || []).filter((modifier) => TOKEN_MODIFIERS.includes(modifier)));
      } catch (error) {
        log(this.vscode, `dropped a token it could not place: ${error.message}`);
      }
    }
    return builder.build();
  }
}

function activate(context) {
  const vscode = require('vscode');
  const cp = require('child_process');
  const fs = require('fs');
  const path = require('path');

  const provider = new TorbSemanticTokensProvider(vscode, cp, fs, path);
  context.subscriptions.push(
    vscode.languages.registerDocumentSemanticTokensProvider({ language: 'trb' }, provider, provider.legend)
  );

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

module.exports = { activate, highlight, TorbSemanticTokensProvider, findExecutable, TOKEN_TYPES, TOKEN_MODIFIERS };
