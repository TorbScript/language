// The tests of a workspace in VS Code's Test Explorer, and in the gutter of every `*.test.trb` file.
//
// **Finding them**: every `*.test.trb` of the workspace is an item. What is inside a file - its `group "..."` and
// `test "..."` calls, with where they are - comes from the language server's request `torbscript/tests`
// (docs/tooling/torb-lsp.md), which parses the file as the editor holds it, open or not. A test whose name is built at
// run time (`test "adds {count}"`) is not in that answer; it appears in the tree once a run reports it.
//
// **Running them**: `torb test --report json` (docs/tooling/torb-test.md) - in the VM, or natively with the profile
// "Run Natively" - with a `--filter "<group> > <test>"` for every group or test that was chosen. The report is one JSON
// object per line on standard output; every other line is what the running test printed. A file, a group and a test
// map to the items by the file's path and the test's full name (`Outer > Inner > name`), which is also the item's id.
//
// **Continuous run**: both profiles support it (the eye in the Test Explorer): the chosen tests run again whenever a
// `.trb` file of the workspace is saved.

'use strict';

const FILE_PATTERN = '**/*.test.trb';
const EXCLUDED = '{**/build/**,**/node_modules/**,**/.*/**}';
const SEPARATOR = ' > ';

/** The data the extension keeps beside each item. */
const kinds = new WeakMap();

class TorbTests {
  constructor(context, vscode, cp, fs, path, toolchain, client, log) {
    this.context = context;
    this.vscode = vscode;
    this.cp = cp;
    this.fs = fs;
    this.path = path;
    this.reportSupportByBinary = new Map();
    this.toolchain = toolchain;
    this.client = client;
    this.log = log;
    this.controller = vscode.tests.createTestController('torbscript', 'TorbScript');
    this.controller.resolveHandler = (item) => this.resolve(item);
    this.controller.refreshHandler = () => this.discoverAll(true);
    this.refreshTimers = new Map();
    this.listed = false;
    const run = (request, token) => this.run(request, token, false);
    const native = (request, token) => this.run(request, token, true);
    this.vmProfile = this.controller.createRunProfile('Run', vscode.TestRunProfileKind.Run, run, true, undefined, true);
    this.nativeProfile = this.controller.createRunProfile('Run Natively', vscode.TestRunProfileKind.Run, native, false, undefined, true);
    context.subscriptions.push(this.controller);

    const watcher = vscode.workspace.createFileSystemWatcher(FILE_PATTERN);
    context.subscriptions.push(
      watcher,
      watcher.onDidCreate((uri) => this.listed && this.fileItem(uri)),
      watcher.onDidDelete((uri) => this.removeFile(uri)),
      watcher.onDidChange((uri) => this.scheduleRefresh(uri)),
      vscode.workspace.onDidOpenTextDocument((document) => this.documentOpened(document)),
      vscode.workspace.onDidChangeTextDocument((event) => this.documentOpened(event.document)),
      vscode.workspace.onDidChangeWorkspaceFolders(() => this.discoverAll(true))
    );
    for (const document of vscode.workspace.textDocuments) {
      this.documentOpened(document);
    }
  }

  isTestFile(uri) {
    return uri.scheme === 'file' && uri.path.endsWith('.test.trb') && Boolean(this.vscode.workspace.getWorkspaceFolder(uri));
  }

  // --- Discovery ---------------------------------------------------------------------------------------------------

  async resolve(item) {
    if (!item) {
      await this.discoverAll(false);
      return;
    }
    const kind = kinds.get(item);
    if (kind && kind.type === 'file') {
      await this.refreshFile(item.uri);
    }
  }

  /** Every `*.test.trb` of the workspace as an item; `again` asks the server for the tests of each once more. */
  async discoverAll(again) {
    const uris = await this.vscode.workspace.findFiles(FILE_PATTERN, EXCLUDED);
    const seen = new Set();
    for (const uri of uris) {
      if (!this.isTestFile(uri)) {
        continue;
      }
      const item = this.fileItem(uri);
      seen.add(item.id);
      if (again) {
        await this.refreshFile(uri);
      }
    }
    this.controller.items.forEach((item) => {
      if (!seen.has(item.id)) {
        this.controller.items.delete(item.id);
      }
    });
    this.listed = true;
    if (again) {
      let groups = 0;
      let tests = 0;
      const count = (item) => {
        const kind = kinds.get(item);
        if (kind && kind.type === 'group') {
          groups += 1;
        } else if (kind && kind.type === 'test') {
          tests += 1;
        }
        item.children.forEach(count);
      };
      this.controller.items.forEach(count);
      this.log(`the Test Explorer: ${seen.size} test file(s), ${groups} group(s) and ${tests} test(s) in their source`);
    }
  }

