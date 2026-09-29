// The smoke test of the playground's toolchain: the WebAssembly `torb` of `sh playground/build.sh`, run under node the
// way playground-worker.js runs it in a page - a fresh instance per program, `std/` inside it, `torb run main.trb` in
// `/torb/work` - on hello world and on the things a page relies on: a diagnostic with its place, the refusal of an
// import the browser does not have, the file system in memory, the target the VM compiles for and the streams of std/io.
// Then the language server: playground-worker.js itself in a thread, started as the page's language worker starts it,
// answers initialize, publishes the diagnostics of a document as it is opened and changed, completes at a member access,
// answers hover and semantic tokens, and ends with 0 after `shutdown` and `exit`.
//
//   node playground/smoke-test.mjs [build/playground]
//
// Prints the time to the first output of hello world, cold (the module compiled, instantiated and run) and warm (the
// compiled module instantiated anew and run), and exits with 1 if anything was not as expected.

import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { Worker } from "node:worker_threads";

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

/** A session with the language server of a worker: requests with their answers, and the notifications it sends. */
function languageSession(worker) {
  const listeners = [];
  let nextId = 0;
  worker.on("message", (message) => {
    for (const listener of listeners.slice()) {
      listener(message);
    }
  });
  function waitFor(accept, what, seconds = 60) {
    return new Promise((resolveWait, rejectWait) => {
      const timer = setTimeout(() => {
        listeners.splice(listeners.indexOf(listener), 1);
        rejectWait(new Error(`no ${what} within ${seconds} s`));
      }, seconds * 1000);
      function listener(message) {
        const found = accept(message);
        if (found !== undefined) {
          clearTimeout(timer);
          listeners.splice(listeners.indexOf(listener), 1);
          resolveWait(found);
        }
      }
      listeners.push(listener);
    });
  }
  function parsed(message) {
    return message.kind === "lsp" ? JSON.parse(message.message) : null;
  }
  return {
    notify(method, params) {
      worker.postMessage({ kind: "lsp", message: JSON.stringify({ jsonrpc: "2.0", method, params }) });
    },
    request(method, params) {
      nextId += 1;
      const id = nextId;
      const answer = waitFor((message) => {
        const value = parsed(message);
        return value !== null && value.id === id && !("method" in value) ? value : undefined;
      }, `answer to ${method}`);
      worker.postMessage({ kind: "lsp", message: JSON.stringify({ jsonrpc: "2.0", id, method, params }) });
      return answer;
    },
    notification(method, accept = () => true) {
      return waitFor((message) => {
        const value = parsed(message);
        return value !== null && value.method === method && accept(value.params) ? value.params : undefined;
      }, method);
    },
    exited() {
      return waitFor((message) => (message.kind === "exited" ? message : undefined), "end of the server");
    },
  };
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

const worker = startWorker();
const session = languageSession(worker);
const uri = "file:///torb/work/main.trb";
worker.postMessage({ kind: "language", module });

await step("the language server answers initialize", async () => {
  const answer = await session.request("initialize", {
    processId: null,
    rootUri: "file:///torb/work",
    capabilities: {},
  });
  const capabilities = answer.result && answer.result.capabilities;
  if (!capabilities || !capabilities.completionProvider || !capabilities.semanticTokensProvider) {
    throw new Error(`capabilities: ${JSON.stringify(answer)}`);
  }
  session.notify("initialized", {});
});
await step("a document with an error is published with its diagnostic", async () => {
  const published = session.notification("textDocument/publishDiagnostics", (params) => params.uri === uri);
  session.notify("textDocument/didOpen", {
    textDocument: { uri, languageId: "torbscript", version: 1, text: 'prnt "no"\n' },
  });
  const params = await published;
  const found = params.diagnostics.find((diagnostic) => diagnostic.message.includes("Cannot find `prnt`"));
  if (!found || found.range.start.line !== 0) {
    throw new Error(JSON.stringify(params.diagnostics));
  }
  return found.message;
});
await step("a change while typing is checked again", async () => {
  const published = session.notification("textDocument/publishDiagnostics", (params) => params.uri === uri);
  session.notify("textDocument/didChange", {
    textDocument: { uri, version: 2 },
    contentChanges: [{ range: { start: { line: 0, character: 0 }, end: { line: 0, character: 4 } }, text: "print" }],
  });
  const params = await published;
  if (params.diagnostics.length !== 0) {
    throw new Error(JSON.stringify(params.diagnostics));
  }
});
await step("completion at a member access answers its members", async () => {
  session.notify("textDocument/didChange", {
    textDocument: { uri, version: 3 },
    contentChanges: [{ text: 'const names = ["Ada", "Alan"]\nprint names.\n' }],
  });
  const answer = await session.request("textDocument/completion", {
    textDocument: { uri },
    position: { line: 1, character: 12 },
  });
  const items = answer.result && (Array.isArray(answer.result) ? answer.result : answer.result.items);
  if (!items || !items.some((item) => item.label === "length")) {
    throw new Error(JSON.stringify(answer).slice(0, 400));
  }
  return `${items.length} items`;
});
await step("hover and semantic tokens answer", async () => {
  const hover = await session.request("textDocument/hover", { textDocument: { uri }, position: { line: 1, character: 7 } });
  if (!hover.result || !JSON.stringify(hover.result.contents).includes("List<String>")) {
    throw new Error(JSON.stringify(hover));
  }
  const tokens = await session.request("textDocument/semanticTokens/full", { textDocument: { uri } });
  if (!tokens.result || tokens.result.data.length === 0 || tokens.result.data.length % 5 !== 0) {
    throw new Error(JSON.stringify(tokens));
  }
});
await step("shutdown and exit end the server with 0", async () => {
  await session.request("shutdown", null);
  const exited = session.exited();
  session.notify("exit", null);
  const message = await exited;
  if (message.code !== 0) {
    throw new Error(`exit code ${message.code}`);
  }
});
await worker.terminate();

process.exit(failures === 0 ? 0 : 1);
