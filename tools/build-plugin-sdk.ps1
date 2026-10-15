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
# 8.44 F3 (audit, 2026-10-15): ask git for the commit the SDK is really at (.git\HEAD usually
# names a branch, not a commit), and stop - not just warn - unless it is the expected one
# and its tracked files are unchanged. premake5.exe is run from this tree, so an altered or
# added program or script in it must never be run.
function Invoke-Git([string[]]$argList) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & git -C $sdk @argList 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    return @{ Code = $code; Out = $out }
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "git not found; it is needed to check plugin-sdk before building it." -ForegroundColor Red
    exit 1
}
$r = Invoke-Git @('rev-parse', 'HEAD')
$head = ($r.Out | Select-Object -First 1)
if ($r.Code -ne 0 -or -not $head) {
    Write-Host "Could not read plugin-sdk's commit (is extern\plugin-sdk a git clone?): $($r.Out -join ' ')" -ForegroundColor Red
    exit 1
}
$head = $head.Trim()
if ($head -ne $expectedCommit) {
    Write-Host "plugin-sdk is at $head, expected $expectedCommit. Not building." -ForegroundColor Red
    Write-Host "Fix: git -C extern\plugin-sdk checkout $expectedCommit" -ForegroundColor Red
    exit 1
}
$r = Invoke-Git @('status', '--porcelain', '--untracked-files=all')
if ($r.Code -ne 0) { Write-Host "git status failed in plugin-sdk: $($r.Out -join ' ')" -ForegroundColor Red; exit 1 }
$changed = @($r.Out | Where-Object { $_ -and $_.Trim() -and -not $_.StartsWith('??') })
$addedCode = @($r.Out | Where-Object { $_ -and $_.StartsWith('??') -and
                                       $_ -match '\.(exe|dll|bat|cmd|ps1|lua|js|py|vbs)$' })
if ($changed.Count -gt 0 -or $addedCode.Count -gt 0) {
    Write-Host "plugin-sdk's files are not as published - not building:" -ForegroundColor Red
    ($changed + $addedCode) | ForEach-Object { Write-Host "  $_" }
    Write-Host "Fix: git -C extern\plugin-sdk reset --hard $expectedCommit, and remove the extra files listed." -ForegroundColor Red
    exit 1
}
Write-Host "plugin-sdk: commit $head, tracked files unchanged - OK" -ForegroundColor Green
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
