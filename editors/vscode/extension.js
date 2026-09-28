// Highlights ```trb code blocks in the Markdown PREVIEW.
// (The editor is handled by the TextMate grammars in ./syntaxes, the preview uses highlight.js classes instead.)
//
// This keyword list must stay in sync with `compiler/src/syntax/token.trb`'s `TokenKind` (the lexer's source of
// truth) plus its three contextual words (`from`, `as`, `by` - ordinary identifiers to the
// lexer, keywords only in the positions this list's caller already restricts them to). `protected` is a word only in
// front of `var` (`protected var count: Int`) and a name everywhere else, so `highlightCode` asks for the `var`.

const { startLanguageClient } = require('./lsp-client');
const { createToolchain } = require('./toolchain');
const { startTests } = require('./testing');
const { startDebugging } = require('./debugging');
const { registerFormatter } = require('./formatting');

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
    } else if (KEYWORDS.has(word) || (word === 'protected' && /^\s+var\b/.test(code.slice(last)))) {
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

// --- Semantic tokens: from `torb lsp`, and from `torb highlight --stdin` where the language server does not run ---
//
// The TextMate grammar in ./syntaxes is a heuristic; it cannot know whether a name is a field or a local, a case
// or a plain type, a method or a function - that needs the syntax tree, which only the `torb` binary has. While the
// language server of ./lsp-client.js runs, its semantic tokens answer: the same resolver as `torb highlight`,
// sharpened by the checker. Where it does not run (turned off, not found, crashed too often) this provider is the
// fallback: one `torb highlight` process per request, its stdout is one JSON document (`compiler/src/highlight/`
// documents the exact shape), and if the binary is missing, fails, or answers late, this provider gives VS Code no
// tokens at all - the TextMate grammar's colors stand on their own, and the only trace is one line in the
// "TorbScript" output channel, never a popup. See README.md for the settings and the color palette this feeds
// through `configurationDefaults`.

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

class TorbSemanticTokensProvider {
  /** `executable` answers the `torb` to run for each request (toolchain.js decides where it is). */
  constructor(vscode, cp, executable) {
    this.vscode = vscode;
    this.cp = cp;
    this.executable = executable;
    this.legend = new vscode.SemanticTokensLegend(TOKEN_TYPES, TOKEN_MODIFIERS);
    this.current = null; // The child process of the request still in flight, if any.
  }

  async provideDocumentSemanticTokens(document, cancellationToken) {
    const { vscode } = this;
    const enabled = vscode.workspace.getConfiguration('torbscript').get('semanticHighlighting.enabled', true);
    const builder = new vscode.SemanticTokensBuilder(this.legend);
    if (!enabled) {
      return builder.build();
    }
    // The language server's legend is this one, in the same order, so its integers are handed on as they are
    if (this.languageClient && this.languageClient.isRunning()) {
      const tokens = await this.languageClient.semanticTokens(document);
      if (tokens) {
        return tokens;
      }
    }
    return this.highlightTokens(document, cancellationToken, builder);
  }

  highlightTokens(document, cancellationToken, builder) {
    const { vscode } = this;
    this.killCurrent();
    const executable = this.executable();
    if (!executable) {
      return builder.build(); // No `torb` found: the TextMate grammar colors alone, and the status bar says why
    }
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

// --- Commands of the walkthrough: a new project, and running a file ---------------------------------------------------

/** Where `torb` is needed and missing: says so, with the installer one click away. Answers whether it is there. */
async function needToolchain(vscode, toolchain, what) {
  if (!toolchain.isFound()) {
    await toolchain.check();
  }
  if (toolchain.isFound()) {
    return true;
  }
  const answer = await vscode.window.showWarningMessage(`${what} needs the TorbScript toolchain.`, 'Install TorbScript');
  if (answer) {
    await vscode.commands.executeCommand('torbscript.installToolchain');
  }
  return false;
}

/** A package name as `torb new` and project.trb take it: lowercase letters, digits and `-`, starting with a letter. */
function packageNameProblem(name) {
  if (!name) {
    return 'The name of the package, which is also the name of its folder';
  }
  return /^[a-z][a-z0-9-]*$/.test(name) ? undefined : 'Lowercase letters, digits and `-`, starting with a letter: `hello-world`';
}

/**
 * "TorbScript: New Project...": asks for a folder and a name, runs `torb new <name>` there - `project.trb`,
 * `src/main.trb` and `tests/main.test.trb` - and opens the new folder, with `src/main.trb` in the editor once it has.
 */
async function newProject(context, vscode, cp, path, toolchain) {
  if (!(await needToolchain(vscode, toolchain, 'A new project'))) {
    return;
  }
  const parents = await vscode.window.showOpenDialog({
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    openLabel: 'Create the Project in This Folder',
    title: 'TorbScript: where the folder of the new project goes',
  });
  if (!parents || parents.length === 0) {
    return;
  }
  const name = await vscode.window.showInputBox({
    title: 'TorbScript: the name of the new package',
    prompt: 'A folder of this name is created, with project.trb, src/main.trb and tests/main.test.trb',
    value: 'hello',
    validateInput: packageNameProblem,
  });
  if (!name) {
    return;
  }
  const parent = parents[0].fsPath;
  const created = await new Promise((resolve) => {
    cp.execFile(toolchain.command(), ['new', name], { cwd: parent, windowsHide: true }, (error, stdout, stderr) => {
      log(vscode, `torb new ${name} in ${parent}: ${(stdout || '').trim()} ${(stderr || '').trim()}`.trim());
      resolve(error ? (stderr || error.message).trim() : null);
    });
  });
  if (created !== null) {
    vscode.window.showErrorMessage(`torb new ${name} failed: ${created}`);
    return;
  }
  const folder = vscode.Uri.file(path.join(parent, name));
  await context.globalState.update(OPEN_AFTER_START, path.join(folder.fsPath, 'src', 'main.trb'));
  const hasWorkspace = (vscode.workspace.workspaceFolders || []).length > 0;
  await vscode.commands.executeCommand('vscode.openFolder', folder, { forceNewWindow: hasWorkspace });
}

/** The file `newProject` asked to open, once the window of its folder has started. */
async function openPendingFile(context, vscode, path) {
  const pending = context.globalState.get(OPEN_AFTER_START);
  if (!pending) {
    return;
  }
  const inWorkspace = (vscode.workspace.workspaceFolders || []).some((folder) =>
    !path.relative(folder.uri.fsPath, pending).startsWith('..')
  );
  if (!inWorkspace) {
    return;
  }
  await context.globalState.update(OPEN_AFTER_START, undefined);
  try {
    await vscode.window.showTextDocument(vscode.Uri.file(pending));
  } catch (error) {
    log(vscode, `could not open ${pending}: ${error.message}`);
  }
}

/**
 * "TorbScript: Run File": saves the file and runs it with `torb run` in a terminal, from its workspace folder. The
 * problems the language server found are in the editor already; the ones `torb run` finds are in the terminal.
 */
async function runFile(vscode, path, toolchain, uri) {
  const editor = vscode.window.activeTextEditor;
  const target = uri instanceof vscode.Uri ? uri : editor && editor.document.languageId === 'trb' ? editor.document.uri : undefined;
  if (!target) {
    vscode.window.showInformationMessage('Open a .trb file to run it.');
    return;
  }
  if (!(await needToolchain(vscode, toolchain, 'Running a file'))) {
    return;
  }
  const document = vscode.workspace.textDocuments.find((each) => each.uri.toString() === target.toString());
  if (document && document.isDirty) {
    await document.save();
  }
  const folder = vscode.workspace.getWorkspaceFolder(target);
  const cwd = folder ? folder.uri.fsPath : path.dirname(target.fsPath);
  const file = path.relative(cwd, target.fsPath);
  const execution = new vscode.ProcessExecution(toolchain.command(), ['run', file], { cwd });
  const task = new vscode.Task(
    { type: 'torbscript', task: 'run', file },
    folder || vscode.TaskScope.Workspace,
    `run ${file}`,
    'TorbScript',
    execution,
    ['$torb']
  );
  task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Shared, clear: true };
  await vscode.tasks.executeTask(task);
}

/** The client of `torb lsp` while the extension is active, or `null` where the setting turns it off. */
let languageClient = null;

/** The walkthrough of package.json, as `workbench.action.openWalkthrough` names it: `<publisher>.<name>#<id>`. */
const WALKTHROUGH = 'torbscript.torbscript#torbscript.gettingStarted';
/** globalState: whether the walkthrough was opened once by itself, and a file to open once a new project's window starts. */
const WALKTHROUGH_SHOWN = 'torbscript.walkthroughShown';
const OPEN_AFTER_START = 'torbscript.openAfterStart';

async function activate(context) {
  const vscode = require('vscode');
  const cp = require('child_process');
  const fs = require('fs');
  const os = require('os');
  const path = require('path');
  const say = (message) => log(vscode, message);

  const toolchain = createToolchain(context, vscode, cp, fs, path, os, say);
  await toolchain.check();

  const provider = new TorbSemanticTokensProvider(vscode, cp, () => toolchain.executable);
  languageClient = startLanguageClient(context, vscode, cp, () => toolchain.command(), say, toolchain.isFound());
  provider.languageClient = languageClient;
  context.subscriptions.push(
    vscode.languages.registerDocumentSemanticTokensProvider(
      [
        { language: 'trb', scheme: 'file' },
        { language: 'trb', scheme: 'untitled' },
      ],
      provider,
      provider.legend
    )
  );
  if (!languageClient) {
    context.subscriptions.push(
      vscode.commands.registerCommand('torbscript.restartLanguageServer', () =>
        vscode.window.showInformationMessage('The language server is turned off (torbscript.languageServer.enabled).')
      )
    );
  }

  registerFormatter(context, vscode, cp, fs, os, path, toolchain, say, languageClient);
  const tests = startTests(context, vscode, cp, fs, path, toolchain, languageClient, say);
  startDebugging(context, vscode, path, toolchain, say);
  if (languageClient) {
    languageClient.onDidStart(() => tests.discoverAll(true));
  }
  // A `torb` that appears, or another one, starts the language server with it
  toolchain.onDidChange(() => {
    if (languageClient && toolchain.isFound()) {
      languageClient.restart();
    }
  });

  context.subscriptions.push(
    vscode.commands.registerCommand('torbscript.openWalkthrough', () =>
      vscode.commands.executeCommand('workbench.action.openWalkthrough', WALKTHROUGH, false)
    ),
    vscode.commands.registerCommand('torbscript.newProject', () => newProject(context, vscode, cp, path, toolchain)),
    vscode.commands.registerCommand('torbscript.runFile', (uri) => runFile(vscode, path, toolchain, uri))
  );

  await openPendingFile(context, vscode, path);
  if (!context.globalState.get(WALKTHROUGH_SHOWN)) {
    await context.globalState.update(WALKTHROUGH_SHOWN, true);
    await vscode.commands.executeCommand('torbscript.openWalkthrough');
  }
  if (!toolchain.isFound()) {
    toolchain.offerInstall();
  }

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

/** Ends the language server with `shutdown` and `exit`, which VS Code waits for. */
function deactivate() {
  return languageClient ? languageClient.stop() : undefined;
}

module.exports = { activate, deactivate, highlight, TorbSemanticTokensProvider, packageNameProblem, TOKEN_TYPES, TOKEN_MODIFIERS };
