# Builds the release package: release\SkateAndreas\ and release\SkateAndreas-<commit>.zip
#
# Run it from "Developer PowerShell for VS 2026", in the project folder:
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1 -Candidate   (uncommitted code: a test package)
#
# 8.44 (audit, 2026-10-15) - what it does now, in order:
#   1. Refuses to package code that is not committed (F6), unless -Candidate is given; a
#      candidate package says so in BUILD-INFO.txt and its zip name.
#   2. Builds from scratch in its own folder, build-release\ (F6), with nothing copied into
#      the GTA folder (N3) and no folder of this PC baked into the binaries (F1b). Runs the
#      tests (parser_tests, convert_test) and stops if one fails.
#   3. Checks the built files for user-profile paths (F1).
#   4. Downloads the embeddable Python and checks its SHA-256 against the value python.org
#      publishes BEFORE unpacking it (F3, F5). No cached copy is ever trusted unchecked.
#   5. Writes BUILD-INFO.txt (commit, clean or not, compiler, SDK commit, Python) and
#      SHA256SUMS.txt (every file in the package) into the package, then zips it.
#
# release\ and build-release\ are covered by the .gitignore whitelist, so none of this
# can be committed by accident. Nothing outside this project folder is written.

param([switch]$Candidate)

$ErrorActionPreference = "Stop"

$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj  = Split-Path -Parent $tools
$rel   = Join-Path $proj "release"
$pkg   = Join-Path $rel "SkateAndreas"
$bld   = Join-Path $proj "build-release"
$bin   = Join-Path $bld "Release"

# The embeddable Python (no installer, no PATH needed). Version and SHA-256 from the
# python.org release page (https://www.python.org/downloads/release/python-3148/,
# "Windows embeddable package (64-bit)", checked 2026-10-15). To move to a newer Python:
# change all three lines together, from that page, never from a downloaded file.
$pyVersion = "3.14.8"
$pyUrl     = "https://www.python.org/ftp/python/$pyVersion/python-$pyVersion-embed-amd64.zip"
$pySha256  = "a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310"

# Game folders the launcher suggests before the player picks theirs: common install
# places, not this PC's (F1b).
$defaultGta      = "C:/Program Files (x86)/Rockstar Games/GTA San Andreas"
$defaultRethawed = "C:/Games/reTHAWed"

# Native programs (git, cmake, the tests) write warnings to stderr; that must not stop the
# script, only their exit code may.
function Invoke-Native([string]$exe, [string[]]$argList) {
    # 2026-10-17: a program that does not exist left the previous program's exit code (0) behind,
    # so a missing cmake or test counted as success. Stop instead.
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        throw "not found: $exe (run this from Developer PowerShell for VS 2026, so cmake is on the PATH)"
    }
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & $exe @argList 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    return @{ Code = $code; Out = $out }
}

Write-Host "release: $rel"
Set-Location $proj

# ---- 1. the source: committed? ----
$r = Invoke-Native "git" @("-C", $proj, "rev-parse", "HEAD")
if ($r.Code -ne 0) { throw "git rev-parse failed: $($r.Out -join ' ')" }
$commit = ($r.Out | Select-Object -First 1).Trim()
$short  = $commit.Substring(0, 7)
$r = Invoke-Native "git" @("-C", $proj, "status", "--porcelain")
if ($r.Code -ne 0) { throw "git status failed: $($r.Out -join ' ')" }
$changes = @($r.Out | Where-Object { $_ -and $_.Trim() })
$clean = ($changes.Count -eq 0)
if (-not $clean -and -not $Candidate) {
    Write-Host "Uncommitted changes:" -ForegroundColor Red
    $changes | ForEach-Object { Write-Host "  $_" }
    throw "A release is built only from committed code. Commit first, or run with -Candidate for a test package."
}
$label = $short
if (-not $clean) { $label = "$short-candidate" }
$state = "clean"
if (-not $clean) { $state = "NOT clean: candidate package" }
Write-Host "  source: commit $commit ($state)"

$sdkDir = Join-Path $proj "extern\plugin-sdk"
$sdkCommit = "unknown"
$r = Invoke-Native "git" @("-C", $sdkDir, "rev-parse", "HEAD")
if ($r.Code -eq 0) { $sdkCommit = ($r.Out | Select-Object -First 1).Trim() }

# ---- 2. a fresh build, nothing deployed, no local folders baked in ----
if (Test-Path $bld) { Remove-Item $bld -Recurse -Force }   # build-owned folder only
$r = Invoke-Native "cmake" @("-S", $proj, "-B", $bld, "-A", "Win32", "-DGTA_DEPLOY_DIR=", "-DMP_LOG_DIR=",
                              "-DGTA_DIR=$defaultGta", "-DRETHAWED_DIR=$defaultRethawed")
$r.Out | ForEach-Object { Write-Host "    $_" }
if ($r.Code -ne 0) { throw "cmake configure failed" }
$compiler = $r.Out | Where-Object { $_ -match "CXX compiler identification is" } | Select-Object -First 1
if (-not $compiler) { $compiler = "unknown" } else { $compiler = $compiler.Trim() }

$r = Invoke-Native "cmake" @("--build", $bld, "--config", "Release", "--target",
                              "gta_plugin", "thaw_plugin", "launcher", "parser_tests", "convert_test")
$r.Out | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" }
if ($r.Code -ne 0) { throw "build failed (see above)" }

foreach ($t in @("parser_tests.exe", "convert_test.exe")) {
    $r = Invoke-Native (Join-Path $bin $t) @()
    if ($r.Code -ne 0) { $r.Out | ForEach-Object { Write-Host "    $_" }; throw "$t FAILED" }
    Write-Host "  tests: $t passed"
}

