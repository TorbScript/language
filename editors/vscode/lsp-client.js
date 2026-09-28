// The client of `torb lsp` (docs/tooling/torb-lsp.md): starts the language server, speaks the Language Server
// Protocol to it over its standard input and output, and hands VS Code what it answers - the diagnostics of the open
// documents and of the rest of the workspace, hover, go to definition, completion with the documentation of each item,
// signature help, the references, a checked rename, the symbols of a document and of the workspace, the formatting of
// `torb format` (formatting.js asks for it), semantic tokens and the quick fixes of the lint rules.
//
// It is written against the protocol directly rather than on `vscode-languageclient`, so the extension stays a folder
// of plain files with no `node_modules` and no build step (docs/design/LANGUAGE-SERVER.md, "The client"). It speaks
// exactly what `torb lsp` answers and nothing more: positions in UTF-16, which is what VS Code counts too, and
// incremental document changes.
//
// When the server cannot be started, or has crashed three times within three minutes, the client says so once in the
// "TorbScript" output channel and stops: the TextMate grammar and the `torb highlight` tokens of extension.js stand on
// their own.

'use strict';

/** Reads the framed messages of the base protocol out of the chunks of a stream. */
class MessageReader {
  constructor(onMessage) {
    this.buffer = Buffer.alloc(0);
    this.onMessage = onMessage;
  }

  add(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    for (;;) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) {
        return;
      }
      const header = this.buffer.slice(0, headerEnd).toString('ascii');
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        // A header without a length cannot be read past; drop it and look for the next one
        this.buffer = this.buffer.slice(headerEnd + 4);
        continue;
      }
      const length = parseInt(match[1], 10);
      const start = headerEnd + 4;
      if (this.buffer.length < start + length) {
        return;
      }
      const body = this.buffer.slice(start, start + length).toString('utf8');
      this.buffer = this.buffer.slice(start + length);
      let message;
      try {
        message = JSON.parse(body);
      } catch (error) {
        continue;
      }
      this.onMessage(message);
    }
  }
}

function frame(message) {
  const body = JSON.stringify(message);
  return `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`;
}

/** One running `torb lsp`, and the VS Code side of every feature it provides. */
class TorbLanguageClient {
  /** `executable` is the path of `torb`, or a function that answers it each time the server is started. */
  constructor(vscode, cp, executable, log) {
    this.vscode = vscode;
    this.cp = cp;
    this.resolveExecutable = typeof executable === 'function' ? executable : () => executable;
    this.executable = this.resolveExecutable();
    this.log = log;
    this.child = null;
    this.nextId = 1;
    this.pending = new Map();
    this.running = false;
    this.stopping = false;
    this.crashes = [];
    this.stopped = null;
    this.capabilities = {};
    this.startListeners = [];
    this.diagnostics = vscode.languages.createDiagnosticCollection('torb');
  }

  /** Whether the server is up and has answered `initialize`. */
  isRunning() {
    return this.running;
  }

  /** Calls `listener()` every time the server has started and answered `initialize`. */
  onDidStart(listener) {
    this.startListeners.push(listener);
  }

  /** Whether the server answers `torbscript/tests` (`capabilities.experimental.tests`). */
  supportsTests() {
    return Boolean(this.capabilities.experimental && this.capabilities.experimental.tests);
  }

