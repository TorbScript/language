// "Format Document" for `.trb` files: `torb format` (docs/tooling/torb-format.md) over the text the editor holds.
//
// `torb format` rewrites files in place and reads no standard input, so the text is written to a file of its own in the
// system's temporary directory, formatted there and read back. The layout depends on the syntax tree alone; the one
// thing `torb format` asks the type checker - which callee is a field that holds a function - a copy there answers for
// what it reaches, and a call whose callee depends on a module the copy does not reach is left as it is written, never
// made a command that would not type check. A text that does not parse is left as it is, and the status bar says so.

'use strict';

let counter = 0;

function registerFormatter(context, vscode, cp, fs, os, path, toolchain, log) {
  const provider = {
    async provideDocumentFormattingEdits(document, options, token) {
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
    },
  };
  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider({ language: 'trb', scheme: 'file' }, provider),
    vscode.languages.registerDocumentFormattingEditProvider({ language: 'trb', scheme: 'untitled' }, provider)
  );
}

module.exports = { registerFormatter };
