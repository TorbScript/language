/*
 * editor.mjs - the editor of the playground: CodeMirror 6 with the language server client of @codemirror/lsp-client,
 * speaking to `torb lsp` in the playground's language worker (playground-worker.js). `build.sh` bundles it, with the
 * exact versions of `package-lock.json`, into `../playground-editor.js`, which is committed; playground.js loads that
 * file when an editor is about to be used, and keeps its own light editor until then (docs/tooling/the-playground.md).
 *
 *   createClient(transport, { tokenize })          the client of one language worker, connected
 *   createEditor(parent, options)                  an editor, with or without a client
 *
 * What the server does is the server's: completion as the reader types (and after a `.`), the diagnostics of every
 * change once the client has synchronized it (half a second after the last keystroke), hover, signature help,
 * definition within the file (F12, or Ctrl or Cmd and a click), rename (F2) and formatting (Shift+Alt+F), all of
 * @codemirror/lsp-client. What this file adds is TorbScript's: the colours - the lexer's at once, the server's
 * semantic tokens as soon as it has checked the text, in the token classes of the site (`t-keyword`, `t-type`, ...,
 * `t-mutable`) - the indentation, the brackets and the comments, and the documentation of hover and completion made
 * safe to show: it is Markdown the server renders from doc comments, and a doc comment of a shared program is anybody's.
 */

import { EditorSelection, EditorState, Prec, RangeSetBuilder, StateEffect, StateField } from "@codemirror/state";
import {
  Decoration,
  EditorView,
  ViewPlugin,
  drawSelection,
  dropCursor,
  highlightActiveLine,
  highlightActiveLineGutter,
  highlightSpecialChars,
  keymap,
  lineNumbers,
} from "@codemirror/view";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { bracketMatching, indentOnInput, indentService, indentUnit } from "@codemirror/language";
import { closeBrackets, closeBracketsKeymap } from "@codemirror/autocomplete";
import { lintGutter, lintKeymap, setDiagnostics } from "@codemirror/lint";
import {
  LSPClient,
  LSPPlugin,
  formatDocument,
  formatKeymap,
  hoverTooltips,
  jumpToDefinition,
  jumpToDefinitionKeymap,
  renameKeymap,
  serverCompletion,
  serverDiagnostics,
  signatureHelp,
} from "@codemirror/lsp-client";

/** The root of the language worker's workspace, where every document of a page is. */
export const workspaceRoot = "file:///torb/work";

// --------------------------------------------------------------------------------------------------- the client --

/**
 * The client of one language worker. `transport` is `{ send, subscribe, unsubscribe }` over the worker's messages;
 * `tokenize` is the lexer of playground.js, which colours the code of the documentation the server sends.
 */
export function createClient(transport, options) {
  const tokenize = options && options.tokenize;
  const client = new LSPClient({
    rootUri: workspaceRoot,
    // The first check of a page reads the standard library: a request behind it may wait a second or two
    timeout: 20000,
    sanitizeHTML: function (html) {
      return safeHtml(html, tokenize);
    },
    notificationHandlers: {
      // The server checked the text it was sent: its tokens are due, and lsp-client shows the diagnostics
      "textDocument/publishDiagnostics": function (connected, params) {
        refreshTokens(connected, params.uri);
        return false;
      },
    },
    extensions: [
      serverCompletion(),
      hoverTooltips({ hoverTime: 350 }),
      signatureHelp(),
      serverDiagnostics(),
      keymap.of([...formatKeymap, ...renameKeymap, ...jumpToDefinitionKeymap]),
    ],
  });
  client.connect(transport);
  return client;
}

// --------------------------------------------------------------------------------------------------- the editor --

/**
 * An editor in `parent`. `options`:
 *
 *   doc            the text
 *   tokenize       the lexer of playground.js: text -> [{ from, to, kind }]
 *   onRun          Ctrl or Cmd+Enter
 *   onChange       every change of the text, with the text
 *   lineNumbers    whether the gutter numbers the lines (the page /play)
 *   label          the accessible name of the text
 *   client, uri    the language server and the document's URI, or none
 *   selection      where the cursor starts, an offset
 */
