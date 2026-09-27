$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $root 'evidence\live_dual'
$config = Get-Content (Join-Path $root 'config\pc.rk3562.local.json') -Raw | ConvertFrom-Json
$config.http_port = 8083
$config.foxglove_port = 8769
$config.board_time_sync_bind = $null
$configPath = Join-Path $evidence 'portable_smoke_config.json'
$config | ConvertTo-Json -Depth 10 | Set-Content $configPath -Encoding UTF8
$python = Join-Path $root '.venv\Scripts\python.exe'
$process = Start-Process -FilePath $python -ArgumentList '-m', 'pc.gateway', '--config', $configPath `
    -WorkingDirectory $root -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $evidence 'portable_smoke.stdout.log') `
    -RedirectStandardError (Join-Path $evidence 'portable_smoke.stderr.log')
try {
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $status = Invoke-RestMethod 'http://127.0.0.1:8083/api/status' -TimeoutSec 1
            if ($status.image.healthy -and $status.pointcloud.healthy) { $ready = $true; break }
        } catch {}
    }
    if (-not $ready) { throw 'gateway did not become healthy' }
    $image = Invoke-WebRequest 'http://127.0.0.1:8083/latest.jpg' -TimeoutSec 3
    [pscustomobject]@{
        Result = 'PASS'
        ImageHz = $status.image.hz
        PointcloudHz = $status.pointcloud.hz
        JpegBytes = $image.RawContentLength
        Endpoint = $status.image.endpoint
    } | Format-List
} finally {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}
