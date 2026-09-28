// The language server in a real VS Code: what `sh tools/vscode-test.sh language-server` runs through
// `--extensionTestsPath`, in an Extension Development Host whose workspace is the package of tests/lsp/navigation/ with
// a file beside it that has a problem nobody opened. It asks VS Code's own commands - the ones its editors run - for a
// hover, signature help, the references, the symbols of the document and of the workspace, a completion and its
// documentation, the formatting of a document and a rename, waits for the diagnostics of the file nobody opened, and
// times a keystroke inside of a function until its diagnostics arrive. What it found is written to the file
// `TORBSCRIPT_TEST_RESULT` names, and a failure fails the run.

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

/** Resolves once `check` answers something, or rejects after `milliseconds`. */
function waitFor(what, check, milliseconds = 60000) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const timer = setInterval(async () => {
      const found = await check();
      if (found) {
        clearInterval(timer);
        resolve(found);
      } else if (Date.now() - started > milliseconds) {
        clearInterval(timer);
        reject(new Error(`waited ${milliseconds} ms for ${what}`));
      }
    }, 50);
  });
}

function textOf(contents) {
  return contents.map((each) => (typeof each === 'string' ? each : each.value)).join('\n');
}

async function run() {
  const lines = [];
  const result = process.env.TORBSCRIPT_TEST_RESULT;
  try {
    const extension = vscode.extensions.getExtension('torbscript.torbscript');
    assert.ok(extension, 'the extension is loaded');
    await extension.activate();
    const folder = vscode.workspace.workspaceFolders[0].uri.fsPath;
    const main = vscode.Uri.file(path.join(folder, 'src', 'main.trb'));
    const helper = vscode.Uri.file(path.join(folder, 'src', 'helper.trb'));
    const broken = vscode.Uri.file(path.join(folder, 'src', 'broken.trb'));
    const document = await vscode.workspace.openTextDocument(main);
    const editor = await vscode.window.showTextDocument(document);
    const onDouble = new vscode.Position(4, 12);

    // The server has started and checked the document once the hover answers
    const hovers = await waitFor('a hover of the language server', async () => {
      const found = await vscode.commands.executeCommand('vscode.executeHoverProvider', main, onDouble);
      return found && found.length > 0 && textOf(found[0].contents).includes('Twice the value') ? found : undefined;
    });
    const hover = textOf(hovers[0].contents);
    assert.ok(hover.includes('fn double(value: Int): Int'), `the hover shows the signature: ${hover}`);
    assert.ok(hover.includes('triple'), 'the hover shows the link of the doc comment');
    lines.push('hover: the signature and the doc comment of `double`, with its link');

    const help = await vscode.commands.executeCommand('vscode.executeSignatureHelpProvider', main, new vscode.Position(7, 19), ',');
    assert.ok(help && help.signatures.length === 1, 'signature help answers');
    assert.strictEqual(help.signatures[0].label, 'fn addDouble(total: Int, value: Int): Int');
    assert.strictEqual(help.activeParameter, 1);
    lines.push(`signature help: ${help.signatures[0].label}, parameter ${help.activeParameter}`);

    const references = await vscode.commands.executeCommand('vscode.executeReferenceProvider', main, onDouble);
    const places = references.map((each) => `${path.basename(each.uri.fsPath)}:${each.range.start.line + 1}`).sort();
    assert.deepStrictEqual(places, ['helper.trb:8', 'main.trb:1', 'main.trb:5']);
    lines.push(`references: ${places.join(', ')}`);

    const symbols = await vscode.commands.executeCommand('vscode.executeDocumentSymbolProvider', main);
    assert.deepStrictEqual(symbols.map((each) => each.name), ['addDouble']);
    const found = await vscode.commands.executeCommand('vscode.executeWorkspaceSymbolProvider', 'triple');
    assert.ok(found.some((each) => each.name === 'triple'), 'the workspace symbols hold `triple`');
    lines.push(`symbols: ${symbols.map((each) => each.name).join(', ')}; of the workspace: ${found.map((each) => each.name).join(', ')}`);

    const completion = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', main, new vscode.Position(4, 12), undefined, 50);
    const item = completion.items.find((each) => each.label === 'double' || each.label.label === 'double');
    assert.ok(item, 'the completion offers `double`');
    const documentation = item.documentation ? item.documentation.value || item.documentation : '';
    assert.ok(String(documentation).includes('Twice the value'), `the item resolves to its documentation: ${documentation}`);
    lines.push('completion: `double`, resolved to its signature and documentation');

    // Diagnostics of a file nobody opened, from the check of the workspace
    const problems = await waitFor('the diagnostics of a file nobody opened', () => {
      const found = vscode.languages.getDiagnostics(broken);
      return found.length > 0 ? found : undefined;
    });
    lines.push(`diagnostics of the workspace: broken.trb: ${problems[0].message}`);

    // A keystroke inside of a function, until its diagnostics arrive
    const before = Date.now();
    await editor.edit((edit) => edit.insert(new vscode.Position(4, 16), 'q'));
    await waitFor('the diagnostics of a keystroke', () => {
      const found = vscode.languages.getDiagnostics(main);
      return found.some((each) => each.message.includes('doubleq')) ? found : undefined;
    });
    lines.push(`a keystroke inside of a function: its diagnostics after ${Date.now() - before} ms`);
    await editor.edit((edit) => edit.delete(new vscode.Range(4, 16, 4, 17)));
    await waitFor('the diagnostics once the keystroke is undone', () => vscode.languages.getDiagnostics(main).length === 0);

    // Formatting through the language server: a line out of the layout
    await editor.edit((edit) => edit.replace(new vscode.Range(4, 0, 4, 2), '      '));
    const edits = await vscode.commands.executeCommand('vscode.executeFormatDocumentProvider', main, { tabSize: 2, insertSpaces: true });
    assert.ok(edits && edits.length === 1, `formatting answers one edit: ${JSON.stringify(edits)}`);
    assert.strictEqual(edits[0].range.start.line, 4, 'the edit is the line out of the layout');
    await editor.edit((edit) => edit.replace(edits[0].range, edits[0].newText));
    assert.strictEqual(editor.document.lineAt(4).text, '  total + double(value)');
    lines.push('formatting: one edit of the line out of the layout');

    // A rename: every place, in both files
    const prepared = await vscode.commands.executeCommand('vscode.prepareRename', main, onDouble);
    assert.ok(prepared, 'prepareRename answers');
    const rename = await vscode.commands.executeCommand('vscode.executeDocumentRenameProvider', main, onDouble, 'twice');
    const renamed = rename.entries().map(([uri, changes]) => `${path.basename(uri.fsPath)}: ${changes.length}`).sort();
    assert.deepStrictEqual(renamed, ['helper.trb: 1', 'main.trb: 2']);
    lines.push(`rename: ${renamed.join(', ')}`);
    let refused = '';
    try {
      await vscode.commands.executeCommand('vscode.executeDocumentRenameProvider', main, onDouble, 'Twice');
    } catch (error) {
      refused = error.message;
    }
    assert.ok(refused.includes('lowercase'), `a name of the wrong case is refused: ${refused}`);
    lines.push(`a refused rename: ${refused}`);
    assert.ok(helper, 'the helper exists');
    lines.push('ok');
  } catch (error) {
    lines.push(`FAILED: ${error.stack || error}`);
    throw error;
  } finally {
    if (result) {
      fs.writeFileSync(result, lines.join('\n') + '\n');
    }
  }
}

module.exports = { run };