  /** The item of a file, made where there is none yet. */
  fileItem(uri) {
    const id = uri.toString();
    const existing = this.controller.items.get(id);
    if (existing) {
      return existing;
    }
    const label = this.vscode.workspace.asRelativePath(uri, (this.vscode.workspace.workspaceFolders || []).length > 1);
    const item = this.controller.createTestItem(id, label, uri);
    item.canResolveChildren = true;
    kinds.set(item, { type: 'file', fullName: '' });
    this.controller.items.add(item);
    return item;
  }

  removeFile(uri) {
    this.controller.items.delete(uri.toString());
  }

  documentOpened(document) {
    if (document.languageId === 'trb' && this.isTestFile(document.uri)) {
      this.fileItem(document.uri);
      this.scheduleRefresh(document.uri);
    }
  }

  /** Asks for the tests of a file half a second after its last change, so typing costs one request. */
  scheduleRefresh(uri) {
    if (!this.isTestFile(uri)) {
      return;
    }
    const key = uri.toString();
    clearTimeout(this.refreshTimers.get(key));
    this.refreshTimers.set(
      key,
      setTimeout(() => {
        this.refreshTimers.delete(key);
        this.refreshFile(uri);
      }, 500)
    );
  }

  /**
   * The groups and tests of one file, as the language server answers them. Where it does not run, or is a `torb` too
   * old to know the request, the file stays one item that runs as a whole.
   */
  async refreshFile(uri) {
    const file = this.fileItem(uri);
    if (!this.client || !this.client.isRunning() || !this.client.supportsTests()) {
      file.canResolveChildren = false;
      return;
    }
    const nodes = await this.client.tests(uri);
    if (!Array.isArray(nodes)) {
      file.canResolveChildren = false;
      return;
    }
    file.error = undefined;
    this.replaceChildren(file, uri, nodes, []);
    file.canResolveChildren = false;
  }

  /** The children of `parent` as `nodes` say, keeping the items a run added for tests the source does not name. */
  replaceChildren(parent, uri, nodes, groups) {
    const children = [];
    const seen = new Set();
    for (const node of nodes) {
      if (!node || typeof node.name !== 'string' || (node.kind !== 'group' && node.kind !== 'test')) {
        continue;
      }
      const path = groups.concat([node.name]);
      const fullName = path.join(SEPARATOR);
      const id = `${uri.toString()}#${fullName}`;
      if (seen.has(id)) {
        continue; // Two tests of one name in one scope: the report cannot tell them apart either
      }
      seen.add(id);
      const item = parent.children.get(id) || this.controller.createTestItem(id, node.name, uri);
      item.range = this.client.fromRange(node.selectionRange || node.range);
      kinds.set(item, { type: node.kind, fullName, fromSource: true });
      if (node.kind === 'group') {
        this.replaceChildren(item, uri, Array.isArray(node.children) ? node.children : [], path);
      }
      children.push(item);
    }
    // Items a run made for tests the source does not name stay until the file changes their parent's shape
    parent.children.forEach((child) => {
      const kind = kinds.get(child);
      if (!seen.has(child.id) && kind && !kind.fromSource) {
        children.push(child);
      }
    });
    parent.children.replace(children);
  }

  /** The item of a group or a test below a file, made (without a place in the source) where the tree lacks it. */
  itemFor(file, groups, name) {
    const uri = file.uri;
    let parent = file;
    const path = [];
    for (const group of groups.concat([name])) {
      path.push(group);
      const fullName = path.join(SEPARATOR);
      const id = `${uri.toString()}#${fullName}`;
      let item = parent.children.get(id);
      if (!item) {
        item = this.controller.createTestItem(id, group, uri);
        kinds.set(item, { type: path.length === groups.length + 1 ? 'test' : 'group', fullName, fromSource: false });
        parent.children.add(item);
      }
      parent = item;
    }
    return parent;
  }

  // --- Running -----------------------------------------------------------------------------------------------------

