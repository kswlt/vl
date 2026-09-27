#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Precise note extraction with bandpass + harmonic suppression.
Focus on 5.5-12.5s (DJI boot music) and 0-2s, 13.3-16.5s."""
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

# bandpass 180-1500 Hz
sos = signal.butter(6, [180/(sr/2), 1500/(sr/2)], btype='band', output='sos')
x = signal.sosfiltfilt(sos, data)

def pitch(x, sr):
    x = x - x.mean()
    rms = np.sqrt((x**2).mean())
    if rms < 150:
        return 0.0, rms
    n = len(x)
    win = np.hanning(n) * x
    spec = np.fft.rfft(win)
    freqs = np.fft.rfftfreq(n, 1/sr)
    mag = np.abs(spec)
    # keep 180-1500
    lo, hi = np.searchsorted(freqs, [180, 1500])
    idx = lo + np.argmax(mag[lo:hi])
    f0 = freqs[idx]
    # harmonic check: energy at f0 should be max
    return f0, rms

def extract(t0, t1, label):
    i0, i1 = int(t0*sr), int(t1*sr)
    seg = x[i0:i1]
    flen = int(sr*0.05)
    hop = int(sr*0.025)
    notes = []
    cur = None
    for st in range(0, len(seg)-flen, hop):
        fr, rms = pitch(seg[st:st+flen], sr)
        t = t0 + st/sr
        if fr > 0:
            if cur and abs(np.log2(cur[2]/fr)) < 0.05 and t - cur[1] < 0.5:
                cur = (cur[0], t, fr)
            else:
                if cur: notes.append(cur)
                cur = (t, t, fr)
        else:
            if cur and t - cur[1] > 0.15:
                notes.append(cur); cur = None
    if cur: notes.append(cur)
    print(f"\n=== {label} ({t0}-{t1}s) ===")
    names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
    seq = []
    for s, e, f in notes:
        m = 69 + 12*np.log2(f/440.0)
        mm = int(round(m))
        pc = mm % 12; octv = mm // 12 - 1
        nm = f"{names[pc]}{octv}"
        dur = e - s
        seq.append(nm)
        print(f"  {s:7.3f} {e:7.3f} dur={dur:5.2f} f={f:7.1f} {nm} (midi {m:5.1f})")
    print("  SEQ:", " ".join(seq))

extract(0.0, 2.2, "seg1")
extract(5.5, 9.0, "seg2")
extract(9.0, 12.6, "seg3")
extract(13.3, 16.5, "seg4")
