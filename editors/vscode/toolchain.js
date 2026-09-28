// Finding `torb`, and installing it where it is not found.
//
// The extension needs one executable for everything it does - `torb lsp`, `torb highlight`, `torb test`, `torb run`,
// `torb new` - and a person who installed the extension from a store may not have it yet. This module answers where it
// is, asks it for its version (`torb --version`), keeps the context key `torbscript.toolchainFound` that the
// walkthrough's first step completes on, and shows a status bar item while it is missing.
//
// **Nothing is installed without a click.** Where `torb` is missing, the extension offers the official installer in a
// notification and in the walkthrough; the installer runs as a task in a terminal the person sees, with the one-liner
// the download page documents (`curl -fsSL https://torb.dev/install.sh | sh`, `irm https://torb.dev/install.ps1 | iex`).
// "TorbScript: Install or Update Toolchain" runs `torb upgrade` instead where a `torb` is found.

'use strict';

const INSTALL_SCRIPT_POSIX = 'https://torb.dev/install.sh';
const INSTALL_SCRIPT_WINDOWS = 'https://torb.dev/install.ps1';
const DOWNLOAD_PAGE = 'https://torb.dev/download';
const CONTEXT_KEY = 'torbscript.toolchainFound';

/** The name of the executable on this system. */
function executableName() {
  return process.platform === 'win32' ? 'torb.exe' : 'torb';
}

/**
 * Where an installer or a package manager puts `torb`, in the order they are tried after `PATH`. VS Code keeps the
 * `PATH` it was started with, so a toolchain installed while it runs is found here without a restart: the installers
 * write `~/.torb/bin/torb` and `%LOCALAPPDATA%\Programs\torb\bin\torb.exe` (tools/install.sh, tools/install.ps1).
 */
function installedLocations(path, os) {
  const home = os.homedir();
  if (process.platform === 'win32') {
    const local = process.env.LOCALAPPDATA || path.join(home, 'AppData', 'Local');
    return [
      path.join(local, 'Programs', 'torb', 'bin', 'torb.exe'),
      path.join(local, 'Microsoft', 'WinGet', 'Links', 'torb.exe'),
      path.join(home, 'scoop', 'shims', 'torb.exe'),
    ];
  }
  return [
    path.join(home, '.torb', 'bin', 'torb'),
    '/opt/homebrew/bin/torb',
    '/usr/local/bin/torb',
    '/home/linuxbrew/.linuxbrew/bin/torb',
  ];
}

function isFile(fs, candidate) {
  try {
    return fs.statSync(candidate).isFile();
  } catch (error) {
    return false;
  }
}

/** The first file of one of `names` (default: `torb`) in a directory of `PATH`, or `null`. */
function onPath(fs, path, names = process.platform === 'win32' ? ['torb.exe', 'torb.cmd', 'torb.bat'] : ['torb']) {
  const directories = (process.env.PATH || process.env.Path || '').split(path.delimiter).filter(Boolean);
  for (const directory of directories) {
    for (const name of names) {
      const candidate = path.join(directory, name);
      if (isFile(fs, candidate)) {
        return candidate;
      }
    }
  }
  return null;
}

/**
 * Where `torb` is, and how that was decided: `torbscript.executablePath` where it is set (and nothing else, so a
 * setting that points nowhere says so instead of quietly starting another `torb`), then `build/release/torb` below an
 * open workspace folder (what `sh tools/bootstrap.sh` writes in a checkout of the TorbScript repository), then `PATH`,
 * then the places the installers and package managers write. `executable` is `null` where none exists.
 */
function locate(vscode, fs, path, os) {
  const configured = (vscode.workspace.getConfiguration('torbscript').get('executablePath') || '').trim();
  if (configured) {
    // A bare command name is looked up on `PATH`; a path has to exist
    const bare = !configured.includes('/') && !configured.includes('\\');
    const names = process.platform === 'win32' && !configured.includes('.') ? [`${configured}.exe`, configured] : [configured];
    const executable = bare ? onPath(fs, path, names) : isFile(fs, configured) ? configured : null;
    return { executable, source: 'setting', configured };
  }
  for (const folder of vscode.workspace.workspaceFolders || []) {
    if (folder.uri.scheme !== 'file') {
      continue;
    }
    const candidate = path.join(folder.uri.fsPath, 'build', 'release', executableName());
    if (isFile(fs, candidate)) {
      return { executable: candidate, source: 'workspace' };
    }
  }
  const fromPath = onPath(fs, path);
  if (fromPath) {
    return { executable: fromPath, source: 'path' };
  }
  for (const candidate of installedLocations(path, os)) {
    if (isFile(fs, candidate)) {
      return { executable: candidate, source: 'installed' };
    }
  }
  return { executable: null, source: 'none' };
}

