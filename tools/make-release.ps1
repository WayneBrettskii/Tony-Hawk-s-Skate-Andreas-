# Builds the release package: release\SkateAndreas\ and release\SkateAndreas.zip
#
# Run it from anywhere:
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1
#
# It copies the built binaries (so build first: cmake --build build --config Release), the map
# tools, the docs and the licence, and adds a small **embeddable Python** under tools\python\ so
# a player needs nothing installed to build the map (DESIGN 8.40).
#
# release\ is covered by the .gitignore whitelist, so none of this can be committed by accident.

$ErrorActionPreference = "Stop"

$tools  = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj   = Split-Path -Parent $tools
$rel    = Join-Path $proj "release"
$pkg    = Join-Path $rel "SkateAndreas"
$bin    = Join-Path $proj "build\Release"

# The embeddable Python (no installer, no PATH needed). win32 *and* amd64 both run on 64-bit
# Windows; amd64 is used because building the whole map is memory hungry. The launcher falls
# back to a system Python if this one cannot run (see FindPython in src/launcher/main.cpp).
$pyVersion = "3.11.9"
$pyUrl     = "https://www.python.org/ftp/python/$pyVersion/python-$pyVersion-embed-amd64.zip"
$pyDest    = Join-Path $pkg "tools\python"

Write-Host "release: $rel"
Remove-Item $pkg -Recurse -Force -ErrorAction SilentlyContinue
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
    if (-not (Test-Path $f.from)) { throw "missing: $($f.from) - build first (cmake --build build --config Release)" }
    Copy-Item $f.from (Join-Path $pkg $f.to) -Force
    Write-Host "  + $($f.to)"
}

# The embeddable Python.
$pyZip = Join-Path $env:TEMP "python-$pyVersion-embed-amd64.zip"
if (-not (Test-Path $pyZip)) {
    Write-Host "  downloading Python $pyVersion (embeddable, about 11 MB)..."
    Invoke-WebRequest -Uri $pyUrl -OutFile $pyZip -UseBasicParsing
}
New-Item -ItemType Directory -Force -Path $pyDest | Out-Null
Expand-Archive -Path $pyZip -DestinationPath $pyDest -Force
foreach ($f in Get-ChildItem $pyDest -File) { Write-Host "  + tools\python\$($f.Name)" }

# The embedded runtime keeps its own licence next to it - required by the PSF licence.
if (-not (Test-Path (Join-Path $pyDest "LICENSE.txt"))) {
    Write-Warning "Python's LICENSE.txt is not in the embeddable package; add it by hand"
}

$zip = Join-Path $rel "SkateAndreas.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $pkg -DestinationPath $zip -Force

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host ""
Write-Host "done: $zip ($mb MB)"
Write-Host "check that the bundled Python runs the map tool:"
Write-Host "  `"$pyDest\python.exe`" `"$pkg\tools\gta_to_thaw_map.py`" --help"
