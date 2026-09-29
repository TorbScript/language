/*
 * playground-worker.js - runs the WebAssembly `torb` off the page's thread, for playground.js. A worker has one of two
 * roles, decided by the first message it gets.
 *
 * **A run.** `{ kind: "run", module, source, file }` instantiates the compiled toolchain anew - an empty file system
 * with `std/` in it, a fresh memory - writes the source as `/torb/work/<file>` and runs `torb run <file>` there, which
 * checks, lowers and runs the program in the VM. The answer is `{ kind: "output", output, errors }` as the program
 * writes, at most every 50 ms, and `{ kind: "done", exitCode, timings }` at the end. The program's environment is empty
 * and its standard input is at its end, as the rule of the browser in docs/design/JAVASCRIPT-AND-PHP.md says: printing
 * and the clock are real, `std/fs` is emscripten's file system in memory, and an import of `std/process` or of the
 * network is refused by `torb` at the import.
 *
 * **The language server.** `{ kind: "language", module }` starts `torb lsp` in one instance that lives as long as the
 * worker, in the working directory `/torb/work`, and every `{ kind: "lsp", message }` after it is one JSON-RPC message
 * for it. The messages travel over the server's standard input and output, as they do for every editor, framed by the
 * base protocol's `Content-Length` header: the page's are framed into standard input, and what the server writes is cut
 * into messages again and posted as `{ kind: "lsp", message }`. Standard input is fed the way runtime/os/browser.c
 * describes: the `stdin` callback answers `undefined` where it has nothing yet, the read waits for the page, the
 * server's scheduler returns here, and `torb_browser_resume` lets it go on once there is more - or once the timer it
 * asked for is due. What the server logs goes to the console at the debug level; if it ends, `{ kind: "exited", code }`
 * says so.
 */
"use strict";

importScripts(new URL("torb.js" + location.search, location.href).href);

/** The most a run may print before it is stopped, standard output and standard error together. */
const outputLimit = 1024 * 1024;

/** The language server of this worker, once `{ kind: "language" }` started it. */
let language = null;

self.onmessage = function (event) {
  const message = event.data;
  if (message.kind === "run") {
    // A name of one file in the working directory, whatever the page asked for
    const file = /^[A-Za-z0-9_.-]+\.trb$/.test(message.file || "") ? message.file : "main.trb";
    run(message.module, message.source, file).catch(function (problem) {
      self.postMessage({ kind: "done", exitCode: null, crash: String(problem && problem.message || problem) });
    });
    return;
  }
  if (message.kind === "language" && language === null) {
    language = startLanguageServer(message.module);
    return;
  }
  if (message.kind === "lsp" && language !== null) {
    language.send(message.message);
  }
};

function instantiator(module) {
  return function (imports, receive) {
    WebAssembly.instantiate(module, imports).then(function (instance) {
      receive(instance, module);
    });
    return {};
  };
}

/** An empty environment: emscripten's defaults (`USER`, `HOME`, `PATH`, ...) are no machine's. */
function emptyEnvironment(instance) {
  for (const name of ["USER", "LOGNAME", "PATH", "PWD", "HOME", "LANG", "_"]) {
    instance.ENV[name] = undefined;
  }
}

// ----------------------------------------------------------------------------------------------------------- a run --

async function run(module, source, file) {
  const started = performance.now();
  const decoders = [new TextDecoder(), new TextDecoder()];
  const pending = ["", ""];
  let written = 0;
  let flushed = started;
  let stopped = false;

  function flush() {
    if (pending[0] || pending[1]) {
      self.postMessage({ kind: "output", output: pending[0], errors: pending[1] });
      pending[0] = "";
      pending[1] = "";
    }
    flushed = performance.now();
  }

  /** Emscripten hands over a stream byte by byte: they are decoded a line, or 256 bytes, at a time. */
  function writer(stream) {
    const bytes = new Uint8Array(256);
    let count = 0;
    function drain(last) {
      pending[stream] += decoders[stream].decode(bytes.subarray(0, count), { stream: !last });
      count = 0;
    }
    function write(byte) {
      if (stopped || byte === null) {
        return;
      }
      bytes[count] = byte;
      count += 1;
      written += 1;
      if (count === bytes.length || byte === 10) {
        drain(false);
        if (performance.now() - flushed > 50) {
          flush();
        }
      }
      if (written > outputLimit) {
        stopped = true;
        drain(false);
        flush();
        self.postMessage({ kind: "done", exitCode: null, stopped: "output" });
      }
    }
    return { write: write, drain: drain };
  }
  const output = writer(0);
  const errors = writer(1);

  const torb = await createTorb({
    instantiateWasm: instantiator(module),
    noInitialRun: true,
    stdin: function () {
      return null;
    },
    stdout: output.write,
    stderr: errors.write,
    preRun: [emptyEnvironment],
  });
  const instantiated = performance.now();
  torb.FS.mkdirTree("/torb/work");
  torb.FS.writeFile("/torb/work/" + file, source);
  torb.FS.chdir("/torb/work");
  let exitCode;
  let crash = null;
  try {
    exitCode = torb.callMain(["run", file]);
  } catch (problem) {
    crash = String(problem && problem.message || problem);
  }
  if (stopped) {
    return;
  }
  // What is left is a last line without its line break
  output.drain(true);
  errors.drain(true);
  flush();
  if (crash !== null) {
    self.postMessage({ kind: "done", exitCode: null, crash: crash });
    return;
  }
  const finished = performance.now();
  self.postMessage({
    kind: "done",
    exitCode: exitCode,
    timings: {
      instantiate: Math.round(instantiated - started),
      run: Math.round(finished - instantiated),
    },
  });
}

