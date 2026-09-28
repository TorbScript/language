// The debugger of docs/design/DEBUGGER.md in VS Code: `torb debug` is the debug adapter, started with the `torb`
// ./toolchain.js found, and speaks the Debug Adapter Protocol over its standard input and output.
//
// **Launching**: a `launch.json` configuration of the type `torbscript` names a `program` (a file or a directory, as
// `torb run` takes it) or a `test` (a test file or a directory, with an optional `filter`), plus `args`, `cwd`, `env`,
// `stopOnEntry` and `justMyCode`. Without a `launch.json`, F5 debugs the file in the editor: its tests where it is a
// `*.test.trb`, the file itself otherwise.
//
// **Entry files**: "Debug File" sits next to "Run File" in the editor's run menu, and a CodeLens "Run | Debug" above
// the first line of an entry file - `src/main.trb`, or a file with top-level code that is no test file.
//
// **Tests**: the Test Explorer's Debug profile lives in ./testing.js; it starts sessions through `debugTests` below.

'use strict';

const TYPE = 'torbscript';

/** Whether a line of a file is top-level code: a statement at the left margin that no declaration starts. */
const DECLARATION = /^(use|fn|type|trait|extend|public|private|native|shared|static|const|var|case|foreign|with|\}|\/\/|\/\*|\*)/;

/**
 * Whether a document is an entry file the CodeLens and "Debug File" are for: `main.trb`, or a file with a statement at
 * its top level - a `const` or a `var` binding counts only in `main.trb`, since a module may declare those too.
 */
function isEntryDocument(document) {
  const name = document.uri.path.split('/').pop() || '';
  if (!name.endsWith('.trb') || name.endsWith('.test.trb') || name === 'project.trb') {
    return false;
  }
  if (name === 'main.trb') {
    return true;
  }
  for (let index = 0; index < document.lineCount; index += 1) {
    const line = document.lineAt(index).text;
    if (/^[A-Za-z_]/.test(line) && !DECLARATION.test(line)) {
      return true;
    }
  }
  return false;
}

/** The first line of an entry file that is not a comment, a `use` or empty: where the CodeLens goes. */
function firstCodeLine(document) {
  for (let index = 0; index < document.lineCount; index += 1) {
    const line = document.lineAt(index).text.trim();
    if (line && !line.startsWith('//') && !line.startsWith('/*') && !line.startsWith('*') && !line.startsWith('use ')) {
      return index;
    }
  }
  return 0;
}

class TorbDebugging {
  constructor(context, vscode, path, toolchain, log) {
    this.vscode = vscode;
    this.path = path;
    this.toolchain = toolchain;
    this.log = log;
    const changed = new vscode.EventEmitter();
    this.lensesChanged = changed;
    context.subscriptions.push(
      changed,
      vscode.debug.registerDebugAdapterDescriptorFactory(TYPE, {
        createDebugAdapterDescriptor: (session) => this.adapterFor(session),
      }),
      vscode.debug.registerDebugConfigurationProvider(TYPE, {
        resolveDebugConfiguration: (folder, configuration) => this.resolve(folder, configuration),
      }),
      vscode.languages.registerCodeLensProvider(
        { language: 'trb', scheme: 'file' },
        {
          onDidChangeCodeLenses: changed.event,
          provideCodeLenses: (document) => this.lensesOf(document),
        }
      ),
      vscode.commands.registerCommand('torbscript.debugFile', (uri) => this.debugFile(uri)),
      vscode.workspace.onDidChangeTextDocument(() => changed.fire())
    );
  }

  /** `torb debug`, in the workspace folder of the session, where the program's `cwd` does not say otherwise. */
  adapterFor(session) {
    const { vscode } = this;
    const folder = session.workspaceFolder ? session.workspaceFolder.uri.fsPath : undefined;
    const cwd = session.configuration.cwd || folder;
    this.log(`debugging with ${this.toolchain.command()} debug${cwd ? ` in ${cwd}` : ''}`);
    return new vscode.DebugAdapterExecutable(this.toolchain.command(), ['debug'], cwd ? { cwd } : {});
  }

  /**
   * A configuration made whole: F5 without a `launch.json` debugs the file in the editor - its tests where it is a test
   * file - and every configuration runs in its workspace folder unless it names a `cwd`.
   */
  resolve(folder, configuration) {
    const { vscode } = this;
    if (!this.toolchain.isFound()) {
      vscode.window.showWarningMessage('Debugging needs the TorbScript toolchain.', 'Install TorbScript').then((answer) => {
        if (answer) {
          vscode.commands.executeCommand('torbscript.installToolchain');
        }
      });
      return undefined;
    }
    const resolved = { ...configuration };
    if (!resolved.type && !resolved.request && !resolved.name) {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== 'trb') {
        vscode.window.showInformationMessage('Open a .trb file to debug it, or add a TorbScript configuration to launch.json.');
        return undefined;
      }
      resolved.type = TYPE;
      resolved.request = 'launch';
      if (editor.document.uri.path.endsWith('.test.trb')) {
        resolved.name = 'Debug Tests of File';
        resolved.test = '${file}';
      } else {
        resolved.name = 'Debug File';
        resolved.program = '${file}';
      }
    }
    if (!resolved.program && !resolved.test) {
      vscode.window.showErrorMessage('A TorbScript debug configuration names a `program` or a `test`.');
      return undefined;
    }
    if (!resolved.cwd && folder) {
      resolved.cwd = folder.uri.fsPath;
    }
    return resolved;
  }

  /** "Run | Debug" above the first line of code of an entry file. */
  lensesOf(document) {
    const { vscode } = this;
    if (!isEntryDocument(document)) {
      return [];
    }
    const line = firstCodeLine(document);
    const range = new vscode.Range(line, 0, line, 0);
    return [
      new vscode.CodeLens(range, { title: '$(play) Run', command: 'torbscript.runFile', arguments: [document.uri] }),
      new vscode.CodeLens(range, { title: '$(debug-alt) Debug', command: 'torbscript.debugFile', arguments: [document.uri] }),
    ];
  }

  /** "TorbScript: Debug File": the file saved and debugged from its workspace folder, as "Run File" runs it. */
  async debugFile(uri) {
    const { vscode, path } = this;
    const editor = vscode.window.activeTextEditor;
    const target = uri instanceof vscode.Uri ? uri : editor && editor.document.languageId === 'trb' ? editor.document.uri : undefined;
    if (!target) {
      vscode.window.showInformationMessage('Open a .trb file to debug it.');
      return;
    }
    const document = vscode.workspace.textDocuments.find((each) => each.uri.toString() === target.toString());
    if (document && document.isDirty) {
      await document.save();
    }
    const folder = vscode.workspace.getWorkspaceFolder(target);
    const cwd = folder ? folder.uri.fsPath : path.dirname(target.fsPath);
    const isTest = target.path.endsWith('.test.trb');
    const configuration = {
      type: TYPE,
      request: 'launch',
      name: `Debug ${path.relative(cwd, target.fsPath)}`,
      cwd,
      [isTest ? 'test' : 'program']: target.fsPath,
    };
    await vscode.debug.startDebugging(folder, configuration);
  }
}

/** Starts the debugger's part of the extension. */
function startDebugging(context, vscode, path, toolchain, log) {
  return new TorbDebugging(context, vscode, path, toolchain, log);
}

module.exports = { startDebugging, isEntryDocument, firstCodeLine, TYPE };