  async run(request, token, native) {
    const { vscode } = this;
    if (!this.toolchain.isFound()) {
      await this.toolchain.check();
    }
    if (!this.toolchain.isFound()) {
      vscode.window.showWarningMessage('Running tests needs the TorbScript toolchain.', 'Install TorbScript').then((answer) => {
        if (answer) {
          vscode.commands.executeCommand('torbscript.installToolchain');
        }
      });
      return;
    }
    await this.runOnce(request, token, native);
    if (!request.continuous) {
      return;
    }
    // Continuous run: the same request again after every save of a `.trb` file, until the person stops it
    let timer = null;
    let running = Promise.resolve();
    const saved = vscode.workspace.onDidSaveTextDocument((document) => {
      if (document.languageId !== 'trb' || !vscode.workspace.getWorkspaceFolder(document.uri)) {
        return;
      }
      clearTimeout(timer);
      timer = setTimeout(() => {
        running = running.then(() => (token.isCancellationRequested ? undefined : this.runOnce(request, token, native)));
      }, 300);
    });
    await new Promise((resolve) => token.onCancellationRequested(resolve));
    clearTimeout(timer);
    saved.dispose();
  }

  /** One run of the request: one `torb test` per workspace folder and per file whose tests were picked one by one. */
  async runOnce(request, token, native) {
    const { vscode } = this;
    if (!this.listed && !request.include) {
      await this.discoverAll(false);
    }
    const run = this.controller.createTestRun(request, undefined, true);
    const excluded = new Set((request.exclude || []).map((item) => item.id));
    const chosen = request.include || this.allItems();
    // Whole files run together; a file of which only some groups or tests were chosen runs with filters, alone
    const wholeFiles = new Map(); // workspace folder path -> [file items]
    const filtered = new Map(); // file id -> { file, names: [] }
    for (const item of chosen) {
      if (excluded.has(item.id)) {
        continue;
      }
      const file = this.fileOf(item);
      if (!file) {
        continue;
      }
      const folder = vscode.workspace.getWorkspaceFolder(file.uri);
      if (!folder) {
        continue;
      }
      const kind = kinds.get(item);
      const excludedBelow = this.hasExcludedDescendant(item, excluded);
      if (kind.type === 'file' && !excludedBelow) {
        const list = wholeFiles.get(folder.uri.fsPath) || [];
        list.push(file);
        wholeFiles.set(folder.uri.fsPath, list);
      } else {
        const entry = filtered.get(file.id) || { file, names: [] };
        for (const name of this.namesToRun(item, excluded)) {
          if (!entry.names.includes(name)) {
            entry.names.push(name);
          }
        }
        filtered.set(file.id, entry);
      }
      this.enqueue(run, item, excluded);
    }
    // A `torb` without `--report json` runs whole files, each once, and its plain report is read instead
    const ranWhole = new Set();
    try {
      for (const [folder, files] of wholeFiles) {
        if (token.isCancellationRequested) {
          break;
        }
        await this.runBatch(run, token, native, folder, files, [], ranWhole);
      }
      for (const [, entry] of filtered) {
        if (token.isCancellationRequested) {
          break;
        }
        if (entry.names.length === 0 || ranWhole.has(entry.file.id)) {
          continue;
        }
        const folder = vscode.workspace.getWorkspaceFolder(entry.file.uri).uri.fsPath;
        await this.runBatch(run, token, native, folder, [entry.file], entry.names, ranWhole);
      }
    } finally {
      run.end();
    }
  }

  /**
   * One `torb test`: with `--report json` and the filters, unless this `torb` is known not to have the flag; then, or
   * when this run finds out that it has not, the whole files with the plain report of an older `torb`.
   */
  async runBatch(run, token, native, folder, files, names, ranWhole) {
    if (this.reportSupport() !== false) {
      const outcome = await this.execute(run, token, native, folder, files, names, true);
      if (!outcome.unsupported) {
        if (outcome.events > 0) {
          this.reportSupportByBinary.set(this.binaryKey(), true);
        }
        return;
      }
      this.markReportUnsupported();
    }
    const whole = files.filter((file) => !ranWhole.has(file.id));
    if (whole.length === 0) {
      return;
    }
    for (const file of whole) {
      ranWhole.add(file.id);
    }
    await this.execute(run, token, native, folder, whole, [], false);
  }

