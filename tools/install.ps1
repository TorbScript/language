# Installs TorbScript for the current user (docs/design/RELEASE.md section 5), published at https://torb.dev/install.ps1:
#
#   irm https://torb.dev/install.ps1 | iex
#
# Detects the target, downloads its archive and SHA256SUMS, verifies the hash, and unpacks per user into
# %LOCALAPPDATA%\torb\toolchains\<version>\, with a copy at %LOCALAPPDATA%\Programs\torb the one in use - the layout
# `torb upgrade` manages (docs/design/RELEASE.md, "torb upgrade"). The user PATH is extended to find it.
#
# Overridable for testing, never needed otherwise:
#   $env:TORB_INSTALL_BASE_URL   default https://torb.dev/download
#   $env:TORB_INSTALL_CHANNEL    default stable ("nightly" for the nightly channel)
#   $env:TORB_INSTALL_VERSION    an exact version, instead of the newest of the channel
#   $env:TORB_INSTALL_TARGET     skips architecture detection, e.g. windows-arm64
#
# Windows PowerShell 5.1 or later.

$ErrorActionPreference = "Stop"

function Fail($message) {
  Write-Error "install.ps1: $message"
  exit 1
}

$baseUrl = if ($env:TORB_INSTALL_BASE_URL) { $env:TORB_INSTALL_BASE_URL } else { "https://torb.dev/download" }
$channel = if ($env:TORB_INSTALL_CHANNEL) { $env:TORB_INSTALL_CHANNEL } else { "stable" }
$torbHome = Join-Path $env:LOCALAPPDATA "torb"
$activeHome = Join-Path $env:LOCALAPPDATA "Programs\torb"

# ------------------------------------------------------------------------------------------------------- the target

$target = $env:TORB_INSTALL_TARGET
if (-not $target) {
  $arch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
  $word = switch ($arch) {
    "AMD64" { "x64" }
    "ARM64" { "arm64" }
    default { Fail "$arch is not a target architecture" }
  }
  $target = "windows-$word"
}

# ---------------------------------------------------------------------------------------------------- the version

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
  $version = $env:TORB_INSTALL_VERSION
  if (-not $version) {
    $listingPath = Join-Path $scratch "versions.txt"
    try {
      Invoke-WebRequest -Uri "$baseUrl/versions.txt" -OutFile $listingPath -UseBasicParsing
    } catch {
      Fail "could not reach $baseUrl/versions.txt"
    }
    $line = Get-Content $listingPath | Where-Object { ($_ -split "\s+")[1] -eq $channel } | Select-Object -First 1
    if (-not $line) {
      Fail "no version of the `"$channel`" channel is listed at $baseUrl/versions.txt"
    }
    $version = ($line -split "\s+")[0]
  }

  $archive = "torb-$version-$target.zip"
  Write-Host "installing TorbScript $version ($target)"
  $archivePath = Join-Path $scratch $archive
  $sumsPath = Join-Path $scratch "SHA256SUMS"
  try {
    Invoke-WebRequest -Uri "$baseUrl/$version/$archive" -OutFile $archivePath -UseBasicParsing
    Invoke-WebRequest -Uri "$baseUrl/$version/SHA256SUMS" -OutFile $sumsPath -UseBasicParsing
  } catch {
    Fail "could not download $archive or its SHA256SUMS"
  }

  # ---------------------------------------------------------------------------------------------------- the hash

  $expected = Get-Content $sumsPath | ForEach-Object {
    $fields = $_ -split "\s+"
    if ($fields.Length -ge 2 -and ($fields[1].TrimStart("*")) -eq $archive) { $fields[0] }
  } | Select-Object -First 1
  if (-not $expected) {
    Fail "$archive is not listed in SHA256SUMS"
  }
  $actual = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash
  if ($actual.ToLowerInvariant() -ne $expected.ToLowerInvariant()) {
    Fail "$archive`: expected sha256 $expected, got $actual"
  }
  Write-Host "sha256 verified"

  # ------------------------------------------------------------------------------------------------------- unpacking

  $extracted = Join-Path $scratch "extracted"
  Expand-Archive -Path $archivePath -DestinationPath $extracted -Force
  # The zip holds one top-level directory, torb-<version>-<target>/ (tools/package.sh), moved up one level
  $unpacked = Join-Path $extracted "torb-$version-$target"
  $toolchain = Join-Path $torbHome "toolchains\$version"
  if (Test-Path $toolchain) { Remove-Item -Recurse -Force $toolchain }
  New-Item -ItemType Directory -Force -Path (Join-Path $torbHome "toolchains") | Out-Null
  Move-Item -Path $unpacked -Destination $toolchain

  # The active copy: Windows has no free per-user symlink, so it is a copy `torb upgrade` replaces with a rename
  if (Test-Path $activeHome) { Remove-Item -Recurse -Force $activeHome }
  New-Item -ItemType Directory -Force -Path (Split-Path $activeHome) | Out-Null
  Copy-Item -Recurse -Path $toolchain -Destination $activeHome

  $activeBin = Join-Path $activeHome "bin"
  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $entries = @()
  if ($userPath) { $entries = $userPath -split ";" }
  if ($entries -notcontains $activeBin) {
    $newPath = if ($userPath) { "$userPath;$activeBin" } else { $activeBin }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "added $activeBin to your user PATH"
  }

  Set-Content -Path (Join-Path $torbHome "active") -Value $version -NoNewline
  # `torb upgrade` reads this to know how the toolchain got here (docs/design/RELEASE.md, "torb upgrade"): fields are
  # written with `=`, which is the format of this file and of nothing else in the repository.
  Set-Content -Path (Join-Path $torbHome "install.trb") -Value "channel = `"$channel`"`nmethod = `"script`""

  Write-Host ""
  Write-Host "TorbScript $version is installed in $toolchain"
  Write-Host "open a new terminal so the PATH change takes effect, then run: torb"
} finally {
  Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
