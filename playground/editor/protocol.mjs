/*
 * protocol.mjs - the half of the playground's bridge to `torb lsp` that knows nothing of Monaco or of the page: a
 * JSON-RPC client over any transport of text messages, the documents it keeps in step with the server, and the
 * translation of the protocol's answers into the plain objects Monaco's providers return. editor.mjs registers the
 * providers and hands them these; smoke-test.mjs drives the same client against playground-worker.js under node.
 *
 *   new LanguageClient(transport, { timeout })   transport: { send(text), subscribe(handler) }, a message a call
 *   client.request(method, params, token?)  ->  Promise of the result (null where it was cancelled)
 *   client.open(uri, text) / change(uri, text) / close(uri)
 *   client.restart()                   the server was replaced: initialize it again and open every document anew
 *
 * Positions need no conversion but the base: the server speaks UTF-16 (`positionEncoding`), as a JavaScript string and
 * Monaco's columns do, so a position is only moved from 0-based to 1-based. Monaco's enums are numbers in its API
 * (monaco.d.ts); the few this file returns are written here as those numbers, so the file runs without Monaco.
 */

/** The root of the language worker's workspace, where every document of a page is. */
export const workspaceRoot = "file:///torb/work";

/** How long a request may wait for its answer, unless `options.timeout` says otherwise: a rename checks twice. */
const requestTimeout = 20000;

/** How long a change waits for the next keystroke before it is sent: a request sends what waits at once. */
const changeDelay = 250;

// ------------------------------------------------------------------------------------------------------ the client --

/**
 * The client of one language server. Requests wait for `initialize` to be answered, and every change that waits is
 * sent before a request, so the server always answers about the text the editor shows.
 */
export class LanguageClient {
  constructor(transport, options) {
    this.transport = transport;
    this.options = options || {};
    this.nextId = 0;
    this.pending = new Map();
    this.handlers = new Map();
    /** The open documents by URI: `{ uri, text, version, sent }`, `sent` false while a change waits */
    this.documents = new Map();
    this.changeTimer = null;
    this.capabilities = null;
    this.ready = null;
    this.receive = (message) => this.received(message);
    transport.subscribe(this.receive);
  }

  /** Initializes the server; answers its capabilities once it is ready. */
  start() {
    this.ready = this.call("initialize", {
      processId: null,
      rootUri: workspaceRoot,
      workspaceFolders: null,
      capabilities: clientCapabilities,
      clientInfo: { name: "torb.dev playground" },
    }).then((result) => {
      this.capabilities = (result && result.capabilities) || {};
      this.send({ jsonrpc: "2.0", method: "initialized", params: {} });
      for (const document of this.documents.values()) {
        this.sendOpen(document);
      }
      return this.capabilities;
    });
    return this.ready;
  }

  /**
   * The server was replaced - the old one ended - and knows nothing: what waited for the old one fails, and the new one
   * is initialized and told every open document as it is now.
   */
  restart() {
    for (const waiting of this.pending.values()) {
      clearTimeout(waiting.timer);
      waiting.reject(new Error("the language server was restarted"));
    }
    this.pending.clear();
    this.capabilities = null;
    return this.start();
  }

  /** A request; `token` is Monaco's cancellation token, and a cancelled request answers `null`. */
  request(method, params, token) {
    if (this.ready === null) {
      this.start();
    }
    return this.ready.then(() => {
      if (token && token.isCancellationRequested) {
        return null;
      }
      this.flush();
      return this.call(method, params, token);
    });
  }

  notify(method, params) {
    const send = () => this.send({ jsonrpc: "2.0", method: method, params: params });
    if (this.capabilities !== null) {
      send();
    } else if (this.ready !== null) {
      this.ready.then(send, () => {});
    }
  }

