/*
 * torb_os.h - the natives of `std/os`, one family per file of `runtime/os/` (docs/design/OS.md section 7). Included from
 * torb.h; nothing includes it directly.
 *
 * Every prototype is declared on every machine, whichever system the file that defines it is compiled for. A function
 * of `runtime/os/windows.c` exists only in a Windows build, but its signature is compared with the manifest's
 * (`torb_natives.h`, by `tests/natives_header_test.c`) on Linux as well, so the two halves cannot drift apart unseen.
 *
 * A call of one of these on a system it does not exist on never reaches the C compiler: the manifest names the systems
 * of every row (`availableOn`), and the compiler reports the call as an error of the program for that target
 * (`torb check --every-target` on any machine).
 *
 * The names are `torb_os_<family>_<name>`: the family is the file, the name the member of its `native type` in
 * `std/os`.
 *
 * **One shape for everything that can fail** (docs/design/OS.md section 4), because a function of the runtime may not
 * build a type of the program: the answer is an outcome, one of the five below, and the payload is written through the
 * pointers - every `torb_text *` and `torb_list *` is a `var` parameter of the declaration, whose old value the callee
 * releases before it writes a new one. `failure` says why, in the system's own words, wherever the outcome is not
 * `TORB_OS_SUCCESS`. A `torb_text` handed by value is borrowed.
 */

#ifndef TORB_OS_H
#define TORB_OS_H

#include <stdint.h>

/** The outcomes `outcome` of `std/os/src/error.trb` turns into a `Result`. */
enum {
  TORB_OS_SUCCESS = 0,
  TORB_OS_FAILED = 1,
  TORB_OS_DENIED = 2,
  TORB_OS_MISSING = 3,
  TORB_OS_UNSUPPORTED = 4
};

/* ---------------------------------------------------------------------------------------- windows.c, on Windows --- */

/** Milliseconds since the system started, from `GetTickCount64`: what `System.uptime` is made of. */
int64_t torb_os_windows_tick_count(void);

/** The kernel's version from `RtlGetVersion`: `10`, `0`, `26100`. */
int64_t torb_os_windows_version(int64_t *major, int64_t *minor, int64_t *build, torb_text *failure);

/** A `REG_SZ` or `REG_EXPAND_SZ` value of a key below `HKEY_LOCAL_MACHINE`, unexpanded. */
int64_t torb_os_windows_registry_text(torb_text key, torb_text value, torb_text *text, torb_text *failure);

/** A `REG_DWORD` or `REG_QWORD` value of a key below `HKEY_LOCAL_MACHINE`. */
int64_t torb_os_windows_registry_integer(torb_text key, torb_text value, int64_t *number, torb_text *failure);

/** `GetComputerNameExW(ComputerNameDnsHostname)`. */
int64_t torb_os_windows_computer_name(torb_text *name, torb_text *failure);

/**
 * `GetNativeSystemInfo`, and `IsWow64Process2` for the machine under an emulated program. The architecture is `1`
 * x86-64, `2` Arm64, `3` 32-bit x86, `4` 32-bit Arm and `0` anything else.
 */
int64_t torb_os_windows_system_information(
  int64_t *page_size,
  int64_t *architecture,
  int64_t *logical,
  torb_text *failure
);

/** `SHGetKnownFolderPath` of the `FOLDERID_` written as text: `{5E6C858F-0E22-4760-9AFE-EA3317B67173}`. */
int64_t torb_os_windows_known_folder(torb_text identifier, torb_text *path, torb_text *failure);

/** `GetTempPath2W`, and `GetTempPathW` where Windows is older than 11. */
int64_t torb_os_windows_temporary_directory(torb_text *path, torb_text *failure);

/* ------------------------------------------------------------------ posix.c, on Linux, macOS and FreeBSD --- */

/** The effective user id of the process, from `geteuid`. */
int64_t torb_os_posix_effective_user_identifier(void);

/** The five fields of `uname`. */
int64_t torb_os_posix_system_names(
  torb_text *system,
  torb_text *node,
  torb_text *release,
  torb_text *version,
  torb_text *machine,
  torb_text *failure
);

/** `gethostname`. */
int64_t torb_os_posix_host_name(torb_text *name, torb_text *failure);

/** `sysconf` of a name without its `_SC_`: `PAGESIZE`, `NPROCESSORS_ONLN`, `CLK_TCK`. `-1` for an unknown name. */
int64_t torb_os_posix_configuration(torb_text name);

/** The account entry of the real user, from `getuid` and `getpwuid_r`. */
int64_t torb_os_posix_account(
  int64_t *identifier,
  torb_text *name,
  torb_text *full_name,
  torb_text *home,
  torb_text *failure
);

/* ------------------------------------------------------------------------------------- linux.c, on Linux --- */

/** A file below `/proc/` or `/sys/`, or `os-release`, read to its end. Every other path is `TORB_OS_DENIED`. */
int64_t torb_os_linux_read_system_file(torb_text path, torb_text *text, torb_text *failure);

/* --------------------------------------------------------------------------- bsd.c, on macOS and FreeBSD --- */

/** A text `sysctl`, by name. */
int64_t torb_os_bsd_sysctl_text(torb_text name, torb_text *text, torb_text *failure);

/** An integer `sysctl` of any width, by name, widened. */
int64_t torb_os_bsd_sysctl_integer(torb_text name, int64_t *number, torb_text *failure);

/** Milliseconds since `kern.boottime`, against the wall clock. */
int64_t torb_os_bsd_uptime(int64_t *milliseconds, torb_text *failure);

/* ------------------------------------------------------------------------------------- macos.c, on macOS --- */

/** `confstr(_CS_DARWIN_USER_TEMP_DIR)`. */
int64_t torb_os_macos_user_temporary_directory(torb_text *path, torb_text *failure);

#endif /* TORB_OS_H */
