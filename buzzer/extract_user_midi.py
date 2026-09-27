#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Extract full note list from user MIDI and emit buzzer C table."""
import mido

path = r"C:\Users\user\Downloads\9月22日.mid"
mid = mido.MidiFile(path)
tpb = mid.ticks_per_beat

names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def nm(n):
    return f"{names[n%12]}{n//12-1}"

def freq_of(n):
    return 440.0 * 2**((n-69)/12)

all_notes = []
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
    for op in notes:
        op['track'] = ti
        all_notes.append(op)

# sort by onset
all_notes.sort(key=lambda x: (x['t'], x['n']))
print("total notes:", len(all_notes))
for x in all_notes:
    t0 = x['t']/tpb
    d0 = (x['end']-x['t'])/tpb
    f = freq_of(x['n'])
    print(f"trk{x['track']} t={t0:6.3f}s dur={d0:5.3f}s note={x['n']:3d} {nm(x['n']):>4} f={f:7.1f}Hz v={x['v']}")
