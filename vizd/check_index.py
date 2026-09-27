# -*- coding: utf-8 -*-
s = open(r'C:\Users\user\Desktop\project\board\web\index.html', encoding='utf-8').read()
print('len=', len(s))
checks = ['id="live"', 'camLive', 'radarTick', 'liveOn', 'radarTimer', '</script>', '</body>', '8081']
for c in checks:
    print(c, '->', s.count(c))
