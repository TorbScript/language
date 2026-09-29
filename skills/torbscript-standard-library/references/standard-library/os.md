---
title: std/os
summary: Environment, System and Directories - the environment a program was started with, which system and version it runs on, and where the user's files belong - with OsError and the three target constants.
kind: package
status: draft
order: 158
keywords:
  - std/os
  - Environment
  - EnvironmentVariables
  - System
  - SystemVersion
  - Directories
  - OsError
  - environment variable
  - host name
  - XDG
  - Known Folders
source:
  - std/os/src/lib.trb
  - docs/design/OS.md
---

> **Draft.** Slice 1 of docs/design/OS.md: the environment, the system's identity and the base directories. The C of
> the macOS and FreeBSD natives is compiled on those systems only and has not run there yet; processors, memory,
> volumes, network interfaces, the user and the current process are later slices.

`std/os` answers what a program asks about the machine it runs on: the environment it was started with, which
operating system and version it is, the host name and how long the machine has been running, and where the user's
configuration, data, cache and temporary files belong. It re-exports `OperatingSystem`, `Architecture` and `ByteOrder`
of `std/core`, so a file that branches on the system writes one import. It is deliberately not in the prelude:
`use System from "std/os"` at the top of a file is the statement "this file asks the operating system".

Every question is a `match OperatingSystem.current` in TorbScript whose arm hands it to the directory of one system.
Only the arm of the build's target is compiled, and every arm is type checked on every machine (see
Compile-time branches (skill `torbscript-language`: `references/language/execution/compile-time-branches.md`)).

## Import

```trb fragment
use Environment, EnvironmentVariables, System, SystemVersion, Directories, OsError from "std/os"
```

```trb check
use System, Directories, OsError from "std/os"
use Path from "std/path"

fn main(): Result<Void, OsError> {
  const version = System.version()?
  print "{version.name} ({version.kernel}) on {System.hostName()?}"
  const settings = Directories.configuration()?
  print settings.joined(Path.from("my-tool"))
  Ok void
}
```

## Declarations

### Environment

```trb fragment
public native type Environment {
  static fn get(name: String): String?
  static fn variables(): EnvironmentVariables
  static fn searchPath(): List<Path>
}
```

The environment this process was started with. `get(name)` answers `None` if the variable is not set, or if the caller
is not allowed to see it - the sandbox and a variable that does not exist look the same on purpose. On Windows a name
is looked up without regard to case, as the system does. `variables()` is every variable the caller may see, as a
value; `searchPath()` is `PATH` split at the system's separator (`;` on Windows, `:` elsewhere), each entry a `Path`,
the empty ones dropped.

There is no `Environment.set`: changing a variable of the running process is global mutable state, and on POSIX a
data race besides. What setting a variable is for is a child process, and a child is handed an `EnvironmentVariables`
value.

Inside a sandboxed script the module `std/os/environment` has to be granted, and only the variables a granted pattern
matches (`environment "APP_*"`) are visible to `get` and `variables`.

### EnvironmentVariables

```trb fragment
public type EnvironmentVariables with Show, Equals {
  static fn empty(): EnvironmentVariables

  fn get(name: String): String?
  fn names(): List<String>
  var fn set(name: String, value: String)
  var fn remove(name: String)
}
```

A set of environment variables as a value. Names compare the way the target compares them: without regard to case on
Windows, exactly everywhere else, so `set("PATH", …)` on Windows replaces an inherited `Path`. `show()` lists the
names only, because an environment carries secrets and a `print` of it must not.

```trb check
use EnvironmentVariables from "std/os"

var variables = EnvironmentVariables.empty()
variables.set "APP_MODE", "debug"
variables.set "NO_COLOR", "1"
variables.remove "NO_COLOR"
print variables
```

### System

```trb fragment
public type System {
  static fn version(): Result<SystemVersion, OsError>
  static fn hostName(): Result<String, OsError>
  static fn uptime(): Result<Duration, OsError>
  static fn pageSize(): Int
  static fn machineArchitecture(): Result<Architecture, OsError>
}
```

| | Windows | Linux | macOS | FreeBSD |
|---|---|---|---|---|
| `version` | registry `ProductName`, `DisplayVersion`, `UBR`; `RtlGetVersion` | `/etc/os-release`, `uname` | `kern.osproductversion`, `kern.osversion`, `kern.osrelease` | `uname` |
| `hostName` | `GetComputerNameExW` | `gethostname` | `gethostname` | `gethostname` |
| `uptime` | `GetTickCount64` | `/proc/uptime` | `kern.boottime` | `kern.boottime` |
| `pageSize` | `GetNativeSystemInfo` | `sysconf` | `sysconf` | `sysconf` |
| `machineArchitecture` | `IsWow64Process2` | `uname` | `uname`, `sysctl.proc_translated` | `uname` |

