#!/usr/bin/env python3
# img2seq.py — 图片 → 蜂鸣器音符序列（声音传真编码器）
# 编码：灰度 48x15，亮度 0-255 -> 频率 1500-2300Hz，每像素 25ms
#       每行前导同步音 1200Hz 40ms；行尾 8ms 静音分隔
# 输出：文本序列，每行 "freq dur_ms"（freq=0 表示静音/休止）
import sys
from PIL import Image

SRC = r"C:\Users\user\Pictures\Screenshots\屏幕截图 2025-01-09 170026.png"
OUT = r"C:\Users\user\Desktop\project\buzzer\img_seq.txt"
W, H = 32, 20          # 网格分辨率（接近原图 1.57:1）
PIX_MS = 25            # 每像素时长
SYNC_F = 1200          # 行同步频率
SYNC_MS = 40
GAP_MS = 8             # 行间静音
F_MIN, F_MAX = 1500, 2300

def main():
    img = Image.open(SRC).convert("L")
    img = img.resize((W, H), Image.LANCZOS)
    notes = []
    for y in range(H):
        notes.append((SYNC_F, SYNC_MS))
        for x in range(W):
            g = img.getpixel((x, y))
            f = int(F_MIN + g * (F_MAX - F_MIN) / 255)
            notes.append((f, PIX_MS))
        notes.append((0, GAP_MS))
    with open(OUT, "w") as fp:
        for f, d in notes:
            fp.write("%d %d\n" % (f, d))
    total_s = sum(d for _, d in notes) / 1000.0
    print("notes=%d  total=%.1fs  grid=%dx%d  f=%d-%dHz pix=%dms" %
          (len(notes), total_s, W, H, F_MIN, F_MAX, PIX_MS))
    # 灰度采样打印（调试）
    for y in range(H):
        row = "".join(" .:-=+*#%@"[min(9, img.getpixel((x, y)) * 10 // 256)] for x in range(W))
        print("%02d %s" % (y, row))

if __name__ == "__main__":
    main()
