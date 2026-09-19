param(
    [string]$Image = 'deepecho-server:release-resource-1',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bundleStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bundleDirectory = Join-Path $repoRoot "build/resource-migration-$bundleStamp"
$bundleArchive = Join-Path $repoRoot "build/resource-migration-$bundleStamp.tar.gz"
foreach ($command in @('docker', 'tar')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command missing: $command"
    }
}
Push-Location $repoRoot
try {
    if (-not $SkipBuild) {
        Write-Host 'Building Linux amd64 server image; Docker Desktop must use Linux containers.'
        & docker build --platform linux/amd64 -f deploy/docker/server.Dockerfile -t $Image .
        if ($LASTEXITCODE -ne 0) { throw 'Image build failed. No migration package was created.' }
    }
    & docker run --rm --network none --entrypoint python3 $Image -c "import os; assert all(os.access('/app/bin/'+n,os.X_OK) for n in ('gate_server','status_server','chat_server','resource_server'))"
    if ($LASTEXITCODE -ne 0) { throw 'Server image validation failed.' }
    New-Item -ItemType Directory -Path $bundleDirectory -ErrorAction Stop | Out-Null
    foreach ($name in @('migrate-resource.py', 'resource.ini', 'generate-resource-cert.sh', 'MIGRATE_RESOURCE.md')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $bundleDirectory
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'database/migrations/002_resources.sql') -Destination $bundleDirectory
    # Normalize text files for Linux even when Git checked them out with CRLF.
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    foreach ($file in Get-ChildItem -LiteralPath $bundleDirectory -File) {
        $content = [System.IO.File]::ReadAllText($file.FullName).Replace("`r`n", "`n")
        [System.IO.File]::WriteAllText($file.FullName, $content, $utf8)
    }
    [System.IO.File]::WriteAllText((Join-Path $bundleDirectory 'image-name.txt'), "$Image`n", $utf8)
    Write-Host 'Saving image into migration package...'
    & docker save -o (Join-Path $bundleDirectory 'server-image.tar') $Image
    if ($LASTEXITCODE -ne 0) { throw 'docker save failed.' }
    & tar -czf $bundleArchive -C $bundleDirectory .
    if ($LASTEXITCODE -ne 0) { throw 'Archive creation failed.' }
    Write-Host "Package ready: $bundleArchive"
    Write-Host 'Upload this archive to a NEW directory on the server; follow MIGRATE_RESOURCE.md.'
}
finally {
    Pop-Location
}
