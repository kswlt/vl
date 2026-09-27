#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Zoomed spectrograms of each energy segment."""
import numpy as np
import wave
from scipy import signal
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

path = r"C:\Users\user\Desktop\project\buzzer\dji_rec.wav"
w = wave.open(path, 'rb')
sr = w.getframerate()
nch = w.getnchannels()
raw = w.readframes(w.getnframes())
w.close()
data = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
if nch > 1:
    data = data.reshape(-1, nch).mean(axis=1)

segs = [(0.0, 2.2), (5.5, 9.0), (9.0, 12.5), (13.3, 16.5)]
fig, axes = plt.subplots(4, 1, figsize=(14, 12))
for ax, (t0, t1) in zip(axes, segs):
    i0, i1 = int(t0*sr), int(t1*sr)
    seg = data[i0:i1]
    f, t, Sxx = signal.spectrogram(seg, sr, nperseg=2048, noverlap=1024, nfft=4096)
    Sdb = 10*np.log10(Sxx + 1e-12)
    im = ax.pcolormesh(t+t0, f, Sdb, shading='gouraud', vmin=-45, vmax=-5, cmap='inferno')
    ax.set_ylim(0, 1600)
    ax.set_xlim(t0, t1)
    ax.set_ylabel('Hz')
    ax.set_title(f'{t0:.1f}-{t1:.1f}s')
    fig.colorbar(im, ax=ax, fraction=0.03)
fig.tight_layout()
fig.savefig(r"C:\Users\user\Desktop\project\buzzer\dji_zoom.png", dpi=90)
print("saved dji_zoom.png")
