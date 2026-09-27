#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Parse Keil keygen MIDI, extract melody notes (square-wave lead tracks)
and emit a note table for the buzzer player."""
import mido, sys, json

path = r"C:\Users\user\Desktop\project\buzzer\keil.mid"
mid = mido.MidiFile(path)
print("tracks:", len(mid.tracks), "type:", mid.type)

def note_name(n):
    return mido.note2name(n)

# collect note events with absolute ticks per track
all_notes = []
for ti, track in enumerate(mid.tracks):
    t = 0
    notes = []
    for msg in track:
        t += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            notes.append({'n': msg.note, 't': t, 'v': msg.velocity})
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            # close matching open note
            for op in notes:
                if op['n'] == msg.note and 'end' not in op:
                    op['end'] = t
                    break
    opened = [x for x in notes if 'end' not in x]
    for x in opened:
        x['end'] = x['t'] + 120  # assume short
    notes = [x for x in notes if 'end' in x]
    if notes:
        all_notes.append((ti, notes))

tpb = mid.ticks_per_beat
print("ticks_per_beat:", tpb)

# Dump per-track summaries (count, note range, time range)
for ti, notes in all_notes:
    ns = [x['n'] for x in notes]
    ts = [x['t'] for x in notes]
    te = [x['end'] for x in notes]
    print(f"track {ti}: {len(notes)} notes, pitch {min(ns)}-{max(ns)} "
          f"({note_name(min(ns))}-{note_name(max(ns))}), "
          f"time {min(ts)/tpb:.1f}-{max(te)/tpb:.1f} beats")
