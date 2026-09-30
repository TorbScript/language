/*
 * editor.mjs - the editor of the playground: Monaco, the editor of VS Code, with a bridge of its own to `torb lsp` in
 * the playground's language worker (playground-worker.js). `build.sh` bundles it, with the exact versions of
 * `package-lock.json`, into `../playground-editor.js`, `../playground-editor.css` and `../playground-codicon.ttf`,
 * which are committed; playground.js loads the module when an editor is about to be used and keeps a static view of
 * the code until then (docs/tooling/the-playground.md).
 *
 *   ready                                  a promise: the stylesheet, the fonts and the theme are there
 *   createClient(transport)                the page's language client over the worker's messages, providers and all
 *   createEditor(host, options)            an editor in `host`, a document of the page's language server once it is used
 *
 * The bridge is Monaco's providers speaking JSON-RPC (protocol.mjs): completion with the documentation of an item
 * resolved when it is shown, hover, signature help, definition within the document (F12, Ctrl or Cmd and a click),
 * rename (F2), formatting (Shift+Alt+F), the quick fixes of a diagnostic (Ctrl+.), semantic tokens, and the
 * diagnostics the server publishes as markers. Monaco renders the Markdown of the documentation itself - with the
 * grammar of grammar.mjs for a ` ```trb ` block - and sanitizes it: no HTML, and no link that runs a command.
 */

import * as monaco from "monaco-editor/editor/editor.api.js";
// Of Monaco's contributions, only what the playground uses: each registers itself
import "monaco-editor/editor/contrib/hover/browser/hoverContribution.js";
import "monaco-editor/editor/contrib/suggest/browser/suggestController.js";
import "monaco-editor/editor/contrib/parameterHints/browser/parameterHints.js";
import "monaco-editor/editor/contrib/gotoSymbol/browser/goToCommands.js";
import "monaco-editor/editor/contrib/gotoSymbol/browser/link/goToDefinitionAtPosition.js";
import "monaco-editor/editor/contrib/rename/browser/rename.js";
import "monaco-editor/editor/contrib/format/browser/formatActions.js";
import "monaco-editor/editor/contrib/codeAction/browser/codeActionContributions.js";
import "monaco-editor/editor/contrib/semanticTokens/browser/documentSemanticTokens.js";
import "monaco-editor/editor/contrib/bracketMatching/browser/bracketMatching.js";
import "monaco-editor/editor/contrib/comment/browser/comment.js";
import "monaco-editor/editor/contrib/clipboard/browser/clipboard.js";
import "monaco-editor/editor/contrib/contextmenu/browser/contextmenu.js";
import "monaco-editor/editor/contrib/cursorUndo/browser/cursorUndo.js";
import "monaco-editor/editor/contrib/linesOperations/browser/linesOperations.js";
import "monaco-editor/editor/contrib/wordOperations/browser/wordOperations.js";
import "monaco-editor/editor/contrib/toggleTabFocusMode/browser/toggleTabFocusMode.js";
import "./node_modules/monaco-editor/esm/vs/base/browser/ui/codicons/codicon/codicon.css";
import { StandaloneServices } from "monaco-editor/editor/standalone/browser/standaloneServices.js";
import { IStorageService } from "monaco-editor/platform/storage/common/storage.js";

import {
  LanguageClient,
  completionsOf,
  diagnosticOf,
  fromRange,
  locationsOf,
  markdownOf,
  markersOf,
  resolvedSuggestion,
  seedTokens,
  semanticTokensOf,
  signatureHelpOf,
  textEditsOf,
  toPosition,
  toRange,
  tokenLegend,
  workspaceEditOf,
} from "./protocol.mjs";
import { languageConfiguration, monarch } from "./grammar.mjs";
import { currentTheme } from "./theme.mjs";

export { workspaceRoot } from "./protocol.mjs";

const languageId = "torbscript";

// ---------------------------------------------------------------------------------------------------- the files --

// Everything this module loads comes from beside it, with its query (`?v=...`), so a page never pairs files of two builds
const moduleAddress = new URL(import.meta.url);

function sibling(name) {
  return new URL(name + moduleAddress.search, moduleAddress).href;
}

