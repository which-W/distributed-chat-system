param(
    [string]$BuildDir = "$(Get-Location)\build\windows-server-release"
)

$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $BuildDir 'bin'
$runDir = Join-Path $BuildDir 'run'
$logDir = Join-Path $BuildDir 'logs'
New-Item -ItemType Directory -Force -Path $runDir, $logDir | Out-Null

$envFile = Join-Path $projectDir '.env'
if (Test-Path $envFile) {
    foreach ($line in Get-Content -LiteralPath $envFile) {
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)\s*$') {
            $value = $Matches[2].Trim().Trim('"').Trim("'")
            [Environment]::SetEnvironmentVariable($Matches[1], $value, 'Process')
        }
    }
}

function Start-ChatProcess {
    param(
        [string]$Name,
        [string]$Executable,
        [string]$ConfigFile,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $binDir
    )

    $pidFile = Join-Path $runDir "$Name.pid"
    if (Test-Path $pidFile) {
        $existingPid = [int](Get-Content $pidFile)
        if (Get-Process -Id $existingPid -ErrorAction SilentlyContinue) {
            Write-Host "$Name is already running (pid $existingPid)"
            return
        }
    }
    if (-not (Test-Path $Executable)) {
        throw "Missing executable: $Executable"
    }

    $previousConfig = $env:CHAT_CONFIG_FILE
    if ($ConfigFile) {
        $env:CHAT_CONFIG_FILE = $ConfigFile
    }
    $startParams = @{
        FilePath = $Executable
        WorkingDirectory = $WorkingDirectory
        RedirectStandardOutput = Join-Path $logDir "$Name.stdout.log"
        RedirectStandardError = Join-Path $logDir "$Name.stderr.log"
        WindowStyle = 'Hidden'
        PassThru = $true
    }
    if ($Arguments.Count -gt 0) {
        $startParams.ArgumentList = $Arguments
    }
    $process = Start-Process @startParams
    $env:CHAT_CONFIG_FILE = $previousConfig
    Set-Content -LiteralPath $pidFile -Value $process.Id
    Write-Host "Started $Name (pid $($process.Id))"
}

$node = (Get-Command node -ErrorAction Stop).Source
if (-not (Test-Path (Join-Path $projectDir 'VarifyServer\node_modules'))) {
    throw 'VarifyServer dependencies are missing; run: npm ci --prefix VarifyServer'
}
Start-ChatProcess -Name 'varify_server' -Executable $node -ConfigFile '' -Arguments @('server.js') `
    -WorkingDirectory (Join-Path $projectDir 'VarifyServer')
Start-ChatProcess -Name 'status_server' -Executable (Join-Path $binDir 'status_server.exe') `
    -ConfigFile (Join-Path $projectDir 'config\status.ini')
Start-ChatProcess -Name 'chatserver1' -Executable (Join-Path $binDir 'chat_server.exe') `
    -ConfigFile (Join-Path $projectDir 'config\chatserver1.ini')
Start-ChatProcess -Name 'chatserver2' -Executable (Join-Path $binDir 'chat_server.exe') `
    -ConfigFile (Join-Path $projectDir 'config\chatserver2.ini')
Start-ChatProcess -Name 'gate_server' -Executable (Join-Path $binDir 'gate_server.exe') `
    -ConfigFile (Join-Path $projectDir 'config\gate.ini')

Write-Host "Logs: $logDir"
