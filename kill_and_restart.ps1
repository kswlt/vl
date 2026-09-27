Stop-ScheduledTask -TaskName PcGatewayAutostart -ErrorAction SilentlyContinue
Start-Sleep 2

$procs = Get-WmiObject Win32_Process | Where-Object { $_.CommandLine -like '*pc.gateway*' -or $_.CommandLine -like '*start_gateway*' }
foreach ($p in $procs) {
    Write-Host "Killing PID $($p.ProcessId): $($p.CommandLine)"
    Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep 2

Start-ScheduledTask -TaskName PcGatewayAutostart
Write-Host "Restarted."
