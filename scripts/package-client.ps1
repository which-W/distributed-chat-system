[CmdletBinding()]
param(
    [string]$BuildDir = 'build/desktop-release',
    [string]$ConfigFile = 'config/client.release.ini',
    [string]$IsccPath,
    [string]$VcRuntimeDir,
    [switch]$StageOnly
)

# Run from an x64 MSVC developer PowerShell after configuring the client in Release.
# No downloads, elevated installers, or server publishing are performed here.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $repoRoot $Path }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$buildPath = Resolve-RepoPath $BuildDir
$configPath = Resolve-RepoPath $ConfigFile
$settings = @{}
$section = ''
foreach ($line in Get-Content -LiteralPath $configPath) {
    $line = $line.Trim()
    if ($line -match '^\[(.+)\]$') { $section = $Matches[1]; continue }
    if ($line -match '^[#;]' -or $line.Length -eq 0) { continue }
    if ($line -match '^([^=]+)=(.*)$') {
        $key = "$section/$($Matches[1].Trim())"
        if ($settings.ContainsKey($key)) { throw "Duplicate configuration key: $key" }
        $settings[$key] = $Matches[2].Trim()
    }
}
if (-not $settings.ContainsKey('GateServer/BaseUrl')) { throw 'GateServer/BaseUrl is required.' }
$gateway = [Uri]$settings['GateServer/BaseUrl']
if (-not $gateway.IsAbsoluteUri -or $gateway.Scheme -ne 'https' -or
    $gateway.IsLoopback -or $gateway.Host -match '(^|\.)example\.(com|org|net)$' -or
    $gateway.UserInfo -or $gateway.Query -or $gateway.Fragment) {
    throw 'Release configuration requires a real HTTPS gateway, without credentials, query, or fragment.'
}
if (-not $settings.ContainsKey('Security/AllowInsecure') -or
    $settings['Security/AllowInsecure'] -ne 'false') {
    throw 'Release configuration must explicitly set Security/AllowInsecure=false.'
}

if (-not $StageOnly) {
    if (-not $IsccPath) {
        $isccCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($isccCommand) { $IsccPath = $isccCommand.Source }
        foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, $env:LOCALAPPDATA)) {
            if ($IsccPath -or -not $base) { continue }
            foreach ($suffix in @('Inno Setup 6/ISCC.exe', 'Programs/Inno Setup 6/ISCC.exe')) {
                $candidate = Join-Path $base $suffix
                if (Test-Path -LiteralPath $candidate) { $IsccPath = $candidate; break }
            }
        }
    }
    if (-not $IsccPath) {
        throw 'Inno Setup 6.3+ is required. Install it or pass -IsccPath. Use -StageOnly to validate staging.'
    }
    $IsccPath = Resolve-RepoPath $IsccPath
}

# Always build: never label an arbitrary pre-existing executable as a new release.
Invoke-Checked 'cmake' @('--build', $buildPath, '--config', 'Release', '--target', 'chat_client')
$manifestPath = Join-Path $buildPath 'client/package-Release.txt'
$metadata = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
    $parts = $line -split '=', 2
    if ($parts.Count -eq 2) { $metadata[$parts[0]] = $parts[1] }
}
foreach ($key in @('version', 'exe', 'ela', 'windeployqt')) {
    if (-not $metadata.ContainsKey($key)) { throw "Missing packaging metadata: $key" }
}
$version = $metadata['version']
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Unsupported release version: $version" }
if ((Get-Item -LiteralPath $metadata['exe']).VersionInfo.ProductVersion -ne $version) {
    throw 'Executable version does not match the configured project version.'
}

# Discover app-local MSVC runtime; Qt's shared libraries still need it even when
# chat_client itself uses /MT. No VC redistributable elevation is needed on Windows 10+.
if (-not $VcRuntimeDir) {
    $redistRoot = $env:VCToolsRedistDir
    if (-not $redistRoot) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -ne 0) { throw 'vswhere failed.' }
            if ($vsPath) {
                $redistRoot = Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC/Redist/MSVC') -Directory |
                    Where-Object Name -Match '^\d+\.\d+\.\d+$' |
                    Sort-Object { [version]$_.Name } -Descending |
                    Select-Object -First 1 -ExpandProperty FullName
            }
        }
    }
    if ($redistRoot) {
        $VcRuntimeDir = Get-ChildItem -LiteralPath (Join-Path $redistRoot 'x64') -Directory |
            Where-Object Name -Like 'Microsoft.VC*.CRT' |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $VcRuntimeDir) { throw 'Pass -VcRuntimeDir pointing to the MSVC x64 Microsoft.VC*.CRT directory.' }
