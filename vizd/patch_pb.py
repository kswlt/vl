# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\vizd\vizd4.c'
s = open(p, encoding='utf-8').read()

old1 = """            PBReader h = { r.d, hl, 0, 0 };
            r.pos += hl;"""
new1 = """            PBReader h = { r.d + r.pos, hl, 0, 0 };
            r.pos += hl;"""
assert s.count(old1) == 1, "h-bug: %d" % s.count(old1)
s = s.replace(old1, new1)

old2 = """            PBReader p = { r.d, (size_t)pl, 0, 0 };
            r.pos += pl;"""
new2 = """            PBReader p = { r.d + r.pos, (size_t)pl, 0, 0 };
            r.pos += pl;"""
assert s.count(old2) == 1, "p-bug: %d" % s.count(old2)
s = s.replace(old2, new2)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("PB_FIX_OK")