  /** Starts the server and says which documents are open. Resolves to whether it runs. */
  async start() {
    const { vscode } = this;
    this.stopping = false;
    this.stopped = null;
    this.executable = this.resolveExecutable();
    const folder = (vscode.workspace.workspaceFolders || [])[0];
    const settings = vscode.workspace.getConfiguration('torbscript');
    let child;
    try {
      child = this.cp.spawn(this.executable, ['lsp'], {
        cwd: folder ? folder.uri.fsPath : undefined,
        stdio: ['pipe', 'pipe', 'pipe'],
      });
    } catch (error) {
      this.log(`could not start "${this.executable} lsp": ${error.message}`);
      return false;
    }
    this.child = child;
    const reader = new MessageReader((message) => this.receive(message));
    child.stdout.on('data', (chunk) => reader.add(chunk));
    child.stderr.on('data', (chunk) => {
      for (const line of chunk.toString('utf8').split(/\r?\n/)) {
        if (line.trim()) {
          this.log(line);
        }
      }
    });
    child.stdin.on('error', () => {});
    const exited = new Promise((resolve) => {
      child.on('error', (error) => {
        this.log(`"${this.executable} lsp" failed: ${error.message}`);
        resolve(false);
      });
      child.on('exit', (code) => {
        this.exited(child, code);
        resolve(false);
      });
    });
    const initialized = this.request('initialize', {
      processId: process.pid,
      clientInfo: { name: 'vscode', version: vscode.version },
      rootUri: folder ? folder.uri.toString() : null,
      workspaceFolders: (vscode.workspace.workspaceFolders || []).map((each) => ({
        uri: each.uri.toString(),
        name: each.name,
      })),
      capabilities: {
        general: { positionEncodings: ['utf-16'] },
        textDocument: {
          synchronization: { didSave: false },
          hover: { contentFormat: ['markdown'] },
          completion: {
            completionItem: {
              snippetSupport: false,
              documentationFormat: ['markdown'],
              resolveSupport: { properties: ['detail', 'documentation'] },
            },
          },
          signatureHelp: {
            signatureInformation: { documentationFormat: ['markdown'], parameterInformation: { labelOffsetSupport: true } },
          },
          references: {},
          rename: { prepareSupport: true },
          documentSymbol: { hierarchicalDocumentSymbolSupport: true },
          formatting: {},
          rangeFormatting: {},
          publishDiagnostics: { tagSupport: { valueSet: [1] } },
          semanticTokens: { requests: { full: true }, formats: ['relative'] },
          codeAction: { codeActionLiteralSupport: { codeActionKind: { valueSet: ['quickfix'] } } },
        },
        workspace: {
          symbol: {},
          workspaceEdit: { documentChanges: true },
          didChangeWatchedFiles: {},
          didChangeConfiguration: {},
          workspaceFolders: true,
        },
      },
      initializationOptions: workspaceSettingsOf(settings),
    }).then(
      (result) => {
        this.capabilities = (result && result.capabilities) || {};
        return true;
      },
      () => false,
    );
    const started = await Promise.race([initialized, exited]);
    if (!started) {
      return false;
    }
    this.notify('initialized', {});
    this.running = true;
    for (const document of vscode.workspace.textDocuments) {
      this.opened(document);
    }
    for (const listener of this.startListeners) {
      try {
        listener();
      } catch (error) {
        this.log(`a listener of the language server's start failed: ${error.message}`);
      }
    }
    return true;
  }

  /** Stops the server where it runs and starts it again, with the crashes forgotten: a newly built or found `torb`. */
  async restart() {
    if (this.child) {
      await this.stop();
    }
    this.crashes = [];
    return this.start();
  }

  /** `shutdown`, then `exit`; the process is killed if it has not gone after two seconds. Once, however often asked. */
  stop() {
    if (!this.stopped) {
      this.stopped = this.shutDown();
    }
    return this.stopped;
  }

  async shutDown() {
    this.stopping = true;
    if (!this.child) {
      return;
    }
    const child = this.child;
    if (this.running) {
      const answered = this.request('shutdown', null).catch(() => undefined);
      await Promise.race([answered, new Promise((resolve) => setTimeout(resolve, 2000))]);
      this.notify('exit', null);
    }
    this.running = false;
    setTimeout(() => {
      try {
        child.kill();
      } catch (error) {
        // Already gone
      }
    }, 2000);
    this.clearDiagnostics();
  }

  /** Clears the diagnostics - unless VS Code disposed the collection already, as it does before a window closes. */
  clearDiagnostics() {
    try {
      this.diagnostics.clear();
    } catch (error) {
      // Disposed: nothing to clear
    }
  }

  exited(child, code) {
    if (this.child !== child) {
      return;
    }
    this.child = null;
    this.running = false;
    for (const [, waiting] of this.pending) {
      waiting.reject(new Error('the language server exited'));
    }
    this.pending.clear();
    this.clearDiagnostics();
    if (this.stopping) {
      return;
    }
    const now = Date.now();
    this.crashes = this.crashes.filter((at) => now - at < 180000);
    this.crashes.push(now);
    if (this.crashes.length > 3) {
      this.log(`"${this.executable} lsp" exited with ${code} three times within three minutes; it is not started again`);
      return;
    }
    this.log(`"${this.executable} lsp" exited with ${code}; starting it again`);
    this.start();
  }

