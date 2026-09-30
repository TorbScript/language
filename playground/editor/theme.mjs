/*
 * theme.mjs - the site's look as a Monaco theme. Monaco wants colours as values, so they are read from the tokens of
 * brand/tokens.css on the page (`--torb-syntax-keyword`, `--torb-surface-raised`, ...) whenever the theme of the page
 * changes - the site's switch sets `data-theme` on <html>, and without it the system's preference decides - and the
 * theme is defined anew from them. So there is one source of every colour, and both themes follow by themselves.
 *
 * The code has the colours of docs/design/BRAND.md section 9, a mutable name its underline, and a deprecated one a
 * line through it. The widgets - completion, hover, signature help, rename - are raised surfaces with a hairline, as
 * the site's menus are; playground.css adds what a theme cannot say (the overlay's shadow, the type of the prose).
 */

/** The token kinds of the grammar and of the semantic legend, each with the site's colour it takes. */
const tokenColours = {
  keyword: "--torb-syntax-keyword",
  type: "--torb-syntax-type",
  interface: "--torb-syntax-type",
  typeParameter: "--torb-syntax-type",
  namespace: "--torb-syntax-type",
  function: "--torb-syntax-function",
  method: "--torb-syntax-function",
  string: "--torb-syntax-string",
  number: "--torb-syntax-number",
  enumMember: "--torb-syntax-case",
  parameter: "--torb-syntax-parameter",
  variable: "--torb-syntax-variable",
  property: "--torb-syntax-variable",
  comment: "--torb-syntax-comment",
  punctuation: "--torb-syntax-punctuation",
};