export function createEditor(parent, options) {
  const settings = options || {};
  const extensions = [
    settings.lineNumbers ? [lineNumbers(), highlightActiveLineGutter()] : [],
    highlightSpecialChars(),
    history(),
    drawSelection(),
    dropCursor(),
    indentOnInput(),
    bracketMatching(),
    closeBrackets(),
    highlightActiveLine(),
    EditorState.tabSize.of(2),
    indentUnit.of("  "),
    torbscriptData,
    torbscriptIndentation,
    colours(settings.tokenize),
    lintGutter(),
    Prec.highest(
      keymap.of([
        {
          key: "Mod-Enter",
          preventDefault: true,
          run: function () {
            if (typeof settings.onRun === "function") {
              settings.onRun();
            }
            return true;
          },
        },
      ]),
    ),
    keymap.of([...closeBracketsKeymap, ...defaultKeymap, ...historyKeymap, ...lintKeymap, indentWithTab]),
    definitionOnClick,
    EditorView.contentAttributes.of({
      "aria-label": settings.label || "TorbScript source",
      spellcheck: "false",
      autocorrect: "off",
      autocapitalize: "off",
    }),
    EditorView.updateListener.of(function (update) {
      if (update.docChanged && typeof settings.onChange === "function") {
        settings.onChange(update.state.doc.toString());
      }
    }),
  ];
  if (settings.client) {
    extensions.push(settings.client.plugin(settings.uri, "torbscript"));
  }
  const start = Math.min(settings.selection || 0, (settings.doc || "").length);
  const view = new EditorView({
    state: EditorState.create({ doc: settings.doc || "", extensions, selection: EditorSelection.cursor(start) }),
    parent: parent,
  });

  return {
    view: view,
    get value() {
      return view.state.doc.toString();
    },
    /** Replaces the whole text, as one change that undo takes back. */
    set value(text) {
      view.dispatch({
        changes: { from: 0, to: view.state.doc.length, insert: text },
        selection: EditorSelection.cursor(0),
        scrollIntoView: true,
        userEvent: "input.replace",
      });
    },
    focus: function () {
      view.focus();
    },
    /** Puts the cursor at `line` and `column`, both from 1, and the focus into the editor. */
    moveTo: function (line, column) {
      const doc = view.state.doc;
      const found = doc.line(Math.max(1, Math.min(line, doc.lines)));
      const offset = Math.min(found.to, found.from + Math.max(0, column - 1));
      view.dispatch({ selection: EditorSelection.cursor(offset), scrollIntoView: true });
      view.focus();
    },
    /** Marks what a run reported, `{ line, column, message }` from 1, until the language server publishes again. */
    mark: function (diagnostics) {
      const doc = view.state.doc;
      const marked = [];
      for (const diagnostic of diagnostics) {
        if (diagnostic.line < 1 || diagnostic.line > doc.lines) {
          continue;
        }
        const line = doc.line(diagnostic.line);
        const from = Math.min(line.to, line.from + Math.max(0, diagnostic.column - 1));
        const word = /^[A-Za-z0-9_]+/.exec(doc.sliceString(from, line.to));
        marked.push({
          from: from,
          to: word ? from + word[0].length : Math.max(from, line.to),
          severity: "error",
          message: diagnostic.message,
        });
      }
      view.dispatch(setDiagnostics(view.state, marked));
    },
    /** The server's `torb format` of the text, where there is a server. */
    format: function () {
      return formatDocument(view);
    },
    destroy: function () {
      view.destroy();
    },
  };
}

// ---------------------------------------------------------------------------------------------- the language --

/** Comments, brackets that close themselves, and the lines that re-indent when a closing bracket is typed. */
const torbscriptData = EditorState.languageData.of(function () {
  return [
    {
      commentTokens: { line: "//", block: { open: "/*", close: "*/" } },
      closeBrackets: { brackets: ["(", "[", "{", '"'] },
      indentOnInput: /^\s*[}\])]$/,
    },
  ];
});

/**
 * The indentation of a new line: the one of the last line above that is not empty, two more behind an opening
 * bracket, two less in front of a closing one - the formatter's layout, as far as a line shows it.
 */
const torbscriptIndentation = indentService.of(function (context, position) {
  const after = context.textAfterPos(position, 1);
  let above = context.simulatedBreak === position ? context.lineAt(position, -1) : null;
  if (above === null) {
    if (position === 0) {
      return 0;
    }
    above = context.lineAt(position - 1, -1);
  }
  let steps = 0;
  while (!/\S/.test(above.text) && above.from > 0 && steps < 200) {
    above = context.lineAt(above.from - 1, -1);
    steps += 1;
  }
  let indentation = /^ */.exec(above.text)[0].length;
  if (/[{([]\s*(\/\/.*)?$/.test(above.text)) {
    indentation += 2;
  }
  if (/^\s*[}\])]/.test(after)) {
    indentation -= 2;
  }
  return Math.max(0, indentation);
});

