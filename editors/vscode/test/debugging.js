// The debugger in a real VS Code: what `sh tools/vscode-test.sh` runs through `--extensionTestsPath`, in an Extension
// Development Host whose workspace holds `main.trb` and `math.test.trb` (tests/debug/) and whose settings name the
// `torb` under test. It debugs the program to a breakpoint, reads the stack and the locals, evaluates a field, steps in
// and over, and goes on to the end; checks the CodeLens of the entry file; and debugs the test file through the Test
// Explorer's Debug profile, whose report arrives as `torbscript/testReport` events. What it found is written to the
// file `TORBSCRIPT_TEST_RESULT` names, and a failure fails the run.

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

/** Every message the adapters of the host sent, in order. */
const messages = [];

/** What went wrong with an adapter itself: an error of its stream, or its exit with a code. */
const troubles = [];

/** Resolves once `check` answers something, or rejects after `milliseconds`. */
function waitFor(what, check, milliseconds = 60000) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const timer = setInterval(() => {
      const found = check();
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

function eventsNamed(name, after = 0) {
  return messages.slice(after).filter((message) => message.type === 'event' && message.event === name);
}

async function debugProgram(folder, lines) {
  const program = vscode.Uri.file(path.join(folder.uri.fsPath, 'main.trb'));
  vscode.debug.addBreakpoints([new vscode.SourceBreakpoint(new vscode.Location(program, new vscode.Position(17, 0)))]);
  const before = messages.length;
  const started = await vscode.debug.startDebugging(folder, {
    type: 'torbscript',
    request: 'launch',
    name: 'Debug main.trb',
    program: program.fsPath,
  });
  assert.ok(started, `the debug session started${troubles.length ? ` (${troubles.join('; ')})` : ''}`);
  await waitFor('the stop at the breakpoint', () => eventsNamed('stopped', before)[0]);
  const session = vscode.debug.activeDebugSession;
  assert.ok(session, 'there is an active debug session');
  const trace = await session.customRequest('stackTrace', { threadId: 1 });
  assert.strictEqual(trace.stackFrames[0].line, 18, 'the breakpoint stopped at line 18');
  lines.push(`stopped at ${trace.stackFrames[0].name}:${trace.stackFrames[0].line}`);
  const scopes = await session.customRequest('scopes', { frameId: trace.stackFrames[0].id });
  const locals = await session.customRequest('variables', { variablesReference: scopes.scopes[0].variablesReference });
  const shown = locals.variables.map((variable) => `${variable.name} = ${variable.value}`);
  assert.deepStrictEqual(shown, ['origin = Point(x: 1, y: 2)', 'numbers = [10, 20, 30]', 'label = "sum"']);
  lines.push(`locals: ${shown.join(', ')}`);
  const evaluated = await session.customRequest('evaluate', { expression: 'origin.y', frameId: trace.stackFrames[0].id, context: 'hover' });
  assert.strictEqual(evaluated.result, '2');
  lines.push(`origin.y = ${evaluated.result}`);
  const beforeStep = messages.length;
  await session.customRequest('stepIn', { threadId: 1 });
  await waitFor('the stop after the step in', () => eventsNamed('stopped', beforeStep)[0]);
  const inside = await session.customRequest('stackTrace', { threadId: 1 });
  assert.strictEqual(inside.stackFrames[0].name, 'sum');
  lines.push(`stepped into ${inside.stackFrames[0].name}:${inside.stackFrames[0].line}`);
  const beforeNext = messages.length;
  await session.customRequest('next', { threadId: 1 });
  await waitFor('the stop after the step over', () => eventsNamed('stopped', beforeNext)[0]);
  const next = await session.customRequest('stackTrace', { threadId: 1 });
  lines.push(`stepped over to ${next.stackFrames[0].name}:${next.stackFrames[0].line}`);
  const beforeEnd = messages.length;
  await session.customRequest('continue', { threadId: 1 });
  const exited = await waitFor('the end of the program', () => eventsNamed('exited', beforeEnd)[0]);
  assert.strictEqual(exited.body.exitCode, 0);
  const output = eventsNamed('output', before)
    .map((event) => event.body.output)
    .join('');
  assert.ok(output.includes('sum 60 at 1'), `the program printed its line: ${JSON.stringify(output)}`);
  lines.push(`exited with ${exited.body.exitCode}, printed ${JSON.stringify(output.trim())}`);
  vscode.debug.removeBreakpoints(vscode.debug.breakpoints);
}

async function checkLenses(folder, lines) {
  const program = vscode.Uri.file(path.join(folder.uri.fsPath, 'main.trb'));
  await vscode.window.showTextDocument(program);
  const lenses = await vscode.commands.executeCommand('vscode.executeCodeLensProvider', program);
  const commands = (lenses || []).map((lens) => lens.command && lens.command.command);
  assert.ok(commands.includes('torbscript.runFile'), 'the entry file has Run');
  assert.ok(commands.includes('torbscript.debugFile'), 'the entry file has Debug');
  lines.push(`CodeLens: ${commands.join(', ')}`);
}

async function debugTests(folder, lines) {
  const before = messages.length;
  const file = vscode.Uri.file(path.join(folder.uri.fsPath, 'math.test.trb'));
  await vscode.window.showTextDocument(file);
  await vscode.commands.executeCommand('testing.debugAll');
  const reports = await waitFor('the report of the debugged tests', () => {
    const found = eventsNamed('torbscript/testReport', before).filter((event) => event.body.event === 'test');
    return found.length >= 2 ? found : undefined;
  });
  const outcomes = reports.map((event) => `${(event.body.groups || []).concat([event.body.name]).join(' > ')}: ${event.body.outcome}`);
  assert.deepStrictEqual(outcomes.sort(), ['Math > doubles: passed', 'Math > halves: passed']);
  lines.push(`Debug profile: ${outcomes.join(', ')}`);
}

async function run() {
  const lines = [];
  const result = process.env.TORBSCRIPT_TEST_RESULT;
  try {
    const extension = vscode.extensions.getExtension('torbscript.torbscript');
    assert.ok(extension, 'the extension is loaded');
    await extension.activate();
    vscode.debug.registerDebugAdapterTrackerFactory('torbscript', {
      createDebugAdapterTracker: () => ({
        onDidSendMessage: (message) => messages.push(message),
        onError: (error) => troubles.push(`the adapter's stream failed: ${error.message}`),
        onExit: (code, signal) => {
          if (code !== 0) {
            troubles.push(`the adapter exited with ${code}${signal ? ` (${signal})` : ''}`);
          }
        },
      }),
    });
    const folder = vscode.workspace.workspaceFolders[0];
    await debugProgram(folder, lines);
    await checkLenses(folder, lines);
    await debugTests(folder, lines);
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