  /** What `torb test --report json` of this `torb` binary is known to do: true, false, or undefined before its first run. */
  reportSupport() {
    return this.reportSupportByBinary.get(this.binaryKey());
  }

  /** The executable and when it was written: a rebuilt `torb` of the same path and version is asked again. */
  binaryKey() {
    const executable = this.toolchain.command();
    let written = 0;
    try {
      written = this.fs.statSync(executable).mtimeMs;
    } catch (error) {
      // A bare name on PATH: the version tells it apart
    }
    return `${executable}|${this.toolchain.version || ''}|${written}`;
  }

  /** Remembers that this `torb` has no `--report json`, and says once what that costs and how to get a newer one. */
  markReportUnsupported() {
    const { vscode } = this;
    this.reportSupportByBinary.set(this.binaryKey(), false);
    this.log(`${this.toolchain.command()} has no \`torb test --report json\`: tests run as whole files, from its plain report`);
    const update = 'Update torb';
    const walkthrough = 'Show Me How';
    vscode.window
      .showInformationMessage(
        'This torb is older than the test integration: a single test or group runs its whole file, and there are no ' +
          'durations. Update it with torb upgrade for the full integration.',
        update,
        walkthrough
      )
      .then((answer) => {
        if (answer === update) {
          vscode.commands.executeCommand('torbscript.installToolchain');
        } else if (answer === walkthrough) {
          vscode.commands.executeCommand(
            'workbench.action.openWalkthrough',
            {
              category: 'torbscript.torbscript#torbscript.gettingStarted',
              step: `torbscript.torbscript#torbscript.gettingStarted#${process.platform === 'win32' ? 'installToolchainWindows' : 'installToolchainPosix'}`,
            },
            false
          );
        }
      });
  }

  allItems() {
    const items = [];
    this.controller.items.forEach((item) => items.push(item));
    return items;
  }

  fileOf(item) {
    let current = item;
    while (current) {
      const kind = kinds.get(current);
      if (kind && kind.type === 'file') {
        return current;
      }
      current = current.parent;
    }
    return undefined;
  }

  hasExcludedDescendant(item, excluded) {
    let found = false;
    item.children.forEach((child) => {
      if (excluded.has(child.id) || this.hasExcludedDescendant(child, excluded)) {
        found = true;
      }
    });
    return found;
  }

  /** The `--filter` arguments that run `item` without its excluded descendants. */
  namesToRun(item, excluded) {
    const kind = kinds.get(item);
    if (!this.hasExcludedDescendant(item, excluded)) {
      return kind.type === 'file' ? [] : [kind.fullName];
    }
    const names = [];
    item.children.forEach((child) => {
      if (!excluded.has(child.id)) {
        names.push(...this.namesToRun(child, excluded));
      }
    });
    return names;
  }

  enqueue(run, item, excluded) {
    if (excluded.has(item.id)) {
      return;
    }
    if (item.children.size === 0) {
      run.enqueued(item);
      return;
    }
    item.children.forEach((child) => this.enqueue(run, child, excluded));
  }