  // --- Messages ----------------------------------------------------------------------------------------------------

  request(method, params) {
    if (!this.child) {
      return Promise.reject(new Error('the language server is not running'));
    }
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.send({ jsonrpc: '2.0', id, method, params });
    });
  }

  notify(method, params) {
    if (this.child) {
      this.send({ jsonrpc: '2.0', method, params });
    }
  }

  send(message) {
    try {
      this.child.stdin.write(frame(message));
    } catch (error) {
      // The process is gone; `exit` says so
    }
  }

  receive(message) {
    if (message.id !== undefined && message.method === undefined) {
      const waiting = this.pending.get(message.id);
      if (!waiting) {
        return;
      }
      this.pending.delete(message.id);
      if (message.error) {
        waiting.reject(new Error(message.error.message));
      } else {
        waiting.resolve(message.result);
      }
      return;
    }
    if (message.method === 'textDocument/publishDiagnostics') {
      this.published(message.params);
    } else if (message.method === 'window/logMessage' && message.params) {
      this.log(message.params.message);
    }
  }

  // --- Documents ---------------------------------------------------------------------------------------------------

  isOurs(document) {
    return document.languageId === 'trb' && document.uri.scheme === 'file';
  }

  opened(document) {
    if (!this.running || !this.isOurs(document)) {
      return;
    }
    this.notify('textDocument/didOpen', {
      textDocument: {
        uri: document.uri.toString(),
        languageId: 'trb',
        version: document.version,
        text: document.getText(),
      },
    });
  }

  changed(event) {
    if (!this.running || !this.isOurs(event.document) || event.contentChanges.length === 0) {
      return;
    }
    this.notify('textDocument/didChange', {
      textDocument: { uri: event.document.uri.toString(), version: event.document.version },
      contentChanges: event.contentChanges.map((change) => ({
        range: this.toRange(change.range),
        text: change.text,
      })),
    });
  }

  closed(document) {
    if (!this.running || !this.isOurs(document)) {
      return;
    }
    this.notify('textDocument/didClose', { textDocument: { uri: document.uri.toString() } });
  }

  watched(uri, type) {
    this.notify('workspace/didChangeWatchedFiles', { changes: [{ uri: uri.toString(), type }] });
  }

  /** Tells the server what the settings of the check of the workspace are now. */
  configured() {
    const settings = this.vscode.workspace.getConfiguration('torbscript');
    this.notify('workspace/didChangeConfiguration', {
      settings: { torbscript: { languageServer: workspaceSettingsOf(settings) } },
    });
  }

  published(params) {
    const { vscode } = this;
    const uri = vscode.Uri.parse(params.uri);
    this.diagnostics.set(uri, (params.diagnostics || []).map((each) => this.fromDiagnostic(each)));
  }

  // --- Features ----------------------------------------------------------------------------------------------------

  async hover(document, position) {
    const result = await this.ask('textDocument/hover', document, position);
    if (!result || !result.contents) {
      return undefined;
    }
    const contents = new this.vscode.MarkdownString(result.contents.value || '');
    return new this.vscode.Hover(contents, result.range ? this.fromRange(result.range) : undefined);
  }

  async definition(document, position) {
    const result = await this.ask('textDocument/definition', document, position);
    if (!result) {
      return undefined;
    }
    const locations = Array.isArray(result) ? result : [result];
    return locations.map((each) => new this.vscode.Location(this.vscode.Uri.parse(each.uri), this.fromRange(each.range)));
  }

  async completion(document, position) {
    const result = await this.ask('textDocument/completion', document, position);
    if (!result) {
      return undefined;
    }
    const items = Array.isArray(result) ? result : result.items || [];
    return new this.vscode.CompletionList(
      items.map((each) => {
        // The protocol counts kinds from 1, VS Code's enum from 0
        const item = new this.vscode.CompletionItem(each.label, (each.kind || 1) - 1);
        if (each.detail) {
          item.detail = each.detail;
        }
        if (each.sortText) {
          item.sortText = each.sortText;
        }
        // What `completionItem/resolve` hands back: the item as the server wrote it, with the place of its declaration
        item.torbItem = each;
        return item;
      }),
      Boolean(result.isIncomplete),
    );
  }

  /** The signature and the documentation of the declaration of an item, once VS Code shows it. */
  async resolveCompletion(item) {
    if (!this.running || !item.torbItem || !item.torbItem.data) {
      return item;
    }
    try {
      const resolved = await this.request('completionItem/resolve', item.torbItem);
      if (resolved && resolved.detail) {
        item.detail = resolved.detail;
      }
      if (resolved && resolved.documentation) {
        item.documentation = new this.vscode.MarkdownString(resolved.documentation.value || '');
      }
    } catch (error) {
      // Nothing more to say than the item already does
    }
    return item;
  }

  async signatureHelp(document, position) {
    const result = await this.ask('textDocument/signatureHelp', document, position);
    if (!result || !Array.isArray(result.signatures) || result.signatures.length === 0) {
      return undefined;
    }
    const { vscode } = this;
    const help = new vscode.SignatureHelp();
    help.signatures = result.signatures.map((each) => {
      const documentation = each.documentation ? new vscode.MarkdownString(each.documentation.value || '') : undefined;
      const information = new vscode.SignatureInformation(each.label, documentation);
      information.parameters = (each.parameters || []).map((parameter) => new vscode.ParameterInformation(parameter.label));
      return information;
    });
    help.activeSignature = result.activeSignature || 0;
    help.activeParameter = result.activeParameter || 0;
    return help;
  }

  async references(document, position, context) {
    if (!this.running) {
      return undefined;
    }
    try {
      const result = await this.request('textDocument/references', {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
        context: { includeDeclaration: Boolean(context && context.includeDeclaration) },
      });
      return (result || []).map((each) => new this.vscode.Location(this.vscode.Uri.parse(each.uri), this.fromRange(each.range)));
    } catch (error) {
      return undefined;
    }
  }

  /** The range and the name a rename starts from; a refusal of the server is thrown, and VS Code shows its reason. */
  async prepareRename(document, position) {
    if (!this.running) {
      throw new Error('the language server is not running');
    }
    const result = await this.request('textDocument/prepareRename', {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!result) {
      throw new Error('there is nothing to rename here');
    }
    return { range: this.fromRange(result.range), placeholder: result.placeholder };
  }

  /** The edits of a checked rename; a refusal of the server - a rename that would change meaning - is thrown. */
  async rename(document, position, newName) {
    if (!this.running) {
      throw new Error('the language server is not running');
    }
    const result = await this.request('textDocument/rename', {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character },
      newName,
    });
    return result ? this.fromWorkspaceEdit(result) : undefined;
  }

  async documentSymbols(document) {
    if (!this.running) {
      return undefined;
    }
    try {
      const result = await this.request('textDocument/documentSymbol', { textDocument: { uri: document.uri.toString() } });
      return (result || []).map((each) => this.fromDocumentSymbol(each, document));
    } catch (error) {
      return undefined;
    }
  }

  async workspaceSymbols(query) {
    if (!this.running) {
      return undefined;
    }
    try {
      const result = await this.request('workspace/symbol', { query });
      const { vscode } = this;
      return (result || []).map(
        (each) =>
          new vscode.SymbolInformation(
            each.name,
            (each.kind || 1) - 1,
            each.containerName || '',
            new vscode.Location(vscode.Uri.parse(each.location.uri), this.fromRange(each.location.range)),
          ),
      );
    } catch (error) {
      return undefined;
    }
  }

  /**
   * The edits of `torb format` for a document, or for the lines of a range of it; `undefined` where the server does not
   * run or does not answer, and formatting.js formats a copy of the text itself.
   */
  async formatting(document, range) {
    if (!this.running || document.uri.scheme !== 'file') {
      return undefined;
    }
    try {
      const textDocument = { uri: document.uri.toString() };
      const options = { tabSize: 2, insertSpaces: true };
      const result = range
        ? await this.request('textDocument/rangeFormatting', { textDocument, range: this.toRange(range), options })
        : await this.request('textDocument/formatting', { textDocument, options });
      if (result === null) {
        // The text does not parse: nothing to do, which is what the fallback would find too
        return [];
      }
      return (result || []).map((each) => this.vscode.TextEdit.replace(this.fromRange(each.range), each.newText));
    } catch (error) {
      return undefined;
    }
  }

  async semanticTokens(document) {
    if (!this.running) {
      return undefined;
    }
    try {
      const result = await this.request('textDocument/semanticTokens/full', {
        textDocument: { uri: document.uri.toString() },
      });
      if (!result || !Array.isArray(result.data)) {
        return undefined;
      }
      return new this.vscode.SemanticTokens(Uint32Array.from(result.data));
    } catch (error) {
      return undefined;
    }
  }

  async codeActions(document, range) {
    if (!this.running) {
      return undefined;
    }
    let result;
    try {
      result = await this.request('textDocument/codeAction', {
        textDocument: { uri: document.uri.toString() },
        range: this.toRange(range),
        context: { diagnostics: [] },
      });
    } catch (error) {
      return undefined;
    }
    const { vscode } = this;
    return (result || []).map((each) => {
      const action = new vscode.CodeAction(each.title, vscode.CodeActionKind.QuickFix);
      action.isPreferred = Boolean(each.isPreferred);
      action.diagnostics = (each.diagnostics || []).map((diagnostic) => this.fromDiagnostic(diagnostic));
      if (each.edit && each.edit.changes) {
        const edit = new vscode.WorkspaceEdit();
        for (const uri of Object.keys(each.edit.changes)) {
          for (const change of each.edit.changes[uri]) {
            edit.replace(vscode.Uri.parse(uri), this.fromRange(change.range), change.newText);
          }
        }
        action.edit = edit;
      }
      return action;
    });
  }

  /**
   * The groups and tests of a `*.test.trb` file (`torbscript/tests`, docs/tooling/torb-lsp.md): nodes of `kind`, `name`,
   * `range`, `selectionRange` and `children`, in the order of the source. `undefined` where the server does not run or
   * does not answer the request.
   */
  async tests(uri) {
    if (!this.running || !this.supportsTests()) {
      return undefined;
    }
    try {
      const result = await this.request('torbscript/tests', { textDocument: { uri: uri.toString() } });
      return Array.isArray(result) ? result : undefined;
    } catch (error) {
      return undefined;
    }
  }

  /** A request about a position; `undefined` where the server is not running or failed to answer. */
  async ask(method, document, position) {
    if (!this.running) {
      return undefined;
    }
    try {
      return await this.request(method, {
        textDocument: { uri: document.uri.toString() },
        position: { line: position.line, character: position.character },
      });
    } catch (error) {
      return undefined;
    }
  }

  // --- Conversions -------------------------------------------------------------------------------------------------

  toRange(range) {
    return {
      start: { line: range.start.line, character: range.start.character },
      end: { line: range.end.line, character: range.end.character },
    };
  }

  fromRange(range) {
    return new this.vscode.Range(range.start.line, range.start.character, range.end.line, range.end.character);
  }

  fromWorkspaceEdit(result) {
    const { vscode } = this;
    const edit = new vscode.WorkspaceEdit();
    for (const change of result.documentChanges || []) {
      const uri = vscode.Uri.parse(change.textDocument.uri);
      for (const each of change.edits || []) {
        edit.replace(uri, this.fromRange(each.range), each.newText);
      }
    }
    for (const uri of Object.keys(result.changes || {})) {
      for (const each of result.changes[uri]) {
        edit.replace(vscode.Uri.parse(uri), this.fromRange(each.range), each.newText);
      }
    }
    return edit;
  }

  fromDocumentSymbol(symbol, document) {
    const { vscode } = this;
    const found = new vscode.DocumentSymbol(
      symbol.name,
      symbol.detail || '',
      (symbol.kind || 1) - 1,
      this.fromRange(symbol.range),
      this.fromRange(symbol.selectionRange),
    );
    found.children = (symbol.children || []).map((each) => this.fromDocumentSymbol(each, document));
    return found;
  }

  fromDiagnostic(diagnostic) {
    const { vscode } = this;
    const severities = [
      vscode.DiagnosticSeverity.Error,
      vscode.DiagnosticSeverity.Warning,
      vscode.DiagnosticSeverity.Information,
      vscode.DiagnosticSeverity.Hint,
    ];
    const result = new vscode.Diagnostic(
      this.fromRange(diagnostic.range),
      diagnostic.message,
      severities[(diagnostic.severity || 1) - 1] || vscode.DiagnosticSeverity.Error,
    );
    if (diagnostic.source) {
      result.source = diagnostic.source;
    }
    if (diagnostic.code !== undefined) {
      result.code = diagnostic.code;
    }
    if (Array.isArray(diagnostic.tags) && diagnostic.tags.includes(1)) {
      result.tags = [vscode.DiagnosticTag.Unnecessary];
    }
    return result;
  }
}