/** Ctrl or Cmd and a click goes to the definition of what was clicked, as F12 does. */
const definitionOnClick = EditorView.domEventHandlers({
  mousedown: function (event, view) {
    if (!(event.ctrlKey || event.metaKey) || event.button !== 0 || !LSPPlugin.get(view)) {
      return false;
    }
    const position = view.posAtCoords({ x: event.clientX, y: event.clientY });
    if (position === null) {
      return false;
    }
    view.dispatch({ selection: EditorSelection.cursor(position) });
    jumpToDefinition(view);
    event.preventDefault();
    return true;
  },
});

// ------------------------------------------------------------------------------------------------- the colours --

/** The kinds of the lexer that name something, which a semantic token of the server may say more about. */
const namingKinds = new Set(["type", "enumMember", "function", "method", "property", "variable"]);

/** The token types and modifiers of the server's legend (compiler/src/language-server/tokens.trb). */
let legend = null;

const setTokens = StateEffect.define();

/** The server's tokens, as marks whose class is the token class, moved along with every change since they came. */
const semanticTokens = StateField.define({
  create: function () {
    return Decoration.none;
  },
  update: function (tokens, transaction) {
    for (const effect of transaction.effects) {
      if (effect.is(setTokens)) {
        return effect.value;
      }
    }
    return transaction.docChanged ? tokens.map(transaction.changes) : tokens;
  },
});

const marks = new Map();

function markOf(classes) {
  let mark = marks.get(classes);
  if (mark === undefined) {
    mark = Decoration.mark({ class: classes });
    marks.set(classes, mark);
  }
  return mark;
}

/**
 * The lexer's colours, which come with every keystroke, sharpened by the server's where a semantic token covers the
 * same name: a parameter, a mutable binding, a type the lexer took for a case.
 */
function colours(tokenize) {
  const plugin = ViewPlugin.fromClass(
    class {
      constructor(view) {
        this.decorations = this.build(view);
      }
      update(update) {
        if (update.docChanged || update.startState.field(semanticTokens) !== update.state.field(semanticTokens)) {
          this.decorations = this.build(update.view);
        }
      }
      build(view) {
        const builder = new RangeSetBuilder();
        if (typeof tokenize !== "function") {
          return builder.finish();
        }
        const semantic = new Map();
        const cursor = view.state.field(semanticTokens).iter();
        while (cursor.value !== null) {
          semantic.set(cursor.from, { to: cursor.to, classes: cursor.value.spec.class });
          cursor.next();
        }
        for (const token of tokenize(view.state.doc.toString())) {
          if (token.to <= token.from) {
            continue;
          }
          let classes = "t-" + token.kind;
          if (namingKinds.has(token.kind)) {
            const found = semantic.get(token.from);
            if (found !== undefined && found.to === token.to) {
              classes = found.classes;
            }
          }
          builder.add(token.from, token.to, markOf(classes));
        }
        return builder.finish();
      }
    },
    {
      decorations: function (value) {
        return value.decorations;
      },
    },
  );
  return [semanticTokens, plugin];
}

/** Asks the server for the tokens of the document at `uri`, where an editor shows it, and gives them to that editor. */
function refreshTokens(client, uri) {
  const file = client.workspace.getFile(uri);
  const view = file && file.getView();
  const plugin = view && LSPPlugin.get(view);
  if (!plugin || !client.serverCapabilities || !client.serverCapabilities.semanticTokensProvider) {
    return;
  }
  if (legend === null) {
    legend = client.serverCapabilities.semanticTokensProvider.legend;
  }
  client.sync();
  client
    .withMapping(function (mapping) {
      return client.request("textDocument/semanticTokens/full", { textDocument: { uri: uri } }).then(function (result) {
        if (!result || !Array.isArray(result.data) || view.state.field(semanticTokens, false) === undefined) {
          return;
        }
        view.dispatch({ effects: setTokens.of(decoded(result.data, uri, mapping)) });
      });
    })
    .catch(function () {});
}

