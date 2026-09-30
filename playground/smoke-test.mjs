// The smoke test of the playground's toolchain: the WebAssembly `torb` of `sh playground/build.sh`, run under node the
// way playground-worker.js runs it in a page - a fresh instance per program, `std/` inside it, `torb run main.trb` in
// `/torb/work` - on hello world and on the things a page relies on: a diagnostic with its place, the refusal of an
// import the browser does not have, the file system in memory, the target the VM compiles for and the streams of std/io.
// Then the language server: playground-worker.js itself in a thread, started as the page's language worker starts it,
// and spoken to by the editor's own client and translations (editor/protocol.mjs), as the page speaks to it: it answers
// initialize, publishes the diagnostics of a document as it is opened and changed (as Monaco's markers), completes at a
// member access and resolves an item's documentation, answers hover, semantic tokens (re-encoded in the editor's
// legend), signature help, definition, formatting and a rename, drops a cancelled request, and ends with 0 after
// `shutdown` and `exit`. Last, the site's highlighting of a block as the semantic tokens the editor starts with.
//
//   node playground/smoke-test.mjs [build/playground]
//
// Prints the time to the first output of hello world, cold (the module compiled, instantiated and run) and warm (the
// compiled module instantiated anew and run), and exits with 1 if anything was not as expected.

import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Worker } from "node:worker_threads";
import {
  LanguageClient,
  completionsOf,
  locationsOf,
  markdownOf,
  markersOf,
  resolvedSuggestion,
  seedTokens,
  semanticTokensOf,
  signatureHelpOf,
  textEditsOf,
  toPosition,
  tokenLegend,
  workspaceEditOf,
} from "./editor/protocol.mjs";

const directory = resolve(process.argv[2] || "build/playground");
const require = createRequire(import.meta.url);
const createTorb = require(join(directory, "torb.js"));

/** Runs `source` as `/torb/work/main.trb`, and answers what it wrote, its exit code and when it wrote first. */
async function run(module, source) {
  const started = performance.now();
  let firstOutput = null;
  const streams = [[], []];
  const collect = (stream) => (byte) => {
    if (byte === null) {
      return;
    }
    if (firstOutput === null) {
      firstOutput = performance.now() - started;
    }
    streams[stream].push(byte);
  };
  const torb = await createTorb({
    instantiateWasm(imports, receive) {
      WebAssembly.instantiate(module, imports).then((instance) => receive(instance, module));
      return {};
    },
    noInitialRun: true,
    stdin: () => null,
    stdout: collect(0),
    stderr: collect(1),
    preRun: [
      (instance) => {
        for (const name of ["USER", "LOGNAME", "PATH", "PWD", "HOME", "LANG", "_"]) {
          instance.ENV[name] = undefined;
        }
      },
    ],
  });
  torb.FS.mkdirTree("/torb/work");
  torb.FS.writeFile("/torb/work/main.trb", source);
  torb.FS.chdir("/torb/work");
  const exitCode = torb.callMain(["run", "main.trb"]);
  const decode = (bytes) => new TextDecoder().decode(new Uint8Array(bytes));
  return {
    output: decode(streams[0]),
    errors: decode(streams[1]),
    exitCode,
    firstOutput,
    milliseconds: performance.now() - started,
  };
}

let failures = 0;

function expect(name, result, check) {
  if (check(result)) {
    console.log(`ok    ${name} (${result.milliseconds.toFixed(0)} ms)`);
    return;
  }
  failures += 1;
  console.log(`FAIL  ${name}: exit code ${result.exitCode}`);
  console.log(`      output: ${JSON.stringify(result.output)}`);
  console.log(`      errors: ${JSON.stringify(result.errors)}`);
}

const hello = 'print "Hello, world!"\n';

const coldStart = performance.now();
const module = await WebAssembly.compile(readFileSync(join(directory, "torb.wasm")));
const compiled = performance.now() - coldStart;
const cold = await run(module, hello);
expect("hello world", cold, (result) => result.exitCode === 0 && result.output === "Hello, world!\n");
const warm = await run(module, hello);
expect("hello world again, in a new instance", warm, (result) => result.output === "Hello, world!\n");