  /** Calls `handler(params)` for every notification of `method`; answers the function that stops it. */
  onNotification(method, handler) {
    if (!this.handlers.has(method)) {
      this.handlers.set(method, []);
    }
    this.handlers.get(method).push(handler);
    return () => {
      const list = this.handlers.get(method);
      list.splice(list.indexOf(handler), 1);
    };
  }

  /** Opens a document with its text: the server checks it and publishes its diagnostics. */
  open(uri, text) {
    if (this.documents.has(uri)) {
      this.change(uri, text);
      return;
    }
    const document = { uri: uri, text: text, version: 1, sent: true };
    this.documents.set(uri, document);
    if (this.ready === null) {
      this.start();
    } else if (this.capabilities !== null) {
      this.sendOpen(document);
    }
  }

  isOpen(uri) {
    return this.documents.has(uri);
  }

  /** The document's whole new text, sent once the typing pauses or a request needs it. */
  change(uri, text) {
    const document = this.documents.get(uri);
    if (document === undefined || document.text === text) {
      return;
    }
    document.text = text;
    document.version += 1;
    document.sent = false;
    clearTimeout(this.changeTimer);
    this.changeTimer = setTimeout(() => this.flush(), changeDelay);
  }

  close(uri) {
    const document = this.documents.get(uri);
    if (document === undefined) {
      return;
    }
    this.documents.delete(uri);
    if (this.capabilities !== null) {
      this.send({ jsonrpc: "2.0", method: "textDocument/didClose", params: { textDocument: { uri: uri } } });
    }
  }

  /** Sends every change that waits. */
  flush() {
    clearTimeout(this.changeTimer);
    this.changeTimer = null;
    if (this.capabilities === null) {
      return;
    }
    for (const document of this.documents.values()) {
      if (!document.sent) {
        document.sent = true;
        // The whole text: the server takes a change without a range as the new text (textDocumentSync 2 allows it),
        // and a program of the playground is a few kilobytes
        this.send({
          jsonrpc: "2.0",
          method: "textDocument/didChange",
          params: { textDocument: { uri: document.uri, version: document.version }, contentChanges: [{ text: document.text }] },
        });
      }
    }
  }

  sendOpen(document) {
    document.sent = true;
    this.send({
      jsonrpc: "2.0",
      method: "textDocument/didOpen",
      params: { textDocument: { uri: document.uri, languageId: "torbscript", version: document.version, text: document.text } },
    });
  }

