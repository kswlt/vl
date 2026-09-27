param(
    [Parameter(Mandatory = $true)][string]$AdapterName,
    [string]$BoardIp = '198.51.100.179',
    [string]$HostIp = '198.51.100.200',
    [string]$Sn = 'DEVICE00000001'
)

$ErrorActionPreference = 'Stop'
$adapter = Get-NetAdapter -Name $AdapterName -ErrorAction Stop
if ($adapter.Status -ne 'Up') { throw "网卡 $AdapterName 未连接。" }

$other = Get-NetIPAddress -AddressFamily IPv4 -IPAddress $HostIp -ErrorAction SilentlyContinue |
    Where-Object InterfaceIndex -ne $adapter.ifIndex
if ($other) { throw "地址 $HostIp 已分配给其他网卡。" }

if (-not (Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $HostIp -ErrorAction SilentlyContinue)) {
    New-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $HostIp -PrefixLength 32 -SkipAsSource $false | Out-Null
}
$destination = "$BoardIp/32"
if (-not (Get-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix $destination -ErrorAction SilentlyContinue |
           Where-Object NextHop -eq '0.0.0.0')) {
    New-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix $destination -NextHop '0.0.0.0' `
        -RouteMetric 5 -PolicyStore PersistentStore | Out-Null
}

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'config\pc.rk3562.json'
$local = Join-Path $root 'config\pc.rk3562.local.json'
$config = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
$config.device_ip = $BoardIp
$config.device_api = "http://${BoardIp}:8080/api/device"
$config.sn = $Sn
$config.image_endpoint = "tcp/${BoardIp}:7447"
$config.pointcloud_endpoint = "tcp/${BoardIp}:7447"
$config.board_time_sync_bind = $HostIp
$config.PSObject.Properties.Remove('ffmpeg_path')
$config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $local -Encoding UTF8

$web = Test-NetConnection $BoardIp -Port 8080 -InformationLevel Quiet -WarningAction SilentlyContinue
$zenoh = Test-NetConnection $BoardIp -Port 7447 -InformationLevel Quiet -WarningAction SilentlyContinue
[pscustomobject]@{
    Adapter = $AdapterName
    HostIp = $HostIp
    BoardIp = $BoardIp
    Web8080 = $web
    Zenoh7447 = $zenoh
    Config = $local
} | Format-List
Write-Host "管理页面: http://${BoardIp}:8080/"
Write-Host "PC 预览: 先运行 setup_pc.ps1 并确保 ffmpeg 在 PATH，然后运行 .venv\Scripts\python.exe -m pc.gateway --config `"$local`""
