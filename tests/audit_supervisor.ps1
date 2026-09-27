$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$listener = Get-NetTCPConnection -LocalPort 8080 -State Listen | Select-Object -First 1
$process = Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)"
if ($process.CommandLine -notlike '*pc.gateway*' -or $process.CommandLine -notlike "*$root*") {
    throw 'Port owner is not this project gateway; refusing to stop it.'
}
$before = $listener.OwningProcess
$started = [Diagnostics.Stopwatch]::StartNew()
Stop-Process -Id $before -Force
$state = $null
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    try {
        $state = Invoke-RestMethod 'http://127.0.0.1:8080/api/status' -TimeoutSec 1
        if ($state.image.healthy -and $state.pointcloud.healthy) { break }
    } catch { }
}
if (-not $state.image.healthy -or -not $state.pointcloud.healthy) { throw 'Gateway did not recover' }
$after = (Get-NetTCPConnection -LocalPort 8080 -State Listen | Select-Object -First 1).OwningProcess
if ($before -eq $after) { throw 'Gateway PID did not change' }
$report = [ordered]@{result='PASS'; old_pid=$before; new_pid=$after; recovery_seconds=$started.Elapsed.TotalSeconds; task_state=(Get-ScheduledTask -TaskName PcGatewayAutostart).State.ToString(); status=$state}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'evidence/supervisor.json') -Encoding utf8
[pscustomobject]$report | Select-Object result,old_pid,new_pid,recovery_seconds,task_state
