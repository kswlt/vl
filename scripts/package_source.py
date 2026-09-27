"""Create a source-only delivery archive from the current workspace."""

from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "RK3562_空间相机_源码_20260928.zip"
SOURCE_DIRS = (
    "board/src", "board/init", "board/web", "pc", "protocol", "vizd",
    "buzzer", "config", "scripts", "tests", "tools", "vendor",
)
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".proto", ".js",
    ".html", ".css", ".sh", ".ps1", ".bat", ".rs", ".cmake",
    ".in", ".m4", ".ac", ".am", ".pc", ".toml", ".yml", ".yaml",
    ".json", ".mk",
}
BUILD_NAMES = {"Makefile", "GNUmakefile", "BSDmakefile", "Dockerfile"}
ROOT_FILES = (
    "acceptance_tool.py", "setup_pc.ps1", "start_gateway.ps1",
    "start_acceptance.bat", "kill_and_restart.ps1", "open_dual_viewers.ps1",
    "requirements.txt", "requirements-dev.txt", "board/build.sh",
    "board/build_zenohc_gateway.sh", "board/deploy_release.sh",
)
EXCLUDED_PARTS = {
    "__pycache__", ".git", ".github", ".pytest_cache", "target", "build",
    "build-cross", "build-u20", "obj-b10", "lib-b10", "fuzz", "docs",
    "examples", "tests-output", "ci", "cmake-cache", "hls_test",
}


def is_source(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    if any(part in EXCLUDED_PARTS for part in relative.parts):
        return False
    if relative.as_posix() == "config/pc.rk3562.local.json":
        return False
    if relative.parts[:2] == ("board", "init"):
        return True
    if relative.parts[0] in {"vizd", "buzzer"} and path.name.startswith("S") and path.suffix == "":
        return True
    if relative.parts[0] == "tools" and path.suffix.lower() in {".json", ".xml"}:
        return False
    if path.name.startswith(("README", "CONTRIBUT", "ChangeList")):
        return False
    if path.name.startswith(("LICENSE", "COPYING", "NOTICE")):
        return True
    if path.name in {"CMakeLists.txt", "version.txt"}:
        return True
    return path.suffix.lower() in SOURCE_SUFFIXES or path.name in BUILD_NAMES


files = {ROOT / name for name in ROOT_FILES}
for name in SOURCE_DIRS:
    files.update(path for path in (ROOT / name).rglob("*") if path.is_file() and is_source(path))
files = sorted((path for path in files if path.is_file()), key=lambda p: p.as_posix())
with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in files:
        archive.write(path, path.relative_to(ROOT).as_posix())
with zipfile.ZipFile(OUTPUT) as archive:
    bad = archive.testzip()
    if bad:
        raise RuntimeError(f"Corrupt ZIP entry: {bad}")
    names = archive.namelist()
    print(f"{OUTPUT}\nfiles={len(names)} size={OUTPUT.stat().st_size}")
    print("top-level:", sorted({name.split("/")[0] for name in names}))
