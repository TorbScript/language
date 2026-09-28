// "Format Document" and "Format Selection" for `.trb` files: the layout of `torb format` (docs/tooling/torb-format.md).
//
// Where the language server runs, it answers (`textDocument/formatting` and `rangeFormatting`): the same code as
// `torb format`, over the text the editor holds and with what its checker knows about the callees, as the few edits that
// change it. Where it does not - it is turned off, it has not started, or the document is untitled and the server does
// not know it - `torb format` rewrites a copy: the text is written to a file of its own in the system's temporary
// directory, formatted there and read back. The layout depends on the syntax tree alone; the one thing `torb format`
// asks the type checker - which callee is a field that holds a function - a copy there answers for what it reaches, and
// a call whose callee depends on a module the copy does not reach is left as it is written, never made a command that
// would not type check. That fallback formats whole documents only. A text that does not parse is left as it is, and
// the status bar says so.

'use strict';

let counter = 0;

function registerFormatter(context, vscode, cp, fs, os, path, toolchain, log, languageClient = null) {
  /** The whole document through `torb format` over a temporary copy. */
  async function formatCopy(document, token) {
    const executable = toolchain.executable;
    if (!executable) {
      vscode.window.setStatusBarMessage('torb format needs the TorbScript toolchain', 5000);
      return [];
    }
    const text = document.getText();
    counter += 1;
    const directory = path.join(os.tmpdir(), `torbscript-format-${process.pid}-${counter}`);
    const file = path.join(directory, 'document.trb');
    try {
      fs.mkdirSync(directory, { recursive: true });
      fs.writeFileSync(file, text, 'utf8');
      const outcome = await new Promise((resolve) => {
        const child = cp.execFile(executable, ['format', file], { timeout: 20000, windowsHide: true }, (error, stdout, stderr) => {
          resolve({ error, output: `${stdout || ''}${stderr || ''}`.trim() });
        });
        token.onCancellationRequested(() => child.kill());
      });
      if (token.isCancellationRequested) {
        return [];
      }
      const formatted = fs.readFileSync(file, 'utf8');
      if (outcome.error && formatted === text) {
        log(`torb format: ${outcome.output || outcome.error.message}`);
        vscode.window.setStatusBarMessage('torb format left the file alone: it does not parse', 5000);
        return [];
      }
      if (formatted === text) {
        return [];
      }
      const whole = new vscode.Range(document.positionAt(0), document.positionAt(text.length));
      return [vscode.TextEdit.replace(whole, formatted)];
    } catch (error) {
      log(`torb format failed: ${error.message}`);
      return [];
    } finally {
      fs.rmSync(directory, { recursive: true, force: true });
    }
  }

  const provider = {
    async provideDocumentFormattingEdits(document, options, token) {
      if (languageClient) {
        const edits = await languageClient.formatting(document, null);
        if (edits !== undefined) {
          return edits;
        }
      }
      return formatCopy(document, token);
    },

    // Only the language server formats a part of a document: the fallback would change the whole of it
    async provideDocumentRangeFormattingEdits(document, range) {
      if (!languageClient) {
        return [];
      }
      return (await languageClient.formatting(document, range)) || [];
    },
  };
  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider({ language: 'trb', scheme: 'file' }, provider),
    vscode.languages.registerDocumentFormattingEditProvider({ language: 'trb', scheme: 'untitled' }, provider),
    vscode.languages.registerDocumentRangeFormattingEditProvider({ language: 'trb', scheme: 'file' }, provider)
  );
}

module.exports = { registerFormatter };