  /**
   * `torb test --report json [--native] [--filter <name>]... <file>...` in `folder`, with its report mapped onto the items:
   * a start, a pass, a failure with its message, its site and its duration. What else the process writes is the output
   * of the test that runs at the time. With `json` false it is `torb test [--native] <file>...`, and the plain report
   * (`  ok      <name>`, `  FAILED  <name>` and the indented lines of the failure) is read instead.
   *
   * Resolves to `{ unsupported: true }` where this `torb` does not know `--report` - it takes it for a path and answers
   * that the file does not exist - and nothing is reported then, so the caller can run the files again the old way.
   */
  execute(run, token, native, folder, files, names, json) {
    const { vscode, path } = this;
    const executable = this.toolchain.command();
    const args = json ? ['test', '--report', 'json'] : ['test'];
    if (native) {
      args.push('--native');
    }
    for (const name of json ? names : []) {
      args.push('--filter', name);
    }
    for (const file of files) {
      args.push(path.relative(folder, file.uri.fsPath) || file.uri.fsPath);
    }
    const byPath = new Map(files.map((file) => [normalized(path, file.uri.fsPath), file]));
    const state = { file: files.length === 1 ? files[0] : undefined, current: undefined, reported: new Set(), events: 0 };
    const plain = new PlainReport(this, run, state, byPath, folder);
    const heldBack = [];
    let command = `> ${executable} ${args.map(quoteForDisplay).join(' ')}\r\n`;
    // Until the first event, the output of a JSON run is held back: an older `torb`'s refusal is not shown, it is read
    const show = (text, item) => {
      if (json && state.events === 0) {
        heldBack.push([text, item]);
      } else {
        if (command) {
          run.appendOutput(command);
          command = '';
        }
        run.appendOutput(text, undefined, item);
      }
    };
    const flush = () => {
      if (command) {
        run.appendOutput(command);
        command = '';
      }
      for (const [text, item] of heldBack.splice(0)) {
        run.appendOutput(text, undefined, item);
      }
    };
    return new Promise((resolve) => {
      let child;
      try {
        child = this.cp.spawn(executable, args, { cwd: folder, windowsHide: true, env: { ...process.env, NO_COLOR: '1' } });
      } catch (error) {
        this.fail(run, files, `could not start ${executable}: ${error.message}`);
        resolve({ unsupported: false });
        return;
      }
      const cancel = token.onCancellationRequested(() => {
        try {
          child.kill();
        } catch (error) {
          // Already gone
        }
      });
      let pending = '';
      let tail = '';
      const onLine = (line) => {
        if (json && line.startsWith('{"event":')) {
          let event;
          try {
            event = JSON.parse(line);
          } catch (error) {
            event = undefined;
          }
          if (event) {
            state.events += 1;
            if (state.events === 1) {
              flush();
            }
            this.onEvent(run, event, state, byPath, folder);
            return;
          }
        }
        if (!json && plain.read(line)) {
          state.events += 1;
          return;
        }
        tail = (tail + line + '\n').slice(-4000);
        show(`${line}\r\n`, state.current);
      };
      child.stdout.on('data', (chunk) => {
        pending += chunk.toString('utf8');
        let newline = pending.indexOf('\n');
        while (newline >= 0) {
          onLine(pending.slice(0, newline).replace(/\r$/, ''));
          pending = pending.slice(newline + 1);
          newline = pending.indexOf('\n');
        }
      });
      child.stderr.on('data', (chunk) => {
        const text = chunk.toString('utf8');
        tail = (tail + text).slice(-4000);
        show(text.replace(/\r?\n/g, '\r\n'), state.current);
      });
      let failedToRun = false;
      child.on('error', (error) => {
        failedToRun = true;
        flush();
        this.fail(run, files, `could not run ${executable}: ${error.message}`);
      });
      child.on('close', (code) => {
        cancel.dispose();
        if (pending) {
          onLine(pending);
        }
        plain.finish();
        if (json && state.events === 0 && code !== 0 && isUnknownReportFlag(tail)) {
          resolve({ unsupported: true });
          return;
        }
        flush();
        this.log(
          `torb test${json ? ' --report json' : ''}${native ? ' --native' : ''} of ${files.length} file(s)` +
            `${names.length && json ? `, ${names.length} filter(s)` : ''}: exit ${code}, ${state.reported.size} test(s) reported`
        );
        if (failedToRun) {
          // Reported by `error` already
        } else if (state.events === 0 && code !== 0 && !token.isCancellationRequested) {
          // Nothing ran: the program did not check or did not build
          this.fail(run, files, tail.trim() || `torb test exited with ${code}`);
        } else if (state.current && !state.reported.has(state.current.id)) {
          // The process ended inside a test: a panic that escaped every recovery point, or the memory limit
          run.errored(state.current, new vscode.TestMessage(tail.trim() || `the test run ended with ${code}`));
        }
        resolve({ unsupported: false, events: state.events });
      });
    });
  }

  onEvent(run, event, state, byPath, folder) {
    const { vscode, path } = this;
    if (event.event === 'file') {
      const found = byPath.get(normalized(path, path.resolve(folder, String(event.path || ''))));
      state.file = found || (byPath.size === 1 ? byPath.values().next().value : this.fileItem(vscode.Uri.file(path.resolve(folder, String(event.path)))));
      return;
    }
    if (!state.file || (event.event !== 'start' && event.event !== 'test')) {
      return;
    }
    const groups = Array.isArray(event.groups) ? event.groups.map(String) : [];
    const item = this.itemFor(state.file, groups, String(event.name));
    if (event.event === 'start') {
      state.current = item;
      run.started(item);
      return;
    }
    state.reported.add(item.id);
    state.current = undefined;
    const duration = typeof event.duration === 'number' ? event.duration : undefined;
    if (event.outcome === 'passed') {
      run.passed(item, duration);
      return;
    }
    run.failed(item, this.messageOf(event, folder, state.file), duration);
  }