/**
 * Registers every feature with VS Code and starts the client, unless `startNow` is false (no `torb` found yet: the
 * extension calls `restart()` once there is one). `executable` is a path or a function that answers one at each start.
 * Answers the client, or `null` where the setting turns the language server off.
 */
function startLanguageClient(context, vscode, cp, executable, log, startNow = true) {
  const settings = vscode.workspace.getConfiguration('torbscript');
  if (!settings.get('languageServer.enabled', true)) {
    return null;
  }
  const client = new TorbLanguageClient(vscode, cp, executable, log);
  const selector = { language: 'trb', scheme: 'file' };
  context.subscriptions.push(
    client.diagnostics,
    vscode.workspace.onDidOpenTextDocument((document) => client.opened(document)),
    vscode.workspace.onDidChangeTextDocument((event) => client.changed(event)),
    vscode.workspace.onDidCloseTextDocument((document) => client.closed(document)),
    vscode.languages.registerHoverProvider(selector, { provideHover: (document, position) => client.hover(document, position) }),
    vscode.languages.registerDefinitionProvider(selector, {
      provideDefinition: (document, position) => client.definition(document, position),
    }),
    vscode.languages.registerCompletionItemProvider(
      selector,
      {
        provideCompletionItems: (document, position) => client.completion(document, position),
        resolveCompletionItem: (item) => client.resolveCompletion(item),
      },
      '.',
    ),
    vscode.languages.registerSignatureHelpProvider(
      selector,
      { provideSignatureHelp: (document, position) => client.signatureHelp(document, position) },
      { triggerCharacters: ['(', ','], retriggerCharacters: [')'] },
    ),
    vscode.languages.registerReferenceProvider(selector, {
      provideReferences: (document, position, context) => client.references(document, position, context),
    }),
    vscode.languages.registerRenameProvider(selector, {
      prepareRename: (document, position) => client.prepareRename(document, position),
      provideRenameEdits: (document, position, newName) => client.rename(document, position, newName),
    }),
    vscode.languages.registerDocumentSymbolProvider(selector, {
      provideDocumentSymbols: (document) => client.documentSymbols(document),
    }),
    vscode.languages.registerWorkspaceSymbolProvider({
      provideWorkspaceSymbols: (query) => client.workspaceSymbols(query),
    }),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (
        event.affectsConfiguration('torbscript.languageServer.workspaceDiagnostics') ||
        event.affectsConfiguration('torbscript.languageServer.workspaceDiagnosticsDelay')
      ) {
        client.configured();
      }
    }),
    // Every folder is a project of the server's own, which it reads when it starts
    vscode.workspace.onDidChangeWorkspaceFolders(() => {
      if (client.isRunning()) {
        client.restart();
      }
    }),
    vscode.languages.registerCodeActionsProvider(
      selector,
      { provideCodeActions: (document, range) => client.codeActions(document, range) },
      { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] },
    ),
    vscode.commands.registerCommand('torbscript.restartLanguageServer', () => client.restart()),
    { dispose: () => client.stop() },
  );
  const watcher = vscode.workspace.createFileSystemWatcher('**/*.trb');
  context.subscriptions.push(
    watcher,
    watcher.onDidCreate((uri) => client.watched(uri, 1)),
    watcher.onDidChange((uri) => client.watched(uri, 2)),
    watcher.onDidDelete((uri) => client.watched(uri, 3)),
  );
  if (startNow) {
    client.start().then((started) => {
      if (!started) {
        log(`the language server did not start; the grammar and \`torb highlight\` color the code`);
      }
    });
  }
  return client;
}

/**
 * What the settings say about the check of the workspace, as the server reads it: in the `initializationOptions` of
 * `initialize`, and below `torbscript.languageServer` in `workspace/didChangeConfiguration`.
 */
function workspaceSettingsOf(settings) {
  return {
    workspaceDiagnostics: settings.get('languageServer.workspaceDiagnostics', true),
    workspaceDiagnosticsDelay: settings.get('languageServer.workspaceDiagnosticsDelay', 1000),
  };
}

module.exports = { startLanguageClient, TorbLanguageClient, MessageReader, frame, workspaceSettingsOf };
