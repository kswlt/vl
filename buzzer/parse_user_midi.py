#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Parse user MIDI, extract melody notes, emit note table for buzzer."""
import mido, sys

path = r"C:\Users\user\Downloads\9月22日.mid"
mid = mido.MidiFile(path)
print("tracks:", len(mid.tracks), "type:", mid.type, "tpb:", mid.ticks_per_beat)
if hasattr(mid, 'length'):
    print("length:", mid.length)

def note_name(n):
    names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
    return f"{names[n%12]}{n//12-1}"

# per-track note events with absolute ticks
for ti, track in enumerate(mid.tracks):
    t = 0
    notes = []
    for msg in track:
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            notes.append({'n': msg.note, 't': t, 'v': msg.velocity, 'end': None})
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            for op in reversed(notes):
                if op['n'] == msg.note and op['end'] is None:
                    op['end'] = t
                    break
    for op in notes:
        if op['end'] is None:
            op['end'] = op['t'] + 100
    if not notes:
        continue
    ns = [x['n'] for x in notes]
    print(f"track {ti}: {len(notes)} notes, pitch {min(ns)}-{max(ns)} "
          f"({note_name(min(ns))}-{note_name(max(ns))}), "
          f"time {min(x['t'] for x in notes)/mid.ticks_per_beat:.1f}-"
          f"{max(x['end'] for x in notes)/mid.ticks_per_beat:.1f} beats")