expect(
  "a diagnostic names its place",
  await run(module, 'prnt "no"\n'),
  (result) => result.exitCode === 1 && result.errors.includes("--> main.trb:1:1"),
);
expect(
  "an import of std/process is refused at the import",
  await run(module, 'use Process from "std/process"\n\nprint Process.arguments()\n'),
  (result) => result.exitCode === 1 && result.errors.includes("`std/process` does not exist in the browser"),
);
expect(
  "std/fs writes and reads in memory",
  await run(
    module,
    [
      'use File from "std/fs"',
      "",
      'File.writeText("notes.txt", "kept in memory").expect("written")',
      'print File.readText("notes.txt").expect("read")',
      "",
    ].join("\n"),
  ),
  (result) => result.exitCode === 0 && result.output === "kept in memory\n",
);
expect(
  "the VM compiles for the browser",
  await run(
    module,
    ['use OperatingSystem, Architecture from "std/core"', "", 'print "{OperatingSystem.current} {Architecture.current}"', ""].join("\n"),
  ),
  (result) => result.exitCode === 0 && result.output === "Browser wasm64\n",
);
expect(
  "a panic ends with 101",
  await run(module, 'panic "on purpose"\n'),
  (result) => result.exitCode === 101 && result.errors.includes("on purpose"),
);
expect(
  "the streams of std/io write, and standard input is at its end",
  await run(
    module,
    [
      'use standardInput, standardOutput from "std/io"',
      "",
      'const written = standardOutput().add("streamed\\n".bytes()).await()',
      "print written.isOk()",
      "var input = standardInput()",
      "print input.next().await().isOk()",
      "",
    ].join("\n"),
  ),
  (result) => result.exitCode === 0 && result.output === "streamed\ntrue\ntrue\n",
);

const first = (result) => (result.firstOutput === null ? "no output" : `${result.firstOutput.toFixed(0)} ms`);
console.log(
  `hello world, first output: cold ${(compiled + cold.firstOutput).toFixed(0)} ms ` +
    `(compile ${compiled.toFixed(0)} ms, then ${first(cold)}), warm ${first(warm)}`,
);

// ---------------------------------------------------------------------------- the language server, through the worker --

/**
 * playground-worker.js itself, in a thread of node's, with the little of a worker's global scope it uses: `self` and
 * its `onmessage`, `postMessage`, `importScripts` and `location`. What the server logs at the debug level is dropped.
 */
function startWorker() {
  const shim = `
    const { parentPort, workerData } = require("node:worker_threads");
    const { readFileSync } = require("node:fs");
    const { pathToFileURL, fileURLToPath } = require("node:url");
    const vm = require("node:vm");
    globalThis.self = globalThis;
    globalThis.location = { search: "", href: pathToFileURL(workerData.worker).href };
    globalThis.importScripts = (address) => {
      globalThis.createTorb = require(fileURLToPath(address));
    };
    globalThis.postMessage = (message) => parentPort.postMessage(message);
    console.debug = () => {};
    parentPort.on("message", (data) => self.onmessage({ data }));
    vm.runInThisContext(readFileSync(workerData.worker, "utf8"), { filename: workerData.worker });
  `;
  return new Worker(shim, { eval: true, workerData: { worker: join(directory, "playground-worker.js") } });
}

/**
 * The transport the page gives the editor's client (playground.js, `languageServer`): one JSON-RPC message per call,
 * over the worker's `{ kind: "lsp" }` messages.
 */
function workerTransport(worker) {
  return {
    send(message) {
      worker.postMessage({ kind: "lsp", message });
    },
    subscribe(handler) {
      worker.on("message", (message) => {
        if (message.kind === "lsp") {
          handler(message.message);
        }
      });
    },
  };
}

/** The next notification of `method` whose parameters `accept` takes, or a failure after `seconds`. */
function notification(client, method, accept = () => true, seconds = 60) {
  return new Promise((resolveWait, rejectWait) => {
    const timer = setTimeout(() => {
      stop();
      rejectWait(new Error(`no ${method} within ${seconds} s`));
    }, seconds * 1000);
    const stop = client.onNotification(method, (params) => {
      if (accept(params)) {
        clearTimeout(timer);
        stop();
        resolveWait(params);
      }
    });
  });
}

async function step(name, body) {
  const started = performance.now();
  try {
    const detail = await body();
    console.log(`ok    ${name} (${(performance.now() - started).toFixed(0)} ms)${detail ? `: ${detail}` : ""}`);
  } catch (problem) {
    failures += 1;
    console.log(`FAIL  ${name}: ${problem && problem.message ? problem.message : problem}`);
  }
}

