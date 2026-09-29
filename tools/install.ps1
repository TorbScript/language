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
#   $env:TORB_INSTALL_CHANNEL    default stable; "preview" for the newest release candidate, beta or alpha (or the
#                                newest stable release where that is newer), "nightly" for the nightly channel
#   $env:TORB_INSTALL_VERSION    an exact version, instead of the newest of the channel
#   $env:TORB_INSTALL_TARGET     skips architecture detection, e.g. windows-arm64
#
# Windows PowerShell 5.1 or later. Runs entirely inside its own scope (the `& { ... }` below), because `iex` runs a
# script in the caller's own scope: no variable of this script, and no change of $ErrorActionPreference, leaks into
# the session that ran `irm ... | iex`, and this script never calls `exit`, which would close that session.
& {
  $ErrorActionPreference = "Stop"

  function Fail($message) {
    # Thrown, not written: caught once below and printed as one clean line, without the stack trace and the red
    # "+ CategoryInfo ..." noise `Write-Error` adds under $ErrorActionPreference = "Stop".
    throw $message
  }

  function Test-NumbersAbove($first, $second) {
    # Whether the numbers of release $first are above those of release $second - all SemVer's precedence needs to set
    # a pre-release against a stable release, since a release is newer than every pre-release of its numbers:
    # 0.2.0-rc.1 is newer than 0.1.0, and 0.1.0 than 0.1.0-rc.1
    [version](($first -split "-")[0]) -gt [version](($second -split "-")[0])
  }

  function Install-Torb {
    $baseUrl = if ($env:TORB_INSTALL_BASE_URL) { $env:TORB_INSTALL_BASE_URL } else { "https://torb.dev/download" }
    $requestedChannel = $env:TORB_INSTALL_CHANNEL
    $channel = if ($requestedChannel) { $requestedChannel } else { "stable" }
    $torbHome = Join-Path $env:LOCALAPPDATA "torb"
    $activeHome = Join-Path $env:LOCALAPPDATA "Programs\torb"

    # ----------------------------------------------------------------------------------------------------- the target

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

    # --------------------------------------------------------------------------------------------------- the version

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
        $lines = @(Get-Content $listingPath)
        # The newest release of a channel: its first line in versions.txt, which release-sync writes newest first
        $firstOf = {
          param($name)
          $found = $lines | Where-Object { ($_ -split "\s+")[1] -eq $name } | Select-Object -First 1
          if ($found) { ($found -split "\s+")[0] } else { $null }
        }
        $version = & $firstOf $channel
        if ($channel -eq "preview") {
          # The preview channel is never behind stable: once 0.1.0 is out it is 0.1.0, not 0.1.0-rc.1, until
          # 0.2.0-rc.1
          $stableVersion = & $firstOf "stable"
          if ($stableVersion -and (-not $version -or -not (Test-NumbersAbove $version $stableVersion))) {
            $version = $stableVersion
          }
        }
        if (-not $version -and -not $requestedChannel -and $channel -eq "stable") {
          # No stable release yet (docs/design/RELEASE.md section 5): the default channel falls back to the newest
          # preview, and without one to the newest nightly, and the toolchain follows that channel (torb upgrade
          # reads install.trb below). An explicitly requested channel or version never falls back.
          foreach ($fallback in @("preview", "nightly")) {
            $fallbackVersion = & $firstOf $fallback
            if ($fallbackVersion) {
              Write-Host "no stable release yet: installing the $fallback $fallbackVersion"
              $channel = $fallback
              $version = $fallbackVersion
              break
            }
          }
        }
        if (-not $version) {
          Fail "no version of the `"$channel`" channel is listed at $baseUrl/versions.txt"
        }
      }

      $archive = "torb-$version-$target.zip"
      Write-Host "installing TorbScript $version ($target)"

      # SHA256SUMS is fetched first and is what decides whether $target has an archive of $version at all: a
      # missing archive is a clear error naming the version and target, not a failed download that looks like a
      # network problem.
      $sumsPath = Join-Path $scratch "SHA256SUMS"
      try {
        Invoke-WebRequest -Uri "$baseUrl/$version/SHA256SUMS" -OutFile $sumsPath -UseBasicParsing
      } catch {
        Fail "could not download SHA256SUMS for $version"
      }
      $expected = Get-Content $sumsPath | ForEach-Object {
        $fields = $_ -split "\s+"
        if ($fields.Length -ge 2 -and ($fields[1].TrimStart("*")) -eq $archive) { $fields[0] }
      } | Select-Object -First 1
      if (-not $expected) {
        Fail "$version has no $target archive yet"
      }

      $archivePath = Join-Path $scratch $archive
      try {
        Invoke-WebRequest -Uri "$baseUrl/$version/$archive" -OutFile $archivePath -UseBasicParsing
      } catch {
        Fail "could not download $archive"
      }

      # ----------------------------------------------------------------------------------------------------- the hash

      $actual = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash
      if ($actual.ToLowerInvariant() -ne $expected.ToLowerInvariant()) {
        Fail "$archive`: expected sha256 $expected, got $actual"
      }
      Write-Host "sha256 verified"

      # --------------------------------------------------------------------------------------------------- unpacking

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
      # `torb upgrade` reads this to know how the toolchain got here (docs/design/RELEASE.md, "torb upgrade"):
      # fields are written with `=`, which is the format of this file and of nothing else in the repository.
      Set-Content -Path (Join-Path $torbHome "install.trb") -Value "channel = `"$channel`"`nmethod = `"script`""

      Write-Host ""
      Write-Host "TorbScript $version is installed in $toolchain"
      Write-Host "open a new terminal so the PATH change takes effect, then run: torb"
    } finally {
      Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
    }
  }

  try {
    Install-Torb
  } catch {
    Write-Host "install.ps1: $($_.Exception.Message)" -ForegroundColor Red
  }
}
