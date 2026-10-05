# vl — RK3562 空间相机源码（脱敏交付版）

RK3562 空间相机项目的源码交付仓库。本仓库内容已脱敏：抹除了公司名、原设备厂商品牌、生产部署 IP、真实设备序列号、本机路径等敏感信息，仅保留业务逻辑与部署文档。

> 新同事请先读 **[README_先读.md](README_先读.md)** 与 **docs/** 目录（部署、构建、接口、验收、已知问题、交接清单六份文档）。

## 仓库结构

| 目录/文件 | 内容 |
|---|---|
| `board/` | 板端（RK3562）核心源码：相机/雷达采集、Zenoh 发布、点云滤波（`xt_m60_filter.hpp`）、内嵌网页、启动脚本 |
| `pc/`、`protocol/` | Windows 端接收查看器与 Protobuf 协议定义 |
| `tools/calibration` | 相机-雷达标定工具链（采集、求解、候选验证） |
| `tools/acceptance`、`tests/` | 协议验收脚本与回归测试 |
| `vendor/` | 第三方开源依赖（zenoh-pico、nlohmann/json、curl 等） |
| `vizd/`、`buzzer/` | 可选预览链路与蜂鸣器/RTC 相关源码（不默认部署） |
| `deploy/` | Windows 上传、板端备份安装与回滚脚本 |
| `docs/` | 部署/构建/接口/验收/已知问题/交接清单说明 |
| `SOURCE_SNAPSHOT.json` | 核心源码摘要与交付导出修改说明 |
| `VALIDATION.json`、`BUILD_RECORD.json`、`dependency_resolution.json` | 构建与验证记录 |

## 未包含在本仓库的内容

- `runtime/app/*`（ARM64 编译产物）与 `dependencies/`（zenoh-c 开发包）——二进制无法做文本脱敏，且体积大，未上传；需要时按 `docs/02` 重新编译或走交付包单独传输。
- 构建/测试日志（`build.log` 等）与 `MANIFEST.sha256`（对应完整交付包，含二进制哈希）——随原交付 zip 提供。
- `docs/reference/XT-M60-datasheet.pdf`（第三方产品手册）。
- 相机凭据、工厂身份、标定文件、历史二进制等一律不在仓库内；相机凭据通过 `camera.env` / 环境变量在部署时注入。

## 部署起点

1. Windows 端：`deploy/deploy_windows.py` 上传板端程序并执行 `install.sh`（备份→安装→回滚）。
2. 板端：`source/board/` 交叉编译产物部署到 `/userdata/xtapp`，init 脚本管理自启。
3. 新板接线与地址规划见 `docs/01_Windows部署新RK3562.md`（文档内地址均为示例值，需按现场修改）。
