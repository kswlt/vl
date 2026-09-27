#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
midi2buzz.py — MIDI -> 单音蜂鸣器音符序列 (8-bit 单音风格)

逻辑：
  1. 自动挑选主旋律轨：轨名含 melody/lead/vocal/voice/vox/主旋律/人声/旋律 优先；
     否则取“非打击乐轨中音符数最多”的轨。
  2. 删除打击乐轨（channel 9/10）。
  3. 同一时刻多个音符：只保留最高音（或 --lowest 保留最低音）。
  4. 音符重叠合并：后音在前音结束前开始 → 截断前音（避免和声/粘连）。
  5. 时值量化到 5ms 网格；短于 --min-ms 的音符钳到 min-ms。
  6. 可选 --transpose 移调、--max-freq/--min-freq 音域钳制。
  7. 输出 C 语言 struct MNote 数组（midi 号 + 时长），可直接贴进 buzzer_play.c。

用法:
  python midi2buzz.py in.mid [-o out.txt] [--transpose 0] [--min-ms 80]
         [--max-freq 2093] [--min-freq 262] [--lowest]
依赖: pip install mido
"""
import sys, argparse, math
import mido

PERCUSSIONS = {9, 10}          # GM 打击乐通道
MELODY_HINT = ("melody", "lead", "vocal", "voice", "vox", "主旋律", "人声", "旋律", "solo")

def midi_to_freq(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)

def pick_track(mid):
    """返回 (track, reason)。先按名字找主旋律轨，再按音符数找。"""
    candidates = []
    for ti, tr in enumerate(mid.tracks):
        name = ""
        for msg in tr:
            if msg.type == "track_name":
                name = msg.name
                break
        notes = [m for m in tr if m.type == "note_on" and m.velocity > 0]
        if not notes:
            continue
        # 打击乐通道过滤
        if any(m.channel in PERCUSSIONS for m in notes):
            continue
        candidates.append((ti, tr, name, len(notes)))
    if not candidates:
        raise RuntimeError("no melodic track found")
    for ti, tr, name, n in candidates:
        low = name.lower()
        if any(h in low for h in MELODY_HINT):
            return tr, f"name-match '{name}'"
    return max(candidates, key=lambda c: c[3])[1], "most-notes"

def quantize_ms(ticks, tpq, ms_per_tick):
    return int(round(ticks * ms_per_tick / 5.0)) * 5

def extract_notes(mid, track, transpose, min_ms, min_freq, max_freq, take_highest):
    tpq = mid.ticks_per_beat
    tempo = 500000  # 默认 120BPM；遇到 set_tempo 更新
    ms_per_tick = tempo / 1000.0 / tpq
    # 先扫 tempo
    for tr in mid.tracks:
        for msg in tr:
            if msg.type == "set_tempo":
                tempo = msg.tempo
                ms_per_tick = tempo / 1000.0 / tpq

    active = {}      # pitch -> (start_tick, velocity)
    events = []      # (start_tick, end_tick, pitch)
    tick = 0
    for msg in track:
        tick += msg.time
        if msg.type == "note_on" and msg.velocity > 0 and msg.channel not in PERCUSSIONS:
            active[msg.note] = (tick, msg.velocity)
        elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
            key = msg.note
            if key in active:
                st, vel = active.pop(key)
                events.append((st, tick, key))

    if take_highest:
        # 同刻重叠音符 → 保留最高音：按 start 排序，重叠时截断低音
        events.sort(key=lambda e: (e[0], -e[2]))
    else:
        events.sort(key=lambda e: (e[0], e[2]))

    merged = []
    for st, en, pitch in events:
        pitch = pitch + transpose
        f = midi_to_freq(pitch)
        if f < min_freq or f > max_freq:
            continue
        d = quantize_ms(en - st, tpq, ms_per_tick)
        if d < min_ms:
            d = min_ms
        merged.append((st, d, pitch))

    # 同一时刻多音（和弦）→ 只保留一个音（默认最高音）
    by_start = {}
    for st, d, pitch in merged:
        if st not in by_start:
            by_start[st] = (st, d, pitch)
        else:
            cur = by_start[st]
            if (take_highest and pitch > cur[2]) or (not take_highest and pitch < cur[2]):
                by_start[st] = (st, d, pitch)
    ordered = sorted(by_start.values(), key=lambda e: e[0])

    # 重叠截断：后音在前音结束前开始 → 压缩前一音符（避免和声粘连）
    out = []
    for st, d, pitch in ordered:
        if out and st < out[-1][0] + out[-1][1]:
            out[-1] = (out[-1][0], max(0, st - out[-1][0]), out[-1][2])
        out.append((st, d, pitch))
    out = [e for e in out if e[1] > 0]
    return out

def main():
    ap = argparse.ArgumentParser(description="MIDI -> buzzer notes")
    ap.add_argument("midi")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--transpose", type=int, default=0)
    ap.add_argument("--min-ms", type=int, default=80)
    ap.add_argument("--min-freq", type=int, default=262)
    ap.add_argument("--max-freq", type=int, default=2093)
    ap.add_argument("--lowest", action="store_true")
    args = ap.parse_args()

    mid = mido.MidiFile(args.midi)
    track, reason = pick_track(mid)
    notes = extract_notes(mid, track, args.transpose, args.min_ms,
                          args.min_freq, args.max_freq, not args.lowest)

    lines = []
    lines.append(f"/* {args.midi}  track={reason}  notes={len(notes)} */")
    for st, d, p in notes:
        lines.append(f"    {{{p},{d}}},")
    text = "\n".join(lines) + "\n"

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"wrote {len(notes)} notes -> {args.out}  ({reason})")
    else:
        print(text)

if __name__ == "__main__":
    main()
