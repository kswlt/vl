from copy import deepcopy
from pathlib import Path

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Pt, RGBColor


SRC = Path(r"C:\Users\user\Documents\xwechat_files\wechat_account\msg\file\2026-09\原设备协议对照及新增兼容逆向工作说明_版式调整.docx")
OUT = Path(r"C:\Users\user\Desktop\project\原设备协议对照及新增兼容逆向工作说明_补充完善版.docx")


def set_east_asia_font(run, name="宋体"):
    run.font.name = name
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.rFonts
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.insert(0, rfonts)
    rfonts.set(qn("w:eastAsia"), name)


def replace_paragraph(paragraph, text):
    old_rpr = None
    if paragraph.runs and paragraph.runs[0]._element.rPr is not None:
        old_rpr = deepcopy(paragraph.runs[0]._element.rPr)
    for run in list(paragraph.runs):
        paragraph._p.remove(run._element)
    run = paragraph.add_run(text)
    if old_rpr is not None:
        run._element.insert(0, old_rpr)
    return run


def set_cell(cell, text):
    paragraph = cell.paragraphs[0]
    old_ppr = deepcopy(paragraph._p.pPr) if paragraph._p.pPr is not None else None
    old_rpr = None
    if paragraph.runs and paragraph.runs[0]._element.rPr is not None:
        old_rpr = deepcopy(paragraph.runs[0]._element.rPr)
    cell.text = ""
    paragraph = cell.paragraphs[0]
    if old_ppr is not None:
        if paragraph._p.pPr is not None:
            paragraph._p.remove(paragraph._p.pPr)
        paragraph._p.insert(0, old_ppr)
    run = paragraph.add_run(text)
    if old_rpr is not None:
        run._element.insert(0, old_rpr)


def clone_last_row(table):
    new_tr = deepcopy(table.rows[-1]._tr)
    table._tbl.append(new_tr)
    return table.rows[-1]


def set_row(table, row_index, values):
    row = table.rows[row_index]
    for cell, value in zip(row.cells, values):
        set_cell(cell, value)


def append_row(table, values):
    row = clone_last_row(table)
    for cell, value in zip(row.cells, values):
        set_cell(cell, value)


doc = Document(SRC)

# Keep the retained design, but make the document title semantically correct for Word navigation.
title = doc.paragraphs[0]
title.style = doc.styles["Title"]
title.alignment = WD_ALIGN_PARAGRAPH.CENTER
# Word's built-in Title style may carry a theme-colored bottom border.  The
# retained document uses whitespace rather than a decorative title rule.
for owner in (doc.styles["Title"]._element, title._p):
    ppr = owner.pPr
    if ppr is not None:
        pbdr = ppr.find(qn("w:pBdr"))
        if pbdr is not None:
            ppr.remove(pbdr)
for run in title.runs:
    run.font.bold = True
    run.font.size = Pt(22)
    run.font.color.rgb = RGBColor(0, 0, 0)
    set_east_asia_font(run, "黑体")

