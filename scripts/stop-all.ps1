param(
    [string]$BuildDir = "$(Get-Location)\build\windows-server-release"
)

$runDir = Join-Path $BuildDir 'run'
foreach ($name in @('gate_server', 'chatserver2', 'chatserver1', 'status_server', 'varify_server')) {
    $pidFile = Join-Path $runDir "$name.pid"
    if (-not (Test-Path $pidFile)) {
        continue
    }
    $processId = [int](Get-Content $pidFile)
    $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
    if ($process) {
        Stop-Process -Id $processId
        Write-Host "Stopped $name (pid $processId)"
    }
    Remove-Item -LiteralPath $pidFile -Force
}