// Monaco's editor worker is not shipped: what the playground needs of it - the smallest edits of a formatting, so the
// cursor stays where it was - Monaco runs in the page, for a program of a screen in a moment (build.mjs makes it do
// so without a warning)
self.MonacoEnvironment = {
  getWorker: function () {
    throw new Error("the playground runs Monaco's worker in the page");
  },
};

/** Monaco's stylesheet, in front of playground.css, so the playground's rules win where both say something. */
const stylesheet = new Promise(function (resolve) {
  let link = document.querySelector("link[data-torb-editor]");
  if (link === null) {
    link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = sibling("playground-editor.css");
    link.setAttribute("data-torb-editor", "");
    const playground = document.querySelector("link[data-torb-playground]");
    document.head.insertBefore(link, playground);
  }
  if (link.sheet) {
    resolve();
  } else {
    link.addEventListener("load", resolve);
    link.addEventListener("error", resolve);
  }
});

// The icons of completion and of the widgets: the font the stylesheet names, with its query, loaded when one is shown
if (typeof FontFace === "function") {
  const address = sibling("playground-codicon.ttf");
  document.fonts.add(new FontFace("codicon", 'url("' + address + '") format("truetype")', { display: "block" }));
}

/** The stylesheet, the code font and the theme: an editor made before them would measure the wrong characters. */
export const ready = Promise.all([
  stylesheet,
  document.fonts ? document.fonts.load("400 14px " + monospaceFamily()).catch(function () {}) : null,
]).then(function () {
  applyTheme();
  watchTheme();
});

/** The site's code font, `--torb-font-mono` of brand/tokens.css. */
function monospaceFamily() {
  const family = getComputedStyle(document.documentElement).getPropertyValue("--torb-font-mono").trim();
  return family || '"Geist Mono", ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
}

// -------------------------------------------------------------------------------------------------- the language --

monaco.languages.register({ id: languageId, extensions: [".trb"], aliases: ["TorbScript", "torbscript", "trb"] });
monaco.languages.setMonarchTokensProvider(languageId, monarch);
monaco.languages.setLanguageConfiguration(languageId, languageConfiguration);

// --------------------------------------------------------------------------------------------------- the theme --

let themeName = null;

/** Defines the theme from the site's tokens as they are now, and gives it every editor of the page. */
function applyTheme() {
  const theme = currentTheme();
  monaco.editor.defineTheme(theme.name, theme.data);
  monaco.editor.setTheme(theme.name);
  themeName = theme.name;
}

/** The site's switch sets `data-theme` on <html>; without it the system's preference decides. */
function watchTheme() {
  new MutationObserver(applyTheme).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
  if (window.matchMedia) {
    window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", applyTheme);
  }
}

// --------------------------------------------------------------------------------------------------- the bridge --

/** The page's language client, once the first editor that is used made it. */
let client = null;

/** What the site's highlighting of a block said, as semantic tokens, until the language server says more. */
const seeds = new Map();

/** Fired when the server has checked a document: its semantic tokens are due again. */
const tokensChanged = new monaco.Emitter();

/** The document of a model, as the language server knows it, or `null` where it does not. */
function documentOf(model) {
  const uri = model.uri.toString();
  return client !== null && client.isOpen(uri) ? uri : null;
}

function modelOf(uri) {
  return monaco.editor.getModel(monaco.Uri.parse(uri));
}

/** A request about a document, answered `null` where the server failed. */
function ask(method, params, token) {
  return client.request(method, params, token).catch(function () {
    return null;
  });
}

/**
 * The page's language client over `transport` (`{ send, subscribe }` of the worker's messages), started, with the
 * providers of TorbScript speaking to it.
 */
export function createClient(transport) {
  client = new LanguageClient(transport);
  client.onNotification("textDocument/publishDiagnostics", function (params) {
    const model = modelOf(params.uri);
    if (model !== null) {
      monaco.editor.setModelMarkers(model, "torb", markersOf(params.diagnostics));
    }
    tokensChanged.fire();
  });
  client.start();
  return client;
}

// Monaco's providers: registered once, for every editor of the page, and each asks only about a document the server has