paragraph_updates = {
    2: "说明：本文档用于说明现阶段技术实现、公开协议与原设备实机行为之间的差异，以及由此产生的新增兼容逆向工作。文中已补入截至 2026 年 9 月 20 日的抓包、命令通道、数据流和部署结果；仍未完成的冷启动、双机同时在线及坐标标定项目均单独标明。本文档为技术说明，不替代双方正式补充协议或商务合同。",
    14: "在取得原设备后，乙方对原设备进行了同机抓取和消息解析，并在公开协议之外复现了多项原设备实际行为。现有证据包括两设备各 20 秒的图像和点云数据、LEGACY IMU 数据、字段级与 wire 级解析、H.264 NAL 分析、Zenoh 1.10 命令回归及板端配置核验。",
    15: "截至 2026 年 9 月 20 日，RK3562 已改用官方 zenohd 1.10.0 与 zenoh-c 1.10.0 作为对外 Zenoh 通道，修复了早期 zenoh-pico 方案在主流 Zenoh 1.10 客户端下 cmd 查询超时的问题。当前对外 SN 为 DEVICE00000001，避免与原 LEGACY 的 LEGACY00000001 发生 Topic 冲突。",
    19: "当前可复核证据包括：xt_gateway 二进制 SHA-256 为 e8751485601c84e1492e4706dc7a2d6633e6bb5935895be198c408ec4c089ac0；最终统计窗口 H.264 为 30.44 Hz、点云为 9.97 Hz；Zenoh 1.10 setting 同会话 50/50、新建会话 100/100、config_file 10/10、非法 reboot 请求 10/10；Python 协议与网关回归测试 15/15 通过。整板冷启动后的双机同时在线复验、20 分钟以上长稳记录和坐标靶标验证仍需补做。",
    27: "上述缺失和矛盾说明，《空间相机通信协议 V1.0》可以支持“按公开字段完成协议发送端”的开发，但不足以作为“完全复刻原 LEGACY 行为”的完整技术规格。抓包已经补足外层批包、视频码流、实际帧率、Unix epoch 微秒时间戳、配置参数和命令行为等软件接口基线；固定点云槽位、物理量程、真实时间同步和坐标外参仍受传感器能力及现场标定约束。",
    29: "基于现阶段目标，新增工作属于针对原 LEGACY 的黑盒兼容逆向及等价行为验证。协议解析、主要数据通道对齐、Zenoh 1.10 命令通道整改和自动化回归已取得阶段性结果；双机冷启动、长稳、受控坐标靶标与真实外参仍属于最终验收工作。新增工作分为以下五类。",
    46: "八、当前替换判定与结论",
    47: "截至 2026 年 9 月 20 日，RK3562 已达到有条件的接口级替换状态：H.264 图像、点云、Unix epoch 微秒时间戳、setting、reboot、config_file 和 /api/device 等软件接口已经实现，Zenoh 1.10 命令通道的原 P0 阻断项已经修复。对于动态读取设备 IP/SN、只消费图像和有效点列表、且不要求 IMU 的上层软件，可进入现场联调。",
    48: "当前不能认定为所有场景下的零配置无缝替换。原 LEGACY 每帧固定 27648 个点云槽位并包含空槽，RK/XT-M60 只发送变长有效点；两者量程约为 40 至 50 米与约 12 米，软件无法弥补该硬件差异。坐标轴转换 X_LEGACY=Z_RK、Y_LEGACY=-X_RK、Z_LEGACY=Y_RK 尚缺正前、左侧、上方受控靶标确认，calibration.json 仍为 valid=false，占位外参不能用于像素级融合。若旧软件硬编码 192.0.2.55、LEGACY00000001、固定点数、远量程或 IMU，仍需修改或单独验收。",
    49: "附 当前实测结果与待验收事项",
}
for idx, value in paragraph_updates.items():
    replace_paragraph(doc.paragraphs[idx], value)

# Table 0: completed work and current evidence.
t = doc.tables[0]
set_cell(t.rows[0].cells[2], "状态/备注")
set_cell(t.rows[4].cells[2], "已完成；最终实测 30.44 Hz，1920×1080")
set_cell(t.rows[5].cells[2], "已完成；最终实测 9.97 Hz")
set_cell(t.rows[6].cells[2], "已完成；对外为 Unix epoch 微秒，图像 33333/33334 μs，点云 100000 μs")
set_cell(t.rows[7].cells[2], "已完成；Zenoh 1.10 同会话 setting 50/50，新会话 100/100，非法 reboot 10/10")
set_cell(t.rows[8].cells[2], "已完成；Zenoh 1.10 查询 10/10，平均 3.73 ms")
set_cell(t.rows[9].cells[2], "软件链路已实现；最终物理拆装记录待补")
set_cell(t.rows[10].cells[2], "最终统计窗口 invalid、queue_drop、seq_gap、timestamp_nonmono 均为 0；冷启动后长稳待补")

