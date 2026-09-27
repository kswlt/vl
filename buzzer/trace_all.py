#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Full recording fine pitch trace, low threshold, to read the real melody."""
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

sos = signal.butter(6, [100/(sr/2), 2000/(sr/2)], btype='band', output='sos')
x = signal.sosfiltfilt(sos, data)

def pitch(x, sr):
    x = x - x.mean()
    rms = np.sqrt((x**2).mean())
    if rms < 50:
        return 0.0, rms
    n = len(x)
    win = np.hanning(n) * x
    spec = np.fft.rfft(win)
    freqs = np.fft.rfftfreq(n, 1/sr)
    mag = np.abs(spec)
    lo, hi = np.searchsorted(freqs, [100, 2000])
    idx = lo + np.argmax(mag[lo:hi])
    return freqs[idx], rms

names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def nm(f):
    m = 69 + 12*np.log2(f/440.0)
    mm = int(round(m))
    return f"{names[mm%12]}{mm//12-1}"

flen = int(sr*0.05)
hop = int(sr*0.025)
# energy gate: print RMS too so quiet segments are visible
for st in range(0, len(x)-flen, hop):
    fr, rms = pitch(x[st:st+flen], sr)
    t = st/sr
    tag = " "
    if fr > 0:
        tag = "*"
        print(f"{t:6.3f} {fr:7.1f} {nm(fr):>4} rms={rms:7.0f} {tag}")
    else:
        print(f"{t:6.3f}  -----   .    rms={rms:7.0f}")
