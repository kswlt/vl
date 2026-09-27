#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Spectrogram of DJI boot recording, focus 0-2000 Hz, save PNG."""
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

f, t, Sxx = signal.spectrogram(data, sr, nperseg=1024, noverlap=512, nfft=2048)
Sdb = 10*np.log10(Sxx + 1e-12)

fig, ax = plt.subplots(figsize=(16, 6))
im = ax.pcolormesh(t, f, Sdb, shading='gouraud', vmin=-40, vmax=0, cmap='inferno')
ax.set_ylim(0, 2000)
ax.set_xlabel('time (s)')
ax.set_ylabel('freq (Hz)')
ax.set_title('DJI boot recording spectrogram 0-2000Hz')
cb = fig.colorbar(im, ax=ax)
cb.set_label('dB')
fig.tight_layout()
fig.savefig(r"C:\Users\user\Desktop\project\buzzer\dji_spec.png", dpi=100)
print("saved dji_spec.png")
