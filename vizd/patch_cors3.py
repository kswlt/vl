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
assert s.count(old) == 1, "serve_radar template not found: %d" % s.count(old)
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("CORS_MAIN_OK; total Access-Control now:", s.count("Access-Control-Allow-Origin"))
