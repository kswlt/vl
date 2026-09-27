#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Analyze DJI boot-chime recording: frame-by-frame pitch detection -> note sequence."""
import numpy as np
import wave

path = r"C:\Users\user\Desktop\project\buzzer\dji_rec.wav"
w = wave.open(path, 'rb')
sr = w.getframerate()
nch = w.getnchannels()
sw = w.getsampwidth()
nframes = w.getnframes()
raw = w.readframes(nframes)
w.close()
data = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
if nch > 1:
    data = data.reshape(-1, nch).mean(axis=1)
print(f"sr={sr} n={len(data)} dur={len(data)/sr:.2f}s")

def pitch_frame(x, sr):
    """Autocorrelation pitch detection for a voiced frame."""
    x = x - x.mean()
    rms = np.sqrt((x**2).mean())
    if rms < 200:
        return 0.0
    # autocorrelation
    n = len(x)
    corr = np.correlate(x, x, mode='full')[n-1:]
    # find first peak after min lag
    min_lag = int(sr / 3000)   # max freq 3000 Hz
    max_lag = int(sr / 200)    # min freq 200 Hz
    seg = corr[min_lag:max_lag]
    if len(seg) < 2 or seg.max() < corr[0]*0.3:
        return 0.0
    lag = min_lag + int(np.argmax(seg))
    return sr / lag

frame_ms = 20
hop_ms = 10
fstep = int(sr * hop_ms / 1000)
flen = int(sr * frame_ms / 1000)
notes = []   # (start_s, end_s, freq)
cur = None
for start in range(0, len(data) - flen, fstep):
    x = data[start:start+flen]
    f = pitch_frame(x, sr)
    t = start / sr
    if f > 0:
        if cur and abs(np.log2(cur[2]/f)) < 0.06 and t - cur[1] < 0.4:
            cur = (cur[0], t + frame_ms/1000, f)
        else:
            if cur:
                notes.append(cur)
            cur = (t, t + frame_ms/1000, f)
    else:
        if cur and t - cur[1] > 0.12:
            notes.append(cur)
            cur = None
if cur:
    notes.append(cur)

def midi_note(f):
    return 69 + 12*np.log2(f/440.0)

print("\nDetected notes (start_s, end_s, freq_Hz, midi, name):")
for s, e, f in notes:
    if f <= 0: continue
    m = midi_note(f)
    nm = "C D EF G A B"[int(round(m)) % 12 - 3 if int(round(m)) % 12 >= 3 else int(round(m)) % 12 + 9]
    # better: proper name
    names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
    pc = int(round(m)) % 12
    octv = int(round(m)) // 12 - 1
    nm = f"{names[pc]}{octv}"
    print(f"  {s:6.3f} {e:6.3f} dur={e-s:5.3f} f={f:7.1f} midi={m:6.2f} {nm}")