  /** The failure of a test as a message at its site - a comparison of two values as expected and actual where it is one. */
  messageOf(event, folder, file) {
    const { vscode } = this;
    const text = [String(event.message || 'the test failed'), event.frames ? String(event.frames) : '']
      .filter(Boolean)
      .join('\n\n');
    const compared = comparisonOf(String(event.message || ''));
    const message = compared ? vscode.TestMessage.diff(text, compared.expected, compared.actual) : new vscode.TestMessage(text);
    const at = event.location;
    const site = at && at.path && Number.isInteger(at.line) ? this.fileOfSite(String(at.path), folder, file) : undefined;
    if (site) {
      const line = Math.max(0, at.line - 1);
      const column = Math.max(0, (Number.isInteger(at.column) ? at.column : 1) - 1);
      message.location = new vscode.Location(vscode.Uri.file(site), new vscode.Position(line, column));
    }
    return message;
  }

  /**
   * The file a site of the report names. The runtime writes a site as the compiler names the file - the package's name
   * in front of the path inside the package (`acme/shop/tests/cart.test.trb`) - so the name is taken off one segment at
   * a time and the rest looked for below the package of the test file, then below the workspace folder. `undefined`
   * where no file is found, except that a site of the test file's own name is taken to be in it.
   */
  fileOfSite(site, folder, file) {
    const { path, fs } = this;
    const isFile = (candidate) => {
      try {
        return fs.statSync(candidate).isFile();
      } catch (error) {
        return false;
      }
    };
    if (path.isAbsolute(site) && isFile(site)) {
      return site;
    }
    const segments = site.split(/[\\/]/).filter(Boolean);
    const roots = [];
    const packageRoot = file ? this.packageRootOf(file.uri.fsPath, folder) : undefined;
    if (packageRoot) {
      roots.push(packageRoot);
    }
    roots.push(folder);
    for (let skipped = 0; skipped < segments.length; skipped += 1) {
      for (const root of roots) {
        const candidate = path.join(root, ...segments.slice(skipped));
        if (isFile(candidate)) {
          return candidate;
        }
      }
    }
    if (file && path.basename(site) === path.basename(file.uri.fsPath)) {
      return file.uri.fsPath;
    }
    return undefined;
  }

  /** The directory of the `project.trb` nearest above a file, up to the workspace folder. */
  packageRootOf(file, folder) {
    const { path, fs } = this;
    let directory = path.dirname(file);
    for (;;) {
      if (fs.existsSync(path.join(directory, 'project.trb'))) {
        return directory;
      }
      const parent = path.dirname(directory);
      if (parent === directory || path.relative(folder, directory) === '') {
        return undefined;
      }
      directory = parent;
    }
  }

  fail(run, files, text) {
    const message = new this.vscode.TestMessage(text);
    for (const file of files) {
      run.errored(file, message);
    }
  }
}

/**
 * The plain report of a `torb` without `--report json` (runtime/test.c), line by line: a file's line, `  ok      <full
 * name>`, `  FAILED  <full name>` with the failure's lines indented by ten spaces - the message, `at <file>:<line>:<column>`,
 * the frames - and the summary. A line it does not know is the output of a test.
 */
class PlainReport {
  constructor(tests, run, state, byPath, folder) {
    this.tests = tests;
    this.run = run;
    this.state = state;
    this.byPath = byPath;
    this.folder = folder;
    this.failing = null;
  }

