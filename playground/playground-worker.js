/*
 * playground-worker.js - runs the WebAssembly `torb` off the page's thread, for playground.js.
 *
 * A message `{ kind: "run", module, source, file }` instantiates the compiled toolchain anew - an empty file system
 * with `std/` in it, a fresh memory - writes the source as `/torb/work/<file>` and runs `torb run <file>` there, which
 * checks, lowers and runs the program in the VM. The answer is `{ kind: "output", output, errors }` as the program
 * writes, at most every 50 ms, and `{ kind: "done", exitCode, timings }` at the end.
 *
 * The program's environment is empty and its standard input is at its end, as the rule of the browser in
 * docs/design/JAVASCRIPT-AND-PHP.md says: printing and the clock are real, `std/fs` is emscripten's file system in
 * memory, and an import of `std/process` or of the network is refused by `torb` at the import.
 */
"use strict";

importScripts(new URL("torb.js" + location.search, location.href).href);

/** The most a run may print before it is stopped, standard output and standard error together. */
const outputLimit = 1024 * 1024;

self.onmessage = function (event) {
  const message = event.data;
  if (message.kind === "run") {
    // A name of one file in the working directory, whatever the page asked for
    const file = /^[A-Za-z0-9_.-]+\.trb$/.test(message.file || "") ? message.file : "main.trb";
    run(message.module, message.source, file).catch(function (problem) {
      self.postMessage({ kind: "done", exitCode: null, crash: String(problem && problem.message || problem) });
    });
  }
};

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
    instantiateWasm: function (imports, receive) {
      WebAssembly.instantiate(module, imports).then(function (instance) {
        receive(instance, module);
      });
      return {};
    },
    noInitialRun: true,
    stdin: function () {
      return null;
    },
    stdout: output.write,
    stderr: errors.write,
    preRun: [
      function (instance) {
        // An empty environment: emscripten's defaults (`USER`, `HOME`, `PATH`, ...) are no machine's
        for (const name of ["USER", "LOGNAME", "PATH", "PWD", "HOME", "LANG", "_"]) {
          instance.ENV[name] = undefined;
        }
      },
    ],
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
