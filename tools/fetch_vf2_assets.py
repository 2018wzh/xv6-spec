#!/usr/bin/env python3
"""Fetch the pinned VisionFive 2 DTB and verify it before use."""

from __future__ import annotations

import hashlib
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / "hardware" / "visionfive2" / "sources.lock"
OUTPUT = ROOT / "build" / "visionfive2.dtb"


def read_lock() -> dict[str, str]:
    values: dict[str, str] = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            values[key] = value
    return values


def main() -> None:
    locked = read_lock()
    url = locked["DTB_URL"]
    expected = locked["DTB_SHA256"]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists() and hashlib.sha256(OUTPUT.read_bytes()).hexdigest() == expected:
        return
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise SystemExit(f"DTB checksum mismatch: expected {expected}, got {actual}")
    OUTPUT.write_bytes(data)


if __name__ == "__main__":
    main()