/** A token of CSS as `#rrggbb` or `#rrggbbaa`, as Monaco reads a colour: through the browser, whatever its syntax. */
function hexOf(value, probe) {
  probe.style.color = "";
  probe.style.color = value;
  const computed = getComputedStyle(probe).color;
  const found = /rgba?\(([\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)(?:[ ,/]+([\d.]+%?))?\)/.exec(computed);
  if (!found) {
    return undefined;
  }
  const channel = (number) => Math.round(Number(number)).toString(16).padStart(2, "0");
  let hex = "#" + channel(found[1]) + channel(found[2]) + channel(found[3]);
  if (found[4] !== undefined) {
    const alpha = found[4].endsWith("%") ? Number(found[4].slice(0, -1)) / 100 : Number(found[4]);
    if (alpha < 1) {
      hex += channel(alpha * 255);
    }
  }
  return hex;
}

/** The same colour with an opacity from 0 to 1. */
function withAlpha(hex, alpha) {
  return hex === undefined ? undefined : hex.slice(0, 7) + Math.round(alpha * 255).toString(16).padStart(2, "0");
}

/**
 * The theme of the page as it is now: `{ name, dark, data }`, `data` Monaco's `IStandaloneThemeData`. The names differ
 * by theme, so that Monaco sees a change even where it caches a theme by its name.
 */
export function currentTheme() {
  const style = getComputedStyle(document.documentElement);
  const probe = document.createElement("span");
  probe.style.display = "none";
  document.body.appendChild(probe);
  const token = (name) => hexOf(style.getPropertyValue(name).trim(), probe);
  const colour = {
    background: token("--torb-background"),
    surface: token("--torb-surface"),
    raised: token("--torb-surface-raised"),
    hover: token("--torb-surface-hover"),
    active: token("--torb-surface-active"),
    code: token("--torb-code"),
    border: token("--torb-border"),
    strong: token("--torb-border-strong"),
    control: token("--torb-border-control"),
    text: token("--torb-text"),
    muted: token("--torb-text-muted"),
    subtle: token("--torb-text-subtle"),
    link: token("--torb-link"),
    focus: token("--torb-focus"),
    selection: token("--torb-selection"),
    error: token("--torb-error-accent"),
    warning: token("--torb-warning-accent"),
    info: token("--torb-info-accent"),
    variable: token("--torb-syntax-variable"),
  };
  const syntax = {};
  for (const [kind, name] of Object.entries(tokenColours)) {
    syntax[kind] = token(name);
  }
  probe.remove();

  const dark = luminance(colour.background) < 0.4;
  const rules = [{ token: "", foreground: strip(colour.variable) }];
  for (const [kind, value] of Object.entries(syntax)) {
    rules.push({ token: kind, foreground: strip(value) });
    // A semantic token's modifiers follow its type, `mutable` first (protocol.mjs, `tokenLegend`)
    rules.push({ token: `${kind}.mutable`, foreground: strip(value), fontStyle: "underline" });
    rules.push({ token: `${kind}.mutable.deprecated`, foreground: strip(value), fontStyle: "underline strikethrough" });
    rules.push({ token: `${kind}.deprecated`, foreground: strip(value), fontStyle: "strikethrough" });
  }
  rules.push({ token: "comment.doc", foreground: strip(syntax.comment) });
  rules.push({ token: "string.escape", foreground: strip(syntax.string) });

  const transparent = "#00000000";
  const colors = {
    focusBorder: colour.focus,
    foreground: colour.text,
    descriptionForeground: colour.muted,
    errorForeground: colour.error,
    // The context menu's shadow, which the stylesheet cannot reach: faint on light, none on dark, where its hairline is
    "widget.shadow": dark ? transparent : withAlpha(colour.text, 0.12),
    "widget.border": colour.border,
    "editor.background": colour.code,
    "editor.foreground": colour.variable,
    "editorCursor.foreground": colour.text,
    "editor.selectionBackground": colour.selection,
    "editor.inactiveSelectionBackground": withAlpha(colour.selection, 0.7),
    "editor.selectionHighlightBackground": transparent,
    "editor.wordHighlightBackground": transparent,
    "editor.wordHighlightStrongBackground": transparent,
    "editor.findMatchHighlightBackground": withAlpha(colour.selection, 0.7),
    "editor.lineHighlightBackground": transparent,
    "editor.lineHighlightBorder": transparent,
    "editor.rangeHighlightBackground": withAlpha(colour.selection, 0.6),
    "editor.hoverHighlightBackground": withAlpha(colour.active, 0.6),
    "editorLink.activeForeground": colour.link,
    "editorLineNumber.foreground": colour.subtle,
    "editorLineNumber.activeForeground": colour.muted,
    "editorIndentGuide.background1": colour.border,
    "editorIndentGuide.activeBackground1": colour.strong,
    "editorWhitespace.foreground": colour.border,
    "editorBracketMatch.background": withAlpha(colour.active, 0.8),
    "editorBracketMatch.border": colour.control,
    "editorGutter.background": colour.code,
    "editorError.foreground": colour.error,
    "editorWarning.foreground": colour.warning,
    "editorInfo.foreground": colour.info,
    "editorHint.foreground": colour.muted,
    "editorUnnecessaryCode.opacity": "#000000a0",
    "editorOverviewRuler.border": transparent,
    "editorLightBulb.foreground": colour.warning,
    "editorLightBulbAutoFix.foreground": colour.info,
    "editorWidget.background": colour.raised,
    "editorWidget.foreground": colour.text,
    "editorWidget.border": colour.border,
    "editorWidget.resizeBorder": colour.strong,
    "editorHoverWidget.background": colour.raised,
    "editorHoverWidget.foreground": colour.text,
    "editorHoverWidget.border": colour.border,
    "editorHoverWidget.highlightForeground": colour.text,
    "editorHoverWidget.statusBarBackground": colour.surface,
    "editorSuggestWidget.background": colour.raised,
    "editorSuggestWidget.foreground": colour.text,
    "editorSuggestWidget.border": colour.border,
    "editorSuggestWidget.selectedBackground": colour.active,
    "editorSuggestWidget.selectedForeground": colour.text,
    "editorSuggestWidget.selectedIconForeground": colour.text,
    "editorSuggestWidget.highlightForeground": colour.text,
    "editorSuggestWidget.focusHighlightForeground": colour.text,
    "editorSuggestWidgetStatus.foreground": colour.muted,
    "list.hoverBackground": colour.hover,
    "list.hoverForeground": colour.text,
    "list.activeSelectionBackground": colour.active,
    "list.activeSelectionForeground": colour.text,
    "list.activeSelectionIconForeground": colour.text,
    "list.inactiveSelectionBackground": colour.active,
    "list.focusBackground": colour.active,
    "list.focusForeground": colour.text,
    "list.focusOutline": transparent,
    "list.focusAndSelectionOutline": transparent,
    "list.highlightForeground": colour.text,
    "list.focusHighlightForeground": colour.text,
    "list.deemphasizedForeground": colour.subtle,
    "input.background": colour.background,
    "input.foreground": colour.text,
    "input.border": colour.control,
    "input.placeholderForeground": colour.subtle,
    "inputOption.activeBorder": colour.control,
    "inputValidation.errorBackground": colour.raised,
    "inputValidation.errorBorder": colour.error,
    "textLink.foreground": colour.link,
    "textLink.activeForeground": colour.link,
    "textCodeBlock.background": colour.code,
    "textPreformat.foreground": colour.variable,
    "textPreformat.background": transparent,
    "textBlockQuote.background": colour.surface,
    "textBlockQuote.border": colour.strong,
    "textSeparator.foreground": colour.border,
    "toolbar.hoverBackground": colour.hover,
    "menu.background": colour.raised,
    "menu.foreground": colour.text,
    "menu.selectionBackground": colour.active,
    "menu.selectionForeground": colour.text,
    "menu.separatorBackground": colour.border,
    "menu.border": colour.border,
    "keybindingLabel.background": colour.surface,
    "keybindingLabel.foreground": colour.muted,
    "keybindingLabel.border": colour.border,
    "keybindingLabel.bottomBorder": colour.border,
    "scrollbar.shadow": transparent,
    "scrollbarSlider.background": withAlpha(colour.subtle, 0.35),
    "scrollbarSlider.hoverBackground": withAlpha(colour.subtle, 0.55),
    "scrollbarSlider.activeBackground": withAlpha(colour.subtle, 0.7),
    "editorCodeLens.foreground": colour.muted,
    "peekView.border": colour.strong,
    "problemsErrorIcon.foreground": colour.error,
    "problemsWarningIcon.foreground": colour.warning,
    "problemsInfoIcon.foreground": colour.info,
    "symbolIcon.methodForeground": syntax.function,
    "symbolIcon.functionForeground": syntax.function,
    "symbolIcon.constructorForeground": syntax.function,
    "symbolIcon.fieldForeground": colour.muted,
    "symbolIcon.propertyForeground": colour.muted,
    "symbolIcon.variableForeground": colour.muted,
    "symbolIcon.constantForeground": colour.muted,
    "symbolIcon.classForeground": syntax.type,
    "symbolIcon.structForeground": syntax.type,
    "symbolIcon.interfaceForeground": syntax.type,
    "symbolIcon.typeParameterForeground": syntax.type,
    "symbolIcon.moduleForeground": syntax.type,
    "symbolIcon.enumeratorForeground": syntax.type,
    "symbolIcon.enumeratorMemberForeground": syntax.enumMember,
    "symbolIcon.keywordForeground": syntax.keyword,
    "symbolIcon.snippetForeground": colour.muted,
    "symbolIcon.textForeground": colour.muted,
  };
  for (const key of Object.keys(colors)) {
    if (colors[key] === undefined) {
      delete colors[key];
    }
  }
  return {
    name: dark ? "torb-dark" : "torb-light",
    dark: dark,
    data: { base: dark ? "vs-dark" : "vs", inherit: true, rules: rules, colors: colors },
  };
}

/** A colour without its `#`, as a rule of Monaco's token theme takes it, and without an opacity it cannot take. */
function strip(hex) {
  return hex === undefined ? undefined : hex.slice(1, 7);
}

/** The relative luminance of a colour, 0 for black and 1 for white. */
function luminance(hex) {
  if (hex === undefined) {
    return 1;
  }
  const channel = (index) => {
    const value = parseInt(hex.slice(index, index + 2), 16) / 255;
    return value <= 0.03928 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
}
