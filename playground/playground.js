/*
 * playground.js - TorbScript in the page: an editor, a Run button and the output, with the whole toolchain running in
 * the browser as WebAssembly and no server (docs/design/RELEASE.md section 6, "The playground").
 *
 * The contract with the site is `docs/tooling/torb-docs-site.md`, "Runnable blocks and exercises": the site's script
 * loads this file where a page has a mount, and calls `TorbPlayground.mount(element, options)` for each one. Nothing is
 * mounted here by itself. What this file loads besides - the workers, `torb.js`, `torb.wasm`, the editor and the
 * examples - it loads relative to its own URL and with its own query, so a new version of the site never pairs an old
 * page with a new toolchain.
 *
 *   TorbPlayground.mount(element, { source, file, expectedOutput, solution, labels, onResult })
 *   TorbPlayground.run(source, file?)  ->  Promise<{ output, errors, exitCode, diagnostics, milliseconds }>
 *   TorbPlayground.stop()               stops the program that runs
 *
 * The patterns are the ones other languages' playgrounds settled on: Run and Ctrl or Cmd+Enter (the Rust Playground),
 * the output under the editor with the places of the checker's errors as links into the code (the Go Playground), Reset
 * and Show solution for an exercise (the Svelte tutorial), the source compressed into the address as the share link
 * (the TypeScript playground), and a gallery of examples on the page `/play` (the playgrounds of Go, Kotlin and Rust).
 * The compiler runs in a worker, so a program that never ends never freezes the page: Run becomes Stop.
 *
 * **The editor.** A mount starts as a static view of its code: the site's own highlighting in a frame of exactly the
 * size, type and spacing of the editor that replaces it. Where the reader is about to write - at once on `/play`, when
 * the pointer comes over a block or the focus into it elsewhere - it becomes Monaco (`playground-editor.js`, built
 * from `editor/`), and the swap moves nothing. Its first focus connects it to `torb lsp`, which runs in a worker of
 * its own for the whole page: completion, the checker's diagnostics while typing, hover, signature help, definition,
 * rename, formatting and the semantic colours are the language server's. A run never waits for it, and it never waits
 * for a run. A device of touch alone, where Monaco is not supported, keeps a textarea under a coloured copy of itself
 * (the Gleam tour's editor) instead.
 *
 * Plain JavaScript without a build step, loaded as a classic script or as a module.
 */