  call(method, params, token) {
    this.nextId += 1;
    const id = this.nextId;
    const timeout = this.options.timeout || requestTimeout;
    return new Promise((resolve, reject) => {
      const waiting = { resolve: resolve, reject: reject, timer: null };
      waiting.timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method}: no answer within ${timeout / 1000} s`));
      }, timeout);
      this.pending.set(id, waiting);
      if (token && typeof token.onCancellationRequested === "function") {
        token.onCancellationRequested(() => {
          if (this.pending.has(id)) {
            this.pending.delete(id);
            clearTimeout(waiting.timer);
            this.send({ jsonrpc: "2.0", method: "$/cancelRequest", params: { id: id } });
            resolve(null);
          }
        });
      }
      this.send({ jsonrpc: "2.0", id: id, method: method, params: params });
    });
  }

  send(message) {
    this.transport.send(JSON.stringify(message));
  }

  received(text) {
    let message;
    try {
      message = typeof text === "string" ? JSON.parse(text) : text;
    } catch (problem) {
      return;
    }
    if (message === null || typeof message !== "object") {
      return;
    }
    if ("id" in message && !("method" in message)) {
      const waiting = this.pending.get(message.id);
      if (waiting === undefined) {
        return;
      }
      this.pending.delete(message.id);
      clearTimeout(waiting.timer);
      if (message.error) {
        waiting.reject(new Error(message.error.message || "the language server answered an error"));
      } else {
        waiting.resolve(message.result === undefined ? null : message.result);
      }
      return;
    }
    if ("id" in message) {
      // A request of the server's: this client offers nothing to ask for, so every one is answered empty
      this.send({ jsonrpc: "2.0", id: message.id, result: null });
      return;
    }
    for (const handler of (this.handlers.get(message.method) || []).slice()) {
      handler(message.params);
    }
  }
}

/** What the client tells the server it understands. */
const clientCapabilities = {
  general: { positionEncodings: ["utf-16"] },
  textDocument: {
    synchronization: { didSave: false, dynamicRegistration: false },
    completion: {
      completionItem: {
        snippetSupport: true,
        documentationFormat: ["markdown", "plaintext"],
        deprecatedSupport: true,
        tagSupport: { valueSet: [1] },
        insertReplaceSupport: true,
        resolveSupport: { properties: ["documentation", "detail"] },
        labelDetailsSupport: true,
      },
      contextSupport: true,
    },
    hover: { contentFormat: ["markdown", "plaintext"] },
    signatureHelp: {
      signatureInformation: {
        documentationFormat: ["markdown", "plaintext"],
        parameterInformation: { labelOffsetSupport: true },
        activeParameterSupport: true,
      },
      contextSupport: true,
    },
    definition: { linkSupport: true },
    rename: { prepareSupport: true },
    formatting: {},
    codeAction: {
      codeActionLiteralSupport: { codeActionKind: { valueSet: ["quickfix"] } },
      isPreferredSupport: true,
    },
    semanticTokens: {
      requests: { full: true },
      tokenTypes: [],
      tokenModifiers: [],
      formats: ["relative"],
    },
    publishDiagnostics: { relatedInformation: false, tagSupport: { valueSet: [1, 2] }, versionSupport: true },
  },
};

// ------------------------------------------------------------------------------------------------- the positions --

/** Monaco's position, `{ lineNumber, column }` from 1, as the protocol's `{ line, character }` from 0. */
export function toPosition(position) {
  return { line: position.lineNumber - 1, character: position.column - 1 };
}

/** The protocol's range as Monaco's. */
export function fromRange(range) {
  return {
    startLineNumber: range.start.line + 1,
    startColumn: range.start.character + 1,
    endLineNumber: range.end.line + 1,
    endColumn: range.end.character + 1,
  };
}

/** Monaco's range as the protocol's. */
export function toRange(range) {
  return {
    start: { line: range.startLineNumber - 1, character: range.startColumn - 1 },
    end: { line: range.endLineNumber - 1, character: range.endColumn - 1 },
  };
}

// --------------------------------------------------------------------------------------------- the documentation --

/**
 * Hover contents, a documentation or a parameter's, as Monaco's Markdown strings. The server writes Markdown with
 * ` ```trb ` blocks, which Monaco colours with the grammar of the language; plain text is escaped so it stays text.
 * HTML in a doc comment stays text (`supportHtml` false), and no link runs a command (`isTrusted` false).
 */
export function markdownOf(contents) {
  if (contents === null || contents === undefined) {
    return [];
  }
  if (Array.isArray(contents)) {
    return contents.flatMap(markdownOf);
  }
  if (typeof contents === "string") {
    return contents ? [markdown(contents)] : [];
  }
  if (typeof contents.kind === "string") {
    const value = contents.kind === "markdown" ? contents.value : escapeMarkdown(contents.value || "");
    return value ? [markdown(value)] : [];
  }
  if (typeof contents.language === "string") {
    return [markdown("```" + contents.language + "\n" + contents.value + "\n```")];
  }
  return [];
}

function markdown(value) {
  return { value: value, isTrusted: false, supportHtml: false };
}

/** Plain text that Markdown shows as it is. */
export function escapeMarkdown(text) {
  return text.replace(/[\\`*_{}[\]()#+\-.!|<>]/g, "\\$&");
}

// ------------------------------------------------------------------------------------------------- the completion --

/** LSP's CompletionItemKind (1-25) as Monaco's `languages.CompletionItemKind`. */
const completionKinds = {
  1: 18, // Text
  2: 0, // Method
  3: 1, // Function
  4: 2, // Constructor
  5: 3, // Field
  6: 4, // Variable
  7: 5, // Class
  8: 7, // Interface
  9: 8, // Module
  10: 9, // Property
  11: 12, // Unit
  12: 13, // Value
  13: 15, // Enum
  14: 17, // Keyword
  15: 27, // Snippet
  16: 19, // Color
  17: 20, // File
  18: 21, // Reference
  19: 23, // Folder
  20: 16, // EnumMember
  21: 14, // Constant
  22: 6, // Struct
  23: 10, // Event
  24: 11, // Operator
  25: 24, // TypeParameter
};

/** Monaco's `CompletionItemInsertTextRule.InsertAsSnippet`, and `CompletionItemTag.Deprecated`. */
const insertAsSnippet = 4;
const deprecatedTag = 1;

/**
 * The server's completion items as Monaco's suggestions. `wordRange` is `{ insert, replace }`, the ranges Monaco
 * replaces where an item names none: up to the cursor, and the whole word. Each suggestion keeps its item, which
 * [resolveCompletionItem] hands back to the server for its documentation.
 */
export function completionsOf(result, wordRange) {
  const items = result === null || result === undefined ? [] : Array.isArray(result) ? result : result.items || [];
  return {
    incomplete: Boolean(result && !Array.isArray(result) && result.isIncomplete),
    suggestions: items.map((item) => suggestionOf(item, wordRange)),
  };
}

export function suggestionOf(item, wordRange) {
  let insertText = item.insertText !== undefined ? item.insertText : item.label;
  let range = wordRange;
  if (item.textEdit) {
    insertText = item.textEdit.newText;
    range = item.textEdit.range
      ? fromRange(item.textEdit.range)
      : { insert: fromRange(item.textEdit.insert), replace: fromRange(item.textEdit.replace) };
  }
  const suggestion = {
    label: item.labelDetails
      ? { label: item.label, detail: item.labelDetails.detail, description: item.labelDetails.description }
      : item.label,
    kind: completionKinds[item.kind] !== undefined ? completionKinds[item.kind] : 18,
    insertText: insertText,
    range: range,
    item: item,
  };
  if (item.insertTextFormat === 2) {
    suggestion.insertTextRules = insertAsSnippet;
  }
  if (item.detail) {
    suggestion.detail = item.detail;
  }
  if (item.documentation) {
    suggestion.documentation = markdownOf(item.documentation)[0];
  }
  if (item.sortText) {
    suggestion.sortText = item.sortText;
  }
  if (item.filterText) {
    suggestion.filterText = item.filterText;
  }
  if (item.preselect) {
    suggestion.preselect = true;
  }
  if (item.deprecated || (item.tags && item.tags.includes(1))) {
    suggestion.tags = [deprecatedTag];
  }
  if (item.commitCharacters) {
    suggestion.commitCharacters = item.commitCharacters;
  }
  if (item.additionalTextEdits) {
    suggestion.additionalTextEdits = textEditsOf(item.additionalTextEdits);
  }
  return suggestion;
}

/** A suggestion with what `completionItem/resolve` added to its item: its full signature and its documentation. */
export function resolvedSuggestion(suggestion, item) {
  if (!item) {
    return suggestion;
  }
  const resolved = Object.assign({}, suggestion, { item: item });
  if (item.detail) {
    resolved.detail = item.detail;
  }
  if (item.documentation) {
    resolved.documentation = markdownOf(item.documentation)[0];
  }
  return resolved;
}

// ------------------------------------------------------------------------------------------------ signature help --

/** The server's signature help as Monaco's, or `null`. */
export function signatureHelpOf(result) {
  if (!result || !Array.isArray(result.signatures) || result.signatures.length === 0) {
    return null;
  }
  return {
    signatures: result.signatures.map((signature) => ({
      label: signature.label,
      documentation: markdownOf(signature.documentation)[0],
      parameters: (signature.parameters || []).map((parameter) => ({
        label: parameter.label,
        documentation: markdownOf(parameter.documentation)[0],
      })),
      activeParameter: signature.activeParameter,
    })),
    activeSignature: result.activeSignature || 0,
    activeParameter: result.activeParameter || 0,
  };
}

// ------------------------------------------------------------------------------------------ edits and locations --

export function textEditsOf(edits) {
  return (edits || []).map((edit) => ({ range: fromRange(edit.range), text: edit.newText }));
}

/**
 * A workspace edit as `[{ uri, edits }]`, one entry per document, from `changes` or `documentChanges`. Only the
 * documents `known(uri)` accepts are kept: the playground edits the code it shows, never the standard library.
 */
export function workspaceEditOf(edit, known) {
  const byUri = new Map();
  function add(uri, edits) {
    if (known && !known(uri)) {
      return;
    }
    if (!byUri.has(uri)) {
      byUri.set(uri, []);
    }
    byUri.get(uri).push(...textEditsOf(edits));
  }
  if (edit && edit.changes) {
    for (const uri of Object.keys(edit.changes)) {
      add(uri, edit.changes[uri]);
    }
  }
  if (edit && Array.isArray(edit.documentChanges)) {
    for (const change of edit.documentChanges) {
      if (change && change.textDocument && Array.isArray(change.edits)) {
        add(change.textDocument.uri, change.edits);
      }
    }
  }
  return Array.from(byUri, ([uri, edits]) => ({ uri: uri, edits: edits }));
}

/** Definitions as `[{ uri, range }]`, only those inside `uri`: a definition in the standard library has no editor. */
export function locationsOf(result, uri) {
  const list = result === null || result === undefined ? [] : Array.isArray(result) ? result : [result];
  const found = [];
  for (const location of list) {
    const target = location.targetUri || location.uri;
    const range = location.targetSelectionRange || location.range;
    if (target === uri && range) {
      found.push({ uri: target, range: fromRange(range) });
    }
  }
  return found;
}

// ------------------------------------------------------------------------------------------------ the diagnostics --

/** LSP's DiagnosticSeverity (1-4) as Monaco's `MarkerSeverity`. */
const markerSeverities = { 1: 8, 2: 4, 3: 2, 4: 1 };

/** The diagnostics of `publishDiagnostics` as Monaco's markers. */
export function markersOf(diagnostics) {
  return (diagnostics || []).map((diagnostic) => {
    const marker = Object.assign(
      {
        severity: markerSeverities[diagnostic.severity] || 8,
        message: diagnostic.message,
        source: diagnostic.source,
      },
      fromRange(diagnostic.range),
    );
    if (diagnostic.code !== undefined && diagnostic.code !== null) {
      marker.code = String(diagnostic.code);
    }
    if (Array.isArray(diagnostic.tags) && diagnostic.tags.length > 0) {
      marker.tags = diagnostic.tags.filter((tag) => tag === 1 || tag === 2);
    }
    return marker;
  });
}

/** A marker of Monaco's as the diagnostic it came from, for `textDocument/codeAction`. */
export function diagnosticOf(marker) {
  const severities = { 8: 1, 4: 2, 2: 3, 1: 4 };
  return {
    range: toRange(marker),
    severity: severities[marker.severity] || 1,
    message: marker.message,
    source: marker.source,
    code: marker.code,
  };
}

// ---------------------------------------------------------------------------------------------- semantic tokens --

/**
 * The legend the editor gives Monaco: the server's types, the lexical kinds of the site's highlighting (which only the
 * first colours of a block use, see [seedTokens]), and of the modifiers only the two that change how a token looks.
 * `mutable` comes first, so Monaco's theme finds `variable.mutable` whatever else a token is.
 */
export const tokenLegend = {
  tokenTypes: [
    "type",
    "interface",
    "typeParameter",
    "enumMember",
    "namespace",
    "function",
    "method",
    "parameter",
    "variable",
    "property",
    "keyword",
    "string",
    "number",
    "comment",
    "punctuation",
  ],
  tokenModifiers: ["mutable", "deprecated"],
};

/**
 * The server's semantic tokens, encoded in `serverLegend`, re-encoded in [tokenLegend]: the type by its name, and of
 * the modifiers `mutable` and `deprecated`. A token of a type the editor does not know is left out.
 */
export function semanticTokensOf(data, serverLegend) {
  const types = serverLegend.tokenTypes.map((name) => tokenLegend.tokenTypes.indexOf(name));
  const mutable = serverLegend.tokenModifiers.indexOf("mutable");
  const deprecated = serverLegend.tokenModifiers.indexOf("deprecated");
  const output = [];
  // Every token is placed absolutely and encoded again relative to the last one kept, so one left out moves nothing
  let line = 0;
  let character = 0;
  let keptLine = 0;
  let keptCharacter = 0;
  for (let index = 0; index + 4 < data.length; index += 5) {
    line += data[index];
    character = data[index] === 0 ? character + data[index + 1] : data[index + 1];
    const type = types[data[index + 3]];
    if (type === undefined || type < 0) {
      continue;
    }
    const modifiers = data[index + 4];
    let bits = 0;
    if (mutable >= 0 && (modifiers & (1 << mutable)) !== 0) {
      bits |= 1;
    }
    if (deprecated >= 0 && (modifiers & (1 << deprecated)) !== 0) {
      bits |= 2;
    }
    const deltaLine = line - keptLine;
    output.push(deltaLine, deltaLine === 0 ? character - keptCharacter : character, data[index + 2], type, bits);
    keptLine = line;
    keptCharacter = character;
  }
  return new Uint32Array(output);
}

/**
 * The colours of the site's own highlighting (`<span class="t-keyword">`, `t-variable t-mutable`, ...) as semantic
 * tokens in [tokenLegend], so that the editor that replaces a highlighted block shows the block's colours exactly
 * until the language server has checked it. `spans` is `[{ from, to, classes }]` in offsets of `text`, in order.
 */
export function seedTokens(text, spans) {
  const lineStarts = [0];
  for (let index = 0; index < text.length; index += 1) {
    if (text.charCodeAt(index) === 10) {
      lineStarts.push(index + 1);
    }
  }
  const output = [];
  let previousLine = 0;
  let previousCharacter = 0;
  let line = 0;
  for (const span of spans) {
    const names = span.classes.split(/\s+/).filter((name) => name.startsWith("t-")).map((name) => name.slice(2));
    const type = names.map((name) => tokenLegend.tokenTypes.indexOf(name)).find((found) => found >= 0);
    if (type === undefined || span.to <= span.from) {
      continue;
    }
    let bits = 0;
    if (names.includes("mutable")) {
      bits |= 1;
    }
    if (names.includes("deprecated")) {
      bits |= 2;
    }
    // A token never spans lines: a block comment or a string of several lines is one token per line
    let from = span.from;
    while (from < span.to) {
      while (line + 1 < lineStarts.length && lineStarts[line + 1] <= from) {
        line += 1;
      }
      const lineEnd = line + 1 < lineStarts.length ? lineStarts[line + 1] - 1 : text.length;
      const to = Math.min(span.to, lineEnd);
      const character = from - lineStarts[line];
      if (to > from) {
        const deltaLine = line - previousLine;
        output.push(deltaLine, deltaLine === 0 ? character - previousCharacter : character, to - from, type, bits);
        previousLine = line;
        previousCharacter = character;
      }
      from = lineEnd + 1;
    }
  }
  return new Uint32Array(output);
}