/** The five numbers per token of the protocol, relative to the one before, as marks in the text as it is now. */
function decoded(data, uri, mapping) {
  const builder = new RangeSetBuilder();
  const types = legend.tokenTypes;
  const modifiers = legend.tokenModifiers;
  const mutable = 1 << modifiers.indexOf("mutable");
  let line = 0;
  let character = 0;
  let last = -1;
  for (let index = 0; index + 4 < data.length; index += 5) {
    line += data[index];
    character = data[index] === 0 ? character + data[index + 1] : data[index + 1];
    const length = data[index + 2];
    const type = types[data[index + 3]];
    if (type === undefined) {
      continue;
    }
    const from = mapping.mapPosition(uri, { line: line, character: character }, 1);
    const to = mapping.mapPosition(uri, { line: line, character: character + length }, -1);
    if (from === null || to === null || to <= from || from < last) {
      continue;
    }
    const classes = "t-" + type + (mutable > 0 && (data[index + 4] & mutable) !== 0 ? " t-mutable" : "");
    builder.add(from, to, markOf(classes));
    last = to;
  }
  return builder.finish();
}

// --------------------------------------------------------------------------------------- the server's documentation --

const allowedElements = new Set([
  "a", "blockquote", "br", "code", "del", "em", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "li", "ol", "p", "pre",
  "span", "strong", "table", "tbody", "td", "th", "thead", "tr", "ul",
]);

/**
 * The HTML lsp-client made of the server's Markdown, with nothing in it but text and the elements of prose: no
 * attribute but a link's `href` to the web, and no script, style, image or form. A code block of TorbScript is coloured
 * by the lexer, in the token classes of the site.
 */
function safeHtml(html, tokenize) {
  const template = document.createElement("template");
  template.innerHTML = html;
  clean(template.content);
  for (const code of template.content.querySelectorAll("code")) {
    const isBlock = code.parentElement !== null && code.parentElement.tagName === "PRE";
    const language = code.getAttribute("class") || "";
    code.removeAttribute("class");
    // A block of another language (`console`, `text`) keeps its text as it is
    if (typeof tokenize === "function" && (!isBlock || language === "" || language === "language-trb")) {
      colourCode(code, tokenize);
    }
  }
  const holder = document.createElement("div");
  holder.appendChild(template.content);
  return holder.innerHTML;
}

function clean(node) {
  for (const child of Array.from(node.childNodes)) {
    if (child.nodeType === 3) {
      continue;
    }
    if (child.nodeType !== 1) {
      child.remove();
      continue;
    }
    const name = child.tagName.toLowerCase();
    if (!allowedElements.has(name)) {
      // What an unknown element holds may still be text: that stays, the element goes
      if (name === "script" || name === "style" || name === "template" || name === "iframe" || name === "object") {
        child.remove();
        continue;
      }
      clean(child);
      child.replaceWith(...Array.from(child.childNodes));
      continue;
    }
    for (const attribute of Array.from(child.attributes)) {
      const keep =
        (name === "a" && attribute.name === "href" && /^https?:\/\//i.test(attribute.value)) ||
        (name === "code" && attribute.name === "class" && /^language-[\w-]+$/.test(attribute.value));
      if (!keep) {
        child.removeAttribute(attribute.name);
      }
    }
    if (name === "a") {
      child.setAttribute("rel", "noopener noreferrer");
      child.setAttribute("target", "_blank");
    }
    clean(child);
  }
}

/** Colours the text of a code element, whose line breaks lsp-client wrote as `<br>`. */
function colourCode(code, tokenize) {
  const text = Array.from(code.childNodes)
    .map(function (node) {
      return node.nodeName === "BR" ? "\n" : node.textContent;
    })
    .join("");
  code.textContent = "";
  let at = 0;
  for (const token of tokenize(text)) {
    if (token.from > at) {
      appendText(code, text.slice(at, token.from));
    }
    const span = document.createElement("span");
    span.className = "t-" + token.kind;
    span.textContent = text.slice(token.from, token.to);
    code.appendChild(span);
    at = token.to;
  }
  if (at < text.length) {
    appendText(code, text.slice(at));
  }
}

function appendText(parent, text) {
  const lines = text.split("\n");
  lines.forEach(function (line, index) {
    if (index > 0) {
      parent.appendChild(document.createElement("br"));
    }
    if (line) {
      parent.appendChild(document.createTextNode(line));
    }
  });
}
