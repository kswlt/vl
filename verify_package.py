"""Verify every package file against MANIFEST.sha256; no network calls."""
import hashlib
from pathlib import Path
root=Path(__file__).resolve().parent
errors=[];lines=(root/'MANIFEST.sha256').read_text(encoding='utf-8').splitlines()
for line in lines:
    expected,name=line.split('  ',1)
    path=(root/name).resolve()
    if not path.is_relative_to(root) or not path.is_file():errors.append('MISSING/INVALID '+name);continue
    if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:errors.append('MISMATCH '+name)
print(f'Checked {len(lines)} files; failures={len(errors)}')
for e in errors:print(e)
raise SystemExit(1 if errors else 0)
