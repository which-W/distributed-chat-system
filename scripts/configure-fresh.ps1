param(
    [ValidateSet('windows-server-release', 'desktop-release', 'desktop-local')]
    [string]$Preset = 'windows-server-release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    cmake --fresh --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build --preset $Preset
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