/** `torb --version`: resolves to the version it printed, `''` where it ran and printed none, `null` where it did not run. */
function versionOf(cp, executable) {
  return new Promise((resolve) => {
    let settled = false;
    const finish = (value) => {
      if (!settled) {
        settled = true;
        resolve(value);
      }
    };
    let child;
    try {
      child = cp.execFile(executable, ['--version'], { timeout: 10000, windowsHide: true }, (error, stdout) => {
        if (error && (error.code === 'ENOENT' || error.code === 'EACCES' || error.code === 'EPERM')) {
          finish(null);
          return;
        }
        const match = /torb\s+(\d+\.\d+\.\d+\S*)/.exec(String(stdout || ''));
        // An older `torb` without `--version` prints its usage and exits with 2: it is still a toolchain
        finish(match ? match[1] : '');
      });
    } catch (error) {
      finish(null);
      return;
    }
    child.on('error', () => finish(null));
  });
}

/** The state of the toolchain, found again whenever it may have changed, and everything the person does about it. */
class Toolchain {
  constructor(context, vscode, cp, fs, path, os, log) {
    this.context = context;
    this.vscode = vscode;
    this.cp = cp;
    this.fs = fs;
    this.path = path;
    this.os = os;
    this.log = log;
    this.executable = null;
    this.version = null;
    this.source = 'none';
    this.configured = undefined;
    this.checked = false;
    this.listeners = [];
    this.checking = null;
    this.polling = null;
    this.warned = false;
    this.statusItem = vscode.window.createStatusBarItem('torbscript.toolchain', vscode.StatusBarAlignment.Left, 0);
    this.statusItem.name = 'TorbScript Toolchain';
    this.statusItem.command = 'torbscript.openWalkthrough';
    context.subscriptions.push(this.statusItem, { dispose: () => this.stopPolling() });
  }

  /** Calls `listener(toolchain)` whenever the executable or its version changes. */
  onDidChange(listener) {
    this.listeners.push(listener);
  }

  /** The executable to start: the one found, or the bare name for `PATH` where none was found yet. */
  command() {
    return this.executable || (this.configured ? this.configured : 'torb');
  }

  /** Looks for `torb` again and asks it for its version. Concurrent calls share one look. */
  check() {
    if (!this.checking) {
      this.checking = this.look().finally(() => {
        this.checking = null;
      });
    }
    return this.checking;
  }

  async look() {
    const found = locate(this.vscode, this.fs, this.path, this.os);
    let version = null;
    let executable = found.executable;
    if (executable) {
      version = await versionOf(this.cp, executable);
      if (version === null) {
        executable = null;
      }
    }
    const changed = !this.checked || executable !== this.executable || version !== this.version;
    this.executable = executable;
    this.version = version;
    this.source = found.source;
    this.configured = found.configured;
    this.checked = true;
    await this.vscode.commands.executeCommand('setContext', CONTEXT_KEY, Boolean(executable));
    this.showStatus();
    if (changed) {
      this.log(
        executable
          ? `torb ${version || '(a version without --version)'}: ${executable}`
          : found.configured
            ? `torbscript.executablePath is "${found.configured}", and no torb is there`
            : 'no torb found: not in torbscript.executablePath, build/release, PATH, or where the installers put it'
      );
      if (executable) {
        this.stopPolling();
      }
      for (const listener of this.listeners) {
        try {
          listener(this);
        } catch (error) {
          this.log(`a listener of the toolchain failed: ${error.message}`);
        }
      }
    }
    return this;
  }

  isFound() {
    return Boolean(this.executable);
  }

  /** `torb 0.1.0 at <path>`, for a message. */
  describe() {
    return this.version ? `torb ${this.version} at ${this.executable}` : `torb at ${this.executable}`;
  }

  showStatus() {
    if (this.executable) {
      this.statusItem.hide();
      return;
    }
    this.statusItem.text = '$(warning) torb missing';
    this.statusItem.tooltip = 'The TorbScript toolchain was not found. Click to install it (Get Started with TorbScript).';
    this.statusItem.backgroundColor = new this.vscode.ThemeColor('statusBarItem.warningBackground');
    this.statusItem.show();
  }

  /** Once per window, where `torb` is missing: a notification that offers the installer and the walkthrough. */
  async offerInstall() {
    const { vscode } = this;
    if (this.executable || this.warned) {
      return;
    }
    if (!vscode.workspace.getConfiguration('torbscript').get('toolchain.offerInstall', true)) {
      return;
    }
    this.warned = true;
    const install = 'Install TorbScript';
    const setPath = 'Set Path';
    const walkthrough = 'Get Started';
    const message = this.configured
      ? `torbscript.executablePath is "${this.configured}", and there is no torb there.`
      : 'The TorbScript toolchain (torb) was not found. The language server, tests and "Run" need it.';
    const answer = await vscode.window.showWarningMessage(message, install, setPath, walkthrough);
    if (answer === install) {
      await this.install();
    } else if (answer === setPath) {
      await vscode.commands.executeCommand('workbench.action.openSettings', 'torbscript.executablePath');
    } else if (answer === walkthrough) {
      await vscode.commands.executeCommand('torbscript.openWalkthrough');
    }
  }

  /** "TorbScript: Install or Update Toolchain": `torb upgrade` where a `torb` is found, the installer otherwise. */
  async installOrUpdate() {
    await this.check();
    if (this.executable) {
      return this.runVisibly('Update Toolchain', this.executable, ['upgrade']);
    }
    return this.install();
  }

