// Bundles editor.mjs with the exact versions of package-lock.json into three files beside the playground:
// ../playground-editor.js (one ES module, minified, with the licenses of every package inside it at its end),
// ../playground-editor.css (Monaco's stylesheet) and ../playground-codicon.ttf (the font of Monaco's icons). build.sh
// runs it after `npm ci`; see there.
//
//   node build.mjs            writes the three files
//   node build.mjs --check    fails where a committed file is not what this would write
//
// Three changes to Monaco's sources are made on the way, each checked, so an upgrade that moves what they change fails
// here rather than in a page: the diff editors, which the editor's API module creates but the playground never shows,
// become empty classes (170 KB less); the warning Monaco prints when it runs its worker's code in the page - which the
// playground chooses, see editor.mjs - is left out; and the codicon font is taken out of the stylesheet, because
// editor.mjs adds it with the query of the build (`?v=`) the stylesheet cannot know.

import * as esbuild from "esbuild";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const checking = process.argv.includes("--check");
const monacoRoot = join(here, "node_modules", "monaco-editor", "esm", "vs");

/** A source of Monaco with one exact piece of it replaced, or a failure where the piece is not there. */
function patched(file, original, replacement) {
  const text = readFileSync(file, "utf8");
  if (!text.includes(original)) {
    throw new Error(`build.mjs: ${file} no longer holds what the build changes: ${JSON.stringify(original.slice(0, 60))}`);
  }
  return text.replace(original, replacement);
}

const trimmed = {
  name: "trimmed-monaco",
  setup(build) {
    const stubs = {
      "diffEditorWidget.js": "export class DiffEditorWidget { constructor() { throw new Error('no diff editor in the playground'); } }\n",
      "multiDiffEditorWidget.js": "export class MultiDiffEditorWidget { constructor() { throw new Error('no diff editor in the playground'); } }\n",
    };
    build.onResolve({ filter: /\/(diffEditorWidget|multiDiffEditorWidget)\.js$/ }, function (args) {
      if (!/standalone[\\/]browser[\\/]standalone(Editor|CodeEditor)\.js$/.test(args.importer)) {
        return undefined;
      }
      return { path: args.path.slice(args.path.lastIndexOf("/") + 1), namespace: "stub" };
    });
    build.onLoad({ filter: /.*/, namespace: "stub" }, function (args) {
      return { contents: stubs[args.path], loader: "js" };
    });
    build.onLoad({ filter: /[\\/]base[\\/]common[\\/]worker[\\/]webWorker\.js$/ }, function (args) {
      const contents = patched(args.path, "function logOnceWebWorkerWarning(err) {", "function logOnceWebWorkerWarning(err) {\n    return;");
      return { contents: contents, loader: "js" };
    });
    build.onLoad({ filter: /[\\/]codicon[\\/]codicon\.css$/ }, function (args) {
      const face = '@font-face {\n\tfont-family: "codicon";\n\tfont-display: block;\n\tsrc: url(./codicon.ttf) format("truetype");\n}\n';
      return { contents: patched(args.path, face, ""), loader: "css" };
    });
  },
};

const result = await esbuild.build({
  entryPoints: { "playground-editor": join(here, "editor.mjs") },
  absWorkingDir: here,
  bundle: true,
  format: "esm",
  // The browsers the playground runs in (WebAssembly with exception handling): what they lack is lowered
  target: ["chrome95", "firefox100", "safari15.2"],
  minify: true,
  charset: "utf8",
  legalComments: "none",
  metafile: true,
  write: false,
  outdir: join(here, "out"),
  plugins: [trimmed],
  logLevel: "warning",
});

// Every package a byte of the bundle came from, with its version and its license, from its own files. Monaco carries
// two packages inside its own sources, at the versions it depends on: their licenses are those packages'
const vendored = { "vs/base/common/marked/": "marked", "vs/base/browser/dompurify/": "dompurify" };
const packages = new Map();
for (const input of Object.keys(result.metafile.inputs)) {
  const path = input.replace(/\\/g, "/");
  const found = /node_modules\/((?:@[^/]+\/)?[^/]+)\//.exec(path);
  if (found && !packages.has(found[1])) {
    packages.set(found[1], join(here, "node_modules", found[1]));
  }
  for (const [fragment, name] of Object.entries(vendored)) {
    if (path.includes(fragment) && !packages.has(name)) {
      packages.set(name, join(here, "node_modules", name));
    }
  }
}
const notices = [];
for (const [name, directory] of [...packages].sort()) {
  const manifest = JSON.parse(readFileSync(join(directory, "package.json"), "utf8"));
  const files = ["LICENSE", "LICENSE.md", "LICENSE.txt", "license", "license.md", "ThirdPartyNotices.txt"]
    .map((file) => join(directory, file))
    .filter((file) => existsSync(file));
  const text = files.length > 0 ? files.map((file) => readFileSync(file, "utf8").trim()).join("\n\n") : `License: ${manifest.license}`;
  notices.push(`${name} ${manifest.version} (${manifest.license})\n\n${text.replace(/\*\//g, "* /")}`);
}

const versions = [...packages]
  .map(([name, directory]) => `${name} ${JSON.parse(readFileSync(join(directory, "package.json"), "utf8")).version}`)
  .sort();
const banner = [
  "/*",
  " * playground-editor.js - the editor of the TorbScript playground: Monaco with a bridge of its own to torb lsp,",
  " * bundled from playground/editor/editor.mjs by playground/editor/build.sh with the exact versions of its",
  " * package-lock.json. Generated: edit the source and run the script. The licenses of the packages are at the end.",
  " *",
  ...versions.map((line) => ` *   ${line}`),
  " */",
  "",
].join("\n");

const outputs = new Map(result.outputFiles.map((file) => [file.path.replace(/\\/g, "/").split("/").pop(), file.contents]));
const code = new TextDecoder().decode(outputs.get("playground-editor.js"));
const style = new TextDecoder().decode(outputs.get("playground-editor.css"));
const monaco = versions.find((line) => line.startsWith("monaco-editor "));

const files = [
  ["playground-editor.js", `${banner}${code}\n/*\n${notices.join("\n\n---\n\n")}\n*/\n`],
  [
    "playground-editor.css",
    `/* playground-editor.css - the stylesheet of ${monaco} (MIT), bundled by playground/editor/build.sh; its license is at the end of playground-editor.js */\n${style}`,
  ],
  ["playground-codicon.ttf", readFileSync(join(monacoRoot, "base", "browser", "ui", "codicons", "codicon", "codicon.ttf"))],
];

let stale = [];
for (const [name, contents] of files) {
  const target = join(here, "..", name);
  const bytes = typeof contents === "string" ? Buffer.from(contents, "utf8") : contents;
  if (checking) {
    if (!existsSync(target) || !readFileSync(target).equals(bytes)) {
      stale.push(name);
    }
  } else {
    writeFileSync(target, bytes);
  }
}
const sizes = files.map(([name, contents]) => `${name} ${Buffer.byteLength(contents)} bytes`).join(", ");
if (checking) {
  if (stale.length > 0) {
    console.error(`playground/${stale.join(", playground/")}: not what playground/editor/build.sh writes: run it and commit`);
    process.exit(1);
  }
  console.log(`the editor is current: ${sizes}, ${packages.size} packages`);
} else {
  console.log(`wrote ${sizes}, ${packages.size} packages`);
}