(function () {
  "use strict";

  // ------------------------------------------------------------------------------------------ where the files are --

  // A classic script knows itself; a module finds the element that loaded it
  const script = document.currentScript || document.querySelector('script[src*="playground.js"]');
  const scriptAddress = new URL(script && script.src ? script.src : "playground.js", location.href);
  /** The query of this script (`?v=...`), carried to every file it loads so that they are of one build. */
  const version = scriptAddress.search;

  function fileAddress(name) {
    return new URL(name + version, scriptAddress).href;
  }

  const defaultLabels = {
    run: "Run",
    stop: "Stop",
    reset: "Reset",
    solution: "Show solution",
    output: "Output",
    expected: "Expected output",
    solved: "Solved",
    unsolved: "Not yet",
    running: "Running…",
    loading: "Loading the toolchain…",
    exitCode: "Exit code",
    stopped: "Stopped",
    tooMuchOutput: "Stopped: the program printed more than a megabyte",
    crashed: "The toolchain stopped unexpectedly",
    unsupported: "This browser cannot run the playground: it needs WebAssembly with exception handling",
    examples: "Examples",
    replace: "Replace your code with this example?",
    format: "Format",
    source: "TorbScript source",
  };

  // --------------------------------------------------------------------------------------------- the highlighting --

  const keywords = new Set([
    "const", "var", "fn", "type", "trait", "extend", "foreign", "case", "use", "public", "private", "native",
    "shared", "lazy", "if", "else", "match", "for", "in", "while", "loop", "break", "continue", "return", "true",
    "false", "void", "self", "Self", "with", "where", "static", "from", "as", "by", "using", "await",
  ]);

  function escapeHtml(text) {
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  /**
   * TorbScript as tokens `{ from, to, kind }` in the kinds of the site's token classes (`t-keyword`, `t-type`, ...),
   * which the site's stylesheet colours as BRAND.md section 9 says. The site colours with the compiler's resolver when
   * it is built, and the editor with the language server once it has checked the text; this is the lexer's half, fast
   * enough for every keystroke: a capitalized name is a type, one behind a dot a case, a name in front of a
   * parenthesis or of an argument a function. What lies between two tokens is white space.
   */
  function tokenize(code) {
    const tokens = [];
    let index = 0;
    const length = code.length;
    // What a closing brace ends: an interpolation of a string resumes the string
    const braces = [];
    let previousWord = "";

    function add(kind, from, to) {
      if (to > from) {
        tokens.push({ from: from, to: to, kind: kind });
      }
    }

    while (index < length) {
      const character = code[index];
      const next = code[index + 1];
      if (character === "/" && next === "/") {
        const end = code.indexOf("\n", index);
        const stop = end < 0 ? length : end;
        add("comment", index, stop);
        index = stop;
        continue;
      }
      if (character === "/" && next === "*") {
        const end = code.indexOf("*/", index + 2);
        const stop = end < 0 ? length : end + 2;
        add("comment", index, stop);
        index = stop;
        continue;
      }
      if (character === '"' || (code.startsWith('raw"', index) && !isWordCharacter(code[index - 1]))) {
        const result = readString(code, index, add);
        index = result.end;
        if (result.interpolation) {
          braces.push("string:" + result.quotes);
        }
        continue;
      }
      if (character === "'") {
        let end = index + 1;
        while (end < length && code[end] !== "'" && code[end] !== "\n") {
          end += code[end] === "\\" ? 2 : 1;
        }
        end = Math.min(length, end + 1);
        add("string", index, end);
        index = end;
        continue;
      }
      if (character === "{") {
        braces.push("block");
        add("punctuation", index, index + 1);
        index += 1;
        continue;
      }
      if (character === "}") {
        const opened = braces.pop();
        if (opened && opened.startsWith("string:")) {
          const quotes = opened.slice("string:".length);
          add("string", index, index + 1);
          const result = readStringRest(code, index + 1, quotes, false, add);
          index = result.end;
          if (result.interpolation) {
            braces.push(opened);
          }
          continue;
        }
        add("punctuation", index, index + 1);
        index += 1;
        continue;
      }
      if (isDigit(character)) {
        const match = /^(0x[0-9a-fA-F_]+|0b[01_]+|[0-9][0-9_]*(\.[0-9][0-9_]*)?([eE][+-]?[0-9]+)?)/.exec(code.slice(index, index + 64));
        const text = match ? match[0] : character;
        add("number", index, index + text.length);
        index += text.length;
        continue;
      }
      if (isWordStart(character)) {
        let end = index + 1;
        while (end < length && isWordCharacter(code[end])) {
          end += 1;
        }
        const word = code.slice(index, end);
        const before = code[index - 1];
        let kind;
        if (keywords.has(word) && before !== ".") {
          kind = "keyword";
        } else if ((before === "." && /^[A-Z]/.test(word)) || preludeCases.has(word)) {
          kind = "enumMember";
        } else if (/^[A-Z]/.test(word)) {
          kind = "type";
        } else if (previousWord === "fn" || code[end] === "(" || isCommandCall(code, end)) {
          kind = before === "." ? "method" : "function";
        } else if (before === ".") {
          kind = "property";
        } else {
          kind = "variable";
        }
        add(kind, index, end);
        previousWord = word;
        index = end;
        continue;
      }
      if (character === " " || character === "\n" || character === "\t" || character === "\r") {
        index += 1;
        continue;
      }
      add("punctuation", index, index + 1);
      index += 1;
    }
    return tokens;
  }

  /** TorbScript as HTML: the tokens as spans, one span per line, so that every line of the result stands on its own. */
  function highlight(code) {
    let html = "";
    let at = 0;
    for (const token of tokenize(code)) {
      html += escapeHtml(code.slice(at, token.from)) + span(token.kind, code.slice(token.from, token.to));
      at = token.to;
    }
    return html + escapeHtml(code.slice(at));
  }

  function span(kind, text) {
    return text.split("\n").map(function (line) {
      return line === "" ? "" : '<span class="t-' + kind + '">' + escapeHtml(line) + "</span>";
    }).join("\n");
  }

  /** The cases the prelude imports, which a program writes bare. */
  const preludeCases = new Set(["Some", "None", "Ok", "Fail"]);

  /**
   * Whether the name that ends at `end` is called as a command - `print "hello"`, `names.append name` - which is one
   * space and then the start of an argument: a literal, a bracket or a name that is not a keyword.
   */
  function isCommandCall(code, end) {
    if (code[end] !== " ") {
      return false;
    }
    const next = code[end + 1];
    if (next === '"' || next === "'" || next === "[" || isDigit(next)) {
      return true;
    }
    if (next === undefined || !isWordStart(next)) {
      return false;
    }
    const word = /^[A-Za-z_][A-Za-z0-9_]*/.exec(code.slice(end + 1, end + 64));
    return word !== null && !keywords.has(word[0]);
  }

  function isDigit(character) {
    return character >= "0" && character <= "9";
  }

  function isWordStart(character) {
    return /[A-Za-z_À-￿]/.test(character);
  }

  function isWordCharacter(character) {
    return character !== undefined && /[A-Za-z0-9_À-￿]/.test(character);
  }

  /** A string from its opening quote: `"..."`, `"""..."""`, `raw"..."`, up to its end or its first interpolation. */
  function readString(code, start, add) {
    let index = start;
    const raw = code.startsWith("raw", index);
    if (raw) {
      index += 3;
    }
    const quotes = code.startsWith('"""', index) ? '"""' : '"';
    index += quotes.length;
    add("string", start, index);
    const rest = readStringRest(code, index, quotes, raw, add);
    return { end: rest.end, interpolation: rest.interpolation, quotes: quotes };
  }

  function readStringRest(code, start, quotes, raw, add) {
    let index = start;
    while (index < code.length) {
      if (code.startsWith(quotes, index)) {
        index += quotes.length;
        add("string", start, index);
        return { end: index, interpolation: false };
      }
      const character = code[index];
      if (character === "\n" && quotes === '"') {
        break;
      }
      if (character === "\\" && !raw) {
        index += 2;
        continue;
      }
      if (character === "{" && !raw) {
        add("string", start, index + 1);
        return { end: index + 1, interpolation: true };
      }
      index += 1;
    }
    add("string", start, index);
    return { end: index, interpolation: false };
  }

  // --------------------------------------------------------------------------------------------- the diagnostics --

  /** Where a diagnostic points (` --> main.trb:3:1`) or a panic happened (`  at main.trb:3:7`). */
  const placePattern = /^(\s*(?:-->|at) )(.+):(\d+):(\d+)(\s*)$/;

  /**
   * The diagnostics in what `torb run` wrote on standard error: the checker's, in the form `torb check` prints them,
   * and a panic with the place it happened at.
   *
   *     error: Cannot find `prnt` here          panic: index 5 is out of bounds for a length of 3
   *      --> main.trb:3:1                         at main.trb:3:7
   *
   * Answers `{ line, column, message }` for each, line and column from 1.
   */
  function diagnosticsOf(errors, file) {
    const found = [];
    let message = null;
    for (const line of errors.split("\n")) {
      const heading = /^(error|warning|panic): (.*)$/.exec(line);
      if (heading) {
        message = heading[2];
        continue;
      }
      const place = placePattern.exec(line);
      if (place && message !== null && isFile(place[2], file)) {
        found.push({ line: Number(place[3]), column: Number(place[4]), message: message });
        message = null;
      }
    }
    return found;
  }

  function isFile(shown, file) {
    return shown === file || shown.endsWith("/" + file);
  }

  // ------------------------------------------------------------------------------------------------ the toolchain --

  /**
   * The compiled `torb.wasm`, fetched once per page when the first run or the language server asks for it (or a reader
   * is about to), and kept: the browser's HTTP cache keeps the file between pages, and its engine keeps the compiled
   * code. Every worker of the page instantiates this one module.
   */
  let compiling = null;
  let compiled = false;

  function compiledToolchain() {
    if (compiling === null) {
      const address = fileAddress("torb.wasm");
      compiling = (async function () {
        if (typeof WebAssembly !== "object" || typeof WebAssembly.compile !== "function") {
          throw new Error("unsupported");
        }
        let module;
        try {
          module = await WebAssembly.compileStreaming(fetch(address));
        } catch (problem) {
          // A server that does not send `application/wasm` refuses the streaming compile; the bytes still compile
          const response = await fetch(address);
          if (!response.ok) {
            throw new Error("torb.wasm: " + response.status + " " + response.statusText);
          }
          module = await WebAssembly.compile(await response.arrayBuffer());
        }
        compiled = true;
        return module;
      })();
      compiling.catch(function () {
        compiling = null;
      });
    }
    return compiling;
  }

  /**
   * The worker that runs `torb`: one per page, started on the first run and replaced when a run is stopped or the
   * toolchain crashed. Every run instantiates the toolchain anew, so a program always starts from an empty file system
   * and a fresh memory.
   */
  let worker = null;
  let running = null;
  let queue = Promise.resolve();

  function startWorker() {
    worker = new Worker(fileAddress("playground-worker.js"));
    worker.onmessage = function (event) {
      if (running) {
        running.receive(event.data);
      }
    };
    worker.onerror = function (event) {
      event.preventDefault();
      if (running) {
        running.receive({ kind: "done", exitCode: null, crash: event.message || "the worker failed" });
      }
    };
  }

  /**
   * Runs `source` as `main.trb` (or `file`) in the VM of the WebAssembly `torb` - check, lower, run - and answers what
   * it wrote, its exit code and the diagnostics found in what it wrote on standard error. `observe` is told every piece
   * of output as it arrives. Runs wait for each other: one worker runs one program at a time. `job`, when given, is
   * the handle [stop] takes: `{}` of the caller, which learns nothing else about the queue.
   */
  function run(source, observe, file, job) {
    const handle = job || {};
    const result = queue.then(function () {
      if (handle.stopped) {
        return stoppedResult("stopped");
      }
      return runNow(source, observe || function () {}, file || "main.trb", handle);
    });
    queue = result.catch(function () {});
    return result;
  }

  function stoppedResult(why) {
    return { output: "", errors: "", exitCode: null, crash: null, stopped: why, diagnostics: [], milliseconds: 0 };
  }

  async function runNow(source, observe, file, job) {
    const module = await compiledToolchain();
    if (job.stopped) {
      return stoppedResult("stopped");
    }
    if (worker === null) {
      startWorker();
    }
    return new Promise(function (resolve) {
      let output = "";
      let errors = "";
      const started = performance.now();
      running = {
        job: job,
        receive: function (message) {
          if (message.kind === "output") {
            output += message.output;
            errors += message.errors;
            observe(message.output, message.errors);
            return;
          }
          if (message.kind === "done") {
            running = null;
            if (message.stopped || message.crash) {
              stopWorker();
            }
            resolve({
              output: output,
              errors: errors,
              exitCode: message.exitCode,
              crash: message.crash || null,
              stopped: message.stopped || null,
              diagnostics: diagnosticsOf(errors, file),
              milliseconds: Math.round(performance.now() - started),
              timings: message.timings || null,
            });
          }
        },
      };
      worker.postMessage({ kind: "run", module: module, source: source, file: file });
    });
  }

  function stopWorker() {
    if (worker !== null) {
      worker.terminate();
      worker = null;
    }
  }

  /**
   * Stops the run of `job`: the program, where it runs - its worker is ended, and the next run starts another - or
   * the run itself, where it still waits for the one before it.
   */
  function stop(job) {
    const handle = job || (running && running.job);
    if (!handle) {
      return;
    }
    handle.stopped = true;
    if (running && running.job === handle) {
      const current = running;
      stopWorker();
      current.receive({ kind: "done", exitCode: null, stopped: "stopped" });
    }
  }

  // ------------------------------------------------------------------------------------------ the language server --

  /**
   * The editor, `playground-editor.js` - Monaco and its bridge to the language server - loaded once per page when the
   * first mount is about to be used: at once on `/play`, when the pointer comes over a block or the focus into it
   * elsewhere. A browser that cannot load it keeps the static view, which still runs.
   */
  let loadingEditor = null;

  function editorModule() {
    if (loadingEditor === null) {
      loadingEditor = import(fileAddress("playground-editor.js")).then(function (module) {
        return module.ready.then(function () {
          return module;
        });
      });
      loadingEditor.catch(function () {
        loadingEditor = null;
      });
    }
    return loadingEditor;
  }

  /** The client of the page's language server, made by the first editor that is used. */
  let languageClient = null;
  /** How often the language worker may end before the page stops starting it again. */
  let languageRestarts = 3;

  /**
   * The language server of the page: `torb lsp` in a worker of its own (playground-worker.js, `{ kind: "language" }`),
   * which lives as long as the page and holds every document of it. The client is the editor's (editor/protocol.mjs),
   * and the transport the worker's messages. A server that ends - it panicked on something the reader wrote - is
   * started again and told the open documents anew, a few times.
   */
  function languageServer(module) {
    if (languageClient !== null) {
      return languageClient;
    }
    const handlers = [];
    let languageWorker = null;
    let pending = [];
    const transport = {
      send: function (message) {
        if (languageWorker !== null) {
          languageWorker.postMessage({ kind: "lsp", message: message });
        } else {
          pending.push(message);
        }
      },
      subscribe: function (handler) {
        handlers.push(handler);
      },
    };

    function start() {
      compiledToolchain().then(function (compiledModule) {
        const started = new Worker(fileAddress("playground-worker.js"));
        started.onmessage = function (event) {
          const message = event.data;
          if (message.kind === "lsp") {
            for (const handler of handlers.slice()) {
              handler(message.message);
            }
          } else if (message.kind === "exited") {
            ended(started);
          }
        };
        started.onerror = function (event) {
          event.preventDefault();
          ended(started);
        };
        started.postMessage({ kind: "language", module: compiledModule });
        languageWorker = started;
        for (const message of pending) {
          started.postMessage({ kind: "lsp", message: message });
        }
        pending = [];
      }, function () {
        // No toolchain in this browser: the editor stays an editor
      });
    }

    function ended(which) {
      which.terminate();
      if (languageWorker !== which) {
        return;
      }
      languageWorker = null;
      if (languageRestarts <= 0) {
        return;
      }
      languageRestarts -= 1;
      // A new server knows nothing: the client initializes it and opens every document of the page anew
      pending = [];
      start();
      languageClient.restart();
    }

    start();
    languageClient = module.createClient(transport);
    return languageClient;
  }

  // -------------------------------------------------------------------------------------------------- the editor --

  /**
   * Whether this is a device of touch alone - a phone, a tablet - where Monaco is not supported: its mounts keep the
   * light editor, which is the browser's own text field, and have no language server.
   */
  const touchOnly = Boolean(window.matchMedia && window.matchMedia("(hover: none) and (pointer: coarse)").matches);

  /**
   * The type and the spacing of the code a mount replaces - the site's `pre.code`, which is larger in the panels of the
   * front page than in the documentation - so that neither the light editor nor Monaco moves a character of it.
   */
  function metricsOf(pre) {
    const metrics = { fontSize: 14, lineHeight: 21, paddingTop: 14, paddingRight: 18, paddingBottom: 14, paddingLeft: 18 };
    if (pre === null) {
      return metrics;
    }
    const style = getComputedStyle(pre);
    const number = function (value, fallback) {
      const parsed = parseFloat(value);
      return isFinite(parsed) && parsed > 0 ? parsed : fallback;
    };
    metrics.fontSize = number(style.fontSize, metrics.fontSize);
    metrics.lineHeight = Math.round(number(style.lineHeight, metrics.fontSize * 1.5));
    metrics.paddingTop = number(style.paddingTop, metrics.paddingTop);
    metrics.paddingRight = number(style.paddingRight, metrics.paddingRight);
    metrics.paddingBottom = number(style.paddingBottom, metrics.paddingBottom);
    metrics.paddingLeft = number(style.paddingLeft, metrics.paddingLeft);
    return metrics;
  }

  /** The metrics as the inline style of an element that shows code: nothing of the site's rules can change them. */
  function applyMetrics(target, metrics) {
    target.style.fontSize = metrics.fontSize + "px";
    target.style.lineHeight = metrics.lineHeight + "px";
    target.style.padding = metrics.paddingTop + "px " + metrics.paddingRight + "px " + metrics.paddingBottom + "px " +
      metrics.paddingLeft + "px";
  }

  /**
   * A textarea whose text is transparent over a `<pre>` that shows the same text coloured: typing, selecting, undo and
   * the keyboard stay the browser's own, and the colours follow on every input. Tab indents by two spaces, as the
   * formatter does; Escape and then Tab leaves the editor, so the keyboard is never trapped in it. The editor of a
   * device of touch alone.
   */
  function createLightEditor(container, source, onRun, label, metrics) {
    const frame = element("div", "playground-editor playground-light");
    const shown = element("pre", "playground-highlight");
    shown.setAttribute("aria-hidden", "true");
    const code = element("code");
    shown.appendChild(code);
    const input = element("textarea", "playground-input");
    input.spellcheck = false;
    input.setAttribute("autocapitalize", "off");
    input.setAttribute("autocomplete", "off");
    input.setAttribute("autocorrect", "off");
    input.setAttribute("wrap", "off");
    input.setAttribute("aria-label", label);
    input.value = source;
    // Both have the metrics of the code they replace, so the coloured copy stays under the text
    applyMetrics(shown, metrics);
    applyMetrics(input, metrics);
    frame.appendChild(shown);
    frame.appendChild(input);
    container.appendChild(frame);

    let marks = [];
    let escaped = false;
    const listeners = [];

    function render() {
      const text = input.value;
      const lines = highlight(text + "\n").split("\n");
      // A reported line is underlined from its first character, and the message stands behind it
      for (const mark of marks) {
        const index = mark.line - 1;
        if (index >= 0 && index < lines.length) {
          const parts = /^( *)(.*)$/.exec(lines[index]);
          lines[index] = parts[1] + '<span class="playground-marked">' + parts[2] + "</span>" +
            '<span class="playground-inline">' + escapeHtml(mark.message) + "</span>";
        }
      }
      code.innerHTML = lines.join("\n");
      fit();
    }

    function fit() {
      const count = input.value.split("\n").length;
      input.style.height = count * metrics.lineHeight + metrics.paddingTop + metrics.paddingBottom + 2 + "px";
      shown.scrollTop = input.scrollTop;
      shown.scrollLeft = input.scrollLeft;
    }

    input.addEventListener("input", function () {
      if (marks.length > 0) {
        marks = [];
      }
      render();
      for (const listener of listeners) {
        listener(input.value);
      }
    });
    input.addEventListener("scroll", function () {
      shown.scrollTop = input.scrollTop;
      shown.scrollLeft = input.scrollLeft;
    });
    input.addEventListener("keydown", function (event) {
      // An input method's Enter confirms what it composed, and is none of the editor's business
      if (event.isComposing) {
        return;
      }
      if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
        event.preventDefault();
        onRun();
        return;
      }
      if (event.key === "Escape") {
        escaped = true;
        return;
      }
      if (event.key === "Tab" && !escaped && !event.ctrlKey && !event.metaKey && !event.altKey) {
        event.preventDefault();
        indent(event.shiftKey);
        return;
      }
      if (event.key === "Enter" && !event.shiftKey && !event.altKey) {
        event.preventDefault();
        newLine();
        return;
      }
      escaped = false;
    });

    /** Replaces the selection through the browser's editing, so that undo takes it back. */
    function insert(text, start, end) {
      input.setSelectionRange(start, end);
      if (!document.execCommand || !document.execCommand("insertText", false, text)) {
        input.setRangeText(text, start, end, "end");
        input.dispatchEvent(new Event("input"));
      }
    }

    function newLine() {
      const value = input.value;
      const start = input.selectionStart;
      const lineStart = value.lastIndexOf("\n", start - 1) + 1;
      const current = value.slice(lineStart, start);
      let indentation = /^ */.exec(current)[0];
      if (/[{([]\s*$/.test(current)) {
        indentation += "  ";
      }
      insert("\n" + indentation, start, input.selectionEnd);
    }

    function indent(outdent) {
      const value = input.value;
      const start = input.selectionStart;
      const end = input.selectionEnd;
      if (start === end && !outdent) {
        insert("  ", start, end);
        return;
      }
      const lineStart = value.lastIndexOf("\n", start - 1) + 1;
      const block = value.slice(lineStart, end);
      const changed = block.split("\n").map(function (line) {
        return outdent ? line.replace(/^ {1,2}/, "") : "  " + line;
      }).join("\n");
      insert(changed, lineStart, end);
      input.setSelectionRange(lineStart, lineStart + changed.length);
    }

    render();

    return {
      frame: frame,
      target: input,
      get value() {
        return input.value;
      },
      set value(text) {
        input.value = text;
        marks = [];
        render();
        for (const listener of listeners) {
          listener(text);
        }
      },
      mark: function (diagnostics) {
        marks = diagnostics;
        render();
      },
      moveTo: function (line, column) {
        const lines = input.value.split("\n");
        let offset = 0;
        for (let index = 0; index < line - 1 && index < lines.length; index += 1) {
          offset += lines[index].length + 1;
        }
        offset += Math.max(0, column - 1);
        input.focus();
        input.setSelectionRange(offset, offset);
      },
      focus: function () {
        input.focus();
      },
      onChange: function (listener) {
        listeners.push(listener);
      },
    };
  }

  /**
   * What the reader sees of a mount until Monaco has replaced it: the code, coloured, in a frame of exactly Monaco's
   * size, type and spacing, so the swap moves nothing (docs/tooling/the-playground.md, "The editor"). It shows the
   * site's own highlighting where the page brought it, and the lexer's otherwise, and it is what the keyboard reaches:
   * its focus brings the editor. `seed` is its colours as spans, which Monaco keeps until the language server's come.
   */
  function createStaticView(container, source, options, metrics, highlighted) {
    const frame = element("div", "playground-editor playground-static");
    const shown = element("pre", "playground-static-code");
    const code = element("code");
    shown.appendChild(code);
    frame.appendChild(shown);
    frame.tabIndex = 0;
    frame.setAttribute("role", "textbox");
    frame.setAttribute("aria-multiline", "true");
    frame.setAttribute("aria-label", options.label);
    applyMetrics(shown, metrics);
    // The page numbers its lines as Monaco does: right-aligned in the width of three digits, 12 px before the code
    let gutter = null;
    let digit = 0;
    if (options.page) {
      gutter = element("div", "playground-static-gutter");
      gutter.setAttribute("aria-hidden", "true");
      gutter.style.top = metrics.paddingTop + "px";
      gutter.style.fontSize = metrics.fontSize + "px";
      gutter.style.lineHeight = metrics.lineHeight + "px";
      frame.appendChild(gutter);
      const context = document.createElement("canvas").getContext("2d");
      if (context) {
        const family = getComputedStyle(document.documentElement).getPropertyValue("--torb-font-mono");
        context.font = metrics.fontSize + "px " + (family || "monospace");
        digit = context.measureText("0").width;
      }
    }
    const view = { frame: frame, code: code, seed: null };

    /** Shows the text, coloured with the given HTML where the page brought it, and by the lexer otherwise. */
    view.show = function (text, html) {
      code.innerHTML = html !== undefined ? html : highlight(text);
      const count = text.split("\n").length;
      if (gutter !== null) {
        const width = Math.round(Math.max(3, String(count).length) * digit);
        gutter.style.width = width + "px";
        shown.style.paddingLeft = width + 12 + "px";
        gutter.textContent = Array.from({ length: count }, function (unused, index) {
          return String(index + 1);
        }).join("\n");
      } else {
        shown.style.height = count * metrics.lineHeight + metrics.paddingTop + metrics.paddingBottom + "px";
      }
    };

    if (highlighted !== null && highlighted.textContent === source) {
      view.show(source, highlighted.innerHTML);
      view.seed = spansOf(code);
    } else {
      view.show(source);
      view.seed = tokenize(source).map(function (token) {
        return { from: token.from, to: token.to, classes: "t-" + token.kind };
      });
    }
    container.appendChild(frame);
    return view;
  }

  /**
   * The colours of highlighted code as spans `{ from, to, classes }` in offsets of its text: each `t-` span as it is,
   * and a name outside of one - one the site's highlighting left plain - as a plain variable.
   */
  function spansOf(code) {
    const spans = [];
    let offset = 0;
    const walker = document.createTreeWalker(code, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) {
      const node = walker.currentNode;
      const text = node.nodeValue;
      const owner = node.parentElement !== null && node.parentElement !== code ? node.parentElement.closest("[class]") : null;
      if (owner !== null && code.contains(owner) && /\bt-/.test(owner.className)) {
        spans.push({ from: offset, to: offset + text.length, classes: owner.className });
      } else {
        const names = /[A-Za-z_][A-Za-z0-9_]*/g;
        let found;
        while ((found = names.exec(text)) !== null) {
          spans.push({ from: offset + found.index, to: offset + found.index + found[0].length, classes: "t-variable" });
        }
      }
      offset += text.length;
    }
    return spans;
  }

  /** The offset in the text of `code` under the pointer of `event`, or `null`. */
  function offsetAt(event, code) {
    let node = null;
    let offset = 0;
    if (document.caretPositionFromPoint) {
      const position = document.caretPositionFromPoint(event.clientX, event.clientY);
      if (position) {
        node = position.offsetNode;
        offset = position.offset;
      }
    } else if (document.caretRangeFromPoint) {
      const range = document.caretRangeFromPoint(event.clientX, event.clientY);
      if (range) {
        node = range.startContainer;
        offset = range.startOffset;
      }
    }
    if (node === null || !code.contains(node) || node.nodeType !== 3) {
      return null;
    }
    let total = 0;
    const walker = document.createTreeWalker(code, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) {
      if (walker.currentNode === node) {
        return total + offset;
      }
      total += walker.currentNode.nodeValue.length;
    }
    return null;
  }

  /** The documents of the page's language server: one per mount, `/play`'s is `main.trb`. */
  let documentCount = 0;

  /**
   * The editor of a mount: Monaco connected to the language server once `upgrade` is called and the editor has loaded,
   * the static view until then - or, on a device of touch alone, the light editor for good. Everything else of the
   * mount talks to this, whichever editor is behind it.
   */
  function createEditor(container, source, options) {
    const listeners = [];

    function changed(text) {
      for (const listener of listeners) {
        listener(text);
      }
    }

    if (touchOnly) {
      const light = createLightEditor(container, source, options.onRun, options.label, options.metrics);
      light.onChange(changed);
      return {
        get value() {
          return light.value;
        },
        set value(text) {
          light.value = text;
        },
        mark: light.mark,
        moveTo: light.moveTo,
        focus: light.focus,
        onChange: function (listener) {
          listeners.push(listener);
        },
        format: function () {
          return false;
        },
        hasLanguageServer: false,
        upgrade: function () {
          return Promise.resolve();
        },
        target: light.target,
      };
    }

    const view = createStaticView(container, source, options, options.metrics, options.highlighted);
    let text = source;
    // Monaco once it is there, or - where it could not load - the light editor; the static view until then
    let rich = null;
    let light = null;
    let upgrading = null;
    // Where a click into the static view put the cursor, for the editor that replaces it
    let clickedAt = null;
    let wantsFocus = false;

    function upgrade() {
      if (upgrading === null) {
        upgrading = editorModule().then(function (module) {
          documentCount += 1;
          const uri = options.page ? "file:///torb/work/main.trb"
            : "file:///torb/work/block-" + documentCount + "/" + (options.file || "main.trb");
          const hadFocus = wantsFocus || document.activeElement === view.frame;
          const frame = element("div", "playground-editor playground-monaco");
          const host = element("div", "playground-monaco-host");
          frame.appendChild(host);
          // One step: the frame takes the static view's place, and Monaco lays out in it at the size it will keep
          view.frame.replaceWith(frame);
          rich = module.createEditor(host, {
            doc: text,
            uri: uri,
            page: options.page,
            metrics: options.metrics,
            seed: text === source ? view.seed : null,
            selection: clickedAt !== null ? clickedAt : 0,
            label: options.label,
            labels: options.labels,
            onRun: options.onRun,
            onChange: changed,
            languageClient: function () {
              return languageServer(module);
            },
          });
          if (hadFocus) {
            rich.focus();
          }
          if (typeof options.onUpgrade === "function") {
            options.onUpgrade();
          }
        }).catch(function (problem) {
          // A browser that cannot load the editor gets the light one in its place, which edits and runs as well
          if (typeof console !== "undefined") {
            console.warn("playground: the editor did not load", problem);
          }
          if (rich === null && light === null) {
            light = createLightEditor(document.createElement("div"), text, options.onRun, options.label, options.metrics);
            light.onChange(function (next) {
              text = next;
              changed(next);
            });
            view.frame.replaceWith(light.frame);
            if (wantsFocus) {
              light.focus();
            }
          }
        });
      }
      return upgrading;
    }

    view.frame.addEventListener("mousedown", function (event) {
      if (event.button === 0) {
        clickedAt = offsetAt(event, view.code);
        wantsFocus = true;
      }
    });
    view.frame.addEventListener("focus", function () {
      wantsFocus = true;
      upgrade();
    });
    view.frame.addEventListener("keydown", function (event) {
      // A key pressed before the editor is there would be lost: Ctrl+Enter still runs
      if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
        event.preventDefault();
        options.onRun();
      }
    });

    /** The editor that edits, where there is one. */
    function editing() {
      return rich !== null ? rich : light;
    }

    return {
      get value() {
        return editing() !== null ? editing().value : text;
      },
      set value(next) {
        if (editing() !== null) {
          editing().value = next;
          return;
        }
        text = next;
        view.show(next);
        view.seed = null;
        changed(next);
      },
      mark: function (diagnostics) {
        if (editing() !== null) {
          editing().mark(diagnostics);
        }
      },
      moveTo: function (line, column) {
        if (editing() !== null) {
          editing().moveTo(line, column);
          return;
        }
        const lines = text.split("\n");
        let offset = 0;
        for (let index = 0; index < line - 1 && index < lines.length; index += 1) {
          offset += lines[index].length + 1;
        }
        clickedAt = offset + Math.max(0, column - 1);
        wantsFocus = true;
        upgrade();
      },
      focus: function () {
        if (editing() !== null) {
          editing().focus();
        } else {
          wantsFocus = true;
          upgrade();
        }
      },
      onChange: function (listener) {
        listeners.push(listener);
      },
      /** The server's formatting of the text; false where there is no Monaco. */
      format: function () {
        return rich !== null ? rich.format() : false;
      },
      get hasLanguageServer() {
        return rich !== null;
      },
      upgrade: upgrade,
      target: view.frame,
    };
  }

  function element(name, classes, text) {
    const created = document.createElement(name);
    if (classes) {
      created.className = classes;
    }
    if (text !== undefined) {
      created.textContent = text;
    }
    return created;
  }

  // -------------------------------------------------------------------------------------------------- the output --

  /** What a run wrote, standard error apart from standard output, with each place of a diagnostic a link into the code. */
  function renderOutput(target, output, errors, file, editor) {
    target.textContent = "";
    if (output) {
      target.appendChild(document.createTextNode(output));
    }
    if (errors) {
      const block = element("span", "playground-errors");
      for (const line of errors.split(/(\n)/)) {
        const heading = /^(error|warning|panic)(:.*)$/.exec(line);
        const place = placePattern.exec(line);
        if (heading) {
          block.appendChild(element("span", heading[1] === "warning" ? "output-warning" : "output-error", heading[1]));
          block.appendChild(document.createTextNode(heading[2]));
        } else if (place && isFile(place[2], file)) {
          block.appendChild(document.createTextNode(place[1]));
          const link = element("button", "playground-place", place[2] + ":" + place[3] + ":" + place[4]);
          link.type = "button";
          const lineNumber = Number(place[3]);
          const column = Number(place[4]);
          link.addEventListener("click", function () {
            editor.moveTo(lineNumber, column);
          });
          block.appendChild(link);
        } else {
          block.appendChild(document.createTextNode(line));
        }
      }
      target.appendChild(block);
    }
  }

  // ------------------------------------------------------------------------------------------------ the sharing --

  /** The source as the address's fragment: deflated and base64url, `#code=...` - never sent to a server. */
  async function fragmentOf(source) {
    const bytes = new TextEncoder().encode(source);
    if (typeof CompressionStream === "function") {
      const stream = new Blob([bytes]).stream().pipeThrough(new CompressionStream("deflate-raw"));
      const deflated = new Uint8Array(await new Response(stream).arrayBuffer());
      return "code=" + base64url(deflated);
    }
    return "source=" + base64url(bytes);
  }

  async function sourceOfFragment(fragment) {
    const found = /^#?(code|source)=([A-Za-z0-9_-]*)$/.exec(fragment);
    if (!found) {
      return null;
    }
    try {
      let bytes = fromBase64url(found[2]);
      if (found[1] === "code") {
        const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
        bytes = new Uint8Array(await new Response(stream).arrayBuffer());
      }
      return new TextDecoder().decode(bytes);
    } catch (problem) {
      return null;
    }
  }

  /** The example an address names, `#example=<id>`, or `null`. */
  function exampleOfFragment(fragment) {
    const found = /^#?example=([A-Za-z0-9_-]+)$/.exec(fragment);
    return found ? found[1] : null;
  }

  function base64url(bytes) {
    let text = "";
    for (let index = 0; index < bytes.length; index += 0x8000) {
      text += String.fromCharCode.apply(null, bytes.subarray(index, index + 0x8000));
    }
    return btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }

  function fromBase64url(text) {
    const binary = atob(text.replace(/-/g, "+").replace(/_/g, "/"));
    const bytes = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; index += 1) {
      bytes[index] = binary.charCodeAt(index);
    }
    return bytes;
  }

  // ------------------------------------------------------------------------------------------------ the examples --

  /**
   * The gallery of `/play`: `examples/index.json` beside this script, written by `playground/examples/` - every
   * example with `id`, `file`, `title`, `description`, `category`, `level`, whether it is the `default`, and `de` with
   * the German title and description. The manifest is a list of them, or an object whose `examples` is that list;
   * `categories`, where it has them, orders the categories and names them (`{ id, title, de: { title } }`). A site
   * without the folder has no gallery.
   */
  let loadingExamples = null;

  function examples() {
    if (loadingExamples === null) {
      loadingExamples = fetch(fileAddress("examples/index.json")).then(function (response) {
        if (!response.ok) {
          throw new Error("examples/index.json: " + response.status);
        }
        return response.json();
      }).then(function (manifest) {
        const list = Array.isArray(manifest) ? manifest : (manifest && manifest.examples) || [];
        const categories = manifest && Array.isArray(manifest.categories) ? manifest.categories : [];
        return {
          examples: list.filter(function (entry) {
            return entry && typeof entry.id === "string" && typeof entry.file === "string";
          }),
          categories: categories,
        };
      });
    }
    return loadingExamples;
  }

  /** The source of an example, from the folder of the manifest. */
  function exampleSource(entry) {
    return fetch(fileAddress("examples/" + entry.file)).then(function (response) {
      if (!response.ok) {
        throw new Error("examples/" + entry.file + ": " + response.status);
      }
      return response.text();
    });
  }

  /** The page's language, as the site sets it on `<html lang>`: German takes the manifest's `de`. */
  function isGerman() {
    return /^de\b/i.test(document.documentElement.lang || "");
  }

  function localized(entry, field) {
    if (isGerman() && entry.de && typeof entry.de[field] === "string" && entry.de[field]) {
      return entry.de[field];
    }
    return typeof entry[field] === "string" ? entry[field] : "";
  }

  /** The categories in the manifest's order, or in the order the examples first name them, each with its examples. */
  function groupedExamples(manifest) {
    const groups = [];
    const byName = new Map();
    function groupOf(name) {
      let group = byName.get(name);
      if (group === undefined) {
        const declared = manifest.categories.find(function (category) {
          return category && category.id === name;
        });
        group = { name: name, title: declared ? localized(declared, "title") || name : name, examples: [] };
        byName.set(name, group);
        groups.push(group);
      }
      return group;
    }
    for (const category of manifest.categories) {
      if (category && typeof category.id === "string") {
        groupOf(category.id);
      }
    }
    for (const entry of manifest.examples) {
      groupOf(typeof entry.category === "string" && entry.category ? entry.category : "").examples.push(entry);
    }
    return groups.filter(function (group) {
      return group.examples.length > 0;
    });
  }

  const levelWords = {
    en: { beginner: "beginner", intermediate: "intermediate", advanced: "advanced" },
    de: { beginner: "Einstieg", intermediate: "Fortgeschritten", advanced: "Vertiefung" },
  };

  function levelOf(entry) {
    if (typeof entry.level !== "string" || !entry.level) {
      return "";
    }
    const words = levelWords[isGerman() ? "de" : "en"];
    return words[entry.level] || entry.level;
  }

  /**
   * The picker above the editor of `/play`: a button with the title of the example that is open, and under it the
   * examples by category, each with its title and its line of description. Choosing one replaces the code - after a
   * question where the reader changed what was there - and the address names the example until the code changes.
   */
  function createGallery(bar, editor, labels, state) {
    const host = element("div", "playground-gallery");
    const button = element("button", "button button-secondary playground-gallery-button");
    button.type = "button";
    button.setAttribute("aria-haspopup", "true");
    button.setAttribute("aria-expanded", "false");
    const caption = element("span", "playground-gallery-caption", labels.examples);
    const current = element("span", "playground-gallery-current");
    const chevron = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    chevron.setAttribute("viewBox", "0 0 16 16");
    chevron.setAttribute("aria-hidden", "true");
    chevron.setAttribute("class", "playground-gallery-chevron");
    const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", "M4 6l4 4 4-4");
    chevron.appendChild(path);
    button.appendChild(caption);
    button.appendChild(current);
    button.appendChild(chevron);
    const panel = element("div", "playground-gallery-panel");
    panel.hidden = true;
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-label", labels.examples);
    host.appendChild(button);
    host.appendChild(panel);
    bar.appendChild(host);
    host.hidden = true;

    let manifest = null;
    const items = [];

    function show(entry) {
      current.textContent = entry ? localized(entry, "title") : "";
      current.hidden = !entry;
      for (const item of items) {
        item.button.setAttribute("aria-current", item.entry === entry ? "true" : "false");
      }
    }

    function open() {
      panel.hidden = false;
      button.setAttribute("aria-expanded", "true");
      // One frame hidden-less first, so the panel rises in rather than appearing
      requestAnimationFrame(function () {
        panel.classList.add("playground-gallery-open");
      });
      const chosen = items.find(function (item) {
        return item.entry === state.example;
      }) || items[0];
      if (chosen) {
        chosen.button.focus();
      }
      document.addEventListener("pointerdown", outside, true);
    }

    function close(returnFocus) {
      panel.classList.remove("playground-gallery-open");
      panel.hidden = true;
      button.setAttribute("aria-expanded", "false");
      document.removeEventListener("pointerdown", outside, true);
      if (returnFocus) {
        button.focus();
      }
    }

    function outside(event) {
      if (!host.contains(event.target)) {
        close(false);
      }
    }

    button.addEventListener("click", function () {
      if (panel.hidden) {
        open();
      } else {
        close(true);
      }
    });
    panel.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        event.preventDefault();
        close(true);
        return;
      }
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        const at = items.findIndex(function (item) {
          return item.button === document.activeElement;
        });
        const next = event.key === "ArrowDown" ? Math.min(items.length - 1, at + 1) : Math.max(0, at - 1);
        items[next].button.focus();
      }
    });

    function choose(entry) {
      close(false);
      if (editor.value !== state.pristine && !window.confirm(labels.replace)) {
        return;
      }
      load(entry).then(function () {
        editor.focus();
      });
    }

    function load(entry) {
      return exampleSource(entry).then(function (source) {
        state.example = entry;
        state.pristine = source;
        editor.value = source;
        show(entry);
        history.replaceState(null, "", "#example=" + entry.id);
      }, function () {});
    }

    function build() {
      for (const group of groupedExamples(manifest)) {
        const section = element("section", "playground-gallery-group");
        if (group.title) {
          section.appendChild(element("h3", "playground-gallery-category", group.title));
        }
        const list = element("ul", "playground-gallery-list");
        for (const entry of group.examples) {
          const row = element("li");
          const choice = element("button", "playground-gallery-item");
          choice.type = "button";
          const heading = element("span", "playground-gallery-title", localized(entry, "title") || entry.id);
          choice.appendChild(heading);
          const level = levelOf(entry);
          if (level) {
            choice.appendChild(element("span", "playground-gallery-level", level));
          }
          const description = localized(entry, "description");
          if (description) {
            choice.appendChild(element("span", "playground-gallery-description", description));
          }
          choice.addEventListener("click", function () {
            choose(entry);
          });
          row.appendChild(choice);
          list.appendChild(row);
          items.push({ entry: entry, button: choice });
        }
        section.appendChild(list);
        panel.appendChild(section);
      }
    }

    return {
      /** Reads the manifest and shows the picker; answers the manifest, or `null` for a site without examples. */
      prepare: function () {
        return examples().then(function (found) {
          if (found.examples.length === 0) {
            return null;
          }
          manifest = found;
          build();
          host.hidden = false;
          return found;
        }, function () {
          return null;
        });
      },
      load: load,
      show: show,
      find: function (id) {
        return manifest === null ? null : manifest.examples.find(function (entry) {
          return entry.id === id;
        }) || null;
      },
      defaultExample: function () {
        return manifest === null ? null : manifest.examples.find(function (entry) {
          return entry.default === true;
        }) || manifest.examples[0] || null;
      },
    };
  }

  // --------------------------------------------------------------------------------------------------- the mount --

  /**
   * Takes over `element`: its children become the editor with `source`, a Run button and the output. With
   * `expectedOutput` it is an exercise: Reset, Show solution (where there is a `solution`), and a line that says
   * whether the output is the expected one. A mount inside an element with `data-playground-page` - the page `/play`,
   * `docs/site/play.md` - fills the page, has the gallery and Format, and the address carries its source.
   */
  function mount(target, options) {
    ensureStylesheet();
    const settings = options || {};
    const labels = Object.assign({}, defaultLabels, settings.labels || {});
    const initial = settings.source !== undefined ? settings.source : (target.getAttribute("data-source") || "");
    const file = settings.file || target.getAttribute("data-file") || "main.trb";
    const expected = settings.expectedOutput;
    const isExercise = typeof expected === "string";
    const isPage = settings.page === true || target.closest("[data-playground-page]") !== null;
    // The line that says how to run it locally, which the site writes under an exercise of Start, stays below it
    const local = target.querySelector(".playground-local");
    // The code the site highlighted: its colours and its metrics are the editor's first
    const sitePre = target.querySelector(".code-block pre") || target.querySelector("pre");
    const metrics = metricsOf(sitePre);
    const highlighted = sitePre !== null ? sitePre.querySelector("code") : null;

    target.textContent = "";
    target.classList.add("playground-mounted");
    if (isPage) {
      target.classList.add("playground-page");
    }

    // The gallery's bar stands above the editor of the page
    const top = element("div", "playground-top");
    if (isPage) {
      target.appendChild(top);
    }

    let formatButton = null;
    const editor = createEditor(target, initial, {
      onRun: start,
      page: isPage,
      file: file,
      label: labels.source,
      labels: labels,
      metrics: metrics,
      highlighted: highlighted,
      onUpgrade: function () {
        if (formatButton !== null) {
          formatButton.hidden = false;
        }
      },
    });
    const bar = element("div", "playground-bar");
    const runButton = element("button", "button button-primary playground-run", labels.run);
    runButton.type = "button";
    runButton.title = labels.run + " (Ctrl+Enter)";
    bar.appendChild(runButton);
    if (isPage) {
      formatButton = element("button", "button button-secondary playground-format", labels.format);
      formatButton.type = "button";
      formatButton.title = labels.format + " (Shift+Alt+F)";
      formatButton.hidden = true;
      formatButton.addEventListener("click", function () {
        editor.format();
      });
      bar.appendChild(formatButton);
    }
    if (isExercise) {
      const resetButton = element("button", "button button-secondary", labels.reset);
      resetButton.type = "button";
      resetButton.addEventListener("click", function () {
        editor.value = initial;
        clear();
      });
      bar.appendChild(resetButton);
      if (typeof settings.solution === "string") {
        const solutionButton = element("button", "button button-secondary", labels.solution);
        solutionButton.type = "button";
        solutionButton.addEventListener("click", function () {
          editor.value = settings.solution;
          clear();
        });
        bar.appendChild(solutionButton);
      }
    }
    const status = element("span", "playground-status");
    status.setAttribute("aria-live", "polite");
    bar.appendChild(status);
    target.appendChild(bar);

    // The page keeps its output in view from the start, so that nothing moves when the first run prints
    const panel = element("div", "playground-panel");
    panel.hidden = !isPage;
    const outputLabel = element("div", "playground-label", labels.output);
    const output = element("pre", "playground-output");
    panel.appendChild(outputLabel);
    panel.appendChild(output);
    target.appendChild(panel);

    const verdict = element("div", "playground-verdict");
    verdict.hidden = true;
    target.appendChild(verdict);

    // The toolchain is fetched as soon as a reader shows the intent to run or to write, not on every page with a mount;
    // writing - the focus in the code, not on a button - brings the editor with the language server
    function prepare() {
      compiledToolchain().catch(function () {});
    }
    target.addEventListener("focusin", prepare, { once: true });
    // The editor comes when the pointer does, so the first click already lands in it; the language server with the
    // first focus in the code (editor/editor.mjs)
    target.addEventListener("pointerenter", function () {
      editor.upgrade();
    }, { once: true });
    runButton.addEventListener("pointerenter", prepare, { once: true });
    runButton.addEventListener("click", function () {
      if (active) {
        stop(active);
      } else {
        start();
      }
    });

    /** The run of this mount while there is one: the handle [stop] takes. */
    let active = null;

    function clear() {
      panel.hidden = !isPage;
      output.textContent = "";
      verdict.hidden = true;
      status.textContent = "";
    }

    async function start() {
      if (active) {
        return;
      }
      const job = {};
      active = job;
      runButton.textContent = labels.stop;
      runButton.classList.add("playground-stop");
      status.textContent = compiled ? labels.running : labels.loading;
      output.textContent = "";
      panel.hidden = false;
      verdict.hidden = true;
      if (!editor.hasLanguageServer) {
        editor.mark([]);
      }
      let streamedOutput = "";
      let streamedErrors = "";
      try {
        const result = await run(editor.value, function (more, moreErrors) {
          status.textContent = labels.running;
          streamedOutput += more;
          streamedErrors += moreErrors;
          renderOutput(output, streamedOutput, streamedErrors, file, editor);
        }, file, job);
        renderOutput(output, result.output, result.errors, file, editor);
        if (result.diagnostics.length > 0 || !editor.hasLanguageServer) {
          editor.mark(result.diagnostics);
        }
        status.textContent = statusOf(result, labels);
        let passed = false;
        if (isExercise) {
          passed = result.exitCode === 0 && result.output.replace(/\n$/, "") === expected;
          showVerdict(passed, labels);
        }
        if (typeof settings.onResult === "function") {
          settings.onResult({ output: result.output, exitCode: result.exitCode, passed: passed });
        }
      } catch (problem) {
        status.textContent = problem && problem.message === "unsupported" ? labels.unsupported : String(problem);
      } finally {
        active = null;
        runButton.textContent = labels.run;
        runButton.classList.remove("playground-stop");
      }
    }

    function showVerdict(passed, words) {
      verdict.textContent = "";
      verdict.hidden = false;
      verdict.className = "playground-verdict " + (passed ? "playground-solved" : "playground-unsolved");
      verdict.appendChild(element("div", "playground-verdict-line", passed ? words.solved : words.unsolved));
      if (!passed) {
        verdict.appendChild(element("div", "playground-label", words.expected));
        verdict.appendChild(element("pre", "playground-output", expected));
      }
    }

    if (local !== null) {
      target.appendChild(local);
    }
    if (isPage) {
      page(editor, top, labels, initial);
      // The page is for writing: the editor and the language server come at once
      prepare();
      editor.upgrade();
    }
    return editor;
  }

  /** `playground.css`, beside this script, once per page: the site's own stylesheet knows nothing of the playground. */
  function ensureStylesheet() {
    if (document.querySelector("link[data-torb-playground]")) {
      return;
    }
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = fileAddress("playground.css");
    link.setAttribute("data-torb-playground", "");
    document.head.appendChild(link);
  }

  function statusOf(result, labels) {
    if (result.stopped === "output") {
      return labels.tooMuchOutput;
    }
    if (result.stopped) {
      return labels.stopped;
    }
    if (result.crash) {
      return labels.crashed + ": " + result.crash;
    }
    const seconds = (result.milliseconds / 1000).toFixed(1) + " s";
    if (result.exitCode !== 0) {
      return labels.exitCode + " " + result.exitCode + " · " + seconds;
    }
    return seconds;
  }

  /**
   * The page `/play`: what the address holds - a program behind `#code=`, an example behind `#example=` - or else the
   * default example of the gallery, or else the page's own program; and every change of the code is written back to
   * the address, which is the link to share.
   */
  function page(editor, top, labels, initial) {
    const state = { example: null, pristine: initial };
    const gallery = createGallery(top, editor, labels, state);
    let pending = null;
    let settled = false;
    editor.onChange(function (text) {
      if (!settled) {
        return;
      }
      clearTimeout(pending);
      pending = setTimeout(function () {
        // An example as it was loaded is named by the address; anything else is carried in it
        if (state.example !== null && text === state.pristine) {
          history.replaceState(null, "", "#example=" + state.example.id);
          return;
        }
        fragmentOf(text).then(function (fragment) {
          history.replaceState(null, "", "#" + fragment);
        });
      }, 400);
    });
    Promise.all([sourceOfFragment(location.hash), gallery.prepare()]).then(function (found) {
      const shared = found[0];
      if (shared !== null) {
        state.pristine = shared;
        editor.value = shared;
        return null;
      }
      const named = exampleOfFragment(location.hash);
      const entry = (named !== null ? gallery.find(named) : null) || gallery.defaultExample();
      // Nothing was written yet: the example replaces the page's program without a question
      if (entry !== null && editor.value === initial) {
        return gallery.load(entry);
      }
      return null;
    }).then(function () {
      settled = true;
    });
  }

  window.TorbPlayground = {
    mount: mount,
    run: function (source, file) {
      return run(source, null, file);
    },
    stop: function () {
      stop(null);
    },
    highlight: highlight,
    tokenize: tokenize,
  };
})();
