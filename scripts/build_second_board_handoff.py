"""Build a self-verifying second-board deployment handoff ZIP.

No runtime secrets, factory identity, calibration, or historical packet captures
are imported from the live board.  Source and documentation are workspace files.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output"
BASE = "RK3562_第二台板部署交接"
STAGE = OUT / "rk3562_second_board_handoff"
ZIP = OUT / f"{BASE}.zip"
DOC = OUT / f"{BASE}.md"

SOURCE_DIRS = ("board", "pc", "protocol", "config", "tests", "tools", "vendor", "vizd", "buzzer", "docs", "scripts")
SOURCE_FILES = (
    "README.md", "HANDOFF.md", "CODEX_AUDIT.md", "requirements.txt", "requirements-dev.txt",
    "setup_pc.ps1", "start_gateway.ps1", "acceptance_tool.py",
    "交叉编译教程.md", "第一周研发进.docx", "第一周研发进.pdf", "第三周计划.docx",
    "4周研发计划docx.docx", "4周研发计划docx.pdf",
    "原设备协议对照及新增兼容逆向工作说明_补充完善版.docx",
)
SKIP_DIRS = {"__pycache__", ".git", ".venv", ".venv_rk", ".pytest_cache", "node_modules"}
SKIP_SUFFIXES = {".pyc", ".pyo", ".log", ".pcap", ".pcapng"}


def add_tree(files: dict[str, Path], path: Path, prefix: str) -> None:
    if not path.exists():
        return
    for f in sorted(path.rglob("*")):
        if not f.is_file() or any(x in SKIP_DIRS for x in f.relative_to(path).parts) or f.suffix.lower() in SKIP_SUFFIXES:
            continue
        files[prefix + "/" + f.relative_to(path).as_posix()] = f


def add_file(files: dict[str, Path], arc: str, path: Path) -> None:
    if path.is_file():
        files[arc] = path


def run(cmd: list[str], path: Path) -> bytes:
    return subprocess.check_output(cmd, cwd=path, stderr=subprocess.DEVNULL)


VERIFY = '''#!/usr/bin/env python3
"""Verify the extracted handoff package against MANIFEST.sha256."""
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parent
failed = []
lines = (root / "MANIFEST.sha256").read_text(encoding="utf-8").splitlines()
for line in lines:
    expected, name = line.split("  ", 1)
    path = root / name
    if not path.is_file():
        failed.append(f"MISSING {name}")
        continue
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    if h.hexdigest() != expected:
        failed.append(f"MISMATCH {name}")
print(f"checked {len(lines)} files; failures={len(failed)}")
for item in failed: print(item)
raise SystemExit(1 if failed else 0)
'''


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    STAGE.mkdir(parents=True, exist_ok=True)
    assert DOC.is_file(), DOC
    assert (STAGE / "live_release" / "SOURCE_MANIFEST.json").is_file(), "capture live release first"
    files: dict[str, Path] = {}
    add_file(files, "HANDOFF.md", DOC)
    add_tree(files, STAGE / "live_release", "live_release")
    add_tree(files, STAGE / "templates", "templates")
    for name in SOURCE_DIRS:
        add_tree(files, ROOT / name, "source/" + name)
    for name in SOURCE_FILES:
        add_file(files, "source/" + name, ROOT / name)
    # Root-level handoffs and dated acceptance/iteration reports are part of
    # the development record, even when superseded by the new HANDOFF.md.
    for path in sorted(ROOT.glob("*.md")):
        add_file(files, "source/" + path.name, path)
    add_tree(files, ROOT / "交叉编译教程", "manuals/交叉编译教程")
    review = Path(r"C:\Users\user\Documents\xwechat_files\wechat_account\msg\file\2026-09\原设备协议对照及新增兼容逆向工作说明_评审批注.docx")
    kit = Path(r"C:\Users\user\Documents\xwechat_files\wechat_account\msg\file\2026-09\supplier_acceptance_kit_5.0.0(验证测试程序).zip")
    add_file(files, "references/protocol/" + review.name, review)
    add_file(files, "references/acceptance/" + kit.name, kit)
    zenoh = ROOT / "tmp" / "zenohc-1.10.0" / "libzenohc-1.10.0-aarch64-unknown-linux-gnu-debian.zip"
    add_file(files, "references/zenohc/" + zenoh.name, zenoh)
    add_file(files, "history/project-history.bundle", OUT / "project-history.bundle")

    (STAGE / "git_status.txt").write_bytes(run(["git", "status", "--short"], ROOT))
    (STAGE / "git_log.txt").write_bytes(run(["git", "log", "-20", "--date=iso", "--pretty=format:%h %ad %s"], ROOT))
    (STAGE / "git_head.txt").write_bytes(run(["git", "rev-parse", "HEAD"], ROOT))
    for name in ("git_status.txt", "git_log.txt", "git_head.txt"):
        add_file(files, "history/" + name, STAGE / name)

    required = ["HANDOFF.md", "live_release/app/xt_gateway", "live_release/app/xt_camera", "live_release/app/xt_radar", "live_release/app/vizd", "source/board/src/xt_camera.cpp", "source/board/src/xt_radar.cpp", "source/vizd/vizd5.c", "source/protocol/active_msgs.proto", "references/acceptance/" + kit.name, "references/zenohc/" + zenoh.name]
    missing = [name for name in required if name not in files]
    if missing:
        raise RuntimeError("required package inputs missing: " + ", ".join(missing))

    hashes = []
    total = 0
    with zipfile.ZipFile(ZIP, "w", allowZip64=True) as z:
        for name, path in sorted(files.items()):
            h = hashlib.sha256()
            with path.open("rb") as f:
                for chunk in iter(lambda: f.read(1024 * 1024), b""):
                    h.update(chunk)
            hashes.append(f"{h.hexdigest()}  {name}\n")
            size = path.stat().st_size
            total += size
            method = zipfile.ZIP_STORED if path.suffix.lower() in {".zip", ".pdf", ".jpg", ".png", ".xz", ".gz", ".bundle", ".deb"} else zipfile.ZIP_DEFLATED
            z.write(path, name, compress_type=method, compresslevel=3)
        verify = VERIFY.encode("utf-8")
        hashes.append(f"{hashlib.sha256(verify).hexdigest()}  verify_package.py\n")
        z.writestr("verify_package.py", verify)
        z.writestr("MANIFEST.sha256", "".join(hashes).encode("utf-8"))
    archive_hash = hashlib.sha256()
    with ZIP.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            archive_hash.update(chunk)
    meta = {"zip": ZIP.name, "files": len(files) + 2, "uncompressed_bytes": total, "zip_bytes": ZIP.stat().st_size, "sha256": archive_hash.hexdigest()}
    (OUT / f"{BASE}.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(meta, ensure_ascii=False))


if __name__ == "__main__":
    main()