# Table 1: strict LEGACY/RK comparison, including the conditions that prevent unconditional replacement.
t = doc.tables[1]
set_row(t, 5, ["图像帧率", "原设备约 29.98 Hz", "RK 最终实测 30.44 Hz；通过"])
set_row(t, 6, ["点云帧率", "原设备约 10.00 Hz", "RK 最终实测 9.97 Hz；通过"])
set_row(t, 7, ["时间戳数值口径", "Unix epoch 微秒", "已对齐；协议呈现时间线稳定，但不等同于硬件 PTP 或曝光同步"])
set_row(t, 8, ["Topic/SN", "active/LEGACY00000001/{image,pointcloud}", "active/DEVICE00000001/{image,pointcloud}；为双机并存主动隔离"])
set_row(t, 9, ["配置参数", "lidar=10、image=30、H264、GOP=3、bitrate=8、imu=100", "lidar=10、image=30、H264、GOP=3、bitrate=8、imu=0；甲方确认不需要 IMU"])
set_row(t, 10, ["点云固定槽位", "每帧固定 27648 个槽位，包含空槽", "未按字节外观伪造；RK 仅发送变长有效点，最终窗口约 3200 点/帧"])
append_row(t, ["Zenoh 1.10 命令", "setting 对 Zenoh 1.10 稳定响应", "已修复；setting 50/50 与新会话 100/100，config_file 10/10"])
append_row(t, ["/api/device", "用于设备信息读取", "已实现，返回 DEVICE00000001；服务端口为 8080"])
append_row(t, ["物理量程", "历史抓包约 40 至 50 m", "XT-M60 现场约 12 m；硬件差异，软件不可补齐"])
append_row(t, ["坐标与外参", "原机坐标由旧软件既有行为约束", "轴置换已实现；左右符号和真实相机雷达外参仍待受控靶标及标定确认"])

# Table 2: add the client interoperability behavior that was only discoverable by testing.
t = doc.tables[2]
append_row(t, [
    "Zenoh queryable 客户端互通",
    "V1.0 定义 setting/reboot/config_file 的消息，不规定 Zenoh 实现、版本及 query reply 路由细节。",
    "原 LEGACY 对 Zenoh 1.10 稳定响应。RK 早期 zenoh-pico 方案超时，现已切换为 zenohd 与 zenoh-c 1.10.0。",
    "必须纳入多会话压力回归；当前 P0 已通过，但不得回退旧 zenoh-pico 网关。",
])

# Appendix table: replace every placeholder with the latest measured result or an explicit pending state.
t = doc.tables[6]
appendix = {
    1: "zenohd 1.10.0 + zenoh-c 1.10.0 + 当前 xt_gateway；H.264 30 fps、GOP 3、8 Mbps；点云 10 fps；IMU 关闭",
    2: "xt_gateway: e8751485601c84e1492e4706dc7a2d6633e6bb5935895be198c408ec4c089ac0",
    3: "H.264 30.44 Hz；点云 9.97 Hz；两路 invalid、queue_drop、seq_gap、timestamp_nonmono 均为 0",
    4: "Zenoh 1.10.0：setting 同会话 50/50，新会话 100/100；非法 reboot 10/10；config_file 10/10",
    5: "最终纯净统计窗口无协议错误或序号缺口；整板冷启动后 20 分钟以上长稳仍待复验",
    6: "不兼容固定槽位：RK 仅发送变长有效点，最终窗口约 3200 点/帧；不得宣称与 27648 槽位严格一致",
    7: "坐标轴转换已实现；左右符号待受控靶标确认；calibration.json 为 valid=false，真实相机雷达外参未完成",
    8: "设计完成：LEGACY 192.0.2.55 / LEGACY00000001；RK 203.0.113.179 / DEVICE00000001。当前 LEGACY 接回后尚未对 192.0.2.55 应答，双机最终在线复验待补",
}
for row_idx, text in appendix.items():
    set_cell(t.rows[row_idx].cells[1], text)

# Keep each comparison record together.  Header rows may still repeat when a
# table spans pages, but a single record must not split into orphan cells.
for table in doc.tables:
    for row in table.rows:
        trpr = row._tr.get_or_add_trPr()
        if trpr.find(qn("w:cantSplit")) is None:
            trpr.append(OxmlElement("w:cantSplit"))

# Normalize the new title and all heading text to black without changing the retained layout.
for paragraph in doc.paragraphs:
    text = paragraph.text.strip()
    if text and (text[0] in "一二三四五六七八九" and "、" in text[:4] or text.startswith(("2.", "3.")) or text.startswith("附 ")):
        for run in paragraph.runs:
            run.font.color.rgb = RGBColor(0, 0, 0)

OUT.parent.mkdir(parents=True, exist_ok=True)
doc.save(OUT)
print(OUT)
