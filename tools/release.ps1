# Publishes a new version: bumps src\Version.h, builds, commits, tags and creates a GitHub release
# with bin\LmuOverlay.exe attached. Everyone using the overlay then sees "update available".
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 1.0.1 -Notes "What changed"
#
# Needs: git, GitHub CLI logged in once with "gh auth login".
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$Notes = ""
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Native tools write progress to stderr: don't let PowerShell treat that as an error, check exit codes instead.
function Run([string]$what, [scriptblock]$cmd) {
  $ErrorActionPreference = 'Continue'
  & $cmd 2>&1 | ForEach-Object { "$_" }
  if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}

$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) { $gh = Join-Path $env:LOCALAPPDATA 'Programs\gh\bin\gh.exe' }
if (-not (Test-Path $gh)) { throw "GitHub CLI not found" }

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.3" }
if (Get-Process LmuOverlay -ErrorAction SilentlyContinue) { throw "Close the overlay first (Ctrl+Alt+Q): the exe is in use." }
if (git tag --list "v$Version") { throw "v$Version already exists" }

# 1. Version number compiled into the exe.
$versionFile = Join-Path $root 'src\Version.h'
$text = Get-Content $versionFile -Raw
$text = $text -replace '#define LMU_OVERLAY_VERSION "[^"]*"', "#define LMU_OVERLAY_VERSION `"$Version`""
[IO.File]::WriteAllText($versionFile, $text)

# 2. Build.
Run "Build" { cmd /c "`"$root\build.bat`"" }
$exe = Join-Path $root 'bin\LmuOverlay.exe'

# 3. Commit + tag + push.
if ($Notes -eq "") { $Notes = "LMU Overlay v$Version" }
Run "git add" { git add -A }
Run "git commit" { git commit -m "Release v$Version" -m $Notes }
Run "git tag" { git tag "v$Version" }
Run "git push" { git push -u origin HEAD }
Run "git push tag" { git push origin "v$Version" }

# 4. GitHub release with the exe.
Run "gh release" { & $gh release create "v$Version" "$exe#LmuOverlay.exe" --title "v$Version" --notes $Notes }
Write-Host "Published v$Version"