monaco.languages.registerCompletionItemProvider(languageId, {
  triggerCharacters: ["."],
  provideCompletionItems: function (model, position, context, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const word = model.getWordUntilPosition(position);
    const whole = model.getWordAtPosition(position);
    const insert = new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, position.column);
    const end = whole ? Math.max(whole.endColumn, position.column) : position.column;
    const replace = new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, end);
    const lspContext = { triggerKind: context.triggerKind + 1 };
    if (context.triggerCharacter) {
      lspContext.triggerCharacter = context.triggerCharacter;
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position), context: lspContext };
    return ask("textDocument/completion", params, token).then(function (result) {
      return completionsOf(result, { insert: insert, replace: replace });
    });
  },
  resolveCompletionItem: function (suggestion, token) {
    const provider = client && client.capabilities && client.capabilities.completionProvider;
    if (!suggestion.item || !provider || !provider.resolveProvider) {
      return suggestion;
    }
    return ask("completionItem/resolve", suggestion.item, token).then(function (item) {
      return resolvedSuggestion(suggestion, item);
    });
  },
});

monaco.languages.registerHoverProvider(languageId, {
  provideHover: function (model, position, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position) };
    return ask("textDocument/hover", params, token).then(function (result) {
      // A name the checker could not resolve has the type `?`: nothing to show
      const contents = (result ? markdownOf(result.contents) : []).filter(function (part) {
        return !/^(```\w*\s*)?\?(\s*```)?$/.test(part.value.trim());
      });
      if (contents.length === 0) {
        return null;
      }
      return { contents: contents, range: result.range ? fromRange(result.range) : undefined };
    });
  },
});

monaco.languages.registerSignatureHelpProvider(languageId, {
  signatureHelpTriggerCharacters: ["(", ","],
  signatureHelpRetriggerCharacters: [")"],
  provideSignatureHelp: function (model, position, token, context) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const lspContext = { triggerKind: context.triggerKind, isRetrigger: context.isRetrigger };
    if (context.triggerCharacter) {
      lspContext.triggerCharacter = context.triggerCharacter;
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position), context: lspContext };
    return ask("textDocument/signatureHelp", params, token).then(function (result) {
      const value = signatureHelpOf(result);
      return value === null ? null : { value: value, dispose: function () {} };
    });
  },
});

monaco.languages.registerDefinitionProvider(languageId, {
  provideDefinition: function (model, position, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position) };
    return ask("textDocument/definition", params, token).then(function (result) {
      return locationsOf(result, uri).map(function (location) {
        return { uri: model.uri, range: location.range };
      });
    });
  },
});

/** The answer of a rename that cannot be made, with the reason Monaco shows at the cursor. */
function refusedRename(position, reason) {
  const range = new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column);
  return { range: range, text: "", rejectReason: reason };
}

monaco.languages.registerRenameProvider(languageId, {
  provideRenameEdits: function (model, position, newName, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position), newName: newName };
    return client.request("textDocument/rename", params, token).then(
      function (result) {
        const edits = [];
        const known = function (target) {
          return modelOf(target) !== null;
        };
        for (const document of workspaceEditOf(result, known)) {
          const target = modelOf(document.uri);
          for (const edit of document.edits) {
            edits.push({ resource: target.uri, textEdit: edit, versionId: target.getVersionId() });
          }
        }
        return { edits: edits };
      },
      function (problem) {
        return { edits: [], rejectReason: problem.message };
      },
    );
  },
  resolveRenameLocation: function (model, position, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return refusedRename(position, "The language server is not running yet");
    }
    const params = { textDocument: { uri: uri }, position: toPosition(position) };
    return client.request("textDocument/prepareRename", params, token).then(
      function (result) {
        if (!result) {
          return refusedRename(position, "This name cannot be renamed");
        }
        const range = fromRange(result.range || result);
        return { range: range, text: result.placeholder || model.getValueInRange(range) };
      },
      function (problem) {
        return refusedRename(position, problem.message);
      },
    );
  },
});

monaco.languages.registerDocumentFormattingEditProvider(languageId, {
  displayName: "torb format",
  provideDocumentFormattingEdits: function (model, options, token) {
    const uri = documentOf(model);
    if (uri === null) {
      return null;
    }
    const params = { textDocument: { uri: uri }, options: { tabSize: options.tabSize, insertSpaces: options.insertSpaces } };
    return ask("textDocument/formatting", params, token).then(textEditsOf);
  },
});

