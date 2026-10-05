# Windows 给另一块 RK3562 部署

## 1. 适用范围

适用于已装 RK3562 Linux 基础镜像的 aarch64 板，使用 MC800S 相机和 XT-M60 雷达。此流程部署应用，不烧录系统镜像。其他板型、不同系统镜像或不同传感器，需要重新核对 ABI、驱动和网络。

最省事的是为新板准备与原板相同的基础镜像：root SSH、`/userdata`、`/etc/init.d`、GStreamer 及 Rockchip MPP 插件、Zenoh 路由器。禁止复制原板整块磁盘作为免配置办法：其中可能有重复身份、IP、凭据和无效标定。

Windows 需要 Python 3.13 和系统 OpenSSH 客户端。部署脚本仅用 Python 标准库，无需安装项目 Python 依赖，也无需在部署电脑重新编译。

```powershell
py -3.13 --version
ssh -V
Get-Command scp
```

## 2. 新板网络和编号

下面示例采用新板管理地址 `198.51.100.180`、电脑有线地址 `198.51.100.200/24`、新 SN `DEVICE00000002`。它们是示例，执行时替换为真实分配值。

- 不与原板 `198.51.100.179` 或原 SN `DEVICE00000001` 冲突。
- 刚上电的第二台设备可先接独立交换机或电脑，避免同地址冲突。
- 只配置实际接线的有线网卡；网关、DNS 留空，不修改 Wi-Fi。
- 当前代码要求相机 `192.0.2.123`、雷达 `192.0.2.101`。雷达板端源地址默认 `192.0.2.179`、接口默认 eth0，可通过部署参数配置。
- **两套同地址传感器不能接在同一二层网络上。** 仅修改管理 IP 或 SN 无法隔离相机/雷达地址；需要独立交换机、VLAN 或改造代码中的固定传感器地址后重编。
- 本入口不修改管理 IP、不重写工厂 YAML。新设备使用 `/userdata/xtapp/device_identity.yaml` 作为应用身份，运行时环境显式指定该路径，协议 SN 与配置一致。正式编号需遵循甲方编号规则。

```powershell
Test-NetConnection 198.51.100.180 -Port 22
ssh root@198.51.100.180 "uname -m; cat /etc/os-release; ip -br addr; df -h /userdata"
```

第一次 SSH 登录，应通过设备负责人/控制台核对主机指纹。使用新板自己的 SSH 凭据，包内不包含密码。

## 3. 基础系统预备

新板必须有：

| 条件 | 核对方法 |
|---|---|
| 架构 aarch64 | `uname -m` |
| root、可写 `/userdata`、`/etc/init.d` | SSH 查看目录和权限 |
| 至少 128 MiB 可用空间 | `df -Pk /userdata`；另留上传和日志空间 |
| `/usr/bin/gst-launch-1.0` | `test -x /usr/bin/gst-launch-1.0` |
| rtspsrc、rtph264depay、h264parse、mppvideodec、mppjpegenc | 逐个 `gst-inspect-1.0 插件名` |
| 兼容的 ARM64 Zenoh 路由器 | `/userdata/xtapp/zenohd` 或 `/usr/bin/zenohd` 可执行；记录其版本 |
| 传感器连接正常 | 相机/雷达地址可达，接口名正确 |

优先使用已经在同款系统验证过的路由器版本。缺少路由器时，由基础镜像负责人提供官方 ARM64 构建及许可证/版本记录，安装到上述路径。不能从本包 `dependencies` 中把 `libzenohc.a` 当作路由器；它是编译用静态库。

JPEG 转码依赖 MPP 插件。仅能 ping 通、安装了普通 FFmpeg 或 CPU 能运行程序，都不能替代这些条件。

## 4. 三步部署

建议解压到较短的路径，例如 `C:\rk_delivery`，并进入 `RK3562_交付包` 目录。先校验：

```powershell
py -3.13 .\verify_package.py
```

### 第一步：只看计划，不连接设备

```powershell
py -3.13 .\deploy\deploy_windows.py --host 198.51.100.180 --sn DEVICE00000002
```

### 第二步：上传临时包并做远程预检，不替换应用

```powershell
py -3.13 .\deploy\deploy_windows.py --host 198.51.100.180 --sn DEVICE00000002 --check-only --configure
```

