from pathlib import Path


root = Path(__file__).resolve().parents[1]
html = (root / "board/web/index.html").read_bytes()
delimiter = b"XTWEB"
assert b")" + delimiter + b'"' not in html
header = (
    b"#pragma once\n"
    b"// Generated from board/web/index.html by scripts/embed_web.py.\n"
    b"static const char kEmbeddedWeb[] = R\""
    + delimiter
    + b"("
    + html
    + b")"
    + delimiter
    + b'\";\n'
)
(root / "board/src/embedded_web.hpp").write_bytes(header)
