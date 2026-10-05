# XT-M60 原始点云有效性审查（2026-09-29）

## 链路与修复位置

`XT-M60 UDP 7687 → xt_radar.cpp::assemble_udp → parse_frame → compute_points → LidarPointMsgArray → xt_gateway → active/DEVICE00000001/pointcloud`。

过滤发生在 `compute_points` 生成 XYZ 之前。被判无效的点写为 NaN；`build_pointcloud_msg` 只序列化有限坐标。因此协议主题和网页预览共同使用过滤后的真实点，网页没有另行隐藏杂点。

## SDK 对照

- `vendor/xtsdk/xtsdk/Iframe.h`：距离无效码从 `DEPTH_ABNORMAL=964000` 开始；振幅错误码从 `AMPLITUDE_ABNORMAL=64000` 开始。
- `vendor/xtsdk/xtsdk/frame.cpp::sortData`：M60 的有效原始距离乘以 `unit_div`，原始距离 `>=64000` 变成 `900000+原始码`。现有解析与此一致。
- `vendor/xtsdk/xtsdk/cartesianTransform.cpp::calculatePointCloud`：可配置的 `cutMinAmp` 会拒绝小于阈值或标记为 `AMPLITUDE_INVALID` 的回波，并拒绝无效距离和角落遮罩。板端当前帧报告 `miniAmp=70`，新过滤器以此为本帧下限；该条件在实测帧中没有额外删除点，因为设备已将低于 70 的点标为无效距离。
- 厂商示例打开边缘、空间、中值等可选滤波，但具体边缘与空间算法位于闭源 `libxtsdk_shared.so`。示例中的阈值不等于本设备已启用的设置。离线调用该库尝试因缺少旧版 TBB ABI (`_ZTIN3tbb4taskE`) 失败，因此不能声称下面的邻域规则与闭源实现逐点一致。

## 新增邻域判定

基础无效码、振幅和角落遮罩按上述 SDK 语义判定。额外邻域判定只删除两类 **弱回波**：

1. 飞点：3×3 邻域至少 5 个原始有效邻居，其中至少 2 个明显近于该点、2 个明显远于该点；该点振幅小于邻居振幅中位数的一半。
2. 孤立弱点：至少 5 个原始有效邻居，无一个与该点深度接近，且该点振幅小于邻居中位数的一半。

深度差门限为 `max(180 mm, 距离的 3%)`。正常强回波、真实深度断边的一侧、画面边界和邻居不足处均保留。只删除符合条件的测量，不对剩余 XYZ 做平滑或补造。邻域判断基于第一阶段固定的有效掩码，不受扫描顺序影响。

## 同一批原始帧对比

板端只读 AF_PACKET 捕获 5 秒 UDP，得到 47 帧完整原始帧（每帧 160×60）。本地原始捕获位于 `build/xt_m60_raw_5s.pcap`，未留在板子上。

| 项目 | 47 帧点数 |
|---|---:|
| 原始像素 | 451,200 |
| 无效距离码 | 127,596 |
| 原程序已剔除的角落点 | 7,568 |
| 原程序实际发布基线 | 316,036 |
| 新增飞点剔除 | 1,830 |
| 新增孤立弱点剔除 | 1,277 |
| 修复后实际发布点 | 312,929 |

47 帧的 `miniAmp` 均为 70。有效距离像素中未发现振幅无效码或低于 70 的振幅，因此这些规则本次没有新增剔除，但实现会处理将来不同配置、固件或故障帧。新增邻域剔除占基线约 0.98%。

独立 Python 原始帧解析和 C++ 板端规则对第一帧一致：原始 9,600，距离无效 2,727，角落 161，飞点 38，孤立弱点 23，剩余 6,651。

## 测试与部署

- `tests/test_xt_m60_filter.cpp`：覆盖距离零值、无效码、振幅错误码、低于阈值、角落、强孤立真点保留、弱孤立点和混合深度飞点；分类总数必须等于原始点数。
- ARM64 静态编译通过；2026-09-29 已仅部署可执行文件到 `/userdata/xtapp/xt_radar`，未向板端复制源码。旧版备份为 `/userdata/xtapp/xt_radar.before-m60-filter-20260929`。
- 板端 `[filter]` 日志每 25 帧报告 raw、valid、mini_amp 和各原因计数，`send_ret=0`。连续数分钟采集日志中帧计数增长、队列丢弃和发布错误均为 0。
- 从 `active/DEVICE00000001/pointcloud` 实收 8 秒共 79 帧，10.0 Hz，每帧 6,019–6,079 个点，`Header.scaler=1000`。这是实时场景，点数与上面的静态原始样本不应逐帧相等。
- 板端当时 `xt_radar` 的 `ps` CPU 为约 43.8% 单核口径，未出现发布速率下降。

复测命令：`python tools/analyze_xt_raw.py build/xt_m60_raw_5s.pcap --cut 70`、`tests/test_xt_m60_filter.cpp` 编译运行、`.venv/Scripts/python.exe tools/verify_xt_m60_live.py`。闭源 SDK 的精确边缘掩码仍需厂商提供兼容的运行库或算法说明，才能作逐点一致性声明。
