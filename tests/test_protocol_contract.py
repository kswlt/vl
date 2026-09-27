"""Static guards for protocol rules that are easy to regress in C++."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RADAR = (ROOT / "board/src/xt_radar.cpp").read_text(encoding="utf-8")
CONFIG = (ROOT / "board/src/device_config.hpp").read_text(encoding="utf-8")
PROTO = (ROOT / "protocol/active_msgs.proto").read_text(encoding="utf-8")


class ProtocolContractTests(unittest.TestCase):
    def test_all_runtime_header_stamps_use_configured_protocol_clock(self):
        self.assertNotIn("int64_t(now_ms())*1000000LL", RADAR)
        self.assertIn('configure_protocol_clock(g_config.value("timestamp_mode", "monotonic"))', RADAR)
        self.assertEqual(RADAR.count("enc_header(file, g_file_seq++, xt::protocol_timestamp_us()"), 1)
        self.assertIn("const uint32_t response_seq = g_cmd_seq++", RADAR)
        self.assertEqual(RADAR.count("enc_header(w, response_seq, xt::protocol_timestamp_us()"), 1)
        self.assertIn("const int64_t source_stamp_us = xt::protocol_timestamp_us()", RADAR)

    def test_legacy_compatibility_contract(self):
        self.assertIn('"legacy_compatibility"', CONFIG)
        self.assertIn('"active/%s/image"', RADAR)
        self.assertIn('"active/%s/pointcloud"', RADAR)
        self.assertIn('"active/%s/cmd/%s"', RADAR)
        self.assertIn('declare_command(stream_sn, "setting"', RADAR)
        self.assertIn('declare_command(stream_sn, "reboot"', RADAR)
        self.assertNotIn('"active/%s/cmd/*"', RADAR)
        self.assertIn('GET /api/device', RADAR)
        self.assertIn('compat_image_count >= g_legacy_image_batch_size', RADAR)
        self.assertIn('fragmented_reliable_data_qos()', RADAR)
        self.assertIn('px.push_back((float)z)', RADAR)

    def test_proto3_zero_scalars_are_omitted(self):
        self.assertIn('void u32(int field, uint32_t v) { if (v == 0) return;', RADAR)
        self.assertIn('void s32(int field, int32_t v) { if (v == 0) return;', RADAR)
        self.assertIn('void s64(int field, int64_t v) { if (v == 0) return;', RADAR)
        self.assertIn('py.push_back((float)-x)', RADAR)
        self.assertIn('pz.push_back((float)y)', RADAR)


    def test_setting_and_imu_contract(self):
        self.assertIn("0=no change, 1=H264, 2=JPEG", PROTO)
        self.assertIn('"imu_fps",0', CONFIG)
        self.assertIn('s.at("imu_fps") != 0', CONFIG)
        self.assertNotIn('"active/{SN}/imu"', RADAR)


    def test_production_topics_and_encoding_contract(self):
        self.assertIn('getenv("XT_PCLOUD_PREVIEW")', RADAR)
        self.assertIn('strcmp(getenv("XT_PCLOUD_PREVIEW"), "1") == 0', RADAR)
        self.assertIn('"active/%s/pointcloud_preview"', RADAR)  # debug path only
        camera = (ROOT / "board/src/xt_camera.cpp").read_text(encoding="utf-8")
        self.assertIn("static const int H264_WIDTH = 1920", camera)
        self.assertNotIn("8000", camera)
        self.assertNotIn("8001", camera)

    def test_h264_gop_is_enforced_by_rk_hardware_encoder(self):
        camera = (ROOT / "board/src/xt_camera.cpp").read_text(encoding="utf-8")
        self.assertIn('H264_GOP = settings["sensors"]["h264_gop"]', camera)
        self.assertIn('"/usr/bin/gst-launch-1.0"', camera)
        self.assertIn('"mpph264enc", gop, bitrate', camera)
        self.assertIn('"header-mode=each-idr"', camera)
        self.assertIn('"video/x-h264,stream-format=byte-stream,alignment=au"', camera)


if __name__ == "__main__":
    unittest.main()
