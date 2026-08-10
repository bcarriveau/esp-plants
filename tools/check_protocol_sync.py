#!/usr/bin/env python3
from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]

files = [
    ROOT / "firmware" / "t5-hub" / "include" / "plant_protocol.h",
    ROOT / "firmware" / "xiao-soil-sensor" / "include" / "plant_protocol.h",
]

missing = [str(p) for p in files if not p.exists()]
if missing:
    print("ERROR: missing protocol file(s):")
    for item in missing:
        print("  ", item)
    sys.exit(2)

contents = [p.read_bytes() for p in files]
hashes = [hashlib.sha256(data).hexdigest() for data in contents]

for path, digest in zip(files, hashes):
    print(f"{digest}  {path.relative_to(ROOT)}")

if contents[0] != contents[1]:
    print("\nERROR: protocol headers differ.")
    print("Update both copies before building/releasing.")
    sys.exit(1)

print("\nOK: protocol headers are byte-identical.")