# ---- 3. no user-profile paths in what we ship ----
$binaries = @("SkateAndreas.asi", "SkateAndreas-thaw.dll", "SkateAndreas-launcher.exe")
foreach ($b in $binaries) {
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $bin $b))
    $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
    $wide  = [System.Text.Encoding]::Unicode.GetString($bytes)
    foreach ($text in @($ascii, $wide)) {
        if ($text -match '(?i)[a-z]:[\\/]users[\\/]') { throw "$b contains a user-profile path - not packaged" }
    }
    $dev = ([regex]::Matches($ascii + $wide, '(?i)[a-z]:[\\/]mods[\\/]')).Count
    if ($dev -gt 0) {
        Write-Host "  note: $b mentions the development folder $dev time(s) (plugin-sdk's own file names, from Plugin.lib)" -ForegroundColor Yellow
    }
}

# ---- 4. the package ----
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "tools") | Out-Null
$files = @(
    @{ from = "$bin\SkateAndreas.asi";            to = "SkateAndreas.asi" },
    @{ from = "$bin\SkateAndreas-thaw.dll";       to = "SkateAndreas-thaw.dll" },
    @{ from = "$bin\SkateAndreas-launcher.exe";   to = "SkateAndreas-launcher.exe" },
    @{ from = "$tools\gta_to_thaw_map.py";        to = "tools\gta_to_thaw_map.py" },
    @{ from = "$tools\gta_area_dump.py";          to = "tools\gta_area_dump.py" },
    @{ from = "$tools\gta_extras.py";             to = "tools\gta_extras.py" },
    @{ from = "$proj\INSTALL.md";                 to = "INSTALL.md" },
    @{ from = "$proj\README.md";                  to = "README.md" },
    @{ from = "$proj\LICENSE";                    to = "LICENSE" }
)
foreach ($f in $files) {
    if (-not (Test-Path $f.from)) { throw "missing: $($f.from)" }
    Copy-Item $f.from (Join-Path $pkg $f.to) -Force
    Write-Host "  + $($f.to)"
}

# The embeddable Python: downloaded into the build folder, checked, then unpacked.
$pyZip  = Join-Path $bld "python-$pyVersion-embed-amd64.zip"
$pyDest = Join-Path $pkg "tools\python"
Write-Host "  downloading Python $pyVersion (embeddable)..."
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $pyUrl -OutFile $pyZip -UseBasicParsing
$got = (Get-FileHash -Algorithm SHA256 $pyZip).Hash.ToLowerInvariant()
if ($got -ne $pySha256) {
    Remove-Item $pyZip -Force
    throw "Python download has the wrong SHA-256 ($got, expected $pySha256) - not used"
}
Write-Host "  Python SHA-256 matches python.org's ($pySha256)"
New-Item -ItemType Directory -Force -Path $pyDest | Out-Null
Expand-Archive -Path $pyZip -DestinationPath $pyDest -Force
if (-not (Test-Path (Join-Path $pyDest "LICENSE.txt"))) {
    throw "Python's LICENSE.txt is not in the embeddable package (the PSF licence must ship with it)"
}
$r = Invoke-Native (Join-Path $pyDest "python.exe") @((Join-Path $pkg "tools\gta_to_thaw_map.py"), "--help")
if ($r.Code -ne 0) { $r.Out | ForEach-Object { Write-Host "    $_" }; throw "the bundled Python could not run the map tool's --help" }
Write-Host "  the bundled Python runs the map tool (--help)"
Remove-Item (Join-Path $pkg "tools\__pycache__") -Recurse -Force -ErrorAction SilentlyContinue

# ---- 5. provenance + checksums, then the zip ----
$sourceLine = "clean (every file committed)"
if (-not $clean) { $sourceLine = "CANDIDATE - built from uncommitted changes, not a release" }
$info = @(
    "Tony Hawk's SkateAndreas - build information",
    "commit:   $commit",
    "source:   $sourceLine",
    "built:    $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC",
    "compiler: $compiler",
    "plugin-sdk commit: $sdkCommit",
    "python:   $pyVersion embeddable amd64, SHA-256 $pySha256",
    "",
    "SHA256SUMS.txt lists every file in this folder. Check one with PowerShell:",
    "  Get-FileHash -Algorithm SHA256 SkateAndreas.asi"
)
Set-Content -Path (Join-Path $pkg "BUILD-INFO.txt") -Value $info -Encoding UTF8

$sums = Get-ChildItem $pkg -Recurse -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | Sort-Object FullName | ForEach-Object {
    $relPath = $_.FullName.Substring($pkg.Length + 1)
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant(), $relPath
}
Set-Content -Path (Join-Path $pkg "SHA256SUMS.txt") -Value $sums -Encoding UTF8

# The shipped binaries must be exactly the ones just built.
foreach ($b in $binaries) {
    if ((Get-FileHash (Join-Path $bin $b)).Hash -ne (Get-FileHash (Join-Path $pkg $b)).Hash) { throw "$b in the package differs from the build" }
}

Get-ChildItem $rel -Filter "SkateAndreas*.zip*" -ErrorAction SilentlyContinue | Remove-Item -Force
$zip = Join-Path $rel "SkateAndreas-$label.zip"
Compress-Archive -Path $pkg -DestinationPath $zip -Force
$zipSha = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
Set-Content -Path "$zip.sha256" -Value "$zipSha  $(Split-Path $zip -Leaf)" -Encoding ASCII

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host ""
Write-Host "done: $zip ($mb MB)" -ForegroundColor Green
Write-Host "      SHA-256 $zipSha"
if (-not $clean) { Write-Host "      CANDIDATE package: built from uncommitted changes - for testing, not for GitHub" -ForegroundColor Yellow }