monaco.languages.registerCodeActionProvider(
  languageId,
  {
    provideCodeActions: function (model, range, context, token) {
      const uri = documentOf(model);
      const markers = context.markers.filter(function (marker) {
        return marker.owner === "torb";
      });
      if (uri === null || markers.length === 0) {
        return null;
      }
      const params = { textDocument: { uri: uri }, range: toRange(range), context: { diagnostics: markers.map(diagnosticOf) } };
      return ask("textDocument/codeAction", params, token).then(function (result) {
        const actions = [];
        for (const action of result || []) {
          if (!action || !action.edit) {
            continue;
          }
          const edits = [];
          const inside = function (target) {
            return target === uri;
          };
          for (const document of workspaceEditOf(action.edit, inside)) {
            for (const edit of document.edits) {
              edits.push({ resource: model.uri, textEdit: edit, versionId: model.getVersionId() });
            }
          }
          actions.push({ title: action.title, kind: action.kind || "quickfix", isPreferred: action.isPreferred, edit: { edits: edits } });
        }
        return { actions: actions, dispose: function () {} };
      });
    },
  },
  { providedCodeActionKinds: ["quickfix"] },
);

monaco.languages.registerDocumentSemanticTokensProvider(languageId, {
  onDidChange: tokensChanged.event,
  getLegend: function () {
    return tokenLegend;
  },
  provideDocumentSemanticTokens: function (model, lastResultId, token) {
    const uri = documentOf(model);
    if (uri === null) {
      // Before the server knows the document: the site's own colours, as long as the text is the site's
      const seed = seeds.get(model.uri.toString());
      return seed && seed.version === model.getVersionId() ? { data: seed.data } : null;
    }
    const params = { textDocument: { uri: uri } };
    return client.request("textDocument/semanticTokens/full", params, token).then(
      function (result) {
        const provider = client.capabilities && client.capabilities.semanticTokensProvider;
        if (!result || !Array.isArray(result.data) || !provider || !provider.legend) {
          return null;
        }
        return { data: semanticTokensOf(result.data, provider.legend), resultId: result.resultId };
      },
      function () {
        return null;
      },
    );
  },
  releaseDocumentSemanticTokens: function () {},
});

// --------------------------------------------------------------------------------------------------- the editor --

/** Whether the documentation of completion was set to show beside the list, which the first editor does. */
let documentationExpanded = false;

/**
 * An editor in `host`. `options`:
 *
 *   doc            the text
 *   uri            the document's URI in the language worker's workspace
 *   page           the page /play: line numbers, the height of the page, the language server at once
 *   metrics        `{ fontSize, lineHeight, paddingTop, paddingBottom, paddingLeft }` of the code it replaces
 *   seed           the site's highlighting of the text, `[{ from, to, classes }]`, shown until the server's colours come
 *   selection      where the cursor starts, an offset
 *   label, labels  the accessible name of the text; the word of Run for the context menu
 *   onRun          Ctrl or Cmd+Enter, and Run in the context menu
 *   onChange       every change of the text, with the text
 *   languageClient a function that answers the page's client, which the first editor that is used starts
 */