`hostName` never asks DNS. `uptime` includes the time the machine was asleep on every system. `pageSize` is a number
of bytes and never fails. In the browser, where the playground runs a program, `version`, `hostName` and `uptime` are
`Unsupported`, `pageSize` is WebAssembly's 65536, and `machineArchitecture` is `.Wasm64`. `machineArchitecture` is the machine and not always `Architecture.current`: an x86-64
program emulated on Arm64 Windows, or translated by Rosetta on a Mac, runs on `.Arm64`.

### SystemVersion

```trb fragment
public type SystemVersion with Show, Equals {
  name: String
  identifier: String
  release: String
  build: String?
  kernel: String

  fn kernelNumbers(): List<Int>
}
```

`name` is for people (`Windows 11 Pro`, `Ubuntu 24.04.1 LTS`, `macOS 15.1`, `FreeBSD 14.1-RELEASE`); `identifier` is
for programs, stable and lower case (`windows`, `ubuntu`, `macos`, `freebsd`); `release` is the product's own version
(`24H2`, `24.04`, `15.1`, `14.1`); `build` is there where the product has one (`26100.2314`, `24B83`); `kernel` is the
kernel's release, and `kernelNumbers()` its leading numbers for comparing (`[6, 8, 0]` of `6.8.0-45-generic`).

Windows 11 still writes `Windows 10` into the registry's `ProductName`, and the name says `Windows 11` from build
22000 on.

### Directories

```trb fragment
public type Directories {
  static fn home(): Result<Path, OsError>
  static fn configuration(): Result<Path, OsError>
  static fn data(): Result<Path, OsError>
  static fn state(): Result<Path, OsError>
  static fn cache(): Result<Path, OsError>
  static fn temporary(): Path
}
```

Where a user's files belong, by the convention of the system. Each answers the base, and an application joins its own
name to it: one join, the same on every system.

| | Linux, FreeBSD (XDG) | Windows (Known Folders) | macOS |
|---|---|---|---|
| `home` | `$HOME`, else the account entry | `FOLDERID_Profile` | `$HOME`, else the account entry |
| `configuration` | `$XDG_CONFIG_HOME`, else `~/.config` | `FOLDERID_RoamingAppData` | `~/Library/Application Support` |
| `data` | `$XDG_DATA_HOME`, else `~/.local/share` | `FOLDERID_RoamingAppData` | `~/Library/Application Support` |
| `state` | `$XDG_STATE_HOME`, else `~/.local/state` | `FOLDERID_LocalAppData` | `~/Library/Application Support` |
| `cache` | `$XDG_CACHE_HOME`, else `~/.cache` | `FOLDERID_LocalAppData` | `~/Library/Caches` |
| `temporary` | `$TMPDIR`, else `/tmp` | `GetTempPath2W` | `$TMPDIR`, else the user's own |

An XDG variable that holds a relative path is ignored, as the XDG specification requires. `temporary()` always
answers, because every system has a temporary directory and `/tmp` is the last resort. In the browser, whose file
system is in memory and belongs to no user, every directory but `temporary()` (`/tmp`) is `Unsupported`.

### OsError

```trb fragment
public type OsError with Show, Equals, Error {
  case Unsupported(question: String, operatingSystem: OperatingSystem)
  case Denied(question: String, reason: String)
  case Missing(question: String, reason: String)
  case Failed(question: String, message: String)
  case Malformed(source: String, detail: String)
}
```

What asking the operating system went wrong with. `Unsupported` is a question the system cannot answer at all;
`Denied` a refusal (a permission, a container that hides `/proc`); `Missing` a question without an answer on this
machine (no `HOME` and no account entry); `Failed` the system's own words for anything else; `Malformed` an answer in
a form its reader did not expect. No error code of a system is carried, because a portable caller that could read one
would compare it. A cancelled question is no case of it: `await()` passes a cancellation on to the waiting task
instead of answering it (see std/task (skill `torbscript-concurrency`: `references/standard-library/task.md`)).

## The per-system modules

Each directory of `std/os/src/` holds what one system, or one standard several share, has: `windows/`, `linux/`,
`macos/`, `freebsd/` and `browser/` (which has no natives), and `posix/`, `bsd/` (the `sysctl` interface of macOS and
FreeBSD) and `xdg/`. Their
`native.trb` modules are the raw natives of `runtime/os/`, and a program that needs one imports it directly
(`use Windows from "std/os/windows/native"`) and reaches it from the arm of its system; `torb check --every-target`
reports a native that is reached on a system it does not exist on. They are not the documented surface of the package.

## Related

- [std/core](core.md) - `OperatingSystem`, `Architecture` and `ByteOrder`, the constants a branch on the target reads.
- Compile-time branches (skill `torbscript-language`: `references/language/execution/compile-time-branches.md`) - the rule that keeps one arm of a `match` on
  the target.
- [std/sandbox](sandbox.md) - `SandboxCapabilities.environment`, which grants the patterns a script may read.
- [std/path](path.md) - `Path`, what `Directories` and `Environment.searchPath` answer.
- [The standard library](index.md) - the other packages.