  /**
   * The official installer of this system, in a terminal the person sees: the one-liner of the download page, run by
   * PowerShell on Windows and by `sh` elsewhere. Afterwards the toolchain is looked for again - every two seconds while
   * the task runs, since a person may close its terminal before it ends.
   */
  async install() {
    if (process.platform === 'win32') {
      return this.runVisibly('Install Toolchain', 'powershell.exe', [
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-Command',
        `irm ${INSTALL_SCRIPT_WINDOWS} | iex`,
      ]);
    }
    const script =
      `if command -v curl >/dev/null 2>&1; then curl -fsSL ${INSTALL_SCRIPT_POSIX} | sh; ` +
      `else wget -qO- ${INSTALL_SCRIPT_POSIX} | sh; fi`;
    return this.runVisibly('Install Toolchain', '/bin/sh', ['-c', script]);
  }

  /**
   * Runs a command as a task, whose terminal shows the command line and keeps its output until it is closed - in the
   * first workspace folder, or in the home directory of an empty window. Where the window cannot run a task, it runs in
   * a terminal of its own instead. Either way the toolchain is looked for again while it runs and once it has ended.
   */
  async runVisibly(name, command, args) {
    const { vscode } = this;
    const folder = (vscode.workspace.workspaceFolders || [])[0];
    const execution = new vscode.ProcessExecution(command, args, { cwd: folder ? folder.uri.fsPath : this.os.homedir() });
    const task = new vscode.Task(
      { type: 'torbscript', task: name },
      folder || vscode.TaskScope.Workspace,
      name,
      'TorbScript',
      execution,
      []
    );
    task.presentationOptions = {
      reveal: vscode.TaskRevealKind.Always,
      focus: true,
      panel: vscode.TaskPanelKind.Dedicated,
      clear: true,
    };
    this.startPolling();
    let started;
    try {
      started = await vscode.tasks.executeTask(task);
    } catch (error) {
      this.log(`${name} could not run as a task (${error.message}); it runs in a terminal instead`);
      const terminal = vscode.window.createTerminal({ name: `TorbScript: ${name}`, shellPath: command, shellArgs: args });
      terminal.show();
      return undefined;
    }
    const ended = vscode.tasks.onDidEndTaskProcess((event) => {
      const same = event.execution === started || (event.execution.task.definition.type === 'torbscript' && event.execution.task.name === name);
      if (!same) {
        return;
      }
      ended.dispose();
      this.check().then(() => {
        if (event.exitCode === 0 && this.executable) {
          vscode.window.showInformationMessage(`TorbScript is ready: ${this.describe()}`);
        } else if (!this.executable) {
          vscode.window
            .showWarningMessage(`${name} ended with ${event.exitCode}, and no torb was found. The terminal says why.`, 'Open Download Page')
            .then((answer) => {
              if (answer) {
                vscode.env.openExternal(vscode.Uri.parse(DOWNLOAD_PAGE));
              }
            });
        }
      });
    });
    this.context.subscriptions.push(ended);
    return started;
  }

  /** Looks every two seconds, for at most fifteen minutes, until a `torb` appears. */
  startPolling() {
    this.stopPolling();
    const until = Date.now() + 15 * 60 * 1000;
    this.polling = setInterval(() => {
      if (Date.now() > until) {
        this.stopPolling();
        return;
      }
      const found = locate(this.vscode, this.fs, this.path, this.os);
      if (found.executable && found.executable !== this.executable) {
        this.check();
      }
    }, 2000);
  }

  stopPolling() {
    if (this.polling) {
      clearInterval(this.polling);
      this.polling = null;
    }
  }
}

/** Creates the toolchain, registers its commands, and looks for `torb` once. */
function createToolchain(context, vscode, cp, fs, path, os, log) {
  const toolchain = new Toolchain(context, vscode, cp, fs, path, os, log);
  context.subscriptions.push(
    vscode.commands.registerCommand('torbscript.installToolchain', () => toolchain.installOrUpdate()),
    vscode.commands.registerCommand('torbscript.installToolchainWithInstaller', () => toolchain.install()),
    vscode.commands.registerCommand('torbscript.checkToolchain', async () => {
      await toolchain.check();
      if (toolchain.isFound()) {
        vscode.window.showInformationMessage(`TorbScript: ${toolchain.describe()}`);
      } else {
        toolchain.warned = false;
        await toolchain.offerInstall();
      }
    }),
    vscode.commands.registerCommand('torbscript.openDownloadPage', () =>
      vscode.env.openExternal(vscode.Uri.parse(DOWNLOAD_PAGE))
    ),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration('torbscript.executablePath')) {
        toolchain.check();
      }
    }),
    vscode.workspace.onDidChangeWorkspaceFolders(() => toolchain.check()),
    // A toolchain installed or built outside of VS Code shows up when the window comes back
    vscode.window.onDidChangeWindowState((state) => {
      if (state.focused && !toolchain.isFound()) {
        toolchain.check();
      }
    })
  );
  return toolchain;
}

module.exports = { createToolchain, locate, versionOf, installedLocations, CONTEXT_KEY, DOWNLOAD_PAGE };