export function createEditor(host, options) {
  const settings = options || {};
  const defaults = { fontSize: 14, lineHeight: 21, paddingTop: 14, paddingBottom: 14, paddingLeft: 18 };
  const metrics = Object.assign(defaults, settings.metrics || {});
  const uri = settings.uri;
  const model = monaco.editor.createModel(settings.doc || "", languageId, monaco.Uri.parse(uri));
  model.updateOptions({ tabSize: 2, indentSize: 2, insertSpaces: true });
  if (Array.isArray(settings.seed) && settings.seed.length > 0) {
    seeds.set(model.uri.toString(), { version: model.getVersionId(), data: seedTokens(model.getValue(), settings.seed) });
  }

  // The context menu is drawn beside the editor, in `host`: the theme's colours are given to what has this class
  host.classList.add("monaco-component");

  const page = settings.page === true;
  const padding = metrics.paddingTop + metrics.paddingBottom;
  // A block fits its code and grows as it is written, up to thirty lines - or the block as it came, where it is longer
  const tallest = Math.max(model.getLineCount(), 30) * metrics.lineHeight + padding;
  if (!page) {
    host.style.height = Math.min(model.getLineCount() * metrics.lineHeight + padding, tallest) + "px";
  }

  const editor = monaco.editor.create(host, {
    model: model,
    theme: themeName || undefined,
    ariaLabel: settings.label || "TorbScript source",
    automaticLayout: true,
    fixedOverflowWidgets: true,
    fontFamily: monospaceFamily(),
    fontSize: metrics.fontSize,
    lineHeight: metrics.lineHeight,
    fontWeight: "400",
    letterSpacing: 0,
    // No ligatures, and the slashed zero: the features of --torb-font-mono-features (BRAND.md section 5)
    fontLigatures: '"zero" 1, "liga" 0, "calt" 0',
    padding: { top: metrics.paddingTop, bottom: metrics.paddingBottom },
    lineNumbers: page ? "on" : "off",
    lineNumbersMinChars: 3,
    lineDecorationsWidth: page ? 12 : metrics.paddingLeft,
    glyphMargin: false,
    folding: false,
    minimap: { enabled: false },
    stickyScroll: { enabled: false },
    wordWrap: "off",
    scrollBeyondLastLine: false,
    scrollbar: {
      alwaysConsumeMouseWheel: false,
      useShadows: false,
      verticalScrollbarSize: 10,
      horizontalScrollbarSize: 10,
      ignoreHorizontalScrollbarInContentHeight: true,
    },
    overviewRulerLanes: 0,
    overviewRulerBorder: false,
    hideCursorInOverviewRuler: true,
    renderLineHighlight: page ? "line" : "none",
    renderLineHighlightOnlyWhenFocus: true,
    guides: { indentation: page, bracketPairs: false, highlightActiveIndentation: false },
    // Brackets in the colour of punctuation, as the site shows them. Monaco reads what concerns its models from its
    // configuration, where the setting has this dotted name, as `semanticHighlighting.enabled` has
    "bracketPairColorization.enabled": false,
    matchBrackets: "near",
    colorDecorators: false,
    defaultColorDecorators: "never",
    links: false,
    codeLens: false,
    inlayHints: { enabled: "off" },
    lightbulb: { enabled: page ? "onCode" : "off" },
    occurrencesHighlight: "off",
    selectionHighlight: false,
    renderWhitespace: "none",
    unicodeHighlight: { ambiguousCharacters: false, invisibleCharacters: false, nonBasicASCII: false },
    "semanticHighlighting.enabled": true,
    quickSuggestions: { other: "on", comments: "off", strings: "off" },
    quickSuggestionsDelay: 10,
    wordBasedSuggestions: "off",
    suggest: { preview: false, showStatusBar: false, insertMode: "replace", showWords: false },
    parameterHints: { enabled: true, cycle: true },
    hover: { enabled: true, delay: 300, sticky: true },
    tabSize: 2,
    insertSpaces: true,
    detectIndentation: false,
    autoIndent: "full",
    formatOnPaste: false,
    formatOnType: false,
    dragAndDrop: false,
    contextmenu: true,
    mouseWheelZoom: false,
    smoothScrolling: false,
    accessibilitySupport: "auto",
  });

  // The documentation of a completion item is shown beside the list from the start, as VS Code shows it once asked
  // to: Monaco keeps that choice in its storage, which lives as long as the page
  if (!documentationExpanded) {
    documentationExpanded = true;
    StandaloneServices.get(IStorageService).store("expandSuggestionDocs", true, 0, 0);
  }

  const start = Math.min(settings.selection || 0, model.getValueLength());
  if (start > 0) {
    editor.setPosition(model.getPositionAt(start));
  }

  if (!page) {
    editor.onDidContentSizeChange(function (event) {
      if (event.contentHeightChanged) {
        const height = Math.min(event.contentHeight, tallest);
        host.style.height = height + "px";
        editor.layout({ width: host.clientWidth, height: height });
      }
    });
  }

  // The document becomes the language server's when the editor is first used: the language worker starts then
  let connected = false;
  function connect() {
    if (connected || typeof settings.languageClient !== "function") {
      return;
    }
    connected = true;
    const found = settings.languageClient();
    if (found) {
      found.open(uri, model.getValue());
    }
  }
  editor.onDidFocusEditorText(connect);
  if (page) {
    connect();
  }

  model.onDidChangeContent(function () {
    const text = model.getValue();
    connect();
    if (client !== null) {
      client.change(uri, text);
    }
    monaco.editor.setModelMarkers(model, "torb-run", []);
    if (typeof settings.onChange === "function") {
      settings.onChange(text);
    }
  });

  const labels = settings.labels || {};
  editor.addAction({
    id: "torbscript.run",
    label: labels.run || "Run",
    keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
    contextMenuGroupId: "navigation",
    contextMenuOrder: 0,
    run: function () {
      if (typeof settings.onRun === "function") {
        settings.onRun();
      }
    },
  });

  // Escape, then Tab, leaves the editor, so the keyboard is never trapped in it (Ctrl+M switches Tab over for good)
  let escaped = false;
  editor.onKeyDown(function (event) {
    if (event.keyCode === monaco.KeyCode.Escape) {
      escaped = true;
      return;
    }
    if (event.keyCode === monaco.KeyCode.Tab && escaped && !event.ctrlKey && !event.metaKey && !event.altKey) {
      // The browser moves the focus on; Monaco does not see the key
      event.stopPropagation();
    }
    escaped = false;
  });

  return {
    editor: editor,
    model: model,
    get value() {
      return model.getValue();
    },
    /** Replaces the whole text, as one change that undo takes back. */
    set value(text) {
      editor.pushUndoStop();
      editor.executeEdits("playground", [{ range: model.getFullModelRange(), text: text, forceMoveMarkers: true }]);
      editor.pushUndoStop();
      editor.setPosition({ lineNumber: 1, column: 1 });
      editor.setScrollPosition({ scrollTop: 0, scrollLeft: 0 });
    },
    focus: function () {
      editor.focus();
    },
    /** Puts the cursor at `line` and `column`, both from 1, and the focus into the editor. */
    moveTo: function (line, column) {
      const lineNumber = Math.max(1, Math.min(line, model.getLineCount()));
      const position = { lineNumber: lineNumber, column: Math.max(1, Math.min(column, model.getLineMaxColumn(lineNumber))) };
      editor.setPosition(position);
      editor.revealPositionInCenterIfOutsideViewport(position);
      editor.focus();
    },
    /**
     * Marks what a run reported, `{ line, column, message }` from 1, until the text changes. What the language server
     * marks already - the checker's errors, the same as the run's - is not marked twice; a panic is the run's alone.
     */
    mark: function (diagnostics) {
      const known = monaco.editor.getModelMarkers({ resource: model.uri, owner: "torb" });
      const markers = [];
      for (const diagnostic of diagnostics) {
        if (diagnostic.line < 1 || diagnostic.line > model.getLineCount()) {
          continue;
        }
        const marked = known.some(function (marker) {
          return marker.startLineNumber === diagnostic.line && marker.message === diagnostic.message;
        });
        if (marked) {
          continue;
        }
        const column = Math.max(1, Math.min(diagnostic.column, model.getLineMaxColumn(diagnostic.line)));
        const word = model.getWordAtPosition({ lineNumber: diagnostic.line, column: column });
        markers.push({
          severity: monaco.MarkerSeverity.Error,
          message: diagnostic.message,
          startLineNumber: diagnostic.line,
          startColumn: column,
          endLineNumber: diagnostic.line,
          endColumn: word ? word.endColumn : model.getLineMaxColumn(diagnostic.line),
        });
      }
      monaco.editor.setModelMarkers(model, "torb-run", markers);
    },
    /** The server's `torb format` of the text. */
    format: function () {
      connect();
      const action = editor.getAction("editor.action.formatDocument");
      return action ? action.run() : Promise.resolve();
    },
    destroy: function () {
      editor.dispose();
      seeds.delete(model.uri.toString());
      if (client !== null) {
        client.close(uri);
      }
      model.dispose();
    },
  };
}