  /** Whether the line was part of the report. */
  read(line) {
    const { path } = this.tests;
    const passed = /^ {2}ok {6}(.*)$/.exec(line);
    const failed = /^ {2}FAILED {2}(.*)$/.exec(line);
    if (passed || failed) {
      this.finish();
      if (!this.state.file) {
        return true;
      }
      const parts = (passed || failed)[1].split(' > ');
      const item = this.tests.itemFor(this.state.file, parts.slice(0, -1), parts[parts.length - 1]);
      this.state.reported.add(item.id);
      if (passed) {
        this.run.passed(item);
      } else {
        this.failing = { item, message: [], frames: [], location: undefined };
      }
      return true;
    }
    if (this.failing && line.startsWith(' '.repeat(10))) {
      const body = line.slice(10);
      const site = /^at (.+):(\d+):(\d+)$/.exec(body);
      if (site && !this.failing.location) {
        this.failing.location = { path: site[1], line: Number(site[2]), column: Number(site[3]) };
      } else if (this.failing.location) {
        this.failing.frames.push(body);
      } else {
        this.failing.message.push(body);
      }
      return true;
    }
    if (/^\d+ passed, \d+ failed \(/.test(line)) {
      this.finish();
      return true;
    }
    if (/^\S.*\.trb$/.test(line)) {
      this.finish();
      const found = this.byPath.get(normalized(path, path.resolve(this.folder, line)));
      if (found) {
        this.state.file = found;
        return true;
      }
      return this.byPath.size === 1;
    }
    this.finish();
    return line.trim() === '';
  }

  /** Reports the failure whose lines were being read. */
  finish() {
    if (!this.failing) {
      return;
    }
    const { item, message, frames, location } = this.failing;
    this.failing = null;
    const event = { message: message.join('\n'), frames: frames.join('\n'), location };
    this.run.failed(item, this.tests.messageOf(event, this.folder, this.state.file));
  }
}

/** Whether a `torb` answered as one that takes `--report` for a path does: `error: --report: The file does not exist`. */
function isUnknownReportFlag(output) {
  return /--report\b[^\n]*(does not exist|not found|unknown|no such)/i.test(output);
}

/** A path as a key: absolute, with one kind of separator, and without case on Windows. */
function normalized(path, file) {
  const resolved = path.resolve(file).replace(/\\/g, '/');
  return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

function quoteForDisplay(argument) {
  return /[\s"]/.test(argument) ? `"${argument.replace(/"/g, '\\"')}"` : argument;
}

/**
 * The failure of `assert(total == 3)` as actual `4` and expected `3`: the left side of a single `==` is what the test
 * computed, the right side what it expected - each the value a capture shows, or the source text where the side is a
 * literal. `assert` words it on several lines,
 *
 *     Assertion failed: total == 3
 *       total = the Int 4
 *
 * and std/expression's fallback on one (`Assertion failed: total == 3   (total = the Int 4)   at ...`); both are read.
 * `undefined` for any other message.
 */
function comparisonOf(message) {
  const text = message.trim();
  let source;
  let captures;
  const oneLine = /^Assertion failed: ([^\n]*?) {3}\(([^\n]*)\) {3}at \S+$/.exec(text);
  if (oneLine) {
    source = oneLine[1];
    captures = oneLine[2].split(/, (?=[A-Za-z_][A-Za-z0-9_.]* = )/);
  } else {
    const lines = text.split(/\r?\n/);
    const first = /^Assertion failed: (.*)$/.exec(lines[0]);
    if (!first) {
      return undefined;
    }
    source = first[1];
    captures = lines.slice(1).map((line) => line.trim()).filter((line) => line && !line.startsWith('at '));
  }
  const sides = source.split(' == ');
  if (sides.length !== 2) {
    return undefined;
  }
  const captured = new Map();
  for (const part of captures) {
    const equals = part.indexOf(' = ');
    if (equals > 0) {
      captured.set(part.slice(0, equals).trim(), part.slice(equals + 3).replace(/^the [A-Za-z0-9]+ /, ''));
    }
  }
  const valueOf = (side) => {
    const trimmed = side.trim();
    if (captured.has(trimmed)) {
      return captured.get(trimmed);
    }
    // A literal needs no capture; anything else that has none is not known
    return /^(-?[0-9][0-9_.]*|"[^"]*"|true|false|'.')$/.test(trimmed) ? trimmed : undefined;
  };
  const actual = valueOf(sides[0]);
  const expected = valueOf(sides[1]);
  return actual !== undefined && expected !== undefined ? { actual, expected } : undefined;
}

/** Starts the test explorer of the workspace. */
function startTests(context, vscode, cp, fs, path, toolchain, client, log) {
  return new TorbTests(context, vscode, cp, fs, path, toolchain, client, log);
}

module.exports = { startTests, comparisonOf, isUnknownReportFlag, PlainReport, TorbTests };
