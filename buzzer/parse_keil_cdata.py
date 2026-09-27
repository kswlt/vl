#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
parse_keil_cdata.py — 解析 keil_cracker_music_stm32f103 的 cdata.c
输出：4 通道完整音符序列（start_ms, dur_ms, midi_pitch）→ 合并主旋律
行速: speed=4, sample_tick=20ms → 每行 80ms；每 pattern 64 行
"""
import re, sys, json

SRC = r"C:\Users\user\Desktop\project\buzzer\keil_cracker_src\keil_cracker_music_stm32f103-main\Core\Src\player\cdata.c"
LINE_MS = 80      # speed=4 × 20ms
PAT_LINES = 64

text = open(SRC, encoding="utf-8").read()

# --- 提取命名数组 ---
arrays = {}
for m in re.finditer(r'static\s+const\s+uint32_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};', text, re.S):
    name, body = m.group(1), m.group(2)
    vals = []
    for t in re.finditer(r'CMD_T\((\d+),NOTE_(\w+)\((\d+)\),0x(\w+)\)|CMD_FX_JMP_T\((\d+),(\d+)\)', body):
        if t.group(1) is not None:
            line, note, octv, samp = int(t.group(1)), t.group(2), int(t.group(3)), int(t.group(4), 16)
            semi = "C CS D DS E F FS G GS A AS B".split().index(note)
            pitch = 12 * (octv + 1) + semi
            vals.append((line, pitch, samp, 0))
        else:
            vals.append((int(t.group(5)), 0, 0, int(t.group(6))))  # jmp
    arrays[name] = vals

# pattern idx 顺序（十进制或 0x）
m = re.search(r'cdata_pattern_idxs\[\d+\]\s*=\s*\{(.*?)\};', text, re.S)
body = m.group(1) if m else ""
pat_order = [int(x, 0) for x in re.findall(r'\b(?:0x[\da-fA-F]+|\d+)\b', body)] if m else []
print("pattern order:", len(pat_order), pat_order)

# 组 pattern: cmd_{p}_{ch}
patterns = {}
for name, vals in arrays.items():
    mm = re.match(r'cmd_(\d+)_(\d+)$', name)
    if mm:
        p, ch = int(mm.group(1)), int(mm.group(2))
        patterns.setdefault(p, {})[ch] = vals
npats = max(patterns.keys()) + 1
print("patterns:", npats)

# --- 按顺序展开时间线 ---
def note_off(pitch, samp):
    pass

tracks = {c: [] for c in range(4)}   # (start_ms, dur_ms, pitch)
cur_pitch = [None]*4
cur_start = [0]*4
pos = 0            # pattern 序号（在 pat_order 中）
line = 0
time_ms = 0
step = 0
while step < 3000:   # 上限保护
    pat = pat_order[pos] if pos < len(pat_order) else None
    if pat is None:
        break
    p = patterns.get(pat)
    if p is None:
        break
    # 本行命令
    for ch in range(4):
        for (ln, pitch, samp, jmp) in p.get(ch, []):
            if ln == line:
                if jmp:
                    # JMP: pos+1, line=jmp_line
                    pass
                else:
                    # 收尾旧音
                    if cur_pitch[ch] is not None:
                        tracks[ch].append((cur_start[ch], time_ms - cur_start[ch], cur_pitch[ch]))
                    cur_pitch[ch] = pitch
                    cur_start[ch] = time_ms
    # 找 jmp（任意通道）
    jmp_target = None
    for ch in range(4):
        for (ln, pitch, samp, jmp) in p.get(ch, []):
            if ln == line and jmp:
                jmp_target = jmp
    time_ms += LINE_MS
    line += 1
    if line >= PAT_LINES or jmp_target is not None:
        pos += 1
        line = jmp_target if jmp_target is not None else 0
        if pos >= len(pat_order):
            # 循环回 0
            pos = 0
            # 收尾所有音
            for ch in range(4):
                if cur_pitch[ch] is not None:
                    tracks[ch].append((cur_start[ch], time_ms - cur_start[ch], cur_pitch[ch]))
                    cur_pitch[ch] = None
    step += 1

# 收尾
for ch in range(4):
    if cur_pitch[ch] is not None:
        tracks[ch].append((cur_start[ch], time_ms - cur_start[ch], cur_pitch[ch]))

# 输出每通道统计 + 合并最高音
print("\ntotal time: %.1fs" % (time_ms/1000))
for ch in range(4):
    ts = tracks[ch]
    if ts:
        notes = len(ts)
        pitches = [t[2] for t in ts]
        print("ch%d: %d notes, pitch range %d..%d (%.0f..%.0fHz)" % (
            ch, notes, min(pitches), max(pitches),
            440*2**((min(pitches)-69)/12), 440*2**((max(pitches)-69)/12)))

# 合并：每个时间窗口取最高音（同刻多音）
merged = []
for t0 in sorted(set(t[0] for ch in range(4) for t in tracks[ch])):
    active = []
    for ch in range(4):
        for (s, d, p) in tracks[ch]:
            if s <= t0 < s + d:
                active.append(p)
    if active:
        merged.append((t0, max(active)))
# 简化：转成 (start, dur, pitch)
seq = []
for i, (t0, p) in enumerate(merged):
    if seq and seq[-1][2] == p:
        seq[-1] = (seq[-1][0], t0 + LINE_MS - seq[-1][0], p)
    else:
        seq.append((t0, LINE_MS, p))
print("\nmerged highest-pitch: %d notes, %.1fs" % (len(seq), time_ms/1000))

# 输出 C 数组（midi 编号, dur_ms）
with open(r"C:\Users\user\Desktop\project\buzzer\keil_merged.txt", "w", encoding="utf-8") as f:
    for (s, d, p) in seq:
        f.write("{%d,%d},\n" % (p, d))
print("wrote keil_merged.txt")
