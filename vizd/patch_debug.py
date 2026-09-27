# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\vizd\vizd4.c'
s = open(p, encoding='utf-8').read()
old = "static volatile int g_debug = 1;"
new = "static volatile int g_debug = 0;"
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("DEBUG_OFF_OK")
