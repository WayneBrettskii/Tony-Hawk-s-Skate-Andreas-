# Builds plugin-sdk's GTA San Andreas library (Plugin.lib) the official way:
# premake generates Visual Studio projects, then msbuild compiles plugin_sa.
# This mirrors plugin-sdk's own automated build (.github/workflows/Build_Plugin_GTA_SA.yml),
# which also runs on Visual Studio 2026.
#
# Run from "Developer PowerShell for VS 2026" (msbuild must be available):
#   powershell -ExecutionPolicy Bypass -File tools\build-plugin-sdk.ps1
#
# Everything it creates stays inside extern\plugin-sdk (gitignored).

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $projectDir 'extern\plugin-sdk'
$expectedCommit = '15f15b60bbf74c106e1b496ff92c98764abf4605'

# 1. Checks
if (-not (Test-Path (Join-Path $sdk 'tools\premake\premake5.exe'))) {
    Write-Host "plugin-sdk not found at $sdk. Download it first (see MODLOG, phase 0a-2a)." -ForegroundColor Red
    exit 1
}
$head = (Get-Content (Join-Path $sdk '.git\HEAD') -Raw).Trim()
if ($head -ne $expectedCommit) {
    Write-Host "Warning: plugin-sdk is at $head, expected $expectedCommit." -ForegroundColor Yellow
}
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    Write-Host "msbuild not found. Open 'Developer PowerShell for VS 2026' and run this again." -ForegroundColor Red
    exit 1
}

# plugin-sdk's project files look up this variable. Set for this script only.
$env:PLUGIN_SDK_DIR = $sdk

# 2. Generate Visual Studio projects
Write-Host "== Generating plugin-sdk projects (premake, vs2026) ==" -ForegroundColor Cyan
& (Join-Path $sdk 'tools\premake\premake5.exe') vs2026 "--file=$(Join-Path $sdk 'tools\premake\premake5.lua')"
if ($LASTEXITCODE -ne 0) { Write-Host "premake failed (exit code $LASTEXITCODE)." -ForegroundColor Red; exit 1 }

# 3. Build only the San Andreas library, Release configuration
Write-Host "== Building plugin_sa (Release) ==" -ForegroundColor Cyan
msbuild (Join-Path $sdk 'plugin.slnx') /property:Configuration=Release "/property:Platform=Mixed Platforms" /target:plugin_sa /m /verbosity:minimal
if ($LASTEXITCODE -ne 0) { Write-Host "msbuild failed (exit code $LASTEXITCODE). Copy all the output above and send it." -ForegroundColor Red; exit 1 }

# 4. Confirm the result
$lib = Join-Path $sdk 'output\lib\Plugin.lib'
if (Test-Path $lib) {
    $size = (Get-Item $lib).Length
    Write-Host "OK: built $lib ($size bytes)" -ForegroundColor Green
} else {
    Write-Host "Build reported success but $lib is missing. Contents of output\lib:" -ForegroundColor Red
    Get-ChildItem (Join-Path $sdk 'output\lib') -ErrorAction SilentlyContinue | Format-Table Name, Length
    exit 1
}