// What the page's editor does, in the page's code: the client and the translations of editor/protocol.mjs
const worker = startWorker();
const client = new LanguageClient(workerTransport(worker), { timeout: 60000 });
const uri = "file:///torb/work/main.trb";
worker.postMessage({ kind: "language", module });

function diagnosticsOf() {
  return notification(client, "textDocument/publishDiagnostics", (params) => params.uri === uri);
}

await step("the language server answers initialize", async () => {
  const capabilities = await client.start();
  if (!capabilities.completionProvider || !capabilities.semanticTokensProvider || !capabilities.renameProvider) {
    throw new Error(`capabilities: ${JSON.stringify(capabilities)}`);
  }
});
await step("a document with an error is published with its diagnostic, as a marker", async () => {
  const published = diagnosticsOf();
  client.open(uri, 'prnt "no"\n');
  const params = await published;
  const marker = markersOf(params.diagnostics).find((found) => found.message.includes("Cannot find `prnt`"));
  // An error is Monaco's MarkerSeverity.Error, 8, and its place counts from 1
  if (!marker || marker.severity !== 8 || marker.startLineNumber !== 1 || marker.startColumn !== 1) {
    throw new Error(JSON.stringify(params.diagnostics));
  }
  return marker.message;
});
await step("a change while typing is checked again", async () => {
  const published = diagnosticsOf();
  client.change(uri, 'print "no"\n');
  const params = await published;
  if (params.diagnostics.length !== 0) {
    throw new Error(JSON.stringify(params.diagnostics));
  }
});
let firstMethod = null;
await step("completion at a member access answers its members as suggestions", async () => {
  client.change(uri, 'const names = ["Ada", "Alan"]\nprint names.\n');
  const range = { startLineNumber: 2, startColumn: 13, endLineNumber: 2, endColumn: 13 };
  const result = await client.request("textDocument/completion", {
    textDocument: { uri },
    position: toPosition({ lineNumber: 2, column: 13 }),
    context: { triggerKind: 2, triggerCharacter: "." },
  });
  const { suggestions } = completionsOf(result, { insert: range, replace: range });
  const length = suggestions.find((suggestion) => suggestion.label === "length");
  // A method is Monaco's CompletionItemKind.Method, 0
  if (!length || length.kind !== 0 || length.insertText !== "length" || length.range.insert !== range) {
    throw new Error(JSON.stringify(suggestions.slice(0, 3)));
  }
  firstMethod = suggestions.find((suggestion) => suggestion.label === "map") || length;
  return `${suggestions.length} suggestions`;
});
await step("a suggestion is resolved with its signature and its documentation as Markdown", async () => {
  const item = await client.request("completionItem/resolve", firstMethod.item);
  const resolved = resolvedSuggestion(firstMethod, item);
  if (!resolved.detail || !resolved.detail.includes("fn ") || !resolved.documentation || resolved.documentation.isTrusted) {
    throw new Error(JSON.stringify(item));
  }
  return resolved.detail;
});
await step("hover answers Markdown with a trb block, and semantic tokens re-encode in the editor's legend", async () => {
  const hover = await client.request("textDocument/hover", { textDocument: { uri }, position: { line: 1, character: 7 } });
  const contents = markdownOf(hover && hover.contents);
  if (contents.length === 0 || !contents[0].value.includes("```trb") || !contents[0].value.includes("List<String>")) {
    throw new Error(JSON.stringify(hover));
  }
  client.change(uri, "var count = 1\ncount = count + 1\nprint count\n");
  const tokens = await client.request("textDocument/semanticTokens/full", { textDocument: { uri } });
  const data = semanticTokensOf(tokens.data, client.capabilities.semanticTokensProvider.legend);
  if (data.length === 0 || data.length % 5 !== 0) {
    throw new Error(JSON.stringify(tokens));
  }
  // `count` is a mutable variable: the editor's legend has `variable` and puts `mutable` first
  const variable = tokenLegend.tokenTypes.indexOf("variable");
  let mutable = 0;
  for (let index = 0; index < data.length; index += 5) {
    if (data[index + 3] === variable && (data[index + 4] & 1) !== 0) {
      mutable += 1;
    }
  }
  if (mutable < 3) {
    throw new Error(`${mutable} mutable variables in ${Array.from(data).join(",")}`);
  }
  return `${data.length / 5} tokens, ${mutable} of them a mutable variable`;
});
await step("signature help, definition and formatting answer in the editor's shapes", async () => {
  client.change(uri, "fn area(width: Int, height: Int): Int {\n  width * height\n}\n\nprint area(3,   4)\n");
  const help = signatureHelpOf(
    await client.request("textDocument/signatureHelp", { textDocument: { uri }, position: { line: 4, character: 13 } }),
  );
  if (!help || !help.signatures[0].label.includes("width: Int") || help.activeParameter !== 1) {
    throw new Error(JSON.stringify(help));
  }
  const definition = locationsOf(
    await client.request("textDocument/definition", { textDocument: { uri }, position: { line: 4, character: 7 } }),
    uri,
  );
  if (definition.length !== 1 || definition[0].range.startLineNumber !== 1) {
    throw new Error(JSON.stringify(definition));
  }
  const edits = textEditsOf(
    await client.request("textDocument/formatting", { textDocument: { uri }, options: { tabSize: 2, insertSpaces: true } }),
  );
  if (edits.length === 0 || !edits.some((edit) => edit.text.includes("area(3, 4)"))) {
    throw new Error(JSON.stringify(edits));
  }
  return `${help.signatures[0].label}, ${edits.length} edit(s)`;
});
await step("a rename, a request that takes steps, is a workspace edit of the document", async () => {
  client.change(uri, 'const names = ["Ada", "Alan"]\nprint names.length()\n');
  const edit = await client.request("textDocument/rename", {
    textDocument: { uri },
    position: { line: 0, character: 7 },
    newName: "people",
  });
  const documents = workspaceEditOf(edit, (target) => target === uri);
  if (documents.length !== 1 || documents[0].edits.length !== 2 || !documents[0].edits.every((one) => one.text === "people")) {
    throw new Error(JSON.stringify(edit).slice(0, 400));
  }
});
await step("a request Monaco cancels answers null", async () => {
  let cancel = null;
  const token = {
    isCancellationRequested: false,
    onCancellationRequested(listener) {
      cancel = listener;
    },
  };
  const answer = client.request("textDocument/hover", { textDocument: { uri }, position: { line: 1, character: 7 } }, token);
  // The request is sent once the client has flushed the text: cancel it right behind that
  await new Promise((resolveSoon) => setTimeout(resolveSoon, 0));
  token.isCancellationRequested = true;
  if (cancel !== null) {
    cancel();
  }
  const result = await answer;
  if (result !== null) {
    throw new Error(JSON.stringify(result));
  }
});
await step("shutdown and exit end the server with 0", async () => {
  await client.request("shutdown", null);
  const exited = new Promise((resolveExit) => {
    worker.on("message", (message) => {
      if (message.kind === "exited") {
        resolveExit(message);
      }
    });
  });
  client.notify("exit", null);
  const message = await exited;
  if (message.code !== 0) {
    throw new Error(`exit code ${message.code}`);
  }
});
await worker.terminate();

// --------------------------------------------------------------------------------- the site's colours as seed tokens --

await step("the site's highlighting becomes semantic tokens, one per line of a token", async () => {
  const text = 'var x = "a"\n/* two\nlines */ x';
  const spans = [
    { from: 0, to: 3, classes: "t-keyword" },
    { from: 4, to: 5, classes: "t-variable t-mutable" },
    { from: 8, to: 11, classes: "t-string" },
    { from: 12, to: 27, classes: "t-comment" },
    { from: 28, to: 29, classes: "t-variable t-mutable" },
  ];
  const data = Array.from(seedTokens(text, spans));
  const type = (name) => tokenLegend.tokenTypes.indexOf(name);
  const expected = [
    [0, 0, 3, type("keyword"), 0],
    [0, 4, 1, type("variable"), 1],
    [0, 4, 3, type("string"), 0],
    [1, 0, 6, type("comment"), 0],
    [1, 0, 8, type("comment"), 0],
    [0, 9, 1, type("variable"), 1],
  ].flat();
  if (JSON.stringify(data) !== JSON.stringify(expected)) {
    throw new Error(`${JSON.stringify(data)}, expected ${JSON.stringify(expected)}`);
  }
});

process.exit(failures === 0 ? 0 : 1);
