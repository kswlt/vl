# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\vizd\vizd.c'
s = open(p, encoding='utf-8').read()

old = '''    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Content-Length: %zu\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n",
        4 + 2 + 2 + 4 + 16 + sizeof(grid));'''
new = '''    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Access-Control-Allow-Origin: *\\r\\n"
        "Content-Length: %zu\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n",
        4 + 2 + 2 + 4 + 16 + sizeof(grid));'''
assert s.count(old) == 1, "cors-radar not found"
s = s.replace(old, new)

old2 = '''        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Content-Length: 8\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n"'''
new2 = '''        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Access-Control-Allow-Origin: *\\r\\n"
        "Content-Length: 8\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n"'''
assert s.count(old2) == 1, "cors-nodata not found"
s = s.replace(old2, new2)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("CORS_PATCH_OK")
