# RK3562 协议网关验收工具

在仓库根目录运行；Windows 使用 `.venv\Scripts\python.exe`，Linux 可换为相同依赖环境的 `python3`。当前现场 endpoint 为 `tcp/198.51.100.179:7447#bind=198.51.100.200:0`，SN 为 `DEVICE00000001`；第二台设备必须填写自己的 IP 和唯一 SN。

| 工具 | 作用 |
|---|---|
| `zenoh_dual_topic_test.py` | 一个 client session 同时订阅 image 与 pointcloud |
| `protobuf_wire_check.py` | 16 个关键字段的 tag/type 与原始 wire 检查 |
| `h264_check.py` | 每 AU 的 Annex-B、NAL、SPS/PPS、IDR、B slice、GOP 与间隔 |
| `timestamp_check.py` | ≥60 秒双流 stamp、seq、最近邻配对统计 |
| `fps_check.py` | 实际收到的图像和点云帧率 |
| `reconnect_test.py` | 断开后重连、双主题和首个可独立解码 IDR 时延 |
| `sn_check.py` | 多端点 SN 唯一性与重启前后身份清单对比 |
| `config_file_check.py` | 只读 query 和配置、标定文件检查 |
| `long_run_test.py` | 长时单 session 采集及上述离线检查 |
| `package_capture.py` | 从原始采集与 PCAP 生成交付目录 |

## 单机 65 秒采集

```powershell
New-Item -ItemType Directory -Path acceptance_YYYYMMDD -Force | Out-Null
$pcap = Start-Process -FilePath 'C:\Program Files\Wireshark\dumpcap.exe' -ArgumentList '-i 11 -f "host 198.51.100.179 and tcp port 7447" -a duration:70 -w acceptance_YYYYMMDD\capture.pcapng' -WindowStyle Hidden -PassThru
& .venv\Scripts\python.exe tools\live_acceptance_capture.py --endpoint 'tcp/198.51.100.179:7447#bind=198.51.100.200:0' --sn DEVICE00000001 --seconds 65 --raw-packets 2000 --output acceptance_YYYYMMDD\raw_capture
Wait-Process -Id $pcap.Id
& .venv\Scripts\python.exe tools\acceptance\timestamp_check.py acceptance_YYYYMMDD\raw_capture --output acceptance_YYYYMMDD\timestamp_stats.json
& .venv\Scripts\python.exe tools\acceptance\fps_check.py acceptance_YYYYMMDD\raw_capture --output acceptance_YYYYMMDD\fps_stats.json
& .venv\Scripts\python.exe tools\acceptance\h264_check.py acceptance_YYYYMMDD\raw_capture --output acceptance_YYYYMMDD\h264_analysis.csv --summary acceptance_YYYYMMDD\h264_summary.json
```

PCAP 和订阅采集需同时运行。`timestamp_check` 同时报告全窗口与两路共同覆盖区间的最近邻结果；采集边界没有相应点云时，全窗口差可能更大。`h264_check` 只分析 `--raw-packets` 保留的原始包，长稳统计使用完整 CSV。

## 重连及多机身份

```powershell
& .venv\Scripts\python.exe tools\acceptance\zenoh_dual_topic_test.py --endpoint 'tcp/198.51.100.179:7447#bind=198.51.100.200:0' --sn DEVICE00000001 --seconds 10
& .venv\Scripts\python.exe tools\acceptance\reconnect_test.py --endpoint 'tcp/198.51.100.179:7447#bind=198.51.100.200:0' --sn DEVICE00000001
& .venv\Scripts\python.exe tools\acceptance\sn_check.py --device 'DEVICE00000001@tcp/198.51.100.179:7447#bind=198.51.100.200:0' --inventory sn_inventory.json
```

第二台到场后，重复 `--device '第二台SN@tcp/第二台IP:7447'`；重启后用同一 `--inventory` 再运行一次。只给一台设备时工具会明确标记 `multi_device_verified=false`。

## 两小时长稳命令

```powershell
& .venv\Scripts\python.exe tools\acceptance\long_run_test.py --endpoint 'tcp/198.51.100.179:7447#bind=198.51.100.200:0' --sn DEVICE00000001 --seconds 7200 --raw-packets 2000 --output acceptance_2h_YYYYMMDD
```

此命令持续采集全部 Header/seq，保存前 2000 个原始包用于 H264 和 wire 检查；尚需现场运行，当前 65 秒 smoke test 不能代替两小时稳定性结论。

## 多机 NTP 配置

板端已有 `chronyd` 和开机运行的 `S49chrony`。已安装 `/etc/init.d/S50xtntp`，它只在 `/userdata/xtapp/ntp-servers.conf` 存在时将每行指定的甲方统一时间服务器写入 chrony 配置并重启现有 daemon。文件格式为每行一个 IP 或主机名，不允许 shell 语法。配置前须由甲方提供服务器地址。诊断命令：`/etc/init.d/S50xtntp status`，包含系统 UTC 时间、server、offset、stratum、最后同步时间和同步状态。未配置时维持现有单机 PC 校时；不可据此宣称多机已同步。
