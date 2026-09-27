# Supervise independent gateways for the simultaneously connected legacy device and RK3562.
$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$python = Join-Path $dir '.venv\Scripts\python.exe'
$runtime = Join-Path $dir 'runtime'
$mutex = New-Object System.Threading.Mutex($false, 'Global\PcGatewaySupervisor')
if (-not $mutex.WaitOne(0)) { exit 0 }
New-Item -ItemType Directory -Path $runtime -Force | Out-Null

$instances = @(
    [pscustomobject]@{ Name='rk3562'; Config='config\pc.rk3562.json'; Port=8081; Process=$null; Failures=0 },
    [pscustomobject]@{ Name='legacy';  Config='config\pc.legacy.json';  Port=8082; Process=$null; Failures=0 }
)

function Start-GatewayInstance($instance) {
    $configPath = Join-Path $dir $instance.Config
    $instance.Process = Start-Process -FilePath $python `
        -ArgumentList '-m', 'pc.gateway', '--config', $configPath `
        -WorkingDirectory $dir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runtime "$($instance.Name).stdout.log") `
        -RedirectStandardError (Join-Path $runtime "$($instance.Name).stderr.log")
    $instance.Failures = 0
}

try {
    if (-not (Test-Path -LiteralPath $python)) { throw 'Run setup_pc.ps1 first.' }
    foreach ($instance in $instances) { Start-GatewayInstance $instance }
    while ($true) {
        Start-Sleep -Seconds 2
        foreach ($instance in $instances) {
            $restart = $instance.Process.HasExited
            if (-not $restart) {
                try {
                    $null = Invoke-RestMethod "http://127.0.0.1:$($instance.Port)/api/status" -TimeoutSec 2
                    $instance.Failures = 0
                } catch {
                    $instance.Failures++
                    $restart = $instance.Failures -ge 5
                }
            }
            if ($restart) {
                if (-not $instance.Process.HasExited) {
                    & taskkill.exe /PID $instance.Process.Id /T /F 2>$null | Out-Null
                }
                "$(Get-Date -Format o) $($instance.Name) gateway restart" |
                    Out-File (Join-Path $runtime 'supervisor.log') -Append -Encoding utf8
                Start-GatewayInstance $instance
            }
        }
    }
} finally {
    foreach ($instance in $instances) {
        if ($instance.Process -and -not $instance.Process.HasExited) {
            & taskkill.exe /PID $instance.Process.Id /T /F 2>$null | Out-Null
        }
    }
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