// ---------------------------------------------------------------------------------------------- the language server --

/**
 * `torb lsp` in an instance of its own, and the two ends of its standard streams. `send` takes one JSON-RPC message as
 * text, and may be called before the instance is there; what the server answers is posted to the page as it is written.
 */
function startLanguageServer(module) {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  // Standard input: the bytes the page handed in, and how many of them the server has read
  let input = new Uint8Array(0);
  let inputAt = 0;
  // Standard output: the header or the body being written, and the length of the body once its header is read
  let output = new Uint8Array(4096);
  let outputLength = 0;
  let bodyLength = -1;
  let logLine = [];
  let torb = null;
  let timer = null;
  let ended = false;

  function readByte() {
    if (inputAt < input.length) {
      const byte = input[inputAt];
      inputAt += 1;
      return byte;
    }
    // Nothing yet: the read answers EAGAIN and the server waits for the page (runtime/os/browser.c)
    return undefined;
  }

  /** A byte of standard output: a header ends with an empty line and names the length of the body behind it. */
  function writeByte(byte) {
    if (byte === null) {
      return;
    }
    if (outputLength === output.length) {
      const grown = new Uint8Array(output.length * 2);
      grown.set(output);
      output = grown;
    }
    output[outputLength] = byte;
    outputLength += 1;
    if (bodyLength < 0) {
      if (byte === 10 && outputLength >= 4 && output[outputLength - 2] === 13 && output[outputLength - 3] === 10) {
        const found = /Content-Length: *(\d+)/i.exec(decoder.decode(output.subarray(0, outputLength)));
        bodyLength = found ? Number(found[1]) : 0;
        outputLength = 0;
      }
    }
    if (bodyLength >= 0 && outputLength === bodyLength) {
      self.postMessage({ kind: "lsp", message: decoder.decode(output.subarray(0, outputLength)) });
      outputLength = 0;
      bodyLength = -1;
    }
  }

  function writeLog(byte) {
    if (byte === null) {
      return;
    }
    if (byte === 10) {
      console.debug(decoder.decode(new Uint8Array(logLine)));
      logLine = [];
      return;
    }
    logLine.push(byte);
  }

  function append(bytes) {
    const rest = input.subarray(inputAt);
    const next = new Uint8Array(rest.length + bytes.length);
    next.set(rest, 0);
    next.set(bytes, rest.length);
    input = next;
    inputAt = 0;
  }

  /** Lets the server go on - with more input, or because its timer is due - until it waits for the page again. */
  function resume(withInput) {
    if (ended || torb === null) {
      return;
    }
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
    let due;
    try {
      due = torb._torb_browser_resume(withInput ? 1 : 0);
    } catch (problem) {
      end(problem);
      return;
    }
    if (due >= 0) {
      timer = setTimeout(function () {
        timer = null;
        resume(false);
      }, due);
    }
  }

  function end(problem) {
    ended = true;
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
    const code = problem && problem.name === "ExitStatus" ? problem.status : null;
    if (code === null) {
      console.error("torb lsp:", problem);
    }
    self.postMessage({ kind: "exited", code: code });
  }

  function send(message) {
    if (ended) {
      return;
    }
    const body = encoder.encode(message);
    append(encoder.encode("Content-Length: " + body.length + "\r\n\r\n"));
    append(body);
    resume(true);
  }

  let exitCode = null;
  createTorb({
    instantiateWasm: instantiator(module),
    noInitialRun: true,
    stdin: readByte,
    stdout: writeByte,
    stderr: writeLog,
    preRun: [emptyEnvironment],
    onExit: function (code) {
      exitCode = code;
    },
  }).then(function (instance) {
    instance.FS.mkdirTree("/torb/work");
    instance.FS.chdir("/torb/work");
    try {
      // Returns once the server waits for its first message: `main` gave up its stack, the server lives on
      instance.callMain(["lsp"]);
    } catch (problem) {
      end(problem);
      return;
    }
    if (exitCode !== null) {
      end({ name: "ExitStatus", status: exitCode });
      return;
    }
    torb = instance;
    // What the page sent while the toolchain was instantiated
    if (inputAt < input.length) {
      resume(true);
    }
  }, end);

  return { send: send };
}
