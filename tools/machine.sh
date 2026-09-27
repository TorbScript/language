# What the machine has, for the scripts that decide how much to run at once: `tools/gates.sh`, `tools/conformance.sh`
# and `tools/repl.sh` read it with `. "$(dirname -- "$0")/machine.sh"`. It defines functions and runs nothing.
#
#   machine_processors        the processors online: getconf, nproc, `$NUMBER_OF_PROCESSORS` (Windows); at least 1
#   machine_memory            the physical memory in MiB, or nothing where it cannot be read: `/proc/meminfo` (Linux,
#                             and Git Bash, which reads it from Windows), `sysctl` on macOS and the BSDs
#   machine_jobs <MiB>        how many processes of about that size at once: one per processor, no more than the memory
#                             holds, at least 1
#
# POSIX sh. Runs in Git Bash on Windows and on Linux/macOS.

machine_processors() {
  machine_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || printf '%s\n' "${NUMBER_OF_PROCESSORS:-1}")
  case "$machine_count" in
    '' | *[!0-9]* | 0) machine_count=1 ;;
  esac
  printf '%s\n' "$machine_count"
}

machine_memory() {
  if [ -r /proc/meminfo ]; then
    awk '/^MemTotal:/ { print int($2 / 1024); exit }' /proc/meminfo
    return 0
  fi
  machine_bytes=$(sysctl -n hw.memsize 2>/dev/null || sysctl -n hw.physmem 2>/dev/null || true)
  case "$machine_bytes" in
    '' | *[!0-9]*) ;;
    *) printf '%s\n' "$((machine_bytes / 1048576))" ;;
  esac
  return 0
}

machine_jobs() {
  machine_result=$(machine_processors)
  machine_total=$(machine_memory)
  case "$machine_total" in
    '' | *[!0-9]*) ;;
    *)
      if [ $((machine_total / $1)) -lt "$machine_result" ]; then
        machine_result=$((machine_total / $1))
      fi
      ;;
  esac
  if [ "$machine_result" -lt 1 ]; then
    machine_result=1
  fi
  printf '%s\n' "$machine_result"
}
