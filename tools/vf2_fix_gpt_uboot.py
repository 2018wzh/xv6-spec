#!/usr/bin/env python3
"""Repair the primary GPT header CRC after the xv6fs partition was added.

Reads LBA 1 and the entry array through U-Boot, recomputes both CRCs with
U-Boot's crc32 command, patches the header in RAM, and writes it back to the
SD card. Run with U-Boot idle at StarFive #.
"""

from __future__ import annotations

import re
import sys
import time

import serial

PROMPT = b"StarFive #"
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"


def run(port: serial.Serial, cmd: str, timeout: float = 30) -> str:
    port.write(cmd.encode() + b"\r")
    port.flush()
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
            if buf.endswith(PROMPT) or b"StarFive # " in buf:
                break
    return buf.decode(errors="replace")


def parse_crc(text: str) -> int:
    match = re.search(r"==>\s*([0-9a-fA-F]{8})", text)
    if not match:
        raise SystemExit(f"crc not found in: {text}")
    return int(match.group(1), 16)


def main() -> int:
    with serial.Serial(PORT, 115200, timeout=0.2, exclusive=True) as port:
        port.write(b"\r")
        port.flush()
        time.sleep(0.4)
        run(port, "mmc dev 1")
        run(port, "mmc read 0x50000000 1 1", 20)
        run(port, "mmc read 0x50004000 2 0x20", 20)
        entries_text = run(port, "crc32 0x50004000 0x4000", 20)
        entries_crc = parse_crc(entries_text)
        print(f"entries_crc={entries_crc:08x}", flush=True)
        run(port, f"mw.l 0x50000058 {entries_crc:#x} 1")
        run(port, "mw.l 0x50000010 0 1")
        header_text = run(port, "crc32 0x50000000 0x5c", 20)
        header_crc = parse_crc(header_text)
        print(f"header_crc={header_crc:08x}", flush=True)
        run(port, f"mw.l 0x50000010 {header_crc:#x} 1")
        run(port, "mmc write 0x50000000 1 1", 30)
        print("primary GPT header repaired", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
