/*
 * grammar.mjs - TorbScript for Monaco: a Monarch grammar, which colours the code at once, before the language server
 * has checked it, and colours the ` ```trb ` blocks of hover and completion documentation; and the language
 * configuration of the VS Code extension (editors/vscode/language-configuration.json), read from there so the two
 * editors comment, close brackets and indent alike.
 *
 * The grammar follows editors/vscode/syntaxes/trb.tmLanguage.json, the TextMate grammar of the extension, as far as
 * Monarch can: a line or a block comment (a doc comment of its own), the four kinds of string with escapes and `{...}`
 * interpolations whose inside is code, a character, the numbers, the reserved words, a word used as a name after a `.`
 * or before a `:`, `fn name`, a capitalized name as a type or - behind a `.` or as `Some`, `None`, `Ok`, `Fail` - as a
 * case, a call with parentheses or as a command (`print "hi"`, `names.append name`), a label, and every other sign as
 * punctuation. The token names are the kinds of the site's highlighting (`keyword`, `type`, `function`, ...), which the
 * theme of editor.mjs colours with the site's tokens; the server's semantic tokens then say what the grammar can only
 * guess: a parameter, a mutable binding, a method called by a name the grammar took for a variable.
 */

import configuration from "../../editors/vscode/language-configuration.json";

/** The reserved words of the lexer (compiler/src/syntax/token.trb, `fromText`). */
const reserved = [
  "const", "var", "fn", "type", "trait", "extend", "foreign", "case", "use", "public", "private", "native", "shared",
  "lazy", "static", "if", "else", "match", "for", "in", "while", "loop", "break", "continue", "return", "true",
  "false", "void", "self", "Self", "with", "where",
];

const reservedPattern = reserved.join("|");

/** What may follow a name called as a command: one space, then a literal, a bracket or a name that is not reserved. */
const commandArgument = `(?= (?:["'\\[0-9]|raw"|(?!(?:${reservedPattern}|as|by|from)\\b)[A-Za-z_]))`;

