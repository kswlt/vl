#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Full-range low-threshold note detection to find the DJI drone boot chime."""
import numpy as np
import wave
from scipy import signal

path = r"C:\Users\user\Desktop\project\buzzer\dji_rec.wav"
w = wave.open(path, 'rb')
sr = w.getframerate()
nch = w.getnchannels()
raw = w.readframes(w.getnframes())
w.close()
data = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
if nch > 1:
    data = data.reshape(-1, nch).mean(axis=1)

sos = signal.butter(6, [100/(sr/2), 1800/(sr/2)], btype='band', output='sos')
x = signal.sosfiltfilt(sos, data)

def pitch(x, sr):
    x = x - x.mean()
    rms = np.sqrt((x**2).mean())
    if rms < 60:
        return 0.0, rms
    n = len(x)
    win = np.hanning(n) * x
    spec = np.fft.rfft(win)
    freqs = np.fft.rfftfreq(n, 1/sr)
    mag = np.abs(spec)
    lo, hi = np.searchsorted(freqs, [100, 1800])
    idx = lo + np.argmax(mag[lo:hi])
    return freqs[idx], rms

names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def nm(f):
    m = 69 + 12*np.log2(f/440.0)
    mm = int(round(m))
    return f"{names[mm%12]}{mm//12-1}"

flen = int(sr*0.06)
hop = int(sr*0.03)
notes = []
cur = None
for st in range(0, len(x)-flen, hop):
    fr, rms = pitch(x[st:st+flen], sr)
    t = st/sr
    if fr > 0:
        if cur and abs(np.log2(cur[2]/fr)) < 0.06 and t - cur[1] < 0.6:
            cur = (cur[0], t, fr)
        else:
            if cur: notes.append(cur)
            cur = (t, t, fr)
    else:
        if cur and t - cur[1] > 0.15:
            notes.append(cur); cur = None
if cur: notes.append(cur)

print("All notes (start, end, dur, freq, name):")
for s, e, f in notes:
    print(f"  {s:7.3f} {e:7.3f} dur={e-s:5.2f} f={f:7.1f} {nm(f)}")
