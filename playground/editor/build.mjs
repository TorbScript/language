// Bundles editor.mjs with the exact versions of package-lock.json into ../playground-editor.js: one ES module, minified,
// with the licenses of every package inside it at its end. build.sh runs it after `npm ci`; see there.
//
//   node build.mjs            writes ../playground-editor.js
//   node build.mjs --check    fails where ../playground-editor.js is not what this would write

import * as esbuild from "esbuild";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const target = join(here, "..", "playground-editor.js");
const checking = process.argv.includes("--check");

const result = await esbuild.build({
  entryPoints: [join(here, "editor.mjs")],
  absWorkingDir: here,
  bundle: true,
  format: "esm",
  target: "es2020",
  minify: true,
  charset: "utf8",
  legalComments: "none",
  metafile: true,
  write: false,
});

// Every package a byte of the bundle came from, with its version and its license, from its own files
const packages = new Map();
for (const input of Object.keys(result.metafile.inputs)) {
  const found = /node_modules\/((?:@[^/]+\/)?[^/]+)\//.exec(input.replace(/\\/g, "/"));
  if (found && !packages.has(found[1])) {
    packages.set(found[1], join(here, "node_modules", found[1]));
  }
}
const notices = [];
for (const [name, directory] of [...packages].sort()) {
  const manifest = JSON.parse(readFileSync(join(directory, "package.json"), "utf8"));
  const licenseFile = ["LICENSE", "LICENSE.md", "LICENSE.txt", "license", "license.md"]
    .map((file) => join(directory, file))
    .find((file) => existsSync(file));
  const text = licenseFile ? readFileSync(licenseFile, "utf8").trim() : `License: ${manifest.license}`;
  notices.push(`${name} ${manifest.version} (${manifest.license})\n\n${text.replace(/\*\//g, "* /")}`);
}

const versions = [...packages]
  .map(([name, directory]) => `${name} ${JSON.parse(readFileSync(join(directory, "package.json"), "utf8")).version}`)
  .sort();
const banner = [
  "/*",
  " * playground-editor.js - the editor of the TorbScript playground: CodeMirror 6 and @codemirror/lsp-client, bundled",
  " * from playground/editor/editor.mjs by playground/editor/build.sh with the exact versions of its package-lock.json.",
  " * Generated: edit editor.mjs and run the script. The licenses of the packages are at the end of the file.",
  " *",
  ...versions.map((line) => ` *   ${line}`),
  " */",
  "",
].join("\n");
const code = new TextDecoder().decode(result.outputFiles[0].contents);
const bundle = `${banner}${code}\n/*\n${notices.join("\n\n---\n\n")}\n*/\n`;

if (checking) {
  const committed = existsSync(target) ? readFileSync(target, "utf8") : "";
  if (committed !== bundle) {
    console.error("playground/playground-editor.js is not what playground/editor/build.sh writes: run it and commit");
    process.exit(1);
  }
  console.log(`playground-editor.js is current: ${bundle.length} bytes, ${packages.size} packages`);
} else {
  writeFileSync(target, bundle);
  console.log(`wrote playground/playground-editor.js: ${bundle.length} bytes, ${packages.size} packages`);
}
