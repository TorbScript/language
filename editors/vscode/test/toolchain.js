// Where `toolchain.js` finds `torb`, without VS Code: `locate` is given a fake `vscode` that holds the setting and a
// fake file system that holds the files, and the platform is set to the one each case is about. Plain Node.js, no
// dependency, and nothing to install:
//
//   node editors/vscode/test/toolchain.js

'use strict';

const assert = require('assert');
const os = require('os');
const path = require('path');
const { locate } = require('../toolchain');

/** A file system of exactly these files, for `fs.statSync`. */
function filesystem(files) {
  return {
    statSync(candidate) {
      if (!files.includes(candidate)) {
        const error = new Error(`ENOENT: ${candidate}`);
        error.code = 'ENOENT';
        throw error;
      }
      return { isFile: () => true };
    },
  };
}

/** A `vscode` whose `torbscript.executablePath` is `setting`, with no workspace folder. */
function vscodeWith(setting) {
  return {
    workspace: {
      getConfiguration: () => ({ get: (name) => (name === 'executablePath' ? setting : undefined) }),
      workspaceFolders: [],
    },
  };
}

/** What `locate` finds for the setting, on the platform, among the files. */
function located(platform, setting, files) {
  const real = Object.getOwnPropertyDescriptor(process, 'platform');
  Object.defineProperty(process, 'platform', { value: platform });
  try {
    return locate(vscodeWith(setting), filesystem(files), path, os);
  } finally {
    Object.defineProperty(process, 'platform', real);
  }
}

const cases = [
  {
    name: 'on Windows a configured path without .exe finds the .exe beside it',
    run() {
      const found = located('win32', 'C:/x/build/release/torb', ['C:/x/build/release/torb.exe']);
      assert.strictEqual(found.executable, 'C:/x/build/release/torb.exe');
      assert.strictEqual(found.source, 'setting');
    },
  },
  {
    name: 'on Windows the path as given comes first',
    run() {
      const files = ['C:\\x\\torb', 'C:\\x\\torb.exe'];
      assert.strictEqual(located('win32', 'C:\\x\\torb', files).executable, 'C:\\x\\torb');
    },
  },
  {
    name: 'on Windows a path that ends in .exe gets no second one, whatever its case',
    run() {
      assert.strictEqual(located('win32', 'C:/x/TORB.EXE', ['C:/x/TORB.EXE']).executable, 'C:/x/TORB.EXE');
      assert.strictEqual(located('win32', 'C:/x/torb.exe', ['C:/x/torb.exe.exe']).executable, null);
    },
  },
  {
    name: 'a configured path that is not there is not found, and says it was the setting',
    run() {
      const found = located('win32', 'C:/x/missing', ['C:/x/torb.exe']);
      assert.strictEqual(found.executable, null);
      assert.strictEqual(found.source, 'setting');
      assert.strictEqual(found.configured, 'C:/x/missing');
    },
  },
  {
    name: 'elsewhere the path as given is the only candidate',
    run() {
      assert.strictEqual(located('linux', '/x/torb', ['/x/torb.exe']).executable, null);
      assert.strictEqual(located('linux', '/x/torb', ['/x/torb']).executable, '/x/torb');
    },
  },
];

let failed = 0;
for (const testCase of cases) {
  try {
    testCase.run();
    console.log(`ok      ${testCase.name}`);
  } catch (error) {
    failed += 1;
    console.log(`FAILED  ${testCase.name}\n        ${error.message}`);
  }
}
console.log(`\n${cases.length - failed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