脚本隐式输入的是**相机密码**，与 SSH 密码不同。输入不回显；相机密码及其设备要求的 MD5 形式写入临时配置，不进入交付压缩包。非默认用户名可传 `--camera-user`，接口可传 `--radar-interface eth1`。相机用户名目前限制为字母、数字、下划线和连字符；密码不允许换行或 NUL。MC800S 固件对 RTSP 密码形式仍需在新设备验证。

远程预检会建立唯一临时目录并检查文件哈希、架构、空间、GStreamer 插件、路由器及标定状态。若已有 `calibration.json`，会停止：应先确认它是否属于该设备、是否兼容本包新坐标轴，必要时手动备份归档；不得未经判断沿用旧标定。

### 第三步：安装并启动

```powershell
py -3.13 .\deploy\deploy_windows.py --host 198.51.100.180 --sn DEVICE00000002 --apply --configure
```

`--configure` 明确允许在备份后写入本次设备配置；没有它时，已有 config.json 会使安装停止。默认工程模板：JPEG 10 fps、点云 10 Hz、IMU 关闭、单图像批次、Unix 微秒时间戳。它是启动模板，不是接收频率承诺；完成独立测量后再提高帧率。

安装只替换核心 `xt_gateway`、`xt_camera`、`xt_radar`、四个核心启动脚本及本次设备配置。不会复制源码到板端，不安装候选标定，不修改管理网络、RTC/NTP 系统角色、工厂文件或 SSH 设置。其他旧服务不会自动清理；部署前需排除旧全功能雷达程序与新网关重复占用端口/主题。

安装前保存 `/userdata/xtapp/backup-delivery-时间-进程号`。运行中途失败会尝试恢复该备份；网络突然断电时仍需人工核查。成功后检查三个文件哈希和四个进程，输出回滚路径。**这只证明安装和进程检查，不证明已经收到合格传感器数据。**

部署助手默认核心链路不启动 `vizd`。独立预览服务见下节。若原系统已有会自动启动的 `S98zzvizd`，须另行检查其凭据、FFmpeg 和 UDP 镜像配置。

## 5. 可选板端独立预览

`runtime/app/vizd` 已重新编译，默认不安装，因为新板还需要合适的 ARM64 FFmpeg 与网络镜像设置。其交付源码从环境读取 `XT_VIZD_RTSP_URL`、`XT_VIZD_SNAPSHOT_URL`、可选 `XT_VIZD_FFMPEG`。应在权限 600 的独立配置中设置，禁止把真实 URL/密码写回交付源码。

若使用当前雷达可选镜像，`XT_PC_UDP_MIRROR=1` 开启目标 `192.0.2.200:17778`。同板预览需要根据实际网络设置本机接收/重定向规则，并单独测试。包内不自动照搬旧 iptables 规则，也未提供经过新板验收的可选预览安装入口。

暂时不配独立预览时，用 PC 查看器接收协议 JPEG/点云。板端 8080 管理接口和协议 JPEG 预览属于核心网关；8081 独立 H.264/点云预览不能因此宣称可用。

## 6. 安装后检查及回滚

```powershell
curl.exe --max-time 5 http://198.51.100.180:8080/api/management/status
Test-NetConnection 198.51.100.180 -Port 7447
ssh root@198.51.100.180 "pidof zenohd xt_gateway xt_camera xt_radar"
```

在需要协议查看/验收的电脑上，进入 `source`，运行 `setup_pc.ps1` 安装开发依赖。按 `config/pc.template.json` 生成本机 `config/pc.local.json`，替换设备 IP、SN、两个 endpoint、board_time_sync_bind，保留 HTTP bind=127.0.0.1。

```powershell
.\.venv\Scripts\python.exe tools\acceptance\zenoh_dual_topic_test.py --endpoint "tcp/198.51.100.180:7447#bind=198.51.100.200:0" --sn DEVICE00000002 --seconds 10
.\.venv\Scripts\python.exe -m pc.gateway --config config\pc.local.json
```

PC 查看器地址 `http://127.0.0.1:8081`。H.264 解码预览需要电脑 PATH 中的 FFmpeg；JPEG 接收显示不需要借助 FFmpeg 解码。

回滚使用安装输出的准确备份路径，例如：

```powershell
ssh root@198.51.100.180 "sh /userdata/xtapp/backup-delivery-实际目录/rollback.sh rollback /userdata/xtapp/backup-delivery-实际目录"
```

回滚只恢复本入口管理的文件，不回退已另行修改的管理网络、基础系统或第三方程序。新板原本没有应用时，回滚移除本次新增文件。
