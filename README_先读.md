# RK3562 空间相机源码与部署交付包

日期：2026-10-04。状态：**工程源码交付及试部署包，尚未达到正式生产验收条件。**

## 最方便的部署方式

在 Windows 上解压本包，用已编译的 ARM64 程序通过 OpenSSH 上传。板端只装运行文件；无需把源码、WSL 或编译器装到板子上。

1. 确认新板已经安装同款 RK3562 Linux 基础系统，具备 Rockchip GStreamer/MPP 插件、兼容的 ARM64 `zenohd` 和 root SSH。
2. 新板单独接线，确认其 IP 与原板不冲突，并分配唯一 SN。
3. 按 `docs/01_Windows部署新RK3562.md` 执行部署计划、远程预检、正式安装。
4. 执行双流检查和重启复测。安装成功不等于标定或甲方验收通过。

本包不含开发板系统镜像、GStreamer/MPP 系统库、FFmpeg 或 Zenoh 路由器二进制。它们属于基础系统/第三方运行依赖；缺失时预检会停止安装。`runtime/app/vizd` 是可选程序，默认部署入口安装核心三个程序，不自动安装独立预览链路。当前协议 JPEG 可用 PC 查看器显示，独立 H.264/点云网页需要完成可选预览配置。

## 包内内容

| 目录/文件 | 用途 |
|---|---|
| `source/board` | 核心板端源码、网页、启动脚本、构建入口 |
| `source/pc`、`source/protocol` | Windows 接收查看器及 Protobuf 协议 |
| `source/tools/calibration` | 本地采集、求解、冻结候选验证工具 |
| `source/tools/acceptance`、`source/tests` | 协议验收脚本和选定的核心回归测试 |
| `source/vendor`、`dependencies` | 编译所用第三方源码、许可证及 zenoh-c ARM64 开发包 |
| `source/vizd`、`source/buzzer` | 可选预览及蜂鸣器/RTC 相关源码，不默认安装 |
| `runtime/app` | 本轮从交付源码重新编译的 ARM64 程序 |
| `deploy` | Windows 上传、板端备份安装和回滚脚本 |
| `docs` | 部署、重建、接口、验收、已知问题、交接清单 |
| `SOURCE_SNAPSHOT.json` | 原始核心源码摘要及交付导出修改说明 |
| `BUILD_RECORD.json`、`build.log` | 编译参数、依赖摘要与实际编译记录 |
| `MANIFEST.sha256`、`verify_package.py` | 包内逐文件完整性校验 |

未打包原板凭据、工厂身份、标定文件、历史二进制、照片/抓包、个人文档及无关逆向材料。**各台设备不得共用原板的标定文件或工厂身份。**

## 接收方先核对

```powershell
py -3.13 .\verify_package.py
```

源码完整性以本包逐文件哈希为准。Git HEAD 仅为参考，当前工作区存在未提交及未跟踪文件。本包包含为交付所作的凭据和身份配置修改，与原板旧运行程序不同，详见快照说明。

相关验收限制见 `docs/05_已知问题与交付边界.md`。完整代码审计尚未完成，本包不能标称“全量审计通过”。
