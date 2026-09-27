# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\board\src\xt_radar.cpp'
s = open(p, encoding='utf-8').read()

old1 = """        if (g_ingest_only) {
            const int result = send_pointcloud_payload(job.data);
            if (result != Z_OK) ++g_publish_errors;
            continue;
        }"""
new1 = """        if (g_ingest_only) {
            const int result = send_pointcloud_payload(job.data);
            if (result != Z_OK) ++g_publish_errors;
            if (job.pointcloud) pc_stream_send(job.seq, job.data);
            continue;
        }"""
assert s.count(old1) == 1, "old1 count=%d" % s.count(old1)
s = s.replace(old1, new1)

old2 = 'printf("[radar] acquisition-only mode; gateway handoff=%s\\n", POINTCLOUD_INGEST_SOCKET);'
new2 = old2 + '\n        pc_stream_init();'
assert s.count(old2) == 1, "old2 count=%d" % s.count(old2)
s = s.replace(old2, new2)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("PATCH_OK")
