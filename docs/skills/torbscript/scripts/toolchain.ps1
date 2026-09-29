<#
.SYNOPSIS
Finds the TorbScript toolchain, and installs it when asked to. Part of the torbscript Agent Skill.

.DESCRIPTION
Prints one "key: value" line per fact:

  torb        the path of the torb in use, or "missing"
  version     what `torb --version` answers
  on-path     "yes", or how to call it when the PATH of this process does not have it
  c-compiler  the C compiler `torb build` and `--native` would use, or "none"

Without -Install it changes nothing. With -Install it runs the official installer first when torb is missing or does
not run, and does nothing when it runs. Ask the user before using -Install: it downloads https://torb.dev/install.ps1
and installs the toolchain for the current user into %LOCALAPPDATA%\torb, with the one in use in
%LOCALAPPDATA%\Programs\torb, whose bin directory it adds to the user's PATH.

Exit status: 0 torb runs, 1 it is missing or does not run or a parameter is unknown, 3 the installer failed. The
installer's own output comes first. Its variables, such as $env:TORB_INSTALL_CHANNEL, pass through; $env:TORB_INSTALLER_URL names another
installer, for a test. Windows PowerShell 5.1 or later.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File toolchain.ps1

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File toolchain.ps1 -Install
#>
[CmdletBinding()]
param([switch]$Install)

# The torb in use: the one on the PATH, else where install.ps1 puts it
function Find-Torb {
  $command = Get-Command torb -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($command) {
    return @{ Path = $command.Source; OnPath = $true }
  }
  if ($env:LOCALAPPDATA) {
    $candidate = Join-Path $env:LOCALAPPDATA "Programs\torb\bin\torb.exe"
    if (Test-Path $candidate) {
      return @{ Path = $candidate; OnPath = $false }
    }
  }
  return $null
}

# What `torb --version` answers without the leading "torb", or $null when it does not run
function Get-TorbVersion($found) {
  if (-not $found) {
    return $null
  }
  $env:NO_COLOR = "1"
  try {
    $answer = & $found.Path --version 2>$null | Select-Object -First 1
    if ($LASTEXITCODE -eq 0 -and $answer) {
      return ($answer -replace '^torb\s+', '').Trim()
    }
  } catch {
  }
  return $null
}

# The compiler `torb build` looks for: $env:TORB_CC, else the first of clang, gcc and cc on the PATH
function Find-CCompiler {
  if ($env:TORB_CC) {
    return "$($env:TORB_CC) (from TORB_CC)"
  }
  foreach ($name in @("clang", "gcc", "cc")) {
    if (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue) {
      return $name
    }
  }
  return "none - torb run, check, test and format work; torb build and --native need one"
}

function Write-Report($found, $version, $compiler) {
  if (-not $found) {
    Write-Output "torb: missing"
    if (-not $Install) {
      Write-Output "install: once the user agrees, run this script with -Install"
    }
  } else {
    Write-Output "torb: $($found.Path)"
    if ($version) {
      Write-Output "version: $version"
    } else {
      Write-Output "version: none - it is there but does not run"
    }
    if ($found.OnPath) {
      Write-Output "on-path: yes"
    } else {
      $directory = Split-Path $found.Path
      Write-Output "on-path: no - call it as $($found.Path), or put $directory on the PATH"
    }
  }
  Write-Output "c-compiler: $compiler"
}

# Downloads the official installer into a file of its own and runs it in a PowerShell of its own
function Invoke-Installer {
  $url = if ($env:TORB_INSTALLER_URL) { $env:TORB_INSTALLER_URL } else { "https://torb.dev/install.ps1" }
  $script = Join-Path ([System.IO.Path]::GetTempPath()) ("torb-install-" + [System.IO.Path]::GetRandomFileName() + ".ps1")
  Write-Host "toolchain.ps1: running $url"
  try {
    Invoke-WebRequest -Uri $url -OutFile $script -UseBasicParsing
  } catch {
    Write-Host "toolchain.ps1: could not download $url - $($_.Exception.Message)"
    return $false
  }
  try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script | Out-Host
    return ($LASTEXITCODE -eq 0)
  } finally {
    Remove-Item -Force $script -ErrorAction SilentlyContinue
  }
}

$found = Find-Torb
$version = Get-TorbVersion $found
$compiler = Find-CCompiler

if ($Install) {
  if ($version) {
    Write-Report $found $version $compiler
    Write-Output "install: nothing to do, torb runs"
    exit 0
  }
  if (-not (Invoke-Installer)) {
    Write-Report $found $version $compiler
    Write-Output "install: failed - the installer's messages are above"
    exit 3
  }
  $found = Find-Torb
  $version = Get-TorbVersion $found
}

Write-Report $found $version $compiler
if (-not $version) {
  exit 1
}
exit 0