$VcRuntimeDir = Resolve-RepoPath $VcRuntimeDir
foreach ($dll in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $VcRuntimeDir $dll))) { throw "Missing MSVC runtime: $dll" }
}

# A fresh staging folder avoids including stale DLLs, tests, screenshots, or secrets.
# Keep all artifacts under ignored build/. No recursive deletion is necessary.
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$outputPath = Join-Path $repoRoot "build/packages/$version/$runId"
$stagePath = Join-Path $outputPath 'app'
New-Item -ItemType Directory -Path $stagePath -Force | Out-Null
Copy-Item -LiteralPath $metadata['exe'], $metadata['ela'] -Destination $stagePath
Copy-Item -LiteralPath $configPath -Destination (Join-Path $stagePath 'config.ini')
Copy-Item -LiteralPath (Join-Path $repoRoot 'client/resources/style') -Destination $stagePath -Recurse
Invoke-Checked $metadata['windeployqt'] @('--release', '--no-translations', '--no-compiler-runtime', (Join-Path $stagePath 'chat_client.exe'))
Get-ChildItem -LiteralPath $VcRuntimeDir -Filter '*.dll' -File |
    Copy-Item -Destination $stagePath

$licenses = Join-Path $stagePath 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $licenses 'NebulaChat.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party/ElaWidgetTools/LICENSE') -Destination (Join-Path $licenses 'ElaWidgetTools.txt')
# Qt ships license texts in its documentation directory, with exact layout varying by SDK.
$qtRoot = Split-Path -Parent (Split-Path -Parent $metadata['windeployqt'])
$qtLicenses = @(Get-ChildItem -LiteralPath (Join-Path $qtRoot 'doc') -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^(LGPL|GPL|LICENSE)' -and $_.Extension -in @('.txt', '.html', '') })
if ($qtLicenses.Count -gt 0) {
    $qtLicenseDir = Join-Path $licenses 'Qt'
    New-Item -ItemType Directory -Path $qtLicenseDir | Out-Null
    foreach ($license in $qtLicenses) {
        $relative = $license.FullName.Substring((Join-Path $qtRoot 'doc').Length).TrimStart('\', '/')
        $destination = Join-Path $qtLicenseDir $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $license.FullName -Destination $destination
    }
} else {
    Write-Warning 'Qt license texts were not found in the SDK documentation; include the applicable notices before distribution.'
}

foreach ($required in @('chat_client.exe', 'ElaWidgetTools.dll', 'Qt6Core.dll', 'Qt6Network.dll',
    'Qt6Sql.dll', 'platforms/qwindows.dll', 'sqldrivers/qsqlite.dll', 'tls/qschannelbackend.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stagePath $required))) { throw "Incomplete package: $required" }
}
$fileList = Get-ChildItem -LiteralPath $stagePath -File -Recurse | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($stagePath.Length + 1).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
[ordered]@{ version = $version; gateway = $gateway.AbsoluteUri; files = @($fileList) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'package-manifest.json') -Encoding UTF8

if ($StageOnly) {
    Write-Output "Staged client: $stagePath"
    return
}
Invoke-Checked $IsccPath @("/DAppVersion=$version", "/DStageDir=$stagePath", "/DOutputPath=$outputPath",
    (Join-Path $repoRoot 'packaging/windows/NebulaChat.iss'))
$installer = Join-Path $outputPath "NebulaChat-$version-windows-x64-setup.exe"
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($installer))" |
    Set-Content -LiteralPath "$installer.sha256" -Encoding ASCII
Write-Output "Installer: $installer"
Write-Output "SHA256: $hash"
Write-Warning 'This installer is unsigned. A SHA-256 file checks integrity, not publisher identity.'