export const monarch = {
  defaultToken: "",
  tokenPostfix: "",
  keywords: reserved,
  brackets: [
    { open: "{", close: "}", token: "punctuation" },
    { open: "[", close: "]", token: "punctuation" },
    { open: "(", close: ")", token: "punctuation" },
  ],
  tokenizer: {
    root: [
      { include: "@code" },
      [/[{}]/, "punctuation"],
    ],

    code: [
      { include: "@whitespace" },
      { include: "@strings" },
      [/'(?:\\(?:u\{[0-9A-Fa-f]+\}|.)|[^'\\])'/, "string"],
      [/0x[0-9A-Fa-f][0-9A-Fa-f_]*/, "number"],
      [/0b[01][01_]*/, "number"],
      [/\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?/, "number"],

      // A word after a `.`: a case when it is capitalized, a method when it is called, and a property otherwise -
      // a reserved word included (`event.type`)
      [/(\.)([A-Z]\w*)/, ["punctuation", "enumMember"]],
      [/(\.)([a-z_]\w*)(?=\s*(?:<[^<>()=]*>)?\()/, ["punctuation", "method"]],
      [/(\.)([a-z_]\w*)(?=\s*\{)/, ["punctuation", "method"]],
      [new RegExp(`(\\.)([a-z_]\\w*)${commandArgument}`), ["punctuation", "method"]],
      [/(\.)([a-z_]\w*)/, ["punctuation", "property"]],

      // Declarations: the name of a function, of a case, of a binding
      [/(fn)(\s+)([A-Za-z_]\w*)/, ["keyword", "", "function"]],
      [/(case)(\s+)([A-Z]\w*)/, ["keyword", "", "enumMember"]],
      [/(const|var)(\s+)(?!(?:self|type|fn|const)\b)([a-z_]\w*)/, ["keyword", "", "variable"]],
      [/(protected)(?=\s+var\b)/, "keyword"],
      [/(from)(?=\s*")/, "keyword"],

      // A reserved word before a `:` is a name: a field or a label (`type: String`)
      [new RegExp(`(?:${reservedPattern})(?=\\s*:(?!:))`), "property"],
      // A label: a named argument, a parameter, a field
      [/([a-z_]\w*)(\s*)(:)(?!:)/, ["parameter", "", "punctuation"]],

      [/[A-Z]\w*/, { cases: { "Self": "keyword", "Some|None|Ok|Fail": "enumMember", "@default": "type" } }],
      [/[a-z_]\w*(?=\s*(?:<[^<>()=]*>)?\()/, { cases: { "@keywords": "keyword", "@default": "function" } }],
      [new RegExp(`[a-z_]\\w*${commandArgument}`), { cases: { "@keywords": "keyword", "by|as": "keyword", "@default": "function" } }],
      [/[a-z_]\w*/, { cases: { "@keywords": "keyword", "by": "keyword", "@default": "variable" } }],

      [/[()[\]]/, "@brackets"],
      [/[=<>!?:&|+\-*/%^~,.;@#$]/, "punctuation"],
    ],

    whitespace: [
      [/[ \t\r\n]+/, ""],
      [/\/\*\*(?!\/)/, "comment.doc", "@documentation"],
      [/\/\*/, "comment", "@comment"],
      [/\/\/.*$/, "comment"],
    ],

    comment: [
      [/[^/*]+/, "comment"],
      [/\/\*/, "comment", "@push"],
      [/\*\//, "comment", "@pop"],
      [/[/*]/, "comment"],
    ],

    documentation: [
      [/[^/*]+/, "comment.doc"],
      [/\/\*/, "comment.doc", "@push"],
      [/\*\//, "comment.doc", "@pop"],
      [/[/*]/, "comment.doc"],
    ],

    strings: [
      [/raw"""/, "string", "@rawTriple"],
      [/raw"/, "string", "@raw"],
      [/"""/, "string", "@triple"],
      [/"/, "string", "@string"],
    ],

    string: [
      [/[^\\"{]+/, "string"],
      [/\\(?:u\{[0-9A-Fa-f]+\}|.)/, "string.escape"],
      [/\{/, "string", "@interpolation"],
      [/"/, "string", "@pop"],
    ],

    triple: [
      [/[^\\"{]+/, "string"],
      [/\\(?:u\{[0-9A-Fa-f]+\}|.)/, "string.escape"],
      [/\{/, "string", "@interpolation"],
      [/"""/, "string", "@pop"],
      [/"/, "string"],
    ],

    raw: [
      [/[^"]+/, "string"],
      [/"/, "string", "@pop"],
    ],

    rawTriple: [
      [/[^"]+/, "string"],
      [/"""/, "string", "@pop"],
      [/"/, "string"],
    ],

    // The inside of `{...}` in a string is code, and a closure's braces inside it are counted
    interpolation: [
      [/\}/, "string", "@pop"],
      [/\{/, "punctuation", "@braces"],
      { include: "@code" },
    ],

    braces: [
      [/\}/, "punctuation", "@pop"],
      [/\{/, "punctuation", "@braces"],
      { include: "@code" },
    ],
  },
};

/** A pattern of the configuration's JSON as a regular expression. */
function pattern(source) {
  return source === undefined ? undefined : new RegExp(source);
}

/** `indent`, `indentOutdent`, `none` and `outdent` of the JSON as Monaco's `IndentAction`. */
const indentActions = { none: 0, indent: 1, indentOutdent: 2, outdent: 3 };

/** The language configuration of the VS Code extension, in Monaco's shape. */
export const languageConfiguration = {
  comments: configuration.comments,
  brackets: configuration.brackets,
  autoClosingPairs: configuration.autoClosingPairs,
  surroundingPairs: configuration.surroundingPairs.map(([open, close]) => ({ open: open, close: close })),
  indentationRules: {
    increaseIndentPattern: pattern(configuration.indentationRules.increaseIndentPattern),
    decreaseIndentPattern: pattern(configuration.indentationRules.decreaseIndentPattern),
  },
  onEnterRules: configuration.onEnterRules.map((rule) => ({
    beforeText: pattern(rule.beforeText),
    afterText: pattern(rule.afterText),
    action: {
      indentAction: indentActions[rule.action.indent] || 0,
      appendText: rule.action.appendText,
      removeText: rule.action.removeText,
    },
  })),
  wordPattern: pattern(configuration.wordPattern),
};
