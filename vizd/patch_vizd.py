# -*- coding: utf-8 -*-
p = r'C:\Users\user\Desktop\project\vizd\vizd.c'
s = open(p, encoding='utf-8').read()

# 1) 网格参数
old1 = """#define GRID_W      160
#define GRID_H      80
#define GRID_XR     40.0f   /* x 范围 ±40m */
#define GRID_YR     20.0f   /* y 范围 ±20m */"""
new1 = """#define GRID_W      160
#define GRID_H      80
#define GRID_X_MIN  0.0f    /* x 范围 0..2m (XT-M60 近距离) */
#define GRID_X_MAX  2.0f
#define GRID_Y_MIN  -1.0f   /* y 范围 ±1m */
#define GRID_Y_MAX  1.0f"""
assert s.count(old1) == 1, "p1"
s = s.replace(old1, new1)

# 2) pc_render: 开头加外层 unwrap
old2 = """static void pc_render(const uint8_t *data, size_t len) {
    PBReader r = { data, len, 0, 0 };"""
new2 = """static void pc_render(const uint8_t *data, size_t len) {
    /* 外层数组包装: 单个 field1 wire2 -> 内层 LidarPointMsg */
    if (len > 4 && data[0] == 0x0a && (data[1] & 0x80)) {
        PBReader ow = { data, len, 0, 0 };
        uint64_t t0 = pb_varint(&ow);
        if (!ow.err && (t0 >> 3) == 1 && (t0 & 7) == 2) {
            uint64_t ol = pb_varint(&ow);
            if (!ow.err && ol > 2 && ol <= len - ow.pos && data[ow.pos] == 0x0a) {
                data = data + ow.pos;
                len = (size_t)ol;
            }
        }
    }
    PBReader r = { data, len, 0, 0 };"""
assert s.count(old2) == 1, "p2"
s = s.replace(old2, new2)

# 3) 坐标映射
old3 = """                float fx = (float)x / (float)scaler;
                float fy = (float)y / (float)scaler;
                int gx = (int)((fx + GRID_XR) / (2 * GRID_XR) * GRID_W);
                int gy = (int)((fy + GRID_YR) / (2 * GRID_YR) * GRID_H);"""
new3 = """                float fx = (float)x / (float)scaler;
                float fy = (float)y / (float)scaler;
                int gx = (int)((fx - GRID_X_MIN) / (GRID_X_MAX - GRID_X_MIN) * GRID_W);
                int gy = (int)((fy - GRID_Y_MIN) / (GRID_Y_MAX - GRID_Y_MIN) * GRID_H);"""
assert s.count(old3) == 1, "p3"
s = s.replace(old3, new3)

# 4) serve_radar meta: 加范围
old4 = """    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Content-Length: %zu\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n",
        4 + 2 + 2 + 4 + sizeof(grid));
    write(cfd, hdr, hn);
    write(cfd, "XTGR", 4);
    uint8_t meta[8];
    meta[0] = (uint8_t)GRID_W; meta[1] = (uint8_t)GRID_H;
    meta[2] = (uint8_t)(seq >> 0); meta[3] = (uint8_t)(seq >> 8);
    meta[4] = (uint8_t)(seq >> 16); meta[5] = (uint8_t)(seq >> 24);
    meta[6] = 0; meta[7] = 0;
    write(cfd, meta, 8);
    write(cfd, grid, sizeof(grid));"""
new4 = """    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\\r\\nContent-Type: application/octet-stream\\r\\n"
        "Content-Length: %zu\\r\\nCache-Control: no-cache\\r\\nConnection: close\\r\\n\\r\\n",
        4 + 2 + 2 + 4 + 16 + sizeof(grid));
    write(cfd, hdr, hn);
    write(cfd, "XTGR", 4);
    uint8_t meta[8];
    meta[0] = (uint8_t)GRID_W; meta[1] = (uint8_t)GRID_H;
    meta[2] = (uint8_t)(seq >> 0); meta[3] = (uint8_t)(seq >> 8);
    meta[4] = (uint8_t)(seq >> 16); meta[5] = (uint8_t)(seq >> 24);
    meta[6] = 0; meta[7] = 0;
    write(cfd, meta, 8);
    float rng[4] = { GRID_X_MIN, GRID_X_MAX, GRID_Y_MIN, GRID_Y_MAX };
    write(cfd, rng, 16);
    write(cfd, grid, sizeof(grid));"""
assert s.count(old4) == 1, "p4"
s = s.replace(old4, new4)

open(p, 'w', encoding='utf-8', newline='').write(s)
print("VIZD_PATCH_OK")
