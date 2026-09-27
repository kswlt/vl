# -*- coding: utf-8 -*-
"""Local same-version wildcard queryable test (no device)."""
import threading
import time
import zenoh

ready = threading.Event()

def server():
    with zenoh.open(zenoh.Config()) as s:
        def handler(query):
            query.reply(str(query.key_expr), b"pong")
        s.declare_queryable("test/xxx/cmd/*", handler)
        ready.set()
        time.sleep(8)

t = threading.Thread(target=server, daemon=True)
t.start()
ready.wait(3)
with zenoh.open(zenoh.Config()) as c:
    for key in ("test/xxx/cmd/setting", "test/xxx/cmd/*"):
        try:
            replies = list(c.get(key, payload=b"", timeout=3))
            out = [( "OK" if r.ok else "ERR", (r.ok.payload.to_bytes() if r.ok else bytes(r.err.payload.to_bytes())).decode(errors="replace")) for r in replies]
            print(f"LOCAL get {key}: {out}")
        except Exception as e:
            print(f"LOCAL get {key}: EXC {e}")
