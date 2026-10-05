# 空间相机协议一键验收

运行 `python acceptance_tool.py --seconds 60`，工具从 `config/pc.json` 读取 SN 和两个 Zenoh endpoint，独立订阅 `active/{sn}/image`、`active/{sn}/pointcloud`，使用 `pc/active_msgs_pb2.py`（由 `protocol/active_msgs.proto` 生成）解码。每次运行在 `evidence/acceptance-YYYYMMDD-HHMMSS/` 创建新目录，输出 pcapng、CSV、JSON、HTML、日志和少量 JPEG/H264 样本。

默认运行是只读旁路接收端：不发送 command，不访问 MC800S HTTP/RTSP。`--check-config` 才查询 `config_file`；`--test-setting` 才发送空的 `SettingRequest`；`--test-reboot` 才发送重启命令。后两项均为显式选择，结果只会在已选择的项目中计入总判定。抓包依赖 Wireshark `dumpcap`（接口默认 1，可按本机调整）；H264 实时/结束解码依赖 `ffmpeg`。缺少可选程序时会保留验收统计，并在日志和 network-summary 中标记 unavailable。
