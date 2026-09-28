// The smoke test of the playground's toolchain: the WebAssembly `torb` of `sh playground/build.sh`, run under node the
// way playground-worker.js runs it in a page - a fresh instance per program, `std/` inside it, `torb run main.trb` in
// `/torb/work` - on hello world and on the four things a page relies on: a diagnostic with its place, the refusal of an
// import the browser does not have, the file system in memory, and the target the VM compiles for.
//
//   node playground/smoke-test.mjs [build/playground]
//
// Prints the time to the first output of hello world, cold (the module compiled, instantiated and run) and warm (the
// compiled module instantiated anew and run), and exits with 1 on the first program whose output is not the expected.

import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";

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

const first = (result) => (result.firstOutput === null ? "no output" : `${result.firstOutput.toFixed(0)} ms`);
console.log(
  `hello world, first output: cold ${(compiled + cold.firstOutput).toFixed(0)} ms ` +
    `(compile ${compiled.toFixed(0)} ms, then ${first(cold)}), warm ${first(warm)}`,
);
process.exit(failures === 0 ? 0 : 1);
